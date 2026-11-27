//
// Created by Moritz Herzog on 07.10.26.
//

#include "ProblemListModel.h"

namespace StructuraSystems::Client {
    ProblemListModel::ProblemListModel(QObject *parent) : QAbstractListModel(parent) {
    }

    int ProblemListModel::rowCount(const QModelIndex &parent) const {
        if (parent.isValid())
            return 0;
        return int(Problems.size());
    }

    QVariant ProblemListModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= Problems.size())
            return {};

        const auto &problem = Problems.at(index.row());
        switch (role) {
            case Qt::DisplayRole:
            case MessageRole:
                return problem.Message;
            case LineRole:
                return problem.Line;
            case ColumnRole:
                return problem.Column;
            case DocumentTitleRole:
                return problem.DocumentTitle;
            case RowRole:
                return problem.Row;
            case SeverityRole:
                return problem.Severity;
            default:
                return {};
        }
    }

    QHash<int, QByteArray> ProblemListModel::roleNames() const {
        return {
            {MessageRole, "message"},
            {LineRole, "line"},
            {ColumnRole, "column"},
            {DocumentTitleRole, "documentTitle"},
            {RowRole, "row"},
            {SeverityRole, "severity"}
        };
    }

    int ProblemListModel::count() const {
        return int(Problems.size());
    }

    void ProblemListModel::setProblems(const QList<Problem> &problems) {
        const bool countDiffers = problems.size() != Problems.size();
        beginResetModel();
        Problems = problems;
        endResetModel();
        if (countDiffers)
            emit countChanged();
    }

    void ProblemListModel::updateProblems(const QList<Problem> &problems) {
        const auto same = [](const Problem &a, const Problem &b) {
            return a.Message == b.Message && a.Line == b.Line && a.Column == b.Column && a.DocumentTitle == b.DocumentTitle &&
                   a.Severity == b.Severity && a.ElementId == b.ElementId;
        };

        // The new list is the old one with problems removed, in the same order: match the two lists front to back.
        QList<int> removed;
        QList<int> rowChanged;
        qsizetype next = 0;
        for (qsizetype i = 0; i < Problems.size(); i++) {
            if (next < problems.size() && same(Problems.at(i), problems.at(next))) {
                if (Problems.at(i).Row != problems.at(next).Row)
                    rowChanged.append(int(i));
                next++;
            } else {
                removed.append(int(i));
            }
        }
        if (next != problems.size()) {
            setProblems(problems);
            return;
        }

        // Rows that stay: take over the new row (indices of the old list, before anything is removed).
        QList<Problem> updated = Problems;
        next = 0;
        for (qsizetype i = 0; i < Problems.size(); i++) {
            if (removed.contains(int(i)))
                continue;
            updated[i].Row = problems.at(next++).Row;
        }
        Problems = updated;
        for (const int i : std::as_const(rowChanged))
            emit dataChanged(index(i), index(i), {RowRole});

        for (qsizetype k = removed.size() - 1; k >= 0; k--) {
            beginRemoveRows(QModelIndex(), removed.at(k), removed.at(k));
            Problems.removeAt(removed.at(k));
            endRemoveRows();
        }
        if (!removed.isEmpty())
            emit countChanged();
    }

    void ProblemListModel::clear() {
        setProblems({});
    }
}
