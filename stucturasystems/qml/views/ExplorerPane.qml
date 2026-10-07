import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Side panel "Explorer": projects of the local working folder.
Pane {
    id: pane

    signal openFolderRequested()
    signal openFilesRequested()
    signal newProjectRequested()

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
                text: qsTr("Explorer")
                font.pixelSize: 18
                font.weight: Font.Medium
                color: Theme.textPrimary
            }
            IconToolButton {
                iconSource: Theme.iconFolder
                tip: qsTr("Open folder (Ctrl+O)")
                onClicked: pane.openFolderRequested()
            }
            IconToolButton {
                iconSource: Theme.iconFiles
                tip: qsTr("Open files (Ctrl+Shift+O)")
                onClicked: pane.openFilesRequested()
            }
            IconToolButton {
                iconSource: Theme.iconNewProject
                tip: qsTr("New project (Ctrl+N)")
                onClicked: pane.newProjectRequested()
            }
        }

        TextField {
            id: search

            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 6
            Layout.bottomMargin: 6
            placeholderText: qsTr("Filter projects")
            leftPadding: 34
            visible: AppController.localProjects.count > 0
            Accessible.name: qsTr("Filter projects")
            selectByMouse: true
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
                anchors.fill: parent
                id: list
                sourceModel: AppController.localProjects
                iconSource: Theme.iconProject
                filterText: search.text
                onProjectActivated: row => AppController.openLocalProject(row)
            }

            Label {
                anchors.centerIn: parent
                visible: list.allFilteredOut
                text: qsTr("No matching projects")
                color: Theme.textSecondary
            }

            EmptyState {
                anchors.fill: parent
                visible: AppController.localProjects.count === 0
                iconSource: Theme.iconFolder
                iconSize: 56
                title: qsTr("No projects yet")
                text: qsTr("Open a folder with .md, .sysml or .kerml files, or create a new project.")

                Button {
                    text: qsTr("Open folder")
                    icon.source: Theme.iconFolder
                    icon.color: "transparent"
                    onClicked: pane.openFolderRequested()
                }
                Button {
                    text: qsTr("New project")
                    flat: true
                    icon.source: Theme.iconNewProject
                    icon.color: "transparent"
                    onClicked: pane.newProjectRequested()
                }
            }
        }
    }
}
