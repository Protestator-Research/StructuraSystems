//
// Created by Moritz Herzog on 15.05.24.
//

#ifndef DIGITALTWIN_PROJECTTREEVIEWITEM_H
#define DIGITALTWIN_PROJECTTREEVIEWITEM_H

#include <QVariant>
#include <boost/uuid/uuid.hpp>

namespace SysMLv2::Entities{
    class Project;
}

namespace StructuraSystems::Client {
    class ProjectTreeViewItem {
    public:
        ProjectTreeViewItem();
        explicit ProjectTreeViewItem(std::shared_ptr<SysMLv2::Entities::Project> project, ProjectTreeViewItem* parent = nullptr);

        ~ProjectTreeViewItem();

        ProjectTreeViewItem* child(int row);
        int childCount();
        int columnCount();
        QVariant data();
        int row();
        ProjectTreeViewItem* parentItem();

        void appendProject(std::shared_ptr<SysMLv2::Entities::Project> project);
        [[nodiscard]] std::shared_ptr<SysMLv2::Entities::Project> getProject() const;

        void clearChildItems();

        std::shared_ptr<SysMLv2::Entities::Project> getProjectById(boost::uuids::uuid projectId);


    private:
        std::vector<ProjectTreeViewItem*> ChildItems;
        std::shared_ptr<SysMLv2::Entities::Project> ProjectData;
        ProjectTreeViewItem *Parent;
    };
}


#endif //DIGITALTWIN_PROJECTTREEVIEWITEM_H
