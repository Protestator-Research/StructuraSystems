//
// Created by Moritz Herzog on 30.10.26.
//

#include "RemoteLibraries.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>

namespace StructuraSystems::Client {
    namespace {
        QString nodeKey(const QString &projectId, const QString &commitId) {
            return projectId + QLatin1Char('@') + commitId;
        }

        QString displayName(const QString &projectName, const QString &projectId, const QString &commitId) {
            return QStringLiteral("%1@%2").arg(projectName.isEmpty() ? projectId.left(8) : projectName, commitId.left(8));
        }

        bool writeFile(const QString &path, const QByteArray &content) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            return file.write(content) == content.size();
        }

        QJsonObject usageToJson(const RemoteUsageRef &usage) {
            return {{"usageId", usage.UsageId}, {"projectId", usage.ProjectId}, {"commitId", usage.CommitId}, {"projectName", usage.ProjectName}};
        }

        RemoteUsageRef usageFromJson(const QJsonObject &json) {
            return {json.value("usageId").toString(), json.value("projectId").toString(), json.value("commitId").toString(),
                    json.value("projectName").toString()};
        }
    }

    RemoteLibraries::RemoteLibraries(QString cacheDirectory, Fetcher fetcher) :
        CacheDirectory(std::move(cacheDirectory)), Fetch(std::move(fetcher)) {
    }

    QString RemoteLibraries::cacheDirectory() const {
        std::lock_guard lock(Mutex);
        return CacheDirectory;
    }

    bool RemoteLibraries::setCacheDirectory(const QString &directory) {
        {
            std::lock_guard lock(Mutex);
            if (CacheDirectory == directory)
                return false;
            CacheDirectory = directory;
            Failures.clear();
            Revalidated.clear();
            ++DirectoryGeneration;
        }
        ++Generation;   // what a reader holds came from the cache of the other server
        return true;
    }

    bool RemoteLibraries::validId(const QString &id) {
        static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F-]{8,64}$"));
        return pattern.match(id).hasMatch();
    }

    QString RemoteLibraries::entryPath(const QString &directory, const QString &projectId, const QString &commitId) {
        return QDir(directory).filePath(projectId + QLatin1Char('/') + commitId);
    }

    int RemoteLibraries::fetchCount() const { return Fetches.load(); }

    quint64 RemoteLibraries::generation() const { return Generation.load(); }

    void RemoteLibraries::cancel() { Cancelled = true; }

    QString RemoteLibraries::failure(const QString &projectId, const QString &commitId) const {
        std::lock_guard lock(Mutex);
        const auto it = Failures.find(nodeKey(projectId, commitId));
        return it == Failures.end() ? QString() : it->second;
    }

    void RemoteLibraries::clearFailures() {
        {
            std::lock_guard lock(Mutex);
            Failures.clear();
        }
        ++Generation;
    }

    bool RemoteLibraries::needsRevalidation(const RemoteLibraryContent &content) {
        // Nothing found is looked at once more (the answer may have been incomplete). The same for a name that could not be read.
        return !content.NameResolved || (!content.HasSources && content.Usages.isEmpty()) || (content.HasSources && content.Sources.isEmpty());
    }

    std::optional<RemoteLibraryContent> RemoteLibraries::cached(const QString &projectId, const QString &commitId, bool needSources) const {
        return cachedIn(cacheDirectory(), projectId, commitId, needSources);
    }

    std::optional<RemoteLibraryContent> RemoteLibraries::cachedIn(const QString &cacheDir, const QString &projectId, const QString &commitId,
                                                                  bool needSources) const {
        if (cacheDir.isEmpty() || !validId(projectId) || !validId(commitId))
            return std::nullopt;

        const QDir directory(entryPath(cacheDir, projectId, commitId));
        // An entry that is upgraded while it is read: meta.json is replaced, the files of the old version are removed afterwards.
        // If a file it names is gone, meta.json has been replaced meanwhile, so it is read once more.
        for (int attempt = 0; attempt < 2; ++attempt) {
            QFile metaFile(directory.filePath(QStringLiteral("meta.json")));
            if (!metaFile.open(QIODevice::ReadOnly))
                return std::nullopt;
            const auto document = QJsonDocument::fromJson(metaFile.readAll());
            if (!document.isObject())
                return std::nullopt;
            const auto meta = document.object();

            RemoteLibraryContent content;
            content.ProjectId = projectId;
            content.CommitId = commitId;
            content.ProjectName = meta.value("projectName").toString();
            content.NameResolved = meta.value("nameResolved").toBool(true);
            content.HasSources = meta.value("hasSources").toBool();
            if (needSources && !content.HasSources)
                return std::nullopt;

            for (const auto &usage : meta.value("usages").toArray())
                content.Usages.append(usageFromJson(usage.toObject()));

            bool complete = true;
            if (content.HasSources) {
                int number = 0;
                for (const auto &entry : meta.value("blocks").toArray()) {
                    const auto block = entry.toObject();
                    const auto fileName = block.value("file").toString();
                    // The file names are written by store(); anything else is a damaged entry.
                    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\')))
                        return std::nullopt;
                    QFile file(directory.filePath(fileName));
                    if (!file.open(QIODevice::ReadOnly)) {
                        complete = false;
                        break;
                    }
                    ++number;
                    LibrarySource source;
                    source.Name = QStringLiteral("%1#%2").arg(displayName(content.ProjectName, projectId, commitId)).arg(number);
                    source.Language = block.value("language").toString();
                    source.Text = QString::fromUtf8(file.readAll());
                    content.Sources.append(source);
                }
                if (complete) {
                    for (const auto &source : std::as_const(content.Sources))
                        for (const auto &package : CodeScanner::scan(source.Text).Packages)
                            if (!content.Packages.contains(package.Name))
                                content.Packages.append(package.Name);
                }
            }
            if (complete)
                return content;
        }
        return std::nullopt;
    }

    bool RemoteLibraries::store(const QString &cacheDir, const RemoteLibraryContent &content, QString &error) {
        if (cacheDir.isEmpty()) {
            error = QStringLiteral("There is no cache folder.");
            return false;
        }
        if (!validId(content.ProjectId) || !validId(content.CommitId)) {
            error = QStringLiteral("The id of the project or commit is not a UUID.");
            return false;
        }

        const QDir projectDirectory(QDir(cacheDir).filePath(content.ProjectId));
        if (!QDir().mkpath(projectDirectory.path())) {
            error = QStringLiteral("The cache folder cannot be created.");
            return false;
        }
        const auto finalPath = entryPath(cacheDir, content.ProjectId, content.CommitId);
        const bool upgrade = QFileInfo::exists(finalPath);

        // A new entry is written to a temporary folder next to the final place and renamed: it is complete or not there. An
        // existing entry (one that is looked at again) is upgraded in place: the new files get new names and are written first,
        // then meta.json, which names them, is replaced atomically. A reader sees the old entry or the new one, never nothing.
        QTemporaryDir temporary(projectDirectory.filePath(QStringLiteral(".tmp-XXXXXX")));
        QString targetDirectory = finalPath;
        if (!upgrade) {
            if (!temporary.isValid()) {
                error = QStringLiteral("The cache folder cannot be written.");
                return false;
            }
            temporary.setAutoRemove(true);
            targetDirectory = temporary.path();
        }

        if (upgrade) {
            // Files of an earlier upgrade that meta.json does not name any more (it was interrupted before the cleanup).
            QFile oldMeta(QDir(finalPath).filePath(QStringLiteral("meta.json")));
            QStringList named;
            if (oldMeta.open(QIODevice::ReadOnly)) {
                for (const auto &entry : QJsonDocument::fromJson(oldMeta.readAll()).object().value("blocks").toArray())
                    named.append(entry.toObject().value("file").toString());
                for (const auto &fileName : QDir(finalPath).entryList({QStringLiteral("block-*")}, QDir::Files))
                    if (!named.contains(fileName))
                        QFile::remove(QDir(finalPath).filePath(fileName));
            }
        }

        const auto token = QUuid::createUuid().toString(QUuid::Id128).left(8);
        QJsonArray blocks;
        QStringList written;
        if (content.HasSources) {
            int number = 0;
            for (const auto &source : content.Sources) {
                ++number;
                const auto fileName = QStringLiteral("block-%1-%2.%3").arg(token).arg(number, 3, 10, QLatin1Char('0'))
                                              .arg(source.Language == QLatin1String("KerML") ? QStringLiteral("kerml") : QStringLiteral("sysml"));
                if (!writeFile(QDir(targetDirectory).filePath(fileName), source.Text.toUtf8())) {
                    error = QStringLiteral("The cache folder cannot be written.");
                    return false;
                }
                written.append(fileName);
                blocks.append(QJsonObject{{"file", fileName}, {"language", source.Language}});
            }
        }
        QJsonArray usages;
        for (const auto &usage : content.Usages)
            usages.append(usageToJson(usage));
        const QJsonObject meta{{"projectName", content.ProjectName}, {"nameResolved", content.NameResolved},
                               {"hasSources", content.HasSources}, {"usages", usages}, {"blocks", blocks}};
        const auto metaBytes = QJsonDocument(meta).toJson(QJsonDocument::Indented);

        if (upgrade) {
            QSaveFile metaFile(QDir(targetDirectory).filePath(QStringLiteral("meta.json")));
            if (!metaFile.open(QIODevice::WriteOnly) || metaFile.write(metaBytes) != metaBytes.size() || !metaFile.commit()) {
                for (const auto &fileName : std::as_const(written))
                    QFile::remove(QDir(targetDirectory).filePath(fileName));
                error = QStringLiteral("The cache entry cannot be updated.");
                return false;
            }
            // The files of the old version are no longer named by meta.json.
            const auto stale = QDir(targetDirectory).entryList({QStringLiteral("block-*")}, QDir::Files);
            for (const auto &fileName : stale)
                if (!written.contains(fileName))
                    QFile::remove(QDir(targetDirectory).filePath(fileName));
            return true;
        }

        if (!writeFile(QDir(targetDirectory).filePath(QStringLiteral("meta.json")), metaBytes)) {
            error = QStringLiteral("The cache folder cannot be written.");
            return false;
        }
        temporary.setAutoRemove(false);
        if (!QDir().rename(temporary.path(), finalPath)) {
            QDir(temporary.path()).removeRecursively();
            if (cachedIn(cacheDir, content.ProjectId, content.CommitId, false).has_value())
                return true;   // another thread was faster
            error = QStringLiteral("The cache entry cannot be completed.");
            return false;
        }
        return true;
    }

    bool RemoteLibraries::fetchAndStore(const QString &cacheDir, quint64 generation, const QString &projectId, const QString &commitId,
                                        bool needSources, bool recordFailure) {
        const auto key = nodeKey(projectId, commitId);
        QString message;
        ++Generation;   // whatever happens, a reader looks again
        try {
            if (Cancelled)
                throw std::runtime_error("The application is closing.");
            if (!validId(projectId) || !validId(commitId))
                throw std::runtime_error("The id of the project or commit is not a UUID.");
            ++Fetches;
            auto content = Fetch(projectId, commitId, needSources, Cancelled);
            if (Cancelled)
                throw std::runtime_error("The application is closing.");
            // The server was changed while this was fetched: it came from the other server and does not belong into this cache.
            if (DirectoryGeneration.load() != generation)
                throw std::runtime_error("The server was changed.");
            content.ProjectId = projectId;
            content.CommitId = commitId;
            content.HasSources = needSources;
            if (!needSources)
                content.Sources.clear();
            if (!store(cacheDir, content, message))
                throw std::runtime_error(message.toStdString());
            std::lock_guard lock(Mutex);
            Failures.erase(key);
            ++Generation;
            return true;
        } catch (const std::exception &ex) {
            message = QString::fromUtf8(ex.what());
        }
        if (!recordFailure || DirectoryGeneration.load() != generation)
            return false;
        std::lock_guard lock(Mutex);
        Failures[key] = message.isEmpty() ? QStringLiteral("unknown error") : message;
        ++Generation;
        return false;
    }

    void RemoteLibraries::revalidate(const QString &cacheDir, quint64 generation, const QString &projectId, const QString &commitId,
                                     bool needSources) {
        const auto content = cachedIn(cacheDir, projectId, commitId, needSources);
        if (!content || !needsRevalidation(*content))
            return;
        {
            std::lock_guard lock(Mutex);
            if (!Revalidated.insert(nodeKey(projectId, commitId)).second)
                return;
        }
        // A failure here is no problem: the entry that exists stays valid and in use.
        fetchAndStore(cacheDir, generation, projectId, commitId, needSources, false);
    }

    int RemoteLibraries::ensureClosure(const QString &projectId, const QString &commitId) {
        const int before = Fetches.load();
        // Everything below works in the cache of the server that is set now; if the server changes, the download stops.
        QString dir;
        quint64 generation;
        {
            std::lock_guard lock(Mutex);
            Failures.clear();   // a new attempt: what failed before is tried again
            dir = CacheDirectory;
            generation = DirectoryGeneration.load();
        }
        ++Generation;
        const auto stopped = [&]() { return Cancelled.load() || DirectoryGeneration.load() != generation; };

        const auto rootKey = nodeKey(projectId, commitId);
        auto root = cachedIn(dir, projectId, commitId, false);
        if (!root && !stopped() && fetchAndStore(dir, generation, projectId, commitId, false, true))
            root = cachedIn(dir, projectId, commitId, false);
        else if (root && !stopped())
            revalidate(dir, generation, projectId, commitId, false);
        root = cachedIn(dir, projectId, commitId, false);
        if (!root)
            return Fetches.load() - before;

        std::set<QString> visited{rootKey};
        std::vector<RemoteUsageRef> queue(root->Usages.begin(), root->Usages.end());
        while (!queue.empty() && !stopped()) {
            const auto usage = queue.back();
            queue.pop_back();
            const auto key = nodeKey(usage.ProjectId, usage.CommitId);
            if (!visited.insert(key).second)
                continue;
            auto content = cachedIn(dir, usage.ProjectId, usage.CommitId, true);
            if (!content && fetchAndStore(dir, generation, usage.ProjectId, usage.CommitId, true, true))
                content = cachedIn(dir, usage.ProjectId, usage.CommitId, true);
            else if (content && !stopped()) {
                revalidate(dir, generation, usage.ProjectId, usage.CommitId, true);
                content = cachedIn(dir, usage.ProjectId, usage.CommitId, true);
            }
            if (!content)
                continue;
            for (const auto &next : std::as_const(content->Usages))
                queue.push_back(next);
        }
        return Fetches.load() - before;
    }

    RemoteClosure RemoteLibraries::closure(const QString &projectId, const QString &commitId) const {
        RemoteClosure result;
        const auto dir = cacheDirectory();
        const auto rootKey = nodeKey(projectId, commitId);
        const auto root = cachedIn(dir, projectId, commitId, false);
        if (!root) {
            const auto reason = failure(projectId, commitId);
            result.Missing = true;
            result.Failed = !reason.isEmpty();
            result.Problems.append(reason.isEmpty() ? QStringLiteral("The project usages of this document are not downloaded yet.")
                                                    : QStringLiteral("The project usages of this document could not be downloaded: %1").arg(reason));
            return result;
        }
        result.RootKnown = true;

        struct Node {
            RemoteLibraryContent Content;
            QString Name;
            std::vector<int> Dependencies;
            int Level = 1;
            bool InCycle = false;
            bool IsLibrary = false;     ///< In the cache and with SysML / KerML text
        };
        std::vector<Node> nodes;
        std::map<QString, int> indexOf;

        std::function<int(const RemoteUsageRef &)> visit = [&](const RemoteUsageRef &usage) -> int {
            const auto key = nodeKey(usage.ProjectId, usage.CommitId);
            if (key == rootKey)
                return -1;
            const auto known = indexOf.find(key);
            if (known != indexOf.end())
                return known->second;

            const int index = int(nodes.size());
            indexOf[key] = index;
            nodes.emplace_back();
            nodes[index].Name = displayName(usage.ProjectName, usage.ProjectId, usage.CommitId);
            const auto content = cachedIn(dir, usage.ProjectId, usage.CommitId, true);
            if (!content) {
                const auto reason = failure(usage.ProjectId, usage.CommitId);
                result.Missing = true;
                result.Failed = result.Failed || !reason.isEmpty();
                // The server answers 404 both for a deleted project and for one the user may not read.
                const bool notFound = reason.contains(QStringLiteral("not found"), Qt::CaseInsensitive) || reason.contains(QStringLiteral("404"));
                result.Problems.append(reason.isEmpty()
                                               ? QStringLiteral("The project '%1' is not downloaded yet.").arg(nodes[index].Name)
                                               : notFound
                                               ? QStringLiteral("The project '%1' could not be downloaded: %2 It does not exist, or you have no access to it.").arg(nodes[index].Name, reason)
                                               : QStringLiteral("The project '%1' could not be downloaded: %2").arg(nodes[index].Name, reason));
                nodes[index].Content.ProjectId = usage.ProjectId;
                nodes[index].Content.CommitId = usage.CommitId;
                nodes[index].Content.HasSources = false;
                return index;
            }
            nodes[index].Content = *content;
            if (!content->ProjectName.isEmpty())
                nodes[index].Name = displayName(content->ProjectName, usage.ProjectId, usage.CommitId);
            if (content->Sources.isEmpty())
                result.Problems.append(QStringLiteral("The project '%1' contains no SysML or KerML text.").arg(nodes[index].Name));
            else
                nodes[index].IsLibrary = true;
            for (const auto &next : std::as_const(content->Usages)) {
                const int dependency = visit(next);
                if (dependency >= 0 && dependency != index)
                    nodes[index].Dependencies.push_back(dependency);
            }
            return index;
        };
        for (const auto &usage : std::as_const(root->Usages))
            visit(usage);

        // Tarjan: the strongly connected components (projects that use each other) are completed dependencies first.
        const int count = int(nodes.size());
        std::vector<int> number(count, -1);
        std::vector<int> low(count, 0);
        std::vector<bool> onStack(count, false);
        std::vector<int> stack;
        std::vector<std::vector<int>> components;
        int counter = 0;
        std::function<void(int)> connect = [&](int node) {
            number[node] = low[node] = counter++;
            stack.push_back(node);
            onStack[node] = true;
            for (const int dependency : nodes[node].Dependencies) {
                if (number[dependency] < 0) {
                    connect(dependency);
                    low[node] = (std::min)(low[node], low[dependency]);
                } else if (onStack[dependency]) {
                    low[node] = (std::min)(low[node], number[dependency]);
                }
            }
            if (low[node] == number[node]) {
                std::vector<int> component;
                int member;
                do {
                    member = stack.back();
                    stack.pop_back();
                    onStack[member] = false;
                    component.push_back(member);
                } while (member != node);
                components.push_back(std::move(component));
            }
        };
        for (int node = 0; node < count; ++node)
            if (number[node] < 0)
                connect(node);

        for (const auto &component : components) {
            int level = 1;
            for (const int member : component)
                for (const int dependency : nodes[member].Dependencies)
                    if (std::find(component.begin(), component.end(), dependency) == component.end())
                        level = (std::max)(level, nodes[dependency].Level + 1);
            level = (std::min)(level, LibraryWorkspace::MaxLibraryLevel);
            for (const int member : component) {
                nodes[member].Level = level;
                nodes[member].InCycle = component.size() > 1;
            }
        }

        for (auto &node : nodes) {
            if (!node.IsLibrary)
                continue;
            RemoteLibrary library;
            library.Content = node.Content;
            library.Level = node.Level;
            library.InCycle = node.InCycle;
            library.Name = node.Name;
            result.MaxLevel = (std::max)(result.MaxLevel, library.Level);
            result.Libraries.append(library);
        }
        std::stable_sort(result.Libraries.begin(), result.Libraries.end(),
                         [](const RemoteLibrary &left, const RemoteLibrary &right) { return left.Level < right.Level; });
        return result;
    }
}
