//
// A NameIndex in two stages (the libraries, shared, and the document on top) has to answer exactly like the flat index over the same
// entries: the same entries and indices, members, types, namespaces, findNamespaces, visibleMembers and completions.
//

#include <memory>

#include <QtTest>

#include "../src/ViewModels/CompletionEngine.h"

using namespace StructuraSystems::Client;

namespace {
    NameEntry entry(const QString &qualified, const QString &type, bool isType, bool isFeature, bool isNamespace, bool inDocument,
                    const QStringList &exports = {}) {
        NameEntry result;
        result.QualifiedName = qualified;
        const auto separator = qualified.lastIndexOf(QStringLiteral("::"));
        result.Name = separator < 0 ? qualified : qualified.mid(separator + 2);
        result.Owner = separator < 0 ? QString() : qualified.left(separator);
        result.Type = type;
        result.IsType = isType;
        result.IsFeature = isFeature;
        result.IsNamespace = isNamespace;
        result.InDocument = inDocument;
        result.Exports = exports;
        return result;
    }

    /** Library: ISQBase (Mass), ISQ (re-exports ISQBase), Parts. Document: its own package P with members, a package ISQ that extends the library's. */
    QList<NameEntry> libraryEntries() {
        return {entry("ISQBase", "LibraryPackage", false, false, true, false),
                entry("ISQBase::MassValue", "AttributeDefinition", true, false, true, false),
                entry("ISQBase::LengthValue", "AttributeDefinition", true, false, true, false),
                entry("ISQ", "LibraryPackage", false, false, true, false, {"ISQBase"}),
                entry("ISQ::mass", "AttributeUsage", false, true, true, false),
                entry("Parts", "LibraryPackage", false, false, true, false),
                entry("Parts::Part", "PartDefinition", true, false, true, false),
                entry("Parts::parts", "PartUsage", false, true, true, false)};
    }

    QList<NameEntry> documentEntries() {
        return {entry("P", "Package", false, false, true, true),
                entry("P::A", "PartDefinition", true, false, true, true),
                entry("P::x", "PartUsage", false, true, true, true),
                entry("P::Inner", "Package", false, false, true, true),
                entry("P::Inner::B", "PartDefinition", true, false, true, true),
                entry("ISQ", "Package", false, false, true, true, {"P"}),
                entry("ISQ::extra", "AttributeUsage", false, true, true, true),
                entry("Parts::Mine", "PartDefinition", true, false, true, true)};
    }

    QStringList describe(const QList<NameEntry> &entries) {
        QStringList lines;
        for (const auto &e: entries)
            lines << e.QualifiedName + "|" + e.Type + "|" + QString::number(e.IsType) + QString::number(e.IsFeature) + QString::number(e.IsNamespace) +
                         QString::number(e.InDocument) + "|" + e.Exports.join(',');
        return lines;
    }

    QStringList describe(const NameIndex &index) {
        QStringList lines;
        for (qsizetype i = 0; i < index.count(); ++i) {
            const auto &e = index.entries().at(i);
            lines << e.QualifiedName + "|" + e.Type + "|" + QString::number(e.IsType) + QString::number(e.IsFeature) + QString::number(e.IsNamespace) +
                         QString::number(e.InDocument) + "|" + e.Exports.join(',');
        }
        return lines;
    }
}

class NameIndexStageTests : public QObject {
    Q_OBJECT

private slots:
    void aTwoStageIndexAnswersLikeTheFlatIndex() {
        const auto library = libraryEntries();
        const auto document = documentEntries();
        QList<NameEntry> all = library;
        all.append(document);
        const NameIndex flat(all);
        const NameIndex staged(std::make_shared<const NameIndex>(library), document);

        QCOMPARE(staged.count(), flat.count());
        QCOMPARE(staged.isEmpty(), flat.isEmpty());
        QCOMPARE(describe(staged), describe(flat));
        QCOMPARE(describe(staged), describe(all));
        QCOMPARE(staged.entries().size(), flat.entries().size());
        QStringList viaRangeFor;
        for (const auto &e: staged.entries())
            viaRangeFor << e.QualifiedName;
        QStringList expected;
        for (const auto &e: all)
            expected << e.QualifiedName;
        QCOMPARE(viaRangeFor, expected);
        for (qsizetype i = 0; i < flat.count(); ++i)
            QCOMPARE(staged.lowerName(i), flat.lowerName(i));
        QCOMPARE(staged.types(), flat.types());
        QCOMPARE(staged.namespaces(), flat.namespaces());
        QCOMPARE(staged.topLevel(), flat.topLevel());
        QStringList owners{"", "ISQ", "ISQBase", "Parts", "P", "P::Inner", "Nothing"};
        for (const auto &owner: owners) {
            QCOMPARE(staged.members(owner), flat.members(owner));
            QCOMPARE(staged.visibleMembers(owner), flat.visibleMembers(owner));
        }
        // (the library's list of members of ISQ has a member of the library first and one of the document last)
        QCOMPARE(staged.members("ISQ").size(), 2);
        QStringList qualifiers{"ISQ", "ISQBase", "Inner", "P::Inner", "P", "Parts", "Part", "Missing", "ISQ::mass"};
        for (const auto &qualifier: qualifiers)
            QCOMPARE(staged.findNamespaces(qualifier), flat.findNamespaces(qualifier));
    }

    void theBaseIsNotCopiedAndStaysUntouched() {
        const auto base = std::make_shared<const NameIndex>(libraryEntries());
        const auto countBefore = base->count();
        const auto membersBefore = base->members("ISQ");
        const NameIndex first(base, documentEntries());
        const NameIndex second(base, QList<NameEntry>{entry("Q", "Package", false, false, true, true)});
        QCOMPARE(base->count(), countBefore);
        QCOMPARE(base->members("ISQ"), membersBefore);
        QCOMPARE(first.count(), countBefore + documentEntries().size());
        QCOMPARE(second.count(), countBefore + 1);
        QCOMPARE(second.members("ISQ"), membersBefore);   // (an owner that the document does not have: the base's list)
        QCOMPARE(second.entries().at(countBefore).QualifiedName, QString("Q"));
        // A stage over an empty document and over an empty base
        const NameIndex noDocument(base, QList<NameEntry>{});
        QCOMPARE(describe(noDocument), describe(*base));
        const NameIndex noBase(nullptr, documentEntries());
        QCOMPARE(describe(noBase), describe(documentEntries()));
    }

    void completionsAreTheSame() {
        const auto library = libraryEntries();
        const auto document = documentEntries();
        QList<NameEntry> all = library;
        all.append(document);
        const NameIndex flat(all);
        const NameIndex staged(std::make_shared<const NameIndex>(library), document);
        const QStringList texts{"package Q { import |", "package Q { import ISQ::|", "package Q { part x : |", "package Q { part x : ISQ::|",
                                "package Q { part x : Pa|", "package Q { attribute a : ISQBase::M|", "package Q { part x :> P::|",
                                "pa|", "package Q { import P::Inner::|"};
        for (const auto &marked: texts) {
            QString text = marked;
            const int cursor = text.indexOf('|');
            text.remove(cursor, 1);
            for (const QString &language: {QString("SysMLv2"), QString("KerML")}) {
                const auto a = CompletionEngine::complete(text, cursor, language, staged);
                const auto b = CompletionEngine::complete(text, cursor, language, flat);
                QCOMPARE(a.size(), b.size());
                for (qsizetype i = 0; i < a.size(); ++i) {
                    QCOMPARE(a[i].Label, b[i].Label);
                    QCOMPARE(a[i].InsertText, b[i].InsertText);
                    QCOMPARE(a[i].Detail, b[i].Detail);
                    QCOMPARE(static_cast<int>(a[i].Kind), static_cast<int>(b[i].Kind));
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(NameIndexStageTests)

#include "tst_NameIndexStages.moc"
