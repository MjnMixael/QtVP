#pragma once

#include <vector>

#include "Core/VpDocument.h"
#include "Previews/PreviewContent.h"

namespace PreviewLoader {

// Everything a preview needs, gathered on the UI thread. Loading then reads only
// through these plain values, so it never touches the document while it is edited.
struct Request
{
    QString name;
    VpFileSource source;
    qint64 size = 0;

    // For an EFF: its frame type and rate, and the frames' sources in order
    QString effType;
    int effFps = 15;
    std::vector<VpFileSource> effFrames;
    QString effError;
};

Request request(const VpDocument& document, int file);

// Reads and decodes for display. Never fails: problems come back as a message.
// Safe on any thread.
PreviewContent load(const Request& request);

// What an EFF file describes. frames holds the files of its frame images, in order;
// on failure it is empty and error says why.
struct EffInfo
{
    QString type;
    int fps = 15;
    std::vector<int> frames;
    QString error;
};

EffInfo readEff(const VpDocument& document, int file);

} // namespace PreviewLoader
