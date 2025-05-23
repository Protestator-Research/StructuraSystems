//
// Created by Moritz Herzog on 09.04.25.
//

#include <sysmlv2/rest/entities/Element.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2file/Parser.h>
#include <QStandardItem>
#include <QListWidget>
#include <QMessageBox>
#include <utility>

#include "CodeWidgetModel.h"
#include "../Widgets/CodeWidget.h"
#include "Parser/Markdown/MarkdownParser.h"
#include "../Widgets/ListWidgets/MarkdownElement.h"
#include "../Widgets/ListWidgets/AddElementWidget.h"
#include "Parser/StructuraSystemsParser.h"


namespace StructuraSystems::Client {
    CodeWidgetModel::CodeWidgetModel(StructuraSystems::Client::CodeWidget *codeWidget,
                                     std::shared_ptr<SysMLv2::Entities::Project> &project,
                                     std::shared_ptr<SysMLv2::Entities::Commit> &commit) :
            QObject(codeWidget),
            Project(project),
            Commit(commit),
            CodeWidget(codeWidget),
            ItemModel(new QStandardItemModel(codeWidget)),
            ElementService(std::make_unique<SysMLv2::API::ElementNavigationService>()){
        updateItemView(project,commit);
    }

    CodeWidgetModel::CodeWidgetModel(StructuraSystems::Client::CodeWidget *codeWidget, std::shared_ptr<SysMLv2::Entities::Project> &project,
                                     std::vector<std::shared_ptr<SysMLv2::Entities::Element>> elements) :
            QObject(codeWidget),
            Project(project),
            Elements(std::move(elements)),
            CodeWidget(codeWidget),
            ItemModel(new QStandardItemModel(codeWidget)),
            ElementService(std::make_unique<SysMLv2::API::ElementNavigationService>()) {
        updateItemView(project,Commit);
    }

    void CodeWidgetModel::updateItemView(std::shared_ptr<SysMLv2::Entities::Project> &project,
                                         std::shared_ptr<SysMLv2::Entities::Commit> &commit) {

        const auto codeDisplayWidget = CodeWidget->getListWidget();

        if (Elements.empty() && (Commit != nullptr))
            Elements = ElementService->getElements(project, commit);

        for (const auto &element: Elements) {
            if(!element->body().empty()) {
                auto markdownElement = new MarkdownElement(element, codeDisplayWidget);
                auto listItemWidget = new QListWidgetItem(codeDisplayWidget);
                connect(markdownElement, SIGNAL(elementEdited()), this, SLOT(elementEdited()));
                listItemWidget->setSizeHint(markdownElement->sizeHint());
                codeDisplayWidget->setItemWidget(listItemWidget, markdownElement);
            }
        }

        auto addElementWidget = new AddElementWidget(codeDisplayWidget);
        auto listItemWidget = new QListWidgetItem(codeDisplayWidget);
        listItemWidget->setSizeHint(addElementWidget->sizeHint());
        codeDisplayWidget->setItemWidget(listItemWidget, addElementWidget);

    }

    CodeWidgetModel::~CodeWidgetModel() {
        delete ItemModel;
    }

    void CodeWidgetModel::elementEdited() {
        CodeWidget->setWindowModified(true);
        emit tabEdited();
    }

    void CodeWidgetModel::createCommit() {
        //TODO
    }

    void CodeWidgetModel::saveFile(std::string basePath) {
        StructuraSystemsParser parser;
        auto path = QString::fromStdString(basePath + "/" + Project->getName());
        parser.writeFile(path, Elements);
    }

    void CodeWidgetModel::parseKerMLSysMLModel() {
        for(const auto& element : Elements){
            if(element->language()=="KerML")
                auto parsedModel = SysMLv2::Files::Parser::parseKerML(element->body());
        }

    }
}
