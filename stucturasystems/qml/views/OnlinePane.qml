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
                onProjectActivated: row => AppController.openOnlineProject(row)
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
}
