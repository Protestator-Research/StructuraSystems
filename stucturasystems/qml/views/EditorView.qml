pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Scrollable list of element cards of one document plus the "New element" button.
Item {
    id: view

    property DocumentModel document
    /** No editing while a background job (commit, upload, parse) works on the same elements or an older commit is shown. */
    readonly property bool locked: AppController.busy || (document !== null && document.detached)
    readonly property int sideMargin: 24

    /** Whether the outline panel is shown. */
    property bool outlineOpen: false
    property int _pendingEditRow: -1
    property int _flashRow: -1
    property int _scrollRow: -1
    property int _scrollTries: 0
    property real _lastItemY: NaN
    property real _lastContentY: NaN

    /**
     * Scrolls to the card of the given row and highlights it for a moment. The cards have different heights that are
     * only known once their delegates exist, so the view is positioned again until the target stopped moving.
     */
    function scrollToRow(row) {
        if (!document || row < 0 || row >= document.count)
            return
        _scrollRow = row
        _scrollTries = 0
        _lastItemY = NaN
        _lastContentY = NaN
        _positionRow()
        scrollTimer.restart()
        _flashRow = row
        flashTimer.restart()
    }

    // Puts the card at the top of the view with a small gap.
    function _positionRow() {
        list.positionViewAtIndex(_scrollRow, ListView.Beginning)
        if (_scrollRow > 0)
            list.contentY -= 8
    }

    /** Inserts a new element and opens its card in edit mode. */
    function insertAndEdit(row, language) {
        if (!document || locked)
            return
        const before = document.count
        document.insertElement(row, language)
        if (document.count === before)
            return
        _pendingEditRow = row
        Qt.callLater(() => list.positionViewAtIndex(row, ListView.Contain))
    }

    Timer {
        id: scrollTimer
        interval: 40
        repeat: true
        onTriggered: {
            const item = list.itemAtIndex(view._scrollRow)
            const stable = !!item && item.y === view._lastItemY && list.contentY === view._lastContentY
            if (stable || view._scrollTries >= 15 || view._scrollRow >= list.count) {
                stop()
                view._scrollRow = -1
                // The flash is shown only once the card is in place.
                if (stable)
                    flashTimer.restart()
                return
            }
            view._scrollTries++
            view._lastItemY = item ? item.y : NaN
            view._positionRow()
            view._lastContentY = list.contentY
        }
    }

    Timer {
        id: flashTimer
        interval: 1600
        onTriggered: view._flashRow = -1
    }

    ListView {
        id: list

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: outlinePanel.visible ? outlinePanel.left : parent.right
        model: view.document
        clip: true
        topMargin: 8
        bottomMargin: 96
        boundsBehavior: Flickable.StopAtBounds
        // Keeps cards (and an open editor with unsaved text) alive while they are scrolled out of view.
        cacheBuffer: 4000
        ScrollBar.vertical: ScrollBar { }

        header: Item {
            width: list.width
            height: topDivider.implicitHeight

            InsertDivider {
                id: topDivider
                x: Math.round((parent.width - width) / 2)
                width: Math.min(parent.width - 2 * view.sideMargin, Theme.cardMaxWidth)
                locked: view.locked
                onInsertRequested: language => view.insertAndEdit(0, language)
            }
        }

        delegate: Item {
            id: delegate

            required property int index
            required property string language
            required property string body
            required property string headerTitle
            required property string headerAuthor
            required property int problemCount
            required property ElementTreeModel elementTree
            required property int elementCount
            required property var continuedPackages

            width: list.width
            height: card.implicitHeight + divider.implicitHeight

            ElementCard {
                id: card

                x: Math.round((parent.width - width) / 2)
                width: Math.min(parent.width - 2 * view.sideMargin, Theme.cardMaxWidth)
                row: delegate.index
                language: delegate.language
                body: delegate.body
                headerTitle: delegate.headerTitle
                headerAuthor: delegate.headerAuthor
                problemCount: delegate.problemCount
                elementTree: delegate.elementTree
                elementCount: delegate.elementCount
                continuedPackages: delegate.continuedPackages
                document: view.document
                tableOfContents: view.document ? view.document.tableOfContents : []
                first: delegate.index === 0
                last: delegate.index === list.count - 1
                locked: view.locked
                flash: view._flashRow === delegate.index

                onCommitRequested: (text, language) => {
                    if (!view.document || view.locked)
                        return
                    if (language !== delegate.language)
                        view.document.setLanguage(delegate.index, language)
                    if (text !== delegate.body)
                        view.document.setBody(delegate.index, text)
                }
                onMoveRequested: target => {
                    if (view.document && !view.locked)
                        view.document.moveRow(delegate.index, target)
                }
                onInsertRequested: (targetRow, language) => view.insertAndEdit(targetRow, language)
                onDeleteRequested: deleteDialog.openFor(delegate.index)
                onTocEntryActivated: row => view.scrollToRow(row)
                onPackageActivated: row => view.scrollToRow(row)
            }

            InsertDivider {
                id: divider
                anchors.top: card.bottom
                x: card.x
                width: card.width
                locked: view.locked
                onInsertRequested: language => view.insertAndEdit(delegate.index + 1, language)
            }

            Component.onCompleted: {
                if (view._pendingEditRow === delegate.index) {
                    view._pendingEditRow = -1
                    card.startEdit()
                }
            }
        }
    }

    // ---------------------------------------------------------------- outline
    OutlinePanel {
        id: outlinePanel

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Math.min(320, Math.round(view.width * 0.5))
        visible: view.outlineOpen && view.document !== null
        outline: view.document ? view.document.outline : null
        onNodeActivated: row => view.scrollToRow(row)
        onCloseRequested: {
            view.outlineOpen = false
            outlineButton.forceActiveFocus()
        }
    }

    Label {
        anchors.centerIn: list
        visible: view.document !== null && view.document.count === 0
        text: qsTr("This document is empty. Use \"New element\" to add content.")
        color: Theme.textSecondary
    }

    // ---------------------------------------------------------------- extended FAB
    // Opens or closes the outline panel; sits above the FAB.
    RoundButton {
        id: outlineButton

        anchors.right: fab.right
        anchors.rightMargin: 4
        anchors.bottom: fab.top
        anchors.bottomMargin: 12
        implicitWidth: 40
        implicitHeight: 40
        enabled: view.document !== null
        checkable: true
        checked: view.outlineOpen
        icon.source: Theme.iconDocument
        icon.width: 20
        icon.height: 20
        icon.color: "transparent"
        Material.elevation: 3
        Material.background: checked ? Theme.primaryContainer : Theme.card
        Accessible.name: view.outlineOpen ? qsTr("Hide the document outline") : qsTr("Show the document outline")
        ToolTip.visible: hovered
        ToolTip.text: view.outlineOpen ? qsTr("Hide the document outline") : qsTr("Show the document outline")
        ToolTip.delay: 700

        onClicked: {
            view.outlineOpen = !view.outlineOpen
            if (view.outlineOpen)
                Qt.callLater(outlinePanel.focusTree)
        }
    }

    Button {
        id: fab

        anchors.right: list.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        text: qsTr("New element")
        enabled: view.document !== null && !view.locked
        icon.source: Theme.iconAdd
        icon.width: 22
        icon.height: 22
        icon.color: "transparent"
        font.weight: Font.Medium
        leftPadding: 18
        rightPadding: 22
        implicitHeight: 48
        Material.elevation: 4
        Material.roundedScale: Material.LargeScale
        Material.background: Theme.primaryContainer
        Material.foreground: Theme.onPrimaryContainer
        Accessible.name: text
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Append a new element to the document")
        ToolTip.delay: 700

        onClicked: fabMenu.popup(fab, fab.width - fabMenu.implicitWidth, -fabMenu.implicitHeight - 4)

        LanguageMenu {
            id: fabMenu
            onLanguageSelected: language => {
                if (view.document)
                    view.insertAndEdit(view.document.count, language)
            }
        }
    }

    // ---------------------------------------------------------------- delete confirmation
    Dialog {
        id: deleteDialog

        property int row: -1

        function openFor(index) {
            row = index
            open()
        }

        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 400
        title: qsTr("Delete element?")
        footer: DialogFooter {
            Button {
                text: qsTr("Cancel")
                flat: true
                onClicked: deleteDialog.reject()
            }
            Button {
                text: qsTr("Delete")
                highlighted: true
                Material.accent: Theme.dangerFill
                icon.source: Theme.iconTrash
                icon.color: "transparent"
                icon.width: 18
                icon.height: 18
                onClicked: deleteDialog.accept()
            }
        }

        Label {
            text: qsTr("The element will be removed from the document. This cannot be undone.")
            wrapMode: Text.Wrap
            width: deleteDialog.availableWidth
        }
        onAccepted: {
            if (view.document && !view.locked)
                view.document.removeElement(row)
        }
    }
}
