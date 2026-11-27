//
// Sequence view of one interaction (occurrence definition or usage): its parts as lifelines (left to right in
// their textual order, nested parts below the header of their owner), the event occurrences of each part on its
// lifeline (top to bottom in their textual order) and the messages between them. Like the other views it holds
// nothing of its own: every lifeline, event and message is read from the semantic model, every edit is written
// back, and the vertical place of an event follows from the text (its order on the lifeline, the messages that
// start or end at it, "then" successions between lifelines).
//
//   lifeline (palette)                 ref part part1;                  (in the interaction)
//   event (palette, onto a lifeline)   event occurrence event1;         (in that part, at the place dropped)
//   message (drag between lifelines)   message message1 from a.message1Sent to b.message1Received;
//                                      (the two events are added where the message starts and ends)
//   "then" (drag from event to event)  first a.sent then b.received;
//
// Coordinates are canvas coordinates; ids are element ids, passed to QML as strings. Headers of parts whose parts
// are the lifelines and the ends of messages between whole lifelines have ids of their own (see elementOf).
//

#ifndef STRUCTURASYSTEMS_SEQUENCECONTROLLER_H
#define STRUCTURASYSTEMS_SEQUENCECONTROLLER_H

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <Diagram/Semantic/SemanticModel.h>

#include "DiagramEdgeModel.h"
#include "DiagramNodeModel.h"
#include "DiagramTypes.h"

namespace StructuraSystems::Client {

    class SequenceController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("Owned by DiagramEditorModel.")
        Q_PROPERTY(StructuraSystems::Client::DiagramNodeModel *nodes READ nodes CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::DiagramEdgeModel *edges READ edges CONSTANT)
        /// Element id of the interaction shown.
        Q_PROPERTY(QString contextId READ contextId WRITE setContextId NOTIFY contextChanged)
        /// Interactions that can be shown: [{id, title}]
        Q_PROPERTY(QVariantList contexts READ contexts NOTIFY contextsChanged)
        /// What dragging from a lifeline / event draws: 0 = message, 1 = "then" (succession between two events).
        Q_PROPERTY(int arrowKind READ arrowKind WRITE setArrowKind NOTIFY arrowKindChanged)
        Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
        Q_PROPERTY(qreal canvasWidth READ canvasWidth NOTIFY canvasSizeChanged)
        Q_PROPERTY(qreal canvasHeight READ canvasHeight NOTIFY canvasSizeChanged)

    public:
        explicit SequenceController(Diagram::SemanticModel &model, QObject *parent = nullptr);
        ~SequenceController() override;

        [[nodiscard]] DiagramNodeModel *nodes() const { return Nodes; }
        [[nodiscard]] DiagramEdgeModel *edges() const { return Edges; }
        [[nodiscard]] QString contextId() const;
        void setContextId(const QString &id);
        [[nodiscard]] QVariantList contexts() const { return Contexts; }
        [[nodiscard]] int arrowKind() const { return ArrowKind; }
        void setArrowKind(int kind);
        [[nodiscard]] bool hasSelection() const { return !SelectedNodes.empty() || !SelectedEdges.empty(); }
        [[nodiscard]] qreal canvasWidth() const { return CanvasSize.width(); }
        [[nodiscard]] qreal canvasHeight() const { return CanvasSize.height(); }

        /// DiagramTypes::ToolLifeline (a new part, placed by x among the lifelines) or ToolEvent (onto a lifeline,
        /// placed by y among its events). Returns the new element id.
        Q_INVOKABLE QString addNode(int tool, qreal x, qreal y);
        /// Drag from (x1, y1) to (x2, y2): a message (from / to an event, or from / to a place on a lifeline, where
        /// an event is added), or "then" between two events (arrowKind). Returns an error text (empty on success).
        Q_INVOKABLE QString connectPoints(qreal x1, qreal y1, qreal x2, qreal y2);
        /// The node at a point (events before lifelines before headers), empty if none.
        Q_INVOKABLE QString nodeAt(qreal x, qreal y) const;
        /// The lifeline whose column contains x (below its header), empty if none.
        Q_INVOKABLE QString lifelineAt(qreal x, qreal y) const;
        Q_INVOKABLE QString edgeAt(qreal x, qreal y) const;
        Q_INVOKABLE QRectF nodeRect(const QString &node) const;
        /// Element of a node: the part of a header, empty for the end of a message between whole lifelines.
        Q_INVOKABLE QString elementOf(const QString &node) const;
        /// The node is an event occurrence (not a lifeline, header or end of a message).
        Q_INVOKABLE bool isEvent(const QString &node) const;
        /// An event dragged to y: its new place among the events of its lifeline.
        Q_INVOKABLE void moveEvent(const QString &event, qreal y);
        /// An event one place up (-1) or down (+1) among the events of its lifeline.
        Q_INVOKABLE void shiftEvent(const QString &event, int direction);
        /// A lifeline (or header) dragged to x: its new place among its sibling lifelines.
        Q_INVOKABLE void moveLifeline(const QString &lifeline, qreal x);
        Q_INVOKABLE void select(const QString &node, bool additive);
        Q_INVOKABLE void selectEdge(const QString &key, bool additive);
        Q_INVOKABLE void clearSelection();
        Q_INVOKABLE void deleteSelection();
        Q_INVOKABLE void deleteEdge(const QString &key);
        [[nodiscard]] Q_INVOKABLE QRectF contentBounds() const;

        void syncNow();

    signals:
        void statusMessage(const QString &message);
        void contextChanged();
        void contextsChanged();
        void arrowKindChanged();
        void selectionChanged();
        void canvasSizeChanged();

    private:
        struct Lane {
            Diagram::ElementId Id = 0;   ///< the part (item, occurrence) of the lifeline
            int Depth = 0;               ///< 0 = owned by the interaction
            qreal CenterX = 0;
            std::vector<Diagram::ElementId> Events; ///< in their textual order
        };

        [[nodiscard]] std::optional<Diagram::ElementId> context() const;
        [[nodiscard]] std::optional<Diagram::ElementId> toNode(const QString &id) const;
        [[nodiscard]] std::optional<Diagram::ElementId> toElement(const QString &id) const;
        [[nodiscard]] const Lane *laneAt(const QPointF &point) const;
        [[nodiscard]] const Lane *laneOf(Diagram::ElementId element) const;
        [[nodiscard]] std::optional<Diagram::ElementId> eventAt(const QPointF &point) const;
        [[nodiscard]] std::string uniqueName(const std::string &base, Diagram::ElementId owner, bool numbered = true) const;
        [[nodiscard]] std::string uniqueMessageName() const;
        [[nodiscard]] qreal rowY(int row) const;
        /// Place among the children of owner for a new / moved event at y (index of the child it goes before).
        [[nodiscard]] std::size_t eventIndexFor(const Lane &lane, qreal y, std::optional<Diagram::ElementId> moved) const;
        void chooseContext();
        void updateContexts();
        void scheduleSync();
        void applySelection();
        void updateCanvasSize();
        bool runModelChange(const std::function<void()> &change);

        Diagram::SemanticModel &Model;
        DiagramNodeModel *Nodes;
        DiagramEdgeModel *Edges;
        Diagram::SemanticModel::ListenerToken ListenerToken = 0;
        std::optional<Diagram::ElementId> ContextElement;
        QVariantList Contexts;
        std::vector<Lane> Lanes;
        std::map<Diagram::ElementId, int> EventRows;
        std::map<Diagram::RelationshipId, int> MessageRows;
        qreal FirstRowY = 0;
        int ArrowKind = 0;
        std::set<std::uint64_t> SelectedNodes;
        std::set<QString> SelectedEdges;
        QSizeF CanvasSize{1600, 1000};
        bool SyncQueued = false;
    };
}

#endif //STRUCTURASYSTEMS_SEQUENCECONTROLLER_H
