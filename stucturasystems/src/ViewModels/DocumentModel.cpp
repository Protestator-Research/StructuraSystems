//
// Created by Moritz Herzog on 07.10.26.
//

#include "DocumentModel.h"

#include <algorithm>
#include <exception>
#include <utility>

#include <QFile>
#include <QRegularExpression>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <yaml-cpp/yaml.h>
#include <kerml/root/elements/Element.h>
#include <kerml/root/annotations/TextualRepresentation.h>
#include <sysmlv2/Parser.h>
#include <sysmlv2/ParserError.h>
#include <sysmlv2/rest/entities/JSONEntities.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/service/implementation/ElementNavigationService.h>

#include "../Models/Parser/StructuraSystemsParser.h"

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
            updateHeader(row);
            Rows.push_back(std::move(row));
        }
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
            {ProblemCountRole, "problemCount"}
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
        if (!validRow(row))
            return;
        auto &entry = Rows[row];
        const auto newBody = body.toStdString();
        if (entry.Element->body() == newBody)
            return;
        entry.Element->setBody(newBody);
        updateHeader(entry);
        markEdited(row, {BodyRole, HeaderTitleRole, HeaderAuthorRole});
    }

    void DocumentModel::setLanguage(int row, const QString &language) {
        if (!validRow(row))
            return;
        auto &entry = Rows[row];
        const auto normalized = normalizeLanguage(language);
        if (normalizeLanguage(QString::fromStdString(entry.Element->language())) == normalized)
            return;
        entry.Element->setLanguage(storedLanguage(normalized).toStdString());
        updateHeader(entry);
        markEdited(row, {LanguageRole, HeaderTitleRole, HeaderAuthorRole});
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
        if (!validRow(from) || !validRow(to) || from == to)
            return;

        beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
        auto moved = std::move(Rows[from]);
        Rows.erase(Rows.begin() + from);
        Rows.insert(Rows.begin() + to, std::move(moved));
        endMoveRows();

        syncElementOrder();
        setModified(true);
        emit edited();
    }

    void DocumentModel::insertElement(int row, const QString &language) {
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
        Rows.insert(Rows.begin() + row, std::move(entry));
        endInsertRows();

        emit countChanged();
        setModified(true);
        emit edited();
    }

    void DocumentModel::removeElement(int row) {
        if (!validRow(row))
            return;

        const auto element = std::static_pointer_cast<KerML::Entities::Element>(Rows[row].Element);
        beginRemoveRows(QModelIndex(), row, row);
        Rows.erase(Rows.begin() + row);
        Elements.erase(std::remove(Elements.begin(), Elements.end(), element), Elements.end());
        endRemoveRows();

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

    ParseResult DocumentModel::runParser(const QList<ParseInput> &inputs, const QString &documentTitle) {
        ParseResult result;
        for (const auto &input : inputs) {
            try {
                auto parsed = input.Language == "KerML"
                              ? SysMLv2::Files::Parser::parseKerML(input.Body)
                              : SysMLv2::Files::Parser::parseSysMLv2(input.Body);

                for (const auto &error : parsed.second) {
                    Problem problem;
                    problem.Message = QString::fromStdString(error->description());
                    problem.Line = error->getLine();
                    problem.Column = error->getColumn();
                    problem.DocumentTitle = documentTitle;
                    problem.Row = input.Row;
                    problem.Severity = error->errorType() == SysMLv2::Files::ErrorType::WARNING ? 2 : 3;
                    result.Problems.append(problem);
                }
                for (const auto &element : parsed.first)
                    result.InstanceElements.push_back(element);
            } catch (const std::exception &ex) {
                Problem problem;
                problem.Message = QString::fromUtf8(ex.what());
                problem.DocumentTitle = documentTitle;
                problem.Row = input.Row;
                result.Problems.append(problem);
            }
        }
        return result;
    }

    void DocumentModel::applyParseResult(ParseResult result) {
        // Previous errors and instance elements are replaced, not accumulated.
        InstanceElements = std::move(result.InstanceElements);
        ParserProblems = std::move(result.Problems);

        for (size_t i = 0; i < Rows.size(); i++) {
            const int rowIndex = int(i);
            const int errors = int(std::count_if(ParserProblems.begin(), ParserProblems.end(),
                                                 [rowIndex](const Problem &problem) { return problem.Row == rowIndex; }));
            if (Rows[i].ProblemCount != errors) {
                Rows[i].ProblemCount = errors;
                const auto changed = index(rowIndex);
                emit dataChanged(changed, changed, {ProblemCountRole});
            }
        }
        emit problemsChanged();
    }

    QList<Problem> DocumentModel::parse() {
        applyParseResult(runParser(parseInputs(), title()));
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
