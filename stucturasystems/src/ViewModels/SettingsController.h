//
// Created by Moritz Herzog on 07.10.26.
//

#ifndef STRUCTURASYSTEMS_SETTINGSCONTROLLER_H
#define STRUCTURASYSTEMS_SETTINGSCONTROLLER_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "../Models/SettingsModel.h"

namespace StructuraSystems::Client {
    /**
     * QML facing wrapper around the SettingsModel. Changes of the properties are held in memory until save() is called.
     */
    class SettingsController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("SettingsController is owned by AppController.")
        Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY workingDirectoryChanged)
        Q_PROPERTY(QUrl workingDirectoryUrl READ workingDirectoryUrl WRITE setWorkingDirectoryUrl NOTIFY workingDirectoryChanged)
        Q_PROPERTY(QString serverPath READ serverPath WRITE setServerPath NOTIFY serverPathChanged)
        Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
        Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
        /** 0 = follow system, 1 = light, 2 = dark */
        Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    public:
        explicit SettingsController(QObject *parent = nullptr);
        ~SettingsController() override = default;

        [[nodiscard]] QString workingDirectory() const;
        void setWorkingDirectory(const QString &workingDirectory);

        [[nodiscard]] QUrl workingDirectoryUrl() const;
        void setWorkingDirectoryUrl(const QUrl &url);

        [[nodiscard]] QString serverPath() const;
        void setServerPath(const QString &serverPath);

        [[nodiscard]] QString username() const;
        void setUsername(const QString &username);

        [[nodiscard]] QString password() const;
        void setPassword(const QString &password);

        [[nodiscard]] int themeMode() const;
        void setThemeMode(int themeMode);

        /** Persists all settings. */
        Q_INVOKABLE void save();

        /** Fills the working directory with the default location if none has been configured yet. */
        void initWithDefaultsIfEmpty();

    signals:
        void workingDirectoryChanged();
        void serverPathChanged();
        void usernameChanged();
        void passwordChanged();
        void themeModeChanged();
        /** Emitted after save() persisted the settings. */
        void saved();

    private:
        SettingsModel Settings;
        QSettings AppearanceSettings;
        int ThemeMode = 0;

        const QString APPEARANCE_SETTINGS_GROUP_NAME = "APPEARANCE";
        const QString THEME_MODE_ENTRY = "THEME_MODE";
    };
}

#endif //STRUCTURASYSTEMS_SETTINGSCONTROLLER_H
