//
// Created by Moritz Herzog on 25.04.24.
//

// You may need to build the project (run Qt uic code generator) to get "ui_DigitalTwinMainWindow.h" resolved

#include "StructuraMainWindow.h"
#include "ui_MainWindow.h"

#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>

namespace StructuraSystems::Client {

    StructuraMainWindow::StructuraMainWindow(QWidget *parent) : QMainWindow(parent){
        ui = new Ui::StructuraMainWindow();
        ui->setupUi(this);
        ui->retranslateUi(this);
    }

    StructuraMainWindow::~StructuraMainWindow() {

    }
} // StructuraSystems::Client
