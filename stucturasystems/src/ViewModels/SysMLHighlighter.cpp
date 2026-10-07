//
// Created by Moritz Herzog on 07.10.26.
//

#include "SysMLHighlighter.h"

#include <QColor>
#include <QFont>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>

namespace StructuraSystems::Client {
    namespace {
        enum BlockState {
            Normal = 0,
            InComment = 1,
            InDocComment = 2
        };

        const QSet<QString> &keywords() {
            static const QSet<QString> Keywords = {
                "part", "def", "attribute", "port", "item", "action", "state", "requirement", "constraint", "package",
                "import", "private", "public", "protected", "connection", "interface", "flow", "ref", "in", "out", "inout",
                "abstract", "specializes", "subsets", "redefines", "doc", "comment", "enum", "calc", "use", "case", "view",
                "viewpoint", "verification", "analysis", "allocation", "metadata", "transition", "then", "first", "accept",
                "do", "entry", "exit", "if", "else", "while", "for", "return", "alias", "library", "standard", "class",
                "struct", "datatype", "feature", "type", "classifier", "function", "predicate", "behavior", "step", "expr",
                "bool", "assoc", "connector", "binding", "succession", "namespace", "member", "about", "language", "true",
                "false", "null", "occurrence", "individual", "variation", "variant", "event", "perform", "exhibit",
                "include", "satisfy", "assert", "assume", "require", "subject", "objective", "actor", "stakeholder",
                "concern", "frame", "render", "rendering", "message", "from", "to", "of", "all", "end", "readonly",
                "derived", "nonunique", "ordered", "conjugate", "conjugates", "typed", "by", "dependency", "filter",
                "expose", "send", "via", "loop", "until", "merge", "decide", "fork", "join", "terminate", "bind", "succession",
                "snapshot", "timeslice", "inverse", "disjoint", "differences", "intersects", "unions", "chains", "featured",
                "multiplicity", "interaction", "metaclass", "var", "const", "not", "and", "or", "xor", "implies", "hastype",
                "istype", "as", "meta", "self", "this", "inv", "default", "assign", "rep", "textualrepresentation"
            };
            return Keywords;
        }

        bool isIdentifierStart(const QChar c) { return c.isLetter() || c == '_'; }
        bool isIdentifierPart(const QChar c) { return c.isLetterOrNumber() || c == '_'; }
    }

    /**
     * Does the actual work, lives as child of the QTextDocument.
     */
    class SysMLSyntaxHighlighter : public QSyntaxHighlighter {
    public:
        explicit SysMLSyntaxHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {
            applyColors();
        }

        void setDark(bool dark) {
            if (Dark == dark)
                return;
            Dark = dark;
            applyColors();
            rehighlight();
        }

    protected:
        void highlightBlock(const QString &text) override {
            int pos = 0;
            const int length = text.length();
            bool lastWasDoc = false;
            int state = previousBlockState();

            if (state == InComment || state == InDocComment) {
                const int end = text.indexOf("*/");
                const auto &format = state == InDocComment ? DocFormat : CommentFormat;
                if (end < 0) {
                    setFormat(0, length, format);
                    setCurrentBlockState(state);
                    return;
                }
                setFormat(0, end + 2, format);
                pos = end + 2;
            }
            setCurrentBlockState(Normal);

            while (pos < length) {
                const QChar c = text.at(pos);
                if (c.isSpace()) {
                    ++pos;
                    continue;
                }

                // Comments
                if (c == '/' && pos + 1 < length && text.at(pos + 1) == '/') {
                    setFormat(pos, length - pos, CommentFormat);
                    return;
                }
                if (c == '/' && pos + 1 < length && text.at(pos + 1) == '*') {
                    const bool isDoc = lastWasDoc;
                    const auto &format = isDoc ? DocFormat : CommentFormat;
                    const int end = text.indexOf("*/", pos + 2);
                    if (end < 0) {
                        setFormat(pos, length - pos, format);
                        setCurrentBlockState(isDoc ? InDocComment : InComment);
                        return;
                    }
                    setFormat(pos, end + 2 - pos, format);
                    pos = end + 2;
                    lastWasDoc = false;
                    continue;
                }

                // Strings and unrestricted names
                if (c == '"' || c == '\'') {
                    int end = pos + 1;
                    while (end < length && text.at(end) != c) {
                        if (text.at(end) == '\\')
                            ++end;
                        ++end;
                    }
                    const int stop = qMin(end + 1, length);
                    setFormat(pos, stop - pos, c == '"' ? StringFormat : NameFormat);
                    pos = stop;
                    lastWasDoc = false;
                    continue;
                }

                // Numbers
                if (c.isDigit()) {
                    int end = pos + 1;
                    while (end < length && (text.at(end).isLetterOrNumber() || text.at(end) == '.' || text.at(end) == '_'))
                        ++end;
                    setFormat(pos, end - pos, NumberFormat);
                    pos = end;
                    lastWasDoc = false;
                    continue;
                }

                // Operators that carry meaning in SysMLv2
                if (c == ':') {
                    int len = 1;
                    if (text.mid(pos, 3) == ":>>")
                        len = 3;
                    else if (text.mid(pos, 2) == ":>" || text.mid(pos, 2) == "::")
                        len = 2;
                    setFormat(pos, len, OperatorFormat);
                    pos += len;
                    lastWasDoc = false;
                    continue;
                }

                // Words
                if (isIdentifierStart(c)) {
                    int end = pos + 1;
                    while (end < length && isIdentifierPart(text.at(end)))
                        ++end;
                    const QString word = text.mid(pos, end - pos);
                    if (keywords().contains(word)) {
                        setFormat(pos, end - pos, KeywordFormat);
                        lastWasDoc = word == "doc" || word == "comment";
                    } else {
                        if (word.at(0).isUpper())
                            setFormat(pos, end - pos, TypeFormat);
                        lastWasDoc = false;
                    }
                    pos = end;
                    continue;
                }

                if (QStringLiteral("=<>!&|+-*/%^~@#?").contains(c))
                    setFormat(pos, 1, OperatorFormat);
                lastWasDoc = false;
                ++pos;
            }
        }

    private:
        static QTextCharFormat make(const QColor &color, bool bold = false, bool italic = false) {
            QTextCharFormat format;
            format.setForeground(color);
            if (bold)
                format.setFontWeight(QFont::DemiBold);
            format.setFontItalic(italic);
            return format;
        }

        void applyColors() {
            if (Dark) {
                KeywordFormat = make(QColor("#c792ea"), true);
                TypeFormat = make(QColor("#4dd0c4"));
                StringFormat = make(QColor("#c3e88d"));
                NameFormat = make(QColor("#f2c98a"));
                NumberFormat = make(QColor("#f78c6c"));
                CommentFormat = make(QColor("#7f8aa0"), false, true);
                DocFormat = make(QColor("#82aaff"), false, true);
                OperatorFormat = make(QColor("#89ddff"), true);
            } else {
                KeywordFormat = make(QColor("#7b2cbf"), true);
                TypeFormat = make(QColor("#00796b"));
                StringFormat = make(QColor("#2e7d32"));
                NameFormat = make(QColor("#8a5a00"));
                NumberFormat = make(QColor("#c2410c"));
                CommentFormat = make(QColor("#6b7280"), false, true);
                DocFormat = make(QColor("#1d4ed8"), false, true);
                OperatorFormat = make(QColor("#0369a1"), true);
            }
        }

        bool Dark = false;
        QTextCharFormat KeywordFormat;
        QTextCharFormat TypeFormat;
        QTextCharFormat StringFormat;
        QTextCharFormat NameFormat;
        QTextCharFormat NumberFormat;
        QTextCharFormat CommentFormat;
        QTextCharFormat DocFormat;
        QTextCharFormat OperatorFormat;
    };

    SysMLHighlighter::SysMLHighlighter(QObject *parent) : QObject(parent) {}

    SysMLHighlighter::~SysMLHighlighter() {
        delete Highlighter.data();
    }

    QQuickTextDocument *SysMLHighlighter::textDocument() const {
        return TextDocument;
    }

    void SysMLHighlighter::setTextDocument(QQuickTextDocument *textDocument) {
        if (TextDocument == textDocument)
            return;
        delete Highlighter.data();
        TextDocument = textDocument;
        if (TextDocument && TextDocument->textDocument()) {
            auto *highlighter = new SysMLSyntaxHighlighter(TextDocument->textDocument());
            highlighter->setDark(DarkTheme);
            Highlighter = highlighter;
        }
        emit textDocumentChanged();
    }

    bool SysMLHighlighter::darkTheme() const {
        return DarkTheme;
    }

    void SysMLHighlighter::setDarkTheme(bool darkTheme) {
        if (DarkTheme == darkTheme)
            return;
        DarkTheme = darkTheme;
        if (Highlighter)
            static_cast<SysMLSyntaxHighlighter *>(Highlighter.data())->setDark(darkTheme);
        emit darkThemeChanged();
    }
}
