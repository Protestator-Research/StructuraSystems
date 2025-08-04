//
// Created by Moritz Herzog on 09.04.25.
//

// You may need to build the project (run Qt uic code generator) to get "ui_CodeWidget.h" resolved

#include "CodeWidget.h"
#include "ui_CodeWidget.h"
#include "../Models/Delegates/HtmlDelegate.h"

namespace StructuraSystems::Client {
    CodeWidget::CodeWidget(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, QWidget *parent) :
            QWidget(parent), ui(new Ui::CodeWidget) {
        ui->setupUi(this);
        ui->retranslateUi(this);
        Model = new CodeWidgetModel(this, project,commit);
        HtmlDelegate = new HTMLDelegate();
        decorateWidget();
    }

    CodeWidget::~CodeWidget() {
        delete ui;
        delete Model;
        delete HtmlDelegate;
    }

    void CodeWidget::decorateWidget() {
        ui->AddElementToolButton->setIcon(QIcon(":/icons/userinterface/Add"));
        ui->AddElementToolButton->setVisible(false);
    }

    QListWidget *CodeWidget::getListWidget() {
        return ui->MarkdownCodeElementsWidget;
    }

    CodeWidget::CodeWidget(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit,
                           std::vector<std::shared_ptr<SysMLv2::REST::Element>> entities, QWidget *parent) :
                           QWidget(parent), ui(new Ui::CodeWidget){
        ui->setupUi(this);
        ui->retranslateUi(this);
        Model = new CodeWidgetModel(this, project, entities, commit);
        HtmlDelegate = new HTMLDelegate();
        decorateWidget();
    }

    CodeWidgetModel *CodeWidget::getModel() const {
        return Model;
    }

    void CodeWidget::safeFile(std::string basePath) {
        Model->saveFile(basePath);
    }

    void CodeWidget::decorateDialogView() {
        ui->MarkdownCodeElementsWidget->setSelectionMode(QAbstractItemView::SelectionMode::MultiSelection);
        Model->setDialogView(true);
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Element>> CodeWidget::getSelectedElements() {
        return Model->getSelectedElements();
    }
} // StructuraSystems::Client
