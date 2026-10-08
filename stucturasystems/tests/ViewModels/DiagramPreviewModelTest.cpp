//
// DiagramPreviewModel: read-only diagram of one text block.
//

#include "../TestFramework.h"
#include "QtPrinting.h"

#include <DiagramPreviewModel.h>

#include <Diagram/Conversion/GeneratedBlockGuard.h>

using namespace StructuraSystems::Client;
using namespace StructuraSystems::Diagram;

TEST_CASE("Preview: shows the parts of a block including calculations") {
    DiagramPreviewModel preview;
    preview.setSource("part def ENGINE_LOAD {\n    #measurable attribute VariableA : Integer;\n"
                      "    function decodeEngineLoad { in feature varA : Integer; }\n}\n");
    preview.rebuild();
    CHECK_EQ(preview.elementCount(), 4); // part def, attribute, function and its parameter
    CHECK_EQ(preview.controller()->nodes()->count(), 1);
    CHECK_EQ(preview.hiddenCount(), 0);
    CHECK_EQ(DiagramNodeModel::entryCount(preview.controller()->nodes()->nodes().front(), "calcs"), 1);
}

TEST_CASE("Preview: the marker line of a generated block is not counted") {
    DiagramPreviewModel preview;
    preview.setSource(QString::fromStdString(GeneratedBlockGuard::wrap("part a;\npart b;\nconnect a to b;\n")));
    preview.rebuild();
    CHECK_EQ(preview.controller()->nodes()->count(), 2);
    CHECK_EQ(preview.controller()->edges()->count(), 1);
    CHECK_EQ(preview.hiddenCount(), 0);
}

TEST_CASE("Preview: KerML blocks and updates of the text") {
    DiagramPreviewModel preview;
    preview.setLanguage("KerML");
    preview.setSource("class Point { feature x : Real; }");
    preview.rebuild();
    CHECK_EQ(preview.controller()->nodes()->count(), 1);
    preview.setSource("class Point; class Line;");
    preview.rebuild();
    CHECK_EQ(preview.controller()->nodes()->count(), 2);
}
