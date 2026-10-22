//
// Created by Moritz Herzog on 21.10.26.
//

#ifndef STRUCTURASYSTEMS_LOCALPROJECTTREEMODEL_H
#define STRUCTURASYSTEMS_LOCALPROJECTTREEMODEL_H

#include <memory>
#include <vector>
#include <QAbstractItemModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    class ProjectItemModel;

    /**
     * Tree view of the flat ProjectItemModel with the local projects. The name of a local project is its path relative
     * to the opened folder (for example "sub/model.sysml"); this model splits the names at '/' into folder nodes and
     * project leaves. Folders are sorted before projects. The filter keeps matching projects and their folders.
     */
    class LocalProjectTreeModel : public QAbstractItemModel {
        Q_OBJECT
        QML_NAMED_ELEMENT(LocalProjectTreeModel)
        QML_UNCREATABLE("LocalProjectTreeModel instances are owned by AppController.")
        Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
        Q_PROPERTY(int count READ count NOTIFY countChanged)
    public:
        enum Roles {
            NameRole = Qt::UserRole + 1,
            DescriptionRole,
            IsFolderRole,
            SourceRowRole,
            PathRole
        };
        Q_ENUM(Roles)

        explicit LocalProjectTreeModel(ProjectItemModel *source, QObject *parent = nullptr);
        ~LocalProjectTreeModel() override;

        QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
        QModelIndex parent(const QModelIndex &child) const override;
        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        int columnCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        QHash<int, QByteArray> roleNames() const override;

        [[nodiscard]] QString filterText() const;
        void setFilterText(const QString &filterText);

        /** Number of visible projects (folders are not counted). */
        [[nodiscard]] int count() const;

    signals:
        void filterTextChanged();
        void countChanged();

    private:
        struct Node {
            QString Name;
            QString Path;
            QString Description;
            bool IsFolder = false;
            int SourceRow = -1;
            Node *Parent = nullptr;
            std::vector<std::unique_ptr<Node>> Children;
        };

        void rebuild();
        static void sortNode(Node &node);
        [[nodiscard]] Node *nodeAt(const QModelIndex &index) const;

        ProjectItemModel *Source;
        std::unique_ptr<Node> Root;
        QString FilterText;
        int Count = 0;
    };
}

#endif //STRUCTURASYSTEMS_LOCALPROJECTTREEMODEL_H
