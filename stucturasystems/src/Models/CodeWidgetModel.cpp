//
// Created by Moritz Herzog on 09.04.25.
//
#include <boost/uuid/uuid.hpp>
#include <kerml/root/elements/Element.h>
#include <kerml/root/annotations/TextualRepresentation.h>
#include <sysmlv2/rest/entities/JSONEntities.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Identification.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/Parser.h>
#include <utility>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <memory>

#include "CodeWidgetModel.h"

#include <qlayout.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/service/online/SysMLAPIImplementation.h>

#include "../Widgets/CodeWidget.h"
#include "Parser/Markdown/MarkdownParser.h"
#include "../Widgets/ListWidgets/MarkdownElement.h"
#include "../Widgets/ListWidgets/AddElementWidget.h"
#include "Parser/StructuraSystemsParser.h"
#include "../Services/BECommunicationService.h"


namespace StructuraSystems::Client {
    CodeWidgetModel::CodeWidgetModel(StructuraSystems::Client::CodeWidget *codeWidget,
                                     std::shared_ptr<SysMLv2::REST::Project> &project,
                                     std::shared_ptr<SysMLv2::REST::Commit> &commit) :
            QObject(codeWidget),
            Project(project),
            Commit(commit),
            CodeWidget(codeWidget),
            ElementService(std::make_unique<SysMLv2::API::ElementNavigationService>()){
        updateItemView(project,commit);
    }

    CodeWidgetModel::CodeWidgetModel(StructuraSystems::Client::CodeWidget *codeWidget, std::shared_ptr<SysMLv2::REST::Project> &project,
                                     std::vector<std::shared_ptr<KerML::Entities::Element>> elements, std::shared_ptr<SysMLv2::REST::Commit> &commit) :
            QObject(codeWidget),
            Project(project),
            Commit(commit),
            Elements(std::move(elements)),
            CodeWidget(codeWidget),
            ElementService(std::make_unique<SysMLv2::API::ElementNavigationService>()) {
        updateItemView(project,Commit);
    }

    void CodeWidgetModel::updateItemView(std::shared_ptr<SysMLv2::REST::Project> &project,
                                         std::shared_ptr<SysMLv2::REST::Commit> &commit) {

        const auto scrollAreaWidget = CodeWidget->getScrollAreaWidget();

        if (Elements.empty() && (Commit != nullptr))
            Elements = ElementService->getElements(project, commit);

        for (const auto &element: Elements) {
            auto type = element->getType();
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            if (type == SysMLv2::REST::TEXTUAL_REPRESENTATION_TYPE) {
                const auto textualRepresentation = std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(element);

                auto markdownElement = new MarkdownElement(textualRepresentation, scrollAreaWidget);
                scrollAreaWidget->layout()->addWidget(markdownElement);
                markdownElement->repaint();

                connect(markdownElement, SIGNAL(elementEdited()), this, SLOT(elementEdited()));
            }
        }
        scrollAreaWidget->repaint();
    }

    CodeWidgetModel::~CodeWidgetModel() {
    }

    void CodeWidgetModel::elementEdited() {
        CodeWidget->setWindowModified(true);
        emit tabEdited();
    }

    void CodeWidgetModel::createCommit([[maybe_unused]] CommunicationService* communicationService) {
        //communicationService->postCommitWithId(Project->getId(), Commit);
        //TODO
    }

    void CodeWidgetModel::saveFile(std::string basePath) {
        StructuraSystemsParser parser;
        auto path = QString::fromStdString(basePath + "/" + Project->getName());
        parser.writeFile(path, Elements);
    }

    void CodeWidgetModel::parseKerMLSysMLModel() {
        for(const auto& element : Elements){
            if (std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(element) != nullptr)
                if(std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(element)->language()=="KerML")
                    auto parsedModel = SysMLv2::Files::Parser::parseKerML(std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(element)->body());
        }

    }

    std::shared_ptr<SysMLv2::REST::Commit> CodeWidgetModel::getCommit() const {
        return Commit;
    }

    std::shared_ptr<SysMLv2::REST::Project> CodeWidgetModel::getProject() const {
        return Project;
    }

    void CodeWidgetModel::setDialogView(bool dialogView) {
        DialogView = dialogView;
        updateItemView(Project, Commit);
    }

    std::vector<std::shared_ptr<KerML::Entities::Element>> CodeWidgetModel::getSelectedElements() const {
        //const auto codeDisplayWidget = CodeWidget->getListWidget();

        //const auto selectedItems = codeDisplayWidget->selectedItems();

        std::vector<std::shared_ptr<KerML::Entities::Element>> result;
        //for (const auto& index : selectedItems) {
        //    const auto& markdownWidget = dynamic_cast<MarkdownElement*>(index);
        //    result.push_back(markdownWidget->getElement());
        //}


        return result;
    }

    void CodeWidgetModel::createProjectAndCommit(CommunicationService* communicationService) {
        Project = communicationService->postProject(Project->getName(), Project->getDescription(), "Main");

        std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> requestedChage;
        for (const auto &element : Elements) {
            auto dataVersion = std::make_shared<SysMLv2::REST::DataVersion>(boost::uuids::random_generator()(), element);
            requestedChage.push_back(dataVersion);
        }

        auto commitRequest = std::make_shared<SysMLv2::REST::CommitRequest>("Upload from Local Project, by Structura Systems", requestedChage);

        Commit = communicationService->postCommitWithId(Project->getId(), commitRequest);
    }


}
