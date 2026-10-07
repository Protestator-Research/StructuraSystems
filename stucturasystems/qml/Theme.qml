pragma Singleton

import QtQuick
import QtQuick.Controls.Material
import StructuraSystems

// Single place for colours, metrics and icon locations of the whole UI.
QtObject {
    id: theme

    // ---------------------------------------------------------------- appearance
    readonly property int themeMode: AppController.settings.themeMode
    readonly property bool dark: themeMode === 2
                                 || (themeMode === 0 && Qt.styleHints.colorScheme === Qt.ColorScheme.Dark)
    readonly property int materialTheme: themeMode === 1 ? Material.Light
                                                         : themeMode === 2 ? Material.Dark : Material.System

    readonly property color primary: dark ? "#1d3c55" : "#1f6f9f"
    readonly property color accent: dark ? "#5cc2c9" : "#0e7c86"

    readonly property color surface: dark ? "#13181d" : "#f3f6f9"
    readonly property color card: dark ? "#1d242b" : "#ffffff"
    readonly property color rail: dark ? "#181e24" : "#e8eef4"
    readonly property color panel: dark ? "#171c21" : "#fafbfd"
    readonly property color outline: dark ? "#2f3a44" : "#d3dce5"
    readonly property color indicator: dark ? "#2c5a70" : "#c3e3ea"
    readonly property color primaryContainer: dark ? "#24506b" : "#cfe5f3"
    readonly property color onPrimaryContainer: dark ? "#d8edfb" : "#0b3550"
    readonly property color textPrimary: dark ? "#e6ebf0" : "#1b2128"
    readonly property color textSecondary: dark ? "#9aa7b3" : "#5a6672"
    readonly property color success: dark ? "#6fcf8f" : "#2e7d4f"
    readonly property color warning: dark ? "#f0b45a" : "#b26a00"
    readonly property color danger: dark ? "#ff8a80" : "#c62828"
    readonly property color info: dark ? "#7fb4e8" : "#1f6f9f"

    // ---------------------------------------------------------------- metrics
    readonly property int railWidth: 72
    readonly property int panelWidth: 280
    readonly property int cardMaxWidth: 960
    readonly property int iconSize: 22
    readonly property int smallIcon: 18
    readonly property int animationDuration: 200
    readonly property int cardRadius: Material.MediumScale

    readonly property string monoFamily: Qt.platform.os === "windows" ? "Consolas"
                                         : Qt.platform.os === "osx" ? "Menlo" : "monospace"

    readonly property var languages: ["Markdown", "YAML", "SysMLv2", "KerML"]

    function languageColor(language) {
        switch (language) {
        case "Markdown": return dark ? "#7fd39a" : "#2e7d4f"
        case "YAML": return dark ? "#f0b45a" : "#a15c00"
        case "SysMLv2": return dark ? "#7fb4e8" : "#1f6f9f"
        case "KerML": return dark ? "#c9a0f0" : "#7b2cbf"
        default: return textSecondary
        }
    }

    function isCode(language) {
        return language === "SysMLv2" || language === "KerML"
    }

    function severityColor(severity) {
        return severity >= 3 ? danger : severity === 2 ? warning : info
    }

    // ---------------------------------------------------------------- icons (resources/Resources.qrc)
    readonly property string _ui: "qrc:/icons/userinterface/icons/"
    readonly property string _cloud: "qrc:/icons/cloud_computing/icons/5183073-cloud-computing/png/"
    readonly property string _sci: "qrc:/icons/sience/icons/4847335-science-and-technology/png/"
    readonly property string _arrows: "qrc:/icons/arrows/icons/2874403-arrows/png/"

    readonly property url iconFolder: "qrc:/icons/userinterface/Open"
    readonly property url iconFiles: _ui + "3032829-user-interface/png/003-note pad.png"
    readonly property url iconNewProject: "qrc:/icons/userinterface/Add"
    readonly property url iconAdd: "qrc:/icons/userinterface/Add"
    readonly property url iconProject: _ui + "3032829-user-interface/png/003-note pad.png"
    readonly property url iconOnlineProject: _cloud + "004-folder.png"
    readonly property url iconCloud: _sci + "034-cloud.png"
    readonly property url iconTwin: _sci + "030-chip.png"
    readonly property url iconSettings: "qrc:/icons/userinterface/Settings"
    readonly property url iconSave: "qrc:/icons/userinterface/Save"
    readonly property url iconParse: "qrc:/icons/sience/Debug"
    readonly property url iconCommit: "qrc:/icons/userinterface/Commit"
    readonly property url iconUpload: _cloud + "019-upload.png"
    readonly property url iconTheme: _ui + "3032829-user-interface/png/049-idea.png"
    readonly property url iconConnection: "qrc:/icons/userinterface/Connection"
    readonly property url iconConnect: _cloud + "027-plug.png"
    readonly property url iconRefresh: _cloud + "017-reload.png"
    readonly property url iconSearch: _ui + "4622503-user-interface/png/047-search.png"
    readonly property url iconEdit: _sci + "024-notepad.png"
    readonly property url iconPreview: _ui + "3032829-user-interface/png/046-eye.png"
    readonly property url iconClose: "qrc:/icons/userinterface/Delete"
    readonly property url iconTrash: _cloud + "010-delete.png"
    // The alias names in the qrc are swapped: "UpGreen" shows a chevron pointing down and vice versa.
    readonly property url iconMoveUp: "qrc:/icons/arrows/DownGreen"
    readonly property url iconMoveDown: "qrc:/icons/arrows/UpGreen"
    readonly property url iconWarning: _ui + "3032829-user-interface/png/002-alert.png"
    readonly property url iconError: _ui + "3032829-user-interface/png/013-alert.png"
    readonly property url iconInfo: _cloud + "029-info.png"
    readonly property url iconCheck: _cloud + "031-check.png"
    readonly property url iconBack: "qrc:/icons/arrows/StepBack"
    readonly property url iconForward: "qrc:/icons/arrows/StepForward"
    readonly property url iconDocument: _cloud + "032-document.png"
    readonly property url iconUser: _ui + "3032829-user-interface/png/045-user.png"
    readonly property url iconPanelCollapse: "qrc:/icons/arrows/StepBack"

    function severityIcon(severity) {
        return severity >= 3 ? iconError : severity === 2 ? iconWarning : iconInfo
    }
}
