//
// Created by Moritz Herzog on 25.04.24.
//

#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QAbstractItemModel>
#include "../Models/MainWindowModel.h"

//---------------------------------------------------
//Forwarding
//---------------------------------------------------
QT_BEGIN_NAMESPACE
namespace StructuraSystems::Client {
    namespace Ui {
        class StructuraMainWindow;
    }
}
QT_END_NAMESPACE

namespace StructuraSystems::Client {
    class StructuraMainWindow : public QMainWindow {
    Q_OBJECT

    public:
        explicit StructuraMainWindow(QWidget *parent = nullptr);
        ~StructuraMainWindow() override;

        void addTabToMainWindow(QWidget* tab, QString title);
        void removeTabWithIndex(int index);
        QString getTabTitle(int index);

        void addModelToLocalTreeView(QAbstractItemModel* model);
        void addModelToExternalTreeView(QAbstractItemModel* model);


    private:
        void initWindow();
        void makeConnections();

        Ui::StructuraMainWindow *ui;
        QToolBar *ToolBar;

        MainWindowModel *WindowModel;


    };
} // StructuraSystems::Client
