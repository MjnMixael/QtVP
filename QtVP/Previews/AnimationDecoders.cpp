#include "AnimationDecoders.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <cstring>

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("AnimationDecoders", text);
}

bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}

// Bounds-checked little-endian reader; reads past the end return 0 and clear ok
struct Reader
{
    const uchar* p;
    const uchar* end;
    bool ok = true;

    quint8 u8()
    {
        if (p >= end) {
            ok = false;
            return 0;
        }
        return *p++;
    }
    quint16 u16()
    {
        const quint16 lo = u8();
        return quint16(lo | (u8() << 8));
    }
    quint32 u32()
    {
        const quint32 lo = u16();
        return lo | (quint32(u16()) << 16);
    }
    void skip(qint64 count)
    {
        if (end - p < count) {
            ok = false;
            p = end;
        } else {
            p += count;
        }
    }
};

quint32 readBigU32(const uchar* p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint16 readBigU16(const uchar* p)
{
    return quint16((p[0] << 8) | p[1]);
}

void appendBigU32(QByteArray& out, quint32 v)
{
    out.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v));
}

quint32 crc32(const char* data, qsizetype length, quint32 crc = 0)
{
    static const auto table = [] {
        std::vector<quint32> t(256);
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();

    crc = ~crc;
    for (qsizetype i = 0; i < length; ++i)
        crc = table[(crc ^ uchar(data[i])) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void appendChunk(QByteArray& png, const char* type, const char* data, qsizetype length)
{
    appendBigU32(png, quint32(length));
    const qsizetype start = png.size();
    png.append(type, 4);
    png.append(data, length);
    appendBigU32(png, crc32(png.constData() + start, length + 4));
}

} // namespace

// Format notes from the engine's anim code: a header (older files start straight
// with the width), a 256-color palette, keyframe records, then one RLE-packed
// frame after another. Each frame starts with its packing method.
bool AnimationDecoders::decodeAni(const QByteArray& data, DecodedAnimation& out, QString* error)
{
    constexpr int PackerCode = 0xEE;        // the engine ignores the header's copy
    constexpr int UnchangedCode = 254;      // in delta frames, keeps the previous frame's pixel
    constexpr int StdRleCode = 0x80;
    constexpr int MaxFrames = 10000;

    Reader r{ reinterpret_cast<const uchar*>(data.constData()), reinterpret_cast<const uchar*>(data.constData()) + data.size() };

    int version = 0;
    int fps = 30;
    QRgb transparent = qRgb(0, 255, 0);
    int width = r.u16();
    if (width == 0) {
        version = r.u16();
        fps = r.u16();
        if (version >= 2) {
            const int red = r.u8();
            const int green = r.u8();
            transparent = qRgb(red, green, r.u8());
        }
        width = r.u16();
    }
    const int height = r.u16();
    const int frameCount = r.u16();
    r.u8();     // packer code

    if (!r.ok || width <= 0 || height <= 0 || width > 8192 || height > 8192 || frameCount <= 0 || frameCount > MaxFrames)
        return fail(error, tr("Not a valid ANI file."));

    // The first palette entry matching the transparent color is see-through
    QList<QRgb> palette(256);
    bool transparentFound = false;
    for (int i = 0; i < 256; ++i) {
        const int red = r.u8();
        const int green = r.u8();
        QRgb color = qRgb(red, green, r.u8());
        if (!transparentFound && color == transparent) {
            color = qRgba(red, green, qBlue(color), 0);
            transparentFound = true;
        }
        palette[i] = color;
    }

    const int keyCount = r.u16();
    r.skip(qint64(keyCount) * 6);
    r.u32();    // size of the compressed data
    if (!r.ok)
        return fail(error, tr("The ANI header ends early."));

    const qsizetype size = qsizetype(width) * height;
    std::vector<uchar> pixels(size, 0);
    out.frames.reserve(frameCount);
    QString damage;

    for (int frame = 0; frame < frameCount && damage.isEmpty(); ++frame) {
        const int method = r.u8();
        if (method > 3) {
            damage = tr("frame %1 has an unknown packing method (%2)").arg(frame).arg(method);
            break;
        }
        const bool key = method == 1 || method == 3;
        const bool standard = method >= 2;

        qsizetype pos = 0;
        while (pos < size) {
            int value = r.u8();
            qsizetype count = 1;
            if (standard && (value & StdRleCode)) {
                count = value & ~StdRleCode;
                value = r.u8();
            } else if (!standard && value == PackerCode) {
                count = r.u8();
                value = count < 2 ? PackerCode : r.u8();
                ++count;
            }
            if (!r.ok) {
                damage = tr("frame %1 ends early").arg(frame);
                break;
            }

            count = std::min(count, size - pos);
            if (key || value != UnchangedCode)
                std::fill_n(pixels.begin() + pos, count, uchar(value));
            pos += count;
        }
        if (!damage.isEmpty())
            break;

        QImage image(width, height, QImage::Format_Indexed8);
        if (image.isNull())
            return fail(error, tr("Not enough memory for the animation."));
        image.setColorTable(palette);
        for (int y = 0; y < height; ++y)
            std::memcpy(image.scanLine(y), pixels.data() + qsizetype(y) * width, width);
        out.frames.push_back(image);
    }

    // Damaged files still show the frames before the damage
    if (out.frames.empty())
        return fail(error, tr("The ANI file is damaged: %1.").arg(damage));

    const int duration = 1000 / std::max(1, fps);
    out.durations.assign(out.frames.size(), duration);
    out.details = tr("%1 frames at %2 fps, %3 keyframes").arg(frameCount).arg(fps).arg(keyCount);
    if (version < 2)
        out.details += tr(", version %1 header").arg(version);
    if (!damage.isEmpty())
        out.details += tr(". DAMAGED: %1, so only %2 frames are shown").arg(damage).arg(out.frames.size());
    return true;
}

bool AnimationDecoders::isApng(const QByteArray& data)
{
    if (data.size() < 8 || !data.startsWith("\x89PNG\r\n\x1a\n"))
        return false;

    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    qsizetype pos = 8;
    while (pos + 8 <= data.size()) {
        const quint32 length = readBigU32(p + pos);
        const char* type = data.constData() + pos + 4;
        if (std::memcmp(type, "acTL", 4) == 0)
            return true;
        if (std::memcmp(type, "IDAT", 4) == 0)
            return false;
        pos += qsizetype(length) + 12;
    }
    return false;
}

// Each frame is rebuilt as a standalone PNG (the shared header chunks plus that
// frame's data) for Qt to decode, then composited per the frame's dispose and blend ops
bool AnimationDecoders::decodeApng(const QByteArray& data, DecodedAnimation& out, QString* error)
{
    struct Frame
    {
        int width = 0, height = 0, x = 0, y = 0;
        int delayNum = 0, delayDen = 0;
        int dispose = 0, blend = 0;
        QByteArray idat;
    };

    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    QByteArray ihdr;
    QByteArray shared;      // complete chunks every frame needs, like PLTE and tRNS
    std::vector<Frame> frames;
    int plays = 0;
    bool seenImageData = false;

    qsizetype pos = 8;
    while (pos + 12 <= data.size()) {
        const quint32 length = readBigU32(p + pos);
        if (qint64(length) > data.size() - pos - 12)
            return fail(error, tr("A PNG chunk runs past the end of the file."));
        const QByteArray type(data.constData() + pos + 4, 4);
        const uchar* body = p + pos + 8;
        const qsizetype chunkSize = qsizetype(length) + 12;

        if (type == "IHDR") {
            ihdr = QByteArray(reinterpret_cast<const char*>(body), length);
        } else if (type == "acTL" && length >= 8) {
            plays = int(readBigU32(body + 4));
        } else if (type == "fcTL" && length >= 26) {
            Frame frame;
            frame.width = int(readBigU32(body + 4));
            frame.height = int(readBigU32(body + 8));
            frame.x = int(readBigU32(body + 12));
            frame.y = int(readBigU32(body + 16));
            frame.delayNum = readBigU16(body + 20);
            frame.delayDen = readBigU16(body + 22);
            frame.dispose = body[24];
            frame.blend = body[25];
            frames.push_back(frame);
        } else if (type == "IDAT") {
            seenImageData = true;
            // IDAT only belongs to the animation when an fcTL came before it
            if (!frames.empty())
                frames.back().idat.append(reinterpret_cast<const char*>(body), length);
        } else if (type == "fdAT" && length >= 4) {
            seenImageData = true;
            if (!frames.empty())
                frames.back().idat.append(reinterpret_cast<const char*>(body + 4), length - 4);
        } else if (type == "IEND") {
            break;
        } else if (!seenImageData && type != "acTL" && type != "fcTL") {
            shared.append(data.constData() + pos, chunkSize);
        }
        pos += chunkSize;
    }

    if (ihdr.size() < 13)
        return fail(error, tr("The PNG has no valid IHDR chunk."));
    frames.erase(std::remove_if(frames.begin(), frames.end(), [](const Frame& f) { return f.idat.isEmpty(); }), frames.end());
    if (frames.empty())
        return fail(error, tr("The APNG has no frames."));

    const int canvasWidth = int(readBigU32(reinterpret_cast<const uchar*>(ihdr.constData())));
    const int canvasHeight = int(readBigU32(reinterpret_cast<const uchar*>(ihdr.constData()) + 4));
    if (canvasWidth <= 0 || canvasHeight <= 0 || canvasWidth > 16384 || canvasHeight > 16384)
        return fail(error, tr("The APNG has invalid dimensions."));

    QImage canvas(canvasWidth, canvasHeight, QImage::Format_ARGB32_Premultiplied);
    if (canvas.isNull())
        return fail(error, tr("Not enough memory for the animation."));
    canvas.fill(Qt::transparent);

    for (size_t i = 0; i < frames.size(); ++i) {
        const Frame& f = frames[i];
        const QRect area(f.x, f.y, f.width, f.height);
        if (f.width <= 0 || f.height <= 0 || !canvas.rect().contains(area))
            return fail(error, tr("APNG frame %1 lies outside the image.").arg(i));

        QByteArray header = ihdr;
        header[0] = char(f.width >> 24); header[1] = char(f.width >> 16); header[2] = char(f.width >> 8); header[3] = char(f.width);
        header[4] = char(f.height >> 24); header[5] = char(f.height >> 16); header[6] = char(f.height >> 8); header[7] = char(f.height);

        QByteArray png("\x89PNG\r\n\x1a\n", 8);
        appendChunk(png, "IHDR", header.constData(), header.size());
        png.append(shared);
        appendChunk(png, "IDAT", f.idat.constData(), f.idat.size());
        appendChunk(png, "IEND", nullptr, 0);

        const QImage image = QImage::fromData(png, "PNG");
        if (image.isNull())
            return fail(error, tr("APNG frame %1 could not be decoded.").arg(i));

        // Dispose op 2 restores what was under the frame; on the first frame it acts like op 1
        const QImage saved = f.dispose == 2 ? canvas.copy(area) : QImage();

        QPainter painter(&canvas);
        painter.setCompositionMode(f.blend == 0 ? QPainter::CompositionMode_Source : QPainter::CompositionMode_SourceOver);
        painter.drawImage(area.topLeft(), image);
        painter.end();

        out.frames.push_back(canvas.convertToFormat(QImage::Format_ARGB32));
        const int den = f.delayDen == 0 ? 100 : f.delayDen;
        out.durations.push_back(std::max(10, f.delayNum * 1000 / den));

        if (f.dispose == 1 || (f.dispose == 2 && i == 0)) {
            QPainter clear(&canvas);
            clear.setCompositionMode(QPainter::CompositionMode_Source);
            clear.fillRect(area, Qt::transparent);
        } else if (f.dispose == 2) {
            QPainter restore(&canvas);
            restore.setCompositionMode(QPainter::CompositionMode_Source);
            restore.drawImage(area.topLeft(), saved);
        }
    }

    out.details = plays == 0 ? tr("%1 frames, loops forever").arg(out.frames.size())
                             : tr("%1 frames, plays %2 times").arg(out.frames.size()).arg(plays);
    return true;
}
