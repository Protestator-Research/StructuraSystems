//
// Created by Moritz Herzog on 30.10.26.
//

#include "CodeEditAssist.h"

#include <QList>
#include <QStringList>
#include <algorithm>

namespace StructuraSystems::Client {
    namespace {
        struct Range {
            int Start;
            int End;
        };

        /** Orders and clamps the selection to the text. */
        Range normalized(const QString &text, int selectionStart, int selectionEnd) {
            int start = std::clamp(std::min(selectionStart, selectionEnd), 0, static_cast<int>(text.size()));
            int end = std::clamp(std::max(selectionStart, selectionEnd), 0, static_cast<int>(text.size()));
            return {start, end};
        }

        int lineStart(const QString &text, int position) {
            return position <= 0 ? 0 : static_cast<int>(text.lastIndexOf(u'\n', position - 1)) + 1;
        }

        int lineEnd(const QString &text, int position) {
            int index = static_cast<int>(text.indexOf(u'\n', position));
            return index < 0 ? static_cast<int>(text.size()) : index;
        }

        bool isBlank(QChar character) {
            return character == u' ' || character == u'\t';
        }

        QChar closingOf(QChar opening) {
            if (opening == u'{')
                return u'}';
            if (opening == u'(')
                return u')';
            if (opening == u'[')
                return u']';
            return QChar();
        }

        bool isClosing(QChar character) {
            return character == u'}' || character == u')' || character == u']';
        }

        /** The leading spaces and tabs of the given text. */
        QString leadingBlanks(const QString &text) {
            int count = 0;
            while (count < text.size() && isBlank(text[count]))
                ++count;
            return text.left(count);
        }

        /** Number of characters that move a line one level out: one tab or up to IndentWidth spaces. */
        int outdentWidth(const QString &line) {
            if (line.startsWith(u'\t'))
                return 1;
            int count = 0;
            while (count < line.size() && count < CodeEditAssist::IndentWidth && line[count] == u' ')
                ++count;
            return count;
        }

        /**
         * Changes the lines that a selection touches. Delta returns the change of the length of the start of a line
         * (positive: spaces added, negative: characters removed).
         */
        template<typename Delta>
        CodeEdit editLines(const QString &text, Range selection, Delta delta) {
            // A selection that ends directly after a line break does not touch the following line.
            int last = (selection.End > selection.Start && text[selection.End - 1] == u'\n') ? selection.End - 1 : selection.End;
            int blockStart = lineStart(text, selection.Start);
            int blockEnd = lineEnd(text, last);
            QStringList lines = text.mid(blockStart, blockEnd - blockStart).split(u'\n');

            QList<int> starts;
            QList<int> deltas;
            int position = blockStart;
            QStringList result;
            for (const QString &line : lines) {
                int change = line.isEmpty() ? 0 : delta(line);
                starts.append(position);
                deltas.append(change);
                result.append(change >= 0 ? QString(change, u' ') + line : line.mid(-change));
                position += static_cast<int>(line.size()) + 1;
            }

            // Moves a position of the old text to the new one. A position in the removed or added part stays at the
            // start of the line.
            auto mapped = [&](int old) {
                int shift = 0;
                for (int i = 0; i < starts.size() && starts[i] < old; ++i) {
                    int change = deltas[i];
                    shift += change >= 0 ? change : -std::min(-change, old - starts[i]);
                }
                return old + shift;
            };

            CodeEdit edit;
            edit.Handled = true;
            edit.RemoveStart = blockStart;
            edit.RemoveEnd = blockEnd;
            edit.Insert = result.join(u'\n');
            edit.Cursor = mapped(selection.End);
            // A cursor without selection stays a cursor.
            if (selection.Start != selection.End) {
                edit.SelectionStart = mapped(selection.Start);
                edit.SelectionEnd = edit.Cursor;
            }
            return edit;
        }
    }

    CodeEditAssist::CodeEditAssist(QObject *parent) : QObject(parent) {
    }

    CodeEdit CodeEditAssist::editNewline(const QString &text, int selectionStart, int selectionEnd) {
        auto [start, end] = normalized(text, selectionStart, selectionEnd);
        int begin = lineStart(text, start);
        QString before = text.mid(begin, start - begin);
        // Inside the indentation of the line only the part left of the cursor is kept.
        QString indent = leadingBlanks(before);

        QChar lastCharacter;
        for (int i = static_cast<int>(before.size()) - 1; i >= 0; --i) {
            if (!isBlank(before[i])) {
                lastCharacter = before[i];
                break;
            }
        }
        QChar closing = closingOf(lastCharacter);

        // The blanks right of the cursor are dropped, they would be trailing whitespace of the new line.
        int after = end;
        while (after < text.size() && isBlank(text[after]))
            ++after;

        CodeEdit edit;
        edit.Handled = true;
        edit.RemoveStart = start;
        edit.RemoveEnd = after;
        if (!closing.isNull()) {
            QString inner = indent + QString(IndentWidth, u' ');
            if (after < text.size() && text[after] == closing) {
                edit.Insert = u'\n' + inner + u'\n' + indent;
            } else {
                edit.Insert = u'\n' + inner;
            }
            edit.Cursor = start + 1 + static_cast<int>(inner.size());
        } else {
            edit.Insert = u'\n' + indent;
            edit.Cursor = start + static_cast<int>(edit.Insert.size());
        }
        return edit;
    }

    CodeEdit CodeEditAssist::editOpenBracket(const QString &text, int selectionStart, int selectionEnd, QChar bracket) {
        QChar closing = closingOf(bracket);
        CodeEdit edit;
        if (closing.isNull())
            return edit;
        auto [start, end] = normalized(text, selectionStart, selectionEnd);

        if (start != end) {
            edit.Handled = true;
            edit.RemoveStart = start;
            edit.RemoveEnd = end;
            edit.Insert = QString(bracket) + text.mid(start, end - start) + closing;
            edit.SelectionStart = start + 1;
            edit.SelectionEnd = end + 1;
            edit.Cursor = end + 1;
            return edit;
        }

        // Only close the bracket if nothing that it could stick to follows.
        if (start < text.size()) {
            QChar next = text[start];
            if (!(next.isSpace() || isClosing(next) || next == u';' || next == u','))
                return edit;
        }
        edit.Handled = true;
        edit.RemoveStart = start;
        edit.RemoveEnd = start;
        edit.Insert = QString(bracket) + closing;
        edit.Cursor = start + 1;
        return edit;
    }

    CodeEdit CodeEditAssist::editCloseBracket(const QString &text, int selectionStart, int selectionEnd, QChar bracket) {
        CodeEdit edit;
        if (!isClosing(bracket))
            return edit;
        auto [start, end] = normalized(text, selectionStart, selectionEnd);
        if (start != end)
            return edit;

        if (start < text.size() && text[start] == bracket) {
            edit.Handled = true;
            edit.RemoveStart = start;
            edit.RemoveEnd = start;
            edit.Cursor = start + 1;
            return edit;
        }

        if (bracket == u'}') {
            int begin = lineStart(text, start);
            QString before = text.mid(begin, start - begin);
            if (!before.isEmpty() && leadingBlanks(before).size() == before.size()) {
                int width = 1;
                if (!before.endsWith(u'\t')) {
                    int spaces = 0;
                    while (spaces < before.size() && before[before.size() - 1 - spaces] == u' ')
                        ++spaces;
                    width = std::min(spaces, IndentWidth);
                }
                edit.Handled = true;
                edit.RemoveStart = start - width;
                edit.RemoveEnd = start;
                edit.Insert = QString(bracket);
                edit.Cursor = start - width + 1;
            }
        }
        return edit;
    }

    CodeEdit CodeEditAssist::editBackspace(const QString &text, int selectionStart, int selectionEnd) {
        CodeEdit edit;
        auto [start, end] = normalized(text, selectionStart, selectionEnd);
        if (start != end || start == 0)
            return edit;

        if (start < text.size() && closingOf(text[start - 1]) == text[start] && !text[start].isNull()) {
            edit.Handled = true;
            edit.RemoveStart = start - 1;
            edit.RemoveEnd = start + 1;
            edit.Cursor = start - 1;
            return edit;
        }

        int begin = lineStart(text, start);
        QString before = text.mid(begin, start - begin);
        if (before.isEmpty())
            return edit;
        bool onlySpaces = std::all_of(before.cbegin(), before.cend(), [](QChar c) { return c == u' '; });
        if (onlySpaces) {
            int count = static_cast<int>(before.size());
            int width = count % IndentWidth == 0 ? IndentWidth : count % IndentWidth;
            edit.Handled = true;
            edit.RemoveStart = start - width;
            edit.RemoveEnd = start;
            edit.Cursor = start - width;
        }
        return edit;
    }

    CodeEdit CodeEditAssist::editIndent(const QString &text, int selectionStart, int selectionEnd) {
        Range selection = normalized(text, selectionStart, selectionEnd);
        if (text.mid(selection.Start, selection.End - selection.Start).contains(u'\n'))
            return editLines(text, selection, [](const QString &) { return IndentWidth; });

        int column = selection.Start - lineStart(text, selection.Start);
        int width = IndentWidth - column % IndentWidth;
        CodeEdit edit;
        edit.Handled = true;
        edit.RemoveStart = selection.Start;
        edit.RemoveEnd = selection.End;
        edit.Insert = QString(width, u' ');
        edit.Cursor = selection.Start + width;
        return edit;
    }

    CodeEdit CodeEditAssist::editOutdent(const QString &text, int selectionStart, int selectionEnd) {
        Range selection = normalized(text, selectionStart, selectionEnd);
        CodeEdit edit = editLines(text, selection, [](const QString &line) { return -outdentWidth(line); });
        // Nothing to remove: the key is still consumed, so the focus does not leave the editor. The selection is kept.
        if (edit.Insert == text.mid(edit.RemoveStart, edit.RemoveEnd - edit.RemoveStart)) {
            edit.Cursor = selection.End;
            if (selection.Start != selection.End) {
                edit.SelectionStart = selection.Start;
                edit.SelectionEnd = selection.End;
            }
            edit.RemoveEnd = edit.RemoveStart;
            edit.Insert.clear();
        }
        return edit;
    }

    QVariantMap CodeEditAssist::toVariantMap(const CodeEdit &edit) {
        return {
            {QStringLiteral("handled"), edit.Handled},
            {QStringLiteral("removeStart"), edit.RemoveStart},
            {QStringLiteral("removeEnd"), edit.RemoveEnd},
            {QStringLiteral("insert"), edit.Insert},
            {QStringLiteral("cursor"), edit.Cursor},
            {QStringLiteral("selectionStart"), edit.SelectionStart},
            {QStringLiteral("selectionEnd"), edit.SelectionEnd}
        };
    }

    QVariantMap CodeEditAssist::newline(const QString &text, int selectionStart, int selectionEnd) const {
        return toVariantMap(editNewline(text, selectionStart, selectionEnd));
    }

    QVariantMap CodeEditAssist::openBracket(const QString &text, int selectionStart, int selectionEnd, const QString &bracket) const {
        return toVariantMap(editOpenBracket(text, selectionStart, selectionEnd, bracket.isEmpty() ? QChar() : bracket[0]));
    }

    QVariantMap CodeEditAssist::closeBracket(const QString &text, int selectionStart, int selectionEnd, const QString &bracket) const {
        return toVariantMap(editCloseBracket(text, selectionStart, selectionEnd, bracket.isEmpty() ? QChar() : bracket[0]));
    }

    QVariantMap CodeEditAssist::backspace(const QString &text, int selectionStart, int selectionEnd) const {
        return toVariantMap(editBackspace(text, selectionStart, selectionEnd));
    }

    QVariantMap CodeEditAssist::indent(const QString &text, int selectionStart, int selectionEnd) const {
        return toVariantMap(editIndent(text, selectionStart, selectionEnd));
    }

    QVariantMap CodeEditAssist::outdent(const QString &text, int selectionStart, int selectionEnd) const {
        return toVariantMap(editOutdent(text, selectionStart, selectionEnd));
    }
}
