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
                "abstract", "readonly", "constant", "ref", "variation", "variant", "individual", "end", "in", "out", "inout",
                "composite", "portion", "var", "public", "private", "protected", "derived", "library", "standard"};

        struct Pending {
            enum class Type {
                Specialization,
                Connection,
                Flow,
                Binding,
                Interface,
                Succession, ///< Source set (shorthand "then b;") or First = source path
                Transition,
                Satisfy,    ///< First = requirement, Second = "by" element
                Verify,     ///< Source = verification, Scope = its objective, First = requirement
                Include,    ///< Source = use case (= Scope), First = included use case
                Message,    ///< message [name] [of T] from First to Second;
                MessageThen, ///< then message ...;
                Allocation, ///< allocate First to Second (refining allocations: see AllocationParent)
                Dependency, ///< dependency [name] from First to Second;
                Frame       ///< Source = requirement (= Scope), First = framed concern
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
            std::string Name;                   ///< Connection/Flow/Interface: optional name
            std::string TypeName;               ///< Connection/Interface: type, Flow: "of" item type
            std::string Guard;                  ///< Succession/Transition
            std::string Trigger;                ///< Transition
            std::string Effect;                 ///< Transition
            /// Statement kept as text at its place until it is resolved (removed then), so a statement that
            /// cannot be resolved keeps its position in the block.
            std::optional<ElementId> Placeholder;
            /// Succession: the target if it is unnamed ("then if c { ... }", "then fork;"), instead of Second
            std::optional<ElementId> TargetId;
            /// Flow: flow definition after ':' ("flow : FuelFlow of Fuel from ...")
            std::string Definition;
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

        /// Indentation of the line that offset is in.
        std::size_t columnOf(const std::string &source, std::size_t offset) {
            const std::size_t lineStart = offset == 0 ? 0 : source.rfind('\n', offset - 1);
            const std::size_t begin = lineStart == std::string::npos ? 0 : (offset == 0 ? 0 : lineStart + 1);
            std::size_t column = 0;
            while (begin + column < offset && (source[begin + column] == ' ' || source[begin + column] == '\t'))
                ++column;
            return column; // the indentation of the line (also if the statement starts behind another one)
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
        /// Statement text without the indentation of its first line: every line keeps its indentation relative to
        /// the first one (continuation lines, bodies, comment blocks), trailing empty lines are dropped.
        std::string normalizeStatement(const std::string &raw, std::size_t firstColumn) {
            auto lines = splitLines(raw);
            while (lines.size() > 1 && lines.back().empty())
                lines.pop_back();
            std::string result;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                std::string line = lines[i];
                if (i > 0) {
                    std::size_t remove = 0;
                    while (remove < firstColumn && remove < line.size() && (line[remove] == ' ' || line[remove] == '\t'))
                        ++remove;
                    line = line.substr(remove);
                    if (line.find_first_not_of(" \t") == std::string::npos)
                        line.clear();
                    result += '\n';
                }
                result += line;
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

            /// A keyword that starts a declaration or statement ("part", "item", "action", ...), never a name.
            bool isDeclarationKeyword(std::size_t ahead = 0) {
                const Token &token = peek(ahead);
                return token.Type == TokenType::Identifier && StatementKeywords.count(token.Text) > 0;
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
            std::optional<ElementId> keepText(ElementId owner, std::size_t begin, std::size_t end, std::size_t line,
                                              bool = false) {
                if (end <= begin)
                    return std::nullopt;
                const std::string raw = Source.substr(begin, end - begin);
                const std::string text = normalizeStatement(raw, columnOf(Source, begin));
                const auto [keyword, name] = SysMLTextImporter::describeStatement(text);
                try {
                    const ElementId id = Model.addElement(ElementKind::Textual, name, owner);
                    Model.setText(id, text, keyword, name);
                    ++Report.ImportedElements;
                    for (std::size_t i = 0; i < Tokens.size(); ++i) {
                        if (Tokens[i].Type == TokenType::Comment && Tokens[i].Begin >= begin && Tokens[i].End <= end)
                            Covered[i] = true;
                    }
                    return id;
                } catch (const std::exception &exception) {
                    lost((keyword.empty() ? std::string("statement") : keyword) + (name.empty() ? "" : " " + name) +
                         " (" + exception.what() + ")", line);
                }
                return std::nullopt;
            }

            /// The last pending relationship keeps its statement as text at its place until it is resolved.
            void reserve() {
                auto &pending = Pendings.back();
                pending.Placeholder = keepText(pending.Scope, pending.Begin, pending.End, pending.Line);
            }

            /// Resolved: the text kept in its place is not needed anymore (comments in it count as lost again).
            void release(const Pending &pending) {
                if (!pending.Placeholder || !Model.contains(*pending.Placeholder))
                    return;
                Model.removeElement(*pending.Placeholder);
                --Report.ImportedElements;
                for (std::size_t i = 0; i < Tokens.size(); ++i) {
                    if (Tokens[i].Type == TokenType::Comment && Tokens[i].Begin >= pending.Begin && Tokens[i].End <= pending.End)
                        Covered[i] = false;
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
                    Model.setExpression(owner, normalizeStatement(raw, columnOf(Source, begin)));
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

            /// "<'R1'>" / "<R1>" in front of the name: the short name as written (quotes kept), empty if none.
            std::string readShortName() {
                if (!(isSymbol("<") && isNameToken(1) && isSymbol(">", 2)))
                    return {};
                take();
                const Token name = take();
                take();
                return Source.substr(name.Begin, name.End - name.Begin);
            }

            void applyShortName(ElementId id, const std::string &shortName, std::size_t line) {
                if (shortName.empty())
                    return;
                try {
                    Model.setShortName(id, shortName);
                } catch (const std::exception &exception) {
                    lost(std::string(exception.what()), line);
                }
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
            /// Type as written (quotes kept, separators without spaces), "~" in front of a conjugated port type.
            std::string readTypeText() {
                std::string text;
                if (isSymbol("~") && isNameToken(1))
                    text = take().Text;
                if (!isNameToken())
                    return {};
                const auto spelled = [this](const Token &token) { return Source.substr(token.Begin, token.End - token.Begin); };
                text += spelled(take());
                while ((isSymbol("::") || isSymbol(".")) && isNameToken(1)) {
                    text += take().Text;
                    text += spelled(take());
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
                    if (isSuccessionEnd(kind))
                        Previous[owner] = id; // source of a following "then b;"
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
                    if (readPrefix(prefixes))
                        continue;
                    if ((inEvaluationBody(owner) || inAnalysisBody(owner)) && isWord("return") && prefixes.empty() &&
                        (isNameToken(1) || isSymbol("<", 1) || isSymbol(":>>", 1)) && !isSymbol(":", 1)) {
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
                if ((peek().Type == TokenType::Name || isSymbol("<")) && directed && acceptsBareParameters(owner)) {
                    // in 'my value' : Real;  return <R1> result;
                    parseUsage(owner, ElementKind::Attribute, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string());
                    return;
                }
                // "end supplier : FuelPort;" in an interface / allocation def: an end without keyword
                const bool end = std::find(prefixes.begin(), prefixes.end(), "end") != prefixes.end();
                // [<short name>] name followed by what may follow the name of a declaration
                const std::size_t named = isSymbol("<") && isNameToken(1) && isSymbol(">", 2) ? 3 : 0;
                const bool bareDeclaration =
                        isNameToken(named) && !isDeclarationKeyword(named) &&
                        (isSymbol(":", named + 1) || isSymbol(";", named + 1) || isSymbol("[", named + 1) ||
                         isSymbol("=", named + 1) || isSymbol(":=", named + 1) || isSymbol("{", named + 1) ||
                         isSymbol(":>", named + 1) || isWord("default", named + 1) || isSymbol(":>>", named + 1) ||
                         isWord("redefines", named + 1));
                if (std::find(prefixes.begin(), prefixes.end(), "variant") != prefixes.end() && bareDeclaration &&
                    variantKind(owner)) {
                    // "variant '4cylEngine';" in a variation: a variant of the kind of the variation
                    parseUsage(owner, *variantKind(owner), prefixes, begin, line, PerformStyle::DeclareAction, std::string());
                    return;
                }
                if (ownerKind(owner) == ElementKind::EnumerationDefinition && isNameToken(named) && isSymbol(";", named + 1) &&
                    !isWord("enum", named)) {
                    // "ok;" / "<'1'> ok;" in an enum def: a value without keyword
                    parseUsage(owner, ElementKind::EnumerationLiteral, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string());
                    return;
                }
                if (end && bareDeclaration &&
                    (ownerKind(owner) == ElementKind::InterfaceDefinition ||
                     ownerKind(owner) == ElementKind::AllocationDefinition)) {
                    parseUsage(owner, ownerKind(owner) == ElementKind::InterfaceDefinition ? ElementKind::PortUsage
                                                                                          : ElementKind::PartUsage,
                               prefixes, begin, line, PerformStyle::DeclareAction, std::string());
                    return;
                }
                const std::size_t redefinitionAt = isSymbol("<") && isNameToken(1) && isSymbol(">", 2) ? 3 : 0;
                if (Dialect == SysMLTextImporter::Dialect::SysML &&
                    (isSymbol(":>>", redefinitionAt) || isWord("redefines", redefinitionAt))) {
                    // ":>> mass = 2000.0;", "ref :>> payload : Fuel;": a feature redefined without keyword
                    parseUsage(owner, ElementKind::Attribute, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string());
                    return;
                }
                if (Dialect == SysMLTextImporter::Dialect::SysML && isSymbol("@") && isNameToken(1)) {
                    take(); // "@Safety { isMandatory = true; }"
                    parseMetadata(owner, prefixes, begin, line, true);
                    return;
                }
                if ((ownerKind(owner) == ElementKind::MetadataUsage || ownerKind(owner) == ElementKind::MetadataDefinition) &&
                    bareDeclaration && owner != RootId) {
                    // "toolName = "ModelCenter";": the value of an attribute of the metadata
                    parseUsage(owner, ElementKind::Attribute, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string());
                    return;
                }
                if (peek().Type != TokenType::Identifier) {
                    fallBackToText(owner, begin, line);
                    return;
                }

                const std::string keyword = peek().Text;
                if (parseNamespaceMember(owner, keyword, prefixes, begin, line))
                    return; // import, alias, expose, render, filter, metadata, view
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
                } else if (acceptsBareParameters(owner) && directed && isNameToken() && !isDeclarationKeyword() &&
                           (isSymbol(":", 1) || isSymbol(";", 1) || isSymbol("[", 1) || isSymbol("=", 1) ||
                            isSymbol(":=", 1) || isWord("default", 1) || isSymbol(":>", 1) || isSymbol(":>>", 1) ||
                            isWord("redefines", 1) || isSymbol("{", 1))) {
                    // in x : Real;   (parameter without keyword)
                    parseUsage(owner, ElementKind::Attribute, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string());
                } else if (!kerml && parseOccurrence(owner, keyword, prefixes, begin, line)) {
                    // occurrences, events, messages, allocations, dependencies
                } else if (!kerml && parseRequirementOrCase(owner, keyword, prefixes, begin, line)) {
                    // requirements, use cases, verifications, subject, actors, satisfy, verify, include
                } else if (!kerml && parseBehavior(owner, keyword, prefixes, begin, line)) {
                    // actions, control nodes, states, successions and transitions
                } else if (Dialect == SysMLTextImporter::Dialect::SysML) {
                    if ((keyword == "connection" || keyword == "flow") && isWord("def", 1)) {
                        take();
                        take();
                        parseDefinition(owner, prefixes, begin, line,
                                        keyword == "connection" ? ElementKind::ConnectionDefinition : ElementKind::FlowDefinition);
                    } else if (keyword == "part" && isWord("def", 1)) {
                        take();
                        take();
                        parseDefinition(owner, prefixes, begin, line);
                    } else if (keyword == "part") {
                        take();
                        parseUsage(owner, ElementKind::PartUsage, prefixes, begin, line);
                    } else if (keyword == "attribute" && !isWord("def", 1)) {
                        take();
                        parseUsage(owner, ElementKind::Attribute, prefixes, begin, line);
                    } else if (isWord("def", 1) && (keyword == "attribute" || keyword == "enum" || keyword == "item" ||
                                                    keyword == "port" || keyword == "interface")) {
                        take();
                        take();
                        const ElementKind kind = keyword == "attribute" ? ElementKind::AttributeDefinition
                                                 : keyword == "enum" ? ElementKind::EnumerationDefinition
                                                 : keyword == "item" ? ElementKind::ItemDefinition
                                                 : keyword == "port" ? ElementKind::PortDefinition
                                                 : ElementKind::InterfaceDefinition;
                        parseDefinition(owner, prefixes, begin, line, kind);
                    } else if (keyword == "item") {
                        take();
                        parseUsage(owner, ElementKind::ItemUsage, prefixes, begin, line);
                    } else if (keyword == "port") {
                        take();
                        parseUsage(owner, ElementKind::PortUsage, prefixes, begin, line);
                    } else if (keyword == "enum" && ownerKind(owner) == ElementKind::EnumerationDefinition) {
                        take();
                        parseUsage(owner, ElementKind::EnumerationLiteral, prefixes, begin, line);
                    } else if ((keyword == "connection" || keyword == "interface") && prefixes.empty()) {
                        take();
                        parseConnection(owner, begin, line, keyword == "interface");
                    } else if (keyword == "perform") {
                        take();
                        parsePerform(owner, prefixes, begin, line);
                    } else if (keyword == "connect" && prefixes.empty()) {
                        take();
                        parseConnect(owner, begin, line);
                    } else if (keyword == "bind" && prefixes.empty()) {
                        take();
                        parseBind(owner, begin, line);
                    } else if (keyword == "flow" && prefixes.empty()) {
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

            /// import, alias, expose, render, filter; metadata (def), view (def); false if the statement is none of them.
            bool parseNamespaceMember(ElementId owner, const std::string &keyword, const std::vector<std::string> &prefixes,
                                      std::size_t begin, std::size_t line) {
                const bool sysml = Dialect == SysMLTextImporter::Dialect::SysML;
                if (keyword == "import" || (sysml && (keyword == "expose" || keyword == "render" || keyword == "filter"))) {
                    take();
                    std::optional<std::string> declared;
                    if (keyword == "import" && isWord("all")) { // "import all Lib::*;"
                        take();
                        declared = std::string("import all");
                    }
                    const std::string target = spanUntil({});
                    const ElementKind kind = keyword == "import" ? ElementKind::Import
                                             : keyword == "expose" ? ElementKind::Expose
                                             : keyword == "render" ? ElementKind::Render : ElementKind::Filter;
                    addTargetElement(owner, kind, std::string(), std::string(), target, declared, prefixes, begin, line);
                    return true;
                }
                if (keyword == "alias") {
                    take();
                    const std::string shortName = readShortName();
                    const std::string name = readName();
                    if (name.empty() || !isWord("for")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    const std::string target = spanUntil({});
                    addTargetElement(owner, ElementKind::Alias, name, shortName, target, std::nullopt, prefixes, begin, line);
                    return true;
                }
                if (!sysml)
                    return false;
                if ((keyword == "metadata" || keyword == "view") && isWord("def", 1)) {
                    take();
                    take();
                    parseDefinition(owner, prefixes, begin, line,
                                    keyword == "metadata" ? ElementKind::MetadataDefinition : ElementKind::ViewDefinition);
                    return true;
                }
                if (keyword == "metadata") {
                    take();
                    parseMetadata(owner, prefixes, begin, line, false);
                    return true;
                }
                if (keyword == "view") {
                    take();
                    parseUsage(owner, ElementKind::ViewUsage, prefixes, begin, line);
                    return true;
                }
                return false;
            }

            /// After "import", "alias name for", ...: the target up to ';' (or a body for an alias).
            void addTargetElement(ElementId owner, ElementKind kind, const std::string &name, const std::string &shortName,
                                  const std::string &target, const std::optional<std::string> &declared,
                                  const std::vector<std::string> &prefixes, std::size_t begin, std::size_t line) {
                if (target.empty() || !(isSymbol(";") || (kind == ElementKind::Alias && isSymbol("{")))) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                const auto id = add(kind, name, owner);
                if (!id) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                try {
                    Model.setTarget(*id, target);
                    applyPrefixes(*id, prefixes);
                    if (declared)
                        Model.setDeclaredKeyword(*id, declared);
                } catch (const std::exception &exception) {
                    lost(std::string(exception.what()), line);
                }
                applyShortName(*id, shortName, line);
                finishDeclaration(*id);
            }

            /// After "metadata" / "@": [name :] Type [about a, b] (';' or a body with the values of its attributes).
            void parseMetadata(ElementId owner, const std::vector<std::string> &prefixes, std::size_t begin,
                               std::size_t line, bool at) {
                const std::string shortName = readShortName();
                std::string name;
                if (isNameToken() && isSymbol(":", 1) && !isSymbol(":", 2)) {
                    name = readName();
                    take(); // ':'
                } else if (!shortName.empty()) {
                    if (!isSymbol(":")) { // "metadata <m1> : Safety;"
                        fallBackToText(owner, begin, line);
                        return;
                    }
                    take();
                }
                const std::string typeName = readTypeText();
                std::vector<std::string> about;
                if (isWord("about")) {
                    take();
                    for (auto &reference: readReferenceList())
                        about.push_back(std::move(reference.Text));
                    if (about.empty()) {
                        fallBackToText(owner, begin, line);
                        return;
                    }
                }
                if (typeName.empty() || (!isSymbol(";") && !isSymbol("{"))) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                const auto id = add(ElementKind::MetadataUsage, name, owner);
                if (!id) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                try {
                    Model.setTypeName(*id, typeName);
                    Model.setAbout(*id, about);
                    applyPrefixes(*id, prefixes);
                    if (at && name.empty())
                        Model.setDeclaredKeyword(*id, std::string("@"));
                } catch (const std::exception &exception) {
                    lost(std::string(exception.what()), line);
                }
                applyShortName(*id, shortName, line);
                finishDeclaration(*id);
            }

            /// One prefix word or metadata reference (#name) in front of a declaration keyword; false if there is none.
            bool readPrefix(std::vector<std::string> &prefixes) {
                if (peek().Type == TokenType::Identifier && PrefixWords.count(peek().Text) > 0 &&
                    !isSymbol(";", 1) && !isSymbol(":", 1) && !isSymbol("{", 1)) {
                    prefixes.push_back(take().Text);
                    if (prefixes.back() == "end") {
                        // cross multiplicity of a connection end: "end [1] part bead : TireBead;"
                        const std::size_t length = crossMultiplicityLength();
                        std::string multiplicity;
                        for (std::size_t i = 0; i < length; ++i)
                            multiplicity += take().Text;
                        if (!multiplicity.empty())
                            prefixes.push_back(multiplicity);
                    }
                    return true;
                }
                if (isSymbol("#") && isNameToken(1)) {
                    take();
                    prefixes.push_back("#" + readReferenceText());
                    return true;
                }
                return false;
            }

            /// "[1]", "[0..*]" (numbers and '*' only) at the token ahead: its number of tokens, 0 if none.
            std::size_t crossMultiplicityLength(std::size_t ahead = 0) {
                if (!isSymbol("[", ahead))
                    return 0;
                std::size_t length = 1;
                while (peek(ahead + length).Type == TokenType::Number || isSymbol("*", ahead + length) ||
                       isSymbol("..", ahead + length))
                    ++length;
                return isSymbol("]", ahead + length) && length > 1 ? length + 1 : 0;
            }

            /// Index (as for peek) of the first token behind the prefixes that start at ahead.
            std::size_t behindPrefixes(std::size_t ahead) {
                while (true) {
                    if (peek(ahead).Type == TokenType::Identifier && PrefixWords.count(peek(ahead).Text) > 0 &&
                        !isSymbol(";", ahead + 1) && !isSymbol(":", ahead + 1) && !isSymbol("{", ahead + 1)) {
                        const bool end = peek(ahead).Text == "end";
                        ++ahead;
                        if (end)
                            ahead += crossMultiplicityLength(ahead);
                        continue;
                    }
                    if (isSymbol("#", ahead) && isNameToken(ahead + 1)) {
                        ahead += 2;
                        while ((isSymbol("::", ahead) || isSymbol(".", ahead)) && isNameToken(ahead + 1))
                            ahead += 2;
                        continue;
                    }
                    return ahead;
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
                const std::string shortName = readShortName();
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
                applyShortName(*id, shortName, line);
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

            ElementKind ownerKind(ElementId owner) const {
                return owner == RootId ? ElementKind::Package : Model.element(owner).kind();
            }

            void parseDefinition(ElementId owner, const std::vector<std::string> &prefixes, std::size_t begin,
                                 std::size_t line, ElementKind kind = ElementKind::PartDefinition,
                                 const std::optional<std::string> &declaredKeyword = std::nullopt) {
                const std::string shortName = Dialect == SysMLTextImporter::Dialect::SysML ? readShortName() : std::string();
                const std::string name = readName();
                std::vector<Reference> specializations;
                while (!name.empty() && (isSymbol(":>") || isWord("specializes") || isWord("subclassifies"))) {
                    take();
                    for (auto &reference: readReferenceList())
                        specializations.push_back(std::move(reference));
                }
                bool parallel = false;
                if (kind == ElementKind::StateDefinition && isWord("parallel")) { // "state def S parallel { ... }"
                    take();
                    parallel = true;
                }
                // Anything else in the declaration (e.g. a KerML keyword the diagram does not know): keep as text.
                const bool kerml = Dialect == SysMLTextImporter::Dialect::KerML;
                if (name.empty() || (!isSymbol(";") && !isSymbol("{"))) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                const auto id = add(kind, name, owner);
                if (!id) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                if (!kerml)
                    applyPrefixes(*id, prefixes);
                if (parallel)
                    Model.setParallel(*id, true);
                if (declaredKeyword)
                    Model.setDeclaredKeyword(*id, declaredKeyword); // "concern def", "viewpoint def"
                applyShortName(*id, shortName, line);
                for (auto &reference: specializations)
                    Pendings.push_back({Pending::Type::Specialization, *id, owner, std::move(reference.Path), {}, line,
                                        reference.Text, 0, 0, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
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

            /// The action form whose keyword is the token at ahead (send, accept, ..., for), std::nullopt if none.
            std::optional<ActionForm> actionFormAt(std::size_t ahead = 0) {
                const Token &token = peek(ahead);
                if (token.Type != TokenType::Identifier)
                    return std::nullopt;
                for (const ActionForm form: {ActionForm::Send, ActionForm::Accept, ActionForm::Assign, ActionForm::Terminate,
                                             ActionForm::If, ActionForm::While, ActionForm::Loop, ActionForm::For}) {
                    if (token.Text == toKeyword(form))
                        return form;
                }
                return std::nullopt;
            }

            /// Index (as for peek) of the first token at bracket depth 0 that is ';', '{', '}' or one of the words.
            std::size_t findAtDepthZero(std::initializer_list<std::string_view> words) {
                int depth = 0;
                for (std::size_t ahead = 0;; ++ahead) {
                    const Token &token = peek(ahead);
                    if (token.Type == TokenType::End)
                        return ahead;
                    if (depth == 0) {
                        if (token.Type == TokenType::Symbol && (token.Text == ";" || token.Text == "{" || token.Text == "}"))
                            return ahead;
                        for (const auto word: words) {
                            if (token.Type == TokenType::Identifier && token.Text == word)
                                return ahead;
                        }
                    }
                    if (token.Type == TokenType::Symbol && (token.Text == "(" || token.Text == "["))
                        ++depth;
                    else if (token.Type == TokenType::Symbol && (token.Text == ")" || token.Text == "]") && depth > 0)
                        --depth;
                }
            }

            void parseUsage(ElementId owner, ElementKind kind, const std::vector<std::string> &prefixes, std::size_t begin,
                            std::size_t line, PerformStyle style = PerformStyle::DeclareAction,
                            std::optional<std::string> declaredKeyword = std::nullopt) {
                std::string shortName = Dialect == SysMLTextImporter::Dialect::SysML ? readShortName() : std::string();
                std::string name;
                std::string redefinitionKeyword;
                std::vector<std::string> redefinitions;
                bool nameFromRedefinition = false;
                bool plain = true; // false: something the model cannot hold -> the statement is kept as text
                const auto readRedefinitions = [&] {
                    redefinitionKeyword = take().Text;
                    for (auto &reference: readReferenceList()) {
                        if (redefinitions.empty() && nameFromRedefinition)
                            name = reference.Path.back();
                        redefinitions.push_back(std::move(reference.Text));
                    }
                    plain = !redefinitions.empty();
                };
                const std::size_t redefinitionAt = isSymbol("<") && isNameToken(1) && isSymbol(">", 2) ? 3 : 0;
                if (Dialect == SysMLTextImporter::Dialect::SysML &&
                    (isSymbol(":>>", redefinitionAt) || isWord("redefines", redefinitionAt))) {
                    // "part redefines cyl[4];", ":>> mass = 2000.0;": named like the feature it redefines
                    nameFromRedefinition = true;
                    readRedefinitions();
                } else if (!(kind == ElementKind::ActionUsage && actionFormAt())) {
                    name = readName();
                }
                // "objective { ... }", "requirement : R;", "if c { ... }", "send s to t;" without a name
                if (name.empty() && !(kind == ElementKind::RequirementUsage && shortName.empty()) &&
                    kind != ElementKind::ActionUsage) {
                    fallBackToText(owner, begin, line);
                    return;
                }

                std::string typeName;
                std::optional<Multiplicity> multiplicity;
                std::vector<Reference> specializations;
                std::string valueOperator;
                std::string value;
                std::string qualifiers; // ordered, nonunique
                bool parallel = false;
                ActionForm form = ActionForm::Plain;
                std::string clause;
                bool namedInBody = false;

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
                    } else if ((isSymbol(":>>") || isWord("redefines")) && redefinitions.empty() &&
                               Dialect == SysMLTextImporter::Dialect::SysML) {
                        readRedefinitions(); // "part smallEng : SmallEngine redefines eng;"
                    } else if ((isWord("ordered") || isWord("nonunique")) && qualifiers.empty()) {
                        qualifiers = take().Text;
                        if (qualifiers == "ordered" && isWord("nonunique"))
                            qualifiers += " " + take().Text;
                        plain = !isWord("ordered") && !isWord("nonunique"); // "nonunique ordered": kept as written
                    } else if ((isSymbol("=") || isSymbol(":=") || isWord("default")) && value.empty()) {
                        valueOperator = take().Text;
                        if (valueOperator == "default" && (isSymbol("=") || isSymbol(":=")))
                            valueOperator += " " + take().Text;
                        const std::size_t from = peek().Begin;
                        skipExpression();
                        value = LastEnd > from ? expressionText(from, LastEnd) : std::string();
                        plain = !value.empty();
                    } else if (isWord("parallel") && !parallel &&
                               (kind == ElementKind::StateUsage || kind == ElementKind::StateDefinition)) {
                        take();
                        parallel = true;
                        plain = isSymbol(";") || isSymbol("{");
                    } else if (kind == ElementKind::ActionUsage && form == ActionForm::Plain && actionFormAt()) {
                        // "action trigger accept scene : Scene;", "send s to t;", "if c { ... }", "loop action l { ... }"
                        form = *actionFormAt();
                        take();
                        if (form == ActionForm::If || form == ActionForm::While || form == ActionForm::For)
                            clause = spanUntil({"action"});
                        else if (form != ActionForm::Loop)
                            clause = spanUntil({});
                        if (clause.empty() && form != ActionForm::Loop && form != ActionForm::Terminate)
                            plain = false;
                        if (isControlStructure(form)) {
                            if (isWord("action")) { // the body named: "loop action charging { ... }"
                                take();
                                if (!name.empty() || !shortName.empty() || !typeName.empty() || nameFromRedefinition) {
                                    plain = false;
                                } else {
                                    shortName = readShortName();
                                    name = readName();
                                    namedInBody = true;
                                    plain = !name.empty();
                                }
                            }
                            plain = plain && isSymbol("{");
                        }
                        break;
                    } else {
                        plain = false;
                    }
                }

                const bool hasBody = isSymbol("{");
                // a body: what the element owns, at least comments and metadata ("in dt : Time { @ToolVariable; }")
                const bool bodyAllowed = kind != ElementKind::ControlNode && kind != ElementKind::EnumerationLiteral;
                const bool multiplicityAllowed = kind == ElementKind::PartUsage || kind == ElementKind::Attribute ||
                                                 kind == ElementKind::ItemUsage || kind == ElementKind::PortUsage ||
                                                 kind == ElementKind::Subject || kind == ElementKind::Actor ||
                                                 kind == ElementKind::OccurrenceUsage;
                const bool valueAllowed = kind == ElementKind::Attribute || kind == ElementKind::PartUsage ||
                                          kind == ElementKind::Subject || kind == ElementKind::OccurrenceUsage;
                const bool redefinitionAllowed = kind != ElementKind::EnumerationLiteral && kind != ElementKind::ControlNode;
                if (!plain || (!qualifiers.empty() && !multiplicityAllowed) ||
                    (!bodyAllowed && hasBody) || (multiplicity && !multiplicityAllowed) || (!value.empty() && !valueAllowed) ||
                    ((kind == ElementKind::ActionUsage || kind == ElementKind::StateUsage) && !value.empty()) ||
                    (kind == ElementKind::EnumerationLiteral &&
                     (!value.empty() || !typeName.empty() || multiplicity || !specializations.empty())) ||
                    (kind == ElementKind::ActionPerformance && (multiplicity || !value.empty())) ||
                    (!redefinitions.empty() && !redefinitionAllowed) ||
                    (form != ActionForm::Plain && (multiplicity || !redefinitions.empty())) ||
                    (Dialect == SysMLTextImporter::Dialect::KerML && !prefixes.empty() && !inEvaluationBody(owner))) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                if (form != ActionForm::Plain && declaredKeyword && declaredKeyword->size() > 7 &&
                    declaredKeyword->compare(declaredKeyword->size() - 7, 7, " action") == 0)
                    declaredKeyword = declaredKeyword->substr(0, declaredKeyword->size() - 7); // "entry action a assign ..."

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
                    if (!qualifiers.empty())
                        Model.setMultiplicityQualifiers(*id, qualifiers);
                    if (kind == ElementKind::ActionPerformance)
                        Model.setPerformStyle(*id, style);
                    if (!value.empty())
                        Model.setValue(*id, valueOperator, value);
                    applyPrefixes(*id, prefixes);
                    if (declaredKeyword.has_value())
                        Model.setDeclaredKeyword(*id, declaredKeyword);
                    if (!redefinitions.empty())
                        Model.setRedefinitions(*id, redefinitionKeyword, redefinitions, nameFromRedefinition);
                    if (parallel)
                        Model.setParallel(*id, true);
                    if (form != ActionForm::Plain)
                        Model.setActionForm(*id, form, clause);
                    if (namedInBody)
                        Model.setNamedInBody(*id, true);
                } catch (const std::exception &exception) {
                    lost(std::string(exception.what()) + " (" + name + ")", line);
                }
                applyShortName(*id, shortName, line);
                for (auto &reference: specializations)
                    Pendings.push_back({Pending::Type::Specialization, *id, owner, std::move(reference.Path), {}, line,
                                        reference.Text, 0, 0, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
                if (isControlStructure(form))
                    finishControlStructure(*id, form, line);
                else
                    finishDeclaration(*id);
            }

            /// After "if c", "while c", "loop", "for v in e" (and the name of the body): the body, then
            /// "until c;" (loop, while) or "else { ... }" / "else if ..." (if).
            void finishControlStructure(ElementId id, ActionForm form, std::size_t line) {
                take(); // '{'
                parseBody(id, true);
                if ((form == ActionForm::Loop || form == ActionForm::While) && isWord("until")) {
                    take();
                    const std::string condition = spanUntil({});
                    if (condition.empty() || !isSymbol(";")) {
                        lost("until condition", line);
                        skipRestOfStatement();
                        return;
                    }
                    take();
                    try {
                        Model.setUntilCondition(id, condition);
                    } catch (const std::exception &exception) {
                        lost(std::string(exception.what()), line);
                    }
                    return;
                }
                if (form != ActionForm::If || !isWord("else") ||
                    !(isSymbol("{", 1) || isWord("if", 1) || isWord("action", 1)))
                    return;
                const std::size_t elseLine = peek().Line;
                const std::size_t elseBegin = peek(1).Begin;
                take(); // else
                std::optional<ElementId> branch;
                if (isWord("if")) {
                    const std::size_t before = Model.children(id).size();
                    parseUsage(id, ElementKind::ActionUsage, {}, elseBegin, elseLine);
                    const auto &children = Model.children(id);
                    if (children.size() > before) {
                        const ElementId added = children.back();
                        if (Model.element(added).kind() == ElementKind::ActionUsage)
                            branch = added;
                        else
                            Model.removeElement(added); // not an action: below
                    }
                } else {
                    std::string shortName;
                    std::string name;
                    const bool named = isWord("action");
                    if (named) {
                        take();
                        shortName = readShortName();
                        name = readName();
                    }
                    if (!isSymbol("{") || (named && name.empty() && shortName.empty())) {
                        lost("else branch", elseLine);
                        skipRestOfStatement();
                        return;
                    }
                    branch = add(ElementKind::ActionUsage, name, id);
                    if (!branch) {
                        lost("else branch", elseLine);
                        skipRestOfStatement();
                        return;
                    }
                    applyShortName(*branch, shortName, elseLine);
                    try {
                        Model.setElseBranch(*branch, true);
                        if (named)
                            Model.setNamedInBody(*branch, true);
                    } catch (const std::exception &exception) {
                        lost(std::string(exception.what()), elseLine);
                    }
                    take(); // '{'
                    parseBody(*branch, true);
                }
                if (!branch) {
                    lost("else branch", elseLine);
                    return;
                }
                try {
                    Model.setElseBranch(*branch, true);
                } catch (const std::exception &exception) {
                    lost(std::string(exception.what()), elseLine);
                }
            }

            bool inEvaluationBody(ElementId owner) const {
                return owner != RootId && isConstraintOrCalc(Model.element(owner).kind());
            }

            /// constraint def / calc def / function / predicate / [assert] constraint / calc, after the keywords.
            void parseEvaluation(ElementId owner, ElementKind kind, const std::optional<std::string> &declaredKeyword,
                                 const std::vector<std::string> &prefixes, std::size_t begin, std::size_t line) {
                const bool definition = isDefinitionKind(kind);
                const std::string shortName = Dialect == SysMLTextImporter::Dialect::SysML ? readShortName() : std::string();
                std::string name;
                std::string redefinitionKeyword;
                std::vector<std::string> redefinitions;
                bool nameFromRedefinition = false;
                const auto readRedefinitions = [&] {
                    redefinitionKeyword = take().Text;
                    for (auto &reference: readReferenceList()) {
                        if (redefinitions.empty() && nameFromRedefinition)
                            name = reference.Path.back();
                        redefinitions.push_back(std::move(reference.Text));
                    }
                    return !redefinitions.empty();
                };
                const bool sysml = Dialect == SysMLTextImporter::Dialect::SysML;
                if (!definition && sysml && (isSymbol(":>>") || isWord("redefines"))) {
                    nameFromRedefinition = true; // "calc :>> total { ... }"
                    readRedefinitions();
                } else {
                    name = readName();
                }
                std::string typeName;
                std::optional<Multiplicity> multiplicity;
                std::vector<Reference> specializations;
                // "require constraint { ... }" / "assume constraint { ... }" are usually unnamed
                const bool unnamedAllowed = declaredKeyword == "require constraint" || declaredKeyword == "assume constraint" ||
                                            declaredKeyword == "assert constraint";
                bool plain = !name.empty() || (unnamedAllowed && shortName.empty());
                while (plain && !atEnd() && !isSymbol(";") && !isSymbol("{") && !isSymbol("}")) {
                    if (!definition && isSymbol(":") && typeName.empty()) {
                        take();
                        typeName = readTypeText();
                        plain = !typeName.empty() && !isSymbol(",");
                    } else if (!definition && isSymbol("[") && !multiplicity) {
                        bool valid = false;
                        multiplicity = readMultiplicity(valid);
                        plain = valid;
                    } else if (!definition && sysml && redefinitions.empty() && (isSymbol(":>>") || isWord("redefines"))) {
                        plain = readRedefinitions();
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
                    if (!redefinitions.empty())
                        Model.setRedefinitions(*id, redefinitionKeyword, redefinitions, nameFromRedefinition);
                } catch (const std::exception &exception) {
                    lost(std::string(exception.what()) + " (" + name + ")", line);
                }
                applyShortName(*id, shortName, line);
                for (auto &reference: specializations)
                    Pendings.push_back({Pending::Type::Specialization, *id, owner, std::move(reference.Path), {}, line,
                                        reference.Text, 0, 0, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
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
                                    std::string(), begin, LastEnd, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
                reserve();
            }

            bool acceptsBareParameters(ElementId owner) const {
                if (owner == RootId)
                    return false;
                const ElementKind kind = Model.element(owner).kind();
                return isConstraintOrCalc(kind) || kind == ElementKind::ActionDefinition || kind == ElementKind::ActionUsage ||
                       kind == ElementKind::ActionPerformance || kind == ElementKind::StateDefinition ||
                       kind == ElementKind::StateUsage || isAnalysisKind(kind);
            }

            bool inAnalysisBody(ElementId owner) const {
                return owner != RootId && isAnalysisKind(Model.element(owner).kind());
            }

            /// Snapshots and time slices of parts and items are ends of successions as well.
            bool isSuccessionEndElement(ElementId id) const {
                const auto &element = Model.element(id);
                return isSuccessionEnd(element.kind()) ||
                       ((element.kind() == ElementKind::PartUsage || element.kind() == ElementKind::ItemUsage) &&
                        isTemporalKeyword(element.declaredKeyword()));
            }

            /// What "variant name;" declares inside a variation of the given kind (std::nullopt: kept as text).
            std::optional<ElementKind> variantKind(ElementId owner) const {
                if (owner == RootId)
                    return std::nullopt;
                const auto &element = Model.element(owner);
                if (std::find(element.prefixes().begin(), element.prefixes().end(), "variation") == element.prefixes().end())
                    return std::nullopt;
                switch (element.kind()) {
                    case ElementKind::PartDefinition:
                    case ElementKind::PartUsage:
                        return ElementKind::PartUsage;
                    case ElementKind::AttributeDefinition:
                        return ElementKind::Attribute;
                    case ElementKind::ItemDefinition:
                    case ElementKind::ItemUsage:
                        return ElementKind::ItemUsage;
                    case ElementKind::ActionDefinition:
                    case ElementKind::ActionUsage:
                        return ElementKind::ActionUsage;
                    case ElementKind::OccurrenceDefinition:
                    case ElementKind::OccurrenceUsage:
                        return ElementKind::OccurrenceUsage;
                    case ElementKind::PortDefinition:
                    case ElementKind::PortUsage:
                        return ElementKind::PortUsage;
                    default:
                        return std::nullopt;
                }
            }

            /// The implicit start / done / entry of an action or state (created on first use).
            std::optional<ElementId> implicitControl(ElementId owner, const std::string &keyword) {
                for (const ElementId child: Model.children(owner)) {
                    const auto &element = Model.element(child);
                    if (element.kind() == ElementKind::ControlNode && element.declaredKeyword() == keyword)
                        return child;
                }
                try {
                    const ElementId id = Model.addElement(ElementKind::ControlNode, keyword, owner);
                    Model.setDeclaredKeyword(id, keyword);
                    return id;
                } catch (const std::exception &) {
                    return std::nullopt;
                }
            }

            /// Source text up to one of the stop words (or ';' / '{' / '}') at bracket depth 0.
            std::string spanUntil(std::initializer_list<std::string_view> stopWords) {
                const std::size_t from = peek().Begin;
                int depth = 0;
                while (!atEnd()) {
                    const Token &token = peek();
                    if (depth == 0) {
                        if (token.Type == TokenType::Symbol && (token.Text == ";" || token.Text == "{" || token.Text == "}"))
                            break;
                        bool stop = false;
                        for (const auto word: stopWords)
                            stop = stop || (token.Type == TokenType::Identifier && token.Text == word);
                        if (stop)
                            break;
                    }
                    if (token.Type == TokenType::Symbol && (token.Text == "(" || token.Text == "["))
                        ++depth;
                    else if (token.Type == TokenType::Symbol && (token.Text == ")" || token.Text == "]") && depth > 0)
                        --depth;
                    take();
                }
                return LastEnd > from ? expressionText(from, LastEnd) : std::string();
            }

            void addSuccession(Pending::Type type, ElementId owner, std::optional<ElementId> source,
                               std::vector<std::string> first, std::vector<std::string> target, std::size_t line,
                               std::size_t begin, const std::string &name, const std::string &guard,
                               const std::string &trigger, const std::string &effect) {
                Pendings.push_back({type, source.value_or(RootId), owner, std::move(first), std::move(target), line,
                                    std::string(), begin, LastEnd, name, std::string(), guard, trigger, effect, std::nullopt, std::nullopt, {}});
                reserve(); // "then b;" too: kept as written if b is not found
            }

            /// Statements of actions and states; false if the statement is none of them.
            bool parseBehavior(ElementId owner, const std::string &keyword, const std::vector<std::string> &prefixes,
                               std::size_t begin, std::size_t line) {
                const auto previous = [&]() -> std::optional<ElementId> {
                    const auto it = Previous.find(owner);
                    return it == Previous.end() ? std::nullopt : std::optional<ElementId>(it->second);
                };
                const bool statePart = ownerKind(owner) == ElementKind::StateDefinition ||
                                       ownerKind(owner) == ElementKind::StateUsage;
                if ((keyword == "action" || keyword == "state") && isWord("def", 1)) {
                    take();
                    take();
                    parseDefinition(owner, prefixes, begin, line,
                                    keyword == "action" ? ElementKind::ActionDefinition : ElementKind::StateDefinition);
                    return true;
                }
                if (keyword == "action" || keyword == "state") {
                    take();
                    parseUsage(owner, keyword == "action" ? ElementKind::ActionUsage : ElementKind::StateUsage, prefixes,
                               begin, line);
                    return true;
                }
                if (keyword == "exhibit" && isWord("state", 1)) {
                    take();
                    take();
                    parseUsage(owner, ElementKind::StateUsage, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string("exhibit state"));
                    return true;
                }
                if ((keyword == "entry" || keyword == "do" || keyword == "exit") && isWord("action", 1)) {
                    take();
                    take();
                    parseUsage(owner, ElementKind::ActionUsage, prefixes, begin, line, PerformStyle::DeclareAction,
                               keyword + " action");
                    return true;
                }
                if ((keyword == "entry" || keyword == "do" || keyword == "exit") && actionFormAt(1)) {
                    // "entry assign t := t + 1;", "do send s to t;"
                    take();
                    parseUsage(owner, ElementKind::ActionUsage, prefixes, begin, line, PerformStyle::DeclareAction, keyword);
                    return true;
                }
                if ((keyword == "entry" || keyword == "do" || keyword == "exit") && statePart && isNameToken(1) &&
                    !isSymbol(";", 1)) {
                    // "do senseTemperature;": an action declared elsewhere, performed in the state
                    take();
                    parseUsage(owner, ElementKind::ActionPerformance, prefixes, begin, line, PerformStyle::ReferenceAction,
                               keyword);
                    return true;
                }
                if (const auto form = actionFormAt()) {
                    // "if c then b;" is a branch of a decision, "accept s then b;" after a state a transition
                    const bool branch = *form == ActionForm::If && previous() && isWord("then", findAtDepthZero({"then"}));
                    const bool transition = *form == ActionForm::Accept && previous() &&
                                            Model.element(*previous()).kind() == ElementKind::StateUsage &&
                                            isWord("then", findAtDepthZero({"then"}));
                    if (!branch && !transition) {
                        // "send s to t;", "assign x := 1;", "terminate;", "if c { ... }", "loop { ... } until c;"
                        parseUsage(owner, ElementKind::ActionUsage, prefixes, begin, line);
                        return true;
                    }
                }
                if (keyword == "decide" || keyword == "merge" || keyword == "fork" || keyword == "join") {
                    take();
                    const std::string shortName = readShortName();
                    const std::string name = isSymbol(";") ? std::string() : readName();
                    if (!isSymbol(";")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    const auto id = add(ElementKind::ControlNode, name, owner);
                    if (!id) {
                        keepText(owner, begin, LastEnd, line);
                        return true;
                    }
                    Model.setDeclaredKeyword(*id, keyword);
                    applyShortName(*id, shortName, line);
                    applyPrefixes(*id, prefixes);
                    return true;
                }
                if (!prefixes.empty())
                    return false;
                if (keyword == "entry" && isSymbol(";", 1) && statePart) {
                    take();
                    take();
                    if (const auto entry = implicitControl(owner, "entry"))
                        Previous[owner] = *entry;
                    return true;
                }
                if (keyword == "first" || keyword == "succession") {
                    take();
                    std::string name;
                    if (keyword == "succession") {
                        if (isNameToken() && !isWord("first"))
                            name = Source.substr(peek().Begin, peek().End - peek().Begin), take();
                        if (!isWord("first")) {
                            fallBackToText(owner, begin, line);
                            return true;
                        }
                        take();
                    }
                    auto source = readPath();
                    if (source.empty()) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    if (isSymbol(";") && keyword == "first") {
                        // "first start;" / "first a;": the source of the following "then" statements
                        take();
                        std::optional<ElementId> found;
                        if (source.size() == 1 && source.front() == "start") {
                            found = implicitControl(owner, "start");
                        } else if (source.size() == 1) {
                            for (const ElementId child: Model.children(owner))
                                if (Model.element(child).name() == source.front() && isSuccessionEndElement(child))
                                    found = child;
                        }
                        if (!found)
                            keepText(owner, begin, LastEnd, line);
                        else
                            Previous[owner] = *found;
                        return true;
                    }
                    std::string guard;
                    if (isWord("if")) {
                        take();
                        guard = spanUntil({"then"});
                    }
                    if (!isWord("then")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    auto target = readPath();
                    if (target.empty() || !isSymbol(";")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    if (name.empty() && keyword == "first") {
                        // written again as "then b;" after a: fine, same meaning
                    }
                    addSuccession(Pending::Type::Succession, owner, std::nullopt, std::move(source), std::move(target), line,
                                  begin, name, guard, {}, {});
                    return true;
                }
                if ((keyword == "then" || keyword == "if" || keyword == "else") && previous()) {
                    take();
                    std::string guard = keyword == "else" ? std::string("else") : std::string();
                    if (keyword == "if") {
                        guard = spanUntil({"then"});
                        if (guard.empty() || !isWord("then")) {
                            fallBackToText(owner, begin, line);
                            return true;
                        }
                        take();
                    }
                    auto target = readPath();
                    if (target.empty() || !isSymbol(";")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    addSuccession(Pending::Type::Succession, owner, previous(), {}, std::move(target), line, begin, {},
                                  guard, {}, {});
                    return true;
                }
                if (keyword == "transition" || (keyword == "accept" && previous() &&
                                                Model.element(*previous()).kind() == ElementKind::StateUsage)) {
                    const bool shorthand = keyword == "accept";
                    std::string name;
                    std::vector<std::string> source;
                    if (!shorthand) {
                        take();
                        if (isNameToken() && !isWord("first"))
                            name = Source.substr(peek().Begin, peek().End - peek().Begin), take();
                        if (!isWord("first")) {
                            fallBackToText(owner, begin, line);
                            return true;
                        }
                        take();
                        source = readPath();
                        if (source.empty()) {
                            fallBackToText(owner, begin, line);
                            return true;
                        }
                    }
                    std::string trigger;
                    std::string guard;
                    std::string effect;
                    if (isWord("accept")) {
                        take();
                        trigger = spanUntil({"if", "do", "then"});
                    }
                    if (isWord("if")) {
                        take();
                        guard = spanUntil({"do", "then"});
                    }
                    if (isWord("do")) {
                        take();
                        effect = spanUntil({"then"});
                    }
                    if (!isWord("then") || (shorthand && trigger.empty())) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    auto target = readPath();
                    if (target.empty() || !isSymbol(";")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    addSuccession(Pending::Type::Transition, owner, shorthand ? previous() : std::nullopt, std::move(source),
                                  std::move(target), line, begin, name, guard, trigger, effect);
                    return true;
                }
                return false;
            }

            /// Statements of interactions (occurrences, events, messages), allocations and dependencies;
            /// false if the statement is none of them.
            bool parseOccurrence(ElementId owner, const std::string &keyword, const std::vector<std::string> &prefixes,
                                 std::size_t begin, std::size_t line) {
                if (keyword == "occurrence" && isWord("def", 1)) {
                    take();
                    take();
                    parseDefinition(owner, prefixes, begin, line, ElementKind::OccurrenceDefinition);
                    return true;
                }
                if (keyword == "occurrence") {
                    take();
                    parseUsage(owner, ElementKind::OccurrenceUsage, prefixes, begin, line);
                    return true;
                }
                if (keyword == "event" && isWord("occurrence", 1)) {
                    take();
                    take();
                    parseUsage(owner, ElementKind::EventOccurrence, prefixes, begin, line);
                    return true;
                }
                if (keyword == "allocation" && isWord("def", 1)) {
                    take();
                    take();
                    parseDefinition(owner, prefixes, begin, line, ElementKind::AllocationDefinition);
                    return true;
                }
                if (keyword == "snapshot" || keyword == "timeslice") {
                    // "snapshot delivery;", "timeslice ownership[0..*] ordered { ... }", "snapshot part t0 : Vehicle_1 { ... }"
                    take();
                    ElementKind kind = ElementKind::OccurrenceUsage;
                    std::string declared = keyword;
                    if (isWord("part") || isWord("item")) {
                        kind = isWord("part") ? ElementKind::PartUsage : ElementKind::ItemUsage;
                        declared += " " + take().Text;
                    }
                    const std::size_t before = Model.children(owner).size();
                    parseUsage(owner, kind, prefixes, begin, line, PerformStyle::DeclareAction, declared);
                    const auto &children = Model.children(owner);
                    if (children.size() > before && Model.element(children.back()).kind() == kind)
                        Previous[owner] = children.back(); // source of a following "then ..."
                    return true;
                }
                if (!prefixes.empty())
                    return false;
                const auto previous = Previous.find(owner);
                const std::size_t declaration = behindPrefixes(1); // "then #m occurrence b;"
                const bool actionNode = (isWord("action", declaration) && !isWord("def", declaration + 1)) ||
                                        actionFormAt(declaration) || isWord("fork", declaration) ||
                                        isWord("join", declaration) || isWord("merge", declaration) ||
                                        isWord("decide", declaration);
                if (keyword == "then" && previous != Previous.end() &&
                    ((isWord("event", declaration) && isWord("occurrence", declaration + 1)) ||
                     isWord("occurrence", declaration) || isWord("snapshot", declaration) || isWord("timeslice", declaration) ||
                     actionNode)) {
                    // "then event occurrence b;", "then timeslice ownership { ... }": b, and a succession from the
                    // previous one (event, occurrence, action, ...) to it
                    const ElementId source = previous->second;
                    take();
                    std::vector<std::string> innerPrefixes;
                    while (readPrefix(innerPrefixes)) {
                    }
                    const std::size_t before = Model.children(owner).size();
                    const std::string inner = peek().Text;
                    if (actionNode)
                        parseBehavior(owner, inner, innerPrefixes, begin, line);
                    else
                        parseOccurrence(owner, inner, innerPrefixes, begin, line);
                    const auto &children = Model.children(owner);
                    if (children.size() > before && Model.element(children.back()).kind() != ElementKind::Textual &&
                        children.back() != source) {
                        Pendings.push_back({Pending::Type::Succession, source, owner, {}, {Model.element(children.back()).name()},
                                            line, std::string(), begin, LastEnd, {}, {}, {}, {}, {}, std::nullopt,
                                            children.back(), {}});
                        const auto &added = Model.element(children.back());
                        if (added.kind() == ElementKind::ActionUsage || added.kind() == ElementKind::ControlNode)
                            Model.setDeclaredWithThen(children.back(), true);
                    }
                    return true;
                }
                if (keyword == "message" || (keyword == "then" && isWord("message", 1))) {
                    const bool sequenced = keyword == "then";
                    take();
                    if (sequenced)
                        take();
                    parseMessage(owner, begin, line, sequenced);
                    return true;
                }
                if (keyword == "allocate" || keyword == "allocation") {
                    take();
                    parseAllocation(owner, begin, line, keyword == "allocation");
                    return true;
                }
                if (keyword == "dependency") {
                    take();
                    parseDependency(owner, begin, line);
                    return true;
                }
                return false;
            }

            /// message [name] [of Payload] from a to b;   (after "message")
            void parseMessage(ElementId owner, std::size_t begin, std::size_t line, bool sequenced) {
                std::string name;
                std::string typeName;
                if (isNameToken() && !isWord("of") && !isWord("from"))
                    name = Source.substr(peek().Begin, peek().End - peek().Begin), take();
                if (isWord("of")) {
                    take();
                    typeName = readTypeText();
                    if (typeName.empty()) {
                        fallBackToText(owner, begin, line);
                        return;
                    }
                }
                if (!isWord("from")) {
                    fallBackToText(owner, begin, line); // e.g. "message m of T;" (events given elsewhere)
                    return;
                }
                take();
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
                Pendings.push_back({sequenced ? Pending::Type::MessageThen : Pending::Type::Message, RootId, owner,
                                    std::move(source), std::move(target), line, std::string(), begin, LastEnd, name,
                                    typeName, {}, {}, {}, std::nullopt, std::nullopt, {}});
                reserve();
            }

            /// dependency [name] from a to b;   (after "dependency"; several clients / suppliers are kept as text)
            void parseDependency(ElementId owner, std::size_t begin, std::size_t line) {
                std::string name;
                if (isNameToken() && !isWord("from"))
                    name = Source.substr(peek().Begin, peek().End - peek().Begin), take();
                if (!isWord("from")) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                take();
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
                Pendings.push_back({Pending::Type::Dependency, RootId, owner, std::move(source), std::move(target), line,
                                    std::string(), begin, LastEnd, name, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
                reserve();
            }

            struct AllocationStatement {
                std::vector<std::string> Source;
                std::vector<std::string> Target;
                std::string Name;
                std::string TypeName;
                std::vector<AllocationStatement> Refined;
            };

            /// "a to b ;" or "a to b { allocate c to d; ... }" at the raw token index (after "allocate"); index is moved
            /// behind it. std::nullopt for anything else (comments included): the statement is then kept as text.
            std::optional<AllocationStatement> scanAllocate(std::size_t &index) const {
                const auto at = [&](std::size_t i) -> const Token & { return Tokens[std::min(i, Tokens.size() - 1)]; };
                const auto isName = [&](std::size_t i) {
                    return at(i).Type == TokenType::Identifier || at(i).Type == TokenType::Name;
                };
                const auto symbol = [&](std::size_t i, std::string_view text) {
                    return at(i).Type == TokenType::Symbol && at(i).Text == text;
                };
                const auto path = [&](std::vector<std::string> &out) {
                    if (!isName(index) || (at(index).Type == TokenType::Identifier && at(index).Text == "to"))
                        return false;
                    out.push_back(at(index++).Text);
                    while ((symbol(index, "::") || symbol(index, ".")) && isName(index + 1)) {
                        out.push_back(at(index + 1).Text);
                        index += 2;
                    }
                    return true;
                };
                AllocationStatement statement;
                if (!path(statement.Source) || !(at(index).Type == TokenType::Identifier && at(index).Text == "to"))
                    return std::nullopt;
                ++index;
                if (!path(statement.Target))
                    return std::nullopt;
                if (symbol(index, ";")) {
                    ++index;
                    return statement;
                }
                if (!symbol(index, "{"))
                    return std::nullopt;
                ++index;
                const auto word = [&](std::size_t i, std::string_view text) {
                    return at(i).Type == TokenType::Identifier && at(i).Text == text;
                };
                while (!symbol(index, "}")) {
                    // allocate c to d ...   allocation [name] [: Type] allocate c to d ...
                    std::string name;
                    std::string typeName;
                    if (word(index, "allocation")) {
                        ++index;
                        if (isName(index) && !word(index, "allocate"))
                            name = Source.substr(at(index).Begin, at(index).End - at(index).Begin), ++index;
                        if (symbol(index, ":")) {
                            ++index;
                            std::vector<std::string> type;
                            const std::size_t from = index;
                            if (!path(type))
                                return std::nullopt;
                            typeName = Source.substr(at(from).Begin, at(index - 1).End - at(from).Begin);
                        }
                        if (name.empty() && typeName.empty())
                            return std::nullopt;
                    }
                    if (!word(index, "allocate"))
                        return std::nullopt;
                    ++index;
                    auto refined = scanAllocate(index);
                    if (!refined)
                        return std::nullopt;
                    refined->Name = name;
                    refined->TypeName = typeName;
                    statement.Refined.push_back(std::move(*refined));
                }
                ++index;
                if (statement.Refined.empty())
                    return std::nullopt; // "{ }": kept as written
                return statement;
            }

            void addAllocation(ElementId owner, AllocationStatement statement, std::size_t line, std::size_t begin,
                               const std::string &name, const std::string &typeName, std::optional<std::size_t> parent) {
                const std::size_t index = Pendings.size();
                Pendings.push_back({Pending::Type::Allocation, RootId, owner, std::move(statement.Source),
                                    std::move(statement.Target), line, std::string(), begin, LastEnd, name, typeName,
                                    {}, {}, {}, std::nullopt, std::nullopt, {}});
                if (parent)
                    AllocationParent[index] = *parent;
                for (auto &refined: statement.Refined) {
                    const std::string refinedName = refined.Name;
                    const std::string refinedType = refined.TypeName;
                    addAllocation(owner, std::move(refined), line, begin, refinedName, refinedType, index);
                }
            }

            /// allocate a to b [{ ... }]   allocation [name] [: Type] allocate a to b [{ ... }]   (after the keyword)
            void parseAllocation(ElementId owner, std::size_t begin, std::size_t line, bool usage) {
                std::string name;
                std::string typeName;
                if (usage) {
                    if (isNameToken() && !isWord("allocate"))
                        name = Source.substr(peek().Begin, peek().End - peek().Begin), take();
                    if (isSymbol(":")) {
                        take();
                        typeName = readTypeText();
                    }
                    if ((name.empty() && typeName.empty()) || !isWord("allocate")) {
                        fallBackToText(owner, begin, line); // e.g. "allocation a : T;" without "allocate"
                        return;
                    }
                    take();
                }
                std::size_t index = Position;
                auto statement = scanAllocate(index);
                if (!statement) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                Position = index;
                LastEnd = Tokens[index - 1].End;
                const std::size_t top = Pendings.size();
                addAllocation(owner, std::move(*statement), line, begin, name, typeName, std::nullopt);
                // the whole statement (with its refining allocations) stays as text until all of it is resolved
                auto &pending = Pendings[top];
                pending.Placeholder = keepText(pending.Scope, pending.Begin, pending.End, pending.Line);
            }

            /// Statements of requirements, use cases and verifications; false if the statement is none of them.
            bool parseRequirementOrCase(ElementId owner, const std::string &keyword, const std::vector<std::string> &prefixes,
                                        std::size_t begin, std::size_t line) {
                const ElementKind ownerKindValue = ownerKind(owner);
                const bool inCase = owner != RootId && isCaseKind(ownerKindValue);
                const bool inRequirement = owner != RootId && isRequirementKind(ownerKindValue);
                const auto definitionOrUsage = [&](std::size_t words, ElementKind definition, ElementKind usage) {
                    for (std::size_t i = 0; i < words; ++i)
                        take();
                    if (isWord("def")) {
                        take();
                        parseDefinition(owner, prefixes, begin, line, definition);
                    } else {
                        parseUsage(owner, usage, prefixes, begin, line);
                    }
                    return true;
                };
                if (keyword == "requirement")
                    return definitionOrUsage(1, ElementKind::RequirementDefinition, ElementKind::RequirementUsage);
                if (keyword == "concern" || keyword == "viewpoint") {
                    // requirements with a keyword of their own: "concern def", "viewpoint 'structure perspective' { ... }"
                    take();
                    if (isWord("def")) {
                        take();
                        parseDefinition(owner, prefixes, begin, line, ElementKind::RequirementDefinition, keyword + " def");
                    } else {
                        parseUsage(owner, ElementKind::RequirementUsage, prefixes, begin, line, PerformStyle::DeclareAction,
                                   keyword);
                    }
                    return true;
                }
                if (keyword == "frame" && prefixes.empty() && inRequirement && !isWord("concern", 1)) {
                    take();
                    auto target = readPath();
                    if (target.empty() || !isSymbol(";")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    Pendings.push_back({Pending::Type::Frame, owner, owner, std::move(target), {}, line, std::string(),
                                        begin, LastEnd, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
                    reserve();
                    return true;
                }
                if (keyword == "use" && isWord("case", 1))
                    return definitionOrUsage(2, ElementKind::UseCaseDefinition, ElementKind::UseCaseUsage);
                if (keyword == "verification")
                    return definitionOrUsage(1, ElementKind::VerificationDefinition, ElementKind::VerificationUsage);
                if (keyword == "analysis")
                    return definitionOrUsage(1, ElementKind::AnalysisDefinition, ElementKind::AnalysisUsage);
                if ((keyword == "require" || keyword == "assume") && isWord("constraint", 1) && inRequirement) {
                    take();
                    take();
                    parseEvaluation(owner, ElementKind::ConstraintUsage, keyword + " constraint", prefixes, begin, line);
                    return true;
                }
                if (keyword == "subject" && (inCase || inRequirement)) {
                    take();
                    parseUsage(owner, ElementKind::Subject, prefixes, begin, line);
                    return true;
                }
                if ((keyword == "actor" && (inCase || inRequirement)) || (keyword == "stakeholder" && inRequirement)) {
                    take();
                    parseUsage(owner, ElementKind::Actor, prefixes, begin, line, PerformStyle::DeclareAction,
                               keyword == "stakeholder" ? std::optional<std::string>("stakeholder") : std::nullopt);
                    return true;
                }
                if (keyword == "objective" && inCase) {
                    take();
                    parseUsage(owner, ElementKind::RequirementUsage, prefixes, begin, line, PerformStyle::DeclareAction,
                               std::string("objective"));
                    return true;
                }
                if (keyword == "include" && isUseCaseKind(ownerKindValue) && owner != RootId) {
                    take();
                    if (isWord("use") && isWord("case", 1)) {
                        take();
                        take();
                        parseUsage(owner, ElementKind::UseCaseUsage, prefixes, begin, line, PerformStyle::DeclareAction,
                                   std::string("include use case"));
                        return true;
                    }
                    auto target = readPath();
                    if (!prefixes.empty() || target.empty() || !isSymbol(";")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    Pendings.push_back({Pending::Type::Include, owner, owner, std::move(target), {}, line, std::string(),
                                        begin, LastEnd, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
                    reserve();
                    return true;
                }
                if (keyword == "verify" && prefixes.empty() && Model.contains(owner) &&
                    Model.element(owner).kind() == ElementKind::RequirementUsage &&
                    Model.element(owner).declaredKeyword() == "objective" &&
                    isVerificationKind(ownerKind(Model.element(owner).owner())) && !isWord("requirement", 1)) {
                    take();
                    auto target = readPath();
                    if (target.empty() || !isSymbol(";")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    Pendings.push_back({Pending::Type::Verify, Model.element(owner).owner(), owner, std::move(target), {},
                                        line, std::string(), begin, LastEnd, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
                    reserve();
                    return true;
                }
                if (keyword == "satisfy" && prefixes.empty() && !isWord("requirement", 1)) {
                    take();
                    auto requirement = readPath();
                    if (!requirement.empty() && isSymbol(";") && owner != RootId) {
                        // "satisfy viewpoint;" in the body of the satisfying view (or part)
                        take();
                        Pendings.push_back({Pending::Type::Satisfy, owner, owner, std::move(requirement), {}, line,
                                            std::string(), begin, LastEnd, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
                        reserve();
                        return true;
                    }
                    if (requirement.empty() || !isWord("by")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    auto by = readPath();
                    if (by.empty() || !isSymbol(";")) {
                        fallBackToText(owner, begin, line);
                        return true;
                    }
                    take();
                    Pendings.push_back({Pending::Type::Satisfy, RootId, owner, std::move(requirement), std::move(by), line,
                                        std::string(), begin, LastEnd, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
                    reserve();
                    return true;
                }
                return false;
            }

            /// connection [name] [: Type] connect a to b;   interface [name] [: Type] connect a to b;
            void parseConnection(ElementId owner, std::size_t begin, std::size_t line, bool interface) {
                std::string name;
                std::string typeName;
                if (isNameToken() && !isWord("connect"))
                    name = Source.substr(peek().Begin, peek().End - peek().Begin), take();
                if (isSymbol(":")) {
                    take();
                    typeName = readTypeText();
                    if (typeName.empty()) {
                        fallBackToText(owner, begin, line);
                        return;
                    }
                }
                if (!isWord("connect") || (!interface && name.empty() && typeName.empty())) {
                    fallBackToText(owner, begin, line);
                    return;
                }
                take();
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
                Pendings.push_back({interface ? Pending::Type::Interface : Pending::Type::Connection, RootId, owner,
                                    std::move(source), std::move(target), line, std::string(), begin, LastEnd, name,
                                    typeName, {}, {}, {}, std::nullopt, std::nullopt, {}});
                reserve();
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
                                    std::string(), begin, LastEnd, {}, {}, {}, {}, {}, std::nullopt, std::nullopt, {}});
                reserve();
            }

            /// flow [name] [: Definition] [of Type] from a to b;
            void parseFlow(ElementId owner, std::size_t begin, std::size_t line) {
                std::string name;
                std::string typeName;
                std::string definition;
                if (isNameToken() && !isWord("of") && !isWord("from"))
                    name = Source.substr(peek().Begin, peek().End - peek().Begin), take();
                if (isSymbol(":")) {
                    take();
                    definition = readTypeText();
                    if (definition.empty()) {
                        fallBackToText(owner, begin, line);
                        return;
                    }
                }
                if (isWord("of")) {
                    take();
                    typeName = readTypeText();
                }
                if (!isWord("from")) {
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
                                    std::string(), begin, LastEnd, name, typeName, {}, {}, {}, std::nullopt, std::nullopt,
                                    definition});
                reserve();
            }

            // --- name resolution ----------------------------------------------------------------

            using Accept = std::function<bool(ElementId)>;

            std::optional<ElementId> child(ElementId owner, const std::string &name, const Accept &accept = {}) const {
                for (const ElementId id: Model.children(owner)) {
                    // statements kept as text (also the placeholders of pending relationships) are never meant
                    if (Model.element(id).kind() != ElementKind::Textual && Model.element(id).name() == name &&
                        (!accept || accept(id)))
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
                for (std::size_t index = 0; index < Pendings.size(); ++index) {
                    const auto &pending = Pendings[index];
                    if (pending.Kind == Pending::Type::Allocation) {
                        if (AllocationParent.count(index) == 0)
                            resolveAllocation(index);
                        continue;
                    }
                    if (pending.Kind == Pending::Type::Message || pending.Kind == Pending::Type::MessageThen ||
                        pending.Kind == Pending::Type::Dependency) {
                        resolveMessageOrDependency(pending);
                        continue;
                    }
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
                    if (pending.Kind == Pending::Type::Succession || pending.Kind == Pending::Type::Transition) {
                        resolveSuccession(pending);
                        continue;
                    }
                    if (pending.Kind == Pending::Type::Satisfy || pending.Kind == Pending::Type::Verify ||
                        pending.Kind == Pending::Type::Include || pending.Kind == Pending::Type::Frame) {
                        resolveRequirementRelationship(pending);
                        continue;
                    }
                    const bool binding = pending.Kind == Pending::Type::Binding;
                    const std::string label = binding ? "bind " + joinPath(pending.First) + " = " + joinPath(pending.Second)
                                              : (pending.Kind == Pending::Type::Connection ? "connect "
                                                 : pending.Kind == Pending::Type::Interface ? "interface " : "flow from ") +
                                                joinPath(pending.First) + " to " + joinPath(pending.Second);
                    const Accept isEnd = [&](ElementId id) {
                        const ElementKind kind = Model.element(id).kind();
                        if (binding)
                            return kind == ElementKind::Attribute;
                        if (pending.Kind == Pending::Type::Interface)
                            return kind == ElementKind::PortUsage;
                        return kind == ElementKind::PartUsage || kind == ElementKind::PortUsage;
                    };
                    const auto source = resolve(pending.Scope, pending.First, false, isEnd);
                    const auto target = resolve(pending.Scope, pending.Second, false, isEnd);
                    const RelationshipKind kind = binding ? RelationshipKind::Binding
                                                  : pending.Kind == Pending::Type::Connection ? RelationshipKind::Connection
                                                  : pending.Kind == Pending::Type::Interface ? RelationshipKind::Interface
                                                                                             : RelationshipKind::Flow;
                    if (!source || !target || !RelationshipRules::canRelate(Model, kind, *source, *target)) {
                        continue; // kept as written (placeholder)
                    }
                    release(pending);
                    addRelationship(kind, *source, *target, label, pending.Line, pending.Name, pending.TypeName,
                                    pending.Definition);
                }
                for (auto &[id, references]: External) {
                    try {
                        Model.setExternalSpecializations(id, references);
                    } catch (const std::exception &exception) {
                        lost(std::string(exception.what()), 0);
                    }
                }
            }

            void resolveSuccession(const Pending &pending) {
                const bool transition = pending.Kind == Pending::Type::Transition;
                const Accept isEnd = [&](ElementId id) { return isSuccessionEndElement(id); };
                const auto endOf = [&](const std::vector<std::string> &path, bool source) -> std::optional<ElementId> {
                    if (const auto found = resolve(pending.Scope, path, false, isEnd))
                        return found;
                    if (path.size() == 1 && (path.front() == "done" || (source && path.front() == "start")))
                        return implicitControl(pending.Scope, path.front());
                    return std::nullopt;
                };
                const std::optional<ElementId> source = pending.First.empty() ? std::optional<ElementId>(pending.Source)
                                                                                : endOf(pending.First, true);
                const auto target = pending.TargetId && Model.contains(*pending.TargetId) ? pending.TargetId
                                                                                          : endOf(pending.Second, false);
                const RelationshipKind kind = transition ? RelationshipKind::Transition : RelationshipKind::Succession;
                const std::string label = (transition ? "transition to " : "succession to ") + joinPath(pending.Second);
                if (!source || !target || !RelationshipRules::canRelate(Model, kind, *source, *target))
                    return; // kept as written (placeholder)
                release(pending);
                try {
                    const auto id = Model.addRelationship(kind, *source, *target);
                    if (!pending.Name.empty())
                        Model.setRelationshipLabel(id, pending.Name, std::string());
                    if (!pending.Guard.empty() || !pending.Trigger.empty() || !pending.Effect.empty())
                        Model.setBehaviorDetails(id, pending.Guard, pending.Trigger, pending.Effect);
                    ++Report.ImportedRelationships;
                } catch (const std::exception &exception) {
                    lost(label + " (" + exception.what() + ")", pending.Line);
                }
            }

            void resolveMessageOrDependency(const Pending &pending) {
                const bool dependency = pending.Kind == Pending::Type::Dependency;
                const Accept isEnd = [&](ElementId id) {
                    return dependency || isMessageEnd(Model.element(id).kind());
                };
                const auto source = resolve(pending.Scope, pending.First, false, isEnd);
                const auto target = resolve(pending.Scope, pending.Second, false, isEnd);
                const RelationshipKind kind = dependency ? RelationshipKind::Dependency : RelationshipKind::Message;
                if (!source || !target || !RelationshipRules::canRelate(Model, kind, *source, *target))
                    return; // kept as written (placeholder)
                release(pending);
                const std::string label = (dependency ? "dependency from " : "message from ") + joinPath(pending.First) +
                                          " to " + joinPath(pending.Second);
                try {
                    const auto id = Model.addRelationship(kind, *source, *target);
                    if (!pending.Name.empty() || !pending.TypeName.empty())
                        Model.setRelationshipLabel(id, pending.Name, pending.TypeName);
                    if (pending.Kind == Pending::Type::MessageThen)
                        Model.setSequenced(id, true);
                    Model.setRelationshipContext(id, pending.Scope); // stays where it was declared
                    ++Report.ImportedRelationships;
                } catch (const std::exception &exception) {
                    lost(label + " (" + exception.what() + ")", pending.Line);
                }
            }

            /// A top-level allocation with the allocations refining it: all of them, or the statement stays text.
            void resolveAllocation(std::size_t top) {
                std::vector<std::size_t> group{top}; // pre-order: a refined allocation before its refining ones
                for (std::size_t i = top + 1; i < Pendings.size(); ++i) {
                    const auto parent = AllocationParent.find(i);
                    if (parent == AllocationParent.end() ||
                        std::find(group.begin(), group.end(), parent->second) == group.end())
                        break;
                    group.push_back(i);
                }
                const Accept isEnd = [&](ElementId id) {
                    const ElementKind kind = Model.element(id).kind();
                    return isUsageKind(kind) && kind != ElementKind::EnumerationLiteral && kind != ElementKind::ControlNode;
                };
                std::vector<std::pair<ElementId, ElementId>> ends;
                for (const std::size_t index: group) {
                    const auto &pending = Pendings[index];
                    const auto source = resolve(pending.Scope, pending.First, false, isEnd);
                    const auto target = resolve(pending.Scope, pending.Second, false, isEnd);
                    if (!source || !target || !RelationshipRules::canRelate(Model, RelationshipKind::Allocation, *source, *target))
                        return; // kept as written (placeholder)
                    ends.emplace_back(*source, *target);
                }
                release(Pendings[top]);
                std::map<std::size_t, RelationshipId> created;
                for (std::size_t k = 0; k < group.size(); ++k) {
                    const auto &pending = Pendings[group[k]];
                    try {
                        const auto id = Model.addRelationship(RelationshipKind::Allocation, ends[k].first, ends[k].second);
                        if (!pending.Name.empty() || !pending.TypeName.empty())
                            Model.setRelationshipLabel(id, pending.Name, pending.TypeName);
                        if (const auto parent = AllocationParent.find(group[k]); parent != AllocationParent.end())
                            Model.setParentRelationship(id, created.at(parent->second));
                        else
                            Model.setRelationshipContext(id, pending.Scope); // stays where it was declared
                        created[group[k]] = id;
                        ++Report.ImportedRelationships;
                    } catch (const std::exception &exception) {
                        lost("allocate " + joinPath(pending.First) + " to " + joinPath(pending.Second) + " (" +
                             exception.what() + ")", pending.Line);
                        return;
                    }
                }
            }

            void resolveRequirementRelationship(const Pending &pending) {
                const auto ofKind = [&](std::vector<ElementKind> kinds) -> Accept {
                    return [this, kinds = std::move(kinds)](ElementId id) {
                        return std::find(kinds.begin(), kinds.end(), Model.element(id).kind()) != kinds.end();
                    };
                };
                std::optional<ElementId> source;
                std::optional<ElementId> target;
                RelationshipKind kind = RelationshipKind::Satisfy;
                std::string label;
                switch (pending.Kind) {
                    case Pending::Type::Satisfy:
                        target = resolve(pending.Scope, pending.First, false, ofKind({ElementKind::RequirementUsage}));
                        if (pending.Second.empty()) // "satisfy r;" in its source
                            source = Model.contains(pending.Source) ? std::optional<ElementId>(pending.Source) : std::nullopt;
                        else
                            source = resolve(pending.Scope, pending.Second, false,
                                             ofKind({ElementKind::PartUsage, ElementKind::PortUsage, ElementKind::ViewUsage,
                                                     ElementKind::ViewDefinition}));
                        label = "satisfy " + joinPath(pending.First) + " by " + joinPath(pending.Second);
                        break;
                    case Pending::Type::Frame: {
                        kind = RelationshipKind::Frame;
                        source = Model.contains(pending.Source) ? std::optional<ElementId>(pending.Source) : std::nullopt;
                        // never the framing requirement itself or one around it
                        const auto concern = ofKind({ElementKind::RequirementUsage});
                        target = resolve(pending.Scope, pending.First, false, [&](ElementId id) {
                            return concern(id) && id != pending.Source && !Model.isAncestor(id, pending.Source);
                        });
                        label = "frame " + joinPath(pending.First);
                        break;
                    }
                    case Pending::Type::Verify:
                        kind = RelationshipKind::Verify;
                        source = Model.contains(pending.Source) ? std::optional<ElementId>(pending.Source) : std::nullopt;
                        target = resolve(pending.Scope, pending.First, false, ofKind({ElementKind::RequirementUsage}));
                        label = "verify " + joinPath(pending.First);
                        break;
                    default:
                        kind = RelationshipKind::Include;
                        source = Model.contains(pending.Source) ? std::optional<ElementId>(pending.Source) : std::nullopt;
                        target = resolve(pending.Scope, pending.First, false, ofKind({ElementKind::UseCaseUsage}));
                        label = "include " + joinPath(pending.First);
                        break;
                }
                if (!source || !target || !RelationshipRules::canRelate(Model, kind, *source, *target))
                    return; // kept as written (placeholder)
                release(pending);
                addRelationship(kind, *source, *target, label, pending.Line);
                if (kind == RelationshipKind::Satisfy && pending.Second.empty()) {
                    for (const auto &relationship: Model.relationships()) {
                        if (relationship.Kind == kind && relationship.Source == *source && relationship.Target == *target)
                            Model.setRelationshipContext(relationship.Id, *source);
                    }
                }
            }

            void addRelationship(RelationshipKind kind, ElementId source, ElementId target, const std::string &label,
                                 std::size_t line, const std::string &name = {}, const std::string &typeName = {},
                                 const std::string &definition = {}) {
                try {
                    const auto id = Model.addRelationship(kind, source, target);
                    if (!name.empty() || !typeName.empty())
                        Model.setRelationshipLabel(id, name, typeName);
                    if (!definition.empty())
                        Model.setFlowDefinition(id, definition);
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
            std::map<ElementId, ElementId> Previous; ///< per body: last action / control node / state
            std::map<ElementId, std::vector<std::string>> External;
            std::map<std::size_t, std::size_t> AllocationParent; ///< pending refining allocation -> the refined one
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

        // "assert", "require", "assume" in front of the name (an unnamed one shows only its expression)
        const std::string declared = element.declaredKeyword().value_or("");
        const bool prefixed = declared == "assert constraint" || declared == "require constraint" ||
                              declared == "assume constraint";
        std::string display = prefixed ? declared.substr(0, declared.find(' ')) : std::string();
        if (!element.name().empty())
            display += (display.empty() ? "" : " ") + element.name();
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
                display += (display.empty() ? "{ " : " { ") + summary.Expression + " }";
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

    std::string SysMLTextImporter::commentBody(const std::string &statement) {
        const std::size_t open = statement.find("/*");
        std::string body;
        if (open == std::string::npos) {
            const std::size_t line = statement.find("//");
            body = line == std::string::npos ? std::string() : statement.substr(line + 2);
        } else {
            const std::size_t close = statement.find("*/", open + 2);
            body = statement.substr(open + 2, close == std::string::npos ? std::string::npos : close - open - 2);
        }
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start <= body.size()) {
            std::size_t end = body.find('\n', start);
            if (end == std::string::npos)
                end = body.size();
            std::string line = body.substr(start, end - start);
            const std::size_t first = line.find_first_not_of(" \t\r");
            line = first == std::string::npos ? std::string() : line.substr(first);
            if (!line.empty() && line.front() == '*' && (line.size() == 1 || line[1] == ' '))
                line = line.substr(line.size() > 1 ? 2 : 1);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
                line.pop_back();
            lines.push_back(line);
            start = end + 1;
        }
        while (!lines.empty() && lines.front().empty())
            lines.erase(lines.begin());
        while (!lines.empty() && lines.back().empty())
            lines.pop_back();
        std::string result;
        for (std::size_t i = 0; i < lines.size(); ++i)
            result += (i > 0 ? "\n" : "") + lines[i];
        return result;
    }

    std::string SysMLTextImporter::docStatement(const std::string &body) {
        std::string text = body;
        for (std::size_t at = text.find("*/"); at != std::string::npos; at = text.find("*/", at))
            text.replace(at, 2, "* /");
        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
            text.pop_back();
        if (text.find('\n') == std::string::npos)
            return "doc /* " + text + " */";
        std::string result = "doc /*";
        std::size_t start = 0;
        while (start <= text.size()) {
            std::size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            const std::string line = text.substr(start, end - start);
            result += "\n *" + (line.empty() ? std::string() : " " + line);
            start = end + 1;
        }
        return result + "\n */";
    }

    std::optional<ElementId> SysMLTextImporter::docOf(const SemanticModel &model, ElementId id) {
        for (const ElementId child: model.children(id)) {
            const auto &element = model.element(child);
            if (element.kind() == ElementKind::Textual && element.keyword() == "doc")
                return child;
        }
        return std::nullopt;
    }

    void SysMLTextImporter::clear(SemanticModel &model) {
        const std::vector<ElementId> roots = model.children(RootId);
        for (const ElementId id: roots)
            model.removeElement(id);
        const auto relationships = model.relationships();
        for (const auto &relationship: relationships) {
            if (model.containsRelationship(relationship.Id)) // refining allocations go with the refined one
                model.removeRelationship(relationship.Id);
        }
    }
}
