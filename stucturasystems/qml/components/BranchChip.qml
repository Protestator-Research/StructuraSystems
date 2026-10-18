pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Chip in the top bar that shows the branch of the current online document. Opens a menu with all branches.
Button {
    id: chip

    property string branchName
    /** True while an older commit is shown instead of the head of the branch. */
    property bool detached: false

    signal branchSelected(string branchId, string branchName)

    implicitHeight: 32
    topPadding: 0
    bottomPadding: 0
    leftPadding: 10
    rightPadding: 8
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5
    text: branchName.length > 0 ? branchName : qsTr("Branch")
    Accessible.name: qsTr("Branch %1").arg(text)
    ToolTip.visible: hovered && !menu.visible
    ToolTip.text: qsTr("Switch branch")
    ToolTip.delay: 600

    onClicked: menu.popup(chip, 0, chip.height + 2)

    background: Rectangle {
        radius: height / 2
        color: chip.down || menu.visible ? Theme.indicator
                                         : chip.hovered ? Qt.alpha(Theme.indicator, 0.55) : "transparent"
        border.width: 1
        border.color: chip.detached ? Theme.warning : Theme.outline
    }

    contentItem: RowLayout {
        spacing: 6

        Icon {
            size: 16
            source: Theme.iconBranch
        }
        Label {
            Layout.maximumWidth: 160
            text: chip.text
            elide: Text.ElideRight
            font.weight: Font.Medium
            color: Theme.textPrimary
        }
        Icon {
            size: 12
            source: Theme.iconChevronDown
        }
    }

    Menu {
        id: menu

        MenuItem {
            visible: AppController.branches.count === 0
            height: visible ? implicitHeight : 0
            enabled: false
            text: AppController.historyLoading ? qsTr("Loading branches…") : qsTr("No branches found")
        }

        Repeater {
            model: AppController.branches

            MenuItem {
                id: entry

                required property string name
                required property string branchId
                required property bool isDefault
                required property bool isCurrent

                text: isDefault ? qsTr("%1  (default)").arg(name) : name
                checkable: true
                checked: isCurrent
                onTriggered: {
                    // The check mark follows the document, not the click.
                    checked = Qt.binding(() => entry.isCurrent)
                    if (!entry.isCurrent)
                        chip.branchSelected(entry.branchId, entry.name)
                }
            }
        }
    }
}
