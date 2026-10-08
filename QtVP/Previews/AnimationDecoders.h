#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <vector>

struct DecodedAnimation
{
    std::vector<QImage> frames;
    std::vector<int> durations;     // milliseconds, one per frame
    QString details;                // e.g. "30 frames at 15 fps"
};

// Each returns false and sets *error on failure
namespace AnimationDecoders {

bool decodeAni(const QByteArray& data, DecodedAnimation& out, QString* error);

// True when a PNG has an acTL chunk before its image data
bool isApng(const QByteArray& data);
bool decodeApng(const QByteArray& data, DecodedAnimation& out, QString* error);

} // namespace AnimationDecoders
