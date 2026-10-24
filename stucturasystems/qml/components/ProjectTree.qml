pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Tree of the local projects: folders of the working folder with the projects inside.
// Double-click or Enter opens the current project (or expands/collapses the current folder).
// projectActivated reports the row in the SOURCE model (AppController.localProjects).
// A delete button appears for the hovered project if it is open as a document (see isOpenFunction).
TreeView {
    id: tree

    /** Function(sourceRow) -> bool, tells if the project of a row is open as a document. Delete is only offered for open projects. */
    property var isOpenFunction: function (sourceRow) { return false }
    /** Switches the delete button and the Delete key on or off, e.g. while the backend is busy. */
    property bool deleteEnabled: true

    signal projectActivated(int sourceRow)
    /** Emitted if the user wants to delete a project. Reports the row in the SOURCE model and the full relative path. */
    signal deleteRequested(int sourceRow, string path)
    /** Emitted if the user wants to set the library level of a file by hand. Reports the path and the level it has now (-1 if it is no library). */
    signal levelRequested(string path, int level)

    readonly property bool allFilteredOut: rows === 0 && AppController.localProjects.count > 0

    /** Selects the first entry and moves the keyboard focus into the tree. */
    function focusFirst() {
        if (rows > 0) {
            selectionModel.setCurrentIndex(index(0, 0), ItemSelectionModel.ClearAndSelect)
            forceActiveFocus()
        }
    }

    // The reference to the document count makes the bindings of the delegates react on opened and closed documents.
    function _isOpen(sourceRow) {
        return AppController.documents.count >= 0 && sourceRow >= 0 && isOpenFunction(sourceRow)
    }

    function _activate(row) {
        const modelIndex = index(row, 0)
        if (tree.model.data(modelIndex, LocalProjectTreeModel.IsFolderRole))
            toggleExpanded(row)
        else
            projectActivated(tree.model.data(modelIndex, LocalProjectTreeModel.SourceRowRole))
    }

    function _deleteCurrent() {
        if (!selectionModel.currentIndex.valid || !deleteEnabled)
            return
        const sourceRow = tree.model.data(selectionModel.currentIndex, LocalProjectTreeModel.SourceRowRole)
        if (_isOpen(sourceRow))
            deleteRequested(sourceRow, tree.model.data(selectionModel.currentIndex, LocalProjectTreeModel.PathRole))
    }

    model: AppController.localProjectTree
    selectionModel: ItemSelectionModel { }
    clip: true
    activeFocusOnTab: true
    boundsBehavior: Flickable.StopAtBounds
    columnWidthProvider: () => tree.width
    ScrollBar.vertical: ScrollBar { }

    Keys.onDeletePressed: _deleteCurrent()
    Keys.onReturnPressed: if (selectionModel.currentIndex.valid) _activate(rowAtIndex(selectionModel.currentIndex))
    Keys.onEnterPressed: if (selectionModel.currentIndex.valid) _activate(rowAtIndex(selectionModel.currentIndex))
    // Up/Down and Left/Right (collapse/expand) come from the built-in key navigation of the TreeView.

    Component.onCompleted: expandRecursively()

    // The tree is rebuilt after every load and filter change; show all of it.
    Connections {
        target: tree.model

        function onModelReset() {
            // Deferred, so the TreeView has processed the reset itself first.
            Qt.callLater(tree.expandRecursively)
        }
    }

    // Context menu of a project: library settings. The level of the library comes from AppController.libraries.
    Menu {
        id: contextMenu

        property string path
        property int level: -1
        property bool manual: false
        property bool declared: false
        property var packages: []

        function openFor(filePath, libraryLevel) {
            path = filePath
            level = libraryLevel
            manual = AppController.libraries.hasOverride(filePath)
            declared = AppController.libraries.isDeclaredLibrary(filePath)
            packages = AppController.libraries.packagesOf(filePath)
            popup()
        }

        // The packages of a file are known when the index is built, which can happen after the menu was opened.
        Connections {
            target: AppController.libraries

            function onChanged() {
                if (contextMenu.opened) {
                    contextMenu.declared = AppController.libraries.isDeclaredLibrary(contextMenu.path)
                    contextMenu.packages = AppController.libraries.packagesOf(contextMenu.path)
                }
            }
        }

        MenuItem {
            // A file with a "library package" is a library already; only a manual setting can be taken back.
            enabled: contextMenu.manual || !contextMenu.declared
            text: contextMenu.manual ? qsTr("Do not use as library") : qsTr("Use as library")
            onTriggered: contextMenu.manual ? AppController.libraries.clearLibrary(contextMenu.path)
                                            : AppController.libraries.useAsLibrary(contextMenu.path)
        }
        MenuItem {
            text: qsTr("Set level...")
            onTriggered: tree.levelRequested(contextMenu.path, contextMenu.level)
        }
        MenuSeparator { visible: contextMenu.packages.length > 0 }

        // One entry per package of the file: use this file if several files define the package.
        Instantiator {
            model: contextMenu.packages
            delegate: MenuItem {
                id: packageItem

                required property string modelData

                text: qsTr("Preferred source for package %1").arg(packageItem.modelData)
                checkable: true
                checked: AppController.libraries.preferredSource(packageItem.modelData) === contextMenu.path
                onTriggered: AppController.libraries.setPreferredSource(packageItem.modelData, checked ? contextMenu.path : "")
            }
            onObjectAdded: (index, object) => contextMenu.insertItem(3 + index, object)
            onObjectRemoved: (index, object) => contextMenu.removeItem(object)
        }
    }

    delegate: TreeViewDelegate {
        id: node

        required property string name
        required property string description
        required property bool isFolder
        required property int sourceRow
        required property string path
        required property int libraryLevel

        implicitHeight: isFolder ? 36 : 52
        hoverEnabled: true
        indentation: 16
        highlighted: tree.selectionModel.currentIndex.valid && tree.rowAtIndex(tree.selectionModel.currentIndex) === node.row

        onClicked: {
            tree.selectionModel.setCurrentIndex(tree.index(node.row, 0), ItemSelectionModel.ClearAndSelect)
            tree.forceActiveFocus()
        }
        onDoubleClicked: tree._activate(node.row)

        TapHandler {
            acceptedButtons: Qt.RightButton
            enabled: !node.isFolder
            onTapped: {
                tree.selectionModel.setCurrentIndex(tree.index(node.row, 0), ItemSelectionModel.ClearAndSelect)
                contextMenu.openFor(node.path, node.libraryLevel)
            }
        }

        ToolTip.visible: hovered && !isFolder && description.length > 0
        ToolTip.text: description
        ToolTip.delay: 800

        contentItem: RowLayout {
            spacing: 12
            Icon {
                size: node.isFolder ? 20 : 24
                source: node.isFolder ? Theme.iconFolder : Theme.iconProject
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: node.name
                    elide: Text.ElideRight
                    font.weight: Font.Medium
                    color: Theme.textPrimary
                }
                Label {
                    Layout.fillWidth: true
                    visible: !node.isFolder
                    text: node.description.length > 0 ? node.description : qsTr("No description")
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: Theme.textSecondary
                }
            }
            Rectangle {
                // Shows that the file is a library and on which level.
                visible: node.libraryLevel >= 0
                implicitWidth: levelLabel.implicitWidth + 14
                implicitHeight: 20
                radius: height / 2
                color: Qt.alpha(Theme.accent, Theme.dark ? 0.22 : 0.14)
                border.width: 1
                border.color: Qt.alpha(Theme.accent, 0.45)

                Label {
                    id: levelLabel
                    anchors.centerIn: parent
                    text: qsTr("Library %1").arg(node.libraryLevel)
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    color: Theme.accent
                }
            }
            IconToolButton {
                // Only for projects that are open, so that nothing is deleted by accident.
                visible: !node.isFolder && tree.deleteEnabled && (node.hovered || hovered) && tree._isOpen(node.sourceRow)
                iconSource: Theme.iconTrash
                tip: qsTr("Delete project")
                onClicked: tree.deleteRequested(node.sourceRow, node.path)
            }
        }
    }
}
