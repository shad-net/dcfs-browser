// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Writes the names of an archive's files to a text or CSV file.

#pragma once

#include "evo_file_system.h"

#include <QString>

#include <optional>
#include <vector>

enum class FileListFormat {
    Names,    // one path per line
    Detailed, // DriveClubFS list-files style: "path (size bytes, Dat Index: n, Offset: 0x...)"
    Csv,      // path,size,data_file,offset,name_hash,md5
};

// "names", "detailed", "csv" (case-insensitive).
std::optional<FileListFormat> fileListFormatFromName(const QString& name);
QString fileListSuffix(FileListFormat format);

// Writes `entries` (indices into fs.index().entries, any order) sorted by path. The file is only
// replaced once everything was written.
bool writeFileList(const EvoFileSystem& fs, std::vector<int> entries, const QString& path,
                   FileListFormat format, QString* error);

// Every entry, or only those stored in one data file (dataIndex >= 0).
std::vector<int> entriesInDataFile(const EvoFileSystem& fs, int dataIndex);

// One list per data file in `folder`, named after it (game000.dat -> game000.txt). Data files
// that are missing still get a list, since the names come from the index. Returns the number
// of lists written, or -1 on error.
int writeFileListPerDataFile(const EvoFileSystem& fs, const QString& folder, FileListFormat format,
                             QString* error);
