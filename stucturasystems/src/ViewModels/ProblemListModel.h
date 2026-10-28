//
// Created by Moritz Herzog on 07.10.26.
//

#ifndef STRUCTURASYSTEMS_PROBLEMLISTMODEL_H
#define STRUCTURASYSTEMS_PROBLEMLISTMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    /**
     * Plain data describing one problem found while parsing a document. Safe to create in worker threads.
     */
    struct Problem {
        QString Message;
        int Line = -1;      ///< 1-based, -1 if unknown
        int Column = -1;    ///< 0-based, -1 if unknown
        QString DocumentTitle;
        int Row = -1;       ///< Row of the DocumentModel that produced the problem, -1 if unknown
        int Severity = 3;   ///< Same scale as AppController::notify: 2 = warning, 3 = error
    };

    class ProblemListModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("ProblemListModel instances are owned by AppController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)
    public:
        enum Roles {
            MessageRole = Qt::UserRole + 1,
            LineRole,
            ColumnRole,
            DocumentTitleRole,
            RowRole,
            SeverityRole
        };
        Q_ENUM(Roles)

        explicit ProblemListModel(QObject *parent = nullptr);
        ~ProblemListModel() override = default;

        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        QHash<int, QByteArray> roleNames() const override;

        [[nodiscard]] int count() const;

        void setProblems(const QList<Problem> &problems);
        void clear();

    signals:
        void countChanged();

    private:
        QList<Problem> Problems;
    };
}

#endif //STRUCTURASYSTEMS_PROBLEMLISTMODEL_H
