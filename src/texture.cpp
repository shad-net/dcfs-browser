// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "texture.h"

#include <QCoreApplication>
#include <QtEndian>

#include <bcdec.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

using Kind = TextureFormat::Kind;

QString tr(const char* text) {
    return QCoreApplication::translate("Texture", text);
}

// evo::renderer::PixelFormat -> DXGI. The first block is DriveClubFS's mapping, kept as-is
// (including DXT3 -> BC3 and DXT5 -> BC5); the second adds simple formats it does not handle.
const TextureFormat Formats[] = {
    {1, "R8G8B8A8_UNORM", 28, 32, 1, Kind::Rgba8, false},
    {2, "R8G8B8A8_UNORM_SRGB", 29, 32, 1, Kind::Rgba8, false},
    {3, "R16G16B16A16_FLOAT", 10, 64, 1, Kind::Rgba16F, false},
    {4, "R32G32B32A32_FLOAT", 2, 128, 1, Kind::Rgba32F, false},
    {9, "BC1_UNORM", 71, 4, 4, Kind::Bc1, false},
    {10, "BC1_UNORM_SRGB", 72, 4, 4, Kind::Bc1, false},
    {11, "BC3_UNORM", 77, 8, 4, Kind::Bc3, false},
    {12, "BC3_UNORM_SRGB", 78, 8, 4, Kind::Bc3, false},
    {13, "BC5_UNORM", 83, 8, 4, Kind::Bc5, false},
    {14, "BC5_SNORM", 84, 8, 4, Kind::Bc5, true},
    {15, "A8_UNORM", 65, 8, 1, Kind::A8, false},
    {23, "R32_FLOAT", 41, 32, 1, Kind::R32F, false},
    {29, "BC4_UNORM", 80, 4, 4, Kind::Bc4, false},
    {30, "BC4_SNORM", 81, 4, 4, Kind::Bc4, true},
    {31, "BC5_UNORM", 83, 8, 4, Kind::Bc5, false},
    {32, "BC5_SNORM", 84, 8, 4, Kind::Bc5, true},
    {33, "BC6H_UF16", 95, 8, 4, Kind::Bc6h, false},
    {34, "BC6H_SF16", 96, 8, 4, Kind::Bc6h, true},
    {35, "BC7_UNORM", 98, 8, 4, Kind::Bc7, false},
    {36, "BC7_UNORM_SRGB", 99, 8, 4, Kind::Bc7, false},

    {16, "R16G16_FLOAT", 34, 32, 1, Kind::Rg16F, false},
    {19, "R8G8_UNORM", 49, 16, 1, Kind::Rg8, false},
    {22, "R16_FLOAT", 54, 16, 1, Kind::R16F, false},
    {26, "R10G10B10A2_UNORM", 24, 32, 1, Kind::Rgb10A2, false},
    {27, "R8_UNORM", 61, 8, 1, Kind::R8, false},
    {28, "B8G8R8A8_UNORM_SRGB", 91, 32, 1, Kind::Bgra8, false},
    {37, "R16G16B16A16_UNORM", 11, 64, 1, Kind::Rgba16, false},
};

const char* const EvoNames[] = {
    "UNDEFINED",   "RGBA_32_I_LIN",  "RGBA_32_I_SRGB",
    "RGBA_64_F",   "RGBA_128_F",     "Z_16",
    "Z_32",        "Z32_S8",         "STENCIL",
    "DXT1_LIN",    "DXT1_SRGB",      "DXT3_LIN",
    "DXT3_SRGB",   "DXT5_LIN",       "DXT5_SRGB",
    "A8_UNORM",    "RG_16_F",        "RG_32_F",
    "RGBA_16_I",   "R8G8",           "A16",
    "L16A16",      "R_16_F",         "R_32_F",
    "R11G11B10_F", "R9G9B9E5",       "R10G10B10A2_UNORM",
    "R8_UNORM",    "BGRA_32_I_SRGB", "BC4_UNORM",
    "BC4_SNORM",   "BC5_UNORM",      "BC5_SNORM",
    "BC6H_UF",     "BC6H_SF",        "BC7_UNORM",
    "BC7_SRGB",    "RGBA_64_UNORM",  "RGBA_32_SNORM",
};

// Morton (Z-order) index -> offset inside an 8x8 tile (DriveClubFS's MortonReorder(t, 8, 8)).
int mortonInTile(int i) {
    int x = 0;
    int y = 0;
    for (int bit = 0; bit < 3; ++bit) {
        x |= ((i >> (2 * bit)) & 1) << bit;
        y |= ((i >> (2 * bit + 1)) & 1) << bit;
    }
    return y * 8 + x;
}

uchar unit(float v) {
    if (!(v > 0.0f)) {
        return 0;
    }
    return v >= 1.0f ? 255 : uchar(v * 255.0f + 0.5f);
}

// HDR to display: a plain clamp keeps colour data right; values above 1 just saturate.
uchar hdr(float v) {
    return unit(v);
}

float half(const uchar* p) {
    const quint16 h = qFromLittleEndian<quint16>(p);
    const int exponent = (h >> 10) & 0x1F;
    const int mantissa = h & 0x3FF;
    float value;
    if (exponent == 0) {
        value = std::ldexp(float(mantissa), -24);
    } else if (exponent == 31) {
        value = mantissa ? NAN : INFINITY;
    } else {
        value = std::ldexp(float(mantissa | 0x400), exponent - 25);
    }
    return (h & 0x8000) ? -value : value;
}

float f32(const uchar* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

void putPixel(QImage& image, int x, int y, uchar r, uchar g, uchar b, uchar a) {
    uchar* p = image.scanLine(y) + x * 4;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}

// A BC5 normal map stores X and Y; rebuild Z so the preview looks like a normal map.
void putNormal(QImage& image, int x, int y, float nx, float ny) {
    const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
    putPixel(image, x, y, unit(nx * 0.5f + 0.5f), unit(ny * 0.5f + 0.5f), unit(nz * 0.5f + 0.5f),
             255);
}

} // namespace

const TextureFormat* findTextureFormat(int evoPixelFormat) {
    for (const TextureFormat& f : Formats) {
        if (f.evo == evoPixelFormat) {
            return &f;
        }
    }
    return nullptr;
}

QString evoPixelFormatName(int evoPixelFormat) {
    if (evoPixelFormat >= 0 && evoPixelFormat < int(std::size(EvoNames))) {
        return QString::fromLatin1(EvoNames[evoPixelFormat]);
    }
    return QStringLiteral("FORMAT_%1").arg(evoPixelFormat);
}

std::optional<Texture> untileTexture(const PixelBuffer& pb, const QByteArray& tiled,
                                     QString* error) {
    if (!pb.isPs4()) {
        *error = tr("PS3 texture (version 0x%1), not supported.").arg(pb.version, 0, 16);
        return std::nullopt;
    }
    const TextureFormat* format = findTextureFormat(pb.pixelFormat);
    if (!format) {
        *error = tr("Pixel format %1 (%2) is not supported.")
                     .arg(evoPixelFormatName(pb.pixelFormat))
                     .arg(pb.pixelFormat);
        return std::nullopt;
    }
    if (pb.width <= 0 || pb.height <= 0 || pb.width > 16384 || pb.height > 16384) {
        *error = tr("Invalid texture size %1 x %2.").arg(pb.width).arg(pb.height);
        return std::nullopt;
    }

    const int ppb = format->pixelsPerBlock;
    const int blockSize = format->blockBytes();
    const int widthBlocks = (pb.width + ppb - 1) / ppb;
    const int heightBlocks = (pb.height + ppb - 1) / ppb;
    const int tilesX = (widthBlocks + 7) / 8;
    const int tilesY = (heightBlocks + 7) / 8;

    Texture texture;
    texture.width = pb.width;
    texture.height = pb.height;
    texture.format = format;
    texture.data = QByteArray(qsizetype(widthBlocks) * heightBlocks * blockSize, '\0');

    const char* in = tiled.constData();
    char* out = texture.data.data();
    qint64 source = 0;
    bool shortData = false;
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            for (int t = 0; t < 64; ++t, source += blockSize) {
                const int inTile = mortonInTile(t);
                const int x = tx * 8 + inTile % 8;
                const int y = ty * 8 + inTile / 8;
                if (x >= widthBlocks || y >= heightBlocks) {
                    continue;
                }
                if (source + blockSize > tiled.size()) {
                    shortData = true;
                    continue;
                }
                std::memcpy(out + (qint64(y) * widthBlocks + x) * blockSize, in + source,
                            size_t(blockSize));
            }
        }
    }
    if (shortData) {
        texture.warning = tr("The texture data is shorter than %1 x %2 needs; the rest is black.")
                              .arg(pb.width)
                              .arg(pb.height);
    }
    return texture;
}

QByteArray ddsFile(const Texture& texture) {
    QByteArray out;
    out.reserve(148 + texture.data.size());
    auto u32 = [&out](quint32 v) {
        const quint32 le = qToLittleEndian(v);
        out.append(reinterpret_cast<const char*>(&le), 4);
    };
    out.append("DDS ", 4);
    u32(124);                     // header size
    u32(0x00001007 | 0x00080000); // CAPS | HEIGHT | WIDTH | PIXELFORMAT | LINEARSIZE
    u32(quint32(texture.height));
    u32(quint32(texture.width));
    u32(0);               // pitch or linear size
    u32(0);               // depth
    u32(1);               // mip count
    out.append(44, '\0'); // reserved
    u32(32);              // pixel format size
    u32(0x04);            // DDPF_FOURCC
    out.append("DX10", 4);
    for (int i = 0; i < 5; ++i) {
        u32(0); // bit count and masks
    }
    u32(0x1000 | 0x400000 | 0x08); // caps
    out.append(16, '\0');          // caps2-4, reserved
    u32(quint32(texture.format->dxgi));
    u32(3); // DDS_DIMENSION_TEXTURE2D
    u32(0); // misc flags
    u32(1); // array size
    u32(0); // misc flags 2
    out.append(texture.data);
    return out;
}

QImage textureImage(const Texture& texture) {
    const TextureFormat& f = *texture.format;
    QImage image(texture.width, texture.height, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    const uchar* data = reinterpret_cast<const uchar*>(texture.data.constData());

    if (f.pixelsPerBlock == 1) {
        const int bpp = f.bitsPerPixel / 8;
        for (int y = 0; y < texture.height; ++y) {
            for (int x = 0; x < texture.width; ++x) {
                const uchar* p = data + (qint64(y) * texture.width + x) * bpp;
                switch (f.kind) {
                case Kind::Rgba8:
                    putPixel(image, x, y, p[0], p[1], p[2], p[3]);
                    break;
                case Kind::Bgra8:
                    putPixel(image, x, y, p[2], p[1], p[0], p[3]);
                    break;
                case Kind::R8:
                case Kind::A8:
                    putPixel(image, x, y, p[0], p[0], p[0], 255);
                    break;
                case Kind::Rg8:
                    putPixel(image, x, y, p[0], p[1], 0, 255);
                    break;
                case Kind::Rgba16F:
                    putPixel(image, x, y, hdr(half(p)), hdr(half(p + 2)), hdr(half(p + 4)),
                             unit(half(p + 6)));
                    break;
                case Kind::Rgba32F:
                    putPixel(image, x, y, hdr(f32(p)), hdr(f32(p + 4)), hdr(f32(p + 8)),
                             unit(f32(p + 12)));
                    break;
                case Kind::R16F: {
                    const uchar v = hdr(half(p));
                    putPixel(image, x, y, v, v, v, 255);
                    break;
                }
                case Kind::Rg16F:
                    putPixel(image, x, y, hdr(half(p)), hdr(half(p + 2)), 0, 255);
                    break;
                case Kind::R32F: {
                    const uchar v = hdr(f32(p));
                    putPixel(image, x, y, v, v, v, 255);
                    break;
                }
                case Kind::Rgb10A2: {
                    const quint32 v = qFromLittleEndian<quint32>(p);
                    putPixel(image, x, y, uchar((v & 0x3FF) >> 2), uchar(((v >> 10) & 0x3FF) >> 2),
                             uchar(((v >> 20) & 0x3FF) >> 2), uchar((v >> 30) * 85));
                    break;
                }
                case Kind::Rgba16:
                    putPixel(image, x, y, p[1], p[3], p[5], p[7]);
                    break;
                default:
                    break;
                }
            }
        }
        return image;
    }

    const int widthBlocks = (texture.width + 3) / 4;
    const int heightBlocks = (texture.height + 3) / 4;
    const int blockSize = f.blockBytes();
    uchar rgba[16 * 4];
    float floats[16 * 3];
    for (int by = 0; by < heightBlocks; ++by) {
        for (int bx = 0; bx < widthBlocks; ++bx) {
            const uchar* block = data + (qint64(by) * widthBlocks + bx) * blockSize;
            switch (f.kind) {
            case Kind::Bc1:
                bcdec_bc1(block, rgba, 16);
                break;
            case Kind::Bc3:
                bcdec_bc3(block, rgba, 16);
                break;
            case Kind::Bc7:
                bcdec_bc7(block, rgba, 16);
                break;
            case Kind::Bc4:
                bcdec_bc4_float(block, floats, 4, f.isSigned);
                break;
            case Kind::Bc5:
                bcdec_bc5_float(block, floats, 8, f.isSigned);
                break;
            case Kind::Bc6h:
                bcdec_bc6h_float(block, floats, 12, f.isSigned);
                break;
            default:
                break;
            }
            for (int py = 0; py < 4; ++py) {
                const int y = by * 4 + py;
                if (y >= texture.height) {
                    break;
                }
                for (int px = 0; px < 4; ++px) {
                    const int x = bx * 4 + px;
                    if (x >= texture.width) {
                        break;
                    }
                    const int i = py * 4 + px;
                    switch (f.kind) {
                    case Kind::Bc4: {
                        const float v = f.isSigned ? floats[i] * 0.5f + 0.5f : floats[i];
                        putPixel(image, x, y, unit(v), unit(v), unit(v), 255);
                        break;
                    }
                    case Kind::Bc5: {
                        const float r = floats[i * 2];
                        const float g = floats[i * 2 + 1];
                        if (f.isSigned) {
                            putNormal(image, x, y, r, g);
                        } else {
                            putNormal(image, x, y, r * 2.0f - 1.0f, g * 2.0f - 1.0f);
                        }
                        break;
                    }
                    case Kind::Bc6h:
                        putPixel(image, x, y, hdr(floats[i * 3]), hdr(floats[i * 3 + 1]),
                                 hdr(floats[i * 3 + 2]), 255);
                        break;
                    default:
                        putPixel(image, x, y, rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2],
                                 rgba[i * 4 + 3]);
                        break;
                    }
                }
            }
        }
    }
    return image;
}
