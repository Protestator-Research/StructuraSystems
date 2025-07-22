//
// Created by Moritz Herzog on 22.07.25.
//

#include "DigitalTwinCreationWizzard.h"

#include "ui_DigitalTwinCreationWizzard.h"


StructuraSystems::Client::DigitalTwinCreationWizzard::DigitalTwinCreationWizzard(QWidget *parent) : QWizard(parent), ui(new Ui::CreateDTWizard){
    ui->setupUi(this);
    ui->retranslateUi(this);

}

StructuraSystems::Client::DigitalTwinCreationWizzard::~DigitalTwinCreationWizzard() {
}
