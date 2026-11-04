// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// PS4 textures from resource packs: format table, untiling, DDS output and decoding to a QImage
// for preview. Untiling and the format mapping follow DriveClubFS by Nenkai (MIT).

#pragma once

#include "resource_pack.h"

#include <QByteArray>
#include <QImage>
#include <QString>

#include <optional>

struct TextureFormat {
    enum class Kind {
        Rgba8,
        Bgra8,
        R8,
        A8,
        Rg8,
        Rgba16F,
        Rgba32F,
        R16F,
        Rg16F,
        R32F,
        Rgb10A2,
        Rgba16,
        Bc1,
        Bc3,
        Bc4,
        Bc5,
        Bc6h,
        Bc7,
    };
    int evo;          // evo::renderer::PixelFormat
    const char* name; // DXGI name without the DXGI_FORMAT_ prefix
    int dxgi;
    int bitsPerPixel;
    int pixelsPerBlock; // 4 for block compressed formats
    Kind kind;
    bool isSigned;

    // Bytes per pixel (uncompressed) or per 4x4 block.
    int blockBytes() const {
        return pixelsPerBlock == 1 ? bitsPerPixel / 8 : bitsPerPixel * 2;
    }
};

const TextureFormat* findTextureFormat(int evoPixelFormat);
QString evoPixelFormatName(int evoPixelFormat);

struct Texture {
    int width = 0;
    int height = 0;
    const TextureFormat* format = nullptr;
    QByteArray data;
    QString warning; // set when the tiled data was shorter than expected
};

std::optional<Texture> untileTexture(const PixelBuffer& pb, const QByteArray& tiled,
                                     QString* error);
QByteArray ddsFile(const Texture& texture);
QImage textureImage(const Texture& texture);
