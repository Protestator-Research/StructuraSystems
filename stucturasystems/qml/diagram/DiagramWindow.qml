pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Graphical editor of one document in a window of its own: draw parts, packages and relationships,
// the generated SysML v2 code is written into the document's dedicated SysML block (live or on demand).
// Opened from the main window (toolbar icon). Closes itself when its document is closed.
ApplicationWindow {
    id: window

    /** The DocumentModel the generated code is written into. */
    property QtObject document: null
    /** Element id of the block shown and edited; empty = the document's generated block. */
    property string blockElementId: ""
    readonly property DiagramController controller: editorModel.controller
    readonly property ParametricController parametric: editorModel.parametric
    readonly property BehaviorController behavior: editorModel.behavior
    readonly property UseCaseController useCases: editorModel.useCases
    readonly property SequenceController sequence: editorModel.sequence
    /** 0 structure, 1 parametric view, 2 behavior view, 3 use case view, 4 sequence view */
    property int view: 0
    readonly property Item activeCanvas: view === 0 ? canvas : view === 1 ? parametricCanvas
                                         : view === 2 ? behaviorCanvas : view === 3 ? useCaseCanvas : sequenceCanvas

    /** Emitted when the window was closed; the main window destroys it then. */
    signal finished

    function showStatus(message) {
        statusLabel.text = message
        statusTimer.restart()
    }

    width: 1200
    height: 800
    minimumWidth: 820
    minimumHeight: 520
    title: document ? (blockElementId.length > 0 ? qsTr("Diagram - %1 (block)") : qsTr("Diagram - %1")).arg(document.title)
                    : qsTr("Diagram")
    color: Theme.surface

    Material.theme: Theme.materialTheme
    Material.primary: Theme.primary
    Material.accent: Theme.accent
    Material.background: Theme.surface

    onClosing: window.finished()
    // The parametric view shows everything when it is opened.
    onViewChanged: {
        if (view === 1)
            Qt.callLater(parametricCanvas.fitToContent)
        else if (view === 2)
            Qt.callLater(behaviorCanvas.fitToContent)
        else if (view === 3)
            Qt.callLater(useCaseCanvas.fitToContent)
        else if (view === 4)
            Qt.callLater(sequenceCanvas.fitToContent)
    }

    DiagramEditorModel {
        id: editorModel

        document: window.document
        blockElementId: window.blockElementId
        // No writing while a commit, upload or parse works on the document.
        writeLocked: AppController.busy

        onDocumentClosed: window.close()
        // Also carries the messages of the controller (drop hints, rule violations).
        onStatusMessage: message => window.showStatus(message)
    }

    Timer {
        id: statusTimer
        interval: 6000
        onTriggered: statusLabel.text = ""
    }

    Shortcut {
        sequences: [StandardKey.Close]
        onActivated: window.close()
    }
    Shortcut {
        sequences: ["Ctrl+G"]
        onActivated: editorModel.generateNow()
    }

    // ---------------------------------------------------------------- top bar
    header: ToolBar {
        Material.background: Theme.panel
        Material.elevation: 0
        implicitHeight: 52

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 10

            Label {
                visible: window.width >= 1500 // room for the five views
                text: qsTr("Code generation")
                color: Theme.textSecondary
            }
            SegmentedControl {
                model: [qsTr("Live"), qsTr("On demand")]
                currentIndex: editorModel.generationMode === DiagramEditorModel.Live ? 0 : 1
                onActivated: index => editorModel.generationMode = index === 0 ? DiagramEditorModel.Live
                                                                              : DiagramEditorModel.OnDemand
                ToolTip.visible: hoverLive.hovered
                ToolTip.text: qsTr("Live: every change is written into the document's SysML block.\n"
                                   + "On demand: only \"Generate SysML\" writes into the block.")
                ToolTip.delay: 700
                HoverHandler {
                    id: hoverLive
                }
            }
            Button {
                text: qsTr("Generate SysML")
                highlighted: editorModel.generationMode === DiagramEditorModel.OnDemand && editorModel.pendingChanges
                enabled: window.document !== null
                onClicked: editorModel.generateNow()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Write the generated code into the document now (Ctrl+G)")
                ToolTip.delay: 700
            }

            // Sync state
            Rectangle {
                implicitWidth: syncRow.implicitWidth + 16
                implicitHeight: 24
                radius: 12
                visible: syncLabel.text.length > 0
                color: Qt.alpha(syncLabel.color, Theme.dark ? 0.2 : 0.12)

                Row {
                    id: syncRow
                    anchors.centerIn: parent
                    Label {
                        id: syncLabel

                        readonly property bool paused: editorModel.blockState === DiagramEditorModel.EditedByHand
                                                       || editorModel.blockState === DiagramEditorModel.NotLoaded
                                                       || editorModel.blockState === DiagramEditorModel.HandWritten

                        text: window.document === null ? qsTr("Preview only")
                              : paused ? qsTr("Paused")
                              : editorModel.pendingChanges ? qsTr("Not yet in document")
                              : editorModel.blockState === DiagramEditorModel.InSync ? qsTr("In document")
                              : ""
                        color: paused ? Theme.warning
                               : editorModel.pendingChanges ? Theme.info
                               : Theme.success
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }

            SegmentedControl {
                objectName: "viewSwitch"
                model: [qsTr("Structure"), qsTr("Parametric"), qsTr("Behavior"), qsTr("Use Cases"), qsTr("Sequence")]
                currentIndex: window.view
                onActivated: index => window.view = index
            }
            ComboBox {
                id: contextBox
                objectName: "contextBox"
                visible: window.view === 1
                Layout.preferredWidth: 200
                model: window.parametric.contexts
                textRole: "title"
                valueRole: "id"
                currentIndex: {
                    const list = window.parametric.contexts
                    for (let i = 0; i < list.length; ++i) {
                        if (list[i].id === window.parametric.contextId)
                            return i
                    }
                    return -1
                }
                onActivated: index => window.parametric.contextId = window.parametric.contexts[index].id
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Part whose values, constraints and calculations are shown")
                ToolTip.delay: 700
            }

            ComboBox {
                id: behaviorBox
                objectName: "behaviorContextBox"
                visible: window.view === 2
                Layout.preferredWidth: 200
                model: window.behavior.contexts
                textRole: "title"
                valueRole: "id"
                currentIndex: {
                    const list = window.behavior.contexts
                    for (let i = 0; i < list.length; ++i) {
                        if (list[i].id === window.behavior.contextId)
                            return i
                    }
                    return -1
                }
                onActivated: index => window.behavior.contextId = window.behavior.contexts[index].id
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Action or state whose flow / state machine is shown")
                ToolTip.delay: 700
            }

            ComboBox {
                id: useCaseBox
                objectName: "useCaseContextBox"
                visible: window.view === 3
                Layout.preferredWidth: 200
                model: window.useCases.contexts
                textRole: "title"
                valueRole: "id"
                currentIndex: {
                    const list = window.useCases.contexts
                    for (let i = 0; i < list.length; ++i) {
                        if (list[i].id === window.useCases.contextId)
                            return i
                    }
                    return -1
                }
                onActivated: index => window.useCases.contextId = window.useCases.contexts[index].id
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Package whose use cases are shown")
                ToolTip.delay: 700
            }

            ComboBox {
                id: sequenceBox
                objectName: "sequenceContextBox"
                visible: window.view === 4
                Layout.preferredWidth: 200
                model: window.sequence.contexts
                textRole: "title"
                valueRole: "id"
                currentIndex: {
                    const list = window.sequence.contexts
                    for (let i = 0; i < list.length; ++i) {
                        if (list[i].id === window.sequence.contextId)
                            return i
                    }
                    return -1
                }
                onActivated: index => window.sequence.contextId = window.sequence.contexts[index].id
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Interaction (occurrence definition) whose lifelines and messages are shown")
                ToolTip.delay: 700
            }

            ToolButton {
                text: "−"
                font.pixelSize: 18
                onClicked: window.activeCanvas.zoomOut()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Zoom out (Ctrl + mouse wheel)")
                ToolTip.delay: 600
            }
            ToolButton {
                text: Math.round(window.activeCanvas.zoom * 100) + "%"
                implicitWidth: 64
                onClicked: window.activeCanvas.resetZoom()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Reset zoom")
                ToolTip.delay: 600
            }
            ToolButton {
                text: "+"
                font.pixelSize: 18
                onClicked: window.activeCanvas.zoomIn()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Zoom in (Ctrl + mouse wheel)")
                ToolTip.delay: 600
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.outline
        }
    }

    // ---------------------------------------------------------------- content
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        BlockStateBar {
            Layout.fillWidth: true
            blockState: editorModel.blockState
            hiddenSummary: editorModel.hiddenSummary
            hiddenCount: editorModel.hiddenCount
            hiddenDetails: editorModel.hiddenDetails
            onReplaceRequested: editorModel.overwriteWithDiagram()
            onReloadRequested: editorModel.reloadFromBlock()
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            RowLayout {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 240
                spacing: 0

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: window.view

                    DiagramCanvas {
                        id: canvas
                        controller: window.controller
                    }
                    ParametricCanvas {
                        id: parametricCanvas
                        controller: window.parametric
                        structure: window.controller
                        onStatusMessage: message => window.showStatus(message)
                    }
                    BehaviorCanvas {
                        id: behaviorCanvas
                        controller: window.behavior
                        structure: window.controller
                        onStatusMessage: message => window.showStatus(message)
                    }
                    UseCaseCanvas {
                        id: useCaseCanvas
                        controller: window.useCases
                        structure: window.controller
                        onStatusMessage: message => window.showStatus(message)
                    }
                    SequenceCanvas {
                        id: sequenceCanvas
                        controller: window.sequence
                        structure: window.controller
                        onStatusMessage: message => window.showStatus(message)
                    }
                }
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    color: Theme.outline
                }
                ToolPalette {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 224
                    controller: window.controller
                    parametric: window.view === 1
                    behavior: window.view === 2
                    useCase: window.view === 3
                    sequence: window.view === 4
                    sequenceController: window.sequence
                    stateMachine: window.behavior.stateMachine
                    definitions: window.parametric.definitions
                    onToolDropped: (tool, sceneX, sceneY) => window.activeCanvas.dropTool(tool, sceneX, sceneY)
                    onToolClicked: tool => window.activeCanvas.insertTool(tool)
                    onDefinitionDropped: (id, sceneX, sceneY) => parametricCanvas.dropDefinition(id, sceneX, sceneY)
                    onDefinitionClicked: id => parametricCanvas.insertDefinition(id)
                }
            }

            // Generated code (read only, always live)
            Pane {
                SplitView.preferredHeight: 190
                SplitView.minimumHeight: 90
                padding: 0
                Material.background: Theme.panel

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Label {
                        Layout.leftMargin: 12
                        Layout.topMargin: 8
                        Layout.bottomMargin: 4
                        text: qsTr("Generated SysML v2")
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: Theme.textSecondary
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        TextArea {
                            id: codeArea
                            readOnly: true
                            selectByMouse: true
                            text: editorModel.code
                            placeholderText: qsTr("Drag elements from the palette onto the canvas.")
                            font.family: Theme.monoFamily
                            font.pixelSize: 13
                            leftPadding: 12
                            background: null

                            SysMLHighlighter {
                                textDocument: codeArea.textDocument
                                darkTheme: Theme.dark
                            }
                        }
                    }
                }
            }
        }

        // Status line
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: Theme.panel

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.outline
            }
            Label {
                id: statusLabel
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                font.pixelSize: 12
                color: Theme.textSecondary
            }
        }
    }
}
