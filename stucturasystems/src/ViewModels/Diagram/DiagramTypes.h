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
        ToolStatement,            ///< any SysML statement written as text (kept word for word)
        ToolAttributeDefinition,  ///< "attribute def"
        ToolEnumDefinition,       ///< "enum def" (attributes dropped onto it are its values)
        ToolItemDefinition,       ///< "item def"
        ToolPortDefinition,       ///< "port def"
        ToolInterfaceDefinition,  ///< "interface def" (ports dropped onto it are its ends)
        ToolPort,                 ///< "port" on a part (drawn on its border), port def or interface def
        ToolItem,                 ///< "item" in a part, item def or port def
        ToolActionDefinition,     ///< "action def"
        ToolStateDefinition,      ///< "state def"
        ToolState,                ///< "state" (in a part: "exhibit state")
        // behavior view (inside an action or state)
        ToolActionUsage,          ///< "action" (onto a state: "do action")
        ToolDecision,             ///< "decide"
        ToolMerge,                ///< "merge"
        ToolFork,                 ///< "fork"
        ToolJoin,                 ///< "join"
        ToolStart,                ///< "first start;"
        ToolDone,                 ///< "then done;"
        ToolInitial,              ///< "entry; then ...;" (initial state)
        // requirements, use cases, verifications
        ToolRequirementDefinition, ///< "requirement def"
        ToolRequirement,           ///< "requirement" (usage)
        ToolUseCaseDefinition,     ///< "use case def"
        ToolUseCase,               ///< "use case" (usage)
        ToolVerificationDefinition, ///< "verification def"
        ToolVerification,          ///< "verification" (usage)
        ToolSubject,               ///< "subject" of a requirement / use case / verification
        ToolActor,                 ///< "actor" of a use case ("stakeholder" of a requirement)
        ToolObjective,             ///< "objective { ... }" of a use case / verification
        // interactions and allocations
        ToolOccurrenceDefinition,  ///< "occurrence def" (an interaction: lifelines, events, messages)
        ToolOccurrence,            ///< "occurrence" (usage)
        ToolEvent,                 ///< "event occurrence" in a part / item / port / action (on its lifeline)
        ToolAllocationDefinition,  ///< "allocation def" (parts dropped onto it are its ends)
        ToolLifeline,              ///< sequence view: "ref part" in the interaction
        // analysis, variability, individuals and their lifetime
        ToolAnalysisDefinition,    ///< "analysis def"
        ToolAnalysis,              ///< "analysis" (usage)
        ToolVariationDefinition,   ///< "variation part def" (variants dropped onto it are its choices)
        ToolVariant,               ///< "variant ..." in a variation (of the kind of the variation)
        ToolIndividualDefinition,  ///< "individual part def"
        ToolIndividual,            ///< "individual part"
        ToolSnapshot,              ///< "snapshot" in a part / individual / occurrence
        ToolTimeslice,             ///< "timeslice" in a part / individual / occurrence
        // actions of phase 7 (behavior view) and connection / flow definitions (structure view)
        ToolSendAction,            ///< "send Signal() to target;"
        ToolAcceptAction,          ///< "accept Signal;" (also "accept after 5 [s];", "accept when c;")
        ToolAssignAction,          ///< "assign x := 0;"
        ToolTerminateAction,       ///< "terminate;"
        ToolIfAction,              ///< "if condition { ... }"
        ToolWhileAction,           ///< "while condition { ... }"
        ToolLoopAction,            ///< "loop { ... } until condition;"
        ToolForAction,             ///< "for x in values { ... }"
        ToolConnectionDefinition,  ///< "connection def" (its ends: "end part a; end part b;")
        ToolFlowDefinition,        ///< "flow def" (its ends: "end port out; end port in;")
        // metadata, views and namespaces (phase 8)
        ToolMetadataDefinition,    ///< "metadata def"
        ToolMetadata,              ///< onto an element: "@Name;" in it; onto the canvas: "metadata Name about ...;"
        ToolViewDefinition,        ///< "view def"
        ToolView,                  ///< "view" (usage); onto a view: a sub-view
        ToolViewpoint,             ///< "viewpoint" (a requirement framing concerns)
        ToolConcern,               ///< "concern" (a requirement of stakeholders)
        ToolExpose,                ///< "expose a::**;" onto a view
        ToolRender,                ///< "render asTreeDiagram;" onto a view (definition)
        ToolFilter,                ///< "filter @Metadata;" onto a package or view
        ToolImport,                ///< "private import Lib::*;" in a package / onto the canvas
        ToolAlias                  ///< "alias Name for Element;"
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
        Reference, ///< parametric view: a value used in an expression (dashed, derived from the text)
        Interface, ///< interface between two ports
        Succession, ///< behavior view: "then b;" (label: [guard])
        Transition, ///< behavior view: transition between states (label: trigger [guard] / effect)
        Satisfy,    ///< «satisfy»: part / port -> requirement (dashed)
        Verify,     ///< «verify»: verification -> requirement (dashed)
        Include,    ///< «include»: use case -> included use case (dashed)
        Association, ///< use case view: actor - use case (plain line)
        Message,    ///< sequence view: message from one event to another (open arrow)
        Allocation, ///< «allocate»: allocate a to b (open arrow)
        Dependency, ///< dependency from a to b (dashed, open arrow)
        Temporal,   ///< a snapshot / time slice of its owner: a line with a half disc at the owner
        Frame       ///< «frame»: requirement (viewpoint) -> concern (dashed)
    };
    Q_ENUM_NS(EdgeKind)

    /// Preset of the arrow tool: ask on release, or always use one kind.
    enum ArrowDefault {
        AskOnRelease,
        AlwaysComposition,
        AlwaysConnection,
        AlwaysFlow,
        AlwaysSpecialization,
        AlwaysInterface,
        AlwaysSatisfy,
        AlwaysVerify,
        AlwaysAllocation,
        AlwaysDependency
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
        ExpressionNode, ///< parametric view: any other expression as value of an attribute
        DefinitionNode, ///< attribute / enum / item / port / interface / action / state definition
        PortNode,       ///< a port on the border of its part
        ActionNode,     ///< behavior view: an action (or performed action)
        StateNode,      ///< behavior view: a state with its entry / do / exit actions
        DecisionNode,   ///< behavior view: decide / merge (diamond)
        ForkNode,       ///< behavior view: fork / join (bar)
        StartNode,      ///< behavior view: start / entry (filled circle)
        DoneNode,       ///< behavior view: done (circle with dot)
        RequirementNode, ///< requirement def / requirement: text (doc), subject, stakeholders, require / assume
        CaseNode,       ///< use case / verification (def or usage) in the structure view
        UseCaseNode,    ///< use case view: a use case (ellipse)
        ActorNode,      ///< use case view: an actor (stick figure), one per name and type
        BoundaryNode,   ///< use case view: the subject (system boundary around its use cases)
        LifelineNode,   ///< sequence view: a part (or item, occurrence) with its header and dashed line
        LifelineGroupNode, ///< sequence view: header of a part whose parts are the lifelines below it
        EventNode       ///< sequence view: an event occurrence on its lifeline (or where a message starts / ends)
    };
    Q_ENUM_NS(NodeKind)
}

#endif //STRUCTURASYSTEMS_DIAGRAMTYPES_H
