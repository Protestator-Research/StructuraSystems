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
    /** Behavior view: actions and control nodes, or states (stateMachine). */
    property bool behavior: false
    property bool stateMachine: false
    /** Use case view: use cases, actors, the subject (system boundary). */
    property bool useCase: false
    /** Sequence view: lifelines, events; messages / "then" are drawn on the canvas. */
    property bool sequence: false
    property SequenceController sequenceController
    readonly property bool structure: !parametric && !behavior && !useCase && !sequence
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
        /** Short text inside the symbol of a definition (a, e, i, p, if) */
        property string glyph: ""
        /** Content of a part (or of a constraint / calculation), not an element of its own. */
        readonly property bool feature: tool === DiagramTypes.ToolAttribute || tool === DiagramTypes.ToolAction
                                        || tool === DiagramTypes.ToolConstraint || tool === DiagramTypes.ToolCalc

        property point _start
        property bool _dragging: false

        objectName: "paletteTile" + tool
        Layout.fillWidth: true
        Layout.preferredWidth: 1 // equal columns
        implicitHeight: 52
        radius: 8
        color: tileArea.containsMouse ? Qt.alpha(Theme.textPrimary, 0.06) : "transparent"
        border.width: 1
        border.color: Theme.outline

        Column {
            anchors.centerIn: parent
            spacing: 3

            // Small symbol of the element
            Item {
                width: 30
                height: 20
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    visible: tile.symbol <= 2 || tile.symbol === 7 || tile.symbol === 8 || tile.glyph.length > 0
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
                    visible: tile.glyph.length > 0
                    anchors.centerIn: parent
                    text: tile.glyph
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: Theme.accent
                }
                // state: rounded box; decide/merge: diamond; fork/join: bar; start: dot; done: ring with dot
                Rectangle {
                    visible: tile.symbol === 12
                    anchors.fill: parent
                    radius: 8
                    color: Theme.card
                    border.width: 1.2
                    border.color: Theme.textPrimary
                }
                Rectangle {
                    visible: tile.symbol === 13
                    anchors.centerIn: parent
                    width: 13
                    height: 13
                    rotation: 45
                    color: Theme.card
                    border.width: 1.3
                    border.color: Theme.accent
                }
                Rectangle {
                    visible: tile.symbol === 14
                    anchors.centerIn: parent
                    width: 26
                    height: 5
                    radius: 1
                    color: Theme.accent
                }
                Rectangle {
                    visible: tile.symbol === 15 || tile.symbol === 16
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    radius: 8
                    color: tile.symbol === 15 ? Theme.accent : Theme.card
                    border.width: 1.4
                    border.color: Theme.accent
                    Rectangle {
                        visible: tile.symbol === 16
                        anchors.centerIn: parent
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.accent
                    }
                }
                // use case: ellipse; actor: stick figure; subject: boundary with a use case inside
                Rectangle {
                    visible: tile.symbol === 17 || tile.symbol === 19
                    anchors.centerIn: parent
                    width: tile.symbol === 17 ? 30 : 16
                    height: tile.symbol === 17 ? 18 : 9
                    radius: height / 2
                    color: Theme.card
                    border.width: 1.2
                    border.color: tile.symbol === 17 ? Theme.textPrimary : Theme.accent
                }
                Rectangle {
                    visible: tile.symbol === 19
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 1.2
                    border.color: Theme.textPrimary
                }
                Canvas {
                    id: stickFigure
                    visible: tile.symbol === 18
                    anchors.fill: parent
                    onPaint: {
                        const context = getContext("2d")
                        context.reset()
                        context.strokeStyle = Theme.accent
                        context.lineWidth = 1.4
                        const c = width / 2
                        context.beginPath()
                        context.arc(c, 4, 3, 0, 2 * Math.PI)
                        context.moveTo(c, 7)
                        context.lineTo(c, 13)
                        context.moveTo(c - 5, 9)
                        context.lineTo(c + 5, 9)
                        context.moveTo(c, 13)
                        context.lineTo(c - 4, 19)
                        context.moveTo(c, 13)
                        context.lineTo(c + 4, 19)
                        context.stroke()
                    }
                    Connections {
                        target: Theme
                        function onDarkChanged() { stickFigure.requestPaint() }
                    }
                }
                // event: a dot on a dashed line; lifeline: a small header with its dashed line
                Rectangle {
                    visible: tile.symbol === 21
                    x: 4
                    width: 22
                    height: 7
                    color: Theme.card
                    border.width: 1.2
                    border.color: Theme.textPrimary
                }
                Repeater {
                    model: tile.symbol === 20 || tile.symbol === 21 ? 4 : 0
                    Rectangle {
                        required property int index
                        x: 14.3
                        y: (tile.symbol === 21 ? 8 : 0) + index * (tile.symbol === 21 ? 3.5 : 5.5)
                        width: 1.4
                        height: 2.4
                        color: Theme.textSecondary
                    }
                }
                Rectangle {
                    visible: tile.symbol === 20
                    x: 11
                    y: 6
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.accent
                }
                // port: a small square on the border of a part
                Rectangle {
                    visible: tile.symbol === 11
                    x: 4
                    width: 22
                    height: 20
                    color: "transparent"
                    border.width: 1.2
                    border.color: Theme.textSecondary
                    Rectangle {
                        x: parent.width - 5
                        y: 5
                        width: 10
                        height: 10
                        color: Theme.card
                        border.width: 1.4
                        border.color: Theme.accent
                    }
                }
                Label {
                    visible: tile.symbol >= 3 && tile.symbol <= 10 && tile.glyph.length === 0
                    anchors.centerIn: parent
                    text: tile.tool === DiagramTypes.ToolItem ? "i:" : tile.symbol === 3 ? "a:" : tile.symbol === 4 ? "▶"
                          : (tile.symbol === 5 || tile.symbol === 7) ? "{ }" : tile.symbol === 9 ? "T" : "ƒ"
                    font.italic: tile.symbol === 6 || tile.symbol === 8
                    font.pixelSize: tile.symbol >= 7 ? 11 : 14
                    font.weight: Font.DemiBold
                    color: Theme.accent
                }
            }
            Label {
                width: tile.width - 8
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: tile.label
                font.pixelSize: tile.label.length > 14 ? 11 : 12
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
                      : tile.owner.sequence && tile.tool === DiagramTypes.ToolLifeline
                      ? qsTr("Drop onto the canvas: a part of the interaction (\"ref part\"), placed among the lifelines")
                      : tile.tool === DiagramTypes.ToolEvent
                      ? qsTr("Drop onto a lifeline (in the structure view onto a part): an event occurrence")
                      : tile.tool === DiagramTypes.ToolVariationDefinition
                      ? qsTr("A variation (variation part def): drop variants onto it, the allowed choices")
                      : tile.tool === DiagramTypes.ToolVariant
                      ? qsTr("Drop onto a variation: one of its choices (variant)")
                      : tile.tool === DiagramTypes.ToolIndividualDefinition || tile.tool === DiagramTypes.ToolIndividual
                      ? qsTr("A single, unique thing (individual), e.g. one vehicle with its serial number")
                      : tile.tool === DiagramTypes.ToolSnapshot || tile.tool === DiagramTypes.ToolTimeslice
                      ? qsTr("Drop onto a part, individual, item or occurrence: a part of its lifetime (snapshot = one instant)")
                      : tile.tool === DiagramTypes.ToolAnalysisDefinition || tile.tool === DiagramTypes.ToolAnalysis
                      ? qsTr("An analysis case: subject, objective, parameters, steps (behavior view) and a result")
                      : tile.tool === DiagramTypes.ToolOccurrenceDefinition
                      ? qsTr("An interaction: its parts are the lifelines of the sequence view")
                      : tile.tool === DiagramTypes.ToolAllocationDefinition
                      ? qsTr("An allocation definition with its two ends; draw «allocate» arrows between usages")
                      : tile.tool === DiagramTypes.ToolMetadata
                      ? qsTr("Drop onto an element: metadata about it (@Name); onto the canvas: metadata about listed elements")
                      : tile.tool === DiagramTypes.ToolExpose
                      ? qsTr("Drop onto a view: the elements it shows (expose vehicle::**)")
                      : tile.tool === DiagramTypes.ToolRender
                      ? qsTr("Drop onto a view or view definition: how it is rendered (render asTreeDiagram)")
                      : tile.tool === DiagramTypes.ToolFilter
                      ? qsTr("Drop onto a package or view: which elements are included (filter @Safety)")
                      : tile.tool === DiagramTypes.ToolViewpoint || tile.tool === DiagramTypes.ToolConcern
                      ? qsTr("A viewpoint frames concerns (draw a Frame arrow to the concern); views satisfy viewpoints")
                      : tile.tool === DiagramTypes.ToolImport || tile.tool === DiagramTypes.ToolAlias
                      ? qsTr("Drop onto the canvas or a package: an import (private import Lib::*) or an alias")
                      : tile.tool === DiagramTypes.ToolConnectionDefinition
                      ? qsTr("A connection definition with its two ends (end part); its usages connect parts")
                      : tile.tool === DiagramTypes.ToolFlowDefinition
                      ? qsTr("A flow definition with its two ends (end port) and an optional payload (ref :>> payload : T)")
                      : tile.tool === DiagramTypes.ToolSendAction || tile.tool === DiagramTypes.ToolAcceptAction ||
                        tile.tool === DiagramTypes.ToolAssignAction || tile.tool === DiagramTypes.ToolTerminateAction
                      ? qsTr("Drop onto the canvas: an action that sends, accepts, assigns or terminates - double-click it to edit")
                      : tile.tool === DiagramTypes.ToolIfAction || tile.tool === DiagramTypes.ToolWhileAction ||
                        tile.tool === DiagramTypes.ToolLoopAction || tile.tool === DiagramTypes.ToolForAction
                      ? qsTr("Drop onto the canvas: a control structure; its body is shown when you choose it above")
                      : tile.tool === DiagramTypes.ToolPort
                      ? qsTr("Drop onto a part (drawn on its border), a port, a port definition or an interface definition (an end)")
                      : tile.tool === DiagramTypes.ToolItem
                      ? qsTr("Drop onto a part, an item or a port definition")
                      : tile.tool === DiagramTypes.ToolAttribute && !tile.owner.parametric
                      ? qsTr("Drop onto a part or definition; onto an enum def it becomes a value")
                      : tile.tool === DiagramTypes.ToolStatement
                      ? qsTr("Any other SysML v2 statement written as text (analysis, allocation, view, ...): drop onto the canvas or a part")
                      : tile.owner.useCase && tile.tool === DiagramTypes.ToolActor
                      ? qsTr("Drop onto a use case: the actor takes part in it. Lines from the actor add it to more use cases")
                      : tile.owner.useCase && tile.tool === DiagramTypes.ToolSubject
                      ? qsTr("Drop onto a use case: its subject becomes the system boundary")
                      : tile.owner.useCase
                      ? qsTr("Drag onto the canvas (into a boundary: a use case of that system), or click to insert")
                      : tile.tool === DiagramTypes.ToolSubject
                      ? qsTr("Drop onto a requirement, use case or verification: what it is about (subject)")
                      : tile.tool === DiagramTypes.ToolActor
                      ? qsTr("Drop onto a use case (actor) or a requirement (stakeholder)")
                      : tile.tool === DiagramTypes.ToolObjective
                      ? qsTr("Drop onto a use case or verification: its objective (draw «verify» arrows from a verification)")
                      : tile.tool === DiagramTypes.ToolRequirement
                      ? qsTr("A requirement (usage): satisfied by a part («satisfy»), verified by a verification («verify»)")
                      : tile.feature
                      ? (tile.tool === DiagramTypes.ToolAction
                         ? qsTr("Drag onto a part, or click to add to the selected part")
                         : qsTr("Drag onto a part, constraint or calculation, or click to add to the selected one"))
                      : qsTr("Drag onto the canvas, or click to insert")
        ToolTip.delay: 900
    }

    // Title of a group of tiles; a click folds the group away.
    component SectionHeader: Item {
        id: header

        property string text
        property bool expanded: true
        property bool collapsible: true

        Layout.fillWidth: true
        implicitHeight: headerLabel.implicitHeight + 4

        Label {
            id: headerLabel
            anchors.verticalCenter: parent.verticalCenter
            text: (header.collapsible ? (header.expanded ? "▾ " : "▸ ") : "") + header.text
            font.pixelSize: 12
            font.weight: Font.Medium
            color: Theme.textSecondary
        }
        MouseArea {
            anchors.fill: parent
            enabled: header.collapsible
            cursorShape: Qt.PointingHandCursor
            onClicked: header.expanded = !header.expanded
        }
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
                visible: palette.structure
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
                visible: palette.structure
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
                visible: palette.structure
                text: qsTr("New arrows")
                font.pixelSize: 11
                color: Theme.textSecondary
            }
            ComboBox {
                visible: palette.structure
                Layout.fillWidth: true
                model: [qsTr("Ask"), qsTr("Composition"), qsTr("Connect"), qsTr("Flow"), qsTr("Specialization"),
                        qsTr("Interface"), qsTr("Satisfy"), qsTr("Verify"), qsTr("Allocate"), qsTr("Dependency")]
                currentIndex: palette.controller ? palette.controller.arrowDefault : 0
                onActivated: index => palette.controller.arrowDefault = index
                Accessible.name: qsTr("Kind of new arrows")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Ask on release, or always create this kind of relationship")
                ToolTip.delay: 700
            }

            SectionHeader {
                id: structureSection
                visible: palette.structure
                Layout.topMargin: 8
                text: qsTr("Elements")
            }
            GridLayout {
                Layout.fillWidth: true
                visible: palette.structure && structureSection.expanded
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolPart
                    visible: palette.structure
                    label: qsTr("Part")
                    symbol: 0
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolPartDefinition
                    visible: palette.structure
                    label: qsTr("Part def")
                    symbol: 1
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolPackage
                    visible: palette.structure
                    label: qsTr("Package")
                    symbol: 2
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolAttributeDefinition
                    visible: palette.structure
                    label: qsTr("Attribute def")
                    symbol: 10
                    glyph: "a"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolEnumDefinition
                    visible: palette.structure
                    label: qsTr("Enum def")
                    symbol: 10
                    glyph: "e"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolItemDefinition
                    visible: palette.structure
                    label: qsTr("Item def")
                    symbol: 10
                    glyph: "i"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolPortDefinition
                    visible: palette.structure
                    label: qsTr("Port def")
                    symbol: 10
                    glyph: "p"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolInterfaceDefinition
                    visible: palette.structure
                    label: qsTr("Interface def")
                    symbol: 10
                    glyph: "if"
                }
            }

            SectionHeader {
                id: behaviorSection
                visible: palette.structure
                Layout.topMargin: 8
                text: qsTr("Behavior and constraints")
            }
            GridLayout {
                Layout.fillWidth: true
                visible: palette.structure && behaviorSection.expanded
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolActionDefinition
                    visible: palette.structure
                    label: qsTr("Action def")
                    symbol: 10
                    glyph: "▶"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolStateDefinition
                    visible: palette.structure
                    label: qsTr("State def")
                    symbol: 10
                    glyph: "◐"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolConstraintDefinition
                    visible: palette.structure
                    label: qsTr("Constraint def")
                    symbol: 7
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolCalcDefinition
                    visible: palette.structure
                    label: qsTr("Calc def")
                    symbol: 8
                }
            }

            SectionHeader {
                id: requirementSection
                visible: palette.structure
                Layout.topMargin: 8
                text: qsTr("Requirements and cases")
            }
            GridLayout {
                Layout.fillWidth: true
                visible: palette.structure && requirementSection.expanded
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolRequirementDefinition
                    visible: palette.structure
                    label: qsTr("Requirement def")
                    symbol: 10
                    glyph: "R"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolRequirement
                    visible: palette.structure
                    label: qsTr("Requirement")
                    symbol: 0
                    glyph: "R"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolUseCaseDefinition
                    visible: palette.structure
                    label: qsTr("Use case def")
                    symbol: 17
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolUseCase
                    visible: palette.structure
                    label: qsTr("Use case")
                    symbol: 17
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolVerificationDefinition
                    visible: palette.structure
                    label: qsTr("Verification def")
                    symbol: 10
                    glyph: "✓"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolVerification
                    visible: palette.structure
                    label: qsTr("Verification")
                    symbol: 0
                    glyph: "✓"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolAnalysisDefinition
                    visible: palette.structure
                    label: qsTr("Analysis def")
                    symbol: 10
                    glyph: "∑"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolAnalysis
                    visible: palette.structure
                    label: qsTr("Analysis")
                    symbol: 0
                    glyph: "∑"
                }
            }

            SectionHeader {
                id: variabilitySection
                visible: palette.structure
                Layout.topMargin: 8
                text: qsTr("Variability and individuals")
            }
            GridLayout {
                Layout.fillWidth: true
                visible: palette.structure && variabilitySection.expanded
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolVariationDefinition
                    visible: palette.structure
                    label: qsTr("Variation def")
                    symbol: 10
                    glyph: "◇"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolVariant
                    visible: palette.structure
                    label: qsTr("Variant")
                    symbol: 0
                    glyph: "◆"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolIndividualDefinition
                    visible: palette.structure
                    label: qsTr("Individual def")
                    symbol: 10
                    glyph: "1"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolIndividual
                    visible: palette.structure
                    label: qsTr("Individual")
                    symbol: 0
                    glyph: "1"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolSnapshot
                    visible: palette.structure
                    label: qsTr("Snapshot")
                    symbol: 0
                    glyph: "◉"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolTimeslice
                    visible: palette.structure
                    label: qsTr("Time slice")
                    symbol: 0
                    glyph: "◫"
                }
            }

            SectionHeader {
                id: interactionSection
                visible: palette.structure
                Layout.topMargin: 8
                text: qsTr("Interactions and allocation")
            }
            GridLayout {
                Layout.fillWidth: true
                visible: palette.structure && interactionSection.expanded
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolOccurrenceDefinition
                    visible: palette.structure
                    label: qsTr("Occurrence def")
                    symbol: 10
                    glyph: "⇄"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolOccurrence
                    visible: palette.structure
                    label: qsTr("Occurrence")
                    symbol: 0
                    glyph: "⇄"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolAllocationDefinition
                    visible: palette.structure
                    label: qsTr("Allocation def")
                    symbol: 10
                    glyph: "↦"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolEvent
                    visible: palette.structure
                    label: qsTr("Event")
                    symbol: 20
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolConnectionDefinition
                    visible: palette.structure
                    label: qsTr("Connection def")
                    symbol: 10
                    glyph: "⟷"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolFlowDefinition
                    visible: palette.structure
                    label: qsTr("Flow def")
                    symbol: 10
                    glyph: "⇢"
                }
            }

            SectionHeader {
                id: viewSection
                visible: palette.structure
                Layout.topMargin: 8
                text: qsTr("Views and metadata")
            }
            GridLayout {
                Layout.fillWidth: true
                visible: palette.structure && viewSection.expanded
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Repeater {
                    model: [
                        { tool: DiagramTypes.ToolViewDefinition, label: qsTr("View def"), symbol: 10, glyph: "▣" },
                        { tool: DiagramTypes.ToolView, label: qsTr("View"), symbol: 10, glyph: "▢" },
                        { tool: DiagramTypes.ToolViewpoint, label: qsTr("Viewpoint"), symbol: 10, glyph: "◉" },
                        { tool: DiagramTypes.ToolConcern, label: qsTr("Concern"), symbol: 10, glyph: "?" },
                        { tool: DiagramTypes.ToolExpose, label: qsTr("Expose"), symbol: 10, glyph: "⤴" },
                        { tool: DiagramTypes.ToolRender, label: qsTr("Render"), symbol: 10, glyph: "▤" },
                        { tool: DiagramTypes.ToolFilter, label: qsTr("Filter"), symbol: 10, glyph: "⊻" },
                        { tool: DiagramTypes.ToolMetadataDefinition, label: qsTr("Metadata def"), symbol: 10, glyph: "#" },
                        { tool: DiagramTypes.ToolMetadata, label: qsTr("Metadata"), symbol: 10, glyph: "@" },
                        { tool: DiagramTypes.ToolImport, label: qsTr("Import"), symbol: 10, glyph: "⇣" },
                        { tool: DiagramTypes.ToolAlias, label: qsTr("Alias"), symbol: 10, glyph: "≡" }
                    ]

                    Tile {
                        required property var modelData
                        owner: palette
                        tool: modelData.tool
                        label: modelData.label
                        symbol: modelData.symbol
                        glyph: modelData.glyph
                    }
                }
            }

            SectionHeader {
                id: contentSection
                visible: palette.structure || palette.parametric
                collapsible: palette.structure
                Layout.topMargin: palette.parametric ? 0 : 8
                text: palette.parametric ? qsTr("Elements") : qsTr("Content")
            }
            GridLayout {
                Layout.fillWidth: true
                visible: (palette.structure && contentSection.expanded) || palette.parametric
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolAttribute
                    visible: palette.structure || palette.parametric
                    label: palette.parametric ? qsTr("Value") : qsTr("Attribute")
                    symbol: 3
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolPort
                    visible: palette.structure
                    label: qsTr("Port")
                    symbol: 11
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolItem
                    visible: palette.structure
                    label: qsTr("Item")
                    symbol: 3
                    glyph: ""
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolState
                    visible: palette.structure
                    label: qsTr("State")
                    symbol: 12
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolAction
                    visible: palette.structure
                    label: qsTr("Action")
                    symbol: 4
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolConstraint
                    visible: palette.structure || palette.parametric
                    label: qsTr("Constraint")
                    symbol: 5
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolCalc
                    visible: palette.structure || palette.parametric
                    label: qsTr("Calc")
                    symbol: 6
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolSubject
                    visible: palette.structure
                    label: qsTr("Subject")
                    symbol: 19
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolActor
                    visible: palette.structure
                    label: qsTr("Actor")
                    symbol: 18
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolObjective
                    visible: palette.structure
                    label: qsTr("Objective")
                    symbol: 0
                    glyph: "◎"
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolStatement
                    visible: palette.structure
                    label: qsTr("Text")
                    symbol: 9
                }
            }

            // Behavior view: actions and control nodes, or states
            Label {
                visible: palette.behavior
                text: palette.stateMachine ? qsTr("States") : qsTr("Actions")
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textSecondary
            }
            GridLayout {
                Layout.fillWidth: true
                visible: palette.behavior
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolState
                    visible: palette.stateMachine
                    label: qsTr("State")
                    symbol: 12
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolInitial
                    visible: palette.stateMachine
                    label: qsTr("Initial")
                    symbol: 15
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolActionUsage
                    label: palette.stateMachine ? qsTr("State action") : qsTr("Action")
                    symbol: 4
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolDecision
                    visible: !palette.stateMachine
                    label: qsTr("Decide")
                    symbol: 13
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolMerge
                    visible: !palette.stateMachine
                    label: qsTr("Merge")
                    symbol: 13
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolFork
                    visible: !palette.stateMachine
                    label: qsTr("Fork")
                    symbol: 14
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolJoin
                    visible: !palette.stateMachine
                    label: qsTr("Join")
                    symbol: 14
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolStart
                    visible: !palette.stateMachine
                    label: qsTr("Start")
                    symbol: 15
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolDone
                    visible: !palette.stateMachine
                    label: qsTr("Done")
                    symbol: 16
                }
                // actions that do something specific (written unnamed: "then send s to t;"), control structures
                Repeater {
                    model: palette.stateMachine ? [] : [
                        { tool: DiagramTypes.ToolSendAction, label: qsTr("Send"), glyph: "↗" },
                        { tool: DiagramTypes.ToolAcceptAction, label: qsTr("Accept"), glyph: "↘" },
                        { tool: DiagramTypes.ToolAssignAction, label: qsTr("Assign"), glyph: ":=" },
                        { tool: DiagramTypes.ToolTerminateAction, label: qsTr("Terminate"), glyph: "⊗" },
                        { tool: DiagramTypes.ToolIfAction, label: qsTr("If"), glyph: "if" },
                        { tool: DiagramTypes.ToolWhileAction, label: qsTr("While"), glyph: "wh" },
                        { tool: DiagramTypes.ToolLoopAction, label: qsTr("Loop"), glyph: "↻" },
                        { tool: DiagramTypes.ToolForAction, label: qsTr("For"), glyph: "for" }
                    ]

                    Tile {
                        required property var modelData
                        owner: palette
                        tool: modelData.tool
                        label: modelData.label
                        symbol: 4
                        glyph: modelData.glyph
                    }
                }
            }

            // Use case view: use cases, actors, the subject (system boundary)
            Label {
                visible: palette.useCase
                text: qsTr("Use cases")
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textSecondary
            }
            GridLayout {
                Layout.fillWidth: true
                visible: palette.useCase
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolUseCaseDefinition
                    visible: palette.useCase
                    label: qsTr("Use case def")
                    symbol: 17
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolUseCase
                    visible: palette.useCase
                    label: qsTr("Use case")
                    symbol: 17
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolActor
                    visible: palette.useCase
                    label: qsTr("Actor")
                    symbol: 18
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolSubject
                    visible: palette.useCase
                    label: qsTr("Subject")
                    symbol: 19
                }
            }

            // Sequence view: lifelines and events; what dragging draws
            Label {
                visible: palette.sequence
                text: qsTr("Interaction")
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textSecondary
            }
            GridLayout {
                Layout.fillWidth: true
                visible: palette.sequence
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolLifeline
                    visible: palette.sequence
                    label: qsTr("Lifeline")
                    symbol: 21
                }
                Tile {
                    owner: palette
                    tool: DiagramTypes.ToolEvent
                    visible: palette.sequence
                    label: qsTr("Event")
                    symbol: 20
                }
            }
            Label {
                visible: palette.sequence
                Layout.topMargin: 4
                text: qsTr("Dragging draws")
                font.pixelSize: 11
                color: Theme.textSecondary
            }
            ComboBox {
                objectName: "sequenceArrowBox"
                visible: palette.sequence
                Layout.fillWidth: true
                model: [qsTr("Message"), qsTr("\"then\" (event order)")]
                currentIndex: palette.sequenceController ? palette.sequenceController.arrowKind : 0
                onActivated: index => palette.sequenceController.arrowKind = index
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Message: from a lifeline or event to another lifeline.\n"
                                   + "\"then\": from one event to another (the second happens after the first).")
                ToolTip.delay: 700
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
            GridLayout {
                Layout.fillWidth: true
                visible: palette.parametric
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

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
                text: palette.sequence
                      ? qsTr("Messages: drag from a lifeline (or an event) to another lifeline - the events are added "
                             + "where it starts and ends. Shift + drag moves an event, dragging a header moves the "
                             + "lifeline. Double-click edits.")
                      : palette.useCase
                      ? qsTr("Lines: drag the small handle at the corner of a use case or actor onto another node - "
                             + "actor to use case, use case to use case («include»), use case onto a boundary. "
                             + "Double-click edits.")
                      : palette.behavior
                      ? qsTr("Arrows: drag the small handle at the corner of a node onto another node. "
                             + "Double-click an arrow for guard, trigger and effect.")
                      : palette.parametric
                      ? qsTr("Lines: drag from the dot of a value onto a parameter (or another value). "
                             + "Double-click edits, Delete removes the selection.")
                      : qsTr("Double-click edits, Delete removes the selection, Ctrl + wheel zooms.")
                font.pixelSize: 11
                color: Theme.textSecondary
            }
        }
    }
}
