pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// One node of the sequence view: a lifeline (header with its dashed line), the header of a part whose parts are
// the lifelines below it, or an event occurrence (a dot on its lifeline). Messages are drawn by dragging from a
// lifeline or an event onto another lifeline; Shift + drag moves an event, dragging a header moves the lifeline.
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

    property SequenceController controller
    /** The SequenceCanvas (arrows, menus, dialogs). */
    property Item canvas

    readonly property color lineColor: selected ? Theme.accent : Theme.textPrimary
    readonly property bool isLifeline: nodeKind === DiagramTypes.LifelineNode
    readonly property bool isGroup: nodeKind === DiagramTypes.LifelineGroupNode
    readonly property bool isEvent: nodeKind === DiagramTypes.EventNode
    /** The end of a message between whole lifelines: nothing to show. */
    readonly property bool isEnd: isEvent && nodeWidth < 8
    readonly property real headerHeight: 44

    x: nodeX
    y: nodeY
    width: nodeWidth
    height: nodeHeight
    z: isEvent ? 20 : isGroup ? 5 : 2
    visible: !isEnd

    // ---------------------------------------------------------------- shapes
    // header of a lifeline (or of a part with lifelines below it)
    Rectangle {
        id: header
        visible: node.isLifeline || node.isGroup
        width: parent.width
        height: node.headerHeight
        radius: 4
        color: node.isGroup ? Qt.alpha(Theme.card, 0.6) : Theme.card
        border.width: node.selected ? 2 : 1.2
        border.color: node.lineColor

        Column {
            anchors.centerIn: parent
            width: parent.width - 12
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: node.keyword
                font.pixelSize: 10
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: node.title
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                elide: Text.ElideRight
            }
        }
    }
    // the dashed line of a lifeline
    Canvas {
        id: line
        visible: node.isLifeline
        x: node.width / 2 - 2
        y: node.headerHeight
        width: 4
        height: Math.max(0, node.height - node.headerHeight)
        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.strokeStyle = node.selected ? Theme.accent : Theme.textSecondary
            context.lineWidth = node.selected ? 2 : 1.2
            context.setLineDash([6, 5])
            context.beginPath()
            context.moveTo(2, 0)
            context.lineTo(2, height)
            context.stroke()
        }
        onHeightChanged: requestPaint()
        Connections {
            target: node
            function onSelectedChanged() { line.requestPaint() }
        }
        Connections {
            target: Theme
            function onDarkChanged() { line.requestPaint() }
        }
    }
    // an event: a dot with its name on the right
    Rectangle {
        visible: node.isEvent
        anchors.fill: parent
        radius: width / 2
        color: node.selected ? Theme.accent : Theme.textPrimary
        border.width: node.selected ? 2 : 0
        border.color: Theme.accent
    }
    Label {
        visible: node.isEvent && node.title.length > 0
        x: node.width + 5
        y: node.height + 1
        text: node.title
        font.pixelSize: 11
        color: Theme.textSecondary
    }

    // ---------------------------------------------------------------- mouse
    // header: select, double-click edits, drag moves the lifeline left / right
    MouseArea {
        id: headerArea

        property point _start
        property bool _dragging: false

        visible: node.isLifeline || node.isGroup
        width: parent.width
        height: node.headerHeight
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        cursorShape: _dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor

        onPressed: mouse => {
            node.canvas.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                node.controller.select(node.elementId, false)
                node.canvas.openNodeMenu(node.elementId)
                return
            }
            _start = headerArea.mapToItem(node.parent, mouse.x, mouse.y)
            _dragging = false
            node.controller.select(node.elementId, (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0)
        }
        onPositionChanged: mouse => {
            if (!(headerArea.pressedButtons & Qt.LeftButton))
                return
            const point = headerArea.mapToItem(node.parent, mouse.x, mouse.y)
            if (!_dragging && Math.abs(point.x - _start.x) < 6)
                return
            _dragging = true
            node.x = node.nodeX + point.x - _start.x
        }
        onReleased: mouse => {
            if (!_dragging)
                return
            _dragging = false
            const point = headerArea.mapToItem(node.parent, mouse.x, mouse.y)
            const middle = node.nodeX + node.width / 2 + point.x - _start.x
            node.x = Qt.binding(() => node.nodeX)
            node.controller.moveLifeline(node.elementId, middle)
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                node.canvas.editElement(node.elementId)
        }
    }
    // the line: dragging from it draws a message (an event is added where it starts)
    MouseArea {
        id: lineArea

        visible: node.isLifeline
        x: node.width / 2 - 10
        y: node.headerHeight
        width: 20
        height: Math.max(0, node.height - node.headerHeight)
        preventStealing: true
        cursorShape: Qt.CrossCursor

        onPressed: mouse => {
            node.canvas.forceActiveFocus()
            node.canvas.beginArrow(lineArea.mapToItem(node.parent, mouse.x, mouse.y))
        }
        onPositionChanged: mouse => node.canvas.updateArrow(lineArea.mapToItem(node.parent, mouse.x, mouse.y))
        onReleased: mouse => node.canvas.finishArrow(lineArea.mapToItem(node.parent, mouse.x, mouse.y))
    }
    // an event: drag draws a message / "then", Shift + drag moves it up or down, double-click edits
    MouseArea {
        id: eventArea

        property real _startY
        property bool _moving: false

        visible: node.isEvent && !node.isEnd
        anchors.fill: parent
        anchors.margins: -6
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        hoverEnabled: true
        cursorShape: _moving ? Qt.SizeVerCursor : Qt.CrossCursor

        onPressed: mouse => {
            node.canvas.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                node.controller.select(node.elementId, false)
                node.canvas.openNodeMenu(node.elementId)
                return
            }
            node.controller.select(node.elementId, (mouse.modifiers & Qt.ControlModifier) !== 0)
            const point = eventArea.mapToItem(node.parent, mouse.x, mouse.y)
            _moving = (mouse.modifiers & Qt.ShiftModifier) !== 0
            _startY = point.y
            if (!_moving)
                node.canvas.beginArrow(Qt.point(node.nodeX + node.width / 2, node.nodeY + node.height / 2))
        }
        onPositionChanged: mouse => {
            if (!(eventArea.pressedButtons & Qt.LeftButton))
                return
            const point = eventArea.mapToItem(node.parent, mouse.x, mouse.y)
            if (_moving)
                node.y = node.nodeY + point.y - _startY
            else
                node.canvas.updateArrow(point)
        }
        onReleased: mouse => {
            if (mouse.button !== Qt.LeftButton)
                return
            const point = eventArea.mapToItem(node.parent, mouse.x, mouse.y)
            if (_moving) {
                _moving = false
                node.y = Qt.binding(() => node.nodeY)
                if (Math.abs(point.y - _startY) > 6)
                    node.controller.moveEvent(node.elementId, node.nodeY + node.height / 2 + point.y - _startY)
                return
            }
            node.canvas.finishArrow(point)
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton) {
                node.canvas.cancelArrow()
                node.canvas.editElement(node.elementId)
            }
        }
        ToolTip.visible: containsMouse && !pressed
        ToolTip.text: qsTr("Drag onto another lifeline: message (or \"then\", see palette). Shift + drag: move up / down")
        ToolTip.delay: 900
    }
}
