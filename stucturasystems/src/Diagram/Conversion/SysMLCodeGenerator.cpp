//
// Semantic model -> SysML v2 textual notation (graphic -> text direction).
//

#include "SysMLCodeGenerator.h"

#include <algorithm>
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

        bool isUsageKind(ElementKind kind) {
            return kind == ElementKind::PartUsage || kind == ElementKind::Attribute ||
                   kind == ElementKind::ActionPerformance || kind == ElementKind::ConstraintUsage ||
                   kind == ElementKind::CalcUsage;
        }

        /// Reference to 'target' as seen from inside 'context' (RootId = top level):
        /// usages are chained with '.', namespaces (packages, definitions) with '::'.
        std::string referenceFrom(const SemanticModel &model, ElementId context, ElementId target) {
            const auto path = model.ownershipPath(target);
            auto start = path.begin();
            if (context != RootId) {
                const auto contextPosition = std::find(path.begin(), path.end(), context);
                if (contextPosition != path.end())
                    start = contextPosition + 1;
            }

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

        bool isOwnedByContext(const SemanticModel &model, const Relationship &relationship, ElementId context) {
            if (relationship.Kind == RelationshipKind::Specialization)
                return false;
            return commonOwner(model, relationship.Source, relationship.Target) == context;
        }
    }

    SysMLCodeGenerator::SysMLCodeGenerator(Options options) :
            GeneratorOptions(std::move(options)) {
    }

    std::string SysMLCodeGenerator::generate(const SemanticModel &model) const {
        std::string out;
        for (const ElementId id: model.children(RootId))
            generateElement(model, id, 0, out);
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
        out += indent(depth) + declaration(model, id);

        const bool hasRelationships = std::any_of(
                model.relationships().begin(), model.relationships().end(),
                [&](const Relationship &relationship) { return isOwnedByContext(model, relationship, id); });

        const std::string &expression = element.expression();
        if (element.ownedElements().empty() && !hasRelationships && expression.empty()) {
            out += ";\n";
            return;
        }

        out += " {\n";
        for (const ElementId child: element.ownedElements())
            generateElement(model, child, depth + 1, out);
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
            const std::string source = referenceFrom(model, owner, relationship.Source);
            const std::string target = referenceFrom(model, owner, relationship.Target);
            if (relationship.Kind == RelationshipKind::Connection)
                out += indent(depth) + "connect " + source + " to " + target + ";\n";
            else if (relationship.Kind == RelationshipKind::Binding)
                out += indent(depth) + "bind " + source + " = " + target + ";\n";
            else
                out += indent(depth) + "flow from " + source + " to " + target + ";\n";
        }
    }

    std::string SysMLCodeGenerator::declaration(const SemanticModel &model, ElementId id) const {
        const auto &element = model.element(id);
        std::string text;
        for (const auto &prefix: element.prefixes())
            text += prefix + " ";

        std::string keyword;
        switch (element.kind()) {
            case ElementKind::Package:
                keyword = "package";
                break;
            case ElementKind::PartDefinition:
                keyword = "part def";
                break;
            case ElementKind::PartUsage:
                keyword = "part";
                break;
            case ElementKind::Attribute:
                keyword = "attribute";
                break;
            case ElementKind::ActionPerformance:
                keyword = element.performStyle() == PerformStyle::DeclareAction ? "perform action" : "perform";
                break;
            case ElementKind::ConstraintDefinition:
                keyword = "constraint def";
                break;
            case ElementKind::CalcDefinition:
                keyword = "calc def";
                break;
            case ElementKind::ConstraintUsage:
                keyword = "constraint";
                break;
            case ElementKind::CalcUsage:
                keyword = "calc";
                break;
            case ElementKind::Textual:
                break;
        }
        if (element.declaredKeyword().has_value())
            keyword = *element.declaredKeyword();
        if (!keyword.empty())
            text += keyword + " ";
        text += formatName(element.name());

        if (!element.typeName().empty())
            text += " : " + element.typeName();
        if (element.multiplicity().has_value())
            text += element.multiplicity()->toString();

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
