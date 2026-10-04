#pragma once

// Where replies sit in a thread.
//
// Threads are two levels deep:
//
//   topic
//   ├─ reply                  level 1: answers the topic
//   │  ├─ reply               level 2: answers a level-1 reply - its sub-thread
//   │  └─ reply               level 2 (answering a level-2 reply lands here too)
//   └─ reply                  level 1
//
// Replying to a level-2 reply doesn't open a third level: the new reply joins
// the same sub-thread, under the level-1 reply it belongs to. That keeps
// conversations readable and gives every sub-thread one clear starting point.
//
// Every reply envelope carries `root` (the topic) and `parent` (what it
// answers), so the rule can be checked by anyone holding the parent.

#include <optional>

#include "forumer_core/post.h"

namespace forumer::thread {

inline constexpr int kMaxDepth = 2;

/// root/parent for a new reply.
struct Target {
    std::string root;
    std::string parent;
};

/// Where a reply to `answering` goes: a topic → level 1 under it; a level-1
/// reply → level 2 under it; a level-2 reply → level 2 under ITS parent.
Target replyTarget(const post::Post& answering);

/// 1 for a reply that answers the topic, 2 otherwise (the caller has checked
/// placement), 0 for a topic.
int depthOf(const post::Post& post);

enum class Placement {
    Ok,           // fits the rules (or the parent isn't known yet)
    WrongThread,  // its parent belongs to a different topic, or isn't a post of this forum
    TooDeep,      // its parent is already a level-2 reply
};

const char* describe(Placement placement);

/// Check a received reply against its parent, when we hold the parent.
/// Topics always pass. With the parent unknown (it hasn't arrived yet) the
/// reply passes: it is shown at level 1 until the parent turns up.
Placement checkPlacement(const post::Post& reply, const std::optional<post::Post>& parent);

} // namespace forumer::thread