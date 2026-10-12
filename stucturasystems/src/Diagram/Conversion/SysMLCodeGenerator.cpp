//
// Semantic model -> SysML v2 textual notation (graphic -> text direction).
//

#include "SysMLCodeGenerator.h"
#include "../Semantic/RelationshipRules.h"

#include <algorithm>
#include <optional>
#include <iterator>
#include <string_view>
#include <utility>

namespace StructuraSystems::Diagram {

    namespace {
        // Reserved words of the SysML v2 textual notation.
        constexpr std::string_view Keywords[] = {
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
                "protected", "public", "redefines", "ref", "references", "render", "rendering", "rep",
                "require", "requirement", "return", "satisfy", "send", "snapshot", "specializes",
                "stakeholder", "standard", "state", "subject", "subsets", "succession", "terminate",
                "then", "timeslice", "to", "transition", "true", "until", "use", "variant", "variation",
                "verification", "verify", "via", "view", "viewpoint", "when", "while", "xor"};

        bool isKeyword(std::string_view word) {
            return std::find(std::begin(Keywords), std::end(Keywords), word) != std::end(Keywords);
        }

        bool isBasicIdentifier(std::string_view name) {
            if (name.empty())
                return false;
            const auto isLetter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
            const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
            if (!isLetter(name.front()))
                return false;
            return std::all_of(name.begin(), name.end(), [&](char c) { return isLetter(c) || isDigit(c); });
        }

        /// Innermost element owning both a and b (RootId if they only share the top level).
        ElementId commonOwner(const SemanticModel &model, ElementId a, ElementId b) {
            const auto pathA = model.ownershipPath(a);
            const auto pathB = model.ownershipPath(b);
            ElementId common = RootId;
            // Exclude the elements themselves: the owner must strictly contain both ends.
            for (std::size_t i = 0; i + 1 < pathA.size() && i + 1 < pathB.size(); ++i) {
                if (pathA[i] != pathB[i])
                    break;
                common = pathA[i];
            }
            return common;
        }

        bool isImplicitControl(const SysMLElement &element) {
            return element.kind() == ElementKind::ControlNode && element.declaredKeyword().has_value() &&
                   isImplicitControlKeyword(*element.declaredKeyword());
        }

        /// Reference to 'target' as seen from inside 'context' (RootId = top level):
        /// usages are chained with '.', namespaces (packages, definitions) with '::'.
        std::string referenceFrom(const SemanticModel &model, ElementId context, ElementId target) {
            const auto path = model.ownershipPath(target);
            auto start = path.begin();
            if (context == target) // the body it is written in: by its name, as seen from outside
                context = model.element(target).owner();
            if (context != RootId) {
                const auto contextPosition = std::find(path.begin(), path.end(), context);
                if (contextPosition != path.end())
                    start = contextPosition + 1;
            }

            if (isImplicitControl(model.element(target)))
                return *model.element(target).declaredKeyword(); // start, done, entry
            std::string reference;
            ElementKind previousKind = ElementKind::Package;
            for (auto it = start; it != path.end(); ++it) {
                const auto &element = model.element(*it);
                if (!reference.empty())
                    reference += isUsageKind(previousKind) ? "." : "::";
                reference += SysMLCodeGenerator::formatName(element.name());
                previousKind = element.kind();
            }
            return reference;
        }

        /// Reference used after ":>" – short name if both live in the same owner, else qualified.
        std::string specializationReference(const SemanticModel &model, ElementId source, ElementId target) {
            const auto &targetElement = model.element(target);
            if (model.element(source).owner() == targetElement.owner())
                return SysMLCodeGenerator::formatName(targetElement.name());
            std::string qualified;
            for (const ElementId id: model.ownershipPath(target)) {
                if (!qualified.empty())
                    qualified += "::";
                qualified += SysMLCodeGenerator::formatName(model.element(id).name());
            }
            return qualified;
        }

        /// An unnamed succession within one body is written as "then b;" right after its source
        /// (between events of different lifelines: "first a.x then b.y;" in the common owner).
        bool isShorthand(const SemanticModel &model, const Relationship &relationship) {
            return relationship.Kind == RelationshipKind::Succession && relationship.Name.empty() &&
                   model.element(relationship.Source).owner() == model.element(relationship.Target).owner();
        }

        /// "event occurrence a; then event occurrence b;", "snapshot t0; then snapshot t1;": the succession to an
        /// event / occurrence declared right after its source is written in front of that declaration ("then" +
        /// declaration) instead of as "then b;".
        std::optional<RelationshipId> mergedThen(const SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            const bool temporal = (element.kind() == ElementKind::PartUsage || element.kind() == ElementKind::ItemUsage) &&
                                  isTemporalKeyword(element.declaredKeyword());
            // actions and control nodes too: "then action b;", "then send s to t;", "then if c { ... }", "then fork;"
            // (named ones only if declared so: "then b; action b;" is kept as well)
            const bool unnamed = element.name().empty();
            const bool actionNode = ((element.kind() == ElementKind::ActionUsage && !element.isElseBranch() &&
                                      !element.declaredKeyword().has_value()) ||
                                     (element.kind() == ElementKind::ControlNode && !isImplicitControl(element))) &&
                                    (unnamed || element.declaredWithThen());
            if (element.kind() != ElementKind::EventOccurrence && element.kind() != ElementKind::OccurrenceUsage &&
                !temporal && !actionNode)
                return std::nullopt;
            // the source: the nearest element before it that a succession can start at (as "then" in the text
            // continues from the last action / event declared before)
            const auto previousEnd = RelationshipRules::previousSuccessionEnd(model, id);
            if (!previousEnd)
                return std::nullopt;
            const ElementId previous = *previousEnd;
            for (const auto &relationship: model.relationships()) {
                if (isShorthand(model, relationship) && relationship.Source == previous && relationship.Target == id &&
                    relationship.Guard.empty())
                    return relationship.Id;
            }
            return std::nullopt;
        }

        bool isObjective(const SysMLElement &element) {
            return element.kind() == ElementKind::RequirementUsage && element.declaredKeyword() == "objective";
        }

        /// The objective the "verify" statements of a verification are written into (the first one).
        std::optional<ElementId> objectiveOf(const SemanticModel &model, ElementId verification) {
            for (const ElementId child: model.children(verification)) {
                if (isObjective(model.element(child)))
                    return child;
            }
            return std::nullopt;
        }

        bool hasRelationshipFrom(const SemanticModel &model, RelationshipKind kind, ElementId source) {
            return std::any_of(model.relationships().begin(), model.relationships().end(),
                               [&](const Relationship &relationship) {
                                   return relationship.Kind == kind && relationship.Source == source;
                               });
        }

        bool isOwnedByContext(const SemanticModel &model, const Relationship &relationship, ElementId context) {
            // verify / include are written into their source (see generateElement), refining allocations into the
            // body of the allocation they refine
            if (relationship.Kind == RelationshipKind::Specialization || isShorthand(model, relationship) ||
                relationship.Kind == RelationshipKind::Verify || relationship.Kind == RelationshipKind::Include ||
                relationship.Kind == RelationshipKind::Frame || relationship.Parent.has_value())
                return false;
            if (relationship.Context)
                return *relationship.Context == context;
            return commonOwner(model, relationship.Source, relationship.Target) == context;
        }

    }

    SysMLCodeGenerator::SysMLCodeGenerator(Options options) :
            GeneratorOptions(std::move(options)) {
    }

    void SysMLCodeGenerator::generateSuccessionsFrom(const SemanticModel &model, ElementId source, std::size_t depth,
                                                     std::string &out) const {
        const ElementId owner = model.element(source).owner();
        for (const auto &relationship: model.relationships()) {
            if (!isShorthand(model, relationship) || relationship.Source != source)
                continue;
            if (mergedThen(model, relationship.Target) == relationship.Id)
                continue; // "then event occurrence b;"

            const std::string target = referenceFrom(model, owner, relationship.Target);
            if (target.empty())
                continue; // an unnamed action that is not declared right behind its source cannot be referenced
            if (relationship.Guard.empty())
                out += indent(depth) + "then " + target + ";\n";
            else if (relationship.Guard == "else")
                out += indent(depth) + "else " + target + ";\n";
            else
                out += indent(depth) + "if " + relationship.Guard + " then " + target + ";\n";
        }
    }

    std::string SysMLCodeGenerator::generate(const SemanticModel &model) const {
        std::string out;
        for (const ElementId id: model.children(RootId)) {
            generateElement(model, id, 0, out);
            generateSuccessionsFrom(model, id, 0, out);
        }
        generateOwnedRelationships(model, RootId, 0, out);
        return out;
    }

    std::string SysMLCodeGenerator::generate(const SemanticModel &model, ElementId id) const {
        std::string out;
        if (id != RootId && model.contains(id))
            generateElement(model, id, 0, out);
        while (!out.empty() && out.back() == '\n')
            out.pop_back();
        return out;
    }

    std::string SysMLCodeGenerator::formatName(const std::string &name) {
        if (isBasicIdentifier(name) && !isKeyword(name))
            return name;
        std::string quoted = "'";
        for (const char c: name) {
            if (c == '\'' || c == '\\')
                quoted += '\\';
            quoted += c;
        }
        quoted += '\'';
        return quoted;
    }

    void SysMLCodeGenerator::generateElement(const SemanticModel &model, ElementId id, std::size_t depth,
                                             std::string &out) const {
        const auto &element = model.element(id);
        if (element.kind() == ElementKind::Textual) {
            // Kept word for word; only the indentation follows the owner.
            std::size_t start = 0;
            const std::string &text = element.text();
            while (start <= text.size()) {
                std::size_t end = text.find('\n', start);
                if (end == std::string::npos)
                    end = text.size();
                const std::string line = text.substr(start, end - start);
                out += line.empty() ? std::string() : indent(depth) + line;
                out += '\n';
                start = end + 1;
            }
            return;
        }
        if (isImplicitControl(element)) {
            // not declared: "first start;" / "entry;" only to start the successions that follow; "done" not at all
            const std::string &keyword = *element.declaredKeyword();
            const bool used = std::any_of(model.relationships().begin(), model.relationships().end(),
                                          [&](const Relationship &relationship) {
                                              return isShorthand(model, relationship) && relationship.Source == id;
                                          });
            if (keyword == "start" && used)
                out += indent(depth) + "first start;\n";
            else if (keyword == "entry")
                out += indent(depth) + "entry;\n";
            return;
        }
        out += indent(depth) + (mergedThen(model, id) ? "then " : "") + declaration(model, id);
        // "verify r;" in the objective of a verification (an objective of its own if it has none),
        // "include u;" in a use case: see generateBody
        generateBody(model, id, depth, out);
    }

    void SysMLCodeGenerator::generateBody(const SemanticModel &model, ElementId id, std::size_t depth,
                                          std::string &out) const {
        const auto &element = model.element(id);
        const bool hasRelationships = std::any_of(
                model.relationships().begin(), model.relationships().end(),
                [&](const Relationship &relationship) { return isOwnedByContext(model, relationship, id); });
        const bool verification = isVerificationKind(element.kind());
        const bool verifies = verification && hasRelationshipFrom(model, RelationshipKind::Verify, id);
        const bool objectiveWithVerifies = isObjective(element) && element.owner() != RootId &&
                                           isVerificationKind(model.element(element.owner()).kind()) &&
                                           objectiveOf(model, element.owner()) == id &&
                                           hasRelationshipFrom(model, RelationshipKind::Verify, element.owner());
        const bool includes = isUseCaseKind(element.kind()) && hasRelationshipFrom(model, RelationshipKind::Include, id);
        const bool frames = hasRelationshipFrom(model, RelationshipKind::Frame, id); // "frame concern;"
        // if / while / loop / for and the else branch always have a body; "until c;" / "else ..." follow it
        const bool control = element.kind() == ElementKind::ActionUsage &&
                             (isControlStructure(element.actionForm()) || element.isElseBranch());
        std::optional<ElementId> elseBranch;
        if (element.kind() == ElementKind::ActionUsage && element.actionForm() == ActionForm::If) {
            for (const ElementId child: element.ownedElements()) {
                if (model.element(child).isElseBranch())
                    elseBranch = child;
            }
        }
        const auto tail = [&] {
            if (!element.untilCondition().empty()) {
                out += " until " + element.untilCondition() + ";\n";
            } else if (elseBranch) {
                const std::string branch = declaration(model, *elseBranch);
                out += " else" + (branch.empty() ? std::string() : " " + branch);
                generateBody(model, *elseBranch, depth, out);
            } else {
                out += "\n";
            }
        };

        const std::string &expression = element.expression();
        const bool empty = std::all_of(element.ownedElements().begin(), element.ownedElements().end(),
                                       [&](ElementId child) { return child == elseBranch; });
        if (empty && !hasRelationships && expression.empty() && !verifies && !objectiveWithVerifies && !includes &&
            !frames) {
            if (control) {
                out += " {\n" + indent(depth) + "}";
                tail();
            } else {
                out += ";\n";
            }
            return;
        }

        out += " {\n";
        for (const ElementId child: element.ownedElements()) {
            if (child == elseBranch)
                continue;
            generateElement(model, child, depth + 1, out);
            generateSuccessionsFrom(model, child, depth + 1, out);
        }
        const auto writeReferences = [&](RelationshipKind kind, ElementId source, ElementId context,
                                          const std::string &keyword, std::size_t level) {
            for (const auto &relationship: model.relationships()) {
                if (relationship.Kind == kind && relationship.Source == source)
                    out += indent(level) + keyword + " " + referenceFrom(model, context, relationship.Target) + ";\n";
            }
        };
        if (objectiveWithVerifies)
            writeReferences(RelationshipKind::Verify, element.owner(), id, "verify", depth + 1);
        if (verifies && !objectiveOf(model, id)) {
            out += indent(depth + 1) + "objective {\n";
            writeReferences(RelationshipKind::Verify, id, id, "verify", depth + 2);
            out += indent(depth + 1) + "}\n";
        }
        if (includes)
            writeReferences(RelationshipKind::Include, id, id, "include", depth + 1);
        if (frames)
            writeReferences(RelationshipKind::Frame, id, id, "frame", depth + 1);
        generateOwnedRelationships(model, id, depth + 1, out);
        // Body expression of a constraint / calculation: last, without ';'.
        if (!expression.empty()) {
            std::size_t start = 0;
            while (start <= expression.size()) {
                std::size_t end = expression.find('\n', start);
                if (end == std::string::npos)
                    end = expression.size();
                const std::string line = expression.substr(start, end - start);
                out += line.empty() ? std::string() : indent(depth + 1) + line;
                out += '\n';
                start = end + 1;
            }
        }
        out += indent(depth) + "}";
        tail();
    }

    void SysMLCodeGenerator::generateOwnedRelationships(const SemanticModel &model, ElementId owner,
                                                        std::size_t depth, std::string &out) const {
        for (const auto &relationship: model.relationships()) {
            if (!isOwnedByContext(model, relationship, owner))
                continue;
            generateRelationship(model, relationship, owner, depth, out);
        }
    }

    void SysMLCodeGenerator::generateRelationship(const SemanticModel &model, const Relationship &relationship,
                                                  ElementId owner, std::size_t depth, std::string &out) const {
        {
            const std::string source = referenceFrom(model, owner, relationship.Source);
            const std::string target = referenceFrom(model, owner, relationship.Target);
            // optional name and type: "connection c : Pipe connect", "flow f of Fuel from", "interface : I connect"
            std::string label;
            if (!relationship.Name.empty())
                label += " " + relationship.Name;
            switch (relationship.Kind) {
                case RelationshipKind::Connection:
                    if (!relationship.TypeName.empty())
                        label += " : " + relationship.TypeName;
                    out += indent(depth) + (label.empty() ? "" : "connection" + label + " ") + "connect " + source + " to " +
                           target + ";\n";
                    break;
                case RelationshipKind::Interface:
                    if (!relationship.TypeName.empty())
                        label += " : " + relationship.TypeName;
                    out += indent(depth) + "interface" + label + " connect " + source + " to " + target + ";\n";
                    break;
                case RelationshipKind::Binding:
                    out += indent(depth) + "bind " + source + " = " + target + ";\n";
                    break;
                case RelationshipKind::Flow:
                    if (!relationship.Definition.empty())
                        label += " : " + relationship.Definition;
                    if (!relationship.TypeName.empty())
                        label += " of " + relationship.TypeName;
                    out += indent(depth) + "flow" + label + " from " + source + " to " + target + ";\n";
                    break;
                case RelationshipKind::Succession:
                    // named, or between events of different lifelines (other unnamed ones follow their source)
                    out += indent(depth) + (label.empty() ? std::string() : "succession" + label + " ") + "first " + source +
                           (relationship.Guard.empty() ? std::string() : " if " + relationship.Guard) + " then " + target + ";\n";
                    break;
                case RelationshipKind::Transition:
                    out += indent(depth) + "transition" + label + " first " + source +
                           (relationship.Trigger.empty() ? std::string() : " accept " + relationship.Trigger) +
                           (relationship.Guard.empty() ? std::string() : " if " + relationship.Guard) +
                           (relationship.Effect.empty() ? std::string() : " do " + relationship.Effect) +
                           " then " + target + ";\n";
                    break;
                case RelationshipKind::Satisfy:
                    if (relationship.Context == std::optional<ElementId>(relationship.Source))
                        out += indent(depth) + "satisfy " + target + ";\n"; // in the body of the satisfying element
                    else
                        out += indent(depth) + "satisfy " + target + " by " + source + ";\n";
                    break;
                case RelationshipKind::Message:
                    if (!relationship.TypeName.empty())
                        label += " of " + relationship.TypeName;
                    out += indent(depth) + (relationship.Sequenced ? "then message" : "message") + label + " from " +
                           source + " to " + target + ";\n";
                    break;
                case RelationshipKind::Dependency:
                    out += indent(depth) + "dependency" + label + " from " + source + " to " + target + ";\n";
                    break;
                case RelationshipKind::Allocation: {
                    if (!relationship.TypeName.empty())
                        label += " : " + relationship.TypeName;
                    out += indent(depth) + (label.empty() ? "" : "allocation" + label + " ") + "allocate " + source +
                           " to " + target;
                    bool refined = false;
                    for (const auto &child: model.relationships()) {
                        if (child.Parent != relationship.Id)
                            continue;
                        if (!refined)
                            out += " {\n";
                        refined = true;
                        generateRelationship(model, child, owner, depth + 1, out);
                    }
                    out += refined ? indent(depth) + "}\n" : ";\n";
                    break;
                }
                case RelationshipKind::Specialization:
                case RelationshipKind::Verify:
                case RelationshipKind::Include:
                case RelationshipKind::Frame:
                    break;
            }
        }
    }

    std::string SysMLCodeGenerator::declaration(const SemanticModel &model, ElementId id) const {
        const auto &element = model.element(id);
        std::string text;
        for (const auto &prefix: element.prefixes())
            text += prefix + " ";
        const auto word = [&text](const std::string &next) {
            if (next.empty())
                return;
            if (!text.empty() && text.back() != ' ')
                text += ' ';
            text += next;
        };
        const auto redefinitions = [&] {
            std::string list;
            for (const auto &reference: element.redefinitions())
                list += (list.empty() ? element.redefinitionKeyword() + " " : std::string(", ")) + reference;
            return list;
        };
        const auto nameAndShortName = [&] {
            if (!element.shortName().empty())
                word("<" + element.shortName() + ">");
            if (element.nameFromRedefinition())
                word(redefinitions()); // "part redefines cyl[4]", ":>> mass = 2000.0"
            else if (!element.name().empty())
                word(formatName(element.name()));
        };
        // everything after the name: ": Type[1..*] ordered :> a, b redefines c = value"
        const auto rest = [&] {
            if (!element.typeName().empty())
                text += " : " + element.typeName();
            if (element.multiplicity().has_value())
                text += element.multiplicity()->toString();
            if (!element.multiplicityQualifiers().empty())
                text += " " + element.multiplicityQualifiers();

            std::string specializations;
            for (const auto &relationship: model.relationships()) {
                if (relationship.Kind != RelationshipKind::Specialization || relationship.Source != id)
                    continue;
                specializations += specializations.empty() ? " :> " : ", ";
                specializations += specializationReference(model, id, relationship.Target);
            }
            for (const auto &reference: element.externalSpecializations()) {
                specializations += specializations.empty() ? " :> " : ", ";
                specializations += reference;
            }
            text += specializations;
            if (!element.nameFromRedefinition() && !element.redefinitions().empty())
                text += " " + redefinitions();
            if (!element.value().empty())
                text += " " + element.valueOperator() + " " + element.value();
            if (element.isParallel())
                text += " parallel";
        };

        if (element.kind() == ElementKind::ActionUsage &&
            (element.actionForm() != ActionForm::Plain || element.isElseBranch())) {
            // [entry|do|exit] [action name : T] send ... / if c [action name] { ... } / else [action name] { ... }
            const bool elseBranch = element.isElseBranch(); // "else [if c] [action name] { ... }": nothing in front
            if (elseBranch)
                text.clear();
            if (element.declaredKeyword().has_value() && !elseBranch) {
                std::string keyword = *element.declaredKeyword(); // "entry action" -> "entry action a assign ..."
                if (keyword.size() > 7 && keyword.compare(keyword.size() - 7, 7, " action") == 0)
                    keyword.resize(keyword.size() - 7);
                word(keyword);
            }
            const bool named = !element.name().empty() || !element.shortName().empty();
            const bool inBody = element.namedInBody() || elseBranch;
            if (!inBody && (named || !element.typeName().empty() || !element.externalSpecializations().empty() ||
                            hasRelationshipFrom(model, RelationshipKind::Specialization, id))) {
                word("action");
                nameAndShortName();
                rest();
            }
            if (element.actionForm() != ActionForm::Plain) {
                word(toKeyword(element.actionForm()));
                word(element.actionClause());
            }
            if (inBody && named) {
                word("action");
                nameAndShortName();
            }
            return text;
        }

        if (isTargetKind(element.kind())) {
            // "private import ISQ::*", "alias Car for Automobile", "expose vehicle::**", "filter @Safety"
            word(element.declaredKeyword().value_or(defaultKeyword(element.kind())));
            if (element.kind() == ElementKind::Alias) {
                nameAndShortName();
                word("for");
            }
            word(element.target());
            return text;
        }
        if (element.kind() == ElementKind::MetadataUsage) {
            // "metadata SafetyFeature about a, b", "@ToolVariable", "metadata m : T" (a named one with "metadata")
            const bool named = !element.name().empty() || !element.shortName().empty();
            if (element.declaredKeyword() == "@" && !named && !element.typeName().empty()) {
                word("@" + element.typeName());
            } else {
                word(element.declaredKeyword().has_value() && *element.declaredKeyword() != "@" ? *element.declaredKeyword()
                                                                                                : std::string("metadata"));
                if (named) {
                    nameAndShortName();
                    text += " :";
                }
                word(element.typeName());
            }
            std::string about;
            for (const auto &reference: element.about())
                about += (about.empty() ? " about " : ", ") + reference;
            text += about;
            return text;
        }

        std::string keyword = defaultKeyword(element.kind());
        if (element.kind() == ElementKind::ActionPerformance && element.performStyle() == PerformStyle::ReferenceAction)
            keyword = "perform";
        if (element.declaredKeyword().has_value())
            keyword = *element.declaredKeyword();
        word(keyword);
        nameAndShortName();
        rest();
        return text;
    }

    std::string SysMLCodeGenerator::indent(std::size_t depth) const {
        std::string result;
        for (std::size_t i = 0; i < depth; ++i)
            result += GeneratorOptions.Indent;
        return result;
    }
}
