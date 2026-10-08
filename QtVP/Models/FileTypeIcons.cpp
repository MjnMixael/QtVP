#include "FileTypeIcons.h"

#include <QFileInfo>
#include <QHash>

Icons::FileKind FileTypeIcons::kindOf(const QString& fileName)
{
    using Kind = Icons::FileKind;
    static const QHash<QString, Kind> kinds{
        { "pcx", Kind::Image }, { "tga", Kind::Image }, { "dds", Kind::Image }, { "png", Kind::Image },
        { "jpg", Kind::Image }, { "jpeg", Kind::Image }, { "bmp", Kind::Image }, { "ktx", Kind::Image },
        { "ani", Kind::Animation }, { "eff", Kind::Animation },
        { "wav", Kind::Sound }, { "ogg", Kind::Sound }, { "mp3", Kind::Sound }, { "flac", Kind::Sound },
        { "opus", Kind::Sound },
        { "mve", Kind::Movie }, { "mp4", Kind::Movie }, { "webm", Kind::Movie }, { "mkv", Kind::Movie },
        { "avi", Kind::Movie }, { "ogv", Kind::Movie },
        { "tbl", Kind::Text }, { "tbm", Kind::Text }, { "txt", Kind::Text }, { "cfg", Kind::Text },
        { "ini", Kind::Text }, { "xml", Kind::Text }, { "json", Kind::Text }, { "csv", Kind::Text },
        { "html", Kind::Text }, { "rml", Kind::Text }, { "rcss", Kind::Text },
        { "fs2", Kind::Mission }, { "fc2", Kind::Mission },
        { "pof", Kind::Model }, { "dae", Kind::Model },
        { "lua", Kind::Script }, { "sdr", Kind::Script }, { "vert", Kind::Script }, { "frag", Kind::Script },
        { "geom", Kind::Script }, { "glsl", Kind::Script },
        { "vf", Kind::Font }, { "ttf", Kind::Font }, { "otf", Kind::Font }, { "fnt", Kind::Font },
    };
    return kinds.value(QFileInfo(fileName).suffix().toLower(), Kind::Other);
}

QIcon FileTypeIcons::forFileName(const QString& fileName)
{
    static QHash<int, QIcon> cache;

    const Icons::FileKind kind = kindOf(fileName);
    auto found = cache.find(int(kind));
    if (found == cache.end())
        found = cache.insert(int(kind), Icons::fileIcon(kind));
    return found.value();
}
