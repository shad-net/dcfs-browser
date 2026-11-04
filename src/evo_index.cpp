// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "evo_index.h"

#include "binary_reader.h"

#include <QCoreApplication>
#include <QTimeZone>

#include <algorithm>
#include <array>

namespace {

constexpr quint32 MagicDatn = 0x4E544144; // "DATN"
constexpr quint32 MagicDatx = 0x58544144; // "DATX"
constexpr quint32 Marker = 0x12345678;
constexpr int MaxEntries = 1000000;
constexpr int MaxDictSize = 0x80;
constexpr int MaxNameLength = 0x100;

QString tr(const char* text) {
    return QCoreApplication::translate("EvoIndex", text);
}

// Windows FILETIME (100 ns since 1601-01-01) to UTC.
QDateTime fromFileTime(qint64 fileTime) {
    constexpr qint64 EpochDifference = 116444736000000000LL;
    return QDateTime::fromMSecsSinceEpoch((fileTime - EpochDifference) / 10000, QTimeZone::utc());
}

// The file names are packed with a small pair dictionary: a byte below 0x80 is a character,
// a byte with the high bit set expands to two dictionary bytes (which may expand again).
// This is a straight port of EvoIndexFile.ExtractString, with bounds checks added.
bool unpackName(const QByteArray& dict, const uchar*& cursor, const uchar* end, int dictSize,
                QByteArray* out) {
    std::array<uchar, 0x88> stack{};
    int depth = 0;
    uchar data = 0xFF;
    out->clear();

    while (data != 0 || out->size() < dictSize - 1) {
        while (true) {
            if (depth > 0) {
                --depth;
                data = stack[depth];
            } else {
                if (cursor >= end) {
                    return false;
                }
                data = *cursor++;
            }
            if ((data & 0x80) == 0) {
                break;
            }
            if (depth + 1 >= int(stack.size())) {
                return false;
            }
            stack[depth] = uchar(dict.at(data));
            stack[depth + 1] = uchar(dict.at(uchar(data - 0x80)));
            depth += 2;
        }
        if (data == 0) {
            return true;
        }
        if (out->size() >= MaxNameLength) {
            return false;
        }
        out->append(char(data));
    }
    return true;
}

} // namespace

QString compressionName(EvoCompression compression) {
    switch (compression) {
    case EvoCompression::NoPack:
        return QStringLiteral("None");
    case EvoCompression::RaPack:
        return QStringLiteral("RaPack");
    case EvoCompression::Zlib:
        return QStringLiteral("zlib");
    case EvoCompression::LZRP3:
        return QStringLiteral("LZRP3");
    case EvoCompression::LZMA:
        return QStringLiteral("LZMA");
    case EvoCompression::LZlib:
        return QStringLiteral("LZlib");
    case EvoCompression::LZ4:
        return QStringLiteral("LZ4");
    case EvoCompression::LZ4HC:
        return QStringLiteral("LZ4HC");
    }
    return QStringLiteral("Unknown (%1)").arg(int(compression));
}

quint32 EvoIndex::hashName(const QString& name) const {
    // The original works in wrapping 32-bit unsigned arithmetic; keep it that way.
    quint32 a = hashA;
    quint32 result = 0;
    for (QChar c : name) {
        c = c.toUpper();
        if (c == QLatin1Char('\\')) {
            c = QLatin1Char('/');
        }
        a = (a * hashB) % 0x7FFFFFFEu;
        result = (result * a + c.unicode()) % 0x7FFFFFFFu;
    }
    return result;
}

int EvoIndex::find(const QString& name) const {
    const QString key = QString(name).replace(QLatin1Char('\\'), QLatin1Char('/')).toLower();
    return byName.value(key, -1);
}

std::optional<EvoIndex> readEvoIndex(QIODevice& device, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };

    BinaryReader in(device);
    EvoIndex index;

    const quint32 magic = in.read<quint32>();
    if (in.ok() && magic != MagicDatn && magic != MagicDatx) {
        return fail(tr("Not an Evo index (magic is not DATN or DATX)."));
    }
    index.version = in.read<quint32>();
    if (in.ok() && index.version != EvoVersionRetail && index.version != EvoVersionAlpha) {
        return fail(tr("Unsupported index version %1 (expected 4300 or 3100).").arg(index.version));
    }
    index.timeStamp = fromFileTime(in.read<qint64>());
    index.totalDataSize = in.read<quint64>();
    index.hashA = in.read<quint32>();
    index.hashB = in.read<quint32>();
    index.compression = EvoCompression(in.read<qint32>());
    index.readBufferSize = in.read<quint32>();
    if (index.version > EvoVersionAlpha) {
        in.read<quint32>();
        in.read<quint32>();
        index.dataFileCount = in.read<qint32>();
        in.read<quint32>();
    }
    if (!in.ok()) {
        return fail(tr("The index header is truncated."));
    }

    // The game itself rejects these.
    if (int(index.compression) < 0 || int(index.compression) > 7) {
        return fail(tr("Invalid compression format %1.").arg(int(index.compression)));
    }
    if (index.readBufferSize >= 0x40000) {
        return fail(tr("Read buffer size 0x%1 is too large.").arg(index.readBufferSize, 0, 16));
    }
    if (index.dataFileCount < 0 || index.dataFileCount > 0xFFFF) {
        return fail(tr("Invalid data file count %1.").arg(index.dataFileCount));
    }

    const qint32 entryCount = in.read<qint32>();
    const int dictSize = in.read<quint8>();
    if (!in.ok()) {
        return fail(tr("The index header is truncated."));
    }
    if (entryCount < 0 || entryCount >= MaxEntries) {
        return fail(tr("Invalid entry count %1.").arg(entryCount));
    }
    if (dictSize > MaxDictSize) {
        return fail(tr("Name dictionary size 0x%1 is too large.").arg(dictSize, 0, 16));
    }

    // Two halves of dictSize bytes; padded to 256 so any byte with the high bit indexes safely.
    QByteArray dict = in.bytes(qint64(dictSize) * 2);
    dict.resize(0x100, '\0');
    const qint32 packedNamesSize = in.read<qint32>();
    if (!in.ok() || packedNamesSize < 0 || packedNamesSize > device.size()) {
        return fail(tr("The packed file names are truncated."));
    }
    const QByteArray packedNames = in.bytes(packedNamesSize);
    if (in.read<quint32>() != Marker || !in.ok()) {
        return fail(tr("Data corrupted: marker after the file names is missing."));
    }

    // 1.00 (2014-08-19) stores the data file index in 8 bits, later builds in 16, without
    // changing the version number; the timestamp is the only way to tell them apart.
    const bool wideDatIndex =
        index.timeStamp > QDateTime(QDate(2014, 8, 20), QTime(0, 0), QTimeZone::utc());
    const bool retail = index.version > EvoVersionAlpha;
    const int entrySize = retail ? 32 : 16;
    const QByteArray table = in.bytes(qint64(entryCount) * entrySize);
    if (!in.ok()) {
        return fail(tr("The file table is truncated."));
    }

    index.entries.resize(size_t(entryCount));
    const uchar* p = reinterpret_cast<const uchar*>(table.constData());
    for (EvoIndexEntry& entry : index.entries) {
        const quint64 location = qFromLittleEndian<quint64>(p);
        if (!retail) {
            entry.datIndex = 0;
            entry.offset = location;
        } else if (wideDatIndex) {
            entry.datIndex = quint16(location & 0xFFFF);
            entry.offset = location >> 16;
        } else {
            entry.datIndex = quint16(location & 0xFF);
            entry.offset = location >> 8;
        }
        entry.size = qFromLittleEndian<qint32>(p + 8);
        entry.nameHash = qFromLittleEndian<quint32>(p + 12);
        if (retail) {
            entry.md5 = QByteArray(reinterpret_cast<const char*>(p + 16), 16);
        }
        p += entrySize;
    }

    if (retail) {
        // 1.00 stores its own offset here, 1.28 the usual marker.
        const quint32 position = quint32(in.pos());
        const quint32 check = in.read<quint32>();
        if (!in.ok() || (check != position && check != Marker)) {
            return fail(tr("Data corrupted: check marker after the file table did not match."));
        }
    }

    const uchar* cursor = reinterpret_cast<const uchar*>(packedNames.constData());
    const uchar* end = cursor + packedNames.size();
    QByteArray name;
    for (size_t i = 0; i < index.entries.size(); ++i) {
        if (!unpackName(dict, cursor, end, dictSize, &name)) {
            return fail(tr("Could not unpack the name of entry %1.").arg(i));
        }
        index.entries[i].name =
            QString::fromUtf8(name).replace(QLatin1Char('\\'), QLatin1Char('/'));
    }
    index.byName.reserve(qsizetype(index.entries.size()));
    for (size_t i = 0; i < index.entries.size(); ++i) {
        index.byName.insert(index.entries[i].name.toLower(), int(i));
    }
    return index;
}
