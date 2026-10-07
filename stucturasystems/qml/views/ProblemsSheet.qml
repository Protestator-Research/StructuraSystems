pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Collapsible bottom sheet that lists the problems found by the last parser run.
Pane {
    id: sheet

    property bool expanded: false
    readonly property int headerHeight: 36
    readonly property int bodyHeight: 190

    signal problemActivated(int row)

    padding: 0
    implicitHeight: headerHeight + (expanded ? bodyHeight : 0) + 1
    clip: true
    Material.background: Theme.panel

    Behavior on implicitHeight {
        NumberAnimation { duration: Theme.animationDuration; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.outline
    }

    // ---------------------------------------------------------------- header
    AbstractButton {
        id: header

        anchors.top: parent.top
        anchors.topMargin: 1
        anchors.left: parent.left
        anchors.right: parent.right
        height: sheet.headerHeight
        hoverEnabled: true
        onClicked: sheet.expanded = !sheet.expanded

        Accessible.name: qsTr("Problems")
        ToolTip.visible: hovered
        ToolTip.text: sheet.expanded ? qsTr("Collapse the problem list") : qsTr("Expand the problem list")
        ToolTip.delay: 700

        background: Rectangle {
            color: header.hovered ? Qt.alpha(Theme.textPrimary, 0.05) : "transparent"
        }
        contentItem: RowLayout {
            spacing: 8

            Item { Layout.preferredWidth: 6 }
            Icon {
                size: 18
                source: AppController.problems.count > 0 ? Theme.iconWarning : Theme.iconCheck
            }
            Label {
                text: qsTr("Problems (%1)").arg(AppController.problems.count)
                font.weight: Font.Medium
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }
            Icon {
                size: 18
                source: sheet.expanded ? Theme.iconMoveDown : Theme.iconMoveUp
            }
            Item { Layout.preferredWidth: 6 }
        }
    }

    // ---------------------------------------------------------------- list
    ListView {
        id: list

        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        model: AppController.problems
        opacity: sheet.expanded ? 1 : 0
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }
        Behavior on opacity { NumberAnimation { duration: Theme.animationDuration; easing.type: Easing.OutCubic } }

        delegate: ItemDelegate {
            id: problem

            required property int index
            required property string message
            required property int line
            required property int column
            required property string documentTitle
            required property int row
            required property int severity

            width: ListView.view.width
            height: 34
            hoverEnabled: true
            onClicked: sheet.problemActivated(problem.row)

            contentItem: RowLayout {
                spacing: 10
                Icon { size: 16; source: Theme.severityIcon(problem.severity) }
                Label {
                    Layout.fillWidth: true
                    text: problem.message
                    elide: Text.ElideRight
                    color: Theme.textPrimary
                }
                Label {
                    visible: problem.line >= 0
                    text: problem.column >= 0 ? qsTr("Line %1, column %2").arg(problem.line).arg(problem.column + 1)
                                              : qsTr("Line %1").arg(problem.line)
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
            }
        }

        Label {
            anchors.centerIn: parent
            visible: AppController.problems.count === 0
            text: qsTr("No problems found. Press F5 to check the document.")
            color: Theme.textSecondary
        }
    }
}
