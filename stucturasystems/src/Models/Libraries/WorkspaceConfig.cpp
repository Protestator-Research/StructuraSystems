//
// Created by Moritz Herzog on 24.10.26.
//

#include "WorkspaceConfig.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>

namespace StructuraSystems::Client {
    namespace {
        const auto FOLDER_NAME = QStringLiteral(".structurasystems");
        const auto FILE_NAME = QStringLiteral("workspace.json");

#ifdef Q_OS_WIN
        constexpr Qt::CaseSensitivity PATH_CASE = Qt::CaseInsensitive;
#else
        constexpr Qt::CaseSensitivity PATH_CASE = Qt::CaseSensitive;
#endif
    }

    QString WorkspaceConfig::relativeFilePath() {
        return FOLDER_NAME + QLatin1Char('/') + FILE_NAME;
    }

    QString WorkspaceConfig::normalizePath(const QString &path) {
        auto normalized = QDir::cleanPath(QString(path).replace(QLatin1Char('\\'), QLatin1Char('/')));
        return normalized;
    }

    bool WorkspaceConfig::samePath(const QString &left, const QString &right) {
        return normalizePath(left).compare(normalizePath(right), PATH_CASE) == 0;
    }

    std::optional<int> WorkspaceConfig::levelFor(const QString &file) const {
        const auto exact = Levels.constFind(file);
        if (exact != Levels.constEnd())
            return exact.value();
        for (auto it = Levels.constBegin(); it != Levels.constEnd(); ++it)
            if (samePath(it.key(), file))
                return it.value();
        return std::nullopt;
    }

    void WorkspaceConfig::setLevel(const QString &file, int level) {
        removeLevel(file);
        Levels.insert(normalizePath(file), level);
    }

    bool WorkspaceConfig::removeLevel(const QString &file) {
        bool removed = false;
        for (auto it = Levels.begin(); it != Levels.end();) {
            if (samePath(it.key(), file)) {
                it = Levels.erase(it);
                removed = true;
            } else {
                ++it;
            }
        }
        return removed;
    }

    QString WorkspaceConfig::preferredFor(const QString &package) const {
        return PreferredSources.value(package);
    }

    WorkspaceConfig WorkspaceConfig::load(const QString &folder) {
        WorkspaceConfig config;
        if (folder.isEmpty())
            return config;

        QFile file(QDir(folder).filePath(relativeFilePath()));
        if (!file.open(QIODevice::ReadOnly))
            return config;
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            return config;

        const auto root = document.object();
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (it.key() == QLatin1String("levels") && it.value().isObject()) {
                const auto levels = it.value().toObject();
                for (auto level = levels.begin(); level != levels.end(); ++level)
                    if (level.value().isDouble())
                        config.Levels.insert(normalizePath(level.key()), level.value().toInt());
            } else if (it.key() == QLatin1String("preferredSources") && it.value().isObject()) {
                const auto preferred = it.value().toObject();
                for (auto source = preferred.begin(); source != preferred.end(); ++source)
                    if (source.value().isString())
                        config.PreferredSources.insert(source.key(), normalizePath(source.value().toString()));
            } else if (it.key() != QLatin1String("version")) {
                config.Unknown.insert(it.key(), it.value());
            }
        }
        return config;
    }

    bool WorkspaceConfig::save(const QString &folder) const {
        if (folder.isEmpty())
            return false;

        const QDir directory(folder);
        const auto path = directory.filePath(relativeFilePath());
        if (isEmpty()) {
            // Nothing to remember: do not leave an empty configuration behind.
            const bool removed = !QFile::exists(path) || QFile::remove(path);
            directory.rmdir(FOLDER_NAME);   // only succeeds if the folder is empty
            return removed;
        }

        if (!directory.mkpath(FOLDER_NAME))
            return false;

        // Sorted keys keep the file stable, so that it diffs well in version control.
        QJsonObject levels;
        auto levelKeys = Levels.keys();
        std::sort(levelKeys.begin(), levelKeys.end());
        for (const auto &key : std::as_const(levelKeys))
            levels.insert(key, Levels.value(key));
        QJsonObject preferred;
        auto preferredKeys = PreferredSources.keys();
        std::sort(preferredKeys.begin(), preferredKeys.end());
        for (const auto &key : std::as_const(preferredKeys))
            preferred.insert(key, PreferredSources.value(key));

        QJsonObject root = Unknown;
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("levels"), levels);
        root.insert(QStringLiteral("preferredSources"), preferred);

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return file.commit();
    }
}
