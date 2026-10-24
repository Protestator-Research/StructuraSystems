//
// Created by Moritz Herzog on 24.10.26.
//

#include "LibraryWorkspace.h"

#include <algorithm>
#include <functional>
#include <set>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace StructuraSystems::Client {
    namespace {
        /** Files of the standard library that are KerML. The resources only keep an alias without suffix, so the language is listed here. */
        const QSet<QString> &kermlAliases() {
            static const QSet<QString> aliases = {
                "Collections", "ScalarValues", "VectorValues",
                "BaseFunctions", "BooleanFunctions", "CollectionFunctions", "ComplexFunctions", "ControlFunctions", "DataFunctions",
                "IntegerFunctions", "NaturalFunctions", "NumericalFunctions", "OccurrenceFunctions", "RationalFunctions",
                "RealFunctions", "ScalarFunctions", "SequenceFunctions", "StringFunctions", "TrigFunctions", "VectorFunctions",
                "Base", "Clocks", "ControlPerformances", "FeatureReferencingPerformances", "KerML", "Links", "Metaobjects", "Objects",
                "Observation", "Occurrences", "Performances", "SpatialFrames", "StatePerformances", "Transfers",
                "TransitionPerformances", "Triggers"};
            return aliases;
        }

        bool isHidden(const QString &relativePath) {
            const auto segments = relativePath.split(QLatin1Char('/'));
            return std::any_of(segments.begin(), segments.end() - 1,
                               [](const QString &segment) { return segment.startsWith(QLatin1Char('.')); });
        }

        int clampLevel(int level) {
            return (std::clamp)(level, 1, LibraryWorkspace::MaxLibraryLevel);
        }
    }

    LibraryWorkspace::LibraryWorkspace() = default;

    LibraryWorkspace::~LibraryWorkspace() = default;

    const std::vector<StandardSource> &LibraryWorkspace::standardLibrary() {
        static const std::vector<StandardSource> library = []() {
            std::vector<StandardSource> sources;
            QStringList aliases;
            QDirIterator iterator(QStringLiteral(":/sysml"), QDir::Files);
            while (iterator.hasNext()) {
                iterator.next();
                aliases.append(iterator.fileName());
            }
            aliases.sort();
            for (const auto &alias : std::as_const(aliases)) {
                QFile file(QStringLiteral(":/sysml/") + alias);
                if (!file.open(QIODevice::ReadOnly))
                    continue;
                StandardSource source;
                source.KerML = kermlAliases().contains(alias);
                source.Name = (alias + (source.KerML ? QStringLiteral(".kerml") : QStringLiteral(".sysml"))).toStdString();
                source.Text = file.readAll().toStdString();
                sources.push_back(std::move(source));
            }
            return sources;
        }();
        return library;
    }

    // ------------------------------------------------------------------ folder

    void LibraryWorkspace::setRoot(const QString &folder) {
        std::lock_guard lock(Mutex);
        Root = folder.isEmpty() ? QString() : QDir::cleanPath(folder);
        {
            std::lock_guard configLock(ConfigMutex);
            ConfigRoot = Root;
            Config = WorkspaceConfig::load(Root);
            SaveSucceeded = true;
        }
        Files.clear();
        PackageFiles.clear();
        IndexBuilt = false;
        Dirty = true;
        LastValidationValid = false;
        LevelsValid = false;
        Levels.clear();
        ++Generation;
    }

    QString LibraryWorkspace::root() const {
        std::lock_guard lock(Mutex);
        return Root;
    }

    quint64 LibraryWorkspace::generation() const {
        std::lock_guard lock(Mutex);
        return Generation;
    }

    // ------------------------------------------------------------------ index

    bool LibraryWorkspace::indexBuilt() const {
        std::lock_guard lock(Mutex);
        return IndexBuilt;
    }

    void LibraryWorkspace::invalidate() {
        std::lock_guard lock(Mutex);
        Dirty = true;
        ++InvalidationCount;
    }

    void LibraryWorkspace::refreshIndex() {
        ensureIndex(true);
    }

    bool LibraryWorkspace::indexStaleLocked() const {
        // The modification times are checked again from time to time, so that the index is right even without file watching.
        return !IndexBuilt || Dirty || !LastValidationValid || LastValidation.msecsTo(QDateTime::currentDateTime()) > 3000;
    }

    void LibraryWorkspace::ensureIndex(bool force) {
        {
            std::lock_guard lock(Mutex);
            if (force) {
                Dirty = true;
                ++InvalidationCount;   // a forced rescan requested during a running scan must not be lost
            }
            if (!indexStaleLocked())
                return;
        }

        // One scan at a time. Mutex is not held while the folder is read, so the user interface (setRoot, indexBuilt, ...)
        // never waits for it.
        std::lock_guard scanLock(ScanMutex);
        QString root;
        std::map<QString, IndexedFile> cache;
        quint64 generation = 0;
        quint64 invalidations = 0;
        bool first = false;
        {
            std::lock_guard lock(Mutex);
            if (!indexStaleLocked())
                return;   // another thread brought the index up to date meanwhile
            root = Root;
            cache = Files;
            generation = Generation;
            invalidations = InvalidationCount;
            first = !IndexBuilt;
        }

        ++IndexBuilds;
        bool changed = first;
        std::map<QString, IndexedFile> files;
        if (!root.isEmpty() && QDir(root).exists()) {
            const QDir directory(root);
            QDirIterator iterator(directory.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
            while (iterator.hasNext()) {
                const auto path = iterator.next();
                if (!CodeScanner::isProjectFile(path))
                    continue;
                const auto relative = directory.relativeFilePath(path);
                if (isHidden(relative))
                    continue;

                const QFileInfo info(path);
                const auto cached = cache.find(relative);
                IndexedFile entry;
                if (cached != cache.end() && cached->second.Size == info.size() &&
                    cached->second.Stamp == info.lastModified().toMSecsSinceEpoch()) {
                    entry = cached->second;
                } else {
                    entry.Size = info.size();
                    entry.Stamp = info.lastModified().toMSecsSinceEpoch();
                    ++FileReads;
                    entry.Scan = CodeScanner::scanFile(path);
                    changed = true;
                }
                files.emplace(relative, std::move(entry));
            }
        }
        if (files.size() != cache.size())
            changed = true;

        std::lock_guard lock(Mutex);
        if (generation != Generation)
            return;   // another folder was opened while this one was scanned
        if (changed) {
            Files = std::move(files);
            PackageFiles.clear();
            for (const auto &[file, entry] : Files) {
                QSet<QString> seen;
                for (const auto &package : entry.Scan.Packages) {
                    if (!seen.contains(package.Name)) {
                        seen.insert(package.Name);
                        PackageFiles[package.Name].append(file);
                    }
                }
            }
            LevelsValid = false;
        }
        IndexBuilt = true;
        Dirty = invalidations != InvalidationCount;   // a change during the scan keeps the index stale
        LastValidation = QDateTime::currentDateTime();
        LastValidationValid = true;
    }

    QStringList LibraryWorkspace::indexedFilesIfBuilt() const {
        std::lock_guard lock(Mutex);
        QStringList files;
        for (const auto &entry : Files)
            files.append(entry.first);
        return files;
    }

    QStringList LibraryWorkspace::indexedFiles() {
        ensureIndex();
        return indexedFilesIfBuilt();
    }

    QString LibraryWorkspace::chosenFileLocked(const QString &package, bool *ambiguous, QStringList *files) const {
        const auto candidates = PackageFiles.value(package);
        if (files != nullptr)
            *files = candidates;
        if (ambiguous != nullptr)
            *ambiguous = false;
        if (candidates.isEmpty())
            return {};
        const auto preferred = preferredSource(package);
        if (!preferred.isEmpty()) {
            for (const auto &candidate : candidates)
                if (WorkspaceConfig::samePath(candidate, preferred))
                    return candidate;
        }
        if (ambiguous != nullptr)
            *ambiguous = candidates.size() > 1;
        return candidates.first();
    }

    PackageLookup LibraryWorkspace::findPackage(const QString &package, bool rescanIfMissing) {
        ensureIndex();
        PackageLookup lookup;
        {
            std::lock_guard lock(Mutex);
            lookup.Chosen = chosenFileLocked(package, &lookup.Ambiguous, &lookup.Files);
        }
        if (lookup.Chosen.isEmpty() && rescanIfMissing) {
            // Before a package is reported as missing, make sure that no file was added since the index was made.
            bool recent;
            {
                std::lock_guard lock(Mutex);
                recent = LastValidationValid && LastValidation.msecsTo(QDateTime::currentDateTime()) <= 500;
            }
            if (!recent) {
                ensureIndex(true);
                std::lock_guard lock(Mutex);
                lookup.Chosen = chosenFileLocked(package, &lookup.Ambiguous, &lookup.Files);
            }
        }
        return lookup;
    }

    QStringList LibraryWorkspace::cachedPackagesOf(const QString &file) {
        std::lock_guard lock(Mutex);
        QStringList packages;
        const auto cached = Files.find(file);
        if (cached == Files.end())
            return packages;
        for (const auto &package : cached->second.Scan.Packages)
            if (!packages.contains(package.Name))
                packages.append(package.Name);
        return packages;
    }

    // ------------------------------------------------------------------ levels

    QStringList LibraryWorkspace::dependenciesLocked(const QString &file) const {
        QStringList dependencies;
        const auto entry = Files.find(file);
        if (entry == Files.end())
            return dependencies;

        QSet<QString> own;
        for (const auto &package : entry->second.Scan.Packages)
            own.insert(package.Name);
        for (const auto &reference : entry->second.Scan.References) {
            if (own.contains(reference))
                continue;
            const auto chosen = chosenFileLocked(reference);
            if (!chosen.isEmpty() && chosen != file && !dependencies.contains(chosen))
                dependencies.append(chosen);
        }
        return dependencies;
    }

    void LibraryWorkspace::computeLevelsLocked() {
        Levels.clear();
        const auto revision = ConfigRevision.load();
        const auto settings = [this]() {
            std::lock_guard configLock(ConfigMutex);
            return Config;
        }();

        std::map<QString, QStringList> dependencies;
        for (const auto &entry : Files)
            dependencies[entry.first] = dependenciesLocked(entry.first);

        // Tarjan's algorithm: the strongly connected components (cycles) are completed dependencies first.
        std::map<QString, int> index;
        std::map<QString, int> lowLink;
        std::set<QString> onStack;
        std::vector<QString> stack;
        std::vector<std::vector<QString>> components;
        int counter = 0;

        std::function<void(const QString &)> visit = [&](const QString &file) {
            index[file] = lowLink[file] = counter++;
            stack.push_back(file);
            onStack.insert(file);
            for (const auto &dependency : dependencies[file]) {
                if (!index.count(dependency)) {
                    visit(dependency);
                    lowLink[file] = (std::min)(lowLink[file], lowLink[dependency]);
                } else if (onStack.count(dependency)) {
                    lowLink[file] = (std::min)(lowLink[file], index[dependency]);
                }
            }
            if (lowLink[file] == index[file]) {
                std::vector<QString> component;
                QString member;
                do {
                    member = stack.back();
                    stack.pop_back();
                    onStack.erase(member);
                    component.push_back(member);
                } while (member != file);
                components.push_back(std::move(component));
            }
        };
        for (const auto &entry : Files)
            if (!index.count(entry.first))
                visit(entry.first);

        for (const auto &component : components) {
            const std::set<QString> members(component.begin(), component.end());
            int automatic = 1;
            for (const auto &file : component)
                for (const auto &dependency : dependencies[file])
                    if (!members.count(dependency))
                        automatic = (std::max)(automatic, Levels[dependency].Level + 1);

            for (const auto &file : component) {
                LibraryLevelInfo info;
                const auto manual = settings.levelFor(file);
                info.Overridden = manual.has_value();
                info.Level = clampLevel(manual.value_or(automatic));
                info.InCycle = component.size() > 1;
                for (const auto &package : Files[file].Scan.Packages)
                    info.DeclaresLibrary = info.DeclaresLibrary || package.IsLibrary;
                Levels[file] = info;
            }
        }
        LevelsValid = revision == ConfigRevision.load();
    }

    void LibraryWorkspace::ensureLevelsLocked() {
        if (!LevelsValid)
            computeLevelsLocked();
    }

    LibraryLevelInfo LibraryWorkspace::levelInfo(const QString &file) {
        ensureIndex();
        std::lock_guard lock(Mutex);
        ensureLevelsLocked();
        const auto it = Levels.find(file);
        if (it != Levels.end())
            return it->second;
        // A file the index does not know (a document that is not saved yet, or a file outside the folder).
        LibraryLevelInfo info;
        const auto manual = levelOverride(file);
        info.Overridden = manual.has_value();
        info.Level = clampLevel(manual.value_or(1));
        return info;
    }

    int LibraryWorkspace::knownLevel(const QString &file) {
        std::lock_guard lock(Mutex);
        if (const auto manual = levelOverride(file))
            return clampLevel(*manual);
        if (!IndexBuilt)
            return -1;
        if (!LevelsValid)
            computeLevelsLocked();
        const auto it = Levels.find(file);
        return it != Levels.end() && it->second.DeclaresLibrary ? it->second.Level : -1;
    }

    QList<LibraryEntry> LibraryWorkspace::libraries() {
        ensureIndex();
        std::lock_guard lock(Mutex);
        ensureLevelsLocked();
        QList<LibraryEntry> libraries;
        for (const auto &[file, info] : Levels) {
            if (!info.DeclaresLibrary && !info.Overridden)
                continue;
            LibraryEntry entry;
            entry.File = file;
            entry.Info = info;
            for (const auto &package : Files[file].Scan.Packages)
                if (!entry.Packages.contains(package.Name))
                    entry.Packages.append(package.Name);
            libraries.append(entry);
        }
        return libraries;
    }

    QStringList LibraryWorkspace::levelWarnings(const QString &file) {
        ensureIndex();
        std::lock_guard lock(Mutex);
        ensureLevelsLocked();
        QStringList warnings;
        const auto self = Levels.find(file);
        if (self == Levels.end())
            return warnings;

        for (const auto &dependency : dependenciesLocked(file)) {
            const auto other = Levels.find(dependency);
            if (other == Levels.end() || other->second.Level <= self->second.Level)
                continue;
            warnings.append(QStringLiteral("Library '%1' (level %2) uses '%3' (level %4). A library only sees libraries of the same "
                                           "or a lower level, so its imports cannot be resolved. Check the manual level settings.")
                                    .arg(file).arg(self->second.Level).arg(dependency).arg(other->second.Level));
        }
        if (self->second.InCycle) {
            QStringList members;
            const auto mine = dependenciesLocked(file);
            for (const auto &[other, info] : Levels)
                if (info.InCycle && info.Level == self->second.Level && other != file) {
                    const auto dependencies = dependenciesLocked(other);
                    if (dependencies.contains(file) || mine.contains(other))
                        members.append(other);
                }
            warnings.append(QStringLiteral("Library '%1' is part of an import cycle%2. The files of a cycle share level %3.")
                                    .arg(file)
                                    .arg(members.isEmpty() ? QString() : QStringLiteral(" with ") + members.join(QStringLiteral(", ")))
                                    .arg(self->second.Level));
        }
        return warnings;
    }

    // ------------------------------------------------------------------ user settings

    void LibraryWorkspace::saveConfigLocked() {
        ++ConfigRevision;
        LevelsValid = false;
        SaveSucceeded = ConfigRoot.isEmpty() ? false : Config.save(ConfigRoot);
    }

    void LibraryWorkspace::setLevelOverride(const QString &file, int level) {
        std::lock_guard lock(ConfigMutex);
        Config.setLevel(file, clampLevel(level));
        saveConfigLocked();
    }

    void LibraryWorkspace::clearLevelOverride(const QString &file) {
        std::lock_guard lock(ConfigMutex);
        if (Config.removeLevel(file))
            saveConfigLocked();
    }

    std::optional<int> LibraryWorkspace::levelOverride(const QString &file) const {
        std::lock_guard lock(ConfigMutex);
        return Config.levelFor(file);
    }

    void LibraryWorkspace::setPreferredSource(const QString &package, const QString &file) {
        std::lock_guard lock(ConfigMutex);
        Config.PreferredSources.insert(package, WorkspaceConfig::normalizePath(file));
        saveConfigLocked();
    }

    void LibraryWorkspace::clearPreferredSource(const QString &package) {
        std::lock_guard lock(ConfigMutex);
        if (Config.PreferredSources.remove(package))
            saveConfigLocked();
    }

    QString LibraryWorkspace::preferredSource(const QString &package) const {
        std::lock_guard lock(ConfigMutex);
        return Config.preferredFor(package);
    }

    bool LibraryWorkspace::lastSaveSucceeded() const {
        std::lock_guard lock(ConfigMutex);
        return SaveSucceeded;
    }

    // ------------------------------------------------------------------ project files

    QString LibraryWorkspace::absolutePath(const QString &file) const {
        std::lock_guard lock(Mutex);
        return Root.isEmpty() ? QString() : QDir(Root).absoluteFilePath(file);
    }

    qint64 LibraryWorkspace::fileStamp(const QString &file) const {
        const auto path = absolutePath(file);
        const QFileInfo info(path);
        return path.isEmpty() || !info.exists() ? -1 : info.lastModified().toMSecsSinceEpoch();
    }

    QList<LibrarySource> LibraryWorkspace::readFile(const QString &file) {
        const auto path = absolutePath(file);
        QList<LibrarySource> sources;
        if (path.isEmpty())
            return sources;

        ++FileReads;
        const auto blocks = CodeScanner::codeBlocksOfFile(path);
        const bool markdown = QFileInfo(path).suffix().toLower() == QLatin1String("md");
        int number = 0;
        for (const auto &block : blocks) {
            ++number;
            sources.append({markdown || blocks.size() > 1 ? QStringLiteral("%1#%2").arg(file).arg(number) : file,
                            block.Language, block.Text});
        }
        return sources;
    }

    int LibraryWorkspace::fileReadCount() const { return FileReads.load(); }

    int LibraryWorkspace::indexBuildCount() const { return IndexBuilds.load(); }
}
