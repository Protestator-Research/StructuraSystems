//
// Created by Moritz Herzog on 18.10.26.
//

#include "BranchListModel.h"

#include <algorithm>

namespace StructuraSystems::Client {
    BranchListModel::BranchListModel(QObject *parent) : QAbstractListModel(parent) {
    }

    int BranchListModel::rowCount(const QModelIndex &parent) const {
        if (parent.isValid())
            return 0;
        return int(Branches.size());
    }

    QVariant BranchListModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= Branches.size())
            return {};

        const auto &branch = Branches.at(index.row());
        switch (role) {
            case Qt::DisplayRole:
            case NameRole:
                return branch.Name;
            case BranchIdRole:
                return branch.Id;
            case HeadCommitIdRole:
                return branch.HeadCommitId;
            case IsDefaultRole:
                return branch.IsDefault;
            case IsCurrentRole:
                return !CurrentBranchId.isEmpty() && branch.Id == CurrentBranchId;
            default:
                return {};
        }
    }

    QHash<int, QByteArray> BranchListModel::roleNames() const {
        return {
            {NameRole, "name"},
            {BranchIdRole, "branchId"},
            {HeadCommitIdRole, "headCommitId"},
            {IsDefaultRole, "isDefault"},
            {IsCurrentRole, "isCurrent"}
        };
    }

    int BranchListModel::count() const {
        return int(Branches.size());
    }

    QList<BranchInfo> BranchListModel::branches() const {
        return Branches;
    }

    BranchInfo BranchListModel::branchWithId(const QString &branchId) const {
        for (const auto &branch : Branches)
            if (branch.Id == branchId)
                return branch;
        return {};
    }

    void BranchListModel::setBranches(QList<BranchInfo> branches) {
        std::stable_sort(branches.begin(), branches.end(), [](const BranchInfo &left, const BranchInfo &right) {
            if (left.IsDefault != right.IsDefault)
                return left.IsDefault;
            return left.Name.compare(right.Name, Qt::CaseInsensitive) < 0;
        });

        const int oldCount = int(Branches.size());
        beginResetModel();
        Branches = std::move(branches);
        endResetModel();
        if (oldCount != int(Branches.size()))
            emit countChanged();
    }

    void BranchListModel::setCurrentBranch(const QString &branchId) {
        if (CurrentBranchId == branchId)
            return;
        CurrentBranchId = branchId;
        if (!Branches.isEmpty())
            emit dataChanged(index(0), index(int(Branches.size()) - 1), {IsCurrentRole});
    }

    void BranchListModel::clear() {
        if (Branches.isEmpty())
            return;
        beginResetModel();
        Branches.clear();
        endResetModel();
        emit countChanged();
    }
}
