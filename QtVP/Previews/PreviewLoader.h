#pragma once

#include "Previews/PreviewContent.h"

class VpArchive;

#include <vector>

namespace PreviewLoader {

// Reads and decodes an entry for display. Never fails: problems come back as a message.
// Thread-safe: it only reads the archive.
PreviewContent load(const VpArchive& archive, int entry);

// What an EFF file describes. frames holds the entries of its frame images, in order;
// on failure it is empty and error says why.
struct EffInfo
{
    QString type;
    int fps = 15;
    std::vector<int> frames;
    QString error;
};

EffInfo readEff(const VpArchive& archive, int entry);

} // namespace PreviewLoader
