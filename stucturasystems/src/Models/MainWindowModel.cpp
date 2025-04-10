//
// Created by Moritz Herzog on 08.04.25.
//

#include "MainWindowModel.h"
#include "../Widgets/StructuraMainWindow.h"
#include "CodeWidgetModel.h"
#include "../Widgets/CodeWidget.h"
#include "ItemModels/ProjectItemModel.h"

#include <QFileDialog>
#include <QStandardPaths>
#include <QFile>
#include <QString>

namespace StructuraSystems::Client {
    MainWindowModel::MainWindowModel(StructuraMainWindow *mainWindow) {
        MainWindow = mainWindow;
        LocalFileItemModel = new ProjectItemModel(MainWindow);
        ExternalFileItemModel = new ProjectItemModel(MainWindow);

        MainWindow->addModelToExternalTreeView(ExternalFileItemModel);
        MainWindow->addModelToLocalTreeView(LocalFileItemModel);
    }

    void MainWindowModel::openFolder() {
        QString directoryString = QFileDialog::getExistingDirectory(MainWindow, tr("Open Folder"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        QDir directory(directoryString);
        QStringList files = directory.entryList(QStringList()<<"*.md"<<"*.kerml"<<"*.sysml"<<"*.xml"<<"*.json");
        for(QString fileName : files) {
            QFile file = QFile(fileName);
            LocalFileItemModel->createProject(file.fileName().split("/").last().toStdString(),"");
        }
    }

    void MainWindowModel::openFile() {
        QStringList filesToOpen = QFileDialog::getOpenFileNames(MainWindow, tr("Open Files"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Markdown File (*.md);;KerML File (*.kerml);;SysML File (*.sysml);;XML-File (*.xml);;JSON-File (*.json)"));
        for(QString fileName : filesToOpen) {
            QFile file = QFile(fileName);
            LocalFileItemModel->createProject(file.fileName().split("/").last().toStdString(),"");
        }
    }

    void MainWindowModel::onTabCloseRequested(int index) {
        MainWindow->getTabTitle(index);
    }


}