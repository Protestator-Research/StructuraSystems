//
// Semantic layer of the diagram editor: one SysML v2 element without any layout information.
//

#ifndef STRUCTURASYSTEMS_DIAGRAM_SYSMLELEMENT_H
#define STRUCTURASYSTEMS_DIAGRAM_SYSMLELEMENT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace StructuraSystems::Diagram {

    using ElementId = std::uint64_t;
    using RelationshipId = std::uint64_t;

    /// Owner id of all top-level elements.
    inline constexpr ElementId RootId = 0;

    enum class ElementKind {
        Package,
        PartDefinition,
        PartUsage,
        Attribute,
        ActionPerformance,
        /// constraint def Name { in x : Real; x > 0 }   (KerML: predicate)
        ConstraintDefinition,
        /// calc def Name { in x : Real; return r : Real = x * 2; }   (KerML: function)
        CalcDefinition,
        /// [assert] constraint name [: Type] { expression }
        ConstraintUsage,
        /// calc name [: Type] { ... }
        CalcUsage,
        /// attribute def Name [:> ...] { attributes }
        AttributeDefinition,
        /// enum def Name { enum a; enum b; }
        EnumerationDefinition,
        /// enum a;  (owned by an enum def)
        EnumerationLiteral,
        /// item def Name { attributes, items }
        ItemDefinition,
        /// [in|out] item name [: Type];
        ItemUsage,
        /// port def Name { in item fuel : Fuel; ... }
        PortDefinition,
        /// port name : [~]PortDef;  (drawn on the border of its part)
        PortUsage,
        /// interface def Name { end a : P; end b : ~P; }
        InterfaceDefinition,
        /// action def Name { action a; then b; ... }
        ActionDefinition,
        /// action name [: Type] [{ ... }]  (also "entry action", "do action", "exit action" in a state);
        /// with a form (ActionForm): "send s to t;", "accept s;", "assign x := 1;", "terminate;", "if c { ... }",
        /// "while c { ... }", "loop { ... } until c;", "for x in xs { ... }" (often unnamed)
        ActionUsage,
        /// decide / merge / fork / join [name];  and the implicit start, done and entry (declared keyword)
        ControlNode,
        /// state def Name { entry; then off; state off; ... }
        StateDefinition,
        /// state name [: Type] [{ ... }]  ("exhibit state" in a part)
        StateUsage,
        /// requirement def [<'R1'>] Name { doc /* ... */ subject ...; require constraint { ... } }
        RequirementDefinition,
        /// requirement [<'R1'>] name [: Type] [{ ... }]  ("objective" in a use case / verification, may be unnamed)
        RequirementUsage,
        /// subject name [: Type];  (of a requirement, use case or verification: the system boundary)
        Subject,
        /// actor name [: Type];  ("stakeholder" in a requirement)
        Actor,
        /// use case def Name { subject ...; actor ...; objective { ... } include use case ...; }
        UseCaseDefinition,
        /// use case name [: Type] [{ ... }]  ("include use case" inside another use case)
        UseCaseUsage,
        /// verification def Name { subject ...; objective { verify requirement; } }
        VerificationDefinition,
        /// verification name [: Type] [{ ... }]
        VerificationUsage,
        /// occurrence def Name { ref part driver : Driver { event occurrence sent; } message ... }  (an interaction)
        OccurrenceDefinition,
        /// occurrence name [: Type] [{ ... }]
        OccurrenceUsage,
        /// event occurrence name [: Type];  (on a lifeline: a part of an interaction)
        EventOccurrence,
        /// allocation def Name { end logical : LogicalElement; end physical : PhysicalElement; }
        AllocationDefinition,
        /// analysis def Name { subject ...; objective { ... } in attribute ...; action ...; return result : T = ...; }
        AnalysisDefinition,
        /// analysis name [: Type] [{ subject vehicle = vehicle_c1; in scenario = cityScenario; }]
        AnalysisUsage,
        /// connection def Name { end [1] part a : A; end [1] part b : B; }  (a part definition between its ends)
        ConnectionDefinition,
        /// flow def Name { ref :>> payload : Fuel; end port out : OutPort; end port in : InPort; }
        FlowDefinition,
        /// metadata def Name [:> ...] { attribute isMandatory : Boolean; }
        MetadataDefinition,
        /// metadata [name :] Type [about a, b] [{ attr = value; }]  /  @Type { ... }  (usually unnamed)
        MetadataUsage,
        /// view def Name { satisfy viewpoint; filter @SysML::PartUsage; render asTreeDiagram; }
        ViewDefinition,
        /// view name [: Type] { expose vehicle::**; render asTreeDiagram; view sub { ... } }
        ViewUsage,
        /// [public|private] import [all] Target;   Target as written: "ISQ::TorqueValue", "ISQ::*", "a::**[@Safety]"
        Import,
        /// [public|private] alias Name for Target [{ ... }]
        Alias,
        /// expose Target;  (in a view, as an import: "vehicle::**", "vehicle::**[@Safety]")
        Expose,
        /// render Target;  (in a view (definition): "asTreeDiagram")
        Render,
        /// filter Target;  (in a package or view; Target is the condition: "@Safety and (as Safety).isMandatory")
        Filter,
        /// Any other statement kept word for word (assert constraint, function, constraint def, import,
        /// comment, ...). Shown in a compartment of its owner or as node; never interpreted.
        Textual
    };

    /// Relationships that are drawn as edges. Composition is NOT part of this list:
    /// it is ownership and therefore modelled as owner/child (nesting in the text).
    enum class RelationshipKind {
        Connection,
        Flow,
        Specialization,
        /// bind source = target;  between two attributes (also parameters of constraints / calculations)
        Binding,
        /// interface [name] [: Type] connect a to b;  between two ports
        Interface,
        /// first a then b;  (written as "then b;" right after a when possible), with an optional guard
        Succession,
        /// transition [name] first a [accept trigger] [if guard] [do effect] then b;
        Transition,
        /// satisfy target by source;  (source: the part / port that satisfies, target: a requirement usage)
        Satisfy,
        /// verify target;  inside the objective of the source (a verification); target: a requirement usage
        Verify,
        /// include target;  inside the source use case; target: a use case usage
        Include,
        /// [then] message [name] [of Payload] from a to b;  between two event occurrences (or occurrences)
        Message,
        /// allocate a to b [{ allocate ... }];  allocation [name] [: Type] allocate a to b;  (nested ones: Parent)
        Allocation,
        /// dependency [name] from a to b;  between any two named elements
        Dependency,
        /// frame target;  inside the source requirement (a viewpoint, ...); target: a concern (requirement usage)
        Frame
    };

    /// import, alias, expose, render, filter: a keyword and a target written as it is (no element of their own).
    [[nodiscard]] inline bool isTargetKind(ElementKind kind) {
        return kind == ElementKind::Import || kind == ElementKind::Alias || kind == ElementKind::Expose ||
               kind == ElementKind::Render || kind == ElementKind::Filter;
    }

    /// import, expose, render, filter: never named.
    [[nodiscard]] inline bool isAnonymousKind(ElementKind kind) {
        return kind == ElementKind::Import || kind == ElementKind::Expose || kind == ElementKind::Render ||
               kind == ElementKind::Filter;
    }

    [[nodiscard]] inline bool isViewKind(ElementKind kind) {
        return kind == ElementKind::ViewDefinition || kind == ElementKind::ViewUsage;
    }

    /// constraint def, calc def and their usages: own parameters (directed attributes) and a body expression.
    [[nodiscard]] inline bool isConstraintOrCalc(ElementKind kind) {
        return kind == ElementKind::ConstraintDefinition || kind == ElementKind::CalcDefinition ||
               kind == ElementKind::ConstraintUsage || kind == ElementKind::CalcUsage;
    }

    [[nodiscard]] inline bool isConstraintKind(ElementKind kind) {
        return kind == ElementKind::ConstraintDefinition || kind == ElementKind::ConstraintUsage;
    }

    [[nodiscard]] inline bool isDefinitionKind(ElementKind kind) {
        return kind == ElementKind::PartDefinition || kind == ElementKind::ConstraintDefinition ||
               kind == ElementKind::CalcDefinition || kind == ElementKind::AttributeDefinition ||
               kind == ElementKind::EnumerationDefinition || kind == ElementKind::ItemDefinition ||
               kind == ElementKind::PortDefinition || kind == ElementKind::InterfaceDefinition ||
               kind == ElementKind::ActionDefinition || kind == ElementKind::StateDefinition ||
               kind == ElementKind::RequirementDefinition || kind == ElementKind::UseCaseDefinition ||
               kind == ElementKind::VerificationDefinition || kind == ElementKind::OccurrenceDefinition ||
               kind == ElementKind::AllocationDefinition || kind == ElementKind::AnalysisDefinition ||
               kind == ElementKind::ConnectionDefinition || kind == ElementKind::FlowDefinition ||
               kind == ElementKind::MetadataDefinition || kind == ElementKind::ViewDefinition;
    }

    /// connection def / flow def: part definitions whose usages connect their ends.
    [[nodiscard]] inline bool isConnectionDefinitionKind(ElementKind kind) {
        return kind == ElementKind::ConnectionDefinition || kind == ElementKind::FlowDefinition;
    }

    [[nodiscard]] inline bool isAnalysisKind(ElementKind kind) {
        return kind == ElementKind::AnalysisDefinition || kind == ElementKind::AnalysisUsage;
    }

    [[nodiscard]] inline bool isOccurrenceKind(ElementKind kind) {
        return kind == ElementKind::OccurrenceDefinition || kind == ElementKind::OccurrenceUsage;
    }

    /// Ends of messages: events and the occurrences they happen to (parts, items, ports, actions, occurrences).
    [[nodiscard]] inline bool isMessageEnd(ElementKind kind) {
        return kind == ElementKind::EventOccurrence || kind == ElementKind::OccurrenceUsage ||
               kind == ElementKind::PartUsage || kind == ElementKind::ItemUsage || kind == ElementKind::PortUsage ||
               kind == ElementKind::ActionUsage || kind == ElementKind::ActionPerformance;
    }

    [[nodiscard]] inline bool isRequirementKind(ElementKind kind) {
        return kind == ElementKind::RequirementDefinition || kind == ElementKind::RequirementUsage;
    }

    [[nodiscard]] inline bool isUseCaseKind(ElementKind kind) {
        return kind == ElementKind::UseCaseDefinition || kind == ElementKind::UseCaseUsage;
    }

    [[nodiscard]] inline bool isVerificationKind(ElementKind kind) {
        return kind == ElementKind::VerificationDefinition || kind == ElementKind::VerificationUsage;
    }

    /// Use cases, verifications and analyses: cases with subject, actors and an objective (and a flow of actions).
    [[nodiscard]] inline bool isCaseKind(ElementKind kind) {
        return isUseCaseKind(kind) || isVerificationKind(kind) || isAnalysisKind(kind);
    }

    /// Elements whose inside is a flow (actions, cases) or a state machine (states).
    [[nodiscard]] inline bool isBehaviorKind(ElementKind kind) {
        return kind == ElementKind::ActionDefinition || kind == ElementKind::ActionUsage ||
               kind == ElementKind::StateDefinition || kind == ElementKind::StateUsage || isCaseKind(kind);
    }

    /// Elements that may be unnamed: "require constraint { ... }", "assume constraint { ... }", "objective { ... }",
    /// actions such as "then send s to t;", "assign x := 1;", "if c { ... }", control nodes such as "then fork;".
    [[nodiscard]] inline bool mayBeUnnamed(ElementKind kind) {
        return kind == ElementKind::ConstraintUsage || kind == ElementKind::RequirementUsage ||
               kind == ElementKind::ActionUsage || kind == ElementKind::ControlNode ||
               kind == ElementKind::MetadataUsage || isAnonymousKind(kind);
    }

    /// Ends of successions: actions, performed actions, control nodes, states and event occurrences.
    [[nodiscard]] inline bool isSuccessionEnd(ElementKind kind) {
        return kind == ElementKind::ActionUsage || kind == ElementKind::ActionPerformance ||
               kind == ElementKind::ControlNode || kind == ElementKind::StateUsage || kind == ElementKind::EventOccurrence ||
               kind == ElementKind::OccurrenceUsage || kind == ElementKind::AnalysisUsage;
    }

    /// "snapshot", "timeslice", "snapshot part", "timeslice item", ...: a portion of the lifetime of its owner.
    [[nodiscard]] inline bool isTemporalKeyword(const std::optional<std::string> &keyword) {
        return keyword && (keyword->rfind("snapshot", 0) == 0 || keyword->rfind("timeslice", 0) == 0);
    }

    /// Control nodes that are not declared in the text but stand for "start", "done" and "entry".
    [[nodiscard]] inline bool isImplicitControlKeyword(const std::string &keyword) {
        return keyword == "start" || keyword == "done" || keyword == "entry";
    }

    /// Definitions of phase 2 (structure): attribute, enumeration, item, port and interface definitions.
    [[nodiscard]] inline bool isTypeDefinitionKind(ElementKind kind) {
        return kind == ElementKind::AttributeDefinition || kind == ElementKind::EnumerationDefinition ||
               kind == ElementKind::ItemDefinition || kind == ElementKind::PortDefinition ||
               kind == ElementKind::InterfaceDefinition;
    }

    /// Features written with a name and an optional type, e.g. "port p : P;", "item fuel : Fuel;".
    [[nodiscard]] inline bool isUsageKind(ElementKind kind) {
        return kind == ElementKind::PartUsage || kind == ElementKind::Attribute || kind == ElementKind::ActionPerformance ||
               kind == ElementKind::ConstraintUsage || kind == ElementKind::CalcUsage || kind == ElementKind::ItemUsage ||
               kind == ElementKind::PortUsage || kind == ElementKind::EnumerationLiteral ||
               kind == ElementKind::ActionUsage || kind == ElementKind::ControlNode || kind == ElementKind::StateUsage ||
               kind == ElementKind::RequirementUsage || kind == ElementKind::Subject || kind == ElementKind::Actor ||
               kind == ElementKind::UseCaseUsage || kind == ElementKind::VerificationUsage ||
               kind == ElementKind::OccurrenceUsage || kind == ElementKind::EventOccurrence ||
               kind == ElementKind::AnalysisUsage || kind == ElementKind::MetadataUsage || kind == ElementKind::ViewUsage;
    }

    /// SysML multiplicity, e.g. [6], [0..1], [1..*].
    struct Multiplicity {
        std::size_t Lower = 1;
        std::optional<std::size_t> Upper = 1; ///< std::nullopt means '*'

        static Multiplicity exactly(std::size_t count);
        static Multiplicity range(std::size_t lower, std::optional<std::size_t> upper);

        [[nodiscard]] std::string toString() const;

        bool operator==(const Multiplicity &other) const = default;
    };

    /// How an ActionPerformance is written.
    enum class PerformStyle {
        DeclareAction,   ///< perform action name;   (self-contained, always valid)
        ReferenceAction  ///< perform name;          (name must resolve to an existing action)
    };

    /// What an action usage does besides being an action ("action a;" = Plain).
    enum class ActionForm {
        Plain,
        Send,      ///< send <clause>;            e.g. "send new Show(p) to screen", "send s via port"
        Accept,    ///< accept <clause>;          e.g. "scene : Scene via viewPort", "at t", "after 5 [s]", "when c"
        Assign,    ///< assign <clause>;          e.g. "position := dynamics.x_out"
        Terminate, ///< terminate [<clause>];     e.g. "processor.workflowProcess"
        If,        ///< if <clause> { ... } [else { ... } | else if ...]
        While,     ///< while <clause> { ... } [until <until>;]
        Loop,      ///< loop { ... } [until <until>;]
        For        ///< for <clause> { ... }     e.g. "vehiclePower in powerProfile"
    };

    /// if / while / loop / for: written with a body in braces.
    [[nodiscard]] inline bool isControlStructure(ActionForm form) {
        return form == ActionForm::If || form == ActionForm::While || form == ActionForm::Loop || form == ActionForm::For;
    }

    /// Keyword of an action form ("send", "if", ...; empty for Plain).
    [[nodiscard]] std::string toKeyword(ActionForm form);

    class SysMLElement {
    public:
        SysMLElement(ElementId id, ElementKind kind, std::string name, ElementId owner);

        [[nodiscard]] ElementId id() const { return Id; }
        [[nodiscard]] ElementKind kind() const { return Kind; }
        [[nodiscard]] const std::string &name() const { return Name; }
        [[nodiscard]] ElementId owner() const { return Owner; }
        [[nodiscard]] const std::vector<ElementId> &ownedElements() const { return OwnedElements; }

        /// Type of a usage, e.g. "Engine" for "part engine : Engine". Empty = untyped.
        [[nodiscard]] const std::string &typeName() const { return TypeName; }
        [[nodiscard]] const std::optional<Multiplicity> &multiplicity() const { return MultiplicityValue; }
        [[nodiscard]] PerformStyle performStyle() const { return Perform; }

        /// Words written before the declaration keyword, in their order, e.g. {"derived", "#measurable"}.
        [[nodiscard]] const std::vector<std::string> &prefixes() const { return Prefixes; }
        /// Value of a usage ("= decodeFuelTrim(VariableA)"): operator ("=", ":=", "default", ...) and expression.
        [[nodiscard]] const std::string &valueOperator() const { return ValueOperator; }
        [[nodiscard]] const std::string &value() const { return Value; }

        /// Textual only: the complete statement (indented relative to its first line, no trailing newline)
        /// and its leading keywords, e.g. "assert constraint", "function", "constraint def", "comment".
        [[nodiscard]] const std::string &text() const { return Text; }
        [[nodiscard]] const std::string &keyword() const { return Keyword; }
        /// Constraint/calc only: the body expression (constraint condition or result expression of a calc),
        /// written last in the body without ';'. Several lines possible, indented relative to the first one.
        [[nodiscard]] const std::string &expression() const { return Expression; }
        /// Keyword as written, if it differs from the default of the kind: "function" (KerML) instead of
        /// "calc def", "assert constraint" instead of "constraint", "feature" or "" (just "in x : Real")
        /// instead of "attribute". std::nullopt = default keyword.
        [[nodiscard]] const std::optional<std::string> &declaredKeyword() const { return DeclaredKeyword; }
        /// Specializations whose target is not part of the model (e.g. a library type), as written after ":>".
        [[nodiscard]] const std::vector<std::string> &externalSpecializations() const { return ExternalSpecializations; }
        /// Short name (id) as written between < >, e.g. "'R1'" or "R1" (empty = none).
        [[nodiscard]] const std::string &shortName() const { return ShortName; }
        /// Written after the multiplicity: "ordered", "nonunique" or "ordered nonunique" (empty = none).
        [[nodiscard]] const std::string &multiplicityQualifiers() const { return MultiplicityQualifiers; }

        /// Features it redefines, as written after ":>>" / "redefines" (e.g. "eng", "Vehicle::mass").
        [[nodiscard]] const std::vector<std::string> &redefinitions() const { return Redefinitions; }
        /// ":>>" or "redefines" (empty if it redefines nothing).
        [[nodiscard]] const std::string &redefinitionKeyword() const { return RedefinitionKeyword; }
        /// "part redefines cyl[4];", ":>> mass = 2000.0;": no name of its own, the name is the one of the redefined
        /// feature (the last segment of the first redefinition).
        [[nodiscard]] bool nameFromRedefinition() const { return NameFromRedefinition; }

        /// Action usage only: send / accept / assign / terminate / if / while / loop / for, with what follows the
        /// keyword ("new Show(p) to screen", "position := x", the condition, "v in values", ...).
        [[nodiscard]] ActionForm actionForm() const { return Form; }
        [[nodiscard]] const std::string &actionClause() const { return ActionClause; }
        /// loop / while: condition after "until" (empty = none).
        [[nodiscard]] const std::string &untilCondition() const { return Until; }
        /// The else branch of the if action that owns it ("} else { ... }", "} else if c { ... }").
        [[nodiscard]] bool isElseBranch() const { return ElseBranch; }
        /// if / while / loop / for / else: the name written in front of the body ("loop action charging { ... }")
        /// instead of in front of the keyword ("action charging loop { ... }").
        [[nodiscard]] bool namedInBody() const { return NamedInBody; }
        /// Action / control node: declared as "then action b;" / "then fork f;" behind the source of its succession
        /// (written so again as long as that succession exists and nothing else is declared in between).
        [[nodiscard]] bool declaredWithThen() const { return DeclaredWithThen; }
        /// import / alias / expose / render / filter: what follows the keyword (alias: after "for"), as written.
        [[nodiscard]] const std::string &target() const { return Target; }
        /// Metadata usage: the annotated elements after "about", as written (empty: its owner).
        [[nodiscard]] const std::vector<std::string> &about() const { return About; }
        /// State usage / definition: its nested states are concurrent ("state s parallel { ... }").
        [[nodiscard]] bool isParallel() const { return Parallel; }

    private:
        friend class SemanticModel;

        ElementId Id;
        ElementKind Kind;
        std::string Name;
        ElementId Owner;
        std::vector<ElementId> OwnedElements;
        std::string TypeName;
        std::optional<Multiplicity> MultiplicityValue;
        PerformStyle Perform = PerformStyle::DeclareAction;
        std::vector<std::string> Prefixes;
        std::string ValueOperator;
        std::string Value;
        std::string Text;
        std::string Keyword;
        std::vector<std::string> ExternalSpecializations;
        std::string Expression;
        std::optional<std::string> DeclaredKeyword;
        std::string ShortName;
        std::string MultiplicityQualifiers;
        std::vector<std::string> Redefinitions;
        std::string RedefinitionKeyword;
        bool NameFromRedefinition = false;
        ActionForm Form = ActionForm::Plain;
        std::string ActionClause;
        std::string Until;
        bool ElseBranch = false;
        bool NamedInBody = false;
        bool Parallel = false;
        bool DeclaredWithThen = false;
        std::string Target;
        std::vector<std::string> About;
    };

    struct Relationship {
        RelationshipId Id;
        RelationshipKind Kind;
        ElementId Source;
        ElementId Target;
        /// Connection / flow / interface: optional name and type ("connection c : Pipe connect ...",
        /// "flow of Fuel from ...", "interface : FuelInterface connect ...").
        std::string Name;
        std::string TypeName;
        /// Succession / transition: "if" condition ("else" for the else branch of a decision)
        std::string Guard;
        /// Transition: what follows "accept", e.g. "StartSignal"
        std::string Trigger;
        /// Transition: what follows "do", e.g. "action ignite"
        std::string Effect;
        /// Message: written as "then message ..." (after the lexically previous message / occurrence)
        bool Sequenced = false;
        /// Allocation: the allocation this one refines (written inside its body), std::nullopt = top level
        std::optional<RelationshipId> Parent;
        /// Flow: the flow definition after ':' ("flow f : FuelFlow of Fuel from ..."); empty = none
        std::string Definition;
        /// Message / allocation / dependency: the body it is declared in (RootId = top level);
        /// std::nullopt = the innermost element owning both ends (as for all other relationships)
        std::optional<ElementId> Context;
    };

    [[nodiscard]] std::string toString(ElementKind kind);
    /// Keyword of the textual notation, e.g. "part def", "port", "enum" (empty for Textual).
    [[nodiscard]] std::string defaultKeyword(ElementKind kind);
    [[nodiscard]] std::string toString(RelationshipKind kind);
}

#endif //STRUCTURASYSTEMS_DIAGRAM_SYSMLELEMENT_H
