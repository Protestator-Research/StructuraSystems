//
// Created by Moritz Herzog on 07.10.26.
//

#include "DocumentModel.h"

#include <algorithm>
#include <exception>
#include <unordered_set>
#include <utility>

#include <QFile>
#include <QRegularExpression>
#include <QVariantMap>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <yaml-cpp/yaml.h>
#include <kerml/root/elements/Element.h>
#include <kerml/root/elements/Relationship.h>
#include <kerml/root/namespaces/Namespace.h>
#include <kerml/root/namespaces/OwningMembership.h>
#include <kerml/root/annotations/TextualRepresentation.h>
#include <sysmlv2/Parser.h>
#include <sysmlv2/ParserError.h>
#include <sysmlv2/rest/entities/JSONEntities.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/service/implementation/ElementNavigationService.h>

#include "../Models/Libraries/DocumentWorkspace.h"
#include "../Models/Libraries/LibraryWorkspace.h"
#include "../Models/Parser/StructuraSystemsParser.h"
#include "../Models/Parser/Markdown/CommonmarkParser/cmark.h"

namespace StructuraSystems::Client {
    namespace {
        // Language strings as they are stored in the elements (and written to files by the StructuraSystemsParser).
        QString storedLanguage(const QString &normalized) {
            if (normalized == "YAML")
                return QStringLiteral("YaML");
            return normalized;
        }

        QString elementIdString(const std::shared_ptr<KerML::Entities::Element> &element) {
            return QString::fromStdString(boost::uuids::to_string(element->getId()));
        }

        QString yamlScalar(const YAML::Node &node, const char *key) {
            const YAML::Node value = node[key];
            if (value.IsDefined() && value.IsScalar())
                return QString::fromStdString(value.as<std::string>());
            return {};
        }

        /** True for an element whose body is just the [toc] marker. */
        bool isTocMarker(const QString &body) {
            return body.trimmed().toLower() == QStringLiteral("[toc]");
        }

        /** Concatenated text and code literals below a heading, line breaks become spaces. */
        QString headingTitle(cmark_node *heading) {
            QString title;
            cmark_iter *iter = cmark_iter_new(heading);
            cmark_event_type event;
            while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
                if (event != CMARK_EVENT_ENTER)
                    continue;
                cmark_node *node = cmark_iter_get_node(iter);
                switch (cmark_node_get_type(node)) {
                    case CMARK_NODE_TEXT:
                    case CMARK_NODE_CODE:
                        if (const char *literal = cmark_node_get_literal(node))
                            title += QString::fromUtf8(literal);
                        break;
                    case CMARK_NODE_SOFTBREAK:
                    case CMARK_NODE_LINEBREAK:
                        title += QLatin1Char(' ');
                        break;
                    default:
                        break;
                }
            }
            cmark_iter_free(iter);
            return title.trimmed();
        }

        bool fileFormatIsWritable(const QString &path) {
            return path.contains(".md") || path.contains(".kerml") || path.contains(".sysml") || path.contains(".sml");
        }
    }

    DocumentModel::DocumentModel(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit,
                                 QObject *parent) :
        QAbstractListModel(parent),
        Project(std::move(project)),
        Commit(std::move(commit)),
        IsOnline(false),
        IsLocalFile(true) {
        if (Commit != nullptr) {
            SysMLv2::API::ElementNavigationService elementService;
            Elements = elementService.getElements(Project, Commit);
        }
        loadRows();
    }

    DocumentModel::DocumentModel(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit,
                                 std::vector<std::shared_ptr<KerML::Entities::Element>> elements, QObject *parent) :
        QAbstractListModel(parent),
        Project(std::move(project)),
        Commit(std::move(commit)),
        Elements(std::move(elements)),
        IsOnline(true),
        IsLocalFile(false) {
        loadRows();
    }

    void DocumentModel::loadRows() {
        for (const auto &element : Elements) {
            if (element == nullptr)
                continue;
            auto type = element->getType();
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            if (type != SysMLv2::REST::TEXTUAL_REPRESENTATION_TYPE)
                continue;
            auto textualRepresentation = std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(element);
            if (textualRepresentation == nullptr)
                continue;

            Row row;
            row.Element = std::move(textualRepresentation);
            row.ElementTree = new ElementTreeModel(this);
            updateHeader(row);
            Rows.push_back(std::move(row));
        }
        rebuildTableOfContents();
    }

    void DocumentModel::rebuildTableOfContents() {
        QVariantList entries;
        for (size_t i = 0; i < Rows.size(); i++) {
            if (normalizeLanguage(QString::fromStdString(Rows[i].Element->language())) != "Markdown")
                continue;
            const auto &body = Rows[i].Element->body();
            if (isTocMarker(QString::fromStdString(body)))
                continue;

            cmark_node *document = cmark_parse_document(body.c_str(), body.size(), 0);
            if (document == nullptr)
                continue;
            cmark_iter *iter = cmark_iter_new(document);
            cmark_event_type event;
            while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
                if (event != CMARK_EVENT_ENTER)
                    continue;
                cmark_node *node = cmark_iter_get_node(iter);
                if (cmark_node_get_type(node) != CMARK_NODE_HEADING || cmark_node_get_heading_level(node) > 3)
                    continue;
                entries.append(QVariantMap{{"level", cmark_node_get_heading_level(node)},
                                           {"title", headingTitle(node)},
                                           {"row", int(i)}});
            }
            cmark_iter_free(iter);
            cmark_node_free(document);
        }

        if (entries == TableOfContents)
            return;
        TableOfContents = std::move(entries);
        emit tableOfContentsChanged();
    }

    QVariantList DocumentModel::tableOfContents() const {
        return TableOfContents;
    }

    void DocumentModel::updateHeader(Row &row) const {
        row.HeaderTitle.clear();
        row.HeaderAuthor.clear();
        if (normalizeLanguage(QString::fromStdString(row.Element->language())) != "YAML")
            return;

        try {
            // Same preprocessing as the former MarkdownElement, but tolerant against \r\n and longer separators.
            static const QRegularExpression separator(QStringLiteral("^[ \\t]*-{3,}[ \\t]*\\r?$"),
                                                       QRegularExpression::MultilineOption);
            QString yamlText = QString::fromStdString(row.Element->body());
            yamlText.remove(separator);
            if (yamlText.trimmed().isEmpty())
                return;

            const YAML::Node node = YAML::Load(yamlText.toStdString());
            if (!node.IsMap())
                return;

            row.HeaderTitle = yamlScalar(node, "name");
            if (row.HeaderTitle.isEmpty())
                row.HeaderTitle = yamlScalar(node, "title");
            row.HeaderAuthor = yamlScalar(node, "maintainer");
        } catch (const std::exception &) {
            row.HeaderTitle.clear();
            row.HeaderAuthor.clear();
        }
    }

    QString DocumentModel::normalizeLanguage(const QString &language) {
        const auto lower = language.trimmed().toLower();
        if (lower == "yaml")
            return QStringLiteral("YAML");
        if (lower == "sysml" || lower == "sysmlv2" || lower == "sysmd")
            return QStringLiteral("SysMLv2");
        if (lower == "kerml")
            return QStringLiteral("KerML");
        return QStringLiteral("Markdown");
    }

    int DocumentModel::rowCount(const QModelIndex &parent) const {
        if (parent.isValid())
            return 0;
        return int(Rows.size());
    }

    bool DocumentModel::validRow(int row) const {
        return row >= 0 && row < int(Rows.size());
    }

    QVariant DocumentModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || !validRow(index.row()))
            return {};

        const auto &row = Rows[index.row()];
        switch (role) {
            case Qt::DisplayRole:
            case BodyRole:
                return QString::fromStdString(row.Element->body());
            case LanguageRole:
                return normalizeLanguage(QString::fromStdString(row.Element->language()));
            case ElementIdRole:
                return elementIdString(row.Element);
            case HeaderTitleRole:
                return row.HeaderTitle;
            case HeaderAuthorRole:
                return row.HeaderAuthor;
            case SelectedRole:
                return row.Selected;
            case ProblemCountRole:
                return row.ProblemCount;
            case ElementTreeRole:
                return QVariant::fromValue(row.ElementTree);
            case ElementCountRole:
                return row.ElementTree ? row.ElementTree->count() : 0;
            default:
                return {};
        }
    }

    bool DocumentModel::setData(const QModelIndex &index, const QVariant &value, int role) {
        if (!index.isValid() || !validRow(index.row()))
            return false;

        switch (role) {
            case BodyRole:
                setBody(index.row(), value.toString());
                return true;
            case LanguageRole:
                setLanguage(index.row(), value.toString());
                return true;
            case SelectedRole: {
                auto &row = Rows[index.row()];
                const bool selected = value.toBool();
                if (row.Selected != selected) {
                    row.Selected = selected;
                    emit dataChanged(index, index, {SelectedRole});
                }
                return true;
            }
            default:
                return false;
        }
    }

    QHash<int, QByteArray> DocumentModel::roleNames() const {
        return {
            {BodyRole, "body"},
            {LanguageRole, "language"},
            {ElementIdRole, "elementId"},
            {HeaderTitleRole, "headerTitle"},
            {HeaderAuthorRole, "headerAuthor"},
            {SelectedRole, "selected"},
            {ProblemCountRole, "problemCount"},
            {ElementTreeRole, "elementTree"},
            {ElementCountRole, "elementCount"}
        };
    }

    QString DocumentModel::title() const {
        return Project ? QString::fromStdString(Project->getName()) : QString();
    }

    bool DocumentModel::modified() const {
        return Modified;
    }

    bool DocumentModel::isOnline() const {
        return IsOnline;
    }

    bool DocumentModel::isLocalFile() const {
        return IsLocalFile;
    }

    int DocumentModel::count() const {
        return int(Rows.size());
    }

    QString DocumentModel::projectId() const {
        return Project ? QString::fromStdString(boost::uuids::to_string(Project->getId())) : QString();
    }

    QString DocumentModel::commitId() const {
        return Commit ? QString::fromStdString(boost::uuids::to_string(Commit->getId())) : QString();
    }

    QString DocumentModel::branchId() const {
        return BranchId;
    }

    QString DocumentModel::branchName() const {
        return BranchName;
    }

    bool DocumentModel::detached() const {
        return Detached;
    }

    void DocumentModel::setBranch(const QString &branchId, const QString &branchName) {
        if (!IsOnline || (BranchId == branchId && BranchName == branchName))
            return;
        BranchId = branchId;
        BranchName = branchName;
        emit branchChanged();
    }

    void DocumentModel::setDetached(bool detached) {
        if (!IsOnline || Detached == detached)
            return;
        Detached = detached;
        emit detachedChanged();
    }

    bool DocumentModel::replaceContent(std::shared_ptr<SysMLv2::REST::Commit> commit,
                                       std::vector<std::shared_ptr<KerML::Entities::Element>> elements) {
        if (!IsOnline)
            return false;

        beginResetModel();
        Commit = std::move(commit);
        Elements = std::move(elements);
        for (auto &row : Rows)
            row.ElementTree->deleteLater();
        Rows.clear();
        loadRows();
        InstanceElements.clear();
        ParserProblems.clear();
        endResetModel();

        setModified(false);
        emit countChanged();
        emit identityChanged();
        emit problemsChanged();
        return true;
    }

    void DocumentModel::setModified(bool modified) {
        if (Modified == modified)
            return;
        Modified = modified;
        emit modifiedChanged();
    }

    void DocumentModel::markEdited(int row, const QList<int> &roles) {
        const auto changed = index(row);
        emit dataChanged(changed, changed, roles);
        setModified(true);
        emit edited();
    }

    void DocumentModel::setBody(int row, const QString &body) {
        if (Detached || !validRow(row))
            return;
        auto &entry = Rows[row];
        const auto newBody = body.toStdString();
        if (entry.Element->body() == newBody)
            return;
        entry.Element->setBody(newBody);
        updateHeader(entry);
        markEdited(row, {BodyRole, HeaderTitleRole, HeaderAuthorRole});
        rebuildTableOfContents();
    }

    void DocumentModel::setLanguage(int row, const QString &language) {
        if (Detached || !validRow(row))
            return;
        auto &entry = Rows[row];
        const auto normalized = normalizeLanguage(language);
        if (normalizeLanguage(QString::fromStdString(entry.Element->language())) == normalized)
            return;
        entry.Element->setLanguage(storedLanguage(normalized).toStdString());
        updateHeader(entry);
        markEdited(row, {LanguageRole, HeaderTitleRole, HeaderAuthorRole});
        rebuildTableOfContents();
    }

    void DocumentModel::syncElementOrder() {
        // The textual representations occupy the same slots in Elements as before, but in the order of the rows.
        size_t next = 0;
        for (auto &element : Elements) {
            if (next >= Rows.size())
                break;
            const bool isRowElement = std::any_of(Rows.begin(), Rows.end(), [&element](const Row &row) {
                return std::static_pointer_cast<KerML::Entities::Element>(row.Element) == element;
            });
            if (isRowElement)
                element = Rows[next++].Element;
        }
    }

    void DocumentModel::moveRow(int from, int to) {
        if (Detached || !validRow(from) || !validRow(to) || from == to)
            return;

        beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
        auto moved = std::move(Rows[from]);
        Rows.erase(Rows.begin() + from);
        Rows.insert(Rows.begin() + to, std::move(moved));
        endMoveRows();

        syncElementOrder();
        rebuildTableOfContents();
        setModified(true);
        emit edited();
    }

    void DocumentModel::insertElement(int row, const QString &language) {
        if (Detached)
            return;
        row = std::clamp(row, 0, int(Rows.size()));

        const auto normalized = normalizeLanguage(language);
        auto element = std::make_shared<KerML::Entities::TextualRepresentation>(storedLanguage(normalized).toStdString(), std::string());

        // Position in Elements: directly before the element that is currently at the requested row, else behind the last row.
        auto position = Elements.end();
        if (row < int(Rows.size())) {
            position = std::find(Elements.begin(), Elements.end(), std::static_pointer_cast<KerML::Entities::Element>(Rows[row].Element));
        } else if (!Rows.empty()) {
            position = std::find(Elements.begin(), Elements.end(), std::static_pointer_cast<KerML::Entities::Element>(Rows.back().Element));
            if (position != Elements.end())
                ++position;
        }

        beginInsertRows(QModelIndex(), row, row);
        Elements.insert(position, element);
        Row entry;
        entry.Element = element;
        entry.ElementTree = new ElementTreeModel(this);
        Rows.insert(Rows.begin() + row, std::move(entry));
        endInsertRows();

        rebuildTableOfContents();
        emit countChanged();
        setModified(true);
        emit edited();
    }

    void DocumentModel::removeElement(int row) {
        if (Detached || !validRow(row))
            return;

        const auto element = std::static_pointer_cast<KerML::Entities::Element>(Rows[row].Element);
        beginRemoveRows(QModelIndex(), row, row);
        Rows[row].ElementTree->deleteLater();
        Rows.erase(Rows.begin() + row);
        Elements.erase(std::remove(Elements.begin(), Elements.end(), element), Elements.end());
        endRemoveRows();

        rebuildTableOfContents();
        emit countChanged();
        setModified(true);
        emit edited();
    }

    QStringList DocumentModel::selectedElementIds() const {
        QStringList ids;
        for (const auto &row : Rows)
            if (row.Selected)
                ids << elementIdString(row.Element);
        return ids;
    }

    void DocumentModel::clearSelection() {
        for (size_t i = 0; i < Rows.size(); i++) {
            if (Rows[i].Selected) {
                Rows[i].Selected = false;
                const auto changed = index(int(i));
                emit dataChanged(changed, changed, {SelectedRole});
            }
        }
    }

    std::shared_ptr<SysMLv2::REST::Project> DocumentModel::getProject() const {
        return Project;
    }

    std::shared_ptr<SysMLv2::REST::Commit> DocumentModel::getCommit() const {
        return Commit;
    }

    bool DocumentModel::save(const QString &basePath) {
        if (Project == nullptr)
            return false;

        const auto path = basePath + "/" + QString::fromStdString(Project->getName());
        if (fileFormatIsWritable(path)) {
            // The StructuraSystemsParser opens files read/write without truncating, so a shorter text would leave
            // the tail of the old content behind. Truncate before writing.
            QFile file(path);
            if (file.exists() && file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                file.close();
        }

        StructuraSystemsParser parser;
        parser.writeFile(path, Elements);

        if (!QFile::exists(path))
            return false;
        setModified(false);
        return true;
    }

    QList<ParseInput> DocumentModel::parseInputs() const {
        QList<ParseInput> inputs;
        for (size_t i = 0; i < Rows.size(); i++) {
            const auto language = normalizeLanguage(QString::fromStdString(Rows[i].Element->language()));
            if (language != "SysMLv2" && language != "KerML")
                continue;
            inputs.append({int(i), language, Rows[i].Element->body()});
        }
        return inputs;
    }

    namespace {
        /** Walks the owners of the first element up to the root namespace. */
        std::shared_ptr<KerML::Entities::Element>
        findRootElement(const std::vector<std::shared_ptr<KerML::Entities::Element>> &elements) {
            std::shared_ptr<KerML::Entities::Element> current;
            for (const auto &element : elements) {
                if (element != nullptr) {
                    current = element;
                    break;
                }
            }
            if (current == nullptr)
                return nullptr;
            while (const auto owner = current->owner())
                current = owner;
            return current;
        }

        /** Adds the owned elements a relationship contributes: its owned related elements and, for an owning membership, the member. */
        void appendRelationshipChildren(const std::shared_ptr<KerML::Entities::Relationship> &relationship,
                                        std::vector<std::shared_ptr<KerML::Entities::Element>> &children) {
            if (relationship == nullptr)
                return;
            for (const auto &related : relationship->ownedRelatedElement())
                children.push_back(related);
            if (const auto membership = std::dynamic_pointer_cast<KerML::Entities::OwningMembership>(relationship))
                children.push_back(membership->ownedMemberElement());
        }

        /**
         * Children of an element in the sense of KerML: the elements owned through its owned relationships (and the members
         * of owning memberships), plus owned elements that are no plain relationships. Relationships are never returned.
         */
        std::vector<std::shared_ptr<KerML::Entities::Element>>
        ownedChildren(const std::shared_ptr<KerML::Entities::Element> &element) {
            std::vector<std::shared_ptr<KerML::Entities::Element>> candidates;
            for (const auto &relationship : element->ownedRelationship())
                appendRelationshipChildren(relationship, candidates);
            for (const auto &owned : element->ownedElements()) {
                if (owned == nullptr)
                    continue;
                const auto relationship = std::dynamic_pointer_cast<KerML::Entities::Relationship>(owned);
                if (relationship != nullptr && std::dynamic_pointer_cast<KerML::Entities::Namespace>(owned) == nullptr)
                    appendRelationshipChildren(relationship, candidates);
                else
                    candidates.push_back(owned);
            }

            std::vector<std::shared_ptr<KerML::Entities::Element>> children;
            std::unordered_set<const KerML::Entities::Element *> seen;
            for (const auto &candidate : candidates) {
                if (candidate != nullptr && seen.insert(candidate.get()).second)
                    children.push_back(candidate);
            }
            return children;
        }

        /** Appends the element and all its children in pre-order. Elements seen before are skipped. */
        void appendElementTree(const std::shared_ptr<KerML::Entities::Element> &element, int parent,
                               std::unordered_set<const KerML::Entities::Element *> &visited,
                               QList<ElementTreeNode> &nodes) {
            if (element == nullptr || !visited.insert(element.get()).second)
                return;

            ElementTreeNode node;
            node.Type = QString::fromStdString(element->getType());
            auto name = element->effectiveName();
            if (!name.has_value())
                name = element->declaredName();
            if (name.has_value())
                node.Name = QString::fromStdString(*name);
            node.Parent = parent;
            const int index = int(nodes.size());
            nodes.append(node);

            for (const auto &child : ownedChildren(element))
                appendElementTree(child, index, visited, nodes);
        }
    }

    ParseResult DocumentModel::runParser(const QList<ParseInput> &inputs, const QString &documentTitle,
                                         std::shared_ptr<DocumentWorkspace> workspace) {
        ParseResult result;
        if (workspace == nullptr)
            workspace = std::make_shared<DocumentWorkspace>();

        std::vector<DocumentSource> sources;
        for (const auto &input : inputs)
            sources.push_back({input.Row, input.Language == "KerML", input.Body});

        try {
            // The element trees are made while the workspace is locked: afterwards another thread may recycle the workspace and
            // take the model apart. Only plain data (and shared_ptrs, which are read on the GUI thread while the document is
            // open) leave this function.
            auto resolved = workspace->update(sources, documentTitle, [&result](const WorkspaceRow &row) {
                for (const auto &element : row.Elements)
                    result.InstanceElements.push_back(element);

                if (!row.HasSyntaxErrors) {
                    const std::shared_ptr<KerML::Entities::Element> root =
                            row.Root != nullptr ? std::static_pointer_cast<KerML::Entities::Element>(row.Root) : findRootElement(row.Elements);
                    if (root != nullptr) {
                        std::unordered_set<const KerML::Entities::Element *> visited;
                        appendElementTree(root, -1, visited, result.ElementTrees[row.Row]);
                    }
                }
            });

            for (const auto &diagnostic : std::as_const(resolved.Diagnostics)) {
                Problem problem;
                problem.Message = diagnostic.Message;
                problem.Line = diagnostic.Line;
                problem.Column = diagnostic.Column;
                problem.DocumentTitle = documentTitle;
                problem.Row = diagnostic.Row;
                problem.Severity = diagnostic.Severity;
                result.Problems.append(problem);
            }
        } catch (const std::exception &ex) {
            Problem problem;
            problem.Message = QString::fromUtf8(ex.what());
            problem.DocumentTitle = documentTitle;
            result.Problems.append(problem);
        }
        return result;
    }

    std::shared_ptr<DocumentWorkspace> DocumentModel::workspace() {
        if (Workspace == nullptr)
            Workspace = std::make_shared<DocumentWorkspace>(Libraries, OwnFile);
        return Workspace;
    }

    void DocumentModel::attachLibraries(std::shared_ptr<LibraryWorkspace> libraries, const QString &ownFile) {
        Libraries = std::move(libraries);
        OwnFile = ownFile;
        if (Workspace != nullptr)
            Workspace->setLibraries(Libraries, OwnFile);
    }

    bool DocumentModel::hasWorkspace() const {
        return Workspace != nullptr;
    }

    void DocumentModel::adoptWorkspace(std::shared_ptr<DocumentWorkspace> workspace) {
        Workspace = std::move(workspace);
        if (Workspace != nullptr)
            Workspace->setLibraries(Libraries, OwnFile);
    }

    std::shared_ptr<DocumentWorkspace> DocumentModel::releaseWorkspace() {
        return std::exchange(Workspace, nullptr);
    }

    void DocumentModel::applyParseResult(ParseResult result) {
        // Previous errors and instance elements are replaced, not accumulated.
        InstanceElements = std::move(result.InstanceElements);
        ParserProblems = std::move(result.Problems);

        for (size_t i = 0; i < Rows.size(); i++) {
            const int rowIndex = int(i);
            const int errors = int(std::count_if(ParserProblems.begin(), ParserProblems.end(),
                                                 [rowIndex](const Problem &problem) { return problem.Row == rowIndex; }));
            QList<int> changedRoles;
            if (Rows[i].ProblemCount != errors) {
                Rows[i].ProblemCount = errors;
                changedRoles.append(ProblemCountRole);
            }
            const int elementsBefore = Rows[i].ElementTree->count();
            Rows[i].ElementTree->setNodes(result.ElementTrees.value(rowIndex));
            if (Rows[i].ElementTree->count() != elementsBefore)
                changedRoles.append(ElementCountRole);
            if (!changedRoles.isEmpty()) {
                const auto changed = index(rowIndex);
                emit dataChanged(changed, changed, changedRoles);
            }
        }
        emit problemsChanged();
    }

    QList<Problem> DocumentModel::parse() {
        applyParseResult(runParser(parseInputs(), title(), workspace()));
        return ParserProblems;
    }

    QList<Problem> DocumentModel::problems() const {
        return ParserProblems;
    }

    const std::vector<std::shared_ptr<KerML::Entities::Element>> &DocumentModel::instanceElements() const {
        return InstanceElements;
    }

    std::shared_ptr<SysMLv2::REST::CommitRequest> DocumentModel::buildCommitRequest(const QString &description) const {
        std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> requestedChange;
        for (const auto &element : Elements) {
            auto dataVersion = std::make_shared<SysMLv2::REST::DataVersion>(boost::uuids::random_generator()(), element);
            requestedChange.push_back(dataVersion);
        }
        return std::make_shared<SysMLv2::REST::CommitRequest>(description.toStdString(), requestedChange);
    }

    void DocumentModel::applyCommit(std::shared_ptr<SysMLv2::REST::Commit> commit) {
        Commit = std::move(commit);
        if (IsOnline && !IsLocalFile)
            setModified(false);
        emit identityChanged();
    }

    void DocumentModel::applyUpload(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit) {
        Project = std::move(project);
        Commit = std::move(commit);
        IsOnline = true;
        emit identityChanged();
    }
}
