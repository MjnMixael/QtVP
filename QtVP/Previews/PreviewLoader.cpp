#include "PreviewLoader.h"

#include <QCoreApplication>
#include <QFileInfo>

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

// An EFF is a small text file naming the frame type, count, and rate. The frames
// are separate images called <name>_0000.<type>, <name>_0001.<type>, and so on.
PreviewContent effPreview(const VpArchive& archive, int entry, const QByteArray& data)
{
    QString type;
    int frameCount = 0;
    int fps = 15;
    for (const QByteArray& raw : data.split('\n')) {
        const QString line = QString::fromLatin1(raw).trimmed();
        const QString value = line.section(':', 1).trimmed();
        if (line.startsWith("$Type:", Qt::CaseInsensitive))
            type = value.toLower();
        else if (line.startsWith("$Frames:", Qt::CaseInsensitive))
            frameCount = value.toInt();
        else if (line.startsWith("$FPS:", Qt::CaseInsensitive))
            fps = std::max(1, value.toInt());
    }

    static const QStringList frameTypes{ "dds", "pcx", "tga", "png", "jpg" };
    if (!frameTypes.contains(type))
        return PreviewContent::fromMessage(tr("The EFF file has an unsupported frame type \"%1\".").arg(type));
    if (frameCount <= 0 || frameCount > MaxEffFrames)
        return PreviewContent::fromMessage(tr("The EFF file lists %1 frames.").arg(frameCount));

    const VpEntry& eff = archive.entries()[entry];
    const QString base = QFileInfo(eff.name).completeBaseName();

    DecodedAnimation animation;
    for (int i = 0; i < frameCount; ++i) {
        const QString frameName = QString("%1_%2.%3").arg(base).arg(i, 4, 10, QChar('0')).arg(type);
        const int frameEntry = findEntry(archive, eff.folder, frameName);
        if (frameEntry < 0)
            return PreviewContent::fromMessage(tr("Frame %1 is not in this VP.").arg(frameName));

        QString details;
        QString error;
        QSize size;
        const QByteArray frameData = archive.readEntry(frameEntry, &error);
        const QImage image = error.isEmpty() ? decodeImage(type, frameData, &details, &error, &size) : QImage();
        if (image.isNull())
            return PreviewContent::fromMessage(tr("Frame %1: %2").arg(frameName, error));
        animation.frames.push_back(image);
    }

    animation.durations.assign(animation.frames.size(), 1000 / fps);
    animation.details = tr("%1 %2 frames at %3 fps").arg(frameCount).arg(type.toUpper()).arg(fps);
    return animationPreview("EFF", animation);
}

} // namespace

PreviewContent PreviewLoader::load(const VpArchive& archive, int entry)
{
    const QString name = archive.entries()[entry].name;
    const QString ext = QFileInfo(name).suffix().toLower();

    static const QStringList previewTypes{ "png", "jpg", "jpeg", "pcx", "tga", "dds", "ani", "eff" };
    if (!previewTypes.contains(ext)) {
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
        content = effPreview(archive, entry, data);
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
