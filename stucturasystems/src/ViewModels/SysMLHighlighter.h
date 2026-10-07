//
// Created by Moritz Herzog on 07.10.26.
//

#ifndef STRUCTURASYSTEMS_SYSMLHIGHLIGHTER_H
#define STRUCTURASYSTEMS_SYSMLHIGHLIGHTER_H

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    /**
     * QML facing syntax highlighter for SysMLv2 and KerML text. Attach it to the textDocument of a TextEdit/TextArea.
     */
    class SysMLHighlighter : public QObject {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
        Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY darkThemeChanged)
    public:
        explicit SysMLHighlighter(QObject *parent = nullptr);
        ~SysMLHighlighter() override;

        [[nodiscard]] QQuickTextDocument *textDocument() const;
        void setTextDocument(QQuickTextDocument *textDocument);

        [[nodiscard]] bool darkTheme() const;
        void setDarkTheme(bool darkTheme);

    signals:
        void textDocumentChanged();
        void darkThemeChanged();

    private:
        QPointer<QQuickTextDocument> TextDocument;
        QPointer<QSyntaxHighlighter> Highlighter;
        bool DarkTheme = false;
    };
}

#endif //STRUCTURASYSTEMS_SYSMLHIGHLIGHTER_H
