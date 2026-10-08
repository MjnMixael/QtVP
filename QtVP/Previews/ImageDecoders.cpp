#include "ImageDecoders.h"

#include <QCoreApplication>

#include <algorithm>
#include <vector>

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
