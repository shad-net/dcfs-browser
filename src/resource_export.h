// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// An opened resource pack together with its top mips packs, and exporting its resources:
// textures as DDS (and optionally PNG), BIN and XML as they are, the rest as raw data.

#pragma once

#include "evo_file_system.h"
#include "resource_pack.h"
#include "texture.h"

#include <QSet>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

struct ResourceExportOptions {
    bool dds = true;           // write textures as DDS
    bool png = false;          // write textures as PNG
    bool other = false;        // write resource types we cannot convert as raw data (.TYPE.bin)
    bool texturesOnly = false; // skip everything that is not a texture (XML and BIN included)
};

class PackContext {
public:
    // Opens another pack this one refers to (its top mips packs), by the same kind of path as
    // `path`; returns nullptr when that file does not exist or is not a pack.
    using Loader = std::function<std::shared_ptr<ResourcePack>(const QString& path)>;

    PackContext(std::shared_ptr<ResourcePack> pack, QString path, Loader loader);

    const ResourcePack& pack() const {
        return *pack_;
    }
    const QString& path() const {
        return path_;
    }

    // The texture of a PIXEL_BUFFER resource, untiled, with its top mips fetched if needed.
    std::optional<Texture> texture(const ResourceInfo& info, QString* error);
    // Names of the top mips packs found so far (loads them on first use).
    QStringList topMipsPacks();

    // Writes one resource below `folder`. `used` keeps file names unique across one export.
    // Returns false with *error set on failure; *skipped is set for resources not written
    // because of their type (or, for BIN, because they are empty).
    bool exportResource(const ResourceInfo& info, const QString& folder,
                        const ResourceExportOptions& options, QSet<QString>* used, bool* skipped,
                        QString* error);

private:
    void loadTopMips();

    std::shared_ptr<ResourcePack> pack_;
    QString path_;
    Loader loader_;
    bool topMipsLoaded_ = false;
    std::vector<std::pair<QString, std::shared_ptr<ResourcePack>>> topMips_;
};

// Opens packs stored in one archive; their top mips packs are looked up in the same archive.
// Packs opened through one source share a reader and a small cache of top mips packs (many packs
// share one *_common_topmips.rpk). Not thread safe: one source per thread.
class ArchivePackSource {
public:
    explicit ArchivePackSource(std::shared_ptr<const EvoFileSystem> fs, int cacheSize = 6);
    std::unique_ptr<PackContext> open(int entryIndex, QString* error);

private:
    struct State;
    std::shared_ptr<State> state_;
};

// The .rpk at entries[entryIndex] of `fs` (a one-off ArchivePackSource).
std::unique_ptr<PackContext> openPackFromArchive(std::shared_ptr<const EvoFileSystem> fs,
                                                 int entryIndex, QString* error);
// True for names that hold only top mips (x_top_mips.rpk, x_topmips.rpk, x_common_topmips.rpk).
bool isTopMipsPackName(const QString& name);
// Opens a .rpk file on disk; top mips packs are looked up next to it.
std::unique_ptr<PackContext> openPackFromDisk(const QString& path, QString* error);
