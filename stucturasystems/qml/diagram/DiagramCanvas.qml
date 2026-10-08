pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Canvas of the graphical editor: dot grid, nodes (one NodeDelegate per row of controller.nodes) and the
// edge layer on top. Scrolls (drag the empty canvas, wheel, scroll bars) and zooms (Ctrl + wheel).
// All editing goes through the DiagramController; this file only translates mouse input.
FocusScope {
    id: canvas
    objectName: "diagramCanvas"

    property DiagramController controller
    /** Preview mode: no editing, no scrolling of its own (the surrounding list keeps the wheel). */
    property bool readOnly: false
    property real zoom: 1.0
    readonly property real minimumZoom: 0.25
    readonly property real maximumZoom: 3.0

    // Arrow being drawn (arrow mode)
    property string _arrowSource: ""

    // ---------------------------------------------------------------- API for window, palette and nodes
    function setZoom(value, anchorX, anchorY) {
        const newZoom = Math.max(minimumZoom, Math.min(maximumZoom, value))
        if (newZoom === zoom)
            return
        // Keep the canvas point under the anchor (default: view centre) in place.
        const ax = anchorX === undefined ? flick.width / 2 : anchorX
        const ay = anchorY === undefined ? flick.height / 2 : anchorY
        const cx = (flick.contentX + ax) / zoom
        const cy = (flick.contentY + ay) / zoom
        zoom = newZoom
        flick.contentX = Math.max(0, Math.min(cx * zoom - ax, flick.contentWidth - flick.width))
        flick.contentY = Math.max(0, Math.min(cy * zoom - ay, flick.contentHeight - flick.height))
    }
    /** Shows all nodes (at most 100 %), e.g. in the preview. */
    function fitToContent() {
        const bounds = controller.contentBounds()
        if (bounds.width <= 0 || bounds.height <= 0)
            return
        zoom = Math.max(minimumZoom, Math.min(1.0, flick.width / (bounds.x + bounds.width + 24),
                                              flick.height / (bounds.y + bounds.height + 24)))
        flick.contentX = 0
        flick.contentY = 0
    }

    function zoomIn() { setZoom(zoom * 1.2) }
    function zoomOut() { setZoom(zoom / 1.2) }
    function resetZoom() { setZoom(1.0) }

    /** Palette drop at a scene (window) position. */
    function dropTool(tool, sceneX, sceneY) {
        const local = flick.mapFromItem(null, sceneX, sceneY)
        if (local.x < 0 || local.y < 0 || local.x > flick.width || local.y > flick.height)
            return
        const point = world.mapFromItem(null, sceneX, sceneY)
        const id = controller.addElement(tool, point.x, point.y)
        canvas.forceActiveFocus()
        if (tool === DiagramTypes.ToolStatement && id.length > 0)
            editDialog.openFor(id) // the text is what this element is about
    }

    /** Palette click: parts and packages appear in the middle of the view, entries go to the selected part. */
    function insertTool(tool) {
        const point = world.mapFromItem(flick, flick.width / 2, flick.height / 2)
        const id = controller.addElementAtSelection(tool, point.x - 40 + Math.random() * 80, point.y - 40 + Math.random() * 80)
        canvas.forceActiveFocus()
        if (tool === DiagramTypes.ToolStatement && id.length > 0)
            editDialog.openFor(id)
    }

    function editElement(id) {
        editDialog.openFor(id)
    }

    function openNodeMenu(id, entryId) {
        nodeMenu.nodeId = id
        nodeMenu.entryId = entryId
        nodeMenu.popup()
    }

    function beginArrow(sourceId, point) {
        _arrowSource = sourceId
        const rect = controller.nodeRect(sourceId)
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
        const choice = controller.arrowReleased(source, point.x, point.y)
        if (choice.kinds !== undefined && choice.kinds.length > 0) {
            edgeKindMenu.source = source
            edgeKindMenu.target = choice.target
            edgeKindMenu.kinds = choice.kinds
            const at = canvas.mapFromItem(world, point.x, point.y)
            edgeKindMenu.popup(canvas, at.x, at.y)
        }
    }

    activeFocusOnTab: !readOnly
    clip: true

    Keys.enabled: !readOnly
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            controller.deleteSelection()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            if (controller.arrowMode)
                controller.arrowMode = false
            else
                controller.clearSelection()
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) {
            zoomIn()
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Minus) {
            zoomOut()
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_0) {
            resetZoom()
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.card
    }

    // Dot grid (below the nodes, follows scrolling and zoom)
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
        interactive: !canvas.readOnly
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 4000
        pressDelay: 0
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            id: world
            objectName: "diagramWorld"

            width: canvas.controller.canvasWidth
            height: canvas.controller.canvasHeight
            scale: canvas.zoom
            transformOrigin: Item.TopLeft

            // Empty canvas: select edges, clear the selection, context menu. Dragging scrolls (Flickable).
            MouseArea {
                anchors.fill: parent
                enabled: !canvas.readOnly
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

                NodeDelegate {
                    controller: canvas.controller
                    canvas: canvas
                    readOnly: canvas.readOnly
                }
            }
        }

        // Ctrl + wheel zooms around the mouse position; the plain wheel scrolls (Flickable).
        WheelHandler {
            id: zoomWheel
            enabled: !canvas.readOnly
            target: null
            acceptedModifiers: Qt.ControlModifier
            onWheel: event => {
                const anchor = zoomWheel.parent.mapToItem(flick, event.x, event.y)
                canvas.setZoom(canvas.zoom * Math.pow(1.15, event.angleDelta.y / 120), anchor.x, anchor.y)
            }
        }
    }

    // Edges on top of the nodes (they end at the node borders)
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

    // Empty state
    Column {
        anchors.centerIn: parent
        spacing: 6
        visible: canvas.controller.elementCount === 0 && !canvas.readOnly
        opacity: 0.85

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Empty diagram")
            font.pixelSize: 18
            font.weight: Font.Medium
            color: Theme.textSecondary
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Drag a part, part definition or package from the palette onto the canvas.\n"
                       + "Use the arrow tool to connect elements; double-click an element to edit it.")
            color: Theme.textSecondary
        }
    }

    // ---------------------------------------------------------------- menus and dialogs
    Menu {
        id: nodeMenu

        property string nodeId
        property string entryId
        readonly property string nodeLabel: nodeId.length > 0 ? canvas.controller.kindLabel(nodeId) : ""
        readonly property string entryLabel: entryId.length > 0 ? canvas.controller.kindLabel(entryId) : ""

        MenuItem {
            visible: nodeMenu.entryId.length > 0
            height: visible ? implicitHeight : 0
            text: qsTr("Edit %1...").arg(nodeMenu.entryLabel)
            onTriggered: canvas.editElement(nodeMenu.entryId)
        }
        MenuItem {
            visible: nodeMenu.entryId.length > 0
            height: visible ? implicitHeight : 0
            text: qsTr("Delete %1").arg(nodeMenu.entryLabel)
            onTriggered: canvas.controller.deleteElement(nodeMenu.entryId)
        }
        MenuSeparator {
            visible: nodeMenu.entryId.length > 0
            height: visible ? implicitHeight : 0
        }
        MenuItem {
            text: qsTr("Edit %1...").arg(nodeMenu.nodeLabel)
            onTriggered: canvas.editElement(nodeMenu.nodeId)
        }
        MenuItem {
            text: qsTr("Delete %1 (with owned elements)").arg(nodeMenu.nodeLabel)
            onTriggered: canvas.controller.deleteElement(nodeMenu.nodeId)
        }
    }

    Menu {
        id: edgeMenu

        property string edgeKey

        MenuItem {
            text: qsTr("Delete relationship")
            onTriggered: canvas.controller.deleteEdge(edgeMenu.edgeKey)
        }
    }

    Menu {
        id: backgroundMenu

        property point point

        MenuItem {
            text: qsTr("Add part here")
            onTriggered: canvas.controller.addElement(DiagramTypes.ToolPart, backgroundMenu.point.x, backgroundMenu.point.y)
        }
        MenuItem {
            text: qsTr("Add part definition here")
            onTriggered: canvas.controller.addElement(DiagramTypes.ToolPartDefinition, backgroundMenu.point.x, backgroundMenu.point.y)
        }
        MenuItem {
            text: qsTr("Add package here")
            onTriggered: canvas.controller.addElement(DiagramTypes.ToolPackage, backgroundMenu.point.x, backgroundMenu.point.y)
        }
    }

    EdgeKindMenu {
        id: edgeKindMenu
        objectName: "edgeKindMenu"
        controller: canvas.controller
    }

    ElementEditDialog {
        id: editDialog
        objectName: "elementEditDialog"
        controller: canvas.controller
    }
}
