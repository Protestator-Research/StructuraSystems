//
// Created by Moritz Herzog on 09.04.25.
//

// You may need to build the project (run Qt uic code generator) to get "ui_CodeWidget.h" resolved

#include "CodeWidget.h"
#include "ui_CodeWidget.h"
#include "../Models/Delegates/HtmlDelegate.h"

namespace StructuraSystems::Client {
    CodeWidget::CodeWidget(std::shared_ptr<SysMLv2::Entities::Project> project, std::shared_ptr<SysMLv2::Entities::Commit> commit, QWidget *parent) :
            QWidget(parent), ui(new Ui::CodeWidget) {
        ui->setupUi(this);
        ui->retranslateUi(this);
        Model = new CodeWidgetModel(this, project,commit);
        HtmlDelegate = new HTMLDelegate();
        ui->MarkdownCodeEditingWidget->setItemDelegate(HtmlDelegate);
    }

    CodeWidget::~CodeWidget() {
        delete ui;
        delete Model;
        delete HtmlDelegate;
    }

    void CodeWidget::setMarkdownCodeEditingWidgetModel(QAbstractItemModel *model) {
        ui->MarkdownCodeEditingWidget->setModel(model);
    }
} // StructuraSystems::Client
