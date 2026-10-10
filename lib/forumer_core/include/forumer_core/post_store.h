#pragma once

// The local post log: every verified post this device knows about, plus the
// outbox of posts this device wrote.
//
// Posts are immutable and content-addressed (see post.h), so the log is
// append-only: a post is stored once under its id and never changed or
// removed by anything that arrives later. That is what makes it safe to fill
// from untrusted peers — a stranger can add posts (which must still pass
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
#include <utility>
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

/// A reply to something this account wrote ("replies to you").
struct InboxItem {
    std::string postId;         // the reply
    std::string rootId;         // its topic
    std::string parentId;       // what it answers
    bool direct = false;        // answers one of our posts
    bool inMyTopic = false;     // sits in a topic we wrote (else: a thread we joined)
    bool read = false;
    int64_t timestampMs = 0;    // the author's clock
};

/// What one account wrote on this device and which replies it has read: the
/// private part of the log. Never stored in the post log file; the app keeps
/// it in the account's encrypted file (AccountStore::savePrivate) and loads
/// it into the store while the account is unlocked (importOwn).
struct OwnData {
    std::string accountId;
    std::vector<OutboxEntry> outbox;
    std::vector<std::pair<std::string, int64_t>> read;  // post id, read at

    std::string toJson() const;
    /// "" (no file yet) gives empty data; nullopt if the text is malformed.
    static std::optional<OwnData> fromJson(std::string_view text, const std::string& accountId);
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

    /// One page of the log in storage order, for handing it to the view in
    /// pieces (review F18): up to `limit` posts stored after `afterCursor`
    /// (0 for the first page). `next` is the cursor for the following page.
    struct Page {
        std::vector<post::Post> posts;
        int64_t next = 0;
        bool done = true;
    };
    Page page(int64_t afterCursor, size_t limit) const;

    /// Keep the log at most `maxPosts` posts: drop the oldest (by author
    /// timestamp) beyond that and compact the file. Returns how many went.
    size_t pruneTo(size_t maxPosts);

    /// Posts whose author timestamp is >= sinceMs, newest first.
    std::vector<PostSummary> recent(int64_t sinceMs) const;

    /// Posts dated in [fromMs, toMs), newest first (history windows).
    std::vector<PostSummary> range(int64_t fromMs, int64_t toMs) const;

    /// How many posts are dated in [fromMs, toMs).
    size_t countRange(int64_t fromMs, int64_t toMs) const;

    /// The author timestamp of the oldest post held, if any.
    std::optional<int64_t> oldestTimestamp() const;

    /// Small persistent settings of the log itself (e.g. how far back the
    /// history is complete). nullopt if unset.
    std::optional<std::string> meta(const std::string& key) const;
    bool setMeta(const std::string& key, const std::string& value);

    /// Every persona key that has signed a stored post (identity restore uses
    /// this to find personas already in use).
    std::set<Bytes> authors() const;

    size_t count() const;

    /// Posts of `kind` signed by `author` whose author timestamp lies in
    /// [fromMs, toMs]. The flood limits are counted with this.
    size_t countByAuthor(const Bytes& author, post::Kind kind, int64_t fromMs, int64_t toMs) const;

    /// Posts of `kind` signed by `author` that WE stored at or after
    /// sinceReceivedMs (our clock) - the arrival-time limit (flood.h, 2a).
    size_t countByAuthorArrival(const Bytes& author, post::Kind kind, int64_t sinceReceivedMs) const;

    /// Whether `author` has signed any stored post.
    bool hasAuthor(const Bytes& author) const;

    /// Posts of `kind` this device wrote for `accountId`, dated at or after
    /// sinceMs — the sender-side limit, which also covers anonymous and
    /// auto-rotated posts (a fresh key each, so no per-key count sees them).
    size_t countOwn(const std::string& accountId, post::Kind kind, int64_t sinceMs) const;

    /// The author timestamps of those same posts (for "next one in 23 min").
    std::vector<int64_t> ownTimestamps(const std::string& accountId, post::Kind kind, int64_t sinceMs) const;

    // Replies to you
    //
    // Worked out locally from the outbox: a reply is in an account's inbox
    // when the account didn't write it and it either answers one of the
    // account's posts, or is a newer reply in a thread the account takes part
    // in (wrote the topic, or replied there). Nothing is published, so nobody
    // else learns who was notified (everyone receives every post anyway).

    /// The account's inbox, newest first, at most `limit` items.
    std::vector<InboxItem> inbox(const std::string& accountId, size_t limit = 200) const;

    /// Unread items in the account's inbox.
    size_t unreadCount(const std::string& accountId) const;

    /// Mark replies read for the account. Ids not in its inbox are ignored.
    bool markRead(const std::string& accountId, const std::vector<std::string>& postIds, int64_t nowMs);

    /// Mark everything currently in the account's inbox read.
    bool markAllRead(const std::string& accountId, int64_t nowMs);

    /// Mark read every reply in the account's inbox dated before `beforeMs`
    /// (after a restore: those were seen on the device the account came from).
    bool markReadBefore(const std::string& accountId, int64_t beforeMs, int64_t nowMs);

    // Outbox
    //
    // The outbox and read marks are held in memory only (TEMP tables), for
    // the accounts whose data was imported with importOwn(). Nothing about
    // which posts this device wrote reaches the log file.

    /// Load an account's private data (on unlock). Merges with what's there.
    bool importOwn(const OwnData& data);
    /// An account's private data as it stands now (to save after a change).
    OwnData exportOwn(const std::string& accountId) const;
    /// Drop every account's private data from memory (on lock / switch).
    void forgetOwn();

    /// Logs written by Forumer 0.2.2 kept the outbox and read marks in clear
    /// (main.outbox, main.inbox_read). Remove one account's rows from there
    /// and return them, to be imported and saved encrypted.
    OwnData takeLegacy(const std::string& accountId);
    /// Whether any account's rows are still in the clear tables.
    bool legacyRowsLeft() const;
    /// Once no rows are left, drop the clear tables and VACUUM the file.
    void dropLegacyTablesIfEmpty();

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