pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Side panel "Digital Twins": the online projects that have digital twins on the server.
// The twins are loaded in the background after the project list, see AppController.twinsLoading.
Pane {
    id: pane

    readonly property bool available: AppController.connected && AppController.currentDocument !== null
                                      && AppController.currentDocument.isOnline
    readonly property int projectCount: AppController.digitalTwinProjects.count

    signal createRequested()
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
                text: qsTr("Digital twins")
                font.pixelSize: 18
                font.weight: Font.Medium
                color: Theme.textPrimary
            }
            IconToolButton {
                iconSource: Theme.iconRefresh
                tip: qsTr("Refresh digital twins")
                enabled: AppController.connected && !AppController.twinsLoading
                onClicked: AppController.refreshDigitalTwins()
            }
            IconToolButton {
                iconSource: Theme.iconAdd
                tip: pane.available ? qsTr("Create digital twin")
                                    : qsTr("Open an online project to create a digital twin from it.")
                enabled: pane.available && !AppController.busy
                onClicked: pane.createRequested()
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            indeterminate: true
            visible: AppController.twinsLoading
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list

                anchors.fill: parent
                visible: AppController.connected
                model: AppController.digitalTwinProjects
                clip: true
                currentIndex: -1
                keyNavigationEnabled: true
                activeFocusOnTab: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }

                Keys.onReturnPressed: if (currentIndex >= 0) AppController.openDigitalTwinProject(currentIndex)
                Keys.onEnterPressed: if (currentIndex >= 0) AppController.openDigitalTwinProject(currentIndex)

                delegate: ItemDelegate {
                    id: entry

                    required property int index
                    required property string name
                    required property string description
                    required property int twinCount
                    required property var twins

                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    hoverEnabled: true
                    onClicked: list.currentIndex = index
                    onDoubleClicked: AppController.openDigitalTwinProject(index)

                    ToolTip.visible: hovered && description.length > 0
                    ToolTip.text: description
                    ToolTip.delay: 800

                    contentItem: ColumnLayout {
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Icon { size: 24; source: Theme.iconOnlineProject }
                            Label {
                                Layout.fillWidth: true
                                text: entry.name
                                elide: Text.ElideRight
                                font.weight: Font.Medium
                                color: Theme.textPrimary
                            }
                            Label {
                                text: qsTr("%n twin(s)", "", entry.twinCount)
                                font.pixelSize: 12
                                color: Theme.textSecondary
                            }
                        }

                        Repeater {
                            model: entry.twins

                            RowLayout {
                                id: twin

                                required property var modelData

                                Layout.fillWidth: true
                                Layout.leftMargin: 36
                                spacing: 8

                                Icon { size: 16; source: Theme.iconTwin }
                                Label {
                                    Layout.fillWidth: true
                                    text: twin.modelData.name
                                    elide: Text.ElideRight
                                    color: Theme.textPrimary
                                }
                                Label {
                                    text: twin.modelData.commitId.length > 0
                                          ? qsTr("Commit %1").arg(twin.modelData.commitId.substring(0, 8)) : ""
                                    font.pixelSize: 12
                                    font.family: "monospace"
                                    color: Theme.textSecondary

                                    HoverHandler { id: commitHover }
                                    ToolTip.visible: commitHover.hovered && twin.modelData.commitId.length > 0
                                    ToolTip.text: twin.modelData.commitId
                                    ToolTip.delay: 500
                                }
                            }
                        }
                    }
                }
            }

            EmptyState {
                anchors.fill: parent
                visible: !AppController.connected
                iconSource: Theme.iconCloud
                iconSize: 56
                title: qsTr("Not connected")
                text: qsTr("Connect to your SysML v2 server to see the projects that have digital twins.")

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
                visible: AppController.connected && !AppController.twinsLoading && pane.projectCount === 0
                iconSource: Theme.iconTwin
                iconSize: 56
                title: qsTr("No digital twins yet")
                text: qsTr("None of the online projects has a digital twin. Open an online project and create a "
                           + "digital twin from its current commit.")

                Button {
                    text: qsTr("Create digital twin")
                    highlighted: true
                    enabled: pane.available && !AppController.busy
                    icon.source: Theme.iconTwin
                    icon.color: "transparent"
                    onClicked: pane.createRequested()
                }
                Label {
                    width: 240
                    visible: !pane.available
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: Theme.textSecondary
                    text: qsTr("Open an online project to create a digital twin from it.")
                }
            }

            Label {
                anchors.centerIn: parent
                visible: AppController.connected && AppController.twinsLoading && pane.projectCount === 0
                text: qsTr("Loading digital twins…")
                color: Theme.textSecondary
            }
        }
    }
}
