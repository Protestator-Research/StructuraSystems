pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Settings page "Libraries": the standard library (level 0, fixed) and the local libraries of the opened folder with
// their levels. A change is stored at once in .structurasystems/workspace.json of the opened folder.
ColumnLayout {
    id: page

    readonly property var libraries: AppController.libraries

    /** Scans the folder (in the background) and fills the list. */
    function refresh() {
        libraries.refresh()
    }

    spacing: 6

    Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 12
        color: Theme.textSecondary
        text: qsTr("A document sees the libraries of the same or a lower level. The level of a local library follows from the "
                   + "libraries it imports, unless you set it here. The open document is always on top.")
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Label {
            text: qsTr("Level 0")
            font.weight: Font.Medium
            color: Theme.textPrimary
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Standard library (fixed)")
            color: Theme.textSecondary
        }
    }

    ListView {
        id: list

        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, 44), 150)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: page.libraries.entries
        ScrollBar.vertical: ScrollBar { }

        delegate: RowLayout {
            id: entry

            required property string file
            required property int level
            required property bool overridden
            required property bool declared
            required property bool inCycle
            required property string packages

            width: ListView.view.width
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: entry.file
                    elide: Text.ElideMiddle
                    color: Theme.textPrimary
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    color: entry.inCycle ? Theme.warning : Theme.textSecondary
                    text: (entry.packages.length > 0 ? entry.packages : qsTr("no package"))
                          + (entry.inCycle ? " - " + qsTr("import cycle") : "")
                          + (entry.overridden ? " - " + qsTr("level set manually") : "")
                }
            }
            SpinBox {
                id: levelBox

                from: 1
                to: 999
                editable: true
                value: entry.level
                // Only a change by the user is stored (not the update that follows from the scan), and only when the user
                // stopped changing the value, so that typing "12" does not store level 1 first. The controller keeps the
                // pending change, so a reset of the list does not lose it.
                onValueModified: page.libraries.setLevelLater(entry.file, levelBox.value)
            }
            ToolButton {
                visible: entry.overridden
                text: "↺"
                ToolTip.visible: hovered
                ToolTip.text: entry.declared ? qsTr("Use the level from the imports") : qsTr("Do not use as library")
                onClicked: page.libraries.clearLibrary(entry.file)
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: list.count === 0
        wrapMode: Text.Wrap
        color: Theme.textSecondary
        text: !page.libraries.hasFolder ? qsTr("Open a folder to see its libraries.")
              : page.libraries.scanning ? qsTr("Scanning the folder...")
              : qsTr("No library found. Files with a \"library package\" are libraries, other files can be made libraries in the "
                     + "context menu of the project tree.")
    }
}
