pragma Singleton
import QtQuick
import PS2Native.Backend

// Design tokens: deep black and PlayStation-era blue, cyan highlights.
QtObject {
    readonly property color bg: "#03050b"
    readonly property color bgRaised: "#060a14"
    readonly property color sidebar: "#050812"
    readonly property color surface: "#0a101f"
    readonly property color surfaceHigh: "#0f182c"
    readonly property color surfaceHover: "#142039"
    readonly property color surfaceSunken: "#060b16"
    readonly property color border: "#17223b"
    readonly property color borderStrong: "#24365c"
    readonly property color accent: "#2f6bff"
    readonly property color accentBright: "#5a8cff"
    readonly property color accentDeep: "#1638a8"
    readonly property color cyan: "#38d6ff"
    readonly property color text: "#e9efff"
    readonly property color textMuted: "#95a2c2"
    readonly property color textFaint: "#5f6c8d"
    readonly property color success: "#36d49b"
    readonly property color warning: "#f4b54d"
    readonly property color danger: "#ff5d73"
    readonly property color info: "#5aa9ff"

    readonly property int radiusSmall: 6
    readonly property int radius: 10
    readonly property int radiusLarge: 16
    readonly property int gap: 16
    readonly property int pagePadding: 32

    readonly property int captionSize: 12
    readonly property int bodySize: 14
    readonly property int subtitleSize: 16
    readonly property int titleSize: 20
    readonly property int headlineSize: 28

    readonly property bool reduceMotion: Launcher.reduceMotion
    readonly property int fast: reduceMotion ? 0 : 130
    readonly property int normal: reduceMotion ? 0 : 220

    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    function hex(c) { return c.toString().slice(1, 7) }

    function compatLabel(key) {
        switch (key) {
        case "pending": return qsTr("Identifying…")
        case "missing": return qsTr("File missing")
        case "invalid-image": return qsTr("Unreadable")
        case "unsupported-format": return qsTr("Unsupported format")
        case "not-ps2": return qsTr("Not a PS2 disc")
        case "runtime-unavailable": return qsTr("Runtime pending")
        case "boots": return qsTr("Boots")
        case "menus": return qsTr("Menus")
        case "in-game": return qsTr("In-game")
        case "playable": return qsTr("Playable")
        }
        return key
    }
    function compatColor(key) {
        switch (key) {
        case "pending": return textMuted
        case "missing":
        case "invalid-image": return danger
        case "unsupported-format":
        case "not-ps2": return warning
        case "runtime-unavailable": return info
        case "boots":
        case "menus": return warning
        case "in-game":
        case "playable": return success
        }
        return textMuted
    }
    function compatIcon(key) {
        switch (key) {
        case "pending": return "clock"
        case "missing": return "x-circle"
        case "invalid-image": return "alert"
        case "unsupported-format": return "layers"
        case "not-ps2": return "disc"
        case "runtime-unavailable": return "lock"
        }
        return "check-circle"
    }
    function compatDescription(key) {
        switch (key) {
        case "pending": return qsTr("The image is being read: volume descriptor and SYSTEM.CNF.")
        case "missing": return qsTr("The registered file is not accessible. Reconnect the drive or remove the entry.")
        case "invalid-image": return qsTr("The file could not be read as a PlayStation disc image.")
        case "unsupported-format": return qsTr("Compressed images cannot be read yet. Register the uncompressed ISO instead.")
        case "not-ps2": return qsTr("This is a PlayStation disc. Only PlayStation 2 software is in scope.")
        case "runtime-unavailable": return qsTr("Identified PlayStation 2 disc. Play attempts original BIOS boot and reports the first unsupported behavior. Gameplay compatibility is unverified.")
        }
        return ""
    }
    // Runtime capability texts come from the core in English; the catalog translates them.
    function capabilityName(capability) { return qsTranslate("Runtime", capability.name) }
    function capabilityReason(capability) { return qsTranslate("Runtime", capability.reason) }
    function statusColor(status) {
        switch (status) {
        case "valid": return success
        case "invalid":
        case "missing": return danger
        case "unsupported": return warning
        }
        return textMuted
    }
    function statusLabel(status) {
        switch (status) {
        case "valid": return qsTr("Verified")
        case "invalid": return qsTr("Invalid")
        case "missing": return qsTr("Missing")
        case "unsupported": return qsTr("Unsupported")
        case "pending": return qsTr("Checking…")
        }
        return status
    }
    function regionName(region) {
        switch (region) {
        case "NTSC-U": return qsTr("North America")
        case "PAL": return qsTr("Europe / PAL")
        case "NTSC-J": return qsTr("Japan / Asia")
        case "NTSC-K": return qsTr("Korea")
        case "NTSC-C": return qsTr("China")
        }
        return region
    }
}
