//
// Created by Moritz Herzog on 15.05.24.
//

#pragma once

#include <QVariant>
#include <boost/uuid/uuid.hpp>

namespace SysMLv2::REST{
    class Project;
}

namespace StructuraSystems::Client {
    class ProjectTreeViewItem {
    public:
        ProjectTreeViewItem();
        explicit ProjectTreeViewItem(std::shared_ptr<SysMLv2::REST::Project> project, ProjectTreeViewItem* parent = nullptr);

        ~ProjectTreeViewItem();

        ProjectTreeViewItem* child(int row);
        int childCount();
        int columnCount();
        QVariant data();
        int row();
        ProjectTreeViewItem* parentItem();

        void appendProject(std::shared_ptr<SysMLv2::REST::Project> project);
        [[nodiscard]] std::shared_ptr<SysMLv2::REST::Project> getProject() const;

        void clearChildItems();

        std::shared_ptr<SysMLv2::REST::Project> getProjectById(boost::uuids::uuid projectId);


    private:
        std::vector<ProjectTreeViewItem*> ChildItems;
        std::shared_ptr<SysMLv2::REST::Project> ProjectData;
        ProjectTreeViewItem *Parent;
    };
}
