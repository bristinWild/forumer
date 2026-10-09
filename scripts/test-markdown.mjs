#!/usr/bin/env node
// Tests for src/qml/markdown.js, the sanitiser between post bodies (written by
// anyone) and Qt's Markdown renderer. Needs only Node:
//
//     node scripts/test-markdown.mjs
//
// The property under test: outside a code span, the only "<" that reaches the
// renderer is the start of our own link anchor, so no post can inject markup,
// load an image, or hide the text after it (review finding F4).

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const source = fs
    .readFileSync(path.join(here, "../src/qml/markdown.js"), "utf8")
    .replace(/^\.pragma library\s*$/m, "");
const Md = new Function(source + "; return { safe, plain, codeSpans };")();

let failures = 0;
function check(name, ok, detail) {
    if (ok) return;
    ++failures;
    console.error("FAIL " + name + (detail ? "\n     " + detail : ""));
}

// Remove what we emit on purpose (our link anchors) and what Markdown shows
// verbatim (fenced blocks, code spans as codeSpans() finds them); whatever
// "<" is left would reach the renderer as markup.
function liveMarkup(out) {
    let inFence = false;
    const lines = out.split("\n").map(function (line) {
        if (/^\s*(```|~~~)/.test(line)) { inFence = !inFence; return ""; }
        if (inFence) return "";
        let rest = "", at = 0;
        for (const s of Md.codeSpans(line)) { rest += line.slice(at, s.start); at = s.end; }
        rest += line.slice(at);
        return rest
            .replace(/<a href="https?:\/\/[^\s"'<>]+"><span style="color:#[0-9a-fA-F]{6,8};">/g, "")
            .replace(/<\/span><\/a>/g, "");
    });
    return lines.join("\n").match(/<[^ ]{0,20}/g) || [];
}

const attacks = [
    "<b>bold</b> after",
    "\\<b>bold</b> after",              // the review's bypass
    "\\\\<b>bold</b> after",
    "\\\\\\<b>bold</b> after",
    "<img src=http://127.0.0.1:9/p.png> after",
    "``x` <b>y</b>` after",             // backtick runs of different lengths
    "\\` <b>y</b> ` after",             // an escaped backtick, then a real one
    "`a` <b>c</b> `d` after",
    "<font color=red>red</font>",
    "<http://evil.example/x>",
    "x <!-- hidden --> y",
    "<a href=\"http://evil\">evil</a>",
    "[x](http://a\"onmouseover=\"y)",
    "[label <b>x</b>](https://ok.example)",
    "```\n<b>in a fence</b>\n```\n<b>after the fence</b>",
    "~~~\n```\n~~~\n<img src=x>",
];
for (const input of attacks) {
    const out = Md.safe(input, "#60a5fa");
    const live = liveMarkup(out);
    check("no live markup: " + JSON.stringify(input), live.length === 0,
          "output " + JSON.stringify(out) + " still has " + JSON.stringify(live));
}

// What should keep working.
check("plain text unchanged", Md.safe("hello world", "#60a5fa") === "hello world");
check("code span left as written", Md.safe("use `<b>` here", "#60a5fa") === "use `<b>` here");
check("http link becomes our anchor",
      Md.safe("[logos](https://logos.co)", "#60a5fa") ===
      '<a href="https://logos.co"><span style="color:#60a5fa;">logos</span></a>');
check("non-http link keeps only its text", Md.safe("[x](javascript:alert)", "#60a5fa") === "x");
check("image becomes its alt text", Md.safe("![cat](http://x/c.png)", "#60a5fa") === "cat");
check("bad link colour falls back", Md.safe("[a](https://a.b)", "red;x").indexOf("#60a5fa") >= 0);
check("plain() strips markup", Md.plain("**bold** and `code`") === "bold and code");

// codeSpans: CommonMark's backtick rule, conservative around backslashes.
const spans = function (s) { return JSON.stringify(Md.codeSpans(s)); };
check("single span", spans("a `b` c") === JSON.stringify([{ start: 2, end: 5 }]));
check("double backticks need a double close", spans("``a`b``") === JSON.stringify([{ start: 0, end: 7 }]));
check("unmatched run is text", spans("``a` b") === "[]");
check("escaped opener is not code", spans("\\`a` b") === "[]");

if (failures) {
    console.error(failures + " markdown test(s) failed");
    process.exit(1);
}
console.log("markdown: all tests passed");
