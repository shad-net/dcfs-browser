// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Exports the textures (or all resources) of many resource packs from an archive in one go.

#pragma once

#include "evo_file_system.h"
#include "resource_export.h"

#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

struct TextureBatchStats {
    int packs = 0;      // packs opened
    int textures = 0;   // textures written
    int resources = 0;  // other resources written
    int noTextures = 0; // packs without any texture
};
Q_DECLARE_METATYPE(TextureBatchStats)

class TextureBatch {
public:
    TextureBatch(std::shared_ptr<const EvoFileSystem> fs, QString outputDir,
                 ResourceExportOptions options);
    static std::vector<int> packsIn(const EvoFileSystem& fs, const std::vector<int>& entries);
    void exportPack(int entryIndex, QStringList* errors);

    const TextureBatchStats& stats() const {
        return stats_;
    }

private:
    std::shared_ptr<const EvoFileSystem> fs_;
    ArchivePackSource source_;
    QString outputDir_;
    ResourceExportOptions options_;
    QHash<QString, QSet<QString>> used_; // per output folder
    TextureBatchStats stats_;
};
