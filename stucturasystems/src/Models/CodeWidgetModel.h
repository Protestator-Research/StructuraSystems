//
// Created by Moritz Herzog on 09.04.25.
//

#ifndef DIGITALTWIN_CODEWIDGETMODEL_H
#define DIGITALTWIN_CODEWIDGETMODEL_H

#include <memory>
#include <QObject>
#include <QStandardItemModel>
#include <sysmlv2service/implementation/ElementNavigationService.h>

namespace SysMLv2::Entities{
    class Project;
    class Commit;
}

namespace StructuraSystems::Client {
    class CodeWidget;

    class CodeWidgetModel : public QObject{
        Q_OBJECT
    public:
        CodeWidgetModel(CodeWidget* codeWidget, std::shared_ptr<SysMLv2::Entities::Project> &project, std::shared_ptr<SysMLv2::Entities::Commit> &commit);
        ~CodeWidgetModel();

        void updateItemView(std::shared_ptr<SysMLv2::Entities::Project> &project, std::shared_ptr<SysMLv2::Entities::Commit> &commit);
    public slots:

    private:
        std::shared_ptr<SysMLv2::Entities::Project> Project;
        std::shared_ptr<SysMLv2::Entities::Commit> Commit;
        CodeWidget* CodeWidget;
        QStandardItemModel *ItemModel;
        std::unique_ptr<SysMLv2::API::ElementNavigationService> ElementService;

    };
}

#endif //DIGITALTWIN_CODEWIDGETMODEL_H
