import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts

// Home: page title, "new topic", domain chips (filter / follow), and two
// columns side by side - every domain, and only the ones you follow. Narrow
// windows stack the columns.
Item {
    id: home

    required property var store
    signal newTopic()

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
                        text: "home"
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.page
                        font.weight: Ui.weight.bold
                        font.letterSpacing: -0.9
                        color: Ui.text
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "the public forum. sign as a persona, an alias, or no one at all."
                        wrapMode: Text.WordWrap
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.reading
                        color: Ui.text2
                    }
                }
                FButton {
                    Layout.alignment: Qt.AlignBottom
                    kind: "primary"
                    icon: "plus"
                    text: "new topic"
                    implicitHeight: 44
                    enabled: home.store.canPost
                    onClicked: home.newTopic()
                }
            }

            // ── Domain chips ──────────────────────────────────────────────────
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: 64
                Layout.rightMargin: 64
                spacing: 8
                visible: home.store.knownDomains.length > 0

                Chip {
                    text: "all"
                    selected: home.store.domainFilter.length === 0
                    onClicked: home.store.domainFilter = ""
                }
                Repeater {
                    model: home.store.knownDomains.slice(0, 12)
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
                    emptyText: home.store.searchText.length > 0 || home.store.domainFilter.length > 0
                               ? "nothing matches."
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
}
