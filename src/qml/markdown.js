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
function safeText(s, linkColor) {
    s = s
        // inline images become their alt text: ![alt](url) -> alt
        .replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1")
        // any other image syntax (reference style) is shown literally
        .replace(/!\[/g, "!\\[")
        // raw HTML is shown literally: <b> -> \<b>
        .replace(/</g, "\\<");
    return s.replace(/\[([^\]]+)\]\(([^)\s]*)\)/g, function (all, label, url) {
        if (!/^https?:\/\/[^\s"'<>]+$/i.test(url)) return label;
        return '<a href="' + url + '"><span style="color:' + linkColor + ';">' + label + '</span></a>';
    });
}

// One line outside a fenced block: escape everything except `code spans`.
function safeLine(line, linkColor) {
    var parts = line.split("`");
    for (var i = 0; i < parts.length; i += 2)
        parts[i] = safeText(parts[i], linkColor);
    // An unmatched backtick leaves the tail unescaped: escape it too.
    if (parts.length % 2 === 0)
        parts[parts.length - 1] = safeText(parts[parts.length - 1], linkColor);
    return parts.join("`");
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