//
// Use case view of a package (or the top level): use cases as ellipses, actors as stick figures, the subject as
// system boundary around its use cases, «include» arrows. Like the other views it holds nothing of its own: every
// node and line is read from the semantic model and every edit is written back.
//
//   use case (ellipse)                     use case def Drive;  /  use case drive : Drive;
//   actor - use case (line)                actor driver : Driver;            (in the use case)
//   system boundary                        subject vehicle : Vehicle;        (in each use case inside it)
//   «include» to a use case definition     include use case refuel : Refuel; (in the including use case)
//   «include» to a use case (usage)        include refuel;
//
// SysML declares actors and the subject in every use case. The view shows one stick figure per actor name and type
// (and one boundary per subject name and type); a line from an actor to another use case declares the actor there
// too, renaming the figure renames every declaration.
//
// Coordinates are canvas coordinates; ids are element ids, passed to QML as strings. The id of an actor figure /
// boundary is the id of its first declaration.
//

#ifndef STRUCTURASYSTEMS_USECASECONTROLLER_H
#define STRUCTURASYSTEMS_USECASECONTROLLER_H

#include <QJsonObject>
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
#include <vector>

#include <Diagram/Semantic/SemanticModel.h>

#include "DiagramEdgeModel.h"
#include "DiagramNodeModel.h"
#include "DiagramTypes.h"

namespace StructuraSystems::Client {

    class UseCaseController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("Owned by DiagramEditorModel.")
        Q_PROPERTY(StructuraSystems::Client::DiagramNodeModel *nodes READ nodes CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::DiagramEdgeModel *edges READ edges CONSTANT)
        /// Element id of the package (or part) whose use cases are shown, "0" for the top level.
        Q_PROPERTY(QString contextId READ contextId WRITE setContextId NOTIFY contextChanged)
        /// Places that can be shown: [{id, title}] (the top level, packages, parts with use cases)
        Q_PROPERTY(QVariantList contexts READ contexts NOTIFY contextsChanged)
        Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
        Q_PROPERTY(qreal canvasWidth READ canvasWidth NOTIFY canvasSizeChanged)
        Q_PROPERTY(qreal canvasHeight READ canvasHeight NOTIFY canvasSizeChanged)

    public:
        explicit UseCaseController(Diagram::SemanticModel &model, QObject *parent = nullptr);
        ~UseCaseController() override;

        [[nodiscard]] DiagramNodeModel *nodes() const { return Nodes; }
        [[nodiscard]] DiagramEdgeModel *edges() const { return Edges; }
        [[nodiscard]] QString contextId() const;
        void setContextId(const QString &id);
        [[nodiscard]] QVariantList contexts() const { return Contexts; }
        [[nodiscard]] bool hasSelection() const { return !SelectedNodes.empty() || !SelectedEdges.empty(); }
        [[nodiscard]] qreal canvasWidth() const { return CanvasSize.width(); }
        [[nodiscard]] qreal canvasHeight() const { return CanvasSize.height(); }

        /// A palette entry (ToolUseCaseDefinition, ToolUseCase, ToolActor, ToolSubject) dropped at (x, y).
        /// A use case dropped into a boundary gets its subject; actors and subjects are dropped onto a use case.
        Q_INVOKABLE QString addNode(int tool, qreal x, qreal y);
        /// Line from one node to another: actor <-> use case, use case -> use case («include»), use case -> boundary
        /// (into that system). Returns an error text (empty on success).
        Q_INVOKABLE QString connectNodes(const QString &source, const QString &target);
        Q_INVOKABLE QString nodeAt(qreal x, qreal y) const;
        Q_INVOKABLE QString edgeAt(qreal x, qreal y) const;
        Q_INVOKABLE QRectF nodeRect(const QString &node) const;
        /// Moves a node; a boundary takes its use cases along.
        Q_INVOKABLE void moveNode(const QString &node, qreal x, qreal y);
        Q_INVOKABLE void select(const QString &node, bool additive);
        Q_INVOKABLE void selectEdge(const QString &key, bool additive);
        Q_INVOKABLE void clearSelection();
        /// Use cases are deleted, an actor figure / boundary deletes all its declarations.
        Q_INVOKABLE void deleteSelection();
        Q_INVOKABLE void deleteEdge(const QString &key);
        /// Element edited by the element dialog for a node or line, empty for an actor / boundary (group dialog).
        Q_INVOKABLE QString elementFor(const QString &nodeOrEdge) const;
        /// Actor figure / boundary: {kindLabel, name, typeName, multiplicity, count}; empty for other nodes.
        Q_INVOKABLE QVariantMap groupDetails(const QString &node) const;
        /// Renames / retypes every declaration of an actor figure or boundary. Returns an error text.
        Q_INVOKABLE QString updateGroup(const QString &node, const QVariantMap &values);
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

    private:
        /// Actors (or subjects) with the same name and type, one figure / boundary.
        struct Group {
            Diagram::ElementId Node = 0;                 ///< first declaration
            std::vector<Diagram::ElementId> Declarations;
            std::vector<Diagram::ElementId> UseCases;    ///< node of the use case of each declaration
            std::vector<Diagram::ElementId> Members;     ///< boundary: every use case drawn inside it
        };

        [[nodiscard]] Diagram::ElementId context() const;
        [[nodiscard]] std::optional<Diagram::ElementId> toNode(const QString &id) const;
        [[nodiscard]] const Group *groupOf(Diagram::ElementId node) const;
        [[nodiscard]] bool isUseCaseNode(Diagram::ElementId id) const;
        [[nodiscard]] std::optional<Diagram::ElementId> boundaryAt(const QPointF &point) const;
        [[nodiscard]] std::optional<Diagram::ElementId> subjectOf(Diagram::ElementId useCase) const;
        [[nodiscard]] std::string uniqueName(const std::string &base, Diagram::ElementId owner) const;
        [[nodiscard]] std::string referenceTo(Diagram::ElementId from, Diagram::ElementId target) const;
        /// Declares a copy of an actor / subject (name, type, multiplicity) in a use case.
        void declareCopy(Diagram::ElementId original, Diagram::ElementId useCase);
        [[nodiscard]] static QSizeF sizeOf(const DiagramNode &node);
        [[nodiscard]] std::string freeName(const std::string &base, Diagram::ElementId owner) const;
        /// Use case definition shown in the view that an "include use case x : T" refers to by its type.
        [[nodiscard]] std::optional<Diagram::ElementId> includedDefinition(Diagram::ElementId include) const;
        void chooseContext();
        void updateContexts();
        void scheduleSync();
        void applySelection();
        void updateBoundaries();
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
        std::vector<Group> Actors;
        std::vector<Group> Subjects;
        std::set<Diagram::ElementId> SelectedNodes;
        std::set<QString> SelectedEdges;
        /// Included use cases drawn as ellipses of their own -> the use case including them (for the layout)
        std::map<Diagram::ElementId, Diagram::ElementId> IncludedBy;
        std::vector<Diagram::ElementId> TopUseCases;
        QSizeF CanvasSize{1600, 1000};
        bool SyncQueued = false;
        bool AutoLayout = true;
    };
}

#endif //STRUCTURASYSTEMS_USECASECONTROLLER_H
