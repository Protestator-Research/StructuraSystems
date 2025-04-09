//
// Created by Moritz Herzog on 08.04.25.
//

#include "MainWindowModel.h"
#include "../Widgets/StructuraMainWindow.h"
#include "CodeWidgetModel.h"
#include "../Widgets/CodeWidget.h"


#include <QFileDialog>
#include <QStandardPaths>

namespace StructuraSystems::Client {
    MainWindowModel::MainWindowModel(StructuraMainWindow *mainWindow) {
        MainWindow = mainWindow;
    }

    void MainWindowModel::openFolder() {
        QFileDialog::getExistingDirectory(MainWindow, tr("Open Folder"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }

    void MainWindowModel::openFile() {
        QStringList filesToOpen = QFileDialog::getOpenFileNames(MainWindow, tr("Open Files"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Markdown File (*md);;KerML File (*.kerml);;SysML File (*.sysml);;XML-File (*.xml);;JSON-File (*.json)"));
        for(filesToOpen)
    }

    void MainWindowModel::onTabCloseRequested(int index) {
        MainWindow->getTabTitle(index);
    }


}