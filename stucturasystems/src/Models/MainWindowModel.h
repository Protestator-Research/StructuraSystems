//
// Created by Moritz Herzog on 08.04.25.
//

#ifndef DIGITALTWIN_MAINWINDOWMODEL_H
#define DIGITALTWIN_MAINWINDOWMODEL_H

#include <QString>
#include <QObject>
#include <QFile>
#include <QStandardItemModel>

#include <map>

#include "SettingsModel.h"
#include "../Services/BECommunicationService.h"

namespace StructuraSystems::Client {
    class StructuraMainWindow;
    class CodeWidget;
    class CodeWidgetModel;
    class ProjectItemModel;

    class MainWindowModel : public QObject{
        Q_OBJECT
    public:
        MainWindowModel()=delete;
        MainWindowModel(StructuraMainWindow* mainWindow);
        ~MainWindowModel() = default;

        void openFolder(QString folder);
        void setSettingsModel(SettingsModel* settingsModel);


    public slots:
        void openFile();
        void openFolder();
        void onTabCloseRequested(int index);
        void onFileDoubleClickClicked(const QModelIndex& index);
        void onOnlineProjectDoubleClicked(const QModelIndex& index);
        void connectToBackend();

    private slots:
        void onTabEdited();

    private:
        StructuraMainWindow* MainWindow;
        std::map<QString,CodeWidget*> CodeWidgetMap;
        std::map<QString, CodeWidgetModel*> CodeWidgetModelMap;
        ProjectItemModel* LocalFileItemModel;
        ProjectItemModel* ExternalFileItemModel;

        SettingsModel* Settings;

        CommunicationService* BackendConnection = nullptr;

        void openProjectFromFileStorage(QString filePath);
    };
}


#endif //DIGITALTWIN_MAINWINDOWMODEL_H
