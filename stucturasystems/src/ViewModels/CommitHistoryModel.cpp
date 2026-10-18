//
// Created by Moritz Herzog on 18.10.26.
//

#include "CommitHistoryModel.h"

#include <algorithm>

#include <QHash>
#include <QSet>

namespace StructuraSystems::Client {
    CommitHistoryModel::CommitHistoryModel(QObject *parent) : QAbstractListModel(parent) {
    }

    int CommitHistoryModel::rowCount(const QModelIndex &parent) const {
        if (parent.isValid())
            return 0;
        return int(Entries.size());
    }

    QVariant CommitHistoryModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= Entries.size())
            return {};

        const auto &entry = Entries.at(index.row());
        switch (role) {
            case Qt::DisplayRole:
            case DescriptionRole:
                return entry.Commit.Description;
            case CommitIdRole:
                return entry.Commit.Id;
            case ShortIdRole:
                return entry.Commit.Id.left(8);
            case CreatedRole:
                return entry.Commit.Created;
            case IsCurrentRole:
                return !CurrentCommitId.isEmpty() && entry.Commit.Id == CurrentCommitId;
            case IsBranchHeadRole:
                return entry.IsBranchHead;
            case HeadOfRole:
                return entry.HeadOf;
            case OnBranchRole:
                return entry.OnBranch;
            default:
                return {};
        }
    }

    QHash<int, QByteArray> CommitHistoryModel::roleNames() const {
        return {
            {CommitIdRole, "commitId"},
            {ShortIdRole, "shortId"},
            {DescriptionRole, "description"},
            {CreatedRole, "created"},
            {IsCurrentRole, "isCurrent"},
            {IsBranchHeadRole, "isBranchHead"},
            {HeadOfRole, "headOf"},
            {OnBranchRole, "onBranch"}
        };
    }

    int CommitHistoryModel::count() const {
        return int(Entries.size());
    }

    int CommitHistoryModel::otherCount() const {
        return OtherCount;
    }

    QDateTime CommitHistoryModel::createdOf(const QString &commitId) const {
        for (const auto &commit : Commits)
            if (commit.Id == commitId)
                return commit.Created;
        return {};
    }

    void CommitHistoryModel::setHistory(QList<CommitInfo> commits, QList<BranchInfo> branches) {
        Commits = std::move(commits);
        Branches = std::move(branches);
        rebuild();
    }

    void CommitHistoryModel::setContext(const QString &currentCommitId, const QString &currentBranchId) {
        if (CurrentCommitId == currentCommitId && CurrentBranchId == currentBranchId)
            return;
        CurrentCommitId = currentCommitId;
        CurrentBranchId = currentBranchId;
        rebuild();
    }

    void CommitHistoryModel::clear() {
        if (Commits.isEmpty() && Branches.isEmpty() && Entries.isEmpty())
            return;
        setHistory({}, {});
    }

    void CommitHistoryModel::rebuild() {
        QHash<QString, int> indexById;
        for (int i = 0; i < Commits.size(); i++)
            indexById.insert(Commits.at(i).Id, i);

        QString headId;
        QHash<QString, QStringList> headsOf;
        for (const auto &branch : std::as_const(Branches)) {
            if (branch.HeadCommitId.isEmpty())
                continue;
            headsOf[branch.HeadCommitId].append(branch.Name);
            if (branch.Id == CurrentBranchId)
                headId = branch.HeadCommitId;
        }

        // First parent chain from the head of the current branch. The visited set protects against cycles, previous
        // commits that are not in the list (e.g. the history is cut off) end the chain.
        QList<int> chain;
        QSet<QString> reachable;
        if (indexById.contains(headId)) {
            QString id = headId;
            while (indexById.contains(id) && !reachable.contains(id)) {
                reachable.insert(id);
                const auto &commit = Commits.at(indexById.value(id));
                chain.append(indexById.value(id));
                id = commit.PreviousIds.isEmpty() ? QString() : commit.PreviousIds.first();
            }

            // Commits that were merged in via further parents also belong to the branch.
            QList<QString> pending;
            for (int row : std::as_const(chain))
                pending.append(Commits.at(row).Id);
            while (!pending.isEmpty()) {
                const auto current = pending.takeLast();
                for (const auto &previous : Commits.at(indexById.value(current)).PreviousIds) {
                    if (indexById.contains(previous) && !reachable.contains(previous)) {
                        reachable.insert(previous);
                        pending.append(previous);
                    }
                }
            }
        }
        // Without a known head of the branch nothing can be called "other".
        const bool headKnown = !chain.isEmpty();

        QList<int> merged;
        QList<int> others;
        QSet<int> inChain(chain.cbegin(), chain.cend());
        for (int i = 0; i < Commits.size(); i++) {
            if (inChain.contains(i))
                continue;
            (reachable.contains(Commits.at(i).Id) || !headKnown ? merged : others).append(i);
        }
        const auto newestFirst = [this](int left, int right) {
            const auto &a = Commits.at(left).Created;
            const auto &b = Commits.at(right).Created;
            if (a != b)
                return a > b;
            return Commits.at(left).Id < Commits.at(right).Id;
        };
        std::stable_sort(merged.begin(), merged.end(), newestFirst);
        std::stable_sort(others.begin(), others.end(), newestFirst);

        QList<Entry> entries;
        entries.reserve(Commits.size());
        const auto append = [&](int row, bool onBranch) {
            Entry entry;
            entry.Commit = Commits.at(row);
            entry.OnBranch = onBranch;
            entry.HeadOf = headsOf.value(entry.Commit.Id);
            entry.IsBranchHead = headKnown && entry.Commit.Id == headId;
            entries.append(entry);
        };
        for (int row : std::as_const(chain))
            append(row, true);
        for (int row : std::as_const(merged))
            append(row, true);
        for (int row : std::as_const(others))
            append(row, false);

        const int oldCount = int(Entries.size());
        const int oldOther = OtherCount;
        beginResetModel();
        Entries = std::move(entries);
        OtherCount = int(others.size());
        endResetModel();
        if (oldCount != int(Entries.size()) || oldOther != OtherCount)
            emit countChanged();
    }
}
