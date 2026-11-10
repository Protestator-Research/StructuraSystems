pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Settings page "Libraries": the standard library (level 0, fixed), the local libraries of the opened folder with
// their levels (a change is stored at once in .structurasystems/workspace.json of the opened folder) and, for an online
// project, its project usages (stored on the server, in the commit of the project).
ColumnLayout {
    id: page

    readonly property var libraries: AppController.libraries

    /** The user wants to add a usage: the dialog is in Main.qml (there is one instance of it). */
    signal addUsageRequested()
    /** The user wants to remove a usage; Main.qml asks first. */
    signal removeUsageRequested(string usageId, string usedProjectName)

    readonly property var document: AppController.currentDocument
    readonly property bool online: document !== null && document.isOnline

    /** Scans the folder (in the background) and fills the lists. */
    function refresh() {
        libraries.refresh()
        AppController.refreshRemoteUsages()
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
                Accessible.name: entry.declared ? qsTr("Use the level from the imports") : qsTr("Do not use as library")
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

    // ---- project usages of the current online project (SysML v2 API: ProjectUsage, stored on the server) ----

    Rectangle {
        visible: page.online
        Layout.fillWidth: true
        Layout.topMargin: 6
        Layout.preferredHeight: 1
        color: Theme.outline
    }
    RowLayout {
        visible: page.online
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.weight: Font.Medium
            color: Theme.textPrimary
            text: qsTr("Project usages of %1").arg(page.document ? page.document.title : "")
        }
        BusyIndicator {
            running: AppController.remoteUsagesLoading
            visible: running
            implicitWidth: 24
            implicitHeight: 24
        }
    }
    Label {
        visible: page.online
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 12
        color: Theme.textSecondary
        text: qsTr("The projects this project uses, each pinned to a commit. They are stored on the server and are used for names that "
                   + "neither the standard library nor the local libraries define. A used project is below its user.")
    }

    ListView {
        id: usageList

        visible: page.online
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, 36), 130)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: AppController.remoteUsages
        ScrollBar.vertical: ScrollBar { }

        delegate: RowLayout {
            id: usage

            required property string usageId
            required property string projectName
            required property string commitId
            required property int level
            required property string status

            width: ListView.view.width
            spacing: 8

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: usageMenu.popup()
            }

            Menu {
                id: usageMenu

                MenuItem {
                    enabled: !AppController.busy
                    text: qsTr("Update to the latest commit")
                    onTriggered: AppController.updateRemoteUsage(usage.usageId)
                }
                MenuItem {
                    enabled: !AppController.busy
                    text: qsTr("Remove usage...")
                    onTriggered: page.removeUsageRequested(usage.usageId, usage.projectName)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.textPrimary
                    text: usage.projectName + " @ " + usage.commitId
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    color: usage.status.length > 0 ? Theme.warning : Theme.textSecondary
                    text: usage.status.length > 0 ? usage.status : qsTr("Level %1").arg(usage.level)
                }
            }
            ToolButton {
                text: "\u21BB"
                enabled: !AppController.busy
                Accessible.name: qsTr("Update to the latest commit")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Update to the latest commit")
                onClicked: AppController.updateRemoteUsage(usage.usageId)
            }
            IconToolButton {
                iconSource: Theme.iconTrash
                tip: qsTr("Remove usage")
                enabled: !AppController.busy
                Accessible.name: qsTr("Remove usage")
                onClicked: page.removeUsageRequested(usage.usageId, usage.projectName)
            }
        }
    }
    Label {
        visible: page.online && usageList.count === 0
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.textSecondary
        text: AppController.remoteUsagesLoading ? qsTr("Reading the project usages...")
              : qsTr("This project uses no other project.")
    }

    Button {
        visible: page.online
        text: qsTr("Add usage...")
        flat: true
        enabled: AppController.connected && !AppController.busy
        onClicked: page.addUsageRequested()
    }
}
