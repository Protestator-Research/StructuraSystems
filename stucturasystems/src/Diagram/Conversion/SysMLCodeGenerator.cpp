//
// Semantic model -> SysML v2 textual notation (graphic -> text direction).
//

#include "SysMLCodeGenerator.h"

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
            if (element.kind() != ElementKind::EventOccurrence && element.kind() != ElementKind::OccurrenceUsage && !temporal)
                return std::nullopt;
            const auto &siblings = model.children(element.owner());
            const auto position = std::find(siblings.begin(), siblings.end(), id);
            if (position == siblings.begin())
                return std::nullopt;
            const ElementId previous = *(position - 1);
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
                relationship.Parent.has_value())
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

        const bool hasRelationships = std::any_of(
                model.relationships().begin(), model.relationships().end(),
                [&](const Relationship &relationship) { return isOwnedByContext(model, relationship, id); });
        // "verify r;" in the objective of a verification (an objective of its own if it has none),
        // "include u;" in a use case
        const bool verification = isVerificationKind(element.kind());
        const bool verifies = verification && hasRelationshipFrom(model, RelationshipKind::Verify, id);
        const bool objectiveWithVerifies = isObjective(element) && element.owner() != RootId &&
                                           isVerificationKind(model.element(element.owner()).kind()) &&
                                           objectiveOf(model, element.owner()) == id &&
                                           hasRelationshipFrom(model, RelationshipKind::Verify, element.owner());
        const bool includes = isUseCaseKind(element.kind()) && hasRelationshipFrom(model, RelationshipKind::Include, id);

        const std::string &expression = element.expression();
        if (element.ownedElements().empty() && !hasRelationships && expression.empty() && !verifies &&
            !objectiveWithVerifies && !includes) {
            out += ";\n";
            return;
        }

        out += " {\n";
        for (const ElementId child: element.ownedElements()) {
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
        out += indent(depth) + "}\n";
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
                    break;
            }
        }
    }

    std::string SysMLCodeGenerator::declaration(const SemanticModel &model, ElementId id) const {
        const auto &element = model.element(id);
        std::string text;
        for (const auto &prefix: element.prefixes())
            text += prefix + " ";

        std::string keyword = defaultKeyword(element.kind());
        if (element.kind() == ElementKind::ActionPerformance && element.performStyle() == PerformStyle::ReferenceAction)
            keyword = "perform";
        if (element.declaredKeyword().has_value())
            keyword = *element.declaredKeyword();
        const auto word = [&text](const std::string &next) {
            if (next.empty())
                return;
            if (!text.empty() && text.back() != ' ')
                text += ' ';
            text += next;
        };
        word(keyword);
        if (!element.shortName().empty())
            word("<" + element.shortName() + ">");
        if (!element.name().empty())
            word(formatName(element.name()));

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
        if (!element.value().empty())
            text += " " + element.valueOperator() + " " + element.value();
        return text;
    }

    std::string SysMLCodeGenerator::indent(std::size_t depth) const {
        std::string result;
        for (std::size_t i = 0; i < depth; ++i)
            result += GeneratorOptions.Indent;
        return result;
    }
}
