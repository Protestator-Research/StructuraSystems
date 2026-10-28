//
// Nodes of the diagram as list model for QML.
//

#include "DiagramNodeModel.h"

namespace StructuraSystems::Client {

    DiagramNodeModel::DiagramNodeModel(QObject *parent) :
            QAbstractListModel(parent) {
    }

    int DiagramNodeModel::rowCount(const QModelIndex &parent) const {
        return parent.isValid() ? 0 : int(Nodes.size());
    }

    QVariant DiagramNodeModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= int(Nodes.size()))
            return {};
        const auto &node = Nodes[std::size_t(index.row())];
        switch (role) {
            case Qt::DisplayRole:
            case TitleRole:
                return node.Title;
            case ElementIdRole:
                return QString::number(node.Id);
            case NodeKindRole:
                return int(node.Kind);
            case KeywordRole:
                return node.Keyword;
            case CompartmentsRole:
                return node.Compartments;
            case NodeXRole:
                return node.Rect.x();
            case NodeYRole:
                return node.Rect.y();
            case NodeWidthRole:
                return node.Rect.width();
            case NodeHeightRole:
                return node.Rect.height();
            case DepthRole:
                return node.Depth;
            case SelectedRole:
                return node.Selected;
            case InPinsRole:
                return node.InPins;
            case OutPinsRole:
                return node.OutPins;
            case DashedRole:
                return node.Dashed;
            case ContainerRole:
                return node.Container;
            default:
                return {};
        }
    }

    QHash<int, QByteArray> DiagramNodeModel::roleNames() const {
        return {
            {ElementIdRole, "elementId"},
            {NodeKindRole, "nodeKind"},
            {KeywordRole, "keyword"},
            {TitleRole, "title"},
            {CompartmentsRole, "compartments"},
            {NodeXRole, "nodeX"},
            {NodeYRole, "nodeY"},
            {NodeWidthRole, "nodeWidth"},
            {NodeHeightRole, "nodeHeight"},
            {DepthRole, "depth"},
            {SelectedRole, "selected"},
            {InPinsRole, "inPins"},
            {OutPinsRole, "outPins"},
            {DashedRole, "dashed"},
            {ContainerRole, "container"}
        };
    }

    int DiagramNodeModel::indexOf(std::uint64_t id) const {
        for (std::size_t row = 0; row < Nodes.size(); ++row) {
            if (Nodes[row].Id == id)
                return int(row);
        }
        return -1;
    }

    const DiagramNode *DiagramNodeModel::find(std::uint64_t id) const {
        const int row = indexOf(id);
        return row < 0 ? nullptr : &Nodes[std::size_t(row)];
    }

    void DiagramNodeModel::append(const DiagramNode &node) {
        const int row = int(Nodes.size());
        beginInsertRows(QModelIndex(), row, row);
        Nodes.push_back(node);
        endInsertRows();
        emit countChanged();
        emit geometryChanged();
    }

    void DiagramNodeModel::removeAt(int row) {
        if (row < 0 || row >= int(Nodes.size()))
            return;
        beginRemoveRows(QModelIndex(), row, row);
        Nodes.erase(Nodes.begin() + row);
        endRemoveRows();
        emit countChanged();
        emit geometryChanged();
    }

    void DiagramNodeModel::updateContent(int row, const DiagramNode &node) {
        if (row < 0 || row >= int(Nodes.size()))
            return;
        auto &existing = Nodes[std::size_t(row)];
        if (existing.sameContent(node))
            return;
        existing.Kind = node.Kind;
        existing.Keyword = node.Keyword;
        existing.Title = node.Title;
        existing.Compartments = node.Compartments;
        existing.Depth = node.Depth;
        existing.InPins = node.InPins;
        existing.OutPins = node.OutPins;
        existing.Dashed = node.Dashed;
        existing.Container = node.Container;
        const auto changed = index(row);
        emit dataChanged(changed, changed, {NodeKindRole, KeywordRole, TitleRole, CompartmentsRole, DepthRole,
                                            InPinsRole, OutPinsRole, DashedRole, ContainerRole});
    }

    void DiagramNodeModel::setRect(int row, const QRectF &rect) {
        if (row < 0 || row >= int(Nodes.size()))
            return;
        auto &node = Nodes[std::size_t(row)];
        if (node.Rect == rect)
            return;
        node.Rect = rect;
        const auto changed = index(row);
        emit dataChanged(changed, changed, {NodeXRole, NodeYRole, NodeWidthRole, NodeHeightRole});
        emit geometryChanged();
    }

    int DiagramNodeModel::entryCount(const DiagramNode &node, const QString &title) {
        for (const auto &compartment: node.Compartments) {
            const auto map = compartment.toMap();
            if (map.value(QStringLiteral("title")).toString() == title)
                return int(map.value(QStringLiteral("entries")).toList().size());
        }
        return 0;
    }

    void DiagramNodeModel::setSelected(int row, bool selected) {
        if (row < 0 || row >= int(Nodes.size()))
            return;
        auto &node = Nodes[std::size_t(row)];
        if (node.Selected == selected)
            return;
        node.Selected = selected;
        const auto changed = index(row);
        emit dataChanged(changed, changed, {SelectedRole});
    }
}
