pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// List of projects (name and description). Double-click or Enter opens the current entry.
// The list shows a filtered view of sourceModel; projectActivated reports the row in the SOURCE model.
// A delete button appears for the hovered entry if its project is open as a document (see isOpenFunction).
ListView {
    id: list

    property ProjectListModel sourceModel
    property url iconSource
    property string filterText

    /** Function(sourceRow) -> bool, tells if the project of a row is open as a document. Delete is only offered for open projects. */
    property var isOpenFunction: function (sourceRow) { return false }
    /** Switches the delete button and the Delete key on or off, e.g. while the backend is busy. */
    property bool deleteEnabled: true

    signal projectActivated(int sourceRow)
    /** Emitted if the user wants to delete a project. Reports the row in the SOURCE model. */
    signal deleteRequested(int sourceRow, string name)

    /** Selects the first entry and moves the keyboard focus into the list. */
    function focusFirst() {
        if (count > 0) {
            currentIndex = 0
            forceActiveFocus()
        }
    }

    // The reference to the document count makes the bindings of the delegates react on opened and closed documents.
    function _isOpen(row) {
        return AppController.documents.count >= 0 && isOpenFunction(filter.sourceRow(row))
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

    Keys.onDeletePressed: {
        if (currentIndex < 0 || !deleteEnabled || !_isOpen(currentIndex))
            return
        const source = filter.sourceRow(currentIndex)
        if (source >= 0)
            deleteRequested(source, filter.data(filter.index(currentIndex, 0), Qt.DisplayRole))
    }
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
            IconToolButton {
                id: deleteButton

                // Only for projects that are open, so that nothing is deleted by accident.
                visible: list.deleteEnabled && (entry.hovered || hovered) && list._isOpen(entry.index)
                iconSource: Theme.iconTrash
                tip: qsTr("Delete project")
                onClicked: {
                    const source = filter.sourceRow(entry.index)
                    if (source >= 0)
                        list.deleteRequested(source, entry.name)
                }
            }
        }
    }
}
