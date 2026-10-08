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
    readonly property bool canCommit: doc !== null && doc.isOnline && AppController.connected && !busy
    readonly property bool canUpload: doc !== null && !doc.isOnline && AppController.connected && !busy

    /** 0 explorer, 1 online, 2 digital twins */
    property int destination: 0
    property bool panelOpen: true
    /** Narrow windows start without side panel; it can still be opened by hand. */
    readonly property bool compact: width < 1100
    property bool _restored: false
    property bool forceClose: false

    function requestClose(index, title, modified) {
        if (index < 0 || busy)
            return
        if (modified)
            closeDialog.openFor(index, title)
        else
            AppController.closeDocument(index)
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
    onVisibilityChanged: {
        if (_restored && (visibility === Window.Windowed || visibility === Window.Maximized))
            windowState.maximized = visibility === Window.Maximized
    }
    onCompactChanged: {
        if (_restored)
            panelOpen = !compact
    }
    onPanelOpenChanged: if (_restored) windowState.panelOpen = panelOpen
    onDestinationChanged: if (_restored) windowState.destination = destination

    onClosing: close => {
        if (!forceClose && AppController.hasUnsavedChanges()) {
            close.accepted = false
            quitDialog.openDialog()
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
        property int destination: 0
        property int problemsHeight: 190
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
    Shortcut {
        sequences: ["Ctrl+W"]
        enabled: root.doc !== null
        onActivated: root.requestClose(AppController.currentIndex, root.doc.title, root.doc.modified)
    }
    Shortcut { sequences: ["Ctrl+,"]; onActivated: settingsDialog.open() }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: root.close() }

    // ---------------------------------------------------------------- reactions to the view models
    Connections {
        target: AppController

        function onNotify(level, message, details) {
            snackbar.show(level, message, details)
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
            Layout.preferredWidth: root.panelOpen ? Theme.panelWidth : 0
            clip: true
            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: Theme.animationDuration; easing.type: Easing.OutCubic }
            }

            StackLayout {
                width: Theme.panelWidth
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
                }
                DigitalTwinPane {
                    onCreateRequested: wizard.openWizard()
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.outline
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
                            if (currentIndex !== AppController.currentIndex)
                                AppController.currentIndex = currentIndex
                        }

                        Repeater {
                            model: AppController.documents

                            TabButton {
                                id: tab

                                required property int index
                                required property string title
                                required property bool modified

                                width: Math.min(Math.max(implicitWidth, 120), 240)
                                padding: 6
                                leftPadding: 12

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

                EditorView {
                    id: editor
                    anchors.fill: parent
                    visible: root.doc !== null
                    document: root.doc
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
            }

            ProblemsSheet {
                id: problemsSheet
                Layout.fillWidth: true
                visible: root.doc !== null
                onProblemActivated: row => editor.scrollToRow(row)
            }
        }
    }

    footer: StatusBar {
        elementCount: root.doc ? root.doc.count : -1
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

    SettingsDialog { id: settingsDialog }
    NewProjectDialog { id: newProjectDialog }
    CommitDialog { id: commitDialog }
    CloseDocumentDialog { id: closeDialog }
    QuitDialog {
        id: quitDialog
        onQuitAccepted: {
            root.forceClose = true
            root.close()
        }
    }
    AboutDialog { id: aboutDialog }
    DetailsDialog { id: detailsDialog }
    DigitalTwinWizard { id: wizard }
}
