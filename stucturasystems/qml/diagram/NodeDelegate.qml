pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// One node of the diagram: «part» (rounded), «part def» (square corners) with its compartments (attributes,
// actions, constraints, functions, ...), a statement kept as text on package level («constraint def»,
// «function», ...), or a package drawn as frame with a title tab. The node only displays the row of the
// DiagramNodeModel; moving, selecting and arrows go through the controller.
Item {
    id: node

    required property string elementId
    required property int nodeKind
    required property string keyword
    required property string title
    /** [{title, entries: [{entryId, text, tooltip}]}] */
    required property var compartments
    required property real nodeX
    required property real nodeY
    required property real nodeWidth
    required property real nodeHeight
    required property int depth
    required property bool selected

    property DiagramController controller
    /** The DiagramCanvas (arrow drawing, menus, edit dialog). */
    property Item canvas
    /** Preview: only shown, no interaction. */
    property bool readOnly: false

    readonly property bool isFrame: nodeKind === DiagramTypes.PackageNode
    readonly property bool isPort: nodeKind === DiagramTypes.PortNode
    /** Border of the part the port sits on (for its label); re-read when the port or its part moves. */
    readonly property string portSide: isPort && controller ? (nodeX, nodeY, controller.portSide(elementId)) : "right"
    readonly property bool isDefinition: nodeKind === DiagramTypes.PartDefinitionNode
                                         || nodeKind === DiagramTypes.DefinitionNode
                                         || nodeKind === DiagramTypes.TextualNode
                                         || nodeKind === DiagramTypes.ConstraintDefinitionNode
                                         || nodeKind === DiagramTypes.CalcDefinitionNode
                                         || ((nodeKind === DiagramTypes.RequirementNode || nodeKind === DiagramTypes.CaseNode)
                                             && keyword.indexOf(" def»") >= 0)
    readonly property color lineColor: selected ? Theme.accent : Theme.textPrimary
    readonly property real padding: 8

    /** Attribute or action shown at a point in node coordinates, empty if none. */
    function entryAt(point) {
        for (let c = 0; c < compartmentRepeater.count; ++c) {
            const compartment = compartmentRepeater.itemAt(c)
            if (!compartment)
                continue
            for (let i = 0; i < compartment.entryRepeater.count; ++i) {
                const item = compartment.entryRepeater.itemAt(i)
                if (!item)
                    continue
                const local = node.mapToItem(item, point.x, point.y)
                if (item.contains(local))
                    return item.entryId
            }
        }
        return ""
    }

    /** Full text of the entry under point (tooltip), empty if none. */
    function tooltipAt(point) {
        for (let c = 0; c < compartmentRepeater.count; ++c) {
            const compartment = compartmentRepeater.itemAt(c)
            if (!compartment)
                continue
            for (let i = 0; i < compartment.entryRepeater.count; ++i) {
                const item = compartment.entryRepeater.itemAt(i)
                if (item && item.contains(node.mapToItem(item, point.x, point.y)))
                    return item.tooltip
            }
        }
        return ""
    }

    function reportSize() {
        if (!isFrame && !isPort && controller)
            controller.setNodeSize(elementId, width, height)
    }

    x: nodeX
    y: nodeY
    width: isFrame || isPort ? nodeWidth : Math.max(140, partContent.contentWidth + 2 * padding)
    height: isFrame || isPort ? nodeHeight : partContent.implicitHeight + padding
    z: isFrame ? depth : isPort ? 3000 + depth : 1000 + depth

    onWidthChanged: reportSize()
    onHeightChanged: reportSize()
    Component.onCompleted: reportSize()

    // ---------------------------------------------------------------- mouse handling
    // Shared by the part body and the frame grips: select, move, arrow, edit, context menu.
    component Grip: MouseArea {
        id: grip

        /** The NodeDelegate this grip belongs to (ids of the file are not visible in an inline component). */
        required property Item owner
        property point _start
        property point _origin
        property bool _dragging: false

        function worldPoint(mouse) {
            return grip.mapToItem(grip.owner.parent, mouse.x, mouse.y)
        }

        enabled: !grip.owner.readOnly
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        cursorShape: grip.owner.controller && grip.owner.controller.arrowMode ? Qt.CrossCursor
                     : _dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor

        onPressed: mouse => {
            grip.owner.canvas.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                const local = grip.mapToItem(grip.owner, mouse.x, mouse.y)
                if (!grip.owner.selected)
                    grip.owner.controller.select(grip.owner.elementId, false)
                grip.owner.canvas.openNodeMenu(grip.owner.elementId, grip.owner.entryAt(local))
                return
            }
            const point = worldPoint(mouse)
            if (grip.owner.controller.arrowMode) {
                grip.owner.canvas.beginArrow(grip.owner.elementId, point)
                return
            }
            _start = point
            _origin = Qt.point(grip.owner.nodeX, grip.owner.nodeY)
            _dragging = false
            const additive = (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
            if (additive || !grip.owner.selected)
                grip.owner.controller.select(grip.owner.elementId, additive)
        }
        onPositionChanged: mouse => {
            if (!(grip.pressedButtons & Qt.LeftButton))
                return
            const point = worldPoint(mouse)
            if (grip.owner.controller.arrowMode) {
                grip.owner.canvas.updateArrow(point)
                return
            }
            if (!_dragging && Math.abs(point.x - _start.x) + Math.abs(point.y - _start.y) < 4)
                return
            _dragging = true
            grip.owner.controller.moveNode(grip.owner.elementId, _origin.x + point.x - _start.x, _origin.y + point.y - _start.y)
        }
        onReleased: mouse => {
            if (mouse.button !== Qt.LeftButton)
                return
            if (grip.owner.controller.arrowMode) {
                grip.owner.canvas.finishArrow(worldPoint(mouse))
                return
            }
            if (_dragging)
                grip.owner.controller.finishMove(grip.owner.elementId)
            _dragging = false
        }
        onDoubleClicked: mouse => {
            if (mouse.button !== Qt.LeftButton || grip.owner.controller.arrowMode)
                return
            const entry = grip.owner.entryAt(grip.mapToItem(grip.owner, mouse.x, mouse.y))
            grip.owner.canvas.editElement(entry.length > 0 ? entry : grip.owner.elementId)
        }
    }

    // ---------------------------------------------------------------- part / part definition
    Rectangle {
        id: partBox

        visible: !node.isFrame && !node.isPort
        anchors.fill: parent
        radius: node.isDefinition ? 2 : 10
        color: Theme.card
        border.width: node.selected ? 2 : 1.2
        border.color: node.lineColor

        Column {
            id: partContent

            readonly property real contentWidth: {
                let width = Math.max(keywordLabel.implicitWidth, titleLabel.implicitWidth)
                for (let c = 0; c < compartmentRepeater.count; ++c) {
                    const item = compartmentRepeater.itemAt(c)
                    if (item)
                        width = Math.max(width, item.entryRepeater.parent.implicitWidth + node.padding)
                }
                return width
            }

            width: parent.width
            topPadding: node.padding / 2

            Label {
                id: keywordLabel
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: node.keyword
                font.pixelSize: 12
                color: Theme.textSecondary
            }
            Label {
                id: titleLabel
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: node.title
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                bottomPadding: 4
            }

            Repeater {
                id: compartmentRepeater
                model: node.compartments

                Column {
                    id: compartment

                    required property var modelData
                    property alias entryRepeater: entries

                    width: partContent.width

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Qt.alpha(Theme.textPrimary, 0.5)
                    }
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: compartment.modelData.title
                        font.pixelSize: 11
                        font.italic: true
                        color: Theme.textSecondary
                    }
                    Column {
                        x: node.padding
                        bottomPadding: 4

                        Repeater {
                            id: entries
                            model: compartment.modelData.entries

                            Label {
                                required property var modelData
                                readonly property string entryId: modelData.entryId
                                readonly property string tooltip: modelData.tooltip
                                text: modelData.text
                                textFormat: Text.PlainText
                                font.pixelSize: 13
                                color: Theme.textPrimary
                            }
                        }
                    }
                }
            }
        }

        // Tooltip with the complete text of long entries and of constraints/functions
        HoverHandler {
            id: entryHover
        }
        ToolTip {
            readonly property string text_: entryHover.hovered ? node.tooltipAt(entryHover.point.position) : ""
            visible: text_.length > 0 && !node.readOnly
            text: text_
            delay: 600
            font.family: Theme.monoFamily
        }

        Grip {
            owner: node
            anchors.fill: parent
        }
    }

    // ---------------------------------------------------------------- port on the border of its part
    Rectangle {
        visible: node.isPort
        anchors.fill: parent
        color: Theme.card
        border.width: node.selected ? 2 : 1.4
        border.color: node.lineColor

        Grip {
            owner: node
            anchors.fill: parent
            anchors.margins: -4
        }
    }
    Label {
        visible: node.isPort
        text: node.title
        textFormat: Text.PlainText
        font.pixelSize: 12
        color: Theme.textPrimary
        // outside the part, next to the square
        x: node.portSide === "left" ? -implicitWidth - 6 : node.portSide === "right" ? node.width + 6 : node.width / 2 - implicitWidth / 2
        y: node.portSide === "top" ? -implicitHeight - 2 : node.portSide === "bottom" ? node.height + 2 : -implicitHeight + 1
    }

    // ---------------------------------------------------------------- package frame
    Item {
        id: frame

        readonly property real tabWidth: Math.min(node.width, frameTitle.implicitWidth + 2 * node.padding + 12)
        readonly property real tabHeight: 26

        visible: node.isFrame
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            color: Qt.alpha(Theme.card, 0.35)
            border.width: node.selected ? 2 : 1.2
            border.color: node.lineColor
        }

        // Title tab with a cut corner (SysML frame notation)
        Canvas {
            id: tabShape

            width: frame.tabWidth
            height: frame.tabHeight
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = node.lineColor
                context.lineWidth = node.selected ? 2 : 1.2
                context.fillStyle = Theme.card
                const inset = context.lineWidth / 2
                context.beginPath()
                context.moveTo(inset, inset)
                context.lineTo(width - inset, inset)
                context.lineTo(width - inset, height - 9)
                context.lineTo(width - 9, height - inset)
                context.lineTo(inset, height - inset)
                context.closePath()
                context.fill()
                context.stroke()
            }
            onWidthChanged: requestPaint()
            Connections {
                target: node
                function onLineColorChanged() { tabShape.requestPaint() }
                function onSelectedChanged() { tabShape.requestPaint() }
            }
            Connections {
                target: Theme
                function onDarkChanged() { tabShape.requestPaint() }
            }
        }
        Label {
            id: frameTitle
            x: node.padding
            width: frame.tabWidth - 2 * node.padding - 6
            height: frame.tabHeight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: node.title
            font.pixelSize: 13
            font.weight: Font.Medium
            color: Theme.textPrimary
        }

        // Grips: title band and the border (the inside stays free for the content and the canvas).
        Grip {
            owner: node
            x: 0
            y: 0
            width: parent.width
            height: 30
        }
        Grip {
            owner: node
            x: -4
            y: 0
            width: 8
            height: parent.height
        }
        Grip {
            owner: node
            x: parent.width - 4
            y: 0
            width: 8
            height: parent.height
        }
        Grip {
            owner: node
            x: 0
            y: parent.height - 4
            width: parent.width
            height: 8
        }

        // Resize handle
        MouseArea {
            id: resizeHandle

            property point _start
            property size _origin

            x: parent.width - 16
            y: parent.height - 16
            width: 18
            height: 18
            preventStealing: true
            cursorShape: Qt.SizeFDiagCursor
            enabled: !node.readOnly && (!node.controller || !node.controller.arrowMode)
            visible: !node.readOnly

            onPressed: mouse => {
                _start = resizeHandle.mapToItem(node.parent, mouse.x, mouse.y)
                _origin = Qt.size(node.nodeWidth, node.nodeHeight)
            }
            onPositionChanged: mouse => {
                const point = resizeHandle.mapToItem(node.parent, mouse.x, mouse.y)
                node.controller.resizeFrame(node.elementId, _origin.width + point.x - _start.x,
                                            _origin.height + point.y - _start.y)
            }

            Canvas {
                anchors.fill: parent
                onPaint: {
                    const context = getContext("2d")
                    context.reset()
                    context.strokeStyle = node.lineColor
                    context.lineWidth = 1
                    context.beginPath()
                    context.moveTo(6, 14)
                    context.lineTo(14, 6)
                    context.moveTo(10, 14)
                    context.lineTo(14, 10)
                    context.stroke()
                }
            }
        }
    }
}
