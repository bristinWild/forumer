#include "forumer_backend.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QPointer>
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

// How far back live digests reach: sync::kWindowMs (48 h), or
// FORUMER_LIVE_WINDOW_HOURS for testing and demos - with 1, everything older
// than an hour comes through the history walk instead. Read once.
int64_t liveWindowMs() {
  static const int64_t window = []() -> int64_t {
    bool ok = false;
    const int hours = qEnvironmentVariableIntValue("FORUMER_LIVE_WINDOW_HOURS", &ok);
    return ok && hours >= 1 && hours <= 48 ? int64_t(hours) * 3'600'000 : fc::sync::kWindowMs;
  }();
  return window;
}

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
// How often to ask the node for its connection state (see pollConnectionStatus).
constexpr int kStatusPollMs = 15'000;

// Digest answers. Wait a moment before re-sending, and skip any post someone
// else re-sent (or that we saw at all) within kSeenWindowMs — so one digest
// in a busy forum doesn't trigger the same post from every peer at once.
constexpr int kAnswerDelayMinMs = 500;
constexpr int kAnswerDelayMaxMs = 2'500;
constexpr int kAnswerBatchGapMs = 5'000;
constexpr qint64 kSeenWindowMs = 30'000;

// History walk: wait this long for answers to one range request, repeat the
// window while a round still brings this many new posts (answers are capped
// at sync::kMaxAnswer per peer), at most this many rounds per window. Start
// a little after learning there is older history, so the live digests settle
// first. Answer one peer's range request at a time, and never about more
// than two windows' worth of time at once.
constexpr int kHistoryRoundMs = 12'000;
constexpr int kHistoryRepeatAt = 24;
constexpr int kHistoryMaxRounds = 20;
constexpr int kHistoryStartDelayMs = 20'000;
// Store nodes: asked once per session, a few seconds after joining.
constexpr int kStoreStartDelayMs = 6'000;
constexpr int64_t kStoreTimeoutMs = 15'000;
constexpr int kStoreMaxPages = 20;                       // x 100 messages, per node
constexpr int64_t kStoreLookbackMs = 30LL * 86'400'000;  // ask for up to a month...
constexpr int64_t kStoreShortLookbackMs = 86'400'000 - 60'000;  // ...or a day, where a node caps it
constexpr qint64 kRangeAnswerGapMs = 3'000;
constexpr int64_t kRangeMaxSpanMs = 2 * fc::sync::kHistorySpanMs;
constexpr int64_t kLoadOlderSpanMs = 4 * fc::sync::kHistorySpanMs;

// Catch-up bursts: after a digest, wait this long for answers; if at least
// kFollowUpAt posts arrived (or were deferred by the new-key budget), send
// another digest. At most kMaxFollowUps in a row, then the periodic schedule.
constexpr int kFollowUpWaitMs = 8'000;
constexpr int kFollowUpAt = 16;
constexpr int kMaxFollowUps = 15;
// How often "covered until" (we were online and syncing up to here) is saved.
constexpr qint64 kCoveredSaveGapMs = 10 * 60'000;
constexpr int kSeenPruneThreshold = 4'096;

// Owner-only permissions (0700 directories, 0600 files) for everything this
// app keeps on disk. Best effort: filesystems without POSIX modes keep theirs.
void ownerOnly(const QString &path, bool directory) {
  std::error_code ec;
  std::filesystem::permissions(path.toStdString(),
                               directory ? std::filesystem::perms::owner_all
                                         : std::filesystem::perms::owner_read |
                                               std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
}

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
  ownerOnly(path, false);
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

// Run `fn` on a worker thread and wait for it here while this thread's event
// loop keeps running (review F16): Argon2id (~256 MiB, about a second) and
// proof-of-work (seconds for a long anonymous post) used to stall the backend,
// network events included. Callers hold m_busy, so no other identity or
// posting slot runs in the meantime; everything else (sync, timers) does.
template <typename F>
std::invoke_result_t<F> runOffThread(F fn) {
  std::optional<std::invoke_result_t<F>> out;
  QEventLoop loop;
  std::thread worker([&]() {
    out.emplace(fn());
    QMetaObject::invokeMethod(&loop, [&loop]() { loop.quit(); }, Qt::QueuedConnection);
  });
  loop.exec(QEventLoop::ExcludeUserInputEvents);
  worker.join();
  return std::move(*out);
}

// Overwrite a password or phrase we were handed, once it has been used.
void wipe(QString &secret) { secret.fill(QChar(0)); }
void wipe(std::string &secret) { fc::mnemonic::wipe(secret); }

// Sets a flag for the scope of one slot that runs work off the thread.
struct BusyScope {
  bool &flag;
  explicit BusyScope(bool &f) : flag(f) { flag = true; }
  ~BusyScope() { flag = false; }
};

const char kBusy[] = "Busy with the previous request — try again in a moment";
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

  setDevMode(qEnvironmentVariableIsSet("FORUMER_DEV"));

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
  //
  // The network: logos.dev unless chosen otherwise (Settings → network).
  // logos.test runs RLN and refuses to start a node without an active RLN
  // membership (registered on the LEZ testnet); logos.dev runs without it.
  // The two are separate fleets: peers only see others on the same one.
  const QJsonObject cfg{
      {"mode", "Core"},
      {"preset", networkPreset()},
  };
  const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
  logEvent("network: " + networkPreset().toStdString());
  setNetwork(networkPreset());
  setNetworkNext(networkPreset());

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
  ownerOnly(dir, true);

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

  // Bounded log (F18): the oldest posts beyond kMaxStoredPosts go, and the
  // automatic history walk doesn't fetch them back.
  if (const size_t pruned = m_posts->pruneTo(kMaxStoredPosts)) {
    if (auto oldest = m_posts->oldestTimestamp())
      m_posts->setMeta("history_limit", std::to_string(*oldest));
    logEvent("pruned " + std::to_string(pruned) + " oldest post(s) beyond " +
             std::to_string(kMaxStoredPosts));
  }
  logEvent("post log open at " + file.string() + " (" + std::to_string(m_posts->count()) +
           " posts), topic " + m_topic);
  ownerOnly(peerIdFile, false);
  // An account unlocked before the log opened gets its own posts now.
  if (m_account)
    attachAccount();
  loadHistoryState();
  setNodeReady(true);
  publishSyncState();

  // The hourly limit is a sliding window: refresh what the composers show.
  auto *quotaTimer = new QTimer(this);
  quotaTimer->setInterval(30'000);
  connect(quotaTimer, &QTimer::timeout, this, [this]() { publishQuota(); });
  quotaTimer->start();
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

  // connectionStateChanged fires on transitions only. In Basecamp the
  // delivery node is shared and may have connected long before we started
  // listening (another app, or this one before an update), so ask for the
  // current state now and keep asking now and then.
  pollConnectionStatus();
  if (!m_statusTimer) {
    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(kStatusPollMs);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() { pollConnectionStatus(); });
    m_statusTimer->start();
  }
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
  maybeStartHistory();
  if (!m_storeStarted)
    QTimer::singleShot(kStoreStartDelayMs, this, [this]() { startStoreHistory(); });
}

void ForumerBackend::pollConnectionStatus() {
  QPointer<ForumerBackend> self(this);
  modules().delivery_module.getConnectionStatusAsync([self](LogosResult r) {
    if (!self || !r.success)
      return;
    QString state = r.value.toString().trimmed();
    state.remove(QLatin1Char('"'));
    if (state != QLatin1String("Connected") && state != QLatin1String("PartiallyConnected") &&
        state != QLatin1String("Disconnected"))
      return;
    if (state == self->m_connectionState)
      return;
    self->m_connectionState = state;
    logEvent("connection state (asked) -> " + state.toStdString());
    self->refreshStatus();
  });
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

QString ForumerBackend::networkPreset() const {
  auto valid = [](const QString &p) {
    return p == QLatin1String("logos.dev") || p == QLatin1String("logos.test");
  };
  const QString env = qEnvironmentVariable("FORUMER_NETWORK").trimmed();
  if (valid(env))
    return env;
  QFile file(dataDir() + QStringLiteral("/network"));
  if (file.open(QIODevice::ReadOnly)) {
    const QString saved = QString::fromUtf8(file.readAll()).trimmed();
    if (valid(saved))
      return saved;
  }
  return QStringLiteral("logos.dev");
}

QString ForumerBackend::chooseNetwork(QString preset) {
  preset = preset.trimmed();
  if (preset != QLatin1String("logos.dev") && preset != QLatin1String("logos.test"))
    return QStringLiteral("Unknown network");
  QDir().mkpath(dataDir());
  QFile file(dataDir() + QStringLiteral("/network"));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return QStringLiteral("Could not save the choice");
  ownerOnly(file.fileName(), false);
  file.write(preset.toUtf8());
  file.close();
  setNetworkNext(networkPreset());
  return QString();
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
    setRestorePending(m_account->restorePending());
  } else {
    setMyPersona(QString());
    setFollowedDomains(QStringLiteral("[]"));
    setRestorePending(false);
  }

  publishQuota();
  publishInbox();

  // Last: the view switches screens on this, so everything it shows is ready.
  setIdentityState(m_account ? QStringLiteral("unlocked")
                             : accounts.empty() ? QStringLiteral("none")
                                                : QStringLiteral("locked"));
}

void ForumerBackend::saveAccount() {
  if (m_account && m_accounts && !m_accounts->save(*m_account))
    logEvent("failed to save account state for " + m_account->state().id);
}

// ── The unlocked account's own posts ──────────────────────────────────────────

void ForumerBackend::attachAccount() {
  if (!m_account)
    return;
  m_ownKeys.clear();
  m_ownKeysTo = 0;
  extendOwnKeys();
  if (m_posts && m_accounts) {
    const std::string id = m_account->state().id;
    m_posts->forgetOwn();
    auto text = m_accounts->loadPrivate(*m_account, "own");
    std::optional<fc::OwnData> own = text ? fc::OwnData::fromJson(*text, id) : std::nullopt;
    if (!own) {
      logEvent("own-posts file of " + id + " can't be read; starting an empty one");
      own = fc::OwnData{id, {}, {}};
    }
    m_posts->importOwn(*own);
    // A log from 0.2.2 kept this account's outbox and read marks in clear:
    // move them into its encrypted file, then out of the log.
    const fc::OwnData legacy = m_posts->takeLegacy(id);
    if (!legacy.outbox.empty() || !legacy.read.empty()) {
      m_posts->importOwn(legacy);
      logEvent("moved " + std::to_string(legacy.outbox.size()) + " own post(s) of " + id +
               " out of the clear log");
    }
    saveOwn();
    m_posts->dropLegacyTablesIfEmpty();
  }
  publishOwnStates();
  publishSyncState();
  publishInbox();
}

void ForumerBackend::detachAccount() {
  if (m_ownSaveScheduled)
    saveOwn();  // flush before the key goes
  if (m_posts)
    m_posts->forgetOwn();
  m_ownKeys.clear();
  m_ownKeysTo = 0;
  publishOwnStates();
  publishSyncState();
}

bool ForumerBackend::saveOwn() {
  m_ownSaveScheduled = false;
  if (!m_account || !m_posts || !m_accounts)
    return false;
  const auto own = m_posts->exportOwn(m_account->state().id);
  if (!m_accounts->savePrivate(*m_account, "own", own.toJson())) {
    logEvent("failed to save own posts of " + m_account->state().id);
    return false;
  }
  return true;
}

void ForumerBackend::scheduleOwnSave() {
  if (m_ownSaveScheduled)
    return;
  m_ownSaveScheduled = true;
  QTimer::singleShot(1'000, this, [this]() {
    if (m_ownSaveScheduled)
      saveOwn();
  });
}

void ForumerBackend::publishOwnStates() {
  QJsonObject states;
  if (m_posts && m_account)
    for (const auto &e : m_posts->outbox())
      states.insert(qs(e.postId), QLatin1String(fc::toString(e.state)));
  const QString json = QString::fromUtf8(QJsonDocument(states).toJson(QJsonDocument::Compact));
  if (json != ownStatesJson())
    setOwnStatesJson(json);
}

void ForumerBackend::extendOwnKeys() {
  if (!m_account)
    return;
  const uint64_t to = m_account->state().nextIndex + fc::identity::kRecoveryGap;
  if (to <= m_ownKeysTo)
    return;
  const auto keys = fc::identity::personaKeys(m_account->masterSecret(), m_ownKeysTo, to);
  for (size_t i = 0; i < keys.size(); ++i)
    m_ownKeys.emplace(keys[i], m_ownKeysTo + i);
  m_ownKeysTo = to;
}

void ForumerBackend::noteOwnPost(const fc::post::Post &post, uint64_t index) {
  // Never hand out this persona (or an older one) again, and after a restore
  // resume as the newest one found.
  if (m_account->notePersonaUsed(index, m_account->restorePending())) {
    saveAccount();
    extendOwnKeys();
    publishIdentityState();
  }
  // Rebuild "My posts": the post was already delivered, by us or the device
  // that wrote it.
  const qint64 now = nowMs();
  if (!m_posts->outboxEntry(post.id) && m_posts->enqueue(post.id, m_account->state().id, now)) {
    m_posts->markState(post.id, fc::SendState::Sent);
    scheduleOwnSave();
    publishOwnStates();
  }
}

void ForumerBackend::noteNetworkContact() {
  m_lastPeerMs = nowMs();
  // The first new post, or the first digest from another peer, this session.
  // (A peer's digest counts too: after a restart mid-restore the log may
  // already hold everything, and then no post is ever "new".)
  if (m_firstArrivalMs != 0) {
    publishSyncState();
    return;
  }
  m_firstArrivalMs = nowMs();
  QTimer::singleShot(61'000, this, [this]() { maybeFinishRestore(); });
  publishSyncState();
}

void ForumerBackend::maybeFinishRestore() {
  if (!m_account || !m_account->restorePending())
    return;
  // Evidence that this device has caught up: posts have arrived from the
  // network (our own messages echo back, so a digest alone proves nothing)
  // and the live window has had a minute to fill, the store nodes have been
  // asked, and the history walk has reached the oldest post any peer knows
  // of. With nothing arriving at all, the user decides (postAfterRestore).
  const qint64 now = nowMs();
  const bool peerSeen = m_firstArrivalMs > 0 && now - m_firstArrivalMs >= 60'000;
  const bool storeDone = !m_storeState.isEmpty() && m_storeState != QLatin1String("asking");
  const bool historyDone = !m_historyActive && !m_historyScheduled &&
                           (m_historyTarget == 0 || m_historyTarget >= m_historyFloor);
  if (!peerSeen || !storeDone || !historyDone || m_followUps > 0) {
    if (now - m_lastRestoreWaitLogMs >= 60'000) {
      m_lastRestoreWaitLogMs = now;
      logEvent(std::string("restore: still waiting -") +
               (m_firstArrivalMs == 0 ? " no peer or new post yet" : "") +
               (m_firstArrivalMs != 0 && !peerSeen ? " first contact under a minute ago" : "") +
               (!storeDone ? " store nodes not done" : "") +
               (!historyDone ? " history walk not done" : "") +
               (m_followUps > 0 ? " catch-up burst running" : ""));
    }
    return;
  }
  markRestoredRepliesRead();
  m_account->finishRestore();
  saveAccount();
  logEvent("restore: history has synced; posting resumes as " +
           m_account->currentPersona().fingerprint());
  publishIdentityState();
}

void ForumerBackend::markRestoredRepliesRead() {
  // Read marks never leave the device they were made on, so a restored
  // account would see every old reply to it as new. Those dated before the
  // restore were read (or skipped) on the device it came from.
  if (!m_account || !m_posts || m_account->restoredAtMs() <= 0)
    return;
  m_posts->markReadBefore(m_account->state().id, m_account->restoredAtMs(), nowMs());
  scheduleOwnSave();
}

QString ForumerBackend::postAfterRestore() {
  if (!m_account)
    return QStringLiteral("Unlock first");
  if (m_account->restorePending()) {
    markRestoredRepliesRead();
    m_account->finishRestore();
    saveAccount();
    logEvent("restore: posting resumed by the user before history finished");
    publishIdentityState();
  }
  return QString();
}

QString ForumerBackend::createIdentity(QString label, QString password) {
  if (!m_accounts)
    return jsonResult(QStringLiteral("Not ready yet"));
  if (m_busy)
    return jsonResult(QString::fromUtf8(kBusy));
  BusyScope busy(m_busy);
  std::string phrase;
  std::string pw = password.toStdString();
  wipe(password);
  const std::string name = label.trimmed().toStdString();
  auto result = runOffThread([&]() { return m_accounts->create(name, pw, phrase); });
  wipe(pw);
  if (!result.ok())
    return jsonResult(qs(result.error));

  detachAccount();
  m_account = std::move(result.account);
  logEvent("created account " + m_account->state().id);
  attachAccount();
  publishIdentityState();

  const QString out = jsonResult(QString(), qs(phrase));
  fc::mnemonic::wipe(phrase);
  return out;
}

QString ForumerBackend::unlockIdentity(QString accountId, QString password) {
  if (!m_accounts)
    return QStringLiteral("Not ready yet");
  if (m_busy)
    return QString::fromUtf8(kBusy);
  BusyScope busy(m_busy);
  std::string pw = password.toStdString();
  wipe(password);
  const std::string id = accountId.toStdString();
  auto result = runOffThread([&]() { return m_accounts->unlock(id, pw); });
  wipe(pw);
  if (!result.ok())
    return qs(result.error);

  m_accounts->select(accountId.toStdString());
  detachAccount();
  m_account = std::move(result.account);
  logEvent("unlocked account " + m_account->state().id);
  attachAccount();
  publishIdentityState();
  return QString();
}

QString ForumerBackend::restoreIdentity(QString phrase, QString label, QString password) {
  if (!m_accounts)
    return QStringLiteral("Not ready yet");
  if (m_busy)
    return QString::fromUtf8(kBusy);
  BusyScope busy(m_busy);

  // Recover the persona counter from authors already in the local post log.
  // (More are found after catch-up; restore again later to pick those up.)
  const std::set<fc::Bytes> known = m_posts ? m_posts->authors() : std::set<fc::Bytes>();
  std::string phraseStd = phrase.toStdString();
  std::string pw = password.toStdString();
  wipe(phrase);
  wipe(password);
  const std::string name = label.trimmed().toStdString();
  auto result = runOffThread([&]() {
    return m_accounts->restore(phraseStd, name, pw,
                               [&known](const fc::Bytes &pk) { return known.count(pk) > 0; });
  });
  wipe(phraseStd);
  wipe(pw);
  if (!result.ok())
    return qs(result.error);

  detachAccount();
  m_account = std::move(result.account);
  logEvent("restored account " + m_account->state().id + " (next persona " +
           std::to_string(m_account->state().nextIndex) +
           (m_account->restorePending() ? ", recovering from history)" : ")"));
  attachAccount();
  // Posts by its personas already in the log rebuild "My posts" now.
  if (m_posts)
    for (const auto &p : m_posts->all()) {
      const auto own = m_ownKeys.find(p.publicKey);
      if (own != m_ownKeys.end())
        noteOwnPost(p, own->second);
    }
  publishIdentityState();
  return QString();
}

QString ForumerBackend::lockIdentity() {
  if (m_busy)
    return QString::fromUtf8(kBusy);
  detachAccount();
  m_account.reset();
  publishIdentityState();
  return QString();
}

QString ForumerBackend::selectAccount(QString accountId) {
  if (m_busy)
    return QString::fromUtf8(kBusy);
  if (!m_accounts || !m_accounts->select(accountId.toStdString()))
    return QStringLiteral("No such account");
  detachAccount();
  m_account.reset(); // switching means unlocking the other account
  publishIdentityState();
  return QString();
}

QString ForumerBackend::revealPhrase(QString password) {
  if (!m_account)
    return jsonResult(QStringLiteral("Unlock first"));
  if (m_busy)
    return jsonResult(QString::fromUtf8(kBusy));
  BusyScope busy(m_busy);
  // Re-check the password even though we're unlocked: the phrase is the whole
  // identity, so someone at an unattended screen shouldn't get it for free.
  std::string pw = password.toStdString();
  wipe(password);
  const std::string id = m_account->state().id;
  auto check = runOffThread([&]() { return m_accounts->unlock(id, pw); });
  wipe(pw);
  if (!check.ok())
    return jsonResult(qs(check.error));
  if (!m_account || m_account->state().id != id)
    return jsonResult(QStringLiteral("The account was locked"));
  std::string phrase = fc::mnemonic::encode(m_account->masterSecret());
  const QString out = jsonResult(QString(), qs(phrase));
  fc::mnemonic::wipe(phrase);
  return out;
}

QString ForumerBackend::changePassword(QString oldPassword, QString newPassword) {
  if (!m_account)
    return QStringLiteral("Unlock first");
  if (m_busy)
    return QString::fromUtf8(kBusy);
  BusyScope busy(m_busy);
  std::string oldPw = oldPassword.toStdString();
  std::string newPw = newPassword.toStdString();
  wipe(oldPassword);
  wipe(newPassword);
  const std::string id = m_account->state().id;
  const std::string error =
      runOffThread([&]() { return m_accounts->changePassword(id, oldPw, newPw); });
  wipe(oldPw);
  wipe(newPw);
  return qs(error);
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
  if (m_busy)
    return QString::fromUtf8(kBusy);
  BusyScope busy(m_busy);
  const std::string id = m_account->state().id;
  std::string pw = password.toStdString();
  wipe(password);
  auto check = runOffThread([&]() { return m_accounts->unlock(id, pw); });
  wipe(pw);
  if (!check.ok())
    return qs(check.error);
  if (!m_account || m_account->state().id != id)
    return QStringLiteral("The account was locked");
  if (!m_accounts->remove(id))
    return QStringLiteral("Couldn't remove the account's files");
  // Its outbox goes with it (the encrypted file is gone; drop the copy in
  // memory and any rows a 0.2.2 log still kept in clear).
  m_ownSaveScheduled = false;
  if (m_posts) {
    m_posts->forgetOwn();
    m_posts->takeLegacy(id);
    m_posts->dropLegacyTablesIfEmpty();
  }
  m_ownKeys.clear();
  m_ownKeysTo = 0;
  m_account.reset();
  publishOwnStates();
  publishSyncState();
  logEvent("removed account " + id + " from this device");
  publishIdentityState();
  return QString();
}

QString ForumerBackend::rotatePersona() {
  if (!m_account)
    return QStringLiteral("Unlock first");
  if (m_posts)
    m_account->skipKnown([this](const fc::Bytes &pk) { return m_posts->hasAuthor(pk); });
  m_account->rotate();
  extendOwnKeys();
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
  if (!alias.isEmpty()) {
    if (const char *problem = fc::post::aliasProblem(alias.toStdString())) {
      QString message = QString::fromUtf8(problem);
      message[0] = message[0].toUpper();
      return message;
    }
  }
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

// ── Replies to you ────────────────────────────────────────────────────────────

QString ForumerBackend::markRepliesRead(QString ids) {
  if (!m_account || !m_posts)
    return QStringLiteral("Unlock first");
  const std::string account = m_account->state().id;
  bool ok = true;
  if (ids.trimmed().isEmpty()) {
    ok = m_posts->markAllRead(account, nowMs());
  } else {
    std::vector<std::string> list;
    for (const QString &id : ids.split(QLatin1Char(','), Qt::SkipEmptyParts))
      list.push_back(id.trimmed().toStdString());
    ok = m_posts->markRead(account, list, nowMs());
  }
  saveOwn();
  publishInbox();
  return ok ? QString() : QStringLiteral("Could not save that replies were read");
}

void ForumerBackend::scheduleInbox() {
  if (m_inboxScheduled)
    return;
  m_inboxScheduled = true;
  QTimer::singleShot(300, this, [this]() {
    m_inboxScheduled = false;
    publishInbox();
  });
}

void ForumerBackend::publishInbox() {
  constexpr size_t kInboxLimit = 200;
  // While a restore is pending, replies keep arriving from history: mark
  // the old ones read before they're shown as new.
  if (m_account && m_account->restorePending())
    markRestoredRepliesRead();
  QJsonArray inbox;
  if (m_account && m_posts) {
    for (const auto &item : m_posts->inbox(m_account->state().id, kInboxLimit))
      inbox.append(QJsonObject{{"id", qs(item.postId)},
                               {"topicId", qs(item.rootId)},
                               {"parentId", qs(item.parentId)},
                               {"direct", item.direct},
                               {"inMyTopic", item.inMyTopic},
                               {"read", item.read}});
  }
  const QString json = QString::fromUtf8(QJsonDocument(inbox).toJson(QJsonDocument::Compact));
  if (json != inboxJson())
    setInboxJson(json);
}

// ── Storage probe ─────────────────────────────────────────────────────────────
//
// A first contact with Logos Storage from inside Forumer: does the node start
// (or can we attach to Basecamp's), can one instance share a small file and
// another fetch it by CID, and how long does that take. History bundles will
// be built on exactly these calls.
//
// storage_module is shared like delivery_module: inside Basecamp its package
// downloader already runs a node, so init() fails and we attach to that one.
// Standalone (nix run), we create our own under this instance's data folder.
//
// Written against storage_module v3.0.0 (see flake.nix), the version
// Basecamp ships.

namespace {

constexpr qint64 kStorageChunk = 64 * 1024;
constexpr qint64 kStorageTestMaxRead = 4096;

QJsonObject payloadOf(const QVariantList &data) {
  return data.isEmpty() ? QJsonObject()
                        : QJsonDocument::fromJson(data.at(0).toString().toUtf8()).object();
}

} // namespace

void ForumerBackend::wireStorage() {
  if (m_storageWired)
    return;
  m_storageWired = true;

  modules().storage_module.on("storageStart", [this](const QVariantList &data) {
    const QJsonObject p = payloadOf(data);
    const bool ok = p.value("success").toBool();
    logEvent("storage: started success=" + std::to_string(ok) + " " +
             p.value("message").toString().toStdString());
    m_storageState = ok ? QStringLiteral("running") : QStringLiteral("failed");
    m_storageDetail = ok ? QString() : p.value("message").toString();
    if (ok)
      QTimer::singleShot(0, this, [this]() { refreshStorageInfo(); });  // not from inside the event
    publishStorage();
  });

  // A dial-in to the file's holder finished (see storageFetch): download now,
  // whether or not it worked - the network may still find another holder.
  modules().storage_module.on("storageConnect", [this](const QVariantList &data) {
    if (m_storageFetch.value("state").toString() != QLatin1String("connecting"))
      return;
    const QJsonObject p = payloadOf(data);
    const bool ok = p.value("success").toBool();
    logEvent("storage: dialed the holder success=" + std::to_string(ok) + " " +
             p.value("message").toString().toStdString());
    if (!ok)
      m_storageFetch.insert("connectError", p.value("message").toString());
    QTimer::singleShot(0, this, [this]() { storageFetchManifest(); });
  });

  modules().storage_module.on("storageUploadDone", [this](const QVariantList &data) {
    const QJsonObject p = payloadOf(data);
    const QString session = p.value("sessionId").toString();
    // A tiny file can finish before uploadUrl() has even returned its session
    // id: while we're sharing and don't know the id yet, the first done is ours.
    if (!m_storageSharing ||
        (!m_storageUploadSession.isEmpty() && session != m_storageUploadSession))
      return;  // someone else's upload (the module is shared)
    m_storageUploadSession.clear();
    m_storageSharing = false;
    if (p.value("success").toBool()) {
      m_storageLastCid = p.value("cid").toString();
      m_storageDetail.clear();
      logEvent("storage: shared test file as " + m_storageLastCid.toStdString());
      // What the other side pastes: the CID plus how to reach us, so it can
      // dial in directly. Asked off this callback: calling the module from
      // inside its own event dispatch stalls.
      QTimer::singleShot(0, this, [this]() {
        refreshStorageInfo();
        m_storageShareCode = m_storageLastCid + QLatin1Char('@') + m_storagePeerId +
                             QLatin1Char('@') + m_storageAddrs.join(QLatin1Char(','));
        logEvent("storage: share code addresses " + m_storageAddrs.join(QLatin1Char(' ')).toStdString());
        publishStorage();
      });
    } else {
      m_storageDetail = QStringLiteral("Upload failed: %1").arg(p.value("error").toString());
      logEvent("storage: upload failed " + p.value("error").toString().toStdString());
    }
    publishStorage();
  });

  // Step 1 of a fetch done: we now hold the manifest, so the download's own
  // (3 s) manifest lookup is local. Step 2: the download itself.
  modules().storage_module.on("storageDownloadManifestDone", [this](const QVariantList &data) {
    const QJsonObject p = payloadOf(data);
    const QString cid = p.value("cid").toString();
    if (cid != m_storageFetch.value("cid").toString() ||
        m_storageFetch.value("state").toString() != QLatin1String("manifest"))
      return;
    if (!p.value("success").toBool()) {
      m_storageFetch.insert("state", "failed");
      m_storageFetch.insert("error", QStringLiteral("manifest not found: %1").arg(p.value("error").toString()));
      m_storageFetch.insert("ms", nowMs() - static_cast<qint64>(m_storageFetch.value("startedMs").toDouble()));
      logEvent("storage: manifest failed for " + cid.toStdString() + ": " +
               p.value("error").toString().toStdString());
      publishStorage();
      return;
    }
    logEvent("storage: got manifest for " + cid.toStdString() + " after " +
             std::to_string(nowMs() - static_cast<qint64>(m_storageFetch.value("startedMs").toDouble())) + " ms");
    QTimer::singleShot(0, this, [this]() { storageDownload(); });
  });

  modules().storage_module.on("storageDownloadDone", [this](const QVariantList &data) {
    const QJsonObject p = payloadOf(data);
    const QString cid = p.value("sessionId").toString();  // a download's session id is its CID
    if (cid != m_storageFetch.value("cid").toString() ||
        m_storageFetch.value("state").toString() != QLatin1String("fetching"))
      return;
    const qint64 took = nowMs() - static_cast<qint64>(m_storageFetch.value("startedMs").toDouble());
    m_storageFetch.insert("ms", took);
    if (p.value("success").toBool()) {
      QFile file(m_storageFetchPath);
      QString text;
      if (file.open(QIODevice::ReadOnly))
        text = QString::fromUtf8(file.read(kStorageTestMaxRead));
      m_storageFetch.insert("state", "done");
      m_storageFetch.insert("text", text);
      logEvent("storage: fetched " + cid.toStdString() + " in " + std::to_string(took) + " ms");
    } else {
      m_storageFetch.insert("state", "failed");
      m_storageFetch.insert("error", p.value("error").toString());
      logEvent("storage: fetch failed " + cid.toStdString() + ": " +
               p.value("error").toString().toStdString());
    }
    publishStorage();
  });
}

QString ForumerBackend::storageStart() {
  // A developer tool (F17): it shares this machine's addresses with whoever
  // gets the share code.
  if (!devMode())
    return QStringLiteral("The storage probe is a developer tool: start Forumer with FORUMER_DEV=1");
  wireStorage();
  if (m_storageState == QLatin1String("running")) {
    refreshStorageInfo();
    return QString();
  }

  // Our own node keeps its blocks under this instance's data folder, so two
  // instances on one machine don't share a repository. We pick the TCP port
  // ourselves (rather than 0 = any) so we know it: a node that isn't
  // reachable from outside announces no address, so share codes are built
  // from this port and the machine's own addresses. (v3 has no separate
  // discovery port: discovery runs over the libp2p DHT.)
  m_storageListenPort = 30000 + static_cast<int>(QRandomGenerator::global()->bounded(20000));
  const QJsonObject cfg{
      {"data-dir", dataDir() + QStringLiteral("/storage")},
      {"listen-port", m_storageListenPort},
      {"log-level", "info"},
  };
  const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

  const bool created = modules().storage_module.init(cfgJson);
  // init() fails when a node already exists (Basecamp's package downloader
  // runs one): attach to it. libstorageVersion() only answers when there is
  // a node to ask.
  m_storageAttached = !created && modules().storage_module.libstorageVersion().success;
  if (m_storageAttached)
    m_storageListenPort = 0;  // not our config: we don't know that node's port
  if (!created && !m_storageAttached) {
    m_storageState = QStringLiteral("failed");
    m_storageDetail = QStringLiteral("Could not create a storage node");
    publishStorage();
    return m_storageDetail;
  }
  logEvent(std::string("storage: ") + (created ? "created node" : "attached to the running node"));

  if (modules().storage_module.isRunning()) {
    m_storageState = QStringLiteral("running");
    m_storageDetail.clear();
    refreshStorageInfo();
    return QString();
  }

  m_storageState = QStringLiteral("starting");
  m_storageDetail.clear();
  publishStorage();
  if (!modules().storage_module.start()) {
    if (m_storageAttached) {
      // The other consumer is starting it: its storageStart event completes this.
    } else {
      m_storageState = QStringLiteral("failed");
      m_storageDetail = QStringLiteral("The storage node refused to start");
      publishStorage();
      return m_storageDetail;
    }
  }

  if (!m_storageTimer) {
    m_storageTimer = new QTimer(this);
    m_storageTimer->setInterval(30'000);
    connect(m_storageTimer, &QTimer::timeout, this, [this]() {
      if (m_storageState == QLatin1String("running"))
        refreshStorageInfo();
    });
    m_storageTimer->start();
  }
  return QString();
}

void ForumerBackend::refreshStorageInfo() {
  LogosResult peer = modules().storage_module.peerId();
  if (peer.success)
    m_storagePeerId = peer.getString();
  LogosResult info = modules().storage_module.debug();
  if (info.success) {
    const QVariantMap map = info.getMap();
    m_storageReachability = map.value("nat").toMap().value("reachability").toString();
    if (m_storageReachability.isEmpty())
      m_storageReachability = QStringLiteral("not reported");
    m_storagePeers = map.value("table").toMap().value("nodes").toList().size();
    // Announced addresses (empty while NotReachable), then the ones we can
    // vouch for ourselves: this machine's interfaces on our listen port.
    QStringList addrs = map.value("addrs").toStringList();
    if (m_storageListenPort > 0) {
      for (const QHostAddress &a : QNetworkInterface::allAddresses()) {
        if (a.protocol() != QAbstractSocket::IPv4Protocol)
          continue;
        const QString ma = QStringLiteral("/ip4/%1/tcp/%2").arg(a.toString()).arg(m_storageListenPort);
        if (!addrs.contains(ma))
          addrs.append(ma);
      }
    }
    m_storageAddrs = addrs;
  }
  publishStorage();
}

QString ForumerBackend::storageShareTest() {
  if (!devMode())
    return QStringLiteral("The storage probe is a developer tool: start Forumer with FORUMER_DEV=1");
  if (m_storageState != QLatin1String("running"))
    return QStringLiteral("Start storage first");
  if (m_storageSharing)
    return QStringLiteral("Already sharing one");

  // A small file that says nothing about who made it: just when, and a
  // random tag so every test is a new CID.
  const QString dir = dataDir() + QStringLiteral("/storage-test");
  QDir().mkpath(dir);
  const qint64 now = nowMs();
  const QString path = dir + QStringLiteral("/hello-%1.txt").arg(now);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return QStringLiteral("Could not write the test file");
  const QString tag = QString::number(QRandomGenerator::global()->generate64(), 16);
  file.write(QStringLiteral("Forumer storage test\nshared at %1\ntag %2\n")
                 .arg(QDateTime::fromMSecsSinceEpoch(now).toUTC().toString(Qt::ISODate), tag)
                 .toUtf8());
  file.close();

  // Sharing before the call: the done event may beat uploadUrl()'s return.
  m_storageSharing = true;
  m_storageUploadSession.clear();
  m_storageLastCid.clear();
  m_storageShareCode.clear();
  // advertise: announce that we hold it, so others can find it.
  LogosResult r = modules().storage_module.uploadUrl(path, kStorageChunk, true);
  if (!r.success) {
    m_storageSharing = false;
    return QStringLiteral("Upload refused: %1").arg(r.getError());
  }
  if (m_storageSharing)  // still waiting: remember which upload is ours
    m_storageUploadSession = r.value.value<QString>();
  QTimer::singleShot(60'000, this, [this]() {
    if (!m_storageSharing)
      return;
    m_storageSharing = false;
    m_storageUploadSession.clear();
    m_storageDetail = QStringLiteral("Sharing didn't finish within 60 s");
    logEvent("storage: upload timed out");
    publishStorage();
  });
  logEvent("storage: sharing " + path.toStdString());
  publishStorage();
  return QString();
}

QString ForumerBackend::storageFetch(QString code) {
  if (!devMode())
    return QStringLiteral("The storage probe is a developer tool: start Forumer with FORUMER_DEV=1");
  // A share code is cid@peerId@addr,addr; a bare CID also works (the
  // network is then asked who holds it).
  const QStringList parts = code.trimmed().split(QLatin1Char('@'));
  QString cid = parts.value(0).trimmed();
  const QString peer = parts.value(1).trimmed();
  const QStringList addrs = parts.value(2).split(QLatin1Char(','), Qt::SkipEmptyParts);
  if (m_storageState != QLatin1String("running"))
    return QStringLiteral("Start storage first");
  if (cid.isEmpty())
    return QStringLiteral("Paste a CID first");
  // Plain character checks, no QRegularExpression: under Basecamp's hardened
  // runtime on macOS its JIT is not allowed to run.
  auto only = [](const QString &text, auto ok) {
    for (const QChar c : text)
      if (c.unicode() > 127 || !ok(static_cast<char>(c.unicode())))
        return false;
    return true;
  };
  // A Forumer post id (32 hex chars, from a thread's link) is the likeliest
  // mix-up: say so instead of letting the download fail.
  if (cid.size() == 32 &&
      only(cid, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
    return QStringLiteral("That's a forum post id, not a storage id. Use the code shown after "
                          "\"share a test file\" in the other window - it starts with z.");
  // Storage CIDs are base58 (z…, Qm…) or base32 (b…) text.
  const bool cidShape = cid.size() >= 20 && cid.size() <= 128 &&
                        only(cid, [](char c) {
                          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                        });
  if (!cidShape)
    return QStringLiteral("That doesn't look like a storage id");

  const QString dir = dataDir() + QStringLiteral("/storage-test");
  QDir().mkpath(dir);
  m_storageFetchPath = dir + QStringLiteral("/fetched-%1.txt").arg(cid.right(16));
  QFile::remove(m_storageFetchPath);

  m_storageFetch = QJsonObject{{"cid", cid}, {"state", "fetching"}, {"startedMs", nowMs()}};

  if (!peer.isEmpty() && peer != m_storagePeerId) {
    // Dial the holder first; storageConnect then starts the download.
    LogosResult r = modules().storage_module.connect(peer, addrs);
    if (r.success) {
      m_storageFetch.insert("state", "connecting");
      logEvent("storage: dialing " + peer.toStdString() + " at " +
               addrs.join(QLatin1Char(' ')).toStdString());
      publishStorage();
      // If no storageConnect arrives, go ahead anyway.
      QTimer::singleShot(20'000, this, [this, cid]() {
        if (m_storageFetch.value("cid").toString() == cid &&
            m_storageFetch.value("state").toString() == QLatin1String("connecting")) {
          m_storageFetch.insert("connectError", "no answer from the holder within 20 s");
          storageFetchManifest();
        }
      });
      return QString();
    }
    m_storageFetch.insert("connectError", r.getError());
  }
  storageFetchManifest();
  return QString();
}

// Step 1 of a fetch: ask the network for the manifest in the background
// (storageDownloadManifestDone continues). downloadToUrl() would look it up
// itself, but gives up after 3 s - too short for a lookup across the network.
void ForumerBackend::storageFetchManifest() {
  const QString cid = m_storageFetch.value("cid").toString();
  m_storageFetch.insert("state", "manifest");
  // Public (isPrivate=false: no mix routing), and advertise once held.
  LogosResult r = modules().storage_module.downloadManifest(cid, false, true);
  if (!r.success) {
    m_storageFetch.insert("state", "failed");
    m_storageFetch.insert("error", QStringLiteral("manifest lookup refused: %1").arg(r.getError()));
    publishStorage();
    return;
  }
  logEvent("storage: looking up manifest for " + cid.toStdString());
  publishStorage();
  QTimer::singleShot(180'000, this, [this, cid]() {
    if (m_storageFetch.value("cid").toString() == cid &&
        m_storageFetch.value("state").toString() == QLatin1String("manifest")) {
      m_storageFetch.insert("state", "failed");
      m_storageFetch.insert("error", "manifest not found within 3 min");
      m_storageFetch.insert("ms", nowMs() - static_cast<qint64>(m_storageFetch.value("startedMs").toDouble()));
      logEvent("storage: manifest timed out for " + cid.toStdString());
      publishStorage();
    }
  });
}

void ForumerBackend::storageDownload() {
  const QString cid = m_storageFetch.value("cid").toString();
  m_storageFetch.insert("state", "fetching");
  // From the network (local=false). Once held, the node serves it to others.
  LogosResult r = modules().storage_module.downloadToUrl(cid, m_storageFetchPath, false,
                                                         kStorageChunk, false, true);
  if (!r.success) {
    m_storageFetch.insert("state", "failed");
    m_storageFetch.insert("error", r.getError());
    m_storageFetch.insert("ms", nowMs() - static_cast<qint64>(m_storageFetch.value("startedMs").toDouble()));
    logEvent("storage: download refused for " + cid.toStdString() + ": " + r.getError().toStdString());
    publishStorage();
    return;
  }
  logEvent("storage: fetching " + cid.toStdString());
  publishStorage();
}

void ForumerBackend::publishStorage() {
  const QJsonObject state{
      {"state", m_storageState},
      {"detail", m_storageDetail},
      {"attached", m_storageAttached},
      {"peerId", m_storagePeerId},
      {"reachability", m_storageReachability},
      {"peers", m_storagePeers},
      {"sharing", m_storageSharing},
      {"lastCid", m_storageLastCid},
      {"shareCode", m_storageShareCode},
      {"fetch", m_storageFetch},
  };
  setStorageJson(QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)));
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
  if (draft.domains.empty()) {
    // Typed something, but nothing that can be a domain (review F12: it
    // used to land in #general without a word).
    if (!domains.trimmed().remove(QLatin1Char(',')).trimmed().isEmpty())
      return QStringLiteral("None of \"%1\" can be a domain: use a-z, 0-9 and -, 2 to 32 "
                            "characters. Leave it empty to post in #general.")
          .arg(domains.trimmed());
    draft.domains = {"general"};
  }
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
  if (m_busy)
    return QString::fromUtf8(kBusy);

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
  if (disclosure == Disclosure::Alias && fc::post::aliasProblem(alias))
    return QStringLiteral("Your alias \"%1\" can't be used any more (%2). Choose another in "
                          "profile & personas.")
        .arg(qs(alias), QString::fromUtf8(fc::post::aliasProblem(alias)));
  if (m_account->restorePending())
    return QStringLiteral("This account was just restored and its history is still arriving. "
                          "Posting now could reuse a persona you rotated away from - wait for "
                          "it to sync, or choose \"post anyway\".");

  // Sender-side flood limit, per account (so rotating personas or posting
  // anonymously doesn't lift it). Peers enforce the same numbers per persona.
  {
    const qint64 now = nowMs();
    const QString what = draft.kind == fc::post::Kind::Post ? QStringLiteral("topics") : QStringLiteral("replies");
    const size_t recent = m_posts->countOwn(m_account->state().id, draft.kind, now - fc::flood::kWindowMs);
    if (!fc::flood::withinLimit(draft.kind, recent))
      return QStringLiteral("That's %1 %2 in the last hour — the most one account can post. Try again a little later.")
          .arg(fc::flood::maxPerWindow(draft.kind))
          .arg(what);
    // Peers hold history imports to a daily limit per key (flood.h, 2b).
    const size_t today = m_posts->countOwn(m_account->state().id, draft.kind, now - fc::flood::kDayMs);
    if (today >= static_cast<size_t>(fc::flood::maxPerDay(draft.kind)))
      return QStringLiteral("That's %1 %2 in the last 24 hours — the most one account can post in a day.")
          .arg(fc::flood::maxPerDay(draft.kind))
          .arg(what);
  }

  // Picking the persona may allocate a new one (Auto, Anonymous); save at once
  // so a crash can never hand the same persona out twice.
  // Never sign with a persona something in the log already used (another
  // device with the same phrase, or history that arrived after a restore).
  m_account->skipKnown([this](const fc::Bytes &pk) { return m_posts->hasAuthor(pk); });
  const fc::identity::Persona persona = m_account->personaForPost(disclosure);
  extendOwnKeys();
  saveAccount();

  // A key's first post is mined a little harder, so it can use the lane
  // peers keep for costlier new keys (flood.h, 3); anonymous posts already
  // carry more than that.
  const bool freshKey = !m_posts->hasAuthor(persona.publicKey());
  const int pow = disclosure == Disclosure::Anonymous ? kAnonymousPowBits
                  : freshKey ? std::max(kPowBits, fc::flood::kPriorityPowBits)
                             : kPowBits;
  const std::string accountId = m_account->state().id;

  // Mining takes from a fraction of a second to many seconds (it hashes the
  // whole post each try): off the thread, so sync keeps running (F16).
  BusyScope busy(m_busy);
  fc::post::Error err = fc::post::Error::None;
  auto signedPost = runOffThread([&]() {
    return fc::post::sign(std::move(draft), persona, disclosure, alias, pow, &err);
  });
  if (!signedPost)
    return QStringLiteral("Couldn't sign the post: %1").arg(fc::post::describe(err));
  if (!m_account || m_account->state().id != accountId)
    return QStringLiteral("The account was locked while the post was being signed; it wasn't sent");

  // Durable before anything else: once these two writes land, the post
  // survives a crash and goes out on the next retry tick at the latest.
  const qint64 now = nowMs();
  if (m_posts->insert(*signedPost, now) == fc::PostStore::Insert::Failed ||
      !m_posts->enqueue(signedPost->id, m_account->state().id, now) || !saveOwn()) {
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
  // authorKey: the full persona key, for comparing authors (the "op" badge).
  // The display text alone can be imitated; the key can't.
  const QString key = qs(fc::toHex(post.publicKey));
  if (c.kind == fc::post::Kind::Post)
    emit topicReceived(qs(post.id), qs(c.title), qs(c.body), qs(post.authorDisplay()),
                       joinDomains(c.domains), toNs(c.timestampMs), key);
  else
    emit replyReceived(qs(post.id), qs(c.root), qs(c.parent), qs(c.body), qs(post.authorDisplay()),
                       toNs(c.timestampMs), key);
}

QJsonObject ForumerBackend::backlogEntry(const fc::post::Post &p,
                                         const QHash<QString, QString> &states) const {
  const bool isTopic = p.content.kind == fc::post::Kind::Post;
  QJsonObject entry{
      {"kind", isTopic ? QStringLiteral("topic") : QStringLiteral("reply")},
      {"id", qs(p.id)},
      {"body", qs(p.content.body)},
      {"author", qs(p.authorDisplay())},
      {"authorKey", qs(fc::toHex(p.publicKey))},
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
  return entry;
}

QHash<QString, QString> ForumerBackend::ownStates() const {
  // Delivery state of the unlocked account's posts, so they come back marked
  // after a restart (ownStatesJson keeps it current across lock / unlock).
  QHash<QString, QString> states;
  if (m_posts)
    for (const auto &e : m_posts->outbox())
      states.insert(qs(e.postId), QLatin1String(fc::toString(e.state)));
  return states;
}

QString ForumerBackend::loadBacklog() {
  if (!m_posts)
    return QStringLiteral("[]");
  // all() is topics first, oldest first — so the view never has to stand up a
  // placeholder for a topic a few entries further down.
  const auto states = ownStates();
  QJsonArray backlog;
  for (const auto &p : m_posts->all())
    backlog.append(backlogEntry(p, states));
  logEvent("backlog: " + std::to_string(backlog.size()) + " post(s)");
  return QString::fromUtf8(QJsonDocument(backlog).toJson(QJsonDocument::Compact));
}

QString ForumerBackend::loadBacklogPage(QString cursor) {
  // A page at a time (F18): a log of tens of thousands of posts would
  // otherwise cross to the view as one string of tens of megabytes.
  constexpr size_t kPageSize = 500;
  QJsonObject out{{"posts", QJsonArray()}, {"cursor", cursor}, {"done", true}};
  if (!m_posts)
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
  bool ok = false;
  const qint64 after = cursor.isEmpty() ? 0 : cursor.toLongLong(&ok);
  if (!cursor.isEmpty() && !ok)
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
  const auto page = m_posts->page(after, kPageSize);
  const auto states = ownStates();
  QJsonArray posts;
  for (const auto &p : page.posts)
    posts.append(backlogEntry(p, states));
  out.insert(QStringLiteral("posts"), posts);
  out.insert(QStringLiteral("cursor"), QString::number(page.next));
  out.insert(QStringLiteral("done"), page.done);
  return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
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
  switch (decoded.message->type) {
  case fc::sync::MessageType::Post:
    handlePost(std::move(*decoded.message->post));
    break;
  case fc::sync::MessageType::Digest:
    // Our own digests come back to us too; only another peer's digest says
    // what someone else holds.
    handleDigest(decoded.message->digest, !m_ownDigests.contains(payloadHash(bytes)));
    break;
  case fc::sync::MessageType::Range:
    handleRange(decoded.message->digest);
    break;
  }
}

std::string ForumerBackend::checkPost(const fc::post::Post &post, bool fromHistory) const {
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
  // Timestamps are the author's to choose, so one key could back-date posts
  // into every past hour (review F6). Live posts are also counted by when
  // they reach us; bulk history and store imports per day of author time.
  if (!fromHistory) {
    const size_t arrived = m_posts->countByAuthorArrival(post.publicKey, post.content.kind,
                                                         nowMs() - fc::flood::kWindowMs);
    if (arrived >= static_cast<size_t>(fc::flood::maxPerArrival(post.content.kind)))
      return "persona's posts are arriving faster than the limit (deferred)";
  } else {
    const size_t inDay = m_posts->countByAuthor(post.publicKey, post.content.kind,
                                                ts - fc::flood::kDayMs, ts);
    if (inDay >= static_cast<size_t>(fc::flood::maxPerDay(post.content.kind)))
      return "persona is over its daily limit";
  }
  return {};
}

void ForumerBackend::handlePost(fc::post::Post post) {
  // "Seen on the wire" lets a peer about to answer a digest skip a post
  // someone else has just re-sent. Only a genuine copy counts: a forged
  // message carrying a real post's id must not suppress its repair (F8).
  const int minPow = post.disclosure == Disclosure::Anonymous ? kAnonymousPowBits : kPowBits;
  if (m_posts->contains(post.id)) {
    // Already have it (including our own echo). The id is a hash over the
    // signed bytes, so a copy that verifies is the same post.
    if (fc::post::verify(post, minPow) == fc::post::Error::None)
      m_lastSeenOnWire.insert(qs(post.id), nowMs());
    return;
  }

  // Old posts arriving in answer to our own history request, or from a store
  // node: these come in bulk, so they skip the live limits (arrival rate,
  // new-key budget) - otherwise history would trickle in at the anti-flood
  // rate - and are held to a per-day limit instead (flood.h, 2b).
  const int64_t ts = post.content.timestampMs;
  const bool inHistoryWindow =
      m_storeImport || (m_historyActive && ts >= m_historyFrom && ts < m_historyTo);

  const std::string problem = checkPost(post, inHistoryWindow);
  if (!problem.empty()) {
    logEvent("dropped " + post.id + ": " + problem);
    return;
  }
  m_lastSeenOnWire.insert(qs(post.id), nowMs());

  // Keys we've never seen (anonymous posts, fresh personas) share a budget.
  // Over it, the post isn't stored yet; the next digest exchange offers it
  // again, so this delays rather than loses. Posts with more work behind
  // them first try a lane of their own, which cheap fresh keys can't drain
  // (review F7).
  if (!inHistoryWindow && !m_posts->hasAuthor(post.publicKey)) {
    const qint64 now = nowMs();
    const bool costly = post.powBits >= fc::flood::kPriorityPowBits;
    if (!(costly && m_priorityKeyBudget.take(now)) && !m_newKeyBudget.take(now)) {
      m_lastSeenOnWire.remove(qs(post.id));   // let peers offer it again
      ++m_roundArrivals;                       // more to come: ask again soon
      logEvent("deferred " + post.id + ": new-key budget used up");
      return;
    }
  }

  switch (m_posts->insert(post, nowMs())) {
  case fc::PostStore::Insert::Added:
    ++m_receivedCount;
    ++m_roundArrivals;
    if (inHistoryWindow) {
      ++m_historyRoundNew;
      ++m_historyReceived;
    }
    logEvent("received " + post.id);
    noteNetworkContact();
    emitPost(post);
    if (m_account) {
      const auto own = m_ownKeys.find(post.publicKey);
      if (own != m_ownKeys.end())
        noteOwnPost(post, own->second);
    }
    publishSyncState();
    if (post.content.kind == fc::post::Kind::Reply)
      scheduleInbox();
    break;
  case fc::PostStore::Insert::Duplicate:
    break;
  case fc::PostStore::Insert::Failed:
    logEvent("could not store " + post.id);
    break;
  }
}

QByteArray ForumerBackend::payloadHash(const std::vector<uint8_t> &payload) {
  const fc::Bytes h = fc::crypto::blake2b(payload, 16);
  return QByteArray(reinterpret_cast<const char *>(h.data()), static_cast<int>(h.size()));
}

void ForumerBackend::confirmHeldByPeers(const fc::sync::Digest &digest) {
  // delivery_module may never settle a send ("messageSent") when the
  // connection is flaky, though the post went out: other peers have it, and
  // it stays "sending…" here. A peer's digest listing it is proof it is on
  // the network.
  const auto unsent = m_posts->unsent();
  if (unsent.empty())
    return;
  const std::set<std::string> have(digest.have.begin(), digest.have.end());
  bool changed = false;
  for (const auto &entry : unsent) {
    if (!have.count(fc::sync::shortId(entry.postId)))
      continue;
    m_posts->markState(entry.postId, fc::SendState::Sent);
    for (auto it = m_pendingSends.begin(); it != m_pendingSends.end();)
      it = it.value() == qs(entry.postId) ? m_pendingSends.erase(it) : std::next(it);
    logEvent("send of " + entry.postId + " confirmed: a peer holds it");
    emit messageStateChanged(qs(entry.postId), QStringLiteral("sent"), QString());
    changed = true;
  }
  if (changed) {
    scheduleOwnSave();
    publishSyncState();
  }
}

void ForumerBackend::handleDigest(const fc::sync::Digest &digest, bool fromPeer) {
  const qint64 now = nowMs();
  if (fromPeer)
    noteNetworkContact();
  if (fromPeer && m_account)
    confirmHeldByPeers(digest);

  // The sender's oldest post tells us how far back the forum's history goes.
  // oldestMs before Forumer existed is already dropped by decode (F10).
  // Never further back than what this device pruned (F18).
  const int64_t oldest = std::max<int64_t>(digest.oldestMs, m_historyLimitMs);
  if (digest.oldestMs >= fc::sync::kForumEpochMs && oldest < now &&
      (m_historyTarget == 0 || oldest < m_historyTarget)) {
    m_historyTarget = oldest;
    publishHistory();
    maybeStartHistory();
  }

  // Does the sender hold posts we don't? Then ask for them now: our digest
  // tells it what we hold, and it answers with the rest. (This is what lets a
  // peer that was offline while others posted catch up within seconds of
  // someone else coming online, not minutes.) Look across the digest's whole
  // window, but never further back than twice ours.
  const int64_t since = std::max<int64_t>(digest.sinceMs, now - 2 * liveWindowMs());
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
  const auto held = m_posts->recent(now - liveWindowMs());
  const auto missing = fc::sync::missingFrom(digest, held);
  if (missing.empty())
    return;

  queueAnswers(missing, "digest");
}

void ForumerBackend::queueAnswers(const std::vector<std::string> &ids, const char *why) {
  for (const auto &id : ids)
    m_answerQueue.insert(qs(id));
  logEvent(std::string(why) + ": peer is missing " + std::to_string(ids.size()) + " post(s) we hold");

  if (!m_answerFlushScheduled) {
    m_answerFlushScheduled = true;
    const int delay = kAnswerDelayMinMs + jitter(kAnswerDelayMaxMs - kAnswerDelayMinMs);
    QTimer::singleShot(delay, this, [this]() { flushAnswers(); });
  }
}

void ForumerBackend::handleRange(const fc::sync::Digest &range) {
  const qint64 now = nowMs();
  // One at a time: a stream of range requests can't make us replay history
  // faster than this (and every answer still draws on the re-send budget).
  if (now - m_lastRangeAnswerMs < kRangeAnswerGapMs)
    return;
  m_lastRangeAnswerMs = now;

  fc::sync::Digest window = range;
  window.untilMs = std::min<int64_t>(window.untilMs, now + fc::sync::kMaxClockSkewMs);
  if (window.untilMs <= window.sinceMs)
    return;
  // Overflow-safe for any decoded pair (F11): answer only the newest part.
  if (fc::sync::spanExceeds(window.sinceMs, window.untilMs, kRangeMaxSpanMs))
    window.sinceMs = window.untilMs - kRangeMaxSpanMs;
  const auto held = m_posts->range(window.sinceMs, window.untilMs);
  const auto missing = fc::sync::missingFrom(window, held);
  if (!missing.empty())
    queueAnswers(missing, "history request");
}

// ── History walk ──────────────────────────────────────────────────────────────

void ForumerBackend::loadHistoryState() {
  const int64_t now = nowMs();
  const int64_t top = now - liveWindowMs();  // live digests cover everything above
  auto number = [this](const char *key) -> int64_t {
    const auto v = m_posts->meta(key);
    try {
      return v ? std::stoll(*v) : 0;
    } catch (...) {
      return 0;
    }
  };
  const int64_t floor = number("history_floor");
  const int64_t covered = number("covered_until");
  m_historyLimitMs = number("history_limit");
  // When we were last online and syncing: "missed" starts there. 0 on a new
  // device - nothing was missed, it's all new (F15).
  setAwaySinceMs(static_cast<qint64>(covered));
  // First run, or away longer than the digest window: there may be a gap
  // between where we stopped syncing and the window. Walk again from the
  // top; windows we already hold complete in one round each.
  m_historyFloor = (floor == 0 || floor > top || covered < top) ? top : floor;
  m_posts->setMeta("history_floor", std::to_string(m_historyFloor));
  m_lastCoveredSaveMs = 0;
  publishHistory();
}

void ForumerBackend::maybeStartHistory() {
  if (m_historyActive || m_historyScheduled || !m_joined || !m_posts)
    return;
  if (m_historyTarget == 0 || m_historyTarget >= m_historyFloor)
    return;
  m_historyScheduled = true;
  logEvent("history: older posts exist (back to " + std::to_string(m_historyTarget) +
           "), fetching from " + std::to_string(m_historyFloor) + " down");
  QTimer::singleShot(kHistoryStartDelayMs, this, [this]() {
    m_historyScheduled = false;
    historyStep();
  });
}

QString ForumerBackend::loadOlderHistory() {
  if (!m_posts)
    return QStringLiteral("The post log isn't open");
  const int64_t deeper = m_historyFloor - kLoadOlderSpanMs;
  if (m_historyTarget == 0 || deeper < m_historyTarget)
    m_historyTarget = std::max<int64_t>(1, deeper);
  publishHistory();
  m_historyByRequest = true;  // the user asked: don't stop at empty weeks
  if (!m_historyActive && !m_historyScheduled && m_joined) {
    logEvent("history: loading four more weeks on request");
    historyStep();
  }
  return QString();
}

QString ForumerBackend::fetchAround(qint64 day) {
  if (!m_posts)
    return QStringLiteral("The post log isn't open");
  if (!m_joined)
    return QStringLiteral("Not connected to the network yet");
  constexpr int64_t kDayMs = 86'400'000;
  const int64_t now = nowMs();
  // The day before through about a week after: the thread and the replies
  // that followed it. (Newer ones come with the live digests anyway.)
  const int64_t since = static_cast<int64_t>(day) * kDayMs - kDayMs;
  const int64_t until = std::min<int64_t>(now, since + 9 * kDayMs);
  if (day <= 0 || since <= 0 || since >= now)
    return QStringLiteral("That link has no usable date");
  if (now - m_lastLinkFetchMs < 5'000)
    return QString();  // already asked a moment ago
  m_lastLinkFetchMs = now;

  const auto held = m_posts->range(since, until);
  const fc::sync::Digest request =
      fc::sync::makeDigest(held, since, fc::sync::kMaxDigestIds, until);
  const auto r = m_transport->publish(m_topic, fc::sync::encodeRange(request));
  if (!r.ok)
    return QStringLiteral("Couldn't ask peers: %1").arg(QString::fromStdString(r.error));
  logEvent("link: asking peers for [" + std::to_string(since) + ", " + std::to_string(until) +
           "), holding " + std::to_string(held.size()));
  return QString();
}

void ForumerBackend::historyStep() {
  if (!m_posts || !m_joined)
    return;
  if (m_historyTarget == 0 || m_historyTarget >= m_historyFloor) {
    if (m_historyActive)
      logEvent("history: complete back to " + std::to_string(m_historyFloor));
    m_historyActive = false;
    m_historyByRequest = false;
    m_historyEmptyWindows = 0;
    publishHistory();
    maybeFinishRestore();
    return;
  }
  m_historyActive = true;
  m_historyTo = m_historyFloor;
  m_historyFrom = fc::sync::historyWindowStart(
      m_historyFloor, m_historyTarget,
      [this](int64_t from, int64_t to) { return m_posts->countRange(from, to); });
  m_historyRounds = 0;
  historySendRange();
}

void ForumerBackend::historySendRange() {
  const auto held = m_posts->range(m_historyFrom, m_historyTo);
  const fc::sync::Digest request =
      fc::sync::makeDigest(held, m_historyFrom, fc::sync::kMaxDigestIds, m_historyTo);
  const auto r = m_transport->publish(m_topic, fc::sync::encodeRange(request));
  if (!r.ok)
    logEvent("history: request failed: " + r.error);
  m_historyRoundNew = 0;
  ++m_historyRounds;
  logEvent("history: asking for [" + std::to_string(m_historyFrom) + ", " +
           std::to_string(m_historyTo) + "), holding " + std::to_string(held.size()));
  publishHistory();
  QTimer::singleShot(kHistoryRoundMs, this, [this]() { historyRoundDone(); });
}

void ForumerBackend::historyRoundDone() {
  if (!m_historyActive)
    return;
  // Answers are capped per peer: a round that brought a lot may have left
  // more behind, so ask about the same window again.
  if (m_historyRoundNew >= kHistoryRepeatAt && m_historyRounds < kHistoryMaxRounds) {
    historySendRange();
    return;
  }
  m_historyFloor = m_historyFrom;
  m_posts->setMeta("history_floor", std::to_string(m_historyFloor));
  // A run of windows with nothing in them: the forum probably doesn't go back
  // that far, whatever a digest claimed (F10). Stop here; "load older" can
  // still go further on request.
  if (m_historyRoundNew == 0 && m_posts->countRange(m_historyFrom, m_historyTo) == 0) {
    if (++m_historyEmptyWindows >= fc::sync::kHistoryMaxEmptyWindows && !m_historyByRequest) {
      logEvent("history: " + std::to_string(m_historyEmptyWindows) +
               " empty windows in a row - stopping at " + std::to_string(m_historyFloor));
      m_historyTarget = m_historyFloor;
    }
  } else {
    m_historyEmptyWindows = 0;
  }
  publishHistory();
  QTimer::singleShot(1'000, this, [this]() { historyStep(); });
}

// ── History from the network's store nodes ───────────────────────────────────
//
// Logos Delivery's fleet nodes keep recent messages (the "store" protocol).
// Peers answer digests and range requests only while someone who holds the
// posts is online; the store nodes answer even when nobody is. Asked once per
// session, node by node, until one has given us posts. Every post is checked
// exactly like a live one (signature, proof-of-work, flood limits) - a store
// node can withhold posts but can't forge or alter them. Only posts are taken
// from the archive: old digests and range requests in it are ignored, or a
// newcomer would answer requests that were settled days ago.

int ForumerBackend::deliveryCluster() const {
  return networkPreset() == QLatin1String("logos.test") ? 2 : 3;
}

QStringList ForumerBackend::storePeers() const {
  const QString custom = qEnvironmentVariable("FORUMER_STORE_PEERS").trimmed();
  if (!custom.isEmpty())
    return custom.split(QLatin1Char(','), Qt::SkipEmptyParts);
  if (networkPreset() == QLatin1String("logos.test"))
    // The logos.test fleet (cluster 2). Not verified from here yet: override
    // with FORUMER_STORE_PEERS if they change.
    return {
        QStringLiteral("/dns4/node-01.do-ams3.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmQ9X2xDfPG3uL77V9piYDhjq14JhKCtcmNYsTMKNqrKCj"),
        QStringLiteral("/dns4/node-01.gc-us-central1-a.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmF8WtwGPmeGHgYAX2277jHgy5cW9F7zsB8EqUjBZQAZQ3"),
        QStringLiteral("/dns4/node-01.ac-cn-hongkong-c.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmL3oU95jh1BZHozn3uNhx8HEneirgr8M1jEAapzXGDqRF"),
        QStringLiteral("/dns4/node-02.do-ams3.logos.test.status.im/tcp/30303/p2p/16Uiu2HAmB8NYprrfQrgWVzsJtYWkfjsXbmJEGNMG6othXsQ53BwG"),
    };
  // The logos.dev fleet (cluster 3): the six entry nodes, peer ids as our own
  // node discovered them.
  return {
      QStringLiteral("/dns4/delivery-01.do-ams3.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAmTUbnxLGT9JvV6mu9oPyDjqHK4Phs1VDJNUgESgNSkuby"),
      QStringLiteral("/dns4/delivery-02.do-ams3.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAmMK7PYygBtKUQ8EHp7EfaD3bCEsJrkFooK8RQ2PVpJprH"),
      QStringLiteral("/dns4/delivery-01.gc-us-central1-a.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAm4S1JYkuzDKLKQvwgAhZKs9otxXqt8SCGtB4hoJP1S397"),
      QStringLiteral("/dns4/delivery-02.gc-us-central1-a.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAm8Y9kgBNtjxvCnf1X6gnZJW5EGE4UwwCL3CCm55TwqBiH"),
      QStringLiteral("/dns4/delivery-01.ac-cn-hongkong-c.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAm8YokiNun9BkeA1ZRmhLbtNUvcwRr64F69tYj9fkGyuEP"),
      QStringLiteral("/dns4/delivery-02.ac-cn-hongkong-c.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAkvwhGHKNry6LACrB8TmEFoCJKEX29XR5dDUzk3UT3UNSE"),
  };
}

void ForumerBackend::startStoreHistory() {
  if (m_storeStarted || !m_joined || !m_transport || !m_posts)
    return;
  if (qEnvironmentVariableIsSet("FORUMER_NO_STORE")) {
    logEvent("store: skipped (FORUMER_NO_STORE)");
    m_storeState = QStringLiteral("skipped");  // counts as done for a pending restore
    publishHistory();
    return;
  }
  m_storeStarted = true;
  m_storePeer = 0;
  m_storePage = 0;
  m_storeNodeMsgs = 0;
  m_storeCursor.clear();
  m_storeState = QStringLiteral("asking");
  m_storeError.clear();
  publishHistory();
  storeQueryNext();
}

void ForumerBackend::storeQueryNext() {
  const QStringList peers = storePeers();
  if (m_storePeer >= peers.size()) {
    m_storeState = m_storeGot > 0 ? QStringLiteral("done")
                   : m_storeError.isEmpty() ? QStringLiteral("empty")
                                            : QStringLiteral("unreachable");
    logEvent("store: finished, " + std::to_string(m_storeGot) + " new post(s)");
    publishHistory();
    maybeFinishRestore();
    return;
  }
  const QString peer = peers.at(m_storePeer);
  m_storeNode = peer.section(QLatin1Char('/'), 2, 2);  // "/dns4/<host>/tcp/..." -> host
  const int64_t since = nowMs() - (m_storeShortRange ? kStoreShortLookbackMs : kStoreLookbackMs);
  QPointer<ForumerBackend> self(this);
  m_transport->queryStorePage(m_topic, deliveryCluster(), since, m_storeCursor, peer.toStdString(),
                              kStoreTimeoutMs,
                              [self](DeliveryModuleTransport::StorePage page) {
                                if (self)
                                  self->storePageArrived(std::move(page));
                              });
}

void ForumerBackend::storePageArrived(DeliveryModuleTransport::StorePage page) {
  auto nextNode = [this]() {
    ++m_storePeer;
    m_storePage = 0;
    m_storeNodeMsgs = 0;
    m_storeCursor.clear();
    // Leave the event handler first: don't call back into the module from
    // inside its own callback.
    QTimer::singleShot(0, this, [this]() { storeQueryNext(); });
  };
  if (!page.ok) {
    // Some store nodes refuse ranges longer than a day ("time range exceeds
    // 24h"): ask the same node again for the last day only.
    if (!m_storeShortRange && page.error.find("time range") != std::string::npos) {
      m_storeShortRange = true;
      m_storeCursor.clear();
      logEvent("store: " + m_storeNode.toStdString() + " caps the time range; asking for the last day");
      QTimer::singleShot(0, this, [this]() { storeQueryNext(); });
      return;
    }
    m_storeError = qs(page.error);
    logEvent("store: " + m_storeNode.toStdString() + ": " + page.error);
    nextNode();
    return;
  }

  const int before = m_receivedCount;
  m_storeImport = true;
  for (const auto &payload : page.payloads) {
    fc::sync::DecodeResult decoded = fc::sync::decode(payload);
    if (decoded.message && decoded.message->type == fc::sync::MessageType::Post)
      handlePost(std::move(*decoded.message->post));
  }
  m_storeImport = false;
  const int added = m_receivedCount - before;
  m_storeGot += added;
  m_storeNodeMsgs += static_cast<int>(page.payloads.size());
  ++m_storePage;
  logEvent("store: " + m_storeNode.toStdString() + " page " + std::to_string(m_storePage) + ": " +
           std::to_string(page.payloads.size()) + " message(s), " + std::to_string(added) + " new");
  publishHistory();

  if (!page.cursor.empty() && !page.payloads.empty() && m_storePage < kStoreMaxPages) {
    m_storeCursor = page.cursor;
    QTimer::singleShot(0, this, [this]() { storeQueryNext(); });
    return;
  }
  // This node is done. One that kept our topic's messages is enough (the
  // fleet nodes share what they relay); one that kept nothing may just be
  // new, so try the next.
  if (m_storeNodeMsgs > 0) {
    m_storePeer = static_cast<int>(storePeers().size());
    QTimer::singleShot(0, this, [this]() { storeQueryNext(); });
    return;
  }
  nextNode();
}

void ForumerBackend::publishHistory() {
  const QJsonObject state{
      {"floorMs", static_cast<qint64>(m_historyFloor)},
      {"targetMs", static_cast<qint64>(m_historyTarget)},
      {"active", m_historyActive || m_historyScheduled},
      {"fromMs", static_cast<qint64>(m_historyFrom)},
      {"toMs", static_cast<qint64>(m_historyTo)},
      {"received", m_historyReceived},
      {"archive", m_storeState},
      {"archiveGot", m_storeGot},
      {"archiveNode", m_storeNode},
      {"archiveError", m_storeError},
  };
  setHistoryJson(QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)));
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

  if (!m_posts->markAttempt(id, nowMs()))
    return;  // not the unlocked account's post
  scheduleOwnSave();
  m_lastSeenOnWire.insert(qs(id), nowMs());
  const auto r = m_transport->publish(m_topic, fc::sync::encodePost(*post));
  if (!r.ok) {
    m_posts->markState(id, fc::SendState::Failed, r.error);
    publishOwnStates();
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
  const auto held = m_posts->recent(now - liveWindowMs());
  fc::sync::Digest digest = fc::sync::makeDigest(held, now - liveWindowMs());
  digest.oldestMs = m_posts->oldestTimestamp().value_or(0);
  const std::vector<uint8_t> payload = fc::sync::encodeDigest(digest);
  // Remember it, to tell it apart from a peer's when it comes back to us.
  m_ownDigests.insert(payloadHash(payload));
  m_ownDigestOrder.append(payloadHash(payload));
  while (m_ownDigestOrder.size() > 64)
    m_ownDigests.remove(m_ownDigestOrder.takeFirst());
  const auto r = m_transport->publish(m_topic, payload);
  if (!r.ok) {
    logEvent(std::string("digest (") + why + ") failed: " + r.error);
    return;
  }
  m_lastDigestMs = now;
  logEvent(std::string("digest (") + why + "): " + std::to_string(digest.have.size()) +
           " post(s) held");
  m_roundArrivals = 0;
  scheduleFollowUp();
  // We're online and syncing: everything up to now - window is covered by
  // live digests. Saved now and then, so a long absence is noticed on restart.
  if (now - m_lastCoveredSaveMs >= kCoveredSaveGapMs) {
    m_lastCoveredSaveMs = now;
    m_posts->setMeta("covered_until", std::to_string(now));
  }
  publishSyncState();
  maybeFinishRestore();
}

void ForumerBackend::scheduleFollowUp() {
  if (m_followUpScheduled)
    return;
  m_followUpScheduled = true;
  QTimer::singleShot(kFollowUpWaitMs, this, [this]() {
    m_followUpScheduled = false;
    if (m_roundArrivals >= kFollowUpAt && m_followUps < kMaxFollowUps) {
      ++m_followUps;
      logEvent("catch-up: " + std::to_string(m_roundArrivals) +
               " post(s) in the last round, asking again");
      sendDigest("follow-up");   // resets the count and schedules the next check
    } else {
      m_followUps = 0;           // burst over
      maybeFinishRestore();
    }
  });
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
      // Settles only a post of the unlocked account. One sent before a lock
      // stays pending in its account's file and is retried after unlock.
      auto entry = m_posts->outboxEntry(postId.toStdString());
      if (!entry)
        return;
      // A late failure from an earlier attempt must not undo a success.
      if (entry->state == fc::SendState::Sent)
        return;
      m_posts->markState(postId.toStdString(),
                         state == QLatin1String("sent") ? fc::SendState::Sent : fc::SendState::Failed,
                         detail.toStdString());
      scheduleOwnSave();
    }
  } else if (m_posts && !m_posts->outboxEntry(postId.toStdString())) {
    return;
  }

  logEvent("send " + requestId.toStdString() + " -> " + state.toStdString() +
           (detail.isEmpty() ? "" : " (" + detail.toStdString() + ")"));
  emit messageStateChanged(postId, state, detail);
  publishSyncState();
}

void ForumerBackend::publishQuota() {
  // How much of the hourly limit (forumer_core/flood.h) the unlocked account
  // has left, for the composers: {"topics": {"left", "max", "waitMin"}, …}.
  QJsonObject quota;
  const qint64 now = nowMs();
  for (const auto kind : {fc::post::Kind::Post, fc::post::Kind::Reply}) {
    const int max = fc::flood::maxPerWindow(kind);
    int left = max;
    int waitMin = 0;
    if (m_account && m_posts) {
      const auto ts = m_posts->ownTimestamps(m_account->state().id, kind, now - fc::flood::kWindowMs);
      left = std::max(0, max - static_cast<int>(ts.size()));
      const int64_t wait = fc::flood::waitMs(kind, ts, now);
      waitMin = static_cast<int>((wait + 59'999) / 60'000);
    }
    quota.insert(kind == fc::post::Kind::Post ? QStringLiteral("topics") : QStringLiteral("replies"),
                 QJsonObject{{"left", left}, {"max", max}, {"waitMin", waitMin}});
  }
  setQuotaJson(QString::fromUtf8(QJsonDocument(quota).toJson(QJsonDocument::Compact)));
}

void ForumerBackend::publishSyncState() {
  if (!m_posts)
    return;
  publishOwnStates();
  publishQuota();
  setUnsentCount(static_cast<int>(m_posts->unsent().size()));

  // "synced" only once another peer has answered (F15): sending our own
  // digest into an empty network proves nothing.
  QStringList parts;
  if (m_lastPeerMs > 0)
    parts << QStringLiteral("synced %1")
                 .arg(QDateTime::fromMSecsSinceEpoch(m_lastPeerMs).toString(QStringLiteral("hh:mm")));
  else if (m_lastDigestMs > 0)
    parts << QStringLiteral("no peers yet");
  if (m_receivedCount > 0)
    parts << QStringLiteral("%1 received").arg(m_receivedCount);
  setSyncInfo(parts.join(QStringLiteral(" · ")));
}