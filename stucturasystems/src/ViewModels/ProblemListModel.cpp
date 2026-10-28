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

    void ProblemListModel::clear() {
        setProblems({});
    }
}
