//
// Created by Moritz Herzog on 21.10.26.
//

#ifndef STRUCTURASYSTEMS_ELEMENTTREEMODEL_H
#define STRUCTURASYSTEMS_ELEMENTTREEMODEL_H

#include <QAbstractItemModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    /**
     * Plain data describing one element of the parsed model. Safe to create in worker threads.
     */
    struct ElementTreeNode {
        QString Type;
        QString Name;       ///< Empty if the element has no name
        int Parent = -1;    ///< Index of the parent node in the flat pre-order list, -1 for roots
    };

    /**
     * Tree model of the parsed elements. It is built from a flat pre-order list of nodes with parent indices.
     */
    class ElementTreeModel : public QAbstractItemModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("ElementTreeModel instances are owned by AppController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)
    public:
        enum Roles {
            ElementTypeRole = Qt::UserRole + 1,
            NameRole
        };
        Q_ENUM(Roles)

        explicit ElementTreeModel(QObject *parent = nullptr);
        ~ElementTreeModel() override = default;

        QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
        QModelIndex parent(const QModelIndex &child) const override;
        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        int columnCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        QHash<int, QByteArray> roleNames() const override;

        [[nodiscard]] int count() const;

        void setNodes(const QList<ElementTreeNode> &nodes);
        void clear();

    signals:
        void countChanged();

    private:
        QList<ElementTreeNode> Nodes;
        QList<QList<int>> Children;     ///< Child node indices per node
        QList<int> Roots;               ///< Indices of the nodes without parent
        QList<int> Rows;                ///< Row of each node within its parent
    };
}

#endif //STRUCTURASYSTEMS_ELEMENTTREEMODEL_H
