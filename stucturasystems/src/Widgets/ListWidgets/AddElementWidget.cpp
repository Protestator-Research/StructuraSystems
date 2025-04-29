#include "AddElementWidget.h"
#include "ui_AddElementWidget.h"
namespace StructuraSystems::Client {
    AddElementWidget::AddElementWidget(QWidget *parent)
            : QWidget(parent), ui(new Ui::AddElementWidget) {
        ui->setupUi(this);
        ui->retranslateUi(this);
        makeConnections();
    }

    AddElementWidget::~AddElementWidget() {
        delete ui;
    }

    void AddElementWidget::makeConnections() {
        connect(ui->commandLinkButton, SIGNAL(clicked(bool)), this, SIGNAL(addElementAbove()));
    }
}