pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// One node of the parametric view: a value (attribute), a call or expression computing a value, or a
// constraint / calculation with its parameters as pins on the border (inputs left, result right).
// Lines are drawn by dragging from a pin (or the small port of a value) onto another pin or value.
// Pin rows follow DiagramGeometry::PinTop / PinRowHeight, so the edge layer meets the pins exactly.
Item {
    id: node

    required property string elementId
    required property int nodeKind
    required property string keyword
    required property string title
    required property real nodeX
    required property real nodeY
    required property real nodeWidth
    required property real nodeHeight
    required property bool selected
    /** [{name, text, open}] */
    required property var inPins
    required property var outPins

    property ParametricController controller
    /** The ParametricCanvas (lines, menus, edit dialog). */
    property Item canvas

    readonly property real pinTop: 46
    readonly property real pinRow: 22
    readonly property bool isValue: nodeKind === DiagramTypes.ValueNode
    readonly property bool isComputation: nodeKind === DiagramTypes.CallNode || nodeKind === DiagramTypes.ExpressionNode
    readonly property color lineColor: selected ? Theme.accent : Theme.textPrimary
    /** y of the port of a value (pin -1), as DiagramGeometry::pinPoint */
    readonly property real portY: Math.min(height / 2, pinTop / 2 + 4)

    x: nodeX
    y: nodeY
    width: nodeWidth
    height: nodeHeight
    z: 10

    // A point where a line starts or ends: pin index (-1 = the value itself) and side.
    component Port: Item {
        id: port

        required property Item owner
        required property int pin
        required property bool output
        property bool open: false

        width: 18
        height: 18

        Rectangle {
            anchors.centerIn: parent
            width: 9
            height: 9
            radius: 4.5
            color: port.open ? Theme.card : (portArea.containsMouse ? Theme.accent : Theme.textPrimary)
            border.width: 1.4
            border.color: port.open ? Theme.warning : (portArea.containsMouse ? Theme.accent : Theme.textPrimary)
        }

        MouseArea {
            id: portArea

            function worldPoint(mouse) {
                return portArea.mapToItem(port.owner.parent, mouse.x, mouse.y)
            }

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.CrossCursor
            onPressed: mouse => {
                port.owner.canvas.forceActiveFocus()
                port.owner.canvas.beginLine(port.owner.elementId, port.pin, port.output, worldPoint(mouse))
            }
            onPositionChanged: mouse => port.owner.canvas.updateLine(worldPoint(mouse))
            onReleased: mouse => port.owner.canvas.finishLine(worldPoint(mouse))
        }

        ToolTip.visible: portArea.containsMouse && !portArea.pressed
        ToolTip.text: qsTr("Drag a line onto a parameter or value")
        ToolTip.delay: 900
    }

    Rectangle {
        anchors.fill: parent
        radius: node.isValue ? 4 : node.isComputation ? 14 : 8
        color: node.isComputation ? Qt.alpha(Theme.accent, Theme.dark ? 0.14 : 0.07) : Theme.card
        border.width: node.selected ? 2 : 1.2
        border.color: node.lineColor

        MouseArea {
            id: body

            property point _start
            property point _origin
            property bool _dragging: false

            function worldPoint(mouse) {
                return body.mapToItem(node.parent, mouse.x, mouse.y)
            }

            anchors.fill: parent
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
                const additive = (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
                if (additive || !node.selected)
                    node.controller.select(node.elementId, additive)
            }
            onPositionChanged: mouse => {
                if (!(body.pressedButtons & Qt.LeftButton))
                    return
                const point = worldPoint(mouse)
                if (!_dragging && Math.abs(point.x - _start.x) + Math.abs(point.y - _start.y) < 4)
                    return
                _dragging = true
                node.controller.moveNode(node.elementId, _origin.x + point.x - _start.x, _origin.y + point.y - _start.y)
            }
            onReleased: _dragging = false
            onDoubleClicked: mouse => {
                if (mouse.button === Qt.LeftButton)
                    node.canvas.editNode(node.elementId)
            }
        }

        Column {
            x: 8
            width: parent.width - 16
            topPadding: node.keyword.length > 0 ? 4 : 10
            spacing: 1

            Label {
                width: parent.width
                visible: node.keyword.length > 0
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: node.keyword
                font.pixelSize: 12
                color: Theme.textSecondary
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: node.title
                font.pixelSize: 14
                font.weight: node.isComputation ? Font.Normal : Font.DemiBold
                font.family: node.nodeKind === DiagramTypes.ExpressionNode ? Theme.monoFamily : font.family
                color: Theme.textPrimary
            }
        }

        // separator above the pins
        Rectangle {
            visible: node.inPins.length > 0 || node.outPins.length > 0
            y: node.pinTop - 4
            width: parent.width
            height: 1
            color: Qt.alpha(node.lineColor, 0.5)
        }
    }

    // ---------------------------------------------------------------- pins
    Repeater {
        model: node.inPins

        Item {
            id: inPin

            required property var modelData
            required property int index

            y: node.pinTop + index * node.pinRow
            width: node.width / 2
            height: node.pinRow

            Port {
                owner: node
                pin: inPin.index
                output: false
                open: inPin.modelData.open
                x: -width / 2
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: node.width - 24
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: inPin.modelData.text.length > 0 ? inPin.modelData.name + " = " + inPin.modelData.text
                                                      : inPin.modelData.name
                font.pixelSize: 12
                color: inPin.modelData.open ? Theme.warning : Theme.textPrimary
            }
        }
    }
    Repeater {
        model: node.outPins

        Item {
            id: outPin

            required property var modelData
            required property int index

            x: node.width / 2
            y: node.pinTop + index * node.pinRow
            width: node.width / 2
            height: node.pinRow

            Port {
                owner: node
                pin: outPin.index
                output: true
                x: parent.width - width / 2
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: outPin.modelData.name
                font.pixelSize: 12
                font.italic: true
                color: Theme.textPrimary
            }
        }
    }

    // A value: port for outgoing lines on the right, incoming on the left. An expression: outgoing only.
    Port {
        visible: node.isValue || node.nodeKind === DiagramTypes.ExpressionNode
        owner: node
        pin: -1
        output: true
        x: node.width - width / 2
        y: node.portY - height / 2
    }
    Port {
        visible: node.isValue
        owner: node
        pin: -1
        output: false
        x: -width / 2
        y: node.portY - height / 2
    }
}
