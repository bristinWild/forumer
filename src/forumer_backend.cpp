#include "forumer_backend.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <QVariantList>

#include <nlohmann/json.hpp>

// Generated umbrella: LogosModules (behind modules()) built from
// metadata.json#dependencies — the typed delivery_module and storage_module
// wrappers. logos_types.h provides LogosResult.
#include "logos_sdk.h"
#include "logos_types.h"

// The vendored local-first engine (lib/, from cloud-data-module) and the two
// Qt-typed adapters that bind it to delivery_module / storage_module.
#include "cloud_data_core/sync_engine.h"
#include "delivery_module_transport.h"
#include "storage_module_blob_store.h"

// Forumer's own engine (lib/forumer_core).
#include "forumer_core/crypto.h"
#include "forumer_core/mnemonic.h"

// Injected by CMake from metadata.json#version.
#ifndef FORUMER_VERSION
#define FORUMER_VERSION "unknown"
#endif

namespace fc = forumer;
using fc::identity::Disclosure;
using fc::identity::RotationPolicy;

namespace {
// One consistently-tagged line per lifecycle hook / delivery event so the
// backend's activity is easy to spot (and grep) in the host's stderr stream.
void logEvent(const std::string &what) {
  std::cerr << "[forumer backend] " << what << std::endl;
}

// A fresh random id — only used for this install's CRDT peer id.
QString newId() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// Subscribe retry: how long to wait before asking again, and how many attempts
// before giving up and leaving the failure on screen.
constexpr int kSubscribeRetryMs = 5000;
constexpr int kMaxSubscribeAttempts = 5;

// Small text-file helpers (the CRDT peer id file).
QString readTextFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(f.readAll()).trimmed();
}

bool writeTextFile(const QString &path, const QString &contents) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;
  QTextStream out(&f);
  out << contents;
  return true;
}

// JSON result for slots that return more than an error string.
QString jsonResult(const QString &error, const QString &phrase = QString()) {
  QJsonObject obj{{"error", error}};
  if (!phrase.isEmpty())
    obj.insert(QStringLiteral("phrase"), phrase);
  return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString qs(const std::string &s) { return QString::fromStdString(s); }

QString joinDomains(const std::vector<std::string> &domains) {
  QStringList parts;
  for (const auto &d : domains)
    parts << qs(d);
  return parts.join(QLatin1Char(','));
}

// Envelope timestamps are ms; the view (and the store's sort key) use ns.
qint64 toNs(int64_t ms) { return static_cast<qint64>(ms) * 1000000LL; }
} // namespace

const char ForumerBackend::kAppName[] = "forumer";
const char ForumerBackend::kTopicsCollection[] = "topics";
const char ForumerBackend::kRepliesCollection[] = "replies";

ForumerBackend::ForumerBackend() {
  // Runs in the ui-host process before the context is wired.
  logEvent("ctor — backend constructed (context not yet wired)");
  if (!fc::crypto::initCrypto())
    logEvent("libsodium failed to initialise — signing and unlocking will fail");
  setAppVersion(QStringLiteral(FORUMER_VERSION));
}

ForumerBackend::~ForumerBackend() {
  logEvent("dtor — backend destroyed");
}

void ForumerBackend::onContextReady() {
  logEvent("onContextReady — context wired, scheduling node bootstrap");
  setTopic(QStringLiteral("%1 + %2")
               .arg(qs(cloud_data_core::sync_engine::contentTopicForDoc(
                        kAppName, kTopicVersion, kTopicsCollection, std::string(), kBucketBytes)),
                    qs(cloud_data_core::sync_engine::contentTopicForDoc(
                        kAppName, kTopicVersion, kRepliesCollection, std::string(), kBucketBytes))));

  // Accounts are purely local, so they're available before the network is.
  m_accounts = std::make_unique<fc::AccountStore>(
      std::filesystem::path((dataDir() + QStringLiteral("/accounts")).toStdString()));
  publishIdentityState();

  // createNode()/start() are synchronous and can block for a moment. Defer the
  // bootstrap to the next event-loop turn so the QML replica comes up promptly.
  QTimer::singleShot(0, [this]() { bootstrap(); });
}

void ForumerBackend::bootstrap() {
  // --- Subscribe to delivery_module events before starting the node ---------

  // Node health. connectionStateChanged (Connected / PartiallyConnected /
  // Disconnected) is the only honest account of connectivity we get, so it
  // drives the status PROP outright. Deliberately ungated: the early events
  // land before the subscribe does, and dropping them is what let a node with
  // no peers sit there reporting "Connected".
  modules().delivery_module.on(
      "connectionStateChanged", [this](const QVariantList &data) {
        if (data.isEmpty())
          return;
        m_connectionState = data.at(0).toString();
        logEvent("connection state -> " + m_connectionState.toStdString());
        refreshStatus();
      });

  // Inbound CRDT ops. data[1] is the content topic, data[2] the raw payload.
  // The engine routes on topic shape (regular ops vs snapshot pointers),
  // merges by op id, and reports anything that actually changed through
  // handleDocumentChanged — so payloads that aren't ours are dropped there
  // rather than here.
  modules().delivery_module.on(
      "messageReceived", [this](const QVariantList &data) {
        if (data.size() < 3 || !m_engine)
          return;
        const QByteArray payload = data.at(2).toByteArray();
        m_engine->handleIncomingMessage(
            data.at(1).toString().toStdString(),
            std::vector<uint8_t>(payload.begin(), payload.end()));
      });

  // Registered alongside — not instead of — the handler above: the engine
  // prefers a reliable channel and latches back to plain send/subscribe only
  // once channelCreate() fails, and a host-owned node can be either. Both
  // paths are idempotent by op id, so an op arriving on both merges once.
  // data[0] is the channel id, which the engine sets to the content topic, so
  // the same routing applies.
  modules().delivery_module.on(
      "channelMessageReceived", [this](const QVariantList &data) {
        if (data.size() < 3 || !m_engine)
          return;
        const QByteArray payload = data.at(2).toByteArray();
        m_engine->handleIncomingMessage(
            data.at(0).toString().toStdString(),
            std::vector<uint8_t>(payload.begin(), payload.end()));
      });

  // Node startup. start() is dispatch-only in delivery_module v0.2.1 — its
  // contract is "`true` once dispatched; completion is reported via
  // `nodeStarted`" — so this event, not start()'s return, says whether the node
  // actually came up. The subscribe does not hang off it (see bootstrap's
  // ordering below); this only reports, and re-drives a subscribe that failed
  // earlier now that the node is definitely up.
  modules().delivery_module.on("nodeStarted", [this](const QVariantList &data) {
    const bool ok = !data.isEmpty() && data.at(0).toBool();
    const QString message = data.value(1).toString();
    logEvent("nodeStarted success=" + std::to_string(ok) + " " +
             message.toStdString());
    if (!ok) {
      setStatus(QStringLiteral("Node failed to start: %1").arg(message));
      return;
    }
    if (m_subscribed) {
      refreshStatus();
      return;
    }
    // Fresh retry budget: attempts spent while the node was still booting were
    // fighting a different problem than the one from here on. Deferred off the
    // event callback — calling synchronously back into the module from inside
    // its own event dispatch is exactly the shape that times out.
    m_subscribeAttempts = 0;
    QTimer::singleShot(0, [this]() { subscribeToForum(); });
  });

  // Delivery outcomes for our own posts, keyed by the request id the transport
  // hands back (see notePublished, which maps it to the post it carried). A
  // successful put() means the post is safe on disk and queued, not that it
  // left the machine; these events are what settle that second question.
  //
  // They do double duty now: as well as driving the view's per-post state,
  // they resolve the engine's outbox row for that request id. Until a row is
  // resolved the engine keeps re-issuing it on the next put/subscribe, so
  // skipping these would mean every op re-sent forever.
  modules().delivery_module.on(
      "messagePropagated", [this](const QVariantList &data) {
        // A waypoint, not an outcome — reported to the view, but the outbox
        // row stays open until the send is settled either way.
        settleSend(data.value(0).toString(), QStringLiteral("propagated"),
                   QString());
      });
  modules().delivery_module.on("messageSent", [this](const QVariantList &data) {
    const QString requestId = data.value(0).toString();
    settleSend(requestId, QStringLiteral("sent"), QString());
    if (m_engine)
      m_engine->onOutboxResolved(requestId.toStdString(), true);
  });
  modules().delivery_module.on(
      "messageError", [this](const QVariantList &data) {
        const QString requestId = data.value(0).toString();
        settleSend(requestId, QStringLiteral("failed"), data.value(2).toString());
        if (m_engine)
          m_engine->onOutboxResolved(requestId.toStdString(), false);
      });

  // The same bookkeeping for the reliable-channel path. Here data[0] is the
  // channel id and data[1] the request id (the plain-send events lead with the
  // request id instead), and the error string moves to data[2].
  modules().delivery_module.on(
      "channelMessageSent", [this](const QVariantList &data) {
        const QString requestId = data.value(1).toString();
        settleSend(requestId, QStringLiteral("sent"), QString());
        if (m_engine)
          m_engine->onOutboxResolved(requestId.toStdString(), true);
      });
  modules().delivery_module.on(
      "channelMessageError", [this](const QVariantList &data) {
        const QString requestId = data.value(1).toString();
        settleSend(requestId, QStringLiteral("failed"), data.value(2).toString());
        if (m_engine)
          m_engine->onOutboxResolved(requestId.toStdString(), false);
      });

  // --- storage_module events -> the engine's snapshot bridge ----------------
  // The blob-store adapter owns storage_module's wire format, so what reaches
  // the engine is already a parsed (success, sessionId, bytes). Inert unless
  // the user has a storage node up: this app never starts one (see openEngine).
  modules().storage_module.onStorageUploadProgress([this](const QString &payload) {
    if (!m_engine)
      return;
    const auto ev = StorageModuleBlobStore::parseUploadProgress(payload.toStdString());
    m_engine->onBlobUploadProgress(ev.success, ev.sessionId);
  });
  modules().storage_module.onStorageDownloadProgress([this](const QString &payload) {
    if (!m_engine)
      return;
    const auto ev = StorageModuleBlobStore::parseDownloadProgress(payload.toStdString());
    m_engine->onBlobDownloadProgress(ev.success, ev.sessionId, ev.chunk);
  });
  modules().storage_module.onStorageDownloadDone([this](const QString &payload) {
    if (!m_engine)
      return;
    const auto ev = StorageModuleBlobStore::parseDownloadDone(payload.toStdString());
    m_engine->onBlobDownloadDone(ev.success, ev.sessionId);
  });

  // --- Open the local store and replay what it holds -------------------------
  // Before the node, deliberately: local reads and writes never touch the
  // network, so the forum can be populated and composable while the node is
  // still bootstrapping — or when it never comes up at all.
  if (!openEngine()) {
    setStatus(QStringLiteral("Local store unavailable — posts can't be saved"));
    return;
  }

  // --- Create + start the node against the logos.test fleet -----------------
  // The *layered* config shape: `mode` and `preset` are both keys the layered
  // parser consumes, which is what earns the structured defaults — ephemeral
  // p2p ports, plus the host's per-instance localStoragePath. Adding any bare
  // WakuNodeConf key at the top level (logLevel, tcpPort, relay, …) flips
  // delivery's isFlatShape() check and reclassifies the whole config as the
  // legacy flat shape, which since delivery v0.2.0 no longer zeroes the
  // listening ports — it binds upstream's fixed defaults (tcp 60000), so two
  // instances on one machine collide. `logLevel` used to sit here and did
  // exactly that. Tuning keys belong inside messagingOverrides /
  // channelsOverrides / kernelConf instead; getAvailableConfigs() reports
  // what the running module accepts.
  const QJsonObject cfg{
      {"mode", "Core"},
      {"preset", "logos.test"},
  };
  const QString cfgJson =
      QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

  LogosResult created = modules().delivery_module.createNode(cfgJson);
  if (!created.success) {
    // delivery_module is a singleton shared across Basecamp apps, so another
    // app may have already created and started the node. createNode then fails
    // and no nodeStarted will ever fire for us — subscribe directly.
    logEvent("createNode failed (node may already be running): " +
             created.getError().toStdString());
    subscribeToForum();
    return;
  }

  logEvent("createNode succeeded");

  // Subscribe *before* start(), which is what use-delivery-module documents
  // ("Subscribe before `start()`, and wire event `.on(...)` handlers before
  // triggering sends, or you'll miss early events"). This is the one window
  // where both hazards are absent: the node is built but not yet bootstrapping,
  // so the call doesn't queue behind discovery work and time out, and the
  // subscription is registered before any message can arrive — so there is no
  // race with start() left to lose either.
  subscribeToForum();

  setStatus(QStringLiteral("Starting node…"));
  LogosResult started = modules().delivery_module.start();
  if (!started.success) {
    setStatus(QStringLiteral("Node failed to start: %1").arg(started.getError()));
    logEvent("start failed: " + started.getError().toStdString());
    return;
  }
  logEvent("start dispatched — waiting for nodeStarted");
}

void ForumerBackend::subscribeToForum() {
  if (m_subscribed)
    return; // the pre-start attempt and nodeStarted can both land

  if (!m_engine)
    return; // nothing to subscribe with yet; openEngine() drives the first try

  ++m_subscribeAttempts;
  // One call per collection. The doc id is irrelevant to the topic at
  // kBucketBytes == 0 (see the header), so a sentinel stands in for it — what
  // this actually joins is the whole collection, which is what a forum needs:
  // posts arrive from peers whose doc ids we have never seen.
  const cloud_data_core::EngineResult topics =
      m_engine->subscribe(kTopicsCollection, "*");
  const cloud_data_core::EngineResult replies =
      m_engine->subscribe(kRepliesCollection, "*");
  if (!topics.success || !replies.success) {
    const std::string detail = topics.success ? replies.error : topics.error;
    setStatus(QStringLiteral("subscribe failed: %1")
                  .arg(QString::fromStdString(detail)));
    logEvent("subscribe attempt " + std::to_string(m_subscribeAttempts) +
             " failed: " + detail);
    // Retry rather than leaving the app receiving nothing. The common failure
    // here is a timeout because the node is busy bootstrapping, which passes
    // on its own. Composing is unaffected — that runs off the local store.
    if (m_subscribeAttempts < kMaxSubscribeAttempts)
      QTimer::singleShot(kSubscribeRetryMs, [this]() { subscribeToForum(); });
    return;
  }

  m_subscribed = true;
  refreshStatus();
  logEvent("subscribed — forum on the topics + replies collections");
}

void ForumerBackend::refreshStatus() {
  if (!m_subscribed)
    return; // bootstrap's own progress messages own the status until then

  if (m_connectionState.isEmpty()) {
    // Subscribed locally, but nothing has yet said we have peers — and a node
    // with none receives nothing while still publishing happily. Name that
    // state instead of claiming "Connected", which is what the old status line
    // did and what made a node talking to nobody indistinguishable from a
    // healthy one.
    setStatus(QStringLiteral("Subscribed — waiting for peers"));
    return;
  }
  setStatus(m_connectionState);
}

void ForumerBackend::settleSend(const QString &requestId,
                                     const QString &state,
                                     const QString &detail) {
  if (requestId.isEmpty())
    return;

  const auto it = m_pendingSends.constFind(requestId);
  if (it == m_pendingSends.constEnd())
    return; // another app's send — delivery_module is shared

  const QString messageId = it.value();
  // "propagated" is a waypoint, not an outcome: the message has reached the
  // network but isn't validated yet, so keep the mapping for the event that
  // settles it.
  if (state != QLatin1String("propagated"))
    m_pendingSends.remove(requestId);

  logEvent("send " + requestId.toStdString() + " -> " + state.toStdString() +
           (detail.isEmpty() ? "" : " (" + detail.toStdString() + ")"));
  emit messageStateChanged(messageId, state, detail);
}

QString ForumerBackend::dataDir() const {
  // Basecamp's --user-dir (and our two-instance test script) gives each
  // instance its own data tree, exported to child processes as LOGOS_USER_DIR.
  const QString userDir = qEnvironmentVariable("LOGOS_USER_DIR");
  if (!userDir.isEmpty())
    return userDir + QStringLiteral("/module_data/forumer");
  // Default launch: AppDataLocation is keyed off the host process's name, so
  // namespace under the module name.
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/forumer");
}

// ── Identity ──────────────────────────────────────────────────────────────────

void ForumerBackend::publishIdentityState() {
  if (!m_accounts)
    return;
  const auto accounts = m_accounts->list();

  QJsonArray view;
  for (const auto &a : accounts)
    view.append(QJsonObject{{"id", qs(a.id)}, {"label", qs(a.label)}});
  setAccountsJson(QString::fromUtf8(QJsonDocument(view).toJson(QJsonDocument::Compact)));

  std::string selected;
  if (m_account)
    selected = m_account->state().id;
  else if (auto id = m_accounts->selectedId())
    selected = *id;

  QString label;
  for (const auto &a : accounts)
    if (a.id == selected)
      label = qs(a.label);
  setSelectedAccountId(qs(selected));
  setMyLabel(label);

  if (m_account) {
    const auto &s = m_account->state();
    setRotationPolicy(qs(std::string(fc::identity::toString(s.policy))));
    setDefaultDisclosure(qs(std::string(fc::identity::toString(s.defaultDisclosure))));
    setAlias(qs(s.alias));
    setMyPersona(qs(m_account->currentPersona().fingerprint()));
  } else {
    setMyPersona(QString());
  }

  // Last: the view switches screens on this, so everything it shows is ready.
  setIdentityState(m_account ? QStringLiteral("unlocked")
                             : accounts.empty() ? QStringLiteral("none")
                                                : QStringLiteral("locked"));
}

void ForumerBackend::saveAccount() {
  if (m_account && m_accounts && !m_accounts->save(*m_account))
    logEvent("failed to save account state for " + m_account->state().id);
}

QString ForumerBackend::createIdentity(QString label, QString password) {
  if (!m_accounts)
    return jsonResult(QStringLiteral("Not ready yet"));
  std::string phrase;
  auto result = m_accounts->create(label.trimmed().toStdString(), password.toStdString(), phrase);
  if (!result.ok())
    return jsonResult(qs(result.error));

  m_account = std::move(result.account);
  logEvent("created account " + m_account->state().id);
  publishIdentityState();

  const QString out = jsonResult(QString(), qs(phrase));
  fc::mnemonic::wipe(phrase);
  return out;
}

QString ForumerBackend::unlockIdentity(QString accountId, QString password) {
  if (!m_accounts)
    return QStringLiteral("Not ready yet");
  auto result = m_accounts->unlock(accountId.toStdString(), password.toStdString());
  if (!result.ok())
    return qs(result.error);

  m_accounts->select(accountId.toStdString());
  m_account = std::move(result.account);
  logEvent("unlocked account " + m_account->state().id);
  publishIdentityState();
  return QString();
}

QString ForumerBackend::restoreIdentity(QString phrase, QString label, QString password) {
  if (!m_accounts)
    return QStringLiteral("Not ready yet");

  // Recover the persona counter from authors already in the local store.
  // (More are found after catch-up; restore again later to pick those up.)
  const std::set<fc::Bytes> known = knownAuthors();
  std::string phraseStd = phrase.toStdString();
  auto result = m_accounts->restore(phraseStd, label.trimmed().toStdString(),
                                    password.toStdString(),
                                    [&known](const fc::Bytes &pk) { return known.count(pk) > 0; });
  fc::mnemonic::wipe(phraseStd);
  if (!result.ok())
    return qs(result.error);

  m_account = std::move(result.account);
  logEvent("restored account " + m_account->state().id + " (next persona " +
           std::to_string(m_account->state().nextIndex) + ")");
  publishIdentityState();
  return QString();
}

QString ForumerBackend::lockIdentity() {
  m_account.reset();
  publishIdentityState();
  return QString();
}

QString ForumerBackend::selectAccount(QString accountId) {
  if (!m_accounts || !m_accounts->select(accountId.toStdString()))
    return QStringLiteral("No such account");
  m_account.reset(); // switching means unlocking the other account
  publishIdentityState();
  return QString();
}

QString ForumerBackend::revealPhrase(QString password) {
  if (!m_account)
    return jsonResult(QStringLiteral("Unlock first"));
  // Re-check the password even though we're unlocked: the phrase is the whole
  // identity, so someone at an unattended screen shouldn't get it for free.
  auto check = m_accounts->unlock(m_account->state().id, password.toStdString());
  if (!check.ok())
    return jsonResult(qs(check.error));
  std::string phrase = fc::mnemonic::encode(m_account->masterSecret());
  const QString out = jsonResult(QString(), qs(phrase));
  fc::mnemonic::wipe(phrase);
  return out;
}

QString ForumerBackend::changePassword(QString oldPassword, QString newPassword) {
  if (!m_account)
    return QStringLiteral("Unlock first");
  return qs(m_accounts->changePassword(m_account->state().id, oldPassword.toStdString(),
                                       newPassword.toStdString()));
}

QString ForumerBackend::renameAccount(QString label) {
  if (!m_account)
    return QStringLiteral("Unlock first");
  label = label.trimmed();
  if (label.isEmpty())
    return QStringLiteral("An account needs a name");
  m_account->setLabel(label.toStdString());
  saveAccount();
  publishIdentityState();
  return QString();
}

QString ForumerBackend::deleteAccount(QString password) {
  if (!m_account)
    return QStringLiteral("Unlock first");
  const std::string id = m_account->state().id;
  auto check = m_accounts->unlock(id, password.toStdString());
  if (!check.ok())
    return qs(check.error);
  if (!m_accounts->remove(id))
    return QStringLiteral("Couldn't remove the account's files");
  m_account.reset();
  logEvent("removed account " + id + " from this device");
  publishIdentityState();
  return QString();
}

QString ForumerBackend::rotatePersona() {
  if (!m_account)
    return QStringLiteral("Unlock first");
  m_account->rotate();
  saveAccount();
  publishIdentityState();
  return QString();
}

QString ForumerBackend::chooseRotationPolicy(QString policy) {
  if (!m_account)
    return QStringLiteral("Unlock first");
  auto parsed = fc::identity::rotationPolicyFrom(policy.toStdString());
  if (!parsed)
    return QStringLiteral("Unknown rotation policy");
  m_account->setPolicy(*parsed);
  saveAccount();
  publishIdentityState();
  return QString();
}

QString ForumerBackend::chooseDefaultDisclosure(QString disclosure) {
  if (!m_account)
    return QStringLiteral("Unlock first");
  auto parsed = fc::identity::disclosureFrom(disclosure.toStdString());
  if (!parsed)
    return QStringLiteral("Unknown disclosure");
  m_account->setDefaultDisclosure(*parsed);
  saveAccount();
  publishIdentityState();
  return QString();
}

QString ForumerBackend::chooseAlias(QString alias) {
  if (!m_account)
    return QStringLiteral("Unlock first");
  alias = alias.trimmed();
  if (alias.toUtf8().size() > static_cast<int>(fc::post::kMaxAlias))
    return QStringLiteral("Alias is too long (max %1 characters)").arg(fc::post::kMaxAlias);
  m_account->setAlias(alias.toStdString());
  saveAccount();
  publishIdentityState();
  return QString();
}

// ── Posting ───────────────────────────────────────────────────────────────────

QString ForumerBackend::createTopic(QString title, QString body, QString domains,
                                    QString disclosure) {
  title = title.trimmed();
  if (title.isEmpty())
    return QStringLiteral("A topic needs a title");

  fc::post::Draft draft;
  draft.kind = fc::post::Kind::Post;
  draft.title = title.toStdString();
  draft.body = body.toStdString();
  draft.domains = fc::post::parseDomains(domains.toStdString());
  if (draft.domains.empty())
    draft.domains = {"general"};
  draft.timestampMs = nowMs();
  return publish(std::move(draft), disclosure);
}

QString ForumerBackend::replyToTopic(QString topicId, QString body, QString disclosure) {
  if (topicId.isEmpty())
    return QStringLiteral("No topic selected");
  if (body.trimmed().isEmpty())
    return QStringLiteral("A reply needs a body");

  fc::post::Draft draft;
  draft.kind = fc::post::Kind::Reply;
  draft.root = topicId.toStdString();
  draft.parent = topicId.toStdString(); // flat thread for now; nesting comes with the new UI
  draft.body = body.toStdString();
  draft.timestampMs = nowMs();
  return publish(std::move(draft), disclosure);
}

QString ForumerBackend::publish(fc::post::Draft draft, const QString &disclosureText) {
  if (!isContextReady() || !m_engine)
    return QStringLiteral("Store not ready");
  if (!m_account)
    return QStringLiteral("Unlock your identity to post");

  Disclosure disclosure = m_account->state().defaultDisclosure;
  if (!disclosureText.isEmpty()) {
    auto parsed = fc::identity::disclosureFrom(disclosureText.toStdString());
    if (!parsed)
      return QStringLiteral("Unknown disclosure \"%1\"").arg(disclosureText);
    disclosure = *parsed;
  }
  const std::string alias = m_account->state().alias;
  if (disclosure == Disclosure::Alias && alias.empty())
    return QStringLiteral("Set an alias first, or post as persona / anonymous");

  // Picking the persona may allocate a new one (Auto, Anonymous); save at once
  // so a crash can never hand the same persona out twice.
  const fc::identity::Persona persona = m_account->personaForPost(disclosure);
  saveAccount();

  const int pow = disclosure == Disclosure::Anonymous ? kAnonymousPowBits : kPowBits;
  fc::post::Error err = fc::post::Error::None;
  auto signedPost = fc::post::sign(std::move(draft), persona, disclosure, alias, pow, &err);
  if (!signedPost)
    return QStringLiteral("Couldn't sign the post: %1").arg(fc::post::describe(err));

  const nlohmann::json doc = {
      {"env", signedPost->toJson()},
      // Sort key for the backlog, as a string: ns overflow a JSON double.
      {"ts", std::to_string(toNs(signedPost->content.timestampMs))},
  };
  const bool isTopic = signedPost->content.kind == fc::post::Kind::Post;
  const std::string collection = isTopic ? kTopicsCollection : kRepliesCollection;
  const cloud_data_core::EngineResult r = m_engine->put(collection, signedPost->id, doc.dump());
  if (!r.success) {
    logEvent("put failed: " + r.error);
    return qs(r.error);
  }
  logEvent("stored " + collection + " " + signedPost->id + " as " +
           persona.fingerprint() + " (" + std::string(fc::identity::toString(disclosure)) + ")");

  // put() already reported the write through handleDocumentChanged(), so the
  // post is on screen; this marks it unconfirmed until delivery settles it.
  emit messageStateChanged(qs(signedPost->id), QStringLiteral("pending"), QString());
  publishIdentityState(); // the current persona may have changed (Auto)
  return QString();
}

std::optional<fc::post::Post> ForumerBackend::decodeDocument(const std::string &docId,
                                                             const std::string &json) const {
  const nlohmann::json doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object())
    return std::nullopt;
  if (doc.value("$deleted", false))
    return std::nullopt;
  auto env = doc.find("env");
  if (env == doc.end() || !env->is_string())
    return std::nullopt; // partially merged, or not one of ours

  auto post = fc::post::Post::fromJson(env->get<std::string>());
  if (!post) {
    logEvent("dropped " + docId + ": unreadable envelope");
    return std::nullopt;
  }
  if (post->id != docId) {
    logEvent("dropped " + docId + ": envelope id does not match");
    return std::nullopt;
  }
  const int minPow = post->disclosure == Disclosure::Anonymous ? kAnonymousPowBits : kPowBits;
  const fc::post::Error err = fc::post::verify(*post, minPow);
  if (err != fc::post::Error::None) {
    logEvent("dropped " + docId + ": " + fc::post::describe(err));
    return std::nullopt;
  }
  return post;
}

void ForumerBackend::emitPost(const fc::post::Post &post) {
  const auto &c = post.content;
  if (c.kind == fc::post::Kind::Post)
    emit topicReceived(qs(post.id), qs(c.title), qs(c.body), qs(post.authorDisplay()),
                       joinDomains(c.domains), toNs(c.timestampMs));
  else
    emit replyReceived(qs(post.id), qs(c.root), qs(c.body), qs(post.authorDisplay()),
                       toNs(c.timestampMs));
}

void ForumerBackend::handleDocumentChanged(const std::string &collectionId,
                                           const std::string &docId, const std::string &json) {
  auto post = decodeDocument(docId, json);
  if (!post)
    return;
  // A post must sit in the collection matching its kind.
  const bool isTopic = post->content.kind == fc::post::Kind::Post;
  if (collectionId != (isTopic ? kTopicsCollection : kRepliesCollection))
    return;
  emitPost(*post);
}

QString ForumerBackend::loadBacklog() {
  if (!m_engine)
    return QStringLiteral("[]");

  QJsonArray backlog;
  int dropped = 0;
  // Topics before replies, so the view never has to stand up a placeholder for
  // a topic that is a few entries further down the same array.
  for (const char *collection : {kTopicsCollection, kRepliesCollection}) {
    const cloud_data_core::EngineResult r = m_engine->query(collection, "{}");
    if (!r.success || !r.value.is_array()) {
      logEvent(std::string("backlog query of ") + collection + " failed: " + r.error);
      continue;
    }
    const bool isTopics = collection == std::string(kTopicsCollection);

    std::vector<fc::post::Post> posts;
    for (const auto &row : r.value) {
      if (!row.is_object() || !row.contains("docId") || !row["docId"].is_string())
        continue;
      auto post = decodeDocument(row["docId"].get<std::string>(), row.dump());
      if (!post || (post->content.kind == fc::post::Kind::Post) != isTopics) {
        ++dropped;
        continue;
      }
      posts.push_back(std::move(*post));
    }
    // SQLite returns rows unordered; sort by the author's timestamp.
    std::stable_sort(posts.begin(), posts.end(), [](const auto &a, const auto &b) {
      return a.content.timestampMs < b.content.timestampMs;
    });

    for (const auto &p : posts) {
      QJsonObject entry{
          {"kind", isTopics ? QStringLiteral("topic") : QStringLiteral("reply")},
          {"id", qs(p.id)},
          {"body", qs(p.content.body)},
          {"author", qs(p.authorDisplay())},
          {"ts", QString::number(toNs(p.content.timestampMs))},
      };
      if (isTopics) {
        entry.insert(QStringLiteral("title"), qs(p.content.title));
        entry.insert(QStringLiteral("domains"), joinDomains(p.content.domains));
      } else {
        entry.insert(QStringLiteral("topicId"), qs(p.content.root));
      }
      backlog.append(entry);
    }
  }

  logEvent("backlog: " + std::to_string(backlog.size()) + " verified post(s), " +
           std::to_string(dropped) + " dropped");
  return QString::fromUtf8(QJsonDocument(backlog).toJson(QJsonDocument::Compact));
}

std::set<fc::Bytes> ForumerBackend::knownAuthors() const {
  std::set<fc::Bytes> authors;
  if (!m_engine)
    return authors;
  for (const char *collection : {kTopicsCollection, kRepliesCollection}) {
    const cloud_data_core::EngineResult r = m_engine->query(collection, "{}");
    if (!r.success || !r.value.is_array())
      continue;
    for (const auto &row : r.value) {
      if (!row.is_object() || !row.contains("docId") || !row["docId"].is_string())
        continue;
      if (auto post = decodeDocument(row["docId"].get<std::string>(), row.dump()))
        authors.insert(post->publicKey);
    }
  }
  return authors;
}

bool ForumerBackend::openEngine() {
  const QString dir = dataDir() + QStringLiteral("/store");
  QDir().mkpath(dir);

  // A stable per-install CRDT peer id. It tie-breaks concurrent writes and
  // namespaces op ids, so it must survive restarts. Unrelated to identity:
  // it's never shown and never linked to a persona.
  const QString peerIdFile = dir + QStringLiteral("/peer_id");
  QString peerId = readTextFile(peerIdFile);
  if (peerId.isEmpty()) {
    peerId = newId();
    if (!writeTextFile(peerIdFile, peerId)) {
      logEvent("failed to persist peer id under " + dir.toStdString());
      return false;
    }
  }

  m_transport = std::make_unique<DeliveryModuleTransport>(modules());
  m_blobStore = std::make_unique<StorageModuleBlobStore>(modules());

  cloud_data_core::EngineConfig cfg;
  cfg.appName = kAppName;
  cfg.topicVersion = kTopicVersion;
  cfg.bucketBytes = kBucketBytes;

  m_engine = std::make_unique<cloud_data_core::CloudDataEngine>(
      dir.toStdString(), peerId.toStdString(), cfg, *m_transport, *m_blobStore);
  if (!m_engine->open()) {
    logEvent("could not open the local store under " + dir.toStdString());
    m_engine.reset();
    return false;
  }

  m_engine->setOnDocumentChanged([this](const std::string &collectionId,
                                        const std::string &docId,
                                        const std::string &json,
                                        const std::string &origin) {
    (void)origin;
    handleDocumentChanged(collectionId, docId, json);
  });
  m_transport->setPublishObserver(
      [this](const std::string &requestId, const std::vector<uint8_t> &payload) {
        notePublished(requestId, payload);
      });

  logEvent("local store open at " + dir.toStdString() + ", peer " + peerId.toStdString());

  // Composing runs off the local store, not the network: put() is durable and
  // queued for broadcast whether or not a node ever comes up.
  setNodeReady(true);
  return true;
}

void ForumerBackend::notePublished(const std::string &requestId,
                                         const std::vector<uint8_t> &payload) {
  if (requestId.empty())
    return;

  // The op carries the doc id, which is the message id the view knows a post
  // by — so decoding what was just published is what links a delivery request
  // id back to a row on screen. Anything that isn't an op (a snapshot pointer,
  // say) is not something the view tracks per-post.
  std::string collectionId;
  cloud_data_core::CrdtOp op;
  if (!cloud_data_core::sync_engine::decodeOp(payload, collectionId, op))
    return;

  m_pendingSends.insert(QString::fromStdString(requestId),
                        QString::fromStdString(op.docId));
}