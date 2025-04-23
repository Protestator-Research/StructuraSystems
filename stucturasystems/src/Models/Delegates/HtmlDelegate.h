//
// Created by Moritz Herzog on 23.04.25.
//

#ifndef STRUCTURASYSTEMS_HTMLDELEGATE_H
#define STRUCTURASYSTEMS_HTMLDELEGATE_H

#include <QStyledItemDelegate>

namespace StructuraSystems::Client {
    class HTMLDelegate : public QStyledItemDelegate{
    public:
        HTMLDelegate()=default;

        ~HTMLDelegate()=default;

    protected:
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const;
    private:
    };
} // StructuraSystems::Client

#endif //STRUCTURASYSTEMS_HTMLDELEGATE_H
