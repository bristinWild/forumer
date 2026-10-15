#pragma once

// Log-safe references (0.2.4, Phase A). A debug log that is attached to a bug
// report must not carry full identifiers: account ids and persona fingerprints
// link a line to an identity, and post ids, content ids (CIDs) and private
// forum ids let a reader correlate one device's posts with its personas. Every
// log line passes through scrubIds(), so no call site has to remember to.

#include <cctype>
#include <cstddef>
#include <string>

namespace forumer {

/// The first `keep` characters, then "...". Shorter strings are returned as is.
inline std::string shortRef(const std::string& s, std::size_t keep = 8) {
    if (s.size() <= keep)
        return s;
    return s.substr(0, keep) + "...";
}

/// Every run of 20 or more letters and digits (a hash, post id, CID, forum id,
/// key) is shortened with shortRef. Words, numbers of a few digits, and
/// timestamps in milliseconds (13 digits) are left as they are.
inline std::string scrubIds(const std::string& text, std::size_t minRun = 20) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (!std::isalnum(static_cast<unsigned char>(text[i]))) {
            out += text[i++];
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && std::isalnum(static_cast<unsigned char>(text[j])))
            ++j;
        out += (j - i >= minRun) ? shortRef(text.substr(i, j - i)) : text.substr(i, j - i);
        i = j;
    }
    return out;
}

} // namespace forumer