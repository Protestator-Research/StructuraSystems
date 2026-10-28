//
// Created by Moritz Herzog on 22.10.26.
//

#ifndef STRUCTURASYSTEMS_SETTINGSMIGRATION_H
#define STRUCTURASYSTEMS_SETTINGSMIGRATION_H

#include <QList>
#include <QString>

namespace StructuraSystems::Client {
    /** Location of settings written by a former organization / application name. */
    struct LegacySettingsSource {
        /** Organization as passed to QSettings (on macOS the former organization domain string). */
        QString Organization;
        QString Application;
    };

    /**
     * Copies the settings of the former organizations (the former "Working Group Cyber Physical Systems" and, on
     * macOS, the former domain of the current organization) to the current location (see main.cpp). Values that
     * already exist are kept, older values only fill the gaps. After a successful copy the old settings are removed
     * and a marker is stored, so the migration runs only once.
     * Must be called after the application name, organization and domain are set and before any QSettings is used.
     */
    void migrateLegacyOrganizationSettings();

    /**
     * Same as migrateLegacyOrganizationSettings() for arbitrary sources.
     * @param sources Former locations, newest first: values of earlier entries win over later ones.
     */
    void migrateOrganizationSettings(const QList<LegacySettingsSource> &sources);
}

#endif //STRUCTURASYSTEMS_SETTINGSMIGRATION_H
