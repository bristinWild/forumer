.pragma library

// Markdown for post bodies. Bodies are stored and sent as plain Markdown (the
// envelope's `body` field is unchanged), and rendered with Qt's Markdown
// support. Two helpers:
//
//   safe(md)  - what may be handed to a MarkdownText renderer. Raw HTML and
//               images are neutralised, so a post can never make this app
//               fetch anything from the network (an image URL would reveal
//               the reader's IP address to whoever posted it) or inject
//               markup. Code spans and fenced blocks are left as written.
//   plain(md) - one line of text for excerpts, lists and notices.

// A "<" is shown literally when an odd number of backslashes stands right
// before it (Markdown's escape), so each "<" gets one backslash added when
// the run in front of it is even (none included). The old escape added one
// unconditionally, and a backslash already in the text cancelled it: \<b>
// became \\<b>, an escaped backslash followed by a live tag (review F4).
// HTML entities ("&lt;") aren't an option: some Qt versions show them as
// typed instead of decoding them.
function escapeLt(s) {
    var out = "", run = 0;
    for (var i = 0; i < s.length; ++i) {
        var c = s[i];
        if (c === "<" && run % 2 === 0) out += "\\";
        out += c;
        run = c === "\\" ? run + 1 : 0;
    }
    return out;
}

// Neutralise one line of non-code text. Links are rebuilt as our own small
// anchor (only http/https; others keep just their text) so they can take the
// theme's link colour - Qt's Markdown renderer ignores Text.linkColor.
function safeText(s, linkColor) {
    s = s
        // inline images become their alt text: ![alt](url) -> alt
        .replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1")
        // any other image syntax (reference style) is shown literally
        .replace(/!\[/g, "!\\[");
    // raw HTML is shown literally: <b> -> \<b>
    s = escapeLt(s);
    return s.replace(/\[([^\]]+)\]\(([^)\s]*)\)/g, function (all, label, url) {
        if (!/^https?:\/\/[^\s"'<>]+$/i.test(url)) return label;
        return '<a href="' + url + '"><span style="color:' + linkColor + ';">' + label + '</span></a>';
    });
}

// The code spans of one line, as Markdown finds them: a run of N backticks
// opens a span that the next run of exactly N backticks closes. A run with a
// backslash right before it is never treated as an opener here - Markdown may
// read it as an escaped backtick, and text we wrongly took for code would go
// out unescaped. (Erring the other way only shows a stray backslash.)
// Returns { spans: [{start, end}] (closing backticks included), open: bool }
// where `open` says a backtick run was left unmatched.
function scanCode(line) {
    var spans = [], open = false;
    var i = 0;
    while (i < line.length) {
        if (line[i] !== "`") { ++i; continue; }
        var start = i;
        while (i < line.length && line[i] === "`") ++i;
        var n = i - start;
        if (start > 0 && line[start - 1] === "\\") { open = true; continue; }
        // Find the next run of exactly n backticks.
        var j = i, close = -1;
        while (j < line.length) {
            if (line[j] !== "`") { ++j; continue; }
            var run = j;
            while (j < line.length && line[j] === "`") ++j;
            if (j - run === n) { close = j; break; }
        }
        if (close < 0) { open = true; continue; }   // unmatched: plain text
        spans.push({ start: start, end: close });
        i = close;
    }
    return { spans: spans, open: open };
}
function codeSpans(line) { return scanCode(line).spans; }

// One line outside a fenced block: escape everything except code spans.
// `trusted` false (a backtick was left open earlier in the paragraph, so
// Markdown may pair backticks across lines differently than one line shows):
// escape the whole line, code spans included.
function safeLine(line, linkColor, trusted) {
    var spans = trusted ? scanCode(line).spans : [];
    var out = "", at = 0;
    for (var k = 0; k < spans.length; ++k) {
        out += safeText(line.slice(at, spans[k].start), linkColor);
        out += line.slice(spans[k].start, spans[k].end);
        at = spans[k].end;
    }
    return out + safeText(line.slice(at), linkColor);
}

// A fenced code block opens only at the start of a line: a fence Markdown
// might place inside a list item or quote (indented, or after "> ") is not
// taken for one here, so its lines are escaped like any others - never the
// other way round. It closes on the same character, at least as many times,
// indented at most 3 spaces, with nothing after it.
function fenceOpen(line) {
    var m = /^(`{3,}|~{3,})(.*)$/.exec(line);
    if (!m || (m[1][0] === "`" && m[2].indexOf("`") >= 0)) return null;
    return { ch: m[1][0], n: m[1].length };
}
function fenceCloses(line, fence) {
    var m = /^ {0,3}(`+|~+)[ \t]*$/.exec(line);
    return m !== null && m[1][0] === fence.ch && m[1].length >= fence.n;
}

function safe(md, linkColor) {
    linkColor = /^#[0-9a-fA-F]{6,8}$/.test(linkColor || "") ? linkColor : "#60a5fa";
    if (!md) return "";
    var lines = String(md).split("\n");
    var fence = null;
    var paragraphOpen = false;   // an unmatched backtick earlier in this paragraph
    for (var i = 0; i < lines.length; ++i) {
        var line = lines[i];
        if (fence) {
            if (fenceCloses(line, fence)) fence = null;
            continue;                               // fenced content: shown as written
        }
        var f = fenceOpen(line);
        if (f) { fence = f; paragraphOpen = false; continue; }
        if (/^\s*$/.test(line)) { paragraphOpen = false; continue; }
        lines[i] = safeLine(line, linkColor, !paragraphOpen);
        if (scanCode(line).open) paragraphOpen = true;
    }
    return lines.join("\n");
}

function plain(md) {
    if (!md) return "";
    return String(md)
        .replace(/^\s*(```|~~~).*$/gm, "")             // code fences
        .replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1")       // images -> alt
        .replace(/\[([^\]]*)\]\([^)]*\)/g, "$1")        // links -> text
        .replace(/^\s{0,3}#{1,6}\s+/gm, "")             // headings
        .replace(/^\s{0,3}>\s?/gm, "")                  // quotes
        .replace(/^\s*([-*+]|\d+[.)])\s+/gm, "")         // list markers
        .replace(/(\*\*|__|~~)(.+?)\1/g, "$2")          // bold, strike
        .replace(/(^|[^\w*])[*_]([^*_\n]+)[*_](?=[^\w*]|$)/g, "$1$2")  // italic
        .replace(/`([^`]*)`/g, "$1")                    // code spans
        .replace(/\\([!-\/:-@\[-`{-~])/g, "$1")         // backslash escapes: \< -> <
        .replace(/\s+/g, " ")
        .trim();
}

// Whether a body uses any Markdown worth rendering (else show it as typed).
function hasMarkup(md) {
    return /(^|\n)\s{0,3}(#{1,6}\s|>|[-*+]\s|\d+[.)]\s|```)|\*\*|__|`|\[[^\]]+\]\(|(^|\W)[*_]\S/.test(md || "");
}