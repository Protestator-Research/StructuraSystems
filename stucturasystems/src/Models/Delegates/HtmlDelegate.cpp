//
// Created by Moritz Herzog on 23.04.25.
//

#include "HtmlDelegate.h"
#include <QStyleOptionViewItem>
#include <QApplication>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPainter>



namespace StructuraSystems::Client {

    void HTMLDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
        QStyleOptionViewItem optionV4 = option;
        initStyleOption(&optionV4, index);

        QStyle *style = optionV4.widget? optionV4.widget->style() : QApplication::style();

        QTextDocument doc;
        doc.setHtml(optionV4.text);

        /// Painting item without text
        optionV4.text = QString();
        style->drawControl(QStyle::CE_ItemViewItem, &optionV4, painter);

        QAbstractTextDocumentLayout::PaintContext ctx;

        // Highlighting text if item is selected
        if (optionV4.state & QStyle::State_Selected)
            ctx.palette.setColor(QPalette::Text, optionV4.palette.color(QPalette::Active, QPalette::HighlightedText));

        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &optionV4);
        painter->save();
        painter->translate(textRect.topLeft());
        painter->setClipRect(textRect.translated(-textRect.topLeft()));
        doc.documentLayout()->draw(painter, ctx);
        painter->restore();
    }

    QSize HTMLDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
        QStyleOptionViewItem optionV4 = option;
        initStyleOption(&optionV4, index);

        QTextDocument doc;
        doc.setHtml(optionV4.text);
        doc.setTextWidth(optionV4.rect.width());
        return QSize(doc.idealWidth(), doc.size().height());
    }
} // StructuraSystems::Client