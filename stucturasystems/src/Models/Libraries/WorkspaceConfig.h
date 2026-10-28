//
// Created by Moritz Herzog on 24.10.26.
//

#ifndef STRUCTURASYSTEMS_WORKSPACECONFIG_H
#define STRUCTURASYSTEMS_WORKSPACECONFIG_H

#include <optional>

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace StructuraSystems::Client {
    /**
     * The user's library settings of an opened folder, stored in `<folder>/.structurasystems/workspace.json`:
     * manual library levels (path to level) and the preferred source file of packages that several files define.
     * The paths are relative to the folder and use '/'; they are normalized (QDir::cleanPath) when read and set, and are
     * compared without regard to the case on Windows. Content of the file that is not known (other top level keys) is kept
     * when the file is written again.
     */
    struct WorkspaceConfig {
        QHash<QString, int> Levels;                 ///< file path -> manually set library level
        QHash<QString, QString> PreferredSources;   ///< package name -> file path
        QJsonObject Unknown;                        ///< top level entries of the file that are not understood, kept as they are

        /** Folder name and file name of the configuration, relative to the opened folder. */
        static QString relativeFilePath();

        /** Normalized form of a relative path: cleaned, '/' as separator. */
        static QString normalizePath(const QString &path);
        /** True if both paths are the same file (normalized, case-insensitive on Windows). */
        static bool samePath(const QString &left, const QString &right);

        /** Reads the configuration of the folder. A missing or unreadable file gives an empty configuration. */
        static WorkspaceConfig load(const QString &folder);

        /**
         * Writes the configuration. An empty configuration (without unknown content) removes the file (and the folder, if
         * it is empty).
         * @return false if the file could not be written.
         */
        bool save(const QString &folder) const;

        [[nodiscard]] std::optional<int> levelFor(const QString &file) const;
        void setLevel(const QString &file, int level);
        /** @return true if there was a level. */
        bool removeLevel(const QString &file);
        /** The preferred file of a package, empty if there is none. */
        [[nodiscard]] QString preferredFor(const QString &package) const;

        [[nodiscard]] bool isEmpty() const { return Levels.isEmpty() && PreferredSources.isEmpty() && Unknown.isEmpty(); }
    };
}

#endif //STRUCTURASYSTEMS_WORKSPACECONFIG_H
