// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// An opened Evo file system: the index plus the headers of every data file it refers to.
// Immutable once opened, so it can be shared between the UI and extraction threads.

#pragma once

#include "evo_data_file.h"
#include "evo_index.h"

#include <QString>

#include <map>
#include <vector>

class EvoFileSystem {
public:
    bool open(const QString& path, QString* error);

    const EvoIndex& index() const {
        return index_;
    }
    const QString& directory() const {
        return directory_;
    }
    const QString& indexPath() const {
        return indexPath_;
    }
    bool isAlpha() const {
        return index_.version <= EvoVersionAlpha;
    }
    int focusDataIndex() const {
        return focusDataIndex_;
    }

    std::vector<int> dataIndices() const;
    QString dataFileName(int dataIndex) const;
    const EvoDataFile* dataFile(int dataIndex) const;
    QString dataFileError(int dataIndex) const;
    int fileCount(int dataIndex) const;

private:
    void loadDataFiles();

    EvoIndex index_;
    QString directory_;
    QString indexPath_;
    int focusDataIndex_ = -1;
    std::map<int, EvoDataFile> dataFiles_;
    std::map<int, QString> dataErrors_;
    std::map<int, int> fileCounts_;
};
