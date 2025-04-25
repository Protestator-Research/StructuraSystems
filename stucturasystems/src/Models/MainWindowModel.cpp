//
// Created by Moritz Herzog on 08.04.25.
//

#include "MainWindowModel.h"
#include "../Widgets/StructuraMainWindow.h"
#include "../Widgets/CodeWidget.h"
#include "ItemModels/ProjectItemModel.h"
#include "Parser/StructuraSystemsParser.h"
#include "../Widgets/CodeWidget.h"

#include <QFileDialog>
#include <QStandardPaths>
#include <QFile>
#include <QString>
#include <QApplication>
#include <QMessageBox>

#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Data.h>
#include <sysmlv2/rest/entities/Element.h>
#include <sysmlv2/rest/entities/DataIdentity.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Branch.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>

namespace StructuraSystems::Client {
    MainWindowModel::MainWindowModel(StructuraMainWindow *mainWindow) {
        MainWindow = mainWindow;
        LocalFileItemModel = new ProjectItemModel(MainWindow);
        ExternalFileItemModel = new ProjectItemModel(MainWindow);

        MainWindow->addModelToExternalTreeView(ExternalFileItemModel);
        MainWindow->addModelToLocalTreeView(LocalFileItemModel);
    }

    void MainWindowModel::openFolder() {
        QString directoryString = QFileDialog::getExistingDirectory(MainWindow, tr("Open Folder"), QString::fromStdString(Settings->workingDirectory()));
        openFolder(directoryString);
    }

    void MainWindowModel::openFile() {
        QStringList filesToOpen = QFileDialog::getOpenFileNames(MainWindow, tr("Open Files"), QString::fromStdString(Settings->workingDirectory()), tr("Markdown File (*.md);;KerML File (*.kerml);;SysML File (*.sysml);;XML-File (*.xml);;JSON-File (*.json)"));
        for(QString fileName : filesToOpen) {
            openProjectFromFileStorage(fileName);
        }
    }

    void MainWindowModel::onTabCloseRequested(int index) {
        const auto &title = MainWindow->getTabTitle(index);
        CodeWidgetModelMap.erase(title);
        MainWindow->removeTabWithIndex(index);
    }

    void MainWindowModel::openProjectFromFileStorage(QString filePath) {
        const auto project = LocalFileItemModel->createProject(filePath.split("/").last().toStdString(),
                                                               "Created from Filesystem");
        StructuraSystemsParser parser;
        auto elements = parser.readFile(filePath);
        auto commit = std::make_shared<SysMLv2::Entities::Commit>("Created from Filesystem", "Created from Filesystem",
                                                                  project);
        for (auto element: elements) {
            auto dataVersion = std::make_shared<SysMLv2::Entities::DataVersion>(
                    std::make_shared<SysMLv2::Entities::DataIdentity>(boost::uuids::random_generator()()), element);
            commit->addChange(dataVersion);
        }

        project->getDefaultBranch()->setHead(commit);
    }

    void MainWindowModel::onDoubleClickClicked(const QModelIndex& index) {
        auto project = LocalFileItemModel->getProjects().at(index.row());
        auto commit = project->getDefaultBranch()->getHead();
        CodeWidgetMap[QString::fromStdString(project->getName())] = new CodeWidget(project,commit,MainWindow);
        MainWindow->addTabToMainWindow(CodeWidgetMap[QString::fromStdString(project->getName())], QString::fromStdString(project->getName()));
    }

    void MainWindowModel::openFolder(QString folder) {
        LocalFileItemModel->clear();
        QDir directory(folder);
        QStringList files = directory.entryList(QStringList()<<"*.md"<<"*.kerml"<<"*.sysml"<<"*.xml"<<"*.json");
        for(QString fileName : files) {
            openProjectFromFileStorage(directory.absolutePath()+"/"+fileName);
        }
    }

    void MainWindowModel::setSettingsModel(SettingsModel *settingsModel) {
        Settings = settingsModel;
    }

    void MainWindowModel::connectToBackend() {
        if(BackendConnection != nullptr) {
            QMessageBox msg = QMessageBox(MainWindow);
            msg.setIcon(QMessageBox::Icon::Critical);
            msg.setText(tr("Online Connection already established. Reconnecting will be canceled."));
            msg.show();
            return;
        }
        BackendConnection = new CommunicationService(Settings->serverPath());
        BackendConnection->setUserForLoginInBackend(Settings->username(), Settings->password());
        auto projects = BackendConnection->getAllProjects();
        for(const auto& project : projects){
            ExternalFileItemModel->createProject(project->getName(),project->getDescription());
        }
    }
}