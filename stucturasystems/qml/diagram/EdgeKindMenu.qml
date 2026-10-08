pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import StructuraSystems

// Choice of the relationship when an arrow is released over a target. Shows only the kinds the
// RelationshipRules allow between the two elements.
Menu {
    id: menu

    property DiagramController controller
    property string source
    property string target
    /** DiagramTypes.EdgeKind values */
    property var kinds: []

    MenuItem {
        text: qsTr("Relationship")
        enabled: false
    }
    MenuSeparator {}

    Repeater {
        model: menu.kinds

        MenuItem {
            required property var modelData
            text: menu.controller ? menu.controller.edgeKindName(modelData) : ""
            onTriggered: menu.controller.createEdge(modelData, menu.source, menu.target)
        }
    }
}
