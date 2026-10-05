#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

// The per-build aggregate of dependency wrappers behind LogosUiPluginContext's
// modules(). Forward-declared so this header stays free of the generated
// umbrella; the .cpp includes logos_sdk.h to make it complete.
struct LogosModules;

/// Forumer's connection to the Logos network: a thin layer over
/// modules().delivery_module.
///
/// Two modes. Plain relay (subscribe/send) is what Forumer uses: every
/// message is delivered the moment it arrives, and reliability comes from
/// Forumer's own digests (forumer_core/sync.h). Reliable channels (SDS) are
/// kept as an option but OFF: SDS delivers a sender's messages in causal
/// order, so when one message is lost for good (the receiver was offline and
/// no store node has it), everything that sender sends afterwards - including
/// the digest answer that would repair the gap - is held back as having
/// "missing dependencies". Observed on delivery_module v0.2.1, 2026-10-04.
///
/// With channels on, the channel id is the content topic itself, so every
/// peer agrees on it without coordination, and the node falls back to plain
/// subscribe/send if it has no channel manager.
///
/// What travels is opaque bytes; framing and verification live in
/// forumer_core (sync.h). Inbound messages are not routed here: delivery
/// events are a node-wide stream the backend subscribes to directly.
///
/// Called from the backend's thread only.
class DeliveryModuleTransport {
public:
    struct SendResult {
        bool ok = false;
        std::string requestId;  // what messageSent / messageError later report
        std::string error;
    };

    /// `peerId` names this install inside reliable channels (SDS sender id).
    /// Random, stable per install, and unrelated to any identity.
    /// `useChannels` false means plain relay only (see above).
    DeliveryModuleTransport(LogosModules& modules, std::string peerId, bool useChannels);

    /// Start receiving `contentTopic`. "" on success, else the reason.
    std::string join(const std::string& contentTopic);

    /// Publish one payload on `contentTopic` (joined or not).
    SendResult publish(const std::string& contentTopic, const std::vector<uint8_t>& payload);

    /// False when channels are off, or once the node has refused one.
    bool usingChannels() const { return channelsAvailable_; }

    /// One page of a store query: the payloads, and where the next page
    /// starts ("" = this was the last).
    struct StorePage {
        bool ok = false;
        std::string error;
        std::vector<std::vector<uint8_t>> payloads;
        std::string cursor;
    };

    /// Asks the store node at `peerAddr` (a full multiaddr with /p2p/) for one
    /// page of `contentTopic` messages since `sinceMs`, oldest first. Async:
    /// `done` runs later on this thread. storeQuery is flagged upstream as
    /// unstable, so every field of the answer is checked (parseStorePage).
    void queryStorePage(const std::string& contentTopic, int cluster, int64_t sinceMs,
                        const std::string& cursor, const std::string& peerAddr,
                        int64_t timeoutMs, std::function<void(StorePage)> done);

    /// StoreQueryResponseHex -> StorePage. Never throws: a store node's last
    /// page carries "paginationCursor": null, and anything unexpected is an
    /// error rather than a crash.
    static StorePage parseStorePage(const std::string& responseJson);

    /// The relay shard a content topic lands on under autosharding: SHA-256
    /// of the topic's application and version fields, last 8 bytes
    /// big-endian, modulo the shard count. "/forumer/3/public/proto" on
    /// cluster 3 -> "/waku/2/rs/3/4". Store queries filter on it.
    static std::string autoshardPubsubTopic(const std::string& contentTopic, int cluster,
                                            int shards = 8);

private:
    // Opens (or re-opens after a restart) the channel for `topic`. False when
    // the node has no reliable-channel manager; that is latched.
    bool ensureChannel(const std::string& topic);

    LogosModules& modules_;
    std::string peerId_;
    std::set<std::string> channels_;
    bool channelsAvailable_ = true;
};