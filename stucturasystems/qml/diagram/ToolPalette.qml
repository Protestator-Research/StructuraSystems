pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Palette on the right of the editor: elements are dragged onto the canvas (or clicked: parts appear in the
// middle of the view, attributes/actions go to the selected part), plus the arrow tool with its preset.
Pane {
    id: palette

    property DiagramController controller
    /** Parametric view: values, constraints, calculations and the definitions of the model. */
    property bool parametric: false
    /** [{id, name, keyword, calc}] (ParametricController.definitions) */
    property var definitions: []

    /** A tile was dragged and released at a scene (window) position. */
    signal toolDropped(int tool, real sceneX, real sceneY)
    /** A tile was clicked without dragging. */
    signal toolClicked(int tool)
    /** A definition tile of the parametric view was dropped / clicked. */
    signal definitionDropped(string definitionId, real sceneX, real sceneY)
    signal definitionClicked(string definitionId)

    padding: 12
    Material.background: Theme.panel

    // One draggable entry of the palette.
    component Tile: Rectangle {
        id: tile

        required property Item owner
        required property int tool
        required property string label
        /** 0 part, 1 part def, 2 package, 3 attribute, 4 action, 5 constraint, 6 calc, 7 constraint def, 8 calc def */
        required property int symbol
        /** Parametric view: a constraint / calc definition of the model instead of a tool. */
        property string definitionId: ""
        /** Content of a part (or of a constraint / calculation), not an element of its own. */
        readonly property bool feature: tool === DiagramTypes.ToolAttribute || tool === DiagramTypes.ToolAction
                                        || tool === DiagramTypes.ToolConstraint || tool === DiagramTypes.ToolCalc

        property point _start
        property bool _dragging: false

        objectName: "paletteTile" + tool
        Layout.fillWidth: true
        implicitHeight: 40
        radius: 8
        color: tileArea.containsMouse ? Qt.alpha(Theme.textPrimary, 0.06) : "transparent"
        border.width: 1
        border.color: Theme.outline

        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: 10
            spacing: 10

            // Small symbol of the element
            Item {
                width: 30
                height: 20
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    visible: tile.symbol <= 2 || tile.symbol === 7 || tile.symbol === 8
                    anchors.fill: parent
                    radius: tile.symbol === 0 ? 5 : 0
                    color: tile.symbol === 2 ? "transparent" : Theme.card
                    border.width: 1.2
                    border.color: Theme.textPrimary
                }
                Rectangle {
                    visible: tile.symbol === 2
                    width: 14
                    height: 7
                    color: Theme.card
                    border.width: 1.2
                    border.color: Theme.textPrimary
                }
                Label {
                    visible: tile.symbol >= 3
                    anchors.centerIn: parent
                    text: tile.symbol === 3 ? "a:" : tile.symbol === 4 ? "▶"
                          : (tile.symbol === 5 || tile.symbol === 7) ? "{ }" : tile.symbol === 9 ? "T" : "ƒ"
                    font.italic: tile.symbol === 6 || tile.symbol === 8
                    font.pixelSize: tile.symbol >= 7 ? 11 : 14
                    font.weight: Font.DemiBold
                    color: Theme.accent
                }
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: tile.label
                color: Theme.textPrimary
            }
        }

        MouseArea {
            id: tileArea

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: tile._dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor

            onPressed: mouse => {
                tile._start = tileArea.mapToItem(null, mouse.x, mouse.y)
                tile._dragging = false
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const scene = tileArea.mapToItem(null, mouse.x, mouse.y)
                if (!tile._dragging && Math.abs(scene.x - tile._start.x) + Math.abs(scene.y - tile._start.y) < 6)
                    return
                tile._dragging = true
                const ghost = tile.owner.ghost
                const local = tileArea.mapToItem(ghost.parent, mouse.x, mouse.y)
                ghost.label = tile.label
                ghost.x = local.x - ghost.width / 2
                ghost.y = local.y - ghost.height / 2
                ghost.visible = true
            }
            onReleased: mouse => {
                tile.owner.ghost.visible = false
                const scene = tileArea.mapToItem(null, mouse.x, mouse.y)
                if (tile._dragging && tile.definitionId.length > 0)
                    tile.owner.definitionDropped(tile.definitionId, scene.x, scene.y)
                else if (tile._dragging)
                    tile.owner.toolDropped(tile.tool, scene.x, scene.y)
                else if (containsMouse && tile.definitionId.length > 0)
                    tile.owner.definitionClicked(tile.definitionId)
                else if (containsMouse)
                    tile.owner.toolClicked(tile.tool)
                tile._dragging = false
            }
            onCanceled: {
                tile.owner.ghost.visible = false
                tile._dragging = false
            }
        }

        ToolTip.visible: tileArea.containsMouse && !tile._dragging
        ToolTip.text: tile.definitionId.length > 0
                      ? (tile.symbol === 8 ? qsTr("Drop onto the canvas for a new computed value, or onto a value to compute it")
                                           : qsTr("Drop onto the canvas: a constraint of this definition"))
                      : tile.owner.parametric && tile.tool === DiagramTypes.ToolAttribute
                      ? qsTr("Drag onto the canvas for a new value, or onto a constraint / calculation for a new parameter")
                      : tile.owner.parametric
                      ? qsTr("Drag onto the canvas, or click to insert")
                      : tile.tool === DiagramTypes.ToolStatement
                      ? qsTr("Any SysML v2 statement written as text (requirement, port def, ...): drop onto the canvas or a part")
                      : tile.feature
                      ? (tile.tool === DiagramTypes.ToolAction
                         ? qsTr("Drag onto a part, or click to add to the selected part")
                         : qsTr("Drag onto a part, constraint or calculation, or click to add to the selected one"))
                      : qsTr("Drag onto the canvas, or click to insert")
        ToolTip.delay: 900
    }

    /** Preview that follows the mouse while a tile is dragged. */
    property Item ghost: Rectangle {
        property string label

        parent: palette.Window.window ? palette.Window.window.contentItem : palette
        z: 10000
        visible: false
        width: 120
        height: 40
        radius: 8
        color: Qt.alpha(Theme.card, 0.9)
        border.width: 1.5
        border.color: Theme.accent
        opacity: 0.9

        Label {
            anchors.centerIn: parent
            text: parent.label
            color: Theme.textPrimary
        }
    }

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroller.availableWidth
            spacing: 8

            Label {
                visible: !palette.parametric
                text: qsTr("Relationship")
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textSecondary
            }
            // Arrow tool (toggle)
            Rectangle {
                id: arrowTool

                readonly property bool on: palette.controller ? palette.controller.arrowMode : false

                objectName: "arrowButton"
                visible: !palette.parametric
                Layout.fillWidth: true
                implicitHeight: 40
                radius: 8
                color: on ? Qt.alpha(Theme.accent, Theme.dark ? 0.28 : 0.16)
                          : arrowArea.containsMouse ? Qt.alpha(Theme.textPrimary, 0.06) : "transparent"
                border.width: on ? 2 : 1
                border.color: on ? Theme.accent : Theme.outline
                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Arrow")
                Accessible.checked: on

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 10
                    spacing: 10

                    Label {
                        width: 30
                        horizontalAlignment: Text.AlignHCenter
                        text: "\u2192"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: Theme.accent
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: arrowTool.on ? qsTr("Arrow (on)") : qsTr("Arrow")
                        font.weight: arrowTool.on ? Font.DemiBold : Font.Normal
                        color: Theme.textPrimary
                    }
                }
                MouseArea {
                    id: arrowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: palette.controller.arrowMode = !palette.controller.arrowMode
                }
                ToolTip.visible: arrowArea.containsMouse
                ToolTip.text: qsTr("Arrow mode: drag from one element to another.\nEsc leaves the arrow mode.")
                ToolTip.delay: 700
            }
            Label {
                visible: !palette.parametric
                text: qsTr("New arrows")
                font.pixelSize: 11
                color: Theme.textSecondary
            }
            ComboBox {
                visible: !palette.parametric
                Layout.fillWidth: true
                model: [qsTr("Ask"), qsTr("Composition"), qsTr("Connect"), qsTr("Flow"), qsTr("Specialization")]
                currentIndex: palette.controller ? palette.controller.arrowDefault : 0
                onActivated: index => palette.controller.arrowDefault = index
                Accessible.name: qsTr("Kind of new arrows")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Ask on release, or always create this kind of relationship")
                ToolTip.delay: 700
            }

            Label {
                visible: !palette.parametric
                Layout.topMargin: 8
                text: qsTr("Elements")
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textSecondary
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolPart
                visible: !palette.parametric
                label: qsTr("Part")
                symbol: 0
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolPartDefinition
                visible: !palette.parametric
                label: qsTr("Part def")
                symbol: 1
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolPackage
                visible: !palette.parametric
                label: qsTr("Package")
                symbol: 2
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolConstraintDefinition
                visible: !palette.parametric
                label: qsTr("Constraint def")
                symbol: 7
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolCalcDefinition
                visible: !palette.parametric
                label: qsTr("Calc def")
                symbol: 8
            }

            Label {
                Layout.topMargin: palette.parametric ? 0 : 8
                text: palette.parametric ? qsTr("Elements") : qsTr("Content")
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textSecondary
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolAttribute
                label: palette.parametric ? qsTr("Value") : qsTr("Attribute")
                symbol: 3
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolAction
                visible: !palette.parametric
                label: qsTr("Action")
                symbol: 4
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolConstraint
                label: qsTr("Constraint")
                symbol: 5
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolCalc
                label: qsTr("Calc")
                symbol: 6
            }
            Tile {
                owner: palette
                tool: DiagramTypes.ToolStatement
                visible: !palette.parametric
                label: qsTr("Text")
                symbol: 9
            }

            // Parametric view: the definitions of the model
            Label {
                visible: palette.parametric
                Layout.topMargin: 8
                text: qsTr("Definitions")
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textSecondary
            }
            Repeater {
                model: palette.parametric ? palette.definitions : []

                Tile {
                    required property var modelData
                    owner: palette
                    tool: -1
                    definitionId: modelData.id
                    label: modelData.name
                    symbol: modelData.calc ? 8 : 7
                }
            }
            Label {
                visible: palette.parametric && palette.definitions.length === 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Constraint and calc definitions of the model appear here.")
                font.pixelSize: 11
                color: Theme.textSecondary
            }

            Label {
                Layout.topMargin: 8
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: palette.parametric
                      ? qsTr("Lines: drag from the dot of a value onto a parameter (or another value). "
                             + "Double-click edits, Delete removes the selection.")
                      : qsTr("Double-click edits, Delete removes the selection, Ctrl + wheel zooms.")
                font.pixelSize: 11
                color: Theme.textSecondary
            }
        }
    }
}
