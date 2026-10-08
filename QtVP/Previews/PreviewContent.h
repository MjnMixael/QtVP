#pragma once

#include <QImage>
#include <QString>

// What a preview shows. Self-contained, so it stays valid after the archive closes.
struct PreviewContent
{
    enum class Kind { Message, Image };

    Kind kind = Kind::Message;
    QString title;      // the file name, or empty for a message
    QString message;
    QImage image;
    QString info;       // one line under the preview, e.g. "TGA, 512 x 512, 32-bit"

    static PreviewContent fromMessage(const QString& text)
    {
        PreviewContent content;
        content.message = text;
        return content;
    }
};
