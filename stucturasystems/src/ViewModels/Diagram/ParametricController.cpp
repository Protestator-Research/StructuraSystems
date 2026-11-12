//
// Parametric view: values, calls, expressions, constraint / calc usages and the lines between them.
//

#include "ParametricController.h"
#include "DiagramController.h"
#include "DiagramGeometry.h"

#include <QMetaObject>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/RelationshipRules.h>

#include <algorithm>
#include <exception>
#include <functional>

namespace StructuraSystems::Client {

    using Diagram::CallExpression;
    using Diagram::ElementId;
    using Diagram::ElementKind;
    using Diagram::RelationshipKind;
    using Diagram::RootId;
    using Diagram::SysMLCodeGenerator;
    using Diagram::SysMLTextImporter;

    namespace {
        constexpr qreal Margin = 40.0;
        constexpr qreal ColumnWidth = 320.0; ///< distance of a new call from its value
        constexpr qreal ColumnGap = 90.0;
        constexpr qreal RowGap = 28.0;
        constexpr qreal PinGrip = 10.0;
        constexpr qreal EdgeHitDistance = 6.0;
        constexpr int MaximumTitleLength = 56;

        bool isPartLike(ElementKind kind) {
            return kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage;
        }

        bool isUsage(ElementKind kind) {
            return kind == ElementKind::ConstraintUsage || kind == ElementKind::CalcUsage;
        }

        /// in / out / inout / return of a parameter, empty for an ordinary attribute.
        std::string directionOf(const Diagram::SysMLElement &element) {
            std::string direction;
            for (const auto &prefix: element.prefixes()) {
                if (prefix == "in" || prefix == "out" || prefix == "inout" || prefix == "return")
                    direction = prefix;
            }
            return direction;
        }

        bool isInput(const std::string &direction) {
            return direction == "in" || direction == "inout";
        }

        bool isResult(const std::string &direction) {
            return direction == "return" || direction == "out";
        }

        QString elide(const QString &text, int length = MaximumTitleLength) {
            return text.size() <= length ? text : text.left(length - 1) + QChar(0x2026);
        }

        QString q(const std::string &text) {
            return QString::fromStdString(text);
        }

        std::string lowerFirst(std::string text) {
            if (!text.empty() && text.front() >= 'A' && text.front() <= 'Z')
                text.front() = char(text.front() - 'A' + 'a');
            return text;
        }

        QString nodeKey(std::uint64_t node) {
            return QString::number(node);
        }
    }

    ParametricController::ParametricController(Diagram::SemanticModel &model, DiagramController *structure,
                                               QObject *parent) :
            QObject(parent),
            Model(model),
            Structure(structure),
            Nodes(new DiagramNodeModel(this)),
            Edges(new DiagramEdgeModel(this)) {
        ListenerToken = Model.addListener([this](const Diagram::ModelChange &) { scheduleSync(); });
        connect(Nodes, &DiagramNodeModel::geometryChanged, this, &ParametricController::updateCanvasSize);
        syncNow();
    }

    ParametricController::~ParametricController() {
        Model.removeListener(ListenerToken);
    }

    // --- context -------------------------------------------------------------------------------------

    std::optional<ElementId> ParametricController::context() const {
        if (ContextElement && Model.contains(*ContextElement) && isPartLike(Model.element(*ContextElement).kind()))
            return ContextElement;
        return std::nullopt;
    }

    QString ParametricController::contextId() const {
        const auto element = context();
        return element ? QString::number(*element) : QString();
    }

    void ParametricController::setContextId(const QString &id) {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || !Model.contains(value) || !isPartLike(Model.element(value).kind()) || ContextElement == value)
            return;
        ContextElement = value;
        Positions.clear();
        SelectedNodes.clear();
        SelectedEdges.clear();
        AutoLayout = true;
        emit contextChanged();
        emit selectionChanged();
        syncNow();
    }

    void ParametricController::chooseContext() {
        if (context())
            return;
        std::optional<ElementId> first;
        std::optional<ElementId> withContent;
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                if (isPartLike(element.kind())) {
                    if (!first)
                        first = child;
                    if (!withContent) {
                        for (const ElementId feature: element.ownedElements()) {
                            const ElementKind kind = Model.element(feature).kind();
                            if (kind == ElementKind::Attribute || isUsage(kind)) {
                                withContent = child;
                                break;
                            }
                        }
                    }
                }
                visit(child);
            }
        };
        visit(RootId);
        const auto chosen = withContent ? withContent : first;
        if (chosen != ContextElement) {
            ContextElement = chosen;
            Positions.clear();
            AutoLayout = true;
            emit contextChanged();
        }
    }

    void ParametricController::updateLists() {
        QVariantList contexts;
        QVariantList definitions;
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                if (isPartLike(element.kind())) {
                    const QString keyword = element.kind() == ElementKind::PartDefinition ? QStringLiteral("part def ")
                                                                                        : QStringLiteral("part ");
                    contexts.append(QVariantMap{{QStringLiteral("id"), QString::number(child)},
                                                {QStringLiteral("title"), keyword + q(Model.qualifiedName(child))}});
                } else if (element.kind() == ElementKind::ConstraintDefinition ||
                           element.kind() == ElementKind::CalcDefinition) {
                    const bool calc = element.kind() == ElementKind::CalcDefinition;
                    const std::string keyword = element.declaredKeyword().value_or(calc ? "calc def" : "constraint def");
                    definitions.append(QVariantMap{{QStringLiteral("id"), QString::number(child)},
                                                   {QStringLiteral("name"), q(element.name())},
                                                   {QStringLiteral("keyword"), q(keyword)},
                                                   {QStringLiteral("calc"), calc}});
                }
                visit(child);
            }
        };
        visit(RootId);
        if (contexts != Contexts || definitions != Definitions) {
            Contexts = contexts;
            Definitions = definitions;
            emit contextsChanged();
        }
    }

    // --- name resolution -----------------------------------------------------------------------------

    std::optional<ElementId> ParametricController::resolve(const std::string &reference,
                                                           const std::function<bool(ElementId)> &accept) const {
        const auto segments = SysMLTextImporter::splitReference(reference);
        const auto start = context();
        if (segments.empty() || !start)
            return std::nullopt;
        const auto child = [&](ElementId owner, const std::string &name, bool last) -> std::optional<ElementId> {
            for (const ElementId id: Model.children(owner)) {
                if (Model.element(id).name() == name && (!last || !accept || accept(id)))
                    return id;
            }
            return std::nullopt;
        };
        // innermost scope outwards for the first segment, then down
        std::optional<ElementId> current;
        for (ElementId scope = *start;; scope = Model.element(scope).owner()) {
            current = child(scope, segments.front(), segments.size() == 1);
            if (current || scope == RootId)
                break;
        }
        for (std::size_t i = 1; current && i < segments.size(); ++i)
            current = child(*current, segments[i], i + 1 == segments.size());
        return current;
    }

    std::optional<ElementId> ParametricController::definitionOf(const std::string &reference, bool calc) const {
        const ElementKind kind = calc ? ElementKind::CalcDefinition : ElementKind::ConstraintDefinition;
        if (const auto found = resolve(reference, [&](ElementId id) { return Model.element(id).kind() == kind; }))
            return found;
        // e.g. imported: any definition of that name
        const auto segments = SysMLTextImporter::splitReference(reference);
        if (segments.empty())
            return std::nullopt;
        std::optional<ElementId> result;
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId id: Model.children(owner)) {
                if (result)
                    return;
                if (Model.element(id).kind() == kind && Model.element(id).name() == segments.back())
                    result = id;
                visit(id);
            }
        };
        visit(RootId);
        return result;
    }

    std::string ParametricController::referenceTo(ElementId element) const {
        const auto start = context();
        const auto path = Model.ownershipPath(element);
        // inside the context: usages chained with '.'
        if (start) {
            const auto position = std::find(path.begin(), path.end(), *start);
            if (position != path.end()) {
                std::string reference;
                for (auto it = position + 1; it != path.end(); ++it)
                    reference += (reference.empty() ? "" : ".") + SysMLCodeGenerator::formatName(Model.element(*it).name());
                return reference;
            }
            // visible from the context (owner is the context or one of its owners): the plain name
            const ElementId owner = Model.element(element).owner();
            if (owner == RootId || owner == *start || Model.isAncestor(owner, *start))
                return SysMLCodeGenerator::formatName(Model.element(element).name());
        }
        std::string reference;
        for (const ElementId id: path)
            reference += (reference.empty() ? "" : "::") + SysMLCodeGenerator::formatName(Model.element(id).name());
        return reference;
    }

    std::string ParametricController::uniqueName(const std::string &base, ElementId owner) const {
        const auto &siblings = Model.children(owner);
        const auto used = [&](const std::string &name) {
            return std::any_of(siblings.begin(), siblings.end(), [&](ElementId sibling) {
                return Model.element(sibling).kind() != ElementKind::Textual && Model.element(sibling).name() == name;
            });
        };
        if (!used(base))
            return base;
        for (int index = 1;; ++index) {
            const std::string candidate = base + std::to_string(index);
            if (!used(candidate))
                return candidate;
        }
    }

    std::vector<ElementId> ParametricController::parametersOf(ElementId owner, bool inputs) const {
        std::vector<ElementId> result;
        for (const ElementId child: Model.children(owner)) {
            const auto &element = Model.element(child);
            if (element.kind() != ElementKind::Attribute)
                continue;
            const std::string direction = directionOf(element);
            if (inputs ? isInput(direction) : isResult(direction))
                result.push_back(child);
        }
        return result;
    }

    std::vector<ParametricController::Pin> ParametricController::usagePins(ElementId usage) const {
        const auto &element = Model.element(usage);
        std::vector<Pin> pins;
        const auto own = parametersOf(usage, true);
        std::vector<std::string> names;
        if (!element.typeName().empty()) {
            if (const auto definition = definitionOf(element.typeName(), element.kind() == ElementKind::CalcUsage)) {
                for (const ElementId parameter: parametersOf(*definition, true))
                    names.push_back(Model.element(parameter).name());
            }
        }
        for (const auto &name: names) {
            Pin pin;
            pin.Name = name;
            for (const ElementId parameter: own) {
                if (Model.element(parameter).name() == name) {
                    pin.Parameter = parameter;
                    pin.Value = Model.element(parameter).value();
                }
            }
            pins.push_back(pin);
        }
        for (const ElementId parameter: own) {
            const std::string &name = Model.element(parameter).name();
            if (std::find(names.begin(), names.end(), name) != names.end())
                continue;
            pins.push_back({name, parameter, Model.element(parameter).value()});
        }
        return pins;
    }

    std::optional<ParametricController::Endpoint> ParametricController::endpointOf(ElementId element) const {
        const auto start = context();
        if (!start || !Model.contains(element))
            return std::nullopt;
        const auto &target = Model.element(element);
        if (target.owner() == *start && target.kind() == ElementKind::Attribute && directionOf(target).empty())
            return Endpoint{element, -1, true};
        if (target.owner() == *start && target.kind() == ElementKind::CalcUsage)
            return Endpoint{element, 0, true};
        if (target.kind() != ElementKind::Attribute || target.owner() == RootId)
            return std::nullopt;
        const ElementId usage = target.owner();
        if (Model.element(usage).owner() != *start || !isUsage(Model.element(usage).kind()))
            return std::nullopt;
        if (isResult(directionOf(target)))
            return Endpoint{usage, 0, true};
        const auto pins = usagePins(usage);
        for (std::size_t i = 0; i < pins.size(); ++i) {
            if (pins[i].Parameter == element)
                return Endpoint{usage, int(i), false};
        }
        return std::nullopt;
    }

    // --- building the graph --------------------------------------------------------------------------

    void ParametricController::scheduleSync() {
        if (SyncQueued)
            return;
        SyncQueued = true;
        QMetaObject::invokeMethod(this, [this] {
            if (SyncQueued)
                syncNow();
        }, Qt::QueuedConnection);
    }

    void ParametricController::build(std::vector<DiagramNode> &nodes, std::vector<DiagramEdge> &edges) {
        Info.clear();
        const auto start = context();
        if (!start)
            return;

        const auto valueNodeOf = [&](const std::string &reference, ElementId self) -> std::optional<ElementId> {
            const auto target = resolve(reference, [&](ElementId id) { return Model.element(id).kind() == ElementKind::Attribute; });
            if (!target || *target == self)
                return std::nullopt;
            const auto &element = Model.element(*target);
            if (element.owner() != *start || !directionOf(element).empty())
                return std::nullopt;
            return target;
        };
        const auto addEdge = [&](DiagramTypes::EdgeKind kind, std::uint64_t source, int sourcePin, std::uint64_t target,
                                 int targetPin, const QString &key) {
            DiagramEdge edge;
            edge.Kind = kind;
            edge.Source = source;
            edge.SourcePin = sourcePin;
            edge.Target = target;
            edge.TargetPin = targetPin;
            edge.Key = key;
            edge.Selected = SelectedEdges.count(key) > 0;
            edges.push_back(edge);
        };
        const auto pinMap = [](const std::string &name, const QString &text, bool open) {
            return QVariantMap{{QStringLiteral("name"), q(name)}, {QStringLiteral("text"), text}, {QStringLiteral("open"), open}};
        };

        // 1. values and usages
        std::vector<ElementId> values;
        std::vector<ElementId> usages;
        for (const ElementId child: Model.children(*start)) {
            const auto &element = Model.element(child);
            if (element.kind() == ElementKind::Attribute && directionOf(element).empty()) {
                values.push_back(child);
                DiagramNode node;
                node.Id = child;
                node.Kind = DiagramTypes::ValueNode;
                QString metadata;
                bool derived = false;
                for (const auto &prefix: element.prefixes()) {
                    if (!prefix.empty() && prefix.front() == '#')
                        metadata += (metadata.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("«") +
                                    q(prefix.substr(1)) + QStringLiteral("»");
                    else if (prefix == "derived")
                        derived = true;
                }
                node.Keyword = metadata;
                QString title = (derived ? QStringLiteral("/") : QString()) + q(element.name());
                if (!element.typeName().empty())
                    title += QStringLiteral(" : ") + q(element.typeName());
                if (element.multiplicity())
                    title += q(element.multiplicity()->toString());
                node.Title = elide(title);
                nodes.push_back(node);
                Info[child] = NodeInfo{DiagramTypes::ValueNode, child, {}, {}};
            } else if (isUsage(element.kind())) {
                usages.push_back(child);
            }
        }

        // 2. how each value is computed
        for (const ElementId value: values) {
            const auto &element = Model.element(value);
            if (element.value().empty())
                continue;
            const std::string &expression = element.value();
            const QString valueKey = QStringLiteral("v:") + QString::number(value);

            if (const auto reference = SysMLTextImporter::parseReference(expression)) {
                if (const auto source = valueNodeOf(*reference, value)) {
                    addEdge(DiagramTypes::ValueFlow, *source, -1, value, -1, valueKey);
                    continue;
                }
                const auto target = resolve(*reference, {});
                if (target) {
                    const auto endpoint = endpointOf(*target);
                    if (endpoint && endpoint->Output && Model.element(endpoint->Node).kind() == ElementKind::CalcUsage) {
                        addEdge(DiagramTypes::ValueFlow, endpoint->Node, 0, value, -1, valueKey);
                        continue;
                    }
                }
            }

            const std::uint64_t computation = computationNodeId(value);
            DiagramNode node;
            node.Id = computation;
            NodeInfo info;
            info.Element = value;
            if (const auto call = SysMLTextImporter::parseCall(expression)) {
                const auto definition = definitionOf(call->Function, true);
                std::vector<ElementId> parameters;
                std::string resultName = "result";
                if (definition) {
                    parameters = parametersOf(*definition, true);
                    const auto results = parametersOf(*definition, false);
                    if (!results.empty())
                        resultName = Model.element(results.front()).name();
                }
                info.Kind = DiagramTypes::CallNode;
                info.ResultName = resultName;
                node.Kind = DiagramTypes::CallNode;
                node.Keyword = QStringLiteral("«") +
                               q(definition ? Model.element(*definition).declaredKeyword().value_or("calc") : std::string("calc")) +
                               QStringLiteral("»");
                if (node.Keyword == QStringLiteral("«calc def»"))
                    node.Keyword = QStringLiteral("«calc»");
                node.Title = elide(q(call->Function));
                const std::size_t count = std::max(call->Arguments.size(), parameters.size());
                for (std::size_t i = 0; i < count; ++i) {
                    Pin pin;
                    if (i < call->Arguments.size() && !call->Arguments[i].Name.empty())
                        pin.Name = call->Arguments[i].Name;
                    else if (i < parameters.size())
                        pin.Name = Model.element(parameters[i]).name();
                    else
                        pin.Name = "arg" + std::to_string(i + 1);
                    QString text;
                    bool open = false;
                    if (i < call->Arguments.size()) {
                        pin.Value = call->Arguments[i].Value;
                        const auto reference = SysMLTextImporter::parseReference(pin.Value);
                        const auto source = reference ? valueNodeOf(*reference, value) : std::nullopt;
                        if (source) {
                            addEdge(DiagramTypes::Binding, *source, -1, computation, int(i),
                                    QStringLiteral("a:%1:%2").arg(value).arg(i));
                        } else {
                            // a name that does not stand for anything here (e.g. the placeholder of a new call)
                            open = reference.has_value() && !resolve(*reference, {});
                            if (!(open && *reference == pin.Name))
                                text = elide(q(SysMLTextImporter::oneLine(pin.Value)), 24);
                        }
                    } else {
                        text = QStringLiteral("—");
                        open = true;
                    }
                    node.InPins.append(pinMap(pin.Name, text, open));
                    info.Inputs.push_back(pin);
                }
                node.OutPins.append(pinMap(resultName, QString(), false));
                addEdge(DiagramTypes::ValueFlow, computation, 0, value, -1, valueKey);
            } else {
                info.Kind = DiagramTypes::ExpressionNode;
                node.Kind = DiagramTypes::ExpressionNode;
                node.Keyword = QStringLiteral("«expression»");
                node.Title = elide(q(SysMLTextImporter::oneLine(expression)));
                for (const auto &reference: SysMLTextImporter::references(expression)) {
                    if (const auto source = valueNodeOf(reference, value))
                        addEdge(DiagramTypes::Reference, *source, -1, computation, -1,
                                QStringLiteral("x:%1:%2").arg(computation).arg(*source));
                }
                addEdge(DiagramTypes::ValueFlow, computation, -1, value, -1, valueKey);
            }
            nodes.push_back(node);
            Info[computation] = info;
        }

        // 3. constraints and calculations
        for (const ElementId usage: usages) {
            const auto &element = Model.element(usage);
            const bool calc = element.kind() == ElementKind::CalcUsage;
            DiagramNode node;
            node.Id = usage;
            node.Kind = calc ? DiagramTypes::CalcNode : DiagramTypes::ConstraintNode;
            node.Keyword = QStringLiteral("«") + q(element.declaredKeyword().value_or(calc ? "calc" : "constraint")) +
                           QStringLiteral("»");
            QString title = q(element.name());
            if (!element.typeName().empty())
                title += QStringLiteral(" : ") + q(element.typeName());
            node.Title = elide(title);
            NodeInfo info;
            info.Kind = node.Kind;
            info.Element = usage;
            info.Inputs = usagePins(usage);
            for (std::size_t i = 0; i < info.Inputs.size(); ++i) {
                const auto &pin = info.Inputs[i];
                const auto reference = SysMLTextImporter::parseReference(pin.Value);
                const auto source = reference ? valueNodeOf(*reference, usage) : std::nullopt;
                QString text;
                if (source && pin.Parameter)
                    addEdge(DiagramTypes::Binding, *source, -1, usage, int(i), QStringLiteral("p:%1").arg(*pin.Parameter));
                else if (!pin.Value.empty())
                    text = elide(q(SysMLTextImporter::oneLine(pin.Value)), 24);
                node.InPins.append(pinMap(pin.Name, text, false));
            }
            if (calc) {
                std::string resultName = "result";
                const auto own = parametersOf(usage, false);
                if (!own.empty()) {
                    resultName = Model.element(own.front()).name();
                } else if (!element.typeName().empty()) {
                    if (const auto definition = definitionOf(element.typeName(), true)) {
                        const auto results = parametersOf(*definition, false);
                        if (!results.empty())
                            resultName = Model.element(results.front()).name();
                    }
                }
                info.ResultName = resultName;
                node.OutPins.append(pinMap(resultName, QString(), false));
            }
            // values used in the expression (own parameters shadow attributes of the same name)
            if (!element.expression().empty()) {
                for (const auto &reference: SysMLTextImporter::references(element.expression())) {
                    const auto segments = SysMLTextImporter::splitReference(reference);
                    const bool own = !segments.empty() && std::any_of(info.Inputs.begin(), info.Inputs.end(),
                                                                      [&](const Pin &pin) { return pin.Name == segments.front(); });
                    if (own)
                        continue;
                    if (const auto source = valueNodeOf(reference, usage))
                        addEdge(DiagramTypes::Reference, *source, -1, usage, -1,
                                QStringLiteral("x:%1:%2").arg(usage).arg(*source));
                }
            }
            nodes.push_back(node);
            Info[usage] = info;
        }

        // 4. bind a = b;
        for (const auto &relationship: Model.relationships()) {
            if (relationship.Kind != RelationshipKind::Binding)
                continue;
            const auto bound = endpointOf(relationship.Source);
            const auto value = endpointOf(relationship.Target);
            if (!bound || !value)
                continue;
            Endpoint from = *value;
            Endpoint to = *bound;
            if (from.Pin >= 0 && !from.Output && (to.Pin < 0 || to.Output))
                std::swap(from, to);
            addEdge(DiagramTypes::Binding, from.Node, from.Output ? from.Pin : -1, to.Node, to.Output ? -1 : to.Pin,
                    QStringLiteral("b:%1").arg(relationship.Id));
        }
    }

    QSizeF ParametricController::sizeOf(const DiagramNode &node) const {
        qreal width = std::max(node.Title.size() * 7.8, node.Keyword.size() * 6.6) + 32;
        int maxIn = 0;
        int maxOut = 0;
        for (const auto &pin: node.InPins) {
            const auto map = pin.toMap();
            const int text = int(map.value(QStringLiteral("text")).toString().size());
            maxIn = std::max(maxIn, int(map.value(QStringLiteral("name")).toString().size()) + (text > 0 ? text + 3 : 0));
        }
        for (const auto &pin: node.OutPins)
            maxOut = std::max(maxOut, int(pin.toMap().value(QStringLiteral("name")).toString().size()));
        width = std::max(width, (maxIn + maxOut) * 6.8 + 56);
        width = std::clamp<qreal>(width, 130, 460);
        const int rows = int(std::max(node.InPins.size(), node.OutPins.size()));
        const qreal height = rows == 0 ? (node.Keyword.isEmpty() ? 40.0 : 54.0)
                                       : DiagramGeometry::PinTop + rows * DiagramGeometry::PinRowHeight + 8;
        return {std::round(width), height};
    }

    void ParametricController::syncNow() {
        SyncQueued = false;
        updateLists();
        chooseContext();

        std::vector<DiagramNode> nodes;
        std::vector<DiagramEdge> edges;
        build(nodes, edges);

        std::set<std::uint64_t> wanted;
        for (const auto &node: nodes)
            wanted.insert(node.Id);
        for (int row = Nodes->count() - 1; row >= 0; --row) {
            if (wanted.count(Nodes->nodes()[std::size_t(row)].Id) == 0)
                Nodes->removeAt(row);
        }
        for (auto it = SelectedNodes.begin(); it != SelectedNodes.end();)
            it = wanted.count(*it) ? std::next(it) : SelectedNodes.erase(it);

        // positions: automatic layout from left (inputs) to right (results and constraints)
        bool missing = false;
        for (const auto &node: nodes)
            missing = missing || Positions.count(node.Id) == 0;
        if (AutoLayout && missing) {
            std::map<std::uint64_t, int> level;
            for (const auto &node: nodes)
                level[node.Id] = 0;
            for (std::size_t pass = 0; pass < nodes.size(); ++pass) {
                bool changed = false;
                for (const auto &edge: edges) {
                    if (level.count(edge.Source) && level.count(edge.Target) && level[edge.Target] < level[edge.Source] + 1 &&
                        level[edge.Source] + 1 <= int(nodes.size())) {
                        level[edge.Target] = level[edge.Source] + 1;
                        changed = true;
                    }
                }
                if (!changed)
                    break;
            }
            // constraints at the very right
            int maximum = 0;
            for (const auto &[id, value]: level)
                maximum = std::max(maximum, value);
            for (const auto &node: nodes) {
                if (node.Kind == DiagramTypes::ConstraintNode)
                    level[node.Id] = std::max(level[node.Id], maximum);
            }
            // columns as wide as their widest node
            std::map<int, qreal> columnWidth;
            for (const auto &node: nodes)
                columnWidth[level[node.Id]] = std::max(columnWidth[level[node.Id]], sizeOf(node).width());
            std::map<int, qreal> columnX;
            qreal x = Margin;
            for (const auto &[column, width]: columnWidth) {
                columnX[column] = x;
                x += width + ColumnGap;
            }
            std::map<int, qreal> columnBottom;
            for (const auto &node: nodes) {
                const int column = level[node.Id];
                const qreal y = columnBottom.count(column) ? columnBottom[column] : Margin;
                Positions[node.Id] = QPointF(columnX[column], y);
                columnBottom[column] = y + sizeOf(node).height() + RowGap;
            }
        } else if (missing) {
            const QRectF bounds = contentBounds();
            qreal y = bounds.isNull() ? Margin : bounds.bottom() + RowGap;
            for (const auto &node: nodes) {
                if (Positions.count(node.Id))
                    continue;
                // a new call / expression next to its value
                const auto info = Info.find(node.Id);
                if (info != Info.end() && node.Id != info->second.Element && Positions.count(info->second.Element)) {
                    const QPointF value = Positions[info->second.Element];
                    Positions[node.Id] = QPointF(std::max(Margin, value.x() - ColumnWidth), value.y());
                    continue;
                }
                Positions[node.Id] = QPointF(Margin, y);
                y += sizeOf(node).height() + RowGap;
            }
        }

        for (auto &node: nodes) {
            node.Selected = SelectedNodes.count(node.Id) > 0;
            const QRectF rect(Positions[node.Id], sizeOf(node));
            const int row = Nodes->indexOf(node.Id);
            if (row >= 0) {
                Nodes->updateContent(row, node);
                Nodes->setRect(row, rect);
                Nodes->setSelected(row, node.Selected);
            } else {
                node.Rect = rect;
                Nodes->append(node);
            }
        }

        for (auto it = SelectedEdges.begin(); it != SelectedEdges.end();) {
            const bool exists = std::any_of(edges.begin(), edges.end(), [&](const DiagramEdge &edge) { return edge.Key == *it; });
            it = exists ? std::next(it) : SelectedEdges.erase(it);
        }
        Edges->setEdges(std::move(edges));
        updateCanvasSize();
        emit selectionChanged();
    }

    void ParametricController::updateCanvasSize() {
        const QRectF bounds = contentBounds();
        const QSizeF size(std::max<qreal>(1600, bounds.right() + 600), std::max<qreal>(1000, bounds.bottom() + 600));
        if (size != CanvasSize) {
            CanvasSize = size;
            emit canvasSizeChanged();
        }
    }

    QRectF ParametricController::contentBounds() const {
        QRectF bounds;
        for (const auto &node: Nodes->nodes())
            bounds = bounds.isNull() ? node.Rect : bounds.united(node.Rect);
        return bounds;
    }

    void ParametricController::layoutAll() {
        Positions.clear();
        AutoLayout = true;
        syncNow();
    }

    // --- hit testing -----------------------------------------------------------------------------------

    std::optional<std::uint64_t> ParametricController::toNode(const QString &id) const {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || Info.count(value) == 0)
            return std::nullopt;
        return value;
    }

    bool ParametricController::isValueNode(std::uint64_t node) const {
        const auto it = Info.find(node);
        return it != Info.end() && it->second.Kind == DiagramTypes::ValueNode;
    }

    QVariantMap ParametricController::hitTest(qreal x, qreal y) const {
        const QPointF point(x, y);
        const auto &nodes = Nodes->nodes();
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            const QRectF grip = it->Rect.adjusted(-PinGrip, 0, PinGrip, 0);
            if (!grip.contains(point))
                continue;
            int pin = -1;
            bool output = false;
            const qreal row = (y - it->Rect.top() - DiagramGeometry::PinTop) / DiagramGeometry::PinRowHeight;
            if (row >= 0) {
                const int index = int(row);
                if (std::abs(x - it->Rect.left()) <= PinGrip && index < it->InPins.size()) {
                    pin = index;
                } else if (std::abs(x - it->Rect.right()) <= PinGrip && index < it->OutPins.size()) {
                    pin = index;
                    output = true;
                }
            }
            if (pin < 0 && !it->Rect.contains(point)) {
                // just outside the border: the port of a value (pin -1)
                const QPointF port = DiagramGeometry::pinPoint(it->Rect, x > it->Rect.center().x(), -1);
                if (std::abs(y - port.y()) > PinGrip)
                    continue;
                output = x > it->Rect.center().x();
            }
            return {{QStringLiteral("node"), nodeKey(it->Id)},
                    {QStringLiteral("element"), elementOf(nodeKey(it->Id))},
                    {QStringLiteral("pin"), pin},
                    {QStringLiteral("output"), output}};
        }
        return {{QStringLiteral("node"), QString()}, {QStringLiteral("pin"), -1}, {QStringLiteral("output"), false}};
    }

    QString ParametricController::edgeAt(qreal x, qreal y) const {
        const QPointF point(x, y);
        const auto &edges = Edges->edges();
        for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
            const auto *source = Nodes->find(it->Source);
            const auto *target = Nodes->find(it->Target);
            if (!source || !target)
                continue;
            const auto points = DiagramGeometry::pinCurve(DiagramGeometry::pinPoint(source->Rect, true, it->SourcePin),
                                                          DiagramGeometry::pinPoint(target->Rect, false, it->TargetPin));
            for (std::size_t i = 1; i < points.size(); ++i) {
                if (DiagramGeometry::distanceToSegment(point, QLineF(points[i - 1], points[i])) <= EdgeHitDistance)
                    return it->Key;
            }
        }
        return {};
    }

    QPointF ParametricController::pinPosition(const QString &node, int pin, bool output) const {
        const auto id = toNode(node);
        const auto *found = id ? Nodes->find(*id) : nullptr;
        return found ? DiagramGeometry::pinPoint(found->Rect, output, pin) : QPointF();
    }

    QRectF ParametricController::nodeRect(const QString &node) const {
        const auto id = toNode(node);
        const auto *found = id ? Nodes->find(*id) : nullptr;
        return found ? found->Rect : QRectF();
    }

    QString ParametricController::elementOf(const QString &node) const {
        const auto id = toNode(node);
        return id ? QString::number(Info.at(*id).Element) : QString();
    }

    // --- selection and moving ------------------------------------------------------------------------

    void ParametricController::applySelection() {
        for (int row = 0; row < Nodes->count(); ++row)
            Nodes->setSelected(row, SelectedNodes.count(Nodes->nodes()[std::size_t(row)].Id) > 0);
        auto edges = Edges->edges();
        for (auto &edge: edges)
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
        Edges->setEdges(std::move(edges));
        emit selectionChanged();
    }

    void ParametricController::select(const QString &node, bool additive) {
        const auto id = toNode(node);
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (id)
            SelectedNodes.insert(*id);
        applySelection();
    }

    void ParametricController::selectEdge(const QString &key, bool additive) {
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (Edges->find(key))
            SelectedEdges.insert(key);
        applySelection();
    }

    void ParametricController::clearSelection() {
        SelectedNodes.clear();
        SelectedEdges.clear();
        applySelection();
    }

    void ParametricController::moveNode(const QString &node, qreal x, qreal y) {
        const auto id = toNode(node);
        if (!id)
            return;
        AutoLayout = false;
        Positions[*id] = QPointF(std::max<qreal>(0, x), std::max<qreal>(0, y));
        const int row = Nodes->indexOf(*id);
        if (row >= 0)
            Nodes->setRect(row, QRectF(Positions[*id], Nodes->nodes()[std::size_t(row)].Rect.size()));
    }

    // --- editing -------------------------------------------------------------------------------------

    bool ParametricController::runModelChange(const std::function<void()> &change) {
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

    QString ParametricController::addValue(qreal x, qreal y) {
        const auto start = context();
        if (!start) {
            emit statusMessage(tr("Choose a part or part definition first."));
            return {};
        }
        // dropped onto a constraint / calculation: a new parameter of it
        const auto target = toNode(hitTest(x, y).value(QStringLiteral("node")).toString());
        if (target && Structure && (Info.at(*target).Kind == DiagramTypes::ConstraintNode ||
                                    Info.at(*target).Kind == DiagramTypes::CalcNode)) {
            const auto parameter = Structure->addFeatureTo(DiagramTypes::ToolAttribute, Info.at(*target).Element);
            syncNow();
            return parameter ? QString::number(*parameter) : QString();
        }
        std::optional<ElementId> id;
        if (!runModelChange([&] {
                id = Model.addElement(ElementKind::Attribute, uniqueName("value", *start), *start);
                AutoLayout = false;
                Positions[*id] = QPointF(std::max<qreal>(0, x - 60), std::max<qreal>(0, y - 20));
            }))
            return {};
        emit statusMessage(tr("Added a value - double-click it to set name, type and prefixes."));
        return QString::number(*id);
    }

    QString ParametricController::addUsage(int tool, qreal x, qreal y) {
        const auto start = context();
        if (!start || !Structure || (tool != DiagramTypes::ToolConstraint && tool != DiagramTypes::ToolCalc))
            return {};
        AutoLayout = false;
        const auto id = Structure->addFeatureTo(tool, *start);
        if (!id)
            return {};
        Positions[*id] = QPointF(std::max<qreal>(0, x - 70), std::max<qreal>(0, y - 20));
        syncNow();
        return QString::number(*id);
    }

    QString ParametricController::dropDefinition(const QString &definitionId, qreal x, qreal y) {
        const auto start = context();
        bool ok = false;
        const ElementId definition = definitionId.toULongLong(&ok);
        if (!start || !ok || !Model.contains(definition))
            return {};
        const auto &element = Model.element(definition);
        const bool calc = element.kind() == ElementKind::CalcDefinition;
        if (!calc && element.kind() != ElementKind::ConstraintDefinition)
            return {};

        CallExpression call;
        call.Function = referenceTo(definition);
        for (const ElementId parameter: parametersOf(definition, true))
            call.Arguments.push_back({std::string(), Model.element(parameter).name()});
        const auto results = parametersOf(definition, false);

        const QVariantMap hit = hitTest(x, y);
        const auto target = toNode(hit.value(QStringLiteral("node")).toString());
        std::optional<ElementId> id;
        const QPointF position(std::max<qreal>(0, x - 70), std::max<qreal>(0, y - 20));
        if (calc && target && isValueNode(*target)) {
            // the value is computed by the calculation from now on
            if (!runModelChange([&] { Model.setValue(*target, "=", SysMLTextImporter::formatCall(call)); }))
                return {};
            emit statusMessage(tr("Draw lines from values to the parameters of the call."));
            return nodeKey(computationNodeId(*target));
        }
        if (!runModelChange([&] {
                AutoLayout = false;
                if (calc) {
                    const std::string base = results.empty() ? lowerFirst(element.name()) : Model.element(results.front()).name();
                    id = Model.addElement(ElementKind::Attribute, uniqueName(base, *start), *start);
                    Model.setPrefixes(*id, {"derived"});
                    if (!results.empty() && !Model.element(results.front()).typeName().empty())
                        Model.setTypeName(*id, Model.element(results.front()).typeName());
                    Model.setValue(*id, "=", SysMLTextImporter::formatCall(call));
                    Positions[*id] = position + QPointF(ColumnWidth, 0);
                    Positions[computationNodeId(*id)] = position;
                } else {
                    id = Model.addElement(ElementKind::ConstraintUsage, uniqueName(lowerFirst(element.name()), *start), *start);
                    Model.setDeclaredKeyword(*id, std::string("assert constraint"));
                    Model.setTypeName(*id, referenceTo(definition));
                    Positions[*id] = position;
                }
            }))
            return {};
        emit statusMessage(tr("Draw lines from values to the parameters."));
        return QString::number(*id);
    }

    namespace {
        enum class Role {
            Value,
            CallInput,
            UsageInput,
            UsageOutput,
            ComputationOutput,
            UsageBody,
            Invalid
        };
    }

    QVariantMap ParametricController::connectOptions(const QString &sourceNode, int sourcePin, bool sourceOutput,
                                                     const QString &targetNode, int targetPin, bool targetOutput) const {
        QVariantMap result;
        QVariantList options;
        QString error;
        const auto role = [&](const QString &node, int pin, bool output) {
            const auto id = toNode(node);
            if (!id)
                return Role::Invalid;
            switch (Info.at(*id).Kind) {
                case DiagramTypes::ValueNode:
                    return Role::Value;
                case DiagramTypes::CallNode:
                    return output || pin < 0 ? Role::ComputationOutput : Role::CallInput;
                case DiagramTypes::ExpressionNode:
                    return Role::ComputationOutput;
                case DiagramTypes::ConstraintNode:
                case DiagramTypes::CalcNode:
                    if (pin >= 0 && output)
                        return Role::UsageOutput;
                    return pin >= 0 ? Role::UsageInput : Role::UsageBody;
                default:
                    return Role::Invalid;
            }
        };
        Role from = role(sourceNode, sourcePin, sourceOutput);
        Role to = role(targetNode, targetPin, targetOutput);
        QString fromNode = sourceNode;
        QString toNodeId = targetNode;
        const auto provider = [](Role r) { return r == Role::Value || r == Role::ComputationOutput || r == Role::UsageOutput; };
        const auto consumer = [](Role r) {
            return r == Role::Value || r == Role::CallInput || r == Role::UsageInput || r == Role::UsageBody;
        };
        if ((!provider(from) || !consumer(to)) && provider(to) && consumer(from)) {
            std::swap(from, to);
            std::swap(fromNode, toNodeId);
        }
        const auto option = [&](const QString &name, const QString &label) {
            options.append(QVariantMap{{QStringLiteral("option"), name}, {QStringLiteral("label"), label}});
        };
        const auto name = [&](const QString &node) {
            const auto id = toNode(node);
            return id ? q(Model.element(Info.at(*id).Element).name()) : QString();
        };

        if (fromNode == toNodeId && from == Role::Value && to == Role::Value) {
            error = tr("Draw the line to another element.");
        } else if (from == Role::Value && to == Role::CallInput) {
            if (Info.at(*toNode(toNodeId)).Element == *toNode(fromNode))
                error = tr("A value cannot be computed from itself.");
            else
                option(QStringLiteral("argument"), tr("Use as argument"));
        } else if (from == Role::Value && to == Role::UsageInput) {
            option(QStringLiteral("parameter"), tr("Use as parameter value"));
        } else if (from == Role::Value && to == Role::Value) {
            option(QStringLiteral("value"), tr("Value: %1 = %2").arg(name(toNodeId), name(fromNode)));
            option(QStringLiteral("bind"), tr("Binding: bind %1 = %2").arg(name(toNodeId), name(fromNode)));
        } else if (from == Role::UsageOutput && to == Role::Value) {
            option(QStringLiteral("value"), tr("Value: %1 = %2").arg(name(toNodeId), name(fromNode)));
        } else if (from == Role::Value && to == Role::UsageBody) {
            error = tr("Draw the line onto a parameter (drop a Value onto the constraint to add one), "
                       "or use the value in its expression (double-click).");
        } else if (from == Role::ComputationOutput && to == Role::Value) {
            error = tr("A call belongs to the value it computes. Drop the calc def onto the other value instead.");
        } else {
            error = tr("These elements cannot be connected.");
        }
        result.insert(QStringLiteral("options"), options);
        result.insert(QStringLiteral("error"), error);
        result.insert(QStringLiteral("source"), fromNode);
        result.insert(QStringLiteral("target"), toNodeId);
        result.insert(QStringLiteral("swapped"), fromNode != sourceNode);
        return result;
    }

    QString ParametricController::connectPins(const QString &sourceNode, int sourcePin, bool sourceOutput,
                                          const QString &targetNode, int targetPin, bool targetOutput,
                                          const QString &option) {
        const QVariantMap options = connectOptions(sourceNode, sourcePin, sourceOutput, targetNode, targetPin, targetOutput);
        bool known = false;
        for (const auto &entry: options.value(QStringLiteral("options")).toList())
            known = known || entry.toMap().value(QStringLiteral("option")).toString() == option;
        if (!known) {
            const QString error = options.value(QStringLiteral("error")).toString();
            return error.isEmpty() ? tr("These elements cannot be connected.") : error;
        }
        const bool swapped = options.value(QStringLiteral("swapped")).toBool();
        const std::uint64_t from = *toNode(options.value(QStringLiteral("source")).toString());
        const std::uint64_t to = *toNode(options.value(QStringLiteral("target")).toString());
        const int toPin = swapped ? sourcePin : targetPin;
        const ElementId fromElement = Info.at(from).Element;
        const ElementId toElement = Info.at(to).Element;

        QString error;
        if (option == QStringLiteral("argument")) {
            error = connectValueToCall(fromElement, toElement, toPin);
        } else if (option == QStringLiteral("parameter")) {
            error = connectValueToParameter(fromElement, toElement, toPin);
        } else if (option == QStringLiteral("value")) {
            if (!runModelChange([&] { Model.setValue(toElement, "=", referenceTo(fromElement)); }))
                error = tr("The value could not be set.");
        } else if (option == QStringLiteral("bind")) {
            if (!runModelChange([&] { Model.addRelationship(RelationshipKind::Binding, toElement, fromElement); }))
                error = tr("The binding could not be created.");
        }
        return error;
    }

    QString ParametricController::connectValueToCall(ElementId value, ElementId attribute, int pin) {
        auto call = SysMLTextImporter::parseCall(Model.element(attribute).value());
        const auto info = Info.find(computationNodeId(attribute));
        if (!call || info == Info.end() || pin < 0 || pin >= int(info->second.Inputs.size()))
            return tr("The value of this attribute is not a call anymore.");
        while (int(call->Arguments.size()) <= pin)
            call->Arguments.push_back({std::string(), info->second.Inputs[call->Arguments.size()].Name});
        call->Arguments[std::size_t(pin)].Value = referenceTo(value);
        const std::string op = Model.element(attribute).valueOperator().empty() ? "=" : Model.element(attribute).valueOperator();
        if (!runModelChange([&] { Model.setValue(attribute, op, SysMLTextImporter::formatCall(*call)); }))
            return tr("The argument could not be set.");
        return {};
    }

    QString ParametricController::connectValueToParameter(ElementId value, ElementId usage, int pin) {
        const auto pins = usagePins(usage);
        if (pin < 0 || pin >= int(pins.size()))
            return tr("Unknown parameter.");
        const std::string reference = referenceTo(value);
        const bool ok = runModelChange([&] {
            if (pins[std::size_t(pin)].Parameter) {
                Model.setValue(*pins[std::size_t(pin)].Parameter, "=", reference);
                return;
            }
            // Parameters of a usage redefine those of its definition in order: create all missing ones.
            const bool none = parametersOf(usage, true).empty();
            for (std::size_t i = 0; i < pins.size(); ++i) {
                if (pins[i].Parameter || (!none && int(i) != pin))
                    continue;
                const ElementId parameter = Model.addElement(ElementKind::Attribute, pins[i].Name, usage);
                Model.setPrefixes(parameter, {"in"});
                Model.setDeclaredKeyword(parameter, std::string());
                if (int(i) == pin)
                    Model.setValue(parameter, "=", reference);
            }
        });
        return ok ? QString() : tr("The parameter could not be set.");
    }

    void ParametricController::clearArgument(ElementId attribute, int pin) {
        auto call = SysMLTextImporter::parseCall(Model.element(attribute).value());
        const auto info = Info.find(computationNodeId(attribute));
        if (!call || info == Info.end() || pin < 0 || pin >= int(call->Arguments.size()))
            return;
        // the parameter name stands for "not connected yet"
        call->Arguments[std::size_t(pin)].Value = info->second.Inputs[std::size_t(pin)].Name;
        Model.setValue(attribute, Model.element(attribute).valueOperator(), SysMLTextImporter::formatCall(*call));
    }

    void ParametricController::deleteEdge(const QString &key) {
        const QStringList parts = key.split(QLatin1Char(':'));
        if (parts.size() < 2)
            return;
        const QString kind = parts.front();
        if (kind == QStringLiteral("x")) {
            emit statusMessage(tr("This line comes from the expression: remove the value from the expression (double-click)."));
            return;
        }
        runModelChange([&] {
            if (kind == QStringLiteral("v")) {
                Model.setValue(parts[1].toULongLong(), "", "");
            } else if (kind == QStringLiteral("a") && parts.size() == 3) {
                clearArgument(parts[1].toULongLong(), parts[2].toInt());
            } else if (kind == QStringLiteral("p")) {
                Model.setValue(parts[1].toULongLong(), "", "");
            } else if (kind == QStringLiteral("b")) {
                Model.removeRelationship(parts[1].toULongLong());
            }
        });
    }

    void ParametricController::deleteSelection() {
        const auto edges = SelectedEdges;
        const auto nodes = SelectedNodes;
        SelectedEdges.clear();
        SelectedNodes.clear();
        for (const auto &key: edges)
            deleteEdge(key);
        runModelChange([&] {
            for (const std::uint64_t node: nodes) {
                const auto info = Info.find(node);
                if (info == Info.end() || !Model.contains(info->second.Element))
                    continue;
                if (info->second.Kind == DiagramTypes::CallNode || info->second.Kind == DiagramTypes::ExpressionNode)
                    Model.setValue(info->second.Element, "", "");
                else
                    Model.removeElement(info->second.Element);
            }
        });
    }
}
