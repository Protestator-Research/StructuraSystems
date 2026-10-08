//
// Enums of the graphical editor that QML needs: palette tools, edge kinds, arrow presets and node kinds.
// Plain (unscoped) enums with unique names, so QML can write e.g. DiagramTypes.ToolPart.
//

#ifndef STRUCTURASYSTEMS_DIAGRAMTYPES_H
#define STRUCTURASYSTEMS_DIAGRAMTYPES_H

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client::DiagramTypes {
    Q_NAMESPACE
    QML_NAMED_ELEMENT(DiagramTypes)

    /// Elements that can be dragged from the palette onto the canvas.
    enum Tool {
        ToolPart,
        ToolPartDefinition,
        ToolPackage,   ///< drawn as frame
        ToolAttribute, ///< must be dropped onto a part / part definition
        ToolAction,    ///< must be dropped onto a part / part definition
        ToolConstraint, ///< "assert constraint" in a part / part definition (or nested in a constraint / calc)
        ToolCalc,       ///< "calc" in a part / part definition (or nested in a constraint / calc)
        ToolConstraintDefinition, ///< "constraint def" (node, like a part definition)
        ToolCalcDefinition,       ///< "calc def" (node, like a part definition)
        ToolStatement             ///< any SysML statement written as text (kept word for word)
    };
    Q_ENUM_NS(Tool)

    /// What an arrow means. Composition is ownership (nesting in the text), the others are relationships.
    enum EdgeKind {
        Composition,
        Connection,
        Flow,
        Specialization,
        Binding,   ///< parametric view: "bind a = b;" or a parameter / argument bound to a value
        ValueFlow, ///< parametric view: the value of an attribute ("= expression", "= calc1")
        Reference  ///< parametric view: a value used in an expression (dashed, derived from the text)
    };
    Q_ENUM_NS(EdgeKind)

    /// Preset of the arrow tool: ask on release, or always use one kind.
    enum ArrowDefault {
        AskOnRelease,
        AlwaysComposition,
        AlwaysConnection,
        AlwaysFlow,
        AlwaysSpecialization
    };
    Q_ENUM_NS(ArrowDefault)

    /// Elements that are drawn as nodes. Attributes, actions, constraints and calculations inside a part are
    /// entries of its compartments; constraints and calculations on package level are nodes of their own
    /// (with the compartments parameters, result, constraint, ...), as is any other statement kept as text.
    enum NodeKind {
        PackageNode,
        PartDefinitionNode,
        PartNode,
        TextualNode,
        ConstraintDefinitionNode,
        CalcDefinitionNode,
        ConstraintNode,
        CalcNode,
        ValueNode,      ///< parametric view: an attribute
        CallNode,       ///< parametric view: "f(a, b)" as value of an attribute
        ExpressionNode  ///< parametric view: any other expression as value of an attribute
    };
    Q_ENUM_NS(NodeKind)
}

#endif //STRUCTURASYSTEMS_DIAGRAMTYPES_H
