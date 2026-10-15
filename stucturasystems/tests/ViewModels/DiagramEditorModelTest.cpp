//
// DiagramEditorModel: code generation, writing into the document's block and the protection of hand edits.
// The document is a small fake that behaves like DocumentModel (one generated block, emits edited()).
//

#include "../TestFramework.h"
#include "QtPrinting.h"

#include <DiagramEditorModel.h>

#include <Diagram/Conversion/GeneratedBlockGuard.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <memory>
#include <optional>
#include <string>

using namespace StructuraSystems::Client;
using namespace StructuraSystems::Diagram;

namespace {
    /// Stands in for DocumentModel: holds the generated block and notifies edits like the real document.
    class FakeDocument : public QObject {
        Q_OBJECT

    public:
        std::optional<std::string> Block;
        int Writes = 0;

        void editByHand(const std::string &text) {
            Block = text;
            emit edited();
        }

    signals:
        void edited();
    };

    class FakeDocumentTarget : public SysMLBlockTarget {
    public:
        explicit FakeDocumentTarget(FakeDocument *document) :
                Document(document) {
        }

        std::optional<std::string> readGeneratedBlock() override {
            return Document ? Document->Block : std::nullopt;
        }

        void writeGeneratedBlock(const std::string &body) override {
            if (!Document)
                return;
            Document->Block = body;
            ++Document->Writes;
            emit Document->edited();
        }

    private:
        QPointer<FakeDocument> Document;
    };

    /// Waits (with a running event loop) until the regeneration timer fired.
    template<typename Condition>
    bool waitFor(Condition condition, int milliseconds = 2000) {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < milliseconds) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(5);
        }
        return condition();
    }

    void addPart(DiagramEditorModel &editor, qreal x = 200, qreal y = 200) {
        editor.controller()->addElement(DiagramTypes::ToolPart, x, y);
    }
}

namespace StructuraSystems::Client {
    // The application links DocumentBlockTarget.cpp; the tests bind the editor to the fake document.
    std::unique_ptr<SysMLBlockTarget> createDocumentBlockTarget(QObject *document, const QString &) {
        return std::make_unique<FakeDocumentTarget>(qobject_cast<FakeDocument *>(document));
    }
}

TEST_CASE("Editor: live mode writes the generated block into the document") {
    FakeDocument document;
    DiagramEditorModel editor;
    editor.setDocument(&document);
    CHECK(editor.blockState() == DiagramEditorModel::NoBlock);

    addPart(editor);
    CHECK(waitFor([&] { return document.Block.has_value(); }));
    CHECK(GeneratedBlockGuard::isGeneratedBlock(*document.Block));
    CHECK_EQ(*GeneratedBlockGuard::extractCode(*document.Block), editor.currentCode().toStdString());
    CHECK(editor.blockState() == DiagramEditorModel::InSync);
    CHECK(!editor.hasPendingChanges());
}

TEST_CASE("Editor: a hand edit pauses writing until the diagram is chosen") {
    FakeDocument document;
    DiagramEditorModel editor;
    editor.setDocument(&document);
    addPart(editor);
    CHECK(waitFor([&] { return document.Block.has_value(); }));

    const std::string handWritten = *document.Block + "\n// my note";
    document.editByHand(handWritten);
    CHECK(editor.blockState() == DiagramEditorModel::EditedByHand);

    const int writes = document.Writes;
    addPart(editor, 600, 200);
    CHECK(waitFor([&] { return editor.hasPendingChanges(); }));
    CHECK_EQ(document.Writes, writes);
    CHECK_EQ(*document.Block, handWritten); // not overwritten, not even in live mode

    editor.overwriteWithDiagram();
    CHECK(editor.blockState() == DiagramEditorModel::InSync);
    CHECK_EQ(*GeneratedBlockGuard::extractCode(*document.Block), editor.currentCode().toStdString());
}

TEST_CASE("Editor: on demand writes only with Generate SysML") {
    FakeDocument document;
    DiagramEditorModel editor;
    editor.setDocument(&document);
    editor.setGenerationMode(DiagramEditorModel::OnDemand);

    addPart(editor);
    CHECK(waitFor([&] { return !editor.currentCode().isEmpty(); }));
    CHECK(!document.Block.has_value());
    CHECK(editor.hasPendingChanges());

    editor.generateNow();
    CHECK(document.Block.has_value());
    CHECK(!editor.hasPendingChanges());
}

TEST_CASE("Editor: nothing is written while the document is busy") {
    FakeDocument document;
    DiagramEditorModel editor;
    editor.setDocument(&document);
    editor.setWriteLocked(true);

    addPart(editor);
    CHECK(waitFor([&] { return editor.hasPendingChanges(); }));
    CHECK(!document.Block.has_value());

    editor.setWriteLocked(false);
    CHECK(document.Block.has_value());
    CHECK(!editor.hasPendingChanges());
}

TEST_CASE("Editor: a generated block of an earlier session is loaded into the diagram") {
    FakeDocument document;
    document.Block = GeneratedBlockGuard::wrap("part earlier {\n    attribute mass;\n}\n");
    DiagramEditorModel editor;
    editor.setDocument(&document);
    CHECK(editor.blockState() == DiagramEditorModel::InSync);
    CHECK_EQ(editor.controller()->nodes()->count(), 1);
    CHECK_EQ(editor.hiddenCount(), 0);

    addPart(editor, 900, 200);
    CHECK(waitFor([&] { return document.Writes > 0; }));
    const std::string code = *GeneratedBlockGuard::extractCode(*document.Block);
    CHECK(code.find("part earlier {") != std::string::npos);
    CHECK(code.find("part part1;") != std::string::npos);
}

TEST_CASE("Editor: a hand-written block is shown but only replaced on request") {
    FakeDocument document;
    const std::string handWritten = "part def Sensor {\n    #measurable attribute value : Real;\n"
                                    "    assert constraint Range { value >= 0 }\n}\n";
    document.Block = handWritten;
    DiagramEditorModel editor;
    editor.setDocument(&document);
    CHECK(editor.blockState() == DiagramEditorModel::HandWritten);
    CHECK_EQ(editor.controller()->nodes()->count(), 1);
    CHECK_EQ(DiagramNodeModel::entryCount(editor.controller()->nodes()->nodes().front(), "attributes"), 1);
    CHECK_EQ(editor.hiddenCount(), 0); // metadata and constraint are part of the diagram now
    addPart(editor, 900, 200);
    CHECK(waitFor([&] { return editor.currentCode().contains("part1"); }));
    editor.generateNow();
    CHECK_EQ(*document.Block, handWritten); // not even "Generate SysML" overwrites it

    editor.overwriteWithDiagram();
    CHECK(GeneratedBlockGuard::isGeneratedBlock(*document.Block));
    CHECK(document.Block->find("part def Sensor") != std::string::npos);
    CHECK(document.Block->find("#measurable attribute value : Real;") != std::string::npos);
    CHECK(document.Block->find("assert constraint Range {") != std::string::npos);
    CHECK(editor.blockState() == DiagramEditorModel::InSync);
}

TEST_CASE("Editor: after a hand edit the text can be loaded into the diagram") {
    FakeDocument document;
    DiagramEditorModel editor;
    editor.setDocument(&document);
    addPart(editor);
    CHECK(waitFor([&] { return document.Block.has_value(); }));

    document.editByHand(*document.Block + "part added;\n");
    CHECK(editor.blockState() == DiagramEditorModel::EditedByHand);
    editor.reloadFromBlock();
    CHECK_EQ(editor.controller()->nodes()->count(), 2);
    CHECK(editor.currentCode().contains("part added;"));
    // the block is hand-edited (fingerprint differs): writing stays paused until the user decides
    CHECK(editor.blockState() == DiagramEditorModel::EditedByHand);
}

TEST_CASE("Editor: closing the document is reported") {
    auto document = std::make_unique<FakeDocument>();
    DiagramEditorModel editor;
    editor.setDocument(document.get());
    bool closed = false;
    QObject::connect(&editor, &DiagramEditorModel::documentClosed, [&] { closed = true; });

    document.reset();
    CHECK(closed);
    addPart(editor); // must not crash without document
    CHECK(waitFor([&] { return !editor.currentCode().isEmpty(); }));
}

TEST_CASE("Editor: moved nodes are written into the layout line and placed there again") {
    FakeDocument document;
    {
        DiagramEditorModel editor;
        editor.setDocument(&document);
        addPart(editor, 200, 200);
        addPart(editor, 600, 200);
        CHECK(waitFor([&] { return document.Block.has_value(); }));
        auto *controller = editor.controller();
        const QString first = QString::number(editor.semanticModel().children(RootId).front());
        controller->moveNode(first, 420, 380);
        controller->finishMove(first);
        CHECK(waitFor([&] { return GeneratedBlockGuard::extractLayout(*document.Block).find("\"part1\":[420,380]") != std::string::npos; }));
        // the code (and its fingerprint) did not change, the block is still the editor's
        CHECK_EQ(*GeneratedBlockGuard::extractCode(*document.Block), editor.currentCode().toStdString());
        CHECK(editor.blockState() == DiagramEditorModel::InSync);
    }
    // opened again: part1 where it was moved to, part2 where it was dropped
    DiagramEditorModel again;
    again.setDocument(&document);
    const auto &model = again.semanticModel();
    const QRectF first = again.controller()->nodeRect(QString::number(model.children(RootId).front()));
    CHECK(first.topLeft() == QPointF(420, 380));
    CHECK(again.blockState() == DiagramEditorModel::InSync);
    // "arrange": back to the automatic layout, the layout line goes with the next write
    again.controller()->layoutAll();
    again.generateNow();
    CHECK(GeneratedBlockGuard::extractLayout(*document.Block).empty());
}

#include "DiagramEditorModelTest.moc"
