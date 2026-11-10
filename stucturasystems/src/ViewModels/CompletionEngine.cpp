//
// Created by Moritz Herzog on 30.10.26.
//

#include <kerml/root/elements/NameEscaping.h>
#include "CompletionEngine.h"

#include <algorithm>
#include <optional>

#include <QSet>

#include "LanguageKeywords.h"

namespace StructuraSystems::Client {
    namespace {
        bool isIdentifierStart(const QChar c) { return c.isLetter() || c == u'_'; }
        bool isIdentifierPart(const QChar c) { return c.isLetterOrNumber() || c == u'_'; }

        // ------------------------------------------------------------------------------------ comments and strings

        /** The text with comments and strings blanked out (same length, line breaks stay), and what the cursor is in. */
        struct MaskedText {
            QString Text;
            bool InComment = false;
            bool InString = false;
            int QuoteStart = -1;   ///< Position of the opening quote if the cursor is inside of a quoted name
        };

        /**
         * Blanks out comments (// and the block comments) and strings. Quoted names stay, they are part of the code. Strings and
         * quoted names end at the end of the line, so a quote that was never closed does not hide the rest of the text.
         * @param cursor the position whose state is reported, -1 for none.
         */
        MaskedText mask(const QString &text, int cursor) {
            enum class State { Code, Line, Block, String, Quoted };
            MaskedText result;
            result.Text = text;
            State state = State::Code;
            int quoteStart = -1;
            const int length = static_cast<int>(text.size());
            auto blank = [&result](int index) {
                if (result.Text[index] != u'\n')
                    result.Text[index] = u' ';
            };
            for (int i = 0; i <= length; ++i) {
                if (i == cursor) {
                    result.InComment = state == State::Line || state == State::Block;
                    result.InString = state == State::String;
                    result.QuoteStart = state == State::Quoted ? quoteStart : -1;
                }
                if (i == length)
                    break;
                const QChar c = text[i];
                switch (state) {
                    case State::Code:
                        if (c == u'/' && i + 1 < length && text[i + 1] == u'/') {
                            state = State::Line;
                            blank(i);
                            blank(i + 1);
                            ++i;
                        } else if (c == u'/' && i + 1 < length && text[i + 1] == u'*') {
                            state = State::Block;
                            blank(i);
                            blank(i + 1);
                            ++i;
                        } else if (c == u'"') {
                            state = State::String;
                            blank(i);
                        } else if (c == u'\'') {
                            state = State::Quoted;
                            quoteStart = i;
                        }
                        break;
                    case State::Line:
                        if (c == u'\n')
                            state = State::Code;
                        else
                            blank(i);
                        break;
                    case State::Block:
                        blank(i);
                        if (c == u'*' && i + 1 < length && text[i + 1] == u'/') {
                            blank(i + 1);
                            ++i;
                            state = State::Code;
                        }
                        break;
                    case State::String:
                        blank(i);
                        if (c == u'\\' && i + 1 < length && text[i + 1] != u'\n') {
                            blank(i + 1);
                            ++i;
                            if (i == cursor)
                                result.InString = true;   // between the backslash and the escaped character
                        } else if (c == u'"' || c == u'\n') {
                            state = State::Code;
                        }
                        break;
                    case State::Quoted:
                        // An escaped quote (\') does not end the name.
                        if (c == u'\\' && i + 1 < length && text[i + 1] != u'\n') {
                            ++i;
                            if (i == cursor)
                                result.QuoteStart = quoteStart;
                        } else if (c == u'\'' || c == u'\n') {
                            state = State::Code;
                        }
                        break;
                }
            }
            return result;
        }

        // ------------------------------------------------------------------------------------ context

        const QSet<QString> &declarationWords() {
            // After these the name of a new element is written.
            static const QSet<QString> Words = {
                    QStringLiteral("def"), QStringLiteral("package"), QStringLiteral("namespace"), QStringLiteral("class"),
                    QStringLiteral("classifier"), QStringLiteral("datatype"), QStringLiteral("struct"), QStringLiteral("assoc"),
                    QStringLiteral("association"), QStringLiteral("behavior"), QStringLiteral("function"), QStringLiteral("predicate"),
                    QStringLiteral("interaction"), QStringLiteral("metaclass"), QStringLiteral("type"), QStringLiteral("alias")};
            return Words;
        }

        enum class Before { Other, StatementStart, Type, Import, Declaration };

        struct BeforeInfo {
            Before What = Before::Other;
            bool AllowFeatures = false;
        };

        /** Moves @p position to the start of the reference (qualified name) that ends there. Unchanged if there is none. */
        int skipReference(const QString &text, int position) {
            const int start = position;
            int p = position;
            while (true) {
                int segmentStart;
                if (p >= 2 && text[p - 1] == u'\'') {
                    segmentStart = static_cast<int>(text.lastIndexOf(u'\'', p - 2));
                    if (segmentStart < 0 || text.mid(segmentStart, p - segmentStart).contains(u'\n'))
                        return start;
                } else {
                    segmentStart = p;
                    while (segmentStart > 0 && isIdentifierPart(text[segmentStart - 1]))
                        --segmentStart;
                    if (segmentStart == p)
                        return start;
                }
                p = segmentStart;
                if (p >= 2 && text[p - 1] == u':' && text[p - 2] == u':')
                    p -= 2;
                else
                    return p;
            }
        }

        /** The identifier that ends at @p end (no whitespace skipping). */
        QString wordEndingAt(const QString &text, int end, int *start = nullptr) {
            int s = end;
            while (s > 0 && isIdentifierPart(text[s - 1]))
                --s;
            if (start != nullptr)
                *start = s;
            return text.mid(s, end - s);
        }

        /** What stands left of the position (after whitespace): an operator that expects a type, 'import', a declaration word ... */
        BeforeInfo classify(const QString &text, int end) {
            for (int round = 0; round < 8; ++round) {
                while (end > 0 && text[end - 1].isSpace())
                    --end;
                if (end == 0)
                    return {round == 0 ? Before::StatementStart : Before::Other, false};
                const QChar last = text[end - 1];
                if (last == u'{' || last == u';' || last == u'}')
                    return {round == 0 ? Before::StatementStart : Before::Other, false};

                const auto endsWith = [&text, end](const char *literal) {
                    const QLatin1String view{literal};
                    return end >= view.size() && QStringView(text).mid(end - view.size(), view.size()) == view;
                };
                if (endsWith(":>>") || endsWith("::>") || endsWith(":>"))
                    return {Before::Type, true};
                if (last == u':' && !(end >= 2 && text[end - 2] == u':'))
                    return {Before::Type, false};
                if (last == u'~')
                    return {Before::Type, false};

                if (last == u',') {
                    // A list of references: look behind the previous element for the operator.
                    int p = end - 1;
                    while (p > 0 && text[p - 1].isSpace())
                        --p;
                    const int skipped = skipReference(text, p);
                    if (skipped == p)
                        return {};
                    end = skipped;
                    continue;
                }

                if (!isIdentifierPart(last))
                    return {};
                int wordStart = 0;
                const QString word = wordEndingAt(text, end, &wordStart).toLower();
                if (word == QLatin1String("specializes") || word == QLatin1String("subsets") || word == QLatin1String("redefines") ||
                    word == QLatin1String("references"))
                    return {Before::Type, true};
                if (word == QLatin1String("conjugates"))
                    return {Before::Type, false};
                if (word == QLatin1String("by")) {
                    int p = wordStart;
                    while (p > 0 && text[p - 1].isSpace())
                        --p;
                    const QString previous = wordEndingAt(text, p).toLower();
                    if (previous == QLatin1String("typed") || previous == QLatin1String("defined"))
                        return {Before::Type, false};
                    return {};
                }
                if (round > 0)
                    return {};
                if (word == QLatin1String("import"))
                    return {Before::Import, false};
                if (word == QLatin1String("all")) {
                    int p = wordStart;
                    while (p > 0 && text[p - 1].isSpace())
                        --p;
                    if (wordEndingAt(text, p).toLower() == QLatin1String("import"))
                        return {Before::Import, false};
                    return {};
                }
                if (declarationWords().contains(word))
                    return {Before::Declaration, false};
                return {};
            }
            return {};
        }

        // ------------------------------------------------------------------------------------ snippets

        struct Snippet {
            const char *Label;
            const char *Template;   ///< "${Name}" is the placeholder that is selected, lines are continued with the indentation
        };

        const QList<Snippet> &sysmlSnippets() {
            static const QList<Snippet> Snippets = {
                    {"part def", "part def ${Name} {\n    \n}"},
                    {"part", "part ${name} : Type;"},
                    {"attribute def", "attribute def ${Name};"},
                    {"attribute", "attribute ${name} : Type;"},
                    {"port def", "port def ${Name} {\n    \n}"},
                    {"item def", "item def ${Name} {\n    \n}"},
                    {"action def", "action def ${Name} {\n    \n}"},
                    {"state def", "state def ${Name} {\n    \n}"},
                    {"requirement def", "requirement def ${Name} {\n    \n}"},
                    {"constraint def", "constraint def ${Name} {\n    \n}"},
                    {"connection def", "connection def ${Name} {\n    \n}"},
                    {"interface def", "interface def ${Name} {\n    \n}"},
                    {"package", "package ${Name} {\n    \n}"},
                    {"import", "import ${Package}::*;"}};
            return Snippets;
        }

        const QList<Snippet> &kermlSnippets() {
            static const QList<Snippet> Snippets = {
                    {"package", "package ${Name} {\n    \n}"},
                    {"class", "class ${Name} {\n    \n}"},
                    {"datatype", "datatype ${Name};"},
                    {"feature", "feature ${name} : Type;"},
                    {"classifier", "classifier ${Name} {\n    \n}"},
                    {"import", "import ${Package}::*;"}};
            return Snippets;
        }

        CompletionItem makeSnippet(const Snippet &snippet, const QString &indent) {
            QString text = QString::fromLatin1(snippet.Template);
            text.replace(QLatin1Char('\n'), QLatin1Char('\n') + indent);
            CompletionItem item;
            item.Label = QString::fromLatin1(snippet.Label);
            item.Kind = CompletionKind::Snippet;
            const auto open = static_cast<int>(text.indexOf(QLatin1String("${")));
            if (open >= 0) {
                const auto close = static_cast<int>(text.indexOf(u'}', open));
                const QString placeholder = text.mid(open + 2, close - open - 2);
                text.replace(open, close - open + 1, placeholder);
                item.CursorOffset = open;
                item.SelectionLength = static_cast<int>(placeholder.size());
            }
            item.InsertText = text;
            const auto firstLine = text.left(text.indexOf(u'\n'));
            item.Detail = text.contains(u'\n') ? firstLine + QStringLiteral(" ...") : text;
            return item;
        }

        // ------------------------------------------------------------------------------------ names

        CompletionKind kindOf(const NameEntry &entry) {
            if (entry.IsType)
                return CompletionKind::Definition;
            if (entry.IsFeature)
                return CompletionKind::Usage;
            if (entry.IsNamespace)
                return CompletionKind::Package;
            return CompletionKind::Other;
        }

        enum Tier {
            SnippetTier = 0,
            KeywordTier = 1,
            LocalTier = 2,
            DocumentTier = 3,
            LibraryTier = 4
        };

        /** An entry of the model as item. @p qualified: the insert text is the qualified name (the item is used without qualifier). */
        CompletionItem itemOf(const NameEntry &entry, bool qualified, const CompletionContext &context, bool stepInto = false) {
            CompletionItem item;
            item.Label = entry.Name;
            item.Kind = kindOf(entry);
            item.Detail = qualified && !entry.Owner.isEmpty() ? entry.QualifiedName : entry.Type;
            item.InsertText = qualified ? CompletionEngine::writtenQualified(entry.QualifiedName, context.InQuotedName)
                                        : CompletionEngine::written(entry.Name, context.InQuotedName);
            if (stepInto)
                item.InsertText += QStringLiteral("::");
            return item;
        }

        CompletionItem itemOf(const LocalName &local, const CompletionContext &context, bool stepInto = false) {
            CompletionItem item;
            item.Label = local.Name;
            item.Kind = local.Kind;
            item.Detail = QStringLiteral("local");
            item.InsertText = CompletionEngine::written(local.Name, context.InQuotedName);
            if (stepInto)
                item.InsertText += QStringLiteral("::");
            return item;
        }

        /**
         * One possible suggestion. The entries of the model are only turned into items if they are among the first MaxItems:
         * the libraries have thousands of names, and complete() runs with every key while the list is open.
         */
        struct Candidate {
            std::optional<CompletionItem> Item;   ///< Made right away (keywords, snippets, local names)
            const NameEntry *Entry = nullptr;     ///< Else made from this entry in result()
            bool Qualified = false;
            bool StepInto = false;
            int Match = 0;      ///< 0 = the name starts with the prefix, 1 = it contains it
            int Tier = 0;       ///< Snippets, keywords, local names, names of the document, names of the libraries
            QString SortKey;
            QString TieKey;     ///< The insert text (or the name it is made from)
            qsizetype Sequence = 0;
        };

        /** Collects the candidates of one request: filters by the prefix, drops duplicates. */
        class Collector {
        public:
            explicit Collector(const CompletionContext &context) : Context(context), Prefix(context.Prefix.toLower()) {}

            /** -1 if the name does not match the prefix, else 0 (start) or 1 (inside). */
            [[nodiscard]] int match(const QString &lowerName) const {
                if (Prefix.isEmpty() || lowerName.startsWith(Prefix))
                    return 0;
                // A single letter would match nearly everything inside of the names.
                if (Prefix.size() >= 2 && lowerName.contains(Prefix))
                    return 1;
                return -1;
            }

            void add(CompletionItem item, const QString &lowerName, int tier) {
                const int matched = match(lowerName);
                if (matched < 0)
                    return;
                Candidate candidate;
                candidate.Match = matched;
                candidate.Tier = tier;
                candidate.SortKey = lowerName;
                candidate.TieKey = item.InsertText;
                candidate.Sequence = Candidates.size();
                candidate.Item = std::move(item);
                Candidates.append(std::move(candidate));
            }

            /** An entry of the model; the item is made later, and only if it is shown. */
            void add(const NameEntry &entry, const QString &lowerName, bool qualified, bool stepInto = false) {
                const int matched = match(lowerName);
                if (matched < 0)
                    return;
                Candidate candidate;
                candidate.Entry = &entry;
                candidate.Qualified = qualified;
                candidate.StepInto = stepInto;
                candidate.Match = matched;
                candidate.Tier = entry.InDocument ? DocumentTier : LibraryTier;
                candidate.SortKey = lowerName;
                candidate.TieKey = qualified ? entry.QualifiedName : entry.Name;
                candidate.Sequence = Candidates.size();
                Candidates.append(std::move(candidate));
            }

            QList<CompletionItem> result() {
                const auto less = [](const Candidate &a, const Candidate &b) {
                    if (a.Match != b.Match)
                        return a.Match < b.Match;
                    if (a.Tier != b.Tier)
                        return a.Tier < b.Tier;
                    if (a.SortKey != b.SortKey)
                        return a.SortKey < b.SortKey;
                    if (a.TieKey != b.TieKey)
                        return a.TieKey < b.TieKey;
                    return a.Sequence < b.Sequence;
                };
                // Only the head of the list is sorted and turned into items (a bit more of it if duplicates are dropped).
                QList<CompletionItem> items;
                QSet<QString> seen;
                const qsizetype count = Candidates.size();
                qsizetype sorted = 0;
                for (qsizetype next = 0; next < count && items.size() < CompletionEngine::MaxItems; ++next) {
                    if (next == sorted) {
                        sorted = (std::min)(count, sorted + 2 * CompletionEngine::MaxItems);
                        std::partial_sort(Candidates.begin() + next, Candidates.begin() + sorted, Candidates.end(), less);
                    }
                    const auto &candidate = Candidates.at(next);
                    CompletionItem item = candidate.Item.has_value() ? *candidate.Item
                                                                     : itemOf(*candidate.Entry, candidate.Qualified, Context, candidate.StepInto);
                    // The same suggestion from two sources (a local name that the model knows, too) is shown once.
                    const QString key = item.Label + QLatin1Char('\x1f') + item.InsertText + QLatin1Char('\x1f') +
                                        QString::number(static_cast<int>(item.Kind));
                    if (seen.contains(key))
                        continue;
                    seen.insert(key);
                    items.append(std::move(item));
                }
                return items;
            }

        private:
            const CompletionContext &Context;
            QString Prefix;
            QList<Candidate> Candidates;
        };

    }

    // ---------------------------------------------------------------------------------------------- NameIndex

    NameIndex::NameIndex(QList<NameEntry> entries) : NameIndex(nullptr, std::move(entries)) {}

    NameIndex::NameIndex(std::shared_ptr<const NameIndex> base, QList<NameEntry> entries)
        : Base(std::move(base)), BaseCount(Base ? Base->count() : 0), Entries(std::move(entries)) {
        if (Base) {
            Types = Base->types();
            Namespaces = Base->namespaces();
        }
        Lower.reserve(Entries.size());
        for (int i = 0; i < Entries.size(); ++i) {
            const auto &entry = Entries.at(i);
            const int index = static_cast<int>(BaseCount) + i;
            Lower.append(entry.Name.toLower());
            auto owner = ByOwner.find(entry.Owner);
            if (owner == ByOwner.end())
                owner = ByOwner.insert(entry.Owner, Base ? Base->members(entry.Owner) : QList<int>());   // (the members of the base come first)
            owner.value().append(index);
            ByQualifiedName[entry.QualifiedName].append(index);
            if (entry.IsType)
                Types.append(index);
            if (entry.IsNamespace)
                Namespaces.append(index);
        }
    }

    const QList<int> &NameIndex::members(const QString &owner) const {
        static const QList<int> None;
        const auto found = ByOwner.constFind(owner);
        if (found != ByOwner.constEnd())
            return found.value();
        return Base ? Base->members(owner) : None;
    }

    QList<int> NameIndex::withQualifiedName(const QString &qualifiedName) const {
        QList<int> result;
        if (Base)
            result = Base->withQualifiedName(qualifiedName);
        const auto found = ByQualifiedName.constFind(qualifiedName);
        if (found != ByQualifiedName.constEnd())
            result.append(found.value());
        return result;
    }

    QList<int> NameIndex::findNamespaces(const QString &qualifier) const {
        QList<int> result;
        QSet<QString> seen;
        for (const int index : withQualifiedName(qualifier)) {
            if (entry(index).IsNamespace && !seen.contains(qualifier)) {
                seen.insert(qualifier);
                result.append(index);
            }
        }
        if (!result.isEmpty())
            return result;
        const QString suffix = QStringLiteral("::") + qualifier;
        for (const int index : Namespaces) {
            const auto &candidate = entry(index);
            if (candidate.QualifiedName.endsWith(suffix) && !seen.contains(candidate.QualifiedName)) {
                seen.insert(candidate.QualifiedName);
                result.append(index);
            }
        }
        return result;
    }

    QList<int> NameIndex::visibleMembers(const QString &owner) const {
        QList<int> result = members(owner);
        QSet<QString> done{owner};
        QStringList pending;
        const auto addExports = [this, &pending](const QString &name) {
            for (const int index : withQualifiedName(name))
                pending.append(entry(index).Exports);
        };
        addExports(owner);
        while (!pending.isEmpty()) {
            const QString next = pending.takeLast();
            if (done.contains(next))
                continue;
            done.insert(next);
            result.append(members(next));
            addExports(next);
        }
        return result;
    }

    // ---------------------------------------------------------------------------------------------- CompletionEngine

    QString CompletionEngine::written(const QString &name, bool force) {
        bool plain = !name.isEmpty() && isIdentifierStart(name.front());
        for (qsizetype i = 1; plain && i < name.size(); ++i)
            plain = isIdentifierPart(name[i]);
        // (a reserved word of KerML or SysML v2 is no basic name: `not` is written 'not')
        if (plain && !force && !KerML::Names::isReservedWord(name.toStdString()))
            return name;
        QString escaped = name;
        escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
        escaped.replace(QLatin1Char('\''), QLatin1String("\\'"));
        return QLatin1Char('\'') + escaped + QLatin1Char('\'');
    }

    QString CompletionEngine::writtenQualified(const QString &qualifiedName, bool forceLast) {
        const QStringList segments = qualifiedName.split(QStringLiteral("::"));
        QStringList result;
        for (int i = 0; i < segments.size(); ++i)
            result.append(written(segments.at(i), forceLast && i == segments.size() - 1));
        return result.join(QStringLiteral("::"));
    }

    CompletionContext CompletionEngine::analyze(const QString &text, int cursor) {
        cursor = std::clamp(cursor, 0, static_cast<int>(text.size()));
        CompletionContext context;
        context.ReplaceStart = cursor;
        context.ReplaceEnd = cursor;

        const MaskedText masked = mask(text, cursor);
        if (masked.InComment || masked.InString) {
            context.Valid = false;
            return context;
        }

        const int lineStart = cursor > 0 ? static_cast<int>(text.lastIndexOf(u'\n', cursor - 1)) + 1 : 0;
        int indentEnd = lineStart;
        while (indentEnd < cursor && (text[indentEnd] == u' ' || text[indentEnd] == u'\t'))
            ++indentEnd;
        context.Indent = text.mid(lineStart, indentEnd - lineStart);

        const QString &t = masked.Text;
        int prefixStart;
        if (masked.QuoteStart >= 0) {
            context.InQuotedName = true;
            prefixStart = masked.QuoteStart;
            context.Prefix = text.mid(prefixStart + 1, cursor - prefixStart - 1);
            // The whole name is replaced, up to its closing quote (if it has one on this line).
            for (int i = cursor; i < text.size() && text[i] != u'\n'; ++i) {
                if (text[i] == u'\\') {
                    ++i;
                } else if (text[i] == u'\'') {
                    context.ReplaceEnd = i + 1;
                    break;
                }
            }
        } else {
            prefixStart = cursor;
            while (prefixStart > 0 && isIdentifierPart(t[prefixStart - 1]))
                --prefixStart;
            context.Prefix = text.mid(prefixStart, cursor - prefixStart);
            if (!context.Prefix.isEmpty() && context.Prefix.front().isDigit()) {
                context.Valid = false;
                return context;
            }
        }
        context.ReplaceStart = prefixStart;

        // Member access with a dot (feature chains) is not known without the model.
        if (prefixStart >= 1 && t[prefixStart - 1] == u'.') {
            context.Valid = false;
            return context;
        }

        // A::B:: in front of the prefix
        QStringList segments;
        int referenceStart = prefixStart;
        if (prefixStart >= 2 && t[prefixStart - 1] == u':' && t[prefixStart - 2] == u':') {
            int p = prefixStart - 2;
            while (true) {
                QString name;
                int segmentStart;
                if (p >= 2 && t[p - 1] == u'\'') {
                    segmentStart = static_cast<int>(t.lastIndexOf(u'\'', p - 2));
                    if (segmentStart < 0 || t.mid(segmentStart, p - segmentStart).contains(u'\n')) {
                        context.Valid = false;
                        return context;
                    }
                    name = t.mid(segmentStart + 1, p - segmentStart - 2);
                } else {
                    segmentStart = p;
                    while (segmentStart > 0 && isIdentifierPart(t[segmentStart - 1]))
                        --segmentStart;
                    if (segmentStart == p) {
                        context.Valid = false;
                        return context;
                    }
                    name = t.mid(segmentStart, p - segmentStart);
                }
                segments.prepend(name);
                referenceStart = segmentStart;
                if (segmentStart >= 2 && t[segmentStart - 1] == u':' && t[segmentStart - 2] == u':') {
                    p = segmentStart - 2;
                    continue;
                }
                break;
            }
        }
        context.Qualifier = segments.join(QStringLiteral("::"));

        const BeforeInfo before = classify(t, referenceStart);
        if (before.What == Before::Declaration) {
            context.Kind = ContextKind::Declaration;
            return context;
        }
        if (!segments.isEmpty()) {
            if (before.What == Before::Import) {
                context.Kind = ContextKind::Import;
            } else {
                context.Kind = ContextKind::Member;
                if (before.What == Before::Type) {
                    context.ExpectsType = true;
                    context.AllowFeatures = before.AllowFeatures;
                }
            }
            return context;
        }
        switch (before.What) {
            case Before::StatementStart:
                context.Kind = ContextKind::Keyword;
                break;
            case Before::Type:
                context.Kind = ContextKind::Type;
                context.ExpectsType = true;
                context.AllowFeatures = before.AllowFeatures;
                break;
            case Before::Import:
                context.Kind = ContextKind::Import;
                break;
            default:
                context.Kind = ContextKind::Anything;
                break;
        }
        return context;
    }

    QList<LocalName> CompletionEngine::scanLocalNames(const QString &text, int cursor) {
        static const QHash<QString, CompletionKind> Declarators = [] {
            QHash<QString, CompletionKind> words;
            for (const char *word : {"def", "class", "classifier", "datatype", "struct", "assoc", "association", "behavior", "function",
                                     "predicate", "interaction", "metaclass", "type"})
                words.insert(QLatin1String(word), CompletionKind::Definition);
            for (const char *word : {"package", "namespace"})
                words.insert(QLatin1String(word), CompletionKind::Package);
            for (const char *word : {"part", "attribute", "port", "item", "action", "state", "requirement", "constraint", "connection",
                                     "interface", "allocation", "view", "viewpoint", "rendering", "verification", "analysis", "case",
                                     "concern", "metadata", "occurrence", "enum", "calc", "flow", "ref", "feature", "connector", "step",
                                     "expr", "event", "snapshot", "timeslice", "in", "out", "inout", "return", "subject", "actor",
                                     "stakeholder", "objective", "end", "binding", "succession", "alias"})
                words.insert(QLatin1String(word), CompletionKind::Usage);
            return words;
        }();

        const QString t = mask(text, -1).Text;
        const int length = static_cast<int>(t.size());
        QList<LocalName> result;
        QSet<QString> seen;
        bool pending = false;
        CompletionKind pendingKind = CompletionKind::Usage;

        int i = 0;
        while (i < length) {
            const QChar c = t[i];
            if (c.isSpace()) {
                ++i;
                continue;
            }
            if (c == u'<' && pending) {
                // short name: part <s> name
                const auto close = t.indexOf(u'>', i);
                i = close < 0 ? length : static_cast<int>(close) + 1;
                continue;
            }
            int start = i;
            QString word;
            bool quoted = false;
            if (isIdentifierStart(c)) {
                while (i < length && isIdentifierPart(t[i]))
                    ++i;
                word = t.mid(start, i - start);
            } else if (c == u'\'') {
                const auto close = t.indexOf(u'\'', i + 1);
                const int end = close < 0 ? length : static_cast<int>(close);
                word = t.mid(i + 1, end - i - 1);
                i = close < 0 ? length : end + 1;
                quoted = true;
            } else {
                pending = false;
                ++i;
                continue;
            }

            const auto declarator = quoted ? Declarators.constEnd() : Declarators.constFind(word);
            if (declarator != Declarators.constEnd()) {
                pending = true;
                pendingKind = declarator.value();
                continue;
            }
            if (!pending)
                continue;
            pending = false;
            if (word.isEmpty() || (!quoted && (sysmlKeywords().contains(word) || kermlKeywords().contains(word))))
                continue;
            // The word that is being typed is not a declaration yet.
            if (start <= cursor && cursor <= i)
                continue;
            // A reference like `perform Library::action`, not a declaration.
            int next = i;
            while (next < length && t[next].isSpace())
                ++next;
            if (next < length && (t[next] == u'.' || (t[next] == u':' && next + 1 < length && t[next + 1] == u':')))
                continue;
            if (!seen.contains(word)) {
                seen.insert(word);
                result.append({word, pendingKind});
            }
        }
        return result;
    }

    QList<CompletionItem> CompletionEngine::complete(const CompletionContext &context, const QString &language, const NameIndex &index,
                                                     const QList<LocalName> &localNames) {
        if (!context.Valid || context.Kind == ContextKind::Declaration)
            return {};

        Collector collector(context);
        const bool kerml = language == QLatin1String("KerML");

        const auto addKeywords = [&] {
            for (const QString &keyword : kerml ? kermlKeywords() : sysmlKeywords()) {
                CompletionItem item;
                item.Label = keyword;
                item.InsertText = keyword;
                item.Detail = QStringLiteral("keyword");
                item.Kind = CompletionKind::Keyword;
                collector.add(std::move(item), keyword.toLower(), KeywordTier);
            }
        };
        const auto addSnippets = [&] {
            for (const auto &snippet : kerml ? kermlSnippets() : sysmlSnippets())
                collector.add(makeSnippet(snippet, context.Indent), QString::fromLatin1(snippet.Label), SnippetTier);
        };
        const auto addLocals = [&](bool definitionsOnly, bool featuresToo, bool stepIntoPackages) {
            for (const auto &local : localNames) {
                if (definitionsOnly) {
                    const bool wanted = local.Kind == CompletionKind::Definition || local.Kind == CompletionKind::Package ||
                                        (featuresToo && local.Kind == CompletionKind::Usage);
                    if (!wanted)
                        continue;
                }
                collector.add(itemOf(local, context, stepIntoPackages && local.Kind == CompletionKind::Package), local.Name.toLower(),
                              LocalTier);
            }
        };

        // Only plain names make sense inside of quotes.
        const bool namesOnly = context.InQuotedName;

        switch (context.Kind) {
            case ContextKind::Keyword:
                if (!namesOnly) {
                    addSnippets();
                    addKeywords();
                }
                addLocals(false, false, false);
                for (qsizetype i = 0; i < index.count(); ++i)
                    collector.add(index.entries().at(i), index.lowerName(i), true);
                break;
            case ContextKind::Anything:
                if (!namesOnly)
                    addKeywords();
                addLocals(false, false, false);
                for (qsizetype i = 0; i < index.count(); ++i)
                    collector.add(index.entries().at(i), index.lowerName(i), true);
                break;
            case ContextKind::Type:
                addLocals(true, context.AllowFeatures, true);
                for (qsizetype i = 0; i < index.count(); ++i) {
                    const auto &entry = index.entries().at(i);
                    if (entry.IsType || (context.AllowFeatures && entry.IsFeature)) {
                        collector.add(entry, index.lowerName(i), true);
                    } else if (entry.IsNamespace && !entry.IsFeature && entry.Owner.isEmpty()) {
                        // A package to step into: the members follow after the two colons.
                        collector.add(entry, index.lowerName(i), true, true);
                    }
                }
                break;
            case ContextKind::Member: {
                for (const int owner : index.findNamespaces(context.Qualifier)) {
                    for (const int member : index.visibleMembers(index.entries().at(owner).QualifiedName)) {
                        const auto &entry = index.entries().at(member);
                        if (context.ExpectsType &&
                            !(entry.IsType || (entry.IsNamespace && !entry.IsFeature) || (context.AllowFeatures && entry.IsFeature)))
                            continue;
                        collector.add(entry, index.lowerName(member), false);
                    }
                }
                break;
            }
            case ContextKind::Import:
                if (context.Qualifier.isEmpty()) {
                    for (const int top : index.topLevel()) {
                        if (index.entries().at(top).IsNamespace)
                            collector.add(index.entries().at(top), index.lowerName(top), false);
                    }
                    for (const auto &local : localNames) {
                        if (local.Kind == CompletionKind::Package)
                            collector.add(itemOf(local, context), local.Name.toLower(), LocalTier);
                    }
                } else {
                    if (context.Prefix.isEmpty()) {
                        CompletionItem all;
                        all.Label = QStringLiteral("*");
                        all.InsertText = QStringLiteral("*");
                        all.Detail = QStringLiteral("all members");
                        all.Kind = CompletionKind::Other;
                        collector.add(all, QString(), SnippetTier);
                        CompletionItem recursive;
                        recursive.Label = QStringLiteral("**");
                        recursive.InsertText = QStringLiteral("**");
                        recursive.Detail = QStringLiteral("all members, recursive");
                        recursive.Kind = CompletionKind::Other;
                        collector.add(recursive, QStringLiteral("*"), SnippetTier);
                    }
                    for (const int owner : index.findNamespaces(context.Qualifier)) {
                        for (const int member : index.visibleMembers(index.entries().at(owner).QualifiedName)) {
                            const auto &entry = index.entries().at(member);
                            collector.add(entry, index.lowerName(member), false);
                        }
                    }
                }
                break;
            case ContextKind::Declaration:
                break;
        }
        return collector.result();
    }

    QList<CompletionItem> CompletionEngine::complete(const QString &text, int cursor, const QString &language, const NameIndex &index,
                                                     CompletionContext *context) {
        const CompletionContext analyzed = analyze(text, cursor);
        if (context != nullptr)
            *context = analyzed;
        if (!analyzed.Valid)
            return {};
        return complete(analyzed, language, index, scanLocalNames(text, cursor));
    }
}
