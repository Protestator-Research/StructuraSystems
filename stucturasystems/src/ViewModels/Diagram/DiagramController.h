//
// Editing logic of the graphical editor without any drawing: turns user actions from QML (drop from the palette,
// drawing arrows, moving, editing, deleting) into changes of the semantic model and keeps the node and edge
// models in sync with it. Successor of the former DiagramScene (Qt Widgets).
//
// Coordinates are canvas coordinates (unscaled) in QML pixels. Element ids are passed to QML as strings,
// because 64 bit integers do not survive the conversion to JavaScript numbers.
//

#ifndef STRUCTURASYSTEMS_DIAGRAMCONTROLLER_H
#define STRUCTURASYSTEMS_DIAGRAMCONTROLLER_H

#include <QObject>
#include <QPointF>
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
#include <utility>
#include <vector>

#include <Diagram/Semantic/SemanticModel.h>

#include "DiagramEdgeModel.h"
#include "DiagramNodeModel.h"
#include "DiagramTypes.h"

namespace StructuraSystems::Client {

    class DiagramController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("Owned by DiagramEditorModel.")
        Q_PROPERTY(StructuraSystems::Client::DiagramNodeModel *nodes READ nodes CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::DiagramEdgeModel *edges READ edges CONSTANT)
        Q_PROPERTY(bool arrowMode READ arrowMode WRITE setArrowMode NOTIFY arrowModeChanged)
        Q_PROPERTY(int arrowDefault READ arrowDefault WRITE setArrowDefault NOTIFY arrowDefaultChanged)
        Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
        Q_PROPERTY(qreal canvasWidth READ canvasWidth NOTIFY canvasSizeChanged)
        Q_PROPERTY(qreal canvasHeight READ canvasHeight NOTIFY canvasSizeChanged)
        Q_PROPERTY(int elementCount READ elementCount NOTIFY elementCountChanged)

    public:
        explicit DiagramController(Diagram::SemanticModel &model, QObject *parent = nullptr);
        ~DiagramController() override;

        [[nodiscard]] DiagramNodeModel *nodes() const { return Nodes; }
        [[nodiscard]] DiagramEdgeModel *edges() const { return Edges; }
        [[nodiscard]] bool arrowMode() const { return ArrowMode; }
        void setArrowMode(bool enabled);
        [[nodiscard]] int arrowDefault() const { return int(ArrowDefaultValue); }
        void setArrowDefault(int value);
        [[nodiscard]] bool hasSelection() const { return !SelectedNodes.empty() || !SelectedEdges.empty(); }
        [[nodiscard]] qreal canvasWidth() const { return CanvasSize.width(); }
        [[nodiscard]] qreal canvasHeight() const { return CanvasSize.height(); }
        [[nodiscard]] int elementCount() const { return int(Model.elementCount()); }

        // ---- adding --------------------------------------------------------------------------------
        /// Adds the element of a palette tool (DiagramTypes::Tool) at a canvas position.
        /// Attributes and actions must be dropped onto a part. @return the new id, empty if nothing was added.
        Q_INVOKABLE QString addElement(int tool, qreal x, qreal y);
        /// Like addElement, but attributes/actions go to the single selected part (palette click).
        Q_INVOKABLE QString addElementAtSelection(int tool, qreal x, qreal y);

        // ---- lookup --------------------------------------------------------------------------------
        /// Topmost node at a canvas position. Frames are hit only at their border and title band,
        /// with includeFrameArea the innermost frame containing the position is the fallback.
        Q_INVOKABLE QString nodeAt(qreal x, qreal y, bool includeFrameArea = false) const;
        /// Key of the edge passing close to a canvas position, empty if none.
        Q_INVOKABLE QString edgeAt(qreal x, qreal y) const;
        Q_INVOKABLE QRectF nodeRect(const QString &id) const;

        // ---- geometry ------------------------------------------------------------------------------
        /// Moves a node (a package frame takes its content along). Ownership is updated by finishMove.
        Q_INVOKABLE void moveNode(const QString &id, qreal x, qreal y);
        /// End of a drag: a node dropped into / out of a package frame changes its owner.
        Q_INVOKABLE void finishMove(const QString &id);
        /// Size of a part node as laid out by QML.
        Q_INVOKABLE void setNodeSize(const QString &id, qreal width, qreal height);
        /// Size of a package frame (resize handle).
        Q_INVOKABLE void resizeFrame(const QString &id, qreal width, qreal height);

        // ---- selection and deleting ----------------------------------------------------------------
        Q_INVOKABLE void select(const QString &id, bool additive);
        Q_INVOKABLE void selectEdge(const QString &key, bool additive);
        Q_INVOKABLE void clearSelection();
        Q_INVOKABLE void deleteSelection();
        /// Deletes a node or an attribute/action entry including everything it owns.
        Q_INVOKABLE void deleteElement(const QString &id);
        Q_INVOKABLE void deleteEdge(const QString &key);

        // ---- edges ---------------------------------------------------------------------------------
        /// DiagramTypes::EdgeKind values allowed from source to target (composition = source owns target).
        Q_INVOKABLE QVariantList allowedEdgeKinds(const QString &source, const QString &target) const;
        Q_INVOKABLE bool createEdge(int kind, const QString &source, const QString &target);
        /**
         * End of an arrow drawn from source to a canvas position. With a preset the edge is created directly.
         * @return {target, kinds} when the user has to choose the kind (menu), otherwise an empty map.
         */
        Q_INVOKABLE QVariantMap arrowReleased(const QString &source, qreal x, qreal y);
        Q_INVOKABLE QString edgeKindName(int kind) const;

        // ---- editing -------------------------------------------------------------------------------
        /// Values for the edit dialog: kind, kindLabel, name, typeName, multiplicity, typed, hasMultiplicity,
        /// isAction, referenceAction.
        Q_INVOKABLE QVariantMap elementDetails(const QString &id) const;
        /// Applies the dialog values. @return an error message, empty on success.
        Q_INVOKABLE QString updateElement(const QString &id, const QVariantMap &values);
        /// e.g. "part definition", for menu texts.
        Q_INVOKABLE QString kindLabel(const QString &id) const;

        /// Applies pending model changes to the node and edge models immediately.
        void syncNow();
        /// Attribute / action / constraint / calc (palette tool) added to owner, as from the palette.
        std::optional<Diagram::ElementId> addFeatureTo(int tool, Diagram::ElementId owner);

        /**
         * Arranges all nodes (e.g. after a text import): packages become frames around their content, parts owned
         * by a part are placed below it, everything else in rows. While nobody moved anything by hand, the layout
         * is repeated once QML reports the real node sizes.
         */
        Q_INVOKABLE void layoutAll();
        /// Bounding rectangle of all nodes (empty if there are none).
        [[nodiscard]] Q_INVOKABLE QRectF contentBounds() const;

        /// "6", "0..1", "1..*", "*", "[2]" or empty. first = valid input, second = parsed value.
        static std::pair<bool, std::optional<Diagram::Multiplicity>> parseMultiplicity(const QString &input);

    signals:
        void statusMessage(const QString &message);
        void arrowModeChanged();
        void arrowDefaultChanged();
        void selectionChanged();
        void canvasSizeChanged();
        void elementCountChanged();

    private:
        [[nodiscard]] std::optional<Diagram::ElementId> toId(const QString &id) const;
        [[nodiscard]] QString nodeIdAt(const QPointF &point, std::optional<Diagram::ElementId> ignore,
                                       bool includeFrameArea) const;
        [[nodiscard]] std::optional<Diagram::ElementId> frameAt(const QPointF &point,
                                                                std::optional<Diagram::ElementId> ignore) const;
        [[nodiscard]] std::string uniqueName(const std::string &base, Diagram::ElementId owner) const;
        [[nodiscard]] QPointF defaultPositionFor(Diagram::ElementId id) const;
        [[nodiscard]] bool isFrame(Diagram::ElementId id) const;
        void fillNode(DiagramNode &node, const Diagram::SysMLElement &element) const;
        /// Entries of the owned features: parameters (in/out/inout), result (return), attributes; the compartments
        /// actions (if withActions), constraints, calcs and other are appended to compartments.
        void fillFeatureEntries(const Diagram::SysMLElement &element, QVariantList &parameters, QVariantList &result,
                                QVariantList &attributes, QVariantList &compartments, bool withActions) const;

        std::optional<Diagram::ElementId> addFeature(int tool, Diagram::ElementId owner);
        QString addStatement(const QPointF &point);
        /// Parameter and expression of a new constraint / calculation (inside a model change).
        void fillNewEvaluation(Diagram::ElementId id);
        void moveFrameContent(Diagram::ElementId frame, const QPointF &delta);
        void updateContainment(Diagram::ElementId id);
        bool runModelChange(const std::function<void()> &change);
        void scheduleSync();
        void applySelection();
        void updateCanvasSize();
        [[nodiscard]] QSizeF estimatedSize(const DiagramNode &node) const;
        QSizeF measure(Diagram::ElementId id, std::map<Diagram::ElementId, QSizeF> &extents,
                       std::map<Diagram::ElementId, std::vector<QPointF>> &framePositions);
        void place(Diagram::ElementId id, const QPointF &topLeft, const std::map<Diagram::ElementId, QSizeF> &extents,
                   const std::map<Diagram::ElementId, std::vector<QPointF>> &framePositions);
        [[nodiscard]] std::vector<Diagram::ElementId> nodeChildren(Diagram::ElementId owner) const;

        Diagram::SemanticModel &Model;
        Diagram::SemanticModel::ListenerToken ListenerToken = 0;
        DiagramNodeModel *Nodes;
        DiagramEdgeModel *Edges;

        std::map<Diagram::ElementId, QPointF> PendingPositions;
        std::set<Diagram::ElementId> SelectedNodes;
        std::set<QString> SelectedEdges;
        bool SyncQueued = false;
        /// true from layoutAll until the user moves or adds something: real sizes trigger a new layout.
        bool AutoLayout = false;
        bool LayoutQueued = false;
        std::set<Diagram::ElementId> Measured;
        int LastElementCount = 0;

        bool ArrowMode = false;
        DiagramTypes::ArrowDefault ArrowDefaultValue = DiagramTypes::AskOnRelease;
        QSizeF CanvasSize{2400, 1600};
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAMCONTROLLER_H
