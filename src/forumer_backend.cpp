#include "forumer_backend.h"

#include <algorithm>
#include <filesystem>
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
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <QVariantList>

// Generated umbrella: LogosModules (behind modules()) built from
// metadata.json#dependencies — the typed delivery_module wrapper.
// logos_types.h provides LogosResult.
#include "logos_sdk.h"
#include "logos_types.h"

// Forumer's own engine (lib/forumer_core).
#include "forumer_core/crypto.h"
#include "forumer_core/flood.h"
#include "forumer_core/mnemonic.h"
#include "forumer_core/thread.h"

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

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// Joining the forum topic: how long to wait before asking again, and how many
// attempts before giving up and leaving the failure on screen.
constexpr int kJoinRetryMs = 5000;
constexpr int kMaxJoinAttempts = 5;

// Digest schedule. The first two go out soon after joining — the first may
// leave before the node has found its peers on the topic, the second catches
// what the first missed — then one every few minutes, jittered so peers that
// started together don't sync in lockstep.
constexpr int kFirstDigestMs = 15'000;
constexpr int kSecondDigestMs = 60'000;
constexpr int kDigestIntervalMs = 180'000;
constexpr int kDigestJitterMs = 30'000;
constexpr int kMinDigestGapMs = 5'000;  // "Catch up" mashed repeatedly

// When a peer's digest lists posts we don't have, send our own digest soon
// (so it answers) rather than waiting for our next periodic one — at most
// once per kPromptedDigestGapMs, so two peers can never ping-pong digests.
constexpr int kPromptedDigestDelayMs = 1'000;
constexpr int kPromptedDigestJitterMs = 2'000;
constexpr qint64 kPromptedDigestGapMs = 30'000;

// Outbox: how often to look for posts due a retry.
constexpr int kRetryTickMs = 5'000;

// Digest answers. Wait a moment before re-sending, and skip any post someone
// else re-sent (or that we saw at all) within kSeenWindowMs — so one digest
// in a busy forum doesn't trigger the same post from every peer at once.
constexpr int kAnswerDelayMinMs = 500;
constexpr int kAnswerDelayMaxMs = 2'500;
constexpr int kAnswerBatchGapMs = 5'000;
constexpr qint64 kSeenWindowMs = 30'000;
constexpr int kSeenPruneThreshold = 4'096;

// Small text-file helpers (the per-install channel peer id).
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

// Envelope timestamps are ms; the view uses ns.
qint64 toNs(int64_t ms) { return static_cast<qint64>(ms) * 1000000LL; }

int jitter(int maxMs) { return static_cast<int>(QRandomGenerator::global()->bounded(maxMs + 1)); }
} // namespace

const char ForumerBackend::kForum[] = "public";

ForumerBackend::ForumerBackend() {
  // Runs in the ui-host process before the context is wired.
  logEvent("ctor — backend constructed (context not yet wired)");
  if (!fc::crypto::initCrypto())
    logEvent("libsodium failed to initialise — signing and unlocking will fail");
  setAppVersion(QStringLiteral(FORUMER_VERSION));
  m_topic = fc::sync::contentTopic(kForum);
  setTopic(qs(m_topic));
}

ForumerBackend::~ForumerBackend() {
  logEvent("dtor — backend destroyed");
}

void ForumerBackend::onContextReady() {
  logEvent("onContextReady — context wired, scheduling node bootstrap");

  // Accounts are purely local, so they're available before the network is.
  m_accounts = std::make_unique<fc::AccountStore>(
      std::filesystem::path((dataDir() + QStringLiteral("/accounts")).toStdString()));
  publishIdentityState();

  // createNode()/start() are synchronous and can block for a moment. Defer the
  // bootstrap to the next event-loop turn so the QML replica comes up promptly.
  QTimer::singleShot(0, this, [this]() { bootstrap(); });
}

void ForumerBackend::bootstrap() {
  // --- Subscribe to delivery_module events before starting the node ---------

  // Node health. connectionStateChanged (Connected / PartiallyConnected /
  // Disconnected) is the only honest account of connectivity we get, so it
  // drives the status PROP outright.
  modules().delivery_module.on(
      "connectionStateChanged", [this](const QVariantList &data) {
        if (data.isEmpty())
          return;
        m_connectionState = data.at(0).toString();
        logEvent("connection state -> " + m_connectionState.toStdString());
        refreshStatus();
      });

  // Inbound payloads. Plain relay (data[1] is the content topic) is the path
  // in use; the channel handler (data[0] is the channel id, which equals the
  // content topic) only matters if reliable channels are switched back on.
  // A payload arriving both ways is stored once (posts dedupe by id).
  modules().delivery_module.on("messageReceived", [this](const QVariantList &data) {
    if (data.size() >= 3)
      handlePayload(data.at(1).toString(), data.at(2).toByteArray());
  });
  modules().delivery_module.on("channelMessageReceived", [this](const QVariantList &data) {
    if (data.size() >= 3)
      handlePayload(data.at(0).toString(), data.at(2).toByteArray());
  });

  // Node startup. start() is dispatch-only in delivery_module v0.2.1, so this
  // event — not start()'s return — says whether the node came up. The join
  // does not hang off it (see the ordering below); this re-drives a join that
  // failed while the node was still booting.
  modules().delivery_module.on("nodeStarted", [this](const QVariantList &data) {
    const bool ok = !data.isEmpty() && data.at(0).toBool();
    const QString message = data.value(1).toString();
    logEvent("nodeStarted success=" + std::to_string(ok) + " " + message.toStdString());
    if (!ok) {
      setStatus(QStringLiteral("Node failed to start: %1").arg(message));
      return;
    }
    if (m_joined) {
      refreshStatus();
      return;
    }
    // Fresh retry budget, deferred off the event callback (calling back into
    // the module from inside its own event dispatch is what times out).
    m_joinAttempts = 0;
    QTimer::singleShot(0, this, [this]() { joinForum(); });
  });

  // Delivery outcomes for our own posts, keyed by the request id publish()
  // handed back. Plain-send events lead with the request id; channel events
  // lead with the channel id and carry the request id second.
  modules().delivery_module.on("messagePropagated", [this](const QVariantList &data) {
    settleSend(data.value(0).toString(), QStringLiteral("propagated"), QString());
  });
  modules().delivery_module.on("messageSent", [this](const QVariantList &data) {
    settleSend(data.value(0).toString(), QStringLiteral("sent"), QString());
  });
  modules().delivery_module.on("messageError", [this](const QVariantList &data) {
    settleSend(data.value(0).toString(), QStringLiteral("failed"), data.value(2).toString());
  });
  modules().delivery_module.on("channelMessageSent", [this](const QVariantList &data) {
    settleSend(data.value(1).toString(), QStringLiteral("sent"), QString());
  });
  modules().delivery_module.on("channelMessageError", [this](const QVariantList &data) {
    settleSend(data.value(1).toString(), QStringLiteral("failed"), data.value(2).toString());
  });

  // --- Open the post log -----------------------------------------------------
  // Before the node, deliberately: reading and composing never touch the
  // network, so the forum is usable while the node bootstraps — or if it
  // never comes up at all. Unsent posts wait in the outbox.
  if (!openStore()) {
    setStatus(QStringLiteral("Local store unavailable — posts can't be saved"));
    return;
  }

  // --- Create + start the node -----------------------------------------------
  // The *layered* config shape: `mode` and `preset` are keys the layered
  // parser consumes, which earns ephemeral p2p ports and the host's
  // per-instance localStoragePath. Any bare WakuNodeConf key at the top level
  // (logLevel, tcpPort, …) reclassifies the whole config as the legacy flat
  // shape, which binds fixed ports — two instances on one machine collide.
  const QJsonObject cfg{
      {"mode", "Core"},
      {"preset", "logos.test"},
  };
  const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

  LogosResult created = modules().delivery_module.createNode(cfgJson);
  if (!created.success) {
    // delivery_module is a singleton shared across Basecamp apps, so another
    // app may have created and started the node already. No nodeStarted will
    // fire for us then — join directly.
    logEvent("createNode failed (node may already be running): " +
             created.getError().toStdString());
    joinForum();
    return;
  }
  logEvent("createNode succeeded");

  // Join *before* start(), as use-delivery-module documents: the node is
  // built but not yet bootstrapping, so the call doesn't queue behind
  // discovery and time out, and nothing can arrive before we're listening.
  joinForum();

  setStatus(QStringLiteral("Starting node…"));
  LogosResult started = modules().delivery_module.start();
  if (!started.success) {
    setStatus(QStringLiteral("Node failed to start: %1").arg(started.getError()));
    logEvent("start failed: " + started.getError().toStdString());
    return;
  }
  logEvent("start dispatched — waiting for nodeStarted");
}

bool ForumerBackend::openStore() {
  const QString dir = dataDir();
  QDir().mkpath(dir);

  // A stable per-install id for reliable channels (SDS sender id). Never
  // shown and never linked to an account or persona.
  const QString peerIdFile = dir + QStringLiteral("/peer_id");
  QString peerId = readTextFile(peerIdFile);
  if (peerId.isEmpty()) {
    peerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!writeTextFile(peerIdFile, peerId)) {
      logEvent("failed to persist peer id under " + dir.toStdString());
      return false;
    }
  }

  const std::filesystem::path file = (dir + QStringLiteral("/posts/posts.sqlite3")).toStdString();
  std::string error;
  m_posts = fc::PostStore::open(file, &error);
  if (!m_posts) {
    logEvent("could not open the post log: " + error);
    return false;
  }
  // Plain relay, not reliable channels: SDS's causal ordering holds back the
  // very digest answers that repair a lost post (see delivery_module_transport.h).
  m_transport = std::make_unique<DeliveryModuleTransport>(modules(), peerId.toStdString(),
                                                          /*useChannels=*/false);

  logEvent("post log open at " + file.string() + " (" + std::to_string(m_posts->count()) +
           " posts), topic " + m_topic);
  setNodeReady(true);
  publishSyncState();
  return true;
}

void ForumerBackend::joinForum() {
  if (m_joined || !m_transport)
    return;

  ++m_joinAttempts;
  const std::string error = m_transport->join(m_topic);
  if (!error.empty()) {
    setStatus(QStringLiteral("Couldn't join the forum: %1").arg(qs(error)));
    logEvent("join attempt " + std::to_string(m_joinAttempts) + " failed: " + error);
    // The common failure is a timeout while the node bootstraps, which passes
    // on its own. Composing is unaffected — posts wait in the outbox.
    if (m_joinAttempts < kMaxJoinAttempts)
      QTimer::singleShot(kJoinRetryMs, this, [this]() { joinForum(); });
    return;
  }

  m_joined = true;
  refreshStatus();
  logEvent(std::string("joined ") + m_topic +
           (m_transport->usingChannels() ? " (reliable channel)" : " (plain relay)"));

  // Outbox retries: anything left unsent from before a restart goes out on
  // the first tick.
  if (!m_retryTimer) {
    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(kRetryTickMs);
    connect(m_retryTimer, &QTimer::timeout, this, [this]() { retryDue(false); });
    m_retryTimer->start();
  }
  scheduleDigest(kFirstDigestMs);
}

void ForumerBackend::refreshStatus() {
  if (!m_joined)
    return; // bootstrap's own progress messages own the status until then

  if (m_connectionState.isEmpty()) {
    // Joined locally, but nothing has said we have peers yet — and a node
    // with none receives nothing while still publishing happily.
    setStatus(QStringLiteral("Joined — waiting for peers"));
    return;
  }
  setStatus(m_connectionState);
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
    QJsonArray followed;
    for (const auto &d : s.followed)
      followed.append(qs(d));
    setFollowedDomains(QString::fromUtf8(QJsonDocument(followed).toJson(QJsonDocument::Compact)));
    setMyPersona(qs(m_account->currentPersona().fingerprint()));
  } else {
    setMyPersona(QString());
    setFollowedDomains(QStringLiteral("[]"));
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

  // Recover the persona counter from authors already in the local post log.
  // (More are found after catch-up; restore again later to pick those up.)
  const std::set<fc::Bytes> known = m_posts ? m_posts->authors() : std::set<fc::Bytes>();
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


// ── Followed domains ──────────────────────────────────────────────────────────

QString ForumerBackend::followDomain(QString domain) {
  if (!m_account)
    return QStringLiteral("Unlock first");
  const auto names = fc::post::normalizeDomains({domain.toStdString()});
  if (names.empty())
    return QStringLiteral("Not a valid domain");
  if (!m_account->follow(names.front()))
    return QStringLiteral("You can follow up to %1 domains").arg(fc::identity::AccountState::kMaxFollowed);
  saveAccount();
  publishIdentityState();
  return QString();
}

QString ForumerBackend::unfollowDomain(QString domain) {
  if (!m_account)
    return QStringLiteral("Unlock first");
  const auto names = fc::post::normalizeDomains({domain.toStdString()});
  if (!names.empty())
    m_account->unfollow(names.front());
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
  draft.forum = kForum;
  draft.title = title.toStdString();
  draft.body = body.toStdString();
  draft.domains = fc::post::parseDomains(domains.toStdString());
  if (draft.domains.empty())
    draft.domains = {"general"};
  draft.timestampMs = nowMs();
  return publish(std::move(draft), disclosure);
}

QString ForumerBackend::replyToTopic(QString topicId, QString body, QString disclosure) {
  return replyToPost(topicId, body, disclosure);
}

QString ForumerBackend::replyToPost(QString postId, QString body, QString disclosure) {
  if (postId.isEmpty())
    return QStringLiteral("Nothing to reply to");
  if (body.trimmed().isEmpty())
    return QStringLiteral("A reply needs a body");
  if (!m_posts)
    return QStringLiteral("Store not ready");

  // Where the reply goes follows from what it answers (two levels at most,
  // see forumer_core/thread.h) — so we need that post.
  auto answering = m_posts->get(postId.toStdString());
  if (!answering)
    return QStringLiteral("That post isn't on this device yet");
  const fc::thread::Target target = fc::thread::replyTarget(*answering);

  fc::post::Draft draft;
  draft.kind = fc::post::Kind::Reply;
  draft.forum = kForum;
  draft.root = target.root;
  draft.parent = target.parent;
  draft.body = body.toStdString();
  draft.timestampMs = nowMs();
  return publish(std::move(draft), disclosure);
}

QString ForumerBackend::publish(fc::post::Draft draft, const QString &disclosureText) {
  if (!isContextReady() || !m_posts)
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

  // Sender-side flood limit, per account (so rotating personas or posting
  // anonymously doesn't lift it). Peers enforce the same numbers per persona.
  {
    const qint64 now = nowMs();
    const size_t recent = m_posts->countOwn(m_account->state().id, draft.kind, now - fc::flood::kWindowMs);
    if (!fc::flood::withinLimit(draft.kind, recent))
      return QStringLiteral("That's %1 %2 in the last hour — the most one account can post. Try again a little later.")
          .arg(fc::flood::maxPerWindow(draft.kind))
          .arg(draft.kind == fc::post::Kind::Post ? QStringLiteral("topics") : QStringLiteral("replies"));
  }

  // Picking the persona may allocate a new one (Auto, Anonymous); save at once
  // so a crash can never hand the same persona out twice.
  const fc::identity::Persona persona = m_account->personaForPost(disclosure);
  saveAccount();

  const int pow = disclosure == Disclosure::Anonymous ? kAnonymousPowBits : kPowBits;
  fc::post::Error err = fc::post::Error::None;
  auto signedPost = fc::post::sign(std::move(draft), persona, disclosure, alias, pow, &err);
  if (!signedPost)
    return QStringLiteral("Couldn't sign the post: %1").arg(fc::post::describe(err));

  // Durable before anything else: once these two writes land, the post
  // survives a crash and goes out on the next retry tick at the latest.
  const qint64 now = nowMs();
  if (m_posts->insert(*signedPost, now) == fc::PostStore::Insert::Failed ||
      !m_posts->enqueue(signedPost->id, m_account->state().id, now)) {
    logEvent("could not store our own post " + signedPost->id);
    return QStringLiteral("Couldn't save the post");
  }
  logEvent("stored " + signedPost->id + " as " + persona.fingerprint() + " (" +
           std::string(fc::identity::toString(disclosure)) + ")");

  emitPost(*signedPost);
  emit messageStateChanged(qs(signedPost->id), QStringLiteral("pending"), QString());
  sendOwn(signedPost->id);
  publishIdentityState(); // the current persona may have changed (Auto)
  publishSyncState();
  return QString();
}

void ForumerBackend::emitPost(const fc::post::Post &post) {
  const auto &c = post.content;
  if (c.kind == fc::post::Kind::Post)
    emit topicReceived(qs(post.id), qs(c.title), qs(c.body), qs(post.authorDisplay()),
                       joinDomains(c.domains), toNs(c.timestampMs));
  else
    emit replyReceived(qs(post.id), qs(c.root), qs(c.parent), qs(c.body), qs(post.authorDisplay()),
                       toNs(c.timestampMs));
}

QString ForumerBackend::loadBacklog() {
  if (!m_posts)
    return QStringLiteral("[]");

  // Delivery state of our own posts, so they come back marked after a restart.
  QHash<QString, QString> states;
  for (const auto &e : m_posts->outbox())
    states.insert(qs(e.postId), QLatin1String(fc::toString(e.state)));

  // all() is topics first, oldest first — so the view never has to stand up a
  // placeholder for a topic a few entries further down.
  QJsonArray backlog;
  for (const auto &p : m_posts->all()) {
    const bool isTopic = p.content.kind == fc::post::Kind::Post;
    QJsonObject entry{
        {"kind", isTopic ? QStringLiteral("topic") : QStringLiteral("reply")},
        {"id", qs(p.id)},
        {"body", qs(p.content.body)},
        {"author", qs(p.authorDisplay())},
        {"ts", QString::number(toNs(p.content.timestampMs))},
    };
    if (isTopic) {
      entry.insert(QStringLiteral("title"), qs(p.content.title));
      entry.insert(QStringLiteral("domains"), joinDomains(p.content.domains));
    } else {
      entry.insert(QStringLiteral("topicId"), qs(p.content.root));
      entry.insert(QStringLiteral("parentId"), qs(p.content.parent));
    }
    const auto state = states.constFind(qs(p.id));
    if (state != states.constEnd())
      entry.insert(QStringLiteral("state"), state.value());
    backlog.append(entry);
  }

  logEvent("backlog: " + std::to_string(backlog.size()) + " post(s)");
  return QString::fromUtf8(QJsonDocument(backlog).toJson(QJsonDocument::Compact));
}

// ── Inbound ───────────────────────────────────────────────────────────────────

void ForumerBackend::handlePayload(const QString &topic, const QByteArray &payload) {
  if (!m_posts || topic.toStdString() != m_topic)
    return; // another app's traffic — delivery_module is shared

  const std::vector<uint8_t> bytes(payload.begin(), payload.end());
  fc::sync::DecodeResult decoded = fc::sync::decode(bytes);
  if (!decoded.message) {
    logEvent(std::string("ignored a message: ") + fc::sync::describe(decoded.error));
    return;
  }
  if (decoded.message->type == fc::sync::MessageType::Post)
    handlePost(std::move(*decoded.message->post));
  else
    handleDigest(decoded.message->digest);
}

std::string ForumerBackend::checkPost(const fc::post::Post &post) const {
  if (post.content.forum != kForum)
    return "belongs to another forum";
  const int minPow = post.disclosure == Disclosure::Anonymous ? kAnonymousPowBits : kPowBits;
  const fc::post::Error err = fc::post::verify(post, minPow);
  if (err != fc::post::Error::None)
    return fc::post::describe(err);
  if (!fc::sync::acceptTimestamp(post.content.timestampMs, nowMs()))
    return "dated in the future";
  // A reply must sit where the thread rules allow, judged against its parent
  // when we already hold it (a reply that arrives first is judged by the view,
  // which shows it at level 1 until the parent turns up).
  if (post.content.kind == fc::post::Kind::Reply) {
    const auto placement = fc::thread::checkPlacement(post, m_posts->get(post.content.parent));
    if (placement != fc::thread::Placement::Ok)
      return fc::thread::describe(placement);
  }
  // Per-persona flood limit, on the post's own timestamps: no more than the
  // hourly limit in the hour up to this post (see forumer_core/flood.h).
  const int64_t ts = post.content.timestampMs;
  const size_t inWindow = m_posts->countByAuthor(post.publicKey, post.content.kind,
                                                 ts - fc::flood::kWindowMs, ts);
  if (!fc::flood::withinLimit(post.content.kind, inWindow))
    return "persona is over its hourly limit";
  return {};
}

void ForumerBackend::handlePost(fc::post::Post post) {
  // Seen on the wire, valid or not — a peer about to answer a digest with
  // this post can skip it.
  m_lastSeenOnWire.insert(qs(post.id), nowMs());

  if (m_posts->contains(post.id))
    return; // already have it (including our own echo)

  const std::string problem = checkPost(post);
  if (!problem.empty()) {
    logEvent("dropped " + post.id + ": " + problem);
    return;
  }

  // Keys we've never seen (anonymous posts, fresh personas) share a budget.
  // Over it, the post isn't stored yet; the next digest exchange offers it
  // again, so this delays rather than loses.
  if (!m_posts->hasAuthor(post.publicKey) && !m_newKeyBudget.take(nowMs())) {
    m_lastSeenOnWire.remove(qs(post.id));   // let peers offer it again
    logEvent("deferred " + post.id + ": new-key budget used up");
    return;
  }

  switch (m_posts->insert(post, nowMs())) {
  case fc::PostStore::Insert::Added:
    ++m_receivedCount;
    logEvent("received " + post.id);
    emitPost(post);
    publishSyncState();
    break;
  case fc::PostStore::Insert::Duplicate:
    break;
  case fc::PostStore::Insert::Failed:
    logEvent("could not store " + post.id);
    break;
  }
}

void ForumerBackend::handleDigest(const fc::sync::Digest &digest) {
  const qint64 now = nowMs();

  // Does the sender hold posts we don't? Then ask for them now: our digest
  // tells it what we hold, and it answers with the rest. (This is what lets a
  // peer that was offline while others posted catch up within seconds of
  // someone else coming online, not minutes.) Look across the digest's whole
  // window, but never further back than twice ours.
  const int64_t since = std::max<int64_t>(digest.sinceMs, now - 2 * fc::sync::kWindowMs);
  if (now - m_lastPromptedDigestMs >= kPromptedDigestGapMs &&
      fc::sync::listsUnknown(digest, m_posts->recent(since))) {
    m_lastPromptedDigestMs = now;
    logEvent("digest: peer holds posts we don't — sending ours");
    QTimer::singleShot(kPromptedDigestDelayMs + jitter(kPromptedDigestJitterMs), this, [this]() {
      if (nowMs() - m_lastDigestMs >= kMinDigestGapMs)
        sendDigest("prompted");
    });
  }

  // Never answer about posts older than our own window, however far back the
  // digest reaches: that would let one message pull our whole history.
  const auto held = m_posts->recent(now - fc::sync::kWindowMs);
  const auto missing = fc::sync::missingFrom(digest, held);
  if (missing.empty())
    return;

  for (const auto &id : missing)
    m_answerQueue.insert(qs(id));
  logEvent("digest: peer is missing " + std::to_string(missing.size()) + " post(s) we hold");

  if (!m_answerFlushScheduled) {
    m_answerFlushScheduled = true;
    const int delay = kAnswerDelayMinMs + jitter(kAnswerDelayMaxMs - kAnswerDelayMinMs);
    QTimer::singleShot(delay, this, [this]() { flushAnswers(); });
  }
}

// ── Outbound ──────────────────────────────────────────────────────────────────

void ForumerBackend::sendOwn(const std::string &id) {
  if (!m_joined || !m_transport)
    return; // stays pending; the retry tick sends it once we've joined

  auto post = m_posts->get(id);
  if (!post) {
    logEvent("outbox entry " + id + " has no stored post");
    return;
  }

  m_posts->markAttempt(id, nowMs());
  m_lastSeenOnWire.insert(qs(id), nowMs());
  const auto r = m_transport->publish(m_topic, fc::sync::encodePost(*post));
  if (!r.ok) {
    m_posts->markState(id, fc::SendState::Failed, r.error);
    logEvent("send of " + id + " failed: " + r.error);
    emit messageStateChanged(qs(id), QStringLiteral("failed"), qs(r.error));
    publishSyncState();
    return;
  }
  // A retry re-sends identical bytes under a new request id; the outcome of
  // whichever attempt settles first is what counts.
  m_pendingSends.insert(qs(r.requestId), qs(id));
}

bool ForumerBackend::resend(const std::string &id) {
  const qint64 now = nowMs();
  const auto seen = m_lastSeenOnWire.constFind(qs(id));
  if (seen != m_lastSeenOnWire.constEnd() && now - seen.value() < kSeenWindowMs)
    return true; // someone (maybe us) just sent it
  if (!m_resendBudget.take(now))
    return false; // over the re-send budget: keep it queued

  auto post = m_posts->get(id);
  if (!post)
    return true;
  m_lastSeenOnWire.insert(qs(id), now);
  const auto r = m_transport->publish(m_topic, fc::sync::encodePost(*post));
  if (!r.ok)
    logEvent("re-send of " + id + " failed: " + r.error);
  return true;
}

void ForumerBackend::flushAnswers() {
  m_answerFlushScheduled = false;
  if (!m_joined || !m_transport || m_answerQueue.isEmpty())
    return;

  // Newest first is what missingFrom() produced, but the queue is a set by
  // now; order doesn't matter for correctness, only the cap does.
  int sent = 0;
  auto it = m_answerQueue.begin();
  while (it != m_answerQueue.end() && sent < static_cast<int>(fc::sync::kMaxAnswer)) {
    if (!resend(it->toStdString()))
      break; // re-send budget used up: the rest waits for the next batch
    it = m_answerQueue.erase(it);
    ++sent;
  }
  logEvent("answered a digest with up to " + std::to_string(sent) + " post(s)");

  if (!m_answerQueue.isEmpty()) {
    m_answerFlushScheduled = true;
    QTimer::singleShot(kAnswerBatchGapMs, this, [this]() { flushAnswers(); });
  }

  // Keep the seen-map from growing without bound.
  if (m_lastSeenOnWire.size() > kSeenPruneThreshold) {
    const qint64 cutoff = nowMs() - kSeenWindowMs;
    for (auto s = m_lastSeenOnWire.begin(); s != m_lastSeenOnWire.end();)
      s = s.value() < cutoff ? m_lastSeenOnWire.erase(s) : std::next(s);
  }
}

void ForumerBackend::retryDue(bool force) {
  if (!m_joined || !m_posts)
    return;
  const qint64 now = nowMs();
  for (const auto &entry : m_posts->unsent()) {
    if (force || fc::sync::dueForRetry(entry, now)) {
      logEvent("retrying " + entry.postId + " (attempt " + std::to_string(entry.attempts + 1) + ")");
      sendOwn(entry.postId);
    }
  }
}

void ForumerBackend::sendDigest(const char *why) {
  if (!m_joined || !m_transport || !m_posts)
    return;
  const qint64 now = nowMs();
  const auto held = m_posts->recent(now - fc::sync::kWindowMs);
  const fc::sync::Digest digest = fc::sync::makeDigest(held, now - fc::sync::kWindowMs);
  const auto r = m_transport->publish(m_topic, fc::sync::encodeDigest(digest));
  if (!r.ok) {
    logEvent(std::string("digest (") + why + ") failed: " + r.error);
    return;
  }
  m_lastDigestMs = now;
  logEvent(std::string("digest (") + why + "): " + std::to_string(digest.have.size()) +
           " post(s) held");
  publishSyncState();
}

void ForumerBackend::scheduleDigest(int delayMs) {
  QTimer::singleShot(delayMs, this, [this]() {
    sendDigest(m_digestRound == 0 ? "joined" : "periodic");
    ++m_digestRound;
    scheduleDigest(m_digestRound == 1 ? kSecondDigestMs - kFirstDigestMs
                                      : kDigestIntervalMs + jitter(kDigestJitterMs));
  });
}

QString ForumerBackend::catchUp() {
  if (!m_joined)
    return QStringLiteral("Not connected to the forum yet");
  if (nowMs() - m_lastDigestMs < kMinDigestGapMs)
    return QString(); // one just went out
  sendDigest("catch-up");
  return QString();
}

QString ForumerBackend::retryUnsent() {
  if (!m_joined)
    return QStringLiteral("Not connected to the forum yet");
  retryDue(true);
  return QString();
}

void ForumerBackend::settleSend(const QString &requestId, const QString &state,
                                const QString &detail) {
  if (requestId.isEmpty())
    return;
  const auto it = m_pendingSends.constFind(requestId);
  if (it == m_pendingSends.constEnd())
    return; // a digest, a re-send, or another app's message

  const QString postId = it.value();
  // "propagated" is a waypoint: the message reached the network but isn't
  // confirmed yet, so keep the mapping for the event that settles it.
  if (state != QLatin1String("propagated")) {
    m_pendingSends.remove(requestId);
    if (m_posts) {
      // A late failure from an earlier attempt must not undo a success.
      auto entry = m_posts->outboxEntry(postId.toStdString());
      if (entry && entry->state == fc::SendState::Sent)
        return;
      m_posts->markState(postId.toStdString(),
                         state == QLatin1String("sent") ? fc::SendState::Sent : fc::SendState::Failed,
                         detail.toStdString());
    }
  }

  logEvent("send " + requestId.toStdString() + " -> " + state.toStdString() +
           (detail.isEmpty() ? "" : " (" + detail.toStdString() + ")"));
  emit messageStateChanged(postId, state, detail);
  publishSyncState();
}

void ForumerBackend::publishSyncState() {
  if (!m_posts)
    return;
  setUnsentCount(static_cast<int>(m_posts->unsent().size()));

  QStringList parts;
  if (m_lastDigestMs > 0)
    parts << QStringLiteral("synced %1")
                 .arg(QDateTime::fromMSecsSinceEpoch(m_lastDigestMs).toString(QStringLiteral("hh:mm")));
  if (m_receivedCount > 0)
    parts << QStringLiteral("%1 received").arg(m_receivedCount);
  setSyncInfo(parts.join(QStringLiteral(" · ")));
}