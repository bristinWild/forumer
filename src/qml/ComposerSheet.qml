import QtQuick
import QtQuick.Layouts

// "new topic": title, opening message, domains (type your own,
// comma-separated, or tap a suggestion; up to 3), and who to post as.
Sheet {
    id: composer

    required property var store
    property string disclosure: store.defaultDisclosure
    property string error: ""
    property bool sending: false

    // Prebuilt suggestions, shown alongside domains already in use.
    readonly property var prebuilt: composer.store.suggestedDomains.concat(["crypto", "politics", "help"])
    readonly property var suggestions: {
        var typed = composer.typedDomains();
        var pool = composer.store.knownDomains.concat(composer.prebuilt);
        var out = [];
        pool.forEach(function (d) { if (out.indexOf(d) < 0 && typed.indexOf(d) < 0) out.push(d); });
        return out.slice(0, 8);
    }

    function typedDomains() {
        return domainsField.text.split(",").map(function (d) { return d.trim().toLowerCase().replace(/^#/, ""); })
                                           .filter(function (d) { return d.length > 0; });
    }

    function addDomain(d) {
        var list = composer.typedDomains();
        if (list.length >= 3 || list.indexOf(d) >= 0) return;
        list.push(d);
        domainsField.text = list.join(", ");
    }

    function post() {
        if (titleField.text.trim().length === 0 || composer.sending) return;
        composer.error = "";
        composer.sending = true;
        composer.store.createTopic(titleField.text.trim(), bodyArea.text, domainsField.text, composer.disclosure,
            function () {
                composer.sending = false;
                titleField.text = ""; bodyArea.text = ""; bodyArea.previewing = false; domainsField.text = "";
                composer.close();
                composer.store.open("home");
            },
            function (err) { composer.sending = false; composer.error = err; });
    }

    title: "new topic"
    onOpened: {
        composer.error = "";
        composer.disclosure = composer.store.defaultDisclosure;
        // Started from a domain's page: post there unless told otherwise.
        if (domainsField.text.trim().length === 0 && composer.store.domainFilter.length > 0)
            domainsField.text = composer.store.domainFilter;
        titleField.focusInput();
    }

    FField {
        id: titleField
        Layout.fillWidth: true
        label: "title"
        hint: titleField.text.length > 0 ? composer.store.sizeNote(titleField.text, composer.store.maxTitleBytes) : ""
        placeholder: "What do you want to ask or share?"
        onAccepted: bodyArea.focusInput()
    }

    MdEditor {
        id: bodyArea
        objectName: "composerBody"
        Layout.fillWidth: true
        label: "opening message"
        placeholder: "Add details (optional). Headings, bold, lists and links are welcome."
        minHeight: 140
        maxHeight: 300
    }
    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Layout.topMargin: -10
        visible: bodyArea.text.length > 0
        horizontalAlignment: Text.AlignRight
        text: composer.store.sizeNote(bodyArea.text, composer.store.maxBodyBytes)
        font.family: Ui.mono
        font.pixelSize: Ui.size.caption
        color: composer.store.utf8Bytes(bodyArea.text) > composer.store.maxBodyBytes ? Ui.failedText : Ui.text3
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10
        FField {
            id: domainsField
            Layout.fillWidth: true
            label: "domains"
            readonly property var preview: composer.store.domainPreview(text)
            hint: text.trim().length === 0 ? "up to 3 · type your own, comma-separated"
                : preview.kept.length === 0 ? "no usable domain: a-z, 0-9 and -"
                : "posts in #" + preview.kept.join(" #") + (preview.changed.length > 0 ? " · " + preview.changed.join(", ") : "")
            placeholder: "privacy, campus"
            mono: true
            onAccepted: composer.post()
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: composer.typedDomains().length < 3
            Repeater {
                model: composer.suggestions
                delegate: Chip {
                    required property string modelData
                    implicitHeight: 30
                    dashed: true
                    text: "+ " + modelData
                    onClicked: composer.addDomain(modelData)
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 10
        Text {
            textFormat: Text.PlainText
            text: "post as"
            font.family: Ui.sans
            font.pixelSize: Ui.size.small
            color: Ui.text2
        }
        Segmented {
            Layout.fillWidth: true
            options: composer.store.disclosureOptions()
            value: composer.disclosure
            onPicked: (v) => composer.disclosure = v
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: composer.store.disclosureHint(composer.disclosure)
            wrapMode: Text.WordWrap
            font.family: Ui.mono
            font.pixelSize: Ui.size.caption
            color: Ui.text3
        }
    }

    QuotaLine {
        visible: composer.store.unlocked && limit > 0
        quota: composer.store.quota.topics
        noun: "topic"
    }

    RestoreNotice { Layout.fillWidth: true; store: composer.store }

    Notice { Layout.fillWidth: true; message: composer.error }

    actions: [
        FButton { text: "cancel"; onClicked: composer.close() },
        FButton {
            kind: "primary"
            text: composer.sending ? "signing…" : "post"
            enabled: titleField.text.trim().length > 0 && !composer.sending && composer.store.canPost
                     && composer.store.topicsLeft
                     && composer.store.utf8Bytes(titleField.text) <= composer.store.maxTitleBytes
                     && composer.store.utf8Bytes(bodyArea.text) <= composer.store.maxBodyBytes
                     && (domainsField.text.trim().length === 0 || domainsField.preview.kept.length > 0)
            onClicked: composer.post()
        }
    ]
}
