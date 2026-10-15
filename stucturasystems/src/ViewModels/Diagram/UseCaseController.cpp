//
// Use case view: use cases, actors, the system boundary (subject) and «include» of one package or the top level.
//

#include "UseCaseController.h"
#include "DiagramLayout.h"
#include "DiagramController.h"
#include "DiagramGeometry.h"

#include <QMetaObject>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/RelationshipRules.h>

#include <algorithm>
#include <cmath>
#include <exception>

namespace StructuraSystems::Client {

    using Diagram::ElementId;
    using Diagram::ElementKind;
    using Diagram::RelationshipKind;
    using Diagram::RootId;

    namespace {
        constexpr qreal Margin = 40.0;
        constexpr qreal ActorColumn = 50.0;
        constexpr qreal UseCaseColumn = 300.0;
        constexpr qreal RowGap = 34.0;
        constexpr qreal IncludeGap = 80.0;
        constexpr qreal BoundaryPadding = 26.0;
        constexpr qreal BoundaryTitle = 40.0;
        constexpr qreal BoundaryGrip = 6.0;
        constexpr qreal EdgeHitDistance = 6.0;

        QString q(const std::string &text) {
            return QString::fromStdString(text);
        }

        bool isInclude(const Diagram::SysMLElement &element) {
            return element.kind() == ElementKind::UseCaseUsage && element.declaredKeyword() == "include use case";
        }

        /// "driver : Driver[0..4]"
        QString usageTitle(const Diagram::SysMLElement &element) {
            QString title = q(element.name());
            if (!element.typeName().empty())
                title += QStringLiteral(" : ") + q(element.typeName());
            if (element.multiplicity())
                title += q(element.multiplicity()->toString());
            return title;
        }

        bool sameDeclaration(const Diagram::SysMLElement &a, const Diagram::SysMLElement &b) {
            return a.name() == b.name() && a.typeName() == b.typeName();
        }
    }

    UseCaseController::UseCaseController(Diagram::SemanticModel &model, QObject *parent) :
            QObject(parent),
            Model(model),
            Nodes(new DiagramNodeModel(this)),
            Edges(new DiagramEdgeModel(this)) {
        ListenerToken = Model.addListener([this](const Diagram::ModelChange &) { scheduleSync(); });
        connect(Nodes, &DiagramNodeModel::geometryChanged, this, &UseCaseController::updateCanvasSize);
        syncNow();
    }

    UseCaseController::~UseCaseController() {
        Model.removeListener(ListenerToken);
    }

    // --- context -------------------------------------------------------------------------------------

    ElementId UseCaseController::context() const {
        if (ContextElement && *ContextElement != RootId && Model.contains(*ContextElement)) {
            const ElementKind kind = Model.element(*ContextElement).kind();
            if (kind == ElementKind::Package || kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage)
                return *ContextElement;
        }
        return RootId;
    }

    QString UseCaseController::contextId() const {
        return QString::number(context());
    }

    void UseCaseController::setContextId(const QString &id) {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || (value != RootId && !Model.contains(value)) || (ContextElement && *ContextElement == value))
            return;
        saveContextLayout();
        ContextElement = value;
        Positions.clear();
        SelectedNodes.clear();
        SelectedEdges.clear();
        AutoLayout = true;
        applyContextLayout();
        emit contextChanged();
        syncNow();
    }

    void UseCaseController::updateContexts() {
        // the top level, every package, parts that own use cases
        const auto hasUseCases = [&](ElementId owner) {
            const auto &children = Model.children(owner);
            return std::any_of(children.begin(), children.end(),
                               [&](ElementId child) { return Diagram::isUseCaseKind(Model.element(child).kind()); });
        };
        QVariantList contexts;
        contexts.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("0")}, {QStringLiteral("title"), tr("Top level")}});
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                const bool package = element.kind() == ElementKind::Package;
                const bool part = element.kind() == ElementKind::PartDefinition || element.kind() == ElementKind::PartUsage;
                if (package || (part && hasUseCases(child)))
                    contexts.append(QVariantMap{{QStringLiteral("id"), QString::number(child)},
                                                {QStringLiteral("title"), q(Diagram::defaultKeyword(element.kind())) +
                                                                          QLatin1Char(' ') + q(Model.qualifiedName(child))}});
                if (package || part)
                    visit(child);
            }
        };
        visit(RootId);
        if (contexts != Contexts) {
            Contexts = contexts;
            emit contextsChanged();
        }
    }

    void UseCaseController::chooseContext() {
        if (ContextElement)
            return;
        // the first place with use cases (the top level if it has some)
        std::function<std::optional<ElementId>(ElementId)> find = [&](ElementId owner) -> std::optional<ElementId> {
            const auto &children = Model.children(owner);
            if (std::any_of(children.begin(), children.end(),
                            [&](ElementId child) { return Diagram::isUseCaseKind(Model.element(child).kind()); }))
                return owner;
            for (const ElementId child: children) {
                const ElementKind kind = Model.element(child).kind();
                if (kind == ElementKind::Package || kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage) {
                    if (const auto found = find(child))
                        return found;
                }
            }
            return std::nullopt;
        };
        if (const auto found = find(RootId)) {
            ContextElement = *found;
            applyContextLayout();
            emit contextChanged();
        }
    }

    // --- graph ---------------------------------------------------------------------------------------

    void UseCaseController::scheduleSync() {
        if (SyncQueued)
            return;
        SyncQueued = true;
        QMetaObject::invokeMethod(this, [this] {
            if (SyncQueued)
                syncNow();
        }, Qt::QueuedConnection);
    }

    QSizeF UseCaseController::sizeOf(const DiagramNode &node) {
        if (node.Kind == DiagramTypes::ActorNode)
            return {std::max<qreal>(70, node.Title.size() * 7.2 + 12), 92};
        const qreal width = std::clamp<qreal>(std::max(node.Title.size() * 7.6, node.Keyword.size() * 6.2) + 60, 150, 340);
        return {std::round(width), 64};
    }

    std::optional<ElementId> UseCaseController::includedDefinition(ElementId include) const {
        const auto segments = Diagram::SysMLTextImporter::splitReference(Model.element(include).typeName());
        if (segments.empty())
            return std::nullopt;
        for (const ElementId id: TopUseCases) {
            const auto &element = Model.element(id);
            if (element.kind() == ElementKind::UseCaseDefinition && element.name() == segments.back())
                return id;
        }
        return std::nullopt;
    }

    std::optional<ElementId> UseCaseController::subjectOf(ElementId useCase) const {
        for (const ElementId child: Model.children(useCase)) {
            if (Model.element(child).kind() == ElementKind::Subject)
                return child;
        }
        return std::nullopt;
    }

    bool UseCaseController::isUseCaseNode(ElementId id) const {
        const auto *node = Nodes->find(id);
        return node && node->Kind == DiagramTypes::UseCaseNode;
    }

    const UseCaseController::Group *UseCaseController::groupOf(ElementId node) const {
        for (const auto *groups: {&Actors, &Subjects}) {
            for (const auto &group: *groups) {
                if (group.Node == node)
                    return &group;
            }
        }
        return nullptr;
    }

    void UseCaseController::syncNow() {
        SyncQueued = false;
        updateContexts();
        chooseContext();
        const ElementId start = context();

        std::vector<DiagramNode> nodes;
        std::vector<DiagramEdge> edges;
        TopUseCases.clear();
        IncludedBy.clear();
        Actors.clear();
        Subjects.clear();

        for (const ElementId child: Model.children(start)) {
            const auto &element = Model.element(child);
            if (Diagram::isUseCaseKind(element.kind()) && !isInclude(element))
                TopUseCases.push_back(child);
        }
        const auto edge = [&](const QString &key, DiagramTypes::EdgeKind kind, ElementId source, ElementId target,
                              const QString &label) {
            DiagramEdge result;
            result.Key = key;
            result.Kind = kind;
            result.Source = source;
            result.Target = target;
            result.Label = label;
            result.Selected = SelectedEdges.count(key) > 0;
            edges.push_back(result);
        };
        // use cases; an included one gets an ellipse of its own unless it is typed by a definition shown here
        std::vector<ElementId> useCases;
        std::function<void(ElementId)> addUseCase = [&](ElementId id) {
            const auto &element = Model.element(id);
            useCases.push_back(id);
            DiagramNode node;
            node.Id = id;
            node.Kind = DiagramTypes::UseCaseNode;
            node.Keyword = QStringLiteral("«") + q(element.declaredKeyword().value_or(Diagram::defaultKeyword(element.kind()))) +
                           QStringLiteral("»");
            node.Title = (element.shortName().empty() ? QString() : QStringLiteral("<") + q(element.shortName()) + QStringLiteral("> ")) +
                         usageTitle(element);
            node.Depth = 1;
            nodes.push_back(node);
            for (const ElementId child: element.ownedElements()) {
                if (!isInclude(Model.element(child)))
                    continue;
                if (const auto definition = includedDefinition(child)) {
                    edge(QStringLiteral("i:%1").arg(child), DiagramTypes::Include, id, *definition, QStringLiteral("«include»"));
                } else {
                    IncludedBy[child] = id;
                    addUseCase(child);
                    edge(QStringLiteral("i:%1").arg(child), DiagramTypes::Include, id, child, QStringLiteral("«include»"));
                }
            }
        };
        for (const ElementId id: TopUseCases)
            addUseCase(id);
        const std::set<ElementId> shown(useCases.begin(), useCases.end());
        for (const auto &relationship: Model.relationships()) {
            if (relationship.Kind == RelationshipKind::Include && shown.count(relationship.Source) && shown.count(relationship.Target))
                edge(QStringLiteral("r:%1").arg(relationship.Id), DiagramTypes::Include, relationship.Source,
                     relationship.Target, QStringLiteral("«include»"));
        }

        // actors and subjects: one figure / boundary per name and type
        const auto addToGroup = [&](std::vector<Group> &groups, ElementId declaration, ElementId useCase) {
            for (auto &group: groups) {
                if (sameDeclaration(Model.element(group.Node), Model.element(declaration))) {
                    group.Declarations.push_back(declaration);
                    group.UseCases.push_back(useCase);
                    return;
                }
            }
            groups.push_back(Group{declaration, {declaration}, {useCase}, {}});
        };
        for (const ElementId useCase: useCases) {
            bool subject = false;
            for (const ElementId child: Model.children(useCase)) {
                const auto &element = Model.element(child);
                if (element.kind() == ElementKind::Actor && element.declaredKeyword().value_or("actor") == "actor")
                    addToGroup(Actors, child, useCase);
                if (element.kind() == ElementKind::Subject && !subject) {
                    addToGroup(Subjects, child, useCase);
                    subject = true;
                }
            }
        }
        // inside a boundary: its use cases and what they include (unless that has a subject of its own)
        for (auto &group: Subjects) {
            std::function<void(ElementId)> member = [&](ElementId id) {
                group.Members.push_back(id);
                for (const auto &[included, owner]: IncludedBy) {
                    if (owner == id && !subjectOf(included))
                        member(included);
                }
            };
            for (const ElementId useCase: group.UseCases)
                member(useCase);
        }
        for (const auto &group: Actors) {
            DiagramNode node;
            node.Id = group.Node;
            node.Kind = DiagramTypes::ActorNode;
            node.Keyword = QStringLiteral("«actor»");
            node.Title = usageTitle(Model.element(group.Node));
            node.Depth = 2;
            nodes.push_back(node);
            for (std::size_t i = 0; i < group.Declarations.size(); ++i)
                edge(QStringLiteral("a:%1").arg(group.Declarations[i]), DiagramTypes::Association, group.Node,
                     group.UseCases[i], QString());
        }
        for (const auto &group: Subjects) {
            DiagramNode node;
            node.Id = group.Node;
            node.Kind = DiagramTypes::BoundaryNode;
            node.Keyword = QStringLiteral("«subject»");
            node.Title = usageTitle(Model.element(group.Node));
            node.Depth = 0;
            nodes.push_back(node);
        }

        // positions: automatic layout until something is moved, new nodes next to the others afterwards
        const bool missing = std::any_of(nodes.begin(), nodes.end(), [&](const DiagramNode &node) {
            return node.Kind != DiagramTypes::BoundaryNode && Positions.count(node.Id) == 0;
        });
        const auto sizeById = [&](ElementId id) {
            for (const auto &node: nodes) {
                if (node.Id == id)
                    return sizeOf(node);
            }
            return QSizeF();
        };
        if (missing && AutoLayout) {
            Positions.clear();
            std::set<ElementId> placed;
            qreal y = Margin;
            // a row: the use case and, to its right, what it includes (as ellipse of its own)
            const auto placeRow = [&](ElementId id, qreal left) {
                qreal x = left;
                qreal height = 0;
                std::function<void(ElementId)> place = [&](ElementId current) {
                    if (placed.count(current))
                        return;
                    placed.insert(current);
                    const QSizeF size = sizeById(current);
                    Positions[current] = QPointF(x, y);
                    x += size.width() + IncludeGap;
                    height = std::max(height, size.height());
                    for (const auto &[included, owner]: IncludedBy) {
                        if (owner == current)
                            place(included);
                    }
                };
                place(id);
                if (height > 0)
                    y += height + RowGap;
            };
            for (const auto &group: Subjects) {
                y += BoundaryTitle;
                for (const ElementId useCase: group.Members) {
                    if (IncludedBy.count(useCase) == 0 || !placed.count(IncludedBy.at(useCase)))
                        placeRow(useCase, UseCaseColumn + BoundaryPadding);
                }
                y += BoundaryPadding + RowGap;
            }
            for (const ElementId useCase: useCases) {
                if (!placed.count(useCase) && IncludedBy.count(useCase) == 0)
                    placeRow(useCase, UseCaseColumn + BoundaryPadding);
            }
            for (const ElementId useCase: useCases) {
                if (!placed.count(useCase))
                    placeRow(useCase, UseCaseColumn + BoundaryPadding);
            }
            // actors on the left, at the height of their use cases
            std::vector<std::pair<qreal, ElementId>> actors;
            for (const auto &group: Actors) {
                qreal sum = 0;
                for (const ElementId useCase: group.UseCases)
                    sum += Positions[useCase].y() + sizeById(useCase).height() / 2;
                actors.emplace_back(sum / qreal(group.UseCases.size()), group.Node);
            }
            std::sort(actors.begin(), actors.end());
            qreal next = Margin;
            for (const auto &[center, id]: actors) {
                const qreal top = std::max(next, center - 46);
                Positions[id] = QPointF(ActorColumn, top);
                next = top + 92 + 24;
            }
        } else if (missing) {
            const QRectF bounds = contentBounds();
            qreal x = bounds.isNull() ? UseCaseColumn : bounds.right() + IncludeGap;
            for (const auto &node: nodes) {
                if (node.Kind == DiagramTypes::BoundaryNode || Positions.count(node.Id))
                    continue;
                Positions[node.Id] = QPointF(x, Margin);
                x += sizeOf(node).width() + IncludeGap;
            }
        }

        // into the node model (boundaries around their use cases)
        std::set<ElementId> wanted;
        for (const auto &node: nodes)
            wanted.insert(node.Id);
        for (int row = Nodes->count() - 1; row >= 0; --row) {
            if (wanted.count(Nodes->nodes()[std::size_t(row)].Id) == 0)
                Nodes->removeAt(row);
        }
        for (auto it = SelectedNodes.begin(); it != SelectedNodes.end();)
            it = wanted.count(*it) ? std::next(it) : SelectedNodes.erase(it);
        for (auto &node: nodes) {
            node.Selected = SelectedNodes.count(node.Id) > 0;
            QRectF rect;
            if (node.Kind == DiagramTypes::BoundaryNode) {
                for (const auto &group: Subjects) {
                    if (group.Node != node.Id)
                        continue;
                    for (const ElementId member: group.Members)
                        rect = rect.isNull() ? QRectF(Positions[member], sizeById(member))
                                             : rect.united(QRectF(Positions[member], sizeById(member)));
                }
                rect.adjust(-BoundaryPadding, -BoundaryTitle, BoundaryPadding, BoundaryPadding);
            } else {
                rect = QRectF(Positions[node.Id], sizeOf(node));
            }
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
            const bool exists = std::any_of(edges.begin(), edges.end(), [&](const DiagramEdge &e) { return e.Key == *it; });
            it = exists ? std::next(it) : SelectedEdges.erase(it);
        }
        Edges->setEdges(std::move(edges));
        updateCanvasSize();
        emit selectionChanged();
    }

    void UseCaseController::updateBoundaries() {
        for (const auto &group: Subjects) {
            const int row = Nodes->indexOf(group.Node);
            if (row < 0)
                continue;
            QRectF rect;
            for (const ElementId member: group.Members) {
                if (const auto *node = Nodes->find(member))
                    rect = rect.isNull() ? node->Rect : rect.united(node->Rect);
            }
            Nodes->setRect(row, rect.adjusted(-BoundaryPadding, -BoundaryTitle, BoundaryPadding, BoundaryPadding));
        }
    }

    void UseCaseController::updateCanvasSize() {
        const QRectF bounds = contentBounds();
        const QSizeF size(std::max<qreal>(1600, bounds.right() + 600), std::max<qreal>(1000, bounds.bottom() + 600));
        if (size != CanvasSize) {
            CanvasSize = size;
            emit canvasSizeChanged();
        }
    }

    QRectF UseCaseController::contentBounds() const {
        QRectF bounds;
        for (const auto &node: Nodes->nodes())
            bounds = bounds.isNull() ? node.Rect : bounds.united(node.Rect);
        return bounds;
    }

    QString UseCaseController::contextKey() const {
        return context() == RootId ? QStringLiteral(".") : DiagramLayout::keyOf(Model, context());
    }

    QJsonObject UseCaseController::currentLayout() const {
        QJsonObject layout;
        for (const auto &[id, position]: Positions) {
            if (Model.contains(id))
                layout.insert(DiagramLayout::keyOf(Model, id), DiagramLayout::toJson(position));
        }
        return layout;
    }

    void UseCaseController::saveContextLayout() {
        const QString key = contextKey();
        if (key.isEmpty())
            return;
        if (AutoLayout)
            SavedLayouts.remove(key);
        else
            SavedLayouts.insert(key, currentLayout());
    }

    void UseCaseController::applyContextLayout() {
        const QJsonObject layout = SavedLayouts.value(contextKey()).toObject();
        if (layout.isEmpty())
            return;
        const auto byKey = DiagramLayout::elementsByKey(Model);
        for (auto it = layout.begin(); it != layout.end(); ++it) {
            const auto element = byKey.find(it.key());
            const auto position = DiagramLayout::positionOf(it.value());
            if (element != byKey.end() && position)
                Positions[element->second] = *position;
        }
        if (!Positions.empty())
            AutoLayout = false;
    }

    QJsonObject UseCaseController::layoutState() const {
        QJsonObject layout = SavedLayouts;
        const QString key = contextKey();
        if (!key.isEmpty()) {
            if (AutoLayout)
                layout.remove(key);
            else
                layout.insert(key, currentLayout());
        }
        return layout;
    }

    void UseCaseController::restoreLayout(const QJsonObject &layout) {
        SavedLayouts = layout;
        Positions.clear();
        AutoLayout = true;
        applyContextLayout();
        syncNow();
    }

    void UseCaseController::layoutAll() {
        Positions.clear();
        AutoLayout = true;
        SavedLayouts.remove(contextKey());
        syncNow();
    }

    // --- lookup --------------------------------------------------------------------------------------

    std::optional<ElementId> UseCaseController::toNode(const QString &id) const {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || !Nodes->find(value))
            return std::nullopt;
        return value;
    }

    QString UseCaseController::nodeAt(qreal x, qreal y) const {
        const QPointF point(x, y);
        const auto &nodes = Nodes->nodes();
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            if (it->Kind != DiagramTypes::BoundaryNode && it->Rect.contains(point))
                return QString::number(it->Id);
        }
        // a boundary at its title band and border (its inside stays free for the canvas)
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            if (it->Kind != DiagramTypes::BoundaryNode)
                continue;
            const QRectF outer = it->Rect.adjusted(-BoundaryGrip, -BoundaryGrip, BoundaryGrip, BoundaryGrip);
            const QRectF inner = it->Rect.adjusted(BoundaryGrip, 30, -BoundaryGrip, -BoundaryGrip);
            if (outer.contains(point) && !inner.contains(point))
                return QString::number(it->Id);
        }
        return {};
    }

    std::optional<ElementId> UseCaseController::boundaryAt(const QPointF &point) const {
        std::optional<ElementId> best;
        qreal area = 0;
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind != DiagramTypes::BoundaryNode || !node.Rect.contains(point))
                continue;
            const qreal size = node.Rect.width() * node.Rect.height();
            if (!best || size < area) {
                best = node.Id;
                area = size;
            }
        }
        return best;
    }

    QString UseCaseController::edgeAt(qreal x, qreal y) const {
        const QPointF point(x, y);
        const auto &edges = Edges->edges();
        for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
            const auto *source = Nodes->find(it->Source);
            const auto *target = Nodes->find(it->Target);
            if (!source || !target)
                continue;
            if (DiagramGeometry::distanceToSegment(point, DiagramGeometry::edgeLine(source->Rect, target->Rect)) <= EdgeHitDistance)
                return it->Key;
        }
        return {};
    }

    QRectF UseCaseController::nodeRect(const QString &node) const {
        const auto id = toNode(node);
        return id ? Nodes->find(*id)->Rect : QRectF();
    }

    // --- selection and moving ------------------------------------------------------------------------

    void UseCaseController::applySelection() {
        for (int row = 0; row < Nodes->count(); ++row)
            Nodes->setSelected(row, SelectedNodes.count(Nodes->nodes()[std::size_t(row)].Id) > 0);
        auto edges = Edges->edges();
        for (auto &edge: edges)
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
        Edges->setEdges(std::move(edges));
        emit selectionChanged();
    }

    void UseCaseController::select(const QString &node, bool additive) {
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (const auto id = toNode(node))
            SelectedNodes.insert(*id);
        applySelection();
    }

    void UseCaseController::selectEdge(const QString &key, bool additive) {
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (Edges->find(key))
            SelectedEdges.insert(key);
        applySelection();
    }

    void UseCaseController::clearSelection() {
        SelectedNodes.clear();
        SelectedEdges.clear();
        applySelection();
    }

    void UseCaseController::moveNode(const QString &node, qreal x, qreal y) {
        const auto id = toNode(node);
        if (!id)
            return;
        AutoLayout = false;
        emit layoutEdited();
        const auto *found = Nodes->find(*id);
        if (found->Kind == DiagramTypes::BoundaryNode) {
            // the use cases inside move along
            const QPointF delta = QPointF(std::max<qreal>(0, x), std::max<qreal>(0, y)) - found->Rect.topLeft();
            if (const auto *group = groupOf(*id)) {
                for (const ElementId member: group->Members) {
                    Positions[member] += delta;
                    const int row = Nodes->indexOf(member);
                    if (row >= 0)
                        Nodes->setRect(row, QRectF(Positions[member], Nodes->nodes()[std::size_t(row)].Rect.size()));
                }
            }
        } else {
            Positions[*id] = QPointF(std::max<qreal>(0, x), std::max<qreal>(0, y));
            const int row = Nodes->indexOf(*id);
            Nodes->setRect(row, QRectF(Positions[*id], Nodes->nodes()[std::size_t(row)].Rect.size()));
        }
        updateBoundaries();
    }

    // --- editing -------------------------------------------------------------------------------------

    bool UseCaseController::runModelChange(const std::function<void()> &change) {
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

    std::string UseCaseController::uniqueName(const std::string &base, ElementId owner) const {
        const auto &siblings = Model.children(owner);
        for (int index = 1;; ++index) {
            const std::string candidate = base + std::to_string(index);
            if (std::none_of(siblings.begin(), siblings.end(),
                             [&](ElementId sibling) { return Model.element(sibling).name() == candidate; }))
                return candidate;
        }
    }

    std::string UseCaseController::freeName(const std::string &base, ElementId owner) const {
        const auto &siblings = Model.children(owner);
        if (std::none_of(siblings.begin(), siblings.end(), [&](ElementId sibling) { return Model.element(sibling).name() == base; }))
            return base;
        return uniqueName(base, owner);
    }

    std::string UseCaseController::referenceTo(ElementId from, ElementId target) const {
        // the target is a use case shown in the view, i.e. owned by the context: found from inside 'from' by its name
        Q_UNUSED(from)
        return Diagram::SysMLCodeGenerator::formatName(Model.element(target).name());
    }

    void UseCaseController::declareCopy(ElementId original, ElementId useCase) {
        const auto &element = Model.element(original);
        const ElementId copy = Model.addElement(element.kind(), element.name(), useCase);
        if (!element.typeName().empty())
            Model.setTypeName(copy, element.typeName());
        if (element.multiplicity())
            Model.setMultiplicity(copy, element.multiplicity());
        if (!element.prefixes().empty())
            Model.setPrefixes(copy, element.prefixes());
    }

    QString UseCaseController::addNode(int tool, qreal x, qreal y) {
        const QPointF point(x, y);
        const ElementId owner = context();
        std::optional<ElementId> id;
        QString message;
        const auto useCaseAt = [&]() -> std::optional<ElementId> {
            const auto node = toNode(nodeAt(x, y));
            if (node && isUseCaseNode(*node))
                return node;
            // a palette click: the one selected use case
            std::optional<ElementId> selected;
            for (const ElementId id: SelectedNodes) {
                if (!isUseCaseNode(id))
                    continue;
                if (selected)
                    return std::nullopt;
                selected = id;
            }
            return selected;
        };
        switch (tool) {
            case DiagramTypes::ToolUseCaseDefinition:
            case DiagramTypes::ToolUseCase: {
                const bool definition = tool == DiagramTypes::ToolUseCaseDefinition;
                const auto boundary = boundaryAt(point);
                if (!runModelChange([&] {
                        id = Model.addElement(definition ? ElementKind::UseCaseDefinition : ElementKind::UseCaseUsage,
                                              uniqueName(definition ? "UseCase" : "useCase", owner), owner);
                        if (boundary)
                            declareCopy(*boundary, *id); // inside the system boundary: its subject
                        AutoLayout = false;
                        // not on top of another use case: below it
                        QRectF place(std::max<qreal>(0, x - 75), std::max<qreal>(0, y - 32), 150, 64);
                        for (bool moved = true; moved;) {
                            moved = false;
                            for (const auto &node: Nodes->nodes()) {
                                if (node.Kind == DiagramTypes::UseCaseNode && node.Rect.intersects(place)) {
                                    place.moveTop(node.Rect.bottom() + 16);
                                    moved = true;
                                }
                            }
                        }
                        Positions[*id] = place.topLeft();
                    }))
                    return {};
                message = boundary ? tr("Added a use case of '%1' - double-click to edit, draw lines from its corner.")
                                             .arg(q(Model.element(*boundary).name()))
                                   : tr("Added a use case - double-click to edit, draw lines from its corner.");
                break;
            }
            case DiagramTypes::ToolActor:
            case DiagramTypes::ToolSubject: {
                const auto useCase = useCaseAt();
                const bool actor = tool == DiagramTypes::ToolActor;
                if (!useCase) {
                    emit statusMessage(actor ? tr("Drop the actor onto a use case - SysML declares actors in the use case.")
                                             : tr("Drop the subject onto a use case - it becomes the system boundary."));
                    return {};
                }
                if (!actor && subjectOf(*useCase)) {
                    emit statusMessage(tr("The use case already has a subject (system boundary)."));
                    return {};
                }
                if (!runModelChange([&] {
                        id = Model.addElement(actor ? ElementKind::Actor : ElementKind::Subject,
                                              uniqueName(actor ? "actor" : "subject", *useCase), *useCase);
                        AutoLayout = false;
                        if (actor) {
                            // left of the use case and of its boundary, in the column of the other actors
                            const QRectF useCaseRect = Nodes->find(*useCase)->Rect;
                            qreal left = useCaseRect.left() - 200;
                            for (const auto &node: Nodes->nodes()) {
                                if (node.Kind == DiagramTypes::BoundaryNode && node.Rect.intersects(useCaseRect))
                                    left = std::min(left, node.Rect.left() - 150);
                                if (node.Kind == DiagramTypes::ActorNode)
                                    left = std::min(left, node.Rect.left());
                            }
                            QRectF place(std::max<qreal>(0, left), std::max<qreal>(0, useCaseRect.center().y() - 46), 90, 92);
                            for (bool moved = true; moved;) {
                                moved = false;
                                for (const auto &node: Nodes->nodes()) {
                                    if (node.Kind != DiagramTypes::BoundaryNode && node.Rect.intersects(place)) {
                                        place.moveTop(node.Rect.bottom() + 16);
                                        moved = true;
                                    }
                                }
                            }
                            Positions[*id] = place.topLeft();
                        }
                    }))
                    return {};
                message = actor ? tr("Added an actor - draw a line from it to other use cases, double-click to rename.")
                                : tr("Added a subject - double-click the boundary title to name the system.");
                break;
            }
            default:
                return {};
        }
        emit statusMessage(message);
        return id ? QString::number(*id) : QString();
    }

    QString UseCaseController::connectNodes(const QString &source, const QString &target) {
        const auto from = toNode(source);
        const auto to = toNode(target);
        if (!from || !to || *from == *to)
            return tr("Draw the line onto another node.");
        const auto kindOf = [&](ElementId id) { return Nodes->find(id)->Kind; };
        const auto declaredIn = [&](const Group &group, ElementId useCase) {
            return std::find(group.UseCases.begin(), group.UseCases.end(), useCase) != group.UseCases.end();
        };

        // actor - use case (either direction): the actor is declared in the use case
        if ((kindOf(*from) == DiagramTypes::ActorNode && kindOf(*to) == DiagramTypes::UseCaseNode) ||
            (kindOf(*to) == DiagramTypes::ActorNode && kindOf(*from) == DiagramTypes::UseCaseNode)) {
            const ElementId actor = kindOf(*from) == DiagramTypes::ActorNode ? *from : *to;
            const ElementId useCase = actor == *from ? *to : *from;
            const Group *group = groupOf(actor);
            if (!group)
                return tr("Unknown actor.");
            if (declaredIn(*group, useCase))
                return tr("The actor already takes part in this use case.");
            const ElementId original = group->Node;
            if (!runModelChange([&] { declareCopy(original, useCase); }))
                return tr("The use case already has an element of this name.");
            emit statusMessage(tr("The actor takes part in the use case."));
            return {};
        }
        // use case -> boundary: into that system (its subject)
        if ((kindOf(*from) == DiagramTypes::UseCaseNode && kindOf(*to) == DiagramTypes::BoundaryNode) ||
            (kindOf(*to) == DiagramTypes::UseCaseNode && kindOf(*from) == DiagramTypes::BoundaryNode)) {
            const ElementId boundary = kindOf(*from) == DiagramTypes::BoundaryNode ? *from : *to;
            const ElementId useCase = boundary == *from ? *to : *from;
            if (const auto subject = subjectOf(useCase))
                return tr("The use case already has the subject '%1'.").arg(q(Model.element(*subject).name()));
            if (!runModelChange([&] { declareCopy(boundary, useCase); }))
                return tr("The use case already has an element of this name.");
            emit statusMessage(tr("The use case belongs to the system now."));
            return {};
        }
        // use case -> use case: «include»
        if (kindOf(*from) == DiagramTypes::UseCaseNode && kindOf(*to) == DiagramTypes::UseCaseNode) {
            const auto &targetElement = Model.element(*to);
            if (targetElement.kind() == ElementKind::UseCaseDefinition) {
                // "include use case refuel : Refuel;"
                for (const ElementId child: Model.children(*from)) {
                    if (isInclude(Model.element(child)) && includedDefinition(child) == *to)
                        return tr("It already includes this use case.");
                }
                std::string base = targetElement.name();
                if (!base.empty() && base.front() >= 'A' && base.front() <= 'Z')
                    base.front() = char(base.front() - 'A' + 'a');
                const bool ok = runModelChange([&] {
                    const ElementId include = Model.addElement(ElementKind::UseCaseUsage, freeName(base, *from), *from);
                    Model.setDeclaredKeyword(include, std::string("include use case"));
                    Model.setTypeName(include, referenceTo(*from, *to));
                });
                if (!ok)
                    return tr("The include could not be added.");
            } else {
                if (const auto rule = Diagram::RelationshipRules::canRelate(Model, RelationshipKind::Include, *from, *to); !rule)
                    return QString::fromStdString(rule.Reason);
                runModelChange([&] { Model.addRelationship(RelationshipKind::Include, *from, *to); });
            }
            emit statusMessage(tr("«include» created."));
            return {};
        }
        return tr("Lines go from an actor to a use case, from a use case to a use case («include») or into a boundary.");
    }

    void UseCaseController::deleteEdge(const QString &key) {
        const auto id = key.mid(2).toULongLong();
        runModelChange([&] {
            if (key.startsWith(QStringLiteral("r:")) && Model.containsRelationship(id))
                Model.removeRelationship(id);
            else if ((key.startsWith(QStringLiteral("a:")) || key.startsWith(QStringLiteral("i:"))) && Model.contains(id))
                Model.removeElement(id); // the actor declaration / the included use case
        });
    }

    void UseCaseController::deleteSelection() {
        const auto edges = SelectedEdges;
        const auto nodes = SelectedNodes;
        SelectedEdges.clear();
        SelectedNodes.clear();
        std::vector<ElementId> elements;
        for (const ElementId node: nodes) {
            if (const auto *group = groupOf(node))
                elements.insert(elements.end(), group->Declarations.begin(), group->Declarations.end());
            else
                elements.push_back(node);
        }
        runModelChange([&] {
            for (const auto &key: edges) {
                const auto id = key.mid(2).toULongLong();
                if (key.startsWith(QStringLiteral("r:")) && Model.containsRelationship(id))
                    Model.removeRelationship(id);
                else if ((key.startsWith(QStringLiteral("a:")) || key.startsWith(QStringLiteral("i:"))) && Model.contains(id))
                    Model.removeElement(id);
            }
            for (const ElementId element: elements) {
                if (Model.contains(element))
                    Model.removeElement(element);
            }
        });
    }

    QString UseCaseController::elementFor(const QString &nodeOrEdge) const {
        if (nodeOrEdge.startsWith(QStringLiteral("i:")))
            return nodeOrEdge.mid(2);
        const auto id = toNode(nodeOrEdge);
        return id && isUseCaseNode(*id) ? QString::number(*id) : QString();
    }

    QVariantMap UseCaseController::groupDetails(const QString &node) const {
        std::optional<ElementId> id = toNode(node);
        if (!id && node.startsWith(QStringLiteral("a:"))) {
            // a line of an actor: its figure
            const auto declaration = node.mid(2).toULongLong();
            for (const auto &group: Actors) {
                if (std::find(group.Declarations.begin(), group.Declarations.end(), declaration) != group.Declarations.end())
                    id = group.Node;
            }
        }
        const Group *group = id ? groupOf(*id) : nullptr;
        if (!group)
            return {};
        const auto &element = Model.element(group->Node);
        QString multiplicity;
        if (element.multiplicity()) {
            multiplicity = q(element.multiplicity()->toString());
            multiplicity = multiplicity.mid(1, multiplicity.size() - 2);
        }
        const bool actor = element.kind() == ElementKind::Actor;
        return {{QStringLiteral("node"), QString::number(group->Node)},
                {QStringLiteral("kindLabel"), actor ? tr("actor") : tr("subject (system)")},
                {QStringLiteral("name"), q(element.name())},
                {QStringLiteral("typeName"), q(element.typeName())},
                {QStringLiteral("multiplicity"), multiplicity},
                {QStringLiteral("typePlaceholder"), actor ? QStringLiteral("Driver") : QStringLiteral("Vehicle")},
                {QStringLiteral("count"), int(group->Declarations.size())}};
    }

    QString UseCaseController::updateGroup(const QString &node, const QVariantMap &values) {
        const auto id = toNode(node);
        const Group *group = id ? groupOf(*id) : nullptr;
        if (!group)
            return tr("The actor or subject does not exist anymore.");
        const std::string name = values.value(QStringLiteral("name")).toString().trimmed().toStdString();
        const std::string typeName = values.value(QStringLiteral("typeName")).toString().trimmed().toStdString();
        if (name.empty())
            return tr("The name must not be empty.");
        const auto [valid, multiplicity] = DiagramController::parseMultiplicity(values.value(QStringLiteral("multiplicity")).toString());
        if (!valid)
            return tr("Multiplicity must look like 6, 0..1 or 1..*");
        const std::vector<ElementId> declarations = group->Declarations;
        // every declaration must be renamable before anything is changed
        for (const ElementId declaration: declarations) {
            for (const ElementId sibling: Model.children(Model.element(declaration).owner())) {
                if (sibling != declaration && Model.element(sibling).kind() != ElementKind::Textual &&
                    Model.element(sibling).name() == name)
                    return tr("'%1' already has an element named '%2'.")
                            .arg(q(Model.element(Model.element(declaration).owner()).name()), q(name));
            }
        }
        QString error;
        runModelChange([&] {
            try {
                for (const ElementId declaration: declarations) {
                    Model.rename(declaration, name);
                    Model.setTypeName(declaration, typeName);
                    Model.setMultiplicity(declaration, multiplicity);
                }
            } catch (const std::exception &exception) {
                error = QString::fromStdString(exception.what());
            }
        });
        return error;
    }
}
