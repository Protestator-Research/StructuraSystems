//
// Edges of the diagram as list model. The edges are drawn by EdgeLayerItem; the model is exposed to QML
// mainly for counts and tests. Filled by the DiagramController.
//

#ifndef STRUCTURASYSTEMS_DIAGRAMEDGEMODEL_H
#define STRUCTURASYSTEMS_DIAGRAMEDGEMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <vector>

#include <QPointF>
#include <QRectF>

#include "DiagramTypes.h"

namespace StructuraSystems::Client {

    struct DiagramEdge {
        /// "c:<child id>" for a composition (ownership), "r:<relationship id>" for a relationship.
        QString Key;
        DiagramTypes::EdgeKind Kind = DiagramTypes::Connection;
        std::uint64_t Source = 0; ///< composition: the owner
        std::uint64_t Target = 0; ///< composition: the owned part
        bool Selected = false;
        /// Parametric view: the line starts at an output pin of the source (index), or at its right border (-1),
        /// and ends at an input pin of the target (index) or its left border (-1).
        int SourcePin = -1;
        int TargetPin = -1;
        /// Written next to the line: name and type of a connection / interface, the item of a flow.
        QString Label;
        /// Written at the ends of the line: multiplicity / role name of the source and target end ("[0..1]").
        QString SourceLabel;
        QString TargetLabel;
        /// n-ary connection / dependency: one line from an end (Source = Target = the end node) to the dot where all
        /// lines of the group (the key of the relationship) meet. 1: an end of a connection, 2: a client of a
        /// dependency (dashed), 3: a supplier of a dependency (dashed, open head at the supplier).
        QString Group;
        int Junction = 0;
        /// Flow: "succession flow" - a dashed line (graphical BNF: sflow).
        bool Dashed = false;
        /// Flow between parameters (pins): a filled arrow head at the target instead of the triangle on the line.
        bool FilledHead = false;
        /// A pin / port end: the direction the line leaves it in (set by the view; null = found from the node it is on).
        QPointF SourceOut;
        QPointF TargetOut;
        /// Annotation of an element shown as entry of a compartment ("comment about part1::attribute1"): the entry
        /// (its entryId) and where it is in the target node (relative to its top left; empty until the node reported it).
        QString TargetEntry;
        QRectF TargetEntryRect;

        bool operator==(const DiagramEdge &other) const = default;
    };

    class DiagramEdgeModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("Owned by DiagramController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)

    public:
        enum Roles {
            EdgeKeyRole = Qt::UserRole + 1,
            EdgeKindRole,
            SourceIdRole,
            TargetIdRole,
            SelectedRole,
            LabelRole
        };
        Q_ENUM(Roles)

        explicit DiagramEdgeModel(QObject *parent = nullptr);

        [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
        [[nodiscard]] int count() const { return int(Edges.size()); }

        // ---- C++ API ----
        [[nodiscard]] const std::vector<DiagramEdge> &edges() const { return Edges; }
        [[nodiscard]] const DiagramEdge *find(const QString &key) const;
        /// Replaces all edges (only if something changed).
        void setEdges(std::vector<DiagramEdge> edges);

    signals:
        void countChanged();

    private:
        std::vector<DiagramEdge> Edges;
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAMEDGEMODEL_H
