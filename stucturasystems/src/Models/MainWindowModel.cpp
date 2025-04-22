//
// Created by Moritz Herzog on 08.04.25.
//

#include "MainWindowModel.h"
#include "../Widgets/StructuraMainWindow.h"
#include "CodeWidgetModel.h"
#include "../Widgets/CodeWidget.h"
#include "ItemModels/ProjectItemModel.h"
#include "Parser/StructuraSystemsParser.h"

#include <QFileDialog>
#include <QStandardPaths>
#include <QFile>
#include <QString>
#include <QApplication>

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
        QString directoryString = QFileDialog::getExistingDirectory(MainWindow, tr("Open Folder"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        QDir directory(directoryString);
        QStringList files = directory.entryList(QStringList()<<"*.md"<<"*.kerml"<<"*.sysml"<<"*.xml"<<"*.json");
        for(QString fileName : files) {
            openProjectFromFileStorage(fileName);
        }
    }

    void MainWindowModel::openFile() {
        QStringList filesToOpen = QFileDialog::getOpenFileNames(MainWindow, tr("Open Files"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Markdown File (*.md);;KerML File (*.kerml);;SysML File (*.sysml);;XML-File (*.xml);;JSON-File (*.json)"));
        for(QString fileName : filesToOpen) {
            openProjectFromFileStorage(fileName);
        }
    }

    void MainWindowModel::onTabCloseRequested(int index) {
        MainWindow->getTabTitle(index);
    }

    void MainWindowModel::openProjectFromFileStorage(QString filePath) {
        QFile file = QFile(filePath);
        const auto project = LocalFileItemModel->createProject(file.fileName().split("/").last().toStdString(),
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

}