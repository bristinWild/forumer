#pragma once

// The local post log: every verified post this device knows about, plus the
// outbox of posts this device wrote.
//
// Posts are immutable and content-addressed (see post.h), so the log is
// append-only: a post is stored once under its id and never changed or
// removed by anything that arrives later. That is what makes it safe to fill
// from untrusted peers - a stranger can add posts (which must still pass
// verify()), but cannot overwrite or delete anyone else's.
//
// The caller verifies before inserting; the store trusts what it is given.
//
// Backed by SQLite (one file). Every method is thread-safe.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "forumer_core/bytes.h"
#include "forumer_core/post.h"

struct sqlite3;

namespace forumer {

/// Delivery state of a post this device wrote.
enum class SendState {
    Pending,  // queued or in flight; not yet confirmed by the network
    Sent,     // the network accepted it
    Failed,   // the last attempt failed; retried automatically (and by hand)
};

const char* toString(SendState state);
std::optional<SendState> sendStateFrom(std::string_view text);

/// One row of the outbox.
struct OutboxEntry {
    std::string postId;
    std::string accountId;      // which local account wrote it ("My posts")
    SendState state = SendState::Pending;
    int attempts = 0;           // sends so far (first send included)
    int64_t createdMs = 0;
    int64_t lastAttemptMs = 0;  // 0 = never attempted
    std::string lastError;
};

/// The minimum the sync protocol needs to know about a stored post.
struct PostSummary {
    std::string id;
    int64_t timestampMs = 0;  // the author's clock
};

class PostStore {
public:
    /// Open (or create) the log at `file`. Parent directories are created.
    /// nullptr on failure, with a reason in `error` if given.
    static std::unique_ptr<PostStore> open(const std::filesystem::path& file,
                                           std::string* error = nullptr);
    ~PostStore();

    PostStore(const PostStore&) = delete;
    PostStore& operator=(const PostStore&) = delete;

    //  Posts

    enum class Insert {
        Added,      // new post, now stored
        Duplicate,  // already had it: nothing changed
        Failed,     // storage error
    };

    /// Store a (verified) post. First copy wins; later copies are ignored.
    Insert insert(const post::Post& post, int64_t receivedMs);

    bool contains(const std::string& id) const;
    std::optional<post::Post> get(const std::string& id) const;

    /// Every stored post: topics first, then replies, each oldest first by
    /// the author's timestamp (ties broken by id, so the order is stable).
    std::vector<post::Post> all() const;

    /// Posts whose author timestamp is >= sinceMs, newest first.
    std::vector<PostSummary> recent(int64_t sinceMs) const;

    /// Every persona key that has signed a stored post (identity restore uses
    /// this to find personas already in use).
    std::set<Bytes> authors() const;

    size_t count() const;

    /// Posts of `kind` signed by `author` whose author timestamp lies in
    /// [fromMs, toMs]. The flood limits are counted with this.
    size_t countByAuthor(const Bytes& author, post::Kind kind, int64_t fromMs, int64_t toMs) const;

    /// Whether `author` has signed any stored post.
    bool hasAuthor(const Bytes& author) const;

    /// Posts of `kind` this device wrote for `accountId`, dated at or after
    /// sinceMs - the sender-side limit, which also covers anonymous and
    /// auto-rotated posts (a fresh key each, so no per-key count sees them).
    size_t countOwn(const std::string& accountId, post::Kind kind, int64_t sinceMs) const;

    // Outbox 

    /// Record that this device wrote `postId` (state Pending, 0 attempts).
    /// The post itself must already be stored. Idempotent.
    bool enqueue(const std::string& postId, const std::string& accountId, int64_t nowMs);

    /// Count a send attempt starting now. State becomes Pending.
    bool markAttempt(const std::string& postId, int64_t nowMs);

    /// Settle the latest attempt. `error` is kept for Failed and cleared otherwise.
    bool markState(const std::string& postId, SendState state, const std::string& error = {});

    std::optional<OutboxEntry> outboxEntry(const std::string& postId) const;

    /// Every outbox row, newest first.
    std::vector<OutboxEntry> outbox() const;

    /// Rows not yet Sent (Pending or Failed), oldest first.
    std::vector<OutboxEntry> unsent() const;

private:
    explicit PostStore(sqlite3* db);

    sqlite3* db_;
    mutable std::mutex mutex_;
};

} // namespace forumer