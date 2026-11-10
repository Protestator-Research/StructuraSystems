//
// Created by Moritz Herzog on 30.10.26.
//

#ifndef STRUCTURASYSTEMS_COMPLETIONENGINE_H
#define STRUCTURASYSTEMS_COMPLETIONENGINE_H

#include <iterator>
#include <memory>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace StructuraSystems::Client {
    /** What a completion item stands for (decides the icon of the popup). */
    enum class CompletionKind {
        Keyword,
        Snippet,
        Definition,
        Usage,
        Package,
        Other
    };

    /** One suggestion of the completion popup. */
    struct CompletionItem {
        QString Label;
        QString InsertText;
        QString Detail;
        CompletionKind Kind = CompletionKind::Other;
        /** Cursor position inside InsertText after the insertion, -1 for the end of the text. */
        int CursorOffset = -1;
        /** Number of characters from CursorOffset on that are selected (placeholder of a snippet), so typing replaces them. */
        int SelectionLength = 0;
    };

    /** What kind of word is expected at the cursor. */
    enum class ContextKind {
        Keyword,        ///< Start of a statement: keywords, snippets and names
        Type,           ///< After a colon, a specialization operator or 'specializes' and the like: names of types
        Member,         ///< After 'A::': the members of A
        Import,         ///< After 'import': packages and the members of a qualifier (plus the star wildcards)
        Declaration,    ///< The name of a new element is written (after 'def' or 'package'): nothing to suggest
        Anything        ///< Everywhere else: keywords and names
    };

    /** Result of the analysis of the text left of the cursor, see CompletionEngine::analyze(). */
    struct CompletionContext {
        ContextKind Kind = ContextKind::Anything;
        /** False inside of comments and strings (and after a dot): no popup there. */
        bool Valid = true;
        /** The letters left of the cursor that are completed. In a quoted name the text after the quote. */
        QString Prefix;
        /** The names in front of the prefix: `A::B` for `A::B::pr`, written without quotes. */
        QString Qualifier;
        /** The range [ReplaceStart, ReplaceEnd) of the text is replaced by the accepted item. */
        int ReplaceStart = 0;
        int ReplaceEnd = 0;
        /** The prefix lies in a quoted name: names only, and they are inserted quoted. */
        bool InQuotedName = false;
        /** Type or Member context after a colon, a specialization operator ...: only types (and namespaces to step into). */
        bool ExpectsType = false;
        /** With ExpectsType: features are valid, too (subsets, redefines ...). */
        bool AllowFeatures = false;
        /** Whitespace at the start of the line of the cursor: continues the lines of a snippet. */
        QString Indent;
    };

    /** A named element of the model. Plain data. */
    struct NameEntry {
        QString Name;
        QString QualifiedName;
        QString Owner;           ///< Qualified name of the owner, empty at the top level
        QString Type;            ///< Name of the metaclass, e.g. PartDefinition
        bool IsType = false;     ///< A classifier: definitions, classes, data types ...
        bool IsFeature = false;  ///< A feature: usages, attributes ...
        bool IsNamespace = false;
        bool InDocument = false; ///< Defined in the text of the document, not in a library
        /** Qualified names of the namespaces whose members this one shows, too (`public import ISQBase::*;`). */
        QStringList Exports;
    };

    /**
     * Snapshot of the names of a model (standard library, libraries, document) for the completion. Plain QStrings, so it
     * can be built on a worker thread and read on the GUI thread; it never changes after it was built and is shared as
     * std::shared_ptr<const NameIndex>.
     */
    class NameIndex {
    public:
        /**
         * Read only view of the entries (the indices of members(), types() ... refer to it). An index can be in two stages, see the
         * second constructor, so this is not a QList; it offers what the callers use: at(), size(), isEmpty() and range for.
         */
        class EntryList {
        public:
            class const_iterator {
            public:
                using iterator_category = std::forward_iterator_tag;
                using value_type = NameEntry;
                using difference_type = qsizetype;
                using pointer = const NameEntry *;
                using reference = const NameEntry &;
                const_iterator(const NameIndex *owner, qsizetype position) : Owner(owner), Position(position) {}
                reference operator*() const { return Owner->entry(Position); }
                pointer operator->() const { return &Owner->entry(Position); }
                const_iterator &operator++() { ++Position; return *this; }
                const_iterator operator++(int) { const_iterator copy = *this; ++Position; return copy; }
                bool operator==(const const_iterator &other) const { return Position == other.Position; }
                bool operator!=(const const_iterator &other) const { return Position != other.Position; }
            private:
                const NameIndex *Owner;
                qsizetype Position;
            };

            explicit EntryList(const NameIndex *owner) : Owner(owner) {}
            [[nodiscard]] const NameEntry &at(qsizetype index) const { return Owner->entry(index); }
            [[nodiscard]] const NameEntry &operator[](qsizetype index) const { return Owner->entry(index); }
            [[nodiscard]] qsizetype size() const { return Owner->count(); }
            [[nodiscard]] bool isEmpty() const { return Owner->isEmpty(); }
            [[nodiscard]] const_iterator begin() const { return {Owner, 0}; }
            [[nodiscard]] const_iterator end() const { return {Owner, Owner->count()}; }
        private:
            const NameIndex *Owner;
        };

        NameIndex() = default;
        explicit NameIndex(QList<NameEntry> entries);
        /**
         * An index in two stages: the entries of @p base (shared, never copied; typically the standard library and the libraries, which
         * stay the same for many documents) come first, then @p entries (the document). Everything answers as the flat index over both
         * lists in this order would, indices included.
         */
        NameIndex(std::shared_ptr<const NameIndex> base, QList<NameEntry> entries);

        [[nodiscard]] EntryList entries() const { return EntryList(this); }
        [[nodiscard]] qsizetype count() const { return BaseCount + Entries.size(); }
        [[nodiscard]] bool isEmpty() const { return count() == 0; }
        /** Lower case name of the entry, for the case insensitive matching. */
        [[nodiscard]] const QString &lowerName(qsizetype index) const {
            return index < BaseCount ? Base->lowerName(index) : Lower.at(index - BaseCount);
        }

        /** Indices of the entries that are owned by the namespace with the given qualified name. */
        [[nodiscard]] const QList<int> &members(const QString &owner) const;
        /** Indices of the entries without owner. */
        [[nodiscard]] const QList<int> &topLevel() const { return members(QString()); }
        /** Indices of all classifiers. */
        [[nodiscard]] const QList<int> &types() const { return Types; }
        /** Indices of all namespaces (packages and everything that can own elements). */
        [[nodiscard]] const QList<int> &namespaces() const { return Namespaces; }
        /**
         * The namespaces a qualifier written in the text can stand for: the ones with exactly that qualified name, else the
         * ones that end with it (the qualifier may be relative). Indices, one per qualified name.
         */
        [[nodiscard]] QList<int> findNamespaces(const QString &qualifier) const;
        /**
         * The members that can be written after `owner::`: the owned ones and those of the namespaces it imports publicly
         * (transitively, e.g. ISQ::mass comes from ISQBase).
         */
        [[nodiscard]] QList<int> visibleMembers(const QString &owner) const;

    private:
        [[nodiscard]] const NameEntry &entry(qsizetype index) const { return index < BaseCount ? Base->entry(index) : Entries.at(index - BaseCount); }
        /** Indices of the entries with this qualified name, the base's first. */
        [[nodiscard]] QList<int> withQualifiedName(const QString &qualifiedName) const;

        std::shared_ptr<const NameIndex> Base;   ///< First stage, null for a flat index
        qsizetype BaseCount = 0;
        QList<NameEntry> Entries;                ///< This stage; its indices start at BaseCount
        QStringList Lower;                       ///< of this stage
        /// The owners of this stage: the indices of their members in both stages. (An owner that only the base has is answered by the base.)
        QHash<QString, QList<int>> ByOwner;
        QHash<QString, QList<int>> ByQualifiedName;   ///< of this stage, indices in both stages
        QList<int> Types;        ///< both stages
        QList<int> Namespaces;   ///< both stages
    };

    /** A name that the text of the editor declares itself (found without parsing), see CompletionEngine::scanLocalNames(). */
    struct LocalName {
        QString Name;
        CompletionKind Kind = CompletionKind::Usage;
    };

    /**
     * Pure logic of the code completion for SysMLv2 and KerML: finds out what is expected at the cursor and filters keywords,
     * snippets and names for it. No QML involved, so it is tested with plain strings.
     */
    class CompletionEngine {
    public:
        static constexpr int MaxItems = 50;

        /**
         * Looks backwards from the cursor (comments and strings are skipped, a cursor inside one makes the context invalid).
         * @param cursor position in the text, 0..length.
         */
        static CompletionContext analyze(const QString &text, int cursor);

        /**
         * Names that are declared in the text (`part def X`, `package X`, `attribute x` ...), found by a simple scan. The word
         * that contains the cursor is not a result. Supplements the model, which only knows the text of the last parse.
         */
        static QList<LocalName> scanLocalNames(const QString &text, int cursor);

        /**
         * The suggestions for the context: filtered by the prefix (case insensitive, matches at the start before matches inside
         * of a name), then snippets, keywords, local names and names of the model in this order, alphabetical; at most MaxItems.
         * @param language "KerML" or anything else for SysMLv2.
         */
        static QList<CompletionItem> complete(const CompletionContext &context, const QString &language, const NameIndex &index,
                                              const QList<LocalName> &localNames = {});

        /** Convenience: analyze + scanLocalNames + complete. The context is returned through @p context if given. */
        static QList<CompletionItem> complete(const QString &text, int cursor, const QString &language, const NameIndex &index,
                                              CompletionContext *context = nullptr);

        /** The name as it is written in the text: quoted if it is not a plain identifier or if @p force is set. */
        static QString written(const QString &name, bool force = false);
        /** A qualified name (segments separated by two colons) as it is written in the text. */
        static QString writtenQualified(const QString &qualifiedName, bool forceLast = false);
    };
}

#endif //STRUCTURASYSTEMS_COMPLETIONENGINE_H
