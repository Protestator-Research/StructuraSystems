//
// Created by Moritz Herzog on 24.10.26.
//

#include "DocumentWorkspace.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <map>
#include <unordered_set>
#include <utility>

#include <QDir>

#include <kerml/core/classifiers/Classifier.h>
#include <kerml/core/features/Feature.h>
#include <kerml/root/elements/Element.h>
#include <kerml/root/namespaces/Import.h>
#include <kerml/root/namespaces/Namespace.h>
#include <kerml/root/namespaces/NamespaceImport.h>
#include <sysmlv2/ParserError.h>
#include <sysmlv2/Workspace.h>

namespace StructuraSystems::Client {
    namespace {
        using SysMLv2::Files::SourceLanguage;

        /** First segment of a (qualified) name as written in the source: `Antrieb` for `Antrieb::Motor::*`, `a b` for `'a b'::c`. */
        QString firstSegment(const std::string &written) {
            const auto name = QString::fromStdString(written).trimmed();
            if (name.isEmpty())
                return {};
            if (name.startsWith(QLatin1Char('\''))) {
                const auto end = name.indexOf(QLatin1Char('\''), 1);
                return end < 0 ? name.mid(1) : name.mid(1, end - 1);
            }
            qsizetype end = 0;
            while (end < name.size() && (name[end].isLetterOrNumber() || name[end] == QLatin1Char('_')))
                ++end;
            return name.left(end);
        }

        int severityOf(const std::shared_ptr<SysMLv2::Files::ParserError> &error) {
            return error->errorType() == SysMLv2::Files::ErrorType::WARNING ? 2 : 3;
        }

        QString text(const std::string &value) {
            return QString::fromStdString(value);
        }

        SourceLanguage languageOf(const QString &language) {
            return language == QLatin1String("KerML") ? SourceLanguage::KerML : SourceLanguage::SysML;
        }

        std::atomic<bool> ShuttingDown{false};

        /** Frees the model of a workspace: it refers to itself in cycles, only replacing the sources takes it apart. */
        void disposeSpace(SysMLv2::Files::Workspace &space) {
            for (size_t source = 0; source < space.sourceCount(); ++source)
                space.replaceSource(source, std::string(), SourceLanguage::Auto);
        }

        /** The qualified name as the name index writes it (names of the owning namespaces up to the unnamed root), empty if unnamed. */
        QString qualifiedNameOf(const KerML::Entities::Element &element) {
            QStringList segments;
            const KerML::Entities::Element *current = &element;
            std::shared_ptr<KerML::Entities::Namespace> owner;
            for (int depth = 0; current != nullptr && depth < 64; ++depth) {
                auto name = current->effectiveName();
                if (!name.has_value())
                    name = current->declaredName();
                if (!name.has_value() || name->empty())
                    break;
                segments.prepend(QString::fromStdString(*name));
                owner = current->owningNamespace();
                current = owner.get();
            }
            return segments.join(QStringLiteral("::"));
        }

        /** Appends the named members of the namespace and, below them, those of their members (anonymous elements are not reachable by name). */
        void collectNames(const KerML::Entities::Namespace &owner, const QString &ownerName, int depth, bool inDocument,
                          std::unordered_set<const KerML::Entities::Element *> &visited, QList<NameEntry> &entries) {
            for (const auto &member : owner.ownedMember()) {
                if (member == nullptr || !visited.insert(member.get()).second)
                    continue;
                auto name = member->effectiveName();
                if (!name.has_value())
                    name = member->declaredName();
                if (!name.has_value() || name->empty())
                    continue;

                NameEntry entry;
                entry.Name = QString::fromStdString(*name);
                entry.Owner = ownerName;
                entry.QualifiedName = ownerName.isEmpty() ? entry.Name : ownerName + QStringLiteral("::") + entry.Name;
                entry.Type = QString::fromStdString(member->getType());
                entry.IsType = dynamic_cast<const KerML::Entities::Classifier *>(member.get()) != nullptr;
                entry.IsFeature = dynamic_cast<const KerML::Entities::Feature *>(member.get()) != nullptr;
                const auto *ownedNamespace = dynamic_cast<const KerML::Entities::Namespace *>(member.get());
                entry.IsNamespace = ownedNamespace != nullptr;
                entry.InDocument = inDocument;
                if (ownedNamespace != nullptr) {
                    // `public import ISQBase::*;` makes the members of ISQBase reachable as ISQ::...
                    for (const auto &import : ownedNamespace->ownedImport()) {
                        if (import == nullptr || import->visibility() != KerML::Entities::PUBLIC ||
                            dynamic_cast<const KerML::Entities::NamespaceImport *>(import.get()) == nullptr)
                            continue;
                        const auto target = import->importedElement();
                        const QString targetName = target != nullptr ? qualifiedNameOf(*target) : QString();
                        if (!targetName.isEmpty())
                            entry.Exports.append(targetName);
                    }
                }
                const QString qualifiedName = entry.QualifiedName;
                entries.append(std::move(entry));

                // The depth limit guards against models that nest deeper than any text does.
                if (ownedNamespace != nullptr && depth < 32)
                    collectNames(*ownedNamespace, qualifiedName, depth + 1, inDocument, visited, entries);
            }
        }

        WorkspaceDiagnostic diagnostic(int severity, const QString &message, int row = -1) {
            WorkspaceDiagnostic result;
            result.Row = row;
            result.Severity = severity;
            result.Message = message;
            return result;
        }
    }

    DocumentWorkspace::DocumentWorkspace(std::shared_ptr<LibraryWorkspace> libraries, QString ownFile,
                                         std::shared_ptr<const std::vector<StandardSource>> standardLibrary) :
        Libraries(std::move(libraries)), OwnFile(std::move(ownFile)), StandardLibrary(std::move(standardLibrary)) {
    }

    DocumentWorkspace::~DocumentWorkspace() {
        if (ShuttingDown) {
            // The process ends: freeing the model would only delay that.
            Space.release();
            return;
        }
        try {
            disposeAll();
        } catch (...) {
        }
    }

    void DocumentWorkspace::beginShutdown() {
        ShuttingDown = true;
    }

    void DocumentWorkspace::setLibraries(std::shared_ptr<LibraryWorkspace> libraries, QString ownFile) {
        std::lock_guard lock(PendingMutex);
        PendingLibraries = std::move(libraries);
        PendingOwnFile = std::move(ownFile);
        HasPending = true;
    }

    QStringList DocumentWorkspace::loadedLibraryFiles() const {
        std::lock_guard lock(Mutex);
        QStringList files;
        for (const auto &library : Loaded)
            files.append(library.File);
        return files;
    }

    void DocumentWorkspace::ensureSpace() {
        if (Space != nullptr)
            return;
        auto space = std::make_unique<SysMLv2::Files::Workspace>();
        try {
            for (const auto &standard : StandardLibrary != nullptr ? *StandardLibrary : LibraryWorkspace::standardLibrary()) {
                if (ShuttingDown)
                    throw std::runtime_error("The application is closing.");
                space->addText(standard.Text, standard.Name, standard.KerML ? SourceLanguage::KerML : SourceLanguage::SysML,
                               LibraryWorkspace::StandardLevel);
            }
        } catch (...) {
            // Nothing half built is kept: the next attempt starts again.
            try {
                disposeSpace(*space);
            } catch (...) {
            }
            throw;
        }
        Space = std::move(space);
        ++ModelEpoch;
        DocumentSlots.clear();
        DocumentTexts.clear();
        DocumentKerML.clear();
        DocumentFailed.clear();
        Loaded.clear();
        LoadedFiles.clear();
        AmbiguousRoots.clear();
        RemoteMax = 0;
    }

    void DocumentWorkspace::warmUp() {
        std::lock_guard lock(Mutex);
        try {
            ensureSpace();
            Space->resolve();
        } catch (const std::exception &) {
            // Nothing to warm up if the library cannot be parsed (or the application closes); the first update() reports a problem.
            if (Space != nullptr) {
                try {
                    disposeAll();
                } catch (...) {
                    Space.release();
                }
            }
        }
    }

    void DocumentWorkspace::disposeNonStandard() {
        if (Space == nullptr)
            return;
        ++ModelEpoch;
        for (const auto slot : DocumentSlots)
            Space->replaceSource(slot, std::string(), SourceLanguage::Auto);
        for (const auto &library : Loaded)
            for (const auto source : library.Sources)
                Space->replaceSource(source, std::string(), SourceLanguage::Auto);
    }

    void DocumentWorkspace::disposeAll() {
        if (Space == nullptr)
            return;
        ++ModelEpoch;
        disposeSpace(*Space);
        Space.reset();
    }

    void DocumentWorkspace::dropLibraries() {
        ++ModelEpoch;
        for (const auto &library : Loaded)
            for (const auto source : library.Sources)
                Space->replaceSource(source, std::string(), SourceLanguage::Auto);
        Loaded.clear();
        LoadedFiles.clear();
        AmbiguousRoots.clear();
        RemoteNotes.clear();
        RemoteMax = 0;
    }

    void DocumentWorkspace::recycle() {
        std::lock_guard lock(Mutex);
        if (Space == nullptr)
            return;
        try {
            disposeNonStandard();
        } catch (const std::exception &) {
            // The workspace cannot be trusted any more, the next owner starts with a new one.
            try {
                disposeAll();
            } catch (...) {
            }
            return;
        }
        Loaded.clear();
        LoadedFiles.clear();
        AmbiguousRoots.clear();
        RemoteMax = 0;
        RemoteNotes.clear();
        Remote.reset();
        RemoteProject.clear();
        RemoteCommit.clear();
        ClosureCache.reset();
        std::fill(DocumentTexts.begin(), DocumentTexts.end(), std::string());
        std::fill(DocumentKerML.begin(), DocumentKerML.end(), false);
        std::fill(DocumentFailed.begin(), DocumentFailed.end(), false);
        ForceReparse = true;
    }

    void DocumentWorkspace::setRemote(std::shared_ptr<RemoteLibraries> remote, QString projectId, QString commitId) {
        std::lock_guard lock(PendingMutex);
        PendingRemote = std::move(remote);
        PendingRemoteProject = std::move(projectId);
        PendingRemoteCommit = std::move(commitId);
        HasPendingRemote = true;
    }

    void DocumentWorkspace::applyPending() {
        std::shared_ptr<LibraryWorkspace> libraries;
        QString ownFile;
        bool librariesChanged = false;
        std::shared_ptr<RemoteLibraries> remote;
        QString remoteProject;
        QString remoteCommit;
        bool remoteChanged = false;
        {
            std::lock_guard lock(PendingMutex);
            if (HasPending) {
                HasPending = false;
                librariesChanged = true;
                libraries = std::move(PendingLibraries);
                ownFile = std::move(PendingOwnFile);
            }
            if (HasPendingRemote) {
                HasPendingRemote = false;
                remote = std::move(PendingRemote);
                remoteProject = std::move(PendingRemoteProject);
                remoteCommit = std::move(PendingRemoteCommit);
                // The same usages are not loaded again: only another project or commit (or cache) changes the libraries.
                remoteChanged = remote != Remote || remoteProject != RemoteProject || remoteCommit != RemoteCommit;
            }
        }
        if (librariesChanged) {
            Libraries = std::move(libraries);
            OwnFile = std::move(ownFile);
            LibraryGeneration = Libraries != nullptr ? Libraries->generation() : 0;
        }
        if (remoteChanged) {
            Remote = std::move(remote);
            RemoteProject = std::move(remoteProject);
            RemoteCommit = std::move(remoteCommit);
            ClosureCache.reset();
        }
        if (librariesChanged || remoteChanged) {
            // What was loaded for another context does not belong to this one (the standard library stays).
            if (Space != nullptr)
                dropLibraries();
            AmbiguousRoots.clear();
            ForceReparse = true;
        }
    }

    const PackageLookup &DocumentWorkspace::lookup(const QString &package) {
        const auto cached = LookupCache.find(package);
        if (cached != LookupCache.end())
            return cached->second;
        auto found = Libraries->findPackage(package);
        if (found.Chosen.isEmpty() && !Rescanned) {
            // A file may have been added since the index was made. The folder is checked once per update.
            Rescanned = true;
            found = Libraries->findPackage(package, true);
        }
        return LookupCache.emplace(package, std::move(found)).first->second;
    }

    LibraryLevelInfo DocumentWorkspace::levelOf(const QString &file) {
        const auto cached = LevelCache.find(file);
        if (cached != LevelCache.end())
            return cached->second;
        return LevelCache.emplace(file, Libraries->levelInfo(file)).first->second;
    }

    int DocumentWorkspace::effectiveLevel(const QString &file) {
        // Local libraries sit above the remote ones (which have the levels 1..RemoteMax): they may use them, not the other way round.
        return (std::min)(levelOf(file).Level + RemoteMax, LibraryWorkspace::MaxLibraryLevel);
    }

    void DocumentWorkspace::reloadLibrary(LoadedLibrary &library) {
        ++ModelEpoch;
        const auto level = effectiveLevel(library.File);
        const auto stamp = Libraries->fileStamp(library.File);
        const auto blocks = Libraries->readFile(library.File);
        for (size_t i = 0; i < size_t(blocks.size()); ++i) {
            const auto &block = blocks[qsizetype(i)];
            if (i < library.Sources.size()) {
                Space->replaceSource(library.Sources[i], block.Text.toStdString(), languageOf(block.Language));
                Space->setSourceLevel(library.Sources[i], level);
            } else {
                library.Sources.push_back(Space->addText(block.Text.toStdString(), block.Name.toStdString(), languageOf(block.Language), level));
            }
        }
        for (size_t i = size_t(blocks.size()); i < library.Sources.size(); ++i)
            Space->replaceSource(library.Sources[i], std::string(), SourceLanguage::Auto);
        // Only after everything worked: otherwise the next update() tries again.
        library.Stamp = stamp;
        library.Level = level;
    }

    void DocumentWorkspace::refreshLibraries(QList<WorkspaceDiagnostic> &diagnostics) {
        if (Libraries == nullptr) {
            // Remote libraries do not depend on a folder; only the local ones are dropped.
            bool anyLocal = false;
            for (const auto &library : Loaded)
                anyLocal = anyLocal || !library.Remote;
            if (anyLocal) {
                dropLibraries();
                ForceReparse = true;
            }
            return;
        }
        const auto generation = Libraries->generation();
        if (generation != LibraryGeneration) {
            // Another folder was opened: what was loaded does not belong to it.
            LibraryGeneration = generation;
            if (!Loaded.empty()) {
                dropLibraries();
                ForceReparse = true;
            }
            return;
        }

        bool changed = false;
        for (auto it = Loaded.begin(); it != Loaded.end();) {
            if (it->Remote) {
                ++it;   // a commit of another project never changes
                continue;
            }
            const auto stamp = Libraries->fileStamp(it->File);
            // The user may have picked another file for a package since the library was loaded.
            bool needed = false;
            for (const auto &trigger : std::as_const(it->Triggers)) {
                const auto &found = lookup(trigger);
                if (!found.Chosen.isEmpty() && WorkspaceConfig::samePath(found.Chosen, it->File))
                    needed = true;
            }
            if (stamp < 0 || !needed) {
                ++ModelEpoch;
                for (const auto source : it->Sources)
                    Space->replaceSource(source, std::string(), SourceLanguage::Auto);
                LoadedFiles.erase(it->File);
                it = Loaded.erase(it);
                changed = true;
                continue;
            }
            if (stamp != it->Stamp || effectiveLevel(it->File) != it->Level)
                changed = true;
            ++it;
        }

        if (changed) {
            // The libraries refer to each other, so they are replaced together (and with the document). The standard library
            // does not refer to them and stays as it is.
            for (auto &library : Loaded) {
                if (library.Remote)
                    continue;
                try {
                    reloadLibrary(library);
                } catch (const std::exception &ex) {
                    diagnostics.append(diagnostic(2, QStringLiteral("Library '%1' could not be read again: %2").arg(library.File, text(ex.what()))));
                }
            }
            ForceReparse = true;
        }
    }

    void DocumentWorkspace::loadLibraryFile(const QString &file, const QString &trigger, QList<WorkspaceDiagnostic> &diagnostics) {
        LoadedLibrary library;
        library.File = file;
        library.Stamp = Libraries->fileStamp(file);
        library.Level = effectiveLevel(file);
        library.Triggers.append(trigger);
        LoadedFiles.insert(file);
        ++ModelEpoch;
        try {
            for (const auto &source : Libraries->readFile(file))
                library.Sources.push_back(Space->addText(source.Text.toStdString(), source.Name.toStdString(), languageOf(source.Language), library.Level));
        } catch (const std::exception &ex) {
            diagnostics.append(diagnostic(2, QStringLiteral("Library '%1' could not be read: %2").arg(file, text(ex.what()))));
        }
        Loaded.push_back(std::move(library));
    }

    void DocumentWorkspace::loadRemoteLibrary(const RemoteLibrary &remote, QList<WorkspaceDiagnostic> &diagnostics) {
        LoadedLibrary library;
        library.File = remote.Name;
        library.Remote = true;
        library.Level = remote.Level;
        library.Triggers.append(remote.Name);
        LoadedFiles.insert(remote.Name);
        ++ModelEpoch;
        try {
            for (const auto &source : remote.Content.Sources)
                library.Sources.push_back(Space->addText(source.Text.toStdString(), source.Name.toStdString(), languageOf(source.Language), library.Level));
        } catch (const std::exception &ex) {
            diagnostics.append(diagnostic(2, QStringLiteral("Remote library '%1' could not be read: %2").arg(remote.Name, text(ex.what()))));
        }
        RemoteMax = (std::max)(RemoteMax, remote.Level);
        Loaded.push_back(std::move(library));
    }

    void DocumentWorkspace::relevelLocalLibraries(QList<WorkspaceDiagnostic> &diagnostics) {
        bool moved = false;
        for (const auto &library : Loaded)
            moved = moved || (!library.Remote && effectiveLevel(library.File) != library.Level);
        if (!moved)
            return;
        // The local libraries move above the remote ones, together with the document (they refer to each other).
        for (auto &library : Loaded) {
            if (library.Remote)
                continue;
            try {
                reloadLibrary(library);
            } catch (const std::exception &ex) {
                diagnostics.append(diagnostic(2, QStringLiteral("Library '%1' could not be read again: %2").arg(library.File, text(ex.what()))));
            }
        }
        parseDocumentBlocks();
    }

    void DocumentWorkspace::addRemoteNote(const QString &note) {
        if (!RemoteNotes.contains(note))
            RemoteNotes.append(note);
    }

    const RemoteClosure &DocumentWorkspace::remoteClosure() {
        const auto generation = Remote->generation();
        if (!ClosureCache || ClosureGeneration != generation) {
            ClosureCache = Remote->closure(RemoteProject, RemoteCommit);
            ClosureGeneration = generation;
        }
        return *ClosureCache;
    }

    void DocumentWorkspace::resolveWithLibraries(QList<WorkspaceDiagnostic> &diagnostics) {
        try {
            Space->resolve();
        } catch (const std::exception &ex) {
            diagnostics.append(diagnostic(3, QStringLiteral("The names could not be resolved: %1").arg(text(ex.what()))));
            return;
        }
        const bool hasRemote = Remote != nullptr && !RemoteProject.isEmpty() && !RemoteCommit.isEmpty();
        if (Libraries == nullptr && !hasRemote)
            return;

        // Nothing of the opened folder (and no remote usage) is touched as long as the standard library resolves everything.
        // Order: the local libraries first, then the usages of the project on the server for what is still missing.
        std::set<QString> tried;         // roots looked up for the document and the local libraries
        std::set<QString> triedRemote;   // roots looked up for the remote libraries
        while (!Space->unresolvedReferences().empty()) {
            // A remote library sees only what is below it: what it imports must come from the usages of the project, never from
            // the local folder (a local package of the same name would sit above the remote library and stay invisible to it).
            std::set<size_t> remoteSources;
            for (const auto &library : Loaded)
                if (library.Remote)
                    remoteSources.insert(library.Sources.begin(), library.Sources.end());

            QStringList roots;
            QStringList remoteRoots;
            for (const auto &reference : Space->unresolvedReferences()) {
                const auto root = firstSegment(reference.name);
                if (root.isEmpty())
                    continue;
                if (remoteSources.count(reference.source)) {
                    if (triedRemote.insert(root).second)
                        remoteRoots.append(root);
                } else if (tried.insert(root).second) {
                    roots.append(root);
                }
            }

            bool loadedAny = false;
            QStringList wantedRemote = remoteRoots;
            for (const auto &root : std::as_const(roots)) {
                if (Libraries == nullptr) {
                    wantedRemote.append(root);
                    continue;
                }
                const auto found = lookup(root);
                if (found.Chosen.isEmpty() || WorkspaceConfig::samePath(found.Chosen, OwnFile)) {
                    wantedRemote.append(root);
                    continue;
                }
                if (found.Ambiguous && !AmbiguousRoots.contains(root))
                    AmbiguousRoots.append(root);
                if (LoadedFiles.count(found.Chosen)) {
                    for (auto &library : Loaded)
                        if (library.File == found.Chosen && !library.Triggers.contains(root))
                            library.Triggers.append(root);
                    continue;
                }
                loadLibraryFile(found.Chosen, root, diagnostics);
                loadedAny = true;
            }

            if (hasRemote && !wantedRemote.isEmpty()) {
                bool remoteLoaded = false;
                const auto &closure = remoteClosure();
                for (const auto &root : std::as_const(wantedRemote)) {
                    QStringList providers;
                    const RemoteLibrary *chosen = nullptr;
                    for (const auto &library : closure.Libraries) {
                        if (!library.Content.Packages.contains(root))
                            continue;
                        providers.append(library.Name);
                        if (chosen == nullptr)
                            chosen = &library;
                    }
                    if (chosen == nullptr)
                        continue;
                    if (providers.size() > 1)
                        addRemoteNote(QStringLiteral("Package '%1' is defined by several project usages (%2). '%3' is used.")
                                              .arg(root, providers.join(QStringLiteral(", ")), chosen->Name));
                    if (Libraries != nullptr && remoteRoots.contains(root)) {
                        const auto local = lookup(root);
                        if (!local.Chosen.isEmpty())
                            addRemoteNote(QStringLiteral("Package '%1' exists in the project '%2' and in the local file '%3'. A project usage cannot "
                                                         "see local files, so '%2' is used for it.")
                                                  .arg(root, chosen->Name, local.Chosen));
                    }
                    if (LoadedFiles.count(chosen->Name))
                        continue;
                    loadRemoteLibrary(*chosen, diagnostics);
                    remoteLoaded = true;
                }
                if (remoteLoaded) {
                    relevelLocalLibraries(diagnostics);
                    loadedAny = true;
                }
            }

            if (!loadedAny)
                break;
            try {
                Space->resolve();
            } catch (const std::exception &ex) {
                diagnostics.append(diagnostic(3, QStringLiteral("The names could not be resolved: %1").arg(text(ex.what()))));
                break;
            }
        }

        // Names are still missing: if the usages of the project could help, say what is not available.
        if (hasRemote && !Space->unresolvedReferences().empty()) {
            const auto &closure = remoteClosure();
            RemoteProblems = closure.Problems;
            NeedsRemoteDownload = closure.Missing;
        }
    }

    QString DocumentWorkspace::hintFor(const std::string &written, bool isImport) {
        QString hint = hintForLocal(written, isImport);
        if (!RemoteProblems.isEmpty())
            hint += QStringLiteral("; project usages: %1").arg(RemoteProblems.join(QStringLiteral(" ")));
        return hint;
    }

    QString DocumentWorkspace::hintForLocal(const std::string &written, bool isImport) {
        const auto name = text(written);
        const auto root = firstSegment(written);
        if (Libraries == nullptr)
            return QStringLiteral("only the standard library is searched");

        const auto folder = Libraries->root();
        // The target of an import is a package (`import A::*;` is recorded as `A`).
        const bool qualified = isImport || name.contains(QStringLiteral("::"));
        if (folder.isEmpty())
            return QStringLiteral("no folder is open, so no local project is searched");

        const auto &found = lookup(root);
        if (found.Chosen.isEmpty()) {
            return qualified
                    ? QStringLiteral("package '%1' was not found in the standard library or in any local project of '%2'")
                              .arg(root, QDir(folder).dirName())
                    : QStringLiteral("it is not defined in the document, the standard library or any local project of '%1'")
                              .arg(QDir(folder).dirName());
        }
        if (WorkspaceConfig::samePath(found.Chosen, OwnFile))
            return QStringLiteral("package '%1' is only defined in the file of this document; the package must be in the edited text").arg(root);
        for (const auto &library : Loaded) {
            if (library.File == found.Chosen) {
                return qualified
                        ? QStringLiteral("package '%1' was loaded from '%2' (level %3) but does not contain this member")
                                  .arg(root, library.File).arg(library.Level)
                        : QStringLiteral("package '%1' of '%2' is not imported here").arg(root, library.File);
            }
        }
        return QStringLiteral("package '%1' is defined in '%2' but the file could not be used").arg(root, found.Chosen);
    }

    void DocumentWorkspace::parseDocumentBlocks() {
        const auto blockCount = (std::max)(DocumentSlots.size(), CurrentSources.size());
        BlockFailures.assign(CurrentSources.size(), QString());
        // A block that the parser cannot handle is reported on its row; replaceSource leaves the workspace unchanged then,
        // and the stored text stays the old one, so that the block is tried again with the next change.
        for (size_t i = 0; i < blockCount; ++i) {
            const bool present = i < CurrentSources.size();
            const std::string body = present ? CurrentSources[i].Text : std::string();
            const bool kerml = present && CurrentSources[i].KerML;
            const auto language = kerml ? SourceLanguage::KerML : SourceLanguage::SysML;
            try {
                if (i < DocumentSlots.size()) {
                    Space->replaceSource(DocumentSlots[i], body, language);
                    DocumentTexts[i] = body;
                    DocumentKerML[i] = kerml;
                    DocumentFailed[i] = false;
                } else {
                    const auto name = CurrentName.toStdString() + "#" + std::to_string(CurrentSources[i].Row);
                    DocumentSlots.push_back(Space->addText(body, name, language, LibraryWorkspace::DocumentLevel));
                    DocumentTexts.push_back(body);
                    DocumentKerML.push_back(kerml);
                    DocumentFailed.push_back(false);
                }
            } catch (const std::exception &ex) {
                if (present)
                    BlockFailures[i] = text(ex.what()).isEmpty() ? QStringLiteral("The text could not be parsed.") : text(ex.what());
                // The stale model of the old text must not stay: the block is empty until it parses, and is tried again.
                if (i < DocumentSlots.size()) {
                    Space->replaceSource(DocumentSlots[i], std::string(), SourceLanguage::Auto);
                    DocumentTexts[i].clear();
                    DocumentKerML[i] = false;
                    DocumentFailed[i] = true;
                } else {
                    // Keep the slots aligned with the blocks.
                    DocumentSlots.push_back(Space->addText(std::string(), CurrentName.toStdString() + "#" + std::to_string(CurrentSources[i].Row),
                                                           language, LibraryWorkspace::DocumentLevel));
                    DocumentTexts.push_back(std::string());
                    DocumentKerML.push_back(false);
                    DocumentFailed.push_back(true);
                }
            }
        }
        ForceReparse = false;
    }

    std::shared_ptr<const NameIndex> DocumentWorkspace::buildNameIndex() {
        std::lock_guard lock(Mutex);
        if (Space == nullptr)
            return std::make_shared<const NameIndex>();
        try {
            const std::set<size_t> documentSlots(DocumentSlots.begin(), DocumentSlots.end());
            if (!LibraryNamesValid || LibraryNamesEpoch != ModelEpoch) {
                QList<NameEntry> libraryNames;
                for (size_t source = 0; source < Space->sourceCount(); ++source) {
                    if (documentSlots.count(source))
                        continue;
                    const auto root = Space->rootNamespace(source);
                    if (root == nullptr)
                        continue;
                    std::unordered_set<const KerML::Entities::Element *> visited;
                    collectNames(*root, QString(), 0, false, visited, libraryNames);
                }
                LibraryNames = std::move(libraryNames);
                LibraryNamesEpoch = ModelEpoch;
                LibraryNamesValid = true;
            }

            QList<NameEntry> entries = LibraryNames;
            for (const auto slot : DocumentSlots) {
                const auto root = Space->rootNamespace(slot);
                if (root == nullptr)
                    continue;
                std::unordered_set<const KerML::Entities::Element *> visited;
                collectNames(*root, QString(), 0, true, visited, entries);
            }
            return std::make_shared<const NameIndex>(std::move(entries));
        } catch (...) {
            // Without names the completion only offers keywords.
            return std::make_shared<const NameIndex>();
        }
    }

    WorkspaceResult DocumentWorkspace::update(const std::vector<DocumentSource> &sources, const QString &documentName,
                                              const RowVisitor &visitor) {
        std::lock_guard lock(Mutex);
        LookupCache.clear();
        LevelCache.clear();
        Rescanned = false;
        RemoteProblems.clear();
        NeedsRemoteDownload = false;

        WorkspaceResult result;
        try {
            applyPending();
            const bool fresh = Space == nullptr;
            ensureSpace();
            if (fresh) {
                LibraryGeneration = Libraries != nullptr ? Libraries->generation() : 0;
                ForceReparse = true;
            } else {
                refreshLibraries(result.Diagnostics);
            }

            // The blocks of a document refer to each other, so they are parsed again together if one of them changed.
            CurrentSources = sources;
            CurrentName = documentName;
            const auto blockCount = (std::max)(DocumentSlots.size(), sources.size());
            bool changed = ForceReparse;
            for (size_t i = 0; i < blockCount && !changed; ++i) {
                if (i >= DocumentSlots.size()) {
                    changed = true;
                    break;
                }
                const bool present = i < sources.size();
                if (DocumentFailed[i] || DocumentTexts[i] != (present ? sources[i].Text : std::string()) ||
                    DocumentKerML[i] != (present && sources[i].KerML))
                    changed = true;
            }
            if (changed)
                parseDocumentBlocks();
            else
                BlockFailures.assign(sources.size(), QString());

            resolveWithLibraries(result.Diagnostics);
            result.NeedsRemoteDownload = NeedsRemoteDownload;

            if (Libraries != nullptr) {
                QStringList stillAmbiguous;
                for (const auto &root : std::as_const(AmbiguousRoots)) {
                    const auto &found = lookup(root);
                    bool used = false;
                    for (const auto &library : Loaded)
                        used = used || library.Triggers.contains(root);
                    if (!found.Ambiguous || !used)
                        continue;
                    stillAmbiguous.append(root);
                    result.Diagnostics.append(diagnostic(2, QStringLiteral("Package '%1' is defined in several files (%2). '%3' is used; choose the "
                                                                           "preferred source in the context menu of the project tree.")
                                                                    .arg(root, found.Files.join(QStringLiteral(", ")), found.Chosen)));
                }
                AmbiguousRoots = stillAmbiguous;
            }
            for (const auto &problem : std::as_const(RemoteProblems))
                result.Diagnostics.append(diagnostic(2, problem));
            for (const auto &note : std::as_const(RemoteNotes))
                result.Diagnostics.append(diagnostic(2, note));

            for (size_t i = 0; i < sources.size(); ++i) {
                const auto slot = DocumentSlots[i];
                WorkspaceRow row;
                row.Row = sources[i].Row;

                if (i < BlockFailures.size() && !BlockFailures[i].isEmpty()) {
                    row.HasSyntaxErrors = true;
                    result.Diagnostics.append(diagnostic(3, BlockFailures[i], row.Row));
                    if (visitor)
                        visitor(row);
                    result.Rows.push_back(std::move(row));
                    continue;
                }

                row.Elements = Space->elements(slot);
                row.Root = Space->rootNamespace(slot);

                for (const auto &error : Space->errors(slot)) {
                    WorkspaceDiagnostic entry;
                    entry.Row = row.Row;
                    entry.Line = error->getLine();
                    entry.Column = error->getColumn();
                    entry.Severity = severityOf(error);
                    entry.Message = text(error->description());
                    result.Diagnostics.append(entry);
                    row.HasSyntaxErrors = true;
                }

                // With syntax errors the model is incomplete and most of its unresolved references are consequences.
                if (!row.HasSyntaxErrors) {
                    std::map<std::pair<int, int>, std::string> warnings;
                    for (const auto &warning : Space->unresolvedAsWarnings(slot))
                        warnings.emplace(std::make_pair(warning->getLine(), warning->getColumn()), warning->description());
                    for (const auto &reference : Space->unresolvedReferences()) {
                        if (reference.source != slot)
                            continue;
                        WorkspaceDiagnostic entry;
                        entry.Row = row.Row;
                        entry.Line = reference.line;
                        entry.Column = reference.column;
                        entry.Severity = 2;
                        const auto warning = warnings.find({reference.line, reference.column});
                        entry.Message = warning != warnings.end() ? text(warning->second)
                                                                  : QStringLiteral("Unresolved reference '%1'").arg(text(reference.name));
                        entry.Message += QStringLiteral(" (%1)").arg(hintFor(reference.name, reference.role == SysMLv2::Files::ReferenceRole::Import));
                        result.Diagnostics.append(entry);
                    }
                }
                if (visitor)
                    visitor(row);
                result.Rows.push_back(std::move(row));
            }

            // Problems of the libraries are not the document's, but they explain missing names.
            for (const auto &library : Loaded) {
                if (!library.Remote && Libraries != nullptr) {
                    for (const auto &warning : Libraries->levelWarnings(library.File))
                        result.Diagnostics.append(diagnostic(2, warning));
                }
                int syntaxErrors = 0;
                QString first;
                int unresolved = 0;
                for (const auto source : library.Sources) {
                    for (const auto &error : Space->errors(source)) {
                        if (syntaxErrors++ == 0)
                            first = QStringLiteral("line %1: %2").arg(error->getLine()).arg(text(error->description()));
                    }
                    for (const auto &reference : Space->unresolvedReferences())
                        if (reference.source == source)
                            ++unresolved;
                }
                const auto kind = library.Remote ? QStringLiteral("Remote library") : QStringLiteral("Library");
                if (syntaxErrors > 0)
                    result.Diagnostics.append(diagnostic(2, QStringLiteral("%1 '%2' has %3 syntax error(s), its names may be missing (%4)")
                                                                    .arg(kind, library.File).arg(syntaxErrors).arg(first)));
                if (unresolved > 0)
                    result.Diagnostics.append(diagnostic(2, QStringLiteral("%1 '%2' has %3 reference(s) that cannot be resolved")
                                                                    .arg(kind, library.File).arg(unresolved)));
            }
            return result;
        } catch (...) {
            // The state of the workspace is unknown after an exception, the next run starts from scratch.
            try {
                disposeAll();
            } catch (...) {
                Space.release();
            }
            throw;
        }
    }
}
