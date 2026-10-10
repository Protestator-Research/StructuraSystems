//
// Semantic layer of the diagram editor: the SysML v2 element tree plus the relationships.
//

#include "SemanticModel.h"
#include "RelationshipRules.h"

#include <algorithm>

namespace StructuraSystems::Diagram {

    // --- elements -----------------------------------------------------------------------

    ElementId SemanticModel::addElement(ElementKind kind, const std::string &name, ElementId owner) {
        if (name.empty() && kind != ElementKind::Textual && !mayBeUnnamed(kind))
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
        if (target.Name == name)
            return;
        if (name.empty()) {
            const auto inside = [&](ElementId end) { return end == id || isAncestor(id, end); };
            for (const auto &relationship: Relationships) {
                if (inside(relationship.Target) ||
                    (inside(relationship.Source) && !(relationship.Source == id && (relationship.Kind == RelationshipKind::Verify ||
                                                                                    relationship.Kind == RelationshipKind::Include))))
                    throw ModelError("The element is referenced by a relationship and needs a name");
            }
        }
        ensureUniqueSiblingName(target.Owner, name, id, target.Kind);
        target.Name = name;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setTypeName(ElementId id, const std::string &typeName) {
        auto &target = mutableElement(id);
        if (target.Kind == ElementKind::Package || isDefinitionKind(target.Kind) || target.Kind == ElementKind::Textual ||
            target.Kind == ElementKind::EnumerationLiteral)
            throw ModelError(toString(target.Kind) + " cannot be typed");
        target.TypeName = typeName;
        notify(ModelChange::Type::ElementChanged, id);
    }

    void SemanticModel::setMultiplicity(ElementId id, std::optional<Multiplicity> multiplicity) {
        auto &target = mutableElement(id);
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
        if (target.Kind != ElementKind::Attribute && target.Kind != ElementKind::PartUsage &&
            target.Kind != ElementKind::Subject && target.Kind != ElementKind::OccurrenceUsage)
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
        const auto named = [&](ElementId end) {
            for (const ElementId element: ownershipPath(end)) {
                if (this->element(element).name().empty())
                    return false;
            }
            return true;
        };
        for (const auto &relationship: Relationships) {
            // ends inside an unnamed element ("objective { ... }") cannot be referenced
            if (!named(relationship.Target) || (!named(relationship.Source) && relationship.Kind != RelationshipKind::Verify &&
                                                relationship.Kind != RelationshipKind::Include)) {
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
        Relationships.push_back(Relationship{id, kind, source, target, {}, {}, {}, {}, {}, false, std::nullopt, std::nullopt});
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
        if (it->Kind != RelationshipKind::Message && it->Kind != RelationshipKind::Allocation &&
            it->Kind != RelationshipKind::Dependency)
            throw ModelError(toString(it->Kind) + " is always declared in the owner of its ends");
        if (context && *context != RootId &&
            (!contains(*context) || element(*context).kind() == ElementKind::Textual || *context == it->Source ||
             *context == it->Target))
            throw ModelError("A relationship cannot be declared there");
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
            it->Kind == RelationshipKind::Include ||
            ((it->Kind == RelationshipKind::Succession || it->Kind == RelationshipKind::Transition ||
              it->Kind == RelationshipKind::Dependency) && !typeName.empty())) {
            if (!name.empty() || !typeName.empty())
                throw ModelError(toString(it->Kind) + " has no name or type");
            return;
        }
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
