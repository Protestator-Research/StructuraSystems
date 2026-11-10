pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import StructuraSystems

ApplicationWindow {
    id: root

    readonly property DocumentModel doc: AppController.currentDocument
    readonly property bool busy: AppController.busy
    readonly property bool canSave: doc !== null && doc.isLocalFile && !busy
    readonly property bool canParse: doc !== null && !busy
    readonly property bool canCommit: doc !== null && doc.isOnline && AppController.connected && !busy && !doc.detached
    readonly property bool isOnlineDoc: doc !== null && doc.isOnline
    readonly property bool canUpload: doc !== null && !doc.isOnline && AppController.connected && !busy

    /** 0 explorer, 1 online, 2 digital twins */
    property int destination: 0
    property bool panelOpen: true
    /** Whether the user wants the history panel; it is only shown for online documents. */
    property bool historyOpen: false
    readonly property bool historyVisible: historyOpen && isOnlineDoc
    /** Narrow windows start without side panel; it can still be opened by hand. */
    readonly property bool compact: width < 1100
    property bool _restored: false

    /** Open diagram editor windows (one per document). */
    property var _diagramWindows: []

    /**
     * Opens the graphical editor of the current document in its own window (or raises it).
     * @param elementId block to show and edit; empty or undefined = the document's generated block
     */
    function openDiagramEditor(elementId) {
        if (doc === null)
            return
        const blockId = elementId === undefined ? "" : elementId
        for (const window of _diagramWindows) {
            if (window.document === doc && window.blockElementId === blockId) {
                window.raise()
                window.requestActivate()
                return
            }
        }
        const window = diagramWindowComponent.createObject(null, { document: doc, blockElementId: blockId })
        _diagramWindows = _diagramWindows.concat([window])
        window.finished.connect(() => {
            _diagramWindows = _diagramWindows.filter(other => other !== window)
            Qt.callLater(() => window.destroy())
        })
        window.show()
    }

    function closeDiagramWindows() {
        for (const window of _diagramWindows.slice())
            window.close()
    }

    /** Width of the side panel, adjustable by dragging its right edge. */
    property real panelWidth: Theme.panelWidth
    readonly property real panelMinWidth: 200
    readonly property real panelMaxWidth: Math.max(panelMinWidth, Math.min(600, width * 0.45))
    readonly property real effectivePanelWidth: clampPanelWidth(panelWidth)
    property bool panelResizing: false

    // Split editor: root.doc (the current document) is always the document of the active side.
    /** The document on the inactive side, null while the editor is not split. */
    property DocumentModel otherDoc: null
    /** 0 left, 1 right */
    property int activePane: 0
    readonly property bool splitOpen: otherDoc !== null
    // The documents shown on both sides. They are set by _syncPanes() only once a change is complete, so a side never
    // shows an intermediate document (that would rebuild its editor and drop the focus of the clicked card).
    property DocumentModel leftDoc: null
    property DocumentModel rightDoc: null
    property bool forceClose: false

    function clampPanelWidth(w) {
        return Math.max(panelMinWidth, Math.min(panelMaxWidth, w))
    }

    function _syncPanes() {
        leftDoc = activePane === 0 ? doc : otherDoc
        rightDoc = activePane === 1 ? doc : otherDoc
    }

    function closeSplit() {
        otherDoc = null
        activePane = 0
        _syncPanes()
    }

    // The other document may have been closed (and deleted) or became the current one.
    function validateSplit() {
        if (otherDoc === null)
            return
        if (otherDoc === doc || AppController.documents.indexOfDocument(otherDoc) < 0)
            closeSplit()
    }

    function activatePane(pane) {
        if (!splitOpen || pane === activePane)
            return
        const newActive = otherDoc
        const newIndex = AppController.documents.indexOfDocument(newActive)
        if (newIndex < 0) {
            closeSplit()
            return
        }
        otherDoc = doc
        activePane = pane
        AppController.currentIndex = newIndex
    }

    // Shows the tab on the other side of the editor; the current document stays where it is.
    function openToSide(index) {
        const target = AppController.documents.at(index)
        if (!target || target === doc)
            return
        if (splitOpen) {
            otherDoc = target
            _syncPanes()
        } else {
            otherDoc = doc
            activePane = 1
            AppController.currentIndex = index
        }
    }

    // Makes the tab the current document; the tab of the inactive side switches the side.
    function showTab(index) {
        const target = AppController.documents.at(index)
        if (!target || target === doc)
            return
        if (splitOpen && target === otherDoc)
            activatePane(1 - activePane)
        else
            AppController.currentIndex = index
    }

    function toggleSplit() {
        if (splitOpen) {
            closeSplit()
            return
        }
        const count = AppController.documents.count
        if (count < 2 || doc === null)
            return
        openToSide((AppController.currentIndex + 1) % count)
    }

    /**
     * Shows the document on the given side (0 left, 1 right) and makes that side the active one, like dropping a tab
     * there. The document that was on that side moves to the other side if the dropped one came from there.
     */
    function placeDocument(document, side) {
        const index = AppController.documents.indexOfDocument(document)
        if (index < 0)
            return
        const sideDoc = side === 0 ? leftDoc : rightDoc
        const opposite = side === 0 ? rightDoc : leftDoc
        if (sideDoc === document) {
            activatePane(side)
            return
        }
        let keep
        if (splitOpen)
            keep = opposite === document ? sideDoc : opposite
        else if (document !== doc)
            keep = doc
        else {
            // The only visible document moves to a side; the other side shows the next tab.
            const count = AppController.documents.count
            if (count < 2)
                return
            keep = AppController.documents.at((index + 1) % count)
        }
        if (!keep || keep === document)
            return
        otherDoc = keep
        activePane = side
        if (document === doc)
            _syncPanes()
        else
            AppController.currentIndex = index  // the change of the current document updates both sides
    }

    // ---- dragging a tab onto a side of the editor
    /** The document of the tab being dragged, null while no tab is dragged. */
    property DocumentModel draggedDoc: null
    property string draggedTitle: ""

    function beginTabDrag(document, title, scenePos) {
        draggedDoc = document
        draggedTitle = title
        moveTabDrag(scenePos)
    }

    function moveTabDrag(scenePos) {
        tabDragGhost.x = scenePos.x - tabDragGhost.width / 2
        tabDragGhost.y = scenePos.y - tabDragGhost.height / 2
    }

    function endTabDrag() {
        if (draggedDoc !== null)
            tabDragGhost.Drag.drop()
        draggedDoc = null
        draggedTitle = ""
    }

    function requestClose(index, title, modified) {
        if (index < 0 || busy)
            return
        if (modified)
            closeDialog.openFor(index, title)
        else
            AppController.closeDocument(index)
    }

    // The content of the document is replaced by branch switch and checkout, so unsaved changes need a confirmation.
    function requestSwitchBranch(branchId, branchName) {
        if (doc && doc.modified)
            discardDialog.openFor(qsTr("Discard changes and switch?"),
                                  qsTr("\"%1\" has uncommitted changes. They will be lost if you switch to branch %2.")
                                      .arg(doc.title).arg(branchName),
                                  qsTr("Discard and switch"), () => AppController.switchBranch(branchId))
        else
            AppController.switchBranch(branchId)
    }

    function requestCheckout(commitId) {
        if (doc && doc.modified)
            discardDialog.openFor(qsTr("Discard changes and open commit?"),
                                  qsTr("\"%1\" has uncommitted changes. They will be lost if you open the commit %2.")
                                      .arg(doc.title).arg(commitId.substring(0, 8)),
                                  qsTr("Discard and open"), () => AppController.checkoutCommit(commitId))
        else
            AppController.checkoutCommit(commitId)
    }

    function openFolder() {
        folderDialog.open()
    }

    function openFiles() {
        fileDialog.open()
    }

    function newProject(preferOnline) {
        newProjectDialog.openNew(preferOnline)
    }

    function saveCurrent() {
        if (canSave)
            AppController.saveCurrent()
    }

    function parseCurrent() {
        if (canParse)
            AppController.parseCurrent()
    }

    function toggleTheme() {
        AppController.settings.themeMode = Theme.dark ? 1 : 2
        AppController.settings.save()
    }

    function confirmRemoveUsage(usageId, usedProjectName) {
        removeUsageDialog.usageDocument = root.doc
        removeUsageDialog.openForUsage(usageId, usedProjectName, root.doc ? root.doc.title : "")
    }

    function selectDestination(index) {
        if (destination === index && panelOpen) {
            panelOpen = false
        } else {
            destination = index
            panelOpen = true
        }
    }

    function _onScreen(px, py) {
        for (const screen of Qt.application.screens) {
            if (px >= screen.virtualX && px < screen.virtualX + screen.width
                    && py >= screen.virtualY && py < screen.virtualY + screen.height)
                return true
        }
        return false
    }

    function _restoreState() {
        width = Math.max(minimumWidth, windowState.width)
        height = Math.max(minimumHeight, windowState.height)
        if (windowState.hasPosition && _onScreen(windowState.x + 48, windowState.y + 48)) {
            x = windowState.x
            y = windowState.y
        }
        destination = Math.max(0, Math.min(2, windowState.destination))
        problemsSheet.bodyHeight = Math.max(problemsSheet.minBodyHeight,
                                            Math.min(problemsSheet.maxBodyHeight, windowState.problemsHeight))
        panelOpen = windowState.panelOpen && !compact
        historyOpen = windowState.historyOpen
        panelWidth = clampPanelWidth(windowState.panelWidth)
        if (windowState.maximized)
            visibility = Window.Maximized
        _restored = true
    }

    // Only the geometry of the normal (not maximized) window is remembered.
    function _storeGeometry() {
        if (!_restored || visibility !== Window.Windowed)
            return
        windowState.x = x
        windowState.y = y
        windowState.width = width
        windowState.height = height
        windowState.hasPosition = true
    }

    onXChanged: _storeGeometry()
    onYChanged: _storeGeometry()
    onWidthChanged: _storeGeometry()
    onHeightChanged: _storeGeometry()
    onVisibilityChanged: visibility => {
        if (_restored && (visibility === Window.Windowed || visibility === Window.Maximized))
            windowState.maximized = visibility === Window.Maximized
    }
    onCompactChanged: {
        if (_restored)
            panelOpen = !compact
    }
    // A new current document is shown on the active side (tab click, opened or closed document, side switch).
    onDocChanged: _syncPanes()
    onPanelOpenChanged: if (_restored) windowState.panelOpen = panelOpen
    onHistoryOpenChanged: if (_restored) windowState.historyOpen = historyOpen
    onDestinationChanged: if (_restored) windowState.destination = destination

    onClosing: close => {
        if (!forceClose && AppController.hasUnsavedChanges()) {
            close.accepted = false
            quitDialog.openDialog()
        } else {
            root.closeDiagramWindows()
        }
    }

    Component.onCompleted: _restoreState()

    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: doc ? qsTr("%1 - Structura Systems").arg(doc.title) : qsTr("Structura Systems")

    Material.theme: Theme.materialTheme
    Material.primary: Theme.primary
    Material.accent: Theme.accent
    Material.background: Theme.surface

    Settings {
        id: windowState

        category: "Window"
        property int x: 0
        property int y: 0
        property int width: 1280
        property int height: 800
        property bool hasPosition: false
        property bool maximized: false
        property bool panelOpen: true
        property bool historyOpen: false
        property int destination: 0
        property int problemsHeight: 190
        property int panelWidth: Theme.panelWidth
    }

    Binding {
        target: windowState
        property: "problemsHeight"
        value: problemsSheet.bodyHeight
        when: root._restored
        restoreMode: Binding.RestoreNone
    }

    Connections {
        target: Qt.application

        function onAboutToQuit() {
            windowState.sync()
        }
    }

    // ---------------------------------------------------------------- shortcuts
    Shortcut { sequences: ["Ctrl+O"]; onActivated: root.openFolder() }
    Shortcut { sequences: ["Ctrl+Shift+O"]; onActivated: root.openFiles() }
    Shortcut { sequences: ["Ctrl+N"]; onActivated: root.newProject(false) }
    Shortcut { sequences: ["Ctrl+S"]; onActivated: root.saveCurrent() }
    Shortcut { sequences: ["F5"]; onActivated: root.parseCurrent() }
    Shortcut { sequences: ["Ctrl+Shift+D"]; onActivated: root.openDiagramEditor() }
    Shortcut {
        sequences: ["Ctrl+W"]
        enabled: root.doc !== null
        onActivated: root.requestClose(AppController.currentIndex, root.doc.title, root.doc.modified)
    }
    Shortcut { sequences: ["Ctrl+,"]; onActivated: settingsDialog.open() }
    Shortcut {
        sequences: ["Ctrl+\\"]
        enabled: root.splitOpen || AppController.documents.count > 1
        onActivated: root.toggleSplit()
    }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: root.close() }

    // ---------------------------------------------------------------- reactions to the view models
    Connections {
        target: AppController

        function onNotify(level, message, details) {
            snackbar.show(level, message, details)
        }
    }

    // The document of the other side may disappear or become the current one.
    Connections {
        target: AppController.documents

        function onCountChanged() {
            root.validateSplit()
        }
    }

    Connections {
        target: AppController

        function onCurrentDocumentChanged() {
            root.validateSplit()
        }
    }

    // A parser run with findings opens the problem list.
    Connections {
        target: root.doc

        function onProblemsChanged() {
            Qt.callLater(() => {
                if (AppController.problems.count > 0)
                    problemsSheet.expanded = true
            })
        }
    }

    // ---------------------------------------------------------------- layout
    RowLayout {
        anchors.fill: parent
        spacing: 0

        NavigationRail {
            Layout.fillHeight: true
            currentIndex: root.destination
            panelOpen: root.panelOpen
            onDestinationClicked: index => root.selectDestination(index)
            onAboutClicked: aboutDialog.open()
            onSettingsClicked: settingsDialog.open()
        }

        // Collapsible side panel. The content keeps its width while the container animates.
        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: root.panelOpen ? root.effectivePanelWidth : 0
            clip: true
            Behavior on Layout.preferredWidth {
                enabled: !root.panelResizing
                NumberAnimation { duration: Theme.animationDuration; easing.type: Easing.OutCubic }
            }

            StackLayout {
                width: root.effectivePanelWidth
                height: parent.height
                currentIndex: root.destination

                ExplorerPane {
                    onOpenFolderRequested: root.openFolder()
                    onOpenFilesRequested: root.openFiles()
                    onNewProjectRequested: root.newProject(false)
                }
                OnlinePane {
                    onNewProjectRequested: root.newProject(true)
                    onSettingsRequested: settingsDialog.open()
                    onAddUsageRequested: (projectId, name) => addUsageDialog.openFor(projectId, name)
                    onAddUsagePickerRequested: addUsageDialog.openPicker()
                    onManageUsagesRequested: settingsDialog.open()
                    onRemoveUsageRequested: (usageId, name) => root.confirmRemoveUsage(usageId, name)
                }
                DigitalTwinPane {
                    onCreateRequested: wizard.openWizard()
                    onSettingsRequested: settingsDialog.open()
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: panelHandle.containsMouse || root.panelResizing ? Theme.accent : Theme.outline
            }

            // Drag handle on the right edge to resize the panel; double click restores the default width.
            MouseArea {
                id: panelHandle

                property real startX: 0
                property real startWidth: 0

                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 6
                visible: root.panelOpen
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                preventStealing: true

                onPressed: mouse => {
                    startX = mapToItem(null, mouse.x, 0).x
                    startWidth = root.effectivePanelWidth
                    root.panelResizing = true
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    root.panelWidth = root.clampPanelWidth(startWidth + mapToItem(null, mouse.x, 0).x - startX)
                }
                onReleased: {
                    root.panelResizing = false
                    windowState.panelWidth = Math.round(root.panelWidth)
                }
                onCanceled: root.panelResizing = false
                onDoubleClicked: {
                    root.panelWidth = Theme.panelWidth
                    windowState.panelWidth = Theme.panelWidth
                }
            }
        }

        // ------------------------------------------------------------ main area
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Top bar: tabs and document actions
            Pane {
                Layout.fillWidth: true
                padding: 0
                implicitHeight: 48
                Material.background: Theme.card

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 0

                    IconToolButton {
                        iconSource: root.panelOpen ? Theme.iconBack : Theme.iconForward
                        tip: root.panelOpen ? qsTr("Hide side panel") : qsTr("Show side panel")
                        onClicked: root.panelOpen = !root.panelOpen
                    }

                    TabBar {
                        id: tabs

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: AppController.currentIndex
                        Material.background: "transparent"
                        background: null
                        onCurrentIndexChanged: {
                            if (currentIndex === AppController.currentIndex)
                                return
                            // The tab of the inactive side switches the side instead of showing the document twice.
                            if (root.splitOpen && currentIndex >= 0
                                    && currentIndex === AppController.documents.indexOfDocument(root.otherDoc))
                                root.activatePane(1 - root.activePane)
                            else
                                AppController.currentIndex = currentIndex
                        }

                        Repeater {
                            model: AppController.documents

                            TabButton {
                                id: tab

                                required property int index
                                required property string title
                                required property bool modified
                                required property DocumentModel document

                                width: Math.min(Math.max(implicitWidth, 120), 240)
                                padding: 6
                                leftPadding: 12

                                // The tab shown on the inactive side of the split editor.
                                readonly property bool onOtherSide: root.splitOpen && tab.document === root.otherDoc

                                // The menu gets the index of its tab; the current document is not changed.
                                function openMenu() {
                                    tabMenu.tabIndex = tab.index
                                    tabMenu.tabOnline = tab.document !== null && tab.document.isOnline
                                    tabMenu.popup()
                                }

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 2
                                    visible: tab.onOtherSide
                                    color: Theme.outline
                                }

                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: tab.openMenu()
                                }
                                // Dragging a tab onto a side of the editor shows it there (see the DropArea of the editor).
                                DragHandler {
                                    target: null
                                    acceptedButtons: Qt.LeftButton
                                    dragThreshold: 10
                                    grabPermissions: PointerHandler.CanTakeOverFromAnything
                                    enabled: !root.busy
                                    onActiveChanged: {
                                        if (active)
                                            root.beginTabDrag(tab.document, tab.title, centroid.scenePosition)
                                        else
                                            root.endTabDrag()
                                    }
                                    onCentroidChanged: {
                                        if (active)
                                            root.moveTabDrag(centroid.scenePosition)
                                    }
                                }
                                // The Menu key and Shift+F10 open the menu of the focused tab.
                                Keys.onPressed: event => {
                                    if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                        tab.openMenu()
                                        event.accepted = true
                                    }
                                }

                                contentItem: RowLayout {
                                    spacing: 6
                                    Label {
                                        Layout.fillWidth: true
                                        text: tab.title
                                        elide: Text.ElideRight
                                        font.weight: tab.checked ? Font.DemiBold : Font.Normal
                                        color: tab.checked ? Theme.textPrimary : Theme.textSecondary
                                    }
                                    Label {
                                        visible: tab.modified
                                        text: "●"
                                        font.pixelSize: 10
                                        color: Theme.accent
                                        ToolTip.visible: modifiedHover.hovered
                                        ToolTip.text: qsTr("Unsaved changes")
                                        HoverHandler { id: modifiedHover }
                                    }
                                    IconToolButton {
                                        iconSource: Theme.iconClose
                                        iconSize: 16
                                        implicitWidth: 28
                                        implicitHeight: 28
                                        padding: 0
                                        enabled: !root.busy
                                        tip: qsTr("Close (Ctrl+W)")
                                        onClicked: root.requestClose(tab.index, tab.title, tab.modified)
                                    }
                                }
                            }
                        }
                    }

                    IconToolButton {
                        iconSource: Theme.iconSplit
                        checked: root.splitOpen
                        enabled: root.splitOpen || AppController.documents.count > 1
                        tip: root.splitOpen ? qsTr("Close split view") : qsTr("Split editor (Ctrl+\\)")
                        onClicked: root.toggleSplit()
                    }
                    BranchChip {
                        visible: root.isOnlineDoc
                        enabled: !root.busy && AppController.connected
                        branchName: root.doc ? root.doc.branchName : ""
                        detached: root.doc !== null && root.doc.detached
                        onBranchSelected: (branchId, name) => root.requestSwitchBranch(branchId, name)
                        onNewBranchRequested: newBranchDialog.openFor("")
                    }
                    IconToolButton {
                        visible: root.isOnlineDoc
                        iconSource: Theme.iconHistory
                        checked: root.historyOpen
                        tip: root.historyOpen ? qsTr("Hide history") : qsTr("Show history")
                        onClicked: root.historyOpen = !root.historyOpen
                    }
                    Rectangle {
                        visible: root.isOnlineDoc
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 24
                        Layout.leftMargin: 4
                        Layout.rightMargin: 4
                        color: Theme.outline
                    }
                    IconToolButton {
                        iconSource: Theme.iconSave
                        tip: qsTr("Save (Ctrl+S)")
                        enabled: root.canSave
                        onClicked: root.saveCurrent()
                    }
                    IconToolButton {
                        iconSource: Theme.iconParse
                        tip: qsTr("Parse and check (F5)")
                        enabled: root.canParse
                        onClicked: root.parseCurrent()
                    }
                    IconToolButton {
                        iconSource: Theme.iconDiagram
                        tip: qsTr("Diagram editor (Ctrl+Shift+D)")
                        enabled: root.doc !== null
                        onClicked: root.openDiagramEditor()
                    }
                    IconToolButton {
                        iconSource: Theme.iconCommit
                        tip: qsTr("Commit to server")
                        enabled: root.canCommit
                        onClicked: commitDialog.open()
                    }
                    IconToolButton {
                        iconSource: Theme.iconUpload
                        tip: qsTr("Upload as new online project")
                        enabled: root.canUpload
                        onClicked: AppController.uploadCurrent()
                    }
                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 24
                        Layout.leftMargin: 4
                        Layout.rightMargin: 4
                        color: Theme.outline
                    }
                    IconToolButton {
                        iconSource: Theme.iconTheme
                        tip: Theme.dark ? qsTr("Switch to light theme") : qsTr("Switch to dark theme")
                        onClicked: root.toggleTheme()
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.outline
                }
            }

            // Editor or empty state
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
            // Editor or empty state, history panel on the right
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                EditorView {
                    id: editor
                    anchors.fill: parent
                    visible: root.doc !== null
                    document: root.doc
                    onDiagramEditorRequested: elementId => root.openDiagramEditor(elementId)
                }
                        SplitView {
                            id: splitView

                            anchors.fill: parent
                            orientation: Qt.Horizontal
                            visible: root.doc !== null

                            handle: Rectangle {
                                implicitWidth: 1
                                color: SplitHandle.pressed || SplitHandle.hovered ? Theme.accent : Theme.outline
                            }

                            EditorPane {
                                id: leftPane

                                SplitView.fillWidth: true
                                SplitView.minimumWidth: 320
                                document: root.leftDoc
                                active: root.activePane === 0
                                split: root.splitOpen
                                onActivationRequested: root.activatePane(0)
                                onReturnRequested: AppController.returnToBranchHead()
                                onNewBranchRequested: newBranchDialog.openFor(document ? document.commitId : "")
                            }

                            EditorPane {
                                id: rightPane

                                SplitView.preferredWidth: splitView.width / 2
                                SplitView.minimumWidth: 320
                                visible: root.splitOpen
                                document: root.rightDoc
                                active: root.activePane === 1
                                split: root.splitOpen
                                onActivationRequested: root.activatePane(1)
                                onReturnRequested: AppController.returnToBranchHead()
                                onNewBranchRequested: newBranchDialog.openFor(document ? document.commitId : "")
                            }
                        }

                        // Drop target for dragged tabs: the side under the pointer is highlighted and gets the document.
                        DropArea {
                            id: tabDropArea

                            /** 0 left, 1 right */
                            property int side: 0
                            readonly property real splitX: root.splitOpen ? leftPane.width : width / 2

                            anchors.fill: parent
                            enabled: root.doc !== null
                            keys: ["structura/tab"]
                            onEntered: drag => side = drag.x < splitX ? 0 : 1
                            onPositionChanged: drag => side = drag.x < splitX ? 0 : 1
                            onDropped: drop => {
                                const side = drop.x < splitX ? 0 : 1
                                drop.accept()
                                // Changing the documents while the drop is delivered could delete the drag source.
                                const document = root.draggedDoc
                                Qt.callLater(() => root.placeDocument(document, side))
                            }

                            Rectangle {
                                visible: tabDropArea.containsDrag
                                x: tabDropArea.side === 0 ? 0 : tabDropArea.splitX
                                y: 0
                                width: tabDropArea.side === 0 ? tabDropArea.splitX : tabDropArea.width - tabDropArea.splitX
                                height: tabDropArea.height
                                color: Qt.alpha(Theme.accent, 0.12)
                                border.width: 2
                                border.color: Theme.accent
                            }
                        }

                EmptyState {
                    anchors.fill: parent
                    visible: root.doc === null
                    iconSource: Theme.iconProject
                    iconSize: 72
                    title: qsTr("No project open")
                    text: qsTr("Open a folder or file to start modeling, or create a new project. "
                               + "Double-click a project in the explorer to open it.")

                    Button {
                        text: qsTr("Open folder")
                        highlighted: true
                        icon.source: Theme.iconFolder
                        icon.color: "transparent"
                        onClicked: root.openFolder()
                    }
                    Button {
                        text: qsTr("New project")
                        flat: true
                        icon.source: Theme.iconNewProject
                        icon.color: "transparent"
                        onClicked: root.newProject(false)
                    }
                }

                // Collapsible history panel. The content keeps its width while the container animates.
                Item {
                    Layout.fillHeight: true
                    Layout.preferredWidth: root.historyVisible ? Theme.historyWidth : 0
                    clip: true
                    Behavior on Layout.preferredWidth {
                        NumberAnimation { duration: Theme.animationDuration; easing.type: Easing.OutCubic }
                    }

                    HistoryPanel {
                        width: Theme.historyWidth
                        height: parent.height
                        branchName: root.doc ? root.doc.branchName : ""
                        onCheckoutRequested: commitId => root.requestCheckout(commitId)
                        onNewBranchRequested: commitId => newBranchDialog.openFor(commitId)
                        onCloseRequested: root.historyOpen = false
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 1
                        color: Theme.outline
                    }
                }
            }

            ProblemsSheet {
                id: problemsSheet
                Layout.fillWidth: true
                visible: root.doc !== null
                onProblemActivated: row => (root.activePane === 0 ? leftPane : rightPane).scrollToRow(row)
            }
        }
    }

    footer: StatusBar {
        elementCount: root.doc ? root.doc.count : -1
    }

    // Follows the pointer while a tab is dragged; it carries the drag to the DropArea of the editor.
    Rectangle {
        id: tabDragGhost

        parent: Overlay.overlay
        z: 1000
        visible: root.draggedDoc !== null
        width: Math.min(Math.max(ghostLabel.implicitWidth + 24, 120), 240)
        height: 32
        radius: 4
        color: Theme.card
        border.width: 1
        border.color: Theme.accent
        opacity: 0.9

        Drag.active: root.draggedDoc !== null
        Drag.keys: ["structura/tab"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2

        Label {
            id: ghostLabel
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            verticalAlignment: Text.AlignVCenter
            text: root.draggedTitle
            elide: Text.ElideRight
            color: Theme.textPrimary
        }
    }

    Snackbar {
        id: snackbar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        z: 100
        onDetailsRequested: details => {
            detailsDialog.details = details
            detailsDialog.open()
        }
    }

    // ---------------------------------------------------------------- dialogs
    FolderDialog {
        id: folderDialog
        title: qsTr("Open folder")
        currentFolder: AppController.settings.workingDirectoryUrl
        onAccepted: AppController.openFolder(selectedFolder)
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open files")
        fileMode: FileDialog.OpenFiles
        currentFolder: AppController.settings.workingDirectoryUrl
        nameFilters: [qsTr("Project files (*.md *.kerml *.sysml *.xml *.json)"), qsTr("All files (*)")]
        onAccepted: AppController.openFiles(selectedFiles)
    }

    SettingsDialog {
        id: settingsDialog
        onAddUsageRequested: addUsageDialog.openPicker()
        onRemoveUsageRequested: (usageId, name) => root.confirmRemoveUsage(usageId, name)
    }
    // The one dialog for adding a project usage, and the one that asks before one is removed (used by the project list, the tab
    // menu and the settings).
    AddUsageDialog { id: addUsageDialog }
    ConfirmDeleteDialog {
        id: removeUsageDialog
        // The document the confirmation was asked for; the usage belongs to it, not to whatever tab is shown now.
        property var usageDocument: null
        onConfirmed: (row, usageId) => {
            if (usageDocument !== root.doc) {
                AppController.notify(2, qsTr("The project usage was not removed."),
                                     qsTr("The current document changed while the confirmation was open."))
                return
            }
            AppController.removeRemoteUsage(usageId)
        }
    }

    // Context menu of a tab: split view and, for online documents, their project usages.
    Menu {
        id: tabMenu

        /** Index of the tab the menu was opened for. */
        property int tabIndex: -1
        property bool tabOnline: false
        readonly property bool usable: AppController.connected

        MenuItem {
            enabled: tabMenu.tabIndex >= 0 && AppController.documents.at(tabMenu.tabIndex) !== root.doc
            text: root.splitOpen ? qsTr("Open on the other side") : qsTr("Open to the side")
            onTriggered: root.openToSide(tabMenu.tabIndex)
        }
        MenuItem {
            enabled: root.splitOpen
            text: qsTr("Close split view")
            onTriggered: root.closeSplit()
        }
        MenuSeparator { visible: tabMenu.tabOnline }
        MenuItem {
            visible: tabMenu.tabOnline
            height: visible ? implicitHeight : 0
            enabled: tabMenu.usable && !root.busy
            text: qsTr("Add project usage...")
            onTriggered: {
                // The usages belong to the current document.
                root.showTab(tabMenu.tabIndex)
                addUsageDialog.openPicker()
            }
        }
        MenuItem {
            visible: tabMenu.tabOnline
            height: visible ? implicitHeight : 0
            text: qsTr("Manage project usages...")
            onTriggered: {
                root.showTab(tabMenu.tabIndex)
                settingsDialog.open()
            }
        }
    }
    NewProjectDialog { id: newProjectDialog }
    CommitDialog { id: commitDialog }
    NewBranchDialog { id: newBranchDialog }
    CloseDocumentDialog { id: closeDialog }
    QuitDialog { id: quitDialog }

    Component {
        id: diagramWindowComponent
        DiagramWindow {}
    }
    AboutDialog { id: aboutDialog }
    DetailsDialog { id: detailsDialog }
    DigitalTwinWizard { id: wizard }
}
