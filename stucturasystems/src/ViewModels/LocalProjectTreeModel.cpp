//
// Created by Moritz Herzog on 21.10.26.
//

#include "LocalProjectTreeModel.h"

#include <algorithm>
#include "../Models/ItemModels/ProjectItemModel.h"

namespace StructuraSystems::Client {
    LocalProjectTreeModel::LocalProjectTreeModel(ProjectItemModel *source, QObject *parent) :
        QAbstractItemModel(parent), Source(source), Root(std::make_unique<Node>()) {
        if (Source != nullptr) {
            // The rebuild is cheap compared to the parsing of the projects, so every change resets the tree.
            connect(Source, &QAbstractItemModel::rowsInserted, this, &LocalProjectTreeModel::rebuild);
            connect(Source, &QAbstractItemModel::rowsRemoved, this, &LocalProjectTreeModel::rebuild);
            connect(Source, &QAbstractItemModel::modelReset, this, &LocalProjectTreeModel::rebuild);
            connect(Source, &QAbstractItemModel::dataChanged, this, &LocalProjectTreeModel::rebuild);
        }
        rebuild();
    }

    LocalProjectTreeModel::~LocalProjectTreeModel() = default;

    LocalProjectTreeModel::Node *LocalProjectTreeModel::nodeAt(const QModelIndex &index) const {
        return index.isValid() ? static_cast<Node *>(index.internalPointer()) : Root.get();
    }

    QModelIndex LocalProjectTreeModel::index(int row, int column, const QModelIndex &parent) const {
        if (column != 0 || row < 0 || parent.column() > 0)
            return {};
        const auto *parentNode = nodeAt(parent);
        if (row >= int(parentNode->Children.size()))
            return {};
        return createIndex(row, column, parentNode->Children[row].get());
    }

    QModelIndex LocalProjectTreeModel::parent(const QModelIndex &child) const {
        if (!child.isValid())
            return {};
        const auto *node = nodeAt(child);
        const auto *parentNode = node->Parent;
        if (parentNode == nullptr || parentNode == Root.get())
            return {};
        const auto &siblings = parentNode->Parent->Children;
        const auto it = std::find_if(siblings.begin(), siblings.end(),
                                     [parentNode](const auto &entry) { return entry.get() == parentNode; });
        return createIndex(int(it - siblings.begin()), 0, const_cast<Node *>(parentNode));
    }

    int LocalProjectTreeModel::rowCount(const QModelIndex &parent) const {
        if (parent.column() > 0)
            return 0;
        return int(nodeAt(parent)->Children.size());
    }

    int LocalProjectTreeModel::columnCount(const QModelIndex &) const { return 1; }

    QVariant LocalProjectTreeModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid())
            return {};
        const auto *node = nodeAt(index);
        switch (role) {
            case Qt::DisplayRole:
            case NameRole: return node->Name;
            case DescriptionRole: return node->Description;
            case IsFolderRole: return node->IsFolder;
            case SourceRowRole: return node->SourceRow;
            case PathRole: return node->Path;
            default: return {};
        }
    }

    QHash<int, QByteArray> LocalProjectTreeModel::roleNames() const {
        return {
            {Qt::DisplayRole, "display"},
            {NameRole, "name"},
            {DescriptionRole, "description"},
            {IsFolderRole, "isFolder"},
            {SourceRowRole, "sourceRow"},
            {PathRole, "path"}
        };
    }

    QString LocalProjectTreeModel::filterText() const { return FilterText; }

    void LocalProjectTreeModel::setFilterText(const QString &filterText) {
        if (filterText == FilterText)
            return;
        FilterText = filterText;
        rebuild();
        emit filterTextChanged();
    }

    int LocalProjectTreeModel::count() const { return Count; }

    void LocalProjectTreeModel::sortNode(Node &node) {
        std::stable_sort(node.Children.begin(), node.Children.end(), [](const auto &left, const auto &right) {
            if (left->IsFolder != right->IsFolder)
                return left->IsFolder;
            return left->Name.compare(right->Name, Qt::CaseInsensitive) < 0;
        });
        for (auto &child : node.Children)
            sortNode(*child);
    }

    void LocalProjectTreeModel::rebuild() {
        beginResetModel();
        Root = std::make_unique<Node>();
        int count = 0;

        if (Source != nullptr) {
            const auto needle = FilterText.trimmed();
            for (int row = 0; row < Source->rowCount(QModelIndex()); ++row) {
                const auto entry = Source->index(row, 0, QModelIndex());
                const auto path = Source->data(entry, ProjectItemModel::NameRole).toString();
                if (!needle.isEmpty() && !path.contains(needle, Qt::CaseInsensitive))
                    continue;

                const auto segments = path.split('/', Qt::SkipEmptyParts);
                if (segments.isEmpty())
                    continue;

                Node *current = Root.get();
                QString partial;
                for (int i = 0; i < segments.size(); ++i) {
                    const bool leaf = i == segments.size() - 1;
                    partial += (partial.isEmpty() ? QString() : QStringLiteral("/")) + segments[i];
                    Node *next = nullptr;
                    if (!leaf) {
                        for (auto &child : current->Children)
                            if (child->IsFolder && child->Name == segments[i])
                                next = child.get();
                    }
                    if (next == nullptr) {
                        auto node = std::make_unique<Node>();
                        node->Name = segments[i];
                        node->Path = partial;
                        node->IsFolder = !leaf;
                        node->Parent = current;
                        if (leaf) {
                            node->Description = Source->data(entry, ProjectItemModel::DescriptionRole).toString();
                            node->SourceRow = row;
                        }
                        next = node.get();
                        current->Children.push_back(std::move(node));
                    }
                    current = next;
                }
                ++count;
            }
            sortNode(*Root);
        }
        endResetModel();

        if (count != Count) {
            Count = count;
            emit countChanged();
        }
    }
}
