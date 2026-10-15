pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Behavior view of one action or state: actions, control nodes and states with successions and transitions.
// Every arrow drawn here is written into the SysML text (see BehaviorController); this file only translates
// mouse input.
FocusScope {
    id: canvas
    objectName: "behaviorCanvas"

    property BehaviorController controller
    /** For the element and edge dialogs (they edit any element / relationship of the model). */
    property DiagramController structure
    property real zoom: 1.0
    readonly property real minimumZoom: 0.25
    readonly property real maximumZoom: 3.0

    property string _arrowSource: ""

    signal statusMessage(string message)

    function setZoom(value, anchorX, anchorY) {
        const newZoom = Math.max(minimumZoom, Math.min(maximumZoom, value))
        if (newZoom === zoom)
            return
        const ax = anchorX === undefined ? flick.width / 2 : anchorX
        const ay = anchorY === undefined ? flick.height / 2 : anchorY
        const cx = (flick.contentX + ax) / zoom
        const cy = (flick.contentY + ay) / zoom
        zoom = newZoom
        flick.contentX = Math.max(0, Math.min(cx * zoom - ax, flick.contentWidth - flick.width))
        flick.contentY = Math.max(0, Math.min(cy * zoom - ay, flick.contentHeight - flick.height))
    }
    function fitToContent() {
        const bounds = controller.contentBounds()
        if (bounds.width <= 0 || flick.width <= 0)
            return
        zoom = Math.max(minimumZoom, Math.min(1.0, flick.width / (bounds.x + bounds.width + 24),
                                              flick.height / (bounds.y + bounds.height + 24)))
        flick.contentX = 0
        flick.contentY = 0
    }
    function zoomIn() { setZoom(zoom * 1.2) }
    function zoomOut() { setZoom(zoom / 1.2) }
    function resetZoom() { setZoom(1.0) }

    function dropTool(tool, sceneX, sceneY) {
        const local = flick.mapFromItem(null, sceneX, sceneY)
        if (local.x < 0 || local.y < 0 || local.x > flick.width || local.y > flick.height)
            return
        const point = world.mapFromItem(null, sceneX, sceneY)
        controller.addNode(tool, point.x, point.y)
        canvas.forceActiveFocus()
    }
    function insertTool(tool) {
        const point = world.mapFromItem(flick, flick.width / 2, flick.height / 2)
        controller.addNode(tool, point.x - 40 + Math.random() * 80, point.y - 40 + Math.random() * 80)
        canvas.forceActiveFocus()
    }

    /** Double-click: the entry / do / exit action under the mouse, else the node itself. */
    function editNode(nodeId, x, y) {
        const entry = controller.entryAt(nodeId, x, y)
        editDialog.openFor(entry.length > 0 ? entry : nodeId)
    }
    function openNodeMenu(nodeId) {
        nodeMenu.nodeId = nodeId
        nodeMenu.popup()
    }

    function beginArrow(nodeId, point) {
        _arrowSource = nodeId
        const rect = controller.nodeRect(nodeId)
        edgeLayer.rubberStart = Qt.point(rect.x + rect.width / 2, rect.y + rect.height / 2)
        edgeLayer.rubberEnd = point
        edgeLayer.rubberBandVisible = true
    }
    function updateArrow(point) {
        if (_arrowSource.length > 0)
            edgeLayer.rubberEnd = point
    }
    function finishArrow(point) {
        edgeLayer.rubberBandVisible = false
        const source = _arrowSource
        _arrowSource = ""
        if (source.length === 0)
            return
        const target = controller.nodeAt(point.x, point.y)
        if (target.length === 0)
            return
        const error = controller.connectNodes(source, target)
        if (error.length > 0)
            canvas.statusMessage(error)
    }

    activeFocusOnTab: true
    clip: true

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            controller.deleteSelection()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            controller.clearSelection()
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.card
    }

    EdgeLayerItem {
        anchors.fill: flick
        showGrid: true
        gridColor: Qt.alpha(Theme.textSecondary, 0.35)
        originX: flick.contentX / canvas.zoom
        originY: flick.contentY / canvas.zoom
        zoom: canvas.zoom
    }

    Flickable {
        id: flick

        anchors.fill: parent
        contentWidth: canvas.controller.canvasWidth * canvas.zoom
        contentHeight: canvas.controller.canvasHeight * canvas.zoom
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 4000
        pressDelay: 0
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            id: world
            objectName: "behaviorWorld"

            width: canvas.controller.canvasWidth
            height: canvas.controller.canvasHeight
            scale: canvas.zoom
            transformOrigin: Item.TopLeft

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                onPressed: canvas.forceActiveFocus()
                onDoubleClicked: mouse => {
                    const edge = canvas.controller.edgeAt(mouse.x, mouse.y)
                    if (edge.length > 0)
                        edgeDialog.openFor(edge)
                }
                onClicked: mouse => {
                    const edge = canvas.controller.edgeAt(mouse.x, mouse.y)
                    if (mouse.button === Qt.RightButton) {
                        if (edge.length > 0) {
                            canvas.controller.selectEdge(edge, false)
                            edgeMenu.edgeKey = edge
                            edgeMenu.popup()
                        } else {
                            backgroundMenu.point = Qt.point(mouse.x, mouse.y)
                            backgroundMenu.popup()
                        }
                        return
                    }
                    if (edge.length > 0)
                        canvas.controller.selectEdge(edge, (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0)
                    else
                        canvas.controller.clearSelection()
                }
            }

            Repeater {
                model: canvas.controller.nodes

                BehaviorNode {
                    controller: canvas.controller
                    canvas: canvas
                }
            }
        }

        WheelHandler {
            id: zoomWheel
            target: null
            acceptedModifiers: Qt.ControlModifier
            onWheel: event => {
                const anchor = zoomWheel.parent.mapToItem(flick, event.x, event.y)
                canvas.setZoom(canvas.zoom * Math.pow(1.15, event.angleDelta.y / 120), anchor.x, anchor.y)
            }
        }
    }

    EdgeLayerItem {
        id: edgeLayer
        routing: true // around the nodes in between

        anchors.fill: flick
        nodes: canvas.controller.nodes
        edges: canvas.controller.edges
        originX: flick.contentX / canvas.zoom
        originY: flick.contentY / canvas.zoom
        zoom: canvas.zoom
        lineColor: Theme.textPrimary
        selectionColor: Theme.accent
        backgroundColor: Theme.card
    }

    Column {
        anchors.centerIn: parent
        spacing: 6
        visible: canvas.controller.nodes.count === 0
        opacity: 0.85

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: canvas.controller.contextId.length > 0 ? qsTr("Nothing in here yet") : qsTr("No action or state yet")
            font.pixelSize: 18
            font.weight: Font.Medium
            color: Theme.textSecondary
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: canvas.controller.contextId.length > 0
                  ? qsTr("Drag actions, control nodes or states from the palette.\n"
                         + "Draw arrows with the small handle at the corner of a node.")
                  : qsTr("Create an action definition or state definition in the structure view first.")
            color: Theme.textSecondary
        }
    }

    Menu {
        id: nodeMenu

        property string nodeId

        MenuItem {
            text: qsTr("Edit...")
            onTriggered: editDialog.openFor(nodeMenu.nodeId)
        }
        MenuItem {
            text: qsTr("Delete")
            onTriggered: {
                canvas.controller.select(nodeMenu.nodeId, false)
                canvas.controller.deleteSelection()
            }
        }
    }

    Menu {
        id: edgeMenu

        property string edgeKey

        MenuItem {
            text: qsTr("Guard, trigger, effect...")
            onTriggered: edgeDialog.openFor(edgeMenu.edgeKey)
        }
        MenuItem {
            text: qsTr("Delete arrow")
            onTriggered: canvas.controller.deleteEdge(edgeMenu.edgeKey)
        }
    }

    Menu {
        id: backgroundMenu

        property point point

        MenuItem {
            text: canvas.controller.stateMachine ? qsTr("Add state here") : qsTr("Add action here")
            onTriggered: canvas.controller.addNode(canvas.controller.stateMachine ? DiagramTypes.ToolState
                                                                                  : DiagramTypes.ToolActionUsage,
                                                   backgroundMenu.point.x, backgroundMenu.point.y)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Arrange automatically")
            onTriggered: canvas.controller.layoutAll()
        }
    }

    EdgeEditDialog {
        id: edgeDialog
        objectName: "behaviorEdgeDialog"
        controller: canvas.structure
    }

    ElementEditDialog {
        id: editDialog
        objectName: "behaviorEditDialog"
        controller: canvas.structure
    }
}
