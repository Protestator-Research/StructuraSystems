//
// Created by Moritz Herzog on 30.10.26.
//

#ifndef STRUCTURASYSTEMS_COMPLETIONCONTROLLER_H
#define STRUCTURASYSTEMS_COMPLETIONCONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    class DocumentModel;

    /**
     * QML facade of the CompletionEngine for one editor: knows the document (whose last parse supplies the names of the
     * libraries) and the language of the text.
     */
    class CompletionController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(StructuraSystems::Client::DocumentModel *document READ document WRITE setDocument NOTIFY documentChanged)
        Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

    public:
        explicit CompletionController(QObject *parent = nullptr);

        [[nodiscard]] DocumentModel *document() const;
        void setDocument(DocumentModel *document);
        [[nodiscard]] QString language() const;
        void setLanguage(const QString &language);

        /**
         * The suggestions at the cursor. Returns replaceStart, replaceEnd, kind (the context: keyword, type, member, import,
         * declaration or anything), prefix, qualifier, valid and items, a list of label, insertText, detail, kind (keyword,
         * snippet, definition, usage, package or other), cursorOffset and selectionLength.
         */
        Q_INVOKABLE QVariantMap complete(const QString &text, int cursor) const;

    signals:
        void documentChanged();
        void languageChanged();

    private:
        QPointer<DocumentModel> Document;
        QString Language;
    };
}

#endif //STRUCTURASYSTEMS_COMPLETIONCONTROLLER_H
