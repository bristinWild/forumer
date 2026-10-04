#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QHash>
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
 * or remove it afterwards — plus the outbox of posts this device wrote.
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

  // ── .rep SLOTs: persona controls ───────────────────────────────────────────
  QString rotatePersona() override;
  QString chooseRotationPolicy(QString policy) override;
  QString chooseDefaultDisclosure(QString disclosure) override;
  QString chooseAlias(QString alias) override;
  QString followDomain(QString domain) override;
  QString unfollowDomain(QString domain) override;

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

  // ── Inbound ────────────────────────────────────────────────────────────────
  // One payload from the network. Ignores other topics (delivery_module is
  // shared by every Basecamp app).
  void handlePayload(const QString &topic, const QByteArray &payload);
  void handlePost(forumer::post::Post post);
  void handleDigest(const forumer::sync::Digest &digest);

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

  // Re-send the posts queued by digest answers (batched, rate-limited).
  void flushAnswers();

  // A delivery event about one of our requests: record it and tell the view.
  void settleSend(const QString &requestId, const QString &state, const QString &detail);

  // ── View ───────────────────────────────────────────────────────────────────
  void emitPost(const forumer::post::Post &post);
  void publishSyncState();

  // ── Identity ───────────────────────────────────────────────────────────────
  // Base directory for this app's local data (accounts + posts), scoped to the
  // Basecamp instance's data tree (LOGOS_USER_DIR) when there is one.
  QString dataDir() const;

  // Push every identity PROP from the store + unlocked account. The single
  // place those PROPs are written.
  void publishIdentityState();

  // Persist the unlocked account's state (after settings changes or a post
  // that allocated a persona). Logs on failure; the in-memory state stands.
  void saveAccount();

  // ── Constants ──────────────────────────────────────────────────────────────
  static const char kForum[];  // "public" — private forums come later

  // Proof-of-work: what we mine when sending, and the minimum we accept.
  static constexpr int kPowBits = 16;
  static constexpr int kAnonymousPowBits = 20;

  // ── State ──────────────────────────────────────────────────────────────────
  std::unique_ptr<forumer::AccountStore> m_accounts;
  std::optional<forumer::identity::Account> m_account;  // set while unlocked

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

  // Flood control (forumer_core/flood.h): posts from never-seen keys, and
  // re-sends in answer to digests.
  forumer::flood::TokenBucket m_newKeyBudget{forumer::flood::kNewKeyPerMinute, forumer::flood::kNewKeyBurst};
  forumer::flood::TokenBucket m_resendBudget{forumer::flood::kResendPerMinute, forumer::flood::kResendBurst};

  // For the status line.
  int m_receivedCount = 0;  // new posts that arrived from the network this session
};