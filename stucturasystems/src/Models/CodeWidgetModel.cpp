//
// Created by Moritz Herzog on 09.04.25.
//

#include <sysmlv2/rest/entities/Element.h>
#include <QStandardItem>

#include "CodeWidgetModel.h"
#include "../Widgets/CodeWidget.h"
#include "Parser/Markdown/MarkdownParser.h"

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
        CodeWidget->setMarkdownCodeEditingWidgetModel(ItemModel);
        updateItemView(project,commit);
    }

    void CodeWidgetModel::updateItemView(std::shared_ptr<SysMLv2::Entities::Project> &project,
                                         std::shared_ptr<SysMLv2::Entities::Commit> &commit) {
        ItemModel->clear();

        const auto & elements = ElementService->getElements(project,commit);
        for(const auto& element : elements) {
            MarkdownParser parser;
            parser.parseMarkdown(QString::fromStdString(element->getMarkdownString()));
            auto item = new QStandardItem(parser.getHTMLOfMarkdown());
            ItemModel->appendRow(item);
        }
    }

    CodeWidgetModel::~CodeWidgetModel() {
        delete ItemModel;
    }
}
