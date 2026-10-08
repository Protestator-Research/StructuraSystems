//
// Edges of the diagram as list model.
//

#include "DiagramEdgeModel.h"

#include <utility>

namespace StructuraSystems::Client {

    DiagramEdgeModel::DiagramEdgeModel(QObject *parent) :
            QAbstractListModel(parent) {
    }

    int DiagramEdgeModel::rowCount(const QModelIndex &parent) const {
        return parent.isValid() ? 0 : int(Edges.size());
    }

    QVariant DiagramEdgeModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= int(Edges.size()))
            return {};
        const auto &edge = Edges[std::size_t(index.row())];
        switch (role) {
            case Qt::DisplayRole:
            case EdgeKeyRole:
                return edge.Key;
            case EdgeKindRole:
                return int(edge.Kind);
            case SourceIdRole:
                return QString::number(edge.Source);
            case TargetIdRole:
                return QString::number(edge.Target);
            case SelectedRole:
                return edge.Selected;
            default:
                return {};
        }
    }

    QHash<int, QByteArray> DiagramEdgeModel::roleNames() const {
        return {
            {EdgeKeyRole, "edgeKey"},
            {EdgeKindRole, "edgeKind"},
            {SourceIdRole, "sourceId"},
            {TargetIdRole, "targetId"},
            {SelectedRole, "selected"}
        };
    }

    const DiagramEdge *DiagramEdgeModel::find(const QString &key) const {
        for (const auto &edge: Edges) {
            if (edge.Key == key)
                return &edge;
        }
        return nullptr;
    }

    void DiagramEdgeModel::setEdges(std::vector<DiagramEdge> edges) {
        if (edges == Edges)
            return;
        const bool countChanges = edges.size() != Edges.size();
        beginResetModel();
        Edges = std::move(edges);
        endResetModel();
        if (countChanges)
            emit countChanged();
    }
}
