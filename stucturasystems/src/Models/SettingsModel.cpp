//
// Created by Moritz Herzog on 23.04.25.
//

#include "SettingsModel.h"

#include <QDebug>
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
        ensurePasswordLoaded();
        return Password;
    }

    void SettingsModel::setPassword(std::string password) {
        if (PasswordLoading)
            return; // The real value is not known yet, it must not be replaced (and later written) by a guess.
        if (PasswordLoaded && PasswordKnown && Password == password)
            return;
        Password = password;
        PasswordLoaded = true;
        PasswordKnown = true;
        PasswordDirty = true;
    }

    bool SettingsModel::passwordLoaded() const {
        return PasswordLoaded;
    }

    bool SettingsModel::passwordLoading() const {
        return PasswordLoading;
    }

    void SettingsModel::setPasswordLoadingCallback(std::function<void()> callback) {
        PasswordLoadingCallback = std::move(callback);
    }

    bool SettingsModel::rememberPassword() {
        return RememberPassword;
    }

    void SettingsModel::setRememberPassword(bool rememberPassword) {
        RememberPassword = rememberPassword;
    }

    bool SettingsModel::keychainAvailable() {
        return Credentials.isAvailable();
    }

    std::string SettingsModel::loadedAccount() const {
        if (LoadedUsername.empty() && LoadedServerPath.empty())
            return "";
        return CredentialStore::accountKey(LoadedUsername, LoadedServerPath);
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

    void SettingsModel::removeLegacyPassword() {
        const auto key = QString::fromStdString(CONNECTION_SETTINGS_GROUP_NAME + "/" + PASSWORD_MAP_ENTRY);
        if (!Settings.contains(key))
            return;
        Settings.remove(key);
        Settings.sync();
    }

    void SettingsModel::ensurePasswordLoaded() {
        // The keychain read spins an event loop, so this can be entered again meanwhile. While loading, the
        // password reads as empty and passwordLoading() is true; nothing is written to the keychain in that time.
        if (PasswordLoaded || PasswordLoading)
            return;
        PasswordLoading = true;
        if (PasswordLoadingCallback)
            PasswordLoadingCallback();
        loadPassword();
        PasswordLoaded = true;
        PasswordLoading = false;
        if (PasswordLoadingCallback)
            PasswordLoadingCallback();
    }

    void SettingsModel::loadPassword() {
        Password = "";

        const auto legacyKey = QString::fromStdString(CONNECTION_SETTINGS_GROUP_NAME + "/" + PASSWORD_MAP_ENTRY);
        const bool hasLegacy = Settings.contains(legacyKey);
        const auto legacyPassword = Settings.value(legacyKey).toString().toStdString();
        const auto account = loadedAccount();

        if (RememberPassword && !account.empty()) {
            const auto result = Credentials.readPassword(account);
            if (result.Status == CredentialStore::ReadStatus::Found) {
                // The keychain is newer than a legacy plaintext entry that might still exist.
                Password = result.Password;
                PasswordKnown = true;
                if (hasLegacy)
                    removeLegacyPassword();
                return;
            }
            if (result.Status == CredentialStore::ReadStatus::Error) {
                // A failed read is not "no password": it must never lead to an entry being overwritten or deleted.
                if (hasLegacy && !legacyPassword.empty()) {
                    Password = legacyPassword;
                    PasswordKnown = true;
                    PasswordDirty = true; // Written to the keychain with the next save.
                } else {
                    PasswordKnown = false;
                }
                return;
            }
        }

        // No keychain entry (or the keychain is not used): only a legacy plaintext password can be left.
        PasswordKnown = true;
        if (!hasLegacy)
            return;
        if (legacyPassword.empty()) {
            removeLegacyPassword();
            return;
        }
        Password = legacyPassword;
        if (!RememberPassword) {
            // The password must not be persisted: keep it in memory only.
            removeLegacyPassword();
        } else if (account.empty()) {
            // Nothing to attach the password to yet; the legacy entry stays until a save with a username / server.
            PasswordDirty = true;
        } else if (Credentials.writePassword(account, legacyPassword)) {
            removeLegacyPassword();
        } else {
            qWarning() << "Could not migrate the stored password to the keychain, keeping the legacy entry.";
            PasswordDirty = true;
        }
    }

    void SettingsModel::readFromMemory() {
        migrateLegacyWorkingDirectory();
        Settings.beginGroup(WORKSPACE_SETTINGS_GROUP_NAME);
        WorkingDirectory = Settings.value(WORKING_DIRECTORY_MAP_ENTRY).toString().toStdString();
        Settings.endGroup();
        Settings.beginGroup(CONNECTION_SETTINGS_GROUP_NAME);
        ServerPath = Settings.value(SERVER_PATH_MAP_ENTRY).toString().toStdString();
        Username = Settings.value(USERNAME_MAP_ENTRY).toString().toStdString();
        RememberPassword = Settings.value(REMEMBER_PASSWORD_MAP_ENTRY, true).toBool();
        Settings.endGroup();
        LoadedUsername = Username;
        LoadedServerPath = ServerPath;
        LoadedRememberPassword = RememberPassword;
        // The password itself is loaded lazily, see ensurePasswordLoaded().
    }

    void SettingsModel::initWithDefaults() {
        WorkingDirectory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdString();
        ServerPath = "";
        Username = "";
        Password = "";
        RememberPassword = true;
        // LoadedUsername / LoadedServerPath stay as they are, so the next save removes the previous keychain entry.
        PasswordLoaded = true;
        PasswordKnown = true;
        PasswordDirty = true;
    }

    void SettingsModel::syncKeychain() {
        if (PasswordLoading)
            return; // Do not write or delete anything based on a value that is not loaded yet.
        const auto oldAccount = loadedAccount();
        const auto newAccount = Username.empty() && ServerPath.empty() ? std::string()
                                                                       : CredentialStore::accountKey(Username, ServerPath);
        const bool accountChanged = oldAccount != newAccount;
        const bool rememberChanged = RememberPassword != LoadedRememberPassword;
        if (!PasswordDirty && !accountChanged && !rememberChanged)
            return; // Nothing changed: do not touch the keychain.

        // The current value has to be known before it is moved to another account.
        ensurePasswordLoaded();

        bool newAccountDone = true;
        if (!newAccount.empty()) {
            if (!RememberPassword)
                newAccountDone = Credentials.deletePassword(newAccount);
            else if (!PasswordKnown)
                newAccountDone = false; // The read failed: leave the stored entries alone, retry later.
            else if (!Password.empty())
                newAccountDone = Credentials.writePassword(newAccount, Password);
            else if (PasswordDirty)
                newAccountDone = Credentials.deletePassword(newAccount);
        }

        bool allDone = newAccountDone;
        // The entry of the previous username / server must not stay behind.
        if (newAccountDone && !oldAccount.empty() && accountChanged)
            allDone = Credentials.deletePassword(oldAccount);

        if (newAccountDone && !newAccount.empty())
            removeLegacyPassword(); // An old plaintext value must never overwrite the keychain at the next start.
        if (allDone) {
            LoadedUsername = Username;
            LoadedServerPath = ServerPath;
            LoadedRememberPassword = RememberPassword;
            PasswordDirty = false;
        }
    }

    void SettingsModel::saveData() {
        Settings.beginGroup(WORKSPACE_SETTINGS_GROUP_NAME);
        Settings.setValue(WORKING_DIRECTORY_MAP_ENTRY, QString::fromStdString(WorkingDirectory));
        Settings.endGroup();
        Settings.beginGroup(CONNECTION_SETTINGS_GROUP_NAME);
        Settings.setValue(SERVER_PATH_MAP_ENTRY, QString::fromStdString(ServerPath));
        Settings.setValue(USERNAME_MAP_ENTRY, QString::fromStdString(Username));
        Settings.setValue(REMEMBER_PASSWORD_MAP_ENTRY, RememberPassword);
        Settings.endGroup();

        syncKeychain();
    }
}
