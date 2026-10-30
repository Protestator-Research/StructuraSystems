//
// Created by Moritz Herzog on 30.10.26.
//

#ifndef STRUCTURASYSTEMS_CODEEDITASSIST_H
#define STRUCTURASYSTEMS_CODEEDITASSIST_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    /**
     * Describes one change of the text of the editor: the range [RemoveStart, RemoveEnd) is replaced by Insert and the cursor
     * is put to Cursor (absolute position after the change). SelectionStart/SelectionEnd are only set (>= 0) if the
     * selection has to be restored. Handled is false if the editor should do its default behavior.
     */
    struct CodeEdit {
        bool Handled = false;
        int RemoveStart = 0;
        int RemoveEnd = 0;
        QString Insert;
        int Cursor = 0;
        int SelectionStart = -1;
        int SelectionEnd = -1;
    };

    /**
     * Pure text logic of the editing help for SysMLv2 and KerML code: indentation, bracket pairs and tab handling. The QML
     * editor calls it on key presses and applies the result with remove/insert, so the changes stay in the undo stack.
     */
    class CodeEditAssist : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON
    public:
        static constexpr int IndentWidth = 4;

        explicit CodeEditAssist(QObject *parent = nullptr);

        /** Enter: keeps the indentation, opens a level after a bracket and splits an empty pair onto three lines. */
        static CodeEdit editNewline(const QString &text, int selectionStart, int selectionEnd);
        /** Typing of '{', '(' or '[': inserts the pair or wraps the selection. */
        static CodeEdit editOpenBracket(const QString &text, int selectionStart, int selectionEnd, QChar bracket);
        /** Typing of '}', ')' or ']': steps over an existing bracket or moves a lone '}' one level out. */
        static CodeEdit editCloseBracket(const QString &text, int selectionStart, int selectionEnd, QChar bracket);
        /** Backspace without selection: removes an empty pair or a whole indentation level. */
        static CodeEdit editBackspace(const QString &text, int selectionStart, int selectionEnd);
        /** Tab: inserts spaces up to the next indentation stop or indents all lines of a multi line selection. */
        static CodeEdit editIndent(const QString &text, int selectionStart, int selectionEnd);
        /** Shift+Tab: moves the lines of the selection (or the current line) one level out. */
        static CodeEdit editOutdent(const QString &text, int selectionStart, int selectionEnd);

        /**
         * Converts an edit to the map that QML gets: handled, removeStart, removeEnd, insert, cursor, selectionStart and
         * selectionEnd.
         */
        static QVariantMap toVariantMap(const CodeEdit &edit);

        Q_INVOKABLE QVariantMap newline(const QString &text, int selectionStart, int selectionEnd) const;
        Q_INVOKABLE QVariantMap openBracket(const QString &text, int selectionStart, int selectionEnd, const QString &bracket) const;
        Q_INVOKABLE QVariantMap closeBracket(const QString &text, int selectionStart, int selectionEnd, const QString &bracket) const;
        Q_INVOKABLE QVariantMap backspace(const QString &text, int selectionStart, int selectionEnd) const;
        Q_INVOKABLE QVariantMap indent(const QString &text, int selectionStart, int selectionEnd) const;
        Q_INVOKABLE QVariantMap outdent(const QString &text, int selectionStart, int selectionEnd) const;
    };
}

#endif //STRUCTURASYSTEMS_CODEEDITASSIST_H
