// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "resource_export.h"

#include "evo_reader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("ResourceExport", text);
}

// A resource name as a relative path we are willing to write.
QString safeName(QString name) {
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    name.replace(QLatin1Char(':'), QLatin1Char('_'));
    QStringList parts;
    for (const QString& part : name.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part != QLatin1String(".") && part != QLatin1String("..")) {
            parts << part;
        }
    }
    return parts.join(QLatin1Char('/'));
}

// "a/b.dds" -> "a/b_<uid>.dds" when the name was already used in this export.
QString unique(const QString& name, quint64 uid, QSet<QString>* used) {
    QString out = name;
    if (used->contains(out.toLower())) {
        const QFileInfo info(name);
        const QString dir = name.left(name.size() - info.fileName().size());
        out = dir + info.completeBaseName() +
              QStringLiteral("_%1").arg(uid, 16, 16, QLatin1Char('0')).toUpper() +
              (info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix());
    }
    used->insert(out.toLower());
    return out;
}

bool writeFile(const QString& path, const QByteArray& data, QString* error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = tr("cannot create folder %1.")
                     .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

} // namespace

PackContext::PackContext(std::shared_ptr<ResourcePack> pack, QString path, Loader loader)
    : pack_(std::move(pack)), path_(std::move(path)), loader_(std::move(loader)) {}

void PackContext::loadTopMips() {
    if (topMipsLoaded_) {
        return;
    }
    topMipsLoaded_ = true;
    if (!loader_) {
        return;
    }
    for (const QString& candidate : topMipsPackCandidates(path_)) {
        if (candidate.compare(path_, Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (auto pack = loader_(candidate)) {
            topMips_.emplace_back(candidate, std::move(pack));
        }
    }
}

QStringList PackContext::topMipsPacks() {
    loadTopMips();
    QStringList out;
    for (const auto& [name, pack] : topMips_) {
        out << name;
    }
    return out;
}

std::optional<Texture> PackContext::texture(const ResourceInfo& info, QString* error) {
    const auto pb = pack_->readPixelBuffer(info, error);
    if (!pb) {
        return std::nullopt;
    }
    if (pb->isPs4() && pb->startMip != 0) {
        loadTopMips();
        std::optional<QByteArray> data;
        for (const auto& [name, pack] : topMips_) {
            data = pack->readTopMips(pb->topMipsUid, error);
            if (data) {
                break;
            }
        }
        if (!data) {
            data = pack_->readTopMips(pb->topMipsUid, error);
        }
        if (!data) {
            *error = tr("Its full-size mips are in a top mips pack (%1) that was not found.")
                         .arg(QFileInfo(topMipsPackCandidates(path_).value(1)).fileName());
            return std::nullopt;
        }
        return untileTexture(*pb, *data, error);
    }
    return untileTexture(*pb, pb->data, error);
}

bool PackContext::exportResource(const ResourceInfo& info, const QString& folder,
                                 const ResourceExportOptions& options, QSet<QString>* used,
                                 bool* skipped, QString* error) {
    *skipped = false;
    QString name = safeName(info.displayName());
    if (name.isEmpty()) {
        name = QStringLiteral("%1").arg(info.uid, 16, 16, QLatin1Char('0')).toUpper();
    }
    const QDir out(folder);

    switch (ResourceType(info.type())) {
    case ResourceType::PixelBuffer: {
        const auto texture = this->texture(info, error);
        if (!texture) {
            return false;
        }
        const QFileInfo base(name);
        const QString stem =
            name.left(name.size() - base.fileName().size()) + base.completeBaseName();
        const QString dds = unique(stem + QStringLiteral(".dds"), info.uid, used);
        if (options.dds && !writeFile(out.filePath(dds), ddsFile(*texture), error)) {
            return false;
        }
        if (options.png) {
            const QString png = out.filePath(dds.left(dds.size() - 4) + QStringLiteral(".png"));
            QDir().mkpath(QFileInfo(png).absolutePath());
            if (!textureImage(*texture).save(png, "PNG")) {
                *error = tr("cannot write %1.").arg(QDir::toNativeSeparators(png));
                return false;
            }
        }
        return true;
    }
    case ResourceType::Bin:
    case ResourceType::Xml: {
        if (options.texturesOnly) {
            *skipped = true;
            return true;
        }
        QByteArray data;
        if (!pack_->readBlob(info, &data, error)) {
            return false;
        }
        if (data.isEmpty()) {
            *skipped = true;
            return true;
        }
        return writeFile(out.filePath(unique(name, info.uid, used)), data, error);
    }
    default:
        if (!options.other || options.texturesOnly) {
            *skipped = true;
            return true;
        }
        return writeFile(
            out.filePath(unique(name + QLatin1Char('.') + resourceTypeName(info.type()) +
                                    QStringLiteral(".bin"),
                                info.uid, used)),
            pack_->raw(info), error);
    }
}

struct ArchivePackSource::State {
    std::shared_ptr<const EvoFileSystem> fs;
    EvoReader reader;
    int cacheSize;
    // Most recently used first; a null pack records a name that is not in the archive.
    std::vector<std::pair<QString, std::shared_ptr<ResourcePack>>> cache;

    State(std::shared_ptr<const EvoFileSystem> f, int size)
        : fs(std::move(f)), reader(fs), cacheSize(size) {}

    std::shared_ptr<ResourcePack> load(const QString& path) {
        const QString key = path.toLower();
        for (size_t i = 0; i < cache.size(); ++i) {
            if (cache[i].first == key) {
                auto hit = cache[i];
                cache.erase(cache.begin() + qsizetype(i));
                cache.insert(cache.begin(), hit);
                return hit.second;
            }
        }
        std::shared_ptr<ResourcePack> pack;
        const int index = fs->index().find(path);
        if (index >= 0) {
            const EvoIndexEntry& entry = fs->index().entries[size_t(index)];
            QByteArray data;
            QString ignored;
            if (reader.read(entry, 0, entry.size, &data, &ignored)) {
                pack = ResourcePack::parse(std::move(data), &ignored);
            }
        }
        cache.insert(cache.begin(), {key, pack});
        if (int(cache.size()) > cacheSize) {
            cache.pop_back();
        }
        return pack;
    }
};

ArchivePackSource::ArchivePackSource(std::shared_ptr<const EvoFileSystem> fs, int cacheSize)
    : state_(std::make_shared<State>(std::move(fs), std::max(1, cacheSize))) {}

std::unique_ptr<PackContext> ArchivePackSource::open(int entryIndex, QString* error) {
    const auto& entries = state_->fs->index().entries;
    if (entryIndex < 0 || entryIndex >= int(entries.size())) {
        *error = tr("no such file in the archive.");
        return nullptr;
    }
    const EvoIndexEntry& entry = entries[size_t(entryIndex)];
    QByteArray bytes;
    if (!state_->reader.read(entry, 0, entry.size, &bytes, error)) {
        return nullptr;
    }
    auto pack = ResourcePack::parse(std::move(bytes), error);
    if (!pack) {
        return nullptr;
    }
    const std::shared_ptr<State> state = state_;
    return std::make_unique<PackContext>(
        std::move(pack), entry.name, [state](const QString& path) { return state->load(path); });
}

std::unique_ptr<PackContext> openPackFromArchive(std::shared_ptr<const EvoFileSystem> fs,
                                                 int entryIndex, QString* error) {
    return ArchivePackSource(std::move(fs)).open(entryIndex, error);
}

bool isTopMipsPackName(const QString& name) {
    const QString lower = name.toLower();
    return lower.endsWith(QLatin1String("_top_mips.rpk")) ||
           lower.endsWith(QLatin1String("_topmips.rpk"));
}

std::unique_ptr<PackContext> openPackFromDisk(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return nullptr;
    }
    auto pack = ResourcePack::parse(file.readAll(), error);
    if (!pack) {
        return nullptr;
    }
    auto loader = [](const QString& other) -> std::shared_ptr<ResourcePack> {
        QFile sibling(other);
        if (!sibling.open(QIODevice::ReadOnly)) {
            return nullptr;
        }
        QString ignored;
        return ResourcePack::parse(sibling.readAll(), &ignored);
    };
    return std::make_unique<PackContext>(std::move(pack), QFileInfo(path).absoluteFilePath(),
                                         loader);
}
