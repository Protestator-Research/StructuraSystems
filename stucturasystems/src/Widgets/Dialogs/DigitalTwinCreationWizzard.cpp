//
// Created by Moritz Herzog on 22.07.25.
//

#include "DigitalTwinCreationWizzard.h"

#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <boost/uuid/uuid.hpp>

#include "ui_DigitalTwinCreationWizzard.h"
#include "../../Services/entities/DigitalTwin.h"

namespace StructuraSystems::Client {
    DigitalTwinCreationWizzard::DigitalTwinCreationWizzard(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements, QWidget *parent) :
    QWizard(parent),
    ui(new Ui::CreateDTWizard){
        ui->setupUi(this);
        ui->retranslateUi(this);

        Project = project;
        Elements = elements;
        Commit = commit;
    }

    DigitalTwinCreationWizzard::~DigitalTwinCreationWizzard() {
        delete ui;
    }

    std::shared_ptr<SysMLv2::REST::DigitalTwin> DigitalTwinCreationWizzard::generateDigitalTwin() {
        std::vector<boost::uuids::uuid> connectedElements;
        return std::make_shared<SysMLv2::REST::DigitalTwin>(ui->DTNameLineEdit->text().toStdString(), connectedElements, Commit->getId());
    }
}
