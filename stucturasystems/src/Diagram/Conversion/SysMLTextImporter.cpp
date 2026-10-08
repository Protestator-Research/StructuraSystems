//
// SysML v2 / KerML textual notation -> semantic model.
//

#include "SysMLTextImporter.h"
#include "../Semantic/RelationshipRules.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace StructuraSystems::Diagram {

    namespace {
        // ---------------------------------------------------------------- tokens

        enum class TokenType {
            Identifier,
            Name,    ///< quoted name '...' (unescaped text)
            Number,
            String,  ///< "..."
            Symbol,
            Comment,
            End
        };

        struct Token {
            TokenType Type = TokenType::End;
            std::string Text;
            std::size_t Line = 0;
            std::size_t Begin = 0; ///< offset in the source text
            std::size_t End = 0;   ///< offset behind the token
        };

        bool isIdentifierStart(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        bool isDigit(char c) {
            return c >= '0' && c <= '9';
        }

        std::vector<Token> tokenize(const std::string &text) {
            static constexpr std::string_view MultiCharSymbols[] = {":>>", "::>", ":>", "::", ":=", "..", "==", "!=",
                                                                    "<=", ">=", "->", "**", "=>"};
            std::vector<Token> tokens;
            std::size_t line = 1;
            std::size_t i = 0;
            const std::size_t size = text.size();

            while (i < size) {
                const char c = text[i];
                if (c == '\n') {
                    ++line;
                    ++i;
                    continue;
                }
                if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
                    ++i;
                    continue;
                }

                const std::size_t startLine = line;
                // comments
                if (c == '/' && i + 1 < size && text[i + 1] == '/') {
                    const std::size_t end = text.find('\n', i);
                    const std::size_t stop = end == std::string::npos ? size : end;
                    tokens.push_back({TokenType::Comment, text.substr(i, stop - i), startLine, i, stop});
                    i = stop;
                    continue;
                }
                if (c == '/' && i + 1 < size && text[i + 1] == '*') {
                    std::size_t end = text.find("*/", i + 2);
                    const std::size_t stop = end == std::string::npos ? size : end + 2;
                    line += static_cast<std::size_t>(std::count(text.begin() + std::ptrdiff_t(i),
                                                                text.begin() + std::ptrdiff_t(stop), '\n'));
                    tokens.push_back({TokenType::Comment, text.substr(i, stop - i), startLine, i, stop});
                    i = stop;
                    continue;
                }
                // quoted name / string
                if (c == '\'' || c == '"') {
                    std::string value;
                    std::size_t j = i + 1;
                    while (j < size && text[j] != c) {
                        if (text[j] == '\\' && j + 1 < size) {
                            ++j;
                        }
                        if (text[j] == '\n')
                            ++line;
                        value += text[j];
                        ++j;
                    }
                    const std::size_t stop = j < size ? j + 1 : size;
                    tokens.push_back({c == '\'' ? TokenType::Name : TokenType::String, value, startLine, i, stop});
                    i = stop;
                    continue;
                }
                if (isIdentifierStart(c)) {
                    std::size_t j = i + 1;
                    while (j < size && (isIdentifierStart(text[j]) || isDigit(text[j])))
                        ++j;
                    tokens.push_back({TokenType::Identifier, text.substr(i, j - i), startLine, i, j});
                    i = j;
                    continue;
                }
                if (isDigit(c)) {
                    std::size_t j = i + 1;
                    while (j < size && isDigit(text[j]))
                        ++j;
                    if (j + 1 < size && text[j] == '.' && isDigit(text[j + 1])) {
                        ++j;
                        while (j < size && isDigit(text[j]))
                            ++j;
                    }
                    if (j < size && (text[j] == 'e' || text[j] == 'E')) {
                        std::size_t k = j + 1;
                        if (k < size && (text[k] == '+' || text[k] == '-'))
                            ++k;
                        if (k < size && isDigit(text[k])) {
                            j = k;
                            while (j < size && isDigit(text[j]))
                                ++j;
                        }
                    }
                    tokens.push_back({TokenType::Number, text.substr(i, j - i), startLine, i, j});
                    i = j;
                    continue;
                }
                bool matched = false;
                for (const auto symbol: MultiCharSymbols) {
                    if (text.compare(i, symbol.size(), symbol) == 0) {
                        tokens.push_back({TokenType::Symbol, std::string(symbol), startLine, i, i + symbol.size()});
                        i += symbol.size();
                        matched = true;
                        break;
                    }
                }
                if (matched)
                    continue;
                // single character (a multi-byte UTF-8 character counts as one symbol)
                std::size_t j = i + 1;
                if (static_cast<unsigned char>(c) >= 0x80) {
                    while (j < size && (static_cast<unsigned char>(text[j]) & 0xC0) == 0x80)
                        ++j;
                }
                tokens.push_back({TokenType::Symbol, text.substr(i, j - i), startLine, i, j});
                i = j;
            }
            tokens.push_back({TokenType::End, std::string(), line, size, size});
            return tokens;
        }

        // ---------------------------------------------------------------- parser

        /// Words before a declaration keyword; kept as prefixes of the element.
        const std::set<std::string, std::less<>> PrefixWords = {
                "abstract", "readonly", "constant", "ref", "variation", "individual", "end", "in", "out", "inout",
                "composite", "portion", "var", "public", "private", "protected", "derived", "library", "standard"};

        struct Pending {
            enum class Type {
                Specialization,
                Connection,
                Flow,
                Binding
            };
            Type Kind = Type::Specialization;
            ElementId Source = RootId;          ///< Specialization: the specializing element
            ElementId Scope = RootId;           ///< where name resolution starts
            std::vector<std::string> First;     ///< Specialization: target, Connection/Flow: source end
            std::vector<std::string> Second;    ///< Connection/Flow: target end
            std::size_t Line = 0;
            std::string Text;                   ///< Specialization: reference as written
            std::size_t Begin = 0;              ///< Connection/Flow: statement in the source (kept as text if
            std::size_t End = 0;                ///< its ends cannot be resolved)
        };

        std::string joinPath(const std::vector<std::string> &path) {
            std::string text;
            for (const auto &segment: path) {
                if (!text.empty())
                    text += "::";
                text += segment;
            }
            return text;
        }

        /// Leading keywords of a statement kept as text, e.g. "assert constraint", "function", "constraint def".
        const std::set<std::string, std::less<>> StatementKeywords = {
                "abstract", "accept", "action", "actor", "alias", "allocate", "allocation", "analysis", "assert", "assign",
                "assume", "attribute", "bind", "calc", "case", "comment", "concern", "connect", "connection", "constraint",
                "decide", "def", "dependency", "do", "doc", "end", "entry", "enum", "event", "exhibit", "exit", "expose",
                "filter", "first", "flow", "for", "fork", "function", "if", "import", "in", "include", "individual",
                "inout", "interface", "item", "join", "language", "loop", "merge", "message", "metadata", "objective",
                "occurrence", "out", "package", "part", "perform", "port", "private", "protected", "public", "ref",
                "render", "rendering", "rep", "require", "requirement", "return", "satisfy", "send", "snapshot",
                "stakeholder", "standard", "state", "subject", "succession", "then", "timeslice", "transition", "use",
                "variant", "variation", "verification", "verify", "view", "viewpoint", "while", "derived", "readonly",
                "constant", "library", "predicate", "behavior", "feature", "class", "struct", "classifier", "datatype",
                "type", "assoc", "step", "expr", "inv", "true", "false"};

        /// Column of offset in its line (only if the line starts with whitespace up to there).
        std::size_t columnOf(const std::string &source, std::size_t offset) {
            const std::size_t lineStart = offset == 0 ? 0 : source.rfind('\n', offset - 1);
            const std::size_t begin = lineStart == std::string::npos ? 0 : (offset == 0 ? 0 : lineStart + 1);
            for (std::size_t i = begin; i < offset; ++i) {
                if (source[i] != ' ' && source[i] != '\t')
                    return 0;
            }
            return offset - begin;
        }

        std::string trimRight(std::string text) {
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.pop_back();
            return text;
        }

        std::vector<std::string> splitLines(const std::string &text) {
            std::vector<std::string> lines;
            std::size_t start = 0;
            while (start <= text.size()) {
                std::size_t end = text.find('\n', start);
                if (end == std::string::npos)
                    end = text.size();
                lines.push_back(trimRight(text.substr(start, end - start)));
                start = end + 1;
            }
            return lines;
        }

        /// Statement text for a Textual element: comments keep their own layout (only the common indentation is
        /// removed), code is indented by its brace depth (4 spaces), so the result is independent of the source layout.
        std::string normalizeStatement(const std::string &raw, std::size_t firstColumn, bool comment) {
            auto lines = splitLines(raw);
            while (lines.size() > 1 && lines.back().empty())
                lines.pop_back();
            std::string result;
            if (comment) {
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    std::string line = lines[i];
                    if (i > 0) {
                        std::size_t remove = 0;
                        while (remove < firstColumn && remove < line.size() && (line[remove] == ' ' || line[remove] == '\t'))
                            ++remove;
                        line = line.substr(remove);
                    }
                    if (i > 0)
                        result += '\n';
                    result += line;
                }
                return result;
            }
            int depth = 0;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const std::size_t first = lines[i].find_first_not_of(" \t");
                const std::string line = first == std::string::npos ? std::string() : lines[i].substr(first);
                int opens = 0;
                int closes = 0;
                bool inQuote = false;
                char quote = 0;
                for (std::size_t c = 0; c < line.size(); ++c) {
                    const char ch = line[c];
                    if (inQuote) {
                        if (ch == '\\')
                            ++c;
                        else if (ch == quote)
                            inQuote = false;
                        continue;
                    }
                    if (ch == '\'' || ch == '"') {
                        inQuote = true;
                        quote = ch;
                    } else if (ch == '/' && c + 1 < line.size() && line[c + 1] == '/') {
                        break;
                    } else if (ch == '{') {
                        ++opens;
                    } else if (ch == '}') {
                        ++closes;
                    }
                }
                const int lineDepth = std::max(0, line.rfind('}', 0) == 0 ? depth - 1 : depth);
                if (i > 0)
                    result += '\n';
                if (!line.empty())
                    result += std::string(std::size_t(lineDepth) * 4, ' ') + line;
                depth = std::max(0, depth + opens - closes);
            }
            return result;
        }

        class Parser {
        public:
            Parser(const std::string &source, const std::vector<Token> &tokens, SemanticModel &model,
                   SysMLTextImporter::Dialect dialect, ImportReport &report) :
                    Source(source),
                    Tokens(tokens),
                    Model(model),
                    Dialect(dialect),
                    Report(report),
                    Covered(tokens.size(), false) {
            }

            void run() {
                parseBody(RootId, false);
                resolve();
                for (std::size_t i = 0; i < Tokens.size(); ++i) {
                    if (Tokens[i].Type == TokenType::Comment && !Covered[i])
                        ++Report.Comments;
                }
            }

        private:
            // --- token access (comments are skipped unless asked for) ---------------------------

            void skipComments() {
                while (Position < Tokens.size() && Tokens[Position].Type == TokenType::Comment)
                    ++Position;
            }

            /// Next token that is not a comment (does not move Position, so comments between statements
            /// stay available for keepComments).
            const Token &peek(std::size_t ahead = 0) {
                std::size_t index = Position;
                while (index < Tokens.size() - 1 && Tokens[index].Type == TokenType::Comment)
                    ++index;
                for (std::size_t step = 0; step < ahead && index < Tokens.size() - 1; ++step) {
                    ++index;
                    while (index < Tokens.size() - 1 && Tokens[index].Type == TokenType::Comment)
                        ++index;
                }
                return Tokens[std::min(index, Tokens.size() - 1)];
            }

            Token take() {
                skipComments();
                const Token token = Tokens[Position];
                if (Position < Tokens.size() - 1) {
                    ++Position;
                    LastEnd = token.End;
                }
                return token;
            }

            bool atEnd() {
                return peek().Type == TokenType::End;
            }

            bool isSymbol(std::string_view symbol, std::size_t ahead = 0) {
                const Token &token = peek(ahead);
                return token.Type == TokenType::Symbol && token.Text == symbol;
            }

            bool isWord(std::string_view word, std::size_t ahead = 0) {
                const Token &token = peek(ahead);
                return token.Type == TokenType::Identifier && token.Text == word;
            }

            bool isNameToken(std::size_t ahead = 0) {
                const Token &token = peek(ahead);
                return token.Type == TokenType::Identifier || token.Type == TokenType::Name;
            }

            void lost(std::string what, std::size_t line) {
                Report.SkippedConstructs.push_back({std::move(what), line});
            }

            // --- skipping ----------------------------------------------------------------------

            /// Current token is '{': skips up to and including the matching '}'.
            void skipBlock() {
                int depth = 0;
                while (!atEnd()) {
                    const Token token = take();
                    if (token.Type != TokenType::Symbol)
                        continue;
                    if (token.Text == "{") {
                        ++depth;
                    } else if (token.Text == "}") {
                        if (--depth <= 0)
                            return;
                    }
                }
            }

            /// Skips one statement: up to and including ';' or a complete '{ ... }' body.
            /// Stops before a '}' that closes the enclosing body. Always consumes at least one token.
            void skipStatement() {
                int depth = 0;
                bool consumed = false;
                while (!atEnd()) {
                    const Token &token = peek();
                    if (token.Type == TokenType::Symbol) {
                        if (token.Text == "{" && depth == 0) {
                            skipBlock();
                            if (isSymbol(";"))
                                take();
                            return;
                        }
                        if (token.Text == "}" && depth == 0) {
                            if (!consumed)
                                take();
                            return;
                        }
                        if (token.Text == ";" && depth == 0) {
                            take();
                            return;
                        }
                        if (token.Text == "(" || token.Text == "[")
                            ++depth;
                        else if ((token.Text == ")" || token.Text == "]") && depth > 0)
                            --depth;
                    }
                    take();
                    consumed = true;
                }
            }

            /// Skips a value expression; stops before ';' or a '{' body at depth 0.
            void skipExpression() {
                int depth = 0;
                while (!atEnd()) {
                    const Token &token = peek();
                    if (token.Type == TokenType::Symbol) {
                        if (depth == 0 && (token.Text == ";" || token.Text == "{" || token.Text == "}"))
                            return;
                        if (token.Text == "(" || token.Text == "[" || token.Text == "{")
                            ++depth;
                        else if ((token.Text == ")" || token.Text == "]" || token.Text == "}") && depth > 0)
                            --depth;
                    }
                    take();
                }
            }

            /// Rest of the current statement (up to ';' or including its body) is consumed.
            void skipRestOfStatement() {
                if (isSymbol("{")) {
                    skipBlock();
                    if (isSymbol(";"))
                        take();
                } else if (isSymbol(";")) {
                    take();
                } else if (!atEnd() && !isSymbol("}")) {
                    skipStatement();
                }
            }

            // --- statements kept as text --------------------------------------------------------

            /// Text from begin to end becomes a Textual element of owner (comments inside are part of it).
            void keepText(ElementId owner, std::size_t begin, std::size_t end, std::size_t line, bool comment = false) {
                if (end <= begin)
                    return;
                const std::string raw = Source.substr(begin, end - begin);
                const std::string text = normalizeStatement(raw, columnOf(Source, begin), comment);
                const auto [keyword, name] = SysMLTextImporter::describeStatement(text);
                try {
                    const ElementId id = Model.addElement(ElementKind::Textual, name, owner);
                    Model.setText(id, text, keyword, name);
                    ++Report.ImportedElements;
                    for (std::size_t i = 0; i < Tokens.size(); ++i) {
                        if (Tokens[i].Type == TokenType::Comment && Tokens[i].Begin >= begin && Tokens[i].End <= end)
                            Covered[i] = true;
                    }
                } catch (const std::exception &exception) {
                    lost((keyword.empty() ? std::string("statement") : keyword) + (name.empty() ? "" : " " + name) +
                         " (" + exception.what() + ")", line);
                }
            }

            /// The statement that started at begin could not be read as structure: keep all of it as text.
            void fallBackToText(ElementId owner, std::size_t begin, std::size_t line) {
                skipRestOfStatement();
                std::size_t end = LastEnd;
                // A statement without ';' ends at the closing '}': comments before it belong to the statement
                // (peek() has already stepped over them). Looked up without moving Position.
                std::size_t next = Position;
                while (next < Tokens.size() - 1 && Tokens[next].Type == TokenType::Comment)
                    ++next;
                const bool closed = LastEnd > 0 && (Source[LastEnd - 1] == ';' || Source[LastEnd - 1] == '}');
                if (!closed && next > Position && Tokens[next].Type == TokenType::Symbol && Tokens[next].Text == "}") {
                    for (std::size_t i = Position; i < next; ++i)
                        end = std::max(end, Tokens[i].End);
                    Position = next;
                }
                if (keepAsExpression(owner, begin, end))
                    return;
                keepText(owner, begin, end, line);
            }

            /// The last statement of a constraint / calc body without ';' is its expression.
            bool keepAsExpression(ElementId owner, std::size_t begin, std::size_t end) {
                if (owner == RootId || end <= begin)
                    return false;
                std::size_t next = Position;
                while (next < Tokens.size() - 1 && Tokens[next].Type == TokenType::Comment)
                    ++next;
                if (!(Tokens[next].Type == TokenType::Symbol && Tokens[next].Text == "}"))
                    return false;
                const auto &element = Model.element(owner);
                if (!isConstraintOrCalc(element.kind()) || !element.expression().empty())
                    return false;
                const std::string raw = Source.substr(begin, end - begin);
                const auto last = raw.find_last_not_of(" \t\r\n");
                if (last == std::string::npos || raw[last] == ';' || raw[last] == '}')
                    return false;
                try {
                    Model.setExpression(owner, normalizeStatement(raw, columnOf(Source, begin), false));
                } catch (const std::exception &) {
                    return false;
                }
                for (std::size_t i = 0; i < Tokens.size(); ++i) {
                    if (Tokens[i].Type == TokenType::Comment && Tokens[i].Begin >= begin && Tokens[i].End <= end)
                        Covered[i] = true;
                }
                return true;
            }

            /// Comments standing on their own between statements are kept (as Textual "comment" elements).
            void keepComments(ElementId owner) {
                while (Position < Tokens.size() && Tokens[Position].Type == TokenType::Comment) {
                    const Token &comment = Tokens[Position];
                    const std::size_t index = Position;
                    ++Position;
                    keepText(owner, comment.Begin, comment.End, comment.Line, true);
                    Covered[index] = true;
                }
            }

            // --- names --------------------------------------------------------------------------

            std::string readName() {
                if (!isNameToken())
                    return {};
                return take().Text;
            }

            /// a::b.c -> {"a", "b", "c"}
            std::vector<std::string> readPath() {
                std::vector<std::string> path;
                if (!isNameToken())
                    return path;
                path.push_back(take().Text);
                while ((isSymbol("::") || isSymbol(".")) && isNameToken(1)) {
                    take();
                    path.push_back(take().Text);
                }
                return path;
            }

            /// Reference as written in the source (separators and quoting kept), e.g. "ISQ::MassValue".
            std::string readReferenceText() {
                if (!isNameToken())
                    return {};
                const std::size_t begin = peek().Begin;
                take();
                while ((isSymbol("::") || isSymbol(".")) && isNameToken(1)) {
                    take();
                    take();
                }
                return Source.substr(begin, LastEnd - begin);
            }

            /// Type text as written (separators kept), e.g. "ISQ::MassValue".
            std::string readTypeText() {
                if (!isNameToken())
                    return {};
                std::string text = take().Text;
                while ((isSymbol("::") || isSymbol(".")) && isNameToken(1)) {
                    text += take().Text;
                    text += take().Text;
                }
                return text;
            }

            struct Reference {
                std::vector<std::string> Path;
                std::string Text;
            };

            std::vector<Reference> readReferenceList() {
                std::vector<Reference> list;
                while (isNameToken()) {
                    const std::size_t begin = peek().Begin;
                    Reference reference;
                    reference.Path = readPath();
                    reference.Text = Source.substr(begin, LastEnd - begin);
                    list.push_back(std::move(reference));
                    if (!(isSymbol(",") && isNameToken(1)))
                        break;
                    take();
                }
                return list;
            }

            std::vector<std::vector<std::string>> readPathList() {
                std::vector<std::vector<std::string>> list;
                for (auto &reference: readReferenceList())
                    list.push_back(std::move(reference.Path));
                return list;
            }

            std::optional<Multiplicity> readMultiplicity(bool &valid) {
                // current token is '['
                take();
                valid = false;
                std::optional<Multiplicity> result;
                const auto number = [&](std::optional<std::size_t> &value) {
                    if (peek().Type == TokenType::Number && peek().Text.size() <= 18 &&
                        peek().Text.find_first_not_of("0123456789") == std::string::npos) {
                        value = static_cast<std::size_t>(std::stoull(take().Text));
                        return true;
                    }
                    if (isSymbol("*")) {
                        take();
                        value = std::nullopt;
                        return true;
                    }
                    return false;
                };
                std::optional<std::size_t> lower;
                const bool lowerIsStar = isSymbol("*");
                if (number(lower)) {
                    if (isSymbol("..")) {
                        take();
                        std::optional<std::size_t> upper;
                        if (!lowerIsStar && number(upper) && isSymbol("]")) {
                            try {
                                result = Multiplicity::range(*lower, upper);
                                valid = true;
                            } catch (const std::exception &) {
                            }
                        }
                    } else if (isSymbol("]")) {
                        if (lowerIsStar)
                            result = Multiplicity::range(0, std::nullopt);
                        else
                            result = Multiplicity::exactly(*lower);
                        valid = true;
                    }
                }
                // up to and including ']'
                int depth = 0;
                while (!atEnd()) {
                    const Token token = take();
                    if (token.Type != TokenType::Symbol)
                        continue;
                    if (token.Text == "[")
                        ++depth;
                    else if (token.Text == "]" && depth-- == 0)
                        break;
                }
                return valid ? result : std::nullopt;
            }

            /// Expression from begin to end on one line (comments inside are dropped and counted as lost).
            std::string expressionText(std::size_t begin, std::size_t end) const {
                std::string text;
                std::size_t previousEnd = begin;
                for (const auto &token: Tokens) {
                    if (token.Type == TokenType::End || token.Type == TokenType::Comment || token.Begin < begin || token.End > end)
                        continue;
                    if (!text.empty() && token.Begin > previousEnd)
                        text += ' ';
                    text += Source.substr(token.Begin, token.End - token.Begin);
                    previousEnd = token.End;
                }
                return text;
            }

            // --- elements -----------------------------------------------------------------------

            std::optional<ElementId> add(ElementKind kind, const std::string &name, ElementId owner) {
                try {
                    const ElementId id = Model.addElement(kind, name, owner);
                    ++Report.ImportedElements;
                    return id;
                } catch (const std::exception &) {
                    return std::nullopt; // the caller keeps the statement as text
                }
            }

            void applyPrefixes(ElementId id, const std::vector<std::string> &prefixes) {
                if (!prefixes.empty())
                    Model.setPrefixes(id, prefixes);
            }

            // --- statements ---------------------------------------------------------------------

            void parseBody(ElementId owner, bool inBraces) {
                while (true) {
                    keepComments(owner);
                    if (atEnd())
                        return;
                    if (isSymbol("}")) {
                        const std::size_t line = peek().Line;
                        take();
                        if (inBraces)
                            return;
                        lost("unmatched '}'", line);
                        continue;
                    }
                    const std::size_t before = Position;
                    parseStatement(owner);
                    if (Position == before) // safety net: never stall
                        take();
                }
            }

            void parseStatement(ElementId owner) {
                const std::size_t line = peek().Line;
                const std::size_t begin = peek().Begin;
                if (isSymbol(";")) {
                    take();
                    return;
                }

                // Prefixes: visibility, modifiers, metadata (#name)
                std::vector<std::string> prefixes;
                while (true) {
                    if (peek().Type == TokenType::Identifier && PrefixWords.count(peek().Text) > 0 &&
                        !isSymbol(";", 1) && !isSymbol(":", 1) && !isSymbol("{", 1)) {
                        prefixes.push_back(take().Text);
                        continue;
                    }
                    if (isSymbol("#") && isNameToken(1)) {
                        take();
                        prefixes.push_back("#" + readReferenceText());
                        continue;
                    }
                    if (inEvaluationBody(owner) && isWord("return") && prefixes.empty() && isNameToken(1) &&
                        !isSymbol(":", 1)) {
                        prefixes.push_back(take().Text);
                        continue;
                    }
                    break;
                }
                const bool directed = std::any_of(prefixes.begin(), prefixes.end(), [](const std::string &prefix) {
                    return prefix == "in" || prefix == "out" || prefix == "inout" || prefix == "return";
                });

                if (atEnd() || isSymbol("}")) {
                    if (!prefixes.empty())
                        keepText(owner, begin, LastEnd, line);
                    return;
                }
                if (peek().Type == TokenType::Name && directed && inEvaluationBody(owner)) {
                    // in 'my value' : Real;
                    parseUsage(owner, ElementKind::Attribute, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string());
                    return;
                }
                if (peek().Type != TokenType::Identifier) {
                    fallBackToText(owner, begin, line);
                    return;
                }

                const std::string keyword = peek().Text;
                const bool kerml = Dialect == SysMLTextImporter::Dialect::KerML;
                if (keyword == "package") {
                    take();
                    parsePackage(owner, prefixes, begin, line);
                } else if ((keyword == "doc" || keyword == "comment") && prefixes.empty()) {
                    parseComment();
                    keepText(owner, begin, LastEnd, line, true);
                } else if (!kerml && keyword == "constraint" && isWord("def", 1)) {
                    take();
                    take();
                    parseEvaluation(owner, ElementKind::ConstraintDefinition, std::nullopt, prefixes, begin, line);
                } else if (!kerml && keyword == "calc" && isWord("def", 1)) {
                    take();
                    take();
                    parseEvaluation(owner, ElementKind::CalcDefinition, std::nullopt, prefixes, begin, line);
                } else if (!kerml && keyword == "constraint") {
                    take();
                    parseEvaluation(owner, ElementKind::ConstraintUsage, std::nullopt, prefixes, begin, line);
                } else if (!kerml && keyword == "assert" && isWord("constraint", 1)) {
                    take();
                    take();
                    parseEvaluation(owner, ElementKind::ConstraintUsage, std::string("assert constraint"), prefixes,
                                    begin, line);
                } else if (!kerml && keyword == "calc") {
                    take();
                    parseEvaluation(owner, ElementKind::CalcUsage, std::nullopt, prefixes, begin, line);
                } else if (keyword == "function") {
                    take();
                    parseEvaluation(owner, ElementKind::CalcDefinition, std::string("function"), prefixes, begin, line);
                } else if (keyword == "predicate") {
                    take();
                    parseEvaluation(owner, ElementKind::ConstraintDefinition, std::string("predicate"), prefixes, begin,
                                    line);
                } else if (inEvaluationBody(owner) && keyword == "feature") {
                    // in feature raw : Integer;   (KerML style parameter)
                    take();
                    parseUsage(owner, ElementKind::Attribute, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string("feature"));
                } else if (inEvaluationBody(owner) && keyword == "attribute" && !isWord("def", 1) && directed) {
                    take();
                    parseUsage(owner, ElementKind::Attribute, prefixes, begin, line);
                } else if (inEvaluationBody(owner) && directed && isNameToken() &&
                           (isSymbol(":", 1) || isSymbol(";", 1) || isSymbol("[", 1) || isSymbol("=", 1) ||
                            isSymbol(":=", 1) || isWord("default", 1) || isSymbol(":>", 1))) {
                    // in x : Real;   (parameter without keyword)
                    parseUsage(owner, ElementKind::Attribute, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string());
                } else if (Dialect == SysMLTextImporter::Dialect::SysML) {
                    if (keyword == "part" && isWord("def", 1)) {
                        take();
                        take();
                        parseDefinition(owner, prefixes, begin, line);
                    } else if (keyword == "part") {
                        take();
                        parseUsage(owner, ElementKind::PartUsage, prefixes, begin, line);
                    } else if (keyword == "attribute" && !isWord("def", 1)) {
                        take();
                        parseUsage(owner, ElementKind::Attribute, prefixes, begin, line);
                    } else if (keyword == "perform") {
                        take();
                        parsePerform(owner, prefixes, begin, line);
                    } else if (keyword == "connect" && prefixes.empty()) {
                        take();
                        parseConnect(owner, begin, line);
                    } else if (keyword == "bind" && prefixes.empty()) {
                        take();
                        parseBind(owner, begin, line);
                    } else if (keyword == "flow" && prefixes.empty() && (isWord("from", 1) || isWord("of", 1))) {
                        take();
                        parseFlow(owner, begin, line);
                    } else {
                        fallBackToText(owner, begin, line);
                    }
                } else {
                    if (keyword == "class" || keyword == "struct" || keyword == "classifier" ||
                        keyword == "datatype" || keyword == "type") {
                        // KerML definitions are shown as definitions, but written back as text of their own kind:
                        // keep the statement as text when anything but the plain structure is used.
                        take();
                        parseDefinition(owner, prefixes, begin, line);
                    } else if (keyword == "feature") {
                        take();
                        parseUsage(owner, ElementKind::Attribute, prefixes, begin, line);
                    } else {
                        fallBackToText(owner, begin, line);
                    }
                }
            }

            void parseComment() {
                // doc / comment [name] [about a, b] [locale "..."] /* text */
                take();
                // The comment text must be taken before peek() would skip it as an ordinary comment.
                const auto takeComment = [this] {
                    if (Tokens[Position].Type != TokenType::Comment)
                        return false;
                    LastEnd = Tokens[Position].End;
                    ++Position;
                    if (isSymbol(";"))
                        take();
                    return true;
                };
                if (takeComment())
                    return;
                if (isNameToken() && !isWord("about") && !isWord("locale"))
                    take();
                if (takeComment())
                    return;
                if (isWord("about")) {
                    take();
                    readPathList();
                    if (takeComment())
                        return;
                }
                if (isWord("locale")) {
                    take();
                    if (peek().Type == TokenType::String)
                        take();
                    if (takeComment())
                        return;
                }
                if (isSymbol(";"))
                    take();
            }

            void parsePackage(ElementId owner, const std::vector<std::string> &prefixes, std::size_t begin,
                              std::size_t line) {
                const std::string name = readName();
                if (name.empty() || (!isSymbol(";") && !isSymbol("{"))) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                const auto id = add(ElementKind::Package, name, owner);
                if (!id) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                applyPrefixes(*id, prefixes);
                finishDeclaration(*id);
            }

            /// ';' or a body.
            void finishDeclaration(ElementId id) {
                if (isSymbol(";")) {
                    take();
                } else if (isSymbol("{")) {
                    take();
                    parseBody(id, true);
                }
            }

            void parseDefinition(ElementId owner, const std::vector<std::string> &prefixes, std::size_t begin,
                                 std::size_t line) {
                const std::string name = readName();
                std::vector<Reference> specializations;
                while (!name.empty() && (isSymbol(":>") || isWord("specializes") || isWord("subclassifies"))) {
                    take();
                    for (auto &reference: readReferenceList())
                        specializations.push_back(std::move(reference));
                }
                // Anything else in the declaration (e.g. a KerML keyword the diagram does not know): keep as text.
                const bool kerml = Dialect == SysMLTextImporter::Dialect::KerML;
                if (name.empty() || (!isSymbol(";") && !isSymbol("{"))) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                const auto id = add(ElementKind::PartDefinition, name, owner);
                if (!id) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                if (!kerml)
                    applyPrefixes(*id, prefixes);
                for (auto &reference: specializations)
                    Pendings.push_back({Pending::Type::Specialization, *id, owner, std::move(reference.Path), {}, line,
                                        reference.Text, 0, 0});
                finishDeclaration(*id);
            }

            void parsePerform(ElementId owner, const std::vector<std::string> &prefixes, std::size_t begin,
                              std::size_t line) {
                PerformStyle style = PerformStyle::ReferenceAction;
                if (isWord("action")) {
                    take();
                    style = PerformStyle::DeclareAction;
                }
                if (style == PerformStyle::ReferenceAction && isNameToken() && (isSymbol(".", 1) || isSymbol("::", 1))) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                parseUsage(owner, ElementKind::ActionPerformance, prefixes, begin, line, style);
            }

            void parseUsage(ElementId owner, ElementKind kind, const std::vector<std::string> &prefixes, std::size_t begin,
                            std::size_t line, PerformStyle style = PerformStyle::DeclareAction,
                            const std::optional<std::string> &declaredKeyword = std::nullopt) {
                const std::string name = readName();
                if (name.empty()) {
                    fallBackToText(owner, begin, line);
                    return;
                }

                std::string typeName;
                std::optional<Multiplicity> multiplicity;
                std::vector<Reference> specializations;
                std::string valueOperator;
                std::string value;
                bool plain = true; // false: something the model cannot hold -> the statement is kept as text

                while (plain && !atEnd() && !isSymbol(";") && !isSymbol("{") && !isSymbol("}")) {
                    if (isSymbol(":") && typeName.empty()) {
                        take();
                        typeName = readTypeText();
                        plain = !typeName.empty() && !isSymbol(",");
                    } else if (isSymbol("[") && !multiplicity) {
                        bool valid = false;
                        multiplicity = readMultiplicity(valid);
                        plain = valid;
                    } else if (isSymbol(":>")) {
                        take();
                        for (auto &reference: readReferenceList())
                            specializations.push_back(std::move(reference));
                    } else if ((isSymbol("=") || isSymbol(":=") || isWord("default")) && value.empty()) {
                        valueOperator = take().Text;
                        if (valueOperator == "default" && (isSymbol("=") || isSymbol(":=")))
                            valueOperator += " " + take().Text;
                        const std::size_t from = peek().Begin;
                        skipExpression();
                        value = LastEnd > from ? expressionText(from, LastEnd) : std::string();
                        plain = !value.empty();
                    } else {
                        plain = false;
                    }
                }

                const bool hasBody = isSymbol("{");
                if (!plain || (kind == ElementKind::Attribute && !specializations.empty()) ||
                    (kind != ElementKind::PartUsage && hasBody) ||
                    (kind == ElementKind::ActionPerformance && (multiplicity || !value.empty())) ||
                    (Dialect == SysMLTextImporter::Dialect::KerML && !prefixes.empty() && !inEvaluationBody(owner))) {
                    fallBackToText(owner, begin, line);
                    return;
                }

                const auto id = add(kind, name, owner);
                if (!id) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                try {
                    if (!typeName.empty())
                        Model.setTypeName(*id, typeName);
                    if (multiplicity)
                        Model.setMultiplicity(*id, multiplicity);
                    if (kind == ElementKind::ActionPerformance)
                        Model.setPerformStyle(*id, style);
                    if (!value.empty())
                        Model.setValue(*id, valueOperator, value);
                    applyPrefixes(*id, prefixes);
                    if (declaredKeyword.has_value())
                        Model.setDeclaredKeyword(*id, declaredKeyword);
                } catch (const std::exception &exception) {
                    lost(std::string(exception.what()) + " (" + name + ")", line);
                }
                for (auto &reference: specializations)
                    Pendings.push_back({Pending::Type::Specialization, *id, owner, std::move(reference.Path), {}, line,
                                        reference.Text, 0, 0});
                finishDeclaration(*id);
            }

            bool inEvaluationBody(ElementId owner) const {
                return owner != RootId && isConstraintOrCalc(Model.element(owner).kind());
            }

            /// constraint def / calc def / function / predicate / [assert] constraint / calc, after the keywords.
            void parseEvaluation(ElementId owner, ElementKind kind, const std::optional<std::string> &declaredKeyword,
                                 const std::vector<std::string> &prefixes, std::size_t begin, std::size_t line) {
                const bool definition = isDefinitionKind(kind);
                const std::string name = readName();
                std::string typeName;
                std::optional<Multiplicity> multiplicity;
                std::vector<Reference> specializations;
                bool plain = !name.empty();
                while (plain && !atEnd() && !isSymbol(";") && !isSymbol("{") && !isSymbol("}")) {
                    if (!definition && isSymbol(":") && typeName.empty()) {
                        take();
                        typeName = readTypeText();
                        plain = !typeName.empty() && !isSymbol(",");
                    } else if (!definition && isSymbol("[") && !multiplicity) {
                        bool valid = false;
                        multiplicity = readMultiplicity(valid);
                        plain = valid;
                    } else if (isSymbol(":>") || (definition && (isWord("specializes") || isWord("subclassifies")))) {
                        take();
                        const auto list = readReferenceList();
                        plain = !list.empty();
                        for (const auto &reference: list)
                            specializations.push_back(reference);
                    } else {
                        plain = false;
                    }
                }
                if (!plain || (!isSymbol(";") && !isSymbol("{")) ||
                    (Dialect == SysMLTextImporter::Dialect::KerML && !prefixes.empty() && !inEvaluationBody(owner))) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                const auto id = add(kind, name, owner);
                if (!id) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                try {
                    if (!typeName.empty())
                        Model.setTypeName(*id, typeName);
                    if (multiplicity)
                        Model.setMultiplicity(*id, multiplicity);
                    applyPrefixes(*id, prefixes);
                    if (declaredKeyword.has_value())
                        Model.setDeclaredKeyword(*id, declaredKeyword);
                } catch (const std::exception &exception) {
                    lost(std::string(exception.what()) + " (" + name + ")", line);
                }
                for (auto &reference: specializations)
                    Pendings.push_back({Pending::Type::Specialization, *id, owner, std::move(reference.Path), {}, line,
                                        reference.Text, 0, 0});
                finishDeclaration(*id);
            }

            void parseConnect(ElementId owner, std::size_t begin, std::size_t line) {
                auto source = readPath();
                if (source.empty() || !isWord("to")) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                take();
                auto target = readPath();
                if (target.empty() || !isSymbol(";")) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                take();
                Pendings.push_back({Pending::Type::Connection, RootId, owner, std::move(source), std::move(target), line,
                                    std::string(), begin, LastEnd});
            }

            void parseBind(ElementId owner, std::size_t begin, std::size_t line) {
                auto source = readPath();
                if (source.empty() || !isSymbol("=")) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                take();
                auto target = readPath();
                if (target.empty() || !isSymbol(";")) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                take();
                Pendings.push_back({Pending::Type::Binding, RootId, owner, std::move(source), std::move(target), line,
                                    std::string(), begin, LastEnd});
            }

            void parseFlow(ElementId owner, std::size_t begin, std::size_t line) {
                if (isWord("of")) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                take(); // from
                auto source = readPath();
                if (source.empty() || !isWord("to")) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                take();
                auto target = readPath();
                if (target.empty() || !isSymbol(";")) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                take();
                Pendings.push_back({Pending::Type::Flow, RootId, owner, std::move(source), std::move(target), line,
                                    std::string(), begin, LastEnd});
            }

            // --- name resolution ----------------------------------------------------------------

            using Accept = std::function<bool(ElementId)>;

            std::optional<ElementId> child(ElementId owner, const std::string &name, const Accept &accept = {}) const {
                for (const ElementId id: Model.children(owner)) {
                    if (Model.element(id).name() == name && (!accept || accept(id)))
                        return id;
                }
                return std::nullopt;
            }

            std::optional<ElementId> resolveFromRoot(const std::vector<std::string> &path, const Accept &accept) const {
                std::optional<ElementId> current = RootId;
                for (std::size_t i = 0; current && i < path.size(); ++i)
                    current = child(*current, path[i], i + 1 == path.size() ? accept : Accept());
                return current;
            }

            /// Qualified names of specializations are written from the top level (SysMLCodeGenerator), so they
            /// are tried from there first; other names are searched from the innermost scope outwards.
            /// accept: which elements may be meant by the last name segment (e.g. a specialization refers to an
            /// element of the same kind, never to itself; "part def A :> A" inside a package means an outer A).
            std::optional<ElementId> resolve(ElementId scope, const std::vector<std::string> &path, bool preferRoot,
                                             const Accept &accept) const {
                if (path.empty())
                    return std::nullopt;
                if (preferRoot && path.size() > 1) {
                    if (const auto absolute = resolveFromRoot(path, accept))
                        return absolute;
                }
                const auto nearest = resolveNearest(scope, path, accept);
                return nearest ? nearest : resolveFromRoot(path, accept);
            }

            std::optional<ElementId> resolveNearest(ElementId scope, const std::vector<std::string> &path,
                                                    const Accept &accept) const {
                std::optional<ElementId> current;
                for (ElementId candidate = scope;; candidate = Model.element(candidate).owner()) {
                    if (candidate != RootId && !Model.contains(candidate))
                        break;
                    current = child(candidate, path.front(), path.size() == 1 ? accept : Accept());
                    if (current || candidate == RootId)
                        break;
                }
                for (std::size_t i = 1; current && i < path.size(); ++i)
                    current = child(*current, path[i], i + 1 == path.size() ? accept : Accept());
                return current;
            }

            void resolve() {
                for (const auto &pending: Pendings) {
                    if (pending.Kind == Pending::Type::Specialization) {
                        if (!Model.contains(pending.Source))
                            continue;
                        const std::string sourceName = Model.element(pending.Source).name();
                        const ElementKind sourceKind = Model.element(pending.Source).kind();
                        const ElementId sourceId = pending.Source;
                        const auto target = resolve(pending.Scope, pending.First, true, [&](ElementId id) {
                            return id != sourceId && Model.element(id).kind() == sourceKind;
                        });
                        // Not in this block (e.g. a library type) or not allowed as edge: kept as written.
                        if (!target || !RelationshipRules::canRelate(Model, RelationshipKind::Specialization,
                                                                     pending.Source, *target)) {
                            External[pending.Source].push_back(pending.Text);
                            continue;
                        }
                        addRelationship(RelationshipKind::Specialization, pending.Source, *target,
                                        "specialization of " + sourceName + " by " + joinPath(pending.First), pending.Line);
                        continue;
                    }
                    const bool binding = pending.Kind == Pending::Type::Binding;
                    const std::string label = binding ? "bind " + joinPath(pending.First) + " = " + joinPath(pending.Second)
                                              : (pending.Kind == Pending::Type::Connection ? "connect " : "flow from ") +
                                                joinPath(pending.First) + " to " + joinPath(pending.Second);
                    const ElementKind endKind = binding ? ElementKind::Attribute : ElementKind::PartUsage;
                    const Accept isEnd = [&](ElementId id) { return Model.element(id).kind() == endKind; };
                    const auto source = resolve(pending.Scope, pending.First, false, isEnd);
                    const auto target = resolve(pending.Scope, pending.Second, false, isEnd);
                    const RelationshipKind kind = binding ? RelationshipKind::Binding
                                                  : pending.Kind == Pending::Type::Connection ? RelationshipKind::Connection
                                                                                              : RelationshipKind::Flow;
                    if (!source || !target || !RelationshipRules::canRelate(Model, kind, *source, *target)) {
                        keepText(pending.Scope, pending.Begin, pending.End, pending.Line); // kept as written
                        continue;
                    }
                    addRelationship(kind, *source, *target, label, pending.Line);
                }
                for (auto &[id, references]: External) {
                    try {
                        Model.setExternalSpecializations(id, references);
                    } catch (const std::exception &exception) {
                        lost(std::string(exception.what()), 0);
                    }
                }
            }

            void addRelationship(RelationshipKind kind, ElementId source, ElementId target, const std::string &label,
                                 std::size_t line) {
                try {
                    Model.addRelationship(kind, source, target);
                    ++Report.ImportedRelationships;
                } catch (const std::exception &exception) {
                    lost(label + " (" + exception.what() + ")", line);
                }
            }

            const std::string &Source;
            const std::vector<Token> &Tokens;
            SemanticModel &Model;
            SysMLTextImporter::Dialect Dialect;
            ImportReport &Report;
            std::vector<bool> Covered; ///< comment tokens that are part of a kept text
            std::size_t Position = 0;
            std::size_t LastEnd = 0;   ///< offset behind the last consumed token
            std::vector<Pending> Pendings;
            std::map<ElementId, std::vector<std::string>> External;
        };
    }

    std::string ImportReport::summary(std::size_t maximumItems) const {
        // Group "<what> on|of <element>" by <what>, keep the order of first appearance.
        std::vector<std::pair<std::string, std::size_t>> groups;
        std::vector<std::string> singleTexts;
        for (const auto &item: SkippedConstructs) {
            std::size_t cut = item.What.rfind(" on ");
            const std::size_t of = item.What.rfind(" of ");
            if (cut == std::string::npos || (of != std::string::npos && of > cut))
                cut = of;
            const std::string key = cut == std::string::npos ? item.What : item.What.substr(0, cut);
            auto group = std::find_if(groups.begin(), groups.end(), [&](const auto &entry) { return entry.first == key; });
            if (group == groups.end()) {
                groups.emplace_back(key, 1);
                singleTexts.push_back(item.What);
            } else {
                ++group->second;
            }
        }

        std::string text;
        std::size_t shown = 0;
        for (std::size_t i = 0; i < groups.size() && shown < maximumItems; ++i, ++shown) {
            if (!text.empty())
                text += ", ";
            text += groups[i].second == 1 ? singleTexts[i]
                                          : groups[i].first + " (" + std::to_string(groups[i].second) + "x)";
        }
        if (groups.size() > shown)
            text += " (+" + std::to_string(groups.size() - shown) + " more)";
        if (Comments > 0) {
            if (!text.empty())
                text += ", ";
            text += std::to_string(Comments) + (Comments == 1 ? " comment" : " comments");
        }
        return text;
    }

    std::string ImportReport::details() const {
        std::string text;
        for (const auto &item: SkippedConstructs) {
            if (!text.empty())
                text += '\n';
            text += "line " + std::to_string(item.Line) + ": " + item.What;
        }
        if (Comments > 0) {
            if (!text.empty())
                text += '\n';
            text += std::to_string(Comments) + (Comments == 1 ? " comment" : " comments");
        }
        return text;
    }

    ImportReport SysMLTextImporter::importText(const std::string &text, SemanticModel &model, Dialect dialect) {
        ImportReport report;
        const auto tokens = tokenize(text);
        Parser parser(text, tokens, model, dialect, report);
        parser.run();
        return report;
    }

    std::pair<std::string, std::string> SysMLTextImporter::describeStatement(const std::string &text) {
        const auto tokens = tokenize(text);
        if (tokens.front().Type == TokenType::Comment || tokens.front().Type == TokenType::End)
            return {"comment", ""};
        std::size_t i = 0;
        // skip prefixes and metadata
        while (i < tokens.size()) {
            if (tokens[i].Type == TokenType::Symbol && tokens[i].Text == "#" && i + 1 < tokens.size()) {
                i += 2;
                while (i + 1 < tokens.size() && tokens[i].Type == TokenType::Symbol && tokens[i].Text == "::")
                    i += 2;
                continue;
            }
            if (tokens[i].Type == TokenType::Identifier && PrefixWords.count(tokens[i].Text) > 0 &&
                i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::Identifier &&
                StatementKeywords.count(tokens[i + 1].Text) > 0) {
                ++i;
                continue;
            }
            break;
        }
        std::string keyword;
        while (i < tokens.size() && tokens[i].Type == TokenType::Identifier && StatementKeywords.count(tokens[i].Text) > 0) {
            if (!keyword.empty())
                keyword += ' ';
            keyword += tokens[i].Text;
            ++i;
        }
        std::string name;
        if (!keyword.empty() && i < tokens.size() &&
            (tokens[i].Type == TokenType::Identifier || tokens[i].Type == TokenType::Name))
            name = tokens[i].Text;
        return {keyword, name};
    }

    StatementSummary SysMLTextImporter::summarize(const SysMLElement &element) {
        StatementSummary summary;
        const std::string &keyword = element.keyword();
        const std::string &text = element.text();
        const auto contains = [&](std::string_view word) {
            const std::string padded = " " + keyword + " ";
            return padded.find(" " + std::string(word) + " ") != std::string::npos;
        };
        if (keyword == "comment" || keyword == "doc") {
            summary.Kind = StatementSummary::Category::Comment;
        } else if (contains("constraint")) {
            summary.Kind = StatementSummary::Category::Constraint;
        } else if (contains("function") || contains("calc") || contains("predicate") || contains("expr")) {
            summary.Kind = StatementSummary::Category::Function;
        } else {
            summary.Kind = StatementSummary::Category::Other;
        }

        const auto tokens = tokenize(text);
        const auto oneLine = [&](std::size_t from, std::size_t to) {
            std::string line;
            std::size_t previousEnd = from;
            for (std::size_t k = from; k < to; ++k) {
                if (tokens[k].Type == TokenType::Comment || tokens[k].Type == TokenType::End)
                    continue;
                if (!line.empty() && tokens[k].Begin > tokens[previousEnd].End)
                    line += ' ';
                line += text.substr(tokens[k].Begin, tokens[k].End - tokens[k].Begin);
                previousEnd = k;
            }
            return line;
        };

        // Body between the first '{' and its matching '}'
        std::size_t open = tokens.size();
        for (std::size_t k = 0; k < tokens.size(); ++k) {
            if (tokens[k].Type == TokenType::Symbol && tokens[k].Text == "{") {
                open = k;
                break;
            }
        }
        if (summary.Kind == StatementSummary::Category::Comment || open == tokens.size()) {
            const std::size_t newline = text.find('\n');
            summary.Display = text.substr(0, newline);
            if (summary.Kind != StatementSummary::Category::Comment && open == tokens.size())
                summary.Display = oneLine(0, tokens.size());
            return summary;
        }

        // statements of the body: parameters (in/out/inout), result (return), the rest is the expression
        std::size_t k = open + 1;
        int depth = 0;
        std::size_t statementStart = k;
        std::vector<std::pair<std::size_t, std::size_t>> statements;
        for (; k < tokens.size() && tokens[k].Type != TokenType::End; ++k) {
            const auto &token = tokens[k];
            if (token.Type != TokenType::Symbol)
                continue;
            if (token.Text == "{" || token.Text == "(" || token.Text == "[") {
                ++depth;
            } else if (token.Text == "}" || token.Text == ")" || token.Text == "]") {
                if (depth == 0 && token.Text == "}")
                    break;
                --depth;
            } else if (token.Text == ";" && depth == 0) {
                statements.emplace_back(statementStart, k);
                statementStart = k + 1;
            }
        }
        if (statementStart < k)
            statements.emplace_back(statementStart, k);

        std::vector<std::string> expressions;
        for (const auto &[from, to]: statements) {
            std::size_t first = from;
            while (first < to && tokens[first].Type == TokenType::Comment)
                ++first;
            if (first >= to)
                continue;
            const std::string &head = tokens[first].Text;
            if (tokens[first].Type == TokenType::Identifier && (head == "in" || head == "out" || head == "inout" || head == "return")) {
                std::size_t declaration = first + 1;
                while (declaration < to && tokens[declaration].Type == TokenType::Identifier &&
                       (tokens[declaration].Text == "feature" || tokens[declaration].Text == "attribute" ||
                        tokens[declaration].Text == "ref" || tokens[declaration].Text == "item" ||
                        tokens[declaration].Text == "part"))
                    ++declaration;
                // up to '=' / ':=' (the value of a result is shown separately)
                std::size_t valueStart = to;
                for (std::size_t v = declaration; v < to; ++v) {
                    if (tokens[v].Type == TokenType::Symbol && (tokens[v].Text == "=" || tokens[v].Text == ":=")) {
                        valueStart = v;
                        break;
                    }
                }
                const std::string declarationText = oneLine(declaration, valueStart);
                if (head == "return") {
                    summary.Result = declarationText;
                    const auto colon = declarationText.find(':');
                    summary.ResultType = colon == std::string::npos ? std::string()
                                                                    : declarationText.substr(colon + 1);
                    while (!summary.ResultType.empty() && summary.ResultType.front() == ' ')
                        summary.ResultType.erase(summary.ResultType.begin());
                    if (valueStart < to)
                        expressions.push_back(oneLine(valueStart + 1, to));
                } else {
                    summary.Parameters.push_back((head == "in" ? std::string() : head + " ") + declarationText);
                }
            } else {
                expressions.push_back(oneLine(first, to));
            }
        }
        for (const auto &expression: expressions) {
            if (!summary.Expression.empty())
                summary.Expression += "; ";
            summary.Expression += expression;
        }

        std::string parameters;
        for (const auto &parameter: summary.Parameters) {
            if (!parameters.empty())
                parameters += ", ";
            parameters += parameter;
        }
        const std::string name = element.name().empty() ? keyword : element.name();
        switch (summary.Kind) {
            case StatementSummary::Category::Constraint: {
                std::string prefix;
                if (contains("assert"))
                    prefix = "assert ";
                else if (contains("assume"))
                    prefix = "assume ";
                else if (contains("require"))
                    prefix = "require ";
                if (contains("def"))
                    prefix += "def ";
                summary.Display = prefix + name + (parameters.empty() ? "" : "(" + parameters + ")") +
                                  (summary.Expression.empty() ? "" : " { " + summary.Expression + " }");
                break;
            }
            case StatementSummary::Category::Function:
                summary.Display = name + "(" + parameters + ")" +
                                  (summary.ResultType.empty() ? "" : " : " + summary.ResultType) +
                                  (summary.Expression.empty() ? "" : " = " + summary.Expression);
                break;
            default:
                summary.Display = keyword + (element.name().empty() ? "" : " " + element.name());
                break;
        }
        return summary;
    }

    namespace {
        std::vector<Token> meaningfulTokens(const std::string &text) {
            std::vector<Token> result;
            for (auto &token: tokenize(text)) {
                if (token.Type != TokenType::Comment && token.Type != TokenType::End)
                    result.push_back(std::move(token));
            }
            return result;
        }

        bool isNamePart(const Token &token) {
            return token.Type == TokenType::Identifier || token.Type == TokenType::Name;
        }

        bool isSeparator(const Token &token) {
            return token.Type == TokenType::Symbol && (token.Text == "::" || token.Text == ".");
        }

        /// Index behind the reference that starts at 'from' (a::b.c); 'from' if there is none.
        std::size_t referenceEnd(const std::vector<Token> &tokens, std::size_t from) {
            if (from >= tokens.size() || !isNamePart(tokens[from]))
                return from;
            std::size_t i = from + 1;
            while (i + 1 < tokens.size() && isSeparator(tokens[i]) && isNamePart(tokens[i + 1]))
                i += 2;
            return i;
        }

        std::string sourceText(const std::string &text, const std::vector<Token> &tokens, std::size_t from, std::size_t to) {
            if (from >= to)
                return {};
            std::string result;
            for (std::size_t k = from; k < to; ++k) {
                if (k > from && tokens[k].Begin > tokens[k - 1].End)
                    result += ' ';
                result += text.substr(tokens[k].Begin, tokens[k].End - tokens[k].Begin);
            }
            return result;
        }

        const std::set<std::string, std::less<>> ExpressionKeywords = {
                "and", "or", "xor", "not", "implies", "true", "false", "null", "if", "then", "else", "as",
                "istype", "hastype", "meta", "all", "new", "@", "in"};
    }

    std::optional<CallExpression> SysMLTextImporter::parseCall(const std::string &expression) {
        const auto tokens = meaningfulTokens(expression);
        const std::size_t nameEnd = referenceEnd(tokens, 0);
        if (nameEnd == 0 || nameEnd >= tokens.size() || tokens[nameEnd].Text != "(" ||
            tokens.back().Type != TokenType::Symbol || tokens.back().Text != ")")
            return std::nullopt;
        if (tokens[0].Type == TokenType::Identifier && ExpressionKeywords.count(tokens[0].Text) > 0)
            return std::nullopt;
        CallExpression call;
        call.Function = sourceText(expression, tokens, 0, nameEnd);
        // arguments: split at top-level commas; the '(' must close at the very end
        int depth = 0;
        std::size_t argumentStart = nameEnd + 1;
        const auto addArgument = [&](std::size_t from, std::size_t to) {
            CallArgument argument;
            if (to > from + 2 && isNamePart(tokens[from]) && tokens[from + 1].Type == TokenType::Symbol &&
                tokens[from + 1].Text == "=") {
                argument.Name = sourceText(expression, tokens, from, from + 1);
                from += 2;
            }
            argument.Value = sourceText(expression, tokens, from, to);
            call.Arguments.push_back(std::move(argument));
        };
        for (std::size_t k = nameEnd; k < tokens.size(); ++k) {
            const Token &token = tokens[k];
            if (token.Type != TokenType::Symbol)
                continue;
            if (token.Text == "(" || token.Text == "[" || token.Text == "{") {
                ++depth;
            } else if (token.Text == ")" || token.Text == "]" || token.Text == "}") {
                --depth;
                if (depth == 0 && k + 1 != tokens.size())
                    return std::nullopt; // e.g. "f(a) + g(b)"
                if (depth == 0) {
                    if (k > argumentStart || !call.Arguments.empty())
                        addArgument(argumentStart, k);
                }
            } else if (token.Text == "," && depth == 1) {
                if (k == argumentStart)
                    return std::nullopt;
                addArgument(argumentStart, k);
                argumentStart = k + 1;
            }
        }
        for (const auto &argument: call.Arguments) {
            if (argument.Value.empty())
                return std::nullopt;
        }
        return call;
    }

    std::string SysMLTextImporter::formatCall(const CallExpression &call) {
        std::string text = call.Function + "(";
        for (std::size_t i = 0; i < call.Arguments.size(); ++i) {
            if (i > 0)
                text += ", ";
            if (!call.Arguments[i].Name.empty())
                text += call.Arguments[i].Name + " = ";
            text += call.Arguments[i].Value;
        }
        return text + ")";
    }

    std::optional<std::string> SysMLTextImporter::parseReference(const std::string &expression) {
        const auto tokens = meaningfulTokens(expression);
        if (tokens.empty() || referenceEnd(tokens, 0) != tokens.size())
            return std::nullopt;
        if (tokens.size() == 1 && tokens[0].Type == TokenType::Identifier && ExpressionKeywords.count(tokens[0].Text) > 0)
            return std::nullopt;
        return sourceText(expression, tokens, 0, tokens.size());
    }

    std::vector<std::string> SysMLTextImporter::references(const std::string &expression) {
        const auto tokens = meaningfulTokens(expression);
        std::vector<std::string> result;
        int unitDepth = 0;
        for (std::size_t k = 0; k < tokens.size();) {
            const Token &token = tokens[k];
            if (token.Type == TokenType::Symbol) {
                if (token.Text == "[")
                    ++unitDepth;
                else if (token.Text == "]" && unitDepth > 0)
                    --unitDepth;
                ++k;
                continue;
            }
            if (!isNamePart(token) || unitDepth > 0) {
                ++k;
                continue;
            }
            // a member access after an expression ("(a).b", "x.y" handled as one reference)
            if (k > 0 && isSeparator(tokens[k - 1])) {
                ++k;
                continue;
            }
            const std::size_t end = referenceEnd(tokens, k);
            const bool keyword = end == k + 1 && token.Type == TokenType::Identifier && ExpressionKeywords.count(token.Text) > 0;
            const bool called = end < tokens.size() && tokens[end].Type == TokenType::Symbol && tokens[end].Text == "(";
            const bool argumentName = end == k + 1 && end < tokens.size() && tokens[end].Type == TokenType::Symbol &&
                                      tokens[end].Text == "=" && k > 0 && tokens[k - 1].Type == TokenType::Symbol &&
                                      (tokens[k - 1].Text == "(" || tokens[k - 1].Text == ",");
            if (!keyword && !called && !argumentName) {
                const std::string text = sourceText(expression, tokens, k, end);
                if (std::find(result.begin(), result.end(), text) == result.end())
                    result.push_back(text);
            }
            k = end;
        }
        return result;
    }

    std::vector<std::string> SysMLTextImporter::splitReference(const std::string &reference) {
        std::vector<std::string> segments;
        for (const auto &token: meaningfulTokens(reference)) {
            if (isNamePart(token))
                segments.push_back(token.Text);
        }
        return segments;
    }

    std::string SysMLTextImporter::oneLine(const std::string &expression) {
        const auto tokens = tokenize(expression);
        std::string line;
        std::size_t previousEnd = 0;
        bool first = true;
        for (const auto &token: tokens) {
            if (token.Type == TokenType::Comment || token.Type == TokenType::End)
                continue;
            if (!first && token.Begin > previousEnd)
                line += ' ';
            line += expression.substr(token.Begin, token.End - token.Begin);
            previousEnd = token.End;
            first = false;
        }
        return line;
    }

    StatementSummary SysMLTextImporter::summarize(const SemanticModel &model, ElementId id) {
        StatementSummary summary;
        const auto &element = model.element(id);
        summary.Kind = isConstraintKind(element.kind()) ? StatementSummary::Category::Constraint
                                                        : StatementSummary::Category::Function;
        std::string returnValue;
        for (const ElementId child: element.ownedElements()) {
            const auto &parameter = model.element(child);
            if (parameter.kind() != ElementKind::Attribute)
                continue;
            std::string direction;
            for (const auto &prefix: parameter.prefixes()) {
                if (prefix == "in" || prefix == "out" || prefix == "inout" || prefix == "return")
                    direction = prefix;
            }
            if (direction.empty())
                continue;
            std::string text = parameter.name();
            if (!parameter.typeName().empty())
                text += " : " + parameter.typeName();
            if (parameter.multiplicity().has_value())
                text += parameter.multiplicity()->toString();
            if (direction == "return") {
                summary.Result = text;
                summary.ResultType = parameter.typeName();
                returnValue = parameter.value();
            } else {
                summary.Parameters.push_back(direction == "in" ? text : direction + " " + text);
            }
        }
        summary.Expression = element.expression().empty() ? returnValue : oneLine(element.expression());

        const bool assertion = element.declaredKeyword().has_value() && *element.declaredKeyword() == "assert constraint";
        std::string display = (assertion ? "assert " : "") + element.name();
        if (!element.typeName().empty())
            display += " : " + element.typeName();
        if (summary.Kind == StatementSummary::Category::Constraint) {
            if (!summary.Parameters.empty() && isDefinitionKind(element.kind())) {
                display += "(";
                for (std::size_t i = 0; i < summary.Parameters.size(); ++i)
                    display += (i > 0 ? ", " : "") + summary.Parameters[i];
                display += ")";
            }
            if (!summary.Expression.empty())
                display += " { " + summary.Expression + " }";
        } else {
            if (!summary.Parameters.empty() || element.typeName().empty()) {
                display += "(";
                for (std::size_t i = 0; i < summary.Parameters.size(); ++i)
                    display += (i > 0 ? ", " : "") + summary.Parameters[i];
                display += ")";
            }
            if (!summary.ResultType.empty())
                display += " : " + summary.ResultType;
            if (!summary.Expression.empty())
                display += " = " + summary.Expression;
        }
        summary.Display = display;
        return summary;
    }

    void SysMLTextImporter::clear(SemanticModel &model) {
        const std::vector<ElementId> roots = model.children(RootId);
        for (const ElementId id: roots)
            model.removeElement(id);
        const auto relationships = model.relationships();
        for (const auto &relationship: relationships)
            model.removeRelationship(relationship.Id);
    }
}
