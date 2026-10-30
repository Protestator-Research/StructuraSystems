//
// Tests of the editing help of the code editor (indentation, bracket pairs, tab handling). The text logic works on plain strings,
// so no GUI is needed. In the texts of the tests '|' marks the cursor and the guillemets mark a selection.
//

#include <QtTest>

#include "../src/ViewModels/CodeEditAssist.h"

using namespace StructuraSystems::Client;

namespace {
    struct Marked {
        QString Text;
        int Start = 0;
        int End = 0;
    };

    Marked parse(const QString &marked) {
        Marked result;
        bool startFound = false;
        for (const QChar character : marked) {
            if (character == u'|') {
                result.Start = result.End = static_cast<int>(result.Text.size());
                startFound = true;
            } else if (character == QChar(0x00AB)) {
                result.Start = static_cast<int>(result.Text.size());
                startFound = true;
            } else if (character == QChar(0x00BB)) {
                result.End = static_cast<int>(result.Text.size());
            } else {
                result.Text.append(character);
            }
        }
        Q_ASSERT(startFound);
        return result;
    }

    /** Applies the edit and writes the result with the cursor or selection markers. */
    QString apply(const Marked &input, const CodeEdit &edit) {
        if (!edit.Handled)
            return QStringLiteral("unhandled");
        QString text = input.Text;
        text.replace(edit.RemoveStart, edit.RemoveEnd - edit.RemoveStart, edit.Insert);
        if (edit.SelectionStart >= 0) {
            text.insert(edit.SelectionEnd, QChar(0x00BB));
            text.insert(edit.SelectionStart, QChar(0x00AB));
        } else {
            text.insert(edit.Cursor, u'|');
        }
        return text;
    }

    using Function = CodeEdit (*)(const QString &, int, int);

    void check(Function function, const QString &input, const QString &expected) {
        Marked marked = parse(input);
        QCOMPARE(apply(marked, function(marked.Text, marked.Start, marked.End)), expected);
    }

    CodeEdit newline(const QString &t, int s, int e) { return CodeEditAssist::editNewline(t, s, e); }
    CodeEdit backspace(const QString &t, int s, int e) { return CodeEditAssist::editBackspace(t, s, e); }
    CodeEdit indent(const QString &t, int s, int e) { return CodeEditAssist::editIndent(t, s, e); }
    CodeEdit outdent(const QString &t, int s, int e) { return CodeEditAssist::editOutdent(t, s, e); }
    CodeEdit open(const QString &t, int s, int e) { return CodeEditAssist::editOpenBracket(t, s, e, u'{'); }
    CodeEdit closeBrace(const QString &t, int s, int e) { return CodeEditAssist::editCloseBracket(t, s, e, u'}'); }
    CodeEdit closeParenthesis(const QString &t, int s, int e) { return CodeEditAssist::editCloseBracket(t, s, e, u')'); }
}

class CodeEditAssistTests : public QObject {
    Q_OBJECT

private slots:
    void newline_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty braces") << "part def A {|}" << "part def A {\n    |\n}";
        QTest::newRow("empty braces with blanks") << "part def A { | }" << "part def A { \n    |\n}";
        QTest::newRow("nested braces") << "    part a {|}" << "    part a {\n        |\n    }";
        QTest::newRow("after opening brace") << "part def A {|" << "part def A {\n    |";
        QTest::newRow("after opening brace with text behind") << "part def A {|x" << "part def A {\n    |x";
        QTest::newRow("keeps indentation") << "    attribute x;|" << "    attribute x;\n    |";
        QTest::newRow("keeps tabs") << "\tattribute x;|" << "\tattribute x;\n\t|";
        QTest::newRow("no indentation") << "attribute x;|" << "attribute x;\n|";
        QTest::newRow("drops blanks right of cursor") << "    a;|   b" << "    a;\n    |b";
        QTest::newRow("cursor in indentation") << "    |a;" << "    \n    |a;";
        QTest::newRow("empty text") << "|" << "\n|";
        QTest::newRow("before closing brace") << "    a;|}" << "    a;\n    |}";
        QTest::newRow("selection replaced") << "x «abc» y" << "x \n|y";
        QTest::newRow("parenthesis pair") << "f(|)" << "f(\n    |\n)";
        QTest::newRow("brace of other kind") << "f({|)" << "f({\n    |)";
    }

    void newline() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        check(::newline, input, expected);
    }

    void openBracket_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("end of text") << "part def A |" << "part def A {|}";
        QTest::newRow("before line end") << "a |\nb" << "a {|}\nb";
        QTest::newRow("before blank") << "a | b" << "a {|} b";
        QTest::newRow("before closing bracket") << "(|)" << "({|})";
        QTest::newRow("before semicolon") << "a |;" << "a {|};";
        QTest::newRow("before word") << "|word" << "unhandled";
        QTest::newRow("wraps selection") << "a «b» c" << "a {«b»} c";
        QTest::newRow("wraps multi line selection") << "«a\nb»" << "{«a\nb»}";
    }

    void openBracket() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        check(open, input, expected);
    }

    void closeBracket_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("steps over brace") << "{a|}" << "{a}|";
        QTest::newRow("moves lone brace out") << "{\n    |" << "{\n}|";
        QTest::newRow("moves nested brace out") << "{\n        |" << "{\n    }|";
        QTest::newRow("moves brace out of partial indentation") << "{\n  |" << "{\n}|";
        QTest::newRow("brace after text") << "a |" << "unhandled";
        QTest::newRow("brace at line start") << "{\n|" << "unhandled";
        QTest::newRow("tab indentation") << "{\n\t|" << "{\n}|";
        QTest::newRow("tab and spaces indentation") << "{\n\t  |" << "{\n\t}|";
        QTest::newRow("selection") << "«a»" << "unhandled";
    }

    void closeBracket() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        check(closeBrace, input, expected);
    }

    void closeParenthesis() {
        check(::closeParenthesis, "(a|)", "(a)|");
        check(::closeParenthesis, "    |", "unhandled");
        check(::closeParenthesis, "(a|", "unhandled");
    }

    void backspace_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty braces") << "a {|}" << "a |";
        QTest::newRow("empty parentheses") << "f(|)" << "f|";
        QTest::newRow("empty brackets") << "x[|]" << "x|";
        QTest::newRow("mismatching pair") << "{|)" << "unhandled";
        QTest::newRow("one level of indentation") << "        |" << "    |";
        QTest::newRow("partial level") << "      |" << "    |";
        QTest::newRow("single blank") << " |" << "|";
        QTest::newRow("indentation before text") << "        |a" << "    |a";
        QTest::newRow("after text") << "a |" << "unhandled";
        QTest::newRow("normal character") << "ab|" << "unhandled";
        QTest::newRow("line start") << "a\n|" << "unhandled";
        QTest::newRow("text start") << "|a" << "unhandled";
        QTest::newRow("selection") << "«ab»" << "unhandled";
        QTest::newRow("tab indentation") << "\t|" << "unhandled";
    }

    void backspace() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        check(::backspace, input, expected);
    }

    void indent_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("text start") << "|" << "    |";
        QTest::newRow("to next stop") << "ab|" << "ab  |";
        QTest::newRow("on a stop") << "abcd|" << "abcd    |";
        QTest::newRow("in second line") << "a\nbc|" << "a\nbc  |";
        QTest::newRow("single line selection") << "a «bc» d" << "a   | d";
        QTest::newRow("multi line") << "«a\nb»" << "«    a\n    b»";
        QTest::newRow("multi line from the middle") << "x«y\nz»w" << "    x«y\n    z»w";
        QTest::newRow("multi line keeps empty lines") << "«a\n\nb»" << "«    a\n\n    b»";
        QTest::newRow("selection ends at line start") << "«a\nb\n»c" << "«    a\n    b\n»c";
        QTest::newRow("existing indentation") << "«  a\n  b»" << "«      a\n      b»";
    }

    void indent() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        check(::indent, input, expected);
    }

    void outdent_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("current line") << "        a|" << "    a|";
        QTest::newRow("cursor in indentation") << "    |a" << "|a";
        QTest::newRow("partial indentation") << "  a|" << "a|";
        QTest::newRow("tab") << "\ta|" << "a|";
        QTest::newRow("nothing to remove") << "a|" << "a|";
        QTest::newRow("nothing to remove keeps selection") << "«a\nb»" << "«a\nb»";
        QTest::newRow("nothing to remove keeps single line selection") << "x«ab»y" << "x«ab»y";        QTest::newRow("multi line") << "«    a\n        b»" << "«a\n    b»";
        QTest::newRow("multi line with unindented line") << "«a\n    b»" << "«a\nb»";
        QTest::newRow("selection ends at line start") << "«    a\n    b\n»    c" << "«a\nb\n»    c";
        QTest::newRow("only second line") << "x\n    y|" << "x\ny|";
    }

    void outdent() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        check(::outdent, input, expected);
    }

    void outdentClampsCursor() {
        // A position behind the text end must not move the cursor out of the text.
        QCOMPARE(CodeEditAssist::editOutdent(QStringLiteral("a"), 5, 5).Cursor, 1);
    }

    void variantMap() {
        QVariantMap map = CodeEditAssist().newline(QStringLiteral("{}"), 1, 1);
        QCOMPARE(map.value("handled").toBool(), true);
        QCOMPARE(map.value("removeStart").toInt(), 1);
        QCOMPARE(map.value("removeEnd").toInt(), 1);
        QCOMPARE(map.value("insert").toString(), QStringLiteral("\n    \n"));
        QCOMPARE(map.value("cursor").toInt(), 6);
        QCOMPARE(map.value("selectionStart").toInt(), -1);
        QCOMPARE(CodeEditAssist().openBracket(QStringLiteral("a"), 1, 1, QStringLiteral("(")).value("insert").toString(), QStringLiteral("()"));
    }
};

QTEST_APPLESS_MAIN(CodeEditAssistTests)

#include "tst_CodeEditAssist.moc"
