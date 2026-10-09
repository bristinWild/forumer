#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

class QTimer;

#include "delivery_module_transport.h"
#include "forumer_core/account_store.h"
#include "forumer_core/flood.h"
#include "forumer_core/identity.h"
#include "forumer_core/post.h"
#include "forumer_core/post_store.h"
#include "forumer_core/sync.h"
#include "logos_ui_plugin_context.h"
#include "rep_forumer_source.h"

/**
 * @brief UI backend for Forumer.
 *
 * Three parts, glued to the QML view through the generated .rep contract:
 *
 * Identity (forumer_core): accounts live in password-protected vaults under
 * dataDir()/accounts (AccountStore). At most one is unlocked at a time; its
 * personas sign posts according to the account's rotation policy and the
 * per-post disclosure. Nothing about identity touches the network.
 *
 * Post log (forumer_core::PostStore): every verified post this device knows,
 * append-only — the first valid copy of a post is kept and nothing can change
 * or remove it afterwards. Which of them the unlocked account wrote (its
 * outbox) and which replies it has read are kept in that account's encrypted
 * file (AccountStore::savePrivate "own") and only in memory while it is
 * unlocked: the log file itself never says who wrote what.
 *
 * Sync (forumer_core::sync over delivery_module): one content topic for the
 * public forum carrying signed posts and digests. Inbound posts are verified
 * (signature, id, proof-of-work, timestamp) before they are stored or shown.
 * Our own posts are retried until the network confirms them, and digests —
 * sent shortly after joining, periodically, and on "Catch up" — make peers
 * re-send whatever either side missed.
 *
 * The C++ backend runs in its own isolated `ui-host` process; lifecycle hooks
 * and delivery events log to `std::cerr`, visible in the host's stderr stream.
 */
class ForumerBackend : public ForumerSimpleSource, public LogosUiPluginContext {
public:
  ForumerBackend();
  ~ForumerBackend() override;

  // ── .rep SLOTs: posting and sync ───────────────────────────────────────────
  QString createTopic(QString title, QString body, QString domains,
                      QString disclosure) override;
  QString replyToTopic(QString topicId, QString body, QString disclosure) override;
  QString replyToPost(QString postId, QString body, QString disclosure) override;
  QString loadBacklog() override;
  QString catchUp() override;
  QString retryUnsent() override;
  QString loadOlderHistory() override;
  QString fetchAround(qint64 day) override;

  // ── .rep SLOTs: identity ───────────────────────────────────────────────────
  QString createIdentity(QString label, QString password) override;
  QString unlockIdentity(QString accountId, QString password) override;
  QString restoreIdentity(QString phrase, QString label, QString password) override;
  QString lockIdentity() override;
  QString selectAccount(QString accountId) override;
  QString revealPhrase(QString password) override;
  QString changePassword(QString oldPassword, QString newPassword) override;
  QString renameAccount(QString label) override;
  QString deleteAccount(QString password) override;
  QString postAfterRestore() override;

  // ── .rep SLOTs: persona controls ───────────────────────────────────────────
  QString rotatePersona() override;
  QString chooseRotationPolicy(QString policy) override;
  QString chooseDefaultDisclosure(QString disclosure) override;
  QString chooseAlias(QString alias) override;
  QString followDomain(QString domain) override;
  QString unfollowDomain(QString domain) override;
  QString chooseNetwork(QString preset) override;

  // ── .rep SLOTs: replies to you ─────────────────────────────────────────────
  QString markRepliesRead(QString ids) override;

  // ── .rep SLOTs: storage probe ──────────────────────────────────────────────
  QString storageStart() override;
  QString storageShareTest() override;
  QString storageFetch(QString code) override;

protected:
  // Fired once after the context is wired (so modules() is live). Schedules
  // bootstrap() off the return path.
  void onContextReady() override;

private:
  // ── Node lifecycle ─────────────────────────────────────────────────────────
  // Wires delivery_module events, opens the post log, then createNode +
  // join + start.
  void bootstrap();

  // Open the post log and build the transport. False if the log could not be
  // opened (composing then stays disabled).
  bool openStore();

  // Join the forum topic. Retries itself on failure (up to
  // kMaxJoinAttempts); idempotent. Starts the sync timers once joined.
  void joinForum();

  // Re-derive the status PROP from the last connectionStateChanged value.
  void refreshStatus();
  void pollConnectionStatus();

  // ── Inbound ────────────────────────────────────────────────────────────────
  // One payload from the network. Ignores other topics (delivery_module is
  // shared by every Basecamp app).
  void handlePayload(const QString &topic, const QByteArray &payload);
  void handlePost(forumer::post::Post post);
  void handleDigest(const forumer::sync::Digest &digest);
  // A history request: answer with what we hold in its window (rate-limited).
  void handleRange(const forumer::sync::Digest &range);
  // Queue posts to re-send for someone else (digest and range answers).
  void queueAnswers(const std::vector<std::string> &ids, const char *why);

  // Full acceptance check for a post from anywhere. "" if acceptable.
  std::string checkPost(const forumer::post::Post &post) const;

  // ── Outbound ───────────────────────────────────────────────────────────────
  // Sign `draft` with the unlocked account, store it, queue it. "" on success.
  QString publish(forumer::post::Draft draft, const QString &disclosure);

  // One send attempt of our own post `id` (outbox bookkeeping included).
  void sendOwn(const std::string &id);

  // Send a post again for someone else's benefit (digest answers). No
  // outbox bookkeeping; skipped if the post was seen on the wire recently.
  // False only when the re-send budget is used up (the caller keeps it queued).
  bool resend(const std::string &id);

  // Retry every unsent post that is due (or all of them, if `force`).
  void retryDue(bool force);

  // Broadcast our digest for the last sync::kWindowMs.
  void sendDigest(const char *why);

  // Schedule the next periodic digest (jittered, so peers don't sync in step).
  void scheduleDigest(int delayMs);

  // After a digest: if its answers brought a lot (peers cap each answer, and
  // new keys are rate-limited), ask again soon instead of waiting minutes.
  void scheduleFollowUp();

  // Re-send the posts queued by digest answers (batched, rate-limited).
  void flushAnswers();

  // A delivery event about one of our requests: record it and tell the view.
  void settleSend(const QString &requestId, const QString &state, const QString &detail);

  // ── View ───────────────────────────────────────────────────────────────────
  void emitPost(const forumer::post::Post &post);
  void publishSyncState();

  // ── History walk ───────────────────────────────────────────────────────────
  // Load where history is complete back to; start walking once a peer has
  // told us of older posts.
  void loadHistoryState();
  void maybeStartHistory();
  void historyStep();          // pick the next window and ask for it
  void historySendRange();     // one request round for the current window
  void historyRoundDone();     // repeat the window, or move the floor down
  void publishHistory();

  // History from the network's store nodes (see startStoreHistory).
  void startStoreHistory();
  void storeQueryNext();
  void storePageArrived(DeliveryModuleTransport::StorePage page);
  QStringList storePeers() const;
  int deliveryCluster() const;
  void publishQuota();

  // Push the unlocked account's inbox ("replies to you") to the view, now or
  // shortly (a catch-up can bring many posts at once: one update covers them).
  void publishInbox();
  void scheduleInbox();

  // ── Storage probe ──────────────────────────────────────────────────────────
  // Subscribe to storage_module's events (once).
  void wireStorage();
  // Peer id, reachability and peer count from the running node.
  void refreshStorageInfo();
  void publishStorage();
  // A fetch: dial the holder (storageFetch), look up the manifest, download.
  void storageFetchManifest();
  void storageDownload();

  // ── Identity ───────────────────────────────────────────────────────────────
  // Base directory for this app's local data (accounts + posts), scoped to the
  // Basecamp instance's data tree (LOGOS_USER_DIR) when there is one.
  QString dataDir() const;

  // The Delivery preset to join: FORUMER_NETWORK, else the saved choice
  // (dataDir()/network), else "logos.dev".
  QString networkPreset() const;

  // Push every identity PROP from the store + unlocked account. The single
  // place those PROPs are written.
  void publishIdentityState();

  // Persist the unlocked account's state (after settings changes or a post
  // that allocated a persona). Logs on failure; the in-memory state stands.
  void saveAccount();

  // The unlocked account's private part of the log ("own" data: its outbox
  // and read marks). attach loads it (migrating rows a 0.2.2 log kept in
  // clear) after unlock / create / restore; detach drops it from memory on
  // lock. saveOwn writes it back, encrypted, after every change;
  // scheduleOwnSave batches the writes when history brings many at once.
  void attachAccount();
  void detachAccount();
  bool saveOwn();
  void scheduleOwnSave();
  void publishOwnStates();

  // Public keys of the unlocked account's personas [0, nextIndex + gap), to
  // recognise its posts as they arrive (after a restore, or from another
  // device with the same phrase).
  void extendOwnKeys();
  // A stored post turned out to be signed by one of our personas.
  void noteOwnPost(const forumer::post::Post &post, uint64_t index);
  // Clear restorePending once history has synced from a peer.
  void maybeFinishRestore();

  // ── Constants ──────────────────────────────────────────────────────────────
  static const char kForum[];  // "public" — private forums come later

  // Proof-of-work: what we mine when sending, and the minimum we accept.
  static constexpr int kPowBits = 16;
  static constexpr int kAnonymousPowBits = 20;

  // ── State ──────────────────────────────────────────────────────────────────
  std::unique_ptr<forumer::AccountStore> m_accounts;
  std::optional<forumer::identity::Account> m_account;  // set while unlocked
  std::map<forumer::Bytes, uint64_t> m_ownKeys;          // persona public key -> index
  uint64_t m_ownKeysTo = 0;                              // m_ownKeys covers [0, this)
  bool m_ownSaveScheduled = false;
  qint64 m_firstArrivalMs = 0;                           // first new post from the network, this session

  std::unique_ptr<forumer::PostStore> m_posts;
  std::unique_ptr<DeliveryModuleTransport> m_transport;
  std::string m_topic;  // content topic of kForum

  bool m_joined = false;
  int m_joinAttempts = 0;
  QString m_connectionState;

  // In-flight sends of our own posts: delivery_module requestId -> post id.
  QHash<QString, QString> m_pendingSends;

  // When each post id was last seen on the wire (sent or received), ms. Lets
  // a peer skip re-sending what another peer has just re-sent.
  QHash<QString, qint64> m_lastSeenOnWire;

  // Post ids queued to re-send in answer to digests, and whether a flush is
  // already scheduled.
  QSet<QString> m_answerQueue;
  bool m_answerFlushScheduled = false;

  // Digest schedule: how many we've sent (the first two come quickly, then
  // they settle into the periodic interval) and when the last one went out.
  int m_digestRound = 0;
  qint64 m_lastDigestMs = 0;
  qint64 m_lastPromptedDigestMs = 0;  // last digest sent because a peer had more

  // Fires every few seconds once joined; retries whatever is due.
  QTimer *m_retryTimer = nullptr;
  QTimer *m_statusTimer = nullptr;

  // Flood control (forumer_core/flood.h): posts from never-seen keys, and
  // re-sends in answer to digests.
  forumer::flood::TokenBucket m_newKeyBudget{forumer::flood::kNewKeyPerMinute, forumer::flood::kNewKeyBurst};
  forumer::flood::TokenBucket m_resendBudget{forumer::flood::kResendPerMinute, forumer::flood::kResendBurst};

  bool m_inboxScheduled = false;

  // History walk (see forumer_core/sync.h "History").
  int64_t m_historyFloor = 0;      // complete back to here
  int64_t m_historyTarget = 0;     // oldest post any peer has told us of (0: unknown)
  bool m_historyActive = false;
  bool m_historyScheduled = false;
  int64_t m_historyFrom = 0;       // window being fetched: [from, to)
  int64_t m_historyTo = 0;
  int m_historyRoundNew = 0;       // new posts in the window this round
  int m_historyRounds = 0;         // rounds spent on this window
  int m_historyReceived = 0;       // older posts that arrived this session
  int m_historyEmptyWindows = 0;   // windows in a row that brought nothing (F10)
  bool m_historyByRequest = false; // walking because the user asked ("load older")
  qint64 m_lastRangeAnswerMs = 0;  // we answer one range request at a time
  qint64 m_lastLinkFetchMs = 0;    // fetchAround: at most one request per few seconds
  qint64 m_lastCoveredSaveMs = 0;

  // Store nodes: what the network itself kept, for a newcomer with nobody
  // else online. One pass per session, node by node.
  bool m_storeStarted = false;
  bool m_storeImport = false;       // handlePost: these come from a store node
  bool m_storeShortRange = false;   // a node refused a month: ask for a day
  int m_storePeer = 0;              // index into storePeers()
  int m_storePage = 0;              // pages read from this node
  int m_storeNodeMsgs = 0;          // messages this node returned (new or not)
  int m_storeGot = 0;               // new posts from all nodes, this session
  std::string m_storeCursor;
  QString m_storeState;             // "" | "asking" | "done" | "empty" | "unreachable"
  QString m_storeNode;              // host of the node that answered last
  QString m_storeError;

  // Catch-up bursts: posts that arrived (or were deferred) since our last
  // digest, and how many follow-up digests this burst has sent.
  int m_roundArrivals = 0;
  int m_followUps = 0;
  bool m_followUpScheduled = false;

  // Storage probe state (see storageJson in forumer.rep).
  bool m_storageWired = false;
  bool m_storageAttached = false;
  bool m_storageSharing = false;
  QString m_storageState = QStringLiteral("off");
  QString m_storageDetail;
  QString m_storagePeerId;
  QString m_storageReachability;
  int m_storagePeers = 0;
  QString m_storageLastCid;
  QString m_storageShareCode;              // cid@peerId@addr,addr - what the other side pastes
  QStringList m_storageAddrs;              // this node's listen addresses
  int m_storageListenPort = 0;             // our node's TCP port (0: attached to another's)
  QString m_storageUploadSession;          // upload in flight (session id)
  QJsonObject m_storageFetch;              // last fetch
  QString m_storageFetchPath;              // where that fetch is written
  QTimer *m_storageTimer = nullptr;        // refreshes reachability while running

  // For the status line.
  int m_receivedCount = 0;  // new posts that arrived from the network this session
};