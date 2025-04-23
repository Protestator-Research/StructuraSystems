
#include "SettingsDialog.h"
#include "ui_SettingsDialog.h"
#include <QFileDialog>

namespace StructuraSystems::Client {
    SettingsDialog::SettingsDialog(SettingsModel* settingsModel, QWidget *parent)
            : QDialog(parent), ui(new Ui::SettingsDialog) {
        ui->setupUi(this);
        ui->retranslateUi(this);
        Settings = settingsModel;
        makeConnections();
        readSettings();
    }

    SettingsDialog::~SettingsDialog() {
        delete ui;
    }

    void SettingsDialog::makeConnections() {
        connect(ui->DialogButtonBox->button(QDialogButtonBox::Discard), SIGNAL(clicked(bool)), this, SLOT(onDiscardClicked()));
        connect(ui->DialogButtonBox->button(QDialogButtonBox::RestoreDefaults), SIGNAL(clicked(bool)), this, SLOT(onResetClicked()));
        connect(ui->DialogButtonBox, SIGNAL(accepted()), this, SLOT(onSaveClicked()));
        connect(ui->SelectFolderButton, SIGNAL(clicked(bool)), this, SLOT(onPathPushButtonClicked()));
    }

    void SettingsDialog::onDiscardClicked() {
        QDialog::reject();
    }

    void SettingsDialog::onSaveClicked() {
        Settings->setWorkingDirectory(ui->DeistinationLineEdit->text().toStdString());

        Settings->setServerPath(ui->ServerAdressLineEdit->text().toStdString());
        Settings->setUsername(ui->UsernameLineEdit->text().toStdString());
        Settings->setPassword(ui->PasswordLineEdit->text().toStdString());

        Settings->saveData();
        QDialog::accept();
    }

    void SettingsDialog::readSettings() {
        ui->DeistinationLineEdit->setText(QString::fromStdString(Settings->workingDirectory()));
        ui->PasswordLineEdit->setText(QString::fromStdString(Settings->password()));
        ui->UsernameLineEdit->setText(QString::fromStdString(Settings->username()));
        ui->ServerAdressLineEdit->setText(QString::fromStdString(Settings->serverPath()));
    }

    void SettingsDialog::onResetClicked() {
        Settings->initWithDefaults();
        readSettings();
    }

    void SettingsDialog::onPathPushButtonClicked() {
        QString filepath = QFileDialog::getExistingDirectory(this, tr("Search for Working Directory"), QString::fromStdString(Settings->workingDirectory()));
        ui->DeistinationLineEdit->setText(filepath);
    }
}