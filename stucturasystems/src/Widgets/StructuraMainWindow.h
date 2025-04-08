//
// Created by Moritz Herzog on 25.04.24.
//

#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QAbstractItemModel>

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

    private:
        Ui::StructuraMainWindow *ui;
        QToolBar *toolBar;

    private slots:

    };
} // StructuraSystems::Client
