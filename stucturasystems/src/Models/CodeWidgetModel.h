//
// Created by Moritz Herzog on 09.04.25.
//

#ifndef DIGITALTWIN_CODEWIDGETMODEL_H
#define DIGITALTWIN_CODEWIDGETMODEL_H

#include <memory>
#include <vector>
#include <QObject>
#include <QStandardItemModel>
#include <sysmlv2service/implementation/ElementNavigationService.h>

namespace SysMLv2::Entities{
    class Project;
    class Commit;
    class Element;
}

namespace StructuraSystems::Client {
    class CodeWidget;

    class CodeWidgetModel : public QObject{
        Q_OBJECT
    public:
        CodeWidgetModel(CodeWidget* codeWidget, std::shared_ptr<SysMLv2::Entities::Project> &project, std::shared_ptr<SysMLv2::Entities::Commit> &commit);
        CodeWidgetModel(CodeWidget* codeWidget, std::shared_ptr<SysMLv2::Entities::Project> &project, std::vector<std::shared_ptr<SysMLv2::Entities::Element>> elements);
        ~CodeWidgetModel() override;

        void updateItemView(std::shared_ptr<SysMLv2::Entities::Project> &project, std::shared_ptr<SysMLv2::Entities::Commit> &commit);
    public slots:

    private:
        std::shared_ptr<SysMLv2::Entities::Project> Project;
        std::shared_ptr<SysMLv2::Entities::Commit> Commit;
        std::vector<std::shared_ptr<SysMLv2::Entities::Element>> Elements;
        CodeWidget* CodeWidget;
        QStandardItemModel *ItemModel;
        std::unique_ptr<SysMLv2::API::ElementNavigationService> ElementService;
    };
}

#endif //DIGITALTWIN_CODEWIDGETMODEL_H
