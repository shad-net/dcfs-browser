// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "evo_reader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>

#include <lz4.h>
#include <zlib.h>

#include <algorithm>

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("EvoReader", text);
}

bool unpackZlib(const QByteArray& in, QByteArray* out, int capacity, QString* error) {
    out->resize(capacity);
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        *error = tr("zlib init failed.");
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
    stream.avail_in = uInt(in.size());
    stream.next_out = reinterpret_cast<Bytef*>(out->data());
    stream.avail_out = uInt(capacity);
    const int status = inflate(&stream, Z_FINISH);
    const qint64 produced = qint64(stream.total_out);
    inflateEnd(&stream);
    // Z_BUF_ERROR with a full buffer just means the chunk was exactly `capacity` bytes.
    if (status != Z_STREAM_END && !(status == Z_BUF_ERROR && produced == capacity)) {
        *error = tr("zlib error %1.").arg(status);
        return false;
    }
    out->resize(produced);
    return true;
}

// [u32 unpacked size][LZ4 block]. LZ4HC produces the same block format.
bool unpackLz4(const QByteArray& in, QByteArray* out, int capacity, QString* error) {
    if (in.size() < 4) {
        *error = tr("LZ4 chunk too short.");
        return false;
    }
    const qint32 expected = qFromLittleEndian<qint32>(in.constData());
    if (expected <= 0 || expected > capacity) {
        *error = tr("LZ4 chunk claims %1 bytes (chunk size is %2).").arg(expected).arg(capacity);
        return false;
    }
    out->resize(capacity);
    const int produced =
        LZ4_decompress_safe(in.constData() + 4, out->data(), int(in.size() - 4), capacity);
    if (produced != expected) {
        *error = tr("LZ4 data is corrupt.");
        return false;
    }
    out->resize(produced);
    return true;
}

// The entry name as a relative path we are willing to write, or empty if it is not one.
QString safeRelativePath(const QString& name) {
    QString path = QDir::cleanPath(QString(name).replace(QLatin1Char('\\'), QLatin1Char('/')));
    while (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }
    if (path.isEmpty() || path == QLatin1String("..") || path.startsWith(QLatin1String("../")) ||
        path.contains(QLatin1Char(':'))) {
        return {};
    }
    return path;
}

} // namespace

EvoReader::EvoReader(std::shared_ptr<const EvoFileSystem> fs) : fs_(std::move(fs)) {}

EvoReader::~EvoReader() = default;

QFile* EvoReader::openData(const EvoDataFile& data, QString* error) {
    auto& slot = files_[data.dataIndex];
    if (!slot) {
        auto file = std::make_unique<QFile>(data.path);
        if (!file->open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(data.path),
                                                  file->errorString());
            files_.erase(data.dataIndex);
            return nullptr;
        }
        slot = std::move(file);
    }
    return slot.get();
}

bool EvoReader::loadChunk(const EvoDataFile& data, int chunk, QString* error) {
    if (chunkData_ == data.dataIndex && chunkIndex_ == chunk) {
        return true;
    }
    chunkData_ = -1;
    chunkIndex_ = -1;
    if (chunk < 0 || chunk >= data.chunkCount()) {
        *error =
            tr("chunk %1 is past the end of %2.").arg(chunk).arg(QFileInfo(data.path).fileName());
        return false;
    }
    QFile* file = openData(data, error);
    if (!file) {
        return false;
    }
    const qint64 stored = data.storedChunkSize(chunk);
    const qint64 at = data.dataStart + qint64(data.chunkOffsets[size_t(chunk)]);
    if (stored < 0 || stored > qint64(data.bufferSize) * 2 || !file->seek(at)) {
        *error =
            tr("chunk %1 of %2 is out of range.").arg(chunk).arg(QFileInfo(data.path).fileName());
        return false;
    }
    packed_ = file->read(stored);
    if (packed_.size() != stored) {
        *error = tr("%1 is truncated (chunk %2).").arg(QFileInfo(data.path).fileName()).arg(chunk);
        return false;
    }

    const int capacity = int(data.bufferSize);
    const EvoCompression compression = fs_->index().compression;
    bool ok = true;
    bool unsupported = false;
    QString unpackError;
    if (compression == EvoCompression::NoPack || stored == capacity) {
        chunk_ = packed_;
    } else if (!packed_.isEmpty() && uchar(packed_.at(0)) == 0x55) {
        unpackError = tr("chunk uses the 0x55 packing variant, which is not supported.");
        ok = false;
        unsupported = true;
    } else {
        switch (compression) {
        case EvoCompression::Zlib:
            ok = unpackZlib(packed_, &chunk_, capacity, &unpackError);
            break;
        case EvoCompression::LZ4:
        case EvoCompression::LZ4HC:
            ok = unpackLz4(packed_, &chunk_, capacity, &unpackError);
            break;
        default:
            unpackError = tr("%1 compression is not supported.").arg(compressionName(compression));
            ok = false;
            unsupported = true;
            break;
        }
    }
    if (!ok) {
        // A short final chunk may simply be stored; if that guess is wrong the MD5 check says so.
        if (!unsupported && chunk == data.chunkCount() - 1 && stored < capacity) {
            chunk_ = packed_;
        } else {
            *error = tr("%1, chunk %2: %3")
                         .arg(QFileInfo(data.path).fileName())
                         .arg(chunk)
                         .arg(unpackError);
            return false;
        }
    }
    chunkData_ = data.dataIndex;
    chunkIndex_ = chunk;
    return true;
}

bool EvoReader::forEachPiece(const EvoIndexEntry& entry, qint64 pos, qint64 length,
                             const Sink& sink, QString* error, const std::atomic_bool* cancel,
                             const std::function<void(qint64)>& progress) {
    const EvoDataFile* data = fs_->dataFile(entry.datIndex);
    if (!data) {
        *error =
            tr("%1 is %2.")
                .arg(fs_->dataFileName(entry.datIndex), fs_->dataFileError(entry.datIndex).isEmpty()
                                                            ? tr("not available")
                                                            : fs_->dataFileError(entry.datIndex));
        return false;
    }
    if (entry.size < 0 || pos < 0 || pos > entry.size) {
        *error = tr("invalid read range.");
        return false;
    }
    length = std::min(length, qint64(entry.size) - pos);
    const qint64 chunkSize = data->bufferSize;
    qint64 absolute = qint64(entry.offset) + pos;
    qint64 done = 0;
    while (done < length) {
        if (cancel && cancel->load()) {
            *error = tr("cancelled.");
            return false;
        }
        const int chunk = int(absolute / chunkSize);
        const qint64 inChunk = absolute % chunkSize;
        if (!loadChunk(*data, chunk, error)) {
            return false;
        }
        const qint64 available = qint64(chunk_.size()) - inChunk;
        if (available <= 0) {
            *error = tr("%1 ends before the file does (chunk %2).")
                         .arg(fs_->dataFileName(entry.datIndex))
                         .arg(chunk);
            return false;
        }
        const qint64 take = std::min(available, length - done);
        if (!sink(chunk_.constData() + inChunk, take)) {
            *error = tr("write failed.");
            return false;
        }
        done += take;
        absolute += take;
        if (progress) {
            progress(done);
        }
    }
    return true;
}

bool EvoReader::read(const EvoIndexEntry& entry, qint64 pos, qint64 length, QByteArray* out,
                     QString* error) {
    out->clear();
    out->reserve(std::max<qint64>(0, std::min<qint64>(length, entry.size)));
    return forEachPiece(entry, pos, length,
                        [out](const char* bytes, qint64 count) {
                            out->append(bytes, count);
                            return true;
                        },
                        error, nullptr, {});
}

bool EvoReader::extract(const EvoIndexEntry& entry, QIODevice& out, bool verify, QString* error,
                        const std::atomic_bool* cancel,
                        const std::function<void(qint64)>& progress) {
    verify = verify && fs_->index().hasChecksums() && entry.md5.size() == 16;
    QCryptographicHash md5(QCryptographicHash::Md5);
    const bool ok = forEachPiece(
        entry, 0, entry.size,
        [&](const char* bytes, qint64 count) {
            if (verify) {
                md5.addData(QByteArrayView(bytes, count));
            }
            return out.write(bytes, count) == count;
        },
        error, cancel, progress);
    if (!ok) {
        return false;
    }
    if (verify && md5.result() != entry.md5) {
        *error = tr("MD5 mismatch (expected %1, got %2).")
                     .arg(QString::fromLatin1(entry.md5.toHex()),
                          QString::fromLatin1(md5.result().toHex()));
        return false;
    }
    return true;
}

bool EvoReader::extractTo(const EvoIndexEntry& entry, const QString& outputDir, bool verify,
                          QString* error, const std::atomic_bool* cancel,
                          const std::function<void(qint64)>& progress) {
    const QString relative = safeRelativePath(entry.name);
    if (relative.isEmpty()) {
        *error = tr("refusing to write unsafe path \"%1\".").arg(entry.name);
        return false;
    }
    const QString target = QDir(outputDir).filePath(relative);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        *error = tr("cannot create folder %1.")
                     .arg(QDir::toNativeSeparators(QFileInfo(target).absolutePath()));
        return false;
    }
    // QSaveFile only replaces the target once everything (including the MD5) checked out.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(target), file.errorString());
        return false;
    }
    if (!extract(entry, file, verify, error, cancel, progress)) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(target), file.errorString());
        return false;
    }
    return true;
}
