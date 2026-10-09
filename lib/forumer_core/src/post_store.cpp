#include "forumer_core/post_store.h"

#include <sqlite3.h>

#include <nlohmann/json.hpp>

#include <cstdio>
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

CREATE INDEX IF NOT EXISTS idx_posts_parent ON posts(parent);
)SQL";

// What a device wrote, and which replies an account has read, never touch
// this file: they live in TEMP tables (memory only, see temp_store), filled
// from the unlocked account's encrypted file and cleared on lock. Forumer
// 0.2.2 kept them in main.outbox / main.inbox_read in clear; takeLegacy()
// moves an account's rows out of those as it unlocks.
constexpr const char* kTempSchema = R"SQL(
PRAGMA temp_store = MEMORY;
CREATE TEMP TABLE IF NOT EXISTS own_outbox (
    post_id      TEXT PRIMARY KEY,
    account_id   TEXT NOT NULL,
    state        TEXT NOT NULL,
    attempts     INTEGER NOT NULL DEFAULT 0,
    created      INTEGER NOT NULL,
    last_attempt INTEGER NOT NULL DEFAULT 0,
    last_error   TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS temp.idx_own_account ON own_outbox(account_id);
CREATE TEMP TABLE IF NOT EXISTS own_read (
    account_id TEXT NOT NULL,
    post_id    TEXT NOT NULL,
    read_at    INTEGER NOT NULL,
    PRIMARY KEY (account_id, post_id)
);
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
    sqlite3_stmt* raw() const { return stmt_; }

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

std::vector<OutboxEntry> queryOutbox(sqlite3* db, const std::string& sql,
                                     const std::string& accountId = {}) {
    std::vector<OutboxEntry> out;
    Stmt s(db, sql.c_str());
    if (!accountId.empty()) s.text(1, accountId);
    while (s.step() == SQLITE_ROW)
        out.push_back(readOutbox(s));
    return out;
}

bool tableExists(sqlite3* db, const char* name) {
    Stmt s(db, "SELECT 1 FROM main.sqlite_master WHERE type = 'table' AND name = ?1;");
    s.text(1, name);
    return s.step() == SQLITE_ROW;
}

// Owner-only (0700 directory, 0600 files). SQLite gives the -wal and -shm
// files the database file's own permissions, so restricting it first covers
// them; any that already exist are restricted too.
void restrictFiles(const std::filesystem::path& file) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (file.has_parent_path())
        fs::permissions(file.parent_path(), fs::perms::owner_all, fs::perm_options::replace, ec);
    for (const char* suffix : {"", "-wal", "-shm"}) {
        fs::path f = file;
        f += suffix;
        if (fs::exists(f, ec))
            fs::permissions(f, fs::perms::owner_read | fs::perms::owner_write,
                            fs::perm_options::replace, ec);
    }
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
    // Create the file empty and owner-only before SQLite writes anything.
    if (!std::filesystem::exists(file, ec)) {
        if (FILE* f = std::fopen(file.string().c_str(), "ab")) std::fclose(f);
    }
    restrictFiles(file);

    sqlite3* db = nullptr;
    if (sqlite3_open(file.string().c_str(), &db) != SQLITE_OK) {
        std::string why = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db);
        return fail("can't open " + file.string() + ": " + why);
    }

    char* msg = nullptr;
    const char* pragmas = "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; PRAGMA busy_timeout=2000;";
    if (sqlite3_exec(db, pragmas, nullptr, nullptr, &msg) != SQLITE_OK ||
        sqlite3_exec(db, kSchema, nullptr, nullptr, &msg) != SQLITE_OK ||
        sqlite3_exec(db, kTempSchema, nullptr, nullptr, &msg) != SQLITE_OK) {
        std::string why = msg ? msg : "schema setup failed";
        sqlite3_free(msg);
        sqlite3_close(db);
        return fail(why);
    }
    restrictFiles(file);
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

std::vector<PostSummary> PostStore::range(int64_t fromMs, int64_t toMs) const {
    std::lock_guard lock(mutex_);
    std::vector<PostSummary> out;
    Stmt s(db_, "SELECT id, ts FROM posts WHERE ts >= ?1 AND ts < ?2 ORDER BY ts DESC, id DESC;");
    s.i64(1, fromMs).i64(2, toMs);
    while (s.step() == SQLITE_ROW)
        out.push_back({s.colText(0), s.colI64(1)});
    return out;
}

size_t PostStore::countRange(int64_t fromMs, int64_t toMs) const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "SELECT COUNT(*) FROM posts WHERE ts >= ?1 AND ts < ?2;");
    s.i64(1, fromMs).i64(2, toMs);
    return s.step() == SQLITE_ROW ? static_cast<size_t>(s.colI64(0)) : 0;
}

std::optional<int64_t> PostStore::oldestTimestamp() const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "SELECT MIN(ts) FROM posts;");
    if (s.step() != SQLITE_ROW || sqlite3_column_type(s.raw(), 0) == SQLITE_NULL)
        return std::nullopt;
    return s.colI64(0);
}

std::optional<std::string> PostStore::meta(const std::string& key) const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "SELECT value FROM meta WHERE key = ?1;");
    s.text(1, key);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return s.colText(0);
}

bool PostStore::setMeta(const std::string& key, const std::string& value) {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "INSERT INTO meta(key, value) VALUES (?1, ?2) "
                "ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
    s.text(1, key).text(2, value);
    return s.step() == SQLITE_DONE;
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
           "SELECT COUNT(*) FROM own_outbox o JOIN posts p ON p.id = o.post_id "
           "WHERE o.account_id = ?1 AND p.kind = ?2 AND p.ts >= ?3;");
    s.text(1, accountId).i64(2, kind == post::Kind::Post ? 0 : 1).i64(3, sinceMs);
    return s.step() == SQLITE_ROW ? static_cast<size_t>(s.colI64(0)) : 0;
}

std::vector<int64_t> PostStore::ownTimestamps(const std::string& accountId, post::Kind kind,
                                              int64_t sinceMs) const {
    std::lock_guard lock(mutex_);
    std::vector<int64_t> out;
    Stmt s(db_,
           "SELECT p.ts FROM own_outbox o JOIN posts p ON p.id = o.post_id "
           "WHERE o.account_id = ?1 AND p.kind = ?2 AND p.ts >= ?3 ORDER BY p.ts ASC;");
    s.text(1, accountId).i64(2, kind == post::Kind::Post ? 0 : 1).i64(3, sinceMs);
    while (s.step() == SQLITE_ROW)
        out.push_back(s.colI64(0));
    return out;
}

//  Replies to you 

namespace {

// Replies (kind 1) that concern the account, ?1 = account id:
//   - answering one of its posts (direct), or
//   - in a thread it takes part in (it wrote the topic or a reply there),
//     dated after its first post in that thread — joining a long thread
//     doesn't flood the inbox with the replies that were already there.
// Its own replies are never in its inbox.
constexpr const char* kMine = "(SELECT post_id FROM own_outbox WHERE account_id = ?1)";

std::string inboxWhere() {
    return std::string("p.kind = 1 "
                       "AND p.id NOT IN ") + kMine + " "
           "AND (p.parent IN " + kMine + " "
           "  OR p.ts > (SELECT MIN(q.ts) FROM posts q WHERE q.id IN " + kMine + " "
           "             AND (q.id = p.root OR q.root = p.root)))";
}

} // namespace

std::vector<InboxItem> PostStore::inbox(const std::string& accountId, size_t limit) const {
    std::lock_guard lock(mutex_);
    std::vector<InboxItem> out;
    const std::string sql =
        std::string("SELECT p.id, p.root, p.parent, p.ts, "
                    "  p.parent IN ") + kMine + ", "
                    "  p.root IN " + kMine + ", "
                    "  EXISTS (SELECT 1 FROM own_read r WHERE r.account_id = ?1 AND r.post_id = p.id) "
                    "FROM posts p WHERE " +
        inboxWhere() + " ORDER BY p.ts DESC, p.id LIMIT ?2;";
    Stmt s(db_, sql.c_str());
    s.text(1, accountId).i64(2, static_cast<int64_t>(limit));
    while (s.step() == SQLITE_ROW) {
        InboxItem item;
        item.postId = s.colText(0);
        item.rootId = s.colText(1);
        item.parentId = s.colText(2);
        item.timestampMs = s.colI64(3);
        item.direct = s.colI64(4) != 0;
        item.inMyTopic = s.colI64(5) != 0;
        item.read = s.colI64(6) != 0;
        out.push_back(std::move(item));
    }
    return out;
}

size_t PostStore::unreadCount(const std::string& accountId) const {
    std::lock_guard lock(mutex_);
    const std::string sql =
        std::string("SELECT COUNT(*) FROM posts p WHERE ") + inboxWhere() +
        " AND NOT EXISTS (SELECT 1 FROM own_read r WHERE r.account_id = ?1 AND r.post_id = p.id);";
    Stmt s(db_, sql.c_str());
    s.text(1, accountId);
    return s.step() == SQLITE_ROW ? static_cast<size_t>(s.colI64(0)) : 0;
}

bool PostStore::markRead(const std::string& accountId, const std::vector<std::string>& postIds,
                         int64_t nowMs) {
    std::lock_guard lock(mutex_);
    const std::string sql =
        std::string("INSERT OR IGNORE INTO own_read(account_id, post_id, read_at) "
                    "SELECT ?1, p.id, ?3 FROM posts p WHERE p.id = ?2 AND ") +
        inboxWhere() + ";";
    bool ok = true;
    for (const auto& id : postIds) {
        Stmt s(db_, sql.c_str());
        s.text(1, accountId).text(2, id).i64(3, nowMs);
        ok = s.step() == SQLITE_DONE && ok;
    }
    return ok;
}

bool PostStore::markAllRead(const std::string& accountId, int64_t nowMs) {
    std::lock_guard lock(mutex_);
    const std::string sql =
        std::string("INSERT OR IGNORE INTO own_read(account_id, post_id, read_at) "
                    "SELECT ?1, p.id, ?2 FROM posts p WHERE ") +
        inboxWhere() + ";";
    Stmt s(db_, sql.c_str());
    s.text(1, accountId).i64(2, nowMs);
    return s.step() == SQLITE_DONE;
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
    {
        Stmt exists(db_, "SELECT 1 FROM posts WHERE id = ?1;");
        exists.text(1, postId);
        if (exists.step() != SQLITE_ROW) return false;  // the post must be stored first
    }
    Stmt s(db_,
           "INSERT OR IGNORE INTO own_outbox(post_id, account_id, state, created) "
           "VALUES (?1, ?2, 'pending', ?3);");
    s.text(1, postId).text(2, accountId).i64(3, nowMs);
    return s.step() == SQLITE_DONE;
}

bool PostStore::markAttempt(const std::string& postId, int64_t nowMs) {
    std::lock_guard lock(mutex_);
    Stmt s(db_,
           "UPDATE own_outbox SET attempts = attempts + 1, last_attempt = ?2, state = 'pending' "
           "WHERE post_id = ?1;");
    s.text(1, postId).i64(2, nowMs);
    return s.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool PostStore::markState(const std::string& postId, SendState state, const std::string& error) {
    std::lock_guard lock(mutex_);
    Stmt s(db_, "UPDATE own_outbox SET state = ?2, last_error = ?3 WHERE post_id = ?1;");
    s.text(1, postId)
        .text(2, toString(state))
        .text(3, state == SendState::Failed ? error : std::string());
    return s.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

std::optional<OutboxEntry> PostStore::outboxEntry(const std::string& postId) const {
    std::lock_guard lock(mutex_);
    Stmt s(db_, (std::string("SELECT ") + kOutboxColumns + " FROM own_outbox WHERE post_id = ?1;").c_str());
    s.text(1, postId);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return readOutbox(s);
}

std::vector<OutboxEntry> PostStore::outbox() const {
    std::lock_guard lock(mutex_);
    return queryOutbox(db_, std::string("SELECT ") + kOutboxColumns +
                                " FROM own_outbox ORDER BY created DESC, post_id DESC;");
}

std::vector<OutboxEntry> PostStore::unsent() const {
    std::lock_guard lock(mutex_);
    return queryOutbox(db_, std::string("SELECT ") + kOutboxColumns +
                                " FROM own_outbox WHERE state != 'sent' ORDER BY created ASC, post_id ASC;");
}

//  Own data: export, import, legacy

namespace {

using json = nlohmann::json;
constexpr int kOwnDataVersion = 1;

} // namespace

std::string OwnData::toJson() const {
    json posts = json::array();
    for (const auto& e : outbox)
        posts.push_back({{"id", e.postId},
                         {"state", toString(e.state)},
                         {"attempts", e.attempts},
                         {"created", e.createdMs},
                         {"last", e.lastAttemptMs},
                         {"error", e.lastError}});
    json reads = json::array();
    for (const auto& [id, at] : read)
        reads.push_back({{"id", id}, {"at", at}});
    return json{{"v", kOwnDataVersion}, {"posts", posts}, {"read", reads}}.dump();
}

std::optional<OwnData> OwnData::fromJson(std::string_view text, const std::string& accountId) {
    OwnData data;
    data.accountId = accountId;
    if (text.empty()) return data;  // no file yet
    const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("v", 0) != kOwnDataVersion)
        return std::nullopt;
    try {
        for (const auto& p : doc.value("posts", json::array())) {
            OutboxEntry e;
            e.postId = p.at("id").get<std::string>();
            e.accountId = accountId;
            e.state = sendStateFrom(p.value("state", std::string())).value_or(SendState::Failed);
            e.attempts = p.value("attempts", 0);
            e.createdMs = p.value("created", int64_t(0));
            e.lastAttemptMs = p.value("last", int64_t(0));
            e.lastError = p.value("error", std::string());
            if (!e.postId.empty()) data.outbox.push_back(std::move(e));
        }
        for (const auto& r : doc.value("read", json::array()))
            data.read.emplace_back(r.at("id").get<std::string>(), r.value("at", int64_t(0)));
    } catch (const json::exception&) {
        return std::nullopt;
    }
    return data;
}

OwnData PostStore::exportOwn(const std::string& accountId) const {
    OwnData data;
    data.accountId = accountId;
    {
        std::lock_guard lock(mutex_);
        data.outbox = queryOutbox(db_,
                                  std::string("SELECT ") + kOutboxColumns +
                                      " FROM own_outbox WHERE account_id = ?1 ORDER BY created ASC, post_id ASC;",
                                  accountId);
        Stmt s(db_, "SELECT post_id, read_at FROM own_read WHERE account_id = ?1 ORDER BY read_at, post_id;");
        s.text(1, accountId);
        while (s.step() == SQLITE_ROW)
            data.read.emplace_back(s.colText(0), s.colI64(1));
    }
    return data;
}

bool PostStore::importOwn(const OwnData& data) {
    std::lock_guard lock(mutex_);
    bool ok = sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr) == SQLITE_OK;
    for (const auto& e : data.outbox) {
        Stmt s(db_,
               "INSERT OR REPLACE INTO own_outbox(post_id, account_id, state, attempts, created, "
               "last_attempt, last_error) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7);");
        s.text(1, e.postId)
            .text(2, data.accountId)
            .text(3, toString(e.state))
            .i64(4, e.attempts)
            .i64(5, e.createdMs)
            .i64(6, e.lastAttemptMs)
            .text(7, e.lastError);
        ok = s.step() == SQLITE_DONE && ok;
    }
    for (const auto& [id, at] : data.read) {
        Stmt s(db_, "INSERT OR IGNORE INTO own_read(account_id, post_id, read_at) VALUES (?1, ?2, ?3);");
        s.text(1, data.accountId).text(2, id).i64(3, at);
        ok = s.step() == SQLITE_DONE && ok;
    }
    sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return ok;
}

void PostStore::forgetOwn() {
    std::lock_guard lock(mutex_);
    sqlite3_exec(db_, "DELETE FROM own_outbox; DELETE FROM own_read;", nullptr, nullptr, nullptr);
}

OwnData PostStore::takeLegacy(const std::string& accountId) {
    std::lock_guard lock(mutex_);
    OwnData data;
    data.accountId = accountId;
    if (tableExists(db_, "outbox")) {
        data.outbox = queryOutbox(db_,
                                  std::string("SELECT ") + kOutboxColumns +
                                      " FROM main.outbox WHERE account_id = ?1 ORDER BY created ASC;",
                                  accountId);
        Stmt del(db_, "DELETE FROM main.outbox WHERE account_id = ?1;");
        del.text(1, accountId);
        del.step();
    }
    if (tableExists(db_, "inbox_read")) {
        Stmt s(db_, "SELECT post_id, read_at FROM main.inbox_read WHERE account_id = ?1;");
        s.text(1, accountId);
        while (s.step() == SQLITE_ROW)
            data.read.emplace_back(s.colText(0), s.colI64(1));
        Stmt del(db_, "DELETE FROM main.inbox_read WHERE account_id = ?1;");
        del.text(1, accountId);
        del.step();
    }
    return data;
}

bool PostStore::legacyRowsLeft() const {
    std::lock_guard lock(mutex_);
    for (const char* table : {"outbox", "inbox_read"}) {
        if (!tableExists(db_, table)) continue;
        Stmt s(db_, (std::string("SELECT 1 FROM main.") + table + " LIMIT 1;").c_str());
        if (s.step() == SQLITE_ROW) return true;
    }
    return false;
}

void PostStore::dropLegacyTablesIfEmpty() {
    if (legacyRowsLeft()) return;
    std::lock_guard lock(mutex_);
    // VACUUM afterwards so the freed pages that held the clear rows are
    // rewritten, not just marked free.
    if (tableExists(db_, "outbox") || tableExists(db_, "inbox_read")) {
        sqlite3_exec(db_, "DROP TABLE IF EXISTS main.outbox; DROP TABLE IF EXISTS main.inbox_read;",
                     nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "VACUUM;", nullptr, nullptr, nullptr);
    }
}

} // namespace forumer