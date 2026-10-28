//
// Created by Moritz Herzog on 24.10.26.
//

#include "DocumentWorkspace.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <map>
#include <utility>

#include <QDir>

#include <kerml/root/elements/Element.h>
#include <kerml/root/namespaces/Namespace.h>
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
        DocumentSlots.clear();
        DocumentTexts.clear();
        DocumentKerML.clear();
        DocumentFailed.clear();
        Loaded.clear();
        LoadedFiles.clear();
        AmbiguousRoots.clear();
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
        for (const auto slot : DocumentSlots)
            Space->replaceSource(slot, std::string(), SourceLanguage::Auto);
        for (const auto &library : Loaded)
            for (const auto source : library.Sources)
                Space->replaceSource(source, std::string(), SourceLanguage::Auto);
    }

    void DocumentWorkspace::disposeAll() {
        if (Space == nullptr)
            return;
        disposeSpace(*Space);
        Space.reset();
    }

    void DocumentWorkspace::dropLibraries() {
        for (const auto &library : Loaded)
            for (const auto source : library.Sources)
                Space->replaceSource(source, std::string(), SourceLanguage::Auto);
        Loaded.clear();
        LoadedFiles.clear();
        AmbiguousRoots.clear();
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
        std::fill(DocumentTexts.begin(), DocumentTexts.end(), std::string());
        std::fill(DocumentKerML.begin(), DocumentKerML.end(), false);
        std::fill(DocumentFailed.begin(), DocumentFailed.end(), false);
        ForceReparse = true;
    }

    void DocumentWorkspace::applyPending() {
        std::shared_ptr<LibraryWorkspace> libraries;
        QString ownFile;
        {
            std::lock_guard lock(PendingMutex);
            if (!HasPending)
                return;
            HasPending = false;
            libraries = std::move(PendingLibraries);
            ownFile = std::move(PendingOwnFile);
        }
        Libraries = std::move(libraries);
        OwnFile = std::move(ownFile);
        if (Space != nullptr)
            dropLibraries();
        AmbiguousRoots.clear();
        LibraryGeneration = Libraries != nullptr ? Libraries->generation() : 0;
        ForceReparse = true;
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

    void DocumentWorkspace::reloadLibrary(LoadedLibrary &library) {
        const auto level = levelOf(library.File).Level;
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
            if (!Loaded.empty()) {
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
            const auto stamp = Libraries->fileStamp(it->File);
            // The user may have picked another file for a package since the library was loaded.
            bool needed = false;
            for (const auto &trigger : std::as_const(it->Triggers)) {
                const auto &found = lookup(trigger);
                if (!found.Chosen.isEmpty() && WorkspaceConfig::samePath(found.Chosen, it->File))
                    needed = true;
            }
            if (stamp < 0 || !needed) {
                for (const auto source : it->Sources)
                    Space->replaceSource(source, std::string(), SourceLanguage::Auto);
                LoadedFiles.erase(it->File);
                it = Loaded.erase(it);
                changed = true;
                continue;
            }
            if (stamp != it->Stamp || levelOf(it->File).Level != it->Level)
                changed = true;
            ++it;
        }

        if (changed) {
            // The libraries refer to each other, so they are replaced together (and with the document). The standard library
            // does not refer to them and stays as it is.
            for (auto &library : Loaded) {
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
        library.Level = levelOf(file).Level;
        library.Triggers.append(trigger);
        LoadedFiles.insert(file);
        try {
            for (const auto &source : Libraries->readFile(file))
                library.Sources.push_back(Space->addText(source.Text.toStdString(), source.Name.toStdString(), languageOf(source.Language), library.Level));
        } catch (const std::exception &ex) {
            diagnostics.append(diagnostic(2, QStringLiteral("Library '%1' could not be read: %2").arg(file, text(ex.what()))));
        }
        Loaded.push_back(std::move(library));
    }

    void DocumentWorkspace::resolveWithLibraries(QList<WorkspaceDiagnostic> &diagnostics) {
        try {
            Space->resolve();
        } catch (const std::exception &ex) {
            diagnostics.append(diagnostic(3, QStringLiteral("The names could not be resolved: %1").arg(text(ex.what()))));
            return;
        }
        if (Libraries == nullptr)
            return;

        // Nothing of the opened folder is touched as long as the standard library resolves everything.
        std::set<QString> tried;
        while (!Space->unresolvedReferences().empty()) {
            QStringList roots;
            for (const auto &reference : Space->unresolvedReferences()) {
                const auto root = firstSegment(reference.name);
                if (!root.isEmpty() && tried.insert(root).second)
                    roots.append(root);
            }

            bool loadedAny = false;
            for (const auto &root : std::as_const(roots)) {
                const auto found = lookup(root);
                if (found.Chosen.isEmpty() || WorkspaceConfig::samePath(found.Chosen, OwnFile))
                    continue;
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
            if (!loadedAny)
                break;
            try {
                Space->resolve();
            } catch (const std::exception &ex) {
                diagnostics.append(diagnostic(3, QStringLiteral("The names could not be resolved: %1").arg(text(ex.what()))));
                break;
            }
        }
    }

    QString DocumentWorkspace::hintFor(const std::string &written, bool isImport) {
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

    WorkspaceResult DocumentWorkspace::update(const std::vector<DocumentSource> &sources, const QString &documentName,
                                              const RowVisitor &visitor) {
        std::lock_guard lock(Mutex);
        LookupCache.clear();
        LevelCache.clear();
        Rescanned = false;

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

            // A block that the parser cannot handle is reported on its row; replaceSource leaves the workspace unchanged then,
            // and the stored text stays the old one, so that the block is tried again with the next change.
            std::vector<QString> failures(sources.size());
            if (changed) {
                for (size_t i = 0; i < blockCount; ++i) {
                    const bool present = i < sources.size();
                    const std::string body = present ? sources[i].Text : std::string();
                    const bool kerml = present && sources[i].KerML;
                    const auto language = kerml ? SourceLanguage::KerML : SourceLanguage::SysML;
                    try {
                        if (i < DocumentSlots.size()) {
                            Space->replaceSource(DocumentSlots[i], body, language);
                            DocumentTexts[i] = body;
                            DocumentKerML[i] = kerml;
                            DocumentFailed[i] = false;
                        } else {
                            const auto name = documentName.toStdString() + "#" + std::to_string(sources[i].Row);
                            DocumentSlots.push_back(Space->addText(body, name, language, LibraryWorkspace::DocumentLevel));
                            DocumentTexts.push_back(body);
                            DocumentKerML.push_back(kerml);
                            DocumentFailed.push_back(false);
                        }
                    } catch (const std::exception &ex) {
                        if (present)
                            failures[i] = text(ex.what()).isEmpty() ? QStringLiteral("The text could not be parsed.") : text(ex.what());
                        // The stale model of the old text must not stay: the block is empty until it parses, and is tried again.
                        if (i < DocumentSlots.size()) {
                            Space->replaceSource(DocumentSlots[i], std::string(), SourceLanguage::Auto);
                            DocumentTexts[i].clear();
                            DocumentKerML[i] = false;
                            DocumentFailed[i] = true;
                        } else {
                            // Keep the slots aligned with the blocks.
                            DocumentSlots.push_back(Space->addText(std::string(), documentName.toStdString() + "#" + std::to_string(sources[i].Row),
                                                                   language, LibraryWorkspace::DocumentLevel));
                            DocumentTexts.push_back(std::string());
                            DocumentKerML.push_back(false);
                            DocumentFailed.push_back(true);
                        }
                    }
                }
                ForceReparse = false;
            }

            resolveWithLibraries(result.Diagnostics);

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

            for (size_t i = 0; i < sources.size(); ++i) {
                const auto slot = DocumentSlots[i];
                WorkspaceRow row;
                row.Row = sources[i].Row;

                if (!failures[i].isEmpty()) {
                    row.HasSyntaxErrors = true;
                    result.Diagnostics.append(diagnostic(3, failures[i], row.Row));
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
                for (const auto &warning : Libraries->levelWarnings(library.File))
                    result.Diagnostics.append(diagnostic(2, warning));
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
                if (syntaxErrors > 0)
                    result.Diagnostics.append(diagnostic(2, QStringLiteral("Library '%1' has %2 syntax error(s), its names may be missing (%3)")
                                                                    .arg(library.File).arg(syntaxErrors).arg(first)));
                if (unresolved > 0)
                    result.Diagnostics.append(diagnostic(2, QStringLiteral("Library '%1' has %2 reference(s) that cannot be resolved")
                                                                    .arg(library.File).arg(unresolved)));
            }
            return result;
        } catch (...) {
            // The state of the workspace is unknown after an exception, the next run starts from scratch.
            try {
                disposeAll();
            } catch (...) {
                Space.reset();
            }
            throw;
        }
    }
}
