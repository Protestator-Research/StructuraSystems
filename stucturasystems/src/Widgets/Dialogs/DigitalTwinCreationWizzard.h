//
// Created by Moritz Herzog on 22.07.25.
//

#ifndef DIGITALTWINCREATIONWIZZARD_H
#define DIGITALTWINCREATIONWIZZARD_H

#include <QWizard>

namespace StructuraSystems::Client {
    namespace Ui {
        class CreateDTWizard;
    }
    class DigitalTwinCreationWizzard : public QWizard{
    public:
        DigitalTwinCreationWizzard(QWidget *parent);
        ~DigitalTwinCreationWizzard();

    private:
        Ui::CreateDTWizard *ui;
    };
}

#endif //DIGITALTWINCREATIONWIZZARD_H
