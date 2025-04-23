//
// Created by Moritz Herzog on 23.04.25.
//

#ifndef STRUCTURASYSTEMS_SETTINGSMODEL_H
#define STRUCTURASYSTEMS_SETTINGSMODEL_H

#include <QSettings>
#include <string>

namespace StructuraSystems::Client {
    class SettingsModel {
    public:
        SettingsModel();
        ~SettingsModel();

        std::string workingDirectory();
        void setWorkingDirectory(std::string workingDirectory);

        std::string serverPath();
        void setServerPath(std::string serverPath);

        std::string username();
        void setUsername(std::string username);

        std::string password();
        void setPassword(std::string password);

        void initWithDefaults();
        void saveData();
    private:
        void readFromMemory();

        std::string WorkingDirectory;
        std::string ServerPath;
        std::string Username;
        std::string Password;


        QSettings Settings;
        const std::string GENERAL_SETTINGS_GROUP_NAME = "GENERAL";
        const std::string WORKING_DIRECTORY_MAP_ENTRY = "WORKING_DIR";
        const std::string CONNECTION_SETTINGS_GROUP_NAME = "CONNECTION";
        const std::string SERVER_PATH_MAP_ENTRY = "SERVER_PATH";
        const std::string USERNAME_MAP_ENTRY = "USERNAME";
        const std::string PASSWORD_MAP_ENTRY = "PASSWORD";


    };
}

#endif //STRUCTURASYSTEMS_SETTINGSMODEL_H
