import QtQuick
import QtQuick.Layouts
import "markdown.js" as Md

// The post body editor: Markdown with a small toolbar, shortcuts and a
// Write / Preview switch. Same surface as FArea (text, placeholder, label,
// bare, editable, input, focusInput()), so composers swap one for the other.
//
//   H1 H2 · B I code · list numbered quote · link        write | preview
//
// Shortcuts: ⌘/Ctrl+B bold, +I italic, +E code. Enter continues a list or
// quote; Enter on an empty item ends it.
ColumnLayout {
    id: editor

    property string label: ""
    property alias text: area.text
    property string placeholder: ""
    property int minHeight: 96
    property int maxHeight: 320
    property bool bare: false
    property bool editable: true
    property bool compact: false        // reply composer: fewer tools
    property alias input: area.input
    property bool previewing: false

    function focusInput() { editor.previewing = false; area.focusInput(); }

    spacing: 8

    // ── Editing helpers ───────────────────────────────────────────────────────
    readonly property var edit: area.input

    function selection() {
        return { s: edit.selectionStart, e: edit.selectionEnd, t: edit.text };
    }

    // Replace [a, b) with `str`, then select [selA, selB) (absolute offsets).
    function replaceRange(a, b, str, selA, selB) {
        edit.remove(a, b);
        edit.insert(a, str);
        edit.select(selA, selB);
        edit.forceActiveFocus();
    }

    // **bold**, _italic_, `code`: wrap the selection, or unwrap it if already
    // wrapped. With nothing selected, insert the pair and put the cursor inside.
    function wrap(mark) {
        var x = selection();
        var m = mark.length;
        if (x.s >= m && x.t.substring(x.s - m, x.s) === mark && x.t.substring(x.e, x.e + m) === mark) {
            replaceRange(x.s - m, x.e + m, x.t.substring(x.s, x.e), x.s - m, x.e - m);
            return;
        }
        var inner = x.t.substring(x.s, x.e);
        replaceRange(x.s, x.e, mark + inner + mark, x.s + m, x.e + m);
    }

    // Line-level markers (headings, lists, quotes) on every line the
    // selection touches. Applying the same marker again removes it.
    function lines(kind) {
        var x = selection();
        var a = x.t.lastIndexOf("\n", x.s - 1) + 1;
        var b = x.t.indexOf("\n", x.e);
        if (b < 0) b = x.t.length;
        var block = x.t.substring(a, b).split("\n");
        var strip = /^(#{1,6}\s+|>\s?|[-*+]\s+|\d+[.)]\s+)/;
        var marker = function (i) {
            return kind === "h1" ? "# " : kind === "h2" ? "## " : kind === "quote" ? "> "
                 : kind === "list" ? "- " : (i + 1) + ". ";
        };
        var already = block.every(function (l, i) {
            return l.length === 0 || l.indexOf(marker(i)) === 0;
        });
        var out = block.map(function (l, i) {
            var bare = l.replace(strip, "");
            if (already) return bare;
            return l.length === 0 && block.length > 1 ? l : marker(i) + bare;
        }).join("\n");
        replaceRange(a, b, out, a + out.length, a + out.length);
    }

    // `code` for part of a line, a fenced block for whole lines.
    function code() {
        var x = selection();
        var inner = x.t.substring(x.s, x.e);
        if (inner.indexOf("\n") < 0 && inner.length > 0) { wrap("`"); return; }
        if (inner.length === 0) { wrap("`"); return; }
        var lead = x.s > 0 && x.t.charAt(x.s - 1) !== "\n" ? "\n" : "";
        var block = lead + "```\n" + inner + "\n```\n";
        replaceRange(x.s, x.e, block, x.s + lead.length + 4, x.s + lead.length + 4 + inner.length);
    }

    // [text](https://): selects the part still to fill in.
    function link() {
        var x = selection();
        var inner = x.t.substring(x.s, x.e);
        if (/^https?:\/\/\S+$/.test(inner)) {
            replaceRange(x.s, x.e, "[link](" + inner + ")", x.s + 1, x.s + 5);
        } else if (inner.length > 0) {
            var s = "[" + inner + "](https://)";
            replaceRange(x.s, x.e, s, x.s + inner.length + 3, x.s + inner.length + 11);
        } else {
            replaceRange(x.s, x.e, "[text](https://)", x.s + 1, x.s + 5);
        }
    }

    // Enter inside a list or quote: continue it ("- ", "3. ", "> "); on an
    // empty item, end it instead. False when the line isn't one.
    function continueBlock() {
        var x = selection();
        if (x.s !== x.e) return false;
        var a = x.t.lastIndexOf("\n", x.s - 1) + 1;
        var line = x.t.substring(a, x.s);
        var m = /^(\s*)([-*+]\s+|(\d+)([.)])\s+|>\s?)/.exec(line);
        if (!m) return false;
        if (line.length === m[0].length) {               // empty item: end the list
            replaceRange(a, x.s, "", a, a);
            return true;
        }
        var next = m[3] !== undefined ? m[1] + (Number(m[3]) + 1) + m[4] + " " : m[1] + m[2];
        replaceRange(x.s, x.s, "\n" + next, x.s + 1 + next.length, x.s + 1 + next.length);
        return true;
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: 2
        visible: editor.editable

        Text {
            textFormat: Text.PlainText
            visible: editor.label.length > 0
            Layout.rightMargin: 10
            text: editor.label
            font.family: Ui.sans
            font.pixelSize: Ui.size.small
            color: Ui.text2
        }

        Row {
            spacing: 2
            visible: !editor.previewing
            MdTool { visible: !editor.compact; label: "H1"; tooltip: "Heading"; onClicked: editor.lines("h1") }
            MdTool { visible: !editor.compact; label: "H2"; tooltip: "Subheading"; onClicked: editor.lines("h2") }
            Rectangle { visible: !editor.compact; width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: Ui.border }
            MdTool { label: "B"; bold: true; tooltip: "Bold  ⌘B"; onClicked: editor.wrap("**") }
            MdTool { label: "I"; italic: true; tooltip: "Italic  ⌘I"; onClicked: editor.wrap("*") }
            MdTool { label: "</>"; mono: true; tooltip: "Code  ⌘E"; onClicked: editor.code() }
            Rectangle { width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: Ui.border }
            MdTool { icon: "list"; tooltip: "Bulleted list"; onClicked: editor.lines("list") }
            MdTool { visible: !editor.compact; label: "1."; mono: true; tooltip: "Numbered list"; onClicked: editor.lines("numbered") }
            MdTool { label: "❝"; tooltip: "Quote"; onClicked: editor.lines("quote") }
            MdTool { icon: "link"; tooltip: "Link"; onClicked: editor.link() }
        }

        Item { Layout.fillWidth: true }

        // write | preview
        Row {
            spacing: 2
            Repeater {
                model: ["write", "preview"]
                delegate: Rectangle {
                    required property string modelData
                    readonly property bool on: (modelData === "preview") === editor.previewing
                    width: tabText.implicitWidth + 18
                    height: 26
                    radius: Ui.radius.m
                    color: on ? Ui.active : (tabMouse.containsMouse ? Ui.hover : "transparent")
                    Text {
                        textFormat: Text.PlainText
                        id: tabText
                        anchors.centerIn: parent
                        text: modelData
                        font.family: Ui.mono
                        font.pixelSize: Ui.size.caption
                        color: parent.on ? Ui.text : Ui.text3
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            editor.previewing = modelData === "preview";
                            if (!editor.previewing) area.focusInput();
                        }
                    }
                }
            }
        }
    }

    FArea {
        id: area
        Layout.fillWidth: true
        visible: !editor.previewing
        placeholder: editor.placeholder
        minHeight: editor.minHeight
        maxHeight: editor.maxHeight
        bare: editor.bare
        editable: editor.editable

        onKeyPressed: function (event) {
            var mod = event.modifiers & (Qt.ControlModifier | Qt.MetaModifier);
            if (mod && !(event.modifiers & Qt.ShiftModifier)) {
                if (event.key === Qt.Key_B) { editor.wrap("**"); event.accepted = true; return; }
                if (event.key === Qt.Key_I) { editor.wrap("*"); event.accepted = true; return; }
                if (event.key === Qt.Key_E) { editor.code(); event.accepted = true; return; }
            }
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && event.modifiers === Qt.NoModifier) {
                if (editor.continueBlock()) event.accepted = true;
            }
        }
    }

    // Preview: exactly how the post will look.
    Rectangle {
        Layout.fillWidth: true
        visible: editor.previewing
        implicitHeight: Math.max(editor.minHeight, Math.min(editor.maxHeight, preview.implicitHeight + 28))
        radius: Ui.radius.l
        color: editor.bare ? "transparent" : Ui.field
        border.width: editor.bare ? 0 : 1
        border.color: Ui.borderStrong
        clip: true

        Flickable {
            anchors.fill: parent
            anchors.margins: editor.bare ? 4 : 14
            contentHeight: preview.implicitHeight
            clip: true
            MdText {
                id: preview
                width: parent.width
                source: editor.text.trim().length > 0 ? editor.text : "_nothing to preview yet_"
                color: editor.text.trim().length > 0 ? Ui.textBody : Ui.text3
            }
        }
    }

    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: editor.editable && !editor.compact && !editor.previewing
        text: "markdown: # heading · **bold** · *italic* · `code` · - list · > quote · [link](https://…)"
        elide: Text.ElideRight
        font.family: Ui.mono
        font.pixelSize: Ui.size.caption
        color: Ui.text3
    }
}
