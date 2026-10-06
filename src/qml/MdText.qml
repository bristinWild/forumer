import QtQuick
import "markdown.js" as Md

// A post body, rendered from Markdown: headings, bold, italic, lists, quotes,
// code and links. Images and HTML are never loaded (markdown.js). Links open
// in the browser only when clicked, and only http(s) ones.
Text {
    id: md

    property string source: ""

    text: Md.safe(md.source, String(Ui.link))
    textFormat: Text.MarkdownText
    wrapMode: Text.Wrap
    font.family: Ui.sans
    font.pixelSize: Ui.size.reading
    lineHeight: 1.25
    color: Ui.textBody
    linkColor: Ui.link

    onLinkActivated: function (link) {
        if (/^https?:\/\//i.test(link)) Qt.openUrlExternally(link);
    }

    HoverHandler {
        cursorShape: md.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
