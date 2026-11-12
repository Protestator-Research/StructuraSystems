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
        if (tool === DiagramTypes.ToolVariant) {
            // a new variant of a fitting kind, or a usage declared elsewhere ("variant part1;", 7.6.7)
            const choices = controller.variantChoices(point.x, point.y)
            if (choices.length > 1) {
                variantMenu.choices = choices
                const at = canvas.mapFromItem(world, point.x, point.y)
                variantMenu.popup(canvas, at.x, at.y)
                return
            }
            if (choices.length === 1) {
                const added = controller.addVariant(choices[0].variation, choices[0].kind, choices[0].existing)
                canvas.forceActiveFocus()
                if (added.length > 0)
                    editDialog.openFor(added)
                return
            }
        }
        const id = controller.addElement(tool, point.x, point.y)
        canvas.forceActiveFocus()
        if ((tool === DiagramTypes.ToolStatement || tool === DiagramTypes.ToolEnumValue) && id.length > 0)
            editDialog.openFor(id) // the text is what this element is about; name / value / unit of an enum value
        else if (id.length > 0)
            askNotation(id, point)
    }

    /** [{header, text} | {subject, group, value, text, checked, enabled}] for the notation menu. */
    function notationItems(choices) {
        const items = []
        for (let c = 0; c < choices.length; ++c) {
            const choice = choices[c]
            const individual = choice.mode === "individual"
            items.push({ header: true, text: choice.label + (individual ? "" : qsTr(" (set for all in the toolbar)")) })
            for (let o = 0; o < choice.options.length; ++o) {
                const option = choice.options[o]
                items.push({ header: false, text: option.label, subject: choice.subject, group: choice.group,
                             value: option.value, checked: option.value === choice.value, enabled: individual })
            }
        }
        return items
    }

    /** Right after creating an element: how it (or its owner) is drawn, where the specification offers a choice. */
    function askNotation(id, point) {
        const choices = controller.creationChoices(id)
        if (choices.length === 0)
            return
        notationMenu.title_ = qsTr("How shall it be drawn?")
        notationMenu.items = notationItems(choices)
        const at = canvas.mapFromItem(world, point.x, point.y)
        notationMenu.popup(canvas, at.x, at.y)
    }

    /** Context menu: the notations of an element (and of the entry under the mouse). */
    function openNotationMenu(ids) {
        let choices = []
        for (let i = 0; i < ids.length; ++i) {
            if (ids[i].length > 0)
                choices = choices.concat(controller.notationChoices(ids[i]))
        }
        if (choices.length === 0)
            return
        notationMenu.title_ = qsTr("Notation")
        notationMenu.items = notationItems(choices)
        notationMenu.popup()
    }

    /** Palette click: parts and packages appear in the middle of the view, entries go to the selected part. */
    function insertTool(tool) {
        const point = world.mapFromItem(flick, flick.width / 2, flick.height / 2)
        const id = controller.addElementAtSelection(tool, point.x - 40 + Math.random() * 80, point.y - 40 + Math.random() * 80)
        canvas.forceActiveFocus()
        if (tool === DiagramTypes.ToolStatement && id.length > 0)
            editDialog.openFor(id)
        else if (id.length > 0)
            askNotation(id, point)
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
        if (choice.kinds !== undefined && choice.kinds.length === 1 && choice.kinds[0] === DiagramTypes.Annotation) {
            // from a comment or metadata: right away the choice what it is about
            const targets = controller.annotationTargets(source, choice.target)
            if (targets.length <= 1) {
                controller.createEdge(DiagramTypes.Annotation, source, choice.target)
                return
            }
            annotationMenu.source = source
            annotationMenu.target = choice.target
            annotationMenu.targets = targets
            const near = canvas.mapFromItem(world, point.x, point.y)
            annotationMenu.popup(canvas, near.x, near.y)
            return
        }
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
                            // inside a package frame: its own entries too (edit, notation)
                            backgroundMenu.frameId = canvas.controller.nodeAt(mouse.x, mouse.y, true)
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

            // a view rendered (framed view, graphical BNF view-frame): «view» name : Def in the tab, the expose /
            // filter / render lines in the info compartment at the top right
            Item {
                id: viewFrame
                objectName: "viewFrame"
                readonly property rect bounds: canvas.controller.viewTitle.length > 0 && canvas.controller.geometryRevision >= 0
                                               ? canvas.controller.contentBounds() : Qt.rect(0, 0, 0, 0)
                visible: canvas.controller.viewTitle.length > 0
                x: bounds.x - 30
                y: bounds.y - 70 - Math.max(0, infoColumn.height - 30)
                width: Math.max(bounds.width + 60, viewTitleLabel.implicitWidth + infoBox.width + 80)
                height: bounds.y + bounds.height + 30 - y
                z: -1

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 1.4
                    border.color: Theme.textPrimary
                }
                Rectangle {
                    id: viewTab
                    width: viewTitleLabel.implicitWidth + 24
                    height: 26
                    color: Theme.card
                    border.width: 1.4
                    border.color: Theme.textPrimary
                    Label {
                        id: viewTitleLabel
                        anchors.centerIn: parent
                        text: canvas.controller.viewTitle
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: Theme.textPrimary
                    }
                }
                Rectangle {
                    id: infoBox
                    anchors.right: parent.right
                    width: infoColumn.implicitWidth + 16
                    height: infoColumn.height + 10
                    visible: canvas.controller.viewInfo.length > 0
                    color: Theme.card
                    border.width: 1.2
                    border.color: Theme.textPrimary
                    Column {
                        id: infoColumn
                        x: 8
                        y: 5
                        Repeater {
                            model: canvas.controller.viewInfo
                            Label {
                                required property string modelData
                                text: modelData
                                font.pixelSize: 11
                                font.family: Theme.monoFamily
                                color: Theme.textPrimary
                            }
                        }
                    }
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
    // the variant tool on a variation: which variant
    Menu {
        id: variantMenu
        objectName: "variantMenu"
        property var choices: []

        Repeater {
            model: variantMenu.choices
            MenuItem {
                required property var modelData
                text: modelData.text
                onTriggered: {
                    const added = canvas.controller.addVariant(modelData.variation, modelData.kind, modelData.existing)
                    canvas.forceActiveFocus()
                    if (added.length > 0 && modelData.existing.length === 0)
                        editDialog.openFor(added) // name and type of the new one
                }
            }
        }
    }

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
        MenuItem {
            // where the specification draws it in more than one way: how this one is drawn
            visible: nodeMenu.nodeId.length > 0 && canvas.controller.notationRevision >= 0
                     && (canvas.controller.notationChoices(nodeMenu.nodeId).length > 0
                         || (nodeMenu.entryId.length > 0 && canvas.controller.notationChoices(nodeMenu.entryId).length > 0))
            height: visible ? implicitHeight : 0
            text: qsTr("Notation...")
            onTriggered: canvas.openNotationMenu([nodeMenu.entryId, nodeMenu.nodeId])
        }
    }

    Menu {
        id: edgeMenu

        property string edgeKey

        MenuItem {
            readonly property bool editable: edgeMenu.edgeKey.length > 0
                                             && canvas.controller.edgeDetails(edgeMenu.edgeKey).editable === true
            visible: editable
            height: visible ? implicitHeight : 0
            text: qsTr("Name and type...")
            onTriggered: edgeDialog.openFor(edgeMenu.edgeKey)
        }
        MenuItem {
            readonly property string subject: edgeMenu.edgeKey.length > 0 ? canvas.controller.notationSubject(edgeMenu.edgeKey) : ""
            visible: subject.length > 0
            height: visible ? implicitHeight : 0
            text: qsTr("Notation...")
            onTriggered: canvas.openNotationMenu([subject])
        }
        MenuItem {
            text: qsTr("Delete relationship")
            onTriggered: canvas.controller.deleteEdge(edgeMenu.edgeKey)
        }
    }

    // the notations offered for an element (table 2, 3, 4, 9): a header per group, its options to tick
    Menu {
        id: notationMenu
        objectName: "notationMenu"
        property string title_
        property var items: []

        MenuItem {
            text: notationMenu.title_
            enabled: false
        }
        MenuSeparator {}
        Repeater {
            model: notationMenu.items
            MenuItem {
                required property var modelData
                text: modelData.header ? modelData.text : "    " + modelData.text
                font.bold: modelData.header
                enabled: !modelData.header && modelData.enabled
                checkable: !modelData.header
                checked: modelData.checked === true
                onTriggered: {
                    if (!modelData.header)
                        canvas.controller.setNotation(modelData.subject, modelData.group, modelData.value)
                }
            }
        }
    }

    Menu {
        id: backgroundMenu

        property point point
        property string frameId
        readonly property string frameLabel: frameId.length > 0 ? canvas.controller.kindLabel(frameId) : ""

        MenuItem {
            visible: backgroundMenu.frameId.length > 0
            height: visible ? implicitHeight : 0
            text: qsTr("Edit %1...").arg(backgroundMenu.frameLabel)
            onTriggered: canvas.editElement(backgroundMenu.frameId)
        }
        MenuItem {
            visible: backgroundMenu.frameId.length > 0 && canvas.controller.notationRevision >= 0
                     && canvas.controller.notationChoices(backgroundMenu.frameId).length > 0
            height: visible ? implicitHeight : 0
            text: qsTr("Notation...")
            onTriggered: canvas.openNotationMenu([backgroundMenu.frameId])
        }
        MenuSeparator {
            visible: backgroundMenu.frameId.length > 0
            height: visible ? implicitHeight : 0
        }

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
        onAnnotationChosen: (source, target) => {
            annotationMenu.source = source
            annotationMenu.target = target
            annotationMenu.targets = canvas.controller.annotationTargets(source, target)
            Qt.callLater(() => annotationMenu.popup(canvas, edgeKindMenu.x, edgeKindMenu.y))
        }
    }

    // a note is about the element or one of the features in its compartments ("about part1::attribute1", table 2)
    Menu {
        id: annotationMenu
        objectName: "annotationTargetMenu"
        property string source
        property string target
        property var targets: []

        MenuItem {
            text: qsTr("The comment is about")
            enabled: false
        }
        MenuSeparator {}
        Repeater {
            model: annotationMenu.targets
            MenuItem {
                required property var modelData
                required property int index
                text: index === 0 ? modelData.label + qsTr(" (the element itself)") : modelData.label
                onTriggered: canvas.controller.createAnnotation(annotationMenu.source, annotationMenu.target, modelData.path)
            }
        }
    }

    EdgeEditDialog {
        id: edgeDialog
        objectName: "edgeEditDialog"
        controller: canvas.controller
    }

    ElementEditDialog {
        id: editDialog
        objectName: "elementEditDialog"
        controller: canvas.controller
    }
}
