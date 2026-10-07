pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// List of projects (name and description). Double-click or Enter opens the current entry.
ListView {
    id: list

    property url iconSource
    property string filterText

    signal projectActivated(int index)

    clip: true
    currentIndex: -1
    keyNavigationEnabled: true
    activeFocusOnTab: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { }

    readonly property bool allFilteredOut: count > 0 && contentHeight < 1

    Keys.onReturnPressed: if (currentIndex >= 0) projectActivated(currentIndex)
    Keys.onEnterPressed: if (currentIndex >= 0) projectActivated(currentIndex)

    delegate: ItemDelegate {
        id: entry

        required property int index
        required property string name
        required property string description
        readonly property bool matches: list.filterText.length === 0
                                        || name.toLowerCase().includes(list.filterText.toLowerCase())
                                        || description.toLowerCase().includes(list.filterText.toLowerCase())

        width: ListView.view.width
        height: matches ? 52 : 0
        visible: matches
        highlighted: ListView.isCurrentItem
        hoverEnabled: true
        onClicked: list.currentIndex = index
        onDoubleClicked: list.projectActivated(index)

        ToolTip.visible: hovered && description.length > 0
        ToolTip.text: description
        ToolTip.delay: 800

        contentItem: RowLayout {
            spacing: 12
            Icon { size: 24; source: list.iconSource }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: entry.name
                    elide: Text.ElideRight
                    font.weight: Font.Medium
                    color: Theme.textPrimary
                }
                Label {
                    Layout.fillWidth: true
                    text: entry.description.length > 0 ? entry.description : qsTr("No description")
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: Theme.textSecondary
                }
            }
        }
    }
}
