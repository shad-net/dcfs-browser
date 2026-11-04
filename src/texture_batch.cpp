// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "texture_batch.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

TextureBatch::TextureBatch(std::shared_ptr<const EvoFileSystem> fs, QString outputDir,
                           ResourceExportOptions options)
    : fs_(fs), source_(std::move(fs)), outputDir_(std::move(outputDir)), options_(options) {}

std::vector<int> TextureBatch::packsIn(const EvoFileSystem& fs, const std::vector<int>& entries) {
    const auto& all = fs.index().entries;
    std::vector<int> out;
    for (int i : entries) {
        const QString& name = all[size_t(i)].name;
        if (name.endsWith(QLatin1String(".rpk"), Qt::CaseInsensitive) && !isTopMipsPackName(name)) {
            out.push_back(i);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    // Archive order: each data file read front to back.
    std::sort(out.begin(), out.end(), [&all](int a, int b) {
        const EvoIndexEntry& x = all[size_t(a)];
        const EvoIndexEntry& y = all[size_t(b)];
        return x.datIndex != y.datIndex ? x.datIndex < y.datIndex : x.offset < y.offset;
    });
    return out;
}

void TextureBatch::exportPack(int entryIndex, QStringList* errors) {
    const QString& name = fs_->index().entries[size_t(entryIndex)].name;
    QString error;
    std::unique_ptr<PackContext> context = source_.open(entryIndex, &error);
    if (!context) {
        *errors << QStringLiteral("%1: %2").arg(name, error);
        return;
    }
    ++stats_.packs;

    const auto& resources = context->pack().resources();
    const auto isTexture = [](const ResourceInfo& info) {
        return info.type() == quint16(ResourceType::PixelBuffer);
    };
    const int textures = int(std::count_if(resources.begin(), resources.end(), isTexture));
    if (textures == 0 && options_.texturesOnly) {
        ++stats_.noTextures;
        return;
    }

    // "a/b/x.tif_pixel_buffer.rpk": folder "a/b", pack stem "x.tif".
    const QFileInfo info(name);
    const QString dir = name.left(name.size() - info.fileName().size());
    QString stem = info.completeBaseName();
    if (stem.endsWith(QLatin1String("_pixel_buffer"), Qt::CaseInsensitive)) {
        stem.chop(int(qstrlen("_pixel_buffer")));
    }
    // "x.tif" as a folder name reads like a file: drop the source image extension.
    static const QStringList imageSuffixes = {QStringLiteral("tif"), QStringLiteral("tiff"),
                                              QStringLiteral("tga"), QStringLiteral("png"),
                                              QStringLiteral("dds"), QStringLiteral("psd"),
                                              QStringLiteral("bmp"), QStringLiteral("jpg")};
    if (imageSuffixes.contains(QFileInfo(stem).suffix().toLower())) {
        stem = QFileInfo(stem).completeBaseName();
    }
    const bool alone = options_.texturesOnly && textures == 1;
    const QString relative = alone ? dir : dir + stem;
    const QString folder = QDir(outputDir_).filePath(relative);
    QSet<QString>& used = used_[relative.toLower()];

    for (const ResourceInfo& resource : resources) {
        if (options_.texturesOnly && !isTexture(resource)) {
            continue;
        }
        bool skipped = false;
        if (!context->exportResource(resource, folder, options_, &used, &skipped, &error)) {
            *errors << QStringLiteral("%1: %2: %3").arg(name, resource.displayName(), error);
        } else if (!skipped) {
            ++(isTexture(resource) ? stats_.textures : stats_.resources);
        }
    }
}
