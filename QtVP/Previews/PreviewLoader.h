#pragma once

#include "Previews/PreviewContent.h"

class VpArchive;

namespace PreviewLoader {

// Reads and decodes an entry for display. Never fails: problems come back as a message.
PreviewContent load(const VpArchive& archive, int entry);

} // namespace PreviewLoader
