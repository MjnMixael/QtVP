#include "ImageDecoders.h"

#include <QCoreApplication>
#include <QPainter>
#include <QtAlgorithms>

#include <algorithm>
#include <cstring>
#include <vector>

#define BCDEC_IMPLEMENTATION
#include "Dependencies/bcdec/bcdec.h"

namespace {

constexpr int MaxDimension = 16384;

QString tr(const char* text)
{
    return QCoreApplication::translate("ImageDecoders", text);
}

QImage fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return QImage();
}

quint16 readU16(const uchar* p)
{
    return quint16(p[0] | (p[1] << 8));
}

quint32 readU32(const uchar* p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

int expand5(int v)
{
    return (v << 3) | (v >> 2);
}

// One TGA pixel or color map entry, stored little-endian as BGR(A) or 1-5-5-5
QRgb tgaColor(const uchar* p, int bits, bool alpha)
{
    switch (bits) {
    case 15:
    case 16: {
        const int v = readU16(p);
        const int a = alpha ? ((v & 0x8000) ? 255 : 0) : 255;
        return qRgba(expand5((v >> 10) & 31), expand5((v >> 5) & 31), expand5(v & 31), a);
    }
    case 24:
        return qRgb(p[2], p[1], p[0]);
    case 32:
        return qRgba(p[2], p[1], p[0], alpha ? p[3] : 255);
    }
    return qRgb(0, 0, 0);
}

} // namespace

// The engine only reads 8-bit, single-plane PCX with a 256-color palette at the end.
// 24-bit PCX (three 8-bit planes) turns up in old VPs too, so show those as well.
QImage ImageDecoders::decodePcx(const QByteArray& data, QString* details, QString* error)
{
    constexpr int HeaderSize = 128;
    constexpr int PaletteSize = 768;
    if (data.size() < HeaderSize + 1)
        return fail(error, tr("The file is too small to be a PCX image."));

    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    const int manufacturer = p[0];
    const int encoding = p[2];
    const int bitsPerPixel = p[3];
    const int planes = p[65];
    if (manufacturer != 0x0A || encoding != 1)
        return fail(error, tr("Not a PCX image."));

    const bool paletted = bitsPerPixel == 8 && planes == 1;
    const bool trueColor = bitsPerPixel == 8 && planes == 3;
    if (!paletted && !trueColor)
        return fail(error, tr("Unsupported PCX image: %1-bit with %2 planes.").arg(bitsPerPixel).arg(planes));

    const int xMin = readU16(p + 4);
    const int yMin = readU16(p + 6);
    const int xMax = readU16(p + 8);
    const int yMax = readU16(p + 10);
    const int bytesPerLine = readU16(p + 66);
    const int width = xMax - xMin + 1;
    const int height = yMax - yMin + 1;
    if (width <= 0 || height <= 0 || width > MaxDimension || height > MaxDimension || bytesPerLine < width)
        return fail(error, tr("The PCX header has invalid dimensions."));

    // Paletted images keep a marker byte and the palette after the pixel data
    const uchar* dataEnd = p + data.size();
    if (paletted) {
        if (data.size() < HeaderSize + PaletteSize + 1 || dataEnd[-PaletteSize - 1] != 0x0C)
            return fail(error, tr("The PCX image has no 256-color palette."));
        dataEnd -= PaletteSize + 1;
    }

    QImage image(width, height, paletted ? QImage::Format_Indexed8 : QImage::Format_RGB32);
    if (image.isNull())
        return fail(error, tr("Not enough memory for a %1 x %2 image.").arg(width).arg(height));

    if (paletted) {
        const uchar* palette = dataEnd + 1;
        QList<QRgb> colors(256);
        for (int i = 0; i < 256; ++i)
            colors[i] = qRgb(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]);
        image.setColorTable(colors);
    }

    // Each row is every plane's bytesPerLine bytes back to back, RLE-coded:
    // a byte with the top two bits set is a run count for the byte after it
    const int rowBytes = bytesPerLine * planes;
    std::vector<uchar> row(rowBytes);
    const uchar* src = p + HeaderSize;
    for (int y = 0; y < height; ++y) {
        int x = 0;
        while (x < rowBytes) {
            if (src >= dataEnd)
                return fail(error, tr("The PCX image data ends early."));
            uchar value = *src++;
            int count = 1;
            if ((value & 0xC0) == 0xC0) {
                count = value & 0x3F;
                if (src >= dataEnd)
                    return fail(error, tr("The PCX image data ends early."));
                value = *src++;
            }
            for (; count > 0 && x < rowBytes; --count)
                row[x++] = value;
        }

        if (paletted) {
            std::copy(row.begin(), row.begin() + width, image.scanLine(y));
        } else {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int i = 0; i < width; ++i)
                line[i] = qRgb(row[i], row[bytesPerLine + i], row[2 * bytesPerLine + i]);
        }
    }

    if (details)
        *details = paletted ? tr("8-bit paletted") : tr("24-bit (the engine does not load 24-bit PCX)");
    return image;
}

// Types 1-3 (color-mapped, truecolor, grayscale) and their RLE forms 9-11
QImage ImageDecoders::decodeTga(const QByteArray& data, QString* details, QString* error)
{
    constexpr int HeaderSize = 18;
    if (data.size() < HeaderSize)
        return fail(error, tr("The file is too small to be a TGA image."));

    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    const uchar* end = p + data.size();
    const int idLength = p[0];
    const int colorMapType = p[1];
    const int imageType = p[2];
    const int mapFirst = readU16(p + 3);
    const int mapLength = readU16(p + 5);
    const int mapBits = p[7];
    const int width = readU16(p + 12);
    const int height = readU16(p + 14);
    const int depth = p[16];
    const int descriptor = p[17];

    const bool rle = imageType >= 9;
    const int baseType = rle ? imageType - 8 : imageType;
    if (baseType < 1 || baseType > 3)
        return fail(error, tr("Unsupported TGA image type %1.").arg(imageType));
    if (width == 0 || height == 0 || width > MaxDimension || height > MaxDimension)
        return fail(error, tr("The TGA header has invalid dimensions."));

    const bool validDepth = baseType == 2 ? (depth == 15 || depth == 16 || depth == 24 || depth == 32) : depth == 8;
    if (!validDepth)
        return fail(error, tr("Unsupported TGA pixel depth %1.").arg(depth));

    // 16-bit alpha only counts when the descriptor says there is an alpha bit
    const bool hasAlpha = depth == 32 || (depth == 16 && (descriptor & 0x0F) != 0);

    const uchar* src = p + HeaderSize + idLength;
    std::vector<QRgb> palette;
    if (colorMapType == 1) {
        if (mapBits != 15 && mapBits != 16 && mapBits != 24 && mapBits != 32)
            return fail(error, tr("Unsupported TGA color map depth %1.").arg(mapBits));
        const int entryBytes = (mapBits + 7) / 8;
        if (end - src < qint64(mapLength) * entryBytes)
            return fail(error, tr("The TGA color map runs past the end of the file."));
        palette.reserve(mapLength);
        for (int i = 0; i < mapLength; ++i)
            palette.push_back(tgaColor(src + i * entryBytes, mapBits, mapBits == 32));
        src += mapLength * entryBytes;
    }
    if (baseType == 1 && palette.empty())
        return fail(error, tr("The color-mapped TGA image has no color map."));

    const int bytesPerPixel = (depth + 7) / 8;
    auto pixel = [&](const uchar* px) -> QRgb {
        if (baseType == 1) {
            const int index = px[0] - mapFirst;
            return index >= 0 && index < int(palette.size()) ? palette[index] : qRgb(0, 0, 0);
        }
        if (baseType == 3)
            return qRgb(px[0], px[0], px[0]);
        return tgaColor(px, depth, hasAlpha);
    };

    const size_t total = size_t(width) * height;
    std::vector<QRgb> pixels(total);
    if (!rle) {
        if (end - src < qint64(total) * bytesPerPixel)
            return fail(error, tr("The TGA image data ends early."));
        for (size_t i = 0; i < total; ++i)
            pixels[i] = pixel(src + i * bytesPerPixel);
    } else {
        // Each packet is a count byte, then one repeated pixel (top bit set) or count raw pixels
        size_t i = 0;
        while (i < total) {
            if (src >= end)
                return fail(error, tr("The TGA image data ends early."));
            const int header = *src++;
            const int count = (header & 0x7F) + 1;
            const qint64 needed = (header & 0x80) ? bytesPerPixel : qint64(count) * bytesPerPixel;
            if (end - src < needed)
                return fail(error, tr("The TGA image data ends early."));

            if (header & 0x80) {
                const QRgb color = pixel(src);
                for (int k = 0; k < count && i < total; ++k)
                    pixels[i++] = color;
            } else {
                for (int k = 0; k < count && i < total; ++k)
                    pixels[i++] = pixel(src + k * bytesPerPixel);
            }
            src += needed;
        }
    }

    const bool alpha = hasAlpha || (baseType == 1 && mapBits == 32);
    QImage image(width, height, alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    if (image.isNull())
        return fail(error, tr("Not enough memory for a %1 x %2 image.").arg(width).arg(height));

    // Rows are stored bottom-up unless descriptor bit 5 is set; bit 4 mirrors them
    const bool topDown = descriptor & 0x20;
    const bool rightToLeft = descriptor & 0x10;
    for (int row = 0; row < height; ++row) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(topDown ? row : height - 1 - row));
        const QRgb* in = pixels.data() + size_t(row) * width;
        for (int x = 0; x < width; ++x)
            line[rightToLeft ? width - 1 - x : x] = in[x];
    }

    if (details) {
        const QString kind = baseType == 1 ? tr("%1-bit paletted").arg(depth)
            : baseType == 3 ? tr("8-bit grayscale")
            : tr("%1-bit").arg(depth);
        *details = rle ? tr("%1, RLE").arg(kind) : kind;
    }
    return image;
}

namespace {

// DDS flags, from the DirectX DDS documentation
constexpr quint32 DdpfAlphaPixels = 0x1;
constexpr quint32 DdpfAlpha = 0x2;
constexpr quint32 DdpfFourCC = 0x4;
constexpr quint32 DdpfPaletteIndexed8 = 0x20;
constexpr quint32 DdpfLuminance = 0x20000;
constexpr quint32 Caps2Cubemap = 0x200;
constexpr quint32 Caps2AllFaces = 0xFC00;
constexpr quint32 Caps2Volume = 0x200000;
constexpr quint32 Dx10MiscCubemap = 0x4;

// Large cubemap faces are shrunk to this before laying out the cross
constexpr int MaxCubeFace = 1024;

using BlockDecoder = void (*)(const void* block, void* pixels, int pitch);

struct DdsFormat
{
    enum class Layout { Masked, Paletted, AlphaOnly, Luminance };

    QString name;
    BlockDecoder decode = nullptr;  // null for uncompressed
    int blockSize = 0;
    int bits = 0;                   // uncompressed only
    quint32 masks[4] = {};          // R, G, B, A
    bool alpha = false;
    Layout layout = Layout::Masked;
};

// Scales the bits under mask to 0-255
int maskChannel(quint32 value, quint32 mask)
{
    if (!mask)
        return 0;
    const int shift = qCountTrailingZeroBits(mask);
    const int width = qPopulationCount(mask);
    const quint32 v = (value & mask) >> shift;
    if (width >= 8)
        return int(v >> (width - 8));
    return int(v * 255 / ((1u << width) - 1));
}

QImage decodeBlocks(const uchar* src, int width, int height, const DdsFormat& format)
{
    // Blocks are 4x4, so decode into a padded image and crop
    const int blocksWide = (width + 3) / 4;
    const int blocksHigh = (height + 3) / 4;
    QImage padded(blocksWide * 4, blocksHigh * 4, QImage::Format_RGBA8888);
    if (padded.isNull())
        return QImage();

    const int pitch = int(padded.bytesPerLine());
    for (int by = 0; by < blocksHigh; ++by) {
        uchar* row = padded.scanLine(by * 4);
        for (int bx = 0; bx < blocksWide; ++bx)
            format.decode(src + (size_t(by) * blocksWide + bx) * format.blockSize, row + bx * 16, pitch);
    }
    return padded.copy(0, 0, width, height);
}

QImage decodeUncompressed(const uchar* src, const uchar* palette, int width, int height, const DdsFormat& format)
{
    QImage image(width, height, format.alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    if (image.isNull())
        return QImage();

    const int bytesPerPixel = format.bits / 8;
    for (int y = 0; y < height; ++y) {
        const uchar* in = src + qsizetype(y) * width * bytesPerPixel;
        QRgb* out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x, in += bytesPerPixel) {
            quint32 value = 0;
            for (int b = 0; b < bytesPerPixel; ++b)
                value |= quint32(in[b]) << (8 * b);

            switch (format.layout) {
            case DdsFormat::Layout::Paletted: {
                const uchar* c = palette + value * 4;
                out[x] = qRgb(c[0], c[1], c[2]);
                break;
            }
            case DdsFormat::Layout::AlphaOnly: {
                const int v = maskChannel(value, format.masks[3] ? format.masks[3] : 0xFF);
                out[x] = qRgb(v, v, v);
                break;
            }
            case DdsFormat::Layout::Luminance: {
                const int v = maskChannel(value, format.masks[0] ? format.masks[0] : 0xFF);
                out[x] = qRgba(v, v, v, format.alpha ? maskChannel(value, format.masks[3]) : 255);
                break;
            }
            case DdsFormat::Layout::Masked:
                out[x] = qRgba(maskChannel(value, format.masks[0]), maskChannel(value, format.masks[1]),
                    maskChannel(value, format.masks[2]), format.alpha ? maskChannel(value, format.masks[3]) : 255);
                break;
            }
        }
    }
    return image;
}

// Fully opaque images do not need the checkerboard
QImage dropOpaqueAlpha(const QImage& image)
{
    if (!image.hasAlphaChannel())
        return image;
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) != 255)
                return argb;
        }
    }
    return argb.convertToFormat(QImage::Format_RGB32);
}

// Lays the faces out as a cross: +Y on top, -X +Z +X -Z across the middle, -Y below
QImage cubeCross(const std::vector<QImage>& faces)
{
    // DDS face order is +X, -X, +Y, -Y, +Z, -Z
    static const QPoint cells[6] = { { 2, 1 }, { 0, 1 }, { 1, 0 }, { 1, 2 }, { 1, 1 }, { 3, 1 } };

    QSize face = faces[0].size();
    if (face.width() > MaxCubeFace || face.height() > MaxCubeFace)
        face.scale(MaxCubeFace, MaxCubeFace, Qt::KeepAspectRatio);

    QImage cross(face.width() * 4, face.height() * 3, QImage::Format_ARGB32);
    if (cross.isNull())
        return QImage();
    cross.fill(Qt::transparent);

    QPainter painter(&cross);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int i = 0; i < 6; ++i)
        painter.drawImage(QRect(QPoint(cells[i].x() * face.width(), cells[i].y() * face.height()), face), faces[i]);
    return cross;
}

bool isPowerOfTwo(quint32 v)
{
    return v && !(v & (v - 1));
}

} // namespace

QImage ImageDecoders::decodeDds(const QByteArray& data, QString* details, QString* error, QSize* faceSize)
{
    constexpr int HeaderSize = 128;
    constexpr int Dx10HeaderSize = 20;
    constexpr int PaletteSize = 1024;
    if (data.size() < HeaderSize || std::memcmp(data.constData(), "DDS ", 4) != 0)
        return fail(error, tr("Not a DDS image."));

    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    const quint32 height = readU32(p + 12);
    const quint32 width = readU32(p + 16);
    const quint32 mipCount = std::clamp<quint32>(readU32(p + 28), 1, 32);
    const quint32 pfFlags = readU32(p + 80);
    const QByteArray fourCC(data.constData() + 84, 4);
    const quint32 bits = readU32(p + 88);
    const quint32 caps2 = readU32(p + 112);

    if (width == 0 || height == 0 || width > MaxDimension || height > MaxDimension)
        return fail(error, tr("The DDS header has invalid dimensions."));
    if (caps2 & Caps2Volume)
        return fail(error, tr("Volume textures are not supported."));
    bool cube = caps2 & Caps2Cubemap;
    if (cube && (caps2 & Caps2AllFaces) != Caps2AllFaces)
        return fail(error, tr("The cubemap does not have all six faces."));

    DdsFormat format;
    qint64 offset = HeaderSize;
    auto compressed = [&](const QString& name, BlockDecoder decode, int blockSize) {
        format.name = name;
        format.decode = decode;
        format.blockSize = blockSize;
    };
    auto uncompressed = [&](const QString& name, quint32 r, quint32 g, quint32 b, quint32 a) {
        format.name = name;
        format.bits = 32;
        format.masks[0] = r;
        format.masks[1] = g;
        format.masks[2] = b;
        format.masks[3] = a;
        format.alpha = a != 0;
    };

    if (pfFlags & DdpfFourCC) {
        if (fourCC == "DXT1") {
            compressed("DXT1", bcdec_bc1, BCDEC_BC1_BLOCK_SIZE);
        } else if (fourCC == "DXT3") {
            compressed("DXT3", bcdec_bc2, BCDEC_BC2_BLOCK_SIZE);
        } else if (fourCC == "DXT5") {
            compressed("DXT5", bcdec_bc3, BCDEC_BC3_BLOCK_SIZE);
        } else if (fourCC == "DXT2" || fourCC == "DXT4") {
            compressed(tr("%1 (premultiplied alpha)").arg(QString::fromLatin1(fourCC)),
                fourCC == "DXT2" ? bcdec_bc2 : bcdec_bc3, BCDEC_BC3_BLOCK_SIZE);
        } else if (fourCC == "DX10") {
            if (data.size() < HeaderSize + Dx10HeaderSize)
                return fail(error, tr("The DDS DX10 header is missing."));
            const quint32 dxgi = readU32(p + 128);
            if (readU32(p + 136) & Dx10MiscCubemap)
                cube = true;
            offset += Dx10HeaderSize;

            switch (dxgi) {
            case 70: case 71: compressed("BC1", bcdec_bc1, BCDEC_BC1_BLOCK_SIZE); break;
            case 72: compressed(tr("BC1 sRGB"), bcdec_bc1, BCDEC_BC1_BLOCK_SIZE); break;
            case 73: case 74: compressed("BC2", bcdec_bc2, BCDEC_BC2_BLOCK_SIZE); break;
            case 75: compressed(tr("BC2 sRGB"), bcdec_bc2, BCDEC_BC2_BLOCK_SIZE); break;
            case 76: case 77: compressed("BC3", bcdec_bc3, BCDEC_BC3_BLOCK_SIZE); break;
            case 78: compressed(tr("BC3 sRGB"), bcdec_bc3, BCDEC_BC3_BLOCK_SIZE); break;
            case 97: case 98: compressed("BC7", bcdec_bc7, BCDEC_BC7_BLOCK_SIZE); break;
            case 99: compressed(tr("BC7 sRGB"), bcdec_bc7, BCDEC_BC7_BLOCK_SIZE); break;
            case 28: case 29: uncompressed(tr("32-bit RGBA"), 0xFF, 0xFF00, 0xFF0000, 0xFF000000); break;
            case 87: case 91: uncompressed(tr("32-bit BGRA"), 0xFF0000, 0xFF00, 0xFF, 0xFF000000); break;
            case 88: case 93: uncompressed(tr("32-bit BGRX"), 0xFF0000, 0xFF00, 0xFF, 0); break;
            default:
                return fail(error, tr("Unsupported DXGI format %1.").arg(dxgi));
            }
        } else {
            return fail(error, tr("Unsupported DDS compression \"%1\".").arg(QString::fromLatin1(fourCC)));
        }
    } else {
        if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
            return fail(error, tr("Unsupported DDS pixel depth %1.").arg(bits));
        format.bits = int(bits);
        for (int i = 0; i < 4; ++i)
            format.masks[i] = readU32(p + 92 + 4 * i);
        format.alpha = pfFlags & DdpfAlphaPixels;

        if (pfFlags & DdpfPaletteIndexed8) {
            if (bits != 8)
                return fail(error, tr("Unsupported paletted DDS pixel depth %1.").arg(bits));
            format.layout = DdsFormat::Layout::Paletted;
            format.alpha = false;
            format.name = tr("8-bit paletted");
            offset += PaletteSize;
        } else if (pfFlags & DdpfLuminance) {
            format.layout = DdsFormat::Layout::Luminance;
            format.name = tr("%1-bit luminance").arg(bits);
        } else if (pfFlags & DdpfAlpha) {
            format.layout = DdsFormat::Layout::AlphaOnly;
            format.alpha = false;
            format.name = tr("%1-bit alpha only, shown as grayscale").arg(bits);
        } else {
            format.name = format.alpha ? tr("%1-bit uncompressed with alpha").arg(bits) : tr("%1-bit uncompressed").arg(bits);
        }
    }

    auto levelBytes = [&](quint32 w, quint32 h) -> qint64 {
        if (format.decode)
            return qint64(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4) * format.blockSize;
        return qint64(w) * h * (format.bits / 8);
    };

    // Each face holds its whole mipmap chain before the next face starts
    qint64 faceBytes = 0;
    for (quint32 i = 0, w = width, h = height; i < mipCount; ++i) {
        faceBytes += levelBytes(w, h);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    const int faces = cube ? 6 : 1;
    if (offset + (faces - 1) * faceBytes + levelBytes(width, height) > data.size())
        return fail(error, tr("The DDS image data ends early."));

    std::vector<QImage> images;
    for (int face = 0; face < faces; ++face) {
        const uchar* src = p + offset + face * faceBytes;
        QImage image = format.decode
            ? decodeBlocks(src, int(width), int(height), format)
            : decodeUncompressed(src, p + HeaderSize, int(width), int(height), format);
        if (image.isNull())
            return fail(error, tr("Not enough memory for a %1 x %2 image.").arg(width).arg(height));
        images.push_back(image);
    }

    const QImage result = cube ? cubeCross(images) : dropOpaqueAlpha(images.front());
    if (result.isNull())
        return fail(error, tr("Not enough memory for the cubemap."));

    if (faceSize)
        *faceSize = QSize(int(width), int(height));
    if (details) {
        QString text = format.name;
        text += mipCount > 1 ? tr(", %1 mipmaps").arg(mipCount) : tr(", no mipmaps");
        if (cube)
            text += tr(", cubemap");
        if (format.decode && !(isPowerOfTwo(width) && isPowerOfTwo(height)))
            text += tr(" (not a power of two, so the engine will not load it)");
        *details = text;
    }
    return result;
}
