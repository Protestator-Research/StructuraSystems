pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// List of projects (name and description). Double-click or Enter opens the current entry.
// The list shows a filtered view of sourceModel; projectActivated reports the row in the SOURCE model.
ListView {
    id: list

    property ProjectListModel sourceModel
    property url iconSource
    property string filterText

    signal projectActivated(int sourceRow)

    /** Selects the first entry and moves the keyboard focus into the list. */
    function focusFirst() {
        if (count > 0) {
            currentIndex = 0
            forceActiveFocus()
        }
    }

    function _activate(row) {
        const source = filter.sourceRow(row)
        if (source >= 0)
            projectActivated(source)
    }

    model: ProjectFilterModel {
        id: filter
        sourceModel: list.sourceModel
        filterText: list.filterText
    }

    clip: true
    currentIndex: -1
    keyNavigationEnabled: true
    activeFocusOnTab: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { }

    readonly property bool allFilteredOut: count === 0 && sourceModel !== null && sourceModel.count > 0

    Keys.onReturnPressed: if (currentIndex >= 0) _activate(currentIndex)
    Keys.onEnterPressed: if (currentIndex >= 0) _activate(currentIndex)

    delegate: ItemDelegate {
        id: entry

        required property int index
        required property string name
        required property string description

        width: ListView.view.width
        height: 52
        highlighted: ListView.isCurrentItem
        hoverEnabled: true
        onClicked: list.currentIndex = index
        onDoubleClicked: list._activate(index)

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
