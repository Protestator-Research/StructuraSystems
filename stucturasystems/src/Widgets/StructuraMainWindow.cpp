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

    StructuraMainWindow::StructuraMainWindow(QWidget *parent) :
        QMainWindow(parent)
    {
        ui = new Ui::StructuraMainWindow();
        ui->setupUi(this);
        ui->retranslateUi(this);

        initWindow();
        makeConnections();
    }

    StructuraMainWindow::~StructuraMainWindow() {

    }

    void StructuraMainWindow::initWindow() {
        ui->ProjectTabWidget->clear();
        ToolBar = new QToolBar();
        WindowModel = new MainWindowModel(this);
    }

    void StructuraMainWindow::makeConnections() {
        connect(ui->ProjectTabWidget, SIGNAL(tabCloseRequested(int)), WindowModel, SLOT(onTabCloseRequested(int)));
        connect(ui->actionOpen_File, SIGNAL(triggered(bool)), WindowModel, SLOT(openFile()));
        connect(ui->actionOpen_Folder, SIGNAL(triggered(bool)), WindowModel, SLOT(openFolder()));
        connect(ui->LocalTreeView, SIGNAL(doubleClicked(const QModelIndex &)), WindowModel, SLOT(onDoubleClickClicked(const QModelIndex &)));
    }

    void StructuraMainWindow::addTabToMainWindow(QWidget *tab, QString title) {
        ui->ProjectTabWidget->addTab(tab,title);
    }

    void StructuraMainWindow::removeTabWithIndex(int index) {
        ui->ProjectTabWidget->removeTab(index);
    }

    QString StructuraMainWindow::getTabTitle(int index) {
        return ui->ProjectTabWidget->tabText(index);
    }

    void StructuraMainWindow::addModelToLocalTreeView(QAbstractItemModel *model) {
        ui->LocalTreeView->setModel(model);
    }

    void StructuraMainWindow::addModelToExternalTreeView(QAbstractItemModel *model) {
        ui->ExternalTreeView->setModel(model);
    }
} // StructuraSystems::Client
