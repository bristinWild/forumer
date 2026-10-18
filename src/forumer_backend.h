#pragma once

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

class QTimer;
class QProcess;

#include "delivery_module_transport.h"
#include "forumer_core/account_store.h"
#include "forumer_core/digest_plan.h"
#include "forumer_core/flood.h"
#include "forumer_core/forums.h"
#include "forumer_core/media.h"
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
 * public forum carrying signed posts and digests, and one per private forum
 * the unlocked account is in (forumer_core/forums.h), where every message is
 * sealed with the forum's key. Inbound posts are verified
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
  QString createTopicIn(QString forum, QString title, QString body, QString domains,
                        QString disclosure) override;
  QString postTopic(QString forum, QString title, QString body, QString domains, QString disclosure,
                    QString attachments) override;
  QString postReply(QString postId, QString body, QString disclosure, QString attachments) override;
  QString replyToTopic(QString topicId, QString body, QString disclosure) override;
  QString replyToPost(QString postId, QString body, QString disclosure) override;
  QString loadBacklog() override;
  QString loadBacklogPage(QString cursor) override;
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

  // ── .rep SLOTs: private forums ─────────────────────────────────────────────
  QString createPrivateForum(QString name) override;
  QString joinPrivateForum(QString key, QString name) override;
  QString leavePrivateForum(QString id) override;
  QString privateForumInvite(QString id) override;

  // ── .rep SLOTs: images ─────────────────────────────────────────────────────
  QString attachImage(QString group, QString forum, QString path) override;
  QString removeAttachment(QString group, QString id) override;
  QString retryAttachment(QString group, QString id) override;
  QString clearAttachments(QString group) override;
  QString pickImages(QString group, QString forum) override;
  QString loadMedia(QString postId, int index) override;
  QString chooseMediaRehost(bool on) override;
  QString chooseMediaAutoFull(bool on) override;
  QString showMedia(QString postId, int index) override;

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
  // `forum`: whose topic it came on ("public" or a private forum's id).
  void handlePost(forumer::post::Post post, const std::string &forum);
  // `fromPeer`: false for our own digest coming back to us.
  void handleDigest(const forumer::sync::Digest &digest, bool fromPeer, const std::string &forum);
  // Our unsent posts that a peer's digest lists are on the network: sent.
  void confirmHeldByPeers(const forumer::sync::Digest &digest);
  static QByteArray payloadHash(const std::vector<uint8_t> &payload);
  // A history request: answer with what we hold in its window (rate-limited).
  void handleRange(const forumer::sync::Digest &range);
  // Queue posts to re-send for someone else (digest and range answers).
  void queueAnswers(const std::vector<std::string> &ids, const char *why);

  // Full acceptance check for a post from anywhere. "" if acceptable.
  // `fromHistory`: it came from a history walk or a store node (bulk, old).
  std::string checkPost(const forumer::post::Post &post, bool fromHistory) const;

  // ── Outbound ───────────────────────────────────────────────────────────────
  // Sign `draft` with the unlocked account, store it, queue it. "" on success.
  // `attachments`: the composer whose ready images go with it ("" = none).
  QString publish(forumer::post::Draft draft, const QString &disclosure,
                  const QString &attachments = QString());

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

  // ── Private forums ─────────────────────────────────────────────────────────
  // The unlocked account's forum `id`, or null (not in it / locked).
  const forumer::forums::Forum *privateForum(const std::string &id) const;
  // Content topic of a forum ("public" or a private forum's id).
  std::string topicOf(const std::string &forum) const;
  // Publish on the forum's topic, sealed for a private one. `sentHash`: the
  // hash of the bytes as sent (to recognise our own digest's echo).
  DeliveryModuleTransport::SendResult publishTo(const std::string &forum,
                                                const std::vector<uint8_t> &payload,
                                                QByteArray *sentHash = nullptr);
  // A stored post, opened if it is in a private forum the account is in.
  std::optional<forumer::post::Post> loadPost(const std::string &id) const;
  // Store a post; a private forum's sealed at rest.
  forumer::PostStore::Insert storePost(const forumer::post::Post &post, int64_t receivedMs);
  // Rebuild m_forums from the unlocked account (or clear it), join their
  // topics, tell the view.
  void loadForums();
  void joinForumTopics();
  void publishForums();
  QString inviteLink(const forumer::forums::Forum &forum) const;
  // Emit stored posts of private forum `only` ("" = all joined) to the view.
  void emitForumPosts(const std::string &only);
  // Ask the store nodes for a private forum's archive (after the public one).
  void enqueueStore(const std::string &forum);

  // ── View ───────────────────────────────────────────────────────────────────
  void emitPost(const forumer::post::Post &post);
  QJsonObject backlogEntry(const forumer::post::Post &post, const QHash<QString, QString> &states) const;
  QHash<QString, QString> ownStates() const;
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
  void wireMediaProgress();
  // Create or attach to the storage node and start it ("" = on its way).
  QString startStorageNode();
  // Peer id, reachability and peer count from the running node.
  void refreshStorageInfo();
  void publishStorage();
  // A fetch: dial the holder (storageFetch), look up the manifest, download.
  void storageFetchManifest();
  void storageDownload();

  // ── Media (see "Media" in forumer_backend.cpp) ────────────────────────────
  QString mediaDir() const;
  QString mediaSummary(const forumer::post::Post &post) const;   // JSON for the view
  void loadMediaSettings();
  void publishMedia();
  void dropAttachment(const QString &id);
  void forgetAccountMedia();          // on lock: attachments and private pictures go
  void cachePut(const QString &key, const QString &url);
  void pumpUploads();                 // start the next queued upload, if any
  void mediaUploadDone(const QJsonObject &payload);
  void mediaStorageReady();
  void mediaStorageFailed();
  void startMediaFetch(const std::string &cid);
  void startMediaFetchFromNetwork(const std::string &cid);
  void mediaManifestDone(const QJsonObject &payload);
  void mediaDownload(const std::string &cid, bool local);
  void mediaDownloadDone(const QJsonObject &payload);
  void failMediaFetch(const std::string &cid, const QString &why);
  void notifyMediaFetching(const std::string &cid, const QString &phase);
  void watchDownload(const std::string &cid, int gen);
  void restartDownload(const std::string &cid);
  void sendPreview(const std::string &postId, uint32_t index, const std::string &forum);
  void handlePreviewPart(forumer::sync::PreviewPart part, const std::string &forum);
  void acceptPreview(const std::string &postId, uint32_t index, const forumer::Bytes &blob,
                     const forumer::Bytes &sig, const std::string &forum);
  void previewsForPost(const std::string &postId);
  void askForHolders(const std::string &cid, const std::string &forum);
  void handleMediaAsk(const std::string &cid, const std::string &forum);
  void handleMediaHere(const forumer::sync::MediaHint &hint);
  void dialHolder(const std::string &cid, const forumer::sync::MediaHint &hint);
  bool pickerAvailable() const;

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
  // First sign of the network this session (a new post or a peer's digest).
  void noteNetworkContact();
  // After a restore, replies dated before it count as read.
  void markRestoredRepliesRead();

  // ── Constants ──────────────────────────────────────────────────────────────
  static const char kForum[];  // "public"; private forums are named by their id

  // Proof-of-work: what we mine when sending, and the minimum we accept.
  static constexpr int kPowBits = 16;
  static constexpr int kAnonymousPowBits = 20;

  // Most posts kept in the log (F18); the oldest beyond it are pruned at start.
  static constexpr size_t kMaxStoredPosts = 50'000;

  // ── State ──────────────────────────────────────────────────────────────────
  // An identity or posting slot is running work off the thread (Argon2id,
  // proof-of-work); others answer "busy" until it's done.
  bool m_busy = false;

  std::unique_ptr<forumer::AccountStore> m_accounts;
  std::optional<forumer::identity::Account> m_account;  // set while unlocked
  std::map<forumer::Bytes, uint64_t> m_ownKeys;          // persona public key -> index
  uint64_t m_ownKeysTo = 0;                              // m_ownKeys covers [0, this)
  bool m_ownSaveScheduled = false;
  qint64 m_firstArrivalMs = 0;                           // first network contact this session (noteNetworkContact)
  qint64 m_lastRestoreWaitLogMs = 0;
  qint64 m_lastPeerMs = 0;                               // last post or digest from another peer
  int64_t m_historyLimitMs = 0;                          // pruned below here: the walk stops there

  std::unique_ptr<forumer::PostStore> m_posts;
  std::unique_ptr<DeliveryModuleTransport> m_transport;
  std::string m_topic;  // content topic of kForum

  // The unlocked account's private forums (id -> forum, keys in memory only
  // while unlocked), their content topics, and every topic joined this
  // session (never unsubscribed: a left forum's topic is simply ignored).
  std::map<std::string, forumer::forums::Forum> m_forums;
  std::map<std::string, std::string> m_forumByTopic;
  std::set<std::string> m_joinedTopics;

  bool m_joined = false;
  int m_joinAttempts = 0;
  QString m_connectionState;

  // In-flight sends of our own posts: delivery_module requestId -> post id.
  QHash<QString, QString> m_pendingSends;

  // Hashes of the digests we sent recently (they echo back to us).
  QSet<QByteArray> m_ownDigests;
  QList<QByteArray> m_ownDigestOrder;

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
  forumer::flood::TokenBucket m_priorityKeyBudget{forumer::flood::kPriorityPerMinute, forumer::flood::kPriorityBurst};
  forumer::flood::TokenBucket m_resendBudget{forumer::flood::kResendPerMinute, forumer::flood::kResendBurst};
  // Unsigned sync work (flood.h, layer 6): digests that read our held set, and mask lookups.
  forumer::flood::TokenBucket m_digestScanBudget{forumer::flood::kDigestScansPerMinute, forumer::flood::kDigestScansBurst};
  forumer::flood::TokenBucket m_mediaAskBudget{forumer::flood::kMediaAskPerMinute, forumer::flood::kMediaAskBurst};

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
  forumer::sync::RangeGate m_rangeGate;  // one range request at a time; only answered ones start the gap
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
  // After the public forum: the private forums' archives, one at a time.
  bool m_storeRunning = false;
  std::string m_storeForum;         // forum being asked for
  std::deque<std::string> m_storeQueue;

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

  // Images being attached: uploaded one at a time (m_mediaUploadId).
  struct Attachment {
    QString id;
    QString group;       // which composer ("topic", "reply")
    QString forum;       // sealed for this forum
    QString name;        // file name, shown in the composer only
    QString state;       // "queued" | "uploading" | "ready" | "failed"
    QString error;
    int progress = 0;
    forumer::post::MediaRef ref;   // cid set once uploaded
    QString blobPath;
    QString thumb;       // data: URL
    QByteArray preview;  // JPEG sent with the post (media.h "Previews")
    int width = 0;
    int height = 0;
  };
  std::vector<Attachment> m_attachments;
  QProcess *m_picker = nullptr;   // the system file picker, while open
  QString m_pickerGroup;
  QString m_pickerForum;
  QString m_mediaNotice;          // why a picked file couldn't be attached
  QString m_mediaNoticeGroup;
  QString m_mediaUploadId;
  QString m_mediaUploadSession;

  // Images being fetched, by CID; each may be wanted by several posts.
  struct MediaFetch {
    forumer::post::MediaRef ref;
    std::string forum;
    QString state;       // "storage" (node starting) | "manifest" | "fetching"
    qint64 startedMs = 0;
    QList<QPair<QString, int>> waiters;   // (post id, index)
    QString path;
    bool local = false;
    int generation = 0;  // stale timeouts ignore themselves
    int progress = -1;
    QString phase;       // "lookup" | "holder" | "download"
    qint64 lastProgressMs = 0;
    qint64 downloadStartedMs = 0;
    int retries = 0;
    std::vector<forumer::sync::MediaHint> hints;   // holders that answered (mhere)
  };
  std::map<std::string, MediaFetch> m_mediaFetches;
  std::set<std::string> m_ownMediaCids;               // uploaded this session
  std::map<std::string, qint64> m_hintAskedAt;        // cid -> when we last asked
  std::map<std::string, qint64> m_hintSentAt;         // cid -> when we last answered
  std::map<std::string, qint64> m_hintSeenAt;         // cid -> last answer seen from anyone

  // Previews in pieces, "<post>:<index>" -> pieces so far.
  struct PreviewAssembly {
    qint64 firstMs = 0;
    std::string forum;
    forumer::Bytes signature;
    std::vector<std::optional<forumer::Bytes>> pieces;
  };
  std::map<std::string, PreviewAssembly> m_previewParts;
  // Complete previews whose post hasn't arrived yet.
  struct WaitingPreview {
    std::string postId;
    uint32_t index = 0;
    forumer::Bytes blob;
    forumer::Bytes signature;
    std::string forum;
    qint64 receivedMs = 0;
  };
  std::vector<WaitingPreview> m_previewsWaiting;
  std::map<std::string, qint64> m_previewSentAt;      // "<post>:<index>" -> when we sent it
  std::map<std::string, qint64> m_previewSeenAt;      // "<post>:<index>" -> when a piece was last seen
  std::map<std::string, qint64> m_previewAskedAt;     // cid -> when we asked for its preview

  // Opened images as data: URLs, "<sha256>|<forum>" -> URL (memory only).
  QHash<QString, QString> m_mediaCache;
  QList<QString> m_mediaCacheOrder;
  qint64 m_mediaCacheBytes = 0;

  // For the status line.
  int m_receivedCount = 0;  // new posts that arrived from the network this session
};