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

// Neutralise one line of non-code text. Links are rebuilt as our own small
// anchor (only http/https; others keep just their text) so they can take the
// theme's link colour - Qt's Markdown renderer ignores Text.linkColor.
//
// "<" becomes the entity "&lt;". Markdown decodes entities to plain text and
// never parses them as markup, and no backslash in front of one can turn it
// back into a tag. (The old escape, a backslash before "<", was undone by a
// backslash already in the text: \<b> became \\<b>, which Markdown reads as
// an escaped backslash followed by a live tag.)
function safeText(s, linkColor) {
    s = s
        // inline images become their alt text: ![alt](url) -> alt
        .replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1")
        // any other image syntax (reference style) is shown literally
        .replace(/!\[/g, "!\\[")
        // raw HTML is shown literally: <b> -> &lt;b>
        .replace(/</g, "&lt;");
    return s.replace(/\[([^\]]+)\]\(([^)\s]*)\)/g, function (all, label, url) {
        if (!/^https?:\/\/[^\s"'<>]+$/i.test(url)) return label;
        return '<a href="' + url + '"><span style="color:' + linkColor + ';">' + label + '</span></a>';
    });
}

// The code spans of one line, as Markdown finds them: a run of N backticks
// opens a span that the next run of exactly N backticks closes. A run with a
// backslash right before it is never treated as an opener here - Markdown may
// read it as an escaped backtick, and text we wrongly took for code would go
// out unescaped. (Erring the other way only shows "&lt;" inside real code.)
// Returns [{start, end}] index ranges, closing backticks included.
function codeSpans(line) {
    var spans = [];
    var i = 0;
    while (i < line.length) {
        if (line[i] !== "`") { ++i; continue; }
        var open = i;
        while (i < line.length && line[i] === "`") ++i;
        var n = i - open;
        if (open > 0 && line[open - 1] === "\\") continue;   // maybe escaped: not an opener
        // Find the next run of exactly n backticks.
        var j = i, close = -1;
        while (j < line.length) {
            if (line[j] !== "`") { ++j; continue; }
            var run = j;
            while (j < line.length && line[j] === "`") ++j;
            if (j - run === n) { close = j; break; }
        }
        if (close < 0) continue;            // unmatched: the backticks are plain text
        spans.push({ start: open, end: close });
        i = close;
    }
    return spans;
}

// One line outside a fenced block: escape everything except code spans.
function safeLine(line, linkColor) {
    var spans = codeSpans(line);
    var out = "", at = 0;
    for (var k = 0; k < spans.length; ++k) {
        out += safeText(line.slice(at, spans[k].start), linkColor);
        out += line.slice(spans[k].start, spans[k].end);
        at = spans[k].end;
    }
    return out + safeText(line.slice(at), linkColor);
}

function safe(md, linkColor) {
    linkColor = /^#[0-9a-fA-F]{6,8}$/.test(linkColor || "") ? linkColor : "#60a5fa";
    if (!md) return "";
    var lines = String(md).split("\n");
    var inFence = false;
    for (var i = 0; i < lines.length; ++i) {
        if (/^\s*(```|~~~)/.test(lines[i])) { inFence = !inFence; continue; }
        if (!inFence) lines[i] = safeLine(lines[i], linkColor);
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
        .replace(/\s+/g, " ")
        .trim();
}

// Whether a body uses any Markdown worth rendering (else show it as typed).
function hasMarkup(md) {
    return /(^|\n)\s{0,3}(#{1,6}\s|>|[-*+]\s|\d+[.)]\s|```)|\*\*|__|`|\[[^\]]+\]\(|(^|\W)[*_]\S/.test(md || "");
}