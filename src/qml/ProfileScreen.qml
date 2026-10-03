import QtQuick
import QtQuick.Layouts

// profile & personas: the persona you post as, how it rotates, your alias,
// and what "post as" defaults to.
Page {
    id: profile

    required property var store
    property string message: ""

    title: "profile"
    subtitle: "your identity never leaves this device. people only ever see personas made from it — or nothing at all."

    Section {
        title: "current persona"
        caption: "who your next post is signed as"

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 14
            Text {
                text: profile.store.myPersona
                font.family: Ui.mono
                font.pixelSize: 28
                font.weight: Ui.weight.medium
                color: Ui.text
            }
            Text {
                Layout.fillWidth: true
                text: "a persona is a key made from your identity. anyone can check a post was signed by it; nobody can tell two of your personas belong to the same person."
                wrapMode: Text.WordWrap
                font.family: Ui.sans
                font.pixelSize: Ui.size.ui
                lineHeight: 1.35
                color: Ui.text2
            }
        }
    }

    Section {
        title: "rotation"
        caption: "when to switch to a fresh persona"

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Segmented {
                    Layout.preferredWidth: 420
                    options: [
                        { value: "keep", label: "keep" },
                        { value: "manual", label: "manual" },
                        { value: "auto", label: "auto · every post" }
                    ]
                    value: profile.store.rotationPolicy
                    onPicked: (v) => profile.store.call(profile.store.backend.chooseRotationPolicy(v))
                }
                FButton {
                    visible: profile.store.rotationPolicy === "manual"
                    text: "rotate now"
                    icon: "refresh"
                    implicitHeight: 46
                    onClicked: profile.store.call(profile.store.backend.rotatePersona(),
                                                  function () { profile.message = "new persona: " + profile.store.myPersona; })
                }
            }
            Text {
                Layout.fillWidth: true
                text: profile.store.rotationPolicy === "keep" ? "always the same persona, so people can follow what you write."
                    : profile.store.rotationPolicy === "auto" ? "every post gets a persona of its own, so no two of your posts can be linked."
                    : "the same persona until you press “rotate now”."
                wrapMode: Text.WordWrap
                font.family: Ui.mono
                font.pixelSize: Ui.size.caption
                color: Ui.text3
            }
            Notice { Layout.fillWidth: true; error: false; message: profile.message }
        }
    }

    Section {
        title: "alias"
        caption: "a display name, shown next to the persona"

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 12
            FField {
                id: aliasField
                Layout.preferredWidth: 320
                placeholder: "e.g. night owl"
                text: profile.store.alias
                onAccepted: profile.store.call(profile.store.backend.chooseAlias(aliasField.text))
            }
            FButton {
                Layout.alignment: Qt.AlignBottom
                implicitHeight: 44
                text: "save"
                enabled: aliasField.text !== profile.store.alias
                onClicked: profile.store.call(profile.store.backend.chooseAlias(aliasField.text))
            }
            Item { Layout.fillWidth: true }
        }
    }

    Section {
        title: "post as, by default"
        caption: "you can still change it on each post"

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 12
            Segmented {
                Layout.preferredWidth: 420
                options: profile.store.disclosureOptions()
                value: profile.store.defaultDisclosure
                onPicked: (v) => profile.store.call(profile.store.backend.chooseDefaultDisclosure(v))
            }
            Text {
                Layout.fillWidth: true
                text: profile.store.disclosureHint(profile.store.defaultDisclosure)
                wrapMode: Text.WordWrap
                font.family: Ui.mono
                font.pixelSize: Ui.size.caption
                color: Ui.text3
            }
        }
    }
}
