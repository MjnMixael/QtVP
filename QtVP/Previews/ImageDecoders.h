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

// Shows the top mipmap. Cubemaps come back as a cross of all six faces, with
// *faceSize set to one face's size (otherwise it is the image size).
QImage decodeDds(const QByteArray& data, QString* details, QString* error, QSize* faceSize);

} // namespace ImageDecoders
