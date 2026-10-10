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

    property BehaviorController controller
    /** The BehaviorCanvas (arrows, menus, dialogs). */
    property Item canvas

    readonly property color lineColor: selected ? Theme.accent : Theme.textPrimary
    readonly property bool isBox: nodeKind === DiagramTypes.ActionNode || nodeKind === DiagramTypes.StateNode

    x: nodeX
    y: nodeY
    width: nodeWidth
    height: nodeHeight
    z: 10

    // ---------------------------------------------------------------- shapes
    // action / state: rounded box
    Rectangle {
        visible: node.isBox
        anchors.fill: parent
        radius: node.nodeKind === DiagramTypes.StateNode ? 14 : 10
        color: Theme.card
        border.width: node.selected ? 2 : 1.2
        border.color: node.lineColor

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
        Rectangle {
            visible: node.compartments.length > 0
            y: 40 + (node.keyword.length > 0 ? 6 : 0)
            width: parent.width
            height: 1
            color: Qt.alpha(node.lineColor, 0.5)
        }
        Column {
            x: 10
            y: 44 + (node.keyword.length > 0 ? 6 : 0)
            Repeater {
                model: node.compartments.length > 0 ? node.compartments[0].entries : []
                Label {
                    required property var modelData
                    height: 18
                    text: modelData.text
                    textFormat: Text.PlainText
                    font.pixelSize: 12
                    color: Theme.textPrimary
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
    // name of a control node next to it
    Label {
        visible: !node.isBox && node.title.length > 0
        x: node.width + 6
        y: node.height / 2 - height / 2
        text: node.keyword + " " + node.title
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
        anchors.margins: node.nodeKind === DiagramTypes.ForkNode ? -6 : 0
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
        visible: node.selected || handleArea.containsMouse || hover.hovered

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
