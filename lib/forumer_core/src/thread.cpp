#include "forumer_core/thread.h"

namespace forumer::thread {

using post::Kind;

Target replyTarget(const post::Post& answering) {
    const auto& c = answering.content;
    if (c.kind == Kind::Post)
        return {answering.id, answering.id};          // level 1 under the topic
    if (c.parent == c.root)
        return {c.root, answering.id};                // level 2 under this reply
    return {c.root, c.parent};                        // stay in the same sub-thread
}

int depthOf(const post::Post& p) {
    const auto& c = p.content;
    if (c.kind == Kind::Post) return 0;
    return c.parent == c.root ? 1 : 2;
}

const char* describe(Placement placement) {
    switch (placement) {
    case Placement::Ok:          return "ok";
    case Placement::WrongThread: return "reply's parent is in another thread";
    case Placement::TooDeep:     return "reply nested deeper than two levels";
    }
    return "unknown";
}

Placement checkPlacement(const post::Post& reply, const std::optional<post::Post>& parent) {
    const auto& c = reply.content;
    if (c.kind == Kind::Post) return Placement::Ok;
    if (!parent) return Placement::Ok;                // can't judge yet

    const auto& pc = parent->content;
    if (pc.forum != c.forum) return Placement::WrongThread;

    if (c.parent == c.root) {
        // Level 1: the parent IS the root, and must be a topic.
        return pc.kind == Kind::Post ? Placement::Ok : Placement::WrongThread;
    }

    // Level 2: the parent is a reply in the same thread...
    if (pc.kind != Kind::Reply || pc.root != c.root) return Placement::WrongThread;
    // ...and a level-1 one.
    if (pc.parent != pc.root) return Placement::TooDeep;
    return Placement::Ok;
}

} // namespace forumer::thread