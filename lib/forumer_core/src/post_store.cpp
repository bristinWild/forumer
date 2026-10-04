#include "forumer_core/post_store.h"

#include <sqlite3.h>

#include <system_error>
#include <utility>

namespace forumer {

namespace {

constexpr const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
INSERT OR IGNORE INTO meta(key, value) VALUES ('schema', '1');

-- One row per verified post. `envelope` is the exact wire JSON; the other
-- columns are copies of envelope fields, kept only for indexing/ordering.
CREATE TABLE IF NOT EXISTS posts (
    id          TEXT PRIMARY KEY,
    kind        INTEGER NOT NULL,          -- 0 post, 1 reply
    forum       TEXT NOT NULL,
    root        TEXT NOT NULL,
    parent      TEXT NOT NULL,
    author      BLOB NOT NULL,             -- persona public key
    ts          INTEGER NOT NULL,          -- author's clock, ms
    received    INTEGER NOT NULL,          -- our clock, ms
    envelope    TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_posts_ts   ON posts(ts);
CREATE INDEX IF NOT EXISTS idx_posts_root ON posts(root);
CREATE INDEX IF NOT EXISTS idx_posts_author ON posts(author, ts);

-- Posts this device wrote, and how sending them is going.
CREATE TABLE IF NOT EXISTS outbox (
    post_id      TEXT PRIMARY KEY REFERENCES posts(id),
    account_id   TEXT NOT NULL,
    state        TEXT NOT NULL,
    attempts     INTEGER NOT NULL DEFAULT 0,
    created      INTEGER NOT NULL,
    last_attempt INTEGER NOT NULL DEFAULT 0,
    last_error   TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_outbox_state ON outbox(state);
)SQL";

// RAII prepared statement. Binding helpers return *this for chaining.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
            stmt_ = nullptr;
    }
    ~Stmt() { sqlite3_finalize(stmt_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool ok() const { return stmt_ != nullptr; }

    Stmt& text(int i, const std::string& v) {
        if (stmt_)
            sqlite3_bind_text(stmt_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& blob(int i, const Bytes& v) {
        if (stmt_)
            sqlite3_bind_blob(stmt_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& i64(int i, int64_t v) {
        if (stmt_)
            sqlite3_bind_int64(stmt_, i, v);
        return *this;
    }

    /// SQLITE_ROW, SQLITE_DONE, or an error code.
    int step() { return stmt_ ? sqlite3_step(stmt_) : SQLITE_ERROR; }

    std::string colText(int i) const {
        const auto* p = sqlite3_column_text(stmt_, i);
        return p ? std::string(reinterpret_cast<const char*>(p),
                               static_cast<size_t>(sqlite3_column_bytes(stmt_, i)))
                 : std::string();
    }
    Bytes colBlob(int i) const {
        const auto* p = static_cast<const uint8_t*>(sqlite3_column_blob(stmt_, i));
        return p ? Bytes(p, p + sqlite3_column_bytes(stmt_, i)) : Bytes();
    }
    int64_t colI64(int i) const { return sqlite3_column_int64(stmt_, i); }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

constexpr const char* kOutboxColumns =
    "post_id, account_id, state, attempts, created, last_attempt, last_error";

OutboxEntry readOutbox(const Stmt& s) {
    OutboxEntry e;
    e.postId = s.colText(0);
    e.accountId = s.colText(1);
    e.state = sendStateFrom(s.colText(2)).value_or(SendState::Failed);
    e.attempts = static_cast<int>(s.colI64(3));
    e.createdMs = s.colI64(4);
    e.lastAttemptMs = s.colI64(5);
    e.lastError = s.colText(6);
    return e;
}

std::vector<OutboxEntry> queryOutbox(sqlite3* db, const std::string& sql) {
    std::vector<OutboxEntry> out;
    Stmt s(db, sql.c_str());
    while (s.step() == SQLITE_ROW)
        out.push_back(readOutbox(s));
    return out;
}

} // namespace

//  SendState 

const char* toString(SendState state) {
    switch (state) {
    case SendState::Pending: return "pending";
    case SendState::Sent:    return "sent";
    case SendState::Failed:  return "failed";
    }
    return "failed";
}

std::optional<SendState> sendStateFrom(std::string_view text) {
    if (text == "pending") return SendState::Pending;
    if (text == "sent")    return SendState::Sent;
    if (text == "failed")  return SendState::Failed;
    return std::nullopt;
}

//  Lifecycle 

std::unique_ptr<PostStore> PostStore::open(const std::filesystem::path& file, std::string* error) {
    auto fail = [error](std::string why) -> std::unique_ptr<PostStore> {
        if (error)
            *error = std::move(why);
        return nullptr;
    };

    std::error_code ec;
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), ec);
    if (ec)
        return fail("can't create " + file.parent_path().string() + ": " + ec.message());

    sqlite3* db = nullptr;
    if (sqlite3_open(file.string().c_str(), &db) != SQLITE_OK) {
        std::string why = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db);
        return fail("can't open " + file.string() + ": " + why);
    }

    char* msg = nullptr;
    const char* pragmas = "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; PRAGMA busy_timeout=2000;";
    if (sqlite3_exec(db, pragmas, nullptr, nullptr, &msg) != SQLITE_OK ||
        sqlite3_exec(db, kSchema, nullptr, nullptr, &msg) != SQLITE_OK) {
        std::string why = msg ? msg : "schema setup failed";
        sqlite3_free(msg);
        sqlite3_close(db);
        return fail(why);
    }
    return std::unique_ptr<PostStore>(new PostStore(db));
}

PostStore::PostStore(sqlite3* db) : db_(db) {}

PostStore::~PostStore() { sqlite3_close(db_); }

//  Posts 

PostStore::Insert PostStore::insert(const post::Post& p, int64_t receivedMs) {
    std::lock_guard lock(mutex_);
    Stmt s(db_,
           "INSERT OR IGNORE INTO posts(id, kind, forum, root, parent, author, ts, received, envelope) "
           "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);");
    s.text(1, p.id)
        .i64(2, p.content.kind == post::Kind::Post ? 0 : 1)
        .text(3, p.content.forum)
        .text(4, p.content.root)
        .text(5, p.content.parent)
        .blob(6, p.publicKey)
        .i64(7, p.content.timestampMs)
        .i64(8, receivedMs)
        .text(9, p.toJson());
    if (s.step() != SQLITE_DONE)
        return Insert::Failed;
    return sqlite3_changes(db_) > 0 ? Insert::Added : Insert::Duplicate;
}

bool PostStore::contains(const std::string& id) const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "SELECT 1 FROM posts WHERE id = ?1;");
    s.text(1, id);
    return s.step() == SQLITE_ROW;
}

std::optional<post::Post> PostStore::get(const std::string& id) const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "SELECT envelope FROM posts WHERE id = ?1;");
    s.text(1, id);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return post::Post::fromJson(s.colText(0));
}

std::vector<post::Post> PostStore::all() const {
    std::lock_guard lock(mutex_);
    std::vector<post::Post> out;
    Stmt s(db_, "SELECT envelope FROM posts ORDER BY kind ASC, ts ASC, id ASC;");
    while (s.step() == SQLITE_ROW) {
        // A row that no longer parses (format change, disk damage) is skipped
        // rather than failing the whole read.
        if (auto p = post::Post::fromJson(s.colText(0)))
            out.push_back(std::move(*p));
    }
    return out;
}

std::vector<PostSummary> PostStore::recent(int64_t sinceMs) const {
    std::lock_guard lock(mutex_);
    std::vector<PostSummary> out;
    Stmt s(db_, "SELECT id, ts FROM posts WHERE ts >= ?1 ORDER BY ts DESC, id DESC;");
    s.i64(1, sinceMs);
    while (s.step() == SQLITE_ROW)
        out.push_back({s.colText(0), s.colI64(1)});
    return out;
}

size_t PostStore::countByAuthor(const Bytes& author, post::Kind kind, int64_t fromMs,
                                int64_t toMs) const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "SELECT COUNT(*) FROM posts WHERE author = ?1 AND kind = ?2 AND ts >= ?3 AND ts <= ?4;");
    s.blob(1, author).i64(2, kind == post::Kind::Post ? 0 : 1).i64(3, fromMs).i64(4, toMs);
    return s.step() == SQLITE_ROW ? static_cast<size_t>(s.colI64(0)) : 0;
}

bool PostStore::hasAuthor(const Bytes& author) const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "SELECT 1 FROM posts WHERE author = ?1 LIMIT 1;");
    s.blob(1, author);
    return s.step() == SQLITE_ROW;
}

size_t PostStore::countOwn(const std::string& accountId, post::Kind kind, int64_t sinceMs) const {
    std::lock_guard lock(mutex_);
    Stmt s(db_,
           "SELECT COUNT(*) FROM outbox o JOIN posts p ON p.id = o.post_id "
           "WHERE o.account_id = ?1 AND p.kind = ?2 AND p.ts >= ?3;");
    s.text(1, accountId).i64(2, kind == post::Kind::Post ? 0 : 1).i64(3, sinceMs);
    return s.step() == SQLITE_ROW ? static_cast<size_t>(s.colI64(0)) : 0;
}

std::vector<int64_t> PostStore::ownTimestamps(const std::string& accountId, post::Kind kind,
                                              int64_t sinceMs) const {
    std::lock_guard lock(mutex_);
    std::vector<int64_t> out;
    Stmt s(db_,
           "SELECT p.ts FROM outbox o JOIN posts p ON p.id = o.post_id "
           "WHERE o.account_id = ?1 AND p.kind = ?2 AND p.ts >= ?3 ORDER BY p.ts ASC;");
    s.text(1, accountId).i64(2, kind == post::Kind::Post ? 0 : 1).i64(3, sinceMs);
    while (s.step() == SQLITE_ROW)
        out.push_back(s.colI64(0));
    return out;
}

std::set<Bytes> PostStore::authors() const {
    std::lock_guard lock(mutex_);
    std::set<Bytes> out;
    Stmt s(db_, "SELECT DISTINCT author FROM posts;");
    while (s.step() == SQLITE_ROW)
        out.insert(s.colBlob(0));
    return out;
}

size_t PostStore::count() const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "SELECT COUNT(*) FROM posts;");
    return s.step() == SQLITE_ROW ? static_cast<size_t>(s.colI64(0)) : 0;
}

//  Outbox 

bool PostStore::enqueue(const std::string& postId, const std::string& accountId, int64_t nowMs) {
    std::lock_guard lock(mutex_);
    Stmt s(db_,
           "INSERT OR IGNORE INTO outbox(post_id, account_id, state, created) "
           "VALUES (?1, ?2, 'pending', ?3);");
    s.text(1, postId).text(2, accountId).i64(3, nowMs);
    return s.step() == SQLITE_DONE;  // fails if the post isn't stored (foreign key)
}

bool PostStore::markAttempt(const std::string& postId, int64_t nowMs) {
    std::lock_guard lock(mutex_);
    Stmt s(db_,
           "UPDATE outbox SET attempts = attempts + 1, last_attempt = ?2, state = 'pending' "
           "WHERE post_id = ?1;");
    s.text(1, postId).i64(2, nowMs);
    return s.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool PostStore::markState(const std::string& postId, SendState state, const std::string& error) {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "UPDATE outbox SET state = ?2, last_error = ?3 WHERE post_id = ?1;");
    s.text(1, postId)
        .text(2, toString(state))
        .text(3, state == SendState::Failed ? error : std::string());
    return s.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

std::optional<OutboxEntry> PostStore::outboxEntry(const std::string& postId) const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, (std::string("SELECT ") + kOutboxColumns + " FROM outbox WHERE post_id = ?1;").c_str());
    s.text(1, postId);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return readOutbox(s);
}

std::vector<OutboxEntry> PostStore::outbox() const {
    std::lock_guard lock(mutex_);
    return queryOutbox(db_, std::string("SELECT ") + kOutboxColumns +
                                " FROM outbox ORDER BY created DESC, post_id DESC;");
}

std::vector<OutboxEntry> PostStore::unsent() const {
    std::lock_guard lock(mutex_);
    return queryOutbox(db_, std::string("SELECT ") + kOutboxColumns +
                                " FROM outbox WHERE state != 'sent' ORDER BY created ASC, post_id ASC;");
}

} // namespace forumer