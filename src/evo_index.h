// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Evolution Studios file system index (game.ndx, or the TOC inside an alpha game.dat).
// Format from DriveClubFS by Nenkai (MIT): https://github.com/Nenkai/DriveClubFS

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>

#include <optional>
#include <vector>

class QIODevice;

// Versions seen so far: 4300 (Driveclub 1.00 - 1.28) and 3100 (alpha, index inside game.dat).
constexpr quint32 EvoVersionRetail = 4300;
constexpr quint32 EvoVersionAlpha = 3100;

enum class EvoCompression : int {
    NoPack = 0,
    RaPack = 1,
    Zlib = 2,
    LZRP3 = 3,
    LZMA = 4,
    LZlib = 5,
    LZ4 = 6,
    LZ4HC = 7,
};

QString compressionName(EvoCompression compression);

struct EvoIndexEntry {
    QString name;         // e.g. "data/cars/xyz.rpk", '/' separated
    quint16 datIndex = 0; // gameNNN.dat; always 0 for the alpha
    quint64 offset = 0;   // in the data file's uncompressed stream
    qint32 size = 0;      // uncompressed size
    quint32 nameHash = 0; // entries are sorted by this
    QByteArray md5;       // 16 bytes, empty for the alpha
};

struct EvoIndex {
    quint32 version = 0;
    QDateTime timeStamp; // UTC
    quint64 totalDataSize = 0;
    quint32 hashA = 0;
    quint32 hashB = 0;
    EvoCompression compression = EvoCompression::NoPack;
    quint32 readBufferSize = 0;
    int dataFileCount = 0; // 0 for the alpha (single game.dat)
    std::vector<EvoIndexEntry> entries;
    // Lower-case path -> entry, built on load for find().
    QHash<QString, int> byName;

    bool hasChecksums() const {
        return version > EvoVersionAlpha;
    }
    // Hash the game uses to find a path (case-insensitive, '\' treated as '/').
    quint32 hashName(const QString& name) const;
    // Index of the entry with this path (case-insensitive), or -1.
    int find(const QString& name) const;
};

// Reads an index from the device's current position. On failure returns nothing and sets *error.
std::optional<EvoIndex> readEvoIndex(QIODevice& device, QString* error);
