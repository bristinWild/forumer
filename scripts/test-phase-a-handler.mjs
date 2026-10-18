import { readFileSync } from "node:fs";
import assert from "node:assert/strict";

const source = readFileSync("src/forumer_backend.cpp", "utf8");

const start = source.indexOf("void ForumerBackend::handleMediaAsk(");
const end = source.indexOf("void ForumerBackend::handleMediaHere(", start);

assert(start >= 0 && end > start, "MediaAsk handler not found");

const handler = source.slice(start, end);

assert.match(
  handler,
  /if\s*\(!m_mediaAskBudget\.take\(nowMs\(\)\)\)\s*return\s*;/,
  "MediaAsk must return when its budget is exhausted"
);

const budget = handler.indexOf("m_mediaAskBudget.take(nowMs())");
const preview = handler.indexOf("m_posts->previewsOf(cid)");
const queued = handler.indexOf("QTimer::singleShot(200 + jitter");
const storage = handler.indexOf("storage_module.exists(qs(cid))");

for (const [name, position] of [
  ["preview lookup", preview],
  ["queued preview", queued],
  ["Storage exists", storage],
]) {
  assert(position >= 0, `${name} not found`);
  assert(budget < position, `MediaAsk budget must precede ${name}`);
}

console.log("PASS: MediaAsk budget precedes all expensive handler work");
