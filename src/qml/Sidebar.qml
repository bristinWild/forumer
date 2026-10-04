import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts

// Left column: wordmark, search (⌘K / Ctrl+K), connection line, navigation,
// followed domains, the "you" section, and the account card with the theme
// toggle and lock.
Rectangle {
    id: sidebar

    required property var store
    signal lockRequested()

    function focusSearch() { searchInput.forceActiveFocus(); searchInput.selectAll(); }

    color: Ui.sidebar

    Rectangle {   // hairline on the right edge
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Ui.border
    }

    B.ScrollView {
        id: scroller
        anchors.fill: parent
        anchors.rightMargin: 1
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scroller.availableWidth
            // At least the window's height, so the account card sits at the bottom.
            height: Math.max(implicitHeight, scroller.availableHeight)
            spacing: 22

            // ── Wordmark ──────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 18
                Layout.leftMargin: 20
                Layout.rightMargin: 14
                spacing: 8
                Text {
                    text: "forumer"
                    font.family: Ui.sans
                    font.pixelSize: 18
                    font.weight: Ui.weight.semibold
                    color: Ui.text
                }
                Text {
                    Layout.alignment: Qt.AlignBaseline
                    text: sidebar.store.appVersion.length > 0 ? "v" + sidebar.store.appVersion : ""
                    font.family: Ui.mono
                    font.pixelSize: 11
                    color: Ui.text3
                }
                Item { Layout.fillWidth: true }
            }

            // ── Search ────────────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                implicitHeight: 44
                radius: Ui.radius.l
                color: Ui.field
                border.width: 1
                border.color: searchInput.activeFocus ? Ui.focus : Ui.borderStrong

                Icon {
                    id: searchIcon
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: 17
                    color: Ui.text3
                }
                TextInput {
                    id: searchInput
                    anchors.left: searchIcon.right
                    anchors.leftMargin: 10
                    anchors.right: keys.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: sidebar.store.searchText
                    onTextEdited: {
                        sidebar.store.searchText = text;
                        if (sidebar.store.screen !== "home") sidebar.store.open("home");
                    }
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.body
                    color: Ui.text
                    selectionColor: Ui.borderStrong
                    clip: true
                    Keys.onEscapePressed: { sidebar.store.searchText = ""; focus = false; }
                    Accessible.name: "Search posts, domains and people"
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: searchInput.text.length === 0
                        text: "Search"
                        font: searchInput.font
                        color: Ui.text3
                    }
                }
                Row {
                    id: keys
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    visible: searchInput.text.length === 0
                    Repeater {
                        model: Qt.platform.os === "osx" ? ["⌘", "K"] : ["Ctrl", "K"]
                        delegate: Rectangle {
                            required property string modelData
                            width: Math.max(22, keyText.implicitWidth + 10)
                            height: 24
                            radius: Ui.radius.s
                            color: Ui.raised
                            border.width: 1
                            border.color: Ui.borderStrong
                            Text {
                                id: keyText
                                anchors.centerIn: parent
                                text: parent.modelData
                                font.family: Ui.mono
                                font.pixelSize: Ui.size.caption
                                color: Ui.text2
                            }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.IBeamCursor
                    onClicked: sidebar.focusSearch()
                    visible: !searchInput.activeFocus
                }
            }

            // ── Connection ────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 22
                Layout.rightMargin: 14
                spacing: 8
                Rectangle {
                    width: 7; height: 7; radius: 4
                    color: sidebar.store.connected ? Ui.live : Ui.sending
                }
                Text {
                    Layout.fillWidth: true
                    text: {
                        var s = sidebar.store.status.length > 0 ? sidebar.store.status : "starting…";
                        s = s === "PartiallyConnected" ? "connected" : s.charAt(0).toLowerCase() + s.slice(1);
                        var synced = sidebar.store.syncInfo.split(" · ")[0];
                        return synced.length > 0 ? s + " · " + synced : s;
                    }
                    elide: Text.ElideRight
                    font.family: Ui.mono
                    font.pixelSize: Ui.size.caption
                    color: Ui.text3
                }
            }

            // ── Navigation ────────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                spacing: 2
                NavItem {
                    Layout.fillWidth: true
                    text: "home"; icon: "home"
                    active: sidebar.store.screen === "home" || sidebar.store.screen === "thread"
                    onClicked: sidebar.store.open("home")
                }
                NavItem {
                    Layout.fillWidth: true
                    text: "replies"; icon: "bell"
                    badge: sidebar.store.unreadReplies > 0 ? String(sidebar.store.unreadReplies) : ""
                    badgeTone: "unread"
                    active: sidebar.store.screen === "replies"
                    onClicked: sidebar.store.open("replies")
                }
                NavItem {
                    Layout.fillWidth: true
                    text: "missed"; icon: "inbox"
                    badge: {
                        var n = sidebar.store.unsentCount + sidebar.store.missed.length;
                        return n > 0 ? String(n) : "";
                    }
                    active: sidebar.store.screen === "missed"
                    onClicked: sidebar.store.open("missed")
                }
                NavItem {
                    Layout.fillWidth: true
                    text: "my posts"; icon: "pen"
                    active: sidebar.store.screen === "mine"
                    onClicked: sidebar.store.open("mine")
                }
                NavItem {
                    Layout.fillWidth: true
                    text: "chats"; icon: "chat"; badge: "soon"
                    active: sidebar.store.screen === "chats"
                    onClicked: sidebar.store.open("chats")
                }
            }

            // ── Followed domains ──────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                spacing: 2
                Text {
                    Layout.leftMargin: 10
                    Layout.bottomMargin: 4
                    text: "followed domains"
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.ui
                    font.weight: Ui.weight.medium
                    color: Ui.text
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    visible: sidebar.store.followed.length === 0
                    text: "follow a #domain from home to keep it here."
                    wrapMode: Text.WordWrap
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.small
                    color: Ui.text3
                }
                Repeater {
                    model: sidebar.store.followed
                    delegate: NavItem {
                        required property string modelData
                        Layout.fillWidth: true
                        implicitHeight: 34
                        mono: true
                        text: "#" + modelData
                        active: sidebar.store.screen === "home" && sidebar.store.domainFilter === modelData
                        onClicked: { sidebar.store.domainFilter = modelData; sidebar.store.open("home"); }
                    }
                }
            }

            // ── You ───────────────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                spacing: 2
                Text {
                    Layout.leftMargin: 10
                    Layout.bottomMargin: 4
                    text: "you"
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.ui
                    font.weight: Ui.weight.medium
                    color: Ui.text
                }
                NavItem {
                    Layout.fillWidth: true
                    text: "profile & personas"; icon: "user"
                    active: sidebar.store.screen === "profile"
                    onClicked: sidebar.store.open("profile")
                }
                NavItem {
                    Layout.fillWidth: true
                    text: "settings"; icon: "settings"
                    active: sidebar.store.screen === "settings"
                    onClicked: sidebar.store.open("settings")
                }
                NavItem {
                    Layout.fillWidth: true
                    text: "backup & restore"; icon: "key"
                    active: sidebar.store.screen === "backup"
                    onClicked: sidebar.store.open("backup")
                }
            }

            Item { Layout.fillHeight: true; Layout.minimumHeight: 12 }

            // ── Account card ──────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.bottomMargin: 14
                implicitHeight: 56
                radius: Ui.radius.xl
                color: Ui.field
                border.width: 1
                border.color: Ui.borderStrong

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        radius: Ui.radius.m
                        color: Ui.raised
                        border.width: 1
                        border.color: Ui.borderStrong
                        Text {
                            anchors.centerIn: parent
                            text: sidebar.store.myLabel.length > 0 ? sidebar.store.myLabel.charAt(0).toUpperCase() : "?"
                            font.family: Ui.sans
                            font.pixelSize: Ui.size.ui
                            font.weight: Ui.weight.semibold
                            color: Ui.text
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: sidebar.store.myLabel
                            elide: Text.ElideRight
                            font.family: Ui.sans
                            font.pixelSize: Ui.size.ui
                            font.weight: Ui.weight.medium
                            color: Ui.text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: sidebar.store.myPersona
                            elide: Text.ElideRight
                            font.family: Ui.mono
                            font.pixelSize: 11
                            color: Ui.text3
                        }
                    }
                    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; Layout.topMargin: 6; Layout.bottomMargin: 6; color: Ui.borderStrong }
                    FButton {
                        implicitHeight: 32
                        kind: "ghost"
                        icon: Ui.dark ? "sun" : "moon"
                        tooltip: Ui.dark ? "Light theme" : "Dark theme"
                        onClicked: Ui.dark = !Ui.dark
                    }
                    FButton {
                        implicitHeight: 32
                        kind: "ghost"
                        icon: "lock"
                        tooltip: "Lock"
                        onClicked: sidebar.lockRequested()
                    }
                }
            }
        }
    }
}
