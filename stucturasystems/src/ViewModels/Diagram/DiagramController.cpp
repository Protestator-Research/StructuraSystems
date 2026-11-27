//
// Editing logic of the graphical editor.
//

#include "DiagramController.h"
#include "DiagramLayout.h"
#include "EdgeRouting.h"
#include "DiagramGeometry.h"

#include <QRegularExpression>
#include <QSet>
#include <QJsonArray>
#include <QMetaObject>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/RelationshipRules.h>

#include <algorithm>
#include <functional>
#include <exception>
#include <iterator>
#include <tuple>
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
                case DiagramTypes::ToolEnumValue:
                    return ElementKind::EnumerationLiteral;
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

        /// A reference to a type or feature as SysML expects it: "Motor", "ScalarValues::Real", "'my type'", "a.b";
        /// with conjugated "~KraftPort" for types. Not a sentence, a number or a keyword.
        bool isReferenceText(const std::string &text, bool conjugationAllowed) {
            std::string reference = text;
            if (conjugationAllowed && !reference.empty() && reference.front() == '~')
                reference.erase(0, 1);
            if (!Diagram::SysMLTextImporter::parseReference(reference))
                return false;
            for (const auto &segment: Diagram::SysMLTextImporter::splitReference(reference)) {
                if (Diagram::SysMLCodeGenerator::formatName(segment) != segment &&
                    reference.find("'" + segment + "'") == std::string::npos)
                    return false; // a keyword written without quotes
            }
            return true;
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
                case DiagramTypes::Exhibit:
                    return RelationshipKind::Exhibit;
                case DiagramTypes::Assert:
                    return RelationshipKind::Assert;
                case DiagramTypes::Assume:
                    return RelationshipKind::Assume;
                case DiagramTypes::Require:
                    return RelationshipKind::Require;
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
                case RelationshipKind::Exhibit:
                    return DiagramTypes::Exhibit;
                case RelationshipKind::Assert:
                    return DiagramTypes::Assert;
                case RelationshipKind::Assume:
                    return DiagramTypes::Assume;
                case RelationshipKind::Require:
                    return DiagramTypes::Require;
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

        /// Prefixes shown in the keyword of a node («variation part def», «abstract part def», «individual part»),
        /// not in its title.
        bool isStereotypeWord(const std::string &prefix) {
            return prefix == "variation" || prefix == "variant" || prefix == "individual" || prefix == "abstract";
        }

        /// The keyword of a node with its prefixes, as in the name compartments of the graphical notation:
        /// «part def» -> «abstract part def», «variation part def», «individual occurrence def», «variant part».
        QString withStereotypes(const Diagram::SysMLElement &element, const QString &keyword) {
            QString words;
            for (const auto &prefix: element.prefixes()) {
                if (isStereotypeWord(prefix))
                    words += QString::fromStdString(prefix) + QLatin1Char(' ');
            }
            if (words.isEmpty() || !keyword.startsWith(QStringLiteral("«")))
                return keyword;
            return QStringLiteral("«") + words + keyword.mid(1);
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
                    derived = true; // "/attribute12" (tables 5, 7)
                else if (text == QLatin1String("constant"))
                    words += QStringLiteral("readonly "); // the graphical notation: "readonly attribute13" (tables 5, 7)
                else
                    words += text + QLatin1Char(' ');
            }
            const QString shortName = element.shortName().empty()
                                      ? QString() : QStringLiteral("<") + QString::fromStdString(element.shortName()) + QStringLiteral("> ");
            return metadata + words + (derived ? QStringLiteral("/") : QString()) + shortName + name;
        }

        /// A text broken into lines of at most width characters at word boundaries (its own line breaks kept).
        QStringList wrapLines(const QString &text, int width) {
            QStringList lines;
            for (const QString &paragraph: text.split(QLatin1Char('\n'))) {
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
            while (!lines.isEmpty() && lines.last().isEmpty())
                lines.removeLast();
            return lines;
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
            // as written: ":>", "subsets" (usage) or "specializes" (definition)
            const QString keyword = element.specializationKeyword().empty()
                                    ? QStringLiteral(":>") : QString::fromStdString(element.specializationKeyword());
            for (const auto &reference: element.externalSpecializations())
                text += (text.isEmpty() ? QLatin1Char(' ') + keyword + QLatin1Char(' ') : QStringLiteral(", ")) +
                        QString::fromStdString(reference);
            return text;
        }

        /// A comment, doc or rep that is drawn as a note (table 2); a "//" note is no element and stays in the code.
        bool isNoteStatement(const Diagram::SysMLElement &element) {
            if (element.kind() != ElementKind::Textual)
                return false;
            const auto &keyword = element.keyword();
            if (keyword != "comment" && keyword != "doc" && keyword != "rep")
                return false;
            return !SysMLTextImporter::describeAnnotation(element.text()).Keyword.empty();
        }

        /// Statements kept as text that are not shown in the diagram (they stay in the code).
        bool isHiddenStatement(const Diagram::SysMLElement &element) {
            if (element.kind() != ElementKind::Textual)
                return false;
            const auto &keyword = element.keyword();
            if (keyword == "comment" || keyword == "doc" || keyword == "rep")
                return !isNoteStatement(element);
            return keyword == "import" || keyword == "alias";
        }

        bool hasPrefix(const Diagram::SysMLElement &element, const char *prefix) {
            return std::find(element.prefixes().begin(), element.prefixes().end(), prefix) != element.prefixes().end();
        }

        /// "variation part def", "variation item itemChoice": an element whose variants are listed in it.
        bool isVariation(const Diagram::SysMLElement &element) { return hasPrefix(element, "variation"); }

        /// "variant part1;", "variant part part1 : Part1;", also one kept as text ("variant part references part1;").
        bool isVariantElement(const Diagram::SysMLElement &element) {
            if (element.kind() == ElementKind::Textual)
                return element.text().rfind("variant ", 0) == 0;
            return hasPrefix(element, "variant");
        }

        QVariantMap entry(ElementId id, const QString &text, const QString &tooltip = QString()) {
            return {{QStringLiteral("entryId"), QString::number(id)},
                    {QStringLiteral("text"), elide(text)},
                    {QStringLiteral("tooltip"), tooltip.isEmpty() && text.size() > MaximumEntryLength ? text : tooltip}};
        }

        /// Reserved words of the SysML v2 textual notation (as in SysMLCodeGenerator).
        bool isSysMLKeyword(const QString &word) {
            static const QSet<QString> keywords = {
                    "about", "abstract", "accept", "action", "actor", "after", "alias", "all", "allocate",
                    "allocation", "analysis", "and", "as", "assert", "assign", "assume", "at", "attribute",
                    "bind", "binding", "by", "calc", "case", "comment", "concern", "connect", "connection",
                    "constant", "constraint", "crosses", "decide", "def", "default", "defined", "dependency",
                    "derived", "do", "doc", "else", "end", "entry", "enum", "event", "exhibit", "exit",
                    "expose", "false", "filter", "first", "flow", "for", "fork", "frame", "from", "hastype",
                    "if", "implies", "import", "in", "include", "individual", "inout", "interface", "istype",
                    "item", "join", "language", "library", "locale", "loop", "merge", "message", "meta",
                    "metadata", "new", "nonunique", "not", "null", "objective", "occurrence", "of", "or",
                    "ordered", "out", "package", "parallel", "part", "perform", "port", "private",
                    "protected", "public", "readonly", "redefines", "ref", "references", "render", "rendering", "rep",
                    "require", "requirement", "return", "satisfy", "send", "snapshot", "specializes",
                    "stakeholder", "standard", "state", "subject", "subsets", "succession", "terminate",
                    "then", "timeslice", "to", "transition", "true", "until", "use", "variant", "variation",
                    "verification", "verify", "via", "view", "viewpoint", "when", "while", "xor"};
            return keywords.contains(word);
        }

        /// An entry of a compartment as in the tables of the specification: the keywords in bold ("ref part",
        /// "subsets", "defines" given as keyword of the entry), the brackets of the multiplicity too ("[0..*]");
        /// names in quotes ('part') and the brackets of units in a value ("= 60 [mm]") stay as they are.
        QString richEntryText(const QString &text, const QString &keyword) {
            const auto escaped = [](QString value) {
                return value.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
                        .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
                        .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
            };
            QString result;
            int at = 0;
            if (!keyword.isEmpty() && text.startsWith(keyword)) {
                result = QStringLiteral("<b>") + escaped(keyword) + QStringLiteral("</b>");
                at = int(keyword.size());
            }
            bool value = false; // after "=", ":=", "default": an expression, its brackets are units
            while (at < text.size()) {
                const QChar c = text.at(at);
                if (c == QLatin1Char('\'')) {
                    const int end = text.indexOf(QLatin1Char('\''), at + 1);
                    const int stop = end < 0 ? int(text.size()) : end + 1;
                    result += escaped(text.mid(at, stop - at));
                    at = stop;
                } else if (c.isLetter() || c == QLatin1Char('_')) {
                    int end = at;
                    while (end < text.size() && (text.at(end).isLetterOrNumber() || text.at(end) == QLatin1Char('_')))
                        ++end;
                    const QString word = text.mid(at, end - at);
                    // part of a name ("x.in", "P::part" stay plain)
                    const bool before = at > 0 && (text.at(at - 1).isLetterOrNumber() || text.at(at - 1) == QLatin1Char('.') ||
                                                   (text.at(at - 1) == QLatin1Char(':') && at > 1 &&
                                                    text.at(at - 2) == QLatin1Char(':')));
                    // a name that is a keyword ('part' : Engine, written in quotes): followed by ":" / "[" / the end
                    int next = end;
                    while (next < text.size() && text.at(next) == QLatin1Char(' '))
                        ++next;
                    // (at the end only as the whole entry: "readonly attribute13 ordered" keeps its keywords)
                    const bool atEnd = next >= text.size();
                    const bool name = (atEnd && at == 0) ||
                                      (!atEnd && text.at(next) == QLatin1Char('[')) ||
                                      (!atEnd && text.at(next) == QLatin1Char(':') &&
                                       !(next + 1 < text.size() && (text.at(next + 1) == QLatin1Char(':') ||
                                                                     text.at(next + 1) == QLatin1Char('>') ||
                                                                     text.at(next + 1) == QLatin1Char('='))));
                    if (word == QLatin1String("default"))
                        value = true;
                    result += !before && !name && isSysMLKeyword(word) ? QStringLiteral("<b>") + word + QStringLiteral("</b>") : word;
                    at = end;
                } else if (!value && (text.mid(at, 3) == QLatin1String(":>>") || text.mid(at, 3) == QLatin1String("::>"))) {
                    result += QStringLiteral("<b>") + escaped(text.mid(at, 3)) + QStringLiteral("</b>"); // ":>> attribute5"
                    at += 3;
                } else if (!value && (text.mid(at, 2) == QLatin1String(":>") || text.mid(at, 2) == QLatin1String(":="))) {
                    if (text.mid(at, 2) == QLatin1String(":="))
                        value = true;
                    result += QStringLiteral("<b>") + escaped(text.mid(at, 2)) + QStringLiteral("</b>"); // ":> attribute7"
                    at += 2;
                } else if (c == QLatin1Char('^') && at == 0) {
                    result += QStringLiteral("<b>^</b>"); // "^attribute2" (inherited)
                    ++at;
                } else {
                    if (c == QLatin1Char('=') && !value && !(at + 1 < text.size() && text.at(at + 1) == QLatin1Char('='))) {
                        value = true;
                        result += QStringLiteral("<b>=</b>"); // "attribute8 : AttributeDef8 = expression1"
                        ++at;
                        continue;
                    }
                    if (!value && (c == QLatin1Char('[') || c == QLatin1Char(']')))
                        result += QStringLiteral("<b>") + c + QStringLiteral("</b>");
                    else
                        result += escaped(QString(c));
                    ++at;
                }
            }
            return result;
        }

        void addCompartment(QVariantList &compartments, const QString &title, const QVariantList &entries) {
            if (entries.isEmpty())
                return;
            QVariantList styled = entries;
            if (title != QLatin1String("doc")) { // the text of a doc / comment as written
                for (auto &value: styled) {
                    QVariantMap map = value.toMap();
                    map[QStringLiteral("rich")] = richEntryText(map.value(QStringLiteral("text")).toString(),
                                                                map.value(QStringLiteral("keyword")).toString());
                    value = map;
                }
            }
            compartments.append(QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("entries"), styled}});
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

        /**
         * The element a reference names, seen from 'context': "a::b" (qualified) or "a.b" (a feature path), the
         * first segment looked up in context and its owners (innermost first), otherwise anywhere if it is unique.
         */
        std::optional<ElementId> resolveReference(const Diagram::SemanticModel &model, ElementId context,
                                                  const std::string &reference) {
            std::string text = reference;
            if (!text.empty() && text.front() == '~')
                text.erase(0, 1);
            const auto segments = SysMLTextImporter::splitReference(text);
            if (segments.empty())
                return std::nullopt;
            const auto childNamed = [&](ElementId owner, const std::string &name) -> std::optional<ElementId> {
                for (const ElementId child: model.children(owner)) {
                    const auto &element = model.element(child);
                    if (element.kind() != ElementKind::Textual && element.name() == name)
                        return child;
                }
                return std::nullopt;
            };
            std::optional<ElementId> first;
            for (ElementId scope = model.contains(context) ? context : RootId;; scope = model.element(scope).owner()) {
                if ((first = childNamed(scope, segments.front())))
                    break;
                if (scope == RootId)
                    break;
            }
            if (!first) {
                // anywhere, if only one element has the name
                std::vector<ElementId> found;
                std::function<void(ElementId)> search = [&](ElementId owner) {
                    for (const ElementId child: model.children(owner)) {
                        if (model.element(child).kind() != ElementKind::Textual && model.element(child).name() == segments.front())
                            found.push_back(child);
                        search(child);
                    }
                };
                search(RootId);
                if (found.size() != 1)
                    return std::nullopt;
                first = found.front();
            }
            ElementId current = *first;
            for (std::size_t i = 1; i < segments.size(); ++i) {
                const auto next = childNamed(current, segments[i]);
                if (!next)
                    return std::nullopt;
                current = *next;
            }
            return current;
        }

        /// How 'target' is written seen from 'source': its name if that finds it, else its qualified name.
        std::string referenceTo(const Diagram::SemanticModel &model, ElementId source, ElementId target) {
            const std::string name = Diagram::SysMLCodeGenerator::formatName(model.element(target).name());
            if (resolveReference(model, source, model.element(target).name()) == target)
                return name;
            std::string qualified;
            for (const ElementId id: model.ownershipPath(target)) {
                if (!qualified.empty())
                    qualified += "::";
                qualified += Diagram::SysMLCodeGenerator::formatName(model.element(id).name());
            }
            return qualified;
        }

        /// ":> part1, ISQ::mass" of a feature listed as text: what it subsets in the model (no line between entries)
        /// and what it subsets outside it, with the word as written (":>", "subsets").
        QString specializationText(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            QStringList targets;
            for (const auto &relationship: model.relationships()) {
                if (relationship.Kind == RelationshipKind::Specialization && relationship.Source == id &&
                    model.contains(relationship.Target))
                    targets.append(QString::fromStdString(referenceTo(model, element.owner(), relationship.Target)));
            }
            for (const auto &reference: element.externalSpecializations())
                targets.append(QString::fromStdString(reference));
            if (targets.isEmpty())
                return {};
            const QString keyword = element.specializationKeyword().empty()
                                    ? QStringLiteral(":>") : QString::fromStdString(element.specializationKeyword());
            return QLatin1Char(' ') + keyword + QLatin1Char(' ') + targets.join(QStringLiteral(", "));
        }

        /// The element an import names ("Definitions::*" -> Definitions, "vehicle::**[@Safety]" -> vehicle).
        std::optional<ElementId> importedElement(const Diagram::SemanticModel &model, ElementId import) {
            std::string target = model.element(import).target();
            if (const auto filter = target.find('['); filter != std::string::npos)
                target = target.substr(0, filter);
            for (const char *suffix: {"::**", "::*"}) {
                const std::string ending(suffix);
                if (target.size() > ending.size() && target.compare(target.size() - ending.size(), ending.size(), ending) == 0) {
                    target.erase(target.size() - ending.size());
                    break;
                }
            }
            return resolveReference(model, model.element(import).owner(), target);
        }

        /// A group of notations: what the specification allows to draw in more than one way.
        struct NotationGroupInfo {
            const char *Id;
            const char *Label;
            std::vector<std::pair<const char *, const char *>> Options; ///< value, label
        };

        const std::vector<NotationGroupInfo> &notationTable() {
            static const std::vector<NotationGroupInfo> table = {
                    // table 3: the members of a package as nodes / "Package with members compartment"
                    {"members", QT_TRANSLATE_NOOP("DiagramController", "Members of a package"),
                     {{"nested", QT_TRANSLATE_NOOP("DiagramController", "Nodes in the package")},
                      {"compartment", QT_TRANSLATE_NOOP("DiagramController", "Text (members)")}}},
                    // table 4: an element in its package / alone with its qualified name (Package1::Package2::PartDef3)
                    {"qualified", QT_TRANSLATE_NOOP("DiagramController", "Element in a package"),
                     {{"nested", QT_TRANSLATE_NOOP("DiagramController", "Inside its package")},
                      {"qualified", QT_TRANSLATE_NOOP("DiagramController", "Alone, qualified name")}}},
                    // table 4: a specialization as line / ":> PartDef0" in the name compartment
                    {"specialization", QT_TRANSLATE_NOOP("DiagramController", "Specialization"),
                     {{"line", QT_TRANSLATE_NOOP("DiagramController", "Line (hollow triangle)")},
                      {"name", QT_TRANSLATE_NOOP("DiagramController", ":> in the name")}}},
                    // table 3: nested / "Membership (owned member)" with ⊕ / packages compartment
                    {"packages", QT_TRANSLATE_NOOP("DiagramController", "Package in package"),
                     {{"nested", QT_TRANSLATE_NOOP("DiagramController", "In the package")},
                      {"line", QT_TRANSLATE_NOOP("DiagramController", "Line with \u2295")},
                      {"compartment", QT_TRANSLATE_NOOP("DiagramController", "Text (packages)")}}},
                    // table 3: nested notation / «import» arrow ("Import (recursive)") / imports compartment
                    {"imports", QT_TRANSLATE_NOOP("DiagramController", "Imports"),
                     {{"nested", QT_TRANSLATE_NOOP("DiagramController", "In the package (dashed)")},
                      {"line", QT_TRANSLATE_NOOP("DiagramController", "\u00abimport\u00bb arrow")},
                      {"compartment", QT_TRANSLATE_NOOP("DiagramController", "Text (imports)")}}},
                    // table 3: alias member symbol / "Membership (unowned member with alias name)" / table 4: name
                    {"aliases", QT_TRANSLATE_NOOP("DiagramController", "Aliases"),
                     {{"nested", QT_TRANSLATE_NOOP("DiagramController", "Symbol in the package")},
                      {"line", QT_TRANSLATE_NOOP("DiagramController", "Line with \u25cb and name")},
                      {"name", QT_TRANSLATE_NOOP("DiagramController", "\u00abalias\u00bb at the element")}}},
                    // table 9: composition lines / "Part with graphical compartment" / parts compartment
                    {"features", QT_TRANSLATE_NOOP("DiagramController", "Parts of a part"),
                     {{"line", QT_TRANSLATE_NOOP("DiagramController", "Nodes with \u25c6 line")},
                      {"nested", QT_TRANSLATE_NOOP("DiagramController", "Inside the part")},
                      {"compartment", QT_TRANSLATE_NOOP("DiagramController", "Text (parts)")}}},
                    // table 4: inherited parts with ^ in the compartment / "Name Compartment - Inherited Usage" as node
                    {"inherited", QT_TRANSLATE_NOOP("DiagramController", "Inherited parts"),
                     {{"compartment", QT_TRANSLATE_NOOP("DiagramController", "Text (^ in parts)")},
                      {"nodes", QT_TRANSLATE_NOOP("DiagramController", "Nodes (^part1, no text)")}}},
                    // table 9: "Part with Ports" / ports compartment
                    {"ports", QT_TRANSLATE_NOOP("DiagramController", "Ports"),
                     {{"border", QT_TRANSLATE_NOOP("DiagramController", "On the border")},
                      {"compartment", QT_TRANSLATE_NOOP("DiagramController", "Text (ports)")}}},
                    // table 3 / 4: the type in the name / "Part defined by Part" line
                    {"typing", QT_TRANSLATE_NOOP("DiagramController", "Type"),
                     {{"name", QT_TRANSLATE_NOOP("DiagramController", "In the name")},
                      {"line", QT_TRANSLATE_NOOP("DiagramController", "Line (defined by)")}}},
                    // variants of a variation: nodes with variant membership (circle with a cross) / compartment
                    // 'variants' / 'variant parts' (graphical BNF 8.2.3, "Variants Compartment")
                    {"variants", QT_TRANSLATE_NOOP("DiagramController", "Variants"),
                     {{"line", QT_TRANSLATE_NOOP("DiagramController", "Nodes with \u2295 line")},
                      {"compartment", QT_TRANSLATE_NOOP("DiagramController", "Text (variants)")}}},
                    // specializes, subsets, defined by, connect to, ...: lines / compartment 'relationships'
                    // ("Relationships Compartment")
                    {"relationships", QT_TRANSLATE_NOOP("DiagramController", "Relationships"),
                     {{"line", QT_TRANSLATE_NOOP("DiagramController", "Lines")},
                      {"compartment", QT_TRANSLATE_NOOP("DiagramController", "Text (relationships)")}}},
                    // table 2: a note with a line / documentation compartment
                    {"docs", QT_TRANSLATE_NOOP("DiagramController", "Doc"),
                     {{"note", QT_TRANSLATE_NOOP("DiagramController", "Note with line")},
                      {"compartment", QT_TRANSLATE_NOOP("DiagramController", "Compartment doc")}}},
            };
            return table;
        }

        const NotationGroupInfo *notationGroup(const QString &group) {
            for (const auto &info: notationTable()) {
                if (group == QLatin1String(info.Id))
                    return &info;
            }
            return nullptr;
        }

        bool isNotationValue(const NotationGroupInfo &info, const QString &value) {
            return std::any_of(info.Options.begin(), info.Options.end(),
                               [&](const auto &option) { return value == QLatin1String(option.first); });
        }

        /// "import Package2::*" (the members of a package, no filter): drawn nested, as a dashed package symbol in the
        /// importing package (table 3 "Package with imported package (nested notation)"). With 'recursive' also
        /// "import Package2::**" (else that is an «import» ** arrow, table 3 "Import (recursive)").
        bool isNestedPackageImport(const Diagram::SemanticModel &model, ElementId import, bool recursive) {
            const auto &element = model.element(import);
            const std::string &target = element.target();
            const auto endsWith = [&](const char *ending) {
                const std::string text(ending);
                return target.size() > text.size() && target.compare(target.size() - text.size(), text.size(), text) == 0;
            };
            const bool members = endsWith("::*") && !endsWith("::**");
            if (element.owner() == RootId || element.declaredKeyword() == "import all" || target.find('[') != std::string::npos ||
                !(members || (recursive && endsWith("::**"))))
                return false;
            const auto imported = importedElement(model, import);
            return !imported || model.element(*imported).kind() == ElementKind::Package;
        }

        /// "«import» *", "«private import» **", "«import»" (and a filter in brackets), as in the graphical BNF:
        /// '«' VisibilityIndicator? 'import' '»' ('*' | '**')?
        QString importLabel(const Diagram::SysMLElement &import) {
            const QString target = QString::fromStdString(import.target());
            QString visibility;
            for (const auto &prefix: import.prefixes()) {
                if (prefix == "private" || prefix == "public" || prefix == "protected")
                    visibility = QString::fromStdString(prefix) + QLatin1Char(' ');
            }
            QString label = QStringLiteral("«") + visibility + QStringLiteral("import»");
            const QString withoutFilter = target.section(QLatin1Char('['), 0, 0);
            if (withoutFilter.endsWith(QLatin1String("::**")))
                label += QStringLiteral(" **");
            else if (withoutFilter.endsWith(QLatin1String("::*")))
                label += QStringLiteral(" *");
            if (target.contains(QLatin1Char('[')))
                label += QStringLiteral(" [") + target.section(QLatin1Char('['), 1).chopped(1) + QLatin1Char(']');
            if (import.declaredKeyword() == "import all")
                label += QStringLiteral(" all");
            return label;
        }

        /// The definition kinds that can type a usage of this kind (for "defined by" arrows).
        bool typesUsage(ElementKind definition, ElementKind usage) {
            switch (usage) {
                case ElementKind::Attribute:
                    return definition == ElementKind::AttributeDefinition || definition == ElementKind::EnumerationDefinition;
                case ElementKind::PortUsage:
                    return definition == ElementKind::PortDefinition;
                case ElementKind::PartUsage:
                    return definition == ElementKind::PartDefinition || definition == ElementKind::ItemDefinition;
                case ElementKind::ItemUsage:
                    return definition == ElementKind::ItemDefinition || definition == ElementKind::PartDefinition;
                case ElementKind::OccurrenceUsage:
                    return definition == ElementKind::OccurrenceDefinition || definition == ElementKind::PartDefinition ||
                           definition == ElementKind::ItemDefinition;
                case ElementKind::ActionUsage:
                    return definition == ElementKind::ActionDefinition;
                case ElementKind::StateUsage:
                    return definition == ElementKind::StateDefinition;
                case ElementKind::RequirementUsage:
                    return definition == ElementKind::RequirementDefinition;
                case ElementKind::UseCaseUsage:
                    return definition == ElementKind::UseCaseDefinition;
                case ElementKind::VerificationUsage:
                    return definition == ElementKind::VerificationDefinition;
                case ElementKind::AnalysisUsage:
                    return definition == ElementKind::AnalysisDefinition;
                case ElementKind::ConstraintUsage:
                    return definition == ElementKind::ConstraintDefinition;
                case ElementKind::CalcUsage:
                    return definition == ElementKind::CalcDefinition;
                case ElementKind::ViewUsage:
                    return definition == ElementKind::ViewDefinition;
                case ElementKind::MetadataUsage:
                    return definition == ElementKind::MetadataDefinition;
                default:
                    return false;
            }
        }

        /// The inherited feature a redefinition names: a feature of the same name in a general of its owner
        /// (what the owner specializes, or its type), searched upwards through their generals.
        std::optional<ElementId> redefinedFeature(const Diagram::SemanticModel &model, ElementId feature,
                                                  const std::string &reference) {
            const auto segments = SysMLTextImporter::splitReference(reference);
            if (segments.empty())
                return std::nullopt;
            const ElementId owner = model.element(feature).owner();
            if (segments.size() > 1)
                return resolveReference(model, owner, reference);
            std::vector<ElementId> queue;
            std::set<ElementId> seen;
            const auto addGenerals = [&](ElementId of) {
                for (const auto &relationship: model.relationships()) {
                    if (relationship.Kind == RelationshipKind::Specialization && relationship.Source == of &&
                        seen.insert(relationship.Target).second)
                        queue.push_back(relationship.Target);
                }
                if (of != RootId && !model.element(of).typeName().empty()) {
                    if (const auto type = resolveReference(model, model.element(of).owner(), model.element(of).typeName());
                        type && seen.insert(*type).second)
                        queue.push_back(*type);
                }
            };
            if (owner == RootId)
                return std::nullopt;
            addGenerals(owner);
            for (std::size_t i = 0; i < queue.size(); ++i) {
                for (const ElementId child: model.children(queue[i])) {
                    if (child != feature && model.element(child).name() == segments.front())
                        return child;
                }
                addGenerals(queue[i]);
            }
            return std::nullopt;
        }

        /// An action of an action tree shown as a node of its own below its owner (composition, as for parts).
        bool isActionTreeNode(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            if (element.kind() != ElementKind::ActionUsage || element.owner() == RootId ||
                element.actionForm() != Diagram::ActionForm::Plain || element.declaredKeyword().has_value() ||
                element.isElseBranch())
                return false;
            const ElementKind owner = model.element(element.owner()).kind();
            return owner == ElementKind::ActionUsage || owner == ElementKind::ActionDefinition;
        }

        bool isNodeElement(const Diagram::SemanticModel &model, ElementId id);

        /// A note of its own: a comment, doc or rep (the doc of a requirement or case is its compartment "doc").
        bool isNoteElement(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            if (!isNoteStatement(element))
                return false;
            if (element.keyword() == "doc" && element.owner() != RootId) {
                const ElementKind owner = model.element(element.owner()).kind();
                if (Diagram::isRequirementKind(owner) || Diagram::isCaseKind(owner))
                    return false;
            }
            return true;
        }

        bool isNodeElement(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            if (isNoteElement(model, id))
                return true;
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
            // an item, attribute or port on package level: a node («item» marketSurvey); in parts an entry
            if ((element.kind() == ElementKind::ItemUsage || element.kind() == ElementKind::Attribute ||
                 element.kind() == ElementKind::PortUsage) &&
                (element.owner() == RootId || model.element(element.owner()).kind() == ElementKind::Package))
                return true;
            if ((element.kind() == ElementKind::ActionUsage || element.kind() == ElementKind::StateUsage) &&
                (element.owner() == RootId || model.element(element.owner()).kind() == ElementKind::Package))
                return true;
            if (isActionTreeNode(model, id))
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

        /// "engine : Engine[4]", "smallEng : SmallEngine :>> eng", "cyl[4] :>> cyl" (named like the redefined feature)
        QString usageText(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            const QString redefinitions = redefinitionText(element);
            QString text = QString::fromStdString(element.name());
            // no name of its own: as in the tables of the specification ":>> attribute5", "redefines cylinders [4]"
            const bool implicitName = element.nameFromRedefinition() && !element.redefinitions().empty();
            if (implicitName)
                text = redefinitions;
            if (!element.typeName().empty())
                text += QStringLiteral(" : ") + QString::fromStdString(element.typeName());
            // as in the tables of the specification: "part1 : Part1 [0..*]", "attribute1 : AttributeDef1 [1..*]"
            if (element.multiplicity().has_value())
                text += QLatin1Char(' ') + QString::fromStdString(element.multiplicity()->toString());
            if (!element.multiplicityQualifiers().empty()) // "[1..*] ordered nonunique"
                text += QLatin1Char(' ') + QString::fromStdString(element.multiplicityQualifiers());
            if (!element.referencedFeature().empty()) // "references action3" (UsageDeclaration)
                text += QLatin1Char(' ') + QString::fromStdString(element.referencesKeyword()) + QLatin1Char(' ') +
                        QString::fromStdString(element.referencedFeature());
            if (!redefinitions.isEmpty() && !implicitName)
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

        /// "pipe : Hose", ": Fuel" (flow), ": FuelInterface" (interface), "=" (binding) (empty for plain relationships).
        QString edgeLabel(const Diagram::Relationship &relationship) {
            QString label = QString::fromStdString(relationship.Name);
            if (!relationship.Definition.empty())
                label += QStringLiteral(" : ") + QString::fromStdString(relationship.Definition);
            if (!relationship.TypeName.empty())
                // flow-label: UsageDeclaration ('of' FlowPayloadFeatureMember)?
                label += (relationship.Kind == RelationshipKind::Flow ? QStringLiteral(" of ") : QStringLiteral(" : ")) +
                         QString::fromStdString(relationship.TypeName);
            label = label.trimmed();
            if (relationship.Kind == RelationshipKind::Binding)
                label = QStringLiteral("=");
            if (relationship.Kind == RelationshipKind::Satisfy)
                label = QStringLiteral("«satisfy»");
            if (relationship.Kind == RelationshipKind::Verify)
                label = QStringLiteral("«verify»");
            if (relationship.Kind == RelationshipKind::Include)
                label = QStringLiteral("«include»");
            if (relationship.Kind == RelationshipKind::Frame)
                label = QStringLiteral("«frame»");
            if (relationship.Kind == RelationshipKind::Exhibit || relationship.Kind == RelationshipKind::Assert ||
                relationship.Kind == RelationshipKind::Assume || relationship.Kind == RelationshipKind::Require)
                label = QStringLiteral("«") + QString::fromLatin1(Diagram::sourceKeyword(relationship.Kind)) + QStringLiteral("»");
            if (relationship.Kind == RelationshipKind::Allocation)
                label = (QStringLiteral("«allocate» ") + label).trimmed();
            if (relationship.Kind == RelationshipKind::Message) {
                label = QString::fromStdString(relationship.Name);
                if (!relationship.TypeName.empty())
                    label += (label.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("of ") +
                             QString::fromStdString(relationship.TypeName);
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
                   tool == DiagramTypes::ToolFilter || tool == DiagramTypes::ToolEnumValue;
        }

        /// Keyword shown above the name: «constraint def», «function», «assert constraint», ...
        /// A result among the parameters: "return f = m * a", inherited "^return f" (slide 80).
        QString returnText(const QString &text) {
            return text.startsWith(QLatin1Char('^')) ? QStringLiteral("^return ") + text.mid(1) : QStringLiteral("return ") + text;
        }

        /// The last segment of a reference: "Vehicle::mass" -> "mass", "a.b" -> "b".
        std::string lastSegment(const std::string &reference) {
            const auto segments = SysMLTextImporter::splitReference(reference);
            return segments.empty() ? reference : segments.back();
        }

        /// What an element inherits from: its definition, what it specializes / subsets and what it redefines,
        /// and their generals in turn (nearest first).
        std::vector<ElementId> generalsOf(const Diagram::SemanticModel &model, ElementId id) {
            std::vector<ElementId> result;
            std::set<ElementId> seen{id};
            for (std::size_t next = 0;; ++next) {
                const ElementId current = next == 0 ? id : result[next - 1];
                const auto &element = model.element(current);
                std::vector<ElementId> direct;
                if (!element.typeName().empty()) {
                    std::string type = element.typeName();
                    if (!type.empty() && type.front() == '~')
                        type.erase(0, 1);
                    if (const auto resolved = resolveReference(model, element.owner(), type))
                        direct.push_back(*resolved);
                }
                for (const auto &reference: element.externalSpecializations()) {
                    if (const auto resolved = resolveReference(model, element.owner(), reference))
                        direct.push_back(*resolved);
                }
                for (const auto &relationship: model.relationships()) {
                    if (relationship.Kind == RelationshipKind::Specialization && relationship.Source == current)
                        direct.push_back(relationship.Target);
                }
                for (const auto &reference: element.redefinitions()) {
                    if (const auto resolved = redefinedFeature(model, current, reference))
                        direct.push_back(*resolved);
                }
                for (const ElementId general: direct) {
                    if (seen.insert(general).second && !model.isAncestor(general, id))
                        result.push_back(general);
                }
                if (next >= result.size())
                    break;
            }
            return result;
        }

        /// The features shown with "^": owned by a general, neither redefined nor named again here.
        std::vector<ElementId> inheritedFeatures(const Diagram::SemanticModel &model, ElementId id) {
            std::set<std::string> names;
            const auto addNames = [&](ElementId feature) {
                const auto &element = model.element(feature);
                if (!element.name().empty())
                    names.insert(element.name());
                for (const auto &reference: element.redefinitions())
                    names.insert(lastSegment(reference));
            };
            for (const ElementId child: model.children(id))
                addNames(child);
            std::vector<ElementId> result;
            for (const ElementId general: generalsOf(model, id)) {
                for (const ElementId child: model.children(general)) {
                    const auto &element = model.element(child);
                    switch (element.kind()) {
                        case ElementKind::Attribute:
                        case ElementKind::PortUsage:
                        case ElementKind::PartUsage:
                        case ElementKind::ItemUsage:
                        case ElementKind::OccurrenceUsage:
                        case ElementKind::ActionUsage:
                        case ElementKind::ActionPerformance:
                        case ElementKind::StateUsage:
                        case ElementKind::ConstraintUsage:
                        case ElementKind::EnumerationLiteral:
                            break;
                        default:
                            continue;
                    }
                    if (element.kind() == ElementKind::ConstraintUsage && element.declaredKeyword().value_or("") != "assert constraint" &&
                        element.declaredKeyword().value_or("") != "require constraint" &&
                        element.declaredKeyword().value_or("") != "assume constraint")
                        continue;
                    // a feature named by what it redefines is shown under that name
                    const std::string name = element.nameFromRedefinition() && !element.redefinitions().empty()
                                             ? lastSegment(element.redefinitions().front()) : element.name();
                    if (!name.empty() && names.count(name))
                        continue; // redefined or named again here (or by a nearer general)
                    if (model.isAncestor(child, id) || child == id)
                        continue;
                    result.push_back(child);
                    addNames(child);
                }
            }
            return result;
        }

        /// Direction of a port for the symbol in its square (graphical BNF: pdh / pdv): "in", "out", "inout" or empty,
        /// from the directed features of the port and of its definition (reversed for a conjugated port "~P").
        QString portDirection(const Diagram::SemanticModel &model, ElementId port) {
            bool in = false;
            bool out = false;
            const auto collect = [&](ElementId owner) {
                for (const ElementId child: model.children(owner)) {
                    const std::string direction = directionOf(model.element(child));
                    in = in || direction == "in" || direction == "inout";
                    out = out || direction == "out" || direction == "inout";
                }
            };
            collect(port);
            for (const ElementId general: generalsOf(model, port))
                collect(general);
            const std::string &type = model.element(port).typeName();
            if (!type.empty() && type.front() == '~')
                std::swap(in, out);
            return in && out ? QStringLiteral("inout") : in ? QStringLiteral("in") : out ? QStringLiteral("out") : QString();
        }

        QString keywordOf(const Diagram::SysMLElement &element) {
            std::string keyword = Diagram::defaultKeyword(element.kind());
            if (element.declaredKeyword().has_value() && !element.declaredKeyword()->empty())
                keyword = *element.declaredKeyword();
            return QStringLiteral("«") + QString::fromStdString(keyword) + QStringLiteral("»") +
                   (element.isParallel() ? QStringLiteral(" «parallel»") : QString());
        }
    }

    DiagramController::DiagramController(Diagram::SemanticModel &model, QObject *parent, bool interconnection) :
            QObject(parent),
            Model(model),
            Nodes(new DiagramNodeModel(this)),
            Edges(new DiagramEdgeModel(this)),
            Interconnection(interconnection) {
        ListenerToken = Model.addListener([this](const Diagram::ModelChange &) { scheduleSync(); });
        connect(Nodes, &DiagramNodeModel::geometryChanged, this, &DiagramController::updateCanvasSize);
        const auto bumpRevision = [this] {
            ++GeometryRevision;
            emit geometryRevisionChanged();
        };
        connect(Nodes, &DiagramNodeModel::geometryChanged, this, bumpRevision);
        connect(Nodes, &QAbstractItemModel::dataChanged, this, bumpRevision);
        connect(Nodes, &QAbstractItemModel::modelReset, this, bumpRevision);
        if (Interconnection)
            layoutAll();
        else
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

    void DiagramController::setViewId(const QString &id) {
        std::optional<ElementId> value = id.isEmpty() ? std::nullopt : toId(id);
        if (value && Model.element(*value).kind() != ElementKind::ViewUsage)
            value.reset();
        if (value == ViewElement)
            return;
        ViewElement = value;
        AutoLayout = true;
        emit viewChanged();
        syncNow();
        layoutAll();
    }

    QString DiagramController::viewTitle() const {
        if (!ViewElement || !Model.contains(*ViewElement))
            return {};
        const auto &view = Model.element(*ViewElement);
        QString title = QStringLiteral("«view» ") + QString::fromStdString(view.name());
        if (!view.typeName().empty())
            title += QStringLiteral(" : ") + QString::fromStdString(view.typeName());
        return title;
    }

    QStringList DiagramController::viewInfo() const {
        QStringList lines;
        if (!ViewElement || !Model.contains(*ViewElement))
            return lines;
        for (const ElementId child: Model.children(*ViewElement)) {
            const auto &member = Model.element(child);
            if (member.kind() == ElementKind::Expose || member.kind() == ElementKind::Filter ||
                member.kind() == ElementKind::Render)
                lines.append(QString::fromStdString(Diagram::defaultKeyword(member.kind())) + QLatin1Char(' ') +
                             QString::fromStdString(member.target()));
        }
        return lines;
    }

    std::set<ElementId> DiagramController::exposedElements() const {
        std::set<ElementId> exposed;
        if (!ViewElement || !Model.contains(*ViewElement))
            return exposed;
        std::function<void(ElementId)> subtree = [&](ElementId id) {
            exposed.insert(id);
            for (const ElementId child: Model.children(id))
                subtree(child);
        };
        // "filter @Safety;": only what carries that metadata (directly, "#Safety", or "@Safety;" inside)
        std::vector<std::string> metadata;
        for (const ElementId child: Model.children(*ViewElement)) {
            const auto &member = Model.element(child);
            if (member.kind() == ElementKind::Filter && !member.target().empty() && member.target().front() == '@')
                metadata.push_back(member.target().substr(1));
        }
        for (const ElementId child: Model.children(*ViewElement)) {
            const auto &expose = Model.element(child);
            if (expose.kind() != ElementKind::Expose)
                continue;
            const auto element = importedElement(Model, child);
            if (!element)
                continue;
            std::string target = expose.target().substr(0, expose.target().find('['));
            const bool members = target.size() > 3 && target.compare(target.size() - 3, 3, "::*") == 0;
            const bool recursive = target.size() > 4 && target.compare(target.size() - 4, 4, "::**") == 0;
            if (members || recursive) {
                for (const ElementId member: Model.children(*element))
                    subtree(member);
                if (recursive)
                    exposed.insert(*element);
            } else {
                subtree(*element);
            }
        }
        if (metadata.empty())
            return exposed;
        const auto carries = [&](ElementId id) {
            const auto &element = Model.element(id);
            for (const auto &prefix: element.prefixes()) {
                for (const auto &name: metadata)
                    if (prefix == "#" + name)
                        return true;
            }
            for (const ElementId child: Model.children(id)) {
                const auto &annotation = Model.element(child);
                if (annotation.kind() != ElementKind::MetadataUsage)
                    continue;
                for (const auto &name: metadata) {
                    const std::string type = annotation.typeName().empty() ? annotation.name() : annotation.typeName();
                    if (type == name || (type.size() > name.size() &&
                                         type.compare(type.size() - name.size() - 2, name.size() + 2, "::" + name) == 0))
                        return true;
                }
            }
            return false;
        };
        std::set<ElementId> filtered;
        for (const ElementId id: exposed) {
            if (carries(id))
                filtered.insert(id);
        }
        return filtered;
    }

    // --- grid and browser view -------------------------------------------------------------------------

    namespace {
        /// Rows and columns of a relationship matrix: what the relationship goes to (rows) and from (columns).
        struct MatrixAxes {
            RelationshipKind Kind;
            bool (*Row)(ElementKind);
            bool (*Column)(ElementKind);
            bool ColumnIsSource; ///< the relationship goes from the column element to the row element
        };

        std::optional<MatrixAxes> matrixAxes(int kind) {
            switch (kind) {
                case DiagramTypes::Satisfy: // requirements satisfied by parts / usages
                    return MatrixAxes{RelationshipKind::Satisfy, [](ElementKind k) { return Diagram::isRequirementKind(k); },
                                      [](ElementKind k) { return k == ElementKind::PartUsage || k == ElementKind::PartDefinition; },
                                      true};
                case DiagramTypes::Verify: // requirements verified by verification cases
                    return MatrixAxes{RelationshipKind::Verify, [](ElementKind k) { return Diagram::isRequirementKind(k); },
                                      [](ElementKind k) {
                                          return k == ElementKind::VerificationUsage || k == ElementKind::VerificationDefinition;
                                      },
                                      true};
                case DiagramTypes::Allocation: // actions allocated to parts
                    return MatrixAxes{RelationshipKind::Allocation, [](ElementKind k) { return k == ElementKind::PartUsage; },
                                      [](ElementKind k) {
                                          return k == ElementKind::ActionUsage || k == ElementKind::ActionPerformance;
                                      },
                                      true};
                case DiagramTypes::Connection: // parts connected with parts
                    return MatrixAxes{RelationshipKind::Connection, [](ElementKind k) { return k == ElementKind::PartUsage; },
                                      [](ElementKind k) { return k == ElementKind::PartUsage; }, true};
                case DiagramTypes::Dependency: // dependencies between definitions and packages
                    return MatrixAxes{RelationshipKind::Dependency,
                                      [](ElementKind k) { return k == ElementKind::Package || Diagram::isDefinitionKind(k); },
                                      [](ElementKind k) { return k == ElementKind::Package || Diagram::isDefinitionKind(k); },
                                      true};
                default:
                    return std::nullopt;
            }
        }
    }

    QVariantList DiagramController::matrixKinds() const {
        return {QVariantMap{{QStringLiteral("kind"), int(DiagramTypes::Satisfy)}, {QStringLiteral("title"), tr("Satisfy: requirements × parts")}},
                QVariantMap{{QStringLiteral("kind"), int(DiagramTypes::Verify)}, {QStringLiteral("title"), tr("Verify: requirements × verifications")}},
                QVariantMap{{QStringLiteral("kind"), int(DiagramTypes::Allocation)}, {QStringLiteral("title"), tr("Allocate: parts × actions")}},
                QVariantMap{{QStringLiteral("kind"), int(DiagramTypes::Connection)}, {QStringLiteral("title"), tr("Connect: parts × parts")}},
                QVariantMap{{QStringLiteral("kind"), int(DiagramTypes::Dependency)}, {QStringLiteral("title"), tr("Dependency: definitions × definitions")}}};
    }

    QVariantMap DiagramController::relationshipMatrix(int kind) const {
        const auto axes = matrixAxes(kind);
        if (!axes)
            return {};
        std::vector<ElementId> rows;
        std::vector<ElementId> columns;
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                if (!element.name().empty() && element.kind() != ElementKind::Textual) {
                    if (axes->Row(element.kind()))
                        rows.push_back(child);
                    if (axes->Column(element.kind()))
                        columns.push_back(child);
                }
                visit(child);
            }
        };
        visit(RootId);
        const auto title = [&](ElementId id) {
            return QString::fromStdString(Model.qualifiedName(id));
        };
        QVariantList rowList;
        for (const ElementId id: rows)
            rowList.append(QVariantMap{{QStringLiteral("id"), QString::number(id)}, {QStringLiteral("title"), title(id)}});
        QVariantList columnList;
        for (const ElementId id: columns)
            columnList.append(QVariantMap{{QStringLiteral("id"), QString::number(id)}, {QStringLiteral("title"), title(id)}});
        QVariantList cells;
        for (const ElementId row: rows) {
            QVariantList line;
            for (const ElementId column: columns) {
                const ElementId source = axes->ColumnIsSource ? column : row;
                const ElementId target = axes->ColumnIsSource ? row : column;
                const bool related = std::any_of(Model.relationships().begin(), Model.relationships().end(),
                                                 [&](const Diagram::Relationship &relationship) {
                    const bool forward = relationship.Source == source && relationship.Target == target;
                    const bool backward = axes->Kind == RelationshipKind::Connection && relationship.Source == target &&
                                          relationship.Target == source;
                    return relationship.Kind == axes->Kind && (forward || backward);
                });
                line.append(related);
            }
            cells.append(QVariant(line));
        }
        return {{QStringLiteral("rows"), rowList}, {QStringLiteral("columns"), columnList}, {QStringLiteral("cells"), cells}};
    }

    QString DiagramController::toggleMatrixCell(int kind, const QString &row, const QString &column) {
        const auto axes = matrixAxes(kind);
        const auto rowId = toId(row);
        const auto columnId = toId(column);
        if (!axes || !rowId || !columnId)
            return tr("Unknown element");
        const ElementId source = axes->ColumnIsSource ? *columnId : *rowId;
        const ElementId target = axes->ColumnIsSource ? *rowId : *columnId;
        for (const auto &relationship: Model.relationships()) {
            const bool forward = relationship.Source == source && relationship.Target == target;
            const bool backward = axes->Kind == RelationshipKind::Connection && relationship.Source == target &&
                                  relationship.Target == source;
            if (relationship.Kind == axes->Kind && (forward || backward)) {
                const auto id = relationship.Id;
                runModelChange([&] { Model.removeRelationship(id); });
                return {};
            }
        }
        if (const auto rule = RelationshipRules::canRelate(Model, axes->Kind, source, target); !rule)
            return QString::fromStdString(rule.Reason);
        return runModelChange([&] { Model.addRelationship(axes->Kind, source, target); }) ? QString()
                                                                                          : tr("Not possible here");
    }

    QVariantList DiagramController::browserTree() const {
        QVariantList result;
        std::function<void(ElementId, int)> visit = [&](ElementId owner, int depth) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                QString name = QString::fromStdString(element.name());
                QString keyword = QString::fromStdString(Diagram::defaultKeyword(element.kind()));
                if (element.kind() == ElementKind::Textual) {
                    keyword = QString::fromStdString(element.keyword());
                    if (name.isEmpty())
                        name = QString::fromStdString(SysMLTextImporter::oneLine(element.text())).left(60);
                } else if (element.declaredKeyword().has_value() && !element.declaredKeyword()->empty()) {
                    keyword = QString::fromStdString(*element.declaredKeyword());
                }
                if (!element.typeName().empty())
                    name += QStringLiteral(" : ") + QString::fromStdString(element.typeName());
                if (!element.target().empty() && name.isEmpty())
                    name = QString::fromStdString(element.target());
                result.append(QVariantMap{{QStringLiteral("id"), QString::number(child)}, {QStringLiteral("depth"), depth},
                                          {QStringLiteral("keyword"), keyword}, {QStringLiteral("name"), name},
                                          {QStringLiteral("children"), int(Model.children(child).size())}});
                visit(child, depth + 1);
            }
        };
        visit(RootId, 0);
        return result;
    }

    void DiagramController::setAllCompartments(bool enabled) {
        if (AllCompartments == enabled)
            return;
        AllCompartments = enabled;
        Measured.clear(); // the nodes change their size
        emit allCompartmentsChanged();
        syncNow();
    }

    // --- notations -------------------------------------------------------------------------------------

    namespace {
        /// A member listed in the compartment 'members' of a package (graphical BNF: DefinitionElement | UsageElement).
        /// What the compartment 'members' of a package lists: its definitions and usages, and its packages
        /// ("package Package2", table 3 "Package with owned package").
        bool isListedMemberKind(ElementKind kind);

        bool isPackageMemberKind(ElementKind kind) {
            return kind != ElementKind::Package && kind != ElementKind::Import && kind != ElementKind::Alias &&
                   kind != ElementKind::Textual && (Diagram::isDefinitionKind(kind) || Diagram::isUsageKind(kind));
        }
    }

    namespace {
        bool isListedMemberKind(ElementKind kind) {
            return kind == ElementKind::Package || isPackageMemberKind(kind);
        }
    }

    bool DiagramController::notationApplies(ElementId id, const QString &group) const {
        if (Interconnection || !Model.contains(id))
            return false;
        const auto &element = Model.element(id);
        const ElementKind kind = element.kind();
        const ElementId owner = element.owner();
        const auto hasChild = [&](auto predicate) {
            const auto &children = Model.children(id);
            return std::any_of(children.begin(), children.end(), predicate);
        };
        if (group == QLatin1String("packages"))
            return kind == ElementKind::Package && owner != RootId && Model.element(owner).kind() == ElementKind::Package;
        if (group == QLatin1String("imports"))
            return kind == ElementKind::Import && owner != RootId;
        if (group == QLatin1String("qualified"))
            // in a package, or a part of a part ("part1::part2::part3 : PartDef3", table 4)
            return owner != RootId && ((Model.element(owner).kind() == ElementKind::Package && isPackageMemberKind(kind)) ||
                                       (composes(Model.element(owner).kind()) &&
                                        (kind == ElementKind::PartUsage || kind == ElementKind::OccurrenceUsage)));
        if (group == QLatin1String("specialization"))
            return !element.redefinitions().empty() ||
                   std::any_of(Model.relationships().begin(), Model.relationships().end(), [&](const Diagram::Relationship &r) {
                       return r.Kind == RelationshipKind::Specialization && r.Source == id;
                   });
        if (group == QLatin1String("members"))
            return kind == ElementKind::Package &&
                   hasChild([&](ElementId child) { return isListedMemberKind(Model.element(child).kind()); });
        if (group == QLatin1String("aliases"))
            return kind == ElementKind::Alias;
        if (group == QLatin1String("features"))
            return composes(kind) && (hasChild([&](ElementId child) {
                       const ElementKind childKind = Model.element(child).kind();
                       return childKind == ElementKind::PartUsage || childKind == ElementKind::OccurrenceUsage;
                   }) || (notationOf(id, QStringLiteral("inherited")) == QLatin1String("nodes") &&
                          notationApplies(id, QStringLiteral("inherited")))); // ^part1 drawn as node
        if (group == QLatin1String("inherited")) {
            if (!composes(kind))
                return false;
            const auto inherited = inheritedFeatures(Model, id);
            return std::any_of(inherited.begin(), inherited.end(), [&](ElementId feature) {
                return Model.element(feature).kind() == ElementKind::PartUsage && !Model.element(feature).name().empty();
            });
        }
        if (group == QLatin1String("ports"))
            return composes(kind) && hasChild([&](ElementId child) { return Model.element(child).kind() == ElementKind::PortUsage; });
        if (group == QLatin1String("typing"))
            return Diagram::isUsageKind(kind) && !element.typeName().empty() && !portAnchor(Model, id);
        if (group == QLatin1String("docs"))
            return hasChild([&](ElementId child) { return isDocInCompartment(child); });
        if (group == QLatin1String("variants"))
            // only where a variant can be a node of its own; others are text in any case
            return isVariation(element) && hasChild([&](ElementId child) {
                       return isVariantElement(Model.element(child)) && isNodeElement(Model, child);
                   });
        if (group == QLatin1String("relationships"))
            return kind != ElementKind::Package && kind != ElementKind::PortUsage && kind != ElementKind::Textual &&
                   kind != ElementKind::Alias && !portAnchor(Model, id) && !relationshipEntries(id).isEmpty();
        return false;
    }

    QString DiagramController::defaultNotation(ElementId id, const QString &group) const {
        if (group == QLatin1String("imports"))
            // table 3: "P::*" nested (also a library the model does not hold), else an «import» arrow
            return isNestedPackageImport(Model, id, false) || !importedElement(Model, id) ? QStringLiteral("nested")
                                                                                         : QStringLiteral("line");
        if (group == QLatin1String("features") || group == QLatin1String("specialization"))
            return QStringLiteral("line");
        if (group == QLatin1String("ports"))
            return QStringLiteral("border");
        if (group == QLatin1String("inherited"))
            return QStringLiteral("compartment");
        // the variants of a variation in its compartment, as in the tables ("Variants Compartment",
        // "Variant Parts Compartment"); nodes with the variant membership line on request
        if (group == QLatin1String("variants"))
            return QStringLiteral("compartment");
        if (group == QLatin1String("relationships"))
            return QStringLiteral("line");
        if (group == QLatin1String("typing"))
            return QStringLiteral("name");
        if (group == QLatin1String("docs"))
            return AllCompartments ? QStringLiteral("compartment") : QStringLiteral("note");
        if (group == QLatin1String("aliases")) {
            // an alias of a definition / usage: in its name compartment (table 4); of a package: its symbol (table 3)
            const auto &alias = Model.element(id);
            const auto aliased = resolveReference(Model, alias.owner(), alias.target());
            return aliased && Model.element(*aliased).kind() != ElementKind::Package ? QStringLiteral("name")
                                                                                       : QStringLiteral("nested");
        }
        return QStringLiteral("nested"); // packages
    }

    bool DiagramController::notationAllowed(ElementId subject, const QString &group, const QString &value) const {
        if (group == QLatin1String("aliases")) {
            // «alias» in the name compartment exists for definitions and usages (table 4), the alias member symbol for
            // packages (table 3); the line with the alias name for both (table 3, membership)
            const auto &alias = Model.element(subject);
            const auto aliased = resolveReference(Model, alias.owner(), alias.target());
            const bool package = aliased && Model.element(*aliased).kind() == ElementKind::Package;
            if (value == QLatin1String("name"))
                return aliased && !package;
            if (value == QLatin1String("nested"))
                return !aliased || package;
        }
        return true;
    }

    QString DiagramController::notationOf(ElementId subject, const QString &group) const {
        if (!notationApplies(subject, group))
            return defaultNotation(subject, group);
        QString value = defaultNotation(subject, group);
        if (const auto mode = NotationModes.find(group);
            mode != NotationModes.end() && mode->second != QLatin1String("individual")) {
            value = mode->second;
        } else if (const auto element = Notations.find(subject); element != Notations.end()) {
            if (const auto chosen = element->second.find(group); chosen != element->second.end())
                value = chosen->second;
        }
        // what the specification does not define for this element: its default
        return notationAllowed(subject, group, value) ? value : defaultNotation(subject, group);
    }

    QVariantMap DiagramController::notationChoice(ElementId id, const QString &group) const {
        const auto *info = notationGroup(group);
        QVariantList options;
        for (const auto &[value, label]: info->Options) {
            if (notationAllowed(id, group, QString::fromLatin1(value)))
                options.append(QVariantMap{{QStringLiteral("value"), QString::fromLatin1(value)},
                                           {QStringLiteral("label"), tr(label)}});
        }
        const auto mode = NotationModes.find(group);
        return {{QStringLiteral("subject"), QString::number(id)},
                {QStringLiteral("group"), group},
                {QStringLiteral("label"), tr(info->Label)},
                {QStringLiteral("value"), notationOf(id, group)},
                {QStringLiteral("mode"), mode == NotationModes.end() ? QStringLiteral("individual") : mode->second},
                {QStringLiteral("options"), options}};
    }

    QVariantList DiagramController::notationGroups() const {
        QVariantList groups;
        for (const auto &info: notationTable()) {
            QVariantList options;
            for (const auto &[value, label]: info.Options)
                options.append(QVariantMap{{QStringLiteral("value"), QString::fromLatin1(value)},
                                           {QStringLiteral("label"), tr(label)}});
            const auto mode = NotationModes.find(QString::fromLatin1(info.Id));
            groups.append(QVariantMap{{QStringLiteral("group"), QString::fromLatin1(info.Id)},
                                      {QStringLiteral("label"), tr(info.Label)},
                                      {QStringLiteral("mode"), mode == NotationModes.end() ? QStringLiteral("individual")
                                                                                          : mode->second},
                                      {QStringLiteral("options"), options}});
        }
        return groups;
    }

    void DiagramController::notationsEdited() {
        ++NotationRevision;
        Measured.clear(); // nodes change their size (compartments come and go)
        emit notationsChanged();
        syncNow();
        if (AutoLayout)
            layoutAll();
        else
            repairContainment();
        emit layoutEdited();
    }

    void DiagramController::repairContainment() {
        // after a change of notation (packages back in their package, members back as nodes, ...): what is drawn in a
        // package lies inside it, the outer ones first
        std::vector<std::pair<int, ElementId>> order;
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind != DiagramTypes::PortNode && Model.contains(node.Id))
                order.emplace_back(node.Depth, node.Id);
        }
        std::sort(order.begin(), order.end());
        for (const auto &[depth, id]: order) {
            const ElementId owner = Model.element(id).owner();
            if (owner == RootId || detachedFromOwner(id))
                continue;
            const auto *ownerNode = Nodes->find(owner);
            const auto *node = Nodes->find(id);
            // a package, or a part drawn around its parts (table 9 "Part with graphical compartment")
            const bool container = isFrame(owner) || (ownerNode && ownerNode->Container && nestedFeatures(owner));
            if (!container || !ownerNode || !node)
                continue;
            const qreal header = ownerNode->Kind == DiagramTypes::PackageNode ? 24 : estimatedSize(*ownerNode).height();
            if (!ownerNode->Rect.adjusted(0, header, 0, 0).contains(node->Rect))
                fitIntoFrame(id, owner);
        }
    }

    void DiagramController::setNotationMode(const QString &group, const QString &mode) {
        const auto *info = notationGroup(group);
        if (!info || (mode != QLatin1String("individual") && !isNotationValue(*info, mode)))
            return;
        const auto current = NotationModes.find(group);
        const QString old = current == NotationModes.end() ? QStringLiteral("individual") : current->second;
        if (old == mode)
            return;
        if (mode == QLatin1String("individual"))
            NotationModes.erase(group);
        else
            NotationModes[group] = mode;
        notationsEdited();
    }

    QVariantList DiagramController::notationChoices(const QString &id) const {
        const auto element = toId(id);
        QVariantList choices;
        if (!element)
            return choices;
        for (const auto &info: notationTable()) {
            const QString group = QString::fromLatin1(info.Id);
            if (notationApplies(*element, group))
                choices.append(notationChoice(*element, group));
        }
        // the aliases shown at this element («alias» in its name) or as lines to it: to be changed here as well
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &candidate = Model.element(child);
                if (candidate.kind() == ElementKind::Alias && child != *element && !Nodes->find(child)) {
                    // named here, or owned by this package (and drawn elsewhere)
                    const auto aliased = resolveReference(Model, candidate.owner(), candidate.target());
                    if ((aliased && *aliased == *element) || candidate.owner() == *element) {
                        QVariantMap choice = notationChoice(child, QStringLiteral("aliases"));
                        choice[QStringLiteral("label")] = choice.value(QStringLiteral("label")).toString() + QStringLiteral(": ") +
                                                          QString::fromStdString(candidate.name());
                        choices.append(choice);
                    }
                }
                visit(child);
            }
        };
        visit(RootId);
        return choices;
    }

    void DiagramController::setNotation(const QString &id, const QString &group, const QString &value) {
        const auto element = toId(id);
        const auto *info = notationGroup(group);
        if (!element || !info || !isNotationValue(*info, value) || !Model.contains(*element))
            return;
        auto &choices = Notations[*element];
        if (const auto existing = choices.find(group); existing != choices.end() && existing->second == value)
            return;
        choices[group] = value;
        // a package that leaves its package (line): beside it, not on top of it
        if (group == QLatin1String("packages") && value == QLatin1String("line") && !AutoLayout) {
            syncNow();
            const auto *ownerNode = Nodes->find(Model.element(*element).owner());
            const auto *node = Nodes->find(*element);
            if (ownerNode && node && ownerNode->Rect.intersects(node->Rect)) {
                const QPointF delta = QPointF(ownerNode->Rect.right() + 60, ownerNode->Rect.top()) - node->Rect.topLeft();
                if (const int row = Nodes->indexOf(*element); row >= 0)
                    Nodes->setRect(row, node->Rect.translated(delta));
                moveFrameContent(*element, delta);
            }
        }
        notationsEdited();
    }

    QString DiagramController::notationSubject(const QString &edgeKey) const {
        for (const QLatin1String prefix: {QLatin1String("i:"), QLatin1String("al:"), QLatin1String("t:"), QLatin1String("c:")}) {
            if (!edgeKey.startsWith(prefix))
                continue;
            bool ok = false;
            const ElementId id = edgeKey.mid(prefix.size()).toULongLong(&ok);
            if (!ok || !Model.contains(id))
                return {};
            if (prefix == QLatin1String("c:"))
                return notationApplies(id, QStringLiteral("packages")) ? QString::number(id)
                       : notationApplies(Model.element(id).owner(), QStringLiteral("features")) ? QString::number(Model.element(id).owner())
                                                                                              : QString();
            return QString::number(id);
        }
        return {};
    }

    QStringList DiagramController::aliasEntries(const QString &id) const {
        // the same order as syncNow writes them into the subtitle
        const auto element = toId(id);
        QStringList entries;
        if (!element)
            return entries;
        const auto *node = Nodes->find(*element);
        if (!node || node->Subtitle.isEmpty() || node->Folder)
            return entries;
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &candidate = Model.element(child);
                if (candidate.kind() == ElementKind::Alias && !Nodes->find(child) &&
                    !(notationOf(child, QStringLiteral("aliases")) == QLatin1String("line") && Nodes->find(candidate.owner()))) {
                    const auto aliased = resolveReference(Model, candidate.owner(), candidate.target());
                    if (aliased && *aliased == *element)
                        entries.append(QString::number(child));
                }
                visit(child);
            }
        };
        visit(RootId);
        return entries;
    }

    QVariantList DiagramController::creationChoices(const QString &id) const {
        const auto element = toId(id);
        QVariantList choices;
        if (!element || !Model.contains(*element))
            return choices;
        const auto individual = [&](const QString &group) {
            const auto mode = NotationModes.find(group);
            return mode == NotationModes.end();
        };
        const auto &created = Model.element(*element);
        for (const QString &group: {QStringLiteral("packages"), QStringLiteral("imports"), QStringLiteral("aliases")}) {
            if (individual(group) && notationApplies(*element, group))
                choices.append(notationChoice(*element, group));
        }
        // a part / port / doc added to an element: how that element shows them, asked once
        const ElementId owner = created.owner();
        const auto askOwner = [&](const QString &group) {
            if (owner == RootId || !individual(group) || !notationApplies(owner, group))
                return;
            if (const auto existing = Notations.find(owner); existing != Notations.end() && existing->second.count(group) > 0)
                return;
            choices.append(notationChoice(owner, group));
        };
        if (created.kind() == ElementKind::PartUsage || created.kind() == ElementKind::OccurrenceUsage)
            askOwner(QStringLiteral("features"));
        if (created.kind() == ElementKind::PortUsage)
            askOwner(QStringLiteral("ports"));
        if (isDocInCompartment(*element))
            askOwner(QStringLiteral("docs"));
        return choices;
    }

    bool DiagramController::detachedFromOwner(ElementId id) const {
        return (notationApplies(id, QStringLiteral("packages")) &&
                notationOf(id, QStringLiteral("packages")) == QLatin1String("line")) ||
               (notationApplies(id, QStringLiteral("qualified")) &&
                notationOf(id, QStringLiteral("qualified")) == QLatin1String("qualified"));
    }

    bool DiagramController::emptiedByQualifiedNames(ElementId id) const {
        // a package all of whose members are drawn alone with their qualified names (table 4): no frame of its own
        if (!Model.contains(id) || Model.element(id).kind() != ElementKind::Package || Model.children(id).empty())
            return false;
        const auto &children = Model.children(id);
        return std::all_of(children.begin(), children.end(), [&](ElementId child) {
            return (notationApplies(child, QStringLiteral("qualified")) &&
                    notationOf(child, QStringLiteral("qualified")) == QLatin1String("qualified")) ||
                   emptiedByQualifiedNames(child);
        });
    }

    bool DiagramController::nestedFeatures(ElementId id) const {
        return !Interconnection && notationApplies(id, QStringLiteral("features")) &&
               notationOf(id, QStringLiteral("features")) == QLatin1String("nested");
    }

    std::vector<ElementId> DiagramController::layoutChildren(ElementId owner) const {
        std::vector<ElementId> result;
        for (const ElementId child: nodeChildren(owner)) {
            if (!detachedFromOwner(child))
                result.push_back(child);
        }
        // the inherited parts drawn as nodes (^part1) below / inside the part inheriting them
        if (!Interconnection) {
            for (const auto &[id, derived]: DerivedPorts) {
                if (derived.Anchor == owner && derivedPort(id) && Nodes->find(id) &&
                    Model.element(derived.Port).kind() != ElementKind::PortUsage)
                    result.push_back(id);
            }
        }
        if (owner == RootId) {
            // packages drawn beside their package (⊕ line), elements alone with their qualified name: laid out on
            // the top level
            std::function<void(ElementId)> collect = [&](ElementId parent) {
                for (const ElementId child: Model.children(parent)) {
                    if (detachedFromOwner(child) && Nodes->find(child) && !portAnchor(Model, child))
                        result.push_back(child);
                    collect(child);
                }
            };
            for (const ElementId child: Model.children(RootId))
                collect(child);
        }
        return result;
    }

    qreal DiagramController::frameHeaderExtra(const DiagramNode &node) const {
        qreal height = node.Subtitle.isEmpty() ? 0 : 18; // «alias» Package3Alias at the top of the body
        for (const auto &value: node.Compartments)
            height += 20 + 17 * qreal(value.toMap().value(QStringLiteral("entries")).toList().size());
        return height;
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
        // onto a variation: a part or an item becomes one of its variants; nothing else goes into it (7.6.7)
        if (const auto target = toId(nodeIdAt(point, std::nullopt, false));
            target && isVariation(Model.element(*target)) && tool != DiagramTypes::ToolVariant &&
            tool != DiagramTypes::ToolComment && tool != DiagramTypes::ToolDocumentation &&
            tool != DiagramTypes::ToolDocumentationInside && tool != DiagramTypes::ToolRepresentation) {
            const auto kinds = variantKinds(*target);
            const ElementKind wanted = tool == DiagramTypes::ToolPart ? ElementKind::PartUsage
                                       : tool == DiagramTypes::ToolItem ? ElementKind::ItemUsage
                                       : ElementKind::Textual;
            if (std::find(kinds.begin(), kinds.end(), wanted) != kinds.end())
                return addVariant(QString::number(*target), QString::fromStdString(Diagram::defaultKeyword(wanted)), QString());
            emit statusMessage(tr("A variation holds only its variants: drop a part / item or the Variant tool onto it."));
            return {};
        }
        // an item onto the canvas (or into a package): "item item1 : Item1;" on package level, as a part
        const bool itemOnCanvas = tool == DiagramTypes::ToolItem && !toId(nodeIdAt(point, std::nullopt, false));
        if (isFeatureTool(tool) && !itemOnCanvas) {
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
                                   : tool == DiagramTypes::ToolEnumValue ? tr("Drop the value onto an enum def.")
                                   : tr("Drop it onto a part or a definition that can own it."));
                return {};
            }
            const auto id = addFeature(tool, *partId);
            return id ? QString::number(*id) : QString();
        }
        if (tool == DiagramTypes::ToolStatement)
            return addStatement(point);
        if (tool == DiagramTypes::ToolComment || tool == DiagramTypes::ToolDocumentation ||
            tool == DiagramTypes::ToolRepresentation || tool == DiagramTypes::ToolDocumentationInside)
            return addAnnotation(tool, point);
        static const std::map<int, ElementKind> NodeTools = {
                {DiagramTypes::ToolPart, ElementKind::PartUsage},
                {DiagramTypes::ToolItem, ElementKind::ItemUsage},
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
        // Dropped onto a part (or part definition): a part of it (composition), placed below it.
        bool composed = false;
        if (const auto target = toId(nodeIdAt(point, std::nullopt, false));
            target && (kind == ElementKind::PartUsage || kind == ElementKind::OccurrenceUsage) &&
            isPartLike(Model.element(*target).kind()) && RelationshipRules::canOwn(Model, *target, kind)) {
            owner = *target;
            composed = true;
        }

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
                                 : kind == ElementKind::ItemUsage ? "item"
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
                if (!composed) // a composed part is placed below its owner (defaultPositionFor)
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
        if (kind == ElementKind::Package && owner != RootId && isFrame(owner)) {
            // a package in a package: inside it, smaller than it (the package grows if needed)
            syncNow();
            fitIntoFrame(*id, owner);
        }

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

    QString DiagramController::addAnnotation(int tool, const QPointF &point) {
        // owned by the element under the mouse (and so about it), else by the package frame, else top level
        ElementId owner = RootId;
        if (const auto node = toId(nodeIdAt(point, std::nullopt, false));
            node && RelationshipRules::canOwn(Model, *node, ElementKind::Textual) && !isFrame(*node))
            owner = *node;
        else if (const auto frame = frameAt(point, std::nullopt); frame && RelationshipRules::canOwn(Model, *frame, ElementKind::Textual))
            owner = *frame;
        if (tool == DiagramTypes::ToolDocumentationInside) {
            // the doc in the box of a part / definition (compartment "doc", table 2): no line to draw
            if (owner == RootId || isFrame(owner) || !isNodeElement(Model, owner) || isNoteElement(Model, owner) ||
                portAnchor(Model, owner)) {
                emit statusMessage(tr("Drop it onto a part or a definition: the doc is shown in its box."));
                return {};
            }
            const auto existing = SysMLTextImporter::docOf(Model, owner);
            if (existing) {
                // it has one: shown in the box from now on
                if (docInside(QString::number(owner))) {
                    emit statusMessage(tr("It has a documentation already - double-click it to edit it."));
                    return {};
                }
                Notations[owner][QStringLiteral("docs")] = QStringLiteral("compartment");
                notationsEdited();
                emit statusMessage(tr("The documentation is shown in the box."));
                return QString::number(owner);
            }
            Notations[owner][QStringLiteral("docs")] = QStringLiteral("compartment");
            Diagram::AnnotationStatement doc;
            doc.Keyword = "doc";
            doc.Body = "Documentation";
            std::optional<ElementId> id;
            if (!runModelChange([&] {
                    id = Model.addElement(ElementKind::Textual, "", owner);
                    Model.setText(*id, SysMLTextImporter::annotationStatement(doc), "doc", "");
                })) {
                Notations[owner].erase(QStringLiteral("docs"));
                return {};
            }
            emit layoutEdited();
            emit statusMessage(tr("Added the documentation in the box - double-click it to write its text."));
            return QString::number(*id);
        }
        Diagram::AnnotationStatement annotation;
        annotation.Keyword = tool == DiagramTypes::ToolComment ? "comment" : tool == DiagramTypes::ToolDocumentation ? "doc" : "rep";
        if (tool == DiagramTypes::ToolDocumentation && SysMLTextImporter::docOf(Model, owner) && owner != RootId) {
            emit statusMessage(tr("It has a documentation already - double-click it to edit it."));
            return {};
        }
        // the parser of the app needs a name for comments and textual representations
        if (tool != DiagramTypes::ToolDocumentation)
            annotation.Name = uniqueName(annotation.Keyword, owner);
        if (tool == DiagramTypes::ToolRepresentation)
            annotation.Language = "text";
        annotation.Body = tool == DiagramTypes::ToolComment ? "Comment" : tool == DiagramTypes::ToolDocumentation
                                                                            ? "Documentation" : "Representation";
        const std::string text = SysMLTextImporter::annotationStatement(annotation);
        std::optional<ElementId> id;
        if (!runModelChange([&] {
                id = Model.addElement(ElementKind::Textual, annotation.Name, owner);
                Model.setText(*id, text, annotation.Keyword, annotation.Name);
                PendingPositions[*id] = QPointF(std::max<qreal>(0, point.x() - 70), std::max<qreal>(0, point.y() - 25));
            }))
            return {};
        SelectedNodes = {*id};
        SelectedEdges.clear();
        applySelection();
        emit statusMessage(tr("Added a note - double-click it to write its text, draw an arrow to say what it is about."));
        return QString::number(*id);
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

    std::vector<ElementKind> DiagramController::variantKinds(ElementId variation) const {
        // the kinds consistent with the kind of the variation (7.6.7): in an item variation an item or a part
        if (!Model.contains(variation) || !isVariation(Model.element(variation)))
            return {};
        const ElementKind base = featureKind(Model.element(variation).kind(), DiagramTypes::ToolVariant);
        std::vector<ElementKind> kinds{base};
        if (base == ElementKind::ItemUsage)
            kinds.push_back(ElementKind::PartUsage);
        return kinds;
    }

    std::optional<QString> DiagramController::refusedByVariation(ElementId id, ElementId newOwner) const {
        // "if isVariation = true, then all the memberships of the Usage must be VariantMemberships" (8.3.6)
        if (!Model.contains(newOwner) || !isVariation(Model.element(newOwner)))
            return std::nullopt;
        const auto kinds = variantKinds(newOwner);
        if (std::find(kinds.begin(), kinds.end(), Model.element(id).kind()) != kinds.end())
            return std::nullopt;
        return tr("A variation holds only its variants (here: %1).")
                .arg(QString::fromStdString(Diagram::defaultKeyword(kinds.front())) +
                     (kinds.size() > 1 ? QStringLiteral(", ") + QString::fromStdString(Diagram::defaultKeyword(kinds.back())) : QString()));
    }

    void DiagramController::reparentKeepingVariants(ElementId id, ElementId newOwner) {
        // into a variation: a variant of it ("variant part p;"); out of it: no variant any more
        Model.reparent(id, newOwner);
        std::vector<std::string> prefixes = Model.element(id).prefixes();
        const auto variant = std::find(prefixes.begin(), prefixes.end(), std::string("variant"));
        const bool into = newOwner != RootId && isVariation(Model.element(newOwner));
        if (into && variant == prefixes.end()) {
            prefixes.insert(prefixes.begin(), "variant");
            Model.setPrefixes(id, prefixes);
        } else if (!into && variant != prefixes.end()) {
            prefixes.erase(variant);
            Model.setPrefixes(id, prefixes);
        }
    }

    QVariantList DiagramController::variantChoices(qreal x, qreal y) const {
        // a variant usage is of a kind consistent with the kind of its variation (7.6.7): in an item variation
        // an item or a part; or a separately declared usage as variant ("variant part1;")
        QVariantList choices;
        const auto variation = toId(nodeIdAt(QPointF(x, y), std::nullopt, false));
        if (!variation || !acceptsFeature(*variation, DiagramTypes::ToolVariant))
            return choices;
        const std::vector<ElementKind> kinds = variantKinds(*variation);
        for (const ElementKind kind: kinds)
            choices.append(QVariantMap{{QStringLiteral("text"), tr("New variant %1").arg(QString::fromStdString(Diagram::defaultKeyword(kind)))},
                                       {QStringLiteral("variation"), QString::number(*variation)},
                                       {QStringLiteral("kind"), QString::fromStdString(Diagram::defaultKeyword(kind))},
                                       {QStringLiteral("existing"), QString()}});
        // usages declared elsewhere, found by their name from the variation ("variant smallEngine;")
        std::set<std::string> taken;
        for (const ElementId child: Model.children(*variation))
            taken.insert(Model.element(child).name());
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                if (child == *variation || Model.isAncestor(*variation, child))
                    continue;
                if (std::find(kinds.begin(), kinds.end(), element.kind()) != kinds.end() && !element.name().empty() &&
                    taken.count(element.name()) == 0 && resolveReference(Model, *variation, element.name()) == child &&
                    choices.size() < 24)
                    choices.append(QVariantMap{{QStringLiteral("text"), tr("Existing: %1").arg(QString::fromStdString(element.name()))},
                                               {QStringLiteral("variation"), QString::number(*variation)},
                                               {QStringLiteral("kind"), QString()},
                                               {QStringLiteral("existing"), QString::fromStdString(element.name())}});
                visit(child);
            }
        };
        visit(RootId);
        return choices;
    }

    QString DiagramController::addVariant(const QString &variationId, const QString &kind, const QString &existing) {
        const auto variation = toId(variationId);
        if (!variation || !acceptsFeature(*variation, DiagramTypes::ToolVariant))
            return {};
        const std::vector<ElementKind> kinds = variantKinds(*variation);
        if (kinds.empty())
            return {};
        const ElementKind base = kinds.front();
        ElementKind chosen = base;
        for (const ElementKind fitting: kinds) {
            if (kind == QString::fromStdString(Diagram::defaultKeyword(fitting)))
                chosen = fitting;
        }
        std::optional<ElementId> id;
        if (!runModelChange([&] {
                if (!existing.isEmpty()) {
                    // "variant part1;": the usage named so is the variant (written without kind keyword)
                    id = Model.addElement(base, existing.toStdString(), *variation);
                    Model.setDeclaredKeyword(*id, std::string());
                } else {
                    id = Model.addElement(chosen, uniqueName(chosen == ElementKind::PartUsage ? "part" : Diagram::defaultKeyword(chosen),
                                                             *variation),
                                          *variation);
                }
                Model.setPrefixes(*id, {"variant"});
            }))
            return {};
        return id ? QString::number(*id) : QString();
    }

    bool DiagramController::acceptsFeature(ElementId owner, int tool) const {
        if (!Model.contains(owner) || !isFeatureTool(tool))
            return false;
        const ElementKind ownerKind = Model.element(owner).kind();
        // the objective belongs to a use case / verification; subject and actors to cases and requirements
        if (tool == DiagramTypes::ToolObjective && !Diagram::isCaseKind(ownerKind))
            return false;
        // an enumerated value only in an enum def (it holds nothing else, 7.6.5)
        if (tool == DiagramTypes::ToolEnumValue && ownerKind != ElementKind::EnumerationDefinition)
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
        if (kind == ElementKind::CalcDefinition) {
            // table 18 "Calc Definition": "calc def CalcDef1 { expression1 }" - only the result compartment with the
            // expression; parameters ("in x : Real;") are added when needed
            Model.setExpression(id, "0.0");
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

    const DiagramController::DerivedPort *DiagramController::derivedPort(ElementId id) const {
        const auto found = DerivedPorts.find(id);
        if (found == DerivedPorts.end() || !Model.contains(found->second.Anchor) || !Model.contains(found->second.Port))
            return nullptr;
        return &found->second;
    }

    std::optional<ElementId> DiagramController::toNodeId(const QString &id) const {
        if (const auto element = toId(id))
            return element;
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (ok && isDerivedId(value) && derivedPort(value) && Nodes->find(value))
            return ElementId(value);
        return std::nullopt;
    }

    ElementId DiagramController::elementOf(ElementId node) const {
        const auto *derived = derivedPort(node);
        return derived ? derived->Port : node;
    }

    std::optional<ElementId> DiagramController::anchorOf(ElementId node) const {
        if (const auto *derived = derivedPort(node))
            return Model.element(derived->Port).kind() == ElementKind::PortUsage ? std::optional<ElementId>(derived->Anchor)
                                                                                  : std::nullopt;
        if (!Model.contains(node))
            return std::nullopt;
        return portAnchor(Model, node);
    }

    std::optional<std::pair<ElementId, std::string>> DiagramController::edgeEnd(const QString &id) const {
        if (const auto element = toId(id))
            return std::make_pair(*element, std::string());
        const auto node = toNodeId(id);
        const auto *derived = node ? derivedPort(*node) : nullptr;
        if (!derived)
            return std::nullopt;
        return std::make_pair(derived->Anchor, Model.element(derived->Port).name());
    }

    bool DiagramController::isFrame(ElementId id) const {
        if (!Model.contains(id))
            return false;
        if (Model.element(id).kind() == ElementKind::Package)
            return true;
        const auto *node = Interconnection ? Nodes->find(id) : nullptr;
        return node && node->Container;
    }

    bool DiagramController::isContainer(const DiagramNode &node) {
        return node.Kind == DiagramTypes::PackageNode || node.Container;
    }

    // --- interconnection view ------------------------------------------------------------------------

    namespace {
        /// What the interconnection view shows inside a part: parts, items, occurrences and ports.
        bool isInterconnectionKind(ElementKind kind) {
            return kind == ElementKind::PartUsage || kind == ElementKind::ItemUsage || kind == ElementKind::PortUsage ||
                   kind == ElementKind::OccurrenceUsage;
        }
    }

    std::optional<ElementId> DiagramController::interconnectionContext() const {
        if (ContextElement && Model.contains(*ContextElement))
            return ContextElement;
        return std::nullopt;
    }

    QString DiagramController::contextId() const {
        const auto context = interconnectionContext();
        return context ? QString::number(*context) : QString();
    }

    void DiagramController::setContextId(const QString &id) {
        const auto value = toId(id);
        if (!Interconnection || !value || !Model.contains(*value) || ContextElement == value)
            return;
        ContextElement = value;
        SelectedNodes.clear();
        SelectedEdges.clear();
        emit contextChanged();
        layoutAll();
    }

    void DiagramController::updateContexts() {
        // parts and part definitions with parts, items or ports of their own
        QVariantList contexts;
        std::optional<ElementId> first;
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                const bool candidate = element.kind() == ElementKind::PartUsage || element.kind() == ElementKind::PartDefinition ||
                                       element.kind() == ElementKind::ItemDefinition || element.kind() == ElementKind::ItemUsage;
                const auto &children = Model.children(child);
                if (candidate && std::any_of(children.begin(), children.end(), [&](ElementId inner) {
                        return isInterconnectionKind(Model.element(inner).kind());
                    })) {
                    contexts.append(QVariantMap{{QStringLiteral("id"), QString::number(child)},
                                                {QStringLiteral("title"), QString::fromStdString(Diagram::defaultKeyword(element.kind())) +
                                                                          QLatin1Char(' ') + QString::fromStdString(Model.qualifiedName(child))}});
                    if (!first)
                        first = child;
                }
                visit(child);
            }
        };
        visit(RootId);
        if (contexts != Contexts) {
            Contexts = contexts;
            emit contextsChanged();
        }
        if (!interconnectionContext() && first) {
            // the first part with parts: arranged as soon as it is shown
            ContextElement = first;
            emit contextChanged();
            QMetaObject::invokeMethod(this, [this] { layoutAll(); }, Qt::QueuedConnection);
        }
    }

    bool DiagramController::shownAsNode(ElementId id) const {
        if (!Interconnection) {
            if (!Model.contains(id))
                return false;
            const auto &element = Model.element(id);
            const ElementId owner = element.owner();
            // the doc of an element in its compartment 'doc' (table 2), not a note of its own
            if (isDocInCompartment(id) && notationOf(owner, QStringLiteral("docs")) == QLatin1String("compartment"))
                return false;
            if (emptiedByQualifiedNames(id))
                return false;
            // a definition / usage listed as text in the compartment 'members' of its package (table 3)
            if (owner != RootId && isListedMemberKind(element.kind()) && Model.element(owner).kind() == ElementKind::Package &&
                notationOf(owner, QStringLiteral("members")) == QLatin1String("compartment"))
                return false;
            // a package / import listed as text in the compartment of its package (table 3)
            if ((notationApplies(id, QStringLiteral("packages")) &&
                 notationOf(id, QStringLiteral("packages")) == QLatin1String("compartment")) ||
                (notationApplies(id, QStringLiteral("imports")) &&
                 notationOf(id, QStringLiteral("imports")) == QLatin1String("compartment")))
                return false;
            // an alias as a line to the element or in its name compartment: only if that element is shown
            if (element.kind() == ElementKind::Alias && notationOf(id, QStringLiteral("aliases")) != QLatin1String("nested")) {
                const auto aliased = resolveReference(Model, owner, element.target());
                if (aliased && *aliased != id && Model.element(*aliased).kind() != ElementKind::Alias && shownAsNode(*aliased))
                    return false;
            }
            // the parts / ports of a part listed as text in its compartments (table 9)
            if (owner != RootId && (element.kind() == ElementKind::PartUsage || element.kind() == ElementKind::OccurrenceUsage) &&
                notationApplies(owner, QStringLiteral("features")) &&
                notationOf(owner, QStringLiteral("features")) == QLatin1String("compartment"))
                return false;
            // the variants of a variation listed in its compartment 'variants' / 'variant parts'
            if (owner != RootId && isVariantElement(element) && isVariation(Model.element(owner)) &&
                notationOf(owner, QStringLiteral("variants")) == QLatin1String("compartment"))
                return false;
            if (owner != RootId && element.kind() == ElementKind::PortUsage && notationApplies(owner, QStringLiteral("ports")) &&
                notationOf(owner, QStringLiteral("ports")) == QLatin1String("compartment"))
                return false;
            return isNodeElement(Model, id) && (!ViewElement || ViewKeep.count(id) > 0);
        }
        const auto context = interconnectionContext();
        if (!context || !Model.contains(id))
            return false;
        if (id == *context)
            return true;
        return isInterconnectionKind(Model.element(id).kind()) && Model.isAncestor(*context, id);
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
            if (EdgeRouting::distanceToRoute(point, EdgeRouting::routeOf(*Nodes, edges, *it)) <= EdgeHitDistance)
                return it->Key;
        }
        return {};
    }

    QRectF DiagramController::nodeRect(const QString &id) const {
        const auto elementId = toNodeId(id);
        const auto *node = elementId ? Nodes->find(*elementId) : nullptr;
        return node ? node->Rect : QRectF();
    }

    // --- geometry ----------------------------------------------------------------------------------

    void DiagramController::moveNode(const QString &id, qreal x, qreal y) {
        const auto elementId = toNodeId(id);
        if (!elementId)
            return;
        const int row = Nodes->indexOf(*elementId);
        if (row < 0)
            return;
        const QRectF old = Nodes->nodes()[std::size_t(row)].Rect;
        if (const auto anchor = anchorOf(*elementId)) {
            const auto *anchorNode = Nodes->find(*anchor);
            if (!anchorNode)
                return;
            const QPointF center = onBorder(anchorNode->Rect, QPointF(x, y) + QPointF(PortSize / 2, PortSize / 2));
            AutoLayout = false;
            AutoPlacedPorts.erase(*elementId);
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

    void DiagramController::fitIntoFrame(ElementId id, ElementId frame) {
        constexpr qreal Pad = 16.0;
        // the tab with the name above the body of a package (NodeDelegate.qml); a part drawn around its parts: its
        // name and compartments
        const auto *frameNode = Nodes->find(frame);
        const qreal Tab = frameNode && frameNode->Kind != DiagramTypes::PackageNode ? estimatedSize(*frameNode).height() : 24.0;
        const auto rectOf = [&](ElementId element) -> std::optional<QRectF> {
            const auto *node = Nodes->find(element);
            return node ? std::optional<QRectF>(node->Rect) : std::nullopt;
        };
        const auto setRectOf = [&](ElementId element, const QRectF &rect) {
            if (const int row = Nodes->indexOf(element); row >= 0)
                Nodes->setRect(row, rect);
        };
        const auto inner = [&](const QRectF &rect) { return rect.adjusted(Pad, Tab + Pad, -Pad, -Pad); };
        const auto outer = [&](const QRectF &rect) { return rect.adjusted(-Pad, -Tab - Pad, Pad, Pad); };
        auto movedRect = rectOf(id);
        auto frameRect = rectOf(frame);
        if (!movedRect || !frameRect)
            return;
        AutoLayout = false;
        QRectF rect = *movedRect;
        {
            // a package: as large as the inside of the package it went into, at most, but not smaller than its content;
            // anything else keeps its size
            const bool package = isFrame(id);
            const QRectF space = inner(*frameRect);
            QRectF content;
            for (const ElementId child: package ? Model.children(id) : std::vector<ElementId>{}) {
                if (const auto childRect = rectOf(child); childRect && !detachedFromOwner(child))
                    content = content.isNull() ? *childRect : content.united(*childRect);
            }
            const qreal minimumWidth = std::max(MinimumFrameWidth, content.isNull() ? 0.0 : content.width() + 2 * Pad);
            const qreal minimumHeight = std::max(MinimumFrameHeight, content.isNull() ? 0.0 : content.height() + Tab + 2 * Pad);
            const QSizeF size = package ? QSizeF(std::max(minimumWidth, std::min(rect.width(), space.width())),
                                                 std::max(minimumHeight, std::min(rect.height(), space.height())))
                                        : rect.size();
            // inside the space where possible, the content moves with it
            QPointF topLeft(std::clamp(rect.left(), space.left(), std::max(space.left(), space.right() - size.width())),
                            std::clamp(rect.top(), space.top(), std::max(space.top(), space.bottom() - size.height())));
            // not on top of what is in the package already: then below it (the package grows)
            // (only those drawn in it: not those beside it with a line or alone with their qualified name)
            const auto inFrame = [&](ElementId sibling) -> std::optional<QRectF> {
                if (sibling == id || detachedFromOwner(sibling))
                    return std::nullopt;
                return rectOf(sibling);
            };
            QRectF siblings;
            for (const ElementId sibling: Model.children(frame)) {
                if (const auto siblingRect = inFrame(sibling))
                    siblings = siblings.isNull() ? *siblingRect : siblings.united(*siblingRect);
            }
            const auto overlapsSibling = [&](const QRectF &candidate) {
                for (const ElementId sibling: Model.children(frame)) {
                    const auto siblingRect = inFrame(sibling);
                    if (siblingRect && siblingRect->adjusted(-8, -8, 8, 8).intersects(candidate))
                        return true;
                }
                return false;
            };
            if (overlapsSibling(QRectF(topLeft, size)))
                topLeft = QPointF(space.left(), std::max(space.top(), siblings.bottom() + Pad + Tab));
            QRectF fitted(topLeft, size);
            if (!content.isNull()) {
                // the content keeps its place in the package (moved along), the package around it
                const QPointF delta = topLeft - rect.topLeft();
                const QRectF movedContent = content.translated(delta);
                const QRectF needed = movedContent.adjusted(-Pad, -Tab - Pad, Pad, Pad);
                if (!fitted.contains(needed)) {
                    const QPointF shift(std::min(0.0, needed.left() - fitted.left()) + std::max(0.0, needed.right() - fitted.right()),
                                        std::min(0.0, needed.top() - fitted.top()) + std::max(0.0, needed.bottom() - fitted.bottom()));
                    fitted.translate(shift);
                }
                moveFrameContent(id, delta);
            }
            translatePorts(id, fitted.topLeft() - rect.topLeft());
            if (!package && isContainer(*Nodes->find(id)))
                moveFrameContent(id, fitted.topLeft() - rect.topLeft());
            rect = fitted;
            setRectOf(id, rect);
        }
        // the packages around it grow where it does not fit (up to the top level)
        QRectF insideRect = rect;
        for (ElementId around = frame; around != RootId && (isFrame(around) || nestedFeatures(around));
             around = Model.element(around).owner()) {
            const auto aroundRect = rectOf(around);
            if (!aroundRect)
                break;
            const QRectF needed = outer(insideRect);
            if (aroundRect->contains(needed))
                break;
            const QRectF grown = aroundRect->united(needed);
            setRectOf(around, grown);
            insideRect = grown;
        }
        emit layoutEdited();
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
        emit layoutEdited();
    }

    void DiagramController::updateContainment(ElementId id) {
        if (!Model.contains(id))
            return;
        const ElementId owner = Model.element(id).owner();
        // What a package (or the top level) owns goes where it is dropped: a frame stands for a package, a part drawn
        // around its parts (notation "inside the part", table 9) for that part. What a part, calc def, action, ...
        // owns otherwise (a feature on a composition line, a note "rep r1 ... /* */" in it) stays there wherever it is
        // placed.
        const bool ownedByContainer = owner != RootId && nestedFeatures(owner);
        // owned by a part on a composition line (tree): it stays there wherever it is placed, unless it is dropped
        // onto another part - then it becomes one of its parts
        const bool ownedInTree = owner != RootId && Model.element(owner).kind() != ElementKind::Package && !ownedByContainer;
        if (ownedInTree && !composes(Model.element(owner).kind()) && !isVariation(Model.element(owner)))
            return; // (the variant of a variation can be moved out of it)

        // a package drawn beside its package (⊕ line) / an element alone with its qualified name stays in it
        if (detachedFromOwner(id))
            return;
        const auto *node = Nodes->find(id);
        if (!node || node->Kind == DiagramTypes::PortNode)
            return;
        const QPointF center = node->Rect.center();
        auto frame = frameAt(center, id);
        if (frame && Model.isAncestor(id, *frame))
            frame.reset();
        // dropped onto a part / part definition: into it ("part part2 { part def Part3; }", a usage body may hold
        // definitions and usages) - the smallest one under the middle, not the element itself or what is in it
        std::optional<ElementId> part;
        qreal partArea = 0;
        for (const auto &other: Nodes->nodes()) {
            if (other.Id == id || !Model.contains(other.Id) || other.Kind == DiagramTypes::PortNode ||
                other.Kind == DiagramTypes::PackageNode ||
                !(composes(Model.element(other.Id).kind()) || isVariation(Model.element(other.Id))) || // or: a variant of it
                Model.isAncestor(id, other.Id) || !other.Rect.contains(center))
                continue;
            if (frame && !Model.isAncestor(*frame, other.Id))
                continue; // a part outside the innermost package under the point
            const qreal area = other.Rect.width() * other.Rect.height();
            if (!part || area < partArea) {
                part = other.Id;
                partArea = area;
            }
        }
        if (part && !isVariation(Model.element(*part)) && !RelationshipRules::canReparent(Model, id, *part))
            part.reset();
        if (ownedInTree && !part)
            return;
        const ElementId newOwner = part ? *part : frame ? *frame : RootId;
        if (newOwner == owner)
            return;
        // the owner of a part drawn as composition tree keeps it (it is placed freely below)
        if (ownedByContainer && part && !nestedFeatures(*part) && Model.element(*part).kind() != ElementKind::Package &&
            Model.isAncestor(owner, *part))
            return;
        auto allowed = RelationshipRules::canReparent(Model, id, newOwner);
        if (newOwner != RootId && isVariation(Model.element(newOwner))) {
            // a variation takes the usages of a fitting kind as its variants, nothing else (7.6.7)
            const auto refused = refusedByVariation(id, newOwner);
            allowed = refused ? Diagram::RuleResult{false, refused->toStdString()} : Diagram::RuleResult{};
        }
        const std::string &name = Model.element(id).name();
        const bool nameTaken = !name.empty() && std::any_of(Model.children(newOwner).begin(), Model.children(newOwner).end(),
                                                            [&](ElementId other) {
                                                                return other != id && Model.element(other).name() == name;
                                                            });
        if (!allowed || nameTaken) {
            // it stays where it is in the model: so it is drawn there again (inside its package), with the reason
            emit statusMessage(nameTaken ? tr("'%1' cannot be moved there: '%2' has an element of that name already - rename one "
                                              "of them first.").arg(QString::fromStdString(name),
                                                                    newOwner == RootId ? tr("the top level")
                                                                                       : QString::fromStdString(Model.element(newOwner).name()))
                                         : tr("It cannot be moved there: %1").arg(QString::fromStdString(allowed.Reason)));
            if (owner != RootId && isFrame(owner))
                fitIntoFrame(id, owner);
            return;
        }
        if (runModelChange([&] { reparentKeepingVariants(id, newOwner); })) {
            if (part) {
                syncNow();
                if (nestedFeatures(*part)) {
                    fitIntoFrame(id, *part); // drawn inside it
                } else if (const auto *ownerNode = Nodes->find(*part); ownerNode && Nodes->find(id)) {
                    // below it, on its line (not on top of it)
                    const QRectF rect = Nodes->find(id)->Rect;
                    QPointF position(ownerNode->Rect.left(), ownerNode->Rect.bottom() + 50);
                    for (const auto &other: Nodes->nodes()) {
                        if (other.Id != id && other.Kind != DiagramTypes::PackageNode && other.Kind != DiagramTypes::PortNode &&
                            other.Rect.adjusted(-12, -12, 12, 12).intersects(QRectF(position, rect.size())))
                            position.setX(std::max(position.x(), other.Rect.right() + 24));
                    }
                    if (const int row = Nodes->indexOf(id); row >= 0)
                        Nodes->setRect(row, QRectF(position, rect.size()));
                    translatePorts(id, position - rect.topLeft());
                    emit layoutEdited();
                }
                emit statusMessage(tr("Moved into '%1'.").arg(QString::fromStdString(Model.element(newOwner).name())));
                return;
            }
            if (frame)
                fitIntoFrame(id, *frame);
            emit statusMessage(frame ? tr("Moved into package '%1'.").arg(QString::fromStdString(Model.element(newOwner).name()))
                                     : tr("Moved out of the package."));
        }
    }

    void DiagramController::setNodeSize(const QString &id, qreal width, qreal height) {
        const auto elementId = toNodeId(id); // also an inherited part drawn as node (^part1)
        if (!elementId || isFrame(*elementId) || width <= 0 || height <= 0 || anchorOf(*elementId))
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
        // ports stay on the border; those nobody placed by hand go to their slot (the part grew to fit them)
        for (const ElementId port: portsOf(*elementId)) {
            const int portRow = Nodes->indexOf(port);
            if (portRow < 0)
                continue;
            if (AutoPlacedPorts.count(port) > 0) {
                Nodes->setRect(portRow, portSlot(port));
                continue;
            }
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
        emit layoutEdited();
    }

    bool DiagramController::docInside(const QString &id) const {
        const auto element = toId(id);
        if (!element)
            return false;
        const auto choices = Notations.find(*element);
        return choices != Notations.end() && choices->second.count(QStringLiteral("docs")) > 0 &&
               choices->second.at(QStringLiteral("docs")) == QLatin1String("compartment");
    }

    void DiagramController::setDocInside(const QString &id, bool inside) {
        setNotation(id, QStringLiteral("docs"), inside ? QStringLiteral("compartment") : QStringLiteral("note"));
    }

    QJsonObject DiagramController::layoutState() const {
        QJsonObject layout;
        // how things are drawn (a choice of the view, not in the text): per group and per element
        QJsonObject modes;
        for (const auto &[group, mode]: NotationModes)
            modes.insert(group, mode);
        if (!modes.isEmpty())
            layout.insert(QStringLiteral("@notationModes"), modes);
        QJsonObject notations;
        for (const auto &[id, choices]: Notations) {
            if (!Model.contains(id) || choices.empty())
                continue;
            QJsonObject entry;
            for (const auto &[group, value]: choices)
                entry.insert(group, value);
            notations.insert(DiagramLayout::keyOf(Model, id), entry);
        }
        if (!notations.isEmpty())
            layout.insert(QStringLiteral("@notations"), notations);
        if (AutoLayout)
            return layout;
        for (const auto &node: Nodes->nodes()) {
            if (!Model.contains(node.Id))
                continue;
            layout.insert(DiagramLayout::keyOf(Model, node.Id),
                          node.Kind == DiagramTypes::PackageNode ? DiagramLayout::toJson(node.Rect)
                                                                 : DiagramLayout::toJson(node.Rect.topLeft()));
        }
        return layout;
    }

    void DiagramController::restoreLayout(const QJsonObject &layout) {
        if (layout.isEmpty())
            return;
        syncNow();
        const auto byKey = DiagramLayout::elementsByKey(Model);
        bool notations = false;
        // (an older layout: "@docsInside")
        for (const auto &value: layout.value(QStringLiteral("@docsInside")).toArray()) {
            if (const auto element = byKey.find(value.toString()); element != byKey.end()) {
                Notations[element->second][QStringLiteral("docs")] = QStringLiteral("compartment");
                notations = true;
            }
        }
        const QJsonObject modes = layout.value(QStringLiteral("@notationModes")).toObject();
        for (auto it = modes.begin(); it != modes.end(); ++it) {
            const auto *info = notationGroup(it.key());
            if (info && isNotationValue(*info, it.value().toString())) {
                NotationModes[it.key()] = it.value().toString();
                notations = true;
            }
        }
        const QJsonObject stored = layout.value(QStringLiteral("@notations")).toObject();
        for (auto it = stored.begin(); it != stored.end(); ++it) {
            const auto element = byKey.find(it.key());
            if (element == byKey.end())
                continue;
            const QJsonObject choices = it.value().toObject();
            for (auto choice = choices.begin(); choice != choices.end(); ++choice) {
                const auto *info = notationGroup(choice.key());
                if (info && isNotationValue(*info, choice.value().toString())) {
                    Notations[element->second][choice.key()] = choice.value().toString();
                    notations = true;
                }
            }
        }
        if (notations) {
            ++NotationRevision;
            emit notationsChanged();
            Measured.clear();
            syncNow();
        }
        bool applied = false;
        for (auto it = layout.begin(); it != layout.end(); ++it) {
            const auto element = byKey.find(it.key());
            const auto position = DiagramLayout::positionOf(it.value());
            if (element == byKey.end() || !position)
                continue;
            const int row = Nodes->indexOf(element->second);
            if (row < 0)
                continue;
            const auto &node = Nodes->nodes()[std::size_t(row)];
            QSizeF size = node.Rect.size();
            if (node.Kind == DiagramTypes::PackageNode)
                size = DiagramLayout::sizeOf(it.value()).value_or(size);
            Nodes->setRect(row, QRectF(*position, size));
            AutoPlacedPorts.erase(element->second);
            applied = true;
        }
        if (applied) {
            AutoLayout = false;
            repairContainment(); // what has no stored place (shown again by its notation) inside its package
        } else if (notations) {
            layoutAll(); // the automatic layout, with the notations
        }
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
            runModelChange([&] { reparentKeepingVariants(child, newOwner); });
            return;
        }
        bool ok = false;
        if (key.startsWith(QStringLiteral("n:"))) {
            // a line of an n-ary relationship: the relationship is removed
            const auto relationship = key.mid(2).section(QLatin1Char(':'), 0, 0).toULongLong(&ok);
            if (ok && Model.containsRelationship(relationship))
                runModelChange([&] { Model.removeRelationship(relationship); });
            return;
        }
        if (key.startsWith(QStringLiteral("s:"))) {
            // reference subsetting: removed
            const ElementId usage = key.mid(2).toULongLong(&ok);
            if (ok && Model.contains(usage))
                runModelChange([&] { Model.setReferencedFeature(usage, "", ""); });
            return;
        }
        if (key.startsWith(QStringLiteral("t:"))) {
            // "defined by": the usage loses its type
            const ElementId usage = key.mid(2).toULongLong(&ok);
            if (ok && Model.contains(usage))
                runModelChange([&] { Model.setTypeName(usage, ""); });
            return;
        }
        if (key.startsWith(QStringLiteral("i:")) || key.startsWith(QStringLiteral("p:")) || key.startsWith(QStringLiteral("al:"))) {
            // «import» / perform / alias line: the statement is removed
            const ElementId statement = key.mid(key.indexOf(QLatin1Char(':')) + 1).toULongLong(&ok);
            if (ok && Model.contains(statement))
                runModelChange([&] { Model.removeElement(statement); });
            return;
        }
        if (key.startsWith(QStringLiteral("a:"))) {
            // an annotation: the element is no longer named after "about" (a note in a body is about its owner)
            const QStringList parts = key.mid(2).split(QLatin1Char(':'));
            const ElementId note = parts.value(0).toULongLong(&ok);
            const int index = parts.value(1).toInt();
            if (!ok || !Model.contains(note))
                return;
            if (index < 0) {
                emit statusMessage(tr("The note belongs to this element - move it or delete it instead."));
                return;
            }
            const auto &element = Model.element(note);
            if (element.kind() == ElementKind::MetadataUsage) {
                auto about = element.about();
                if (index < int(about.size())) {
                    about.erase(about.begin() + index);
                    runModelChange([&] { Model.setAbout(note, about); });
                }
                return;
            }
            auto annotation = SysMLTextImporter::describeAnnotation(element.text());
            if (index >= int(annotation.About.size()))
                return;
            annotation.About.erase(annotation.About.begin() + index);
            const std::string text = SysMLTextImporter::annotationStatement(annotation);
            runModelChange([&] { Model.setText(note, text, element.keyword(), element.name()); });
            return;
        }
        if (key.startsWith(QStringLiteral("d:"))) {
            // a redefinition: that reference is removed
            const QStringList parts = key.mid(2).split(QLatin1Char(':'));
            const ElementId feature = parts.value(0).toULongLong(&ok);
            const int index = parts.value(1).toInt();
            if (!ok || !Model.contains(feature) || index < 0 ||
                index >= int(Model.element(feature).redefinitions().size()))
                return;
            auto references = Model.element(feature).redefinitions();
            references.erase(references.begin() + index);
            const auto &element = Model.element(feature);
            if (element.nameFromRedefinition() && index == 0) {
                emit statusMessage(tr("The feature is named after the redefined one - edit it instead."));
                return;
            }
            runModelChange([&] { Model.setRedefinitions(feature, element.redefinitionKeyword(), references, false); });
            return;
        }
        if (key.startsWith(QStringLiteral("x:"))) {
            // an «expose»: the expose statement goes
            const ElementId expose = key.mid(2).toULongLong(&ok);
            if (ok && Model.contains(expose) && Model.element(expose).kind() == ElementKind::Expose)
                runModelChange([&] { Model.removeElement(expose); });
            return;
        }
        if (key.startsWith(QStringLiteral("cd:"))) {
            // the line of a connection definition stands for its ends: they are edited in the definition
            emit statusMessage(tr("The line shows the ends of the connection definition - edit or delete the definition."));
            return;
        }
        const auto relationship = key.mid(2).toULongLong(&ok);
        if (ok && Model.containsRelationship(relationship))
            runModelChange([&] { Model.removeRelationship(relationship); });
    }

    // --- edges -------------------------------------------------------------------------------------

    std::optional<std::pair<ElementId, ElementId>> DiagramController::documentationEnds(ElementId source, ElementId target) const {
        // a doc documents the element it is in (table 2, "Annotation-Documentation"): a line between a doc and another
        // element (either way) moves the doc into that element
        const auto isDoc = [&](ElementId id) {
            return Model.element(id).kind() == ElementKind::Textual && Model.element(id).keyword() == "doc" &&
                   isNoteStatement(Model.element(id));
        };
        const auto canDocument = [&](ElementId doc, ElementId owner) -> bool {
            const auto &element = Model.element(owner);
            return element.kind() != ElementKind::Textual && element.kind() != ElementKind::Import &&
                   element.kind() != ElementKind::MetadataUsage && Model.element(doc).owner() != owner &&
                   !Model.isAncestor(doc, owner) && !element.performsPath() &&
                   bool(RelationshipRules::canReparent(Model, doc, owner));
        };
        if (source == target)
            return std::nullopt;
        if (isDoc(source) && !isDoc(target) && canDocument(source, target))
            return std::make_pair(source, target);
        if (isDoc(target) && !isDoc(source) && canDocument(target, source))
            return std::make_pair(target, source);
        return std::nullopt;
    }

    QVariantList DiagramController::allowedEdgeKinds(const QString &source, const QString &target) const {
        QVariantList kinds;
        // from / to a port a part inherits: "connect tank.fuelOut to engine.fuelIn" (connections, flows and
        // interfaces between the features of the parts)
        if (!toId(source) || !toId(target)) {
            const auto from = edgeEnd(source);
            const auto to = edgeEnd(target);
            if (!from || !to || source == target)
                return kinds;
            for (const int kind: {int(DiagramTypes::Connection), int(DiagramTypes::Flow), int(DiagramTypes::Interface)}) {
                if (RelationshipRules::canRelate(Model, *toRelationshipKind(kind), from->first, to->first, from->second,
                                                 to->second))
                    kinds.append(kind);
            }
            return kinds;
        }
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
                              int(DiagramTypes::Frame), int(DiagramTypes::Exhibit), int(DiagramTypes::Assert),
                              int(DiagramTypes::Assume), int(DiagramTypes::Require)}) {
            if (RelationshipRules::canRelate(Model, *toRelationshipKind(kind), *sourceId, *targetId))
                kinds.append(kind);
        }
        const auto &sourceElement = Model.element(*sourceId);
        // "defined by": the definition becomes the type of the usage
        if (typesUsage(targetKind, sourceKind) && sourceKind != ElementKind::PortUsage && !sourceElement.performsPath() &&
            resolveReference(Model, sourceElement.owner(), sourceElement.typeName()) != targetId)
            kinds.append(int(DiagramTypes::DefinedBy));
        // "references": a usage references another usage declared elsewhere (four dots and a hollow triangle)
        if (Diagram::isUsageKind(sourceKind) && Diagram::isUsageKind(targetKind) && sourceElement.referencedFeature().empty() &&
            !Model.isAncestor(*sourceId, *targetId) && !Model.isAncestor(*targetId, *sourceId) &&
            sourceKind != ElementKind::EnumerationLiteral && targetKind != ElementKind::EnumerationLiteral &&
            sourceKind != ElementKind::ControlNode && targetKind != ElementKind::ControlNode && !sourceElement.performsPath() &&
            !(sourceKind == ElementKind::ActionPerformance && sourceElement.performStyle() == Diagram::PerformStyle::ReferenceAction))
            kinds.append(int(DiagramTypes::ReferenceSubsetting));
        // a comment or metadata is about another element ("comment c about x", "metadata M about x")
        if ((sourceKind == ElementKind::MetadataUsage ||
             (isNoteElement(Model, *sourceId) && sourceElement.keyword() == "comment")) &&
            !Model.isAncestor(*targetId, *sourceId))
            kinds.append(int(DiagramTypes::Annotation));
        else if (documentationEnds(*sourceId, *targetId))
            kinds.append(int(DiagramTypes::Annotation)); // a doc: moved into the element
        // «expose»: a view exposes an element (a package: its members)
        if (sourceKind == ElementKind::ViewUsage && !Model.isAncestor(*sourceId, *targetId) && *sourceId != *targetId &&
            targetKind != ElementKind::Textual && !Model.element(*targetId).name().empty())
            kinds.append(int(DiagramTypes::Expose));
        // «import»: a package imports another package (its members) or an element
        if (sourceKind == ElementKind::Package && !Model.isAncestor(*sourceId, *targetId) &&
            !Model.isAncestor(*targetId, *sourceId))
            kinds.append(int(DiagramTypes::Import));
        // a part performs an action of an action tree ("perform providePower.generateTorque;")
        if (isPartLike(sourceKind) && targetKind == ElementKind::ActionUsage && !Model.isAncestor(*sourceId, *targetId)) {
            std::string path = Model.element(*targetId).name();
            for (ElementId owner = Model.element(*targetId).owner();
                 owner != RootId && Model.element(owner).kind() == ElementKind::ActionUsage; owner = Model.element(owner).owner())
                path = Model.element(owner).name() + "." + path;
            if (resolveReference(Model, *sourceId, path) == targetId)
                kinds.append(int(DiagramTypes::Performs));
        }
        return kinds;
    }

    bool DiagramController::createEdge(int kind, const QString &source, const QString &target) {
        if (!toId(source) || !toId(target)) {
            // an end is a port the part inherits: written as a feature of the part ("tank.fuelOut")
            const auto from = edgeEnd(source);
            const auto to = edgeEnd(target);
            const auto relationship = toRelationshipKind(kind);
            if (!from || !to || !relationship || !allowedEdgeKinds(source, target).contains(kind))
                return false;
            const bool ok = runModelChange([&] {
                Model.addRelationship(*relationship, from->first, to->first, from->second, to->second);
            });
            if (ok)
                emit statusMessage(tr("%1 created.").arg(edgeKindName(kind)));
            return ok;
        }
        const auto sourceId = toId(source);
        const auto targetId = toId(target);
        const bool special = kind == DiagramTypes::Composition || kind == DiagramTypes::DefinedBy ||
                             kind == DiagramTypes::Import || kind == DiagramTypes::Performs ||
                             kind == DiagramTypes::Annotation || kind == DiagramTypes::ReferenceSubsetting ||
                             kind == DiagramTypes::Expose;
        if (!sourceId || !targetId || (!special && !toRelationshipKind(kind)))
            return false;
        if (special && kind != DiagramTypes::Composition && !allowedEdgeKinds(source, target).contains(kind))
            return false;
        const bool ok = runModelChange([&] {
            if (kind == DiagramTypes::Composition) {
                if (const auto refused = refusedByVariation(*targetId, *sourceId))
                    throw Diagram::ModelError(refused->toStdString());
                reparentKeepingVariants(*targetId, *sourceId);
            } else if (kind == DiagramTypes::DefinedBy) {
                Model.setTypeName(*sourceId, referenceTo(Model, Model.element(*sourceId).owner(), *targetId));
            } else if (kind == DiagramTypes::Import) {
                const ElementId import = Model.addElement(ElementKind::Import, "", *sourceId);
                std::string target;
                for (const ElementId id: Model.ownershipPath(*targetId))
                    target += (target.empty() ? "" : "::") + Diagram::SysMLCodeGenerator::formatName(Model.element(id).name());
                if (Model.element(*targetId).kind() == ElementKind::Package)
                    target += "::*";
                Model.setTarget(import, target);
                Model.setPrefixes(import, {"private"});
            } else if (kind == DiagramTypes::Expose) {
                // "expose Vehicles::vehicle;" ("::*" for the members of a package)
                const ElementId expose = Model.addElement(ElementKind::Expose, "", *sourceId);
                std::string target;
                for (const ElementId id: Model.ownershipPath(*targetId))
                    target += (target.empty() ? "" : "::") + Diagram::SysMLCodeGenerator::formatName(Model.element(id).name());
                if (Model.element(*targetId).kind() == ElementKind::Package)
                    target += "::*";
                Model.setTarget(expose, target);
            } else if (kind == DiagramTypes::ReferenceSubsetting) {
                Model.setReferencedFeature(*sourceId, "references",
                                           referenceTo(Model, Model.element(*sourceId).owner(), *targetId));
            } else if (kind == DiagramTypes::Annotation) {
                if (const auto documentation = documentationEnds(*sourceId, *targetId))
                    Model.reparent(documentation->first, documentation->second);
                else
                    addAbout(*sourceId, annotationReference(*sourceId, *targetId));
            } else if (kind == DiagramTypes::Performs) {
                std::string path = Model.element(*targetId).name();
                for (ElementId owner = Model.element(*targetId).owner();
                     owner != RootId && Model.element(owner).kind() == ElementKind::ActionUsage; owner = Model.element(owner).owner())
                    path = Model.element(owner).name() + "." + path;
                const ElementId perform = Model.addElement(ElementKind::ActionPerformance, path, *sourceId);
                Model.setPerformStyle(perform, Diagram::PerformStyle::ReferenceAction);
                if (path.find('.') != std::string::npos)
                    Model.setPerformsPath(perform, true);
            } else {
                Model.addRelationship(*toRelationshipKind(kind), *sourceId, *targetId);
            }
        });
        if (ok)
            emit statusMessage(tr("%1 created.").arg(edgeKindName(kind)));
        return ok;
    }

    std::string DiagramController::annotationReference(ElementId note, ElementId target) const {
        // named from where the note is: a member of its owner by name, anything else qualified
        const ElementId noteOwner = Model.element(note).owner();
        if (noteOwner != RootId && Model.isAncestor(noteOwner, target))
            return referenceTo(Model, noteOwner, target);
        std::string reference;
        for (const ElementId id: Model.ownershipPath(target))
            reference += (reference.empty() ? "" : "::") + Diagram::SysMLCodeGenerator::formatName(Model.element(id).name());
        return reference;
    }

    void DiagramController::addAbout(ElementId note, const std::string &reference) {
        const auto &element = Model.element(note);
        if (element.kind() == ElementKind::MetadataUsage) {
            auto about = element.about();
            about.push_back(reference);
            Model.setAbout(note, about);
            return;
        }
        auto annotation = SysMLTextImporter::describeAnnotation(element.text());
        annotation.About.push_back(reference);
        if (annotation.Name.empty() && annotation.ShortName.empty()) // the parser of the app needs a name
            annotation.Name = uniqueName("comment", element.owner());
        annotation.KeywordWritten = true;
        Model.setText(note, SysMLTextImporter::annotationStatement(annotation), "comment", annotation.Name);
    }

    QVariantList DiagramController::annotationTargets(const QString &source, const QString &target) const {
        // the element itself and what is shown in its compartments (owned and inherited features), as in table 2
        QVariantList targets;
        if (!allowedEdgeKinds(source, target).contains(int(DiagramTypes::Annotation)))
            return targets;
        auto targetId = toId(target);
        if (const auto documentation = documentationEnds(*toId(source), *targetId)) {
            // a doc goes into the element (no "about"): the element is the only choice
            const auto &owner = Model.element(documentation->second);
            targets.append(QVariantMap{{QStringLiteral("path"), QString()},
                                       {QStringLiteral("label"), keywordOf(owner) + QLatin1Char(' ') +
                                                                 QString::fromStdString(owner.name())}});
            return targets;
        }
        const auto &element = Model.element(*targetId);
        const auto label = [&](ElementId id) {
            const auto &feature = Model.element(id);
            QString text = QString::fromStdString(Diagram::defaultKeyword(feature.kind()));
            if (feature.declaredKeyword().has_value() && !feature.declaredKeyword()->empty())
                text = QString::fromStdString(*feature.declaredKeyword());
            return text + QLatin1Char(' ') + usageText(Model, id);
        };
        targets.append(QVariantMap{{QStringLiteral("path"), QString()},
                                   {QStringLiteral("label"), keywordOf(element) + QLatin1Char(' ') +
                                                             QString::fromStdString(element.name())}});
        const auto add = [&](ElementId id, bool inherited) {
            const auto &feature = Model.element(id);
            if (feature.name().empty() || feature.kind() == ElementKind::Textual || feature.kind() == ElementKind::Import ||
                feature.kind() == ElementKind::MetadataUsage || isNoteElement(Model, id))
                return;
            targets.append(QVariantMap{{QStringLiteral("path"), QString::fromStdString(
                                                Diagram::SysMLCodeGenerator::formatName(feature.name()))},
                                       {QStringLiteral("label"), (inherited ? QStringLiteral("^") : QString()) + label(id)}});
        };
        for (const ElementId child: Model.children(*targetId))
            add(child, false);
        for (const ElementId inherited: inheritedFeatures(Model, *targetId))
            add(inherited, true);
        return targets;
    }

    bool DiagramController::createAnnotation(const QString &source, const QString &target, const QString &path) {
        const auto sourceId = toId(source);
        const auto targetId = toId(target);
        if (!sourceId || !targetId || !allowedEdgeKinds(source, target).contains(int(DiagramTypes::Annotation)))
            return false;
        if (documentationEnds(*sourceId, *targetId))
            return createEdge(DiagramTypes::Annotation, source, target);
        // "comment c about part1::attribute1": a feature of the element (owned or inherited)
        std::string reference = annotationReference(*sourceId, *targetId);
        if (!path.isEmpty())
            reference += "::" + path.toStdString();
        const bool ok = runModelChange([&] { addAbout(*sourceId, reference); });
        if (ok)
            emit statusMessage(tr("Annotation created."));
        return ok;
    }

    QVariantMap DiagramController::arrowReleased(const QString &source, qreal x, qreal y) {
        const auto sourceId = toNodeId(source);
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
                const auto from = edgeEnd(source);
                const auto to = edgeEnd(target);
                if (from && to)
                    emit statusMessage(QString::fromStdString(
                            RelationshipRules::canRelate(Model, *relationship, from->first, to->first, from->second,
                                                         to->second).Reason));
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
            case DiagramTypes::Import:
                return tr("Import");
            case DiagramTypes::DefinedBy:
                return tr("Defined by (type)");
            case DiagramTypes::Redefinition:
                return tr("Redefinition");
            case DiagramTypes::Performs:
                return tr("Perform (action)");
            case DiagramTypes::Aggregation:
                return tr("Reference");
            case DiagramTypes::Annotation:
                return tr("Annotation (about)");
            case DiagramTypes::VariantMembership:
                return tr("Variant");
            case DiagramTypes::Exhibit:
                return tr("Exhibit (state)");
            case DiagramTypes::Assert:
                return tr("Assert (constraint)");
            case DiagramTypes::Assume:
                return tr("Assume (constraint)");
            case DiagramTypes::Require:
                return tr("Require (constraint)");
            case DiagramTypes::Expose:
                return tr("Expose");
            case DiagramTypes::ReferenceSubsetting:
                return tr("References");
            case DiagramTypes::Membership:
                return tr("Owned member");
            case DiagramTypes::AliasMembership:
                return tr("Alias");
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
        // ports that can be listed and added in the dialog (not the ends of an interface definition)
        const bool hasPorts = kind != ElementKind::InterfaceDefinition && acceptsFeature(*elementId, DiagramTypes::ToolPort);
        QVariantList ports;
        if (hasPorts) {
            for (const ElementId child: Model.children(*elementId)) {
                const auto &port = Model.element(child);
                if (port.kind() == ElementKind::PortUsage)
                    ports.append(QVariantMap{{QStringLiteral("id"), QString::number(child)},
                                             {QStringLiteral("name"), QString::fromStdString(port.name())},
                                             {QStringLiteral("typeName"), QString::fromStdString(port.typeName())}});
            }
        }
        // the prefixes of the standard as switches (8.2.2 RefPrefix / BasicUsagePrefix / DefinitionPrefix /
        // VariantUsageMember): which apply to this element, which are set; the rest (#metadata, ...) as text
        const bool definition = Diagram::isDefinitionKind(kind);
        const bool usageKind = Diagram::isUsageKind(kind) && kind != ElementKind::EnumerationLiteral &&
                               kind != ElementKind::ControlNode && !element.isElseBranch();
        const bool occurrenceLike = kind == ElementKind::PartUsage || kind == ElementKind::PartDefinition ||
                                    kind == ElementKind::ItemUsage || kind == ElementKind::ItemDefinition ||
                                    kind == ElementKind::OccurrenceUsage || kind == ElementKind::OccurrenceDefinition;
        QStringList prefixOptions;
        if (definition)
            prefixOptions = {QStringLiteral("abstract"), QStringLiteral("variation")};
        else if (usageKind)
            prefixOptions = {QStringLiteral("derived"), QStringLiteral("abstract"), QStringLiteral("variation"),
                             QStringLiteral("constant"), QStringLiteral("ref"), QStringLiteral("end")};
        if (occurrenceLike)
            prefixOptions.append(QStringLiteral("individual"));
        if (usageKind && element.owner() != RootId && isVariation(Model.element(element.owner())))
            prefixOptions.prepend(QStringLiteral("variant"));
        QVariantMap prefixFlags;
        QString direction;
        QStringList otherPrefixes;
        for (const auto &prefix: element.prefixes()) {
            const QString text = QString::fromStdString(prefix);
            if (prefixOptions.contains(text))
                prefixFlags[text] = true;
            else if (usageKind && (text == QLatin1String("in") || text == QLatin1String("out") || text == QLatin1String("inout")))
                direction = text;
            else
                otherPrefixes.append(text);
        }
        // "60 [mm]" -> value "60", unit "mm"
        QString enumValue = QString::fromStdString(element.value());
        QString enumUnit;
        if (kind == ElementKind::EnumerationLiteral && enumValue.endsWith(QLatin1Char(']'))) {
            const auto open = enumValue.lastIndexOf(QLatin1Char('['));
            if (open > 0) {
                enumUnit = enumValue.mid(open + 1, enumValue.size() - open - 2).trimmed();
                enumValue = enumValue.left(open).trimmed();
            }
        }
        // what it specializes / subsets: the elements of the model (lines) and those it does not hold
        QStringList specializations;
        for (const auto &relationship: Model.relationships()) {
            if (relationship.Kind == RelationshipKind::Specialization && relationship.Source == *elementId &&
                Model.contains(relationship.Target))
                specializations.append(QString::fromStdString(referenceTo(Model, element.owner(), relationship.Target)));
        }
        for (const auto &reference: element.externalSpecializations())
            specializations.append(QString::fromStdString(reference));
        const QStringList qualifiers = QString::fromStdString(element.multiplicityQualifiers()).split(QLatin1Char(' '),
                                                                                                       Qt::SkipEmptyParts);
        return {
            {QStringLiteral("prefixOptions"), prefixOptions},
            {QStringLiteral("prefixFlags"), prefixFlags},
            {QStringLiteral("hasDirection"), usageKind},
            {QStringLiteral("direction"), direction},
            {QStringLiteral("otherPrefixes"), otherPrefixes.join(QLatin1Char(' '))},
            {QStringLiteral("hasSpecializations"), definition || usageKind},
            {QStringLiteral("specializationWord"), definition ? QStringLiteral("specializes") : QStringLiteral("subsets")},
            {QStringLiteral("specializations"), specializations.join(QStringLiteral(", "))},
            {QStringLiteral("specializationKeyword"), element.specializationKeyword().empty()
                                                      ? QStringLiteral(":>") : QString::fromStdString(element.specializationKeyword())},
            // an enumerated value (8.2.2.8): "enum red;" / "red;", "enum = 60 [mm];" / "= 60 [mm];"
            {QStringLiteral("isEnumValue"), kind == ElementKind::EnumerationLiteral},
            {QStringLiteral("enumKeyword"), element.declaredKeyword() != std::optional<std::string>(std::string())},
            {QStringLiteral("enumValue"), enumValue},
            {QStringLiteral("enumUnit"), enumUnit},
            {QStringLiteral("hasReferences"), usageKind && kind != ElementKind::ActionPerformance},
            {QStringLiteral("references"), QString::fromStdString(element.referencedFeature())},
            {QStringLiteral("referencesKeyword"), element.referencesKeyword().empty()
                                                  ? QStringLiteral("references") : QString::fromStdString(element.referencesKeyword())},
            {QStringLiteral("ordered"), qualifiers.contains(QStringLiteral("ordered"))},
            {QStringLiteral("nonunique"), qualifiers.contains(QStringLiteral("nonunique"))},
            {QStringLiteral("hasPorts"), hasPorts},
            {QStringLiteral("ports"), ports},
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
                                             kind == ElementKind::ControlNode || kind == ElementKind::MetadataUsage ||
                                             kind == ElementKind::EnumerationLiteral},
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
              (kind == ElementKind::ConstraintUsage && assertion) ||
              (kind == ElementKind::EnumerationLiteral && !values.value(QStringLiteral("enumValue")).toString().trimmed().isEmpty())))
            return kind == ElementKind::EnumerationLiteral ? tr("An enumerated value needs a name or a value (enum = 60 [mm]).")
                                                           : tr("The name must not be empty.");
        if (kind == ElementKind::EnumerationLiteral && values.value(QStringLiteral("enumValue")).toString().trimmed().isEmpty() &&
            !values.value(QStringLiteral("enumUnit")).toString().trimmed().isEmpty())
            return tr("A unit belongs to a value: enter the value too (60 with unit mm).");
        if (kind == ElementKind::MetadataUsage && values.value(QStringLiteral("typeName")).toString().trimmed().isEmpty())
            return tr("Metadata needs its definition as type, e.g. Safety.");
        if (kind == ElementKind::Alias && values.contains(QStringLiteral("target"))) {
            // "alias Package2Alias for Package2": a further name of an element that exists (7.5.2)
            const QVariantMap status = targetStatus(id, values.value(QStringLiteral("target")).toString());
            if (values.value(QStringLiteral("target")).toString().trimmed().isEmpty())
                return tr("The alias needs the element it names after 'for', e.g. Package2.");
            if (!status.value(QStringLiteral("known")).toBool())
                return status.value(QStringLiteral("message")).toString();
        }
        if (requirementConstraint && !isRequirementOwner(ownerKind))
            return tr("require / assume constraint only in a requirement.");
        const auto [multiplicityValid, multiplicity] = parseMultiplicity(values.value(QStringLiteral("multiplicity")).toString());
        if (!multiplicityValid)
            return tr("Multiplicity must look like 6, 0..1 or 1..*");

        const std::string typeName = values.value(QStringLiteral("typeName")).toString().trimmed().toStdString();
        if (!typeName.empty() && !isReferenceText(typeName, true))
            return isRequirementOwner(kind)
                   ? tr("'%1' is not a type name. The type is a requirement definition (e.g. MassRequirement); "
                        "the text of the requirement goes into the field Text.").arg(QString::fromStdString(typeName))
                   : tr("'%1' is not a type name, e.g. Motor, ScalarValues::Real or ~KraftPort.")
                             .arg(QString::fromStdString(typeName));
        const bool createType = values.value(QStringLiteral("createType")).toBool() && isUnknownType(typeName);

        // ports listed in the dialog: {id (empty for a new one), name, typeName, createType}
        struct PortEntry {
            std::optional<ElementId> Id;
            std::string Name;
            std::string TypeName;
            bool CreateType = false;
        };
        std::vector<PortEntry> portEntries;
        const bool editPorts = values.contains(QStringLiteral("ports")) && acceptsFeature(*elementId, DiagramTypes::ToolPort);
        if (editPorts) {
            std::set<std::string> portNames;
            for (const QVariant &entry: values.value(QStringLiteral("ports")).toList()) {
                const QVariantMap port = entry.toMap();
                PortEntry portEntry;
                portEntry.Name = port.value(QStringLiteral("name")).toString().trimmed().toStdString();
                portEntry.TypeName = port.value(QStringLiteral("typeName")).toString().trimmed().toStdString();
                portEntry.CreateType = port.value(QStringLiteral("createType")).toBool();
                if (const auto portId = toId(port.value(QStringLiteral("id")).toString());
                    portId && Model.contains(*portId) && Model.element(*portId).owner() == *elementId &&
                    Model.element(*portId).kind() == ElementKind::PortUsage)
                    portEntry.Id = portId;
                if (portEntry.Name.empty())
                    return tr("Every port needs a name.");
                if (!portNames.insert(portEntry.Name).second)
                    return tr("There are two ports named '%1'.").arg(QString::fromStdString(portEntry.Name));
                if (!portEntry.TypeName.empty() && !isReferenceText(portEntry.TypeName, true))
                    return tr("'%1' is not a type name, e.g. FuelPort or ~FuelPort.")
                            .arg(QString::fromStdString(portEntry.TypeName));
                portEntries.push_back(std::move(portEntry));
            }
        }
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
            if (reference.trimmed().isEmpty())
                continue;
            if (!isReferenceText(reference.trimmed().toStdString(), false))
                return tr("'%1' is not a feature name, e.g. mass or Vehicle::mass.").arg(reference.trimmed());
            redefinitions.push_back(reference.trimmed().toStdString());
        }
        if (nameFromRedefinition && redefinitions.empty())
            return tr("Name it after a redefined feature: enter the feature it redefines.");
        // what it specializes / subsets: "part1", "Vehicle::mass, b"
        std::vector<std::string> specializations;
        for (const QString &reference: values.value(QStringLiteral("specializes")).toString().split(QLatin1Char(','),
                                                                                                     Qt::SkipEmptyParts)) {
            if (reference.trimmed().isEmpty())
                continue;
            if (!isReferenceText(reference.trimmed().toStdString(), false))
                return tr("'%1' is not a name, e.g. part1 or Vehicle::mass.").arg(reference.trimmed());
            specializations.push_back(reference.trimmed().toStdString());
        }
        if (const QString reference = values.value(QStringLiteral("references")).toString().trimmed();
            !reference.isEmpty() && !isReferenceText(reference.toStdString(), false))
            return tr("'%1' is not a feature name, e.g. part1 or vehicle.engine.").arg(reference);
        // the standard: abstract or variation, not both (RefPrefix)
        {
            const QStringList words = values.value(QStringLiteral("prefixes")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (words.contains(QStringLiteral("abstract")) && words.contains(QStringLiteral("variation")))
                return tr("abstract and variation exclude each other.");
        }
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
            if (createType)
                createTypeDefinition(kind, typeName, *elementId);
            if (editPorts) {
                std::vector<ElementId> kept;
                for (const auto &entry: portEntries)
                    if (entry.Id)
                        kept.push_back(*entry.Id);
                std::vector<ElementId> removed;
                for (const ElementId child: Model.children(*elementId)) {
                    const auto &element = Model.element(child);
                    if (element.kind() == ElementKind::PortUsage &&
                        std::find(kept.begin(), kept.end(), child) == kept.end())
                        removed.push_back(child);
                }
                for (const ElementId port: removed)
                    Model.removeElement(port);
                for (const auto &entry: portEntries) {
                    if (entry.CreateType)
                        createTypeDefinition(ElementKind::PortUsage, entry.TypeName, *elementId);
                    const ElementId port = entry.Id ? *entry.Id : Model.addElement(ElementKind::PortUsage, entry.Name, *elementId);
                    if (Model.element(port).name() != entry.Name)
                        Model.rename(port, entry.Name);
                    if (Model.element(port).typeName() != entry.TypeName)
                        Model.setTypeName(port, entry.TypeName);
                }
            }
            if (values.contains(QStringLiteral("redefines")) && Diagram::isUsageKind(kind) &&
                kind != ElementKind::EnumerationLiteral && kind != ElementKind::ControlNode)
                Model.setRedefinitions(*elementId, redefinitionKeyword, redefinitions, false);
            if (kind == ElementKind::EnumerationLiteral && values.contains(QStringLiteral("enumValue"))) {
                // the value first: a value without name needs it ("enum = 60 [mm];")
                const QString number = values.value(QStringLiteral("enumValue")).toString().trimmed();
                const QString unit = values.value(QStringLiteral("enumUnit")).toString().trimmed();
                const QString expression = number.isEmpty() ? QString()
                                           : unit.isEmpty() ? number
                                                            : number + QStringLiteral(" [") + unit + QLatin1Char(']');
                if (!expression.isEmpty())
                    Model.setValue(*elementId, "=", expression.toStdString());
                if (ownName != Model.element(*elementId).name())
                    Model.rename(*elementId, ownName);
                if (expression.isEmpty())
                    Model.setValue(*elementId, "=", std::string());
                Model.setDeclaredKeyword(*elementId, values.value(QStringLiteral("enumKeyword"), true).toBool()
                                                     ? std::nullopt : std::optional<std::string>(std::string()));
            }
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
            // "[1..*] ordered nonunique"
            if (values.contains(QStringLiteral("ordered")) || values.contains(QStringLiteral("nonunique"))) {
                QStringList qualifiers;
                if (values.value(QStringLiteral("ordered")).toBool())
                    qualifiers.append(QStringLiteral("ordered"));
                if (values.value(QStringLiteral("nonunique")).toBool())
                    qualifiers.append(QStringLiteral("nonunique"));
                // the order as written stays ("nonunique ordered")
                const QStringList written = QString::fromStdString(Model.element(*elementId).multiplicityQualifiers())
                                                    .split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (written.size() == qualifiers.size() &&
                    std::all_of(written.begin(), written.end(), [&](const QString &word) { return qualifiers.contains(word); }))
                    qualifiers = written;
                if (qualifiers.join(QLatin1Char(' ')).toStdString() != Model.element(*elementId).multiplicityQualifiers())
                    Model.setMultiplicityQualifiers(*elementId, qualifiers.join(QLatin1Char(' ')).toStdString());
            }
            // "part p references q;" / "::> q" (reference subsetting, 7.6.3)
            if (values.contains(QStringLiteral("references"))) {
                const std::string feature = values.value(QStringLiteral("references")).toString().trimmed().toStdString();
                const std::string keyword = values.value(QStringLiteral("referencesKeyword"), QStringLiteral("references"))
                                                    .toString().toStdString();
                if (feature != Model.element(*elementId).referencedFeature() ||
                    (!feature.empty() && keyword != Model.element(*elementId).referencesKeyword()))
                    Model.setReferencedFeature(*elementId, feature.empty() ? std::string() : keyword, feature);
            }
            if (values.contains(QStringLiteral("specializes")))
                applySpecializations(*elementId, specializations,
                                     values.value(QStringLiteral("specializationKeyword")).toString().toStdString());
        } catch (const std::exception &exception) {
            error = QString::fromStdString(exception.what());
        }
        syncNow();
        return error;
    }

    void DiagramController::applySpecializations(ElementId id, const std::vector<std::string> &references,
                                                 const std::string &keyword) {
        // elements of the model: Specialization relationships (a line where both are shown); others as written
        const auto &element = Model.element(id);
        std::vector<ElementId> wanted;
        std::vector<std::string> external;
        for (const auto &reference: references) {
            const auto target = resolveReference(Model, element.owner(), reference);
            // a line only where the rules allow one (parts, ports, items, definitions); "attribute a :> b" as written
            const bool present = target && std::any_of(Model.relationships().begin(), Model.relationships().end(),
                                                       [&](const Diagram::Relationship &relationship) {
                                                           return relationship.Kind == RelationshipKind::Specialization &&
                                                                  relationship.Source == id && relationship.Target == *target;
                                                       });
            if (target && *target != id &&
                (present || RelationshipRules::canRelate(Model, RelationshipKind::Specialization, id, *target)))
                wanted.push_back(*target);
            else
                external.push_back(reference);
        }
        std::vector<Diagram::RelationshipId> removed;
        std::vector<ElementId> present;
        for (const auto &relationship: Model.relationships()) {
            if (relationship.Kind != RelationshipKind::Specialization || relationship.Source != id)
                continue;
            if (std::find(wanted.begin(), wanted.end(), relationship.Target) == wanted.end())
                removed.push_back(relationship.Id);
            else
                present.push_back(relationship.Target);
        }
        for (const Diagram::RelationshipId relationship: removed)
            Model.removeRelationship(relationship);
        for (const ElementId target: wanted) {
            if (std::find(present.begin(), present.end(), target) == present.end())
                Model.addRelationship(RelationshipKind::Specialization, id, target);
        }
        if (external != Model.element(id).externalSpecializations())
            Model.setExternalSpecializations(id, external);
        const std::string word = keyword == ":>" ? std::string() : keyword;
        if (word != Model.element(id).specializationKeyword())
            Model.setSpecializationKeyword(id, word);
    }

    QVariantMap DiagramController::edgeDetails(const QString &key) const {
        if (!key.startsWith(QStringLiteral("r:")) && !key.startsWith(QStringLiteral("n:")))
            return {};
        const auto id = key.mid(2).section(QLatin1Char(':'), 0, 0).toULongLong();
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
        // n-ary: the further ends by name; binary connection / interface: the multiplicities of the ends
        const auto names = [&](const std::vector<ElementId> &ends) {
            QStringList list;
            for (const ElementId end: ends)
                list.append(QString::fromStdString(referenceTo(Model, Model.element(end).owner(), end)));
            return list.join(QStringLiteral(", "));
        };
        const bool connection = relationship.Kind == RelationshipKind::Connection;
        const bool multiplicities = (connection || relationship.Kind == RelationshipKind::Interface) && !relationship.Nary &&
                                    relationship.ExtraTargets.empty() && relationship.SourceFeature.empty() &&
                                    relationship.TargetFeature.empty();
        return {{QStringLiteral("kindLabel"), edgeKindName(int(toEdgeKind(relationship.Kind)))},
                {QStringLiteral("hasEnds"), connection || relationship.Kind == RelationshipKind::Dependency},
                {QStringLiteral("hasClients"), relationship.Kind == RelationshipKind::Dependency},
                {QStringLiteral("moreTargets"), names(relationship.ExtraTargets)},
                {QStringLiteral("moreSources"), names(relationship.ExtraSources)},
                {QStringLiteral("hasMultiplicities"), multiplicities},
                {QStringLiteral("sourceMultiplicity"), QString::fromStdString(relationship.SourceMultiplicity)},
                {QStringLiteral("targetMultiplicity"), QString::fromStdString(relationship.TargetMultiplicity)},
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
        if (!key.startsWith(QStringLiteral("r:")) && !key.startsWith(QStringLiteral("n:")))
            return tr("Only relationships have a name and a type.");
        const auto id = key.mid(2).section(QLatin1Char(':'), 0, 0).toULongLong();
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
                // "connect (a, b, c)", "dependency a, b to c, d": the further ends, names separated by commas
                if ((kind == RelationshipKind::Connection || kind == RelationshipKind::Dependency) &&
                    (values.contains(QStringLiteral("moreTargets")) || values.contains(QStringLiteral("moreSources")))) {
                    const auto &relationship = Model.relationship(id);
                    const ElementId scope = Model.element(relationship.Source).owner();
                    const auto resolveList = [&](const QString &text) {
                        std::vector<ElementId> ends;
                        for (const QString &name: text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                            const auto end = resolveReference(Model, scope, name.trimmed().toStdString());
                            if (!end)
                                throw Diagram::ModelError("'" + name.trimmed().toStdString() + "' is not found");
                            ends.push_back(*end);
                        }
                        return ends;
                    };
                    auto targets = resolveList(values.value(QStringLiteral("moreTargets")).toString());
                    auto sources = kind == RelationshipKind::Dependency
                                   ? resolveList(values.value(QStringLiteral("moreSources")).toString())
                                   : std::vector<ElementId>();
                    const bool nary = kind == RelationshipKind::Connection && !targets.empty();
                    Model.setExtraEnds(id, std::move(sources), std::move(targets), nary);
                }
                if ((kind == RelationshipKind::Connection || kind == RelationshipKind::Interface) &&
                    values.contains(QStringLiteral("sourceMultiplicity"))) {
                    const auto bracket = [](QString text) {
                        text = text.trimmed();
                        if (!text.isEmpty() && !text.startsWith(QLatin1Char('[')))
                            text = QLatin1Char('[') + text + QLatin1Char(']');
                        return text.toStdString();
                    };
                    Model.setEndMultiplicities(id, bracket(values.value(QStringLiteral("sourceMultiplicity")).toString()),
                                               bracket(values.value(QStringLiteral("targetMultiplicity")).toString()));
                }
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

        // bounds as expressions: "m", "1..n" (feature references, table 4 "[m]")
        static const QRegularExpression expression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_:.]*$|^[0-9]+\\s*\\.\\.\\s*[A-Za-z_][A-Za-z0-9_:.]*$|"
                                                                  "^[A-Za-z_][A-Za-z0-9_:.]*\\s*\\.\\.\\s*([0-9]+|\\*|[A-Za-z_][A-Za-z0-9_:.]*)$"));
        if (expression.match(text).hasMatch())
            return {true, Multiplicity::expression(text.simplified().toStdString())};

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
                if (!shownAsNode(child)) {
                    if (!Interconnection && emptiedByQualifiedNames(child))
                        collect(child); // its members are drawn alone
                    continue;
                }
                nodeIds.push_back(child);
                collect(child);
            }
        };
        if (Interconnection) {
            // the part shown with what is inside it
            updateContexts();
            if (const auto context = interconnectionContext()) {
                nodeIds.push_back(*context);
                collect(*context);
            }
        } else {
            // the views of the model to render the structure view by; with one: only what it exposes (and the
            // owners on the way, so the nesting stays)
            QVariantList views;
            std::function<void(ElementId)> findViews = [&](ElementId owner) {
                for (const ElementId child: Model.children(owner)) {
                    if (Model.element(child).kind() == ElementKind::ViewUsage)
                        views.append(QVariantMap{{QStringLiteral("id"), QString::number(child)},
                                                 {QStringLiteral("title"), QString::fromStdString(Model.qualifiedName(child))}});
                    findViews(child);
                }
            };
            findViews(RootId);
            if (views != Views) {
                Views = views;
                emit viewsChanged();
            }
            if (ViewElement && !Model.contains(*ViewElement)) {
                ViewElement.reset();
                emit viewChanged();
            }
            ViewKeep.clear();
            if (ViewElement) {
                for (const ElementId id: exposedElements()) {
                    for (const ElementId step: Model.ownershipPath(id))
                        ViewKeep.insert(step);
                }
                emit viewChanged(); // the info lines may have changed
            }
            collect(RootId);
        }
        // aliases in the name compartment of their element (notation "name", table 4) / as a line (○, table 3)
        std::vector<std::pair<ElementId, ElementId>> aliasLines; // alias, aliased element
        std::map<ElementId, QStringList> aliasNamesShown;
        {
            const std::set<ElementId> shown(nodeIds.begin(), nodeIds.end());
            std::function<void(ElementId)> visit = [&](ElementId owner) {
                for (const ElementId child: Model.children(owner)) {
                    const auto &element = Model.element(child);
                    if (element.kind() == ElementKind::Alias && !shown.count(child) && !Interconnection) {
                        const auto aliased = resolveReference(Model, element.owner(), element.target());
                        if (aliased && shown.count(*aliased)) {
                            if (notationOf(child, QStringLiteral("aliases")) == QLatin1String("line") &&
                                shown.count(element.owner()))
                                aliasLines.emplace_back(child, *aliased);
                            else
                                // "P::PartDefAlias1" where the alias is in another namespace than the element (table 4)
                                aliasNamesShown[*aliased].append(QString::fromStdString(
                                        element.owner() == Model.element(*aliased).owner() ? element.name()
                                                                                             : Model.qualifiedName(child)));
                        }
                    }
                    visit(child);
                }
            };
            visit(RootId);
        }
        // an import drawn as line (notation "imports") whose imported element is shown too: an «import» arrow, no node
        std::vector<std::pair<ElementId, ElementId>> importArrows; // import, imported element
        {
            const std::set<ElementId> shown(nodeIds.begin(), nodeIds.end());
            std::vector<ElementId> kept;
            for (const ElementId id: nodeIds) {
                const auto &element = Model.element(id);
                if (element.kind() == ElementKind::Import && element.owner() != RootId && shown.count(element.owner())) {
                    const auto imported = importedElement(Model, id);
                    if (imported && *imported != element.owner() && shown.count(*imported) &&
                        !Model.isAncestor(element.owner(), *imported) &&
                        notationOf(id, QStringLiteral("imports")) == QLatin1String("line")) {
                        importArrows.emplace_back(id, *imported);
                        continue;
                    }
                }
                kept.push_back(id);
            }
            nodeIds = std::move(kept);
        }
        const std::set<ElementId> wanted(nodeIds.begin(), nodeIds.end());

        // interconnection view: the ports a part inherits from its definition, on its border (as in the definition)
        std::vector<ElementId> derivedIds;
        if (!Interconnection) {
            // structure view: inherited parts as nodes of their own (notation "inherited" = "nodes", table 4
            // "Inherited Usage"): drawn only, no textual notation
            for (const ElementId id: nodeIds) {
                if (notationOf(id, QStringLiteral("inherited")) != QLatin1String("nodes"))
                    continue;
                for (const ElementId feature: inheritedFeatures(Model, id)) {
                    if (Model.element(feature).kind() != ElementKind::PartUsage || Model.element(feature).name().empty())
                        continue;
                    auto [found, added] = DerivedIds.emplace(std::make_pair(id, feature), NextDerivedId);
                    if (added) {
                        DerivedPorts[NextDerivedId] = DerivedPort{id, feature};
                        ++NextDerivedId;
                    }
                    derivedIds.push_back(found->second);
                }
            }
        }
        if (Interconnection) {
            for (const ElementId id: nodeIds) {
                if (Model.element(id).kind() != ElementKind::PartUsage)
                    continue;
                for (const ElementId feature: inheritedFeatures(Model, id)) {
                    if (Model.element(feature).kind() != ElementKind::PortUsage || Model.element(feature).name().empty())
                        continue;
                    auto [found, added] = DerivedIds.emplace(std::make_pair(id, feature), NextDerivedId);
                    if (added) {
                        DerivedPorts[NextDerivedId] = DerivedPort{id, feature};
                        ++NextDerivedId;
                    }
                    derivedIds.push_back(found->second);
                }
            }
        }
        const std::set<ElementId> derivedWanted(derivedIds.begin(), derivedIds.end());

        for (int row = Nodes->count() - 1; row >= 0; --row) {
            const auto id = Nodes->nodes()[std::size_t(row)].Id;
            if (wanted.count(id) == 0 && derivedWanted.count(id) == 0) {
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
            if (!node.Keyword.isEmpty() && node.Kind != DiagramTypes::PortNode)
                node.Keyword = withStereotypes(element, node.Keyword); // «variation part def»
            if (!Interconnection && notationApplies(id, QStringLiteral("qualified")) &&
                notationOf(id, QStringLiteral("qualified")) == QLatin1String("qualified") && !element.name().empty()) {
                // alone: "Package1::Package2::PartDef3" (table 4)
                const QString name = QString::fromStdString(element.name());
                const QString qualified = QString::fromStdString(Model.qualifiedName(id));
                const auto at = node.Title.indexOf(name);
                node.Title = at >= 0 ? node.Title.left(at) + qualified + node.Title.mid(at + name.size()) : qualified;
            }
            const bool generalsInName = !Interconnection &&
                                        notationOf(id, QStringLiteral("specialization")) == QLatin1String("name");
            QStringList generals; // "PartDef1 :> PartDef0" in the name compartment (table 4)
            if (generalsInName) {
                for (const auto &relationship: Model.relationships()) {
                    if (relationship.Kind == RelationshipKind::Specialization && relationship.Source == id &&
                        Model.contains(relationship.Target))
                        generals.append(QString::fromStdString(Model.element(relationship.Target).name()));
                }
            }
            const QString specializationKeyword = element.specializationKeyword().empty()
                                                  ? QStringLiteral(":>") : QString::fromStdString(element.specializationKeyword());
            if (Diagram::isUsageKind(element.kind()) && node.Kind != DiagramTypes::PortNode) {
                // a usage (table 4): "subsets part1" / "redefines part1 = part2" on a line of its own below the name
                QString second = generals.isEmpty() ? QString()
                                                    : specializationKeyword + QLatin1Char(' ') + generals.join(QStringLiteral(", "));
                QString redefinitions = redefinitionText(element);
                if (!redefinitions.isEmpty() && !generalsInName && !Interconnection) {
                    // drawn as redefinition line (doc figure "Redefinition"): only the line, no "redefines part2"
                    // in the name; a redefined feature that is not shown stays written in the name
                    QStringList kept;
                    for (const auto &reference: element.redefinitions()) {
                        const auto redefined = redefinedFeature(Model, id, reference);
                        if (!redefined || !wanted.count(*redefined) || *redefined == id)
                            kept.append(QString::fromStdString(reference));
                    }
                    if (kept.size() != int(element.redefinitions().size())) {
                        const QString rest = kept.isEmpty() ? QString()
                                                            : QString::fromStdString(element.redefinitionKeyword()) +
                                                                  QLatin1Char(' ') + kept.join(QStringLiteral(", "));
                        const auto at = node.Title.indexOf(QLatin1Char(' ') + redefinitions);
                        if (at > 0)
                            node.Title.replace(at, redefinitions.size() + 1, rest.isEmpty() ? QString() : QLatin1Char(' ') + rest);
                        redefinitions = rest;
                    }
                }
                if (!redefinitions.isEmpty()) {
                    const auto at = node.Title.indexOf(QLatin1Char(' ') + redefinitions);
                    if (at > 0) {
                        second += (second.isEmpty() ? QString() : QStringLiteral(" ")) + node.Title.mid(at + 1);
                        node.Title = node.Title.left(at);
                    }
                }
                if (!second.isEmpty())
                    node.Title += QLatin1Char('\n') + second;
            } else if (!generals.isEmpty()) {
                node.Title += QLatin1Char(' ') + specializationKeyword + QLatin1Char(' ') + generals.join(QStringLiteral(", "));
            }
            if (const auto aliases = aliasNamesShown.find(id); aliases != aliasNamesShown.end() && !node.Folder)
                node.Subtitle = QStringLiteral("«alias» ") + aliases->second.join(QStringLiteral(", "));
            if (notationOf(id, QStringLiteral("docs")) == QLatin1String("compartment") && !Interconnection &&
                node.Kind != DiagramTypes::PackageNode &&
                node.Kind != DiagramTypes::RequirementNode && node.Kind != DiagramTypes::CaseNode) // those have it already
                node.Compartments = docCompartment(id) + node.Compartments;
            if (AllCompartments && !Interconnection && node.Kind != DiagramTypes::PackageNode &&
                node.Kind != DiagramTypes::PortNode && node.Kind != DiagramTypes::NoteNode &&
                node.Kind != DiagramTypes::LibraryNode && element.kind() != ElementKind::Textual)
                node.Compartments += extraCompartments(id);
            else if (relationshipsAsText(id))
                addCompartment(node.Compartments, QStringLiteral("relationships"), relationshipEntries(id));
            if (Interconnection && !nodeChildren(id).empty()) {
                // a part around its parts: keyword and name on top, its parts inside
                node.Container = true;
                node.Compartments.clear();
            }
            if (nestedFeatures(id) &&
                (!nodeChildren(id).empty() || notationOf(id, QStringLiteral("inherited")) == QLatin1String("nodes")))
                node.Container = true; // table 9 "Part with graphical compartment": its parts inside, below its compartments
            if (Interconnection && element.kind() == ElementKind::PartUsage) {
                // the ports it inherits are drawn on its border (derived port nodes), not listed
                for (int c = 0; c < node.Compartments.size(); ++c) {
                    auto compartment = node.Compartments[c].toMap();
                    if (compartment.value(QStringLiteral("title")).toString() != QLatin1String("ports"))
                        continue;
                    QVariantList kept;
                    for (const auto &value: compartment.value(QStringLiteral("entries")).toList()) {
                        if (!value.toMap().value(QStringLiteral("text")).toString().startsWith(QLatin1Char('^')))
                            kept.append(value);
                    }
                    if (kept.isEmpty()) {
                        node.Compartments.removeAt(c--);
                    } else {
                        compartment[QStringLiteral("entries")] = kept;
                        node.Compartments[c] = compartment;
                    }
                }
            }
            if (Interconnection && node.Kind == DiagramTypes::PartDefinitionNode) // "part def" as context: a box
                node.Kind = DiagramTypes::PartNode;
            const int row = Nodes->indexOf(id);
            if (row >= 0) {
                Nodes->updateContent(row, node);
                continue;
            }
            if (node.Kind == DiagramTypes::PortNode) {
                PendingPositions.erase(id);
                node.Rect = portSlot(id);
                AutoPlacedPorts.insert(id);
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
        for (const ElementId id: derivedIds) {
            const auto &derived = DerivedPorts.at(id);
            DiagramNode node;
            node.Id = id;
            node.Depth = int(Model.ownershipPath(derived.Anchor).size());
            fillNode(node, Model.element(derived.Port));
            if (Model.element(derived.Port).kind() != ElementKind::PortUsage) {
                // an inherited part: "^part1 : PartDef1", marked as drawn only (no textual notation)
                node.Kind = DiagramTypes::PartNode;
                node.Title = QLatin1Char('^') + node.Title;
                node.Derived = true;
                node.Compartments.clear();
                const int row = Nodes->indexOf(id);
                if (row >= 0) {
                    Nodes->updateContent(row, node);
                    continue;
                }
                // below the part it is inherited by, beside the others
                QPointF position(40, 40);
                if (const auto *anchorNode = Nodes->find(derived.Anchor))
                    position = QPointF(anchorNode->Rect.left(), anchorNode->Rect.bottom() + 70);
                for (const auto &other: Nodes->nodes()) {
                    if (other.Kind != DiagramTypes::PackageNode && other.Kind != DiagramTypes::PortNode &&
                        other.Rect.adjusted(-12, -12, 12, 12).intersects(QRectF(position, QSizeF(DefaultPartWidth, DefaultPartHeight))))
                        position.setX(std::max(position.x(), other.Rect.right() + 24));
                }
                node.Rect = QRectF(position, QSizeF(DefaultPartWidth, DefaultPartHeight));
                Nodes->append(node);
                continue;
            }
            node.Kind = DiagramTypes::PortNode;
            node.Keyword = portDirection(Model, derived.Port);
            const int row = Nodes->indexOf(id);
            if (row >= 0) {
                Nodes->updateContent(row, node);
                continue;
            }
            node.Rect = QRectF(0, 0, PortSize, PortSize);
            Nodes->append(node);
            AutoPlacedPorts.insert(id);
            if (const int added = Nodes->indexOf(id); added >= 0)
                Nodes->setRect(added, portSlot(id)); // its slot once it is among the ports of its part
        }

        // Edges: ownership of a part by a part is drawn as composition, the rest are relationships.
        std::vector<DiagramEdge> edges;
        for (const ElementId id: nodeIds) {
            const ElementId owner = Model.element(id).owner();
            if (owner == RootId || wanted.count(owner) == 0)
                continue;
            const ElementKind ownerKind = Model.element(owner).kind();
            const ElementKind kind = Model.element(id).kind();
            // a variant drawn as node: a line with a circle with a cross at its variation (also in a variation item)
            const bool variantNode = isVariantElement(Model.element(id)) && isVariation(Model.element(owner));
            const bool composed = variantNode ||
                                  (composes(ownerKind) && (kind == ElementKind::PartUsage || kind == ElementKind::OccurrenceUsage)) ||
                                  ((isPartLike(ownerKind) || isRequirementOwner(ownerKind)) && kind == ElementKind::RequirementUsage) ||
                                  isActionTreeNode(Model, id);
            if (!composed || nestedFeatures(owner) || detachedFromOwner(id)) // inside its owner / alone: no line
                continue;
            DiagramEdge edge;
            // "ref part": a reference, no composition - a plain line
            const auto &prefixes = Model.element(id).prefixes();
            const bool reference = std::find(prefixes.begin(), prefixes.end(), "ref") != prefixes.end();
            const bool variant = variantNode || std::find(prefixes.begin(), prefixes.end(), "variant") != prefixes.end();
            edge.Kind = Diagram::isTemporalKeyword(Model.element(id).declaredKeyword()) ? DiagramTypes::Temporal
                        : variant ? DiagramTypes::VariantMembership // a circle with a cross at the variation
                        : reference ? DiagramTypes::Aggregation : DiagramTypes::Composition;
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
            if (relationship.Nary || !relationship.ExtraSources.empty() || !relationship.ExtraTargets.empty()) {
                // n-ary connection / dependency: a line from each end shown to the dot where they meet
                if (relationship.Kind == RelationshipKind::Succession || relationship.Kind == RelationshipKind::Transition)
                    continue;
                std::vector<std::pair<ElementId, int>> ends; // node, junction role
                const bool dependency = relationship.Kind == RelationshipKind::Dependency;
                const auto addEnd = [&](ElementId end, bool client) {
                    const auto node = dependency ? shownIn(end) : (wanted.count(end) ? std::optional<ElementId>(end) : std::nullopt);
                    if (node && std::none_of(ends.begin(), ends.end(), [&](const auto &other) { return other.first == *node; }))
                        ends.emplace_back(*node, dependency ? (client ? 2 : 3) : 1);
                };
                addEnd(relationship.Source, true);
                for (const ElementId end: relationship.ExtraSources)
                    addEnd(end, true);
                addEnd(relationship.Target, false);
                for (const ElementId end: relationship.ExtraTargets)
                    addEnd(end, false);
                if (ends.size() < 2)
                    continue;
                for (std::size_t k = 0; k < ends.size(); ++k) {
                    DiagramEdge edge;
                    edge.Kind = toEdgeKind(relationship.Kind);
                    edge.Source = ends[k].first;
                    edge.Target = ends[k].first;
                    edge.Junction = ends[k].second;
                    edge.Group = edgeKey(false, relationship.Id);
                    edge.Key = QStringLiteral("n:") + QString::number(relationship.Id) + QLatin1Char(':') + QString::number(k);
                    edge.Selected = SelectedEdges.count(edge.Key) > 0;
                    if (k == 0)
                        edge.Label = edgeLabel(relationship);
                    edges.push_back(edge);
                }
                continue;
            }
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
            // "connect tank.fuelOut to engine.fuelIn": to the inherited port drawn on the part
            const auto toPort = [&](std::optional<ElementId> &end, ElementId element, const std::string &feature) {
                if (!end || *end != element || feature.empty())
                    return;
                const std::string first = feature.substr(0, feature.find('.'));
                for (const ElementId id: derivedIds) {
                    const auto &derived = DerivedPorts.at(id);
                    if (derived.Anchor == element && Model.element(derived.Port).name() == first) {
                        end = id;
                        return;
                    }
                }
            };
            toPort(source, relationship.Source, relationship.SourceFeature);
            toPort(target, relationship.Target, relationship.TargetFeature);
            if (!source || !target || *source == *target)
                continue;
            // successions and transitions belong to the behavior view
            if (relationship.Kind == RelationshipKind::Succession || relationship.Kind == RelationshipKind::Transition)
                continue;
            // ":> PartDef0" written in the name of the specializing element: no line (table 4)
            if (relationship.Kind == RelationshipKind::Specialization &&
                notationOf(relationship.Source, QStringLiteral("specialization")) == QLatin1String("name"))
                continue;
            // listed in the compartment 'relationships' of its source ("specializes PartDef1"), of either end of a
            // connection ("connect to part3"): no line
            if (relationshipsAsText(relationship.Source) ||
                ((relationship.Kind == RelationshipKind::Connection || relationship.Kind == RelationshipKind::Interface) &&
                 relationshipsAsText(relationship.Target)))
                continue;
            DiagramEdge edge;
            edge.Kind = toEdgeKind(relationship.Kind);
            edge.Source = *source;
            edge.Target = *target;
            edge.Dashed = relationship.Kind == RelationshipKind::Flow && relationship.SuccessionFlow; // sflow: dashed
            edge.Key = edgeKey(false, relationship.Id);
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
            edge.Label = edgeLabel(relationship);
            if (!detail.isEmpty())
                edge.Label = (edge.Label + QLatin1Char(' ') + detail).trimmed();
            // "connect [0..1] a to [1] b": the multiplicities at the ends
            edge.SourceLabel = QString::fromStdString(relationship.SourceMultiplicity);
            edge.TargetLabel = QString::fromStdString(relationship.TargetMultiplicity);
            edges.push_back(edge);
        }
        // notes (comment, doc, rep, metadata): a dashed line to each element they are about (annotation-link)
        for (const ElementId id: nodeIds) {
            const auto &element = Model.element(id);
            const bool note = isNoteElement(Model, id);
            if (!note && element.kind() != ElementKind::MetadataUsage)
                continue;
            const std::vector<std::string> about = note ? SysMLTextImporter::describeAnnotation(element.text()).About
                                                        : element.about();
            const auto addAnnotation = [&](ElementId target, int index, const QString &label, std::optional<ElementId> entry) {
                DiagramEdge edge;
                edge.Kind = DiagramTypes::Annotation;
                edge.Source = id;
                edge.Target = target;
                edge.Key = QStringLiteral("a:") + QString::number(id) + QLatin1Char(':') + QString::number(index);
                edge.Selected = SelectedEdges.count(edge.Key) > 0;
                edge.Label = entry ? QString() : label; // at an entry the highlighted entry shows what is meant
                if (entry) {
                    // the line ends at the entry of the compartment (table 2: "Annotation"), highlighted in the node
                    edge.TargetEntry = QString::number(*entry);
                    const auto rect = EntryRects.find({target, edge.TargetEntry});
                    if (rect != EntryRects.end())
                        edge.TargetEntryRect = rect->second;
                }
                edges.push_back(edge);
            };
            for (std::size_t index = 0; index < about.size(); ++index) {
                auto annotated = resolveReference(Model, element.owner(), about[index]);
                std::optional<ElementId> node = annotated ? shownIn(*annotated) : std::nullopt;
                if (!annotated) {
                    // an inherited feature ("part1::attribute1" with attribute1 of Part1): an entry "^attribute1" of part1
                    const auto segments = SysMLTextImporter::splitReference(about[index]);
                    if (segments.size() > 1) {
                        std::string prefix;
                        for (std::size_t k = 0; k + 1 < segments.size(); ++k)
                            prefix += (k > 0 ? "::" : "") + segments[k];
                        const auto owner = resolveReference(Model, element.owner(), prefix);
                        if (owner && wanted.count(*owner)) {
                            for (const ElementId inherited: inheritedFeatures(Model, *owner)) {
                                if (Model.element(inherited).name() == segments.back()) {
                                    annotated = inherited;
                                    node = owner;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (!annotated || !node || *node == id)
                    continue;
                if (*annotated == *node) {
                    addAnnotation(*node, int(index), QString(), std::nullopt);
                    continue;
                }
                // an element inside a compartment ("part1::attribute1"): the line ends at its entry, the label names it
                QStringList names;
                if (Model.isAncestor(*node, *annotated)) {
                    for (ElementId current = *annotated; current != *node; current = Model.element(current).owner())
                        names.prepend(QString::fromStdString(Model.element(current).name()));
                } else {
                    names.append(QStringLiteral("^") + QString::fromStdString(Model.element(*annotated).name()));
                }
                addAnnotation(*node, int(index), names.join(QLatin1Char('.')), annotated);
            }
            // without "about": the owner is the annotated element (a package shows it by nesting)
            if (about.empty() && element.owner() != RootId && wanted.count(element.owner()) &&
                Model.element(element.owner()).kind() != ElementKind::Package)
                addAnnotation(element.owner(), -1, QString(), std::nullopt);
        }
        // connection def / interface def between the definitions of its ends (graphical BNF: connection-def-graphical,
        // table 11): a line with the role names and multiplicities at the ends, a dot for more than two ends
        if (!Interconnection) {
            for (const ElementId id: nodeIds) {
                const auto &definition = Model.element(id);
                if (!Diagram::isConnectionDefinitionKind(definition.kind()) && definition.kind() != ElementKind::InterfaceDefinition)
                    continue;
                std::vector<std::pair<ElementId, QString>> ends; // type shown, "role [multiplicity]"
                bool complete = true;
                for (const ElementId child: Model.children(id)) {
                    const auto &end = Model.element(child);
                    const auto &prefixes = end.prefixes();
                    if (std::find(prefixes.begin(), prefixes.end(), "end") == prefixes.end())
                        continue;
                    std::string type = end.typeName();
                    if (!type.empty() && type.front() == '~')
                        type.erase(0, 1);
                    const auto typed = type.empty() ? std::nullopt : resolveReference(Model, id, type);
                    if (!typed || !wanted.count(*typed) || *typed == id) {
                        complete = false;
                        break;
                    }
                    QString role = QString::fromStdString(end.name());
                    if (end.multiplicity().has_value())
                        role += QLatin1Char(' ') + QString::fromStdString(end.multiplicity()->toString());
                    ends.emplace_back(*typed, role);
                }
                if (!complete || ends.size() < 2)
                    continue;
                const QString label = keywordOf(definition) + QLatin1Char(' ') + QString::fromStdString(definition.name());
                if (ends.size() == 2) {
                    DiagramEdge edge;
                    edge.Kind = DiagramTypes::Association;
                    edge.Source = ends[0].first;
                    edge.Target = ends[1].first;
                    edge.Key = QStringLiteral("cd:") + QString::number(id);
                    edge.Selected = SelectedEdges.count(edge.Key) > 0;
                    edge.Label = label;
                    edge.SourceLabel = ends[0].second;
                    edge.TargetLabel = ends[1].second;
                    if (edge.Source != edge.Target)
                        edges.push_back(edge);
                    continue;
                }
                std::set<ElementId> seen;
                for (std::size_t k = 0; k < ends.size(); ++k) {
                    if (!seen.insert(ends[k].first).second)
                        continue; // one line per definition shown
                    DiagramEdge edge;
                    edge.Kind = DiagramTypes::Association;
                    edge.Source = ends[k].first;
                    edge.Target = ends[k].first;
                    edge.Junction = 1;
                    edge.Group = QStringLiteral("cd:") + QString::number(id);
                    edge.Key = QStringLiteral("cd:") + QString::number(id) + QLatin1Char(':') + QString::number(k);
                    edge.Selected = SelectedEdges.count(edge.Key) > 0;
                    edge.SourceLabel = ends[k].second;
                    if (k == 0)
                        edge.Label = label;
                    edges.push_back(edge);
                }
            }
        }
        // «expose» from a view to what it exposes, if shown (table 24: dashed, open head; «expose»*, «expose»**)
        for (const ElementId id: nodeIds) {
            if (Model.element(id).kind() != ElementKind::ViewUsage)
                continue;
            for (const ElementId child: Model.children(id)) {
                const auto &expose = Model.element(child);
                if (expose.kind() != ElementKind::Expose)
                    continue;
                const auto exposed = importedElement(Model, child);
                const auto node = exposed ? shownIn(*exposed) : std::nullopt;
                if (!node || *node == id)
                    continue;
                const QString target = QString::fromStdString(expose.target()).section(QLatin1Char('['), 0, 0);
                DiagramEdge edge;
                edge.Kind = DiagramTypes::Expose;
                edge.Source = id;
                edge.Target = *node;
                edge.Key = QStringLiteral("x:") + QString::number(child);
                edge.Selected = SelectedEdges.count(edge.Key) > 0;
                edge.Label = QStringLiteral("«expose»") + (target.endsWith(QLatin1String("::**")) ? QStringLiteral("**")
                                                           : target.endsWith(QLatin1String("::*")) ? QStringLiteral("*")
                                                                                                    : QString());
                edges.push_back(edge);
            }
        }
        // «import» arrows from the importing package
        for (const auto &[import, imported]: importArrows) {
            DiagramEdge edge;
            edge.Kind = DiagramTypes::Import;
            edge.Source = Model.element(import).owner();
            edge.Target = imported;
            edge.Key = QStringLiteral("i:") + QString::number(import);
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
            edge.Label = importLabel(Model.element(import));
            edges.push_back(edge);
        }
        // an inherited part drawn as node: composed in the part inheriting it (unless drawn inside it)
        for (const ElementId id: derivedIds) {
            const auto &derived = DerivedPorts.at(id);
            if (Model.element(derived.Port).kind() == ElementKind::PortUsage || nestedFeatures(derived.Anchor))
                continue;
            DiagramEdge edge;
            edge.Kind = DiagramTypes::Composition;
            edge.Source = derived.Anchor;
            edge.Target = id;
            edge.Key = QStringLiteral("h:") + QString::number(id);
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
            edges.push_back(edge);
        }
        // aliases as line: from the package (circle) to the element, labelled with the alias name (table 3)
        for (const auto &[alias, aliased]: aliasLines) {
            DiagramEdge edge;
            edge.Kind = DiagramTypes::AliasMembership;
            edge.Source = Model.element(alias).owner();
            edge.Target = aliased;
            edge.Key = QStringLiteral("al:") + QString::number(alias);
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
            edge.Label = QString::fromStdString(Model.element(alias).name());
            edges.push_back(edge);
        }

        for (const ElementId id: nodeIds) {
            const auto &element = Model.element(id);
            // a package beside the package owning it, a definition in a part: the ⊕ line of an owned member (table 3)
            const bool ownedDefinition = Diagram::isDefinitionKind(element.kind()) && element.owner() != RootId &&
                                         composes(Model.element(element.owner()).kind()) && !nestedFeatures(element.owner());
            if (((notationApplies(id, QStringLiteral("packages")) && detachedFromOwner(id)) || ownedDefinition) &&
                wanted.count(element.owner())) {
                DiagramEdge edge;
                edge.Kind = DiagramTypes::Membership;
                edge.Source = element.owner();
                edge.Target = id;
                edge.Key = edgeKey(true, id);
                edge.Selected = SelectedEdges.count(edge.Key) > 0;
                edges.push_back(edge);
            }
            // "defined by": a usage and its definition, both shown; only if asked for - the type is in the name of the
            // usage already ("part2 : Part2", table 3 "Package with owned members" has no line)
            if (notationOf(id, QStringLiteral("typing")) == QLatin1String("line") && Diagram::isUsageKind(element.kind()) &&
                !portAnchor(Model, id) && !element.typeName().empty()) {
                const auto type = resolveReference(Model, element.owner(), element.typeName());
                if (type && wanted.count(*type) && typesUsage(Model.element(*type).kind(), element.kind()) &&
                    !relationshipsAsText(id) && !relationshipsAsText(*type)) { // "defined by" / "defines" as text
                    DiagramEdge edge;
                    edge.Kind = DiagramTypes::DefinedBy;
                    edge.Source = id;
                    edge.Target = *type;
                    edge.Key = QStringLiteral("t:") + QString::number(id);
                    edge.Selected = SelectedEdges.count(edge.Key) > 0;
                    edges.push_back(edge);
                }
            }
            // "references action3" / "::> part1": reference subsetting, if shown (hollow triangle with four dots)
            if (!element.referencedFeature().empty() && !relationshipsAsText(id)) {
                const auto referenced = resolveReference(Model, element.owner(), element.referencedFeature());
                if (referenced && wanted.count(*referenced) && *referenced != id) {
                    DiagramEdge edge;
                    edge.Kind = DiagramTypes::ReferenceSubsetting;
                    edge.Source = id;
                    edge.Target = *referenced;
                    edge.Key = QStringLiteral("s:") + QString::number(id);
                    edge.Selected = SelectedEdges.count(edge.Key) > 0;
                    edges.push_back(edge);
                }
            }
            // ":>> eng": the inherited feature it redefines, if shown (not when written in its name, table 4)
            for (std::size_t index = 0;
                 notationOf(id, QStringLiteral("specialization")) != QLatin1String("name") && !relationshipsAsText(id) &&
                 index < element.redefinitions().size();
                 ++index) {
                const auto redefined = redefinedFeature(Model, id, element.redefinitions()[index]);
                if (!redefined || !wanted.count(*redefined) || *redefined == id)
                    continue;
                DiagramEdge edge;
                edge.Kind = DiagramTypes::Redefinition;
                edge.Source = id;
                edge.Target = *redefined;
                edge.Key = QStringLiteral("d:") + QString::number(id) + QLatin1Char(':') + QString::number(index);
                edge.Selected = SelectedEdges.count(edge.Key) > 0;
                edges.push_back(edge);
            }
            // "perform providePower;" of a part: an arrow to the performed action, if shown
            for (const ElementId child: element.ownedElements()) {
                const auto &perform = Model.element(child);
                if (perform.kind() != ElementKind::ActionPerformance ||
                    perform.performStyle() != Diagram::PerformStyle::ReferenceAction ||
                    !perform.declaredKeyword().value_or("").empty()) // "entry action1;" of a state: a state action
                    continue;
                // looked up from the owner: the perform itself carries the name of the action (as do the
                // performs of the owners: "perform providePower.generateTorque" in a part of vehicle_b)
                std::optional<ElementId> action;
                for (ElementId context = element.owner();;) {
                    action = resolveReference(Model, context, perform.name());
                    const auto segments = SysMLTextImporter::splitReference(perform.name());
                    const auto first = segments.empty() ? std::nullopt : resolveReference(Model, context, segments.front());
                    if (!first || Model.element(*first).kind() != ElementKind::ActionPerformance || context == RootId ||
                        Model.element(*first).owner() == RootId)
                        break;
                    const ElementId outer = Model.element(Model.element(*first).owner()).owner();
                    action.reset();
                    if (outer == context || (outer != RootId && !Model.isAncestor(outer, context)))
                        break; // found itself (or a perform further in): not an action that is shown
                    context = outer;
                }
                if (!action || !wanted.count(*action) || *action == id)
                    continue;
                DiagramEdge edge;
                edge.Kind = DiagramTypes::Performs;
                edge.Label = QStringLiteral("«perform»");
                edge.Source = id;
                edge.Target = *action;
                edge.Key = QStringLiteral("p:") + QString::number(child);
                edge.Selected = SelectedEdges.count(edge.Key) > 0;
                edges.push_back(edge);
            }
        }
        if (Interconnection) {
            // nesting shows the ownership: only what connects the parts and ports
            edges.erase(std::remove_if(edges.begin(), edges.end(), [](const DiagramEdge &edge) {
                return edge.Kind != DiagramTypes::Connection && edge.Kind != DiagramTypes::Flow &&
                       edge.Kind != DiagramTypes::Interface && edge.Kind != DiagramTypes::Binding &&
                       edge.Kind != DiagramTypes::ValueFlow && edge.Kind != DiagramTypes::Message;
            }), edges.end());
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
        ++ModelRevision;
        emit modelRevisionChanged();
    }

    void DiagramController::fillNode(DiagramNode &node, const Diagram::SysMLElement &element) const {
        const ElementId id = element.id();
        const QString name = QString::fromStdString(element.name());
        switch (element.kind()) {
            case ElementKind::Package:
                node.Kind = DiagramTypes::PackageNode;
                node.Title = decorate(element, name); // the tab holds the name only (table 3)
                if (!Interconnection) {
                    // packages / imports shown as text (notation "compartment"): compartments of the frame (table 3)
                    QVariantList members;
                    QVariantList packages;
                    QVariantList imports;
                    const bool membersAsText = notationOf(id, QStringLiteral("members")) == QLatin1String("compartment");
                    // "part def PartDef3" (keyword bold) at a depth below the package listed as text
                    const auto memberEntry = [&](ElementId member, int depth) {
                        const auto &memberElement = Model.element(member);
                        const QString keyword = kindText(memberElement) == QStringLiteral("statement")
                                                ? QString() : QString::fromStdString(Diagram::defaultKeyword(memberElement.kind()));
                        const QString name = Diagram::isUsageKind(memberElement.kind())
                                             ? usageText(Model, member)
                                             : QString::fromStdString(memberElement.name());
                        QVariantMap value = entry(member, (keyword + QLatin1Char(' ') + decorate(memberElement, name)).trimmed());
                        value[QStringLiteral("keyword")] = keyword;
                        value[QStringLiteral("indent")] = depth;
                        return value;
                    };
                    // a package listed as text keeps its content visible: below it, indented
                    std::function<void(QVariantList &, ElementId, int)> addContent = [&](QVariantList &list, ElementId package,
                                                                                    int depth) {
                        for (const ElementId inner: Model.children(package)) {
                            const auto &innerElement = Model.element(inner);
                            if (isListedMemberKind(innerElement.kind())) {
                                list.append(memberEntry(inner, depth));
                                if (innerElement.kind() == ElementKind::Package)
                                    addContent(list, inner, depth + 1);
                            } else if (innerElement.kind() == ElementKind::Import || innerElement.kind() == ElementKind::Alias) {
                                QVariantMap value = entry(inner, (innerElement.kind() == ElementKind::Alias ? QStringLiteral("alias ")
                                                                                                          : QStringLiteral("import ")) +
                                                                 targetText(innerElement));
                                value[QStringLiteral("indent")] = depth;
                                list.append(value);
                            }
                        }
                    };
                    for (const ElementId child: Model.children(id)) {
                        const auto &childElement = Model.element(child);
                        if (membersAsText && isPackageMemberKind(childElement.kind())) {
                            // compartment 'members': definitions and usages, "part def PartDef1" (the keyword in bold)
                            members.append(memberEntry(child, 0));
                        }
                        // compartment 'packages': the packages listed as text (graphical BNF: packages-compartment)
                        if ((membersAsText && childElement.kind() == ElementKind::Package) ||
                            (notationApplies(child, QStringLiteral("packages")) &&
                             notationOf(child, QStringLiteral("packages")) == QLatin1String("compartment"))) {
                            packages.append(memberEntry(child, 0));
                            addContent(packages, child, 1);
                        }
                        if (notationApplies(child, QStringLiteral("imports")) &&
                            notationOf(child, QStringLiteral("imports")) == QLatin1String("compartment")) {
                            // "private import Package3::*" (table 3)
                            QString text;
                            for (const auto &prefix: childElement.prefixes())
                                text += QString::fromStdString(prefix) + QLatin1Char(' ');
                            text += childElement.declaredKeyword() == "import all" ? QStringLiteral("import all ")
                                                                                     : QStringLiteral("import ");
                            imports.append(entry(child, text + QString::fromStdString(childElement.target())));
                        }
                    }
                    addCompartment(node.Compartments, QStringLiteral("members"), members);
                    addCompartment(node.Compartments, QStringLiteral("packages"), packages);
                    addCompartment(node.Compartments, QStringLiteral("imports"), imports);
                }
                return;
            case ElementKind::Textual: {
                if (isNoteStatement(element)) {
                    // table 2: a note; «comment» Name, «doc», «rep» with locale / language, the text below
                    const auto note = SysMLTextImporter::describeAnnotation(element.text());
                    node.Kind = DiagramTypes::NoteNode;
                    // a bare "/* ... */" without keyword; "comment about x /* ... */" shows «comment» (table 2)
                    const bool keyword = note.Keyword != "comment" || note.KeywordWritten;
                    node.Keyword = keyword ? QStringLiteral("«") + QString::fromStdString(note.Keyword) + QStringLiteral("»") : QString();
                    QString title = note.ShortName.empty() ? QString() : QStringLiteral("<") + QString::fromStdString(note.ShortName) +
                                                                       QStringLiteral("> ");
                    title += QString::fromStdString(note.Name);
                    if (!note.Locale.empty())
                        title += (title.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("locale \"") +
                                 QString::fromStdString(note.Locale) + QLatin1Char('"');
                    node.Title = title.trimmed();
                    QVariantList lines;
                    if (!note.Language.empty())
                        lines.append(entry(id, QStringLiteral("language \"") + QString::fromStdString(note.Language) + QLatin1Char('"')));
                    const QString full = QString::fromStdString(note.Body);
                    for (const QString &line: wrapLines(full, 44))
                        lines.append(entry(id, line, full));
                    addCompartment(node.Compartments, QString(), lines);
                    return;
                }
                // any other statement on package level: a node with what can be read from its text
                const auto summary = SysMLTextImporter::summarize(element);
                node.Kind = DiagramTypes::TextualNode;
                node.Keyword = QStringLiteral("«") + QString::fromStdString(element.keyword()) + QStringLiteral("»");
                node.Title = name.isEmpty() ? QString::fromStdString(element.keyword()) : name;
                if (element.keyword() == "dependency") {
                    // ends not found in the model ("dependency Package2 to Package1;" without these packages):
                    // the statement as written, so it is clear which ends are meant
                    QString text = QString::fromStdString(SysMLTextImporter::oneLine(element.text())).trimmed();
                    if (text.startsWith(QLatin1String("dependency")))
                        text = text.mid(10).trimmed();
                    if (text.endsWith(QLatin1Char(';')))
                        text.chop(1);
                    node.Title = text;
                }
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
                // "return : ScalarValues::Real;" (an unnamed result, kept as written): in the result compartment
                for (int c = 0; c < node.Compartments.size(); ++c) {
                    auto compartment = node.Compartments[c].toMap();
                    if (compartment.value(QStringLiteral("title")).toString() != QLatin1String("other"))
                        continue;
                    QVariantList kept;
                    for (const auto &value: compartment.value(QStringLiteral("entries")).toList()) {
                        const QString text = value.toMap().value(QStringLiteral("text")).toString();
                        if (text.startsWith(QLatin1String("return ")) || text.startsWith(QLatin1String("return:"))) {
                            auto map = value.toMap();
                            QString shown = text.mid(6).trimmed();
                            if (shown.endsWith(QLatin1Char(';')))
                                shown.chop(1);
                            map[QStringLiteral("text")] = shown; // "return" is put in front below
                            result.append(map);
                        } else {
                            kept.append(value);
                        }
                    }
                    if (kept.isEmpty()) {
                        node.Compartments.removeAt(c--);
                    } else {
                        compartment[QStringLiteral("entries")] = kept;
                        node.Compartments[c] = compartment;
                    }
                }
                // the result is a parameter too: "return f :> ISQ::force = m*a" (slides: calc def Force)
                for (auto &value: result) {
                    auto map = value.toMap();
                    map[QStringLiteral("text")] = returnText(map.value(QStringLiteral("text")).toString());
                    value = map;
                }
                QVariantList expressionEntries;
                if (!element.expression().empty()) {
                    const QString expression = QString::fromStdString(SysMLTextImporter::oneLine(element.expression()));
                    const QString full = QString::fromStdString(element.expression());
                    auto last = result.isEmpty() ? QVariantMap() : result.last().toMap();
                    if (!Diagram::isConstraintKind(kind) && !last.isEmpty() &&
                        !last.value(QStringLiteral("text")).toString().contains(QStringLiteral(" = "))) {
                        // the body computes the result
                        last[QStringLiteral("text")] = last.value(QStringLiteral("text")).toString() + QStringLiteral(" = ") + expression;
                        last[QStringLiteral("tooltip")] = full;
                        result.last() = last;
                    } else {
                        expressionEntries.append(entry(id, expression, full));
                    }
                }
                QVariantList ordered;
                if (Diagram::isConstraintKind(kind)) {
                    addCompartment(ordered, QStringLiteral("parameters"), parameters + result);
                    addCompartment(ordered, QStringLiteral("expression"), expressionEntries);
                } else {
                    // calc def / calc (table 18): the parameters, and the result compartment with what is returned
                    addCompartment(ordered, QStringLiteral("parameters"), parameters);
                    addCompartment(ordered, QStringLiteral("result"), result + expressionEntries);
                }
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
                        map[QStringLiteral("text")] = returnText(map.value(QStringLiteral("text")).toString());
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
                if (!portAnchor(Model, id)) {
                    // on package level: a usage of its own («port» fuel : FuelPort), drawn as the other usages
                    node.Kind = DiagramTypes::PartNode;
                    node.Keyword = QStringLiteral("«port»");
                    node.Title = decorate(element, usageText(Model, id), true) + specializationText(element);
                    node.Dashed = std::find(element.prefixes().begin(), element.prefixes().end(), "ref") != element.prefixes().end();
                    break;
                }
                // a port on the border of its part: "fuelIn : ~FuelPort" next to the small square
                node.Kind = DiagramTypes::PortNode;
                node.Title = decorate(element, usageText(Model, id), true) + specializationText(element);
                node.Keyword = portDirection(Model, id); // the arrow in the square
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
                if (Diagram::isBehaviorKind(element.kind())) {
                    // action / state def: "parameters  in pwrCmd : PwrCmd, out torqueToWheels : Torque[*]"
                    for (auto &value: result) {
                        auto map = value.toMap();
                        map[QStringLiteral("text")] = returnText(map.value(QStringLiteral("text")).toString());
                        value = map;
                    }
                    addCompartment(node.Compartments, QStringLiteral("parameters"), parameters + result);
                    addCompartment(node.Compartments, QStringLiteral("attributes"), attributes);
                } else if (element.kind() == ElementKind::PortDefinition) {
                    // port def: directed attributes among the directed features ("in attribute x : Real")
                    QVariantList directed;
                    for (auto value: parameters) {
                        auto map = value.toMap();
                        QString text = map.value(QStringLiteral("text")).toString();
                        for (const QString &direction: {QStringLiteral("inout "), QStringLiteral("in "), QStringLiteral("out ")}) {
                            const auto at = text.indexOf(direction);
                            if (at >= 0) {
                                text.insert(at + direction.size(), QStringLiteral("attribute "));
                                break;
                            }
                        }
                        map[QStringLiteral("text")] = text;
                        directed.append(map);
                    }
                    for (auto &compartment: rest) {
                        auto map = compartment.toMap();
                        if (map.value(QStringLiteral("title")).toString() == QLatin1String("directed features")) {
                            map[QStringLiteral("entries")] = map.value(QStringLiteral("entries")).toList() + directed;
                            compartment = map;
                            directed.clear();
                        }
                    }
                    if (!directed.isEmpty())
                        rest.prepend(QVariantMap{{QStringLiteral("title"), QStringLiteral("directed features")},
                                                 {QStringLiteral("entries"), directed}});
                    addCompartment(node.Compartments, QStringLiteral("attributes"), result + attributes);
                } else {
                    addCompartment(node.Compartments, QStringLiteral("attributes"), parameters + result + attributes);
                }
                node.Compartments += rest;
                return;
            }
            case ElementKind::MetadataUsage: {
                // table 26: a note «metadata» SafetyFeature with its values, a dashed line to what it is about
                node.Kind = DiagramTypes::NoteNode;
                node.Keyword = QStringLiteral("«metadata»");
                node.Title = decorate(element, element.name().empty() ? QString::fromStdString(element.typeName())
                                                                      : usageText(Model, id));
                QVariantList values;
                for (const ElementId child: element.ownedElements()) {
                    const auto &value = Model.element(child);
                    if (value.kind() == ElementKind::Attribute)
                        values.append(entry(child, usageText(Model, child) + valueText(value)));
                }
                addCompartment(node.Compartments, QString(), values); // "toolName = \"X\"" as in the note of table 26
                // what it is about: a dashed line where the element is shown, else listed
                QVariantList about;
                for (const auto &reference: element.about()) {
                    const auto annotated = resolveReference(Model, element.owner(), reference);
                    if (!annotated || !isNodeElement(Model, *annotated))
                        about.append(entry(id, QString::fromStdString(reference)));
                }
                addCompartment(node.Compartments, QStringLiteral("about"), about);
                return;
            }
            case ElementKind::Import:
                if (element.owner() != RootId) {
                    // the imported package (of the model or a library such as ISQ) inside the importing package: a
                    // dashed package symbol; "Package2::*" as in table 3 («private» Package3), else «import» ** ISQ
                    node.Kind = DiagramTypes::LibraryNode;
                    node.Keyword = importLabel(element);
                    if (QString::fromStdString(element.target()).section(QLatin1Char('['), 0, 0).endsWith(QLatin1String("*"))) {
                        node.Keyword.clear();
                        for (const auto &prefix: element.prefixes()) {
                            if (prefix == "private" || prefix == "protected")
                                node.Keyword = QStringLiteral("«") + QString::fromStdString(prefix) + QStringLiteral("»");
                        }
                        // "Package2::**": recursively, marked with ** (as the «import» ** arrow)
                        if (QString::fromStdString(element.target()).endsWith(QLatin1String("::**")))
                            node.Keyword = (node.Keyword + QStringLiteral(" **")).trimmed();
                    }
                    QString package = QString::fromStdString(element.target()).section(QLatin1Char('['), 0, 0);
                    for (const QLatin1String suffix: {QLatin1String("::**"), QLatin1String("::*")}) {
                        if (package.endsWith(suffix)) {
                            package.chop(suffix.size());
                            break;
                        }
                    }
                    node.Title = package;
                    return;
                }
                [[fallthrough]];
            case ElementKind::Alias:
                if (const auto aliased = resolveReference(Model, element.owner(), element.target());
                    aliased && Model.element(*aliased).kind() == ElementKind::Package) {
                    // table 3 "Package with alias member (unowned)": a package symbol, «alias» Package2Alias for Package2
                    node.Kind = DiagramTypes::DefinitionNode;
                    node.Folder = true;
                    node.Keyword = QStringLiteral("«alias»");
                    node.Title = (element.shortName().empty() ? QString()
                                  : QStringLiteral("<") + QString::fromStdString(element.shortName()) + QStringLiteral("> ")) +
                                 QString::fromStdString(element.name());
                    node.Subtitle = QStringLiteral("for ") + QString::fromStdString(element.target());
                    return;
                }
                [[fallthrough]];
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
                               : element.kind() == ElementKind::ItemUsage ? QStringLiteral("«item»")
                               : element.kind() == ElementKind::Attribute ? QStringLiteral("«attribute»")
                                                                          : QStringLiteral("«part»");
                node.Title = decorate(element, usageText(Model, id), true) + specializationText(element) + valueText(element);
                node.Dashed = std::find(element.prefixes().begin(), element.prefixes().end(), "ref") != element.prefixes().end();
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

    bool DiagramController::isDocInCompartment(ElementId id) const {
        if (!Model.contains(id))
            return false;
        const auto &element = Model.element(id);
        if (!isNoteElement(Model, id) || element.keyword() != "doc" || element.owner() == RootId)
            return false;
        // in a package frame (no compartments there) it stays a note
        const ElementId owner = element.owner();
        // (and on a port on the border of its part, a small square, it stays a note too)
        return Model.element(owner).kind() != ElementKind::Package && Model.element(owner).kind() != ElementKind::Textual &&
               !portAnchor(Model, owner) && isNodeElement(Model, owner);
    }

    QVariantList DiagramController::docCompartment(ElementId id) const {
        // "doc" / "doc Document1" / "doc locale "fr_BE"": all of them in the compartment, in the order of the text
        QVariantList entries;
        for (const ElementId child: Model.children(id)) {
            if (!isDocInCompartment(child))
                continue;
            const auto note = SysMLTextImporter::describeAnnotation(Model.element(child).text());
            QString head = QString::fromStdString(note.Name);
            if (!note.Locale.empty())
                head += (head.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("locale \"") +
                        QString::fromStdString(note.Locale) + QLatin1Char('"');
            if (!head.isEmpty())
                entries.append(entry(child, head));
            const QString full = QString::fromStdString(note.Body);
            for (const QString &line: wrapLines(full, 44))
                entries.append(entry(child, line, full));
        }
        QVariantList compartments;
        addCompartment(compartments, QStringLiteral("doc"), entries);
        return compartments;
    }

    bool DiagramController::relationshipsAsText(ElementId id) const {
        if (Interconnection || !Model.contains(id))
            return false;
        // quick answer without collecting the entries: nothing chosen as text
        const auto mode = NotationModes.find(QStringLiteral("relationships"));
        const bool individual = mode == NotationModes.end() || mode->second == QLatin1String("individual");
        if (!individual && mode->second != QLatin1String("compartment"))
            return false;
        if (individual) {
            const auto chosen = Notations.find(id);
            if (chosen == Notations.end() || chosen->second.count(QStringLiteral("relationships")) == 0 ||
                chosen->second.at(QStringLiteral("relationships")) != QLatin1String("compartment"))
                return false;
        }
        return
               notationOf(id, QStringLiteral("relationships")) == QLatin1String("compartment") &&
               notationApplies(id, QStringLiteral("relationships"));
    }

    QVariantList DiagramController::relationshipEntries(ElementId id) const {
        const auto &element = Model.element(id);
        const auto nameOf = [&](ElementId other, const std::string &feature = {}) {
            QString text = QString::fromStdString(Model.element(other).name());
            if (text.isEmpty())
                text = QString::fromStdString(Diagram::defaultKeyword(Model.element(other).kind()));
            if (!feature.empty())
                text += QLatin1Char('.') + QString::fromStdString(feature);
            return text;
        };
        // relationships of the element itself
        QVariantList relationships;
        // the keyword in bold ("defines part2", "subsets part1", "connect to part3")
        const auto add = [&](const QString &text) {
            QVariantMap value = entry(id, text);
            static const QStringList keywords = {QStringLiteral("connect to"), QStringLiteral("defined by")};
            QString keyword = text.section(QLatin1Char(' '), 0, 0);
            for (const QString &two: keywords) {
                if (text.startsWith(two + QLatin1Char(' ')))
                    keyword = two;
            }
            value[QStringLiteral("keyword")] = keyword;
            relationships.append(value);
        };
        const bool usage = Diagram::isUsageKind(element.kind());
        // a usage specializes a usage by subsetting (":>" = "subsets"), a definition a definition
        const QString specializes = usage ? QStringLiteral("subsets ") : QStringLiteral("specializes ");
        // a definition: the usages it defines ("defines part2")
        if (Diagram::isDefinitionKind(element.kind())) {
            std::vector<ElementId> defined;
            std::function<void(ElementId)> visit = [&](ElementId owner) {
                for (const ElementId child: Model.children(owner)) {
                    const auto &other = Model.element(child);
                    if (Diagram::isUsageKind(other.kind()) && !other.typeName().empty() &&
                        resolveReference(Model, other.owner(), other.typeName()) == id)
                        defined.push_back(child);
                    visit(child);
                }
            };
            visit(RootId);
            // the same name twice (part1 and partChoice::part1): qualified
            for (const ElementId child: defined) {
                const bool twice = std::count_if(defined.begin(), defined.end(), [&](ElementId other) {
                                       return Model.element(other).name() == Model.element(child).name();
                                   }) > 1;
                add(QStringLiteral("defines ") +
                    (twice ? QString::fromStdString(Model.qualifiedName(child)) : nameOf(child)));
            }
        }
        if (usage && !element.typeName().empty())
            add(QStringLiteral("defined by ") + QString::fromStdString(element.typeName()));
        for (const auto &reference: element.externalSpecializations())
            add(specializes + QString::fromStdString(reference));
        for (const auto &reference: element.redefinitions())
            add(QStringLiteral("redefines ") + QString::fromStdString(reference));
        if (!element.referencedFeature().empty())
            add(QStringLiteral("references ") + QString::fromStdString(element.referencedFeature()));
        for (const auto &relationship: Model.relationships()) {
            const bool source = relationship.Source == id ||
                                std::find(relationship.ExtraSources.begin(), relationship.ExtraSources.end(), id) !=
                                relationship.ExtraSources.end();
            const bool target = relationship.Target == id ||
                                std::find(relationship.ExtraTargets.begin(), relationship.ExtraTargets.end(), id) !=
                                relationship.ExtraTargets.end();
            if (!source && !target)
                continue;
            const QString other = source ? nameOf(relationship.Target, relationship.TargetFeature)
                                         : nameOf(relationship.Source, relationship.SourceFeature);
            switch (relationship.Kind) {
                case RelationshipKind::Specialization:
                    if (source) // only the specializing side (as in the text: no "specialized by")
                        add(specializes + other);
                    break;
                case RelationshipKind::Connection:
                case RelationshipKind::Interface:
                case RelationshipKind::Binding:
                    add(QStringLiteral("connect to ") + other);
                    break;
                case RelationshipKind::Flow:
                    add((source ? QStringLiteral("flow to ") : QStringLiteral("flow from ")) + other);
                    break;
                case RelationshipKind::Message:
                    add((source ? QStringLiteral("message to ") : QStringLiteral("message from ")) + other);
                    break;
                case RelationshipKind::Allocation:
                    add((source ? QStringLiteral("allocated to ") : QStringLiteral("allocated from ")) + other);
                    break;
                case RelationshipKind::Satisfy:
                    add((source ? QStringLiteral("satisfies ") : QStringLiteral("satisfied by ")) + other);
                    break;
                case RelationshipKind::Verify:
                    add((source ? QStringLiteral("verifies ") : QStringLiteral("verified by ")) + other);
                    break;
                case RelationshipKind::Dependency:
                    add((source ? QStringLiteral("depends on ") : QStringLiteral("dependency of ")) + other);
                    break;
                case RelationshipKind::Exhibit:
                    add((source ? QStringLiteral("exhibits ") : QStringLiteral("exhibited by ")) + other);
                    break;
                case RelationshipKind::Include:
                    add((source ? QStringLiteral("includes ") : QStringLiteral("included by ")) + other);
                    break;
                case RelationshipKind::Frame:
                    add((source ? QStringLiteral("frames ") : QStringLiteral("framed by ")) + other);
                    break;
                case RelationshipKind::Assert:
                    add((source ? QStringLiteral("asserts ") : QStringLiteral("asserted by ")) + other);
                    break;
                case RelationshipKind::Assume:
                    add((source ? QStringLiteral("assumes ") : QStringLiteral("assumed by ")) + other);
                    break;
                case RelationshipKind::Require:
                    add((source ? QStringLiteral("requires ") : QStringLiteral("required by ")) + other);
                    break;
                default:
                    break;
            }
        }
        // what it performs, and (an action) by whom it is performed
        for (const ElementId child: Model.children(id)) {
            const auto &perform = Model.element(child);
            if (perform.kind() == ElementKind::ActionPerformance && perform.performStyle() == Diagram::PerformStyle::ReferenceAction &&
                perform.declaredKeyword().value_or("").empty())
                add(QStringLiteral("performs ") + QString::fromStdString(perform.name()));
        }
        if (element.kind() == ElementKind::ActionUsage) {
            std::function<void(ElementId)> visit = [&](ElementId owner) {
                for (const ElementId child: Model.children(owner)) {
                    const auto &perform = Model.element(child);
                    if (perform.kind() == ElementKind::ActionPerformance &&
                        perform.performStyle() == Diagram::PerformStyle::ReferenceAction &&
                        perform.owner() != RootId && // looked up beside the performer (the perform carries the name)
                        resolveReference(Model, Model.element(perform.owner()).owner(), perform.name()) == id)
                        add(QStringLiteral("performed by ") + nameOf(perform.owner()));
                    visit(child);
                }
            };
            visit(RootId);
        }
        return relationships;
    }

    QVariantList DiagramController::extraCompartments(ElementId id) const {
        // graphical BNF 8.2.3: relationships-compartment (defines, specializes, connect to, performs, satisfies, ...),
        // connections / interfaces / flows / allocations / messages compartments of the element they are written in,
        // occurrences / individuals / timeslices / snapshots, variants
        QVariantList compartments;
        const auto &element = Model.element(id);
        addCompartment(compartments, QStringLiteral("relationships"), relationshipEntries(id));

        // the relationships written in its body: connections, interfaces, flows, allocations, messages
        QVariantList connections;
        QVariantList interfaces;
        QVariantList flows;
        QVariantList allocations;
        QVariantList messages;
        for (const auto &relationship: Model.relationships()) {
            if (!Model.contains(relationship.Source) || !Model.contains(relationship.Target))
                continue;
            // the innermost element owning both ends
            const auto pathSource = Model.ownershipPath(relationship.Source);
            const auto pathTarget = Model.ownershipPath(relationship.Target);
            std::size_t common = 0;
            while (common < pathSource.size() && common < pathTarget.size() && pathSource[common] == pathTarget[common])
                ++common;
            const ElementId owner = common == 0 ? RootId : pathSource[common - 1];
            const ElementId context = relationship.Context ? *relationship.Context : owner;
            if (context != id || relationship.Source == id || relationship.Target == id)
                continue;
            const auto path = [&](ElementId end, const std::string &feature) {
                QStringList names;
                for (ElementId current = end; current != id && current != RootId; current = Model.element(current).owner())
                    names.prepend(QString::fromStdString(Model.element(current).name()));
                QString text = names.join(QLatin1Char('.'));
                if (!feature.empty())
                    text += QLatin1Char('.') + QString::fromStdString(feature);
                return text;
            };
            const QString ends = path(relationship.Source, relationship.SourceFeature) + QStringLiteral(" to ") +
                                 path(relationship.Target, relationship.TargetFeature);
            const QString label = edgeLabel(relationship);
            const QString named = label.isEmpty() ? QString() : label + QLatin1Char(' ');
            switch (relationship.Kind) {
                case RelationshipKind::Connection:
                    connections.append(entry(id, named + QStringLiteral("connect ") + ends));
                    break;
                case RelationshipKind::Binding:
                    connections.append(entry(id, QStringLiteral("bind ") + path(relationship.Source, relationship.SourceFeature) +
                                                 QStringLiteral(" = ") + path(relationship.Target, relationship.TargetFeature)));
                    break;
                case RelationshipKind::Interface:
                    interfaces.append(entry(id, named + QStringLiteral("connect ") + ends));
                    break;
                case RelationshipKind::Flow:
                    flows.append(entry(id, (relationship.SuccessionFlow ? QStringLiteral("succession flow ") : QStringLiteral("flow ")) +
                                           named + QStringLiteral("from ") + ends));
                    break;
                case RelationshipKind::Allocation:
                    allocations.append(entry(id, named + QStringLiteral("allocate ") + ends));
                    break;
                case RelationshipKind::Message:
                    messages.append(entry(id, QStringLiteral("message ") + named + QStringLiteral("from ") + ends));
                    break;
                default:
                    break;
            }
        }
        addCompartment(compartments, QStringLiteral("connections"), connections);
        addCompartment(compartments, QStringLiteral("interfaces"), interfaces);
        addCompartment(compartments, QStringLiteral("flows"), flows + messages);
        addCompartment(compartments, QStringLiteral("allocations"), allocations);

        // owned occurrences by their kind, variants of a variation
        QVariantList occurrences;
        QVariantList individuals;
        QVariantList timeslices;
        QVariantList snapshots;
        for (const ElementId child: Model.children(id)) {
            const auto &owned = Model.element(child);
            const auto &prefixes = owned.prefixes();
            const QString text = usageText(Model, child);
            const auto keyword = owned.declaredKeyword().value_or("");
            if (isVariantElement(owned) && isVariation(element))
                continue; // in its compartment 'variants' already
            if (keyword.find("timeslice") != std::string::npos)
                timeslices.append(entry(child, text));
            else if (keyword.find("snapshot") != std::string::npos)
                snapshots.append(entry(child, text));
            else if (std::find(prefixes.begin(), prefixes.end(), "individual") != prefixes.end())
                individuals.append(entry(child, QString::fromStdString(Diagram::defaultKeyword(owned.kind())) + QLatin1Char(' ') + text));
            else if (owned.kind() == ElementKind::OccurrenceUsage)
                occurrences.append(entry(child, text));
        }
        addCompartment(compartments, QStringLiteral("occurrences"), occurrences);
        addCompartment(compartments, QStringLiteral("individuals"), individuals);
        addCompartment(compartments, QStringLiteral("timeslices"), timeslices);
        addCompartment(compartments, QStringLiteral("snapshots"), snapshots);
        return compartments;
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
        QVariantList directed;     // port (def): "in item x : T"
        QVariantList performs;     // "perform actions"
        QVariantList exhibits;     // "exhibit states"
        QVariantList stateActions; // "state actions": entry / do / exit
        QVariantList asserts;      // "assert constraints"
        const bool partLike = composes(element.kind());
        // notations "features" / "ports" = "compartment": the parts / ports of a part as text (table 9)
        const bool partsAsText = partLike && notationApplies(element.id(), QStringLiteral("features")) &&
                                 notationOf(element.id(), QStringLiteral("features")) == QLatin1String("compartment");
        const bool portsAsText = partLike && notationApplies(element.id(), QStringLiteral("ports")) &&
                                 notationOf(element.id(), QStringLiteral("ports")) == QLatin1String("compartment");
        const bool inheritedAsNodes = !Interconnection && partLike &&
                                      notationOf(element.id(), QStringLiteral("inherited")) == QLatin1String("nodes");
        const bool portLike = element.kind() == ElementKind::PortDefinition || element.kind() == ElementKind::PortUsage;
        const bool requirementOwner = isRequirementOwner(element.kind());
        // one owned (or, with "^", inherited) feature into its compartment
        const auto addFeature = [&](ElementId child, bool inherited) {
            const auto &childElement = Model.element(child);
            const QString mark = inherited ? QStringLiteral("^") : QString();
            switch (childElement.kind()) {
                case ElementKind::ItemUsage: {
                    QString text = mark + decorate(childElement, usageText(Model, child)) + specializationText(Model, child);
                    const std::string direction = directionOf(childElement);
                    if (portLike && !direction.empty()) {
                        // a port: "in item pwrCmd : PwrCmd" among the directed features
                        const QString word = QString::fromStdString(direction) + QLatin1Char(' ');
                        const auto at = text.indexOf(word);
                        if (at >= 0)
                            text.insert(at + word.size(), QStringLiteral("item "));
                        directed.append(entry(child, text));
                    } else {
                        items.append(entry(child, text));
                    }
                    break;
                }
                case ElementKind::PortUsage:
                    if (!partLike || inherited || portsAsText) // on a part its own ports are drawn on its border
                        ports.append(entry(child, mark + decorate(childElement, usageText(Model, child)) + specializationText(Model, child)));
                    break;
                case ElementKind::PartUsage:
                    if (inherited && inheritedAsNodes)
                        break; // drawn as node of its own (^part1)
                    if (!partLike || inherited || partsAsText) // in an item: a list; in a part: its own parts are nodes with composition
                        parts.append(entry(child, mark + decorate(childElement, usageText(Model, child)) +
                                                  specializationText(Model, child)));
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
                                               decorate(childElement, mark + usageText(Model, child))));
                    break;
                }
                case ElementKind::OccurrenceUsage:
                    if (!partLike || inherited || partsAsText) {
                        // "snapshot t0", "timeslice ownership[*]" in an item, port, action, ...
                        const std::string keyword = childElement.declaredKeyword().value_or("occurrence");
                        occurrences.append(entry(child, decorate(childElement, QString::fromStdString(keyword) +
                                                                                QLatin1Char(' ') + mark + usageText(Model, child)) +
                                                        valueText(childElement)));
                    }
                    break;
                case ElementKind::EnumerationLiteral:
                    // "red", "= 60 [mm]"
                    literals.append(entry(child, (mark + QString::fromStdString(childElement.name()) + valueText(childElement)).trimmed()));
                    break;
                case ElementKind::ActionUsage: {
                    if (!inherited && isActionTreeNode(Model, child))
                        break; // a node of its own below this one
                    // state actions "entry action1", "do send s to t"; or "action check : Check", "send s to t", "if c", ...
                    const std::string keyword = childElement.declaredKeyword().value_or("action");
                    const std::string role = keyword.substr(0, keyword.find(' '));
                    const QString form = actionFormText(childElement);
                    if (role == "entry" || role == "do" || role == "exit") {
                        stateActions.append(entry(child, QString::fromStdString(role) + QLatin1Char(' ') +
                                                         decorate(childElement, form.isEmpty() ? mark + usageText(Model, child)
                                                                                               : mark + form)));
                        break;
                    }
                    actions.append(entry(child, decorate(childElement, form.isEmpty() ? QStringLiteral("action ") + mark +
                                                                                        usageText(Model, child)
                                                                                      : mark + form)));
                    break;
                }
                case ElementKind::StateUsage:
                    if (childElement.declaredKeyword() == "exhibit state")
                        exhibits.append(entry(child, mark + decorate(childElement, usageText(Model, child))));
                    else
                        states.append(entry(child, mark + decorate(childElement, usageText(Model, child))));
                    break;
                case ElementKind::Attribute: {
                    const std::string direction = directionOf(childElement);
                    if (direction == "return") {
                        // "return" is the compartment, not a word of the entry
                        QString text = mark + usageText(Model, child) + specializationText(Model, child) + valueText(childElement);
                        QString metadata;
                        for (const auto &prefix: childElement.prefixes()) {
                            if (prefix != "return")
                                metadata += QString::fromStdString(prefix) + QLatin1Char(' ');
                        }
                        result.append(entry(child, metadata + text));
                    } else if (!direction.empty()) {
                        parameters.append(entry(child, mark + decorate(childElement, usageText(Model, child)) +
                                                       specializationText(Model, child) + valueText(childElement)));
                    } else {
                        attributes.append(entry(child, mark + decorate(childElement, usageText(Model, child)) +
                                                       specializationText(Model, child) + valueText(childElement)));
                    }
                    break;
                }
                case ElementKind::ActionPerformance: {
                    // "entry action1;" in a state; "perform providePower;" in a part
                    const std::string role = childElement.declaredKeyword().value_or("");
                    if (role == "entry" || role == "do" || role == "exit")
                        stateActions.append(entry(child, QString::fromStdString(role) + QLatin1Char(' ') +
                                                         decorate(childElement, mark + usageText(Model, child))));
                    else
                        performs.append(entry(child, mark + decorate(childElement, usageText(Model, child))));
                    break;
                }
                case ElementKind::ConstraintDefinition:
                case ElementKind::ConstraintUsage: {
                    // in a requirement: "require {...}" / "assume {...}" in the compartment constraints
                    const std::string declared = childElement.declaredKeyword().value_or("");
                    QString display = QString::fromStdString(SysMLTextImporter::summarize(Model, child).Display);
                    const QString tooltip = QString::fromStdString(generator.generate(Model, child));
                    if (requirementOwner && (declared == "require constraint" || declared == "assume constraint")) {
                        // "require {massActual <= massRequired}" in the compartment constraints
                        const QString word = QString::fromStdString(declared.substr(0, declared.find(' ')));
                        QString rest = display;
                        for (const QString &start: {word + QStringLiteral(" constraint "), word + QLatin1Char(' ')}) {
                            if (rest.startsWith(start)) {
                                rest = rest.mid(start.size());
                                break;
                            }
                        }
                        (declared == "require constraint" ? required : assumes)
                                .append(entry(child, mark + word + QLatin1Char(' ') + rest, tooltip)); // "^require {...}"
                    } else if (declared == "assert constraint" && childElement.kind() == ElementKind::ConstraintUsage) {
                        // "assert constraints": "{fuel.fuelMass <= fuelMassMax}" (or the name of the constraint)
                        QString rest = display;
                        for (const QString &start: {QStringLiteral("assert constraint "), QStringLiteral("assert ")}) {
                            if (rest.startsWith(start)) {
                                rest = rest.mid(start.size());
                                break;
                            }
                        }
                        asserts.append(entry(child, mark + rest, tooltip));
                    } else {
                        constraints.append(entry(child, mark + display, tooltip));
                    }
                    break;
                }
                case ElementKind::Subject:
                    subject.append(entry(child, mark + decorate(childElement, usageText(Model, child)) + specializationText(Model, child)));
                    break;
                case ElementKind::Actor:
                    actors.append(entry(child, mark + decorate(childElement, usageText(Model, child)) + specializationText(Model, child)));
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
                            objective.append(entry(child, decorate(childElement, QStringLiteral("objective ") + mark + usageText(Model, child)).trimmed()));
                    } else if (!partLike && !requirementOwner) {
                        parts.append(entry(child, QStringLiteral("requirement ") + decorate(childElement, mark + usageText(Model, child))));
                    }
                    break;
                case ElementKind::UseCaseUsage:
                    if (childElement.declaredKeyword() == "include use case")
                        includes.append(entry(child, mark + decorate(childElement, usageText(Model, child))));
                    else if (!partLike)
                        includes.append(entry(child, QStringLiteral("use case ") + decorate(childElement, mark + usageText(Model, child))));
                    break;
                case ElementKind::VerificationUsage:
                    if (!partLike)
                        other.append(entry(child, QStringLiteral("verification ") + decorate(childElement, mark + usageText(Model, child))));
                    break;
                case ElementKind::CalcDefinition:
                    calcs.append(entry(child, QString::fromStdString(SysMLTextImporter::summarize(Model, child).Display),
                                       QString::fromStdString(generator.generate(Model, child))));
                    break;
                case ElementKind::CalcUsage: {
                    if (childElement.typeName().empty()) {
                        // a calculation of its own: its signature "calc1(x : Real) : Real = x"
                        calcs.append(entry(child, mark + QString::fromStdString(SysMLTextImporter::summarize(Model, child).Display),
                                           QString::fromStdString(generator.generate(Model, child))));
                        break;
                    }
                    // "force : Force(m = 1500 [kg], a = 9.806 [m / s ^ 2])": the bound input values
                    QStringList arguments;
                    for (const ElementId parameter: childElement.ownedElements()) {
                        const auto &value = Model.element(parameter);
                        if (value.kind() != ElementKind::Attribute || directionOf(value) == "return" || directionOf(value).empty())
                            continue;
                        arguments.append(QString::fromStdString(value.name()) + valueText(value));
                    }
                    QString text = mark + decorate(childElement, usageText(Model, child)) + specializationText(Model, child);
                    if (!arguments.isEmpty())
                        text += QLatin1Char('(') + arguments.join(QStringLiteral(", ")) + QLatin1Char(')');
                    if (!childElement.expression().empty())
                        text += QStringLiteral(" = ") + QString::fromStdString(SysMLTextImporter::oneLine(childElement.expression()));
                    calcs.append(entry(child, text, QString::fromStdString(generator.generate(Model, child))));
                    break;
                }
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
                    views.append(entry(child, mark + decorate(childElement, usageText(Model, child))));
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
                    if (isHiddenStatement(childElement) || isNoteStatement(childElement))
                        break; // notes are nodes of their own (the doc of a requirement / case: its compartment)
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
        };
        // the variants of a variation (graphical BNF 8.2.3 "Variants Compartment" / "Variant Parts Compartment"):
        // "part1" (variant part1;), "part references part1", "part part1 : Part1"; of a variation part with
        // only variant parts: 'variant parts' with "part1 : PartDef1"
        // the inherited features first ("^attribute1", tables 4, 5, 7), then its own
        for (const ElementId child: inheritedFeatures(Model, element.id()))
            addFeature(child, true);
        QVariantList variants;
        const bool variation = isVariation(element);
        bool onlyParts = composes(element.kind());
        for (const ElementId child: element.ownedElements()) {
            if (variation && isVariantElement(Model.element(child)))
                onlyParts = onlyParts && Model.element(child).kind() == ElementKind::PartUsage;
        }
        for (const ElementId child: element.ownedElements()) {
            const auto &childElement = Model.element(child);
            if (!variation || !isVariantElement(childElement)) {
                addFeature(child, false);
                continue;
            }
            if (shownAsNode(child))
                continue; // a node with a line to the variation
            QString text;
            QString keyword;
            if (childElement.kind() == ElementKind::Textual) {
                // "variant part references part1;" -> "part references part1", the keywords in bold
                text = QString::fromStdString(childElement.text()).simplified().mid(8);
                if (text.endsWith(QLatin1Char(';')))
                    text.chop(1);
                const auto words = text.split(QLatin1Char(' '));
                if (words.size() > 1)
                    keyword = words.mid(0, words.size() - 1).join(QLatin1Char(' '));
            } else {
                // the compartment says 'variants': without the word "variant"
                QString declared = decorate(childElement, usageText(Model, child)) + specializationText(Model, child);
                declared.replace(QRegularExpression(QStringLiteral("(^|\\s)variant\\s")), QStringLiteral("\\1"));
                if (onlyParts || (childElement.typeName().empty() && !childElement.multiplicity())) {
                    text = declared; // "part1", "part1 : PartDef1"
                } else {
                    keyword = QString::fromStdString(
                            childElement.declaredKeyword().value_or(Diagram::defaultKeyword(childElement.kind())));
                    text = keyword + QLatin1Char(' ') + declared; // "part part1 : Part1"
                }
            }
            QVariantMap value = entry(child, text.trimmed());
            if (!keyword.isEmpty())
                value[QStringLiteral("keyword")] = keyword;
            variants.append(value);
        }
        addCompartment(compartments, onlyParts ? QStringLiteral("variant parts") : QStringLiteral("variants"), variants);

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
        addCompartment(compartments, QStringLiteral("include use cases"), includes);
        // in a requirement (table 20): 'require constraints' and 'assume constraints'
        addCompartment(compartments, QStringLiteral("require constraints"), required);
        addCompartment(compartments, QStringLiteral("assume constraints"), assumes);
        if (requirementOwner)
            addCompartment(compartments, QStringLiteral("constraints"), constraints);
        addCompartment(compartments, QStringLiteral("enums"), literals);
        addCompartment(compartments, QStringLiteral("directed features"), directed);
        const bool ends = element.kind() == ElementKind::InterfaceDefinition ||
                          element.kind() == ElementKind::AllocationDefinition ||
                          Diagram::isConnectionDefinitionKind(element.kind());
        // interface / allocation / connection defs: 'ends' (tables 11, 12)
        addCompartment(compartments, ends ? QStringLiteral("ends") : QStringLiteral("ports"), ports);
        addCompartment(compartments, QStringLiteral("items"), items);
        addCompartment(compartments, ends ? QStringLiteral("ends") : QStringLiteral("parts"), parts);
        addCompartment(compartments, QStringLiteral("occurrences"), occurrences);
        addCompartment(compartments, QStringLiteral("events"), events);
        addCompartment(compartments, QStringLiteral("messages"), messages);
        const bool behavior = Diagram::isBehaviorKind(element.kind());
        if (withActions || behavior)
            addCompartment(compartments, QStringLiteral("actions"), actions);
        addCompartment(compartments, QStringLiteral("perform actions"), performs);
        // entry, do, exit (own and inherited ones)
        std::stable_sort(stateActions.begin(), stateActions.end(), [](const QVariant &a, const QVariant &b) {
            const auto rank = [](const QVariant &value) {
                const QString text = value.toMap().value(QStringLiteral("text")).toString();
                return text.startsWith(QLatin1String("entry")) ? 0 : text.startsWith(QLatin1String("do")) ? 1 : 2;
            };
            return rank(a) < rank(b);
        });
        addCompartment(compartments, QStringLiteral("state actions"), stateActions);
        addCompartment(compartments, QStringLiteral("states"), states);
        addCompartment(compartments, QStringLiteral("exhibit states"), exhibits);
        addCompartment(compartments, QStringLiteral("assert constraints"), asserts);
        if (!requirementOwner)
            addCompartment(compartments, QStringLiteral("constraints"), constraints);
        addCompartment(compartments, QStringLiteral("calcs"), calcs);
        addCompartment(compartments, QStringLiteral("views"), views);
        // table 24: 'exposes', 'filters', 'rendering'
        addCompartment(compartments, QStringLiteral("exposes"), exposes);
        addCompartment(compartments, QStringLiteral("filters"), filters);
        addCompartment(compartments, QStringLiteral("rendering"), renders);
        addCompartment(compartments, QStringLiteral("imports"), imports);
        addCompartment(compartments, QStringLiteral("metadata"), metadata);
        addCompartment(compartments, QStringLiteral("other"), other);
    }

    QPointF DiagramController::defaultPositionFor(ElementId id) const {
        const ElementId owner = Model.element(id).owner();
        if (const auto *ownerNode = Nodes->find(owner)) {
            const auto &siblings = Model.children(owner);
            const auto index = static_cast<qreal>(std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
            if (isContainer(*ownerNode) || (Interconnection && Model.element(id).kind() != ElementKind::PortUsage))
                return ownerNode->Rect.topLeft() + QPointF(20 + index * 30, 50 + index * 30);
            return ownerNode->Rect.topLeft() + QPointF(std::max<qreal>(0, index * 180 - 60), ownerNode->Rect.height() + 60);
        }
        const int index = Nodes->count();
        return {40.0 + (index % 4) * 220.0, 40.0 + (index / 4) * 200.0};
    }

    // --- automatic layout ---------------------------------------------------------------------------

    namespace {
        constexpr qreal LayoutMargin = 40.0;
        constexpr qreal LayoutGap = 80.0;          ///< between nodes in a row (room for arrow labels)
        constexpr qreal LayoutRowGap = 60.0;       ///< between rows
        constexpr qreal CompositionGap = 70.0;     ///< owner part -> owned parts below
        constexpr qreal FramePadding = 24.0;
        constexpr qreal FrameTitleSpace = 44.0;
        constexpr qreal ContainerPadding = 70.0; ///< interconnection view: room for port labels inside a part
        constexpr qreal MaximumRowWidth = 1400.0;
        constexpr qreal MaximumFrameRowWidth = 900.0;
        /// Interconnection view: the parts of a part side by side (connections between their ports run left to right)
        constexpr qreal MaximumContainerRowWidth = 1600.0;

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
        if (owner != RootId && !Model.contains(owner))
            return result; // an inherited part drawn as node
        if (Interconnection && owner == RootId) {
            if (const auto context = interconnectionContext())
                result.push_back(*context);
            return result;
        }
        for (const ElementId child: Model.children(owner)) {
            if (shownAsNode(child) && !portAnchor(Model, child))
                result.push_back(child);
        }
        return result;
    }

    // --- ports on the border of their part ------------------------------------------------------------

    std::vector<ElementId> DiagramController::portsOf(ElementId anchor) const {
        std::vector<ElementId> ports;
        if (!Model.contains(anchor))
            return ports; // an inherited part drawn as node
        std::function<void(ElementId)> collect = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                if (Model.element(child).kind() != ElementKind::PortUsage)
                    continue;
                ports.push_back(child);
                collect(child);
            }
        };
        collect(anchor);
        // inherited ports drawn on it (interconnection view), after its own
        for (const auto &[id, derived]: DerivedPorts) {
            if (derived.Anchor == anchor && derivedPort(id) && Nodes->find(id) &&
                Model.element(derived.Port).kind() == ElementKind::PortUsage)
                ports.push_back(id);
        }
        return ports;
    }

    QRectF DiagramController::portSlot(ElementId port) const {
        const auto anchor = anchorOf(port);
        const auto *anchorNode = anchor ? Nodes->find(*anchor) : nullptr;
        if (!anchorNode)
            return QRectF(0, 0, PortSize, PortSize);
        // incoming ports on the left border, all others on the right, in the order of the text
        // ("in port p", or a port whose directed features all go in: "port fuelIn : ~FuelPort" with "out item fuel")
        const auto incoming = [&](ElementId id) {
            const auto &prefixes = Model.element(elementOf(id)).prefixes();
            return std::find(prefixes.begin(), prefixes.end(), "in") != prefixes.end() ||
                   portDirection(Model, elementOf(id)) == QLatin1String("in");
        };
        int index = 0;
        for (const ElementId other: portsOf(*anchor)) {
            if (other == port)
                break;
            if (incoming(other) == incoming(port))
                ++index;
        }
        const QRectF rect = anchorNode->Rect;
        // as many as fit on the side; the others continue along the bottom border (no two on the same spot)
        const int onSide = std::max(1, int((rect.height() - PortTop - PortSize / 2) / PortSpacing) + 1);
        if (index < onSide) {
            const qreal x = incoming(port) ? rect.left() : rect.right();
            const qreal y = rect.top() + PortTop + index * PortSpacing;
            return QRectF(x - PortSize / 2, y - PortSize / 2, PortSize, PortSize);
        }
        const qreal along = PortTop + (index - onSide) * PortSpacing;
        const qreal x = incoming(port) ? std::min(rect.left() + along, rect.right())
                                       : std::max(rect.right() - along, rect.left());
        return QRectF(x - PortSize / 2, rect.bottom() - PortSize / 2, PortSize, PortSize);
    }

    void DiagramController::placePorts(ElementId anchor) {
        for (const ElementId port: portsOf(anchor)) {
            const int row = Nodes->indexOf(port);
            if (row >= 0) {
                Nodes->setRect(row, portSlot(port));
                AutoPlacedPorts.insert(port);
            }
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

    QStringList DiagramController::annotatedEntries(const QString &node) const {
        QStringList entries;
        const auto id = toId(node);
        if (!id)
            return entries;
        for (const auto &edge: Edges->edges()) {
            if (edge.Target == *id && !edge.TargetEntry.isEmpty() && !entries.contains(edge.TargetEntry))
                entries.append(edge.TargetEntry);
        }
        return entries;
    }

    void DiagramController::setEntryRect(const QString &node, const QString &entry, const QRectF &rect) {
        const auto id = toId(node);
        if (!id)
            return;
        auto &stored = EntryRects[{*id, entry}];
        if (stored == rect)
            return;
        stored = rect;
        auto edges = Edges->edges();
        bool changed = false;
        for (auto &edge: edges) {
            if (edge.Target == *id && edge.TargetEntry == entry && edge.TargetEntryRect != rect) {
                edge.TargetEntryRect = rect;
                changed = true;
            }
        }
        if (changed)
            Edges->setEdges(std::move(edges));
    }

    QString DiagramController::portSide(const QString &id) const {
        const auto elementId = toNodeId(id);
        const auto anchor = elementId ? anchorOf(*elementId) : std::nullopt;
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

    // --- port labels -------------------------------------------------------------------------------

    namespace {
        constexpr qreal LabelHeight = 16.0;
        constexpr qreal LabelGap = 4.0;

        /// Width of a port label (12 px font), estimated so that all labels are placed alike.
        qreal labelWidth(const QString &text) {
            return 8.0 + 6.8 * qreal(text.size());
        }

        /// Overlap area of two rectangles (0 if they do not overlap).
        qreal overlap(const QRectF &a, const QRectF &b) {
            const QRectF both = a.intersected(b);
            return both.isEmpty() ? 0.0 : both.width() * both.height();
        }
    }

    QPointF DiagramController::portLabelOffset(const QString &id) const {
        const auto elementId = toNodeId(id);
        if (!elementId)
            return {PortSize + LabelGap, -LabelHeight + 1};
        computePortLabels();
        const auto found = LabelOffsets.find(*elementId);
        return found != LabelOffsets.end() ? found->second : QPointF(PortSize + LabelGap, -LabelHeight + 1);
    }

    qreal DiagramController::minimumNodeHeight(const QString &id) const {
        const auto elementId = toId(id);
        if (!elementId)
            return 0;
        int incoming = 0;
        int other = 0;
        for (const ElementId port: portsOf(*elementId)) {
            if (anchorOf(port) != elementId)
                continue;
            const auto &prefixes = Model.element(elementOf(port)).prefixes();
            ++(std::find(prefixes.begin(), prefixes.end(), "in") != prefixes.end() ||
               portDirection(Model, elementOf(port)) == QLatin1String("in") ? incoming : other);
        }
        const int perSide = std::max(incoming, other);
        return perSide == 0 ? 0 : PortTop + (perSide - 1) * PortSpacing + PortSize / 2 + 10;
    }

    void DiagramController::computePortLabels() const {
        if (LabelRevision == GeometryRevision)
            return;
        LabelRevision = GeometryRevision;
        LabelOffsets.clear();

        // obstacles: all port squares, all other nodes (not frames), the labels placed so far
        std::vector<const DiagramNode *> ports;
        std::vector<const DiagramNode *> others;
        for (const auto &node: Nodes->nodes()) {
            if (!Model.contains(node.Id) && !derivedPort(node.Id)) // removed from the model, not yet from the nodes
                continue;
            if (node.Kind == DiagramTypes::PortNode)
                ports.push_back(&node);
            else if (node.Kind != DiagramTypes::PackageNode)
                others.push_back(&node);
        }
        // placed in a stable order: top to bottom, left to right
        std::sort(ports.begin(), ports.end(), [](const DiagramNode *a, const DiagramNode *b) {
            return std::make_tuple(a->Rect.top(), a->Rect.left(), a->Id) < std::make_tuple(b->Rect.top(), b->Rect.left(), b->Id);
        });
        std::vector<QRectF> placed;
        for (const DiagramNode *port: ports) {
            const auto anchor = anchorOf(port->Id);
            const DiagramNode *anchorNode = anchor ? Nodes->find(*anchor) : nullptr;
            const QString side = portSide(QString::number(port->Id));
            const qreal w = labelWidth(port->Title);
            const qreal h = LabelHeight;
            const qreal s = port->Rect.width();
            // candidates relative to the square: outside first (beside the line leaving the port), inside last
            std::vector<std::pair<QPointF, bool>> candidates; // offset, inside the part
            if (side == QLatin1String("right"))
                candidates = {{{s + LabelGap, -h + 1}, false}, {{s + LabelGap, s - 1}, false},
                              {{-w - LabelGap, (s - h) / 2}, true}};
            else if (side == QLatin1String("left"))
                candidates = {{{-w - LabelGap, -h + 1}, false}, {{-w - LabelGap, s - 1}, false},
                              {{s + LabelGap, (s - h) / 2}, true}};
            else if (side == QLatin1String("top"))
                candidates = {{{s + LabelGap, s / 2 - h - 1}, false}, {{-w - LabelGap, s / 2 - h - 1}, false},
                              {{(s - w) / 2, s + 2}, true}};
            else
                candidates = {{{s + LabelGap, s / 2 + 1}, false}, {{-w - LabelGap, s / 2 + 1}, false},
                              {{(s - w) / 2, -h - 2}, true}};

            qreal bestCost = -1;
            QPointF best = candidates.front().first;
            for (std::size_t k = 0; k < candidates.size(); ++k) {
                const auto &[offset, inside] = candidates[k];
                const QRectF rect(port->Rect.topLeft() + offset, QSizeF(w, h));
                const QRectF padded = rect.adjusted(-2, -2, 2, 2);
                qreal cost = 0;
                for (const DiagramNode *other: ports)
                    if (other != port)
                        cost += overlap(padded, other->Rect);
                for (const QRectF &label: placed)
                    cost += overlap(padded, label);
                for (const DiagramNode *other: others) {
                    if (anchorNode && other == anchorNode)
                        continue;
                    cost += overlap(padded, other->Rect);
                }
                // outside the part it must not cover the part itself; inside is the last resort
                if (anchorNode && !inside)
                    cost += overlap(rect, anchorNode->Rect);
                cost += qreal(k) * 0.5; // prefer the earlier candidate on a tie
                if (inside)
                    cost += 20.0;
                if (bestCost < 0 || cost < bestCost) {
                    bestCost = cost;
                    best = offset;
                }
                if (cost < 1.0) // free
                    break;
            }
            LabelOffsets[port->Id] = best;
            placed.emplace_back(port->Rect.topLeft() + best, QSizeF(w, h));
        }
    }

    // --- types -------------------------------------------------------------------------------------

    namespace {
        struct DefinitionFor {
            ElementKind Kind;
            const char *Keyword;
        };

        /// The definition that types a usage of this kind (created for an unknown type).
        std::optional<DefinitionFor> definitionFor(ElementKind usage) {
            switch (usage) {
                case ElementKind::PartUsage:
                case ElementKind::Subject:
                case ElementKind::Actor:
                    return DefinitionFor{ElementKind::PartDefinition, "part def"};
                case ElementKind::PortUsage:
                    return DefinitionFor{ElementKind::PortDefinition, "port def"};
                case ElementKind::Attribute:
                    return DefinitionFor{ElementKind::AttributeDefinition, "attribute def"};
                case ElementKind::ItemUsage:
                    return DefinitionFor{ElementKind::ItemDefinition, "item def"};
                case ElementKind::ActionUsage:
                    return DefinitionFor{ElementKind::ActionDefinition, "action def"};
                case ElementKind::StateUsage:
                    return DefinitionFor{ElementKind::StateDefinition, "state def"};
                case ElementKind::ConstraintUsage:
                    return DefinitionFor{ElementKind::ConstraintDefinition, "constraint def"};
                case ElementKind::CalcUsage:
                    return DefinitionFor{ElementKind::CalcDefinition, "calc def"};
                case ElementKind::RequirementUsage:
                    return DefinitionFor{ElementKind::RequirementDefinition, "requirement def"};
                case ElementKind::UseCaseUsage:
                    return DefinitionFor{ElementKind::UseCaseDefinition, "use case def"};
                case ElementKind::VerificationUsage:
                    return DefinitionFor{ElementKind::VerificationDefinition, "verification def"};
                case ElementKind::AnalysisUsage:
                    return DefinitionFor{ElementKind::AnalysisDefinition, "analysis def"};
                case ElementKind::OccurrenceUsage:
                    return DefinitionFor{ElementKind::OccurrenceDefinition, "occurrence def"};
                case ElementKind::MetadataUsage:
                    return DefinitionFor{ElementKind::MetadataDefinition, "metadata def"};
                case ElementKind::ViewUsage:
                    return DefinitionFor{ElementKind::ViewDefinition, "view def"};
                default:
                    return std::nullopt;
            }
        }

        /// Definitions offered as type of a usage of this kind.
        std::vector<ElementKind> offeredDefinitions(ElementKind usage) {
            switch (usage) {
                case ElementKind::Attribute:
                    return {ElementKind::AttributeDefinition, ElementKind::EnumerationDefinition};
                case ElementKind::Subject:
                case ElementKind::Actor:
                case ElementKind::ItemUsage:
                    return {ElementKind::PartDefinition, ElementKind::ItemDefinition};
                case ElementKind::PortUsage:
                    return {ElementKind::PortDefinition};
                case ElementKind::ConstraintUsage:
                    return {ElementKind::ConstraintDefinition};
                case ElementKind::EventOccurrence:
                    return {ElementKind::OccurrenceDefinition, ElementKind::PartDefinition, ElementKind::ItemDefinition};
                default:
                    if (const auto definition = definitionFor(usage))
                        return {definition->Kind};
                    return {};
            }
        }

        /// Library types offered for attributes (and parameters).
        const std::vector<const char *> &attributeLibraryTypes() {
            static const std::vector<const char *> types = {
                    "ScalarValues::Real", "ScalarValues::Integer", "ScalarValues::Natural", "ScalarValues::Positive",
                    "ScalarValues::Rational", "ScalarValues::Boolean", "ScalarValues::String", "ISQ::MassValue",
                    "ISQ::LengthValue", "ISQ::SpeedValue", "ISQ::AccelerationValue", "ISQ::ForceValue",
                    "ISQ::PowerValue", "ISQ::EnergyValue", "ISQ::TorqueValue"};
            return types;
        }

        /// Library types that are usually written without their package (after "import ScalarValues::*").
        bool isLibraryName(const std::string &name) {
            static const std::set<std::string, std::less<>> names = {"Real", "Integer", "Natural", "Positive", "Rational",
                                                                     "Boolean", "String", "Complex", "Number", "Anything"};
            return names.count(name) > 0;
        }

        std::string keywordText(ElementKind kind) {
            static const std::map<ElementKind, const char *> keywords = {
                    {ElementKind::PartDefinition, "part def"}, {ElementKind::PortDefinition, "port def"},
                    {ElementKind::AttributeDefinition, "attribute def"}, {ElementKind::EnumerationDefinition, "enum def"},
                    {ElementKind::ItemDefinition, "item def"}, {ElementKind::ActionDefinition, "action def"},
                    {ElementKind::StateDefinition, "state def"}, {ElementKind::ConstraintDefinition, "constraint def"},
                    {ElementKind::CalcDefinition, "calc def"}, {ElementKind::RequirementDefinition, "requirement def"},
                    {ElementKind::UseCaseDefinition, "use case def"},
                    {ElementKind::VerificationDefinition, "verification def"},
                    {ElementKind::AnalysisDefinition, "analysis def"},
                    {ElementKind::OccurrenceDefinition, "occurrence def"},
                    {ElementKind::MetadataDefinition, "metadata def"}, {ElementKind::ViewDefinition, "view def"},
                    {ElementKind::InterfaceDefinition, "interface def"}};
            const auto found = keywords.find(kind);
            return found != keywords.end() ? found->second : "def";
        }
    }

    QVariantList DiagramController::typeSuggestions(const QString &id, const QString &text) const {
        const auto elementId = toId(id);
        if (!elementId)
            return {};
        return suggestionsFor(Model.element(*elementId).kind(), text);
    }

    QVariantList DiagramController::targetSuggestions(const QString &id, const QString &text) const {
        const auto elementId = toId(id);
        if (!elementId)
            return {};
        const auto &element = Model.element(*elementId);
        const ElementKind kind = element.kind();
        if (kind != ElementKind::Alias && kind != ElementKind::Import && kind != ElementKind::Expose)
            return {};
        const QString filter = text.trimmed();
        struct Candidate {
            QString Text;
            QString Detail;
            int Rank;
        };
        std::vector<Candidate> candidates;
        const auto consider = [&](const QString &name, const QString &detail) {
            int rank = 0;
            if (!filter.isEmpty()) {
                if (name.startsWith(filter, Qt::CaseInsensitive) ||
                    name.section(QStringLiteral("::"), -1).startsWith(filter, Qt::CaseInsensitive))
                    rank = 0;
                else if (name.contains(filter, Qt::CaseInsensitive))
                    rank = 1;
                else
                    return;
            }
            candidates.push_back({name, detail, rank});
        };
        std::function<void(ElementId)> collect = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &candidate = Model.element(child);
                if (child == *elementId || candidate.name().empty() || candidate.kind() == ElementKind::Textual ||
                    Diagram::isAnonymousKind(candidate.kind()) || candidate.kind() == ElementKind::Alias)
                    continue;
                const QString qualified = QString::fromStdString(Model.qualifiedName(child));
                const QString detail = QString::fromStdString(Diagram::defaultKeyword(candidate.kind()));
                const bool package = candidate.kind() == ElementKind::Package;
                if (kind == ElementKind::Alias) {
                    consider(qualified, detail);
                } else if (package) {
                    // "import P::*" (its members), "P::**" (recursively), "P" (the package itself)
                    consider(qualified + QStringLiteral("::*"), tr("members of the package"));
                    consider(qualified + QStringLiteral("::**"), tr("members, recursively"));
                    consider(qualified, detail);
                } else {
                    consider(qualified, detail);
                }
                collect(child);
            }
        };
        collect(RootId);
        if (kind == ElementKind::Import) {
            for (const char *library: {"ScalarValues::*", "ISQ::*", "SI::*", "USCustomaryUnits::*", "Quantities::*",
                                       "MeasurementReferences::*", "ISQBase::*", "ISQSpaceTime::*", "ISQMechanics::*"})
                consider(QString::fromLatin1(library), tr("library"));
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.Rank < b.Rank; });
        QVariantList result;
        for (const auto &candidate: candidates) {
            if (result.size() >= 15)
                break;
            if (candidate.Text == filter)
                continue;
            result.append(QVariantMap{{QStringLiteral("text"), candidate.Text}, {QStringLiteral("detail"), candidate.Detail}});
        }
        return result;
    }

    QVariantMap DiagramController::targetStatus(const QString &id, const QString &text) const {
        const auto elementId = toId(id);
        if (!elementId)
            return {{QStringLiteral("known"), true}};
        const auto &element = Model.element(*elementId);
        const std::string target = text.trimmed().toStdString();
        if (element.kind() != ElementKind::Alias || target.empty())
            return {{QStringLiteral("known"), true}};
        if (target.find('*') != std::string::npos)
            return {{QStringLiteral("known"), false},
                    {QStringLiteral("message"), tr("An alias names one element (e.g. Package2), no '*' - that is an import.")}};
        // a library (ScalarValues::Real, ISQ::TorqueValue) is not in the model: taken as it is
        static const std::set<std::string> libraries = {"ScalarValues", "ISQ", "SI", "USCustomaryUnits", "Quantities",
                                                        "MeasurementReferences", "ISQBase", "ISQSpaceTime", "ISQMechanics",
                                                        "Base", "ScalarFunctions", "Collections", "Time", "SIPrefixes"};
        const auto segments = SysMLTextImporter::splitReference(target);
        if (segments.size() > 1 && libraries.count(segments.front()) > 0)
            return {{QStringLiteral("known"), true}};
        const auto resolved = resolveReference(Model, element.owner(), target);
        if (!resolved || *resolved == *elementId)
            return {{QStringLiteral("known"), false},
                    {QStringLiteral("message"), tr("'%1' is no element of the model: an alias is a further name of an element "
                                                   "that exists - choose one from the list.").arg(text.trimmed())}};
        return {{QStringLiteral("known"), true}};
    }

    QVariantList DiagramController::portTypeSuggestions(const QString &text) const {
        return suggestionsFor(ElementKind::PortUsage, text);
    }

    QVariantMap DiagramController::typeStatus(const QString &id, const QString &text) const {
        const auto elementId = toId(id);
        if (!elementId)
            return {{QStringLiteral("known"), true}};
        return statusFor(Model.element(*elementId).kind(), text);
    }

    QVariantMap DiagramController::portTypeStatus(const QString &text) const {
        return statusFor(ElementKind::PortUsage, text);
    }

    QVariantList DiagramController::suggestionsFor(ElementKind usage, const QString &text) const {
        QString filter = text.trimmed();
        QString prefix;
        if (usage == ElementKind::PortUsage && filter.startsWith(QLatin1Char('~'))) {
            prefix = QStringLiteral("~");
            filter = filter.mid(1);
        }
        const auto kinds = offeredDefinitions(usage);
        struct Candidate {
            QString Text;
            QString Detail;
            int Rank; // 0 starts with the input, 1 contains it
            bool Library;
        };
        std::vector<Candidate> candidates;
        const auto consider = [&](const QString &name, const QString &detail, bool library) {
            if (name.isEmpty())
                return;
            const QString shortName = name.section(QStringLiteral("::"), -1);
            int rank = 0;
            if (!filter.isEmpty()) {
                if (shortName.startsWith(filter, Qt::CaseInsensitive) || name.startsWith(filter, Qt::CaseInsensitive))
                    rank = 0;
                else if (name.contains(filter, Qt::CaseInsensitive))
                    rank = 1;
                else
                    return;
            }
            for (const auto &existing: candidates)
                if (existing.Text == prefix + name)
                    return;
            candidates.push_back({prefix + name, detail, rank, library});
        };

        // definitions of the model: by name, qualified where the name is not unique
        std::map<std::string, int> nameCount;
        std::vector<ElementId> definitions;
        std::function<void(ElementId)> collect = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                if (std::find(kinds.begin(), kinds.end(), element.kind()) != kinds.end() && !element.name().empty()) {
                    definitions.push_back(child);
                    ++nameCount[element.name()];
                }
                collect(child);
            }
        };
        collect(RootId);
        for (const ElementId definition: definitions) {
            const auto &element = Model.element(definition);
            const std::string name = nameCount[element.name()] > 1 ? Model.qualifiedName(definition)
                                                                   : Diagram::SysMLCodeGenerator::formatName(element.name());
            consider(QString::fromStdString(name), QString::fromStdString(keywordText(element.kind())), false);
        }
        if (usage == ElementKind::Attribute || usage == ElementKind::Subject)
            for (const char *type: attributeLibraryTypes())
                consider(QString::fromLatin1(type), tr("library"), true);

        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
            return std::tie(a.Rank, a.Library) < std::tie(b.Rank, b.Library);
        });
        QVariantList result;
        for (const auto &candidate: candidates) {
            if (result.size() >= 12)
                break;
            // the exact input itself is no suggestion
            if (candidate.Text == text.trimmed())
                continue;
            result.append(QVariantMap{{QStringLiteral("text"), candidate.Text}, {QStringLiteral("detail"), candidate.Detail}});
        }
        return result;
    }

    bool DiagramController::isUnknownType(const std::string &typeName) const {
        std::string name = typeName;
        if (!name.empty() && name.front() == '~')
            name.erase(0, 1);
        if (name.empty() || !isReferenceText(name, false))
            return false;
        const auto segments = Diagram::SysMLTextImporter::splitReference(name);
        if (segments.size() != 1)
            return false; // qualified: a library or another package, which the model may not contain
        if (isLibraryName(segments.front()))
            return false;
        bool found = false;
        std::function<void(ElementId)> search = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                if (found)
                    return;
                const auto &element = Model.element(child);
                const bool definition = Diagram::isDefinitionKind(element.kind()) || element.kind() == ElementKind::Alias ||
                                        (element.kind() == ElementKind::Textual &&
                                         element.keyword().size() >= 3 &&
                                         element.keyword().compare(element.keyword().size() - 3, 3, "def") == 0);
                if (definition && element.name() == segments.front()) {
                    found = true;
                    return;
                }
                search(child);
            }
        };
        search(RootId);
        return !found;
    }

    QVariantMap DiagramController::statusFor(ElementKind usage, const QString &text) const {
        const std::string typeName = text.trimmed().toStdString();
        const auto definition = definitionFor(usage);
        if (!definition || !isUnknownType(typeName))
            return {{QStringLiteral("known"), true}};
        std::string name = typeName;
        if (name.front() == '~')
            name.erase(0, 1);
        return {{QStringLiteral("known"), false},
                {QStringLiteral("name"), QString::fromStdString(Diagram::SysMLTextImporter::splitReference(name).front())},
                {QStringLiteral("keyword"), QString::fromLatin1(definition->Keyword)}};
    }

    void DiagramController::createTypeDefinition(ElementKind usage, const std::string &typeName, ElementId near) {
        const auto definition = definitionFor(usage);
        if (!definition || !isUnknownType(typeName))
            return;
        std::string name = typeName;
        if (name.front() == '~')
            name.erase(0, 1);
        name = Diagram::SysMLTextImporter::splitReference(name).front();
        // in the package of the element (or on the top level)
        ElementId owner = Model.contains(near) ? Model.element(near).owner() : RootId;
        while (owner != RootId && Model.element(owner).kind() != ElementKind::Package)
            owner = Model.element(owner).owner();
        if (owner != RootId && !RelationshipRules::canOwn(Model, owner, definition->Kind))
            owner = RootId;
        const ElementId created = Model.addElement(definition->Kind, name, owner);
        // beside the other definitions: in the top row of the package (or the canvas), right of what is there
        const DiagramNode *ownerNode = owner != RootId ? Nodes->find(owner) : nullptr;
        QPointF position = ownerNode ? ownerNode->Rect.topLeft() + QPointF(24, 44) : QPointF(40, 40);
        const qreal rowTop = position.y();
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind == DiagramTypes::PortNode || node.Id == owner || !Model.contains(node.Id) ||
                (owner != RootId && !Model.isAncestor(owner, node.Id)))
                continue;
            if (node.Rect.top() < rowTop + 60 && node.Rect.bottom() > rowTop - 10)
                position.setX(std::max(position.x(), node.Rect.right() + 30));
        }
        PendingPositions[created] = position;
    }

    QSizeF DiagramController::estimatedSize(const DiagramNode &node) const {
        if ((Measured.count(node.Id) > 0 && !node.Container) || node.Kind == DiagramTypes::PortNode)
            return node.Rect.size();
        // Same structure as NodeDelegate: keyword, title, compartments with caption and entries.
        qreal longest = std::max(node.Keyword.size() * 7.0, node.Title.size() * 8.5);
        if (node.Kind == DiagramTypes::LibraryNode)
            return {std::max<qreal>(140, longest + 16), 66};
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
        const auto children = layoutChildren(id);
        std::vector<QSizeF> childSizes;
        for (const ElementId child: children)
            childSizes.push_back(measure(child, extents, framePositions));

        QSizeF extent;
        if (node->Kind == DiagramTypes::PackageNode) {
            std::vector<QPointF> positions;
            const QSizeF content = flow(childSizes, MaximumFrameRowWidth, positions);
            framePositions[id] = positions;
            const qreal header = FrameTitleSpace + frameHeaderExtra(*node); // below its compartments packages / imports
            extent = QSizeF(std::max(DefaultFrameWidth * 0.75, content.width() + 2 * FramePadding),
                            std::max(DefaultFrameHeight * 0.6, content.height() + header + FramePadding));
        } else if ((Interconnection || node->Container) && !children.empty()) {
            // a part around its parts; room for the labels of the ports on its border and on theirs
            std::vector<QPointF> positions;
            const QSizeF content = flow(childSizes, MaximumContainerRowWidth, positions);
            framePositions[id] = positions;
            const QSizeF own = estimatedSize(*node);
            // structure view: below its name and compartments (table 9 "Part with graphical compartment")
            const qreal header = Interconnection ? FrameTitleSpace : own.height() + 8;
            const qreal padding = Interconnection ? ContainerPadding : FramePadding;
            extent = QSizeF(std::max(own.width(), content.width() + 2 * padding),
                            std::max(own.height(), content.height() + header + padding));
            extent.setHeight(std::max(extent.height(), PortTop + qreal(portsOf(id).size()) * PortSpacing / 2 + 8));
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
        const auto children = layoutChildren(id);

        if (node.Kind == DiagramTypes::PackageNode || ((Interconnection || node.Container) && !children.empty())) {
            const bool package = node.Kind == DiagramTypes::PackageNode;
            const qreal header = package ? FrameTitleSpace + frameHeaderExtra(node)
                                 : Interconnection ? FrameTitleSpace : estimatedSize(node).height() + 8;
            Nodes->setRect(row, QRectF(topLeft, extent));
            const auto &positions = framePositions.at(id);
            const qreal padding = package || !Interconnection ? FramePadding : ContainerPadding;
            for (std::size_t i = 0; i < children.size() && i < positions.size(); ++i)
                place(children[i], topLeft + QPointF(padding, header) + positions[i], extents, framePositions);
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
        const auto roots = layoutChildren(RootId);
        std::vector<QSizeF> sizes;
        for (const ElementId id: roots)
            sizes.push_back(measure(id, extents, framePositions));
        std::vector<QPointF> positions;
        flow(sizes, MaximumRowWidth, positions);
        for (std::size_t i = 0; i < roots.size(); ++i)
            place(roots[i], QPointF(LayoutMargin, LayoutMargin) + positions[i] +
                            (ViewElement && !Interconnection ? QPointF(30, 100) : QPointF()), // room for the view frame
                  extents, framePositions);
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
        // packages are numbered through the whole model (Package1, Package2, ... also when nested): a package moved out
        // of another one never meets one of the same name
        std::set<std::string> used;
        if (base == "Package") {
            std::function<void(ElementId)> collect = [&](ElementId parent) {
                for (const ElementId child: Model.children(parent)) {
                    used.insert(Model.element(child).name());
                    collect(child);
                }
            };
            collect(RootId);
        } else {
            for (const ElementId sibling: Model.children(owner))
                used.insert(Model.element(sibling).name());
        }
        for (int index = 1;; ++index) {
            const std::string candidate = base + std::to_string(index);
            if (used.count(candidate) == 0)
                return candidate;
        }
    }
}
