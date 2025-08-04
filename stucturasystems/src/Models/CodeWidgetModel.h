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

namespace SysMLv2::REST{
    class Project;
    class Commit;
    class Element;
}

namespace StructuraSystems::Client {
    class CodeWidget;
    class CommunicationService;

    class CodeWidgetModel : public QObject{
        Q_OBJECT
    public:
        CodeWidgetModel(CodeWidget* codeWidget, std::shared_ptr<SysMLv2::REST::Project> &project, std::shared_ptr<SysMLv2::REST::Commit> &commit);
        CodeWidgetModel(CodeWidget* codeWidget, std::shared_ptr<SysMLv2::REST::Project> &project, std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements, std::shared_ptr<SysMLv2::REST::Commit> &commit);
        ~CodeWidgetModel() override;

        void updateItemView(std::shared_ptr<SysMLv2::REST::Project> &project, std::shared_ptr<SysMLv2::REST::Commit> &commit);

        void createCommit(CommunicationService* communicationService);
        void createProjectAndCommit(CommunicationService* communicationService);

        void saveFile(std::string basePath);

        void parseKerMLSysMLModel();

        std::shared_ptr<SysMLv2::REST::Commit> getCommit() const;
        std::shared_ptr<SysMLv2::REST::Project> getProject() const;

        void setDialogView(bool dialogView);

        std::vector<std::shared_ptr<SysMLv2::REST::Element>> getSelectedElements() const;
    signals:
        void tabEdited();

    public slots:


    private slots:
        void elementEdited();

    private:


        std::shared_ptr<SysMLv2::REST::Project> Project;
        std::shared_ptr<SysMLv2::REST::Commit> Commit;
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> Elements;
        CodeWidget* CodeWidget;
        //QStandardItemModel *ItemModel;
        std::unique_ptr<SysMLv2::API::ElementNavigationService> ElementService;
        bool DialogView = false;

    };
}

#endif //DIGITALTWIN_CODEWIDGETMODEL_H
