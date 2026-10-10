pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Use case view of one package (or the top level): use cases, actors, system boundaries and «include». Every line
// drawn here is written into the SysML text (see UseCaseController); this file only translates mouse input.
FocusScope {
    id: canvas
    objectName: "useCaseCanvas"

    property UseCaseController controller
    /** For the element dialog (use cases, included use cases). */
    property DiagramController structure
    property real zoom: 1.0
    readonly property real minimumZoom: 0.25
    readonly property real maximumZoom: 3.0

    property string _lineSource: ""

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
        // actors and subjects go to the selected use case
        const point = world.mapFromItem(flick, flick.width / 2, flick.height / 2)
        controller.addNode(tool, point.x - 40 + Math.random() * 80, point.y - 40 + Math.random() * 80)
        canvas.forceActiveFocus()
    }

    /** Double-click: a use case in the element dialog, an actor / boundary in the group dialog. */
    function editNode(nodeId) {
        const element = controller.elementFor(nodeId)
        if (element.length > 0)
            editDialog.openFor(element)
        else
            groupDialog.openFor(nodeId)
    }
    function editEdge(key) {
        const element = controller.elementFor(key)
        if (element.length > 0)
            editDialog.openFor(element)
        else if (key.startsWith("a:"))
            groupDialog.openFor(key)
    }
    function openNodeMenu(nodeId) {
        nodeMenu.nodeId = nodeId
        nodeMenu.popup()
    }

    function beginLine(nodeId, point) {
        _lineSource = nodeId
        const rect = controller.nodeRect(nodeId)
        edgeLayer.rubberStart = Qt.point(rect.x + rect.width / 2, rect.y + rect.height / 2)
        edgeLayer.rubberEnd = point
        edgeLayer.rubberBandVisible = true
    }
    function updateLine(point) {
        if (_lineSource.length > 0)
            edgeLayer.rubberEnd = point
    }
    function finishLine(point) {
        edgeLayer.rubberBandVisible = false
        const source = _lineSource
        _lineSource = ""
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
            objectName: "useCaseWorld"

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
                        canvas.editEdge(edge)
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

                UseCaseNode {
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
            text: qsTr("No use case here yet")
            font.pixelSize: 18
            font.weight: Font.Medium
            color: Theme.textSecondary
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Drag a use case from the palette, then actors and a subject onto it.\n"
                       + "Draw lines with the small handle at the corner of a node.")
            color: Theme.textSecondary
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
            text: qsTr("Edit...")
            enabled: !edgeMenu.edgeKey.startsWith("r:")
            onTriggered: canvas.editEdge(edgeMenu.edgeKey)
        }
        MenuItem {
            text: qsTr("Delete line")
            onTriggered: canvas.controller.deleteEdge(edgeMenu.edgeKey)
        }
    }

    Menu {
        id: backgroundMenu

        property point point

        MenuItem {
            text: qsTr("Add use case here")
            onTriggered: canvas.controller.addNode(DiagramTypes.ToolUseCaseDefinition, backgroundMenu.point.x,
                                                   backgroundMenu.point.y)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Arrange automatically")
            onTriggered: canvas.controller.layoutAll()
        }
    }

    ElementEditDialog {
        id: editDialog
        objectName: "useCaseEditDialog"
        controller: canvas.structure
    }

    GroupEditDialog {
        id: groupDialog
        objectName: "groupEditDialog"
        controller: canvas.controller
    }
}
