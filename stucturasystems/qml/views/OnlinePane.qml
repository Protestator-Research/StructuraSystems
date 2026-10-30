import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Side panel "Online": connection to the backend and its projects.
Pane {
    id: pane

    signal newProjectRequested()
    signal settingsRequested()
    /** The user wants to add the online project of a row (row in AppController.onlineProjects) as a usage to the current document. */
    signal addUsageRequested(string projectId, string name)
    /** The user wants to add a usage to the current document and picks the project in the dialog. */
    signal addUsagePickerRequested()
    signal manageUsagesRequested()
    /** The user wants to remove a usage of the current document (asks first). */
    signal removeUsageRequested(string usageId, string usedProjectName)

    padding: 0
    Material.background: Theme.panel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 6
            Layout.topMargin: 6
            spacing: 0

            Label {
                Layout.fillWidth: true
                text: qsTr("Online")
                font.pixelSize: 18
                font.weight: Font.Medium
                color: Theme.textPrimary
            }
            IconToolButton {
                iconSource: Theme.iconRefresh
                tip: qsTr("Refresh projects")
                enabled: AppController.connected && !AppController.busy
                onClicked: AppController.refreshOnlineProjects()
            }
            IconToolButton {
                iconSource: Theme.iconNewProject
                tip: qsTr("New online project")
                enabled: AppController.connected && !AppController.busy
                onClicked: pane.newProjectRequested()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 6
            spacing: 8

            // Connection status chip
            Rectangle {
                implicitWidth: statusRow.implicitWidth + 20
                implicitHeight: 28
                radius: 14
                color: Qt.alpha(AppController.connected ? Theme.success : Theme.textSecondary, Theme.dark ? 0.2 : 0.12)
                border.width: 1
                border.color: Qt.alpha(AppController.connected ? Theme.success : Theme.textSecondary, 0.45)

                Row {
                    id: statusRow
                    anchors.centerIn: parent
                    spacing: 6
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        size: 16
                        source: Theme.iconConnection
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: AppController.connected ? qsTr("Connected") : qsTr("Disconnected")
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: AppController.connected ? Theme.success : Theme.textSecondary
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: AppController.connected ? qsTr("Disconnect") : qsTr("Connect")
                flat: AppController.connected
                highlighted: !AppController.connected
                enabled: !AppController.busy
                icon.source: Theme.iconConnect
                icon.color: "transparent"
                icon.width: 18
                icon.height: 18
                onClicked: AppController.connected ? AppController.disconnectFromBackend()
                                                   : AppController.connectToBackend()
            }
        }

        TextField {
            id: search

            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 6
            placeholderText: qsTr("Filter projects")
            leftPadding: 34
            visible: AppController.connected && AppController.onlineProjects.count > 0
            selectByMouse: true
            Accessible.name: qsTr("Filter projects")
            Keys.onDownPressed: list.focusFirst()
            Keys.onEscapePressed: text = ""
            onAccepted: list.focusFirst()

            Icon {
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                size: 16
                source: Theme.iconSearch
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ProjectList {
                id: list
                anchors.fill: parent
                visible: AppController.connected
                sourceModel: AppController.onlineProjects
                iconSource: Theme.iconOnlineProject
                filterText: search.text
                deleteEnabled: AppController.connected && !AppController.busy
                isOpenFunction: row => AppController.isOnlineProjectOpen(row)
                onProjectActivated: row => AppController.openOnlineProject(row)
                onDeleteRequested: (row, name) => deleteDialog.openFor(row, name)
                onContextMenuRequested: (row, projectId, name) => projectMenu.openFor(row, projectId, name)
            }

            Label {
                anchors.centerIn: parent
                visible: AppController.connected && list.allFilteredOut
                text: qsTr("No matching projects")
                color: Theme.textSecondary
            }

            EmptyState {
                anchors.fill: parent
                visible: !AppController.connected
                iconSource: Theme.iconCloud
                iconSize: 56
                title: qsTr("Not connected")
                text: qsTr("Connect to your SysML v2 server to browse and edit online projects. "
                           + "The server address and your credentials are set in the settings.")

                Button {
                    text: qsTr("Open settings")
                    flat: true
                    icon.source: Theme.iconSettings
                    icon.color: "transparent"
                    onClicked: pane.settingsRequested()
                }
            }

            EmptyState {
                anchors.fill: parent
                visible: AppController.connected && AppController.onlineProjects.count === 0
                iconSource: Theme.iconOnlineProject
                iconSize: 56
                title: qsTr("No online projects")
                text: qsTr("The server does not list any projects yet. Create one or refresh the list.")

                Button {
                    text: qsTr("New online project")
                    icon.source: Theme.iconNewProject
                    icon.color: "transparent"
                    onClicked: pane.newProjectRequested()
                }
            }
        }
    }

    // Context menu of an online project: open it, and the project usages of the current document. The project is kept by its id.
    Menu {
        id: projectMenu

        readonly property var states: AppController.UsageState

        property int row: -1
        property string projectId
        property string projectName
        property int usageState: AppController.UsageState.NoDocument
        property string usageId
        readonly property var document: AppController.currentDocument
        // The answer of the server is awaited: the entries that depend on it wait too.
        readonly property bool idle: !AppController.busy && !AppController.remoteUsagesLoading
        readonly property string reason: {
            switch (usageState) {
            case states.NoDocument: return qsTr("Open an online project first: the usage is added to it.")
            case states.NotOnline: return qsTr("The open document is a local project. Upload it first; usages belong to online projects.")
            case states.Detached: return qsTr("The document shows an older commit. Return to the branch head first.")
            case states.Offline: return qsTr("Connect to the server first.")
            case states.Invalid: return qsTr("The project is not in the list (any more). Refresh the list of projects.")
            default: return ""
            }
        }

        function openFor(sourceRow, id, name) {
            row = sourceRow
            projectId = id
            projectName = name
            recompute()
            AppController.refreshRemoteUsages()   // the answer arrives later and updates the entries (modelReset below)
            popup()
        }

        function recompute() {
            usageState = AppController.usageState(projectId)
            usageId = AppController.usageIdOf(projectId)
        }

        Connections {
            target: AppController.remoteUsages

            function onModelReset() {
                if (projectMenu.opened)
                    projectMenu.recompute()
            }
        }

        MenuItem {
            text: qsTr("Open")
            enabled: projectMenu.row >= 0
            onTriggered: AppController.openOnlineProject(projectMenu.row)
        }
        MenuSeparator { }

        // Another project than the one of the document: add it as a usage.
        MenuItem {
            visible: projectMenu.usageState !== projectMenu.states.Self && projectMenu.usageState !== projectMenu.states.Used
            height: visible ? implicitHeight : 0
            enabled: projectMenu.usageState === projectMenu.states.Ready && projectMenu.idle
            text: qsTr("Add as usage to \"%1\"...").arg(projectMenu.document ? projectMenu.document.title : "")
            onTriggered: pane.addUsageRequested(projectMenu.projectId, projectMenu.projectName)
        }
        MenuItem {
            // Why the entry above is not available.
            visible: projectMenu.reason.length > 0
            height: visible ? implicitHeight : 0
            enabled: false
            text: projectMenu.reason
        }

        // Already used: offer the update.
        MenuItem {
            visible: projectMenu.usageState === projectMenu.states.Used
            height: visible ? implicitHeight : 0
            enabled: projectMenu.idle
            text: qsTr("Update usage to the latest commit")
            onTriggered: AppController.updateRemoteUsage(projectMenu.usageId)
        }
        MenuItem {
            visible: projectMenu.usageState === projectMenu.states.Used
            height: visible ? implicitHeight : 0
            enabled: projectMenu.idle
            text: qsTr("Remove usage...")
            onTriggered: pane.removeUsageRequested(projectMenu.usageId, projectMenu.projectName)
        }

        // The project of the document itself.
        MenuItem {
            visible: projectMenu.usageState === projectMenu.states.Self
            height: visible ? implicitHeight : 0
            enabled: AppController.connected && projectMenu.idle
            text: qsTr("Add project usage...")
            onTriggered: pane.addUsagePickerRequested()
        }
        MenuItem {
            visible: projectMenu.usageState === projectMenu.states.Self || projectMenu.usageState === projectMenu.states.Used
            height: visible ? implicitHeight : 0
            text: qsTr("Manage project usages...")
            onTriggered: pane.manageUsagesRequested()
        }
    }

    ConfirmDeleteDialog {
        id: deleteDialog
        online: true
        onConfirmed: (row, name) => AppController.deleteOnlineProject(row, name)
    }
}
