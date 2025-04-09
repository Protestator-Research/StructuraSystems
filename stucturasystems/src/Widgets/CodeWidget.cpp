//
// Created by Moritz Herzog on 09.04.25.
//

// You may need to build the project (run Qt uic code generator) to get "ui_CodeWidget.h" resolved

#include "CodeWidget.h"
#include "ui_CodeWidget.h"

namespace StructuraSystems::Client {
    CodeWidget::CodeWidget(QWidget *parent) :
            QWidget(parent), ui(new Ui::CodeWidget) {
        ui->setupUi(this);
    }

    CodeWidget::~CodeWidget() {
        delete ui;
    }
} // StructuraSystems::Client
