//
// Created by Moritz Herzog on 22.10.26.
//

#include "SettingsMigration.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include <memory>

namespace StructuraSystems::Client {
    namespace {
        const QString MIGRATION_DONE_KEY = QStringLiteral("MIGRATION/LEGACY_ORGANIZATION_DONE");
        const QString PASSWORD_KEY = QStringLiteral("CONNECTION/PASSWORD");
        const QString USERNAME_KEY = QStringLiteral("CONNECTION/USERNAME");
        const QString SERVER_PATH_KEY = QStringLiteral("CONNECTION/SERVER_PATH");

        /** Newest source first. */
        QList<LegacySettingsSource> legacySources() {
            const QString application = QStringLiteral("Structura Systems");
            QList<LegacySettingsSource> sources;
#ifdef Q_OS_MACOS
            // On macOS QSettings derives the plist name from the organization *domain* via comify() in Qt's
            // qsettings_mac.cpp: the organization string is turned into a reversed domain, the application is appended.
            // - Former domain of this application, "https://www.protestator-research.com" (ends in ".com", so only
            //   '/' becomes ' '):  com.protestator-research.https:  www.Structura Systems.plist
            // - Former CPS domain "https://cps.cs.rptu.de/" (ends in '/', so every non-alphanumeric becomes '-' and
            //   ".com" is appended):  com.https-cps-cs-rptu-de.Structura Systems.plist
            // Current domain "https://protestator-research.com":  com.https:  protestator-research.Structura Systems.plist
            sources.append({QStringLiteral("https://www.protestator-research.com"), application});
            sources.append({QStringLiteral("https://cps.cs.rptu.de/"), application});
#else
            // Windows registry / Linux INI files use the organization name.
            sources.append({QStringLiteral("Working Group Cyber Physical Systems"), application});
#endif
            return sources;
        }

        /**
         * Removes the storage of the cleared old settings where the platform allows it. On Windows QSettings cannot
         * delete the empty registry keys and on macOS the empty plist stays; both are harmless.
         */
        void removeEmptyLegacyStorage(QSettings &legacy) {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
            // INI file below ~/.config/<organization>/.
            if (legacy.allKeys().isEmpty()) {
                QFile::remove(legacy.fileName());
                QDir().rmdir(QFileInfo(legacy.fileName()).absolutePath()); // Only succeeds if the directory is empty.
            }
#else
            Q_UNUSED(legacy)
#endif
        }

        /**
         * Copies the keys of the source that are missing in the current settings. The password is bound to the account
         * (username and server): it is only taken over if it belongs to the account that ended up in the current settings.
         */
        void copyMissingKeys(QSettings &legacy, QSettings &current) {
            for (const auto &key : legacy.allKeys()) {
                if (key != PASSWORD_KEY && !current.contains(key))
                    current.setValue(key, legacy.value(key));
            }

            if (!legacy.contains(PASSWORD_KEY) || current.contains(PASSWORD_KEY))
                return;
            const auto username = legacy.value(USERNAME_KEY).toString();
            const auto serverPath = legacy.value(SERVER_PATH_KEY).toString();
            const bool sameAccount = (!username.isEmpty() || !serverPath.isEmpty()) &&
                                     current.value(USERNAME_KEY).toString() == username &&
                                     current.value(SERVER_PATH_KEY).toString() == serverPath;
            // SettingsModel moves this plaintext password to the keychain on first use and removes it from the settings.
            if (sameAccount)
                current.setValue(PASSWORD_KEY, legacy.value(PASSWORD_KEY));
        }
    }

    void migrateLegacyOrganizationSettings() {
        migrateOrganizationSettings(legacySources());
    }

    void migrateOrganizationSettings(const QList<LegacySettingsSource> &sources) {
        QSettings current;
        current.setFallbacksEnabled(false);
        if (current.value(MIGRATION_DONE_KEY, false).toBool())
            return;

        // Sources without keys (nothing to migrate or already cleared) are skipped.
        QList<std::shared_ptr<QSettings>> legacySettings;
        for (const auto &source : sources) {
            auto legacy = std::make_shared<QSettings>(QSettings::NativeFormat, QSettings::UserScope,
                                                      source.Organization, source.Application);
            legacy->setFallbacksEnabled(false);
            if (!legacy->allKeys().isEmpty())
                legacySettings.append(legacy);
        }
        if (legacySettings.isEmpty())
            return;

        for (const auto &legacy : legacySettings)
            copyMissingKeys(*legacy, current);
        current.setValue(MIGRATION_DONE_KEY, true);
        current.sync();
        if (current.status() != QSettings::NoError) {
            qWarning() << "Could not migrate the settings of the former organization, keeping them.";
            return;
        }

        for (const auto &legacy : legacySettings) {
            legacy->clear();
            legacy->sync();
            if (legacy->status() != QSettings::NoError) {
                qWarning() << "The settings of the former organization were copied but could not be removed.";
                continue;
            }
            removeEmptyLegacyStorage(*legacy);
        }
    }
}
