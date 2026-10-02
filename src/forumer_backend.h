#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QHash>
#include <QString>

#include "cloud_data_core/engine.h"
#include "delivery_module_transport.h"
#include "forumer_core/account_store.h"
#include "forumer_core/identity.h"
#include "forumer_core/post.h"
#include "logos_ui_plugin_context.h"
#include "rep_forumer_source.h"
#include "storage_module_blob_store.h"

/**
 * @brief UI backend for Forumer.
 *
 * Two halves, glued to the QML view through the generated .rep contract:
 *
 * Identity (forumer_core): accounts live in password-protected vaults under
 * dataDir()/accounts (AccountStore). At most one is unlocked at a time; its
 * personas sign posts according to the account's rotation policy and the
 * per-post disclosure. Nothing about identity touches the network.
 *
 * Content: every post is a signed envelope (forumer_core::post) stored as a
 * document in the local-first CRDT store (cloud_data_core, vendored under lib/),
 * which syncs it over delivery_module. Documents are { "env": <envelope JSON>,
 * "ts": <ns string> } keyed by the envelope id. Every document that
 * materialises — our own put, a merged remote op, or a replay from disk — is
 * verified (signature, id, proof-of-work, shape) before the view sees it;
 * anything that fails is dropped.
 *
 * The C++ backend runs in its own isolated `ui-host` process; lifecycle hooks
 * and delivery events log to `std::cerr`, visible in the host's stderr stream.
 */
class ForumerBackend : public ForumerSimpleSource, public LogosUiPluginContext {
public:
  ForumerBackend();
  ~ForumerBackend() override;

  //  .rep SLOTs: posting 
  QString createTopic(QString title, QString body, QString domains,
                      QString disclosure) override;
  QString replyToTopic(QString topicId, QString body, QString disclosure) override;
  QString loadBacklog() override;

  //  .rep SLOTs: identity 
  QString createIdentity(QString label, QString password) override;
  QString unlockIdentity(QString accountId, QString password) override;
  QString restoreIdentity(QString phrase, QString label, QString password) override;
  QString lockIdentity() override;
  QString selectAccount(QString accountId) override;
  QString revealPhrase(QString password) override;
  QString changePassword(QString oldPassword, QString newPassword) override;
  QString renameAccount(QString label) override;
  QString deleteAccount(QString password) override;

  //  .rep SLOTs: persona controls 
  QString rotatePersona() override;
  QString chooseRotationPolicy(QString policy) override;
  QString chooseDefaultDisclosure(QString disclosure) override;
  QString chooseAlias(QString alias) override;

protected:
  // Fired once after the context is wired (so modules() is live). Schedules
  // bootstrap() off the return path.
  void onContextReady() override;

private:
  //  Network / store plumbing 
  // Wires delivery_module + storage_module events, opens the store, then
  // createNode + subscribe + start.
  void bootstrap();

  // Subscribe to both collections. Retries itself on failure (up to
  // kMaxSubscribeAttempts); idempotent.
  void subscribeToForum();

  // Re-derive the status PROP from the last connectionStateChanged value.
  void refreshStatus();

  // Map a delivery_module requestId back to the post it carried and report
  // `state` to the view. Ignores other apps' sends (delivery_module is shared).
  void settleSend(const QString &requestId, const QString &state,
                  const QString &detail);

  // Build the adapters and the engine and open the local store. Returns false
  // if the store could not be opened (composing then stays disabled).
  bool openEngine();

  // The engine's single report of a materialised document change. Verifies the
  // envelope and fans it out to topicReceived / replyReceived.
  void handleDocumentChanged(const std::string &collectionId,
                             const std::string &docId, const std::string &json);

  // Record which of our posts a delivery request id belongs to.
  void notePublished(const std::string &requestId,
                     const std::vector<uint8_t> &payload);

  //  Posts 
  // Parse and fully verify a stored/received document. nullopt (and a log
  // line) for anything that isn't a valid post with id `docId`.
  std::optional<forumer::post::Post> decodeDocument(const std::string &docId,
                                                     const std::string &json) const;

  // Sign `draft` with the unlocked account and put() it. "" on success.
  QString publish(forumer::post::Draft draft, const QString &disclosure);

  // Emit the matching .rep signal for a verified post.
  void emitPost(const forumer::post::Post &post);

  // Every persona public key in the local store's verified posts — what
  // restore scans to recover the persona counter.
  std::set<forumer::Bytes> knownAuthors() const;

  //  Identity 
  // Base directory for this app's local data (accounts + store), scoped to the
  // Basecamp instance's data tree (LOGOS_USER_DIR) when there is one.
  QString dataDir() const;

  // Push every identity PROP from the store + unlocked account. The single
  // place those PROPs are written.
  void publishIdentityState();

  // Persist the unlocked account's state (after settings changes or a post
  // that allocated a persona). Logs on failure; the in-memory state stands.
  void saveAccount();

  //  Constants 
  // LIP-23 app segment and topic version for every content topic. Version 2:
  // signed envelopes are not readable by (or mixed with) the sample's v1 posts.
  static const char kAppName[];
  static constexpr int kTopicVersion = 2;
  static const char kTopicsCollection[];
  static const char kRepliesCollection[];

  // One content topic per collection (see cloud_data_core contentTopicForDoc).
  static constexpr int kBucketBytes = 0;

  // Proof-of-work: what we mine when sending, and the minimum we accept.
  static constexpr int kPowBits = 16;
  static constexpr int kAnonymousPowBits = 20;

  //  State 
  std::unique_ptr<forumer::AccountStore> m_accounts;
  std::optional<forumer::identity::Account> m_account;  // set while unlocked

  bool m_subscribed = false;
  int m_subscribeAttempts = 0;
  QString m_connectionState;

  // In-flight sends: delivery_module requestId -> post id.
  QHash<QString, QString> m_pendingSends;

  // Declared in this order so the engine (which references both adapters) is
  // destroyed first.
  std::unique_ptr<DeliveryModuleTransport> m_transport;
  std::unique_ptr<StorageModuleBlobStore> m_blobStore;
  std::unique_ptr<cloud_data_core::CloudDataEngine> m_engine;
};