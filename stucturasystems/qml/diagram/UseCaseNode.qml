pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// One node of the use case view: a use case (ellipse), an actor (stick figure with its name below) or the system
// boundary (a frame with the subject as title, drawn behind its use cases). Lines are drawn by dragging the small
// handle at the lower right onto another node.
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

    property UseCaseController controller
    /** The UseCaseCanvas (lines, menus, dialogs). */
    property Item canvas

    readonly property color lineColor: selected ? Theme.accent : Theme.textPrimary
    readonly property bool isUseCase: nodeKind === DiagramTypes.UseCaseNode
    readonly property bool isActor: nodeKind === DiagramTypes.ActorNode
    readonly property bool isBoundary: nodeKind === DiagramTypes.BoundaryNode

    x: nodeX
    y: nodeY
    width: nodeWidth
    height: nodeHeight
    z: isBoundary ? 1 : 10

    // ---------------------------------------------------------------- shapes
    // system boundary: frame with the subject as title
    Rectangle {
        visible: node.isBoundary
        anchors.fill: parent
        color: Qt.alpha(Theme.card, 0.35)
        border.width: node.selected ? 2 : 1.2
        border.color: node.lineColor

        Row {
            x: 10
            y: 8
            spacing: 6
            Label {
                text: node.keyword
                font.pixelSize: 11
                color: Theme.textSecondary
                anchors.baseline: boundaryTitle.baseline
            }
            Label {
                id: boundaryTitle
                text: node.title
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
        }
    }
    // use case: ellipse with keyword and name
    Rectangle {
        visible: node.isUseCase
        anchors.fill: parent
        radius: height / 2
        color: Theme.card
        border.width: node.selected ? 2 : 1.2
        border.color: node.lineColor

        Column {
            anchors.centerIn: parent
            width: parent.width - 36
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: node.keyword
                font.pixelSize: 10
                color: Theme.textSecondary
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: node.title
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
        }
    }
    // actor: stick figure, name below
    Canvas {
        id: figure

        visible: node.isActor
        width: 40
        height: 62
        anchors.horizontalCenter: parent.horizontalCenter
        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.strokeStyle = node.lineColor
            context.lineWidth = node.selected ? 2 : 1.6
            const c = width / 2
            context.beginPath()
            context.arc(c, 9, 7, 0, 2 * Math.PI)
            context.moveTo(c, 16)
            context.lineTo(c, 38)
            context.moveTo(c - 14, 24)
            context.lineTo(c + 14, 24)
            context.moveTo(c, 38)
            context.lineTo(c - 12, 58)
            context.moveTo(c, 38)
            context.lineTo(c + 12, 58)
            context.stroke()
        }
        Connections {
            target: node
            function onLineColorChanged() { figure.requestPaint() }
        }
    }
    Label {
        visible: node.isActor
        y: 64
        width: node.width
        horizontalAlignment: Text.AlignHCenter
        text: node.title
        font.pixelSize: 12
        font.weight: Font.Medium
        color: Theme.textPrimary
    }

    // ---------------------------------------------------------------- mouse
    // the boundary only at its title band and border, so the canvas inside stays reachable
    component Grip: MouseArea {
        id: grip

        /** The UseCaseNode this grip belongs to (ids of the file are not visible in an inline component). */
        required property Item owner
        property point _start
        property point _origin
        property bool _dragging: false

        function worldPoint(mouse) {
            return grip.mapToItem(grip.owner.parent, mouse.x, mouse.y)
        }

        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        cursorShape: _dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor

        onPressed: mouse => {
            grip.owner.canvas.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                if (!grip.owner.selected)
                    grip.owner.controller.select(grip.owner.elementId, false)
                grip.owner.canvas.openNodeMenu(grip.owner.elementId)
                return
            }
            _start = worldPoint(mouse)
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
            if (!_dragging && Math.abs(point.x - _start.x) + Math.abs(point.y - _start.y) < 4)
                return
            _dragging = true
            grip.owner.controller.moveNode(grip.owner.elementId, _origin.x + point.x - _start.x,
                                           _origin.y + point.y - _start.y)
        }
        onReleased: _dragging = false
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                grip.owner.canvas.editNode(grip.owner.elementId)
        }
    }

    Grip {
        owner: node
        visible: !node.isBoundary
        anchors.fill: parent
    }
    Grip {
        owner: node
        visible: node.isBoundary
        width: parent.width
        height: 30
    }
    Grip {
        owner: node
        visible: node.isBoundary
        x: -4
        width: 8
        height: parent.height
    }
    Grip {
        owner: node
        visible: node.isBoundary
        x: parent.width - 4
        width: 8
        height: parent.height
    }
    Grip {
        owner: node
        visible: node.isBoundary
        y: parent.height - 4
        width: parent.width
        height: 8
    }

    // handle for new lines (lower right corner)
    Rectangle {
        id: handle
        objectName: "lineHandle"
        width: 12
        height: 12
        radius: 6
        x: node.isUseCase ? node.width * 0.85 : node.width - 6
        y: node.isUseCase ? node.height * 0.85 : node.isActor ? 52 : node.height - 6
        color: handleArea.containsMouse || handleArea.pressed ? Theme.accent : Theme.card
        border.width: 1.4
        border.color: Theme.accent
        visible: (node.isUseCase || node.isActor || node.isBoundary)
                 && (node.selected || handleArea.containsMouse || hover.hovered)

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
            onPressed: mouse => node.canvas.beginLine(node.elementId, worldPoint(mouse))
            onPositionChanged: mouse => node.canvas.updateLine(worldPoint(mouse))
            onReleased: mouse => node.canvas.finishLine(worldPoint(mouse))
        }
        ToolTip.visible: handleArea.containsMouse && !handleArea.pressed
        ToolTip.text: node.isActor ? qsTr("Drag onto a use case: the actor takes part in it")
                      : node.isBoundary ? qsTr("Drag onto a use case: it belongs to this system")
                      : qsTr("Drag onto a use case («include»), an actor, or a boundary")
        ToolTip.delay: 800
    }
    HoverHandler {
        id: hover
        enabled: !node.isBoundary
    }
}
