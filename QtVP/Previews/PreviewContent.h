#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <vector>

#include "Core/VpDocument.h"

// What a preview shows. Self-contained except for a movie, which still reads from the
// archive, so previews are cleared before an archive closes.
struct PreviewContent
{
    enum class Kind { Message, Image, Animation, Sound, Text, Movie };

    Kind kind = Kind::Message;
    QString title;      // the file name, or empty for a message
    QString message;
    QImage image;       // for an animation, the first frame
    QString info;       // one line under the preview, e.g. "TGA, 512 x 512, 32-bit"

    std::vector<QImage> frames;
    std::vector<int> durations;     // milliseconds, one per frame

    QByteArray audio;               // the whole file, played from memory

    QString text;

    // A movie is not read up front; the player streams it from here
    VpFileSource movie;

    static PreviewContent fromMessage(const QString& text)
    {
        PreviewContent content;
        content.message = text;
        return content;
    }
};
