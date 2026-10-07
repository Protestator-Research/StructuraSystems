//
// Created by Moritz Herzog on 07.10.26.
//

#include "ProjectFilterModel.h"

#include "../Models/ItemModels/ProjectItemModel.h"

namespace StructuraSystems::Client {
    ProjectFilterModel::ProjectFilterModel(QObject *parent) : QSortFilterProxyModel(parent) {
        connect(this, &QAbstractItemModel::rowsInserted, this, &ProjectFilterModel::updateCount);
        connect(this, &QAbstractItemModel::rowsRemoved, this, &ProjectFilterModel::updateCount);
        connect(this, &QAbstractItemModel::modelReset, this, &ProjectFilterModel::updateCount);
        connect(this, &QAbstractItemModel::layoutChanged, this, &ProjectFilterModel::updateCount);
    }

    QString ProjectFilterModel::filterText() const {
        return FilterText;
    }

    void ProjectFilterModel::setFilterText(const QString &filterText) {
        if (filterText == FilterText)
            return;
        FilterText = filterText;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange();
#else
        invalidateFilter();
#endif
        emit filterTextChanged();
    }

    int ProjectFilterModel::count() const {
        return Count;
    }

    int ProjectFilterModel::sourceRow(int row) const {
        const auto source = mapToSource(index(row, 0));
        return source.isValid() ? source.row() : -1;
    }

    bool ProjectFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
        const auto needle = FilterText.trimmed();
        if (needle.isEmpty())
            return true;
        const auto *model = sourceModel();
        if (model == nullptr)
            return false;
        const auto entry = model->index(sourceRow, 0, sourceParent);
        const auto name = model->data(entry, ProjectItemModel::NameRole).toString();
        const auto description = model->data(entry, ProjectItemModel::DescriptionRole).toString();
        return name.contains(needle, Qt::CaseInsensitive) || description.contains(needle, Qt::CaseInsensitive);
    }

    void ProjectFilterModel::updateCount() {
        const int count = rowCount();
        if (count == Count)
            return;
        Count = count;
        emit countChanged();
    }
}
