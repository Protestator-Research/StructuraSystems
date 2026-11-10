//
// Tests of the code completion logic (context analysis, filtering, snippets, local names, name index). The logic works on plain
// strings, so no GUI is needed. In the texts of the tests '|' marks the cursor.
//

#include <QtTest>

#include "../src/ViewModels/CompletionEngine.h"
#include "../src/ViewModels/LanguageKeywords.h"

using namespace StructuraSystems::Client;

namespace {
    struct Marked {
        QString Text;
        int Cursor = 0;
    };

    Marked parse(const QString &marked) {
        Marked result;
        const auto bar = marked.indexOf(u'|');
        Q_ASSERT(bar >= 0);
        result.Text = marked;
        result.Text.remove(bar, 1);
        result.Cursor = static_cast<int>(bar);
        return result;
    }

    QString kindName(ContextKind kind) {
        switch (kind) {
            case ContextKind::Keyword: return QStringLiteral("keyword");
            case ContextKind::Type: return QStringLiteral("type");
            case ContextKind::Member: return QStringLiteral("member");
            case ContextKind::Import: return QStringLiteral("import");
            case ContextKind::Declaration: return QStringLiteral("declaration");
            case ContextKind::Anything: break;
        }
        return QStringLiteral("anything");
    }

    NameEntry entry(const QString &qualified, const QString &type, bool isType, bool isFeature, bool inDocument = false) {
        NameEntry result;
        result.QualifiedName = qualified;
        const auto separator = qualified.lastIndexOf(QStringLiteral("::"));
        result.Name = separator < 0 ? qualified : qualified.mid(separator + 2);
        result.Owner = separator < 0 ? QString() : qualified.left(separator);
        result.Type = type;
        result.IsType = isType;
        result.IsFeature = isFeature;
        result.IsNamespace = true;
        result.InDocument = inDocument;
        return result;
    }

    /** package P { part def A; part x; }, package ISQ { attribute def Mass; }, part def Alpha */
    NameIndex sampleIndex() {
        return NameIndex({entry("P", "Package", false, false, true),
                          entry("P::A", "PartDefinition", true, false, true),
                          entry("P::x", "PartUsage", false, true, true),
                          entry("ISQ", "LibraryPackage", false, false),
                          entry("ISQ::Mass", "AttributeDefinition", true, false),
                          entry("ISQ::my mass", "AttributeUsage", false, true),
                          entry("Alpha", "PartDefinition", true, false, true)});
    }

    QStringList labels(const QList<CompletionItem> &items) {
        QStringList result;
        for (const auto &item : items)
            result.append(item.Label);
        return result;
    }

    QList<CompletionItem> complete(const QString &marked, const NameIndex &index, const QString &language = QStringLiteral("SysMLv2")) {
        const auto parsed = parse(marked);
        return CompletionEngine::complete(parsed.Text, parsed.Cursor, language, index);
    }

    const CompletionItem *find(const QList<CompletionItem> &items, const QString &label, CompletionKind kind) {
        for (const auto &item : items) {
            if (item.Label == label && item.Kind == kind)
                return &item;
        }
        return nullptr;
    }
}

class CompletionEngineTests : public QObject {
    Q_OBJECT

private slots:
    void keywordsComeFromTheLexers() {
        QVERIFY(sysmlKeywords().contains(QStringLiteral("part")));
        QVERIFY(sysmlKeywords().contains(QStringLiteral("package")));
        QVERIFY(kermlKeywords().contains(QStringLiteral("package")));
        QVERIFY(kermlKeywords().contains(QStringLiteral("class")));
        // Different languages, different keywords.
        QVERIFY(sysmlKeywords() != kermlKeywords());
    }

    void analyze_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("prefix");
        QTest::addColumn<QString>("qualifier");
        QTest::addColumn<bool>("valid");

        QTest::newRow("empty text") << "|" << "keyword" << "" << "" << true;
        QTest::newRow("statement start") << "part def A {\n    pa|" << "keyword" << "pa" << "" << true;
        QTest::newRow("after semicolon") << "part a;pa|" << "keyword" << "pa" << "" << true;
        QTest::newRow("after closing brace") << "part def A {}\npa|" << "keyword" << "pa" << "" << true;
        QTest::newRow("after a comment") << "// pa\nx|" << "keyword" << "x" << "" << true;
        QTest::newRow("typed by") << "part x : |" << "type" << "" << "" << true;
        QTest::newRow("typed by with prefix") << "part x : Ve|" << "type" << "Ve" << "" << true;
        QTest::newRow("typed by without space") << "part x :|" << "type" << "" << "" << true;
        QTest::newRow("subsets") << "part x :> Ve|" << "type" << "Ve" << "" << true;
        QTest::newRow("redefines") << "part x :>> |" << "type" << "" << "" << true;
        QTest::newRow("specializes") << "part def A specializes B|" << "type" << "B" << "" << true;
        QTest::newRow("specializes list") << "part def A specializes B, C|" << "type" << "C" << "" << true;
        QTest::newRow("typed by words") << "part x typed by |" << "type" << "" << "" << true;
        QTest::newRow("conjugate operator") << "port p : ~|" << "type" << "" << "" << true;
        QTest::newRow("member of a package") << "P::|" << "member" << "" << "P" << true;
        QTest::newRow("member with prefix") << "x = P::Q::ma|" << "member" << "ma" << "P::Q" << true;
        QTest::newRow("member of a type") << "part x : Pkg::|" << "member" << "" << "Pkg" << true;
        QTest::newRow("quoted qualifier") << "x = 'my pkg'::a|" << "member" << "a" << "my pkg" << true;
        QTest::newRow("import") << "import |" << "import" << "" << "" << true;
        QTest::newRow("private import") << "private import IS|" << "import" << "IS" << "" << true;
        QTest::newRow("import all") << "import all |" << "import" << "" << "" << true;
        QTest::newRow("import member") << "import ISQ::|" << "import" << "" << "ISQ" << true;
        QTest::newRow("import nested member") << "import A::B::Ma|" << "import" << "Ma" << "A::B" << true;
        QTest::newRow("anything") << "part x = 1 + pa|" << "anything" << "pa" << "" << true;
        QTest::newRow("after a modifier") << "abstract pa|" << "anything" << "pa" << "" << true;
        QTest::newRow("name of a definition") << "part def Ve|" << "declaration" << "Ve" << "" << true;
        QTest::newRow("name of a package") << "package |" << "declaration" << "" << "" << true;
        QTest::newRow("line comment") << "part a; // some pa|" << "anything" << "" << "" << false;
        QTest::newRow("line comment start") << "//|" << "anything" << "" << "" << false;
        QTest::newRow("block comment") << "/* some\n pa| */" << "anything" << "" << "" << false;
        QTest::newRow("after a block comment") << "/* x */ pa|" << "keyword" << "pa" << "" << true;
        QTest::newRow("string") << "attribute s = \"ab|" << "anything" << "" << "" << false;
        QTest::newRow("after a string") << "attribute s = \"ab\"; pa|" << "keyword" << "pa" << "" << true;
        QTest::newRow("dot") << "x.|" << "anything" << "" << "" << false;
        QTest::newRow("number") << "x = 12|" << "anything" << "" << "" << false;
        QTest::newRow("quoted name") << "part 'my pa|" << "anything" << "my pa" << "" << true;
        QTest::newRow("quoted name after type operator") << "part x : 'my|" << "type" << "my" << "" << true;
    }

    void analyze() {
        QFETCH(QString, input);
        QFETCH(QString, kind);
        QFETCH(QString, prefix);
        QFETCH(QString, qualifier);
        QFETCH(bool, valid);
        const auto parsed = parse(input);
        const auto context = CompletionEngine::analyze(parsed.Text, parsed.Cursor);
        QCOMPARE(context.Valid, valid);
        if (!valid)
            return;
        QCOMPARE(kindName(context.Kind), kind);
        QCOMPARE(context.Prefix, prefix);
        QCOMPARE(context.Qualifier, qualifier);
        if (!context.InQuotedName)
            QCOMPARE(context.ReplaceStart, parsed.Cursor - static_cast<int>(prefix.size()));
        QCOMPARE(context.ReplaceEnd >= parsed.Cursor, true);
    }

    void analyzeDetails() {
        {
            const auto parsed = parse("part x :> |");
            const auto context = CompletionEngine::analyze(parsed.Text, parsed.Cursor);
            QVERIFY(context.ExpectsType);
            QVERIFY(context.AllowFeatures);
        }
        {
            const auto parsed = parse("part x : |");
            const auto context = CompletionEngine::analyze(parsed.Text, parsed.Cursor);
            QVERIFY(context.ExpectsType);
            QVERIFY(!context.AllowFeatures);
        }
        {
            const auto parsed = parse("part x : P::|");
            const auto context = CompletionEngine::analyze(parsed.Text, parsed.Cursor);
            QVERIFY(context.ExpectsType);
        }
        {
            // The quote is replaced, and the closing quote that follows is taken, too.
            const auto parsed = parse("part 'my pa|'");
            const auto context = CompletionEngine::analyze(parsed.Text, parsed.Cursor);
            QVERIFY(context.InQuotedName);
            QCOMPARE(context.ReplaceStart, 5);
            QCOMPARE(context.ReplaceEnd, parsed.Cursor + 1);
        }
        {
            const auto parsed = parse("    part a {\n        |");
            const auto context = CompletionEngine::analyze(parsed.Text, parsed.Cursor);
            QCOMPARE(context.Indent, QStringLiteral("        "));
        }
    }

    void keywordsAndSnippetsAreFiltered() {
        const auto items = complete("pa|", NameIndex());
        const auto names = labels(items);
        QVERIFY(names.contains(QStringLiteral("part")));
        QVERIFY(names.contains(QStringLiteral("part def")));
        QVERIFY(names.contains(QStringLiteral("package")));
        QVERIFY(find(items, QStringLiteral("part"), CompletionKind::Keyword) != nullptr);
        QVERIFY(find(items, QStringLiteral("part"), CompletionKind::Snippet) != nullptr);
        // Matches at the start come first, the snippets before the keywords, then alphabetical.
        QCOMPARE(items.first().Kind, CompletionKind::Snippet);
        QCOMPARE(items.first().Label, QStringLiteral("package"));
        QVERIFY(names.indexOf(QStringLiteral("part")) < names.indexOf(QStringLiteral("part def")));
        for (const auto &item : items) {
            if (item.Kind == CompletionKind::Keyword || item.Kind == CompletionKind::Snippet)
                QVERIFY2(item.Label.contains(QStringLiteral("pa"), Qt::CaseInsensitive), qPrintable(item.Label));
        }
    }

    void matchesAtTheStartComeBeforeMatchesInside() {
        const NameIndex index({entry("xmass", "PartUsage", false, true, true), entry("Mass", "PartDefinition", true, false, true),
                               entry("amassed", "PartDefinition", true, false, true)});
        const auto items = complete("part y :> ma|", index);
        QCOMPARE(labels(items), QStringList({"Mass", "amassed", "xmass"}));
    }

    void prefixIsCaseInsensitive() {
        const auto items = complete("PA|", NameIndex());
        QVERIFY(labels(items).contains(QStringLiteral("part")));
    }

    void snippetKeepsTheIndentation() {
        const auto items = complete("package P {\n    pa|", NameIndex());
        const auto *snippet = find(items, QStringLiteral("part def"), CompletionKind::Snippet);
        QVERIFY(snippet != nullptr);
        QCOMPARE(snippet->InsertText, QStringLiteral("part def Name {\n        \n    }"));
        // The placeholder is selected, so typing replaces it.
        QCOMPARE(snippet->CursorOffset, 9);
        QCOMPARE(snippet->SelectionLength, 4);
        QCOMPARE(snippet->InsertText.mid(snippet->CursorOffset, snippet->SelectionLength), QStringLiteral("Name"));
    }

    void snippetWithoutBody() {
        const auto items = complete("attribute|", NameIndex());
        const auto *snippet = find(items, QStringLiteral("attribute"), CompletionKind::Snippet);
        QVERIFY(snippet != nullptr);
        QCOMPARE(snippet->InsertText, QStringLiteral("attribute name : Type;"));
        QCOMPARE(snippet->InsertText.mid(snippet->CursorOffset, snippet->SelectionLength), QStringLiteral("name"));
    }

    void kermlHasItsOwnSnippets() {
        const auto kerml = complete("da|", NameIndex(), QStringLiteral("KerML"));
        QVERIFY(find(kerml, QStringLiteral("datatype"), CompletionKind::Snippet) != nullptr);
        const auto sysml = complete("da|", NameIndex(), QStringLiteral("SysMLv2"));
        QVERIFY(find(sysml, QStringLiteral("datatype"), CompletionKind::Snippet) == nullptr);

        const auto kermlAttribute = complete("attribute|", NameIndex(), QStringLiteral("KerML"));
        QVERIFY(find(kermlAttribute, QStringLiteral("attribute"), CompletionKind::Snippet) == nullptr);
        const auto kermlClass = complete("cla|", NameIndex(), QStringLiteral("KerML"));
        QVERIFY(find(kermlClass, QStringLiteral("class"), CompletionKind::Snippet) != nullptr);
        QVERIFY(find(kermlClass, QStringLiteral("class"), CompletionKind::Keyword) != nullptr);
    }

    void snippetsOnlyAtTheStartOfAStatement() {
        const auto items = complete("part x = pa|", NameIndex());
        QVERIFY(find(items, QStringLiteral("part"), CompletionKind::Snippet) == nullptr);
        QVERIFY(find(items, QStringLiteral("part"), CompletionKind::Keyword) != nullptr);
    }

    void afterColonOnlyTypes() {
        const auto items = complete("part y : |", sampleIndex());
        const auto names = labels(items);
        QVERIFY(names.contains(QStringLiteral("A")));
        QVERIFY(names.contains(QStringLiteral("Mass")));
        QVERIFY(names.contains(QStringLiteral("Alpha")));
        // Features and keywords are no types.
        QVERIFY(!names.contains(QStringLiteral("x")));
        QVERIFY(!names.contains(QStringLiteral("part")));
        // Packages can be stepped into.
        const auto *package = find(items, QStringLiteral("ISQ"), CompletionKind::Package);
        QVERIFY(package != nullptr);
        QCOMPARE(package->InsertText, QStringLiteral("ISQ::"));
        // The qualified name is inserted, so it resolves where the text is.
        const auto *type = find(items, QStringLiteral("Mass"), CompletionKind::Definition);
        QVERIFY(type != nullptr);
        QCOMPARE(type->InsertText, QStringLiteral("ISQ::Mass"));
        QCOMPARE(type->Detail, QStringLiteral("ISQ::Mass"));
        // The names of the document come before the ones of the libraries.
        QVERIFY(names.indexOf(QStringLiteral("Alpha")) < names.indexOf(QStringLiteral("Mass")));
    }

    void afterSubsetsFeaturesAreSuggestedToo() {
        const auto items = complete("part y :> |", sampleIndex());
        QVERIFY(labels(items).contains(QStringLiteral("x")));
        QVERIFY(labels(items).contains(QStringLiteral("A")));
    }

    void membersOfAQualifier() {
        const auto items = complete("P::|", sampleIndex());
        QCOMPARE(labels(items), QStringList({"A", "x"}));
        // Without the qualifier, which is already written.
        QCOMPARE(find(items, QStringLiteral("A"), CompletionKind::Definition)->InsertText, QStringLiteral("A"));
        QCOMPARE(find(items, QStringLiteral("x"), CompletionKind::Usage)->InsertText, QStringLiteral("x"));
    }

    void membersAfterColonAreTypes() {
        const auto items = complete("part y : P::|", sampleIndex());
        QCOMPARE(labels(items), QStringList({"A"}));
    }

    void membersWithPrefix() {
        const auto items = complete("x = ISQ::Ma|", sampleIndex());
        QCOMPARE(labels(items), QStringList({"Mass", "my mass"}));
        QCOMPARE(find(items, QStringLiteral("my mass"), CompletionKind::Usage)->InsertText, QStringLiteral("'my mass'"));
    }

    void relativeQualifier() {
        const NameIndex index({entry("Outer", "Package", false, false), entry("Outer::Inner", "Package", false, false),
                               entry("Outer::Inner::Deep", "PartDefinition", true, false)});
        QCOMPARE(labels(complete("Inner::|", index)), QStringList({"Deep"}));
        QCOMPARE(labels(complete("Outer::Inner::|", index)), QStringList({"Deep"}));
        QVERIFY(complete("Unknown::|", index).isEmpty());
    }

    void importOffersPackagesAndWildcards() {
        const auto packages = complete("import |", sampleIndex());
        const auto packageNames = labels(packages);
        QVERIFY(packageNames.contains(QStringLiteral("P")));
        QVERIFY(packageNames.contains(QStringLiteral("ISQ")));
        QVERIFY(!packageNames.contains(QStringLiteral("part")));

        const auto members = complete("import ISQ::|", sampleIndex());
        const auto memberNames = labels(members);
        QVERIFY(memberNames.contains(QStringLiteral("*")));
        QVERIFY(memberNames.contains(QStringLiteral("**")));
        QVERIFY(memberNames.contains(QStringLiteral("Mass")));
        QVERIFY(!memberNames.contains(QStringLiteral("A")));

        // The wildcards only when no letter was typed.
        QVERIFY(!labels(complete("import ISQ::Ma|", sampleIndex())).contains(QStringLiteral("*")));
    }

    void noSuggestionsInCommentsStringsAndDeclarations() {
        QVERIFY(complete("// pa|", sampleIndex()).isEmpty());
        QVERIFY(complete("/* pa| */", sampleIndex()).isEmpty());
        QVERIFY(complete("attribute s = \"pa|", sampleIndex()).isEmpty());
        QVERIFY(complete("part def Pa|", sampleIndex()).isEmpty());
        QVERIFY(complete("x.|", sampleIndex()).isEmpty());
    }

    void localNamesAreFound() {
        const auto parsed = parse("package P {\n    part def Vehicle;\n    attribute 'my mass';\n    part v : Veh|\n}");
        const auto names = CompletionEngine::scanLocalNames(parsed.Text, parsed.Cursor);
        QStringList found;
        for (const auto &name : names)
            found.append(name.Name);
        QCOMPARE(found, QStringList({"P", "Vehicle", "my mass", "v"}));
        QCOMPARE(names.at(0).Kind, CompletionKind::Package);
        QCOMPARE(names.at(1).Kind, CompletionKind::Definition);
        QCOMPARE(names.at(2).Kind, CompletionKind::Usage);
    }

    void localNamesIgnoreWhatIsNoDeclaration() {
        const auto parsed = parse("import ISQ::*;\n// part Hidden;\npart a :> Base;\nperform Lib::act;\npart def Ve|");
        const auto names = CompletionEngine::scanLocalNames(parsed.Text, parsed.Cursor);
        QStringList found;
        for (const auto &name : names)
            found.append(name.Name);
        // Not the imported package, not the comment, not the reference, not the word that is typed.
        QCOMPARE(found, QStringList({"a"}));
    }

    void localNamesAreSuggested() {
        const auto items = complete("part def Vehicle;\npart v : Veh|", NameIndex());
        const auto *vehicle = find(items, QStringLiteral("Vehicle"), CompletionKind::Definition);
        QVERIFY(vehicle != nullptr);
        QCOMPARE(vehicle->InsertText, QStringLiteral("Vehicle"));
        QCOMPARE(vehicle->Detail, QStringLiteral("local"));
        // A usage is no type.
        const auto usage = complete("part def Vehicle;\npart v;\npart w : |", NameIndex());
        QVERIFY(!labels(usage).contains(QStringLiteral("v")));
        QVERIFY(labels(usage).contains(QStringLiteral("Vehicle")));
    }

    void localAndModelNameAreOneItem() {
        const NameIndex index({entry("Vehicle", "PartDefinition", true, false, true)});
        const auto items = complete("part def Vehicle;\npart v : Veh|", index);
        int count = 0;
        for (const auto &item : items)
            count += item.Label == QStringLiteral("Vehicle") ? 1 : 0;
        QCOMPARE(count, 1);
    }

    void quotedNameContext() {
        const auto parsed = parse("part x :> 'my|");
        const auto context = CompletionEngine::analyze(parsed.Text, parsed.Cursor);
        const auto items = CompletionEngine::complete(context, QStringLiteral("SysMLv2"), sampleIndex());
        // Only names, and written with quotes; the quote that was typed is replaced.
        QCOMPARE(labels(items), QStringList({"my mass"}));
        QCOMPARE(items.first().InsertText, QStringLiteral("ISQ::'my mass'"));
        QCOMPARE(context.ReplaceStart, 10);
    }

    void writtenNames() {
        QCOMPARE(CompletionEngine::written(QStringLiteral("Mass")), QStringLiteral("Mass"));
        QCOMPARE(CompletionEngine::written(QStringLiteral("my mass")), QStringLiteral("'my mass'"));
        QCOMPARE(CompletionEngine::written(QStringLiteral("1st")), QStringLiteral("'1st'"));
        QCOMPARE(CompletionEngine::written(QStringLiteral("Mass"), true), QStringLiteral("'Mass'"));
        QCOMPARE(CompletionEngine::writtenQualified(QStringLiteral("ISQ::my mass")), QStringLiteral("ISQ::'my mass'"));
        // The names of the index are the names of the elements, without quotes (the text and the XMI library agree on that): an operator or
        // a name with a space is quoted once when it is written, and never twice.
        QCOMPARE(CompletionEngine::written(QStringLiteral("!=")), QStringLiteral("'!='"));
        QCOMPARE(CompletionEngine::written(QStringLiteral("a b")), QStringLiteral("'a b'"));
        // reserved words are no basic names
        QCOMPARE(CompletionEngine::written(QStringLiteral("not")), QStringLiteral("'not'"));
        QCOMPARE(CompletionEngine::written(QStringLiteral("action")), QStringLiteral("'action'"));
        QCOMPARE(CompletionEngine::written(QStringLiteral("notes")), QStringLiteral("notes"));
        QCOMPARE(CompletionEngine::writtenQualified(QStringLiteral("BaseFunctions::not")), QStringLiteral("BaseFunctions::'not'"));
        QCOMPARE(CompletionEngine::writtenQualified(QStringLiteral("ControlFunctions::if")), QStringLiteral("ControlFunctions::'if'"));
        QCOMPARE(CompletionEngine::written(QStringLiteral("it's")), QStringLiteral("'it\\'s'"));
        QCOMPARE(CompletionEngine::writtenQualified(QStringLiteral("DataFunctions::!=")), QStringLiteral("DataFunctions::'!='"));
        QCOMPARE(CompletionEngine::writtenQualified(QStringLiteral("'a b'::c")), QStringLiteral("'\\'a b\\''::c"));   // (a name that starts with a quote is a name)
    }

    void resultsAreLimited() {
        QList<NameEntry> entries;
        for (int i = 0; i < 200; ++i)
            entries.append(entry(QStringLiteral("Name%1").arg(i), QStringLiteral("PartDefinition"), true, false));
        const NameIndex index(entries);
        QCOMPARE(complete("part x : |", index).size(), CompletionEngine::MaxItems);
        QCOMPARE(complete("part x : Name19|", index).size(), 11);
    }

    void cursorInsideAQuotedNameReplacesTheWholeName() {
        // 'my m|ass' must not become 'my mass'ass'.
        const auto parsed = parse("part x : ISQ::'my m|ass';");
        const auto context = CompletionEngine::analyze(parsed.Text, parsed.Cursor);
        QVERIFY(context.InQuotedName);
        QCOMPARE(parsed.Text.mid(context.ReplaceStart, context.ReplaceEnd - context.ReplaceStart), QStringLiteral("'my mass'"));
    }

    void escapesInQuotedNamesAndStrings() {
        {
            // The escaped quote does not end the name: the cursor is still inside of it.
            const auto parsed = parse("part 'a\\'b|");
            const auto context = CompletionEngine::analyze(parsed.Text, parsed.Cursor);
            QVERIFY(context.Valid);
            QVERIFY(context.InQuotedName);
            QCOMPARE(context.ReplaceStart, 5);
        }
        {
            // Between the backslash and the escaped character of a string.
            const auto parsed = parse("doc \"a\\|\" ");
            QVERIFY(!CompletionEngine::analyze(parsed.Text, parsed.Cursor).Valid);
        }
    }

    void membersOfPublicImportsAreVisible() {
        auto front = entry("Front", "LibraryPackage", false, false);
        front.Exports = QStringList{QStringLiteral("Base")};
        auto base = entry("Base", "LibraryPackage", false, false);
        base.Exports = QStringList{QStringLiteral("Front")};   // a cycle must not hang
        const NameIndex index({front, entry("Front::Own", "AttributeDefinition", true, false), base,
                               entry("Base::MassValue", "AttributeDefinition", true, false)});
        QCOMPARE(index.visibleMembers(QStringLiteral("Front")).size(), 2);
        const auto names = labels(complete("part x : Front::|", index));
        QVERIFY(names.contains(QStringLiteral("Own")));
        QVERIFY(names.contains(QStringLiteral("MassValue")));
    }

    void onlyTheShownItemsAreSortedButTheOrderIsKept() {
        // Many matches: the head of the list is still the alphabetical start, documents names before library names.
        QList<NameEntry> entries;
        for (int i = 999; i >= 0; --i)
            entries.append(entry(QStringLiteral("Lib::N%1").arg(i, 4, 10, QLatin1Char('0')), QStringLiteral("PartDefinition"), true, false));
        entries.append(entry(QStringLiteral("Doc::N9999"), QStringLiteral("PartDefinition"), true, false, true));
        // The same name twice is one item, even when it is not among the first candidates.
        entries.append(entry(QStringLiteral("Lib::N0000"), QStringLiteral("PartDefinition"), true, false));
        const auto items = complete("part x : N|", NameIndex(entries));
        QCOMPARE(items.size(), CompletionEngine::MaxItems);
        QCOMPARE(items.at(0).InsertText, QStringLiteral("Doc::N9999"));
        QCOMPARE(items.at(1).InsertText, QStringLiteral("Lib::N0000"));
        QCOMPARE(items.at(2).InsertText, QStringLiteral("Lib::N0001"));
        QCOMPARE(items.last().InsertText, QStringLiteral("Lib::N0048"));
    }

    void nameIndexLookups() {
        const auto index = sampleIndex();
        QCOMPARE(index.topLevel().size(), 3);
        QCOMPARE(index.members(QStringLiteral("P")).size(), 2);
        QCOMPARE(index.members(QStringLiteral("Unknown")).size(), 0);
        QCOMPARE(index.types().size(), 3);
        QCOMPARE(index.findNamespaces(QStringLiteral("ISQ")).size(), 1);
        QCOMPARE(index.lowerName(0), QStringLiteral("p"));
    }
};

QTEST_APPLESS_MAIN(CompletionEngineTests)

#include "tst_CompletionEngine.moc"
