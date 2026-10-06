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
    // "Night city": ink with a violet cast instead of neutral grey, one signal
    // colour (cyan) for what you act on, and hot magenta kept for one job only:
    // something new for you (replies, unread). Light mode is the same idea on
    // a cool paper.
    readonly property color bg:          pick("#08080e", "#f6f7fb")   // page
    readonly property color sidebar:     pick("#0b0a13", "#eef0f6")
    readonly property color surface:     pick("#0e0d18", "#ffffff")   // panels, composer
    readonly property color field:       pick("#12111e", "#ffffff")   // inputs, search
    readonly property color raised:      pick("#17162a", "#e8eaf3")   // chips, pickers
    readonly property color active:      pick("#1a1930", "#e3e6f1")   // selected nav row
    readonly property color hover:       pick("#141324", "#eceef6")
    readonly property color border:      pick("#1f1d33", "#dcdfeb")   // section rules
    readonly property color borderSoft:  pick("#171628", "#e8eaf2")   // row dividers
    readonly property color borderStrong:pick("#2c2a47", "#c7cbdb")   // inputs, outline buttons
    readonly property color scrim:       pick("#c405040a", "#660b0c14")

    readonly property color text:        pick("#e8eaf4", "#0b0c14")
    readonly property color textBody:    pick("#cdd0de", "#262838")   // long-form reading
    readonly property color text2:       pick("#a2a5bd", "#4c4f66")   // secondary
    readonly property color text3:       pick("#8487a1", "#62657d")   // captions, metadata

    // The signal colour: primary buttons, selected chips, focus, links.
    readonly property color accent:      pick("#2be4d3", "#00807a")
    readonly property color inverse:     accent                         // primary button fill
    readonly property color inverseText: pick("#05060b", "#ffffff")
    readonly property color link:        accent

    readonly property color live:        pick("#3cf0a0", "#0f7a4a")
    readonly property color sending:     pick("#ffc24b", "#a35a00")
    readonly property color failed:      pick("#ff5470", "#c0233f")
    readonly property color failedText:  pick("#ff8fa3", "#c0233f")
    readonly property color unread:      pick("#ff3d9a", "#c2186a")   // new replies to you
    readonly property color focus:       accent

    // ── Type ──────────────────────────────────────────────────────────────────
    // Chakra Petch for everything you read: squared-off, technical, still
    // comfortable at body size. Share Tech Mono for machine details
    // (fingerprints, times, #domains, delivery states). Both bundled under
    // fonts/ (SIL OFL 1.1, fonts/OFL-*.txt).
    readonly property FontLoader sansRegular:  FontLoader { source: "fonts/ChakraPetch-Regular.ttf" }
    readonly property FontLoader sansMedium:   FontLoader { source: "fonts/ChakraPetch-Medium.ttf" }
    readonly property FontLoader sansSemiBold: FontLoader { source: "fonts/ChakraPetch-SemiBold.ttf" }
    readonly property FontLoader sansBold:     FontLoader { source: "fonts/ChakraPetch-Bold.ttf" }
    readonly property FontLoader monoRegular:  FontLoader { source: "fonts/ShareTechMono-Regular.ttf" }

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
        // Squared, like a HUD: corners only soften the edge.
        readonly property int s: 2
        readonly property int m: 3
        readonly property int l: 4
        readonly property int xl: 4
        readonly property int sheet: 6
        readonly property int pill: 3
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
