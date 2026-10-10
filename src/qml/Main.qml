import QtQuick
import QtQuick.Layouts

// Forumer's view: the shell around every screen.
//
//   locked / no account  →  OnboardingScreen (unlock · create · restore)
//   fresh phrase         →  PhraseScreen (full screen until confirmed)
//   unlocked             →  Sidebar | current screen, plus the "new topic" sheet
//
// All data and backend calls live in ForumStore; the look lives in Ui (a
// singleton, see qmldir); each screen is its own file in this folder.
Item {
    id: root

    function log(msg) { console.log("[forumer qml] " + msg); }

    // Typed replica of the C++ backend - auto-synced properties and callable slots.
    readonly property var backend: logos.module("forumer")
    property bool ready: false

    Connections {
        target: logos
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "forumer")
                root.ready = isReady && root.backend !== null;
        }
    }

    ForumStore {
        id: store
        objectName: "forumStore"
        backend: root.backend
    }

    Binding { target: store; property: "viewReady"; value: root.ready }

    // Shared links: Basecamp hands Forumer the "forumer.open" intent declared
    // in metadata.json, from another app or from a basecamp:// link (0.3.1+).
    // The payload is checked again here (links.js): anyone can write a link.
    Connections {
        target: logos
        ignoreUnknownSignals: true
        function onIntentRequested(requestId, intent, params, requesterName) {
            if (intent !== "forumer.open") return;
            var link = store.linkFromParams(params);
            root.log("intent forumer.open from " + requesterName + (link ? " -> " + link.kind : " (not a valid link)"));
            if (link) store.openLink(link);
            if (typeof logos.respond === "function")
                logos.respond(requestId, link !== null, {}, link ? "" : "not a Forumer link");
        }
    }

    Component.onCompleted: {
        log("view created");
        root.ready = root.backend !== null && logos.isViewModuleReady("forumer");
    }

    // Shown over the unlocked app when restoring another account from backup.
    property bool restoreOverlay: false

    function lock() {
        store.call(store.backend.lockIdentity());
        onboarding.reset("unlock");
    }

    Rectangle { anchors.fill: parent; color: Ui.bg }

    // ── The app ───────────────────────────────────────────────────────────────
    Item {
        id: app
        anchors.fill: parent
        visible: store.unlocked

        Sidebar {
            id: sidebar
            width: Ui.sidebarWidth
            height: parent.height
            store: store
            onLockRequested: root.lock()
        }

        Item {
            id: main
            anchors.left: sidebar.right
            anchors.right: parent.right
            height: parent.height

            HomeScreen {
                anchors.fill: parent
                visible: store.screen === "home"
                store: store
                onNewTopic: composer.open()
            }
            ThreadScreen {
                anchors.fill: parent
                visible: store.screen === "thread"
                store: store
            }
            RepliesScreen {
                anchors.fill: parent
                visible: store.screen === "replies"
                store: store
            }
            MissedScreen {
                anchors.fill: parent
                visible: store.screen === "missed"
                store: store
            }
            MyPostsScreen {
                anchors.fill: parent
                visible: store.screen === "mine"
                store: store
            }
            ChatsScreen {
                anchors.fill: parent
                visible: store.screen === "chats"
            }
            ProfileScreen {
                anchors.fill: parent
                visible: store.screen === "profile"
                store: store
            }
            SettingsScreen {
                anchors.fill: parent
                visible: store.screen === "settings"
                store: store
                onLockRequested: root.lock()
            }
            BackupScreen {
                anchors.fill: parent
                visible: store.screen === "backup"
                store: store
                onRestoreRequested: { onboarding.reset("restore"); root.restoreOverlay = true; }
            }

            ReplyToast {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 24
                store: store
                z: 5
            }

            // Errors from actions that have no screen of their own.
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: 24
                visible: store.lastError.length > 0
                width: Math.min(parent.width - 48, errorText.implicitWidth + 64)
                height: errorText.implicitHeight + 24
                radius: Ui.radius.l
                color: Ui.surface
                border.width: 1
                border.color: Ui.failed
                Text {
                    textFormat: Text.PlainText
                    id: errorText
                    anchors.left: parent.left
                    anchors.right: closeError.left
                    anchors.margins: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: store.lastError
                    wrapMode: Text.WordWrap
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.ui
                    color: Ui.failedText
                }
                FButton {
                    id: closeError
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "ghost"
                    icon: "x"
                    implicitHeight: 28
                    tooltip: "Dismiss"
                    onClicked: store.lastError = ""
                }
            }
        }

        ComposerSheet {
            id: composer
            objectName: "composer"
            store: store
        }
    }

    // ── Onboarding / unlock ───────────────────────────────────────────────────
    OnboardingScreen {
        id: onboarding
        anchors.fill: parent
        store: store
        visible: (!store.unlocked || root.restoreOverlay) && store.pendingPhrase.length === 0
        canGoBack: root.restoreOverlay
        onBack: { root.restoreOverlay = false; onboarding.reset("unlock"); }
    }

    // ── Recovery phrase ───────────────────────────────────────────────────────
    PhraseScreen {
        anchors.fill: parent
        visible: store.pendingPhrase.length > 0
        phrase: store.pendingPhrase
        onDone: store.pendingPhrase = ""
    }

    // After locking or removing an account, land on the right onboarding card.
    Connections {
        target: store
        function onIdentityStateChanged() {
            root.log("identityState -> " + store.identityState);
            if (store.identityState === "none") onboarding.reset("create");
            else if (store.identityState === "locked" && !root.restoreOverlay) onboarding.reset("unlock");
            else if (store.identityState === "unlocked") {
                store.open("home");
                if (store.pendingLink) store.openLink(store.pendingLink);
            }
        }
    }

    // ⌘K / Ctrl+K: search. Ctrl+N: new topic. Esc closes the sheet.
    Shortcut {
        sequences: ["Ctrl+K", "Ctrl+F"]
        enabled: store.unlocked
        onActivated: sidebar.focusSearch()
    }
    Shortcut {
        sequence: "Ctrl+N"
        enabled: store.unlocked && store.canCompose
        onActivated: composer.open()
    }
    Shortcut {
        sequence: "Escape"
        enabled: composer.visible
        onActivated: composer.close()
    }
}
