//
// Created by Moritz Herzog on 08.04.25.
//

#ifndef DIGITALTWIN_MAINWINDOWMODEL_H
#define DIGITALTWIN_MAINWINDOWMODEL_H

#include <QString>
#include <QObject>
#include <QStandardItemModel>

#include <map>

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


    public slots:
        void openFile();
        void openFolder();
        void onTabCloseRequested(int index);

    private:
        StructuraMainWindow* MainWindow;
        std::map<QString,CodeWidget*> CodeWidgetMap;
        std::map<QString, CodeWidgetModel*> CodeWidgetModelMap;
        ProjectItemModel* LocalFileItemModel;
        ProjectItemModel* ExternalFileItemModel;
    };
}


#endif //DIGITALTWIN_MAINWINDOWMODEL_H
