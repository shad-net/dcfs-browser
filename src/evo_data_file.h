// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Header and chunk table of an Evo data file (gameNNN.dat, or the alpha's game.dat).
// Format from DriveClubFS by Nenkai (MIT): https://github.com/Nenkai/DriveClubFS

#pragma once

#include "evo_index.h"

#include <QDateTime>
#include <QString>

#include <optional>
#include <vector>

struct EvoDataFile {
    QString path;
    int dataIndex = 0;
    quint32 version = 0;
    QDateTime timeStamp; // UTC
    quint32 bufferSize = 0;
    qint64 dataStart = 0;              // file offset of chunk 0
    std::vector<quint64> chunkOffsets; // relative to dataStart, chunkCount + 1 values
    std::optional<EvoIndex> embedded;  // alpha only: the index stored in the same file

    int chunkCount() const {
        return int(chunkOffsets.size()) - 1;
    }
    qint64 storedChunkSize(int chunk) const {
        return qint64(chunkOffsets[size_t(chunk) + 1] - chunkOffsets[size_t(chunk)]);
    }
    // Bytes of file data this data file holds once unpacked (upper bound for the last chunk).
    quint64 unpackedSize() const {
        return quint64(chunkCount()) * bufferSize;
    }
    quint64 storedSize() const {
        return chunkOffsets.empty() ? 0 : chunkOffsets.back();
    }
};

std::optional<EvoDataFile> readEvoDataFile(const QString& path, int dataIndex, QString* error);
