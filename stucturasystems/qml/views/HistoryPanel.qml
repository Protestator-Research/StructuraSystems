pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Right side panel: the commits of the current online project as a timeline. The commits of the current branch come
// first, the commits of other branches follow in their own section.
Pane {
    id: panel

    property string branchName
    readonly property int count: AppController.commitHistory.count

    signal checkoutRequested(string commitId)
    signal closeRequested()

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
                text: panel.branchName.length > 0 ? qsTr("History · %1").arg(panel.branchName) : qsTr("History")
                elide: Text.ElideRight
                font.pixelSize: 18
                font.weight: Font.Medium
                color: Theme.textPrimary
            }
            IconToolButton {
                iconSource: Theme.iconRefresh
                tip: qsTr("Refresh history")
                enabled: AppController.connected && !AppController.historyLoading
                onClicked: AppController.refreshHistory()
            }
            IconToolButton {
                iconSource: Theme.iconClose
                iconSize: 18
                tip: qsTr("Close history")
                onClicked: panel.closeRequested()
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            indeterminate: true
            visible: AppController.historyLoading
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list

                anchors.fill: parent
                model: AppController.commitHistory
                clip: true
                currentIndex: -1
                topMargin: 4
                bottomMargin: 12
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }

                section.property: "onBranch"
                section.criteria: ViewSection.FullString
                section.delegate: Item {
                    id: sectionItem

                    required property string section
                    readonly property bool onBranch: section === "true"
                    // Without commits of other branches there is only one section and it needs no heading.
                    readonly property bool needed: AppController.commitHistory.otherCount > 0

                    width: ListView.view.width
                    height: needed ? 40 : 0
                    visible: needed

                    Label {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 6
                        elide: Text.ElideRight
                        text: sectionItem.onBranch
                              ? (panel.branchName.length > 0 ? qsTr("On %1").arg(panel.branchName) : qsTr("On this branch"))
                              : qsTr("Other commits")
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.6
                        color: Theme.textSecondary
                    }
                }

                delegate: ItemDelegate {
                    id: entry

                    required property int index
                    required property string commitId
                    required property string shortId
                    required property string description
                    required property date created
                    required property bool isCurrent
                    required property bool isBranchHead
                    required property var headOf
                    required property bool onBranch

                    readonly property bool expanded: ListView.isCurrentItem
                    readonly property bool lineAbove: index > 0 && ListView.previousSection === ListView.section
                    readonly property bool lineBelow: index < ListView.view.count - 1 && ListView.nextSection === ListView.section
                    readonly property string title: description.length > 0 ? description : qsTr("(no description)")

                    width: ListView.view.width
                    padding: 0
                    leftPadding: 8
                    rightPadding: 12
                    hoverEnabled: true
                    highlighted: expanded
                    onClicked: list.currentIndex = expanded ? -1 : index
                    onDoubleClicked: if (!isCurrent && !AppController.busy) panel.checkoutRequested(commitId)

                    ToolTip.visible: hovered && !expanded
                    ToolTip.text: title
                    ToolTip.delay: 600

                    contentItem: RowLayout {
                        spacing: 4
                        opacity: entry.onBranch ? 1 : 0.65

                        // ---- timeline: line and dot
                        Item {
                            Layout.preferredWidth: 28
                            Layout.fillHeight: true

                            Rectangle {
                                x: Math.round((parent.width - width) / 2)
                                y: entry.lineAbove ? 0 : 20
                                width: 2
                                height: (entry.lineBelow ? parent.height : 20) - y
                                color: Theme.outline
                            }
                            Rectangle {
                                x: Math.round((parent.width - width) / 2)
                                y: 20 - height / 2
                                width: entry.isCurrent ? 14 : 12
                                height: width
                                radius: width / 2
                                color: entry.isCurrent ? Theme.accent : Theme.panel
                                border.width: entry.isCurrent ? 0 : 2
                                border.color: entry.onBranch ? Theme.accent : Theme.textSecondary

                                // Ring around the commit that is loaded in the editor.
                                Rectangle {
                                    anchors.centerIn: parent
                                    visible: entry.isCurrent
                                    width: parent.width + 8
                                    height: width
                                    radius: width / 2
                                    color: "transparent"
                                    border.width: 2
                                    border.color: Qt.alpha(Theme.accent, 0.4)
                                }
                            }
                        }

                        // ---- text
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                            spacing: 4

                            Label {
                                Layout.fillWidth: true
                                text: entry.title
                                elide: entry.expanded ? Text.ElideNone : Text.ElideRight
                                wrapMode: entry.expanded ? Text.Wrap : Text.NoWrap
                                font.weight: entry.isCurrent ? Font.DemiBold : Font.Normal
                                color: Theme.textPrimary
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Label {
                                    text: Theme.formatCommitDate(entry.created)
                                    visible: text.length > 0
                                    font.pixelSize: 12
                                    color: Theme.textSecondary
                                }
                                Label {
                                    text: entry.shortId
                                    font.pixelSize: 12
                                    font.family: Theme.monoFamily
                                    color: Theme.textSecondary
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                visible: entry.headOf.length > 0

                                Repeater {
                                    model: entry.headOf

                                    Rectangle {
                                        id: headChip

                                        required property string modelData

                                        width: chipRow.implicitWidth + 14
                                        height: 22
                                        radius: height / 2
                                        color: Qt.alpha(Theme.accent, Theme.dark ? 0.22 : 0.14)
                                        border.width: 1
                                        border.color: Qt.alpha(Theme.accent, 0.45)

                                        Row {
                                            id: chipRow
                                            anchors.centerIn: parent
                                            spacing: 4
                                            Icon {
                                                anchors.verticalCenter: parent.verticalCenter
                                                size: 12
                                                source: Theme.iconBranch
                                            }
                                            Label {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: headChip.modelData
                                                font.pixelSize: 11
                                                font.weight: Font.Medium
                                                color: Theme.accent
                                            }
                                        }
                                    }
                                }
                            }

                            // ---- details of the selected commit
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: entry.expanded
                                spacing: 6

                                Label {
                                    Layout.fillWidth: true
                                    text: entry.commitId
                                    elide: Text.ElideRight
                                    font.pixelSize: 11
                                    font.family: Theme.monoFamily
                                    color: Theme.textSecondary
                                }
                                Button {
                                    text: entry.isCurrent ? qsTr("Currently open") : qsTr("Open this commit")
                                    enabled: !entry.isCurrent && !AppController.busy
                                    highlighted: !entry.isCurrent
                                    flat: entry.isCurrent
                                    icon.source: Theme.iconHistory
                                    icon.color: "transparent"
                                    icon.width: 18
                                    icon.height: 18
                                    onClicked: panel.checkoutRequested(entry.commitId)
                                }
                            }
                        }
                    }
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: visible
                visible: AppController.historyLoading && panel.count === 0
            }

            EmptyState {
                anchors.fill: parent
                visible: !AppController.historyLoading && panel.count === 0
                iconSource: Theme.iconHistory
                iconSize: 56
                title: AppController.connected ? qsTr("No commits yet") : qsTr("Not connected")
                text: AppController.connected
                      ? qsTr("The project has no commits on the server. Commit your changes to start the history.")
                      : qsTr("Connect to the server to see the branches and commits of this project.")
            }
        }
    }
}
