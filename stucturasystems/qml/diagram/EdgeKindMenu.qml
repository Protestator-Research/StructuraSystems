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

    /** An annotation onto an element with features: which of them the note is about is asked next. */
    signal annotationChosen(string source, string target)

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
            onTriggered: {
                if (modelData === DiagramTypes.Annotation
                        && menu.controller.annotationTargets(menu.source, menu.target).length > 1)
                    menu.annotationChosen(menu.source, menu.target)
                else
                    menu.controller.createEdge(modelData, menu.source, menu.target)
            }
        }
    }
}
