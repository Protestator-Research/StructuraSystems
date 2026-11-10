//
// Created by Moritz Herzog on 30.10.26.
//

// The ANTLR headers have to come before any Qt header (Qt defines macros like emit and signals).
#include <kerml/parser/KerMLLexer.h>
#include <sysmlv2/parser/SysMLv2Lexer.h>

#include "LanguageKeywords.h"

namespace StructuraSystems::Client {
    namespace {
        /**
         * Collects all KEYWORD_* literals of the given ANTLR lexer vocabulary.
         */
        template<typename LexerType>
        QSet<QString> collectKeywords() {
            antlr4::ANTLRInputStream input{std::string{}};
            LexerType lexer(&input);
            const auto &vocabulary = lexer.getVocabulary();
            QSet<QString> result;
            for (size_t type = 1; type <= vocabulary.getMaxTokenType(); ++type) {
                const std::string symbolic{vocabulary.getSymbolicName(type)};
                if (symbolic.rfind("KEYWORD_", 0) != 0)
                    continue;
                std::string literal{vocabulary.getLiteralName(type)};
                if (literal.size() >= 2 && literal.front() == '\'' && literal.back() == '\'')
                    literal = literal.substr(1, literal.size() - 2);
                if (!literal.empty())
                    result.insert(QString::fromStdString(literal));
            }
            return result;
        }
    }

    const QSet<QString> &sysmlKeywords() {
        static const QSet<QString> Keywords = collectKeywords<SysMLv2Lexer>();
        return Keywords;
    }

    const QSet<QString> &kermlKeywords() {
        static const QSet<QString> Keywords = collectKeywords<KerMLLexer>();
        return Keywords;
    }
}
