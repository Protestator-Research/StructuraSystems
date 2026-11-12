pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Sequence view of one interaction: lifelines, events and messages. Every message drawn here is written into
// the SysML text (see SequenceController); this file only translates mouse input.
FocusScope {
    id: canvas
    objectName: "sequenceCanvas"

    property SequenceController controller
    /** For the element and edge dialogs (they edit any element / relationship of the model). */
    property DiagramController structure
    property real zoom: 1.0
    readonly property real minimumZoom: 0.25
    readonly property real maximumZoom: 3.0

    property bool _arrow: false
    property point _arrowStart

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
        // a lifeline on the right of the others; an event below the others on the selected lifeline
        const bounds = controller.contentBounds()
        if (tool === DiagramTypes.ToolLifeline) {
            controller.addNode(tool, bounds.width > 0 ? bounds.x + bounds.width + 100 : 120, 60)
        } else {
            canvas.statusMessage(qsTr("Drag the event onto a lifeline."))
        }
        canvas.forceActiveFocus()
    }

    function editElement(nodeId) {
        const element = controller.elementOf(nodeId)
        if (element.length > 0)
            editDialog.openFor(element)
    }
    function openNodeMenu(nodeId) {
        nodeMenu.nodeId = nodeId
        nodeMenu.isEvent = controller.isEvent(nodeId)
        nodeMenu.popup()
    }

    function beginArrow(point) {
        _arrow = true
        _arrowStart = point
        edgeLayer.rubberStart = point
        edgeLayer.rubberEnd = point
        edgeLayer.rubberBandVisible = true
    }
    function updateArrow(point) {
        if (_arrow)
            edgeLayer.rubberEnd = point
    }
    function cancelArrow() {
        _arrow = false
        edgeLayer.rubberBandVisible = false
    }
    function finishArrow(point) {
        edgeLayer.rubberBandVisible = false
        if (!_arrow)
            return
        _arrow = false
        if (Math.abs(point.x - _arrowStart.x) + Math.abs(point.y - _arrowStart.y) < 12)
            return // a click, no arrow
        const error = controller.connectPoints(_arrowStart.x, _arrowStart.y, point.x, point.y)
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
            cancelArrow()
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
        interactive: !canvas._arrow
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            id: world
            objectName: "sequenceWorld"

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

                SequenceNode {
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
            text: canvas.controller.contextId.length > 0 ? qsTr("No lifelines yet") : qsTr("No interaction yet")
            font.pixelSize: 18
            font.weight: Font.Medium
            color: Theme.textSecondary
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: canvas.controller.contextId.length > 0
                  ? qsTr("Drag lifelines from the palette, then draw messages from one lifeline to another.")
                  : qsTr("Create an occurrence definition (an interaction) in the structure view first.")
            color: Theme.textSecondary
        }
    }

    Menu {
        id: nodeMenu

        property string nodeId
        property bool isEvent: false

        MenuItem {
            text: qsTr("Edit...")
            onTriggered: canvas.editElement(nodeMenu.nodeId)
        }
        MenuItem {
            visible: nodeMenu.isEvent
            height: visible ? implicitHeight : 0
            text: qsTr("Move up")
            onTriggered: canvas.controller.shiftEvent(nodeMenu.nodeId, -1)
        }
        MenuItem {
            visible: nodeMenu.isEvent
            height: visible ? implicitHeight : 0
            text: qsTr("Move down")
            onTriggered: canvas.controller.shiftEvent(nodeMenu.nodeId, 1)
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
            text: qsTr("Name, payload...")
            onTriggered: edgeDialog.openFor(edgeMenu.edgeKey)
        }
        MenuItem {
            text: qsTr("Delete")
            onTriggered: canvas.controller.deleteEdge(edgeMenu.edgeKey)
        }
    }

    Menu {
        id: backgroundMenu

        property point point

        MenuItem {
            text: qsTr("Add lifeline here")
            onTriggered: canvas.controller.addNode(DiagramTypes.ToolLifeline, backgroundMenu.point.x, backgroundMenu.point.y)
        }
        MenuItem {
            text: qsTr("Add event here")
            onTriggered: canvas.controller.addNode(DiagramTypes.ToolEvent, backgroundMenu.point.x, backgroundMenu.point.y)
        }
    }

    EdgeEditDialog {
        id: edgeDialog
        objectName: "sequenceEdgeDialog"
        controller: canvas.structure
    }

    ElementEditDialog {
        id: editDialog
        objectName: "sequenceEditDialog"
        controller: canvas.structure
    }
}
