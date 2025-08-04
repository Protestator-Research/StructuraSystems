//
// Created by Moritz Herzog on 08.04.25.
//

#include "MainWindowModel.h"
#include "../Widgets/StructuraMainWindow.h"
#include "../Widgets/CodeWidget.h"
#include "ItemModels/ProjectItemModel.h"
#include "Parser/StructuraSystemsParser.h"
#include "../Widgets/Dialogs/DigitalTwinCreationWizzard.h"
#include "../Widgets/CodeWidget.h"

#include <QFileDialog>
#include <QStandardPaths>
#include <QFile>
#include <QString>
#include <QApplication>
#include <QMessageBox>
#include <QTextStream>

#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Data.h>
#include <sysmlv2/rest/entities/Element.h>
#include <sysmlv2/rest/entities/DataIdentity.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Branch.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <curl/curl.h>

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
        auto commit = std::make_shared<SysMLv2::REST::Commit>("Created from Filesystem", "Created from Filesystem",
                                                                  project);
        for (auto element: elements) {
            auto dataVersion = std::make_shared<SysMLv2::REST::DataVersion>(
                    std::make_shared<SysMLv2::REST::DataIdentity>(boost::uuids::random_generator()()), element);
            commit->addChange(dataVersion);
        }

        project->getDefaultBranch()->setHead(commit);
    }

    void MainWindowModel::onFileDoubleClickClicked(const QModelIndex& index) {
        auto project = LocalFileItemModel->getProjects().at(index.row());
        auto commit = project->getDefaultBranch()->getHead();
        CodeWidgetMap[QString::fromStdString(project->getName())] = new CodeWidget(project,commit,MainWindow);
        MainWindow->addTabToMainWindow(CodeWidgetMap[QString::fromStdString(project->getName())], QString::fromStdString(project->getName()));
        CodeWidgetModelMap[QString::fromStdString(project->getName())] =  CodeWidgetMap[QString::fromStdString(project->getName())]->getModel();
        connect(CodeWidgetModelMap[QString::fromStdString(project->getName())], SIGNAL(tabEdited()), this, SLOT(onTabEdited()));
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
        try {
            if (BackendConnection != nullptr) {
                QMessageBox msg = QMessageBox(MainWindow);
                msg.setIcon(QMessageBox::Icon::Critical);
                msg.setText(tr("Online Connection already established. Reconnecting will be canceled."));
                msg.show();
                return;
            }
            BackendConnection = new CommunicationService(Settings->serverPath());
            BackendConnection->setUserForLoginInBackend(Settings->username(), Settings->password());
            auto projects = BackendConnection->getAllProjects();
            for (const auto &project: projects) {
                ExternalFileItemModel->appendProject(project);
            }
        }catch (std::exception& ex) {
            QMessageBox msg = QMessageBox(MainWindow);
            msg.setIcon(QMessageBox::Icon::Critical);
            msg.setText(tr("Could not connect to backend. \r\n Reason is:"));
            msg.setInformativeText(QString::fromStdString(ex.what()));
            msg.show();
            BackendConnection = nullptr;
        }
    }

    void MainWindowModel::onOnlineProjectDoubleClicked(const QModelIndex &index) {
        try {
            auto project = ExternalFileItemModel->getProjects().at(index.row());
            auto branches = BackendConnection->getAllBranchesForProjectWithID(project->getId());
            std::shared_ptr<SysMLv2::REST::Branch> mainBranch;
            for (const auto &branch: branches)
                if (branch->getId() == project->getDefaultBranch()->getId())
                    mainBranch = branch;

            auto commit = BackendConnection->getCommitWithId(project->getId(), mainBranch->getHead()->getId());
            CodeWidgetMap[QString::fromStdString(project->getName())] = new CodeWidget(project, commit,
                                                                                       BackendConnection->getAllElements(
                                                                                               commit->getId(),
                                                                                               project->getId()),
                                                                                       MainWindow);
            MainWindow->addTabToMainWindow(CodeWidgetMap[QString::fromStdString(project->getName())],
                                           QString::fromStdString(project->getName()));

            CodeWidgetModelMap[QString::fromStdString(project->getName())] =  CodeWidgetMap[QString::fromStdString(project->getName())]->getModel();
            connect(CodeWidgetModelMap[QString::fromStdString(project->getName())], SIGNAL(tabEdited()), this, SLOT(onTabEdited()));
        }catch (std::exception &ex) {
            QMessageBox msg;
            msg.setText("Error Downloading Elements.");
            msg.setInformativeText(ex.what());
            msg.show();
        }
    }

    void MainWindowModel::onTabEdited() {
        MainWindow->setWindowModified(true);
    }

    void MainWindowModel::saveFile() {
        const auto projectName = MainWindow->getTabTitle(MainWindow->getActiveTabIndex());
        CodeWidgetModelMap[projectName]->saveFile(Settings->workingDirectory());
    }

    void MainWindowModel::newFile() {
        const auto filename = QFileDialog::getSaveFileName(MainWindow,tr("Create new File"), QString::fromStdString(Settings->workingDirectory()),tr("Markdown File (*.md);;KerML File (*.kerml);;SysML File (*.sysml);;XML-File (*.xml);;JSON-File (*.json)"));
        QFile file;
        file.setFileName(filename);
        if (file.open(QIODevice::ReadWrite)) {
            QTextStream stream(&file);
            stream << tr("Get Started!");
        }
        file.flush();
        file.close();
        openFolder(QString::fromStdString(Settings->workingDirectory()));
    }

    void MainWindowModel::onActionParseModelClicked() {
        QString activeTabName = MainWindow->getTabTitle(MainWindow->getActiveTabIndex());
        CodeWidgetModelMap[activeTabName]->parseKerMLSysMLModel();
    }

    void MainWindowModel::onUploadModelClicked() {
        const auto modelName = MainWindow->getTabTitle(MainWindow->getActiveTabIndex());
        const auto model = CodeWidgetModelMap[modelName];
        model->createProjectAndCommit(BackendConnection);
    }

    void MainWindowModel::onCreateDTClicked() {
        int index = MainWindow->getActiveTabIndex();
        const auto modelName = MainWindow->getTabTitle(index+1);
        const auto model = CodeWidgetModelMap[modelName];
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements = BackendConnection->getAllElements(model->getCommit()->getId(), model->getProject()->getId());
        auto wizzard = DigitalTwinCreationWizzard(model->getProject(),model->getCommit(), elements, MainWindow);
        wizzard.show();
        if (wizzard.exec()==QDialog::Accepted) {
            const auto digitalTwin = wizzard.generateDigitalTwin();
            BackendConnection->postDigitalTwinToProject(model->getProject()->getId(), digitalTwin);
        }
            return;
            // BackendConnection->postDigitalTwinToProject();
    }
}
