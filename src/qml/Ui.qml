pragma Singleton
import QtQuick

// Forumer's design tokens: colours, type, spacing, and the bundled fonts.
// One place to change the look; every view file reads from here (`Ui.text`,
// `Ui.space.l`, `Ui.sans`…).
//
// Dark is the default; `Ui.dark = false` switches every colour to the light
// palette at once (the sidebar's sun/moon toggle).
QtObject {
    id: ui

    property bool dark: true

    function pick(d, l) { return ui.dark ? d : l; }

    // ── Colours ───────────────────────────────────────────────────────────────
    readonly property color bg:          pick("#070707", "#ffffff")   // page
    readonly property color sidebar:     pick("#0b0b0b", "#fafafa")
    readonly property color surface:     pick("#0d0d0d", "#ffffff")   // panels, composer
    readonly property color field:       pick("#111111", "#f5f5f5")   // inputs, search
    readonly property color raised:      pick("#161616", "#f0f0f0")   // chips, pickers
    readonly property color active:      pick("#1c1c1c", "#ededed")   // selected nav row
    readonly property color hover:       pick("#141414", "#f4f4f4")
    readonly property color border:      pick("#1d1d1d", "#e5e5e5")   // section rules
    readonly property color borderSoft:  pick("#161616", "#efefef")   // row dividers
    readonly property color borderStrong:pick("#262626", "#d4d4d4")   // inputs, outline buttons
    readonly property color scrim:       pick("#b8000000", "#66000000")

    readonly property color text:        pick("#ededed", "#0a0a0a")
    readonly property color textBody:    pick("#d4d4d4", "#262626")   // long-form reading
    readonly property color text2:       pick("#a3a3a3", "#525252")   // secondary
    readonly property color text3:       pick("#8a8a8a", "#6b6b6b")   // captions, metadata

    readonly property color inverse:     pick("#ededed", "#0a0a0a")   // primary button fill
    readonly property color inverseText:   pick("#0a0a0a", "#fafafa")

    readonly property color live:        pick("#4ade80", "#15803d")
    readonly property color sending:     pick("#fbbf24", "#b45309")
    readonly property color failed:      pick("#f87171", "#b91c1c")
    readonly property color failedText:  pick("#fca5a5", "#b91c1c")
    readonly property color focus:       pick("#7c7c7c", "#6b6b6b")

    // ── Type ──────────────────────────────────────────────────────────────────
    // Geist for text, Geist Mono for machine details (fingerprints, times,
    // #domains, delivery states). Bundled under fonts/ (SIL OFL, fonts/OFL.txt).
    readonly property FontLoader sansRegular:  FontLoader { source: "fonts/Geist-Regular.ttf" }
    readonly property FontLoader sansMedium:   FontLoader { source: "fonts/Geist-Medium.ttf" }
    readonly property FontLoader sansSemiBold: FontLoader { source: "fonts/Geist-SemiBold.ttf" }
    readonly property FontLoader sansBold:     FontLoader { source: "fonts/Geist-Bold.ttf" }
    readonly property FontLoader monoRegular:  FontLoader { source: "fonts/GeistMono-Regular.ttf" }
    readonly property FontLoader monoMedium:   FontLoader { source: "fonts/GeistMono-Medium.ttf" }

    readonly property string sans: sansRegular.status === FontLoader.Ready ? sansRegular.font.family : "sans-serif"
    readonly property string mono: monoRegular.status === FontLoader.Ready ? monoRegular.font.family : "monospace"

    readonly property QtObject size: QtObject {
        readonly property int caption: 12
        readonly property int small: 13
        readonly property int ui: 14
        readonly property int body: 15
        readonly property int reading: 16
        readonly property int title: 17     // topic titles in lists
        readonly property int section: 20
        readonly property int sheet: 22
        readonly property int thread: 38    // a thread's title
        readonly property int page: 44      // "home", "missed"
    }

    readonly property QtObject weight: QtObject {
        readonly property int regular: Font.Normal
        readonly property int medium: Font.Medium
        readonly property int semibold: Font.DemiBold
        readonly property int bold: Font.Bold
    }

    // ── Spacing & shape ───────────────────────────────────────────────────────
    readonly property QtObject space: QtObject {
        readonly property int xs: 4
        readonly property int s: 8
        readonly property int m: 12
        readonly property int l: 16
        readonly property int xl: 24
        readonly property int xxl: 32
        readonly property int page: 56      // page padding
    }

    readonly property QtObject radius: QtObject {
        readonly property int s: 6
        readonly property int m: 8
        readonly property int l: 10
        readonly property int xl: 12
        readonly property int sheet: 16
        readonly property int pill: 999
    }

    readonly property int sidebarWidth: 280
    readonly property int readingWidth: 720
    readonly property int controlHeight: 40

    // ── Delivery states (our own posts) ───────────────────────────────────────
    function deliveryLabel(state) {
        if (state === "pending" || state === "propagated") return "sending…";
        if (state === "sent") return "live";
        if (state === "failed") return "failed · retrying";
        return "";
    }

    function deliveryColor(state) {
        if (state === "sent") return ui.live;
        if (state === "failed") return ui.failed;
        return ui.sending;
    }
}
