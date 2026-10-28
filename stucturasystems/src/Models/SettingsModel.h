//
// Created by Moritz Herzog on 23.04.25.
//

#ifndef STRUCTURASYSTEMS_SETTINGSMODEL_H
#define STRUCTURASYSTEMS_SETTINGSMODEL_H

#include <QSettings>
#include <functional>
#include <string>

#include "../Services/CredentialStore.h"

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

        /**
         * The password is loaded lazily: the first call reads it from the OS keychain (and migrates a legacy
         * plaintext entry), so constructing the model never touches the keychain.
         */
        std::string password();
        void setPassword(std::string password);
        /** true once the password has been loaded or set, i.e. password() will not access the keychain anymore. */
        bool passwordLoaded() const;
        /**
         * true while password() reads the keychain. The read spins an event loop, so password() can be called
         * re-entrantly; it then returns an empty string that must not be treated as the real password.
         */
        bool passwordLoading() const;
        /** Called whenever passwordLoading() changes. */
        void setPasswordLoadingCallback(std::function<void()> callback);

        /** If false the password is only kept in memory and removed from the keychain on save. */
        bool rememberPassword();
        void setRememberPassword(bool rememberPassword);

        /** false if the OS keychain could not be used by the last access. */
        bool keychainAvailable();

        void initWithDefaults();
        void saveData();
    private:
        void readFromMemory();
        /** Moves the working directory from the legacy "GENERAL" group (INI root section) to WORKSPACE. */
        void migrateLegacyWorkingDirectory();
        /** Loads the password from the keychain on first use, including the migration of the legacy plaintext entry. */
        void ensurePasswordLoaded();
        /** Does the actual work of ensurePasswordLoaded(); runs with PasswordLoading set. */
        void loadPassword();
        /** Removes the legacy plaintext password from QSettings. */
        void removeLegacyPassword();
        /** Writes / deletes the keychain entries so they match the in-memory state. Only called if something changed. */
        void syncKeychain();
        /** Account key of the entry that was loaded from / last written to the keychain. */
        std::string loadedAccount() const;

        std::string WorkingDirectory;
        std::string ServerPath;
        std::string Username;
        std::string Password;
        bool RememberPassword = true;

        CredentialStore Credentials;
        /** Username, server path and remember flag the keychain currently reflects (last load / successful save). */
        std::string LoadedUsername;
        std::string LoadedServerPath;
        bool LoadedRememberPassword = true;
        /** The password was loaded or set. */
        bool PasswordLoaded = false;
        /** A keychain read is in progress. */
        bool PasswordLoading = false;
        std::function<void()> PasswordLoadingCallback;
        /** Password is trustworthy: not the result of a failed keychain read. Only then it may overwrite or delete entries. */
        bool PasswordKnown = false;
        /** The password was changed in memory and still has to be written to the keychain. */
        bool PasswordDirty = false;

        QSettings Settings;
        const std::string WORKSPACE_SETTINGS_GROUP_NAME = "WORKSPACE";
        const std::string WORKING_DIRECTORY_MAP_ENTRY = "WORKING_DIR";
        const std::string CONNECTION_SETTINGS_GROUP_NAME = "CONNECTION";
        const std::string SERVER_PATH_MAP_ENTRY = "SERVER_PATH";
        const std::string USERNAME_MAP_ENTRY = "USERNAME";
        /** Legacy plaintext entry, only read for the migration to the keychain. */
        const std::string PASSWORD_MAP_ENTRY = "PASSWORD";
        const std::string REMEMBER_PASSWORD_MAP_ENTRY = "REMEMBER_PASSWORD";


    };
}

#endif //STRUCTURASYSTEMS_SETTINGSMODEL_H
