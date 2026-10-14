#pragma once

// Log-safe references (0.2.4, Phase A). Account ids and persona fingerprints
// link a log line to an identity; a debug log that is attached to a bug report
// should not carry them in full. Post ids are public (they travel in every
// digest), so logs may keep those.

#include <cstddef>
#include <string>

namespace forumer {

/// The first `keep` characters, then "...". Shorter strings are returned as is.
inline std::string shortRef(const std::string& s, std::size_t keep = 8) {
    if (s.size() <= keep)
        return s;
    return s.substr(0, keep) + "...";
}

} // namespace forumer