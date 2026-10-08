#include "PreviewLoader.h"

#include <QCoreApplication>
#include <QFileInfo>

#include "Core/VpArchive.h"
#include "Previews/ImageDecoders.h"

namespace {

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

PreviewContent imagePreview(const QString& ext, const QByteArray& data)
{
    QString details;
    QString error;
    QImage image;
    QSize size;

    if (ext == "pcx") {
        image = ImageDecoders::decodePcx(data, &details, &error);
    } else if (ext == "tga") {
        image = ImageDecoders::decodeTga(data, &details, &error);
    } else if (ext == "dds") {
        image = ImageDecoders::decodeDds(data, &details, &error, &size);
    } else {
        image = QImage::fromData(data);
        if (image.isNull())
            error = tr("The image could not be decoded.");
        else
            details = qtImageDetails(image);
    }

    if (image.isNull())
        return PreviewContent::fromMessage(error);

    PreviewContent content;
    content.kind = PreviewContent::Kind::Image;
    content.image = image;
    if (!size.isValid())
        size = image.size();
    content.info = tr("%1, %2 x %3, %4").arg(ext.toUpper()).arg(size.width()).arg(size.height()).arg(details);
    return content;
}

} // namespace

PreviewContent PreviewLoader::load(const VpArchive& archive, int entry)
{
    const QString name = archive.entries()[entry].name;
    const QString ext = QFileInfo(name).suffix().toLower();

    static const QStringList imageTypes{ "png", "jpg", "jpeg", "pcx", "tga", "dds" };
    if (!imageTypes.contains(ext)) {
        PreviewContent content = PreviewContent::fromMessage(ext.isEmpty()
            ? tr("No preview for files without an extension")
            : tr("No preview for .%1 files").arg(ext));
        content.title = name;
        return content;
    }

    QString error;
    const QByteArray data = archive.readEntry(entry, &error);
    PreviewContent content = error.isEmpty()
        ? imagePreview(ext, data)
        : PreviewContent::fromMessage(tr("Could not read the file: %1").arg(error));
    content.title = name;
    return content;
}
