#include "AddElementWidget.h"
#include "ui_AddElementWidget.h"
namespace StructuraSystems::Client {
    AddElementWidget::AddElementWidget(QWidget *parent)
            : QWidget(parent), ui(new Ui::AddElementWidget) {
        ui->setupUi(this);
        ui->retranslateUi(this);
    }

    AddElementWidget::~AddElementWidget() {
        delete ui;
    }
}