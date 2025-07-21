//
// Created by Moritz Herzog on 10.04.25.
//

#include "ProjectItemModel.h"
#include "ProjectTreeViewItem.h"
#include <sysmlv2/rest/entities/Project.h>

namespace StructuraSystems::Client {
    ProjectItemModel::ProjectItemModel(QObject *parent) : QAbstractItemModel(parent) {
        RootItem = new ProjectTreeViewItem();
    }

    ProjectItemModel::~ProjectItemModel() {

    }

    QModelIndex ProjectItemModel::parent(const QModelIndex &child) const {
        if (!child.isValid())
            return {};

        auto *childItem = static_cast<ProjectTreeViewItem*>(child.internalPointer());
        ProjectTreeViewItem *parentItem = childItem->parentItem();

        return parentItem != RootItem
               ? createIndex(parentItem->row(), 0, parentItem) : QModelIndex{};
}

    int ProjectItemModel::rowCount(const QModelIndex &) const {
        return RootItem->childCount();
    }

    int ProjectItemModel::columnCount(const QModelIndex &) const {
        return 1;
    }

    QVariant ProjectItemModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};

        ProjectTreeViewItem *item = const_cast<ProjectTreeViewItem *>(static_cast<const ProjectTreeViewItem *>(index.internalPointer()));
        return item->data();
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Project>> ProjectItemModel::getProjects() {
        std::vector<std::shared_ptr<SysMLv2::REST::Project>> returnValue;
        for(int i = 0; i<RootItem->childCount(); i++)
            returnValue.push_back(RootItem->child(i)->getProject());
        return returnValue;
    }

    std::shared_ptr<SysMLv2::REST::Project> ProjectItemModel::getProjectById(boost::uuids::uuid projectId) {
        return RootItem->getProjectById(projectId);
    }

    std::shared_ptr<SysMLv2::REST::Project>
    ProjectItemModel::createProject(std::string projectName, std::string description) {
        beginInsertRows(QModelIndex(), 0, RootItem->childCount());
        auto project = std::make_shared<SysMLv2::REST::Project>(projectName, description, "main");
        RootItem->appendProject(project);
        endInsertRows();
        emit dataChanged(index(0,0), index(RootItem->childCount()-1,1), {Qt::DisplayRole});
        return project;
    }

    std::shared_ptr<SysMLv2::REST::Project>
    ProjectItemModel::updateProject(boost::uuids::uuid projectId, std::string projectName, std::string description,
                                    std::shared_ptr<SysMLv2::REST::Branch> branch) {
        auto project = RootItem->getProjectById(projectId);
        if(!projectName.empty())
            project->setName(projectName);
        if(!description.empty())
            project->setDescription(description);
        if(branch)
            project->setDefaultBranch(branch);
        emit dataChanged(index(0,0), index(RootItem->childCount()-1,1), {Qt::DisplayRole});
        return project;
    }

    std::shared_ptr<SysMLv2::REST::Project> ProjectItemModel::deleteProject(boost::uuids::uuid projectId) {
//        beginRemoveColumns(index(getIndexFromProjectId(projectId),0), getIndexFromProjectId(projectId), getIndexFromProjectId(projectId));
        auto project =  RootItem->getProjectById(projectId);
//        endRemoveColumns();
//        emit dataChanged(index(0,0), index(ProjectMap.size(),1), {Qt::DisplayRole});
        return project;
    }

    QModelIndex ProjectItemModel::index(int row, int column, const QModelIndex &parent) const {
        if (!hasIndex(row, column, parent))
            return {};

        ProjectTreeViewItem *parentItem = parent.isValid()
                                          ? static_cast<ProjectTreeViewItem*>(parent.internalPointer())
                                          : RootItem;

        if (auto *childItem = parentItem->child(row))
            return createIndex(row, column, childItem);

        return {};
    }

    QVariant ProjectItemModel::headerData(int , Qt::Orientation orientation, int role) const {
        return orientation == Qt::Horizontal && role == Qt::DisplayRole
               ? RootItem->data() : QVariant{};
    }

    void ProjectItemModel::clear() {
        const int lastElement  = RootItem->childCount()-1;

        if (lastElement < 0)
            return;

        beginRemoveRows(QModelIndex(), 0, lastElement);
        RootItem->clearChildItems();
        endRemoveRows();
        removeRows(0, lastElement, index(0,0));
    }

    void ProjectItemModel::appendProject(std::shared_ptr<SysMLv2::REST::Project> project) {
        beginInsertRows(QModelIndex(), 0, RootItem->childCount());
        RootItem->appendProject(project);
        endInsertRows();
        emit dataChanged(index(0,0), index(RootItem->childCount()-1,1), {Qt::DisplayRole});
    }
}