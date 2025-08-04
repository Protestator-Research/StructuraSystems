//
// Created by Moritz Herzog on 22.07.25.
//

#ifndef DIGITALTWINCREATIONWIZZARD_H
#define DIGITALTWINCREATIONWIZZARD_H

#include <QWizard>
namespace SysMLv2::REST {
    class Project;
    class Element;
    class Commit;
    class DigitalTwin;
}

namespace StructuraSystems::Client {
    class CodeWidget;

    namespace Ui {
        class CreateDTWizard;
    }
    class DigitalTwinCreationWizzard : public QWizard{
    public:
        DigitalTwinCreationWizzard(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements, QWidget *parent);
        ~DigitalTwinCreationWizzard() override;

        std::shared_ptr<SysMLv2::REST::DigitalTwin> generateDigitalTwin();

    private:
        void decorateUI();

        Ui::CreateDTWizard *ui;
        std::shared_ptr<SysMLv2::REST::Project> Project;
        std::shared_ptr<SysMLv2::REST::Commit> Commit;
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> Elements;

        CodeWidget *_CodeWidget;
    };
}

#endif //DIGITALTWINCREATIONWIZZARD_H
