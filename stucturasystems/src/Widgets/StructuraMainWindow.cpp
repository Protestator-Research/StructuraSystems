//
// Created by Moritz Herzog on 25.04.24.
//

// You may need to build the project (run Qt uic code generator) to get "ui_DigitalTwinMainWindow.h" resolved

#include "StructuraMainWindow.h"
#include "ui_MainWindow.h"
#include "Dialogs/SettingsDialog.h"

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
        readSettings();
    }

    StructuraMainWindow::~StructuraMainWindow() {

    }

    void StructuraMainWindow::initWindow() {
        ui->ProjectTabWidget->clear();
        ToolBar = new QToolBar(this);
        ModelToolBar = new QToolBar(this);
        WindowModel = new MainWindowModel(this);

        ui->actionOpen_Folder->setIcon(QIcon(":/icons/userinterface/Open"));
        ui->actionConnect->setIcon(QIcon(":/icons/userinterface/Connection"));
        ui->actionStep_Back->setIcon(QIcon(":/icons/arrows/StepBack"));
        ui->actionStep_Forward->setIcon(QIcon(":/icons/arrows/StepForward"));
        ui->actionSave_File->setIcon(QIcon(":/icons/userinterface/Save"));
        ui->actionNew->setIcon(QIcon(":/icons/userinterface/New"));
        ui->actionParse_Model->setIcon(QIcon(":/icons/sience/Debug"));

        addToolBar(ToolBar);
        ToolBar->addAction(ui->actionNew);
        ToolBar->addAction(ui->actionSave_File);
        ToolBar->addAction(ui->actionOpen_Folder);
        ToolBar->addAction(ui->actionConnect);

        addToolBar(ModelToolBar);
        ModelToolBar->addAction(ui->actionParse_Model);
        ModelToolBar->addAction(ui->actionStep_Back);
        ModelToolBar->addAction(ui->actionStep_Forward);
    }

    void StructuraMainWindow::makeConnections() {
        connect(ui->ProjectTabWidget, SIGNAL(tabCloseRequested(int)), WindowModel, SLOT(onTabCloseRequested(int)));
        connect(ui->actionOpen_File, SIGNAL(triggered(bool)), WindowModel, SLOT(openFile()));
        connect(ui->actionOpen_Folder, SIGNAL(triggered(bool)), WindowModel, SLOT(openFolder()));
        connect(ui->LocalTreeView, SIGNAL(doubleClicked(const QModelIndex &)), WindowModel, SLOT(onFileDoubleClickClicked(const QModelIndex &)));
        connect(ui->actionSettings, SIGNAL(triggered(bool)), this, SLOT(openSettingsWindow()));
        connect(ui->actionConnection_Settings, SIGNAL(triggered(bool)), this, SLOT(openSettingsWindow()));
        connect(ui->actionConnect, SIGNAL(triggered(bool)), WindowModel, SLOT(connectToBackend()));
        connect(ui->ExternalTreeView, SIGNAL(doubleClicked(const QModelIndex &)), WindowModel, SLOT(onOnlineProjectDoubleClicked(const QModelIndex &)));
        connect(ui->actionNew, SIGNAL(triggered(bool)), WindowModel, SLOT(newFile()));
        connect(ui->actionSave_File, SIGNAL(triggered(bool)),WindowModel, SLOT(saveFile()));
        connect(ui->actionParse_Model, SIGNAL(triggered(bool)), WindowModel, SLOT(onActionParseModelClicked()));
        connect(ui->actionUpload_Local_Project, SIGNAL(triggered(bool)), WindowModel, SLOT(onUploadModelClicked()));
        connect(ui->actionCreate_Digital_Twin, SIGNAL(triggered(bool)), WindowModel, SLOT(onCreateDTClicked()));
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

    void StructuraMainWindow::openSettingsWindow() {
        SettingsDialog dialog = SettingsDialog(Settings,this);
        dialog.show();
        if(dialog.exec()==QDialog::Accepted)
            WindowModel->openFolder(QString::fromStdString(Settings->workingDirectory()));
    }

    void StructuraMainWindow::readSettings() {
        Settings = new SettingsModel();
        WindowModel->setSettingsModel(Settings);
        if(Settings->workingDirectory().empty())
        {
            if(QMessageBox::question(this, tr("Loading Default Settings"), tr("Do you want to initialize the application with the default Settings?"))==QMessageBox::StandardButton::Yes) {
                Settings->initWithDefaults();
            }else
                return;
        }
        WindowModel->openFolder(QString::fromStdString(Settings->workingDirectory()));
    }

    QWidget *StructuraMainWindow::getActiveTabWidget() {
        return ui->tabWidget->currentWidget();
    }

    int StructuraMainWindow::getActiveTabIndex() {
        return ui->tabWidget->currentIndex() - 1;
    }
} // StructuraSystems::Client
