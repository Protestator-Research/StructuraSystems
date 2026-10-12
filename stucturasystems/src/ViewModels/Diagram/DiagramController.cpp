//
// Editing logic of the graphical editor.
//

#include "DiagramController.h"
#include "DiagramGeometry.h"

#include <QMetaObject>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/RelationshipRules.h>

#include <algorithm>
#include <functional>
#include <exception>
#include <iterator>
#include <vector>

namespace StructuraSystems::Client {

    using Diagram::ElementId;
    using Diagram::ElementKind;
    using Diagram::Multiplicity;
    using Diagram::RelationshipKind;
    using Diagram::RelationshipRules;
    using Diagram::RootId;
    using Diagram::SysMLTextImporter;

    namespace {
        constexpr qreal DefaultPartWidth = 160.0;
        constexpr qreal DefaultPartHeight = 64.0;
        constexpr qreal DefaultFrameWidth = 360.0;
        constexpr qreal DefaultFrameHeight = 240.0;
        constexpr qreal MinimumFrameWidth = 160.0;
        constexpr qreal MinimumFrameHeight = 90.0;
        /// Frames are grabbed at their border (this distance) and at their title band.
        constexpr qreal FrameBorderGrip = 6.0;
        constexpr qreal FrameTitleHeight = 30.0;
        constexpr qreal EdgeHitDistance = 6.0;
        constexpr qreal CanvasMargin = 600.0;

        bool isNodeKind(ElementKind kind) {
            return kind == ElementKind::Package || kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage ||
                   Diagram::isTypeDefinitionKind(kind) || kind == ElementKind::ActionDefinition ||
                   kind == ElementKind::StateDefinition || kind == ElementKind::RequirementDefinition ||
                   kind == ElementKind::UseCaseDefinition || kind == ElementKind::VerificationDefinition ||
                   kind == ElementKind::OccurrenceDefinition || kind == ElementKind::AllocationDefinition ||
                   kind == ElementKind::AnalysisDefinition || Diagram::isConnectionDefinitionKind(kind) ||
                   kind == ElementKind::MetadataDefinition || kind == ElementKind::ViewDefinition;
        }

        /// "objective { ... }" of a use case / verification (shown in its compartment, not as node).
        bool isObjective(const Diagram::SysMLElement &element) {
            return element.kind() == ElementKind::RequirementUsage && element.declaredKeyword() == "objective";
        }

        bool isRequirementOwner(ElementKind kind) {
            return Diagram::isRequirementKind(kind);
        }

        using Diagram::isConstraintOrCalc;

        constexpr qreal PortSize = 14.0;
        constexpr qreal PortSpacing = 24.0;
        constexpr qreal PortTop = 30.0;

        /// What a palette entry becomes inside an owner of the given kind.
        ElementKind featureKind(ElementKind owner, int tool) {
            switch (tool) {
                case DiagramTypes::ToolAction:
                    // in a part: "perform action"; in an action, state or case: an action of its own
                    return Diagram::isBehaviorKind(owner) ? ElementKind::ActionUsage : ElementKind::ActionPerformance;
                case DiagramTypes::ToolSubject:
                    return ElementKind::Subject;
                case DiagramTypes::ToolActor:
                    return ElementKind::Actor;
                case DiagramTypes::ToolObjective:
                    return ElementKind::RequirementUsage;
                case DiagramTypes::ToolConstraint:
                    return ElementKind::ConstraintUsage;
                case DiagramTypes::ToolCalc:
                    return ElementKind::CalcUsage;
                case DiagramTypes::ToolPort:
                    return ElementKind::PortUsage;
                case DiagramTypes::ToolItem:
                    return ElementKind::ItemUsage;
                case DiagramTypes::ToolState:
                    return ElementKind::StateUsage;
                case DiagramTypes::ToolActionUsage:
                    return ElementKind::ActionUsage;
                case DiagramTypes::ToolEvent:
                    return ElementKind::EventOccurrence;
                case DiagramTypes::ToolOccurrence:
                case DiagramTypes::ToolSnapshot:
                case DiagramTypes::ToolTimeslice:
                    return ElementKind::OccurrenceUsage;
                case DiagramTypes::ToolVariant:
                    // a choice of the kind of the variation
                    switch (owner) {
                        case ElementKind::AttributeDefinition:
                            return ElementKind::Attribute;
                        case ElementKind::ItemDefinition:
                        case ElementKind::ItemUsage:
                            return ElementKind::ItemUsage;
                        case ElementKind::ActionDefinition:
                        case ElementKind::ActionUsage:
                            return ElementKind::ActionUsage;
                        case ElementKind::PortDefinition:
                        case ElementKind::PortUsage:
                            return ElementKind::PortUsage;
                        default:
                            return ElementKind::PartUsage;
                    }
                case DiagramTypes::ToolMetadata:
                    return ElementKind::MetadataUsage;
                case DiagramTypes::ToolExpose:
                    return ElementKind::Expose;
                case DiagramTypes::ToolRender:
                    return ElementKind::Render;
                case DiagramTypes::ToolFilter:
                    return ElementKind::Filter;
                default:
                    return owner == ElementKind::EnumerationDefinition ? ElementKind::EnumerationLiteral
                                                                       : ElementKind::Attribute;
            }
        }

        /// in / out / inout / return of a parameter (empty if the attribute is not a parameter).
        std::string directionOf(const Diagram::SysMLElement &element) {
            std::string direction;
            for (const auto &prefix: element.prefixes()) {
                if (prefix == "in" || prefix == "out" || prefix == "inout" || prefix == "return")
                    direction = prefix;
            }
            return direction;
        }

        bool isPartLike(ElementKind kind) {
            return kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage;
        }

        /// Parts (and occurrences) whose parts are drawn as nodes of their own, with a composition edge.
        bool composes(ElementKind kind) {
            return isPartLike(kind) || Diagram::isOccurrenceKind(kind);
        }

        std::optional<RelationshipKind> toRelationshipKind(int kind) {
            switch (kind) {
                case DiagramTypes::Connection:
                    return RelationshipKind::Connection;
                case DiagramTypes::Flow:
                    return RelationshipKind::Flow;
                case DiagramTypes::Specialization:
                    return RelationshipKind::Specialization;
                case DiagramTypes::Interface:
                    return RelationshipKind::Interface;
                case DiagramTypes::Satisfy:
                    return RelationshipKind::Satisfy;
                case DiagramTypes::Verify:
                    return RelationshipKind::Verify;
                case DiagramTypes::Include:
                    return RelationshipKind::Include;
                case DiagramTypes::Message:
                    return RelationshipKind::Message;
                case DiagramTypes::Allocation:
                    return RelationshipKind::Allocation;
                case DiagramTypes::Dependency:
                    return RelationshipKind::Dependency;
                case DiagramTypes::Frame:
                    return RelationshipKind::Frame;
                default:
                    return std::nullopt;
            }
        }

        DiagramTypes::EdgeKind toEdgeKind(RelationshipKind kind) {
            switch (kind) {
                case RelationshipKind::Connection:
                    return DiagramTypes::Connection;
                case RelationshipKind::Flow:
                    return DiagramTypes::Flow;
                case RelationshipKind::Specialization:
                    return DiagramTypes::Specialization;
                case RelationshipKind::Binding:
                    return DiagramTypes::Binding;
                case RelationshipKind::Interface:
                    return DiagramTypes::Interface;
                case RelationshipKind::Succession:
                    return DiagramTypes::Succession;
                case RelationshipKind::Transition:
                    return DiagramTypes::Transition;
                case RelationshipKind::Satisfy:
                    return DiagramTypes::Satisfy;
                case RelationshipKind::Verify:
                    return DiagramTypes::Verify;
                case RelationshipKind::Include:
                    return DiagramTypes::Include;
                case RelationshipKind::Message:
                    return DiagramTypes::Message;
                case RelationshipKind::Allocation:
                    return DiagramTypes::Allocation;
                case RelationshipKind::Dependency:
                    return DiagramTypes::Dependency;
                case RelationshipKind::Frame:
                    return DiagramTypes::Frame;
            }
            return DiagramTypes::Connection;
        }

        std::optional<int> presetEdgeKind(DiagramTypes::ArrowDefault value) {
            switch (value) {
                case DiagramTypes::AlwaysComposition:
                    return DiagramTypes::Composition;
                case DiagramTypes::AlwaysConnection:
                    return DiagramTypes::Connection;
                case DiagramTypes::AlwaysFlow:
                    return DiagramTypes::Flow;
                case DiagramTypes::AlwaysSpecialization:
                    return DiagramTypes::Specialization;
                case DiagramTypes::AlwaysInterface:
                    return DiagramTypes::Interface;
                case DiagramTypes::AlwaysSatisfy:
                    return DiagramTypes::Satisfy;
                case DiagramTypes::AlwaysVerify:
                    return DiagramTypes::Verify;
                case DiagramTypes::AlwaysAllocation:
                    return DiagramTypes::Allocation;
                case DiagramTypes::AlwaysDependency:
                    return DiagramTypes::Dependency;
                case DiagramTypes::AskOnRelease:
                    break;
            }
            return std::nullopt;
        }

        constexpr int MaximumEntryLength = 90;

        QString elide(const QString &text, int length = MaximumEntryLength) {
            return text.size() <= length ? text : text.left(length - 1) + QChar(0x2026);
        }

        QString kindText(ElementKind kind) {
            switch (kind) {
                case ElementKind::Package:
                    return QObject::tr("package");
                case ElementKind::PartDefinition:
                    return QObject::tr("part definition");
                case ElementKind::PartUsage:
                    return QObject::tr("part");
                case ElementKind::Attribute:
                    return QObject::tr("attribute");
                case ElementKind::ActionPerformance:
                    return QObject::tr("action");
                case ElementKind::ConstraintDefinition:
                    return QObject::tr("constraint definition");
                case ElementKind::CalcDefinition:
                    return QObject::tr("calculation definition");
                case ElementKind::ConstraintUsage:
                    return QObject::tr("constraint");
                case ElementKind::CalcUsage:
                    return QObject::tr("calculation");
                case ElementKind::AttributeDefinition:
                    return QObject::tr("attribute definition");
                case ElementKind::EnumerationDefinition:
                    return QObject::tr("enumeration definition");
                case ElementKind::EnumerationLiteral:
                    return QObject::tr("enumeration value");
                case ElementKind::ItemDefinition:
                    return QObject::tr("item definition");
                case ElementKind::ItemUsage:
                    return QObject::tr("item");
                case ElementKind::PortDefinition:
                    return QObject::tr("port definition");
                case ElementKind::PortUsage:
                    return QObject::tr("port");
                case ElementKind::InterfaceDefinition:
                    return QObject::tr("interface definition");
                case ElementKind::ActionDefinition:
                    return QObject::tr("action definition");
                case ElementKind::ActionUsage:
                    return QObject::tr("action");
                case ElementKind::ControlNode:
                    return QObject::tr("control node");
                case ElementKind::StateDefinition:
                    return QObject::tr("state definition");
                case ElementKind::StateUsage:
                    return QObject::tr("state");
                case ElementKind::RequirementDefinition:
                    return QObject::tr("requirement definition");
                case ElementKind::RequirementUsage:
                    return QObject::tr("requirement");
                case ElementKind::Subject:
                    return QObject::tr("subject");
                case ElementKind::Actor:
                    return QObject::tr("actor");
                case ElementKind::UseCaseDefinition:
                    return QObject::tr("use case definition");
                case ElementKind::UseCaseUsage:
                    return QObject::tr("use case");
                case ElementKind::VerificationDefinition:
                    return QObject::tr("verification definition");
                case ElementKind::VerificationUsage:
                    return QObject::tr("verification");
                case ElementKind::OccurrenceDefinition:
                    return QObject::tr("occurrence definition");
                case ElementKind::OccurrenceUsage:
                    return QObject::tr("occurrence");
                case ElementKind::EventOccurrence:
                    return QObject::tr("event");
                case ElementKind::AllocationDefinition:
                    return QObject::tr("allocation definition");
                case ElementKind::AnalysisDefinition:
                    return QObject::tr("analysis definition");
                case ElementKind::AnalysisUsage:
                    return QObject::tr("analysis");
                case ElementKind::ConnectionDefinition:
                    return QObject::tr("connection definition");
                case ElementKind::FlowDefinition:
                    return QObject::tr("flow definition");
                case ElementKind::MetadataDefinition:
                    return QObject::tr("metadata definition");
                case ElementKind::MetadataUsage:
                    return QObject::tr("metadata");
                case ElementKind::ViewDefinition:
                    return QObject::tr("view definition");
                case ElementKind::ViewUsage:
                    return QObject::tr("view");
                case ElementKind::Import:
                    return QObject::tr("import");
                case ElementKind::Alias:
                    return QObject::tr("alias");
                case ElementKind::Expose:
                    return QObject::tr("expose");
                case ElementKind::Render:
                    return QObject::tr("rendering");
                case ElementKind::Filter:
                    return QObject::tr("filter");
                case ElementKind::Textual:
                    return QObject::tr("statement");
            }
            return {};
        }

        QString kindText(const Diagram::SysMLElement &element) {
            if (element.kind() == ElementKind::Textual && !element.keyword().empty())
                return QString::fromStdString(element.keyword());
            if ((isConstraintOrCalc(element.kind()) || element.kind() == ElementKind::RequirementUsage ||
                 element.kind() == ElementKind::Actor || element.kind() == ElementKind::UseCaseUsage) &&
                element.declaredKeyword().has_value() && !element.declaredKeyword()->empty())
                return QString::fromStdString(*element.declaredKeyword()); // function, assert constraint, objective, ...
            if (Diagram::isTemporalKeyword(element.declaredKeyword()))
                return QString::fromStdString(*element.declaredKeyword()); // snapshot, timeslice part, ...
            return kindText(element.kind());
        }

        /// Prefixes shown as keyword of a node («variation», «variant», «individual»), not in its title.
        bool isStereotypeWord(const std::string &prefix) {
            return prefix == "variation" || prefix == "variant" || prefix == "individual";
        }

        /// «variation» «individual» ... in front of the keyword of a node.
        QString stereotypes(const Diagram::SysMLElement &element) {
            QString text;
            for (const auto &prefix: element.prefixes()) {
                if (isStereotypeWord(prefix))
                    text += QStringLiteral("«") + QString::fromStdString(prefix) + QStringLiteral("» ");
            }
            return text;
        }

        /// «measurable» for metadata, "/" for derived (UML notation), other prefixes as words (the stereotype words
        /// not, if they are shown as keyword of the node).
        QString decorate(const Diagram::SysMLElement &element, const QString &name, bool stereotypesShown = false) {
            QString metadata;
            QString words;
            bool derived = false;
            for (const auto &prefix: element.prefixes()) {
                const QString text = QString::fromStdString(prefix);
                if (stereotypesShown && isStereotypeWord(prefix))
                    continue;
                if (text.startsWith(QLatin1Char('#')))
                    metadata += QStringLiteral("«") + text.mid(1) + QStringLiteral("» ");
                else if (text == QLatin1String("derived"))
                    derived = true;
                else
                    words += text + QLatin1Char(' ');
            }
            const QString shortName = element.shortName().empty()
                                      ? QString() : QStringLiteral("<") + QString::fromStdString(element.shortName()) + QStringLiteral("> ");
            return metadata + words + (derived ? QStringLiteral("/") : QString()) + shortName + name;
        }

        /// Lines of the doc text of an element for a compartment (long lines wrapped at word boundaries).
        QStringList docLines(const Diagram::SemanticModel &model, ElementId id, int width = 44) {
            const auto doc = SysMLTextImporter::docOf(model, id);
            if (!doc)
                return {};
            QStringList lines;
            for (const QString &paragraph: QString::fromStdString(SysMLTextImporter::commentBody(model.element(*doc).text()))
                                                   .split(QLatin1Char('\n'))) {
                QString line;
                for (const QString &word: paragraph.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                    if (!line.isEmpty() && line.size() + 1 + word.size() > width) {
                        lines.append(line);
                        line.clear();
                    }
                    line += (line.isEmpty() ? QString() : QStringLiteral(" ")) + word;
                }
                lines.append(line);
            }
            return lines;
        }

        QString specializationText(const Diagram::SysMLElement &element) {
            QString text;
            for (const auto &reference: element.externalSpecializations())
                text += (text.isEmpty() ? QStringLiteral(" :> ") : QStringLiteral(", ")) + QString::fromStdString(reference);
            return text;
        }

        /// Statements kept as text that are not shown in the diagram (they stay in the code).
        bool isHiddenStatement(const Diagram::SysMLElement &element) {
            if (element.kind() != ElementKind::Textual)
                return false;
            const auto &keyword = element.keyword();
            return keyword == "comment" || keyword == "doc" || keyword == "import" || keyword == "alias";
        }

        QVariantMap entry(ElementId id, const QString &text, const QString &tooltip = QString()) {
            return {{QStringLiteral("entryId"), QString::number(id)},
                    {QStringLiteral("text"), elide(text)},
                    {QStringLiteral("tooltip"), tooltip.isEmpty() && text.size() > MaximumEntryLength ? text : tooltip}};
        }

        void addCompartment(QVariantList &compartments, const QString &title, const QVariantList &entries) {
            if (!entries.isEmpty())
                compartments.append(QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("entries"), entries}});
        }

        /// Packages, definitions and parts; statements kept as text only on package level (constraint def, ...).
        /// The part a port is drawn on: its owner, or the part of the port that owns it ("port p { port q; }").
        std::optional<ElementId> portAnchor(const Diagram::SemanticModel &model, ElementId id) {
            if (model.element(id).kind() != ElementKind::PortUsage)
                return std::nullopt;
            for (ElementId owner = model.element(id).owner(); owner != RootId; owner = model.element(owner).owner()) {
                const ElementKind kind = model.element(owner).kind();
                if (kind == ElementKind::PartUsage || kind == ElementKind::PartDefinition)
                    return owner;
                if (kind != ElementKind::PortUsage)
                    return std::nullopt;
            }
            return std::nullopt;
        }

        bool isNodeElement(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            // the ends of an allocation / connection / flow definition are entries of its compartment "ends"
            if (element.kind() == ElementKind::PartUsage && element.owner() != RootId &&
                (model.element(element.owner()).kind() == ElementKind::AllocationDefinition ||
                 Diagram::isConnectionDefinitionKind(model.element(element.owner()).kind())))
                return false;
            if (isNodeKind(element.kind()) || portAnchor(model, id))
                return true;
            if (element.kind() == ElementKind::OccurrenceUsage) {
                const ElementKind owner = element.owner() == RootId ? ElementKind::Package : model.element(element.owner()).kind();
                return owner == ElementKind::Package || composes(owner);
            }
            if ((element.kind() == ElementKind::ActionUsage || element.kind() == ElementKind::StateUsage) &&
                (element.owner() == RootId || model.element(element.owner()).kind() == ElementKind::Package))
                return true;
            // views, metadata, imports, aliases and filters on package level: nodes; elsewhere entries of their owner
            if ((element.kind() == ElementKind::ViewUsage || element.kind() == ElementKind::MetadataUsage ||
                 element.kind() == ElementKind::Import || element.kind() == ElementKind::Alias ||
                 element.kind() == ElementKind::Filter) &&
                (element.owner() == RootId || model.element(element.owner()).kind() == ElementKind::Package))
                return true;
            // requirements (not an objective, nor inside one), use cases and verifications on package level / in parts
            if (element.kind() == ElementKind::RequirementUsage) {
                for (ElementId owner = id; owner != RootId; owner = model.element(owner).owner()) {
                    if (isObjective(model.element(owner)) || Diagram::isCaseKind(model.element(owner).kind()))
                        return false;
                }
                return true;
            }
            if (element.kind() == ElementKind::UseCaseUsage || element.kind() == ElementKind::VerificationUsage ||
                element.kind() == ElementKind::AnalysisUsage) {
                const ElementKind owner = element.owner() == RootId ? ElementKind::Package : model.element(element.owner()).kind();
                return owner == ElementKind::Package || isPartLike(owner);
            }
            if (!(isConstraintOrCalc(element.kind()) || element.kind() == ElementKind::Textual) || isHiddenStatement(element))
                return false;
            return element.owner() == RootId || model.element(element.owner()).kind() == ElementKind::Package;
        }

        /// ":>> eng", "redefines a, b" (empty if it redefines nothing).
        QString redefinitionText(const Diagram::SysMLElement &element) {
            QString text;
            for (const auto &reference: element.redefinitions())
                text += (text.isEmpty() ? QString::fromStdString(element.redefinitionKeyword()) + QLatin1Char(' ')
                                        : QStringLiteral(", ")) + QString::fromStdString(reference);
            return text;
        }

        /// "engine : Engine[4]", "smallEng : SmallEngine :>> eng", ":>> cyl[4]" (named like the redefined feature)
        QString usageText(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            const QString redefinitions = redefinitionText(element);
            QString text = element.nameFromRedefinition() ? redefinitions : QString::fromStdString(element.name());
            if (!element.typeName().empty())
                text += QStringLiteral(" : ") + QString::fromStdString(element.typeName());
            if (element.multiplicity().has_value())
                text += QString::fromStdString(element.multiplicity()->toString());
            if (!element.nameFromRedefinition() && !redefinitions.isEmpty())
                text += QLatin1Char(' ') + redefinitions;
            return text;
        }

        /// "private ISQ::*", "Car for Automobile", "vehicle::**", "@Safety" (import / alias / expose / render / filter).
        QString targetText(const Diagram::SysMLElement &element) {
            QString text = QString::fromStdString(element.target());
            if (element.kind() == ElementKind::Alias)
                text = QString::fromStdString(element.name()) + QStringLiteral(" for ") + text;
            if (element.declaredKeyword() == "import all")
                text = QStringLiteral("all ") + text;
            for (const auto &prefix: element.prefixes())
                text = QString::fromStdString(prefix) + QLatin1Char(' ') + text;
            return text;
        }

        /// "@Safety {isMandatory = true}", "SafetyFeature about seatBelt, alarm", "m : ToolExecution".
        QString metadataText(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            QString text = QString::fromStdString(element.typeName());
            if (!element.name().empty())
                text = QString::fromStdString(element.name()) + QStringLiteral(" : ") + text;
            else
                text = QLatin1Char('@') + text;
            QStringList values;
            for (const ElementId child: element.ownedElements()) {
                const auto &value = model.element(child);
                if (value.kind() == ElementKind::Attribute)
                    values.append(QString::fromStdString(value.nameFromRedefinition() ? value.redefinitions().front()
                                                                                       : value.name()) +
                                  (value.value().empty() ? QString()
                                                         : QStringLiteral(" = ") + QString::fromStdString(value.value())));
            }
            if (!values.isEmpty())
                text += QStringLiteral(" {") + values.join(QStringLiteral("; ")) + QLatin1Char('}');
            QStringList about;
            for (const auto &reference: element.about())
                about.append(QString::fromStdString(reference));
            if (!about.isEmpty())
                text += QStringLiteral(" about ") + about.join(QStringLiteral(", "));
            return text;
        }

        /// An action with a form: "send new Show(p) to screen", "trigger: accept scene : Scene", "loop charging
        /// until c", "else"; empty for a plain action.
        QString actionFormText(const Diagram::SysMLElement &element) {
            if (element.kind() != ElementKind::ActionUsage)
                return {};
            QString text;
            if (element.isElseBranch())
                text = QStringLiteral("else");
            if (element.actionForm() != Diagram::ActionForm::Plain) {
                text += (text.isEmpty() ? QString() : QStringLiteral(" ")) +
                        QString::fromStdString(Diagram::toKeyword(element.actionForm()));
                if (!element.actionClause().empty())
                    text += QLatin1Char(' ') + QString::fromStdString(element.actionClause());
            }
            if (text.isEmpty())
                return {};
            if (!element.name().empty())
                text = QString::fromStdString(element.name()) + QStringLiteral(": ") + text;
            if (!element.untilCondition().empty())
                text += QStringLiteral(" until ") + QString::fromStdString(element.untilCondition());
            return text;
        }

        QString valueText(const Diagram::SysMLElement &element) {
            if (element.value().empty())
                return {};
            return QLatin1Char(' ') + QString::fromStdString(element.valueOperator()) + QLatin1Char(' ') +
                   QString::fromStdString(element.value());
        }

        /// "pipe : Hose", "of Fuel", "«interface» : FuelInterface" (empty for plain relationships).
        QString edgeLabel(const Diagram::Relationship &relationship) {
            QString label = QString::fromStdString(relationship.Name);
            if (!relationship.Definition.empty())
                label += QStringLiteral(" : ") + QString::fromStdString(relationship.Definition);
            if (!relationship.TypeName.empty())
                label += (relationship.Kind == RelationshipKind::Flow ? QStringLiteral(" of ") : QStringLiteral(" : ")) +
                         QString::fromStdString(relationship.TypeName);
            label = label.trimmed();
            if (relationship.Kind == RelationshipKind::Interface)
                label = (QStringLiteral("«interface» ") + label).trimmed();
            if (relationship.Kind == RelationshipKind::Satisfy)
                label = QStringLiteral("«satisfy»");
            if (relationship.Kind == RelationshipKind::Verify)
                label = QStringLiteral("«verify»");
            if (relationship.Kind == RelationshipKind::Include)
                label = QStringLiteral("«include»");
            if (relationship.Kind == RelationshipKind::Frame)
                label = QStringLiteral("«frame»");
            if (relationship.Kind == RelationshipKind::Allocation)
                label = (QStringLiteral("«allocate» ") + label).trimmed();
            if (relationship.Kind == RelationshipKind::Message) {
                label = QString::fromStdString(relationship.Name);
                if (!relationship.TypeName.empty())
                    label += (label.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("of ") +
                             QString::fromStdString(relationship.TypeName);
                if (relationship.Sequenced)
                    label = QStringLiteral("then ") + label;
            }
            if (relationship.Kind == RelationshipKind::Succession || relationship.Kind == RelationshipKind::Transition) {
                // UML/SysML notation: trigger [guard] / effect
                label = QString::fromStdString(relationship.Trigger);
                if (!relationship.Guard.empty())
                    label += QStringLiteral(" [") + QString::fromStdString(relationship.Guard) + QStringLiteral("]");
                if (!relationship.Effect.empty())
                    label += QStringLiteral(" / ") + QString::fromStdString(relationship.Effect);
                label = label.trimmed();
            }
            return label;
        }

        QString edgeKey(bool composition, std::uint64_t id) {
            return (composition ? QStringLiteral("c:") : QStringLiteral("r:")) + QString::number(id);
        }

        bool isFeatureTool(int tool) {
            return tool == DiagramTypes::ToolAttribute || tool == DiagramTypes::ToolAction ||
                   tool == DiagramTypes::ToolConstraint || tool == DiagramTypes::ToolCalc ||
                   tool == DiagramTypes::ToolPort || tool == DiagramTypes::ToolItem || tool == DiagramTypes::ToolState ||
                   tool == DiagramTypes::ToolActionUsage || tool == DiagramTypes::ToolSubject ||
                   tool == DiagramTypes::ToolActor || tool == DiagramTypes::ToolObjective || tool == DiagramTypes::ToolEvent ||
                   tool == DiagramTypes::ToolVariant || tool == DiagramTypes::ToolSnapshot || tool == DiagramTypes::ToolTimeslice ||
                   tool == DiagramTypes::ToolMetadata || tool == DiagramTypes::ToolExpose || tool == DiagramTypes::ToolRender ||
                   tool == DiagramTypes::ToolFilter;
        }

        /// Keyword shown above the name: «constraint def», «function», «assert constraint», ...
        QString keywordOf(const Diagram::SysMLElement &element) {
            std::string keyword = Diagram::defaultKeyword(element.kind());
            if (element.declaredKeyword().has_value() && !element.declaredKeyword()->empty())
                keyword = *element.declaredKeyword();
            return QStringLiteral("«") + QString::fromStdString(keyword) + QStringLiteral("»") +
                   (element.isParallel() ? QStringLiteral(" «parallel»") : QString());
        }
    }

    DiagramController::DiagramController(Diagram::SemanticModel &model, QObject *parent) :
            QObject(parent),
            Model(model),
            Nodes(new DiagramNodeModel(this)),
            Edges(new DiagramEdgeModel(this)) {
        ListenerToken = Model.addListener([this](const Diagram::ModelChange &) { scheduleSync(); });
        connect(Nodes, &DiagramNodeModel::geometryChanged, this, &DiagramController::updateCanvasSize);
        syncNow();
    }

    DiagramController::~DiagramController() {
        Model.removeListener(ListenerToken);
    }

    // --- properties --------------------------------------------------------------------------------

    void DiagramController::setArrowMode(bool enabled) {
        if (ArrowMode == enabled)
            return;
        ArrowMode = enabled;
        if (enabled)
            emit statusMessage(tr("Drag from one element to another to connect them. Esc leaves the arrow mode."));
        emit arrowModeChanged();
    }

    void DiagramController::setArrowDefault(int value) {
        const auto preset = static_cast<DiagramTypes::ArrowDefault>(
                std::clamp(value, int(DiagramTypes::AskOnRelease), int(DiagramTypes::AlwaysDependency)));
        if (preset == ArrowDefaultValue)
            return;
        ArrowDefaultValue = preset;
        emit arrowDefaultChanged();
    }

    // --- adding ------------------------------------------------------------------------------------

    QString DiagramController::addElement(int tool, qreal x, qreal y) {
        AutoLayout = false;
        const QPointF point(x, y);
        if (isFeatureTool(tool)) {
            auto partId = toId(nodeIdAt(point, std::nullopt, false));
            // a filter onto a package frame: "filter @Safety;" in the package
            if (!partId && tool == DiagramTypes::ToolFilter)
                partId = frameAt(point, std::nullopt);
            // metadata onto the canvas: "metadata Name about ...;" on package level
            if (!partId && tool == DiagramTypes::ToolMetadata)
                return addTopLevelMetadata(point);
            if (!partId || !acceptsFeature(*partId, tool)) {
                emit statusMessage(tool == DiagramTypes::ToolAction ? tr("Drop actions onto a part or part definition.")
                                   : tool == DiagramTypes::ToolPort ? tr("Drop ports onto a part, a port or a port / interface definition.")
                                   : tool == DiagramTypes::ToolItem ? tr("Drop items onto a part, an item or a port definition.")
                                   : tool == DiagramTypes::ToolSubject ? tr("Drop the subject onto a requirement, use case or verification.")
                                   : tool == DiagramTypes::ToolActor ? tr("Drop actors onto a use case (stakeholders onto a requirement).")
                                   : tool == DiagramTypes::ToolObjective ? tr("Drop the objective onto a use case or verification.")
                                   : tool == DiagramTypes::ToolEvent ? tr("Drop events onto a part, item, port, action or occurrence.")
                                   : tool == DiagramTypes::ToolVariant ? tr("Drop the variant onto a variation (variation part def, ...).")
                                   : tool == DiagramTypes::ToolSnapshot || tool == DiagramTypes::ToolTimeslice
                                   ? tr("Drop it onto a part, an individual, an item or an occurrence.")
                                   : tool == DiagramTypes::ToolExpose ? tr("Drop it onto a view.")
                                   : tool == DiagramTypes::ToolRender ? tr("Drop it onto a view or view definition.")
                                   : tool == DiagramTypes::ToolFilter ? tr("Drop it onto a package or a view.")
                                   : tr("Drop it onto a part or a definition that can own it."));
                return {};
            }
            const auto id = addFeature(tool, *partId);
            return id ? QString::number(*id) : QString();
        }
        if (tool == DiagramTypes::ToolStatement)
            return addStatement(point);
        static const std::map<int, ElementKind> NodeTools = {
                {DiagramTypes::ToolPart, ElementKind::PartUsage},
                {DiagramTypes::ToolPartDefinition, ElementKind::PartDefinition},
                {DiagramTypes::ToolPackage, ElementKind::Package},
                {DiagramTypes::ToolConstraintDefinition, ElementKind::ConstraintDefinition},
                {DiagramTypes::ToolCalcDefinition, ElementKind::CalcDefinition},
                {DiagramTypes::ToolAttributeDefinition, ElementKind::AttributeDefinition},
                {DiagramTypes::ToolEnumDefinition, ElementKind::EnumerationDefinition},
                {DiagramTypes::ToolItemDefinition, ElementKind::ItemDefinition},
                {DiagramTypes::ToolPortDefinition, ElementKind::PortDefinition},
                {DiagramTypes::ToolInterfaceDefinition, ElementKind::InterfaceDefinition},
                {DiagramTypes::ToolActionDefinition, ElementKind::ActionDefinition},
                {DiagramTypes::ToolStateDefinition, ElementKind::StateDefinition},
                {DiagramTypes::ToolRequirementDefinition, ElementKind::RequirementDefinition},
                {DiagramTypes::ToolRequirement, ElementKind::RequirementUsage},
                {DiagramTypes::ToolUseCaseDefinition, ElementKind::UseCaseDefinition},
                {DiagramTypes::ToolUseCase, ElementKind::UseCaseUsage},
                {DiagramTypes::ToolVerificationDefinition, ElementKind::VerificationDefinition},
                {DiagramTypes::ToolVerification, ElementKind::VerificationUsage},
                {DiagramTypes::ToolOccurrenceDefinition, ElementKind::OccurrenceDefinition},
                {DiagramTypes::ToolOccurrence, ElementKind::OccurrenceUsage},
                {DiagramTypes::ToolAllocationDefinition, ElementKind::AllocationDefinition},
                {DiagramTypes::ToolConnectionDefinition, ElementKind::ConnectionDefinition},
                {DiagramTypes::ToolFlowDefinition, ElementKind::FlowDefinition},
                {DiagramTypes::ToolMetadataDefinition, ElementKind::MetadataDefinition},
                {DiagramTypes::ToolViewDefinition, ElementKind::ViewDefinition},
                {DiagramTypes::ToolView, ElementKind::ViewUsage},
                {DiagramTypes::ToolViewpoint, ElementKind::RequirementUsage},
                {DiagramTypes::ToolConcern, ElementKind::RequirementUsage},
                {DiagramTypes::ToolImport, ElementKind::Import},
                {DiagramTypes::ToolAlias, ElementKind::Alias},
                {DiagramTypes::ToolAnalysisDefinition, ElementKind::AnalysisDefinition},
                {DiagramTypes::ToolAnalysis, ElementKind::AnalysisUsage},
                {DiagramTypes::ToolVariationDefinition, ElementKind::PartDefinition},
                {DiagramTypes::ToolIndividualDefinition, ElementKind::PartDefinition},
                {DiagramTypes::ToolIndividual, ElementKind::PartUsage}};
        const auto toolKind = NodeTools.find(tool);
        if (toolKind == NodeTools.end())
            return {};
        const ElementKind kind = toolKind->second;

        // Dropped into a frame: the package owns the new element.
        ElementId owner = RootId;
        if (const auto frame = frameAt(point, std::nullopt); frame && RelationshipRules::canOwn(Model, *frame, kind))
            owner = *frame;

        const std::string base = tool == DiagramTypes::ToolViewpoint ? "viewpoint"
                                 : tool == DiagramTypes::ToolConcern ? "concern"
                                 : kind == ElementKind::MetadataDefinition ? "Metadata"
                                 : kind == ElementKind::ViewDefinition ? "View"
                                 : kind == ElementKind::ViewUsage ? "view"
                                 : kind == ElementKind::Alias ? "Alias"
                                 : tool == DiagramTypes::ToolVariationDefinition ? "Choices"
                                 : tool == DiagramTypes::ToolIndividualDefinition ? "Individual"
                                 : tool == DiagramTypes::ToolIndividual ? "individual"
                                 : kind == ElementKind::AnalysisDefinition ? "Analysis"
                                 : kind == ElementKind::AnalysisUsage ? "analysis"
                                 : kind == ElementKind::PartUsage ? "part"
                                 : kind == ElementKind::PartDefinition ? "Part"
                                 : kind == ElementKind::ConstraintDefinition ? "Constraint"
                                 : kind == ElementKind::CalcDefinition ? "Calc"
                                 : kind == ElementKind::AttributeDefinition ? "Value"
                                 : kind == ElementKind::EnumerationDefinition ? "State"
                                 : kind == ElementKind::ItemDefinition ? "Item"
                                 : kind == ElementKind::PortDefinition ? "Port"
                                 : kind == ElementKind::InterfaceDefinition ? "Interface"
                                 : kind == ElementKind::ActionDefinition ? "Action"
                                 : kind == ElementKind::StateDefinition ? "States"
                                 : kind == ElementKind::RequirementDefinition ? "Requirement"
                                 : kind == ElementKind::RequirementUsage ? "requirement"
                                 : kind == ElementKind::UseCaseDefinition ? "UseCase"
                                 : kind == ElementKind::UseCaseUsage ? "useCase"
                                 : kind == ElementKind::VerificationDefinition ? "Verification"
                                 : kind == ElementKind::VerificationUsage ? "verification"
                                 : kind == ElementKind::OccurrenceDefinition ? "Interaction"
                                 : kind == ElementKind::OccurrenceUsage ? "occurrence"
                                 : kind == ElementKind::AllocationDefinition ? "Allocation"
                                 : kind == ElementKind::ConnectionDefinition ? "Connection"
                                 : kind == ElementKind::FlowDefinition ? "Flow"
                                 : "Package";
        const std::string name = Diagram::isAnonymousKind(kind) ? std::string() : uniqueName(base, owner);
        std::optional<ElementId> id;
        const QPointF offset = kind == ElementKind::Package ? QPointF(20, 15) : QPointF(70, 25);
        if (!runModelChange([&] {
                id = Model.addElement(kind, name, owner);
                PendingPositions[*id] = QPointF(std::max<qreal>(0, x - offset.x()), std::max<qreal>(0, y - offset.y()));
                if (isConstraintOrCalc(kind))
                    fillNewEvaluation(*id);
                if (kind == ElementKind::RequirementDefinition) {
                    // a starting point: the text of the requirement
                    const ElementId doc = Model.addElement(ElementKind::Textual, "", *id);
                    Model.setText(doc, SysMLTextImporter::docStatement("Describe the requirement."), "doc", "");
                }
                if (tool == DiagramTypes::ToolViewpoint || tool == DiagramTypes::ToolConcern)
                    Model.setDeclaredKeyword(*id, std::string(tool == DiagramTypes::ToolViewpoint ? "viewpoint" : "concern"));
                if (kind == ElementKind::Import) {
                    Model.setTarget(*id, "ScalarValues::*");
                    Model.setPrefixes(*id, {"private"});
                }
                if (kind == ElementKind::Alias)
                    Model.setTarget(*id, "ScalarValues::Real");
                if (tool == DiagramTypes::ToolVariationDefinition)
                    Model.setPrefixes(*id, {"variation"});
                if (tool == DiagramTypes::ToolIndividualDefinition || tool == DiagramTypes::ToolIndividual)
                    Model.setPrefixes(*id, {"individual"});
                if (kind == ElementKind::AllocationDefinition) {
                    // its two ends: "end logical; end physical;"
                    for (const char *end: {"logical", "physical"}) {
                        const ElementId part = Model.addElement(ElementKind::PartUsage, end, *id);
                        Model.setPrefixes(part, {"end"});
                        Model.setDeclaredKeyword(part, std::string());
                    }
                }
                if (Diagram::isConnectionDefinitionKind(kind)) {
                    // its two ends: "end part source; end part target;" / "end port source; end port target;"
                    for (const char *end: {"source", "target"}) {
                        const ElementId part = Model.addElement(kind == ElementKind::FlowDefinition ? ElementKind::PortUsage
                                                                                                     : ElementKind::PartUsage,
                                                                end, *id);
                        Model.setPrefixes(part, {"end"});
                    }
                }
            }))
            return {};

        SelectedNodes = {*id};
        SelectedEdges.clear();
        applySelection();
        emit statusMessage(name.empty() ? tr("Added %1 - double-click to edit.").arg(kindText(kind))
                                        : tr("Added %1 '%2' - double-click to edit.").arg(kindText(kind), QString::fromStdString(name)));
        return QString::number(*id);
    }

    QString DiagramController::addTopLevelMetadata(const QPointF &point) {
        ElementId owner = RootId;
        if (const auto frame = frameAt(point, std::nullopt); frame && RelationshipRules::canOwn(Model, *frame, ElementKind::MetadataUsage))
            owner = *frame;
        std::optional<ElementId> id;
        if (!runModelChange([&] {
                id = Model.addElement(ElementKind::MetadataUsage, std::string(), owner);
                Model.setTypeName(*id, metadataTypeName());
                PendingPositions[*id] = QPointF(std::max<qreal>(0, point.x() - 70), std::max<qreal>(0, point.y() - 25));
            }))
            return {};
        SelectedNodes = {*id};
        SelectedEdges.clear();
        applySelection();
        emit statusMessage(tr("Added metadata - double-click it to say what it is about."));
        return QString::number(*id);
    }

    std::string DiagramController::metadataTypeName() const {
        // the first metadata definition of the model, else a placeholder to be edited
        std::function<std::optional<std::string>(ElementId)> visit = [&](ElementId owner) -> std::optional<std::string> {
            for (const ElementId child: Model.children(owner)) {
                if (Model.element(child).kind() == ElementKind::MetadataDefinition)
                    return Diagram::SysMLCodeGenerator::formatName(Model.element(child).name());
                if (const auto found = visit(child))
                    return found;
            }
            return std::nullopt;
        };
        return visit(RootId).value_or("Metadata");
    }

    QString DiagramController::addStatement(const QPointF &point) {
        // Into the part / constraint / calculation under the mouse, else into the package frame, else top level.
        ElementId owner = RootId;
        if (const auto node = toId(nodeIdAt(point, std::nullopt, false));
            node && RelationshipRules::canOwn(Model, *node, ElementKind::Textual) && !isFrame(*node))
            owner = *node;
        else if (const auto frame = frameAt(point, std::nullopt); frame && RelationshipRules::canOwn(Model, *frame, ElementKind::Textual))
            owner = *frame;
        const std::string name = uniqueName("Analysis", owner);
        const std::string text = "analysis def " + name + " {\n    // any SysML v2 text\n}";
        std::optional<ElementId> id;
        if (!runModelChange([&] {
                id = Model.addElement(ElementKind::Textual, name, owner);
                Model.setText(*id, text, "analysis def", name);
                if (owner == RootId || Model.element(owner).kind() == ElementKind::Package)
                    PendingPositions[*id] = QPointF(std::max<qreal>(0, point.x() - 70), std::max<qreal>(0, point.y() - 25));
            }))
            return {};
        emit statusMessage(tr("Added a SysML statement - write any SysML v2 text into it."));
        return QString::number(*id);
    }

    std::optional<ElementId> DiagramController::addFeatureTo(int tool, ElementId owner) {
        if (!Model.contains(owner) || !acceptsFeature(owner, tool))
            return std::nullopt;
        return addFeature(tool, owner);
    }

    bool DiagramController::acceptsFeature(ElementId owner, int tool) const {
        if (!Model.contains(owner) || !isFeatureTool(tool))
            return false;
        const ElementKind ownerKind = Model.element(owner).kind();
        // the objective belongs to a use case / verification; subject and actors to cases and requirements
        if (tool == DiagramTypes::ToolObjective && !Diagram::isCaseKind(ownerKind))
            return false;
        if (tool == DiagramTypes::ToolVariant) {
            const auto &prefixes = Model.element(owner).prefixes();
            if (std::find(prefixes.begin(), prefixes.end(), "variation") == prefixes.end())
                return false;
        }
        return bool(RelationshipRules::canOwn(Model, owner, featureKind(ownerKind, tool)));
    }

    QString DiagramController::addElementAtSelection(int tool, qreal x, qreal y) {
        if (tool == DiagramTypes::ToolStatement && SelectedNodes.size() == 1) {
            const ElementId selected = *SelectedNodes.begin();
            if (Model.contains(selected) && !isFrame(selected)) {
                const QRectF rect = Nodes->find(selected) ? Nodes->find(selected)->Rect : QRectF();
                return addStatement(rect.center());
            }
        }
        if (!isFeatureTool(tool))
            return addElement(tool, x, y);

        std::optional<ElementId> selectedPart;
        for (const ElementId id: SelectedNodes) {
            if (!Model.contains(id) || !acceptsFeature(id, tool))
                continue;
            if (selectedPart) {
                selectedPart.reset();
                break;
            }
            selectedPart = id;
        }
        if (!selectedPart) {
            emit statusMessage(tr("Select exactly one part first, or drag the entry onto a part."));
            return {};
        }
        const auto id = addFeature(tool, *selectedPart);
        return id ? QString::number(*id) : QString();
    }

    std::optional<ElementId> DiagramController::addFeature(int tool, ElementId owner) {
        const ElementKind ownerKind = Model.element(owner).kind();
        const bool evaluationOwner = isConstraintOrCalc(ownerKind);
        const ElementKind kind = featureKind(ownerKind, tool);
        std::string base = kind == ElementKind::ActionPerformance ? "action"
                           : kind == ElementKind::ConstraintUsage ? "constraint"
                           : kind == ElementKind::CalcUsage ? "calc"
                           : kind == ElementKind::PortUsage ? (ownerKind == ElementKind::InterfaceDefinition ? "end" : "port")
                           : kind == ElementKind::ItemUsage ? "item"
                           : kind == ElementKind::EnumerationLiteral ? "value"
                           : kind == ElementKind::StateUsage ? "state"
                           : kind == ElementKind::ActionUsage ? "action"
                           : kind == ElementKind::Subject ? "subject"
                           : kind == ElementKind::EventOccurrence ? "event"
                           : tool == DiagramTypes::ToolSnapshot ? "snapshot"
                           : tool == DiagramTypes::ToolTimeslice ? "timeslice"
                           : tool == DiagramTypes::ToolVariant ? "variant"
                           : kind == ElementKind::OccurrenceUsage ? "occurrence"
                           : kind == ElementKind::Actor ? (isRequirementOwner(ownerKind) ? "stakeholder" : "actor")
                           : evaluationOwner ? "x" // a parameter
                           : "attribute";
        // unnamed: "require constraint { ... }" in a requirement, "objective { ... }" in a case
        const bool requirement = kind == ElementKind::ConstraintUsage && isRequirementOwner(ownerKind);
        const bool objective = tool == DiagramTypes::ToolObjective;
        const std::string name = requirement || objective || kind == ElementKind::MetadataUsage || Diagram::isAnonymousKind(kind)
                                 ? std::string() : uniqueName(base, owner);
        std::optional<ElementId> id;
        if (!runModelChange([&] {
                if (objective) {
                    for (const ElementId child: Model.children(owner)) {
                        if (isObjective(Model.element(child)))
                            throw Diagram::ModelError("It already has an objective - double-click it to edit.");
                    }
                }
                if (kind == ElementKind::Subject) {
                    for (const ElementId child: Model.children(owner)) {
                        if (Model.element(child).kind() == ElementKind::Subject)
                            throw Diagram::ModelError("It already has a subject - double-click it to edit.");
                    }
                }
                id = Model.addElement(kind, name, owner);
                if (requirement) {
                    Model.setDeclaredKeyword(*id, std::string("require constraint"));
                    Model.setExpression(*id, "true");
                    return;
                }
                if (objective) {
                    Model.setDeclaredKeyword(*id, std::string("objective"));
                    const ElementId doc = Model.addElement(ElementKind::Textual, "", *id);
                    Model.setText(doc, SysMLTextImporter::docStatement("What the case shall achieve."), "doc", "");
                    return;
                }
                if (kind == ElementKind::Actor && isRequirementOwner(ownerKind)) {
                    Model.setDeclaredKeyword(*id, std::string("stakeholder"));
                    return;
                }
                if (kind == ElementKind::MetadataUsage) { // "@Safety;"
                    Model.setTypeName(*id, metadataTypeName());
                    Model.setDeclaredKeyword(*id, std::string("@"));
                    return;
                }
                if (Diagram::isTargetKind(kind)) {
                    Model.setTarget(*id, kind == ElementKind::Expose ? "Model::**"
                                         : kind == ElementKind::Render ? "asTreeDiagram" : "@SysML::PartUsage");
                    return;
                }
                if (tool == DiagramTypes::ToolSnapshot || tool == DiagramTypes::ToolTimeslice) {
                    Model.setDeclaredKeyword(*id, std::string(tool == DiagramTypes::ToolSnapshot ? "snapshot" : "timeslice"));
                    return;
                }
                if (tool == DiagramTypes::ToolVariant) {
                    Model.setPrefixes(*id, {"variant"});
                    return;
                }
                if (kind == ElementKind::Attribute && evaluationOwner) {
                    Model.setPrefixes(*id, {"in"});
                    Model.setDeclaredKeyword(*id, std::string()); // in x : Real;
                    Model.setTypeName(*id, "Real");
                } else if (isConstraintOrCalc(kind)) {
                    fillNewEvaluation(*id);
                } else if (kind == ElementKind::PortUsage && ownerKind == ElementKind::InterfaceDefinition) {
                    Model.setPrefixes(*id, {"end"});
                } else if (kind == ElementKind::StateUsage && isPartLike(ownerKind)) {
                    Model.setDeclaredKeyword(*id, std::string("exhibit state"));
                } else if (kind == ElementKind::ActionUsage &&
                           (ownerKind == ElementKind::StateUsage || ownerKind == ElementKind::StateDefinition)) {
                    Model.setDeclaredKeyword(*id, std::string("do action"));
                }
            }))
            return std::nullopt;
        const QString what = kind == ElementKind::Attribute && evaluationOwner ? tr("parameter")
                             : requirement ? tr("required constraint") : objective ? tr("objective")
                             : kindText(Model.element(*id));
        emit statusMessage(name.empty() ? tr("Added %1 - double-click it to edit.").arg(what)
                                        : tr("Added %1 '%2' - double-click it to edit.").arg(what, QString::fromStdString(name)));
        return id;
    }

    void DiagramController::fillNewEvaluation(ElementId id) {
        // Starting point that is valid SysML: one parameter and an expression using it.
        const ElementKind kind = Model.element(id).kind();
        if (kind == ElementKind::ConstraintUsage) {
            Model.setDeclaredKeyword(id, std::string("assert constraint"));
            Model.setExpression(id, "true");
            return;
        }
        const ElementId parameter = Model.addElement(ElementKind::Attribute, "x", id);
        Model.setPrefixes(parameter, {"in"});
        Model.setDeclaredKeyword(parameter, std::string());
        Model.setTypeName(parameter, "Real");
        if (Diagram::isConstraintKind(kind)) {
            Model.setExpression(id, "x >= 0.0");
            return;
        }
        const ElementId result = Model.addElement(ElementKind::Attribute, "result", id);
        Model.setPrefixes(result, {"return"});
        Model.setDeclaredKeyword(result, std::string());
        Model.setTypeName(result, "Real");
        Model.setValue(result, "=", "x");
    }

    // --- lookup ------------------------------------------------------------------------------------

    std::optional<ElementId> DiagramController::toId(const QString &id) const {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || value == RootId || !Model.contains(value))
            return std::nullopt;
        return ElementId(value);
    }

    bool DiagramController::isFrame(ElementId id) const {
        return Model.contains(id) && Model.element(id).kind() == ElementKind::Package;
    }

    QString DiagramController::nodeAt(qreal x, qreal y, bool includeFrameArea) const {
        return nodeIdAt(QPointF(x, y), std::nullopt, includeFrameArea);
    }

    QString DiagramController::nodeIdAt(const QPointF &point, std::optional<ElementId> ignore, bool includeFrameArea) const {
        // Parts are drawn above frames; among them the deepest (drawn last) wins.
        const DiagramNode *best = nullptr;
        for (const auto &node: Nodes->nodes()) {
            const QRectF hit = node.Kind == DiagramTypes::PortNode ? node.Rect.adjusted(-4, -4, 4, 4) : node.Rect;
            if (node.Kind == DiagramTypes::PackageNode || (ignore && node.Id == *ignore) || !hit.contains(point))
                continue;
            if (!best || node.Depth >= best->Depth)
                best = &node;
        }
        if (best)
            return QString::number(best->Id);

        // Frames only at their border and title band, so the content inside stays reachable.
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind != DiagramTypes::PackageNode || (ignore && node.Id == *ignore))
                continue;
            const QRectF outer = node.Rect.adjusted(-FrameBorderGrip, -FrameBorderGrip, FrameBorderGrip, FrameBorderGrip);
            const QRectF inner = node.Rect.adjusted(FrameBorderGrip, FrameTitleHeight, -FrameBorderGrip, -FrameBorderGrip);
            if (outer.contains(point) && !inner.contains(point) && (!best || node.Depth >= best->Depth))
                best = &node;
        }
        if (best)
            return QString::number(best->Id);

        if (includeFrameArea) {
            if (const auto frame = frameAt(point, ignore))
                return QString::number(*frame);
        }
        return {};
    }

    std::optional<ElementId> DiagramController::frameAt(const QPointF &point, std::optional<ElementId> ignore) const {
        std::optional<ElementId> best;
        qreal bestArea = 0;
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind != DiagramTypes::PackageNode || (ignore && node.Id == *ignore) || !node.Rect.contains(point))
                continue;
            const qreal area = node.Rect.width() * node.Rect.height();
            if (!best || area < bestArea) {
                best = node.Id;
                bestArea = area;
            }
        }
        return best;
    }

    QString DiagramController::edgeAt(qreal x, qreal y) const {
        const QPointF point(x, y);
        const auto &edges = Edges->edges();
        for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
            const auto *source = Nodes->find(it->Source);
            const auto *target = Nodes->find(it->Target);
            if (!source || !target)
                continue;
            const QLineF line = DiagramGeometry::edgeLine(source->Rect, target->Rect);
            if (DiagramGeometry::distanceToSegment(point, line) <= EdgeHitDistance)
                return it->Key;
        }
        return {};
    }

    QRectF DiagramController::nodeRect(const QString &id) const {
        const auto elementId = toId(id);
        const auto *node = elementId ? Nodes->find(*elementId) : nullptr;
        return node ? node->Rect : QRectF();
    }

    // --- geometry ----------------------------------------------------------------------------------

    void DiagramController::moveNode(const QString &id, qreal x, qreal y) {
        const auto elementId = toId(id);
        if (!elementId)
            return;
        const int row = Nodes->indexOf(*elementId);
        if (row < 0)
            return;
        const QRectF old = Nodes->nodes()[std::size_t(row)].Rect;
        if (const auto anchor = portAnchor(Model, *elementId)) {
            const auto *anchorNode = Nodes->find(*anchor);
            if (!anchorNode)
                return;
            const QPointF center = onBorder(anchorNode->Rect, QPointF(x, y) + QPointF(PortSize / 2, PortSize / 2));
            AutoLayout = false;
            Nodes->setRect(row, QRectF(center - QPointF(PortSize / 2, PortSize / 2), old.size()));
            return;
        }
        const QPointF position(std::max<qreal>(0, x), std::max<qreal>(0, y));
        const QPointF delta = position - old.topLeft();
        if (delta.isNull())
            return;
        AutoLayout = false;
        Nodes->setRect(row, QRectF(position, old.size()));
        translatePorts(*elementId, delta);
        if (isFrame(*elementId))
            moveFrameContent(*elementId, delta);
    }

    void DiagramController::moveFrameContent(ElementId frame, const QPointF &delta) {
        for (const ElementId child: Model.children(frame)) {
            const int row = Nodes->indexOf(child);
            if (row < 0)
                continue;
            Nodes->setRect(row, Nodes->nodes()[std::size_t(row)].Rect.translated(delta));
            translatePorts(child, delta);
            if (isFrame(child))
                moveFrameContent(child, delta);
        }
    }

    void DiagramController::finishMove(const QString &id) {
        if (const auto elementId = toId(id))
            updateContainment(*elementId);
    }

    void DiagramController::updateContainment(ElementId id) {
        if (!Model.contains(id))
            return;
        const ElementId owner = Model.element(id).owner();
        if (owner != RootId && (isPartLike(Model.element(owner).kind()) || isRequirementOwner(Model.element(owner).kind())))
            return; // owned by a part / requirement (composition): the placement does not change the ownership

        const auto *node = Nodes->find(id);
        if (!node)
            return;
        auto frame = frameAt(node->Rect.center(), id);
        if (frame && Model.isAncestor(id, *frame))
            frame.reset();
        const ElementId newOwner = frame ? *frame : RootId;
        if (newOwner == owner || !RelationshipRules::canReparent(Model, id, newOwner))
            return;
        if (runModelChange([&] { Model.reparent(id, newOwner); })) {
            emit statusMessage(frame ? tr("Moved into package '%1'.").arg(QString::fromStdString(Model.element(newOwner).name()))
                                     : tr("Moved out of the package."));
        }
    }

    void DiagramController::setNodeSize(const QString &id, qreal width, qreal height) {
        const auto elementId = toId(id);
        if (!elementId || isFrame(*elementId) || width <= 0 || height <= 0 || portAnchor(Model, *elementId))
            return;
        const int row = Nodes->indexOf(*elementId);
        if (row < 0)
            return;
        const QRectF old = Nodes->nodes()[std::size_t(row)].Rect;
        Measured.insert(*elementId);
        if (old.size() == QSizeF(width, height))
            return;
        const QRectF resized(old.topLeft(), QSizeF(width, height));
        Nodes->setRect(row, resized);
        // ports stay on the border
        for (const ElementId port: portsOf(*elementId)) {
            const int portRow = Nodes->indexOf(port);
            if (portRow < 0)
                continue;
            const QPointF center = onBorder(resized, Nodes->nodes()[std::size_t(portRow)].Rect.center());
            Nodes->setRect(portRow, QRectF(center - QPointF(PortSize / 2, PortSize / 2), QSizeF(PortSize, PortSize)));
        }
        if (AutoLayout && !LayoutQueued) {
            // Real sizes arrived after an automatic layout: arrange again with them (once per batch).
            LayoutQueued = true;
            QMetaObject::invokeMethod(this, [this] {
                LayoutQueued = false;
                if (AutoLayout)
                    layoutAll();
            }, Qt::QueuedConnection);
        }
    }

    void DiagramController::resizeFrame(const QString &id, qreal width, qreal height) {
        const auto elementId = toId(id);
        if (!elementId || !isFrame(*elementId))
            return;
        const int row = Nodes->indexOf(*elementId);
        if (row < 0)
            return;
        const QRectF old = Nodes->nodes()[std::size_t(row)].Rect;
        AutoLayout = false;
        Nodes->setRect(row, QRectF(old.topLeft(), QSizeF(std::max(width, MinimumFrameWidth),
                                                         std::max(height, MinimumFrameHeight))));
    }

    void DiagramController::updateCanvasSize() {
        qreal right = 0;
        qreal bottom = 0;
        for (const auto &node: Nodes->nodes()) {
            right = std::max(right, node.Rect.right());
            bottom = std::max(bottom, node.Rect.bottom());
        }
        const QSizeF size(std::max<qreal>(2400, right + CanvasMargin), std::max<qreal>(1600, bottom + CanvasMargin));
        if (size == CanvasSize)
            return;
        CanvasSize = size;
        emit canvasSizeChanged();
    }

    // --- selection and deleting --------------------------------------------------------------------

    void DiagramController::select(const QString &id, bool additive) {
        const auto elementId = toId(id);
        if (!elementId)
            return;
        if (!additive) {
            SelectedNodes = {*elementId};
            SelectedEdges.clear();
        } else if (!SelectedNodes.erase(*elementId)) {
            SelectedNodes.insert(*elementId);
        }
        applySelection();
    }

    void DiagramController::selectEdge(const QString &key, bool additive) {
        if (!Edges->find(key))
            return;
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges = {key};
        } else if (!SelectedEdges.erase(key)) {
            SelectedEdges.insert(key);
        }
        applySelection();
    }

    void DiagramController::clearSelection() {
        if (SelectedNodes.empty() && SelectedEdges.empty())
            return;
        SelectedNodes.clear();
        SelectedEdges.clear();
        applySelection();
    }

    void DiagramController::applySelection() {
        for (auto it = SelectedNodes.begin(); it != SelectedNodes.end();)
            it = Nodes->indexOf(*it) < 0 ? SelectedNodes.erase(it) : std::next(it);
        for (auto it = SelectedEdges.begin(); it != SelectedEdges.end();)
            it = Edges->find(*it) == nullptr ? SelectedEdges.erase(it) : std::next(it);

        for (int row = 0; row < Nodes->count(); ++row)
            Nodes->setSelected(row, SelectedNodes.count(Nodes->nodes()[std::size_t(row)].Id) > 0);

        auto edges = Edges->edges();
        for (auto &edge: edges)
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
        Edges->setEdges(std::move(edges));
        emit selectionChanged();
    }

    void DiagramController::deleteSelection() {
        if (!hasSelection())
            return;
        const std::set<QString> edges = SelectedEdges;
        const std::set<ElementId> nodes = SelectedNodes;
        for (const auto &key: edges)
            deleteEdge(key);
        for (const ElementId id: nodes) {
            if (Model.contains(id)) // may already be gone together with its owner
                runModelChange([&] { Model.removeElement(id); });
        }
        clearSelection();
        emit statusMessage(tr("Deleted. Owned elements were deleted with their owner."));
    }

    void DiagramController::deleteElement(const QString &id) {
        if (const auto elementId = toId(id))
            runModelChange([&] { Model.removeElement(*elementId); });
    }

    void DiagramController::deleteEdge(const QString &key) {
        const auto *edge = Edges->find(key);
        if (!edge)
            return;
        if (key.startsWith(QStringLiteral("c:"))) {
            // Removing ownership (also of a "ref part"): the part moves to the package (frame) it lies in, or to the top level.
            const ElementId child = edge->Target;
            if (!Model.contains(child))
                return;
            ElementId newOwner = RootId;
            if (const auto *node = Nodes->find(child)) {
                if (const auto frame = frameAt(node->Rect.center(), child))
                    newOwner = *frame;
            }
            runModelChange([&] { Model.reparent(child, newOwner); });
            return;
        }
        bool ok = false;
        const auto relationship = key.mid(2).toULongLong(&ok);
        if (ok && Model.containsRelationship(relationship))
            runModelChange([&] { Model.removeRelationship(relationship); });
    }

    // --- edges -------------------------------------------------------------------------------------

    QVariantList DiagramController::allowedEdgeKinds(const QString &source, const QString &target) const {
        QVariantList kinds;
        const auto sourceId = toId(source);
        const auto targetId = toId(target);
        if (!sourceId || !targetId || *sourceId == *targetId)
            return kinds;

        const ElementKind sourceKind = Model.element(*sourceId).kind();
        const ElementKind targetKind = Model.element(*targetId).kind();
        // composition: a part owns a part; a requirement (or part) owns a requirement
        const bool composable = (composes(sourceKind) && targetKind == ElementKind::PartUsage) ||
                                ((isPartLike(sourceKind) || isRequirementOwner(sourceKind)) &&
                                 targetKind == ElementKind::RequirementUsage && !isObjective(Model.element(*targetId)));
        if (composable && Model.element(*targetId).owner() != *sourceId && RelationshipRules::canReparent(Model, *targetId, *sourceId))
            kinds.append(int(DiagramTypes::Composition));

        for (const int kind: {int(DiagramTypes::Connection), int(DiagramTypes::Flow), int(DiagramTypes::Interface),
                              int(DiagramTypes::Specialization), int(DiagramTypes::Satisfy), int(DiagramTypes::Verify),
                              int(DiagramTypes::Include), int(DiagramTypes::Allocation), int(DiagramTypes::Dependency),
                              int(DiagramTypes::Frame)}) {
            if (RelationshipRules::canRelate(Model, *toRelationshipKind(kind), *sourceId, *targetId))
                kinds.append(kind);
        }
        return kinds;
    }

    bool DiagramController::createEdge(int kind, const QString &source, const QString &target) {
        const auto sourceId = toId(source);
        const auto targetId = toId(target);
        if (!sourceId || !targetId || (kind != DiagramTypes::Composition && !toRelationshipKind(kind)))
            return false;
        const bool ok = runModelChange([&] {
            if (kind == DiagramTypes::Composition)
                Model.reparent(*targetId, *sourceId);
            else
                Model.addRelationship(*toRelationshipKind(kind), *sourceId, *targetId);
        });
        if (ok)
            emit statusMessage(tr("%1 created.").arg(edgeKindName(kind)));
        return ok;
    }

    QVariantMap DiagramController::arrowReleased(const QString &source, qreal x, qreal y) {
        const auto sourceId = toId(source);
        if (!sourceId)
            return {};
        const QString target = nodeIdAt(QPointF(x, y), sourceId, false);
        if (target.isEmpty()) {
            emit statusMessage(tr("Release the arrow over another element."));
            return {};
        }
        const QVariantList kinds = allowedEdgeKinds(source, target);

        if (const auto preset = presetEdgeKind(ArrowDefaultValue)) {
            if (kinds.contains(*preset)) {
                createEdge(*preset, source, target);
            } else if (const auto relationship = toRelationshipKind(*preset)) {
                const auto targetId = toId(target);
                emit statusMessage(QString::fromStdString(
                        RelationshipRules::canRelate(Model, *relationship, *sourceId, *targetId).Reason));
            } else {
                emit statusMessage(tr("Composition is not possible here (a part is owned by a part, a requirement by a "
                                      "requirement or part)."));
            }
            return {};
        }

        if (kinds.isEmpty()) {
            emit statusMessage(tr("No relationship is possible between these elements."));
            return {};
        }
        return {{QStringLiteral("target"), target}, {QStringLiteral("kinds"), kinds}};
    }

    QString DiagramController::edgeKindName(int kind) const {
        switch (kind) {
            case DiagramTypes::Composition:
                return tr("Composition");
            case DiagramTypes::Connection:
                return tr("Connect");
            case DiagramTypes::Flow:
                return tr("Flow");
            case DiagramTypes::Specialization:
                return tr("Specialization");
            case DiagramTypes::Binding:
                return tr("Binding");
            case DiagramTypes::Interface:
                return tr("Interface");
            case DiagramTypes::Succession:
                return tr("Succession");
            case DiagramTypes::Transition:
                return tr("Transition");
            case DiagramTypes::Satisfy:
                return tr("Satisfy");
            case DiagramTypes::Verify:
                return tr("Verify");
            case DiagramTypes::Include:
                return tr("Include");
            case DiagramTypes::Message:
                return tr("Message");
            case DiagramTypes::Allocation:
                return tr("Allocate");
            case DiagramTypes::Dependency:
                return tr("Dependency");
            case DiagramTypes::Frame:
                return tr("Frame (concern)");
            default:
                return {};
        }
    }

    // --- editing -----------------------------------------------------------------------------------

    QString DiagramController::kindLabel(const QString &id) const {
        const auto elementId = toId(id);
        return elementId ? kindText(Model.element(*elementId)) : QString();
    }

    QVariantMap DiagramController::elementDetails(const QString &id) const {
        const auto elementId = toId(id);
        if (!elementId)
            return {};
        const auto &element = Model.element(*elementId);
        const ElementKind kind = element.kind();
        QString multiplicity;
        if (element.multiplicity().has_value()) {
            multiplicity = QString::fromStdString(element.multiplicity()->toString());
            multiplicity = multiplicity.mid(1, multiplicity.size() - 2); // without [ ]
        }
        QStringList prefixes;
        for (const auto &prefix: element.prefixes())
            prefixes.append(QString::fromStdString(prefix));
        QString value;
        if (!element.value().empty()) {
            value = QString::fromStdString(element.value());
            if (element.valueOperator() != "=")
                value = QString::fromStdString(element.valueOperator()) + QLatin1Char(' ') + value;
        }
        const bool evaluation = isConstraintOrCalc(kind);
        const bool usage = kind == ElementKind::PartUsage || kind == ElementKind::Attribute ||
                           kind == ElementKind::ActionPerformance || kind == ElementKind::ConstraintUsage ||
                           kind == ElementKind::CalcUsage || kind == ElementKind::PortUsage || kind == ElementKind::ItemUsage ||
                           kind == ElementKind::ActionUsage || kind == ElementKind::StateUsage ||
                           kind == ElementKind::RequirementUsage || kind == ElementKind::Subject || kind == ElementKind::Actor ||
                           kind == ElementKind::UseCaseUsage || kind == ElementKind::VerificationUsage ||
                           kind == ElementKind::OccurrenceUsage || kind == ElementKind::EventOccurrence ||
                           kind == ElementKind::AnalysisUsage || kind == ElementKind::MetadataUsage ||
                           kind == ElementKind::ViewUsage;
        const ElementKind ownerKind = element.owner() == RootId ? ElementKind::Package : Model.element(element.owner()).kind();
        // doc text: requirements, use cases, verifications and their objective
        const bool hasDoc = Diagram::isRequirementKind(kind) || Diagram::isCaseKind(kind);
        const auto doc = hasDoc ? SysMLTextImporter::docOf(Model, *elementId) : std::nullopt;
        // constraint: "constraint", "assert constraint", in a requirement also "require" / "assume constraint"
        QString constraintRole = QStringLiteral("constraint");
        if (kind == ElementKind::ConstraintUsage && element.declaredKeyword().has_value())
            constraintRole = QString::fromStdString(*element.declaredKeyword());
        const bool stateAction = kind == ElementKind::ActionUsage && element.owner() != RootId &&
                                 (Model.element(element.owner()).kind() == ElementKind::StateUsage ||
                                  Model.element(element.owner()).kind() == ElementKind::StateDefinition);
        const std::string declared = element.declaredKeyword().value_or("");
        QStringList redefinitions;
        for (const auto &reference: element.redefinitions())
            redefinitions.append(QString::fromStdString(reference));
        const QString redefinitionList = redefinitions.join(QStringLiteral(", "));
        QStringList annotated;
        for (const auto &reference: element.about())
            annotated.append(QString::fromStdString(reference));
        const QString aboutList = annotated.join(QStringLiteral(", "));
        return {
            {QStringLiteral("stateAction"), stateAction},
            {QStringLiteral("hasRedefinitions"), Diagram::isUsageKind(kind) && kind != ElementKind::EnumerationLiteral &&
                                                  kind != ElementKind::ControlNode && !element.isElseBranch()},
            {QStringLiteral("redefines"), redefinitionList},
            {QStringLiteral("redefinitionKeyword"), QString::fromStdString(element.redefinitionKeyword().empty()
                                                                           ? std::string(":>>") : element.redefinitionKeyword())},
            {QStringLiteral("nameFromRedefinition"), element.nameFromRedefinition()},
            {QStringLiteral("isActionUsage"), kind == ElementKind::ActionUsage && !element.isElseBranch()},
            {QStringLiteral("actionForm"), QString::fromStdString(element.actionForm() == Diagram::ActionForm::Plain
                                                                  ? std::string("action") : Diagram::toKeyword(element.actionForm()))},
            {QStringLiteral("actionClause"), QString::fromStdString(element.actionClause())},
            {QStringLiteral("until"), QString::fromStdString(element.untilCondition())},
            {QStringLiteral("isState"), kind == ElementKind::StateUsage || kind == ElementKind::StateDefinition},
            {QStringLiteral("parallel"), element.isParallel()},
            {QStringLiteral("hasTarget"), Diagram::isTargetKind(kind)},
            {QStringLiteral("target"), QString::fromStdString(element.target())},
            {QStringLiteral("targetLabel"), kind == ElementKind::Filter ? tr("Condition")
                                            : kind == ElementKind::Alias ? tr("For") : tr("What")},
            {QStringLiteral("targetPlaceholder"), kind == ElementKind::Import ? QStringLiteral("ISQ::* or vehicle::**[@Safety]")
                                                  : kind == ElementKind::Alias ? QStringLiteral("ISQ::TorqueValue")
                                                  : kind == ElementKind::Expose ? QStringLiteral("vehicle::**")
                                                  : kind == ElementKind::Render ? QStringLiteral("asTreeDiagram")
                                                  : QStringLiteral("@Safety and (as Safety).isMandatory")},
            {QStringLiteral("anonymous"), Diagram::isAnonymousKind(kind)},
            {QStringLiteral("hasAbout"), kind == ElementKind::MetadataUsage},
            {QStringLiteral("about"), aboutList},
            {QStringLiteral("role"), QString::fromStdString(declared.empty() ? std::string("action")
                                                                              : declared.substr(0, declared.find(' ')))},
            {QStringLiteral("isControl"), kind == ElementKind::ControlNode},
            {QStringLiteral("hasExpression"), evaluation},
            {QStringLiteral("expression"), QString::fromStdString(element.expression())},
            {QStringLiteral("isConstraintUsage"), kind == ElementKind::ConstraintUsage},
            {QStringLiteral("assertion"), element.declaredKeyword().has_value() &&
                                          *element.declaredKeyword() == "assert constraint"},
            {QStringLiteral("isConstraint"), Diagram::isConstraintKind(kind)},
            {QStringLiteral("constraintRole"), constraintRole},
            {QStringLiteral("inRequirement"), isRequirementOwner(ownerKind)},
            {QStringLiteral("hasDoc"), hasDoc},
            {QStringLiteral("doc"), doc ? QString::fromStdString(SysMLTextImporter::commentBody(Model.element(*doc).text()))
                                        : QString()},
            {QStringLiteral("shortName"), QString::fromStdString(element.shortName())},
            {QStringLiteral("hasShortName"), kind != ElementKind::Textual && !Diagram::isAnonymousKind(kind)},
            {QStringLiteral("mayBeUnnamed"), (kind == ElementKind::ConstraintUsage &&
                                              (constraintRole == QLatin1String("require constraint") ||
                                               constraintRole == QLatin1String("assume constraint") ||
                                               constraintRole == QLatin1String("assert constraint"))) ||
                                             kind == ElementKind::RequirementUsage || kind == ElementKind::ActionUsage ||
                                             kind == ElementKind::ControlNode || kind == ElementKind::MetadataUsage},
            {QStringLiteral("kindLabel"), kindText(element)},
            {QStringLiteral("name"), QString::fromStdString(element.name())},
            {QStringLiteral("typeName"), QString::fromStdString(element.typeName())},
            {QStringLiteral("multiplicity"), multiplicity},
            {QStringLiteral("typed"), usage},
            {QStringLiteral("hasMultiplicity"), kind == ElementKind::PartUsage || kind == ElementKind::Attribute ||
                                                kind == ElementKind::PortUsage || kind == ElementKind::ItemUsage ||
                                                kind == ElementKind::Subject || kind == ElementKind::Actor ||
                                                kind == ElementKind::OccurrenceUsage},
            {QStringLiteral("hasValue"), kind == ElementKind::PartUsage || kind == ElementKind::Attribute ||
                                         kind == ElementKind::Subject || kind == ElementKind::OccurrenceUsage},
            {QStringLiteral("value"), value},
            {QStringLiteral("prefixes"), prefixes.join(QLatin1Char(' '))},
            {QStringLiteral("isAction"), kind == ElementKind::ActionPerformance},
            {QStringLiteral("isTextual"), kind == ElementKind::Textual},
            {QStringLiteral("text"), QString::fromStdString(element.text())},
            {QStringLiteral("referenceAction"), element.performStyle() == Diagram::PerformStyle::ReferenceAction},
            {QStringLiteral("typePlaceholder"), kind == ElementKind::Attribute ? QStringLiteral("ISQ::MassValue")
                                                : kind == ElementKind::ConstraintUsage ? QStringLiteral("NonNegativeReal")
                                                : kind == ElementKind::CalcUsage ? QStringLiteral("Power")
                                                : kind == ElementKind::PortUsage ? QStringLiteral("FuelPort or ~FuelPort")
                                                : kind == ElementKind::ItemUsage ? QStringLiteral("Fuel")
                                                : kind == ElementKind::RequirementUsage ? QStringLiteral("MassRequirement")
                                                : kind == ElementKind::Subject ? QStringLiteral("Vehicle")
                                                : kind == ElementKind::Actor ? QStringLiteral("Driver")
                                                : kind == ElementKind::UseCaseUsage ? QStringLiteral("TransportPassenger")
                                                : kind == ElementKind::VerificationUsage ? QStringLiteral("MassTest")
                                                : kind == ElementKind::EventOccurrence ? QStringLiteral("SetSpeed")
                                                : kind == ElementKind::OccurrenceUsage ? QStringLiteral("CruiseControlInteraction")
                                                : QStringLiteral("Engine")}
        };
    }

    QString DiagramController::updateElement(const QString &id, const QVariantMap &values) {
        const auto elementId = toId(id);
        if (!elementId)
            return tr("The element does not exist anymore.");
        const ElementKind kind = Model.element(*elementId).kind();

        if (kind == ElementKind::Textual) {
            QString text = values.value(QStringLiteral("text")).toString();
            while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char(' ')))
                text.chop(1);
            if (text.trimmed().isEmpty())
                return tr("The text must not be empty.");
            if (text.count(QLatin1Char('{')) != text.count(QLatin1Char('}')))
                return tr("The braces { } are not balanced.");
            const auto [keyword, name] = SysMLTextImporter::describeStatement(text.toStdString());
            QString error;
            try {
                Model.setText(*elementId, text.toStdString(), keyword, name);
            } catch (const std::exception &exception) {
                error = QString::fromStdString(exception.what());
            }
            syncNow();
            return error;
        }

        const std::string name = values.value(QStringLiteral("name")).toString().trimmed().toStdString();
        const auto &current = Model.element(*elementId);
        const QString constraintRole = values.value(QStringLiteral("constraintRole"),
                                                    QString::fromStdString(current.declaredKeyword().value_or("constraint")))
                                               .toString();
        const ElementKind ownerKind = current.owner() == RootId ? ElementKind::Package : Model.element(current.owner()).kind();
        const bool requirementConstraint = kind == ElementKind::ConstraintUsage &&
                                           (constraintRole == QLatin1String("require constraint") ||
                                            constraintRole == QLatin1String("assume constraint"));
        // "assert constraint { ... }" may be unnamed as well
        const bool assertion = values.contains(QStringLiteral("constraintRole"))
                               ? constraintRole == QLatin1String("assert constraint")
                               : values.contains(QStringLiteral("assertion")) ? values.value(QStringLiteral("assertion")).toBool()
                               : current.declaredKeyword() == "assert constraint";
        // "send s to t;", "fork;" may be unnamed; a feature named like the one it redefines takes that name
        const bool nameFromRedefinition = values.value(QStringLiteral("nameFromRedefinition")).toBool();
        if (name.empty() && !nameFromRedefinition &&
            !(requirementConstraint || kind == ElementKind::RequirementUsage || kind == ElementKind::ActionUsage ||
              kind == ElementKind::ControlNode || kind == ElementKind::MetadataUsage || Diagram::isAnonymousKind(kind) ||
              (kind == ElementKind::ConstraintUsage && assertion)))
            return tr("The name must not be empty.");
        if (kind == ElementKind::MetadataUsage && values.value(QStringLiteral("typeName")).toString().trimmed().isEmpty())
            return tr("Metadata needs its definition as type, e.g. Safety.");
        if (requirementConstraint && !isRequirementOwner(ownerKind))
            return tr("require / assume constraint only in a requirement.");
        const auto [multiplicityValid, multiplicity] = parseMultiplicity(values.value(QStringLiteral("multiplicity")).toString());
        if (!multiplicityValid)
            return tr("Multiplicity must look like 6, 0..1 or 1..*");

        const std::string typeName = values.value(QStringLiteral("typeName")).toString().trimmed().toStdString();
        const bool referenceAction = values.value(QStringLiteral("referenceAction")).toBool();
        std::vector<std::string> prefixes;
        for (const auto &prefix: values.value(QStringLiteral("prefixes")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts))
            prefixes.push_back(prefix.toStdString());
        // "decode(x)" (operator "="), ":= decode(x)" or "default = 3"
        QString valueText = values.value(QStringLiteral("value")).toString().trimmed();
        QString valueOperator = QStringLiteral("=");
        for (const QString &candidate: {QStringLiteral("default :="), QStringLiteral("default ="), QStringLiteral(":="),
                                       QStringLiteral("default"), QStringLiteral("=")}) {
            if (valueText.startsWith(candidate + QLatin1Char(' ')) || valueText == candidate) {
                valueOperator = candidate;
                valueText = valueText.mid(candidate.size()).trimmed();
                break;
            }
        }

        QString expression;
        if (isConstraintOrCalc(kind)) {
            expression = values.value(QStringLiteral("expression")).toString();
            while (expression.endsWith(QLatin1Char('\n')) || expression.endsWith(QLatin1Char(' ')))
                expression.chop(1);
            if (expression.count(QLatin1Char('{')) != expression.count(QLatin1Char('}')) ||
                expression.count(QLatin1Char('(')) != expression.count(QLatin1Char(')')) ||
                expression.count(QLatin1Char('[')) != expression.count(QLatin1Char(']')))
                return tr("The brackets in the expression are not balanced.");
            const QString trimmed = expression.trimmed();
            if (trimmed.endsWith(QLatin1Char(';')))
                return tr("The expression is written without ';' at its end.");
        }

        // redefined features: "eng", "Vehicle::mass, b"
        std::vector<std::string> redefinitions;
        for (const QString &reference: values.value(QStringLiteral("redefines")).toString().split(QLatin1Char(','),
                                                                                                   Qt::SkipEmptyParts)) {
            if (!reference.trimmed().isEmpty())
                redefinitions.push_back(reference.trimmed().toStdString());
        }
        if (nameFromRedefinition && redefinitions.empty())
            return tr("Name it after a redefined feature: enter the feature it redefines.");
        const auto segments = redefinitions.empty() ? std::vector<std::string>()
                                                    : SysMLTextImporter::splitReference(redefinitions.front());
        if (nameFromRedefinition && segments.empty())
            return tr("'%1' is not a feature name.").arg(QString::fromStdString(redefinitions.front()));
        const std::string ownName = nameFromRedefinition ? segments.back() : name;
        const std::string redefinitionKeyword =
                values.value(QStringLiteral("redefinitionKeyword"), QStringLiteral(":>>")).toString().toStdString();
        // send / accept / ... with what follows the keyword
        const QString formName = values.value(QStringLiteral("actionForm"), QStringLiteral("action")).toString();
        Diagram::ActionForm form = Diagram::ActionForm::Plain;
        for (const auto candidate: {Diagram::ActionForm::Send, Diagram::ActionForm::Accept, Diagram::ActionForm::Assign,
                                    Diagram::ActionForm::Terminate, Diagram::ActionForm::If, Diagram::ActionForm::While,
                                    Diagram::ActionForm::Loop, Diagram::ActionForm::For}) {
            if (formName.toStdString() == Diagram::toKeyword(candidate))
                form = candidate;
        }
        const std::string clause = values.value(QStringLiteral("actionClause")).toString().simplified().toStdString();
        const std::string until = values.value(QStringLiteral("until")).toString().simplified().toStdString();

        QString error;
        try {
            if (values.contains(QStringLiteral("redefines")) && Diagram::isUsageKind(kind) &&
                kind != ElementKind::EnumerationLiteral && kind != ElementKind::ControlNode)
                Model.setRedefinitions(*elementId, redefinitionKeyword, redefinitions, false);
            if (ownName != Model.element(*elementId).name() && !Diagram::isAnonymousKind(kind))
                Model.rename(*elementId, ownName);
            if (Diagram::isTargetKind(kind) && values.contains(QStringLiteral("target")))
                Model.setTarget(*elementId, values.value(QStringLiteral("target")).toString().simplified().toStdString());
            if (kind == ElementKind::MetadataUsage && values.contains(QStringLiteral("about"))) {
                std::vector<std::string> about;
                for (const QString &reference: values.value(QStringLiteral("about")).toString().split(QLatin1Char(','),
                                                                                                       Qt::SkipEmptyParts))
                    if (!reference.trimmed().isEmpty())
                        about.push_back(reference.trimmed().toStdString());
                Model.setAbout(*elementId, about);
            }
            if (kind == ElementKind::MetadataUsage || kind == ElementKind::ViewUsage)
                Model.setTypeName(*elementId, typeName);
            if (nameFromRedefinition)
                Model.setRedefinitions(*elementId, redefinitionKeyword, redefinitions, true);
            if (kind == ElementKind::ActionUsage && values.contains(QStringLiteral("actionForm"))) {
                Model.setActionForm(*elementId, form, clause);
                Model.setUntilCondition(*elementId, form == Diagram::ActionForm::Loop || form == Diagram::ActionForm::While
                                                    ? until : std::string());
            }
            if ((kind == ElementKind::StateUsage || kind == ElementKind::StateDefinition) &&
                values.contains(QStringLiteral("parallel")))
                Model.setParallel(*elementId, values.value(QStringLiteral("parallel")).toBool());
            if (kind == ElementKind::PartUsage || kind == ElementKind::Attribute || kind == ElementKind::ActionPerformance ||
                kind == ElementKind::ConstraintUsage || kind == ElementKind::CalcUsage || kind == ElementKind::PortUsage ||
                kind == ElementKind::ItemUsage || kind == ElementKind::RequirementUsage || kind == ElementKind::Subject ||
                kind == ElementKind::Actor || kind == ElementKind::UseCaseUsage || kind == ElementKind::VerificationUsage ||
                kind == ElementKind::OccurrenceUsage || kind == ElementKind::EventOccurrence ||
                kind == ElementKind::AnalysisUsage)
                Model.setTypeName(*elementId, typeName);
            if (kind == ElementKind::Subject || kind == ElementKind::Actor || kind == ElementKind::OccurrenceUsage)
                Model.setMultiplicity(*elementId, multiplicity);
            if (kind == ElementKind::Subject || kind == ElementKind::OccurrenceUsage)
                Model.setValue(*elementId, valueOperator.toStdString(), valueText.toStdString());
            if (values.contains(QStringLiteral("shortName"))) {
                // "R1" stays as it is, anything else is quoted: "1.2" -> '1.2'
                QString shortName = values.value(QStringLiteral("shortName")).toString().trimmed();
                if (shortName.startsWith(QLatin1Char('<')) && shortName.endsWith(QLatin1Char('>')))
                    shortName = shortName.mid(1, shortName.size() - 2).trimmed();
                const bool basic = !shortName.isEmpty() && !shortName.front().isDigit() &&
                                   std::all_of(shortName.begin(), shortName.end(), [](QChar c) {
                                       return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                                              (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || c.isDigit() ||
                                              c == QLatin1Char('_');
                                   });
                const bool quoted = shortName.size() > 1 && shortName.startsWith(QLatin1Char('\'')) &&
                                    shortName.endsWith(QLatin1Char('\''));
                if (!shortName.isEmpty() && !basic && !quoted) {
                    shortName.replace(QLatin1String("\\"), QLatin1String("\\\\"));
                    shortName.replace(QLatin1String("'"), QLatin1String("\\'"));
                    shortName = QLatin1Char('\'') + shortName + QLatin1Char('\'');
                }
                Model.setShortName(*elementId, shortName.toStdString());
            }
            if (values.contains(QStringLiteral("doc")) && (Diagram::isRequirementKind(kind) || Diagram::isCaseKind(kind))) {
                const std::string text = values.value(QStringLiteral("doc")).toString().trimmed().toStdString();
                const auto doc = SysMLTextImporter::docOf(Model, *elementId);
                if (text.empty() && doc) {
                    Model.removeElement(*doc);
                } else if (!text.empty()) {
                    const std::string statement = SysMLTextImporter::docStatement(text);
                    if (doc) {
                        if (Model.element(*doc).text() != statement)
                            Model.setText(*doc, statement, "doc", "");
                    } else {
                        const ElementId added = Model.addElement(ElementKind::Textual, "", *elementId);
                        Model.setText(added, statement, "doc", "");
                        Model.setChildIndex(added, 0); // the text comes first
                    }
                }
            }
            if (kind == ElementKind::ConstraintUsage && values.contains(QStringLiteral("constraintRole")))
                Model.setDeclaredKeyword(*elementId, constraintRole == QLatin1String("constraint")
                                                     ? std::nullopt : std::optional<std::string>(constraintRole.toStdString()));
            if (isConstraintOrCalc(kind) && values.contains(QStringLiteral("expression")))
                Model.setExpression(*elementId, expression.toStdString());
            if (kind == ElementKind::ConstraintUsage && values.contains(QStringLiteral("assertion")) &&
                !values.contains(QStringLiteral("constraintRole"))) {
                const bool assertion = values.value(QStringLiteral("assertion")).toBool();
                Model.setDeclaredKeyword(*elementId, assertion ? std::optional<std::string>("assert constraint")
                                                               : std::nullopt);
            }
            if (kind == ElementKind::PortUsage || kind == ElementKind::ItemUsage)
                Model.setMultiplicity(*elementId, multiplicity);
            if (kind == ElementKind::ActionUsage || kind == ElementKind::StateUsage)
                Model.setTypeName(*elementId, typeName);
            if (kind == ElementKind::ActionUsage && values.contains(QStringLiteral("role"))) {
                const QString role = values.value(QStringLiteral("role")).toString();
                Model.setDeclaredKeyword(*elementId, role == QStringLiteral("action") || role.isEmpty()
                                                     ? std::nullopt
                                                     : std::optional<std::string>(role.toStdString() + " action"));
            }
            if (kind == ElementKind::PartUsage || kind == ElementKind::Attribute) {
                Model.setMultiplicity(*elementId, multiplicity);
                Model.setValue(*elementId, valueOperator.toStdString(), valueText.toStdString());
            }
            if (kind == ElementKind::ActionPerformance)
                Model.setPerformStyle(*elementId, referenceAction ? Diagram::PerformStyle::ReferenceAction
                                                                  : Diagram::PerformStyle::DeclareAction);
            if (values.contains(QStringLiteral("prefixes")))
                Model.setPrefixes(*elementId, prefixes);
        } catch (const std::exception &exception) {
            error = QString::fromStdString(exception.what());
        }
        syncNow();
        return error;
    }

    QVariantMap DiagramController::edgeDetails(const QString &key) const {
        if (!key.startsWith(QStringLiteral("r:")))
            return {};
        const auto id = key.mid(2).toULongLong();
        if (!Model.containsRelationship(id))
            return {};
        const auto &relationship = Model.relationship(id);
        const bool behavior = relationship.Kind == RelationshipKind::Succession ||
                              relationship.Kind == RelationshipKind::Transition;
        const bool labeled = relationship.Kind == RelationshipKind::Connection || relationship.Kind == RelationshipKind::Flow ||
                             relationship.Kind == RelationshipKind::Interface || relationship.Kind == RelationshipKind::Message ||
                             relationship.Kind == RelationshipKind::Allocation ||
                             relationship.Kind == RelationshipKind::Dependency || behavior;
        const bool message = relationship.Kind == RelationshipKind::Message;
        return {{QStringLiteral("kindLabel"), edgeKindName(int(toEdgeKind(relationship.Kind)))},
                {QStringLiteral("editable"), labeled},
                {QStringLiteral("hasType"), !behavior && relationship.Kind != RelationshipKind::Dependency},
                {QStringLiteral("hasSequence"), message},
                {QStringLiteral("hasDefinition"), relationship.Kind == RelationshipKind::Flow},
                {QStringLiteral("definition"), QString::fromStdString(relationship.Definition)},
                {QStringLiteral("sequenced"), relationship.Sequenced},
                {QStringLiteral("hasGuard"), behavior},
                {QStringLiteral("hasTrigger"), relationship.Kind == RelationshipKind::Transition},
                {QStringLiteral("guard"), QString::fromStdString(relationship.Guard)},
                {QStringLiteral("trigger"), QString::fromStdString(relationship.Trigger)},
                {QStringLiteral("effect"), QString::fromStdString(relationship.Effect)},
                {QStringLiteral("name"), QString::fromStdString(relationship.Name)},
                {QStringLiteral("typeName"), QString::fromStdString(relationship.TypeName)},
                {QStringLiteral("typeLabel"), relationship.Kind == RelationshipKind::Flow ? tr("Item (of)")
                                              : message ? tr("Payload (of)") : tr("Type")},
                {QStringLiteral("typePlaceholder"), relationship.Kind == RelationshipKind::Flow ? QStringLiteral("Fuel")
                                                    : relationship.Kind == RelationshipKind::Interface ? QStringLiteral("FuelInterface")
                                                    : message ? QStringLiteral("SetSpeed")
                                                    : relationship.Kind == RelationshipKind::Allocation
                                                    ? QStringLiteral("LogicalToPhysical")
                                                    : QStringLiteral("Hose")}};
    }

    QString DiagramController::updateEdge(const QString &key, const QVariantMap &values) {
        if (!key.startsWith(QStringLiteral("r:")))
            return tr("Only relationships have a name and a type.");
        const auto id = key.mid(2).toULongLong();
        if (!Model.containsRelationship(id))
            return tr("The relationship does not exist anymore.");
        QString error;
        runModelChange([&] {
            try {
                Model.setRelationshipLabel(id, values.value(QStringLiteral("name")).toString().trimmed().toStdString(),
                                           values.value(QStringLiteral("typeName")).toString().trimmed().toStdString());
                const auto kind = Model.relationship(id).Kind;
                if (kind == RelationshipKind::Message && values.contains(QStringLiteral("sequenced")))
                    Model.setSequenced(id, values.value(QStringLiteral("sequenced")).toBool());
                if (kind == RelationshipKind::Flow && values.contains(QStringLiteral("definition")))
                    Model.setFlowDefinition(id, values.value(QStringLiteral("definition")).toString().trimmed().toStdString());
                if (kind == RelationshipKind::Succession || kind == RelationshipKind::Transition)
                    Model.setBehaviorDetails(id, values.value(QStringLiteral("guard")).toString().toStdString(),
                                             values.value(QStringLiteral("trigger")).toString().toStdString(),
                                             values.value(QStringLiteral("effect")).toString().toStdString());
            } catch (const std::exception &exception) {
                error = QString::fromStdString(exception.what());
            }
        });
        return error;
    }

    std::pair<bool, std::optional<Multiplicity>> DiagramController::parseMultiplicity(const QString &input) {
        QString text = input.trimmed();
        if (text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']')))
            text = text.mid(1, text.size() - 2).trimmed();
        if (text.isEmpty())
            return {true, std::nullopt};
        if (text == QLatin1String("*"))
            return {true, Multiplicity::range(0, std::nullopt)};

        const auto parseNumber = [](const QString &value, bool &ok) -> std::size_t {
            return static_cast<std::size_t>(value.trimmed().toULongLong(&ok));
        };

        const auto separator = text.indexOf(QLatin1String(".."));
        bool ok = false;
        if (separator < 0) {
            const std::size_t count = parseNumber(text, ok);
            if (!ok)
                return {false, std::nullopt};
            return {true, Multiplicity::exactly(count)};
        }

        const std::size_t lower = parseNumber(text.left(separator), ok);
        if (!ok)
            return {false, std::nullopt};
        const QString upperText = text.mid(separator + 2).trimmed();
        if (upperText == QLatin1String("*"))
            return {true, Multiplicity::range(lower, std::nullopt)};
        const std::size_t upper = parseNumber(upperText, ok);
        if (!ok || upper < lower)
            return {false, std::nullopt};
        return {true, Multiplicity::range(lower, upper)};
    }

    // --- model -> node and edge models -------------------------------------------------------------

    bool DiagramController::runModelChange(const std::function<void()> &change) {
        bool ok = true;
        try {
            change();
        } catch (const std::exception &exception) {
            ok = false;
            emit statusMessage(QString::fromStdString(exception.what()));
        }
        syncNow();
        return ok;
    }

    void DiagramController::scheduleSync() {
        if (SyncQueued)
            return;
        SyncQueued = true;
        QMetaObject::invokeMethod(this, [this] {
            if (SyncQueued)
                syncNow();
        }, Qt::QueuedConnection);
    }

    void DiagramController::syncNow() {
        SyncQueued = false;

        std::vector<ElementId> nodeIds;
        std::function<void(ElementId)> collect = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                if (!isNodeElement(Model, child))
                    continue;
                nodeIds.push_back(child);
                collect(child);
            }
        };
        collect(RootId);
        const std::set<ElementId> wanted(nodeIds.begin(), nodeIds.end());

        for (int row = Nodes->count() - 1; row >= 0; --row) {
            const auto id = Nodes->nodes()[std::size_t(row)].Id;
            if (wanted.count(id) == 0) {
                Measured.erase(id);
                Nodes->removeAt(row);
            }
        }

        for (const ElementId id: nodeIds) {
            const auto &element = Model.element(id);
            DiagramNode node;
            node.Id = id;
            node.Depth = int(Model.ownershipPath(id).size()) - 1;
            fillNode(node, element);
            if (!node.Keyword.isEmpty())
                node.Keyword = stereotypes(element) + node.Keyword; // «variation» «part def»
            const int row = Nodes->indexOf(id);
            if (row >= 0) {
                Nodes->updateContent(row, node);
                continue;
            }
            if (node.Kind == DiagramTypes::PortNode) {
                PendingPositions.erase(id);
                node.Rect = portSlot(id);
                Nodes->append(node);
                continue;
            }
            const bool frame = node.Kind == DiagramTypes::PackageNode;
            const QSizeF size = frame ? QSizeF(DefaultFrameWidth, DefaultFrameHeight) : QSizeF(DefaultPartWidth, DefaultPartHeight);
            const auto pending = PendingPositions.find(id);
            QPointF position = pending != PendingPositions.end() ? pending->second : defaultPositionFor(id);
            if (pending != PendingPositions.end()) {
                PendingPositions.erase(pending);
            } else if (!frame) {
                // a new node of its own (a variant, a snapshot added to a part, ...): not on top of another one
                for (int attempt = 0; attempt < 40; ++attempt) {
                    const QRectF wanted(position, size);
                    const auto overlapping = std::find_if(Nodes->nodes().begin(), Nodes->nodes().end(), [&](const DiagramNode &other) {
                        return other.Kind != DiagramTypes::PackageNode && other.Kind != DiagramTypes::PortNode &&
                               other.Rect.adjusted(-12, -12, 12, 12).intersects(wanted);
                    });
                    if (overlapping == Nodes->nodes().end())
                        break;
                    position.setX(overlapping->Rect.right() + 24);
                }
            }
            node.Rect = QRectF(position, size);
            Nodes->append(node);
        }

        // Edges: ownership of a part by a part is drawn as composition, the rest are relationships.
        std::vector<DiagramEdge> edges;
        for (const ElementId id: nodeIds) {
            const ElementId owner = Model.element(id).owner();
            if (owner == RootId || wanted.count(owner) == 0)
                continue;
            const ElementKind ownerKind = Model.element(owner).kind();
            const ElementKind kind = Model.element(id).kind();
            const bool composed = (composes(ownerKind) && (kind == ElementKind::PartUsage || kind == ElementKind::OccurrenceUsage)) ||
                                  ((isPartLike(ownerKind) || isRequirementOwner(ownerKind)) && kind == ElementKind::RequirementUsage);
            if (!composed)
                continue;
            DiagramEdge edge;
            // "ref part": a reference, no composition - a plain line
            const auto &prefixes = Model.element(id).prefixes();
            const bool reference = std::find(prefixes.begin(), prefixes.end(), "ref") != prefixes.end();
            edge.Kind = Diagram::isTemporalKeyword(Model.element(id).declaredKeyword()) ? DiagramTypes::Temporal
                        : reference ? DiagramTypes::Association : DiagramTypes::Composition;
            edge.Source = owner;
            edge.Target = id;
            edge.Key = edgeKey(true, id);
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
            edges.push_back(edge);
        }
        // the node an end is shown in: itself, or (allocation, dependency) the node of the part owning it
        const auto shownIn = [&](ElementId end) -> std::optional<ElementId> {
            for (ElementId current = end; current != RootId; current = Model.element(current).owner()) {
                if (wanted.count(current) > 0)
                    return current;
            }
            return std::nullopt;
        };
        for (const auto &relationship: Model.relationships()) {
            std::optional<ElementId> source = wanted.count(relationship.Source) ? std::optional<ElementId>(relationship.Source)
                                                                                : std::nullopt;
            std::optional<ElementId> target = wanted.count(relationship.Target) ? std::optional<ElementId>(relationship.Target)
                                                                                : std::nullopt;
            const bool lifted = relationship.Kind == RelationshipKind::Allocation ||
                                relationship.Kind == RelationshipKind::Dependency;
            QString detail;
            if (lifted && (!source || !target)) {
                source = shownIn(relationship.Source);
                target = shownIn(relationship.Target);
                // "generateTorque -> engine.generateTorque": which features of the two nodes are meant
                const auto path = [&](ElementId end, std::optional<ElementId> node) {
                    QStringList names;
                    for (ElementId current = end; node && current != *node; current = Model.element(current).owner())
                        names.prepend(QString::fromStdString(Model.element(current).name()));
                    return names.join(QLatin1Char('.'));
                };
                const QString from = path(relationship.Source, source);
                const QString to = path(relationship.Target, target);
                detail = (from.isEmpty() ? QStringLiteral("·") : from) + QStringLiteral(" \u2192 ") +
                         (to.isEmpty() ? QStringLiteral("·") : to);
            }
            if (!source || !target || *source == *target)
                continue;
            DiagramEdge edge;
            edge.Kind = toEdgeKind(relationship.Kind);
            edge.Source = *source;
            edge.Target = *target;
            edge.Key = edgeKey(false, relationship.Id);
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
            edge.Label = edgeLabel(relationship);
            if (!detail.isEmpty())
                edge.Label = (edge.Label + QLatin1Char(' ') + detail).trimmed();
            edges.push_back(edge);
        }
        Edges->setEdges(std::move(edges));

        // Drop selections of elements that are gone.
        const bool selectionGone =
                std::any_of(SelectedNodes.begin(), SelectedNodes.end(), [&](ElementId id) { return wanted.count(id) == 0; }) ||
                std::any_of(SelectedEdges.begin(), SelectedEdges.end(), [&](const QString &key) { return !Edges->find(key); });
        if (selectionGone)
            applySelection();

        if (LastElementCount != int(Model.elementCount())) {
            LastElementCount = int(Model.elementCount());
            emit elementCountChanged();
        }
    }

    void DiagramController::fillNode(DiagramNode &node, const Diagram::SysMLElement &element) const {
        const ElementId id = element.id();
        const QString name = QString::fromStdString(element.name());
        switch (element.kind()) {
            case ElementKind::Package:
                node.Kind = DiagramTypes::PackageNode;
                node.Title = decorate(element, QStringLiteral("package ") + name);
                return;
            case ElementKind::Textual: {
                // any other statement on package level: a node with what can be read from its text
                const auto summary = SysMLTextImporter::summarize(element);
                node.Kind = DiagramTypes::TextualNode;
                node.Keyword = QStringLiteral("«") + QString::fromStdString(element.keyword()) + QStringLiteral("»");
                node.Title = name.isEmpty() ? QString::fromStdString(element.keyword()) : name;
                const QString fullText = QString::fromStdString(element.text());
                QVariantList parameters;
                for (const auto &parameter: summary.Parameters)
                    parameters.append(entry(id, QString::fromStdString(parameter), fullText));
                addCompartment(node.Compartments, QStringLiteral("parameters"), parameters);
                if (summary.Kind == Diagram::StatementSummary::Category::Function) {
                    QString result = QString::fromStdString(summary.Result);
                    if (!summary.Expression.empty())
                        result += QStringLiteral(" = ") + QString::fromStdString(summary.Expression);
                    if (!result.isEmpty())
                        addCompartment(node.Compartments, QStringLiteral("result"), {entry(id, result, fullText)});
                } else if (summary.Kind == Diagram::StatementSummary::Category::Constraint) {
                    if (!summary.Expression.empty())
                        addCompartment(node.Compartments, QStringLiteral("constraint"),
                                       {entry(id, QString::fromStdString(summary.Expression), fullText)});
                } else {
                    QVariantList lines;
                    const auto textLines = fullText.split(QLatin1Char('\n'));
                    for (int i = 1; i < textLines.size() && lines.size() < 6; ++i) {
                        const QString line = textLines.at(i).trimmed();
                        if (!line.isEmpty() && line != QLatin1String("}"))
                            lines.append(entry(id, line, fullText));
                    }
                    addCompartment(node.Compartments, QStringLiteral("text"), lines);
                }
                return;
            }
            case ElementKind::ConstraintDefinition:
            case ElementKind::CalcDefinition:
            case ElementKind::ConstraintUsage:
            case ElementKind::CalcUsage: {
                // SysML v2 general view: «constraint def» / «calc def» / «constraint» / «calc» with the compartments
                // parameters, result (calc) or constraint (the expression in braces), attributes, constraints, calcs.
                const ElementKind kind = element.kind();
                node.Kind = kind == ElementKind::ConstraintDefinition ? DiagramTypes::ConstraintDefinitionNode
                            : kind == ElementKind::CalcDefinition ? DiagramTypes::CalcDefinitionNode
                            : kind == ElementKind::ConstraintUsage ? DiagramTypes::ConstraintNode
                            : DiagramTypes::CalcNode;
                node.Keyword = keywordOf(element);
                node.Title = decorate(element, Diagram::isDefinitionKind(kind) ? name : usageText(Model, id), true) +
                             specializationText(element);
                QVariantList parameters;
                QVariantList result;
                QVariantList attributes;
                fillFeatureEntries(element, parameters, result, attributes, node.Compartments, false);
                if (!element.expression().empty()) {
                    const QString expression = QString::fromStdString(SysMLTextImporter::oneLine(element.expression()));
                    const QString full = QString::fromStdString(element.expression());
                    if (Diagram::isConstraintKind(kind))
                        addCompartment(node.Compartments, QStringLiteral("constraint"),
                                       {entry(id, QStringLiteral("{ ") + expression + QStringLiteral(" }"), full)});
                    else
                        result.append(entry(id, expression, full));
                }
                QVariantList ordered;
                addCompartment(ordered, QStringLiteral("parameters"), parameters);
                addCompartment(ordered, QStringLiteral("result"), result);
                addCompartment(ordered, QStringLiteral("attributes"), attributes);
                node.Compartments = ordered + node.Compartments;
                return;
            }
            case ElementKind::RequirementDefinition:
            case ElementKind::RequirementUsage:
            case ElementKind::UseCaseDefinition:
            case ElementKind::UseCaseUsage:
            case ElementKind::VerificationDefinition:
            case ElementKind::VerificationUsage:
            case ElementKind::AnalysisDefinition:
            case ElementKind::AnalysisUsage: {
                // SysML v2 general view: «requirement def» with the compartments doc, subject, stakeholders, attributes,
                // require / assume (constraints); «use case» / «verification» with subject, actors, objective, ...
                const ElementKind kind = element.kind();
                node.Kind = Diagram::isRequirementKind(kind) ? DiagramTypes::RequirementNode : DiagramTypes::CaseNode;
                node.Keyword = keywordOf(element);
                node.Title = decorate(element, Diagram::isDefinitionKind(kind) ? name : usageText(Model, id), true) +
                             specializationText(element);
                QVariantList doc;
                if (const auto docId = SysMLTextImporter::docOf(Model, id)) {
                    const QString full = QString::fromStdString(SysMLTextImporter::commentBody(Model.element(*docId).text()));
                    for (const QString &line: docLines(Model, id))
                        doc.append(entry(*docId, line, full));
                }
                QVariantList parameters;
                QVariantList result;
                QVariantList attributes;
                QVariantList rest;
                fillFeatureEntries(element, parameters, result, attributes, rest, false);
                addCompartment(node.Compartments, QStringLiteral("doc"), doc);
                // subject, actors / stakeholders, objective, includes come first (fillFeatureEntries puts them first)
                QVariantList ordered;
                QVariantList later;
                for (const auto &compartment: rest) {
                    const QString title = compartment.toMap().value(QStringLiteral("title")).toString();
                    if (title == QLatin1String("subject") || title == QLatin1String("actors") ||
                        title == QLatin1String("stakeholders") || title == QLatin1String("objective"))
                        ordered.append(compartment);
                    else
                        later.append(compartment);
                }
                node.Compartments += ordered;
                if (Diagram::isAnalysisKind(kind)) {
                    // an analysis: its parameters and its result ("return") apart from its attributes
                    QVariantList results;
                    for (const auto &entryValue: result) {
                        auto map = entryValue.toMap();
                        map[QStringLiteral("text")] = QStringLiteral("return ") + map.value(QStringLiteral("text")).toString();
                        results.append(map);
                    }
                    addCompartment(node.Compartments, QStringLiteral("parameters"), parameters + results);
                    addCompartment(node.Compartments, QStringLiteral("attributes"), attributes);
                } else {
                    addCompartment(node.Compartments, QStringLiteral("attributes"), parameters + result + attributes);
                }
                node.Compartments += later;
                return;
            }
            case ElementKind::PartDefinition:
                node.Kind = DiagramTypes::PartDefinitionNode;
                node.Keyword = QStringLiteral("«part def»");
                node.Title = decorate(element, name, true) + specializationText(element);
                break;
            case ElementKind::PortUsage:
                // a port on the border of its part: "fuelIn : ~FuelPort" next to the small square
                node.Kind = DiagramTypes::PortNode;
                node.Title = decorate(element, usageText(Model, id), true) + specializationText(element);
                return;
            case ElementKind::AttributeDefinition:
            case ElementKind::EnumerationDefinition:
            case ElementKind::ItemDefinition:
            case ElementKind::PortDefinition:
            case ElementKind::InterfaceDefinition:
            case ElementKind::ActionDefinition:
            case ElementKind::StateDefinition:
            case ElementKind::ActionUsage:
            case ElementKind::StateUsage:
            case ElementKind::OccurrenceDefinition:
            case ElementKind::AllocationDefinition:
            case ElementKind::ConnectionDefinition:
            case ElementKind::FlowDefinition:
            case ElementKind::MetadataDefinition:
            case ElementKind::ViewDefinition:
            case ElementKind::ViewUsage: {
                node.Kind = DiagramTypes::DefinitionNode;
                node.Keyword = keywordOf(element);
                if (element.kind() == ElementKind::ViewUsage) {
                    node.Title = decorate(element, usageText(Model, id), true) + specializationText(element);
                    QVariantList parameters;
                    QVariantList result;
                    QVariantList attributes;
                    QVariantList rest;
                    fillFeatureEntries(element, parameters, result, attributes, rest, false);
                    addCompartment(node.Compartments, QStringLiteral("attributes"), parameters + result + attributes);
                    node.Compartments += rest;
                    return;
                }
                node.Title = decorate(element, name, true) + specializationText(element);
                QVariantList parameters;
                QVariantList result;
                QVariantList attributes;
                QVariantList rest;
                fillFeatureEntries(element, parameters, result, attributes, rest, false);
                // directed attributes of a port def are shown with their direction (in / out)
                addCompartment(node.Compartments, QStringLiteral("attributes"), parameters + result + attributes);
                node.Compartments += rest;
                return;
            }
            case ElementKind::MetadataUsage: {
                // «metadata» SafetyFeature, its values and what it is about
                node.Kind = DiagramTypes::DefinitionNode;
                node.Keyword = QStringLiteral("«metadata»");
                node.Title = decorate(element, element.name().empty() ? QString::fromStdString(element.typeName())
                                                                      : usageText(Model, id));
                QVariantList values;
                for (const ElementId child: element.ownedElements()) {
                    const auto &value = Model.element(child);
                    if (value.kind() == ElementKind::Attribute)
                        values.append(entry(child, usageText(Model, child) + valueText(value)));
                }
                addCompartment(node.Compartments, QStringLiteral("values"), values);
                QVariantList about;
                for (const auto &reference: element.about())
                    about.append(entry(id, QString::fromStdString(reference)));
                addCompartment(node.Compartments, QStringLiteral("about"), about);
                return;
            }
            case ElementKind::Import:
            case ElementKind::Alias:
            case ElementKind::Filter:
                // «import» private ISQ::*, «alias» Car for Automobile, «filter» @Safety
                node.Kind = DiagramTypes::DefinitionNode;
                node.Keyword = QStringLiteral("«") + QString::fromStdString(Diagram::defaultKeyword(element.kind())) +
                               QStringLiteral("»");
                node.Title = (element.shortName().empty() ? QString()
                              : QStringLiteral("<") + QString::fromStdString(element.shortName()) + QStringLiteral("> ")) +
                             targetText(element);
                return;
            default:
                node.Kind = DiagramTypes::PartNode;
                // «snapshot», «timeslice part», «occurrence», «part»
                node.Keyword = Diagram::isTemporalKeyword(element.declaredKeyword())
                               ? QStringLiteral("«") + QString::fromStdString(*element.declaredKeyword()) + QStringLiteral("»")
                               : element.kind() == ElementKind::OccurrenceUsage ? QStringLiteral("«occurrence»")
                                                                                : QStringLiteral("«part»");
                node.Title = decorate(element, usageText(Model, id), true) + specializationText(element) + valueText(element);
                break;
        }

        QVariantList parameters;
        QVariantList result;
        QVariantList attributes;
        QVariantList rest;
        fillFeatureEntries(element, parameters, result, attributes, rest, true);
        // a part has no parameters: directed attributes are shown with their direction
        QVariantList all = parameters + result + attributes;
        addCompartment(node.Compartments, QStringLiteral("attributes"), all);
        node.Compartments += rest;
    }

    void DiagramController::fillFeatureEntries(const Diagram::SysMLElement &element, QVariantList &parameters,
                                               QVariantList &result, QVariantList &attributes,
                                               QVariantList &compartments, bool withActions) const {
        const Diagram::SysMLCodeGenerator generator;
        QVariantList actions;
        QVariantList constraints;
        QVariantList calcs;
        QVariantList other;
        QVariantList items;
        QVariantList ports;
        QVariantList parts;
        QVariantList literals;
        QVariantList states;
        QVariantList subject;
        QVariantList actors;
        QVariantList objective;
        QVariantList includes;
        QVariantList required;
        QVariantList assumes;
        QVariantList events;
        QVariantList occurrences;
        QVariantList messages;
        QVariantList imports;
        QVariantList metadata;
        QVariantList views;
        QVariantList exposes;
        QVariantList renders;
        QVariantList filters;
        const bool partLike = composes(element.kind());
        const bool requirementOwner = isRequirementOwner(element.kind());
        for (const ElementId child: element.ownedElements()) {
            const auto &childElement = Model.element(child);
            switch (childElement.kind()) {
                case ElementKind::ItemUsage:
                    items.append(entry(child, decorate(childElement, usageText(Model, child)) + specializationText(childElement)));
                    break;
                case ElementKind::PortUsage:
                    if (!partLike) // on a part the ports are drawn on its border
                        ports.append(entry(child, decorate(childElement, usageText(Model, child)) + specializationText(childElement)));
                    break;
                case ElementKind::PartUsage:
                    if (!partLike) // in an item: a list; in a part: nodes with composition
                        parts.append(entry(child, decorate(childElement, usageText(Model, child))));
                    break;
                case ElementKind::EventOccurrence: {
                    // "then" when a succession from the event before leads to it
                    const bool after = std::any_of(Model.relationships().begin(), Model.relationships().end(),
                                                   [&](const Diagram::Relationship &relationship) {
                                                       return relationship.Kind == RelationshipKind::Succession &&
                                                              relationship.Target == child &&
                                                              Model.element(relationship.Source).owner() == element.id();
                                                   });
                    events.append(entry(child, (after ? QStringLiteral("then ") : QString()) +
                                               decorate(childElement, usageText(Model, child))));
                    break;
                }
                case ElementKind::OccurrenceUsage:
                    if (!partLike) {
                        // "snapshot t0", "timeslice ownership[*]" in an item, port, action, ...
                        const std::string keyword = childElement.declaredKeyword().value_or("occurrence");
                        occurrences.append(entry(child, decorate(childElement, QString::fromStdString(keyword) +
                                                                                QLatin1Char(' ') + usageText(Model, child)) +
                                                        valueText(childElement)));
                    }
                    break;
                case ElementKind::EnumerationLiteral:
                    literals.append(entry(child, QString::fromStdString(childElement.name())));
                    break;
                case ElementKind::ActionUsage: {
                    // "entry / warmUp", "do / run", or "action check : Check"; "send s to t", "if c", ...
                    const std::string keyword = childElement.declaredKeyword().value_or("action");
                    const QString role = keyword == "action" ? QStringLiteral("action ")
                                         : QString::fromStdString(keyword.substr(0, keyword.find(' '))) + QStringLiteral(" / ");
                    const QString form = actionFormText(childElement);
                    actions.append(entry(child, decorate(childElement, form.isEmpty() ? role + usageText(Model, child)
                                                                      : (keyword == "action" ? QString() : role) + form)));
                    break;
                }
                case ElementKind::StateUsage:
                    states.append(entry(child, decorate(childElement, usageText(Model, child))));
                    break;
                case ElementKind::Attribute: {
                    const std::string direction = directionOf(childElement);
                    if (direction == "return") {
                        // "return" is the compartment, not a word of the entry
                        QString text = usageText(Model, child) + valueText(childElement);
                        QString metadata;
                        for (const auto &prefix: childElement.prefixes()) {
                            if (prefix != "return")
                                metadata += QString::fromStdString(prefix) + QLatin1Char(' ');
                        }
                        result.append(entry(child, metadata + text));
                    } else if (!direction.empty()) {
                        parameters.append(entry(child, decorate(childElement, usageText(Model, child)) + valueText(childElement)));
                    } else {
                        attributes.append(entry(child, decorate(childElement, usageText(Model, child)) + valueText(childElement)));
                    }
                    break;
                }
                case ElementKind::ActionPerformance:
                    actions.append(entry(child, decorate(childElement, QStringLiteral("perform ") + usageText(Model, child))));
                    break;
                case ElementKind::ConstraintDefinition:
                case ElementKind::ConstraintUsage: {
                    // in a requirement: "require" / "assume" are compartments of their own, shown without the word
                    const std::string declared = childElement.declaredKeyword().value_or("");
                    QString display = QString::fromStdString(SysMLTextImporter::summarize(Model, child).Display);
                    const QString tooltip = QString::fromStdString(generator.generate(Model, child));
                    if (requirementOwner && (declared == "require constraint" || declared == "assume constraint")) {
                        const QString word = QString::fromStdString(declared.substr(0, declared.find(' '))) + QLatin1Char(' ');
                        if (display.startsWith(word))
                            display = display.mid(word.size());
                        (declared == "require constraint" ? required : assumes).append(entry(child, display, tooltip));
                    } else {
                        constraints.append(entry(child, display, tooltip));
                    }
                    break;
                }
                case ElementKind::Subject:
                    subject.append(entry(child, decorate(childElement, usageText(Model, child)) + specializationText(childElement)));
                    break;
                case ElementKind::Actor:
                    actors.append(entry(child, decorate(childElement, usageText(Model, child)) + specializationText(childElement)));
                    break;
                case ElementKind::RequirementUsage:
                    if (isObjective(childElement)) {
                        // its text, the requirements it contains; "verify" arrows are drawn
                        const QStringList lines = docLines(Model, child);
                        const QString full = QString::fromStdString(generator.generate(Model, child));
                        for (const QString &line: lines)
                            objective.append(entry(child, line, full));
                        for (const ElementId inner: childElement.ownedElements()) {
                            const auto &innerElement = Model.element(inner);
                            if (innerElement.kind() == ElementKind::Textual && innerElement.keyword() == "doc")
                                continue;
                            objective.append(entry(inner, kindText(innerElement) + QLatin1Char(' ') +
                                                           (innerElement.kind() == ElementKind::Textual
                                                            ? QString::fromStdString(SysMLTextImporter::summarize(innerElement).Display)
                                                            : usageText(Model, inner)), full));
                        }
                        if (objective.isEmpty())
                            objective.append(entry(child, decorate(childElement, QStringLiteral("objective ") + usageText(Model, child)).trimmed()));
                    } else if (!partLike && !requirementOwner) {
                        parts.append(entry(child, QStringLiteral("requirement ") + decorate(childElement, usageText(Model, child))));
                    }
                    break;
                case ElementKind::UseCaseUsage:
                    if (childElement.declaredKeyword() == "include use case")
                        includes.append(entry(child, decorate(childElement, usageText(Model, child))));
                    else if (!partLike)
                        includes.append(entry(child, QStringLiteral("use case ") + decorate(childElement, usageText(Model, child))));
                    break;
                case ElementKind::VerificationUsage:
                    if (!partLike)
                        other.append(entry(child, QStringLiteral("verification ") + decorate(childElement, usageText(Model, child))));
                    break;
                case ElementKind::CalcDefinition:
                case ElementKind::CalcUsage:
                    calcs.append(entry(child, QString::fromStdString(SysMLTextImporter::summarize(Model, child).Display),
                                       QString::fromStdString(generator.generate(Model, child))));
                    break;
                case ElementKind::Import:
                case ElementKind::Alias:
                    imports.append(entry(child, (childElement.kind() == ElementKind::Alias ? QStringLiteral("alias ")
                                                                                           : QStringLiteral("import ")) +
                                                targetText(childElement)));
                    break;
                case ElementKind::MetadataUsage:
                    metadata.append(entry(child, metadataText(Model, child)));
                    break;
                case ElementKind::ViewUsage:
                    views.append(entry(child, decorate(childElement, usageText(Model, child))));
                    break;
                case ElementKind::Expose:
                    exposes.append(entry(child, targetText(childElement)));
                    break;
                case ElementKind::Render:
                    renders.append(entry(child, targetText(childElement)));
                    break;
                case ElementKind::Filter:
                    filters.append(entry(child, targetText(childElement)));
                    break;
                case ElementKind::Textual: {
                    if (isHiddenStatement(childElement))
                        break;
                    const auto summary = SysMLTextImporter::summarize(childElement);
                    const QString display = QString::fromStdString(summary.Display);
                    const QString tooltip = QString::fromStdString(childElement.text());
                    if (summary.Kind == Diagram::StatementSummary::Category::Constraint)
                        constraints.append(entry(child, display, tooltip));
                    else if (summary.Kind == Diagram::StatementSummary::Category::Function)
                        calcs.append(entry(child, display, tooltip));
                    else
                        other.append(entry(child, display, tooltip));
                    break;
                }
                default:
                    break;
            }
        }
        if (Diagram::isOccurrenceKind(element.kind())) {
            // the messages of an interaction: "setSpeedMessage of SetSpeed: driver.sent \u2192 vehicle.received"
            for (const auto &relationship: Model.relationships()) {
                if (relationship.Kind != RelationshipKind::Message)
                    continue;
                const bool declaredHere = relationship.Context ? *relationship.Context == element.id()
                                                               : Model.isAncestor(element.id(), relationship.Source) &&
                                                                 Model.isAncestor(element.id(), relationship.Target);
                if (!declaredHere)
                    continue;
                const auto path = [&](ElementId end) {
                    QStringList names;
                    for (ElementId current = end; current != element.id() && current != RootId;
                         current = Model.element(current).owner())
                        names.prepend(QString::fromStdString(Model.element(current).name()));
                    return names.join(QLatin1Char('.'));
                };
                QString label = edgeLabel(relationship);
                messages.append(entry(element.id(), (label.isEmpty() ? QString() : label + QStringLiteral(": ")) +
                                                    path(relationship.Source) + QStringLiteral(" \u2192 ") +
                                                    path(relationship.Target)));
            }
        }
        addCompartment(compartments, QStringLiteral("subject"), subject);
        addCompartment(compartments, requirementOwner ? QStringLiteral("stakeholders") : QStringLiteral("actors"), actors);
        addCompartment(compartments, QStringLiteral("objective"), objective);
        addCompartment(compartments, QStringLiteral("include"), includes);
        addCompartment(compartments, QStringLiteral("require"), required);
        addCompartment(compartments, QStringLiteral("assume"), assumes);
        addCompartment(compartments, QStringLiteral("values"), literals);
        const bool ends = element.kind() == ElementKind::InterfaceDefinition ||
                          element.kind() == ElementKind::AllocationDefinition ||
                          Diagram::isConnectionDefinitionKind(element.kind());
        addCompartment(compartments, ends ? QStringLiteral("ends") : QStringLiteral("ports"), ports);
        addCompartment(compartments, QStringLiteral("items"), items);
        addCompartment(compartments, ends ? QStringLiteral("ends") : QStringLiteral("parts"), parts);
        addCompartment(compartments, QStringLiteral("occurrences"), occurrences);
        addCompartment(compartments, QStringLiteral("events"), events);
        addCompartment(compartments, QStringLiteral("messages"), messages);
        const bool behavior = Diagram::isBehaviorKind(element.kind());
        if (withActions || behavior)
            addCompartment(compartments, QStringLiteral("actions"), actions);
        addCompartment(compartments, QStringLiteral("states"), states);
        addCompartment(compartments, QStringLiteral("constraints"), constraints);
        addCompartment(compartments, QStringLiteral("calcs"), calcs);
        addCompartment(compartments, QStringLiteral("views"), views);
        addCompartment(compartments, QStringLiteral("expose"), exposes);
        addCompartment(compartments, QStringLiteral("render"), renders);
        addCompartment(compartments, QStringLiteral("filter"), filters);
        addCompartment(compartments, QStringLiteral("imports"), imports);
        addCompartment(compartments, QStringLiteral("metadata"), metadata);
        addCompartment(compartments, QStringLiteral("other"), other);
    }

    QPointF DiagramController::defaultPositionFor(ElementId id) const {
        const ElementId owner = Model.element(id).owner();
        if (const auto *ownerNode = Nodes->find(owner)) {
            const auto &siblings = Model.children(owner);
            const auto index = static_cast<qreal>(std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
            if (ownerNode->Kind == DiagramTypes::PackageNode)
                return ownerNode->Rect.topLeft() + QPointF(20 + index * 30, 50 + index * 30);
            return ownerNode->Rect.topLeft() + QPointF(std::max<qreal>(0, index * 180 - 60), ownerNode->Rect.height() + 60);
        }
        const int index = Nodes->count();
        return {40.0 + (index % 4) * 220.0, 40.0 + (index / 4) * 200.0};
    }

    // --- automatic layout ---------------------------------------------------------------------------

    namespace {
        constexpr qreal LayoutMargin = 40.0;
        constexpr qreal LayoutGap = 50.0;          ///< between nodes in a row
        constexpr qreal LayoutRowGap = 60.0;       ///< between rows
        constexpr qreal CompositionGap = 70.0;     ///< owner part -> owned parts below
        constexpr qreal FramePadding = 24.0;
        constexpr qreal FrameTitleSpace = 44.0;
        constexpr qreal MaximumRowWidth = 1400.0;
        constexpr qreal MaximumFrameRowWidth = 900.0;

        /// Places sizes in rows (left to right, wrapping at maxWidth). Returns the positions and the total size.
        QSizeF flow(const std::vector<QSizeF> &sizes, qreal maxWidth, std::vector<QPointF> &positions) {
            positions.clear();
            qreal x = 0;
            qreal y = 0;
            qreal rowHeight = 0;
            qreal width = 0;
            for (const auto &size: sizes) {
                if (x > 0 && x + size.width() > maxWidth) {
                    x = 0;
                    y += rowHeight + LayoutRowGap;
                    rowHeight = 0;
                }
                positions.emplace_back(x, y);
                x += size.width() + LayoutGap;
                rowHeight = std::max(rowHeight, size.height());
                width = std::max(width, x - LayoutGap);
            }
            return {width, sizes.empty() ? 0.0 : y + rowHeight};
        }
    }

    std::vector<ElementId> DiagramController::nodeChildren(ElementId owner) const {
        std::vector<ElementId> result;
        for (const ElementId child: Model.children(owner)) {
            if (isNodeElement(Model, child) && !portAnchor(Model, child))
                result.push_back(child);
        }
        return result;
    }

    // --- ports on the border of their part ------------------------------------------------------------

    std::vector<ElementId> DiagramController::portsOf(ElementId anchor) const {
        std::vector<ElementId> ports;
        std::function<void(ElementId)> collect = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                if (Model.element(child).kind() != ElementKind::PortUsage)
                    continue;
                ports.push_back(child);
                collect(child);
            }
        };
        collect(anchor);
        return ports;
    }

    QRectF DiagramController::portSlot(ElementId port) const {
        const auto anchor = portAnchor(Model, port);
        const auto *anchorNode = anchor ? Nodes->find(*anchor) : nullptr;
        if (!anchorNode)
            return QRectF(0, 0, PortSize, PortSize);
        // incoming ports on the left border, all others on the right, in the order of the text
        const auto incoming = [&](ElementId id) {
            const auto &prefixes = Model.element(id).prefixes();
            return std::find(prefixes.begin(), prefixes.end(), "in") != prefixes.end();
        };
        int index = 0;
        for (const ElementId other: portsOf(*anchor)) {
            if (other == port)
                break;
            if (incoming(other) == incoming(port))
                ++index;
        }
        const QRectF rect = anchorNode->Rect;
        const qreal x = incoming(port) ? rect.left() : rect.right();
        const qreal y = std::min(rect.top() + PortTop + index * PortSpacing, rect.bottom() - PortSize / 2);
        return QRectF(x - PortSize / 2, y - PortSize / 2, PortSize, PortSize);
    }

    void DiagramController::placePorts(ElementId anchor) {
        for (const ElementId port: portsOf(anchor)) {
            const int row = Nodes->indexOf(port);
            if (row >= 0)
                Nodes->setRect(row, portSlot(port));
        }
    }

    void DiagramController::translatePorts(ElementId anchor, const QPointF &delta) {
        if (!Model.contains(anchor) || !isPartLike(Model.element(anchor).kind()))
            return;
        for (const ElementId port: portsOf(anchor)) {
            const int row = Nodes->indexOf(port);
            if (row >= 0)
                Nodes->setRect(row, Nodes->nodes()[std::size_t(row)].Rect.translated(delta));
        }
    }

    QPointF DiagramController::onBorder(const QRectF &rect, const QPointF &point) {
        // nearest point on the outline of rect
        const qreal x = std::clamp(point.x(), rect.left(), rect.right());
        const qreal y = std::clamp(point.y(), rect.top(), rect.bottom());
        const qreal left = std::abs(x - rect.left());
        const qreal right = std::abs(rect.right() - x);
        const qreal top = std::abs(y - rect.top());
        const qreal bottom = std::abs(rect.bottom() - y);
        const qreal nearest = std::min({left, right, top, bottom});
        if (nearest == left)
            return {rect.left(), y};
        if (nearest == right)
            return {rect.right(), y};
        if (nearest == top)
            return {x, rect.top()};
        return {x, rect.bottom()};
    }

    QString DiagramController::portSide(const QString &id) const {
        const auto elementId = toId(id);
        const auto anchor = elementId ? portAnchor(Model, *elementId) : std::nullopt;
        const auto *node = elementId ? Nodes->find(*elementId) : nullptr;
        const auto *anchorNode = anchor ? Nodes->find(*anchor) : nullptr;
        if (!node || !anchorNode)
            return QStringLiteral("right");
        const QPointF c = node->Rect.center();
        const QRectF r = anchorNode->Rect;
        const qreal left = std::abs(c.x() - r.left());
        const qreal right = std::abs(r.right() - c.x());
        const qreal top = std::abs(c.y() - r.top());
        const qreal bottom = std::abs(r.bottom() - c.y());
        const qreal nearest = std::min({left, right, top, bottom});
        return nearest == left ? QStringLiteral("left") : nearest == right ? QStringLiteral("right")
               : nearest == top ? QStringLiteral("top") : QStringLiteral("bottom");
    }

    QSizeF DiagramController::estimatedSize(const DiagramNode &node) const {
        if (Measured.count(node.Id) > 0 || node.Kind == DiagramTypes::PortNode)
            return node.Rect.size();
        // Same structure as NodeDelegate: keyword, title, compartments with caption and entries.
        qreal longest = std::max(node.Keyword.size() * 7.0, node.Title.size() * 8.5);
        qreal height = 4 + 18 + 24;
        for (const auto &compartment: node.Compartments) {
            const auto entries = compartment.toMap().value(QStringLiteral("entries")).toList();
            for (const auto &item: entries)
                longest = std::max(longest, item.toMap().value(QStringLiteral("text")).toString().size() * 7.6);
            height += 1 + 16 + entries.size() * 19.0 + 4;
        }
        return {std::max<qreal>(140, longest + 16), height + 8};
    }

    QSizeF DiagramController::measure(ElementId id, std::map<ElementId, QSizeF> &extents,
                                      std::map<ElementId, std::vector<QPointF>> &framePositions) {
        const auto *node = Nodes->find(id);
        if (!node)
            return {};
        const auto children = nodeChildren(id);
        std::vector<QSizeF> childSizes;
        for (const ElementId child: children)
            childSizes.push_back(measure(child, extents, framePositions));

        QSizeF extent;
        if (node->Kind == DiagramTypes::PackageNode) {
            std::vector<QPointF> positions;
            const QSizeF content = flow(childSizes, MaximumFrameRowWidth, positions);
            framePositions[id] = positions;
            extent = QSizeF(std::max(DefaultFrameWidth * 0.75, content.width() + 2 * FramePadding),
                            std::max(DefaultFrameHeight * 0.6, content.height() + FrameTitleSpace + FramePadding));
        } else {
            QSizeF own = estimatedSize(*node);
            // room for the labels of the ports next to the border
            qreal labels = 0;
            for (const ElementId port: portsOf(id)) {
                if (const auto *portNode = Nodes->find(port))
                    labels = std::max<qreal>(labels, portNode->Title.size() * 7.0 + 16);
            }
            own.setWidth(own.width() + 2 * labels);
            own.setHeight(std::max(own.height(), PortTop + qreal(portsOf(id).size()) * PortSpacing / 2 + 8));
            if (children.empty()) {
                extent = own;
            } else {
                qreal width = 0;
                qreal height = 0;
                for (const auto &size: childSizes) {
                    width += size.width();
                    height = std::max(height, size.height());
                }
                width += LayoutGap * qreal(childSizes.size() - 1);
                extent = QSizeF(std::max(own.width(), width), own.height() + CompositionGap + height);
            }
        }
        extents[id] = extent;
        return extent;
    }

    void DiagramController::place(ElementId id, const QPointF &topLeft, const std::map<ElementId, QSizeF> &extents,
                                  const std::map<ElementId, std::vector<QPointF>> &framePositions) {
        const int row = Nodes->indexOf(id);
        if (row < 0)
            return;
        const auto &node = Nodes->nodes()[std::size_t(row)];
        const QSizeF extent = extents.at(id);
        const auto children = nodeChildren(id);

        if (node.Kind == DiagramTypes::PackageNode) {
            Nodes->setRect(row, QRectF(topLeft, extent));
            const auto &positions = framePositions.at(id);
            for (std::size_t i = 0; i < children.size() && i < positions.size(); ++i)
                place(children[i], topLeft + QPointF(FramePadding, FrameTitleSpace) + positions[i], extents, framePositions);
            return;
        }

        const QSizeF own = estimatedSize(node);
        Nodes->setRect(row, QRectF(QPointF(topLeft.x() + (extent.width() - own.width()) / 2, topLeft.y()), own));
        // own extent incl. port labels: children start below the part itself

        if (children.empty())
            return;
        qreal childrenWidth = 0;
        for (const ElementId child: children)
            childrenWidth += extents.at(child).width();
        childrenWidth += LayoutGap * qreal(children.size() - 1);
        qreal x = topLeft.x() + (extent.width() - childrenWidth) / 2;
        const qreal y = topLeft.y() + own.height() + CompositionGap;
        for (const ElementId child: children) {
            place(child, QPointF(x, y), extents, framePositions);
            x += extents.at(child).width() + LayoutGap;
        }
    }

    void DiagramController::layoutAll() {
        syncNow();
        std::map<ElementId, QSizeF> extents;
        std::map<ElementId, std::vector<QPointF>> framePositions;
        const auto roots = nodeChildren(RootId);
        std::vector<QSizeF> sizes;
        for (const ElementId id: roots)
            sizes.push_back(measure(id, extents, framePositions));
        std::vector<QPointF> positions;
        flow(sizes, MaximumRowWidth, positions);
        for (std::size_t i = 0; i < roots.size(); ++i)
            place(roots[i], QPointF(LayoutMargin, LayoutMargin) + positions[i], extents, framePositions);
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind == DiagramTypes::PartNode || node.Kind == DiagramTypes::PartDefinitionNode)
                placePorts(node.Id);
        }
        AutoLayout = true;
    }

    QRectF DiagramController::contentBounds() const {
        QRectF bounds;
        for (const auto &node: Nodes->nodes())
            bounds = bounds.isNull() ? node.Rect : bounds.united(node.Rect);
        return bounds;
    }

    std::string DiagramController::uniqueName(const std::string &base, ElementId owner) const {
        const auto &siblings = Model.children(owner);
        for (int index = 1;; ++index) {
            const std::string candidate = base + std::to_string(index);
            const bool used = std::any_of(siblings.begin(), siblings.end(),
                                          [&](ElementId sibling) { return Model.element(sibling).name() == candidate; });
            if (!used)
                return candidate;
        }
    }
}
