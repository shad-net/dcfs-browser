// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reads file contents out of an EvoFileSystem: finds the chunks an entry spans, unpacks them
// (zlib, LZ4, LZ4HC or stored) and copies the entry's bytes out.

#pragma once

#include "evo_file_system.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>
#include <map>
#include <memory>

class QFile;
class QIODevice;

// Not thread safe: use one reader per thread. The file system itself may be shared.
class EvoReader {
public:
    explicit EvoReader(std::shared_ptr<const EvoFileSystem> fs);
    ~EvoReader();

    // Reads up to `length` bytes starting at `pos` within the entry.
    bool read(const EvoIndexEntry& entry, qint64 pos, qint64 length, QByteArray* out,
              QString* error);

    // Writes the whole entry to `out`. With `verify`, checks the MD5 stored in the index (when
    // the index has one). `progress` gets the bytes written so far for this entry.
    bool extract(const EvoIndexEntry& entry, QIODevice& out, bool verify, QString* error,
                 const std::atomic_bool* cancel = nullptr,
                 const std::function<void(qint64)>& progress = {});

    // Extracts to `outputDir`/entry.name, creating folders. Refuses names that would escape it.
    bool extractTo(const EvoIndexEntry& entry, const QString& outputDir, bool verify,
                   QString* error, const std::atomic_bool* cancel = nullptr,
                   const std::function<void(qint64)>& progress = {});

private:
    using Sink = std::function<bool(const char*, qint64)>;
    bool forEachPiece(const EvoIndexEntry& entry, qint64 pos, qint64 length, const Sink& sink,
                      QString* error, const std::atomic_bool* cancel,
                      const std::function<void(qint64)>& progress);
    bool loadChunk(const EvoDataFile& data, int chunk, QString* error);
    QFile* openData(const EvoDataFile& data, QString* error);

    std::shared_ptr<const EvoFileSystem> fs_;
    std::map<int, std::unique_ptr<QFile>> files_;
    int chunkData_ = -1;
    int chunkIndex_ = -1;
    QByteArray packed_;
    QByteArray chunk_;
};
