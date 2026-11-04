// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "resource_pack.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QtEndian>

#include <cstring>

namespace {

constexpr quint32 InfoBlockMarker = 0x11111111;
constexpr quint32 MaxCount = 10000000;

QString tr(const char* text) {
    return QCoreApplication::translate("ResourcePack", text);
}

// Bounds-checked reads from memory in either byte order. A failed read sets ok() to false and
// every later read returns zero.
class Reader {
public:
    explicit Reader(const QByteArray& data, qint64 pos = 0) : data_(data), pos_(pos) {
        ok_ = pos >= 0 && pos <= data.size();
    }

    bool ok() const {
        return ok_;
    }
    qint64 pos() const {
        return pos_;
    }
    bool bigEndian() const {
        return big_;
    }

    template <typename T>
    T read() {
        T value{};
        if (!take(sizeof(T))) {
            return value;
        }
        std::memcpy(&value, data_.constData() + pos_ - qint64(sizeof(T)), sizeof(T));
        return big_ ? qFromBigEndian(value) : qFromLittleEndian(value);
    }

    QByteArray bytes(qint64 count) {
        if (count < 0 || !take(count)) {
            return {};
        }
        return data_.mid(pos_ - count, count);
    }

    // "EVOS" then "LITL" or "BIG ": every stream (and every resource) starts with one.
    bool streamHeader(QString* error) {
        const QByteArray magic = bytes(4);
        const QByteArray endian = bytes(4);
        if (!ok_ || magic != "EVOS") {
            *error = tr("EVOS stream header missing at 0x%1.").arg(pos_ - 8, 0, 16);
            ok_ = false;
            return false;
        }
        if (endian == "LITL") {
            big_ = false;
        } else if (endian == "BIG ") {
            big_ = true;
        } else {
            *error = tr("unknown byte order in stream header.");
            ok_ = false;
            return false;
        }
        return true;
    }

private:
    bool take(qint64 count) {
        if (!ok_ || pos_ + count > data_.size()) {
            ok_ = false;
            return false;
        }
        pos_ += count;
        return true;
    }

    const QByteArray& data_;
    qint64 pos_;
    bool big_ = false;
    bool ok_ = true;
};

QString poolString(const QByteArray& pool, quint32 offset) {
    if (offset >= quint32(pool.size())) {
        return {};
    }
    const char* start = pool.constData() + offset;
    const void* end = std::memchr(start, 0, size_t(pool.size()) - offset);
    const qsizetype length =
        end ? static_cast<const char*>(end) - start : pool.size() - qsizetype(offset);
    return QString::fromUtf8(start, length);
}

const char* const TypeNames[] = {
    "UNKNOWN",
    "RESOURCE_INFO_BLOCK",
    "STREAM_FORMAT",
    "VERTEX_BUFFER",
    "INDEX_BUFFER",
    "PIXEL_BUFFER",
    "SHADER",
    "MATERIAL",
    "MESH",
    "RESOURCE_COLLECTION",
    "HAVOK",
    "HIERARCHY",
    "RIGID_BODY_BINDINGS",
    "MOTORSTORM_WORLD",
    "MESHES",
    "SCENE",
    "MOTORSTORM_VEHICLE",
    "FONT",
    "CAMERAS",
    "CAMERA",
    "MOTORSTORM_SCENEOBJECT",
    "MOTORSTORM_ENVIRONMENT",
    "MOTORSTORM_CHARACTER",
    "BASICBITMAP",
    "MOTORSTORM_SOLIDS",
    "MOTORSTORM_GROUND_RUSH",
    "MOTORSTORM_PARTICLEEFFECT",
    "ROADSYSTEM",
    "GPU_PROGRAM",
    "GPU_PROGRAMBASE",
    "ATTRIBUTE_SET",
    "ATTRIBUTE_SET_BINDINGS",
    "BASICFONT",
    "OCTREE",
    "GUI_ANIM",
    "PLANT",
    "ANIMATIONS",
    "MOTORSTORM_LIGHTS",
    "BIN",
    "XML",
    "PATCH",
    "NEXUS_CHARACTER",
    "NEXUS_ANIMATION",
    "LEVEL_DATA",
    "LEVEL_TRIGGER_VOLUME",
    "LEVEL_DO_RESET_VOLUME",
    "LEVEL_NO_RESET_VOLUME",
    "LEVEL_SPAWN_POINT",
    "LEVEL_ROAD_REGION_VOLUME",
    "LEVEL_START_FINISH_VOLUME",
    nullptr,
    nullptr,
    "LEVEL_ROAD_DATA",
    "AUDIO_VEHICLEAUDIO",
    "AUDIO_VEHICLESOUNDEFFECT",
    "AUDIO_VEHICLESAMPLE",
    "ROAD_NETWORK",
    "LEVEL_SPLIT_VOLUME",
    "DEM",
    "DEM_TILE",
    "VEHICLE_CUSTOM_DATA",
    "LEVEL_SCRIPTED_ROAD_LINE",
    "PROCEDURAL_PLACEMENT",
    "ACTOR_DATA",
    "HABOOB_TEX_ATLAS_INDEX",
    "CROWDS",
    "AUDIO_SOUNDGROUP",
    "SVGMESH",
    "LEVEL_DRIFT_ZONE",
    "AUDIO_FSB",
    "ANIMATIONLIB",
    "TOP_MIPS",
    "LEVEL_AI_MAX_SPEED_VOLUME",
    "SCRIPTED_ROAD_REGION_VOLUME",
    "TRIGGER_DATA",
    "DUST_DATA",
    "VEHICLE_PARTICLES_DATA",
};

} // namespace

QString resourceTypeName(quint16 type) {
    if (type < std::size(TypeNames) && TypeNames[type]) {
        return QString::fromLatin1(TypeNames[type]);
    }
    return QStringLiteral("TYPE_%1").arg(type);
}

QString ResourceInfo::displayName() const {
    if (!names.isEmpty() && !names.first().isEmpty()) {
        return names.first();
    }
    return QStringLiteral("%1").arg(uid, 16, 16, QLatin1Char('0')).toUpper();
}

std::shared_ptr<ResourcePack> ResourcePack::parse(QByteArray data, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return nullptr;
    };

    auto pack = std::make_shared<ResourcePack>();
    pack->data_ = std::move(data);
    const QByteArray& bytes = pack->data_;

    Reader in(bytes);
    QString headerError;
    if (!in.streamHeader(&headerError)) {
        return fail(tr("Not a resource pack: %1").arg(headerError));
    }
    const qint32 magicLength = in.read<qint32>();
    if (in.bytes(magicLength) != "Resource PacK file" || !in.ok()) {
        return fail(tr("Not a resource pack (\"Resource PacK file\" missing)."));
    }
    in.read<quint32>(); // toc size
    const quint32 tocOffset = in.read<quint32>();
    if (!in.ok() || tocOffset >= quint32(bytes.size())) {
        return fail(tr("The resource table offset is out of range."));
    }
    pack->bigEndian_ = in.bigEndian();

    Reader toc(bytes, tocOffset);
    if (!toc.streamHeader(&headerError)) {
        return fail(tr("Resource table: %1").arg(headerError));
    }
    const quint64 blockUid = toc.read<quint64>();
    pack->version_ = toc.read<quint32>();
    if (quint16(blockUid >> 48) != quint16(ResourceType::ResourceInfoBlock)) {
        return fail(tr("The resource table is not a RESOURCE_INFO_BLOCK."));
    }
    if (toc.read<quint32>() != InfoBlockMarker) {
        return fail(tr("The resource table does not start with its 0x11111111 marker."));
    }
    const quint32 count = toc.read<quint32>();
    toc.read<quint32>(); // alias count
    toc.read<quint32>(); // dependency count
    const quint32 namePoolSize = toc.read<quint32>();
    const quint32 sourcePoolSize = toc.read<quint32>();
    quint32 unk3Count = 0;
    quint32 requiredCount = 0;
    if (pack->version_ >= 0x40002) {
        unk3Count = toc.read<quint32>();
        requiredCount = toc.read<quint32>();
    }
    pack->root_ = toc.read<quint64>();
    if (!toc.ok() || count > MaxCount || unk3Count > MaxCount || requiredCount > MaxCount) {
        return fail(tr("The resource table header is corrupt."));
    }
    const QByteArray namePool = toc.bytes(namePoolSize);
    const QByteArray sourcePool = toc.bytes(sourcePoolSize);

    pack->resources_.reserve(count);
    for (quint32 i = 0; i < count && toc.ok(); ++i) {
        ResourceInfo info;
        info.uid = toc.read<quint64>();
        info.size = toc.read<quint32>();
        info.offset = toc.read<quint32>();
        const quint16 dependencies = toc.read<quint16>();
        for (int d = 0; d < dependencies; ++d) {
            info.dependencies.push_back(toc.read<quint64>());
        }
        const quint16 names = toc.read<quint16>();
        for (int n = 0; n < names; ++n) {
            info.names << poolString(namePool, toc.read<quint32>());
        }
        const quint16 sources = toc.read<quint16>();
        for (int n = 0; n < sources; ++n) {
            info.sourcePaths << poolString(sourcePool, toc.read<quint32>());
        }
        pack->byUid_.insert(info.uid, int(pack->resources_.size()));
        pack->resources_.push_back(std::move(info));
    }
    for (quint32 i = 0; i < unk3Count && toc.ok(); ++i) {
        toc.read<quint64>();
        toc.read<quint32>();
    }
    for (quint32 i = 0; i < requiredCount && toc.ok(); ++i) {
        pack->requiredPacks_ << poolString(namePool, toc.read<quint32>());
    }
    if (!toc.ok()) {
        return fail(tr("The resource table is truncated."));
    }
    return pack;
}

const ResourceInfo* ResourcePack::find(quint64 uid) const {
    const auto it = byUid_.constFind(uid);
    return it == byUid_.constEnd() ? nullptr : &resources_[size_t(*it)];
}

QByteArray ResourcePack::raw(const ResourceInfo& info) const {
    return data_.mid(info.offset, info.size);
}

bool ResourcePack::readBlob(const ResourceInfo& info, QByteArray* out, QString* error) const {
    Reader in(data_, info.offset);
    if (!in.streamHeader(error)) {
        return false;
    }
    in.read<quint64>(); // uid
    in.read<quint32>(); // version
    const quint32 size = in.read<quint32>();
    *out = in.bytes(size);
    if (!in.ok()) {
        *error = tr("resource data is truncated.");
        return false;
    }
    return true;
}

std::optional<PixelBuffer> ResourcePack::readPixelBuffer(const ResourceInfo& info,
                                                         QString* error) const {
    Reader in(data_, info.offset);
    if (!in.streamHeader(error)) {
        return std::nullopt;
    }
    PixelBuffer pb;
    in.read<quint64>();
    pb.version = in.read<quint32>();
    if (pb.version == 0x20000) {
        in.read<quint64>();
    }
    pb.width = in.read<quint16>();
    pb.height = in.read<quint16>();
    pb.depth = in.read<quint16>();
    pb.pixelFormat = in.read<quint16>();
    pb.mipLevels = in.read<quint8>();
    pb.type = in.read<quint8>();
    in.read<quint8>(); // server usage
    if (pb.version >= 0x20003) {
        in.read<quint8>();
        pb.startMip = in.read<quint8>();
        if (pb.version >= 0x20005) {
            in.read<quint8>();
        }
        pb.topMipsUid = in.read<quint64>();
    }
    const quint32 size = in.read<quint32>();
    pb.data = in.bytes(size);
    if (!in.ok()) {
        *error = tr("texture data is truncated.");
        return std::nullopt;
    }
    return pb;
}

std::optional<QByteArray> ResourcePack::readTopMips(quint64 uid, QString* error) const {
    const ResourceInfo* info = find(uid);
    if (!info) {
        return std::nullopt;
    }
    Reader in(data_, info->offset);
    if (!in.streamHeader(error)) {
        return std::nullopt;
    }
    in.read<quint64>();
    in.read<quint32>();
    in.read<quint64>(); // pixel buffer uid
    const quint32 size = in.read<quint32>();
    QByteArray out = in.bytes(size);
    if (!in.ok()) {
        *error = tr("top mips data is truncated.");
        return std::nullopt;
    }
    return out;
}

QStringList topMipsPackCandidates(const QString& rpkPath) {
    const QFileInfo info(rpkPath);
    QString dir = rpkPath.left(rpkPath.size() - info.fileName().size());
    const QString base = info.completeBaseName();
    QStringList out;
    // Single-texture packs: "x.tif_pixel_buffer.rpk" keeps its top mips in "x.tif_top_mips.rpk".
    const QString pixelBuffer = QStringLiteral("_pixel_buffer");
    if (base.endsWith(pixelBuffer, Qt::CaseInsensitive)) {
        out << dir + base.left(base.size() - pixelBuffer.size()) + QStringLiteral("_top_mips.rpk");
    }
    out << dir + base + QStringLiteral("_top_mips.rpk")
        << dir + base + QStringLiteral("_topmips.rpk");
    for (int i = 0; i < base.size(); ++i) {
        if (base.at(i) == QLatin1Char('_')) {
            out << dir + base.left(i) + QStringLiteral("_common_topmips.rpk");
        }
    }
    out.removeDuplicates();
    return out;
}
