pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Parametric view of one part: values, calls, expressions, constraints and calculations with their
// parameters, and the lines between them. Every line drawn here is written into the SysML text (see
// ParametricController); this file only translates mouse input.
FocusScope {
    id: canvas
    objectName: "parametricCanvas"

    property ParametricController controller
    /** For the element dialog (it edits any element of the model). */
    property DiagramController structure
    property real zoom: 1.0
    readonly property real minimumZoom: 0.25
    readonly property real maximumZoom: 3.0

    property string _lineNode: ""
    property int _linePin: -1
    property bool _lineOutput: false

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
    /** Zooms out (never in) so that all nodes are visible. */
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

    function worldFromScene(sceneX, sceneY) {
        const local = flick.mapFromItem(null, sceneX, sceneY)
        if (local.x < 0 || local.y < 0 || local.x > flick.width || local.y > flick.height)
            return null
        return world.mapFromItem(null, sceneX, sceneY)
    }
    function viewCenter() {
        const point = world.mapFromItem(flick, flick.width / 2, flick.height / 2)
        return Qt.point(point.x - 40 + Math.random() * 80, point.y - 40 + Math.random() * 80)
    }

    function addTool(tool, point) {
        if (tool === DiagramTypes.ToolAttribute)
            controller.addValue(point.x, point.y)
        else if (tool === DiagramTypes.ToolConstraint || tool === DiagramTypes.ToolCalc)
            controller.addUsage(tool, point.x, point.y)
        canvas.forceActiveFocus()
    }
    /** Palette drop / click */
    function dropTool(tool, sceneX, sceneY) {
        const point = worldFromScene(sceneX, sceneY)
        if (point)
            addTool(tool, point)
    }
    function insertTool(tool) {
        addTool(tool, viewCenter())
    }
    function dropDefinition(definitionId, sceneX, sceneY) {
        const point = worldFromScene(sceneX, sceneY)
        if (point)
            controller.dropDefinition(definitionId, point.x, point.y)
        canvas.forceActiveFocus()
    }
    function insertDefinition(definitionId) {
        const point = viewCenter()
        controller.dropDefinition(definitionId, point.x, point.y)
        canvas.forceActiveFocus()
    }

    function editNode(nodeId) {
        const element = controller.elementOf(nodeId)
        if (element.length > 0)
            editDialog.openFor(element)
    }
    function openNodeMenu(nodeId) {
        nodeMenu.nodeId = nodeId
        nodeMenu.popup()
    }

    // ---- lines ----
    function beginLine(nodeId, pin, output, point) {
        _lineNode = nodeId
        _linePin = pin
        _lineOutput = output
        edgeLayer.rubberStart = controller.pinPosition(nodeId, pin, output)
        edgeLayer.rubberEnd = point
        edgeLayer.rubberBandVisible = true
    }
    function updateLine(point) {
        if (_lineNode.length > 0)
            edgeLayer.rubberEnd = point
    }
    function finishLine(point) {
        edgeLayer.rubberBandVisible = false
        const source = _lineNode
        _lineNode = ""
        if (source.length === 0)
            return
        const hit = controller.hitTest(point.x, point.y)
        if (hit.node.length === 0)
            return
        if (hit.node === source && hit.pin === _linePin && hit.output === _lineOutput)
            return
        const result = controller.connectOptions(source, _linePin, _lineOutput, hit.node, hit.pin, hit.output)
        if (result.options.length === 0) {
            canvas.statusMessage(result.error)
            return
        }
        lineMenu.request = { source: source, sourcePin: _linePin, sourceOutput: _lineOutput,
                             target: hit.node, targetPin: hit.pin, targetOutput: hit.output }
        if (result.options.length === 1) {
            lineMenu.apply(result.options[0].option)
            return
        }
        lineMenu.options = result.options
        const at = canvas.mapFromItem(world, point.x, point.y)
        lineMenu.popup(canvas, at.x, at.y)
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
            objectName: "parametricWorld"

            width: canvas.controller.canvasWidth
            height: canvas.controller.canvasHeight
            scale: canvas.zoom
            transformOrigin: Item.TopLeft

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                onPressed: canvas.forceActiveFocus()
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

                ParametricNode {
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
        pinMode: true
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
            text: canvas.controller.contextId.length > 0 ? qsTr("No values yet") : qsTr("No part yet")
            font.pixelSize: 18
            font.weight: Font.Medium
            color: Theme.textSecondary
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: canvas.controller.contextId.length > 0
                  ? qsTr("Drag values, constraints, calculations or a definition from the palette.\n"
                         + "Draw lines from a value onto a parameter to bind it.")
                  : qsTr("Create a part or part definition in the structure view first.")
            color: Theme.textSecondary
        }
    }

    // ---------------------------------------------------------------- menus and dialogs
    Menu {
        id: lineMenu
        objectName: "lineMenu"

        property var request: ({})
        property var options: []

        width: 340

        function apply(option) {
            const r = request
            const error = canvas.controller.connectPins(r.source, r.sourcePin, r.sourceOutput,
                                                        r.target, r.targetPin, r.targetOutput, option)
            if (error.length > 0)
                canvas.statusMessage(error)
        }

        Repeater {
            model: lineMenu.options

            MenuItem {
                required property var modelData
                text: modelData.label
                onTriggered: lineMenu.apply(modelData.option)
            }
        }
    }

    Menu {
        id: nodeMenu

        property string nodeId

        MenuItem {
            text: qsTr("Edit...")
            onTriggered: canvas.editNode(nodeMenu.nodeId)
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
            text: qsTr("Delete line")
            onTriggered: canvas.controller.deleteEdge(edgeMenu.edgeKey)
        }
    }

    Menu {
        id: backgroundMenu

        property point point

        MenuItem {
            text: qsTr("Add value here")
            onTriggered: canvas.controller.addValue(backgroundMenu.point.x, backgroundMenu.point.y)
        }
        MenuItem {
            text: qsTr("Add constraint here")
            onTriggered: canvas.controller.addUsage(DiagramTypes.ToolConstraint, backgroundMenu.point.x, backgroundMenu.point.y)
        }
        MenuItem {
            text: qsTr("Add calculation here")
            onTriggered: canvas.controller.addUsage(DiagramTypes.ToolCalc, backgroundMenu.point.x, backgroundMenu.point.y)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Arrange automatically")
            onTriggered: canvas.controller.layoutAll()
        }
    }

    ElementEditDialog {
        id: editDialog
        objectName: "parametricEditDialog"
        controller: canvas.structure
    }
}
