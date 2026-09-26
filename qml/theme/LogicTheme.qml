pragma Singleton
import QtQuick

QtObject {
    readonly property bool mobile: Qt.platform.os === "ios" || Qt.application.arguments.indexOf("--mobile-ui") >= 0
    readonly property int touchHeight: mobile ? 44 : 28
    // Follow macOS appearance when using Qt Quick Controls macOS/Fusion styles.
    readonly property bool dark: {
        try {
            if (typeof App !== "undefined" && App.storeScreenshotView && App.storeScreenshotView.length)
                return true
            return Qt.styleHints.colorScheme === Qt.ColorScheme.Dark
        } catch (e) {
            return false
        }
    }

    readonly property color windowBg: dark ? "#1C1C1E" : "#ECECEC"
    readonly property color panelBg: dark ? "#2C2C2E" : "#F6F6F6"
    readonly property color panelBgRaised: dark ? "#3A3A3C" : "#FFFFFF"
    readonly property color hairline: dark ? "#48484A" : "#D0D0D0"
    readonly property color textPrimary: dark ? "#F5F5F7" : "#1D1D1F"
    readonly property color textSecondary: dark ? "#A1A1A6" : "#6E6E73"
    readonly property color textMuted: dark ? "#636366" : "#8E8E93"
    readonly property color accent: dark ? "#5E9CE6" : "#007AFF"
    readonly property color accentDim: dark ? "#3A6EA5" : "#0056B3"
    readonly property color danger: "#FF453A"
    readonly property color success: dark ? "#30D158" : "#34C759"
    readonly property color warning: dark ? "#FFD60A" : "#FF9F0A"
    readonly property color muteRed: "#FF453A"
    readonly property color soloYellow: dark ? "#FFD60A" : "#FF9F0A"
    readonly property color faderTrack: dark ? "#1A1A1C" : "#D8D8DC"
    readonly property color faderFill: accent
    readonly property color keyWhite: "#E8E8ED"
    readonly property color keyBlack: "#1A1A1C"
    readonly property color zoneFill: dark ? "#5E9CE680" : "#007AFF66"
    readonly property color selectedBg: dark ? "#3A5A80" : "#CCDDF5"

    readonly property int toolbarHeight: mobile ? 56 : 40
    readonly property int statusHeight: 24
    readonly property int stripWidth: mobile ? 104 : 72
    readonly property int inspectorWidth: 280
    readonly property int radius: 6

    readonly property int fontSize: mobile ? 15 : 13
    readonly property int fontSizeSmall: mobile ? 13 : 11
    readonly property int fontSizeTitle: mobile ? 16 : 13
    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI" : ".AppleSystemUIFont"
}
