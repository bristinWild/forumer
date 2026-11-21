import QtQuick
import "markdown.js" as Md

// A post body, rendered from Markdown: headings, bold, italic, lists, quotes,
// code and links. Images and HTML are never loaded (markdown.js). The text can
// be selected and copied (read-only, like the thread title). Links open in the
// browser only when clicked, and only http(s) ones.
TextEdit {
    id: md

    property string source: ""

    text: Md.safe(md.source, String(Ui.link))
    textFormat: TextEdit.MarkdownText
    readOnly: true
    selectByMouse: true
    persistentSelection: true
    wrapMode: TextEdit.Wrap
    font.family: Ui.sans
    font.pixelSize: Ui.size.reading
    color: Ui.textBody
    selectionColor: Ui.borderStrong
    selectedTextColor: Ui.text

    onLinkActivated: function (link) {
        if (/^https?:\/\//i.test(link)) Qt.openUrlExternally(link);
    }

    HoverHandler {
        cursorShape: md.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
    }
}