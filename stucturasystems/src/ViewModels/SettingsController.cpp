//
// Created by Moritz Herzog on 07.10.26.
//

#include "SettingsController.h"

#include <QStandardPaths>

namespace StructuraSystems::Client {
    SettingsController::SettingsController(QObject *parent) : QObject(parent) {
        AppearanceSettings.beginGroup(APPEARANCE_SETTINGS_GROUP_NAME);
        ThemeMode = qBound(0, AppearanceSettings.value(THEME_MODE_ENTRY, 0).toInt(), 2);
        AppearanceSettings.endGroup();
    }

    QString SettingsController::workingDirectory() const {
        // SettingsModel getters are not const.
        return QString::fromStdString(const_cast<SettingsModel &>(Settings).workingDirectory());
    }

    void SettingsController::setWorkingDirectory(const QString &workingDirectory) {
        if (workingDirectory == this->workingDirectory())
            return;
        Settings.setWorkingDirectory(workingDirectory.toStdString());
        emit workingDirectoryChanged();
    }

    QUrl SettingsController::workingDirectoryUrl() const {
        const auto directory = workingDirectory();
        return directory.isEmpty() ? QUrl() : QUrl::fromLocalFile(directory);
    }

    void SettingsController::setWorkingDirectoryUrl(const QUrl &url) {
        setWorkingDirectory(url.isLocalFile() ? url.toLocalFile() : url.toString());
    }

    QString SettingsController::serverPath() const {
        return QString::fromStdString(const_cast<SettingsModel &>(Settings).serverPath());
    }

    void SettingsController::setServerPath(const QString &serverPath) {
        if (serverPath == this->serverPath())
            return;
        Settings.setServerPath(serverPath.toStdString());
        emit serverPathChanged();
    }

    QString SettingsController::username() const {
        return QString::fromStdString(const_cast<SettingsModel &>(Settings).username());
    }

    void SettingsController::setUsername(const QString &username) {
        if (username == this->username())
            return;
        Settings.setUsername(username.toStdString());
        emit usernameChanged();
    }

    QString SettingsController::password() const {
        return QString::fromStdString(const_cast<SettingsModel &>(Settings).password());
    }

    void SettingsController::setPassword(const QString &password) {
        if (password == this->password())
            return;
        Settings.setPassword(password.toStdString());
        emit passwordChanged();
    }

    int SettingsController::themeMode() const {
        return ThemeMode;
    }

    void SettingsController::setThemeMode(int themeMode) {
        themeMode = qBound(0, themeMode, 2);
        if (themeMode == ThemeMode)
            return;
        ThemeMode = themeMode;
        emit themeModeChanged();
    }

    void SettingsController::save() {
        // TODO: store password in system keychain
        Settings.saveData();

        AppearanceSettings.beginGroup(APPEARANCE_SETTINGS_GROUP_NAME);
        AppearanceSettings.setValue(THEME_MODE_ENTRY, ThemeMode);
        AppearanceSettings.endGroup();
        AppearanceSettings.sync();

        emit saved();
    }

    void SettingsController::initWithDefaultsIfEmpty() {
        if (!workingDirectory().isEmpty())
            return;
        // Only the working directory is defaulted, so already configured connection data is not lost.
        setWorkingDirectory(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }
}
