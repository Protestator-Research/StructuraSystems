//
// Created by Moritz Herzog on 21.10.26.
//

#include "ElementTreeModel.h"

namespace StructuraSystems::Client {
    ElementTreeModel::ElementTreeModel(QObject *parent) : QAbstractItemModel(parent) {
    }

    QModelIndex ElementTreeModel::index(int row, int column, const QModelIndex &parent) const {
        if (row < 0 || column != 0)
            return {};

        if (!parent.isValid()) {
            if (row >= Roots.size())
                return {};
            return createIndex(row, column, quintptr(Roots.at(row)));
        }

        if (parent.internalId() >= quintptr(Nodes.size()))
            return {};
        const QList<int> &siblings = Children.at(int(parent.internalId()));
        if (row >= siblings.size())
            return {};
        return createIndex(row, column, quintptr(siblings.at(row)));
    }

    QModelIndex ElementTreeModel::parent(const QModelIndex &child) const {
        if (!child.isValid() || child.internalId() >= quintptr(Nodes.size()))
            return {};

        const int parentNode = Nodes.at(int(child.internalId())).Parent;
        if (parentNode < 0)
            return {};
        return createIndex(Rows.at(parentNode), 0, quintptr(parentNode));
    }

    int ElementTreeModel::rowCount(const QModelIndex &parent) const {
        if (!parent.isValid())
            return int(Roots.size());
        if (parent.column() != 0 || parent.internalId() >= quintptr(Nodes.size()))
            return 0;
        return int(Children.at(int(parent.internalId())).size());
    }

    int ElementTreeModel::columnCount(const QModelIndex &) const {
        return 1;
    }

    QVariant ElementTreeModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || index.internalId() >= quintptr(Nodes.size()))
            return {};

        const auto &node = Nodes.at(int(index.internalId()));
        switch (role) {
            case Qt::DisplayRole:
            case NameRole:
                return node.Name;
            case ElementTypeRole:
                return node.Type;
            default:
                return {};
        }
    }

    QHash<int, QByteArray> ElementTreeModel::roleNames() const {
        return {
            {ElementTypeRole, "elementType"},
            {NameRole, "name"}
        };
    }

    int ElementTreeModel::count() const {
        return int(Nodes.size());
    }

    void ElementTreeModel::setNodes(const QList<ElementTreeNode> &nodes) {
        const bool countDiffers = nodes.size() != Nodes.size();
        beginResetModel();
        Nodes = nodes;
        Children = QList<QList<int>>(Nodes.size());
        Rows = QList<int>(Nodes.size(), 0);
        Roots.clear();
        for (int i = 0; i < Nodes.size(); ++i) {
            const int parent = Nodes.at(i).Parent;
            // Parents always precede their children in the pre-order list.
            QList<int> &siblings = (parent >= 0 && parent < i) ? Children[parent] : Roots;
            Rows[i] = int(siblings.size());
            siblings.append(i);
        }
        endResetModel();
        if (countDiffers)
            emit countChanged();
    }

    void ElementTreeModel::clear() {
        setNodes({});
    }
}
