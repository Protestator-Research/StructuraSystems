//
// Created by Moritz Herzog on 15.05.24.
//

#include "ProjectTreeViewItem.h"

#include <sysmlv2/rest/entities/Project.h>
#include <algorithm>

namespace StructuraSystems::Client {
    ProjectTreeViewItem::ProjectTreeViewItem() :
    ProjectData(nullptr),
    Parent(nullptr)
    {     }

    ProjectTreeViewItem::ProjectTreeViewItem(std::shared_ptr<SysMLv2::REST::Project> project, ProjectTreeViewItem *parent) :
        ProjectData(project),
        Parent(parent)
    {    }

    ProjectTreeViewItem *ProjectTreeViewItem::child(int row) {
        return row >= 0 && row < childCount() ? ChildItems.at(row) : nullptr;
    }

    int ProjectTreeViewItem::childCount() {
        return int(ChildItems.size());
    }

    int ProjectTreeViewItem::columnCount() {
        return 1;
    }

    QVariant ProjectTreeViewItem::data() {
        if(ProjectData != nullptr)
            return QVariant(QString::fromStdString(ProjectData->getName()));
        return QVariant("Projects");
    }

    int ProjectTreeViewItem::row() {
        if (Parent == nullptr)
            return 0;

        const auto it = std::find_if(Parent->ChildItems.cbegin(), Parent->ChildItems.cend(),
                                     [this](ProjectTreeViewItem *treeItem) {
                                         return treeItem == this;
                                     });

        if (it != Parent->ChildItems.cend())
            return std::distance(Parent->ChildItems.cbegin(), it);

        Q_ASSERT(false); // should not happen
        return -1;
    }

    ProjectTreeViewItem *ProjectTreeViewItem::parentItem() {
        return Parent;
    }

    void ProjectTreeViewItem::appendProject(std::shared_ptr<SysMLv2::REST::Project> project) {
        ChildItems.push_back(new ProjectTreeViewItem(project,this));
    }

    ProjectTreeViewItem::~ProjectTreeViewItem() {
        for(auto item : ChildItems)
            delete item;

        ChildItems.clear();
    }

    std::shared_ptr<SysMLv2::REST::Project> ProjectTreeViewItem::getProject() const {
        return ProjectData;
    }

    void ProjectTreeViewItem::clearChildItems() {
        for(auto item : ChildItems)
            delete item;

        ChildItems.clear();
    }

    std::shared_ptr<SysMLv2::REST::Project> ProjectTreeViewItem::getProjectById(boost::uuids::uuid projectId) {
        for(const auto& project : ChildItems)
            if(project->ProjectData->getId()==projectId)
                return project->ProjectData;

        return std::shared_ptr<SysMLv2::REST::Project>(nullptr);
    }
}
