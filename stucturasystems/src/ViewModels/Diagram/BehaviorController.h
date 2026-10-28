//
// Behavior view of one action or state (definition or usage): its actions, control nodes (decide, merge, fork,
// join, start, done) and states as nodes, successions and transitions as arrows. Like the other views it holds
// nothing of its own: every node and arrow is read from the semantic model and every edit is written back.
//
//   arrow between two nodes of an action      then b;            (right after a, written by the generator)
//   arrow between two states                  transition first a then b;
//   guard / trigger / effect of an arrow      if g then b;   transition first a accept T if g do E then b;
//   Start / Done from the palette             first start; then ...;    then done;
//   Initial (in a state machine)              entry; then off;
//
// Coordinates are canvas coordinates; ids are element ids, passed to QML as strings.
//

#ifndef STRUCTURASYSTEMS_BEHAVIORCONTROLLER_H
#define STRUCTURASYSTEMS_BEHAVIORCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
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

    class BehaviorController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("Owned by DiagramEditorModel.")
        Q_PROPERTY(StructuraSystems::Client::DiagramNodeModel *nodes READ nodes CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::DiagramEdgeModel *edges READ edges CONSTANT)
        /// Element id of the action / state shown.
        Q_PROPERTY(QString contextId READ contextId WRITE setContextId NOTIFY contextChanged)
        /// Actions and states that can be shown: [{id, title}]
        Q_PROPERTY(QVariantList contexts READ contexts NOTIFY contextsChanged)
        /// The context is a state (machine): states, initial state, transitions.
        Q_PROPERTY(bool stateMachine READ stateMachine NOTIFY contextChanged)
        Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
        Q_PROPERTY(qreal canvasWidth READ canvasWidth NOTIFY canvasSizeChanged)
        Q_PROPERTY(qreal canvasHeight READ canvasHeight NOTIFY canvasSizeChanged)
        /// The frame around the nodes with the name of the context: "action transportPassenger".
        Q_PROPERTY(QRectF frameRect READ frameRect NOTIFY frameChanged)
        Q_PROPERTY(QString frameTitle READ frameTitle NOTIFY frameChanged)

    public:
        explicit BehaviorController(Diagram::SemanticModel &model, QObject *parent = nullptr);
        ~BehaviorController() override;

        [[nodiscard]] DiagramNodeModel *nodes() const { return Nodes; }
        [[nodiscard]] DiagramEdgeModel *edges() const { return Edges; }
        [[nodiscard]] QString contextId() const;
        void setContextId(const QString &id);
        [[nodiscard]] QVariantList contexts() const { return Contexts; }
        [[nodiscard]] bool stateMachine() const;
        [[nodiscard]] bool hasSelection() const { return !SelectedNodes.empty() || !SelectedEdges.empty(); }
        [[nodiscard]] qreal canvasWidth() const { return CanvasSize.width(); }
        [[nodiscard]] qreal canvasHeight() const { return CanvasSize.height(); }
        [[nodiscard]] QRectF frameRect() const { return FrameRect; }
        [[nodiscard]] QString frameTitle() const { return FrameTitle; }

        /// A palette entry (DiagramTypes::ToolActionUsage, ToolDecision, ..., ToolState, ToolInitial) dropped at (x, y).
        /// An action dropped onto a state becomes its "do action".
        Q_INVOKABLE QString addNode(int tool, qreal x, qreal y);
        /// Arrow from one node to another: succession or transition. Returns an error text (empty on success).
        Q_INVOKABLE QString connectNodes(const QString &source, const QString &target);
        Q_INVOKABLE QString nodeAt(qreal x, qreal y) const;
        Q_INVOKABLE QString edgeAt(qreal x, qreal y) const;
        Q_INVOKABLE QRectF nodeRect(const QString &node) const;
        Q_INVOKABLE void moveNode(const QString &node, qreal x, qreal y);
        Q_INVOKABLE void select(const QString &node, bool additive);
        Q_INVOKABLE void selectEdge(const QString &key, bool additive);
        Q_INVOKABLE void clearSelection();
        Q_INVOKABLE void deleteSelection();
        Q_INVOKABLE void deleteEdge(const QString &key);
        /// Entry / do / exit action of a state shown at a point of the state node (for editing), empty if none.
        Q_INVOKABLE QString entryAt(const QString &node, qreal x, qreal y) const;
        Q_INVOKABLE void layoutAll();
        /// Positions of the nodes of every context shown so far, by context and element key (see DiagramLayout);
        /// contexts with the automatic layout are left out.
        [[nodiscard]] QJsonObject layoutState() const;
        /// Positions stored by layoutState() (after loading): used whenever their context is shown.
        void restoreLayout(const QJsonObject &layout);
        [[nodiscard]] Q_INVOKABLE QRectF contentBounds() const;

        void syncNow();

    signals:
        void statusMessage(const QString &message);
        /// A node was moved by the user (only the layout changed, not the model).
        void layoutEdited();
        void contextChanged();
        void contextsChanged();
        void selectionChanged();
        void canvasSizeChanged();
        void frameChanged();

    private:
        [[nodiscard]] std::optional<Diagram::ElementId> context() const;
        [[nodiscard]] std::optional<Diagram::ElementId> toNode(const QString &id) const;
        [[nodiscard]] bool isNode(Diagram::ElementId id) const;
        [[nodiscard]] QSizeF sizeOf(const DiagramNode &node) const;
        /// The node a nested state is drawn in (none for the nodes of the context itself).
        [[nodiscard]] std::optional<Diagram::ElementId> parentNode(Diagram::ElementId id) const;
        /// Positions of the nodes owned by 'owner' relative to the top left of their area; returns its size.
        QSizeF layoutLevel(Diagram::ElementId owner, const std::vector<DiagramNode> &nodes, const std::vector<DiagramEdge> &edges,
                           std::map<Diagram::ElementId, QPointF> &relative);
        /// Rectangles from the positions: a state with nested states grows around them; the frame around all.
        void applyRects();
        [[nodiscard]] std::string uniqueName(const std::string &base, Diagram::ElementId owner) const;
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
        std::map<Diagram::ElementId, QPointF> Positions;
        /// Stored positions of the contexts by context key (see layoutState).
        QJsonObject SavedLayouts;
        [[nodiscard]] QString contextKey() const;
        [[nodiscard]] QJsonObject currentLayout() const;
        /// The positions of the context shown are kept in SavedLayouts / taken from there.
        void saveContextLayout();
        void applyContextLayout();
        std::set<Diagram::ElementId> SelectedNodes;
        std::set<QString> SelectedEdges;
        QSizeF CanvasSize{1600, 1000};
        QRectF FrameRect;
        QString FrameTitle;
        bool SyncQueued = false;
        bool AutoLayout = true;
    };
}

#endif //STRUCTURASYSTEMS_BEHAVIORCONTROLLER_H
