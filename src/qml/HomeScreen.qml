import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts

// Home: page title, "new topic", domain chips (filter / follow), and two
// columns side by side - every domain, and only the ones you follow. Narrow
// windows stack the columns. It shows the current forum: the public one, or
// a private one (then with its invite link and "leave").
Item {
    id: home

    required property var store
    signal newTopic()
    signal newForum()
    signal joinForum()

    B.ScrollView {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            id: page
            width: scroller.availableWidth
            spacing: 28

            // ── Header ────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Ui.space.page
                Layout.leftMargin: 64
                Layout.rightMargin: 64
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: home.store.inPrivateForum ? home.store.forumName(home.store.currentForum) : "home"
                        elide: Text.ElideRight
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.page
                        font.weight: Ui.weight.bold
                        font.letterSpacing: -0.9
                        color: Ui.text
                    }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: home.store.inPrivateForum
                              ? "a private forum. only people with its invite link can read it - and every one of them can share it on."
                              : "the public forum. sign as a persona, an alias, or no one at all."
                        wrapMode: Text.WordWrap
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.reading
                        color: Ui.text2
                    }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignBottom
                    spacing: 8
                    FButton {
                        kind: "outline"
                        icon: "lock"
                        text: "new private forum"
                        tooltip: "A forum only people with its invite link can read"
                        implicitHeight: 44
                        onClicked: home.newForum()
                    }
                    FButton {
                        kind: "outline"
                        icon: "link"
                        text: "join"
                        tooltip: "Join a private forum with an invite link"
                        implicitHeight: 44
                        onClicked: home.joinForum()
                    }
                    FButton {
                        kind: "primary"
                        icon: "plus"
                        text: "new topic"
                        implicitHeight: 44
                        enabled: home.store.canCompose
                        onClicked: home.newTopic()
                    }
                }
            }

            // A private forum: its invite link again, and leaving it.
            RowLayout {
                id: forumBar
                Layout.fillWidth: true
                Layout.leftMargin: 64
                Layout.rightMargin: 64
                Layout.topMargin: -12
                visible: home.store.inPrivateForum
                spacing: 12
                property bool copied: false
                property bool confirmLeave: false
                property string error: ""
                Connections {
                    target: home.store
                    function onCurrentForumChanged() { forumBar.confirmLeave = false; forumBar.error = ""; }
                }
                FButton {
                    compact: true
                    kind: "ghost"
                    icon: forumBar.copied ? "check" : "link"
                    text: forumBar.copied ? "invite link copied" : "copy invite link"
                    tooltip: "Whoever gets this link can read and post here"
                    onClicked: home.store.privateForumInvite(home.store.currentForum, function (r) {
                        clipboard.text = r.invite;
                        clipboard.selectAll();
                        clipboard.copy();
                        clipboard.text = "";
                        forumBar.copied = true;
                        inviteCopied.restart();
                    }, function (err) { forumBar.error = err; })
                    Timer { id: inviteCopied; interval: 1500; onTriggered: forumBar.copied = false }
                }
                FButton {
                    compact: true
                    kind: "ghost"
                    text: forumBar.confirmLeave ? "really leave? its posts go from this device" : "leave"
                    onClicked: {
                        if (!forumBar.confirmLeave) { forumBar.confirmLeave = true; return; }
                        forumBar.confirmLeave = false;
                        home.store.leavePrivateForum(home.store.currentForum, null,
                                                     function (err) { forumBar.error = err; });
                    }
                }
                FButton {
                    compact: true
                    kind: "ghost"
                    visible: forumBar.confirmLeave
                    text: "keep"
                    onClicked: forumBar.confirmLeave = false
                }
                Notice { Layout.fillWidth: true; message: forumBar.error }
                Item { Layout.fillWidth: true; visible: forumBar.error.length === 0 }
            }

            // A just-restored account: say why posting waits, and offer to go on.
            RestoreNotice {
                Layout.fillWidth: true
                Layout.leftMargin: 64
                Layout.rightMargin: 64
                store: home.store
            }

            // ── Domain chips ──────────────────────────────────────────────────
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: 64
                Layout.rightMargin: 64
                spacing: 8

                Chip {
                    text: "all"
                    selected: home.store.domainFilter.length === 0
                    onClicked: home.store.domainFilter = ""
                }
                Repeater {
                    // Domains in use, then the suggested ones nobody has used yet.
                    model: {
                        var out = home.store.knownDomains.slice(0, 12);
                        home.store.suggestedDomains.forEach(function (d) { if (out.indexOf(d) < 0) out.push(d); });
                        return out;
                    }
                    delegate: Chip {
                        required property string modelData
                        text: "#" + modelData
                        selected: home.store.domainFilter === modelData
                        onClicked: home.store.domainFilter = selected ? "" : modelData
                    }
                }
            }

            // Follow / unfollow the domain being filtered on.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 64
                Layout.rightMargin: 64
                Layout.topMargin: -12
                visible: home.store.domainFilter.length > 0 || home.store.searchText.length > 0
                spacing: 12
                Text {
                    textFormat: Text.PlainText
                    text: {
                        var parts = [];
                        if (home.store.domainFilter.length > 0) parts.push("#" + home.store.domainFilter);
                        if (home.store.searchText.length > 0) parts.push("“" + home.store.searchText + "”");
                        return "showing " + parts.join(" matching ");
                    }
                    font.family: Ui.mono
                    font.pixelSize: Ui.size.caption
                    color: Ui.text3
                }
                // A real button so it reads as one: filled "+ follow" when you
                // don't follow the domain yet, outlined "✓ following" when you do
                // (pressing it again unfollows).
                FButton {
                    readonly property bool following: home.store.isFollowed(home.store.domainFilter)
                    visible: home.store.domainFilter.length > 0
                    implicitHeight: 32
                    kind: following ? "outline" : "primary"
                    icon: following ? "check" : "plus"
                    text: (following ? "following #" : "follow #") + home.store.domainFilter
                    tooltip: following ? "Unfollow #" + home.store.domainFilter : ""
                    onClicked: home.store.toggleFollow(home.store.domainFilter)
                }
                FButton {
                    visible: home.store.domainFilter.length > 0
                    compact: true
                    kind: "ghost"
                    icon: copied ? "check" : "link"
                    text: copied ? "link copied" : "copy link"
                    tooltip: "A basecamp:// link that opens #" + home.store.domainFilter + " for whoever you share it with"
                    property bool copied: false
                    onClicked: {
                        clipboard.text = home.store.domainLink(home.store.domainFilter);
                        clipboard.selectAll();
                        clipboard.copy();
                        copied = true;
                        domainCopied.restart();
                    }
                    Timer { id: domainCopied; interval: 1500; onTriggered: parent.copied = false }
                }
                FButton {
                    compact: true
                    kind: "ghost"
                    text: "clear"
                    onClicked: { home.store.domainFilter = ""; home.store.searchText = ""; }
                }
                Item { Layout.fillWidth: true }
            }

            // ── Two feeds ─────────────────────────────────────────────────────
            GridLayout {
                id: feeds
                Layout.fillWidth: true
                Layout.leftMargin: 64
                Layout.rightMargin: 64
                Layout.bottomMargin: 64
                columns: page.width > 960 ? 2 : 1
                columnSpacing: 48
                rowSpacing: 40

                FeedColumn {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 1
                    title: "all domains"
                    caption: "newest first"
                    items: home.store.allFeed
                    emptyText: home.store.searchText.length > 0 ? "nothing matches."
                               : home.store.domainFilter.length > 0 ? "nothing in #" + home.store.domainFilter + " yet. start the first topic with “new topic”."
                               : home.store.inPrivateForum ? "no topics here yet - or they're still arriving from the other members. start one with “new topic”."
                               : "no topics yet. start one with “new topic”."
                    onOpenTopic: (id) => home.store.openTopic(id)
                }
                FeedColumn {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 1
                    title: "followed"
                    caption: home.store.followed.length > 0 ? "#" + home.store.followed.join(" #") : ""
                    items: home.store.followedFeed
                    emptyText: home.store.followed.length === 0
                               ? "pick a #domain above and choose “follow” to collect it here."
                               : "nothing new in the domains you follow."
                    onOpenTopic: (id) => home.store.openTopic(id)
                }
            }
        }
    }

    // Off-screen helper for "copy link" (QML has no clipboard API of its own).
    TextEdit { id: clipboard; visible: false; textFormat: TextEdit.PlainText }
}
