#pragma once

#include <string>

/// Minimal RFC 4648 base64 decoder.
///
/// Logos modules base64-encode payloads in some responses: delivery_module's
/// storeQuery() returns message payloads that way (and storage_module's
/// downloadChunks() encodes each chunk independently — decode each chunk on
/// arrival and concatenate the raw bytes, never the base64 text, which breaks
/// when an interior chunk carries padding).
///
/// Lives in src/ rather than lib/forumer_core deliberately: this is
/// wire-format knowledge about specific Logos modules, and forumer_core deals
/// only in raw bytes.
std::string base64Decode(const std::string& input);