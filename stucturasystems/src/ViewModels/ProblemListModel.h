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
        /**
         * Current row of the card in the DocumentModel, -1 for a problem of the whole document. The DocumentModel keeps it current
         * in its own list (DocumentModel::problems()) when cards move, are inserted or removed; the copy in the ProblemListModel is
         * refreshed by the AppController for the current document only (see ElementId).
         */
        int Row = -1;
        int Severity = 3;   ///< Same scale as AppController::notify: 2 = warning, 3 = error
        /**
         * Id of the TextualRepresentation (card) the problem belongs to, empty for a problem of the whole document. This is what ties
         * the problem to its card: Row only says where the card is now and changes when cards are moved, inserted or removed.
         */
        QString ElementId;
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
        /**
         * Updates the list after the rows of the cards changed: @p problems must be the current problems with some removed and
         * the Row of others changed. Rows are updated or removed in place (the view keeps its scroll position); anything else
         * falls back to setProblems.
         */
        void updateProblems(const QList<Problem> &problems);
        void clear();

    signals:
        void countChanged();

    private:
        QList<Problem> Problems;
    };
}

#endif //STRUCTURASYSTEMS_PROBLEMLISTMODEL_H
