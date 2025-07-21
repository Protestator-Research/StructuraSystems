//
// Created by Moritz Herzog on 10.04.25.
//

#ifndef STRUCTURASYSTEMS_PROJECTITEMMODEL_H
#define STRUCTURASYSTEMS_PROJECTITEMMODEL_H

#include <QAbstractItemModel>
#include <sysmlv2service/interfaces/IProjectService.h>

namespace StructuraSystems::Client {
    class ProjectTreeViewItem;

    class ProjectItemModel : public QAbstractItemModel, public SysMLv2::API::IProjectService{
    Q_OBJECT
    public:
        explicit ProjectItemModel(QObject* parent = nullptr);
        ~ProjectItemModel() override;

        QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;

        QModelIndex parent(const QModelIndex &child) const override;

        int rowCount(const QModelIndex &parent) const override;

        int columnCount(const QModelIndex &parent) const override;

        QVariant data(const QModelIndex &index, int role) const override;

        std::vector<std::shared_ptr<SysMLv2::REST::Project>> getProjects() override;

        std::shared_ptr<SysMLv2::REST::Project> getProjectById(boost::uuids::uuid projectId) override;

        std::shared_ptr<SysMLv2::REST::Project> createProject(std::string projectName, std::string description) override;

        void appendProject(std::shared_ptr<SysMLv2::REST::Project> project);

        std::shared_ptr<SysMLv2::REST::Project> updateProject(boost::uuids::uuid projectId, std::string projectName, std::string description, std::shared_ptr<SysMLv2::REST::Branch> branch) override;

        std::shared_ptr<SysMLv2::REST::Project> deleteProject(boost::uuids::uuid projectId) override;

        QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

        void clear();
    private:
        ProjectTreeViewItem* RootItem;
    };
}

#endif //STRUCTURASYSTEMS_PROJECTITEMMODEL_H
