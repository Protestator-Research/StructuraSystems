//
// Behavior view: actions, control nodes and states of one action or state, with successions and transitions.
//

#include "BehaviorController.h"
#include "DiagramGeometry.h"

#include <QMetaObject>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Semantic/RelationshipRules.h>

#include <algorithm>
#include <deque>
#include <exception>

namespace StructuraSystems::Client {

    using Diagram::ElementId;
    using Diagram::ElementKind;
    using Diagram::RelationshipKind;
    using Diagram::RootId;

    namespace {
        constexpr qreal Margin = 40.0;
        constexpr qreal RowHeight = 120.0;
        constexpr qreal ColumnGap = 60.0;
        constexpr qreal HeaderHeight = 40.0;
        constexpr qreal EntryHeight = 18.0;
        constexpr qreal EdgeHitDistance = 6.0;

        QString q(const std::string &text) {
            return QString::fromStdString(text);
        }

        std::string keywordOf(const Diagram::SysMLElement &element) {
            return element.declaredKeyword().value_or(Diagram::defaultKeyword(element.kind()));
        }

        /// entry / do / exit action of a state (shown inside the state, not as node): "entry action a;",
        /// "entry assign t := 0;", "do senseTemperature;"
        bool isStateAction(const Diagram::SysMLElement &element) {
            const auto keyword = element.declaredKeyword().value_or("");
            return (element.kind() == ElementKind::ActionUsage || element.kind() == ElementKind::ActionPerformance) &&
                   (keyword == "entry action" || keyword == "do action" || keyword == "exit action" || keyword == "entry" ||
                    keyword == "do" || keyword == "exit");
        }

        /// "send new Show(p) to screen", "if x > 0", "loop until c", "else" (empty for a plain action).
        QString formText(const Diagram::SysMLElement &element) {
            if (element.kind() != ElementKind::ActionUsage)
                return {};
            QString text = element.isElseBranch() ? QStringLiteral("else") : QString();
            if (element.actionForm() != Diagram::ActionForm::Plain) {
                text += (text.isEmpty() ? QString() : QStringLiteral(" ")) + q(Diagram::toKeyword(element.actionForm()));
                if (!element.actionClause().empty())
                    text += QLatin1Char(' ') + q(element.actionClause());
            }
            if (!text.isEmpty() && !element.untilCondition().empty())
                text += QStringLiteral(" until ") + q(element.untilCondition());
            return text;
        }

        bool isState(ElementKind kind) {
            return kind == ElementKind::StateDefinition || kind == ElementKind::StateUsage;
        }

        QString usageTitle(const Diagram::SysMLElement &element) {
            QString title = q(element.name());
            if (!element.typeName().empty())
                title += QStringLiteral(" : ") + q(element.typeName());
            return title;
        }
    }

    BehaviorController::BehaviorController(Diagram::SemanticModel &model, QObject *parent) :
            QObject(parent),
            Model(model),
            Nodes(new DiagramNodeModel(this)),
            Edges(new DiagramEdgeModel(this)) {
        ListenerToken = Model.addListener([this](const Diagram::ModelChange &) { scheduleSync(); });
        connect(Nodes, &DiagramNodeModel::geometryChanged, this, &BehaviorController::updateCanvasSize);
        syncNow();
    }

    BehaviorController::~BehaviorController() {
        Model.removeListener(ListenerToken);
    }

    // --- context -------------------------------------------------------------------------------------

    std::optional<ElementId> BehaviorController::context() const {
        if (ContextElement && Model.contains(*ContextElement) && Diagram::isBehaviorKind(Model.element(*ContextElement).kind()))
            return ContextElement;
        return std::nullopt;
    }

    QString BehaviorController::contextId() const {
        const auto element = context();
        return element ? QString::number(*element) : QString();
    }

    bool BehaviorController::stateMachine() const {
        const auto element = context();
        return element && isState(Model.element(*element).kind());
    }

    void BehaviorController::setContextId(const QString &id) {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || !Model.contains(value) || !Diagram::isBehaviorKind(Model.element(value).kind()) || ContextElement == value)
            return;
        ContextElement = value;
        Positions.clear();
        SelectedNodes.clear();
        SelectedEdges.clear();
        AutoLayout = true;
        emit contextChanged();
        syncNow();
    }

    void BehaviorController::updateContexts() {
        QVariantList contexts;
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                // actions that only send, accept, assign or terminate have no flow of their own (unless they own one)
                const bool flowless = element.kind() == ElementKind::ActionUsage &&
                                      element.actionForm() != Diagram::ActionForm::Plain &&
                                      !Diagram::isControlStructure(element.actionForm()) && element.ownedElements().empty();
                if (Diagram::isBehaviorKind(element.kind()) && !isStateAction(element) && !flowless) {
                    // "action def ChargeBattery", "loop ChargeBattery::charging", "if ChargeBattery::charging::(if c)"
                    QString path;
                    for (const ElementId step: Model.ownershipPath(child)) {
                        const auto &segment = Model.element(step);
                        const QString form = formText(segment);
                        path += (path.isEmpty() ? QString() : QStringLiteral("::")) +
                                (!segment.name().empty() ? q(segment.name())
                                 : QStringLiteral("(") + (form.isEmpty() ? q(keywordOf(segment)) : form) + QStringLiteral(")"));
                    }
                    const QString form = formText(element);
                    const QString title = (form.isEmpty() ? q(keywordOf(element)) : form.section(QLatin1Char(' '), 0, 0)) +
                                          QLatin1Char(' ') + path;
                    contexts.append(QVariantMap{{QStringLiteral("id"), QString::number(child)},
                                                {QStringLiteral("title"), title}});
                }
                visit(child);
            }
        };
        visit(RootId);
        if (contexts != Contexts) {
            Contexts = contexts;
            emit contextsChanged();
        }
    }

    void BehaviorController::chooseContext() {
        if (context())
            return;
        std::optional<ElementId> first;
        std::optional<ElementId> withContent;
        for (const auto &entry: Contexts) {
            const ElementId id = entry.toMap().value(QStringLiteral("id")).toString().toULongLong();
            if (!first)
                first = id;
            const auto &children = Model.children(id);
            if (!withContent && std::any_of(children.begin(), children.end(), [&](ElementId child) {
                    const auto &element = Model.element(child);
                    return Diagram::isSuccessionEnd(element.kind()) && !isStateAction(element);
                }))
                withContent = id;
        }
        const auto chosen = withContent ? withContent : first;
        if (chosen != ContextElement) {
            ContextElement = chosen;
            Positions.clear();
            AutoLayout = true;
            emit contextChanged();
        }
    }

    bool BehaviorController::isNode(ElementId id) const {
        const auto start = context();
        if (!start || !Model.contains(id))
            return false;
        const auto &element = Model.element(id);
        return element.owner() == *start && Diagram::isSuccessionEnd(element.kind()) && !isStateAction(element);
    }

    // --- graph ---------------------------------------------------------------------------------------

    void BehaviorController::scheduleSync() {
        if (SyncQueued)
            return;
        SyncQueued = true;
        QMetaObject::invokeMethod(this, [this] {
            if (SyncQueued)
                syncNow();
        }, Qt::QueuedConnection);
    }

    QSizeF BehaviorController::sizeOf(const DiagramNode &node) const {
        switch (node.Kind) {
            case DiagramTypes::DecisionNode:
                return {36, 36};
            case DiagramTypes::ForkNode:
                return {100, 8};
            case DiagramTypes::StartNode:
                return {22, 22};
            case DiagramTypes::DoneNode:
                return {26, 26};
            default:
                break;
        }
        qreal width = std::max(node.Title.size() * 7.8, node.Keyword.size() * 6.6) + 32;
        int entries = 0;
        for (const auto &compartment: node.Compartments) {
            for (const auto &entry: compartment.toMap().value(QStringLiteral("entries")).toList()) {
                width = std::max(width, entry.toMap().value(QStringLiteral("text")).toString().size() * 7.2 + 24);
                ++entries;
            }
        }
        width = std::clamp<qreal>(width, 120, 360);
        const qreal height = HeaderHeight + (node.Keyword.isEmpty() ? 0 : 6) + (entries > 0 ? entries * EntryHeight + 8 : 0);
        return {std::round(width), height};
    }

    void BehaviorController::syncNow() {
        SyncQueued = false;
        updateContexts();
        chooseContext();

        std::vector<DiagramNode> nodes;
        std::vector<DiagramEdge> edges;
        if (const auto start = context()) {
            for (const ElementId child: Model.children(*start)) {
                if (!isNode(child))
                    continue;
                const auto &element = Model.element(child);
                DiagramNode node;
                node.Id = child;
                switch (element.kind()) {
                    case ElementKind::ControlNode: {
                        const std::string keyword = keywordOf(element);
                        node.Kind = keyword == "decide" || keyword == "merge" ? DiagramTypes::DecisionNode
                                    : keyword == "fork" || keyword == "join" ? DiagramTypes::ForkNode
                                    : keyword == "done" ? DiagramTypes::DoneNode
                                    : DiagramTypes::StartNode;
                        node.Keyword = q(keyword);
                        node.Title = Diagram::isImplicitControlKeyword(keyword) ? QString() : q(element.name());
                        break;
                    }
                    case ElementKind::StateUsage: {
                        node.Kind = DiagramTypes::StateNode;
                        node.Title = usageTitle(element);
                        if (element.declaredKeyword())
                            node.Keyword = QStringLiteral("«") + q(*element.declaredKeyword()) + QStringLiteral("»");
                        if (element.isParallel()) // its states are concurrent
                            node.Keyword = (node.Keyword + QStringLiteral(" «parallel»")).trimmed();
                        QVariantList entries;
                        for (const ElementId action: element.ownedElements()) {
                            const auto &actionElement = Model.element(action);
                            if (!isStateAction(actionElement))
                                continue;
                            const std::string keyword = *actionElement.declaredKeyword();
                            const QString form = formText(actionElement);
                            entries.append(QVariantMap{{QStringLiteral("entryId"), QString::number(action)},
                                                       {QStringLiteral("text"), q(keyword.substr(0, keyword.find(' '))) +
                                                                                QStringLiteral(" / ") +
                                                                                (form.isEmpty() ? usageTitle(actionElement) : form)},
                                                       {QStringLiteral("tooltip"), QString()}});
                        }
                        if (!entries.isEmpty())
                            node.Compartments.append(QVariantMap{{QStringLiteral("title"), QString()},
                                                                 {QStringLiteral("entries"), entries}});
                        break;
                    }
                    default:
                        node.Kind = DiagramTypes::ActionNode;
                        node.Title = usageTitle(element);
                        if (element.kind() == ElementKind::ActionPerformance)
                            node.Keyword = QStringLiteral("«perform»");
                        else if (element.declaredKeyword())
                            node.Keyword = QStringLiteral("«") + q(*element.declaredKeyword()) + QStringLiteral("»");
                        if (const QString form = formText(element); !form.isEmpty()) {
                            // «send» new Show(p) to screen, «if» x > 0, «loop» charging until c
                            const QString keyword = form.section(QLatin1Char(' '), 0, 0);
                            const QString rest = form.section(QLatin1Char(' '), 1);
                            node.Keyword = QStringLiteral("«") + keyword + QStringLiteral("»");
                            node.Title = element.name().empty() ? rest : usageTitle(element) +
                                                                         (rest.isEmpty() ? QString() : QStringLiteral(": ") + rest);
                        }
                        break;
                }
                nodes.push_back(node);
            }
            for (const auto &relationship: Model.relationships()) {
                if ((relationship.Kind != RelationshipKind::Succession && relationship.Kind != RelationshipKind::Transition) ||
                    !isNode(relationship.Source) || !isNode(relationship.Target))
                    continue;
                DiagramEdge edge;
                edge.Kind = relationship.Kind == RelationshipKind::Transition ? DiagramTypes::Transition : DiagramTypes::Succession;
                edge.Source = relationship.Source;
                edge.Target = relationship.Target;
                edge.Key = QStringLiteral("r:%1").arg(relationship.Id);
                edge.Selected = SelectedEdges.count(edge.Key) > 0;
                QString label = q(relationship.Trigger);
                if (!relationship.Guard.empty())
                    label += QStringLiteral(" [") + q(relationship.Guard) + QStringLiteral("]");
                if (!relationship.Effect.empty())
                    label += QStringLiteral(" / ") + q(relationship.Effect);
                if (!relationship.Name.empty())
                    label = q(relationship.Name) + (label.isEmpty() ? QString() : QStringLiteral(": ") + label.trimmed());
                edge.Label = label.trimmed();
                edges.push_back(edge);
            }
        }

        std::set<ElementId> wanted;
        for (const auto &node: nodes)
            wanted.insert(node.Id);
        for (int row = Nodes->count() - 1; row >= 0; --row) {
            if (wanted.count(Nodes->nodes()[std::size_t(row)].Id) == 0)
                Nodes->removeAt(row);
        }
        for (auto it = SelectedNodes.begin(); it != SelectedNodes.end();)
            it = wanted.count(*it) ? std::next(it) : SelectedNodes.erase(it);

        // positions: rows from the start downwards (breadth first along the arrows)
        const bool missing = std::any_of(nodes.begin(), nodes.end(), [&](const DiagramNode &node) {
            return Positions.count(node.Id) == 0;
        });
        if (missing && AutoLayout) {
            std::map<ElementId, int> level;
            std::map<ElementId, int> incoming;
            for (const auto &edge: edges)
                ++incoming[edge.Target];
            std::deque<ElementId> open;
            const auto startFrom = [&](ElementId id) {
                if (level.count(id))
                    return;
                level[id] = 0;
                open.push_back(id);
                while (!open.empty()) {
                    const ElementId current = open.front();
                    open.pop_front();
                    for (const auto &edge: edges) {
                        if (edge.Source == current && level.count(edge.Target) == 0) {
                            level[edge.Target] = level[current] + 1;
                            open.push_back(edge.Target);
                        }
                    }
                }
            };
            for (const auto &node: nodes) {
                if (node.Kind == DiagramTypes::StartNode)
                    startFrom(node.Id);
            }
            for (const auto &node: nodes) {
                if (incoming[node.Id] == 0)
                    startFrom(node.Id);
            }
            for (const auto &node: nodes)
                startFrom(node.Id);
            std::map<int, std::vector<const DiagramNode *>> rows;
            for (const auto &node: nodes)
                rows[level[node.Id]].push_back(&node);
            qreal widest = 0;
            for (const auto &[row, members]: rows) {
                qreal width = 0;
                for (const auto *node: members)
                    width += sizeOf(*node).width() + ColumnGap;
                widest = std::max(widest, width);
            }
            for (const auto &[row, members]: rows) {
                qreal width = 0;
                for (const auto *node: members)
                    width += sizeOf(*node).width() + ColumnGap;
                qreal x = Margin + (widest - width) / 2;
                for (const auto *node: members) {
                    const QSizeF size = sizeOf(*node);
                    Positions[node->Id] = QPointF(x, Margin + row * RowHeight + (60 - std::min<qreal>(size.height(), 60)) / 2);
                    x += size.width() + ColumnGap;
                }
            }
        } else if (missing) {
            const QRectF bounds = contentBounds();
            qreal x = bounds.isNull() ? Margin : bounds.right() + ColumnGap;
            for (const auto &node: nodes) {
                if (Positions.count(node.Id))
                    continue;
                Positions[node.Id] = QPointF(x, Margin);
                x += sizeOf(node).width() + ColumnGap;
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

    void BehaviorController::updateCanvasSize() {
        const QRectF bounds = contentBounds();
        const QSizeF size(std::max<qreal>(1600, bounds.right() + 600), std::max<qreal>(1000, bounds.bottom() + 600));
        if (size != CanvasSize) {
            CanvasSize = size;
            emit canvasSizeChanged();
        }
    }

    QRectF BehaviorController::contentBounds() const {
        QRectF bounds;
        for (const auto &node: Nodes->nodes())
            bounds = bounds.isNull() ? node.Rect : bounds.united(node.Rect);
        return bounds;
    }

    void BehaviorController::layoutAll() {
        Positions.clear();
        AutoLayout = true;
        syncNow();
    }

    // --- lookup --------------------------------------------------------------------------------------

    std::optional<ElementId> BehaviorController::toNode(const QString &id) const {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || !Nodes->find(value))
            return std::nullopt;
        return value;
    }

    QString BehaviorController::nodeAt(qreal x, qreal y) const {
        const QPointF point(x, y);
        const auto &nodes = Nodes->nodes();
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            const qreal grow = it->Rect.height() < 20 ? 8 : 0; // the thin fork / join bars
            if (it->Rect.adjusted(-grow, -grow, grow, grow).contains(point))
                return QString::number(it->Id);
        }
        return {};
    }

    QString BehaviorController::edgeAt(qreal x, qreal y) const {
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

    QRectF BehaviorController::nodeRect(const QString &node) const {
        const auto id = toNode(node);
        return id ? Nodes->find(*id)->Rect : QRectF();
    }

    QString BehaviorController::entryAt(const QString &node, qreal x, qreal y) const {
        const auto id = toNode(node);
        if (!id)
            return {};
        const auto *found = Nodes->find(*id);
        if (found->Kind != DiagramTypes::StateNode || found->Compartments.isEmpty())
            return {};
        const auto entries = found->Compartments.front().toMap().value(QStringLiteral("entries")).toList();
        const qreal top = found->Rect.top() + HeaderHeight + (found->Keyword.isEmpty() ? 0 : 6) + 4;
        const int index = int(std::floor((y - top) / EntryHeight));
        Q_UNUSED(x)
        if (index < 0 || index >= entries.size())
            return {};
        return entries.at(index).toMap().value(QStringLiteral("entryId")).toString();
    }

    // --- selection and moving ------------------------------------------------------------------------

    void BehaviorController::applySelection() {
        for (int row = 0; row < Nodes->count(); ++row)
            Nodes->setSelected(row, SelectedNodes.count(Nodes->nodes()[std::size_t(row)].Id) > 0);
        auto edges = Edges->edges();
        for (auto &edge: edges)
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
        Edges->setEdges(std::move(edges));
        emit selectionChanged();
    }

    void BehaviorController::select(const QString &node, bool additive) {
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (const auto id = toNode(node))
            SelectedNodes.insert(*id);
        applySelection();
    }

    void BehaviorController::selectEdge(const QString &key, bool additive) {
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (Edges->find(key))
            SelectedEdges.insert(key);
        applySelection();
    }

    void BehaviorController::clearSelection() {
        SelectedNodes.clear();
        SelectedEdges.clear();
        applySelection();
    }

    void BehaviorController::moveNode(const QString &node, qreal x, qreal y) {
        const auto id = toNode(node);
        if (!id)
            return;
        AutoLayout = false;
        Positions[*id] = QPointF(std::max<qreal>(0, x), std::max<qreal>(0, y));
        const int row = Nodes->indexOf(*id);
        Nodes->setRect(row, QRectF(Positions[*id], Nodes->nodes()[std::size_t(row)].Rect.size()));
    }

    // --- editing -------------------------------------------------------------------------------------

    bool BehaviorController::runModelChange(const std::function<void()> &change) {
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

    std::string BehaviorController::uniqueName(const std::string &base, ElementId owner) const {
        const auto &siblings = Model.children(owner);
        for (int index = 1;; ++index) {
            const std::string candidate = base + std::to_string(index);
            if (std::none_of(siblings.begin(), siblings.end(),
                             [&](ElementId sibling) { return Model.element(sibling).name() == candidate; }))
                return candidate;
        }
    }

    QString BehaviorController::addNode(int tool, qreal x, qreal y) {
        const auto start = context();
        if (!start) {
            emit statusMessage(tr("Create an action or state definition in the structure view first."));
            return {};
        }
        const QPointF position(std::max<qreal>(0, x - 40), std::max<qreal>(0, y - 18));
        std::optional<ElementId> id;
        QString message;
        const bool ok = runModelChange([&] {
            AutoLayout = false;
            switch (tool) {
                case DiagramTypes::ToolActionUsage: {
                    // onto a state: its "do action"
                    const auto target = toNode(nodeAt(x, y));
                    if (target && Model.element(*target).kind() == ElementKind::StateUsage) {
                        id = Model.addElement(ElementKind::ActionUsage, uniqueName("action", *target), *target);
                        Model.setDeclaredKeyword(*id, std::string("do action"));
                        message = tr("Added a do action - double-click it in the state to make it an entry or exit action.");
                        return;
                    }
                    id = Model.addElement(ElementKind::ActionUsage, uniqueName("action", *start), *start);
                    break;
                }
                case DiagramTypes::ToolState:
                    id = Model.addElement(ElementKind::StateUsage, uniqueName("state", *start), *start);
                    break;
                case DiagramTypes::ToolSendAction:
                case DiagramTypes::ToolAcceptAction:
                case DiagramTypes::ToolAssignAction:
                case DiagramTypes::ToolTerminateAction:
                case DiagramTypes::ToolIfAction:
                case DiagramTypes::ToolWhileAction:
                case DiagramTypes::ToolLoopAction:
                case DiagramTypes::ToolForAction: {
                    // unnamed, as usually written ("then send s to t;"); named when an arrow needs it
                    const auto form = tool == DiagramTypes::ToolSendAction ? Diagram::ActionForm::Send
                                      : tool == DiagramTypes::ToolAcceptAction ? Diagram::ActionForm::Accept
                                      : tool == DiagramTypes::ToolAssignAction ? Diagram::ActionForm::Assign
                                      : tool == DiagramTypes::ToolTerminateAction ? Diagram::ActionForm::Terminate
                                      : tool == DiagramTypes::ToolIfAction ? Diagram::ActionForm::If
                                      : tool == DiagramTypes::ToolWhileAction ? Diagram::ActionForm::While
                                      : tool == DiagramTypes::ToolLoopAction ? Diagram::ActionForm::Loop
                                                                             : Diagram::ActionForm::For;
                    const std::string clause = form == Diagram::ActionForm::Send ? "Signal() to target"
                                               : form == Diagram::ActionForm::Accept ? "Signal"
                                               : form == Diagram::ActionForm::Assign ? "x := 0"
                                               : form == Diagram::ActionForm::If || form == Diagram::ActionForm::While
                                               ? "condition"
                                               : form == Diagram::ActionForm::For ? "x in values" : "";
                    id = Model.addElement(ElementKind::ActionUsage, std::string(), *start);
                    Model.setActionForm(*id, form, clause);
                    message = tr("Double-click it to edit what it does.");
                    break;
                }
                case DiagramTypes::ToolDecision:
                case DiagramTypes::ToolMerge:
                case DiagramTypes::ToolFork:
                case DiagramTypes::ToolJoin: {
                    const std::string keyword = tool == DiagramTypes::ToolDecision ? "decide"
                                                : tool == DiagramTypes::ToolMerge ? "merge"
                                                : tool == DiagramTypes::ToolFork ? "fork" : "join";
                    id = Model.addElement(ElementKind::ControlNode, uniqueName(keyword, *start), *start);
                    Model.setDeclaredKeyword(*id, keyword);
                    break;
                }
                case DiagramTypes::ToolStart:
                case DiagramTypes::ToolDone:
                case DiagramTypes::ToolInitial: {
                    const std::string keyword = tool == DiagramTypes::ToolStart ? "start"
                                                : tool == DiagramTypes::ToolDone ? "done" : "entry";
                    for (const ElementId child: Model.children(*start)) {
                        if (Model.element(child).kind() == ElementKind::ControlNode && Model.element(child).declaredKeyword() == keyword)
                            throw Diagram::ModelError("There is already a " + keyword + " node.");
                    }
                    id = Model.addElement(ElementKind::ControlNode, keyword, *start);
                    Model.setDeclaredKeyword(*id, keyword);
                    if (keyword != "entry")
                        message = tr("Draw an arrow from or to it - without one it is not written into the text.");
                    break;
                }
                default:
                    return;
            }
            Positions[*id] = position;
        });
        if (!ok || !id)
            return {};
        if (!message.isEmpty())
            emit statusMessage(message);
        return QString::number(*id);
    }

    QString BehaviorController::connectNodes(const QString &source, const QString &target) {
        const auto from = toNode(source);
        const auto to = toNode(target);
        if (!from || !to)
            return tr("Draw the arrow onto another node.");
        const bool states = Model.element(*from).kind() == ElementKind::StateUsage &&
                            Model.element(*to).kind() == ElementKind::StateUsage;
        const RelationshipKind kind = states ? RelationshipKind::Transition : RelationshipKind::Succession;
        // an unnamed action ("send s to t;") that is not declared right after the source gets a name to be referenced
        const auto &targetElement = Model.element(*to);
        const bool needsName = targetElement.name().empty() &&
                               !Diagram::RelationshipRules::canRelate(Model, kind, *from, *to) &&
                               (targetElement.kind() == ElementKind::ActionUsage ||
                                targetElement.kind() == ElementKind::ControlNode);
        if (needsName) {
            std::string base = targetElement.kind() == ElementKind::ControlNode ? keywordOf(targetElement)
                                                                                   : Diagram::toKeyword(targetElement.actionForm());
            if (base.empty())
                base = "action";
            const std::string name = uniqueName(base, targetElement.owner());
            try {
                Model.rename(*to, name);
            } catch (const std::exception &) {
            }
        }
        if (const auto rule = Diagram::RelationshipRules::canRelate(Model, kind, *from, *to); !rule)
            return QString::fromStdString(rule.Reason);
        runModelChange([&] { Model.addRelationship(kind, *from, *to); });
        emit statusMessage(states ? tr("Transition created - double-click it for trigger, guard and effect.")
                                  : tr("Succession created - double-click it for a guard."));
        return {};
    }

    void BehaviorController::deleteEdge(const QString &key) {
        if (!key.startsWith(QStringLiteral("r:")))
            return;
        const auto id = key.mid(2).toULongLong();
        runModelChange([&] {
            if (Model.containsRelationship(id))
                Model.removeRelationship(id);
        });
    }

    void BehaviorController::deleteSelection() {
        const auto edges = SelectedEdges;
        const auto nodes = SelectedNodes;
        SelectedEdges.clear();
        SelectedNodes.clear();
        runModelChange([&] {
            for (const auto &key: edges) {
                const auto id = key.mid(2).toULongLong();
                if (Model.containsRelationship(id))
                    Model.removeRelationship(id);
            }
            for (const ElementId node: nodes) {
                if (Model.contains(node))
                    Model.removeElement(node);
            }
        });
    }
}
