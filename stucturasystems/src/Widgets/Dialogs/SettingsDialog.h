#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include "../../Models/SettingsModel.h"

namespace StructuraSystems::Client {
    namespace Ui {
        class SettingsDialog;
    }

    class SettingsDialog : public QDialog {
    Q_OBJECT
    public:
        explicit SettingsDialog(SettingsModel *settingsModel, QWidget *parent = nullptr);
        ~SettingsDialog();

    private slots:
        void onDiscardClicked();
        void onSaveClicked();
        void onResetClicked();
        void onPathPushButtonClicked();

    private:
        void makeConnections();
        void readSettings();

        Ui::SettingsDialog *ui;
        SettingsModel* Settings;
    };
}
#endif // SETTINGSDIALOG_H
