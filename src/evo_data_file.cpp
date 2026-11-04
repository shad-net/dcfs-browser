// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "evo_data_file.h"

#include "binary_reader.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimeZone>
#include <QtEndian>

namespace {

constexpr quint32 MagicDatf = 0x46544144; // "DATF"
constexpr quint32 MagicChnk = 0x4B4E4843; // "CHNK"
constexpr quint32 MagicChnkSwapped = 0x43484E4B;
constexpr quint32 MagicData = 0x41544144; // "DATA"
constexpr qint64 MaxChunks = 64 * 1024 * 1024;

QString tr(const char* text) {
    return QCoreApplication::translate("EvoDataFile", text);
}

QDateTime fromFileTime(qint64 fileTime) {
    constexpr qint64 EpochDifference = 116444736000000000LL;
    return QDateTime::fromMSecsSinceEpoch((fileTime - EpochDifference) / 10000, QTimeZone::utc());
}

} // namespace

std::optional<EvoDataFile> readEvoDataFile(const QString& path, int dataIndex, QString* error) {
    auto fail = [error, &path](const QString& message) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(path, message);
        }
        return std::nullopt;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(file.errorString());
    }
    BinaryReader in(file);
    EvoDataFile data;
    data.path = path;
    data.dataIndex = dataIndex;

    if (in.read<quint32>() != MagicDatf || !in.ok()) {
        return fail(tr("not an Evo data file (DATF magic missing)."));
    }
    data.version = in.read<quint32>();
    if (in.ok() && data.version != EvoVersionRetail && data.version != EvoVersionAlpha) {
        return fail(tr("unsupported version %1 (expected 4300 or 3100).").arg(data.version));
    }
    data.timeStamp = fromFileTime(in.read<qint64>());
    const qint64 tocOffset = in.read<qint64>();

    qint64 chunkCount = 0;
    if (data.version > EvoVersionAlpha) {
        chunkCount = in.read<qint32>();
        data.bufferSize = in.read<quint32>();
    } else {
        // Alpha: the data follows this header and the index sits at tocOffset, then the chunks.
        data.dataStart = in.pos();
        if (!in.ok() || tocOffset <= 0 || tocOffset >= file.size() || !in.seek(tocOffset)) {
            return fail(tr("the index offset is out of range."));
        }
        QString indexError;
        data.embedded = readEvoIndex(file, &indexError);
        if (!data.embedded) {
            return fail(indexError);
        }
        data.bufferSize = data.embedded->readBufferSize;
    }

    const quint32 chunkMagic = in.read<quint32>();
    if (!in.ok() || (chunkMagic != MagicChnk && chunkMagic != MagicChnkSwapped)) {
        return fail(tr("data corrupted (CHNK magic missing)."));
    }
    if (data.version <= EvoVersionAlpha) {
        chunkCount = in.read<qint32>();
    }
    if (!in.ok() || chunkCount < 0 || chunkCount > MaxChunks) {
        return fail(tr("invalid chunk count %1.").arg(chunkCount));
    }
    if (data.bufferSize == 0 || data.bufferSize >= 0x40000) {
        return fail(tr("invalid chunk size 0x%1.").arg(data.bufferSize, 0, 16));
    }

    const QByteArray sizes = in.bytes(chunkCount * 4);
    if (data.version > EvoVersionAlpha) {
        if (in.read<quint32>() != MagicData || !in.ok()) {
            return fail(tr("data corrupted (DATA magic missing)."));
        }
        data.dataStart = in.pos();
    }
    if (!in.ok()) {
        return fail(tr("the chunk table is truncated."));
    }

    // Sizes to offsets; the extra last value is the end, so next - this = stored size.
    data.chunkOffsets.resize(size_t(chunkCount) + 1);
    const uchar* p = reinterpret_cast<const uchar*>(sizes.constData());
    quint64 offset = 0;
    for (qint64 i = 0; i < chunkCount; ++i) {
        data.chunkOffsets[size_t(i)] = offset;
        offset += qFromLittleEndian<quint32>(p + i * 4);
    }
    data.chunkOffsets.back() = offset;
    return data;
}
