// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Evolution Studios resource packs (.rpk): a table of typed resources (textures, XML, binary
// blobs, meshes, ...) with names. Format from DriveClubFS by Nenkai (MIT):
// https://github.com/Nenkai/DriveClubFS

#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

// evo resource type ids (the top 16 bits of a resource uid).
enum class ResourceType : quint16 {
    ResourceInfoBlock = 1,
    PixelBuffer = 5,
    Bin = 38,
    Xml = 39,
    TopMips = 71,
};

// "PIXEL_BUFFER", "XML", ... (RTUID_ prefix dropped), or "TYPE_<n>".
QString resourceTypeName(quint16 type);

struct ResourceInfo {
    quint64 uid = 0;
    quint32 size = 0;
    quint32 offset = 0;
    std::vector<quint64> dependencies;
    QStringList names;
    QStringList sourcePaths;

    quint16 type() const {
        return quint16(uid >> 48);
    }
    // names[0], or the uid when the resource has no name.
    QString displayName() const;
};

// A PIXEL_BUFFER resource: one texture, still in the PS4's tiled layout.
struct PixelBuffer {
    quint32 version = 0;
    int width = 0;
    int height = 0;
    int depth = 0;
    int pixelFormat = 0; // evo::renderer::PixelFormat
    int mipLevels = 0;
    int type = 0;
    int startMip = 0;
    quint64 topMipsUid = 0;
    QByteArray data;

    // PS4 layout (0x20003+); older versions are PS3 textures, which are not supported.
    bool isPs4() const {
        return version >= 0x20003;
    }
};

class ResourcePack {
public:
    // Parses a whole .rpk held in memory. On failure returns nothing and sets *error.
    static std::shared_ptr<ResourcePack> parse(QByteArray data, QString* error);

    const std::vector<ResourceInfo>& resources() const {
        return resources_;
    }
    const ResourceInfo* find(quint64 uid) const;
    quint64 rootUid() const {
        return root_;
    }
    quint32 version() const {
        return version_;
    }
    bool bigEndian() const {
        return bigEndian_;
    }
    const QStringList& requiredPacks() const {
        return requiredPacks_;
    }
    qint64 byteSize() const {
        return data_.size();
    }

    // The payload of a BIN or XML resource.
    bool readBlob(const ResourceInfo& info, QByteArray* out, QString* error) const;
    std::optional<PixelBuffer> readPixelBuffer(const ResourceInfo& info, QString* error) const;
    // The data of a TOP_MIPS resource with this uid, if this pack has it.
    std::optional<QByteArray> readTopMips(quint64 uid, QString* error) const;
    // The resource's bytes exactly as stored, header included.
    QByteArray raw(const ResourceInfo& info) const;

private:
    QByteArray data_;
    quint64 root_ = 0;
    quint32 version_ = 0;
    bool bigEndian_ = false;
    std::vector<ResourceInfo> resources_;
    QHash<quint64, int> byUid_;
    QStringList requiredPacks_;
};

// Where the top mips of `rpkPath` ("dir/name.rpk") may live: for "x_pixel_buffer.rpk" first
// "x_top_mips.rpk", then as DriveClubFS tries them: name_top_mips.rpk, name_topmips.rpk, then
// <prefix>_common_topmips.rpk for every '_'.
QStringList topMipsPackCandidates(const QString& rpkPath);
