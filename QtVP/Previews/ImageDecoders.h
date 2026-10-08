#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// Decoders for the image formats Qt does not read itself. Each returns a null
// image and sets *error on failure, and fills *details with a short format
// description such as "8-bit paletted".
namespace ImageDecoders {

QImage decodePcx(const QByteArray& data, QString* details, QString* error);
QImage decodeTga(const QByteArray& data, QString* details, QString* error);

} // namespace ImageDecoders
