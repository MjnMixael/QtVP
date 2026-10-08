#include "PreviewLoader.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>
#include <QStringDecoder>

#include <algorithm>

#include "Core/VpArchive.h"
#include "Previews/AnimationDecoders.h"
#include "Previews/ImageDecoders.h"

namespace {

constexpr int MaxEffFrames = 10000;

QString tr(const char* text)
{
    return QCoreApplication::translate("PreviewLoader", text);
}

QString qtImageDetails(const QImage& image)
{
    if (image.format() == QImage::Format_Indexed8)
        return tr("8-bit paletted");
    return image.hasAlphaChannel() ? tr("%1-bit with alpha").arg(image.depth()) : tr("%1-bit").arg(image.depth());
}

// *size is the size to report, which differs from the image for cubemaps
QImage decodeImage(const QString& ext, const QByteArray& data, QString* details, QString* error, QSize* size)
{
    QImage image;
    if (ext == "pcx") {
        image = ImageDecoders::decodePcx(data, details, error);
    } else if (ext == "tga") {
        image = ImageDecoders::decodeTga(data, details, error);
    } else if (ext == "dds") {
        image = ImageDecoders::decodeDds(data, details, error, size);
    } else {
        image = QImage::fromData(data);
        if (image.isNull())
            *error = tr("The image could not be decoded.");
        else
            *details = qtImageDetails(image);
    }

    if (!image.isNull() && !size->isValid())
        *size = image.size();
    return image;
}

PreviewContent imagePreview(const QString& ext, const QByteArray& data)
{
    QString details;
    QString error;
    QSize size;
    const QImage image = decodeImage(ext, data, &details, &error, &size);
    if (image.isNull())
        return PreviewContent::fromMessage(error);

    PreviewContent content;
    content.kind = PreviewContent::Kind::Image;
    content.image = image;
    content.info = tr("%1, %2 x %3, %4").arg(ext.toUpper()).arg(size.width()).arg(size.height()).arg(details);
    return content;
}

PreviewContent animationPreview(const QString& label, DecodedAnimation& animation)
{
    PreviewContent content;
    content.kind = PreviewContent::Kind::Animation;
    content.image = animation.frames.front();
    content.frames = std::move(animation.frames);
    content.durations = std::move(animation.durations);
    content.info = tr("%1, %2 x %3, %4").arg(label).arg(content.image.width()).arg(content.image.height()).arg(animation.details);
    return content;
}

// The engine finds files by name anywhere, but EFF frames normally sit next to the EFF
int findEntry(const VpArchive& archive, int folder, const QString& name)
{
    for (const int entry : archive.folders()[folder].entries) {
        if (archive.entries()[entry].name.compare(name, Qt::CaseInsensitive) == 0)
            return entry;
    }
    for (int entry = 0; entry < int(archive.entries().size()); ++entry) {
        if (archive.entries()[entry].name.compare(name, Qt::CaseInsensitive) == 0)
            return entry;
    }
    return -1;
}

PreviewContent effPreview(const VpArchive& archive, int entry)
{
    const PreviewLoader::EffInfo eff = PreviewLoader::readEff(archive, entry);
    if (!eff.error.isEmpty())
        return PreviewContent::fromMessage(eff.error);

    DecodedAnimation animation;
    for (const int frame : eff.frames) {
        const QString frameName = archive.entries()[frame].name;
        QString details;
        QString error;
        QSize size;
        const QByteArray frameData = archive.readEntry(frame, &error);
        const QImage image = error.isEmpty() ? decodeImage(eff.type, frameData, &details, &error, &size) : QImage();
        if (image.isNull())
            return PreviewContent::fromMessage(tr("Frame %1: %2").arg(frameName, error));
        animation.frames.push_back(image);
    }

    animation.durations.assign(animation.frames.size(), 1000 / eff.fps);
    animation.details = tr("%1 %2 frames at %3 fps").arg(eff.frames.size()).arg(eff.type.toUpper()).arg(eff.fps);
    return animationPreview("EFF", animation);
}

QString formatDuration(double seconds)
{
    if (seconds < 60)
        return tr("%1 s").arg(seconds, 0, 'f', 1);
    const int whole = int(seconds + 0.5);
    return QString("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QChar('0'));
}

QString channelText(int channels)
{
    return channels == 1 ? tr("mono") : channels == 2 ? tr("stereo") : tr("%1 channels").arg(channels);
}

quint32 readLe32(const char* p)
{
    const auto* u = reinterpret_cast<const uchar*>(p);
    return quint32(u[0]) | (quint32(u[1]) << 8) | (quint32(u[2]) << 16) | (quint32(u[3]) << 24);
}

quint16 readLe16(const char* p)
{
    const auto* u = reinterpret_cast<const uchar*>(p);
    return quint16(u[0] | (u[1] << 8));
}

// Codec, rate, channels, and length from the RIFF chunks
QString wavDetails(const QByteArray& data)
{
    if (data.size() < 12 || !data.startsWith("RIFF") || data.mid(8, 4) != "WAVE")
        return tr("not a RIFF WAVE file");

    int tag = 0, channels = 0, bits = 0;
    quint32 rate = 0, byteRate = 0, dataBytes = 0;
    qsizetype pos = 12;
    while (pos + 8 <= data.size()) {
        const QByteArray id = data.mid(pos, 4);
        const quint32 length = readLe32(data.constData() + pos + 4);
        if (id == "fmt " && length >= 16 && pos + 24 <= data.size()) {
            const char* fmt = data.constData() + pos + 8;
            tag = readLe16(fmt);
            channels = readLe16(fmt + 2);
            rate = readLe32(fmt + 4);
            byteRate = readLe32(fmt + 8);
            bits = readLe16(fmt + 14);
        } else if (id == "data") {
            dataBytes = quint32(std::min<qint64>(length, data.size() - pos - 8));
        }
        pos += 8 + qsizetype(length) + (length & 1);
    }

    QString codec;
    switch (tag) {
    case 0: return tr("no format chunk");
    case 1: codec = tr("PCM %1-bit").arg(bits); break;
    case 2: codec = tr("MS ADPCM"); break;
    case 0x11: codec = tr("IMA ADPCM"); break;
    default: codec = tr("codec 0x%1").arg(tag, 0, 16); break;
    }

    QString text = tr("%1, %2 Hz, %3").arg(codec).arg(rate).arg(channelText(channels));
    if (byteRate > 0)
        text += tr(", %1").arg(formatDuration(double(dataBytes) / byteRate));
    return text;
}

// Rate and channels from the Vorbis identification header; length from the last page's granule position
QString oggDetails(const QByteArray& data)
{
    if (data.size() < 28 || !data.startsWith("OggS"))
        return tr("not an Ogg file");

    const int segments = uchar(data[26]);
    const qsizetype packet = 27 + segments;
    if (data.size() < packet + 16 || data.mid(packet, 7) != QByteArray("\x01vorbis", 7))
        return tr("Ogg, not Vorbis");

    const int channels = uchar(data[packet + 11]);
    const quint32 rate = readLe32(data.constData() + packet + 12);
    QString text = tr("Vorbis, %1 Hz, %2").arg(rate).arg(channelText(channels));

    const qsizetype last = data.lastIndexOf("OggS");
    if (rate > 0 && last >= 0 && last + 14 <= data.size()) {
        const quint64 granule = quint64(readLe32(data.constData() + last + 6))
            | (quint64(readLe32(data.constData() + last + 10)) << 32);
        if (granule != ~quint64(0))
            text += tr(", %1").arg(formatDuration(double(granule) / rate));
    }
    return text;
}

// Tables, missions, scripts, shaders, and UI markup. Shown as UTF-8 when valid, otherwise Latin-1.
PreviewContent textPreview(const QString& ext, QByteArray data)
{
    constexpr qsizetype MaxTextBytes = 8 * 1024 * 1024;

    if (data.left(4096).contains('\0'))
        return PreviewContent::fromMessage(tr("This .%1 file is binary, not text.").arg(ext));

    const qsizetype fullSize = data.size();
    if (fullSize > MaxTextBytes)
        data.truncate(MaxTextBytes);

    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8(data);
    QString encoding = tr("UTF-8");
    if (utf8.hasError()) {
        text = QString::fromLatin1(data);
        encoding = tr("Latin-1");
    }

    PreviewContent content;
    content.kind = PreviewContent::Kind::Text;
    content.text = text;
    const qsizetype lines = text.count('\n') + (text.isEmpty() || text.endsWith('\n') ? 0 : 1);
    content.info = tr("%1, %2 lines, %3").arg(ext.toUpper()).arg(QLocale().toString(lines)).arg(encoding);
    if (fullSize > MaxTextBytes)
        content.info += tr(", showing the first %1 of %2").arg(QLocale().formattedDataSize(MaxTextBytes), QLocale().formattedDataSize(fullSize));
    return content;
}

PreviewContent soundPreview(const QString& ext, const QByteArray& data)
{
    PreviewContent content;
    content.kind = PreviewContent::Kind::Sound;
    content.audio = data;
    content.info = tr("%1, %2").arg(ext.toUpper(), ext == "wav" ? wavDetails(data) : oggDetails(data));
    return content;
}

} // namespace

PreviewContent PreviewLoader::load(const VpArchive& archive, int entry)
{
    const QString name = archive.entries()[entry].name;
    const QString ext = QFileInfo(name).suffix().toLower();

    static const QStringList textTypes{ "tbl", "tbm", "fs2", "fc2", "lua", "sdr", "vert", "frag", "geom",
        "rml", "rcss", "txt", "html" };
    static const QStringList previewTypes{ "png", "jpg", "jpeg", "pcx", "tga", "dds", "ani", "eff", "wav", "ogg" };
    if (!previewTypes.contains(ext) && !textTypes.contains(ext)) {
        PreviewContent content = PreviewContent::fromMessage(ext.isEmpty()
            ? tr("No preview for files without an extension")
            : tr("No preview for .%1 files").arg(ext));
        content.title = name;
        return content;
    }

    QString error;
    const QByteArray data = archive.readEntry(entry, &error);
    PreviewContent content;
    if (!error.isEmpty()) {
        content = PreviewContent::fromMessage(tr("Could not read the file: %1").arg(error));
    } else if (ext == "eff") {
        content = effPreview(archive, entry);
    } else if (ext == "wav" || ext == "ogg") {
        content = soundPreview(ext, data);
    } else if (textTypes.contains(ext)) {
        content = textPreview(ext, data);
    } else if (ext == "ani" || (ext == "png" && AnimationDecoders::isApng(data))) {
        DecodedAnimation animation;
        const bool ani = ext == "ani";
        const bool ok = ani ? AnimationDecoders::decodeAni(data, animation, &error)
                            : AnimationDecoders::decodeApng(data, animation, &error);
        content = ok ? animationPreview(ani ? "ANI" : "APNG", animation) : PreviewContent::fromMessage(error);
    } else {
        content = imagePreview(ext, data);
    }

    content.title = name;
    return content;
}

// An EFF is a small text file naming the frame type, count, and rate. The frames
// are separate images called <name>_0000.<type>, <name>_0001.<type>, and so on.
PreviewLoader::EffInfo PreviewLoader::readEff(const VpArchive& archive, int entry)
{
    EffInfo info;
    QString error;
    const QByteArray data = archive.readEntry(entry, &error);
    if (!error.isEmpty()) {
        info.error = tr("Could not read the file: %1").arg(error);
        return info;
    }

    int frameCount = 0;
    for (const QByteArray& raw : data.split('\n')) {
        const QString line = QString::fromLatin1(raw).trimmed();
        const QString value = line.section(':', 1).trimmed();
        if (line.startsWith("$Type:", Qt::CaseInsensitive))
            info.type = value.toLower();
        else if (line.startsWith("$Frames:", Qt::CaseInsensitive))
            frameCount = value.toInt();
        else if (line.startsWith("$FPS:", Qt::CaseInsensitive))
            info.fps = std::max(1, value.toInt());
    }

    static const QStringList frameTypes{ "dds", "pcx", "tga", "png", "jpg" };
    if (!frameTypes.contains(info.type)) {
        info.error = tr("The EFF file has an unsupported frame type \"%1\".").arg(info.type);
        return info;
    }
    if (frameCount <= 0 || frameCount > MaxEffFrames) {
        info.error = tr("The EFF file lists %1 frames.").arg(frameCount);
        return info;
    }

    const VpEntry& eff = archive.entries()[entry];
    const QString base = QFileInfo(eff.name).completeBaseName();
    for (int i = 0; i < frameCount; ++i) {
        const QString frameName = QString("%1_%2.%3").arg(base).arg(i, 4, 10, QChar('0')).arg(info.type);
        const int frame = findEntry(archive, eff.folder, frameName);
        if (frame < 0) {
            info.frames.clear();
            info.error = tr("Frame %1 is not in this VP.").arg(frameName);
            return info;
        }
        info.frames.push_back(frame);
    }
    return info;
}
