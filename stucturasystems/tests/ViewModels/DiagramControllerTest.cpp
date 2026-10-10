//
// DiagramController: the editing logic behind the QML canvas.
//

#include "../TestFramework.h"
#include "QtPrinting.h"

#include <DiagramController.h>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/SemanticModel.h>

using namespace StructuraSystems::Client;
using namespace StructuraSystems::Diagram;

namespace {
    ElementId idOf(const QString &id) {
        return ElementId(id.toULongLong());
    }

    bool offers(const QVariantList &kinds, int kind) {
        return kinds.contains(kind);
    }

    /// A part dropped at (x, y) with a defined size, as QML would report it.
    QString dropPart(DiagramController &controller, qreal x, qreal y, int tool = DiagramTypes::ToolPart) {
        const QString id = controller.addElement(tool, x, y);
        controller.setNodeSize(id, 160, 60);
        return id;
    }
}

TEST_CASE("Controller: a dropped part becomes a node at the drop position") {
    SemanticModel model;
    DiagramController controller(model);

    const QString id = controller.addElement(DiagramTypes::ToolPart, 300, 200);
    CHECK(!id.isEmpty());
    CHECK(model.element(idOf(id)).kind() == ElementKind::PartUsage);
    CHECK_EQ(model.element(idOf(id)).name(), std::string("part1"));
    CHECK_EQ(controller.nodes()->count(), 1);

    const QRectF rect = controller.nodeRect(id);
    CHECK_EQ(rect.x(), 230.0); // drop position minus the grab offset (70, 25)
    CHECK_EQ(rect.y(), 175.0);
    CHECK(controller.hasSelection()); // the new element is selected
}

TEST_CASE("Controller: attributes and actions need a part") {
    SemanticModel model;
    DiagramController controller(model);

    CHECK(controller.addElement(DiagramTypes::ToolAttribute, 50, 50).isEmpty());
    CHECK_EQ(model.elementCount(), std::size_t(0));

    const QString part = dropPart(controller, 300, 200);
    const QRectF rect = controller.nodeRect(part);
    const QString attribute = controller.addElement(DiagramTypes::ToolAttribute, rect.center().x(), rect.center().y());
    CHECK(!attribute.isEmpty());
    CHECK_EQ(model.element(idOf(attribute)).owner(), idOf(part));
    CHECK_EQ(controller.nodes()->count(), 1); // an entry, not a node
    CHECK_EQ(DiagramNodeModel::entryCount(controller.nodes()->nodes().front(), "attributes"), 1);

    // Palette click: goes to the single selected part.
    controller.select(part, false);
    const QString action = controller.addElementAtSelection(DiagramTypes::ToolAction, 0, 0);
    CHECK(!action.isEmpty());
    CHECK(model.element(idOf(action)).kind() == ElementKind::ActionPerformance);
}

TEST_CASE("Controller: dropping into a package frame and moving out changes the owner") {
    SemanticModel model;
    DiagramController controller(model);

    const QString package = controller.addElement(DiagramTypes::ToolPackage, 120, 115);
    const QRectF frame = controller.nodeRect(package); // (100, 100) 360 x 240
    const QString part = dropPart(controller, frame.x() + 150, frame.y() + 120);
    CHECK_EQ(model.element(idOf(part)).owner(), idOf(package));

    controller.moveNode(part, 900, 900);
    controller.finishMove(part);
    CHECK_EQ(model.element(idOf(part)).owner(), RootId);

    controller.moveNode(part, frame.x() + 40, frame.y() + 60);
    controller.finishMove(part);
    CHECK_EQ(model.element(idOf(part)).owner(), idOf(package));
}

TEST_CASE("Controller: moving a frame takes its content along") {
    SemanticModel model;
    DiagramController controller(model);

    const QString package = controller.addElement(DiagramTypes::ToolPackage, 120, 115);
    const QRectF frame = controller.nodeRect(package);
    const QString part = dropPart(controller, frame.x() + 150, frame.y() + 120);
    const QRectF before = controller.nodeRect(part);

    controller.moveNode(package, frame.x() + 50, frame.y() + 30);
    const QRectF after = controller.nodeRect(part);
    CHECK_EQ(after.x(), before.x() + 50);
    CHECK_EQ(after.y(), before.y() + 30);
}

TEST_CASE("Controller: frames are grabbed at their title, parts anywhere") {
    SemanticModel model;
    DiagramController controller(model);

    const QString package = controller.addElement(DiagramTypes::ToolPackage, 120, 115);
    const QRectF frame = controller.nodeRect(package);
    CHECK_EQ(controller.nodeAt(frame.x() + 40, frame.y() + 10), package);  // title band
    CHECK(controller.nodeAt(frame.center().x(), frame.center().y()).isEmpty()); // inside stays free
    CHECK_EQ(controller.nodeAt(frame.center().x(), frame.center().y(), true), package);

    const QString part = dropPart(controller, frame.x() + 150, frame.y() + 120);
    const QRectF rect = controller.nodeRect(part);
    CHECK_EQ(controller.nodeAt(rect.center().x(), rect.center().y()), part);
}

TEST_CASE("Controller: an arrow between parts offers all kinds, composition nests") {
    SemanticModel model;
    DiagramController controller(model);

    const QString vehicle = dropPart(controller, 100, 100);
    const QString engine = dropPart(controller, 500, 100);
    const QVariantList kinds = controller.allowedEdgeKinds(vehicle, engine);
    CHECK(offers(kinds, DiagramTypes::Composition));
    CHECK(offers(kinds, DiagramTypes::Connection));
    CHECK(offers(kinds, DiagramTypes::Flow));
    CHECK(offers(kinds, DiagramTypes::Specialization));

    const QRectF target = controller.nodeRect(engine);
    const QVariantMap choice = controller.arrowReleased(vehicle, target.center().x(), target.center().y());
    CHECK_EQ(choice.value("target").toString(), engine);
    CHECK_EQ(choice.value("kinds").toList().size(), kinds.size());

    CHECK(controller.createEdge(DiagramTypes::Composition, vehicle, engine));
    CHECK_EQ(model.element(idOf(engine)).owner(), idOf(vehicle));
    CHECK_EQ(controller.edges()->count(), 1);
    CHECK_EQ(controller.edges()->edges().front().Key, QString("c:") + engine);
    CHECK_EQ(controller.nodes()->count(), 2); // the owned part stays a node of its own
}

TEST_CASE("Controller: an arrow preset creates the edge without asking") {
    SemanticModel model;
    DiagramController controller(model);

    const QString a = dropPart(controller, 100, 100);
    const QString b = dropPart(controller, 500, 100);
    controller.setArrowDefault(DiagramTypes::AlwaysFlow);
    const QRectF target = controller.nodeRect(b);
    CHECK(controller.arrowReleased(a, target.center().x(), target.center().y()).isEmpty());
    CHECK_EQ(model.relationships().size(), std::size_t(1));
    CHECK(model.relationships().front().Kind == RelationshipKind::Flow);

    // Released on empty canvas: nothing happens.
    CHECK(controller.arrowReleased(a, 1500, 1500).isEmpty());
    CHECK_EQ(model.relationships().size(), std::size_t(1));
}

TEST_CASE("Controller: an edge is found near its line and can be deleted") {
    SemanticModel model;
    DiagramController controller(model);

    const QString a = dropPart(controller, 100, 100); // rect (30, 75) 160 x 60
    const QString b = dropPart(controller, 500, 100); // rect (430, 75)
    CHECK(controller.createEdge(DiagramTypes::Connection, a, b));
    const QString key = controller.edgeAt(300, 105);
    CHECK(key.startsWith("r:"));
    CHECK(controller.edgeAt(300, 300).isEmpty());

    controller.selectEdge(key, false);
    controller.deleteSelection();
    CHECK(model.relationships().empty());
    CHECK_EQ(controller.edges()->count(), 0);
}

TEST_CASE("Controller: deleting a composition edge moves the part back to the top level") {
    SemanticModel model;
    DiagramController controller(model);

    const QString vehicle = dropPart(controller, 100, 100);
    const QString engine = dropPart(controller, 500, 100);
    CHECK(controller.createEdge(DiagramTypes::Composition, vehicle, engine));
    controller.deleteEdge(QString("c:") + engine);
    CHECK_EQ(model.element(idOf(engine)).owner(), RootId);
    CHECK_EQ(controller.edges()->count(), 0);
}

TEST_CASE("Controller: deleting a part deletes what it owns") {
    SemanticModel model;
    DiagramController controller(model);

    const QString vehicle = dropPart(controller, 100, 100);
    const QString engine = dropPart(controller, 500, 100);
    controller.createEdge(DiagramTypes::Composition, vehicle, engine);
    controller.select(vehicle, false);
    controller.addElementAtSelection(DiagramTypes::ToolAttribute, 0, 0);
    controller.select(vehicle, false);
    controller.deleteSelection();
    CHECK_EQ(model.elementCount(), std::size_t(0));
    CHECK_EQ(controller.nodes()->count(), 0);
    CHECK(!controller.hasSelection());
}

TEST_CASE("Controller: edit values are validated and applied") {
    SemanticModel model;
    DiagramController controller(model);

    const QString part = dropPart(controller, 100, 100);
    const QVariantMap details = controller.elementDetails(part);
    CHECK(details.value("typed").toBool());
    CHECK(details.value("hasMultiplicity").toBool());
    CHECK_EQ(details.value("name").toString(), QString("part1"));

    CHECK(!controller.updateElement(part, {{"name", ""}}).isEmpty());
    CHECK(!controller.updateElement(part, {{"name", "engine"}, {"multiplicity", "2..1"}}).isEmpty());
    CHECK(controller.updateElement(part, {{"name", "engine"}, {"typeName", "Engine"}, {"multiplicity", "1..*"}}).isEmpty());
    CHECK_EQ(model.element(idOf(part)).name(), std::string("engine"));
    CHECK_EQ(controller.nodes()->nodes().front().Title, QString("engine : Engine[1..*]"));
}

TEST_CASE("Controller: multiplicity input") {
    CHECK(DiagramController::parseMultiplicity("").first);
    CHECK(!DiagramController::parseMultiplicity("").second.has_value());
    CHECK_EQ(DiagramController::parseMultiplicity("6").second->toString(), std::string("[6]"));
    CHECK_EQ(DiagramController::parseMultiplicity("[0..1]").second->toString(), std::string("[0..1]"));
    CHECK_EQ(DiagramController::parseMultiplicity("1..*").second->toString(), std::string("[1..*]"));
    CHECK(!DiagramController::parseMultiplicity("x").first);
    CHECK(!DiagramController::parseMultiplicity("3..1").first);
}

TEST_CASE("Controller: unique names per owner") {
    SemanticModel model;
    DiagramController controller(model);

    dropPart(controller, 100, 100);
    const QString second = dropPart(controller, 500, 100);
    CHECK_EQ(model.element(idOf(second)).name(), std::string("part2"));
    const QString definition = dropPart(controller, 900, 100, DiagramTypes::ToolPartDefinition);
    CHECK_EQ(model.element(idOf(definition)).name(), std::string("Part1"));
}

TEST_CASE("Controller: the automatic layout puts frames around their content and avoids overlaps") {
    SemanticModel model;
    SysMLTextImporter::importText(
            "package P {\n    part def A;\n    part def B;\n    package Q { part def C; }\n}\n"
            "part vehicle {\n    part engine;\n    part wheels[4];\n}\npart def D;\n", model);
    DiagramController controller(model);
    controller.layoutAll();

    const auto &nodes = controller.nodes()->nodes();
    CHECK_EQ(nodes.size(), std::size_t(9));
    for (const auto &node: nodes) {
        const ElementId owner = model.element(node.Id).owner();
        if (owner != RootId && model.element(owner).kind() == ElementKind::Package)
            CHECK(controller.nodes()->find(owner)->Rect.contains(node.Rect)); // inside its frame
        for (const auto &other: nodes) {
            if (other.Id == node.Id || model.isAncestor(node.Id, other.Id) || model.isAncestor(other.Id, node.Id))
                continue;
            CHECK(!node.Rect.intersects(other.Rect));
        }
    }
    // owned parts are placed below their owner
    const QRectF vehicle = controller.nodes()->find(model.children(RootId).at(1))->Rect;
    for (const ElementId part: model.children(model.children(RootId).at(1)))
        CHECK(controller.nodes()->find(part)->Rect.top() > vehicle.bottom());
}

namespace {
    QStringList entryTexts(const DiagramNode &node, const QString &title) {
        QStringList texts;
        for (const auto &compartment: node.Compartments) {
            const auto map = compartment.toMap();
            if (map.value("title").toString() != title)
                continue;
            for (const auto &item: map.value("entries").toList())
                texts.append(item.toMap().value("text").toString());
        }
        return texts;
    }
}

TEST_CASE("Controller: constraints, calculations, metadata and derived values are shown") {
    SemanticModel model;
    SysMLTextImporter::importText(
            "part def SHORT_TIME_INJECTION_CORRECTION_1_3 {\n"
            "#measurable attribute VariableA : Integer;\n"
            "assert constraint ByteRange {\nVariableA >= 0 and VariableA <= 255\n}\n"
            "function decodeFuelTrim {\nin feature raw : Integer;\nreturn feature trimPercent : Rational = raw * 100.0 / 128.0 - 100.0;\n}\n"
            "derived #measurable attribute PercentA : Rational = decodeFuelTrim(VariableA);\n"
            "// a note\n"
            "}\n"
            "constraint def NonNegativeReal {\n    in x : Real;\n    x >= 0.0\n}\n", model);
    DiagramController controller(model);
    controller.layoutAll();

    CHECK_EQ(controller.nodes()->count(), 2);
    const DiagramNode &part = controller.nodes()->nodes().at(0);
    CHECK_EQ(entryTexts(part, "attributes"), QStringList({QString("«measurable» VariableA : Integer"),
                                                          QString("«measurable» /PercentA : Rational = decodeFuelTrim(VariableA)")}));
    CHECK_EQ(entryTexts(part, "constraints"), QStringList({QString("assert ByteRange { VariableA >= 0 and VariableA <= 255 }")}));
    CHECK_EQ(entryTexts(part, "calcs"), QStringList({QString("decodeFuelTrim(raw : Integer) : Rational = raw * 100.0 / 128.0 - 100.0")}));
    CHECK(entryTexts(part, "other").isEmpty()); // the comment is kept in the code, not shown

    const DiagramNode &definition = controller.nodes()->nodes().at(1);
    CHECK(definition.Kind == DiagramTypes::ConstraintDefinitionNode);
    CHECK_EQ(definition.Keyword, QString("«constraint def»"));
    CHECK_EQ(definition.Title, QString("NonNegativeReal"));
    CHECK_EQ(entryTexts(definition, "parameters"), QStringList({QString("in x : Real")}));
    CHECK_EQ(entryTexts(definition, "constraint"), QStringList({QString("{ x >= 0.0 }")}));
}

TEST_CASE("Controller: constraint and calc definitions are nodes with the compartments of the standard") {
    SemanticModel model;
    SysMLTextImporter::importText(
            "package Calcs {\n"
            "    function decodeFuelTrim {\n"
            "        in feature raw : Integer;\n"
            "        return feature trimPercent : Rational = raw * 100.0 / 128.0 - 100.0;\n"
            "    }\n"
            "    calc def Twice {\n"
            "        in x : Real;\n"
            "        attribute factor : Real = 2;\n"
            "        x * factor\n"
            "    }\n"
            "    assert constraint limit : Bounded;\n"
            "}\n", model);
    DiagramController controller(model);
    controller.layoutAll();
    CHECK_EQ(controller.nodes()->count(), 4); // package + 3

    const DiagramNode &decode = controller.nodes()->nodes().at(1);
    CHECK(decode.Kind == DiagramTypes::CalcDefinitionNode);
    CHECK_EQ(decode.Keyword, QString("«function»"));
    CHECK_EQ(entryTexts(decode, "parameters"), QStringList({QString("in raw : Integer")}));
    CHECK_EQ(entryTexts(decode, "result"), QStringList({QString("trimPercent : Rational = raw * 100.0 / 128.0 - 100.0")}));

    const DiagramNode &twice = controller.nodes()->nodes().at(2);
    CHECK_EQ(twice.Keyword, QString("«calc def»"));
    CHECK_EQ(entryTexts(twice, "attributes"), QStringList({QString("factor : Real = 2")}));
    CHECK_EQ(entryTexts(twice, "result"), QStringList({QString("x * factor")}));

    const DiagramNode &limit = controller.nodes()->nodes().at(3);
    CHECK(limit.Kind == DiagramTypes::ConstraintNode);
    CHECK_EQ(limit.Keyword, QString("«assert constraint»"));
    CHECK_EQ(limit.Title, QString("limit : Bounded"));
}

TEST_CASE("Controller: constraint and calc definitions from the palette, parameters and specialization") {
    SemanticModel model;
    DiagramController controller(model);
    const QString general = dropPart(controller, 100, 100, DiagramTypes::ToolConstraintDefinition);
    const QString special = dropPart(controller, 400, 100, DiagramTypes::ToolConstraintDefinition);
    const QString calc = dropPart(controller, 100, 400, DiagramTypes::ToolCalcDefinition);
    CHECK(model.element(idOf(general)).kind() == ElementKind::ConstraintDefinition);
    CHECK(model.element(idOf(calc)).kind() == ElementKind::CalcDefinition);
    CHECK_EQ(SysMLCodeGenerator().generate(model, idOf(calc)),
             std::string("calc def Calc1 {\n    in x : Real;\n    return result : Real = x;\n}"));
    CHECK_EQ(SysMLCodeGenerator().generate(model, idOf(general)),
             std::string("constraint def Constraint1 {\n    in x : Real;\n    x >= 0.0\n}"));

    // specialization between two constraint definitions, not between a constraint and a calc
    CHECK(offers(controller.allowedEdgeKinds(special, general), DiagramTypes::Specialization));
    CHECK(!offers(controller.allowedEdgeKinds(special, calc), DiagramTypes::Specialization));
    CHECK(!offers(controller.allowedEdgeKinds(special, calc), DiagramTypes::Connection));
    CHECK(controller.createEdge(DiagramTypes::Specialization, special, general));
    CHECK(SysMLCodeGenerator().generate(model).find("constraint def Constraint2 :> Constraint1 {") != std::string::npos);

    // an attribute dropped onto a calculation becomes a parameter
    controller.select(calc, false);
    const QString parameter = controller.addElementAtSelection(DiagramTypes::ToolAttribute, 0, 0);
    CHECK_EQ(SysMLCodeGenerator().generate(model, idOf(parameter)), std::string("in x1 : Real;"));
    // actions are not possible there
    controller.select(calc, false);
    CHECK(controller.addElementAtSelection(DiagramTypes::ToolAction, 0, 0).isEmpty());
}

TEST_CASE("Controller: constraints and calculations from the palette, editing expressions, values and prefixes") {
    SemanticModel model;
    DiagramController controller(model);
    const QString part = dropPart(controller, 200, 200, DiagramTypes::ToolPartDefinition);
    controller.select(part, false);
    const QString constraint = controller.addElementAtSelection(DiagramTypes::ToolConstraint, 0, 0);
    controller.select(part, false);
    const QString function = controller.addElementAtSelection(DiagramTypes::ToolCalc, 0, 0);
    CHECK(!constraint.isEmpty());
    CHECK(!function.isEmpty());
    CHECK_EQ(entryTexts(controller.nodes()->nodes().front(), "constraints"), QStringList({QString("assert constraint1 { true }")}));
    CHECK_EQ(entryTexts(controller.nodes()->nodes().front(), "calcs"), QStringList({QString("calc1(x : Real) : Real = x")}));

    const QVariantMap details = controller.elementDetails(constraint);
    CHECK(!details.value("isTextual").toBool());
    CHECK(details.value("hasExpression").toBool());
    CHECK(details.value("assertion").toBool());
    CHECK_EQ(details.value("expression").toString(), QString("true"));
    CHECK(!controller.updateElement(constraint, {{"name", "Range"}, {"expression", "(x > 0"}}).isEmpty()); // unbalanced
    CHECK(!controller.updateElement(constraint, {{"name", "Range"}, {"expression", "x > 0;"}}).isEmpty());
    CHECK(controller.updateElement(constraint, {{"name", "Range"}, {"expression", "x > 0 and\nx < 10"},
                                                {"assertion", true}}).isEmpty());
    CHECK_EQ(model.element(idOf(constraint)).name(), std::string("Range"));
    CHECK(SysMLCodeGenerator().generate(model).find("    assert constraint Range {\n        x > 0 and\n        x < 10\n    }\n")
          != std::string::npos);
    CHECK(controller.updateElement(constraint, {{"name", "Range"}, {"expression", "x > 0"}, {"assertion", false}}).isEmpty());
    CHECK(SysMLCodeGenerator().generate(model).find("    constraint Range {\n") != std::string::npos);

    controller.select(part, false);
    const QString attribute = controller.addElementAtSelection(DiagramTypes::ToolAttribute, 0, 0);
    CHECK(controller.updateElement(attribute, {{"name", "percent"}, {"typeName", "Rational"},
                                               {"value", "decode(raw)"}, {"prefixes", "derived #measurable"}}).isEmpty());
    const auto &element = model.element(idOf(attribute));
    CHECK_EQ(element.value(), std::string("decode(raw)"));
    CHECK_EQ(element.valueOperator(), std::string("="));
    CHECK_EQ(element.prefixes().size(), std::size_t(2));
    CHECK(SysMLCodeGenerator().generate(model).find("    derived #measurable attribute percent : Rational = decode(raw);\n") != std::string::npos);
}

TEST_CASE("Controller: any SysML statement can be added as text and is written word for word") {
    SemanticModel model;
    DiagramController controller(model);
    const QString statement = controller.addElement(DiagramTypes::ToolStatement, 300, 300);
    CHECK(!statement.isEmpty());
    CHECK(model.element(idOf(statement)).kind() == ElementKind::Textual);
    CHECK_EQ(controller.nodes()->count(), 1); // on the top level it is a node of its own
    CHECK(controller.updateElement(statement, {{"text", "port def FuelPort {\n    in item fuel : Fuel;\n}"}}).isEmpty());
    CHECK_EQ(controller.nodes()->nodes().front().Keyword, QString("«port def»"));
    CHECK_EQ(SysMLCodeGenerator().generate(model), std::string("port def FuelPort {\n    in item fuel : Fuel;\n}\n"));

    // dropped onto a part: an entry of the part
    const QString part = dropPart(controller, 600, 100);
    const QRectF rect = controller.nodeRect(part);
    const QString inside = controller.addElement(DiagramTypes::ToolStatement, rect.center().x(), rect.center().y());
    CHECK_EQ(model.element(idOf(inside)).owner(), idOf(part));
}

TEST_CASE("Controller: definitions as nodes, ports on the border of their part, interfaces with name and type") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "port def FuelPort {\n"
            "    out item fuel : Fuel;\n"
            "}\n"
            "enum def PumpState {\n"
            "    enum off;\n"
            "    running;\n"
            "}\n"
            "interface def FuelInterface {\n"
            "    end supplier : FuelPort;\n"
            "    end consumer : ~FuelPort;\n"
            "}\n"
            "part tank {\n"
            "    port fuelOut : FuelPort;\n"
            "    item stored : Fuel;\n"
            "}\n"
            "part engine {\n"
            "    in port fuelIn : ~FuelPort;\n"
            "}\n"
            "interface : FuelInterface connect tank.fuelOut to engine.fuelIn;\n", model).lossless());
    DiagramController controller(model);
    controller.layoutAll();

    // 3 definitions, 2 parts, 2 ports
    CHECK_EQ(controller.nodes()->count(), 7);
    const auto *portDef = controller.nodes()->find(model.children(RootId).at(0));
    CHECK(portDef->Kind == DiagramTypes::DefinitionNode);
    CHECK_EQ(portDef->Keyword, QString("«port def»"));
    CHECK_EQ(entryTexts(*portDef, "items"), QStringList({QString("out fuel : Fuel")}));
    CHECK_EQ(entryTexts(*controller.nodes()->find(model.children(RootId).at(1)), "values"),
             QStringList({QString("off"), QString("running")}));
    CHECK_EQ(entryTexts(*controller.nodes()->find(model.children(RootId).at(2)), "ends"),
             QStringList({QString("end supplier : FuelPort"), QString("end consumer : ~FuelPort")}));

    const ElementId tank = model.children(RootId).at(3);
    const ElementId engine = model.children(RootId).at(4);
    const ElementId fuelOut = model.children(tank).at(0);
    const ElementId fuelIn = model.children(engine).at(0);
    const auto *tankNode = controller.nodes()->find(tank);
    CHECK_EQ(entryTexts(*tankNode, "items"), QStringList({QString("stored : Fuel")}));
    // ports: small squares on the border (out on the right, in on the left), no composition edge
    const QRectF out = controller.nodeRect(QString::number(fuelOut));
    CHECK_EQ(out.width(), 14.0);
    CHECK_EQ(out.center().x(), tankNode->Rect.right());
    CHECK_EQ(controller.nodeRect(QString::number(fuelIn)).center().x(), controller.nodeRect(QString::number(engine)).left());
    CHECK_EQ(controller.portSide(QString::number(fuelIn)), QString("left"));
    CHECK_EQ(controller.edges()->count(), 1);
    const DiagramEdge edge = controller.edges()->edges().front(); // a copy: the list is replaced on changes
    CHECK(edge.Kind == DiagramTypes::Interface);
    CHECK_EQ(edge.Label, QString("«interface» : FuelInterface"));

    // the part takes its ports along; a port slides along the border
    controller.moveNode(QString::number(tank), tankNode->Rect.x() + 100, tankNode->Rect.y() + 50);
    CHECK_EQ(controller.nodeRect(QString::number(fuelOut)).center().x(), controller.nodeRect(QString::number(tank)).right());
    const QRectF tankRect = controller.nodeRect(QString::number(tank));
    controller.moveNode(QString::number(fuelOut), tankRect.center().x(), tankRect.top() - 30);
    CHECK_EQ(controller.nodeRect(QString::number(fuelOut)).center().y(), tankRect.top());

    // name and type of the interface
    CHECK(controller.edgeDetails(edge.Key).value("editable").toBool());
    CHECK(controller.updateEdge(edge.Key, {{"name", "line"}, {"typeName", "FuelInterface"}}).isEmpty());
    CHECK(SysMLCodeGenerator().generate(model).find("interface line : FuelInterface connect tank.fuelOut to engine.fuelIn;")
          != std::string::npos);
    CHECK(!controller.updateEdge(edge.Key, {{"name", "two words"}, {"typeName", ""}}).isEmpty());

    // a connection between two ports offers interface, connect and flow
    controller.deleteEdge(edge.Key);
    const QVariantList kinds = controller.allowedEdgeKinds(QString::number(fuelOut), QString::number(fuelIn));
    CHECK(offers(kinds, DiagramTypes::Interface));
    CHECK(offers(kinds, DiagramTypes::Connection));
    CHECK(offers(kinds, DiagramTypes::Flow));
    CHECK(!offers(kinds, DiagramTypes::Composition));
    CHECK(controller.createEdge(DiagramTypes::Flow, QString::number(fuelOut), QString::number(fuelIn)));
    const QString flow = controller.edges()->edges().front().Key;
    CHECK(controller.updateEdge(flow, {{"name", ""}, {"typeName", "Fuel"}}).isEmpty());
    CHECK_EQ(controller.edges()->edges().front().Label, QString("of Fuel"));
    CHECK(SysMLCodeGenerator().generate(model).find("flow of Fuel from tank.fuelOut to engine.fuelIn;") != std::string::npos);
}

TEST_CASE("Controller: palette entries for definitions, ports, items and enum values") {
    SemanticModel model;
    DiagramController controller(model);
    const QString portDef = dropPart(controller, 100, 100, DiagramTypes::ToolPortDefinition);
    const QString enumDef = dropPart(controller, 400, 100, DiagramTypes::ToolEnumDefinition);
    const QString interfaceDef = dropPart(controller, 700, 100, DiagramTypes::ToolInterfaceDefinition);
    const QString part = dropPart(controller, 100, 400);
    for (const auto &id: {portDef, enumDef, interfaceDef})
        CHECK(isTypeDefinitionKind(model.element(idOf(id)).kind()));

    controller.select(enumDef, false);
    const QString literal = controller.addElementAtSelection(DiagramTypes::ToolAttribute, 0, 0);
    CHECK(model.element(idOf(literal)).kind() == ElementKind::EnumerationLiteral);
    controller.select(interfaceDef, false);
    const QString end = controller.addElementAtSelection(DiagramTypes::ToolPort, 0, 0);
    CHECK_EQ(SysMLCodeGenerator().generate(model, idOf(end)), std::string("end port end1;"));
    controller.select(portDef, false);
    CHECK(!controller.addElementAtSelection(DiagramTypes::ToolItem, 0, 0).isEmpty());
    controller.select(portDef, false);
    CHECK(controller.addElementAtSelection(DiagramTypes::ToolAction, 0, 0).isEmpty()); // no actions in a port def

    // a port dropped onto a part: a node on its border
    const QRectF rect = controller.nodeRect(part);
    const QString port = controller.addElement(DiagramTypes::ToolPort, rect.center().x(), rect.center().y());
    CHECK(model.element(idOf(port)).kind() == ElementKind::PortUsage);
    CHECK(controller.nodes()->find(idOf(port))->Kind == DiagramTypes::PortNode);
    CHECK(controller.updateElement(port, {{"name", "fuelIn"}, {"typeName", "~FuelPort"}, {"multiplicity", ""},
                                          {"prefixes", "in"}}).isEmpty());
    CHECK(SysMLCodeGenerator().generate(model).find("    in port fuelIn : ~FuelPort;\n") != std::string::npos);
    // a port onto the port: nested, also on the border of the part
    const QRectF portRect = controller.nodeRect(port);
    const QString nested = controller.addElement(DiagramTypes::ToolPort, portRect.center().x(), portRect.center().y());
    CHECK_EQ(model.element(idOf(nested)).owner(), idOf(port));
    CHECK(controller.nodes()->find(idOf(nested))->Kind == DiagramTypes::PortNode);
}

TEST_CASE("Controller: action and state definitions with their actions and states, states in parts") {
    SemanticModel model;
    DiagramController controller(model);
    const QString action = dropPart(controller, 100, 100, DiagramTypes::ToolActionDefinition);
    const QString states = dropPart(controller, 400, 100, DiagramTypes::ToolStateDefinition);
    const QString part = dropPart(controller, 100, 400);
    controller.select(action, false);
    CHECK(!controller.addElementAtSelection(DiagramTypes::ToolAction, 0, 0).isEmpty()); // an action of its own
    controller.select(states, false);
    const QString state = controller.addElementAtSelection(DiagramTypes::ToolState, 0, 0);
    CHECK(!state.isEmpty());
    controller.select(part, false);
    const QString exhibited = controller.addElementAtSelection(DiagramTypes::ToolState, 0, 0);
    controller.select(part, false);
    CHECK(!controller.addElementAtSelection(DiagramTypes::ToolAction, 0, 0).isEmpty()); // perform action
    CHECK(controller.updateElement(exhibited, {{"name", "states"}, {"typeName", "States1"}, {"multiplicity", ""},
                                               {"prefixes", ""}}).isEmpty());
    CHECK_EQ(entryTexts(*controller.nodes()->find(idOf(action)), "actions"), QStringList({QString("action action1")}));
    CHECK_EQ(entryTexts(*controller.nodes()->find(idOf(states)), "states"), QStringList({QString("state1")}));
    CHECK_EQ(entryTexts(*controller.nodes()->find(idOf(part)), "states"), QStringList({QString("states : States1")}));
    const std::string text = SysMLCodeGenerator().generate(model);
    CHECK(text.find("action def Action1 {\n    action action1;\n}") != std::string::npos);
    CHECK(text.find("    exhibit state states : States1;\n    perform action action1;\n") != std::string::npos);
}

TEST_CASE("Controller: requirements with text, subject, stakeholders and required constraints") {
    SemanticModel model;
    DiagramController controller(model);
    const QString definition = dropPart(controller, 100, 100, DiagramTypes::ToolRequirementDefinition);
    CHECK(model.element(idOf(definition)).kind() == ElementKind::RequirementDefinition);
    const QRectF rect = controller.nodeRect(definition);
    const QPointF center = rect.center();
    CHECK(!controller.addElement(DiagramTypes::ToolSubject, center.x(), center.y()).isEmpty());
    CHECK(controller.addElement(DiagramTypes::ToolSubject, center.x(), center.y()).isEmpty()); // only one subject
    const QString stakeholder = controller.addElement(DiagramTypes::ToolActor, center.x(), center.y());
    CHECK_EQ(*model.element(idOf(stakeholder)).declaredKeyword(), std::string("stakeholder"));
    const QString constraint = controller.addElement(DiagramTypes::ToolConstraint, center.x(), center.y());
    CHECK(model.element(idOf(constraint)).name().empty());
    CHECK_EQ(*model.element(idOf(constraint)).declaredKeyword(), std::string("require constraint"));
    CHECK(controller.addElement(DiagramTypes::ToolObjective, center.x(), center.y()).isEmpty()); // only in cases

    // edit: id, text, the constraint, the subject
    const QVariantMap details = controller.elementDetails(definition);
    CHECK(details.value("hasDoc").toBool());
    CHECK_EQ(details.value("doc").toString(), QString("Describe the requirement."));
    CHECK(controller.updateElement(definition, {{"name", "MassRequirement"}, {"shortName", "1.1"},
                                                {"doc", "The mass of the vehicle\nshall be below the limit."}}).isEmpty());
    CHECK_EQ(model.element(idOf(definition)).shortName(), std::string("'1.1'"));
    CHECK(controller.updateElement(constraint, {{"name", ""}, {"typeName", ""}, {"multiplicity", ""},
                                                {"expression", "subject1.mass <= 2000"},
                                                {"constraintRole", "assume constraint"}}).isEmpty());
    const QString subject = QString::number(model.children(idOf(definition)).at(1));
    CHECK(controller.updateElement(subject, {{"name", "vehicle"}, {"typeName", "Vehicle"}, {"multiplicity", ""}}).isEmpty());
    const std::string text = SysMLCodeGenerator().generate(model);
    CHECK_EQ(text, std::string("requirement def <'1.1'> MassRequirement {\n"
                               "    doc /*\n"
                               "     * The mass of the vehicle\n"
                               "     * shall be below the limit.\n"
                               "     */\n"
                               "    subject vehicle : Vehicle;\n"
                               "    stakeholder stakeholder1;\n"
                               "    assume constraint {\n"
                               "        subject1.mass <= 2000\n"
                               "    }\n"
                               "}\n"));
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(again), text);

    // the node: title with id, compartments doc, subject, stakeholders, assume
    controller.syncNow();
    const auto *node = controller.nodes()->find(idOf(definition));
    CHECK(node->Kind == DiagramTypes::RequirementNode);
    CHECK_EQ(node->Title, QString("<'1.1'> MassRequirement"));
    QStringList titles;
    for (const auto &compartment: node->Compartments)
        titles.append(compartment.toMap().value("title").toString());
    CHECK_EQ(titles, QStringList({"doc", "subject", "stakeholders", "assume"}));
    // a plain constraint in a part may not become "require constraint"
    const QString part = dropPart(controller, 600, 100);
    const QString assertion = controller.addElement(DiagramTypes::ToolConstraint, controller.nodeRect(part).center().x(),
                                                    controller.nodeRect(part).center().y());
    CHECK(!controller.updateElement(assertion, {{"name", ""}, {"constraintRole", "require constraint"}}).isEmpty());
    CHECK(!controller.updateElement(part, {{"name", ""}}).isEmpty());
}

TEST_CASE("Controller: satisfy, verify and sub-requirements drawn as arrows") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "requirement def MassRequirement;\n"
            "requirement vehicleMass : MassRequirement;\n"
            "requirement dryMass;\n"
            "part vehicle;\n"
            "verification def MassTest;\n", model).lossless());
    DiagramController controller(model);
    const auto id = [&](const std::string &name) {
        for (const ElementId child: model.children(RootId))
            if (model.element(child).name() == name)
                return QString::number(child);
        return QString();
    };
    CHECK_EQ(controller.nodes()->count(), 5);
    QVariantList kinds = controller.allowedEdgeKinds(id("vehicle"), id("vehicleMass"));
    CHECK(offers(kinds, DiagramTypes::Satisfy));
    CHECK(offers(kinds, DiagramTypes::Composition)); // a part may own a requirement
    CHECK(!offers(controller.allowedEdgeKinds(id("vehicle"), id("MassRequirement")), DiagramTypes::Satisfy));
    CHECK(controller.createEdge(DiagramTypes::Satisfy, id("vehicle"), id("vehicleMass")));
    CHECK(offers(controller.allowedEdgeKinds(id("MassTest"), id("vehicleMass")), DiagramTypes::Verify));
    CHECK(controller.createEdge(DiagramTypes::Verify, id("MassTest"), id("vehicleMass")));
    CHECK(controller.createEdge(DiagramTypes::Composition, id("vehicleMass"), id("dryMass")));
    CHECK(controller.createEdge(DiagramTypes::Interface, id("vehicle"), id("vehicleMass")) == false);

    const std::string text = SysMLCodeGenerator().generate(model);
    CHECK_EQ(text, std::string("requirement def MassRequirement;\n"
                               "requirement vehicleMass : MassRequirement {\n"
                               "    requirement dryMass;\n"
                               "}\n"
                               "part vehicle;\n"
                               "verification def MassTest {\n"
                               "    objective {\n"
                               "        verify vehicleMass;\n"
                               "    }\n"
                               "}\n"
                               "satisfy vehicleMass by vehicle;\n"));
    controller.syncNow();
    int satisfy = 0;
    int verify = 0;
    int composition = 0;
    for (const auto &edge: controller.edges()->edges()) {
        satisfy += edge.Kind == DiagramTypes::Satisfy && edge.Label == "«satisfy»" ? 1 : 0;
        verify += edge.Kind == DiagramTypes::Verify && edge.Label == "«verify»" ? 1 : 0;
        composition += edge.Kind == DiagramTypes::Composition ? 1 : 0;
    }
    CHECK_EQ(satisfy, 1);
    CHECK_EQ(verify, 1);
    CHECK_EQ(composition, 1);
    // the verification shows its objective; deleting the arrow removes the verify again
    const auto *test = controller.nodes()->find(idOf(id("MassTest")));
    CHECK(test->Kind == DiagramTypes::CaseNode);
    for (const auto &edge: controller.edges()->edges())
        if (edge.Kind == DiagramTypes::Verify)
            controller.deleteEdge(edge.Key);
    CHECK(SysMLCodeGenerator().generate(model).find("verify") == std::string::npos);
    // the arrow presets include satisfy and verify (and interface is no longer clamped away)
    controller.setArrowDefault(DiagramTypes::AlwaysInterface);
    CHECK_EQ(controller.arrowDefault(), int(DiagramTypes::AlwaysInterface));
    controller.setArrowDefault(DiagramTypes::AlwaysVerify);
    CHECK_EQ(controller.arrowDefault(), int(DiagramTypes::AlwaysVerify));
}

TEST_CASE("Controller: use cases and verifications with subject, actors and objective") {
    SemanticModel model;
    DiagramController controller(model);
    const QString useCase = dropPart(controller, 100, 100, DiagramTypes::ToolUseCaseDefinition);
    const QPointF center = controller.nodeRect(useCase).center();
    CHECK(!controller.addElement(DiagramTypes::ToolSubject, center.x(), center.y()).isEmpty());
    CHECK(!controller.addElement(DiagramTypes::ToolActor, center.x(), center.y()).isEmpty());
    const QString objective = controller.addElement(DiagramTypes::ToolObjective, center.x(), center.y());
    CHECK(!objective.isEmpty());
    CHECK(controller.addElement(DiagramTypes::ToolObjective, center.x(), center.y()).isEmpty()); // only one
    CHECK(controller.updateElement(objective, {{"name", ""}, {"typeName", ""}, {"doc", "Reach the destination."}}).isEmpty());
    CHECK(!controller.addElement(DiagramTypes::ToolAction, center.x(), center.y()).isEmpty());
    const std::string text = SysMLCodeGenerator().generate(model);
    CHECK_EQ(text, std::string("use case def UseCase1 {\n"
                               "    subject subject1;\n"
                               "    actor actor1;\n"
                               "    objective {\n"
                               "        doc /* Reach the destination. */\n"
                               "    }\n"
                               "    action action1;\n"
                               "}\n"));
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(again), text);
    controller.syncNow();
    QStringList titles;
    for (const auto &compartment: controller.nodes()->find(idOf(useCase))->Compartments)
        titles.append(compartment.toMap().value("title").toString());
    CHECK_EQ(titles, QStringList({"subject", "actors", "objective", "actions"}));
}

TEST_CASE("Controller: variations, variants, individuals, snapshots, time slices and analysis cases") {
    SemanticModel model;
    DiagramController controller(model);
    // a variation with two variants
    const QString choices = dropPart(controller, 200, 150, DiagramTypes::ToolVariationDefinition);
    CHECK(controller.addFeatureTo(DiagramTypes::ToolVariant, idOf(choices)).has_value());
    CHECK(controller.addFeatureTo(DiagramTypes::ToolVariant, idOf(choices)).has_value());
    const QString plain = dropPart(controller, 600, 150, DiagramTypes::ToolPartDefinition);
    CHECK(!controller.addFeatureTo(DiagramTypes::ToolVariant, idOf(plain)).has_value()); // only in a variation
    // an individual with a snapshot and a time slice
    const QString individual = dropPart(controller, 200, 500, DiagramTypes::ToolIndividual);
    CHECK(controller.addFeatureTo(DiagramTypes::ToolSnapshot, idOf(individual)).has_value());
    CHECK(controller.addFeatureTo(DiagramTypes::ToolTimeslice, idOf(individual)).has_value());
    // an analysis definition with subject and objective
    const QString analysis = dropPart(controller, 900, 500, DiagramTypes::ToolAnalysisDefinition);
    CHECK(controller.addFeatureTo(DiagramTypes::ToolSubject, idOf(analysis)).has_value());
    CHECK(controller.addFeatureTo(DiagramTypes::ToolObjective, idOf(analysis)).has_value());
    const std::string text = SysMLCodeGenerator().generate(model);
    CHECK_EQ(text, std::string("variation part def Choices1 {\n"
                               "    variant part variant1;\n"
                               "    variant part variant2;\n"
                               "}\n"
                               "part def Part1;\n"
                               "individual part individual1 {\n"
                               "    snapshot snapshot1;\n"
                               "    timeslice timeslice1;\n"
                               "}\n"
                               "analysis def Analysis1 {\n"
                               "    subject subject1;\n"
                               "    objective {\n"
                               "        doc /* What the case shall achieve. */\n"
                               "    }\n"
                               "}\n"));
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(again), text);

    // the keywords of the nodes: «variation» «part def», «individual» «part», «snapshot», «analysis def»
    CHECK_EQ(controller.nodes()->find(idOf(choices))->Keyword, QString("«variation» «part def»"));
    CHECK_EQ(controller.nodes()->find(idOf(choices))->Title, QString("Choices1"));
    CHECK_EQ(controller.nodes()->find(idOf(individual))->Keyword, QString("«individual» «part»"));
    CHECK_EQ(controller.nodes()->find(idOf(analysis))->Keyword, QString("«analysis def»"));
    const ElementId snapshot = model.children(idOf(individual)).front();
    CHECK(controller.nodes()->find(snapshot) != nullptr);
    CHECK_EQ(controller.nodes()->find(snapshot)->Keyword, QString("«snapshot»"));
    // the snapshot hangs at the individual by a temporal decomposition, the variants by composition
    int temporal = 0;
    for (const auto &edge: controller.edges()->edges())
        temporal += edge.Kind == DiagramTypes::Temporal ? 1 : 0;
    CHECK_EQ(temporal, 2);

    // an analysis: parameters and result in a compartment of their own
    SemanticModel analyses;
    CHECK(SysMLTextImporter::importText("analysis def FuelEconomyAnalysis {\n"
                                        "    subject vehicle : Vehicle;\n"
                                        "    in attribute scenario : WayPoint[*];\n"
                                        "    attribute distance : LengthValue;\n"
                                        "    return fuelEconomyResult : DistancePerVolumeValue;\n"
                                        "}\n", analyses).lossless());
    DiagramController view(analyses);
    const auto &node = view.nodes()->nodes().front();
    CHECK_EQ(DiagramNodeModel::entryCount(node, "parameters"), 2);
    CHECK_EQ(DiagramNodeModel::entryCount(node, "attributes"), 1);
    CHECK_EQ(DiagramNodeModel::entryCount(node, "subject"), 1);
}
