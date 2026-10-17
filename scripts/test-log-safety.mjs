import assert from "node:assert/strict";
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";

const source = readFileSync("src/forumer_backend.cpp", "utf8");
const start = source.indexOf("void logEvent(");
const end = source.indexOf("qint64 nowMs()", start);

assert(start >= 0 && end > start, "logEvent not found");

const logger = source.slice(start, end);

assert.match(logger, /fc::scrubIds\(raw\)/);
assert.match(logger, /std::cerr\s*<<[\s\S]*?<< what << std::endl/);
assert.match(logger, /file\s*<<[\s\S]*?<< what << std::endl/);


// A5: prevent sensitive diagnostic values from being reintroduced.
assert.match(source, /logEvent\("storage: sharing started"\)/);
assert.match(source, /logEvent\("storage: dialing holder"\)/);
assert.match(source, /logEvent\(std::string\("send outcome: "\)/);

// These values may be used by the application, but must not be
// interpolated into the diagnostic log.
assert.doesNotMatch(
  source,
  /logEvent\("storage: sharing " \+ path\.toStdString\(\)\)/
);
assert.doesNotMatch(
  source,
  /logEvent\("storage: dialing " \+ peer\.toStdString\(\)/
);
assert.doesNotMatch(
  source,
  /logEvent\("send " \+ requestId\.toStdString\(\)/
);

const dir = mkdtempSync(join(tmpdir(), "forumer-log-test-"));

function check(path) {
  return spawnSync("bash", ["scripts/check-logs.sh", path], {
    encoding: "utf8",
  });
}

try {
  const path = join(dir, "backend.log");
  const secret = "a".repeat(64);

  writeFileSync(path, "[forumer backend] posted aaaaaaaa...\n");
  assert.equal(check(path).status, 0, "redacted log should pass");

  writeFileSync(path, `[forumer backend] posted ${secret}\n`);
  const leaked = check(path);

  assert.equal(leaked.status, 1, "full identifier must fail");
  assert(
    !(leaked.stdout + leaked.stderr).includes(secret),
    "log checker must not print leaked identifiers"
  );

  assert.equal(check(join(dir, "missing.log")).status, 2);

  console.log("PASS: backend logging and log-leak detection");
} finally {
  rmSync(dir, { recursive: true, force: true });
}
