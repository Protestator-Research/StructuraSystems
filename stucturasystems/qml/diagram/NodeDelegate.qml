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
    /** Below the name: "«alias» Car" */
    required property string subtitle
    /** A package symbol without contents: an alias of a package (table 3), «alias» Package2Alias for Package2. */
    required property bool folder
    /** An inherited usage drawn as node (^part1, table 4 "Inherited Usage"): drawn only, no textual notation. */
    required property bool derived
    /** [{title, entries: [{entryId, text, tooltip}]}] */
    required property var compartments
    required property real nodeX
    required property real nodeY
    required property real nodeWidth
    required property real nodeHeight
    required property int depth
    required property bool selected
    /** A reference ("ref part"): a dashed border. */
    required property bool dashed
    /** Interconnection view: a part drawn around its parts (its size comes from the layout). */
    required property bool container

    property DiagramController controller
    /** The DiagramCanvas (arrow drawing, menus, edit dialog). */
    property Item canvas
    /** Preview: only shown, no interaction. */
    property bool readOnly: false

    readonly property bool isFrame: nodeKind === DiagramTypes.PackageNode
    readonly property bool isPort: nodeKind === DiagramTypes.PortNode
    /** An imported library package (ISQ, ScalarValues): a dashed package symbol. */
    readonly property bool isLibrary: nodeKind === DiagramTypes.LibraryNode
    /** comment, doc, rep, metadata: a note with a folded corner (table 2, 26). */
    readonly property bool isNote: nodeKind === DiagramTypes.NoteNode
    // definitions have square corners, usages rounded ones («action», «state», «view» are usages too);
    // «alias», «import», «filter» and statements kept as text are drawn as plain rectangles
    readonly property bool isDefinition: nodeKind === DiagramTypes.PartDefinitionNode
                                         || (nodeKind === DiagramTypes.DefinitionNode
                                             && (keyword.indexOf(" def»") >= 0 || keyword === "«alias»"
                                                 || keyword.indexOf("import»") >= 0 || keyword === "«filter»"))
                                         || nodeKind === DiagramTypes.TextualNode
                                         || nodeKind === DiagramTypes.ConstraintDefinitionNode
                                         || nodeKind === DiagramTypes.CalcDefinitionNode
                                         || ((nodeKind === DiagramTypes.RequirementNode || nodeKind === DiagramTypes.CaseNode)
                                             && keyword.indexOf(" def»") >= 0)
    readonly property color lineColor: derived ? Theme.danger : selected ? Theme.accent : Theme.textPrimary
    readonly property real padding: 8

    /** Attribute or action shown at a point in node coordinates, empty if none. */
    function entryAt(point) {
        // "«alias» Car" below the name: the alias
        if (subtitleLabel.visible && !folder) {
            const local = node.mapToItem(subtitleLabel, point.x, point.y)
            if (local.x >= 0 && local.y >= 0 && local.x <= subtitleLabel.width && local.y <= subtitleLabel.height) {
                const alias = aliasAt(subtitleLabel, local.x, local.y)
                if (alias.length > 0)
                    return alias
            }
        }
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

    /** The alias under the mouse in "«alias» A, B" (the label shows them in the order of aliasEntries), or "". */
    function aliasAt(label, x, y) {
        if (!controller || folder)
            return ""
        const entries = controller.aliasEntries(elementId)
        if (entries.length === 0)
            return ""
        const position = label.positionAt(x, y)
        const before = label.text.substring(0, position)
        const index = Math.min(entries.length - 1, before.split(",").length - 1)
        return entries[index]
    }

    function reportSize() {
        if (!isFrame && !isPort && !container && controller)
            controller.setNodeSize(elementId, width, height)
        Qt.callLater(reportEntries)
    }

    /** Entries a note is about ("comment about part1::attribute1"): highlighted, the dashed line ends there. */
    property var annotatedEntries: []
    function updateAnnotatedEntries() {
        const entries = controller ? controller.annotatedEntries(elementId) : []
        if (JSON.stringify(entries) !== JSON.stringify(annotatedEntries))
            annotatedEntries = entries
        Qt.callLater(reportEntries)
    }
    function reportEntries() {
        if (!controller || annotatedEntries.length === 0)
            return
        for (let c = 0; c < compartmentRepeater.count; ++c) {
            const compartment = compartmentRepeater.itemAt(c)
            if (!compartment)
                continue
            for (let i = 0; i < compartment.entryRepeater.count; ++i) {
                const item = compartment.entryRepeater.itemAt(i)
                if (!item || annotatedEntries.indexOf(item.entryId) < 0)
                    continue
                const at = item.mapToItem(node, 0, 0)
                controller.setEntryRect(elementId, item.entryId, Qt.rect(node.padding / 2, at.y, node.width - node.padding, item.height))
            }
        }
    }
    Connections {
        target: node.controller ? node.controller.edges : null
        function onModelReset() { node.updateAnnotatedEntries() }
    }
    // a notation or "All compartments" changed: the controller forgot the sizes, so report it again (also when this
    // node keeps its size)
    Connections {
        target: node.controller
        function onNotationsChanged() { Qt.callLater(node.reportSize) }
        function onAllCompartmentsChanged() { Qt.callLater(node.reportSize) }
    }
    onCompartmentsChanged: Qt.callLater(updateAnnotatedEntries)

    x: nodeX
    y: nodeY
    width: isFrame || isPort || container ? nodeWidth : Math.max(140, partContent.contentWidth + 2 * padding)
    // tall enough for its ports on the left and right border
    height: isFrame || isPort || container ? nodeHeight
                              : isLibrary ? 66
                              : Math.max(partContent.implicitHeight + padding,
                                         controller && controller.modelRevision >= 0 ? controller.minimumNodeHeight(elementId) : 0)
    z: isFrame ? depth : isPort ? 3000 + depth : 1000 + depth

    onWidthChanged: reportSize()
    onHeightChanged: reportSize()
    Component.onCompleted: {
        reportSize()
        updateAnnotatedEntries()
    }

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
            if (mouse.button !== Qt.LeftButton || grip.owner.controller.arrowMode || grip.owner.derived)
                return
            const entry = grip.owner.entryAt(grip.mapToItem(grip.owner, mouse.x, mouse.y))
            grip.owner.canvas.editElement(entry.length > 0 ? entry : grip.owner.elementId)
        }
    }

    // ---------------------------------------------------------------- part / part definition
    Rectangle {
        id: partBox

        visible: !node.isFrame && !node.isPort && !node.isLibrary
        anchors.fill: parent
        radius: node.isDefinition || node.isNote ? 2 : 10
        color: node.isNote || node.folder ? "transparent"
               : node.derived ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12) : Theme.card
        border.width: node.dashed || node.isNote || node.folder ? 0 : node.selected || node.derived ? 2 : 1.2
        border.color: node.lineColor

        // an alias of a package: the package symbol (an empty tab on the body), the text in the body
        Canvas {
            id: folderShape

            readonly property real tab: 16
            visible: node.folder
            anchors.fill: parent
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = node.lineColor
                context.lineWidth = node.selected ? 2 : 1.2
                context.fillStyle = Theme.card
                const inset = context.lineWidth / 2
                const tabWidth = Math.min(width * 0.45, 70)
                context.beginPath()
                context.rect(inset, inset, tabWidth, tab - inset)
                context.fill()
                context.stroke()
                context.beginPath()
                context.rect(inset, tab, width - 2 * inset, height - tab - inset)
                context.fill()
                context.stroke()
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()
            Connections {
                target: node
                function onLineColorChanged() { folderShape.requestPaint() }
                function onSelectedChanged() { folderShape.requestPaint() }
            }
            Connections {
                target: Theme
                function onDarkChanged() { folderShape.requestPaint() }
            }
        }

        // a note: the outline with the folded upper right corner
        Canvas {
            id: noteShape

            visible: node.isNote
            anchors.fill: parent
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = node.lineColor
                context.lineWidth = node.selected ? 2 : 1.2
                context.fillStyle = Theme.card
                const inset = context.lineWidth / 2
                const fold = 14
                context.beginPath()
                context.moveTo(inset, inset)
                context.lineTo(width - fold, inset)
                context.lineTo(width - inset, fold)
                context.lineTo(width - inset, height - inset)
                context.lineTo(inset, height - inset)
                context.closePath()
                context.fill()
                context.stroke()
                context.beginPath()
                context.moveTo(width - fold, inset)
                context.lineTo(width - fold, fold)
                context.lineTo(width - inset, fold)
                context.stroke()
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()
            Connections {
                target: node
                function onLineColorChanged() { noteShape.requestPaint() }
                function onSelectedChanged() { noteShape.requestPaint() }
            }
            Connections {
                target: Theme
                function onDarkChanged() { noteShape.requestPaint() }
            }
        }

        // dashed border of a reference
        Canvas {
            id: dashedBorder

            visible: node.dashed
            anchors.fill: parent
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = node.lineColor
                context.lineWidth = node.selected ? 2 : 1.2
                context.setLineDash([5, 3])
                const inset = context.lineWidth / 2
                const r = partBox.radius
                context.beginPath()
                context.roundedRect(inset, inset, width - 2 * inset, height - 2 * inset, r, r)
                context.stroke()
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()
            Connections {
                target: node
                function onLineColorChanged() { dashedBorder.requestPaint() }
                function onSelectedChanged() { dashedBorder.requestPaint() }
            }
        }

        Column {
            id: partContent

            readonly property real contentWidth: {
                // the alias line ("for Package1::Package2") too, up to a width; longer it wraps
                let width = Math.max(keywordLabel.implicitWidth, titleLabel.implicitWidth,
                                     subtitleLabel.visible ? Math.min(subtitleLabel.implicitWidth, 280) : 0)
                for (let c = 0; c < compartmentRepeater.count; ++c) {
                    const item = compartmentRepeater.itemAt(c)
                    if (item)
                        width = Math.max(width, item.entryRepeater.parent.implicitWidth + node.padding)
                }
                return width
            }

            width: parent.width
            topPadding: node.padding / 2 + (node.folder ? folderShape.tab + 4 : 0)

            Label {
                id: keywordLabel
                width: parent.width
                visible: !node.isNote || node.keyword.length > 0
                horizontalAlignment: Text.AlignHCenter
                text: node.keyword
                font.pixelSize: 12
                color: node.derived ? Theme.danger : Theme.textSecondary
            }
            Label {
                id: titleLabel
                width: parent.width
                visible: !node.isNote || node.title.length > 0
                horizontalAlignment: Text.AlignHCenter
                // as in the tables of the specification: the name in normal weight, the keywords of the name
                // compartment ("subsets", "redefines", ...) in bold, each further line ("redefines part1 = part2") below
                textFormat: Text.StyledText
                text: {
                    const escaped = node.title.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                    return escaped.replace(/(^|\s)(subsets|redefines|references|specializes|conjugates|crosses|for)(?=\s)/g,
                                           "$1<b>$2</b>")
                                  .replace(/\n/g, "<br>")
                }
                font.pixelSize: 14
                // an abstract definition: its name in italics (table 4)
                font.italic: node.isDefinition && node.keyword.indexOf("«abstract ") === 0
                color: node.derived ? Theme.danger : Theme.textPrimary
                bottomPadding: node.subtitle.length > 0 ? 0 : 4
            }
            // an inherited usage drawn as node: allowed in the graphical notation only, it has no text
            Label {
                width: parent.width
                visible: node.derived
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                leftPadding: node.padding
                rightPadding: node.padding
                text: qsTr("inherited - no textual notation")
                font.pixelSize: 10
                font.italic: true
                color: Theme.danger
                bottomPadding: 4
            }
            // "«alias» Car, Auto": the aliases of the element in its name compartment
            Label {
                id: subtitleLabel
                objectName: "aliasLabel"
                width: parent.width
                leftPadding: node.padding
                rightPadding: node.padding
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                visible: node.subtitle.length > 0
                horizontalAlignment: Text.AlignHCenter
                // "for Package2" of an alias drawn as package: "for" in bold, as large as the name (table 3)
                textFormat: node.folder ? Text.StyledText : Text.PlainText
                // a long qualified name may break after "::" (a zero width space there)
                text: node.folder ? "<b>for</b> " + node.subtitle.substring(4).replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                                       .replace(/::/g, "::\u200B")
                                  : node.subtitle.replace(/::/g, "::\u200B")
                font.pixelSize: node.folder ? 14 : 11
                color: node.folder ? Theme.textPrimary : Theme.textSecondary
                bottomPadding: 4
                MouseArea {
                    anchors.fill: parent
                    enabled: !node.folder && !node.readOnly && node.canvas !== null
                    acceptedButtons: Qt.RightButton
                    onClicked: mouse => {
                        const alias = node.aliasAt(subtitleLabel, mouse.x, mouse.y)
                        if (alias.length > 0)
                            node.canvas.openNodeMenu(node.elementId, alias)
                    }
                    // left clicks (select, move, double-click) go to the node below
                }
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
                        visible: !node.isNote // the text of a note has no compartment line
                        width: parent.width
                        height: 1
                        color: Qt.alpha(Theme.textPrimary, 0.5)
                    }
                    Label {
                        visible: compartment.modelData.title.length > 0
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
                                // the annotated element of a note (table 2): a grey band across the compartment
                                background: Rectangle {
                                    visible: node.annotatedEntries.indexOf(parent.entryId) >= 0
                                    x: -node.padding / 2
                                    implicitWidth: 0
                                    implicitHeight: 0
                                    width: partContent.width - node.padding
                                    height: parent.height
                                    color: Qt.alpha(Theme.textPrimary, 0.1)
                                }
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

        // direction of the port (graphical BNF pdh / pdv): -> into the part for "in", out of it for "out", <-> for
        // "inout"; the side of the part decides which way the arrow points
        Canvas {
            id: portArrow

            readonly property string sideName: node.isPort && node.controller && node.controller.geometryRevision >= 0
                                               ? node.controller.portSide(node.elementId) : "left"
            readonly property int side: sideName === "left" ? 0 : sideName === "right" ? 1 : sideName === "top" ? 2 : 3
            visible: node.isPort && node.keyword.length > 0
            anchors.fill: parent
            anchors.margins: 2
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = node.lineColor
                context.lineWidth = 1.2
                const horizontal = side <= 1
                // "inwards" on the left border is to the right, on the right border to the left, ...
                const inwards = side === 0 ? 1 : side === 1 ? -1 : side === 2 ? 1 : -1
                const both = node.keyword === "inout"
                const sign = node.keyword === "out" ? -inwards : inwards
                const cx = width / 2
                const cy = height / 2
                const half = (horizontal ? width : height) / 2 - 0.5
                const head = Math.min(4, half)
                const line = (x1, y1, x2, y2) => { context.moveTo(x1, y1); context.lineTo(x2, y2) }
                context.beginPath()
                if (horizontal) {
                    line(cx - half, cy, cx + half, cy)
                    const tip = cx + sign * half
                    line(tip, cy, tip - sign * head, cy - head)
                    line(tip, cy, tip - sign * head, cy + head)
                    if (both) {
                        line(cx - sign * half, cy, cx - sign * half + sign * head, cy - head)
                        line(cx - sign * half, cy, cx - sign * half + sign * head, cy + head)
                    }
                } else {
                    line(cx, cy - half, cx, cy + half)
                    const tip = cy + sign * half
                    line(cx, tip, cx - head, tip - sign * head)
                    line(cx, tip, cx + head, tip - sign * head)
                    if (both) {
                        line(cx, cy - sign * half, cx - head, cy - sign * half + sign * head)
                        line(cx, cy - sign * half, cx + head, cy - sign * half + sign * head)
                    }
                }
                context.stroke()
            }
            onSideChanged: requestPaint()
            onVisibleChanged: requestPaint()
            onWidthChanged: requestPaint()
            Connections {
                target: node
                function onKeywordChanged() { portArrow.requestPaint() }
                function onLineColorChanged() { portArrow.requestPaint() }
            }
        }

        Grip {
            owner: node
            anchors.fill: parent
            anchors.margins: -4
        }
    }
    Label {
        id: portLabel

        // placed by the controller where it covers no other port, label or node (outside the part if possible);
        // computed again whenever any node moves
        // (the revision is read so that the binding depends on it)
        readonly property point offset: node.isPort && node.controller && node.controller.geometryRevision >= 0
                                        ? node.controller.portLabelOffset(node.elementId)
                                        : Qt.point(node.width + 4, -15)
        // estimated widths are used for the placement: a longer text grows away from the square
        readonly property bool leftOfSquare: offset.x < 0

        objectName: "portLabel"
        visible: node.isPort
        text: node.title
        textFormat: Text.PlainText
        font.pixelSize: 12
        color: Theme.textPrimary
        leftPadding: 2
        rightPadding: 2
        x: leftOfSquare ? -implicitWidth - 4 : offset.x
        y: offset.y

        // lines running behind the label do not cross the text
        background: Rectangle {
            color: Qt.alpha(Theme.card, 0.85)
            radius: 2
        }
    }

    // ---------------------------------------------------------------- imported library package (dashed)
    Item {
        id: library

        visible: node.isLibrary
        anchors.fill: parent

        Canvas {
            id: libraryShape

            anchors.fill: parent
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = node.lineColor
                context.lineWidth = node.selected ? 2 : 1.2
                context.fillStyle = Theme.card
                context.setLineDash([5, 3])
                const inset = context.lineWidth / 2
                const tab = Math.min(width * 0.45, 70)
                // tab on top, body below
                context.beginPath()
                context.moveTo(inset, 16)
                context.lineTo(inset, inset)
                context.lineTo(tab, inset)
                context.lineTo(tab, 16)
                context.moveTo(inset, 16)
                context.lineTo(width - inset, 16)
                context.lineTo(width - inset, height - inset)
                context.lineTo(inset, height - inset)
                context.closePath()
                context.fill()
                context.stroke()
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: node
                function onLineColorChanged() { libraryShape.requestPaint() }
                function onSelectedChanged() { libraryShape.requestPaint() }
            }
            Connections {
                target: Theme
                function onDarkChanged() { libraryShape.requestPaint() }
            }
        }
        Column {
            y: 22
            width: parent.width
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: node.keyword
                font.pixelSize: 12
                color: Theme.textSecondary
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: node.title
                font.pixelSize: 14
                color: Theme.textPrimary
            }
        }
        Grip {
            owner: node
            anchors.fill: parent
        }
    }

    // ---------------------------------------------------------------- package frame
    Item {
        id: frame

        readonly property real tabWidth: Math.min(node.width * 0.8, frameTitle.implicitWidth + 2 * node.padding + 8)
        readonly property real tabHeight: 24

        visible: node.isFrame
        anchors.fill: parent

        // Table 3 "Package (name in tab)": a folder - the name in a tab on top of the body, the body below it
        Rectangle {
            y: frame.tabHeight
            width: parent.width
            height: parent.height - frame.tabHeight
            color: Qt.alpha(Theme.card, 0.35)
            border.width: node.selected ? 2 : 1.2
            border.color: node.lineColor
        }

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
                // a closed rectangle standing on the body: the line under the name is the top of the body
                context.beginPath()
                context.rect(inset, inset, width - 2 * inset, height - inset)
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
            width: frame.tabWidth - 2 * node.padding
            height: frame.tabHeight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: node.title
            font.pixelSize: 13
            color: Theme.textPrimary
        }

        // packages / imports listed as text (notation "compartment", table 3): compartments at the top of the body
        Column {
            objectName: "frameCompartments"
            x: node.padding
            y: frame.tabHeight + 4
            width: parent.width - 2 * node.padding
            visible: node.isFrame && (node.compartments.length > 0 || node.subtitle.length > 0)
            // "«alias» Package3Alias": the aliases of the package (notation "«alias» at the element", table 4)
            Label {
                objectName: "frameAliasLabel"
                width: parent.width
                visible: node.subtitle.length > 0
                horizontalAlignment: Text.AlignHCenter
                text: node.subtitle
                font.pixelSize: 12
                color: Theme.textSecondary
                height: visible ? 18 : 0
                // the alias itself: double-click to edit, right-click for its menu (notation, delete)
                MouseArea {
                    anchors.fill: parent
                    enabled: !node.readOnly && node.canvas !== null
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mouse => {
                        const alias = node.aliasAt(parent, mouse.x, mouse.y)
                        if (mouse.button === Qt.RightButton && alias.length > 0)
                            node.canvas.openNodeMenu(node.elementId, alias)
                    }
                    onDoubleClicked: mouse => {
                        const alias = node.aliasAt(parent, mouse.x, mouse.y)
                        if (alias.length > 0)
                            node.canvas.editElement(alias)
                    }
                }
            }
            Repeater {
                model: node.isFrame ? node.compartments : []
                Column {
                    id: frameCompartment
                    required property var modelData
                    width: parent.width
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: frameCompartment.modelData.title
                        font.pixelSize: 11
                        font.italic: true
                        color: Theme.textSecondary
                    }
                    Repeater {
                        model: frameCompartment.modelData.entries
                        Label {
                            id: frameEntry
                            required property var modelData
                            readonly property string keyword: modelData.keyword !== undefined ? modelData.keyword : ""
                            function escaped(value) { return value.replace(/&/g, "&amp;").replace(/</g, "&lt;") }
                            // "part def PartDef1": the keyword in bold (table 3 "Package with members compartment")
                            textFormat: Text.StyledText
                            // the content of a package listed as text: indented below it
                            readonly property string indent: "&nbsp;".repeat(4 * (modelData.indent !== undefined ? modelData.indent : 0))
                            text: indent + (keyword.length > 0 && modelData.text.indexOf(keyword) === 0
                                            ? "<b>" + escaped(keyword) + "</b>" + escaped(modelData.text.substring(keyword.length))
                                            : escaped(modelData.text))
                            font.pixelSize: 12
                            color: Theme.textPrimary
                            height: 17
                            MouseArea {
                                anchors.fill: parent
                                enabled: !node.readOnly && node.canvas !== null
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: mouse => {
                                    if (mouse.button === Qt.RightButton)
                                        node.canvas.openNodeMenu(node.elementId, frameEntry.modelData.entryId)
                                }
                                onDoubleClicked: node.canvas.editElement(frameEntry.modelData.entryId)
                            }
                        }
                    }
                }
            }
        }

        // Grips: the tab, the top of the body and the border (the inside stays free for the content and the canvas).
        Grip {
            owner: node
            x: 0
            y: 0
            width: frame.tabWidth
            height: frame.tabHeight
        }
        Grip {
            owner: node
            x: 0
            y: frame.tabHeight - 3
            width: parent.width
            height: 8
        }
        Grip {
            owner: node
            x: -4
            y: frame.tabHeight
            width: 8
            height: parent.height - frame.tabHeight
        }
        Grip {
            owner: node
            x: parent.width - 4
            y: frame.tabHeight
            width: 8
            height: parent.height - frame.tabHeight
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
