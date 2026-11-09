pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// One node of the behavior view: an action, a state (with its entry / do / exit actions), or a control node
// (decision / merge diamond, fork / join bar, start / initial circle, done). Arrows are drawn by dragging the
// small handle at the lower right onto another node.
Item {
    id: node

    required property string elementId
    required property int nodeKind
    required property string keyword
    required property string title
    /** [{title, entries: [{entryId, text}]}] - entry / do / exit actions of a state */
    required property var compartments
    required property real nodeX
    required property real nodeY
    required property real nodeWidth
    required property real nodeHeight
    required property bool selected
    /** Nesting depth: nested states lie on top of the state they are in. */
    required property int depth

    property BehaviorController controller
    /** The BehaviorCanvas (arrows, menus, dialogs). */
    property Item canvas

    readonly property color lineColor: selected ? Theme.accent : Theme.textPrimary
    readonly property bool isBox: nodeKind === DiagramTypes.ActionNode || nodeKind === DiagramTypes.StateNode
    /** A parameter as pin on the border of its action or of the frame: "in" on top, "out" below, "inout" right. */
    readonly property bool isPin: nodeKind === DiagramTypes.PortNode
    /** top / bottom / right of its action, left / right / bottom of the frame */
    readonly property string pinSide: isPin && controller ? controller.pinSide(elementId) : ""

    x: nodeX
    y: nodeY
    width: nodeWidth
    height: nodeHeight
    z: 10 + depth

    // ---------------------------------------------------------------- shapes
    // action / state: rounded box
    Rectangle {
        visible: node.isBox
        anchors.fill: parent
        radius: node.nodeKind === DiagramTypes.StateNode ? 14 : 10
        color: Theme.card
        border.width: node.selected ? 2 : 1.2
        border.color: node.lineColor

        // send / accept action: the flag of the graphical notation in the upper left corner
        Canvas {
            id: flag
            readonly property bool send: node.keyword === "«send action»"
            readonly property bool accept: node.keyword === "«accept action»"
            visible: send || accept
            x: 7
            y: 6
            width: 16
            height: 10
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.fillStyle = node.lineColor
                context.beginPath()
                if (send) { // a pentagon pointing to the right
                    context.moveTo(0, 0); context.lineTo(11, 0); context.lineTo(16, 5); context.lineTo(11, 10); context.lineTo(0, 10)
                } else {    // a rectangle with a notch on the right
                    context.moveTo(0, 0); context.lineTo(16, 0); context.lineTo(11, 5); context.lineTo(16, 10); context.lineTo(0, 10)
                }
                context.closePath()
                context.fill()
            }
            onVisibleChanged: requestPaint()
            Connections {
                target: node
                function onLineColorChanged() { flag.requestPaint() }
                function onKeywordChanged() { flag.requestPaint() }
            }
        }
        Column {
            x: 8
            width: parent.width - 16
            topPadding: node.keyword.length > 0 ? 3 : 10
            Label {
                width: parent.width
                visible: node.keyword.length > 0
                horizontalAlignment: Text.AlignHCenter
                text: node.keyword
                font.pixelSize: 11
                color: Theme.textSecondary
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: node.title
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
        }
        // "state actions" (entry / do / exit), "assert constraints": a line, the caption (16) and the entries (18 each)
        Column {
            y: 40 + (node.keyword.length > 0 ? 6 : 0)
            width: parent.width
            Repeater {
                model: node.compartments
                Column {
                    id: compartment
                    required property var modelData
                    width: node.width
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Qt.alpha(node.lineColor, 0.5)
                    }
                    Label {
                        width: parent.width
                        height: 15
                        horizontalAlignment: Text.AlignHCenter
                        text: compartment.modelData.title
                        font.pixelSize: 10
                        font.italic: true
                        color: Theme.textSecondary
                    }
                    Repeater {
                        model: compartment.modelData.entries
                        Label {
                            required property var modelData
                            x: 10
                            height: 18
                            text: modelData.text
                            textFormat: Text.PlainText
                            font.pixelSize: 12
                            color: Theme.textPrimary
                        }
                    }
                }
            }
        }
    }
    // decide / merge: diamond
    Rectangle {
        visible: node.nodeKind === DiagramTypes.DecisionNode
        anchors.centerIn: parent
        width: parent.width / Math.SQRT2
        height: parent.height / Math.SQRT2
        rotation: 45
        color: Theme.card
        border.width: node.selected ? 2 : 1.4
        border.color: node.lineColor
    }
    // fork / join: bar
    Rectangle {
        visible: node.nodeKind === DiagramTypes.ForkNode
        anchors.fill: parent
        radius: 2
        color: node.lineColor
    }
    // start / initial state: filled circle; done: circle with a dot
    Rectangle {
        visible: node.nodeKind === DiagramTypes.StartNode || node.nodeKind === DiagramTypes.DoneNode
        anchors.fill: parent
        radius: width / 2
        color: node.nodeKind === DiagramTypes.StartNode ? node.lineColor : Theme.card
        border.width: 1.6
        border.color: node.lineColor
        Rectangle {
            visible: node.nodeKind === DiagramTypes.DoneNode
            anchors.centerIn: parent
            width: parent.width - 10
            height: width
            radius: width / 2
            color: node.lineColor
        }
    }
    // terminate: a circle with a cross
    Canvas {
        id: terminateShape
        visible: node.nodeKind === DiagramTypes.TerminateNode
        anchors.fill: parent
        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.strokeStyle = node.lineColor
            context.lineWidth = 1.6
            context.fillStyle = Theme.card
            const r = width / 2 - 1
            context.beginPath()
            context.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
            context.fill()
            context.stroke()
            const d = r * Math.SQRT1_2
            context.beginPath()
            context.moveTo(width / 2 - d, height / 2 - d); context.lineTo(width / 2 + d, height / 2 + d)
            context.moveTo(width / 2 + d, height / 2 - d); context.lineTo(width / 2 - d, height / 2 + d)
            context.stroke()
        }
        onVisibleChanged: requestPaint()
        Connections {
            target: node
            function onLineColorChanged() { terminateShape.requestPaint() }
        }
    }
    // parameter pin (graphical BNF: param-t / param-b / param-r) with its direction: into the action from above
    // ("in"), out of it below ("out"), both ways on the right ("inout")
    Rectangle {
        visible: node.isPin
        anchors.fill: parent
        color: Theme.card
        border.width: node.selected ? 2 : 1.3
        border.color: node.lineColor
        Canvas {
            id: pinArrow
            anchors.fill: parent
            anchors.margins: 2
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = node.lineColor
                context.lineWidth = 1.1
                const cx = width / 2
                const cy = height / 2
                const half = width / 2 - 0.5
                const head = Math.min(3.5, half)
                // the arrow points into the action for "in", out of it for "out", both ways for "inout"; on the frame
                // of the context its parameters lead into / out of the frame
                const side = node.pinSide
                const horizontal = side === "left" || side === "right"
                // inwards: down on the top border, up on the bottom, right on the left border, left on the right
                const inwards = side === "top" || side === "left" ? 1 : -1
                const sign = node.keyword === "out" ? -inwards : inwards
                const both = node.keyword === "inout"
                context.beginPath()
                if (horizontal) {
                    context.moveTo(cx - half, cy); context.lineTo(cx + half, cy)
                    const tip = cx + sign * half
                    context.moveTo(tip - sign * head, cy - head); context.lineTo(tip, cy); context.lineTo(tip - sign * head, cy + head)
                    if (both) {
                        const back = cx - sign * half
                        context.moveTo(back + sign * head, cy - head); context.lineTo(back, cy); context.lineTo(back + sign * head, cy + head)
                    }
                } else {
                    context.moveTo(cx, cy - half); context.lineTo(cx, cy + half)
                    const tip = cy + sign * half
                    context.moveTo(cx - head, tip - sign * head); context.lineTo(cx, tip); context.lineTo(cx + head, tip - sign * head)
                    if (both) {
                        const back = cy - sign * half
                        context.moveTo(cx - head, back + sign * head); context.lineTo(cx, back); context.lineTo(cx + head, back + sign * head)
                    }
                }
                context.stroke()
            }
            onVisibleChanged: requestPaint()
            Connections {
                target: node
                function onLineColorChanged() { pinArrow.requestPaint() }
                function onKeywordChanged() { pinArrow.requestPaint() }
                function onPinSideChanged() { pinArrow.requestPaint() }
            }
        }
    }
    Label {
        objectName: "pinLabel"
        visible: node.isPin
        text: node.title
        textFormat: Text.PlainText
        font.pixelSize: 11
        color: Theme.textPrimary
        leftPadding: 2
        rightPadding: 2
        // beside the pin, outside the action: above a pin on the top border, below one at the bottom, beside one on
        // the left / right (on the frame: inside it, below the pin)
        x: node.pinSide === "left" ? node.width + 3 : node.pinSide === "right" ? (node.width - implicitWidth) - node.width - 3 : node.width + 2
        y: node.pinSide === "top" ? -height + 2 : node.pinSide === "bottom" ? node.height - 2
           : node.pinSide === "left" ? node.height : node.height
        background: Rectangle {
            color: Qt.alpha(Theme.card, 0.85)
        }
    }

    // name of a control node next to it
    Label {
        visible: !node.isBox && !node.isPin && node.title.length > 0
        x: node.width + 6
        y: node.height / 2 - height / 2
        text: node.title
        font.pixelSize: 11
        color: Theme.textSecondary
    }

    // ---------------------------------------------------------------- mouse
    MouseArea {
        id: body

        property point _start
        property point _origin
        property bool _dragging: false

        function worldPoint(mouse) {
            return body.mapToItem(node.parent, mouse.x, mouse.y)
        }

        anchors.fill: parent
        anchors.margins: node.nodeKind === DiagramTypes.ForkNode || node.isPin ? -4 : 0
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        cursorShape: _dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor

        onPressed: mouse => {
            node.canvas.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                if (!node.selected)
                    node.controller.select(node.elementId, false)
                node.canvas.openNodeMenu(node.elementId)
                return
            }
            _start = worldPoint(mouse)
            _origin = Qt.point(node.nodeX, node.nodeY)
            _dragging = false
            if (node.isPin) {
                // a pin stays where it is: dragging from it draws a flow to another pin
                node.controller.select(node.elementId, false)
                node.canvas.beginArrow(node.elementId, _start)
                return
            }
            const additive = (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
            if (additive || !node.selected)
                node.controller.select(node.elementId, additive)
        }
        onPositionChanged: mouse => {
            if (!(body.pressedButtons & Qt.LeftButton))
                return
            const point = worldPoint(mouse)
            if (node.isPin) {
                node.canvas.updateArrow(point)
                return
            }
            if (!_dragging && Math.abs(point.x - _start.x) + Math.abs(point.y - _start.y) < 4)
                return
            _dragging = true
            node.controller.moveNode(node.elementId, _origin.x + point.x - _start.x, _origin.y + point.y - _start.y)
        }
        onReleased: mouse => {
            _dragging = false
            if (node.isPin && mouse.button === Qt.LeftButton)
                node.canvas.finishArrow(worldPoint(mouse))
        }
        onDoubleClicked: mouse => {
            if (mouse.button !== Qt.LeftButton)
                return
            const point = worldPoint(mouse)
            node.canvas.editNode(node.elementId, point.x, point.y)
        }
    }

    // handle for new arrows (lower right corner)
    Rectangle {
        id: handle
        objectName: "arrowHandle"
        width: 12
        height: 12
        radius: 6
        x: node.width - 2
        y: node.height - 2
        color: handleArea.containsMouse || handleArea.pressed ? Theme.accent : Theme.card
        border.width: 1.4
        border.color: Theme.accent
        visible: !node.isPin && (node.selected || handleArea.containsMouse || hover.hovered)

        MouseArea {
            id: handleArea

            function worldPoint(mouse) {
                return handleArea.mapToItem(node.parent, mouse.x, mouse.y)
            }

            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.CrossCursor
            onPressed: mouse => node.canvas.beginArrow(node.elementId, worldPoint(mouse))
            onPositionChanged: mouse => node.canvas.updateArrow(worldPoint(mouse))
            onReleased: mouse => node.canvas.finishArrow(worldPoint(mouse))
        }
        ToolTip.visible: handleArea.containsMouse && !handleArea.pressed
        ToolTip.text: qsTr("Drag onto another node: succession / transition")
        ToolTip.delay: 800
    }
    HoverHandler {
        id: hover
    }
}
