//
// Sequence view: lifelines, event occurrences and messages of one interaction.
//

#include "SequenceController.h"
#include "DiagramGeometry.h"

#include <QMetaObject>

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
        constexpr qreal ColumnWidth = 190.0;
        constexpr qreal HeaderWidth = 164.0;
        constexpr qreal HeaderHeight = 44.0;
        constexpr qreal HeaderStep = 58.0;  ///< vertical distance of the header rows of nested lifelines
        constexpr qreal RowHeight = 50.0;
        constexpr qreal EventSize = 14.0;
        constexpr qreal EdgeHitDistance = 6.0;
        /// Ids of nodes that are no element: the header of a part with lifelines below it, and the end of a
        /// message between whole lifelines (no event).
        constexpr std::uint64_t GroupBit = std::uint64_t(1) << 62;
        constexpr std::uint64_t EndBit = std::uint64_t(1) << 61;

        QString q(const std::string &text) {
            return QString::fromStdString(text);
        }

        /// Kinds of usages drawn as lifeline.
        bool isLifelineKind(ElementKind kind) {
            return kind == ElementKind::PartUsage || kind == ElementKind::ItemUsage || kind == ElementKind::OccurrenceUsage;
        }

        QString headerTitle(const Diagram::SysMLElement &element) {
            QString title = q(element.name());
            if (!element.typeName().empty())
                title += QStringLiteral(" : ") + q(element.typeName());
            return title;
        }

        QString keywordOf(const Diagram::SysMLElement &element) {
            std::string keyword = Diagram::defaultKeyword(element.kind());
            if (element.declaredKeyword() && !element.declaredKeyword()->empty())
                keyword = *element.declaredKeyword();
            const auto &prefixes = element.prefixes();
            if (std::find(prefixes.begin(), prefixes.end(), "ref") != prefixes.end())
                keyword = "ref " + keyword;
            return QStringLiteral("«") + q(keyword) + QStringLiteral("»");
        }
    }

    SequenceController::SequenceController(Diagram::SemanticModel &model, QObject *parent) :
            QObject(parent),
            Model(model),
            Nodes(new DiagramNodeModel(this)),
            Edges(new DiagramEdgeModel(this)) {
        ListenerToken = Model.addListener([this](const Diagram::ModelChange &) { scheduleSync(); });
        syncNow();
    }

    SequenceController::~SequenceController() {
        Model.removeListener(ListenerToken);
    }

    // --- context -------------------------------------------------------------------------------------

    std::optional<ElementId> SequenceController::context() const {
        if (ContextElement && Model.contains(*ContextElement) && Diagram::isOccurrenceKind(Model.element(*ContextElement).kind()))
            return ContextElement;
        return std::nullopt;
    }

    QString SequenceController::contextId() const {
        const auto element = context();
        return element ? QString::number(*element) : QString();
    }

    void SequenceController::setContextId(const QString &id) {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || !Model.contains(value) || !Diagram::isOccurrenceKind(Model.element(value).kind()) || ContextElement == value)
            return;
        ContextElement = value;
        SelectedNodes.clear();
        SelectedEdges.clear();
        emit contextChanged();
        syncNow();
    }

    void SequenceController::setArrowKind(int kind) {
        const int value = kind == 1 ? 1 : 0;
        if (value == ArrowKind)
            return;
        ArrowKind = value;
        emit arrowKindChanged();
    }

    void SequenceController::updateContexts() {
        QVariantList contexts;
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                if (Diagram::isOccurrenceKind(element.kind()))
                    contexts.append(QVariantMap{{QStringLiteral("id"), QString::number(child)},
                                                {QStringLiteral("title"), q(Diagram::defaultKeyword(element.kind())) +
                                                                          QLatin1Char(' ') + q(Model.qualifiedName(child))}});
                visit(child);
            }
        };
        visit(RootId);
        if (contexts != Contexts) {
            Contexts = contexts;
            emit contextsChanged();
        }
    }

    void SequenceController::chooseContext() {
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
                    return isLifelineKind(Model.element(child).kind());
                }))
                withContent = id;
        }
        const auto chosen = withContent ? withContent : first;
        if (chosen != ContextElement) {
            ContextElement = chosen;
            emit contextChanged();
        }
    }

    // --- graph ---------------------------------------------------------------------------------------

    void SequenceController::scheduleSync() {
        if (SyncQueued)
            return;
        SyncQueued = true;
        QMetaObject::invokeMethod(this, [this] {
            if (SyncQueued)
                syncNow();
        }, Qt::QueuedConnection);
    }

    qreal SequenceController::rowY(int row) const {
        return FirstRowY + row * RowHeight;
    }

    void SequenceController::syncNow() {
        SyncQueued = false;
        updateContexts();
        chooseContext();

        std::vector<DiagramNode> nodes;
        std::vector<DiagramEdge> edges;
        Lanes.clear();
        EventRows.clear();
        const auto start = context();

        // lifelines: parts (items, occurrences) of the interaction; a part with parts gets a header above them
        struct Group {
            ElementId Id;
            int Depth;
            std::size_t FirstLane;
            std::size_t LastLane;
        };
        std::vector<Group> groups;
        int maxDepth = 0;
        const auto eventsOf = [&](ElementId owner) {
            std::vector<ElementId> events;
            for (const ElementId child: Model.children(owner)) {
                if (Model.element(child).kind() == ElementKind::EventOccurrence)
                    events.push_back(child);
            }
            return events;
        };
        std::function<void(ElementId, int)> collect = [&](ElementId owner, int depth) {
            for (const ElementId child: Model.children(owner)) {
                if (!isLifelineKind(Model.element(child).kind()))
                    continue;
                const auto &children = Model.children(child);
                const bool nested = std::any_of(children.begin(), children.end(), [&](ElementId inner) {
                    return isLifelineKind(Model.element(inner).kind());
                });
                maxDepth = std::max(maxDepth, depth);
                if (!nested) {
                    Lanes.push_back({child, depth, 0, eventsOf(child)});
                    continue;
                }
                const std::size_t first = Lanes.size();
                auto own = eventsOf(child);
                if (!own.empty())
                    Lanes.push_back({child, depth + 1, 0, std::move(own)}); // its own events, below its header
                collect(child, depth + 1);
                groups.push_back({child, depth, first, Lanes.size() - 1});
                maxDepth = std::max(maxDepth, depth + 1);
            }
        };
        if (start) {
            // events of the interaction itself: a lifeline of its own on the left
            auto own = eventsOf(*start);
            if (!own.empty())
                Lanes.push_back({*start, 0, 0, std::move(own)});
            collect(*start, 0);
        }
        for (std::size_t i = 0; i < Lanes.size(); ++i)
            Lanes[i].CenterX = Margin + ColumnWidth * qreal(i) + HeaderWidth / 2;
        FirstRowY = Margin + HeaderStep * (maxDepth + 1) + 24;

        // messages and "then" between events shown here
        std::map<ElementId, const Lane *> laneOfEvent;
        std::map<ElementId, const Lane *> laneOfPart;
        for (const auto &lane: Lanes) {
            laneOfPart[lane.Id] = &lane;
            for (const ElementId event: lane.Events)
                laneOfEvent[event] = &lane;
        }
        const auto shown = [&](ElementId end) { return laneOfEvent.count(end) > 0 || laneOfPart.count(end) > 0; };
        std::vector<const Diagram::Relationship *> messages;
        std::vector<const Diagram::Relationship *> successions;
        for (const auto &relationship: Model.relationships()) {
            if (relationship.Kind == RelationshipKind::Message && shown(relationship.Source) && shown(relationship.Target))
                messages.push_back(&relationship);
            if (relationship.Kind == RelationshipKind::Succession && laneOfEvent.count(relationship.Source) &&
                laneOfEvent.count(relationship.Target))
                successions.push_back(&relationship);
        }

        // rows: textual order on each lifeline, a message from and to the same row (if possible), the messages one
        // below the other in their textual order (if possible), "then" below
        std::map<Diagram::RelationshipId, int> messageRows;
        const auto solve = [&](bool level, bool ordered) {
            EventRows.clear();
            messageRows.clear();
            for (const auto &lane: Lanes)
                for (const ElementId event: lane.Events)
                    EventRows[event] = 0;
            for (const auto *message: messages)
                messageRows[message->Id] = 0;
            for (int iteration = 0; iteration < 120; ++iteration) {
                bool changed = false;
                const auto raise = [&](int &value, int minimum) {
                    if (value < minimum) {
                        value = minimum;
                        changed = true;
                    }
                };
                for (const auto &lane: Lanes)
                    for (std::size_t k = 1; k < lane.Events.size(); ++k)
                        raise(EventRows[lane.Events[k]], EventRows[lane.Events[k - 1]] + 1);
                std::optional<Diagram::RelationshipId> previous;
                for (const auto *message: messages) {
                    int &row = messageRows[message->Id];
                    const bool sourceEvent = laneOfEvent.count(message->Source) > 0;
                    const bool targetEvent = laneOfEvent.count(message->Target) > 0;
                    if (sourceEvent)
                        raise(row, EventRows[message->Source]);
                    if (targetEvent)
                        raise(row, EventRows[message->Target]);
                    if (previous && (ordered || message->Sequenced))
                        raise(row, messageRows[*previous] + 1);
                    const bool sameLane = sourceEvent && targetEvent &&
                                          laneOfEvent[message->Source] == laneOfEvent[message->Target];
                    if (level && !sameLane) {
                        if (sourceEvent)
                            raise(EventRows[message->Source], row);
                        if (targetEvent)
                            raise(EventRows[message->Target], row);
                    }
                    previous = message->Id;
                }
                for (const auto *succession: successions)
                    raise(EventRows[succession->Target], EventRows[succession->Source] + 1);
                if (!changed)
                    return true;
            }
            return false;
        };
        const bool solved = solve(true, true) || solve(true, false);
        if (!solved)
            solve(false, false); // contradicting messages (crossing): drawn slanted

        MessageRows = messageRows;
        int lastRow = 0;
        for (const auto &[event, row]: EventRows)
            lastRow = std::max(lastRow, row);
        for (const auto &[message, row]: messageRows)
            lastRow = std::max(lastRow, row);
        const qreal bottom = rowY(lastRow) + RowHeight * 3; // room for new messages below the last one

        if (start) {
            for (const auto &group: groups) {
                const auto &element = Model.element(group.Id);
                DiagramNode node;
                node.Id = group.Id | GroupBit;
                node.Kind = DiagramTypes::LifelineGroupNode;
                node.Keyword = keywordOf(element);
                node.Title = headerTitle(element);
                const qreal left = Lanes[group.FirstLane].CenterX - HeaderWidth / 2;
                const qreal right = Lanes[group.LastLane].CenterX + HeaderWidth / 2;
                node.Rect = QRectF(left, Margin + HeaderStep * group.Depth, right - left, HeaderHeight);
                node.Depth = group.Depth;
                nodes.push_back(node);
            }
            for (const auto &lane: Lanes) {
                const auto &element = Model.element(lane.Id);
                DiagramNode node;
                node.Id = lane.Id;
                node.Kind = DiagramTypes::LifelineNode;
                node.Keyword = keywordOf(element);
                node.Title = headerTitle(element);
                const qreal top = Margin + HeaderStep * lane.Depth;
                node.Rect = QRectF(lane.CenterX - HeaderWidth / 2, top, HeaderWidth, bottom - top);
                node.Depth = lane.Depth;
                nodes.push_back(node);
                for (const ElementId event: lane.Events) {
                    const auto &eventElement = Model.element(event);
                    DiagramNode dot;
                    dot.Id = event;
                    dot.Kind = DiagramTypes::EventNode;
                    dot.Title = headerTitle(eventElement);
                    dot.Rect = QRectF(lane.CenterX - EventSize / 2, rowY(EventRows[event]) - EventSize / 2, EventSize, EventSize);
                    dot.Depth = 50;
                    nodes.push_back(dot);
                }
            }
            for (const auto *message: messages) {
                // a message between whole lifelines: an end of its own on the lifeline at the row of the message
                const auto end = [&](ElementId element, std::uint64_t side) -> std::uint64_t {
                    if (laneOfEvent.count(element))
                        return element;
                    DiagramNode dot;
                    dot.Id = EndBit | (message->Id << 1) | side;
                    dot.Kind = DiagramTypes::EventNode;
                    const qreal x = laneOfPart[element]->CenterX;
                    dot.Rect = QRectF(x - 2, rowY(messageRows[message->Id]) - 2, 4, 4);
                    dot.Depth = 50;
                    nodes.push_back(dot);
                    return dot.Id;
                };
                DiagramEdge edge;
                edge.Kind = DiagramTypes::Message;
                edge.Source = end(message->Source, 0);
                edge.Target = end(message->Target, 1);
                edge.Key = QStringLiteral("r:%1").arg(message->Id);
                QString label = q(message->Name);
                if (!message->TypeName.empty())
                    label += (label.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("of ") + q(message->TypeName);
                edge.Label = (message->Sequenced ? QStringLiteral("then ") : QString()) + label;
                edges.push_back(edge);
            }
            for (const auto *succession: successions) {
                DiagramEdge edge;
                edge.Kind = DiagramTypes::Succession;
                edge.Source = succession->Source;
                edge.Target = succession->Target;
                edge.Key = QStringLiteral("r:%1").arg(succession->Id);
                // along one lifeline the arrow says it all; between lifelines: "then"
                const bool along = laneOfEvent[succession->Source] == laneOfEvent[succession->Target];
                edge.Label = !succession->Name.empty() ? q(succession->Name) : along ? QString() : QStringLiteral("then");
                edges.push_back(edge);
            }
        }

        std::set<std::uint64_t> wanted;
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
            const int row = Nodes->indexOf(node.Id);
            if (row >= 0) {
                Nodes->updateContent(row, node);
                Nodes->setRect(row, node.Rect);
                Nodes->setSelected(row, node.Selected);
            } else {
                Nodes->append(node);
            }
        }
        for (auto it = SelectedEdges.begin(); it != SelectedEdges.end();) {
            const bool exists = std::any_of(edges.begin(), edges.end(), [&](const DiagramEdge &edge) { return edge.Key == *it; });
            it = exists ? std::next(it) : SelectedEdges.erase(it);
        }
        for (auto &edge: edges)
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
        Edges->setEdges(std::move(edges));
        updateCanvasSize();
        emit selectionChanged();
    }

    void SequenceController::updateCanvasSize() {
        const QRectF bounds = contentBounds();
        const QSizeF size(std::max<qreal>(1600, bounds.right() + 600), std::max<qreal>(1000, bounds.bottom() + 600));
        if (size != CanvasSize) {
            CanvasSize = size;
            emit canvasSizeChanged();
        }
    }

    QRectF SequenceController::contentBounds() const {
        QRectF bounds;
        for (const auto &node: Nodes->nodes())
            bounds = bounds.isNull() ? node.Rect : bounds.united(node.Rect);
        return bounds;
    }

    // --- lookup --------------------------------------------------------------------------------------

    std::optional<ElementId> SequenceController::toNode(const QString &id) const {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || !Nodes->find(value))
            return std::nullopt;
        return value;
    }

    std::optional<ElementId> SequenceController::toElement(const QString &id) const {
        const auto node = toNode(id);
        if (!node || (*node & EndBit) != 0)
            return std::nullopt;
        const ElementId element = *node & ~GroupBit;
        return Model.contains(element) ? std::optional<ElementId>(element) : std::nullopt;
    }

    QString SequenceController::elementOf(const QString &node) const {
        const auto element = toElement(node);
        return element ? QString::number(*element) : QString();
    }

    bool SequenceController::isEvent(const QString &node) const {
        const auto element = toElement(node);
        return element && Model.element(*element).kind() == ElementKind::EventOccurrence;
    }

    const SequenceController::Lane *SequenceController::laneAt(const QPointF &point) const {
        const Lane *best = nullptr;
        for (const auto &lane: Lanes) {
            const auto *node = Nodes->find(lane.Id);
            if (!node || point.y() < node->Rect.top() || std::abs(point.x() - lane.CenterX) > ColumnWidth / 2)
                continue;
            if (!best || std::abs(point.x() - lane.CenterX) < std::abs(point.x() - best->CenterX))
                best = &lane;
        }
        return best;
    }

    const SequenceController::Lane *SequenceController::laneOf(ElementId element) const {
        for (const auto &lane: Lanes) {
            if (lane.Id == element || std::find(lane.Events.begin(), lane.Events.end(), element) != lane.Events.end())
                return &lane;
        }
        return nullptr;
    }

    std::optional<ElementId> SequenceController::eventAt(const QPointF &point) const {
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind == DiagramTypes::EventNode && (node.Id & EndBit) == 0 &&
                node.Rect.adjusted(-7, -7, 7, 7).contains(point))
                return node.Id;
        }
        return std::nullopt;
    }

    QString SequenceController::nodeAt(qreal x, qreal y) const {
        const QPointF point(x, y);
        if (const auto event = eventAt(point))
            return QString::number(*event);
        // headers, then the lines below them
        for (const auto &node: Nodes->nodes()) {
            if ((node.Kind == DiagramTypes::LifelineNode || node.Kind == DiagramTypes::LifelineGroupNode) &&
                QRectF(node.Rect.topLeft(), QSizeF(node.Rect.width(), HeaderHeight)).contains(point))
                return QString::number(node.Id);
        }
        if (const auto *lane = laneAt(point); lane && std::abs(x - lane->CenterX) <= 24)
            return QString::number(lane->Id);
        return {};
    }

    QString SequenceController::lifelineAt(qreal x, qreal y) const {
        const auto *lane = laneAt(QPointF(x, y));
        return lane ? QString::number(lane->Id) : QString();
    }

    QString SequenceController::edgeAt(qreal x, qreal y) const {
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

    QRectF SequenceController::nodeRect(const QString &node) const {
        const auto id = toNode(node);
        return id ? Nodes->find(*id)->Rect : QRectF();
    }

    // --- selection -----------------------------------------------------------------------------------

    void SequenceController::applySelection() {
        for (int row = 0; row < Nodes->count(); ++row)
            Nodes->setSelected(row, SelectedNodes.count(Nodes->nodes()[std::size_t(row)].Id) > 0);
        auto edges = Edges->edges();
        for (auto &edge: edges)
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
        Edges->setEdges(std::move(edges));
        emit selectionChanged();
    }

    void SequenceController::select(const QString &node, bool additive) {
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (const auto id = toNode(node); id && (*id & EndBit) == 0)
            SelectedNodes.insert(*id);
        applySelection();
    }

    void SequenceController::selectEdge(const QString &key, bool additive) {
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (Edges->find(key))
            SelectedEdges.insert(key);
        applySelection();
    }

    void SequenceController::clearSelection() {
        SelectedNodes.clear();
        SelectedEdges.clear();
        applySelection();
    }

    // --- editing -------------------------------------------------------------------------------------

    bool SequenceController::runModelChange(const std::function<void()> &change) {
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

    std::string SequenceController::uniqueName(const std::string &base, ElementId owner, bool numbered) const {
        const auto &siblings = Model.children(owner);
        for (int index = 1;; ++index) {
            // "event1", "event2", ...; not numbered: "message1Sent", "message1Sent2", ...
            const std::string candidate = numbered ? base + std::to_string(index)
                                                   : index == 1 ? base : base + std::to_string(index);
            if (std::none_of(siblings.begin(), siblings.end(),
                             [&](ElementId sibling) { return Model.element(sibling).name() == candidate; }))
                return candidate;
        }
    }

    std::string SequenceController::uniqueMessageName() const {
        for (int index = 1;; ++index) {
            const std::string candidate = "message" + std::to_string(index);
            if (std::none_of(Model.relationships().begin(), Model.relationships().end(),
                             [&](const Diagram::Relationship &relationship) { return relationship.Name == candidate; }))
                return candidate;
        }
    }

    std::size_t SequenceController::eventIndexFor(const Lane &lane, qreal y, std::optional<ElementId> moved) const {
        // before the first event of the lifeline that lies below y, else after its last child
        const auto &children = Model.children(lane.Id);
        for (const ElementId event: lane.Events) {
            if (moved && event == *moved)
                continue;
            if (rowY(EventRows.at(event)) > y) {
                std::size_t index = std::size_t(std::find(children.begin(), children.end(), event) - children.begin());
                if (moved) {
                    const auto current = std::find(children.begin(), children.end(), *moved);
                    if (current != children.end() && std::size_t(current - children.begin()) < index)
                        --index; // the moved one leaves its place first
                }
                return index;
            }
        }
        return children.size();
    }

    QString SequenceController::addNode(int tool, qreal x, qreal y) {
        const auto start = context();
        if (!start) {
            emit statusMessage(tr("Create an occurrence definition (an interaction) in the structure view first."));
            return {};
        }
        std::optional<ElementId> id;
        if (tool == DiagramTypes::ToolLifeline) {
            // "ref part part1;" among the lifelines of the interaction, placed by x
            std::optional<ElementId> before;
            for (const auto &lane: Lanes) {
                if (lane.Depth != 0 || lane.Id == *start)
                    continue;
                if (lane.CenterX > x) {
                    before = lane.Id;
                    break;
                }
            }
            for (const auto &node: Nodes->nodes()) {
                // a header of nested lifelines also counts as one lifeline
                if (node.Kind == DiagramTypes::LifelineGroupNode && node.Depth == 0 && node.Rect.center().x() > x &&
                    (!before || node.Rect.center().x() < Nodes->find(*before)->Rect.center().x()))
                    before = node.Id & ~GroupBit;
            }
            if (!runModelChange([&] {
                    id = Model.addElement(ElementKind::PartUsage, uniqueName("part", *start), *start);
                    Model.setPrefixes(*id, {"ref"});
                    if (before) {
                        const auto &children = Model.children(*start);
                        Model.setChildIndex(*id, std::size_t(std::find(children.begin(), children.end(), *before) -
                                                             children.begin()));
                    }
                }))
                return {};
            emit statusMessage(tr("Added a lifeline - double-click its header to name and type it."));
            return QString::number(*id);
        }
        if (tool == DiagramTypes::ToolEvent) {
            const auto *lane = laneAt(QPointF(x, y));
            if (!lane) {
                emit statusMessage(tr("Drop the event onto a lifeline."));
                return {};
            }
            const ElementId owner = lane->Id;
            const std::size_t index = eventIndexFor(*lane, y, std::nullopt);
            if (!runModelChange([&] {
                    id = Model.addElement(ElementKind::EventOccurrence, uniqueName("event", owner), owner);
                    Model.setChildIndex(*id, index);
                }))
                return {};
            emit statusMessage(tr("Added an event - draw a message from or to it."));
            return QString::number(*id);
        }
        return {};
    }

    QString SequenceController::connectPoints(qreal x1, qreal y1, qreal x2, qreal y2) {
        const auto start = context();
        if (!start)
            return tr("Choose an interaction first.");
        const QPointF from(x1, y1);
        const QPointF to(x2, y2);
        const auto sourceEvent = eventAt(from);
        const auto targetEvent = eventAt(to);
        if (ArrowKind == 1) {
            // "then": from one event to another
            if (!sourceEvent || !targetEvent)
                return tr("Draw \"then\" from one event to another.");
            if (const auto rule = Diagram::RelationshipRules::canRelate(Model, RelationshipKind::Succession, *sourceEvent,
                                                                        *targetEvent); !rule)
                return QString::fromStdString(rule.Reason);
            runModelChange([&] { Model.addRelationship(RelationshipKind::Succession, *sourceEvent, *targetEvent); });
            emit statusMessage(tr("\"then\" created: the second event happens after the first."));
            return {};
        }
        const Lane *sourceLane = sourceEvent ? laneOf(*sourceEvent) : laneAt(from);
        const Lane *targetLane = targetEvent ? laneOf(*targetEvent) : laneAt(to);
        if (!sourceLane || !targetLane)
            return tr("Draw the message from one lifeline to another.");
        if (sourceLane == targetLane)
            return tr("A message goes to another lifeline.");
        if (!sourceEvent && sourceLane->Id == *start)
            return tr("Start the message at an event of the interaction.");
        if (!targetEvent && targetLane->Id == *start)
            return tr("End the message at an event of the interaction.");
        const std::string name = uniqueMessageName();
        // the indices are computed before anything changes (an event added to one lifeline does not move the other)
        const std::size_t sourceIndex = sourceEvent ? 0 : eventIndexFor(*sourceLane, y1, std::nullopt);
        const std::size_t targetIndex = targetEvent ? 0 : eventIndexFor(*targetLane, y2, std::nullopt);
        const ElementId sourceOwner = sourceLane->Id;
        const ElementId targetOwner = targetLane->Id;
        const bool ok = runModelChange([&] {
            ElementId source = sourceEvent.value_or(0);
            ElementId target = targetEvent.value_or(0);
            if (!sourceEvent) {
                source = Model.addElement(ElementKind::EventOccurrence, uniqueName(name + "Sent", sourceOwner, false), sourceOwner);
                Model.setChildIndex(source, sourceIndex);
            }
            if (!targetEvent) {
                target = Model.addElement(ElementKind::EventOccurrence, uniqueName(name + "Received", targetOwner, false), targetOwner);
                Model.setChildIndex(target, targetIndex);
            }
            const auto message = Model.addRelationship(RelationshipKind::Message, source, target);
            Model.setRelationshipLabel(message, name, "");
            Model.setRelationshipContext(message, *start);
            // the messages are drawn one below the other in their textual order: before the first one below it
            const qreal y = (y1 + y2) / 2;
            const auto &relationships = Model.relationships();
            for (std::size_t index = 0; index < relationships.size(); ++index) {
                const auto row = MessageRows.find(relationships[index].Id);
                if (row != MessageRows.end() && rowY(row->second) > y) {
                    Model.setRelationshipIndex(message, index);
                    break;
                }
            }
        });
        if (ok)
            emit statusMessage(tr("Message created - double-click it for its name and payload."));
        return {};
    }

    void SequenceController::moveEvent(const QString &event, qreal y) {
        const auto id = toElement(event);
        if (!id || Model.element(*id).kind() != ElementKind::EventOccurrence)
            return;
        const auto *lane = laneOf(*id);
        if (!lane)
            return;
        const std::size_t index = eventIndexFor(*lane, y, *id);
        runModelChange([&] { Model.setChildIndex(*id, index); });
    }

    void SequenceController::shiftEvent(const QString &event, int direction) {
        const auto id = toElement(event);
        if (!id || Model.element(*id).kind() != ElementKind::EventOccurrence)
            return;
        const auto *lane = laneOf(*id);
        if (!lane)
            return;
        const auto position = std::find(lane->Events.begin(), lane->Events.end(), *id);
        const auto offset = position - lane->Events.begin();
        const auto neighbour = offset + (direction < 0 ? -1 : 1);
        if (neighbour < 0 || neighbour >= std::ptrdiff_t(lane->Events.size()))
            return;
        const auto &children = Model.children(lane->Id);
        const ElementId other = lane->Events[std::size_t(neighbour)];
        const std::size_t index = std::size_t(std::find(children.begin(), children.end(), other) - children.begin());
        runModelChange([&] { Model.setChildIndex(*id, index); });
    }

    void SequenceController::moveLifeline(const QString &lifeline, qreal x) {
        const auto id = toElement(lifeline);
        const auto start = context();
        if (!id || !start || *id == *start)
            return;
        // before the first sibling lifeline (or header) whose middle lies right of x
        const ElementId owner = Model.element(*id).owner();
        const auto &siblings = Model.children(owner);
        std::optional<ElementId> before;
        qreal beforeX = 0;
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind != DiagramTypes::LifelineNode && node.Kind != DiagramTypes::LifelineGroupNode)
                continue;
            const ElementId element = node.Id & ~GroupBit;
            if (element == *id || !Model.contains(element) || Model.element(element).owner() != owner)
                continue;
            const qreal middle = node.Rect.center().x();
            if (middle > x && (!before || middle < beforeX)) {
                before = element;
                beforeX = middle;
            }
        }
        std::size_t index = siblings.size();
        if (before) {
            index = std::size_t(std::find(siblings.begin(), siblings.end(), *before) - siblings.begin());
            const auto current = std::find(siblings.begin(), siblings.end(), *id);
            if (std::size_t(current - siblings.begin()) < index)
                --index;
        }
        runModelChange([&] { Model.setChildIndex(*id, index); });
    }

    void SequenceController::deleteEdge(const QString &key) {
        if (!key.startsWith(QStringLiteral("r:")))
            return;
        const auto id = key.mid(2).toULongLong();
        runModelChange([&] {
            if (Model.containsRelationship(id))
                Model.removeRelationship(id);
        });
    }

    void SequenceController::deleteSelection() {
        const auto edges = SelectedEdges;
        const auto nodes = SelectedNodes;
        SelectedEdges.clear();
        SelectedNodes.clear();
        const auto start = context();
        runModelChange([&] {
            for (const auto &key: edges) {
                const auto id = key.mid(2).toULongLong();
                if (Model.containsRelationship(id))
                    Model.removeRelationship(id);
            }
            for (const std::uint64_t node: nodes) {
                const ElementId element = node & ~GroupBit;
                if ((node & EndBit) == 0 && Model.contains(element) && element != start)
                    Model.removeElement(element);
            }
        });
    }
}
