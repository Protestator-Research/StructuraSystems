//
// Created by Moritz Herzog on 23.04.25.
//

#include "SettingsModel.h"
#include <QStandardPaths>
#include <QString>

namespace StructuraSystems::Client {
    SettingsModel::SettingsModel() {
        readFromMemory();
    }

    SettingsModel::~SettingsModel() {

    }

    std::string SettingsModel::workingDirectory() {
        return WorkingDirectory;
    }

    void SettingsModel::setWorkingDirectory(std::string workingDirectory) {
        WorkingDirectory = workingDirectory;
    }

    std::string SettingsModel::serverPath() {
        return ServerPath;
    }

    void SettingsModel::setServerPath(std::string serverPath) {
        ServerPath = serverPath;
    }

    std::string SettingsModel::username() {
        return Username;
    }

    void SettingsModel::setUsername(std::string username) {
        Username = username;
    }

    std::string SettingsModel::password() {
        return Password;
    }

    void SettingsModel::setPassword(std::string password) {
        Password = password;
    }

    void SettingsModel::migrateLegacyWorkingDirectory() {
        const auto newKey = QString::fromStdString(WORKSPACE_SETTINGS_GROUP_NAME + "/" + WORKING_DIRECTORY_MAP_ENTRY);
        const auto entry = QString::fromStdString(WORKING_DIRECTORY_MAP_ENTRY);
        // The old group "GENERAL" is the root section of an INI file, so the value ended up at the root level
        // (or, on other backends, below "GENERAL/").
        const QString oldKeys[] = {entry, QStringLiteral("GENERAL/") + entry};

        bool migrated = false;
        for (const auto &oldKey : oldKeys) {
            if (!Settings.contains(oldKey))
                continue;
            if (!Settings.contains(newKey)) {
                Settings.setValue(newKey, Settings.value(oldKey));
                migrated = true;
            }
            Settings.remove(oldKey);
        }
        if (migrated)
            Settings.sync();
    }

    void SettingsModel::readFromMemory() {
        migrateLegacyWorkingDirectory();
        Settings.beginGroup(WORKSPACE_SETTINGS_GROUP_NAME);
        WorkingDirectory = Settings.value(WORKING_DIRECTORY_MAP_ENTRY).toString().toStdString();
        Settings.endGroup();
        Settings.beginGroup(CONNECTION_SETTINGS_GROUP_NAME);
        ServerPath = Settings.value(SERVER_PATH_MAP_ENTRY).toString().toStdString();
        Username = Settings.value(USERNAME_MAP_ENTRY).toString().toStdString();
        Password = Settings.value(PASSWORD_MAP_ENTRY).toString().toStdString();
        Settings.endGroup();
    }

    void SettingsModel::initWithDefaults() {
        WorkingDirectory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdString();
        ServerPath = "";
        Username = "";
        Password = "";
    }

    void SettingsModel::saveData() {
        Settings.beginGroup(WORKSPACE_SETTINGS_GROUP_NAME);
        Settings.setValue(WORKING_DIRECTORY_MAP_ENTRY, QString::fromStdString(WorkingDirectory));
        Settings.endGroup();
        Settings.beginGroup(CONNECTION_SETTINGS_GROUP_NAME);
        Settings.setValue(SERVER_PATH_MAP_ENTRY, QString::fromStdString(ServerPath));
        Settings.setValue(USERNAME_MAP_ENTRY, QString::fromStdString(Username));
        Settings.setValue(PASSWORD_MAP_ENTRY, QString::fromStdString(Password));
        Settings.endGroup();
    }
}
