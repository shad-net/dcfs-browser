// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//

#pragma once

#include "resource_pack.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <optional>
#include <vector>

struct MaterialSlot {
    quint8 flags = 0;              // 1 or 3 in almost all materials
    quint32 nameHash = 0;          // shader texture slot name
    std::array<quint8, 4> state{}; // sampler state: 00000004, 00000003, 01020104, 00000000...
    std::array<float, 3> values{}; // 0, 0, 255 (mip bias / min LOD / max LOD?); 0 0 0 if unused
};

struct Material {
    quint32 version = 0;
    quint8 subVersion = 0; // byte 0x14, always 2 so far
    QByteArray reserved;   // bytes 0x16..0x25, not understood
    // Texture slots. The first (0xd1bb02dc in every material seen) has no texture of its own;
    // the others take the material's PIXEL_BUFFER dependencies in order.
    std::vector<MaterialSlot> textureSlots; // ("slots" is a Qt keyword)
    quint8 blockFlags = 0;
    quint32 blockHash = 0; // shader parameter block name
    std::vector<quint32> parameterHashes;
    bool hasConstants = false; // a few materials end before the constants
    QByteArray constants;      // parameter values: 32-bit floats, laid out by the shader

    int floatCount() const {
        return int(constants.size() / 4);
    }
    float constant(int index) const;
};

std::optional<Material> parseMaterial(const ResourcePack& pack, const ResourceInfo& info,
                                      QString* error);
std::vector<quint64> materialSlotTextures(const Material& material, const ResourceInfo& info);
