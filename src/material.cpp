// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "material.h"

#include <QCoreApplication>
#include <QtEndian>

#include <cstring>

namespace {

constexpr int HeaderSize = 8 + 8 + 4; // "EVOSLITL", uid, version
constexpr int SlotTableStart = 0x26;
constexpr int SlotSize = 21;

QString tr(const char* text) {
    return QCoreApplication::translate("Material", text);
}

} // namespace

float Material::constant(int index) const {
    float value = 0.0f;
    if (index >= 0 && index < floatCount()) {
        const quint32 bits = qFromLittleEndian<quint32>(constants.constData() + index * 4);
        std::memcpy(&value, &bits, 4);
    }
    return value;
}

std::optional<Material> parseMaterial(const ResourcePack& pack, const ResourceInfo& info,
                                      QString* error) {
    auto fail = [error](const QString& message) {
        *error = message;
        return std::nullopt;
    };
    if (info.type() != 7) {
        return fail(tr("not a MATERIAL resource."));
    }
    if (pack.bigEndian()) {
        return fail(tr("big-endian (PS3) materials are not supported."));
    }
    const QByteArray d = pack.raw(info);
    const auto* p = reinterpret_cast<const uchar*>(d.constData());
    const qsizetype size = d.size();
    if (size < SlotTableStart || !d.startsWith("EVOSLITL")) {
        return fail(tr("too short, or no EVOS header."));
    }

    Material m;
    m.version = qFromLittleEndian<quint32>(p + 16);
    if (m.version != 0x20007) {
        return fail(
            tr("version 0x%1 is not supported (only 0x20007 is known).").arg(m.version, 0, 16));
    }
    m.subVersion = p[HeaderSize];
    const int count = p[HeaderSize + 1];
    m.reserved = d.mid(HeaderSize + 2, SlotTableStart - HeaderSize - 2);
    if (count < 1) {
        return fail(tr("no entries."));
    }

    qsizetype pos = SlotTableStart;
    for (int i = 0; i < count - 1; ++i) {
        if (pos + SlotSize > size) {
            return fail(tr("texture slot %1 is cut off.").arg(i));
        }
        MaterialSlot slot;
        slot.flags = p[pos];
        slot.nameHash = qFromLittleEndian<quint32>(p + pos + 1);
        std::memcpy(slot.state.data(), p + pos + 5, 4);
        for (int v = 0; v < 3; ++v) {
            const quint32 bits = qFromLittleEndian<quint32>(p + pos + 9 + v * 4);
            std::memcpy(&slot.values[size_t(v)], &bits, 4);
        }
        m.textureSlots.push_back(slot);
        pos += SlotSize;
    }

    if (pos + 6 > size) {
        return fail(tr("the parameter block is cut off."));
    }
    m.blockFlags = p[pos];
    m.blockHash = qFromLittleEndian<quint32>(p + pos + 1);
    const int parameters = p[pos + 5];
    pos += 6;
    if (pos + qsizetype(parameters) * 4 > size) {
        return fail(tr("the parameter names are cut off."));
    }
    for (int i = 0; i < parameters; ++i) {
        m.parameterHashes.push_back(qFromLittleEndian<quint32>(p + pos));
        pos += 4;
    }

    // Then the constants with their length, or nothing at all.
    if (pos == size) {
        return m;
    }
    if (pos + 4 > size) {
        return fail(tr("%1 stray bytes after the parameter names.").arg(size - pos));
    }
    const quint32 length = qFromLittleEndian<quint32>(p + pos);
    pos += 4;
    if (qsizetype(length) != size - pos) {
        return fail(tr("the constants say %1 bytes but %2 are left.").arg(length).arg(size - pos));
    }
    m.hasConstants = true;
    m.constants = d.mid(pos);
    return m;
}

std::vector<quint64> materialSlotTextures(const Material& material, const ResourceInfo& info) {
    std::vector<quint64> textures;
    for (quint64 dependency : info.dependencies) {
        if (quint16(dependency >> 48) == quint16(ResourceType::PixelBuffer)) {
            textures.push_back(dependency);
        }
    }
    if (material.textureSlots.empty() || textures.size() != material.textureSlots.size() - 1) {
        return {};
    }
    std::vector<quint64> out = {0};
    out.insert(out.end(), textures.begin(), textures.end());
    return out;
}
