//
// Created by Moritz Herzog on 24.10.26.
//

#include "CodeScanner.h"

#include <vector>

#include <QFile>
#include <QFileInfo>

#include <kerml/root/elements/Element.h>
#include <kerml/root/annotations/TextualRepresentation.h>
#include "../Parser/Markdown/MarkdownParser.h"

namespace StructuraSystems::Client {
    namespace {
        enum class TokenKind { Word, Quoted, ColonColon, OpenBrace, CloseBrace, Other };

        struct Token {
            TokenKind Kind = TokenKind::Other;
            QString Text;
            int Depth = 0;   ///< Brace depth at the token (a `{` is at the depth before it opens, a `}` at the depth after it closes).
        };

        bool isWordStart(QChar c) { return c.isLetter() || c == QLatin1Char('_'); }
        bool isWordPart(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

        std::vector<Token> tokenize(const QString &text) {
            std::vector<Token> tokens;
            const auto size = text.size();
            int depth = 0;
            qsizetype i = 0;
            while (i < size) {
                const QChar c = text[i];
                if (c.isSpace()) {
                    ++i;
                } else if (c == QLatin1Char('/') && i + 1 < size && text[i + 1] == QLatin1Char('/')) {
                    while (i < size && text[i] != QLatin1Char('\n'))
                        ++i;
                } else if (c == QLatin1Char('/') && i + 1 < size && text[i + 1] == QLatin1Char('*')) {
                    const auto end = text.indexOf(QStringLiteral("*/"), i + 2);
                    i = end < 0 ? size : end + 2;
                } else if (c == QLatin1Char('"')) {
                    ++i;
                    while (i < size && text[i] != QLatin1Char('"')) {
                        if (text[i] == QLatin1Char('\\'))
                            ++i;
                        ++i;
                    }
                    ++i;
                    tokens.push_back({TokenKind::Other, QString(), depth});
                } else if (c == QLatin1Char('\'')) {
                    QString name;
                    ++i;
                    while (i < size && text[i] != QLatin1Char('\'')) {
                        if (text[i] == QLatin1Char('\\') && i + 1 < size)
                            ++i;
                        name += text[i];
                        ++i;
                    }
                    ++i;
                    tokens.push_back({TokenKind::Quoted, name, depth});
                } else if (isWordStart(c)) {
                    const auto start = i;
                    while (i < size && isWordPart(text[i]))
                        ++i;
                    tokens.push_back({TokenKind::Word, text.mid(start, i - start), depth});
                } else if (c == QLatin1Char(':') && i + 1 < size && text[i + 1] == QLatin1Char(':')) {
                    tokens.push_back({TokenKind::ColonColon, QStringLiteral("::"), depth});
                    i += 2;
                } else if (c == QLatin1Char('{')) {
                    tokens.push_back({TokenKind::OpenBrace, QStringLiteral("{"), depth});
                    ++depth;
                    ++i;
                } else if (c == QLatin1Char('}')) {
                    if (depth > 0)
                        --depth;
                    tokens.push_back({TokenKind::CloseBrace, QStringLiteral("}"), depth});
                    ++i;
                } else {
                    tokens.push_back({TokenKind::Other, QString(c), depth});
                    ++i;
                }
            }
            return tokens;
        }

        bool isNameToken(const Token &token) {
            return token.Kind == TokenKind::Word || token.Kind == TokenKind::Quoted;
        }

        bool isWord(const Token &token, const char *word) {
            return token.Kind == TokenKind::Word && token.Text == QLatin1String(word);
        }

        bool isSymbol(const Token &token, const char *symbol) {
            return token.Kind == TokenKind::Other && token.Text == QLatin1String(symbol);
        }

        void appendUnique(QStringList &list, const QString &value) {
            if (!list.contains(value))
                list.append(value);
        }
    }

    ScanResult CodeScanner::scan(const QString &text) {
        ScanResult result;
        const auto tokens = tokenize(text);
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto &token = tokens[i];

            // The first segment of a qualified name: a name followed by '::' that is not itself behind a '::'.
            if (isNameToken(token) && i + 1 < tokens.size() && tokens[i + 1].Kind == TokenKind::ColonColon &&
                !(i > 0 && tokens[i - 1].Kind == TokenKind::ColonColon))
                appendUnique(result.References, token.Text);

            // The target of an import (`import Pkg;`, `import all Pkg as X;`) or alias (`alias A for Pkg;`) is a dependency even
            // without a '::' behind its first segment.
            if (isWord(token, "import")) {
                size_t next = i + 1;
                if (next < tokens.size() && isWord(tokens[next], "all"))
                    ++next;
                if (next < tokens.size() && isNameToken(tokens[next]))
                    appendUnique(result.References, tokens[next].Text);
            } else if (isWord(token, "alias")) {
                size_t next = i + 1;
                while (next < tokens.size() && !isWord(tokens[next], "for") && !isSymbol(tokens[next], ";") &&
                       tokens[next].Kind != TokenKind::OpenBrace)
                    ++next;
                if (next + 1 < tokens.size() && isWord(tokens[next], "for") && isNameToken(tokens[next + 1]))
                    appendUnique(result.References, tokens[next + 1].Text);
            }

            // `[standard] [library] package Name` on the top level.
            if (isWord(token, "package") && token.Depth == 0) {
                bool library = false;
                for (size_t back = i; back > 0 && tokens[back - 1].Depth == 0; --back) {
                    const auto &previous = tokens[back - 1];
                    if (isWord(previous, "library"))
                        library = true;
                    else if (!isWord(previous, "standard"))
                        break;
                }
                size_t next = i + 1;
                // Short name: <name>
                if (next < tokens.size() && isSymbol(tokens[next], "<")) {
                    while (next < tokens.size() && !isSymbol(tokens[next], ">"))
                        ++next;
                    ++next;
                }
                if (next < tokens.size() && isNameToken(tokens[next]))
                    result.Packages.append({tokens[next].Text, library});
            }
        }
        return result;
    }

    QString CodeScanner::codeLanguage(const QString &language) {
        const auto lower = language.trimmed().toLower();
        if (lower == QLatin1String("sysml") || lower == QLatin1String("sysmlv2") || lower == QLatin1String("sysmd"))
            return QStringLiteral("SysMLv2");
        if (lower == QLatin1String("kerml"))
            return QStringLiteral("KerML");
        return {};
    }

    bool CodeScanner::isProjectFile(const QString &fileName) {
        const auto suffix = QFileInfo(fileName).suffix().toLower();
        return suffix == QLatin1String("sysml") || suffix == QLatin1String("kerml") || suffix == QLatin1String("md");
    }

    QList<CodeBlock> CodeScanner::codeBlocksOfFile(const QString &absolutePath) {
        QList<CodeBlock> blocks;
        const auto suffix = QFileInfo(absolutePath).suffix().toLower();
        if (suffix == QLatin1String("md")) {
            MarkdownParser parser;
            parser.parseMarkdownFile(absolutePath);
            for (const auto &element : parser.getElementsOfProject()) {
                const auto textual = std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(element);
                if (textual == nullptr)
                    continue;
                const auto language = codeLanguage(QString::fromStdString(textual->language()));
                if (!language.isEmpty())
                    blocks.append({language, QString::fromStdString(textual->body())});
            }
        } else if (suffix == QLatin1String("sysml") || suffix == QLatin1String("kerml")) {
            QFile file(absolutePath);
            if (!file.open(QIODevice::ReadOnly))
                return blocks;
            blocks.append({suffix == QLatin1String("kerml") ? QStringLiteral("KerML") : QStringLiteral("SysMLv2"),
                           QString::fromUtf8(file.readAll())});
        }
        return blocks;
    }

    ScanResult CodeScanner::scanFile(const QString &absolutePath) {
        ScanResult combined;
        for (const auto &block : codeBlocksOfFile(absolutePath)) {
            const auto scanned = scan(block.Text);
            combined.Packages.append(scanned.Packages);
            for (const auto &reference : scanned.References)
                appendUnique(combined.References, reference);
        }
        return combined;
    }
}
