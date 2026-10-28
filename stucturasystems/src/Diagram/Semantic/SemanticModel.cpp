//
// Semantic layer of the diagram editor: the SysML v2 element tree plus the relationships.
//

#include "SemanticModel.h"
#include "RelationshipRules.h"

#include <algorithm>
#include <cctype>

namespace StructuraSystems::Diagram {

    namespace {
        /// "providePower.generateTorque": segments of letters, digits and '_' separated by single dots.
        bool isDottedPath(const std::string &text) {
            if (text.empty() || text.front() == '.' || text.back() == '.')
                return false;
            for (std::size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (c == '.' && text[i + 1] != '.')))
                    return false;
            }
            return true;
        }
    }

    namespace {
        /// Last segment of a reference as written ("a::'b c'.d" -> "d", "'x y'" -> "x y").
        std::string lastSegment(const std::string &reference) {
            std::string segment;
            bool quoted = false;
            for (std::size_t i = 0; i < reference.size(); ++i) {
                const char c = reference[i];
                if (quoted) {
                    if (c == '\\' && i + 1 < reference.size())
                        segment += reference[++i];
                    else if (c == '\'')
                        quoted = false;
                    else
                        segment += c;
                } else if (c == '\'') {
                    quoted = true;
                } else if (c == '.' || c == ':') {
                    if (c == ':' && i + 1 < reference.size() && reference[i + 1] == ':')
                        ++i;
                    segment.clear();
                } else if (c != ' ') {
                    segment += c;
                }
            }
            return segment;
        }

        /// ';', '{', '}' or a line break outside of string literals: the text would end its statement.
        bool breaksStatement(const std::string &text) {
            char quote = 0;
            for (std::size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (c == '\n' || c == '\r')
                    return true;
                if (quote) {
                    if (c == '\\')
                        ++i;
                    else if (c == quote)
                        quote = 0;
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == ';' || c == '{' || c == '}') {
                    return true;
                }
            }
            return quote != 0;
        }
    }

    // --- elements -----------------------------------------------------------------------

    ElementId SemanticModel::addElement(ElementKind kind, const std::string &name, ElementId owner) {
        if (owner != RootId && contains(owner) && element(owner).PerformPath)
            throw ModelError("\"perform a.b;\" has no body");
        if (name.empty() && kind != ElementKind::Textual && !mayBeUnnamed(kind))
            throw ModelError("Element name must not be empty");
        if (!name.empty() && isAnonymousKind(kind))
            throw ModelError(toString(kind) + " has no name");
        if (const auto rule = RelationshipRules::canOwn(*this, owner, kind); !rule)
            throw ModelError(rule.Reason);
        ensureUniqueSiblingName(owner, name, RootId, kind);

        const ElementId id = NextElementId++;
        Elements.emplace(id, SysMLElement(id, kind, name, owner));
        mutableChildren(owner).push_back(id);
        notify(ModelChange::Type::ElementAdded, id);
        return id;
    }

    void SemanticModel::removeElement(ElementId id) {
        if (!contains(id))
            throw ModelError("Unknown element id " + std::to_string(id));

        // Collect the whole subtree first (children before parents for the notifications).
        std::vector<ElementId> subtree;
        std::vector<ElementId> open{id};
        while (!open.empty()) {
            const ElementId current = open.back();
            open.pop_back();
            subtree.push_back(current);
            for (const ElementId child: element(current).ownedElements())
                open.push_back(child);
        }

        // Relationships touching any removed element go first.
        std::vector<RelationshipId> danglingRelationships;
        for (const auto &relationship: Relationships) {
            const bool touches = std::find(subtree.begin(), subtree.end(), relationship.Source) != subtree.end() ||
                                 std::find(subtree.begin(), subtree.end(), relationship.Target) != subtree.end();
            if (touches)
                danglingRelationships.push_back(relationship.Id);
        }
        for (const RelationshipId relationshipId: danglingRelationships) {
            if (containsRelationship(relationshipId)) // refining allocations go with the refined one
                removeRelationship(relationshipId);
        }
        // declared in the removed subtree: back to the innermost common owner of the ends
        for (auto &relationship: Relationships) {
            if (relationship.Context && std::find(subtree.begin(), subtree.end(), *relationship.Context) != subtree.end()) {
                relationship.Context.reset();
                notify(ModelChange::Type::RelationshipChanged, relationship.Id);
            }
        }

        auto &siblings = mutableChildren(element(id).owner());
        siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());

        for (auto it = subtree.rbegin(); it != subtree.rend(); ++it) {
            Elements.erase(*it);
            notify(ModelChange::Type::ElementRemoved, *it);
        }
    }

    void SemanticModel::rename(ElementId id, const std::string &name) {
        auto &target = mutableElement(id);
        if (name.empty() && target.Kind != ElementKind::Textual && !mayBeUnnamed(target.Kind))
            throw ModelError("Element name must not be empty");
        if (name.empty() && target.Kind == ElementKind::EnumerationLiteral && target.Value.empty())
            throw ModelError("An enumeration value needs a name or a value (\"enum = 60 [mm];\")");
        if (!name.empty() && isAnonymousKind(target.Kind))
            throw ModelError(toString(target.Kind) + " has no name");
        if (target.Name == name)
            return;
        if (target.PerformPath && !isDottedPath(name))
            throw ModelError("'" + name + "' is not a path like providePower.generateTorque");
        if (name.empty()) {
            const auto inside = [&](ElementId end) { return end == id || isAncestor(id, end); };
            for (const auto &relationship: Relationships) {
                if (inside(relationship.Target) ||
                    (inside(relationship.Source) && !(relationship.Source == id && (relationship.Kind == RelationshipKind::Verify ||
                                                                                    relationship.Kind == RelationshipKind::Include ||
                                                                                    relationship.Kind == RelationshipKind::Frame))))
                    throw ModelError("The element is referenced by a relationship and needs a name");
            }
        }
        ensureUniqueSiblingName(target.Owner, name, id, target.Kind);
        target.Name = name;
        if (target.NameFromRedefinition && (target.Redefinitions.empty() || lastSegment(target.Redefinitions.front()) != name))
            target.NameFromRedefinition = false; // a name of its own now: "part newName :>> cyl;"
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setTypeName(ElementId id, const std::string &typeName) {
        auto &target = mutableElement(id);
        if (target.PerformPath && !typeName.empty())
            throw ModelError("\"perform a.b;\" has nothing but the path of the performed action");
        if (target.Kind == ElementKind::Package || isDefinitionKind(target.Kind) || target.Kind == ElementKind::Textual ||
            target.Kind == ElementKind::EnumerationLiteral || isTargetKind(target.Kind))
            throw ModelError(toString(target.Kind) + " cannot be typed");
        target.TypeName = typeName;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setMultiplicity(ElementId id, std::optional<Multiplicity> multiplicity) {
        auto &target = mutableElement(id);
        if (target.PerformPath && multiplicity.has_value())
            throw ModelError("\"perform a.b;\" has nothing but the path of the performed action");
        if (target.Kind != ElementKind::PartUsage && target.Kind != ElementKind::Attribute &&
            target.Kind != ElementKind::ConstraintUsage && target.Kind != ElementKind::CalcUsage &&
            target.Kind != ElementKind::ItemUsage && target.Kind != ElementKind::PortUsage &&
            target.Kind != ElementKind::Subject && target.Kind != ElementKind::Actor &&
            target.Kind != ElementKind::OccurrenceUsage)
            throw ModelError(toString(target.Kind) + " cannot have a multiplicity");
        target.MultiplicityValue = multiplicity;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setPerformStyle(ElementId id, PerformStyle style) {
        auto &target = mutableElement(id);
        if (target.Kind != ElementKind::ActionPerformance)
            throw ModelError("Only actions have a perform style");
        target.Perform = style;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setPerformsPath(ElementId id, bool path) {
        auto &target = mutableElement(id);
        if (path) {
            if (target.Kind != ElementKind::ActionPerformance || target.Perform != PerformStyle::ReferenceAction)
                throw ModelError("Only \"perform a.b;\" names the performed action by its path");
            if (!isDottedPath(target.Name))
                throw ModelError("'" + target.Name + "' is not a path like providePower.generateTorque");
            if (!target.TypeName.empty() || target.MultiplicityValue || !target.Prefixes.empty() || !target.Value.empty() ||
                !target.Redefinitions.empty() || !target.ExternalSpecializations.empty() || !children(id).empty())
                throw ModelError("\"perform a.b;\" has nothing but the path of the performed action");
            for (const auto &relationship: Relationships) {
                if (relationship.Source == id || relationship.Target == id)
                    throw ModelError("\"perform a.b;\" cannot be connected");
            }
        }
        if (target.PerformPath == path)
            return;
        target.PerformPath = path;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setPrefixes(ElementId id, std::vector<std::string> prefixes) {
        auto &target = mutableElement(id);
        if (target.PerformPath && !prefixes.empty())
            throw ModelError("\"perform a.b;\" has nothing but the path of the performed action");
        if (target.Kind == ElementKind::Textual)
            throw ModelError("Textual elements keep their prefixes in their text");
        for (const auto &prefix: prefixes) {
            if (prefix.empty() || prefix.find_first_of(" \t\r\n;{}") != std::string::npos)
                throw ModelError("Invalid prefix '" + prefix + "'");
        }
        if (target.Prefixes == prefixes)
            return;
        target.Prefixes = std::move(prefixes);
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setValue(ElementId id, const std::string &op, const std::string &expression) {
        auto &target = mutableElement(id);
        if (target.PerformPath && !expression.empty())
            throw ModelError("\"perform a.b;\" has nothing but the path of the performed action");
        if (target.Kind != ElementKind::Attribute && target.Kind != ElementKind::PartUsage &&
            target.Kind != ElementKind::Subject && target.Kind != ElementKind::OccurrenceUsage &&
            target.Kind != ElementKind::EnumerationLiteral)
            throw ModelError(toString(target.Kind) + " cannot have a value");
        if (target.Kind == ElementKind::EnumerationLiteral && expression.empty() && target.Name.empty())
            throw ModelError("An enumeration value needs a name or a value (\"enum = 60 [mm];\")");
        if (target.Kind == ElementKind::EnumerationLiteral && !expression.empty() && !op.empty() && op != "=")
            throw ModelError("An enumeration value is bound with '=' (\"enum = 60 [mm];\")");
        const std::string newOperator = expression.empty() ? std::string() : (op.empty() ? std::string("=") : op);
        if (target.ValueOperator == newOperator && target.Value == expression)
            return;
        target.ValueOperator = newOperator;
        target.Value = expression;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setExternalSpecializations(ElementId id, std::vector<std::string> references) {
        auto &target = mutableElement(id);
        if (target.PerformPath && !references.empty())
            throw ModelError("\"perform a.b;\" has nothing but the path of the performed action");
        if (target.Kind == ElementKind::Textual || target.Kind == ElementKind::Package)
            throw ModelError(toString(target.Kind) + " cannot specialize");
        if (target.ExternalSpecializations == references)
            return;
        target.ExternalSpecializations = std::move(references);
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setSpecializationKeyword(ElementId id, const std::string &keyword) {
        auto &target = mutableElement(id);
        if (!keyword.empty() && keyword != ":>" &&
            !(keyword == "subsets" && isUsageKind(target.Kind)) &&
            !(keyword == "specializes" && isDefinitionKind(target.Kind)))
            throw ModelError("'" + keyword + "' cannot be written for " + toString(target.Kind));
        const std::string value = keyword == ":>" ? std::string() : keyword;
        if (target.SpecializationKeyword == value)
            return;
        target.SpecializationKeyword = value;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setExpression(ElementId id, const std::string &expression) {
        auto &target = mutableElement(id);
        if (!isConstraintOrCalc(target.Kind))
            throw ModelError(toString(target.Kind) + " has no body expression");
        std::string text = expression;
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        const auto first = text.find_first_not_of(" \t\r\n");
        text = first == std::string::npos ? std::string() : text.substr(first);
        if (target.Expression == text)
            return;
        target.Expression = std::move(text);
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setDeclaredKeyword(ElementId id, std::optional<std::string> keyword) {
        auto &target = mutableElement(id);
        if (target.Kind == ElementKind::Textual)
            throw ModelError("Textual elements keep their keyword in their text");
        if (keyword == "@" && target.Kind == ElementKind::MetadataUsage) {
            // "@Safety;" instead of "metadata Safety;"
        } else if (keyword.has_value()) {
            bool space = true;
            for (const char c: *keyword) {
                const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                if (!letter && !(c == ' ' && !space))
                    throw ModelError("Invalid keyword '" + *keyword + "'");
                space = c == ' ';
            }
            if (!keyword->empty() && space)
                throw ModelError("Invalid keyword '" + *keyword + "'");
        }
        if (target.DeclaredKeyword == keyword)
            return;
        target.DeclaredKeyword = std::move(keyword);
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setMultiplicityQualifiers(ElementId id, const std::string &qualifiers) {
        auto &target = mutableElement(id);
        if (!qualifiers.empty() && (target.Kind == ElementKind::Textual || isDefinitionKind(target.Kind) ||
                                    target.Kind == ElementKind::Package))
            throw ModelError(toString(target.Kind) + " has no multiplicity");
        if (!qualifiers.empty() && qualifiers != "ordered" && qualifiers != "nonunique" && qualifiers != "ordered nonunique")
            throw ModelError("Invalid qualifiers '" + qualifiers + "' (ordered, nonunique)");
        if (target.MultiplicityQualifiers == qualifiers)
            return;
        target.MultiplicityQualifiers = qualifiers;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setShortName(ElementId id, const std::string &shortName) {
        auto &target = mutableElement(id);
        if (target.Kind == ElementKind::Textual)
            throw ModelError("Textual elements keep their short name in their text");
        if (!shortName.empty() && isAnonymousKind(target.Kind))
            throw ModelError(toString(target.Kind) + " has no name");
        const bool quoted = shortName.size() > 1 && shortName.front() == '\'' && shortName.back() == '\'' &&
                            shortName.find('\n') == std::string::npos;
        bool basic = !shortName.empty() && !(shortName.front() >= '0' && shortName.front() <= '9');
        for (const char c: shortName)
            basic = basic && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
        if (!shortName.empty() && !quoted && !basic)
            throw ModelError("Invalid short name '" + shortName + "' (a name, or a quoted name like '1.2')");
        if (quoted) {
            // inside the quotes: \' and \\ only escaped
            for (std::size_t i = 1; i + 1 < shortName.size(); ++i) {
                if (shortName[i] == '\\')
                    ++i;
                else if (shortName[i] == '\'')
                    throw ModelError("Invalid short name '" + shortName + "'");
            }
            if (shortName.size() > 2 && shortName[shortName.size() - 2] == '\\') {
                std::size_t backslashes = 0;
                for (std::size_t i = shortName.size() - 2; i > 0 && shortName[i] == '\\'; --i)
                    ++backslashes;
                if (backslashes % 2 == 1)
                    throw ModelError("Invalid short name '" + shortName + "'");
            }
        }
        if (target.ShortName == shortName)
            return;
        target.ShortName = shortName;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setRedefinitions(ElementId id, const std::string &keyword, std::vector<std::string> references,
                                         bool nameFromRedefinition) {
        auto &target = mutableElement(id);
        if (target.PerformPath && !references.empty())
            throw ModelError("\"perform a.b;\" has nothing but the path of the performed action");
        if (!references.empty() && (!isUsageKind(target.Kind) || target.Kind == ElementKind::EnumerationLiteral ||
                                    target.Kind == ElementKind::ControlNode || target.Kind == ElementKind::MetadataUsage))
            throw ModelError(toString(target.Kind) + " cannot redefine");
        if (!references.empty() && keyword != ":>>" && keyword != "redefines")
            throw ModelError("Invalid redefinition keyword '" + keyword + "' (:>>, redefines)");
        for (const auto &reference: references) {
            if (reference.empty() || breaksStatement(reference) || reference.find(',') != std::string::npos)
                throw ModelError("Invalid redefined feature '" + reference + "'");
        }
        if (references.empty())
            nameFromRedefinition = false;
        if (nameFromRedefinition && lastSegment(references.front()) != target.Name)
            throw ModelError("The name must be the one of the redefined feature");
        const std::string newKeyword = references.empty() ? std::string() : keyword;
        if (target.Redefinitions == references && target.RedefinitionKeyword == newKeyword &&
            target.NameFromRedefinition == nameFromRedefinition)
            return;
        target.Redefinitions = std::move(references);
        target.RedefinitionKeyword = newKeyword;
        target.NameFromRedefinition = nameFromRedefinition;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setActionForm(ElementId id, ActionForm form, const std::string &clause) {
        auto &target = mutableElement(id);
        if (target.Kind != ElementKind::ActionUsage && form != ActionForm::Plain)
            throw ModelError(toString(target.Kind) + " has no action form");
        const std::string text = form == ActionForm::Plain ? std::string() : clause;
        if (breaksStatement(text))
            throw ModelError("'" + text + "' must be one line without ; { }");
        if (text.empty() && form != ActionForm::Plain && form != ActionForm::Loop && form != ActionForm::Terminate)
            throw ModelError(toKeyword(form) + " needs " + (form == ActionForm::Send ? "what is sent"
                                                            : form == ActionForm::Accept ? "what is accepted"
                                                            : form == ActionForm::Assign ? "\"feature := value\""
                                                            : form == ActionForm::For ? "\"variable in values\""
                                                                                      : "a condition"));
        if (form == ActionForm::Loop && !text.empty())
            throw ModelError("loop has no condition (use while or until)");
        if (target.ElseBranch && form != ActionForm::Plain && form != ActionForm::If)
            throw ModelError("An else branch is a body or another if action (\"else if c { ... }\")");
        if (target.Form == form && target.ActionClause == text)
            return;
        target.Form = form;
        target.ActionClause = text;
        if (form != ActionForm::Loop && form != ActionForm::While)
            target.Until.clear();
        if (!isControlStructure(form) && !target.ElseBranch)
            target.NamedInBody = false;
        if (form != ActionForm::If) {
            for (const ElementId child: target.OwnedElements) {
                auto &branch = mutableElement(child);
                if (branch.ElseBranch) {
                    branch.ElseBranch = false;
                    if (!isControlStructure(branch.Form))
                        branch.NamedInBody = false;
                    notify(ModelChange::Type::ElementChanged, child);
                }
            }
        }
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setUntilCondition(ElementId id, const std::string &condition) {
        auto &target = mutableElement(id);
        if (!condition.empty() && target.Form != ActionForm::Loop && target.Form != ActionForm::While)
            throw ModelError("Only loop and while actions have an until condition");
        if (breaksStatement(condition))
            throw ModelError("'" + condition + "' must be one line without ; { }");
        if (target.Until == condition)
            return;
        target.Until = condition;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setElseBranch(ElementId id, bool elseBranch) {
        auto &target = mutableElement(id);
        if (elseBranch) {
            if (target.Kind != ElementKind::ActionUsage || target.Owner == RootId ||
                element(target.Owner).Form != ActionForm::If)
                throw ModelError("Only an action in an if action can be its else branch");
            if (target.Form != ActionForm::Plain && target.Form != ActionForm::If)
                throw ModelError("An else branch is a body or another if action (\"else if c { ... }\")");
            if (!target.Prefixes.empty() || target.DeclaredKeyword || !target.TypeName.empty() ||
                !target.Redefinitions.empty() || !target.ExternalSpecializations.empty())
                throw ModelError("An else branch has a name at most (\"else action name { ... }\")");
            for (const auto &relationship: Relationships) {
                if (relationship.Source == id || relationship.Target == id)
                    throw ModelError("The else branch of an if action cannot be connected");
            }
            for (const ElementId sibling: children(target.Owner)) {
                if (sibling != id && element(sibling).ElseBranch)
                    throw ModelError("The if action already has an else branch");
            }
        }
        if (target.ElseBranch == elseBranch)
            return;
        target.ElseBranch = elseBranch;
        if (!elseBranch && !isControlStructure(target.Form))
            target.NamedInBody = false;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setNamedInBody(ElementId id, bool namedInBody) {
        auto &target = mutableElement(id);
        if (namedInBody && !isControlStructure(target.Form) && !target.ElseBranch)
            throw ModelError("Only if, while, loop, for and else name their body");
        if (target.NamedInBody == namedInBody)
            return;
        target.NamedInBody = namedInBody;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setDeclaredWithThen(ElementId id, bool declaredWithThen) {
        auto &target = mutableElement(id);
        if (declaredWithThen && target.Kind != ElementKind::ActionUsage && target.Kind != ElementKind::ControlNode)
            throw ModelError("Only actions and control nodes are declared with 'then'");
        if (target.DeclaredWithThen == declaredWithThen)
            return;
        target.DeclaredWithThen = declaredWithThen;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setTarget(ElementId id, const std::string &text) {
        auto &target = mutableElement(id);
        if (!isTargetKind(target.Kind))
            throw ModelError(toString(target.Kind) + " has no target");
        if (text.find_first_not_of(" \t") == std::string::npos || breaksStatement(text))
            throw ModelError("'" + text + "' must be one line without ; { } (and not empty)");
        if (target.Target == text)
            return;
        target.Target = text;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setAbout(ElementId id, std::vector<std::string> references) {
        auto &target = mutableElement(id);
        if (target.Kind != ElementKind::MetadataUsage && !references.empty())
            throw ModelError("Only metadata is about other elements");
        for (const auto &reference: references) {
            if (reference.empty() || breaksStatement(reference) || reference.find(',') != std::string::npos)
                throw ModelError("Invalid annotated element '" + reference + "'");
        }
        if (target.About == references)
            return;
        target.About = std::move(references);
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setBlankLineBefore(ElementId id, bool blankLine) {
        auto &target = mutableElement(id);
        if (target.BlankLineBefore == blankLine)
            return;
        target.BlankLineBefore = blankLine;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setParallel(ElementId id, bool parallel) {
        auto &target = mutableElement(id);
        if (parallel && target.Kind != ElementKind::StateUsage && target.Kind != ElementKind::StateDefinition)
            throw ModelError("Only states can be parallel");
        if (target.Parallel == parallel)
            return;
        target.Parallel = parallel;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setText(ElementId id, const std::string &text, const std::string &keyword, const std::string &name) {
        auto &target = mutableElement(id);
        if (target.Kind != ElementKind::Textual)
            throw ModelError("Only textual elements have a text");
        if (text.find_first_not_of(" \t\r\n") == std::string::npos)
            throw ModelError("The text must not be empty");
        target.Text = text;
        target.Keyword = keyword;
        target.Name = name;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::reparent(ElementId id, ElementId newOwner) {
        if (const auto rule = RelationshipRules::canReparent(*this, id, newOwner); !rule)
            throw ModelError(rule.Reason);
        auto &target = mutableElement(id);
        if (target.Owner == newOwner)
            return;
        ensureUniqueSiblingName(newOwner, target.Name, id, target.Kind);

        auto &oldSiblings = mutableChildren(target.Owner);
        oldSiblings.erase(std::remove(oldSiblings.begin(), oldSiblings.end(), id), oldSiblings.end());
        mutableChildren(newOwner).push_back(id);
        target.Owner = newOwner;
        if (target.ElseBranch) { // the else branch of its old if action only
            target.ElseBranch = false;
            if (!isControlStructure(target.Form))
                target.NamedInBody = false;
        }

        // A connection/flow between an element and its new (indirect) owner is no longer valid.
        std::vector<RelationshipId> invalid;
        for (const auto &relationship: Relationships) {
            // the ends are referenced from where the relationship is written: from there on, every element on the
            // way to an end needs a name (not so below an unnamed "objective { ... }" or "if c { ... }" around it)
            const bool shorthand = relationship.Kind == RelationshipKind::Succession && relationship.Name.empty() &&
                                   element(relationship.Source).owner() == element(relationship.Target).owner();
            const auto pathSource = ownershipPath(relationship.Source);
            const auto pathTarget = ownershipPath(relationship.Target);
            std::size_t common = 0;
            if (relationship.Context) {
                const auto contextPath = *relationship.Context == RootId ? std::vector<ElementId>()
                                                                          : ownershipPath(*relationship.Context);
                common = contextPath.size();
            } else {
                while (common + 1 < pathSource.size() && common + 1 < pathTarget.size() &&
                       pathSource[common] == pathTarget[common])
                    ++common;
            }
            const auto named = [&](const std::vector<ElementId> &path) {
                for (std::size_t i = std::min(common, path.size()); i < path.size(); ++i) {
                    if (element(path[i]).name().empty())
                        return false;
                }
                return true;
            };
            if (!shorthand && (!named(pathTarget) || (!named(pathSource) && relationship.Kind != RelationshipKind::Verify &&
                                                       relationship.Kind != RelationshipKind::Include &&
                                                       relationship.Kind != RelationshipKind::Frame))) {
                invalid.push_back(relationship.Id);
                continue;
            }
            if (relationship.Kind == RelationshipKind::Specialization)
                continue;
            const bool events = element(relationship.Source).kind() == ElementKind::EventOccurrence &&
                                element(relationship.Target).kind() == ElementKind::EventOccurrence;
            if ((relationship.Kind == RelationshipKind::Succession || relationship.Kind == RelationshipKind::Transition) &&
                element(relationship.Source).owner() != element(relationship.Target).owner() && !events) {
                invalid.push_back(relationship.Id);
                continue;
            }
            if (isAncestor(relationship.Source, relationship.Target) ||
                isAncestor(relationship.Target, relationship.Source))
                invalid.push_back(relationship.Id);
        }
        for (const RelationshipId relationshipId: invalid) {
            if (containsRelationship(relationshipId))
                removeRelationship(relationshipId);
        }

        notify(ModelChange::Type::ElementReparented, id);
    }

    void SemanticModel::setChildIndex(ElementId id, std::size_t index) {
        auto &siblings = mutableChildren(element(id).owner());
        const auto current = std::find(siblings.begin(), siblings.end(), id);
        const auto position = std::size_t(current - siblings.begin());
        const std::size_t wanted = std::min(index, siblings.size() - 1);
        if (position == wanted)
            return;
        siblings.erase(current);
        siblings.insert(siblings.begin() + std::ptrdiff_t(wanted), id);
        notify(ModelChange::Type::ElementChanged, id);
    }

    bool SemanticModel::contains(ElementId id) const {
        return Elements.find(id) != Elements.end();
    }

    const SysMLElement &SemanticModel::element(ElementId id) const {
        const auto it = Elements.find(id);
        if (it == Elements.end())
            throw ModelError("Unknown element id " + std::to_string(id));
        return it->second;
    }

    const std::vector<ElementId> &SemanticModel::children(ElementId owner) const {
        if (owner == RootId)
            return RootElements;
        return element(owner).ownedElements();
    }

    std::string SemanticModel::qualifiedName(ElementId id) const {
        std::string result;
        for (const ElementId pathElement: ownershipPath(id)) {
            if (!result.empty())
                result += "::";
            result += element(pathElement).name();
        }
        return result;
    }

    std::vector<ElementId> SemanticModel::ownershipPath(ElementId id) const {
        std::vector<ElementId> path;
        for (ElementId current = id; current != RootId; current = element(current).owner())
            path.push_back(current);
        std::reverse(path.begin(), path.end());
        return path;
    }

    bool SemanticModel::isAncestor(ElementId ancestor, ElementId descendant) const {
        if (!contains(descendant))
            return false;
        for (ElementId current = element(descendant).owner(); current != RootId; current = element(current).owner()) {
            if (current == ancestor)
                return true;
        }
        return false;
    }

    // --- relationships ------------------------------------------------------------------

    RelationshipId SemanticModel::addRelationship(RelationshipKind kind, ElementId source, ElementId target) {
        if (const auto rule = RelationshipRules::canRelate(*this, kind, source, target); !rule)
            throw ModelError(rule.Reason);
        const RelationshipId id = NextRelationshipId++;
        Relationships.push_back(Relationship{id, kind, source, target, {}, {}, {}, {}, {}, false, std::nullopt, {}, std::nullopt, {}, {}, false});
        notify(ModelChange::Type::RelationshipAdded, id);
        return id;
    }

    void SemanticModel::removeRelationship(RelationshipId id) {
        const auto it = std::find_if(Relationships.begin(), Relationships.end(),
                                     [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        Relationships.erase(it);
        // allocations refining the removed one go with it
        std::vector<RelationshipId> refining;
        for (const auto &relationship: Relationships) {
            if (relationship.Parent == id)
                refining.push_back(relationship.Id);
        }
        notify(ModelChange::Type::RelationshipRemoved, id);
        for (const RelationshipId child: refining) {
            if (containsRelationship(child))
                removeRelationship(child);
        }
    }

    void SemanticModel::setSequenced(RelationshipId id, bool sequenced) {
        auto it = std::find_if(Relationships.begin(), Relationships.end(),
                               [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        if (it->Kind != RelationshipKind::Message)
            throw ModelError("Only messages can be written with 'then'");
        if (it->Sequenced == sequenced)
            return;
        it->Sequenced = sequenced;
        notify(ModelChange::Type::RelationshipChanged, id);
    }

    void SemanticModel::setRelationshipIndex(RelationshipId id, std::size_t index) {
        const auto it = std::find_if(Relationships.begin(), Relationships.end(),
                                     [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        const auto position = std::size_t(it - Relationships.begin());
        const std::size_t wanted = std::min(index, Relationships.size() - 1);
        if (position == wanted)
            return;
        const Relationship moved = *it;
        Relationships.erase(it);
        Relationships.insert(Relationships.begin() + std::ptrdiff_t(wanted), moved);
        notify(ModelChange::Type::RelationshipChanged, id);
    }

    void SemanticModel::setRelationshipContext(RelationshipId id, std::optional<ElementId> context) {
        auto it = std::find_if(Relationships.begin(), Relationships.end(),
                               [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        if (it->Kind == RelationshipKind::Satisfy) {
            // "satisfy r;" in the satisfying element, or "satisfy r by s;"
            if (context && *context != it->Source)
                throw ModelError("Satisfy is declared in its source or in the owner of its ends");
            if (it->Context == context)
                return;
            it->Context = context;
            notify(ModelChange::Type::RelationshipChanged, id);
            return;
        }
        if (it->Kind != RelationshipKind::Message && it->Kind != RelationshipKind::Allocation &&
            it->Kind != RelationshipKind::Dependency)
            throw ModelError(toString(it->Kind) + " is always declared in the owner of its ends");
        if (context && *context != RootId &&
            (!contains(*context) || element(*context).kind() == ElementKind::Textual || *context == it->Source ||
             *context == it->Target))
            throw ModelError("A relationship cannot be declared there");
        if (context) {
            // the ends are referenced from there: every element on the way needs a name
            for (const ElementId end: {it->Source, it->Target}) {
                const auto path = ownershipPath(end);
                auto from = std::find(path.begin(), path.end(), *context);
                from = from == path.end() ? path.begin() : from + 1;
                for (auto step = from; step != path.end(); ++step) {
                    if (element(*step).name().empty())
                        throw ModelError("An element without a name cannot be referenced from there");
                }
            }
        }
        if (it->Context == context)
            return;
        it->Context = context;
        notify(ModelChange::Type::RelationshipChanged, id);
    }

    void SemanticModel::setParentRelationship(RelationshipId id, std::optional<RelationshipId> parent) {
        auto it = std::find_if(Relationships.begin(), Relationships.end(),
                               [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        if (it->Kind != RelationshipKind::Allocation)
            throw ModelError("Only allocations can refine another allocation");
        if (parent) {
            if (!containsRelationship(*parent) || relationship(*parent).Kind != RelationshipKind::Allocation)
                throw ModelError("An allocation can only refine another allocation");
            for (std::optional<RelationshipId> current = parent; current; current = relationship(*current).Parent) {
                if (*current == id)
                    throw ModelError("An allocation cannot refine itself");
            }
        }
        if (it->Parent == parent)
            return;
        it->Parent = parent;
        notify(ModelChange::Type::RelationshipChanged, id);
    }

    bool SemanticModel::containsRelationship(RelationshipId id) const {
        return std::any_of(Relationships.begin(), Relationships.end(),
                           [id](const Relationship &relationship) { return relationship.Id == id; });
    }

    void SemanticModel::setRelationshipLabel(RelationshipId id, const std::string &name, const std::string &typeName) {
        auto it = std::find_if(Relationships.begin(), Relationships.end(),
                               [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        if (it->Kind == RelationshipKind::Specialization || it->Kind == RelationshipKind::Binding ||
            it->Kind == RelationshipKind::Satisfy || it->Kind == RelationshipKind::Verify ||
            it->Kind == RelationshipKind::Include || it->Kind == RelationshipKind::Frame ||
            ((it->Kind == RelationshipKind::Succession || it->Kind == RelationshipKind::Transition ||
              it->Kind == RelationshipKind::Dependency) && !typeName.empty())) {
            if (!name.empty() || !typeName.empty())
                throw ModelError(toString(it->Kind) + " has no name or type");
            return;
        }
        if (it->Kind == RelationshipKind::Succession && !name.empty() &&
            (element(it->Source).name().empty() || element(it->Target).name().empty()))
            throw ModelError("A named succession needs named ends (\"succession s first a then b;\")");
        for (const std::string *text: {&name, &typeName}) {
            if (text->find_first_of(" \t\r\n;{}=,") != std::string::npos &&
                !(text->size() > 1 && text->front() == '\'' && text->back() == '\''))
                throw ModelError("Invalid name or type '" + *text + "'");
        }
        if (it->Name == name && it->TypeName == typeName)
            return;
        it->Name = name;
        it->TypeName = typeName;
        notify(ModelChange::Type::RelationshipChanged, id);
    }

    void SemanticModel::setFlowDefinition(RelationshipId id, const std::string &definition) {
        auto it = std::find_if(Relationships.begin(), Relationships.end(),
                               [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        if (it->Kind != RelationshipKind::Flow && !definition.empty())
            throw ModelError("Only flows have a flow definition");
        if (definition.find_first_of(" \t\r\n;{}=,") != std::string::npos &&
            !(definition.size() > 1 && definition.front() == '\'' && definition.back() == '\''))
            throw ModelError("Invalid flow definition '" + definition + "'");
        if (it->Definition == definition)
            return;
        it->Definition = definition;
        notify(ModelChange::Type::RelationshipChanged, id);
    }

    void SemanticModel::setEndFeatures(RelationshipId id, const std::string &sourceFeature, const std::string &targetFeature) {
        auto it = std::find_if(Relationships.begin(), Relationships.end(),
                               [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        const bool connecting = it->Kind == RelationshipKind::Connection || it->Kind == RelationshipKind::Flow ||
                                it->Kind == RelationshipKind::Interface || it->Kind == RelationshipKind::Binding;
        if (!connecting && (!sourceFeature.empty() || !targetFeature.empty()))
            throw ModelError("Only connections, flows, interfaces and bindings have end features");
        for (const std::string *feature: {&sourceFeature, &targetFeature}) {
            if (!feature->empty() && !isDottedPath(*feature))
                throw ModelError("Invalid end feature '" + *feature + "'");
        }
        if (it->SourceFeature == sourceFeature && it->TargetFeature == targetFeature)
            return;
        it->SourceFeature = sourceFeature;
        it->TargetFeature = targetFeature;
        notify(ModelChange::Type::RelationshipChanged, id);
    }

    void SemanticModel::setSuccessionFlow(RelationshipId id, bool successionFlow) {
        auto it = std::find_if(Relationships.begin(), Relationships.end(),
                               [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        if (it->Kind != RelationshipKind::Flow && successionFlow)
            throw ModelError("Only a flow can be a succession flow");
        if (it->SuccessionFlow == successionFlow)
            return;
        it->SuccessionFlow = successionFlow;
        notify(ModelChange::Type::RelationshipChanged, id);
    }

    void SemanticModel::setBehaviorDetails(RelationshipId id, const std::string &guard, const std::string &trigger,
                                           const std::string &effect) {
        auto it = std::find_if(Relationships.begin(), Relationships.end(),
                               [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        const bool transition = it->Kind == RelationshipKind::Transition;
        if (it->Kind != RelationshipKind::Succession && !transition)
            throw ModelError(toString(it->Kind) + " has no guard, trigger or effect");
        if (!transition && (!trigger.empty() || !effect.empty()))
            throw ModelError("A succession has a guard only");
        if (!transition && guard.find_first_not_of(" \t\r\n") != std::string::npos &&
            element(it->Target).name().empty())
            throw ModelError("A succession to an unnamed action has no guard (\"then if c { ... }\" follows its source)");
        for (const std::string *text: {&guard, &trigger, &effect}) {
            if (text->find_first_of(";{}") != std::string::npos)
                throw ModelError("'" + *text + "' must not contain ; { }");
        }
        const auto trimmed = [](std::string text) {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        };
        if (it->Guard == trimmed(guard) && it->Trigger == trimmed(trigger) && it->Effect == trimmed(effect))
            return;
        it->Guard = trimmed(guard);
        it->Trigger = trimmed(trigger);
        it->Effect = trimmed(effect);
        notify(ModelChange::Type::RelationshipChanged, id);
    }

    const Relationship &SemanticModel::relationship(RelationshipId id) const {
        const auto it = std::find_if(Relationships.begin(), Relationships.end(),
                                     [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        return *it;
    }

    // --- listeners ----------------------------------------------------------------------

    SemanticModel::ListenerToken SemanticModel::addListener(Listener listener) {
        const ListenerToken token = NextListenerToken++;
        Listeners.emplace(token, std::move(listener));
        return token;
    }

    void SemanticModel::removeListener(ListenerToken token) {
        Listeners.erase(token);
    }

    // --- private ------------------------------------------------------------------------

    SysMLElement &SemanticModel::mutableElement(ElementId id) {
        const auto it = Elements.find(id);
        if (it == Elements.end())
            throw ModelError("Unknown element id " + std::to_string(id));
        return it->second;
    }

    std::vector<ElementId> &SemanticModel::mutableChildren(ElementId owner) {
        if (owner == RootId)
            return RootElements;
        return mutableElement(owner).OwnedElements;
    }

    void SemanticModel::ensureUniqueSiblingName(ElementId owner, const std::string &name, ElementId ignore,
                                                ElementKind kind) const {
        if (kind == ElementKind::Textual || name.empty())
            return; // statements kept as text are not checked (overloads, imports, comments, ...), nor unnamed ones
        for (const ElementId sibling: children(owner)) {
            if (sibling != ignore && element(sibling).kind() != ElementKind::Textual && element(sibling).name() == name)
                throw ModelError("An element named '" + name + "' already exists in this owner");
        }
    }

    void SemanticModel::notify(ModelChange::Type type, std::uint64_t id) const {
        // Copy so that listeners may add/remove listeners while being notified.
        const auto listeners = Listeners;
        for (const auto &[token, listener]: listeners)
            listener(ModelChange{type, id});
    }
}
