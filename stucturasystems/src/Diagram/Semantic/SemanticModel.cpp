//
// Semantic layer of the diagram editor: the SysML v2 element tree plus the relationships.
//

#include "SemanticModel.h"
#include "RelationshipRules.h"

#include <algorithm>

namespace StructuraSystems::Diagram {

    // --- elements -----------------------------------------------------------------------

    ElementId SemanticModel::addElement(ElementKind kind, const std::string &name, ElementId owner) {
        if (name.empty() && kind != ElementKind::Textual)
            throw ModelError("Element name must not be empty");
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
        for (const RelationshipId relationshipId: danglingRelationships)
            removeRelationship(relationshipId);

        auto &siblings = mutableChildren(element(id).owner());
        siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());

        for (auto it = subtree.rbegin(); it != subtree.rend(); ++it) {
            Elements.erase(*it);
            notify(ModelChange::Type::ElementRemoved, *it);
        }
    }

    void SemanticModel::rename(ElementId id, const std::string &name) {
        auto &target = mutableElement(id);
        if (name.empty() && target.Kind != ElementKind::Textual)
            throw ModelError("Element name must not be empty");
        if (target.Name == name)
            return;
        ensureUniqueSiblingName(target.Owner, name, id, target.Kind);
        target.Name = name;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setTypeName(ElementId id, const std::string &typeName) {
        auto &target = mutableElement(id);
        if (target.Kind == ElementKind::Package || isDefinitionKind(target.Kind) || target.Kind == ElementKind::Textual)
            throw ModelError(toString(target.Kind) + " cannot be typed");
        target.TypeName = typeName;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setMultiplicity(ElementId id, std::optional<Multiplicity> multiplicity) {
        auto &target = mutableElement(id);
        if (target.Kind != ElementKind::PartUsage && target.Kind != ElementKind::Attribute &&
            target.Kind != ElementKind::ConstraintUsage && target.Kind != ElementKind::CalcUsage)
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

    void SemanticModel::setPrefixes(ElementId id, std::vector<std::string> prefixes) {
        auto &target = mutableElement(id);
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
        if (target.Kind != ElementKind::Attribute && target.Kind != ElementKind::PartUsage)
            throw ModelError(toString(target.Kind) + " cannot have a value");
        const std::string newOperator = expression.empty() ? std::string() : (op.empty() ? std::string("=") : op);
        if (target.ValueOperator == newOperator && target.Value == expression)
            return;
        target.ValueOperator = newOperator;
        target.Value = expression;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setExternalSpecializations(ElementId id, std::vector<std::string> references) {
        auto &target = mutableElement(id);
        if (target.Kind == ElementKind::Textual || target.Kind == ElementKind::Package)
            throw ModelError(toString(target.Kind) + " cannot specialize");
        if (target.ExternalSpecializations == references)
            return;
        target.ExternalSpecializations = std::move(references);
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
        if (keyword.has_value()) {
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

        // A connection/flow between an element and its new (indirect) owner is no longer valid.
        std::vector<RelationshipId> invalid;
        for (const auto &relationship: Relationships) {
            if (relationship.Kind == RelationshipKind::Specialization)
                continue;
            if (isAncestor(relationship.Source, relationship.Target) ||
                isAncestor(relationship.Target, relationship.Source))
                invalid.push_back(relationship.Id);
        }
        for (const RelationshipId relationshipId: invalid)
            removeRelationship(relationshipId);

        notify(ModelChange::Type::ElementReparented, id);
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
        Relationships.push_back(Relationship{id, kind, source, target});
        notify(ModelChange::Type::RelationshipAdded, id);
        return id;
    }

    void SemanticModel::removeRelationship(RelationshipId id) {
        const auto it = std::find_if(Relationships.begin(), Relationships.end(),
                                     [id](const Relationship &relationship) { return relationship.Id == id; });
        if (it == Relationships.end())
            throw ModelError("Unknown relationship id " + std::to_string(id));
        Relationships.erase(it);
        notify(ModelChange::Type::RelationshipRemoved, id);
    }

    bool SemanticModel::containsRelationship(RelationshipId id) const {
        return std::any_of(Relationships.begin(), Relationships.end(),
                           [id](const Relationship &relationship) { return relationship.Id == id; });
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
        if (kind == ElementKind::Textual)
            return; // statements kept as text are not checked (overloads, imports, comments, ...)
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
