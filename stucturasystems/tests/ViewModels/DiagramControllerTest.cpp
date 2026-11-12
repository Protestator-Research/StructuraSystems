//
// DiagramController: the editing logic behind the QML canvas.
//

#include "../TestFramework.h"
#include "QtPrinting.h"

#include <DiagramController.h>
#include <EdgeRouting.h>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/SemanticModel.h>

using namespace StructuraSystems::Client;
using namespace StructuraSystems::Diagram;

namespace {
    ElementId idOf(const QString &id) {
        return ElementId(id.toULongLong());
    }

    QString key(ElementId id) {
        return QString::number(id);
    }

    std::string generated(const SemanticModel &model) {
        return SysMLCodeGenerator().generate(model);
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

TEST_CASE("Controller: a part dropped onto a part becomes one of its parts") {
    SemanticModel model;
    DiagramController controller(model);

    const QString package = controller.addElement(DiagramTypes::ToolPackage, 120, 115);
    const QRectF frame = controller.nodeRect(package);
    const QString vehicle = dropPart(controller, frame.x() + 150, frame.y() + 120);
    const QRectF vehicleRect = controller.nodeRect(vehicle);

    const QString engine = dropPart(controller, vehicleRect.center().x(), vehicleRect.center().y());
    CHECK_EQ(model.element(idOf(engine)).owner(), idOf(vehicle));
    CHECK(controller.nodeRect(engine).top() > vehicleRect.bottom()); // placed below its owner
    CHECK_EQ(generated(model), std::string("package Package1 {\n    part part1 {\n        part part1;\n    }\n}\n")); // names are unique per owner

    // definitions are not owned by the part: they stay in the package
    const QString definition = controller.addElement(DiagramTypes::ToolPartDefinition, vehicleRect.center().x(),
                                                     vehicleRect.center().y());
    CHECK_EQ(model.element(idOf(definition)).owner(), idOf(package));
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
    CHECK_EQ(controller.nodes()->nodes().front().Title, QString("engine : Engine [1..*]"));

    // the type and redefined features must be references: no sentences, numbers or keywords
    for (const char *type: {"max Masse Auto 1500kg", "1500", "part", "Engine;", "A, B"})
        CHECK(!controller.updateElement(part, {{"name", "engine"}, {"typeName", type}}).isEmpty());
    for (const char *type: {"Vehicles::Engine", "'my engine'", "~EnginePort", "'part'"})
        CHECK(controller.updateElement(part, {{"name", "engine"}, {"typeName", type}}).isEmpty());
    CHECK(!controller.updateElement(part, {{"name", "engine"}, {"redefines", "eng 2"}}).isEmpty());
    CHECK_EQ(model.element(idOf(part)).typeName(), std::string("'part'"));

    const QString requirement = controller.addElement(DiagramTypes::ToolRequirement, 600, 100);
    CHECK(controller.updateElement(requirement, {{"name", "maxMass"}, {"typeName", "max mass 1500 kg"}})
                  .contains("Text"));
}

TEST_CASE("Controller: multiplicity input") {
    CHECK(DiagramController::parseMultiplicity("").first);
    CHECK(!DiagramController::parseMultiplicity("").second.has_value());
    CHECK_EQ(DiagramController::parseMultiplicity("6").second->toString(), std::string("[6]"));
    CHECK_EQ(DiagramController::parseMultiplicity("[0..1]").second->toString(), std::string("[0..1]"));
    CHECK_EQ(DiagramController::parseMultiplicity("1..*").second->toString(), std::string("[1..*]"));
    CHECK(!DiagramController::parseMultiplicity("x y").first);
    CHECK(!DiagramController::parseMultiplicity("3..1").first);
    // bounds as feature references (table 4: "[m]")
    CHECK_EQ(DiagramController::parseMultiplicity("m").second->toString(), std::string("[m]"));
    CHECK_EQ(DiagramController::parseMultiplicity("[1..n]").second->toString(), std::string("[1..n]"));
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
    CHECK_EQ(entryTexts(part, "assert constraints"), QStringList({QString("ByteRange { VariableA >= 0 and VariableA <= 255 }")}));
    CHECK_EQ(entryTexts(part, "calcs"), QStringList({QString("decodeFuelTrim(raw : Integer) : Rational = raw * 100.0 / 128.0 - 100.0")}));
    CHECK(entryTexts(part, "other").isEmpty()); // the comment is kept in the code, not shown

    const DiagramNode &definition = controller.nodes()->nodes().at(1);
    CHECK(definition.Kind == DiagramTypes::ConstraintDefinitionNode);
    CHECK_EQ(definition.Keyword, QString("«constraint def»"));
    CHECK_EQ(definition.Title, QString("NonNegativeReal"));
    CHECK_EQ(entryTexts(definition, "parameters"), QStringList({QString("in x : Real")}));
    CHECK_EQ(entryTexts(definition, "expression"), QStringList({QString("x >= 0.0")}));
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
    // table 18: the parameters, and the result compartment with what is returned
    CHECK_EQ(entryTexts(decode, "parameters"), QStringList({QString("in raw : Integer")}));
    CHECK_EQ(entryTexts(decode, "result"), QStringList({QString("return trimPercent : Rational = raw * 100.0 / 128.0 - 100.0")}));

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
             std::string("calc def Calc1 {\n    0.0\n}")); // table 18: only the result expression
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
    CHECK_EQ(entryTexts(controller.nodes()->nodes().front(), "assert constraints"), QStringList({QString("constraint1 { true }")}));
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
    CHECK_EQ(entryTexts(*portDef, "directed features"), QStringList({QString("out item fuel : Fuel")}));
    CHECK_EQ(entryTexts(*controller.nodes()->find(model.children(RootId).at(1)), "enums"),
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
    CHECK_EQ(edge.Label, QString(": FuelInterface"));

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
    // an action of an action definition: a node of its own below it (action tree, composition)
    CHECK(entryTexts(*controller.nodes()->find(idOf(action)), "actions").isEmpty());
    const auto actionChild = model.children(idOf(action)).front();
    CHECK(controller.nodes()->find(actionChild) != nullptr);
    CHECK(controller.edges()->find(QStringLiteral("c:") + QString::number(actionChild)) != nullptr);
    CHECK_EQ(entryTexts(*controller.nodes()->find(idOf(states)), "states"), QStringList({QString("state1")}));
    CHECK_EQ(entryTexts(*controller.nodes()->find(idOf(part)), "exhibit states"), QStringList({QString("states : States1")}));
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

    // the node: title with id, compartments doc, subject, stakeholders, constraints (assume)
    controller.syncNow();
    const auto *node = controller.nodes()->find(idOf(definition));
    CHECK(node->Kind == DiagramTypes::RequirementNode);
    CHECK_EQ(node->Title, QString("<'1.1'> MassRequirement"));
    QStringList titles;
    for (const auto &compartment: node->Compartments)
        titles.append(compartment.toMap().value("title").toString());
    CHECK_EQ(titles, QStringList({"doc", "subject", "stakeholders", "assume constraints"}));
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
    QStringList verifies; // deleting changes the edges: collect the keys first
    for (const auto &edge: controller.edges()->edges())
        if (edge.Kind == DiagramTypes::Verify)
            verifies.append(edge.Key);
    for (const QString &verifyKey: verifies)
        controller.deleteEdge(verifyKey);
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

    // the keywords of the nodes: «variation part def», «individual» «part», «snapshot», «analysis def»
    CHECK_EQ(controller.nodes()->find(idOf(choices))->Keyword, QString("«variation part def»"));
    CHECK_EQ(controller.nodes()->find(idOf(choices))->Title, QString("Choices1"));
    CHECK_EQ(controller.nodes()->find(idOf(individual))->Keyword, QString("«individual part»"));
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

TEST_CASE("Structure view: redefinitions, connection and flow definitions") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("part def Engine {\n"
                                        "    part cyl : Cylinder[4..6];\n"
                                        "}\n"
                                        "part def SmallEngine :> Engine {\n"
                                        "    part redefines cyl[4];\n"
                                        "    attribute :>> mass = 120.0;\n"
                                        "}\n"
                                        "part vehicle {\n"
                                        "    part smallEng : SmallEngine redefines eng;\n"
                                        "}\n"
                                        "flow def FuelFlow {\n"
                                        "    ref :>> payload : Fuel;\n"
                                        "    end port fuelOut : FuelOutPort;\n"
                                        "    end port fuelIn : FuelInPort;\n"
                                        "}\n", model).lossless());
    DiagramController structure(model);
    const ElementId small = model.children(RootId)[1];
    const auto *smallNode = structure.nodes()->find(small);
    CHECK(smallNode != nullptr);
    QStringList texts;
    for (const auto &compartment: smallNode->Compartments)
        for (const auto &entry: compartment.toMap().value("entries").toList())
            texts.append(entry.toMap().value("text").toString());
    CHECK(texts.contains(":>> mass = 120.0"));
    const ElementId vehicle = model.children(RootId)[2];
    const ElementId smallEngine = model.children(vehicle).front();
    CHECK_EQ(structure.nodes()->find(smallEngine)->Title, QString("smallEng : SmallEngine\nredefines eng")); // table 4: own line
    const ElementId flow = model.children(RootId)[3];
    const auto *flowNode = structure.nodes()->find(flow);
    CHECK(flowNode != nullptr);
    CHECK_EQ(flowNode->Keyword, QString("«flow def»"));
    CHECK_EQ(DiagramNodeModel::entryCount(*flowNode, "ends"), 2);

    // edited: a name of its own, then named like the redefined feature again
    const ElementId cylinders = model.children(small).front();
    QVariantMap details = structure.elementDetails(key(cylinders));
    CHECK(details.value("hasRedefinitions").toBool());
    CHECK(details.value("nameFromRedefinition").toBool());
    CHECK_EQ(details.value("redefines").toString(), QString("cyl"));
    details["nameFromRedefinition"] = false;
    details["name"] = "cylinders";
    CHECK(structure.updateElement(key(cylinders), details).isEmpty());
    CHECK(generated(model).find("    part cylinders[4] redefines cyl;\n") != std::string::npos);
    details["nameFromRedefinition"] = true;
    details["redefinitionKeyword"] = ":>>";
    CHECK(structure.updateElement(key(cylinders), details).isEmpty());
    CHECK(generated(model).find("    part :>> cyl[4];\n") != std::string::npos);

    // new connection / flow definitions start with their two ends
    structure.addElement(DiagramTypes::ToolConnectionDefinition, 800, 800);
    CHECK(generated(model).find("connection def Connection1 {\n    end part source;\n    end part target;\n}\n") !=
          std::string::npos);
    structure.addElement(DiagramTypes::ToolFlowDefinition, 1100, 800);
    CHECK(generated(model).find("flow def Flow1 {\n    end port source;\n    end port target;\n}\n") != std::string::npos);

    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated(model), again).lossless());
    CHECK_EQ(generated(again), generated(model));
}

TEST_CASE("Structure view: metadata, imports, aliases, views, viewpoints and concerns") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("private import ScalarValues::*;\n"
                                        "alias Car for Automobile;\n"
                                        "metadata def Safety {\n"
                                        "    attribute isMandatory : Boolean;\n"
                                        "}\n"
                                        "part def Automobile;\n"
                                        "part vehicle {\n"
                                        "    part seatBelt {\n"
                                        "        @Safety {\n"
                                        "            isMandatory = true;\n"
                                        "        }\n"
                                        "    }\n"
                                        "}\n"
                                        "metadata Safety about vehicle;\n"
                                        "concern modularity;\n"
                                        "viewpoint structure {\n"
                                        "    frame modularity;\n"
                                        "}\n"
                                        "view def StructureView {\n"
                                        "    filter @SysML::PartUsage;\n"
                                        "    satisfy structure;\n"
                                        "}\n"
                                        "view vehicleView : StructureView {\n"
                                        "    expose vehicle::**;\n"
                                        "    render asTreeDiagram;\n"
                                        "}\n", model).lossless());
    DiagramController structure(model);
    QMap<QString, const DiagramNode *> byKeyword;
    for (const auto &node: structure.nodes()->nodes())
        byKeyword.insert(node.Keyword + " " + node.Title, &node);
    CHECK(byKeyword.contains("«import» private ScalarValues::*"));
    CHECK(!byKeyword.contains("«alias» Car for Automobile")); // an alias of a part def: in its name (table 4)
    CHECK(byKeyword.contains("«metadata def» Safety"));
    CHECK(byKeyword.contains("«metadata» Safety"));
    CHECK(byKeyword.contains("«view def» StructureView"));
    CHECK(byKeyword.contains("«view» vehicleView : StructureView"));
    // the alias in the name compartment of what it names (table 4), by default
    CHECK_EQ(byKeyword.value("«part def» Automobile")->Subtitle, QString("«alias» Car"));
    {
        DiagramController named(model);
        named.setNotationMode("aliases", "name");
        bool aliasNode = false;
        QString subtitle;
        for (const auto &node: named.nodes()->nodes()) {
            aliasNode = aliasNode || node.Keyword == "«alias»";
            if (node.Title == "Automobile")
                subtitle = node.Subtitle;
        }
        CHECK(!aliasNode);
        CHECK_EQ(subtitle, QString("«alias» Car"));
    }
    // «expose» from the view to what it exposes
    CHECK(std::any_of(structure.edges()->edges().begin(), structure.edges()->edges().end(), [&](const DiagramEdge &edge) {
        return edge.Kind == DiagramTypes::Expose && edge.Label == QString("«expose»**") &&
               model.element(edge.Target).name() == "vehicle";
    }));
    // rendered by the view: only what it exposes (vehicle and its parts), in a frame with its expose lines
    const QString vehicleView = byKeyword.value("«view» vehicleView : StructureView") ? key(byKeyword.value("«view» vehicleView : StructureView")->Id) : QString();
    CHECK_EQ(structure.views().size(), 1);
    structure.setViewId(vehicleView);
    CHECK_EQ(structure.viewTitle(), QString("«view» vehicleView : StructureView"));
    CHECK(structure.viewInfo().contains(QString("expose vehicle::**")));
    CHECK(structure.viewInfo().contains(QString("render asTreeDiagram")));
    QStringList shown;
    for (const auto &node: structure.nodes()->nodes())
        shown.append(QString::fromStdString(model.element(node.Id).name()));
    CHECK(shown.contains(QString("vehicle")));
    CHECK(shown.contains(QString("seatBelt")));
    CHECK(!shown.contains(QString("Automobile")));
    structure.setViewId(QString());
    CHECK(structure.viewTitle().isEmpty());
    byKeyword.clear();
    for (const auto &node: structure.nodes()->nodes())
        byKeyword.insert(node.Keyword + " " + node.Title, &node);
    // the metadata is a note with a dashed line to the vehicle (the line replaces the entry "about")
    CHECK(byKeyword.value("«metadata» Safety")->Kind == DiagramTypes::NoteNode);
    CHECK_EQ(DiagramNodeModel::entryCount(*byKeyword.value("«metadata» Safety"), "about"), 0);
    CHECK(std::any_of(structure.edges()->edges().begin(), structure.edges()->edges().end(), [&](const DiagramEdge &edge) {
        return edge.Kind == DiagramTypes::Annotation && model.element(edge.Target).name() == "vehicle";
    }));
    CHECK_EQ(DiagramNodeModel::entryCount(*byKeyword.value("«view» vehicleView : StructureView"), "exposes"), 1);
    CHECK_EQ(DiagramNodeModel::entryCount(*byKeyword.value("«view» vehicleView : StructureView"), "rendering"), 1);
    CHECK_EQ(DiagramNodeModel::entryCount(*byKeyword.value("«view def» StructureView"), "filters"), 1);
    // the metadata of the seat belt is an entry of its node
    const ElementId vehicle = model.children(RootId)[4];
    const ElementId seatBelt = model.children(vehicle).front();
    CHECK_EQ(DiagramNodeModel::entryCount(*structure.nodes()->find(seatBelt), "metadata"), 1);
    // «frame» and «satisfy» arrows
    int frames = 0;
    int satisfies = 0;
    for (const auto &edge: structure.edges()->edges()) {
        frames += edge.Kind == DiagramTypes::Frame ? 1 : 0;
        satisfies += edge.Kind == DiagramTypes::Satisfy ? 1 : 0;
    }
    CHECK_EQ(frames, 1);
    CHECK_EQ(satisfies, 1);

    // drawn: a concern, a frame arrow from the viewpoint to it, metadata onto a part, an import, a filter
    const QString concern = structure.addElement(DiagramTypes::ToolConcern, 1200, 100);
    const ElementId viewpoint = model.children(RootId)[7];
    CHECK(structure.allowedEdgeKinds(key(viewpoint), concern).contains(int(DiagramTypes::Frame)));
    CHECK(structure.createEdge(DiagramTypes::Frame, key(viewpoint), concern));
    CHECK(generated(model).find("    frame concern1;\n") != std::string::npos);
    const QRectF seatBeltRect = structure.nodeRect(key(seatBelt));
    CHECK(!structure.addElement(DiagramTypes::ToolMetadata, seatBeltRect.center().x(), seatBeltRect.center().y()).isEmpty());
    CHECK_EQ(model.children(seatBelt).size(), std::size_t(2));
    CHECK(generated(model).find("        @Safety;\n") != std::string::npos);
    const QString import = structure.addElement(DiagramTypes::ToolImport, 1500, 900);
    CHECK(generated(model).rfind("private import ScalarValues::*;\n") > 0); // a second one, at the end
    QVariantMap details = structure.elementDetails(import);
    CHECK(details.value("hasTarget").toBool());
    CHECK(details.value("anonymous").toBool());
    details["target"] = "ISQ::**";
    details["prefixes"] = "public";
    CHECK(structure.updateElement(import, details).isEmpty());
    CHECK(generated(model).find("public import ISQ::**;\n") != std::string::npos);
    details["target"] = "";
    CHECK(!structure.updateElement(import, details).isEmpty()); // an import needs a target

    // metadata about other elements, edited
    const ElementId about = model.children(RootId)[5];
    details = structure.elementDetails(key(about));
    CHECK(details.value("hasAbout").toBool());
    details["about"] = "vehicle, vehicle::seatBelt";
    CHECK(structure.updateElement(key(about), details).isEmpty());
    CHECK(generated(model).find("metadata Safety about vehicle, vehicle::seatBelt;\n") != std::string::npos);

    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated(model), again).lossless());
    CHECK_EQ(generated(again), generated(model));
}

TEST_CASE("Edges: straight where free, around the nodes in between, side by side") {
    using namespace StructuraSystems::Client::EdgeRouting;
    const QRectF a(0, 0, 100, 60);
    const QRectF b(400, 0, 100, 60);
    // nothing in between: one straight line from border to border
    const auto free = route(a, b, {});
    CHECK_EQ(free.size(), std::size_t(2));
    CHECK(free.front() == QPointF(100, 30));
    CHECK(free.back() == QPointF(400, 30));
    // a node in the way: a route with bends that passes no node
    const QRectF blocker(200, -20, 100, 100);
    const auto around = route(a, b, {blocker});
    CHECK(around.size() > 2);
    CHECK_EQ(crossings(around, {blocker}), 0);
    CHECK(around.front().x() >= 0 && around.back().x() >= 400 - 1e-6);
    // horizontal and vertical segments only
    for (std::size_t i = 1; i < around.size(); ++i)
        CHECK(std::abs(around[i].x() - around[i - 1].x()) < 1e-6 || std::abs(around[i].y() - around[i - 1].y()) < 1e-6);
    // two edges between the same nodes are drawn side by side
    std::vector<DiagramEdge> edges(2);
    edges[0].Key = "r:1";
    edges[0].Source = 1;
    edges[0].Target = 2;
    edges[1].Key = "r:2";
    edges[1].Source = 2;
    edges[1].Target = 1;
    CHECK(parallelOffset(edges, edges[0]) != 0);
    // not on the same line, in both directions: around a node as well
    const auto first = route(a, b, {blocker}, parallelOffset(edges, edges[0]));
    const auto second = route(b, a, {blocker}, parallelOffset(edges, edges[1]));
    std::vector<QPointF> reversed(second.rbegin(), second.rend());
    CHECK(first != reversed);
    const auto straightFirst = route(a, b, {}, parallelOffset(edges, edges[0]));
    const auto straightSecond = route(b, a, {}, parallelOffset(edges, edges[1]));
    CHECK(std::abs(straightFirst.front().y() - straightSecond.back().y()) > 5);
}

TEST_CASE("Controller: type suggestions offer the fitting definitions and library types") {
    SemanticModel model;
    SysMLTextImporter::importText("package P {\n    port def KraftPort;\n    port def Signal;\n    part def Motor;\n"
                                  "    attribute def Mass;\n    enum def Colour;\n    part vehicle {\n"
                                  "        port p;\n        attribute m;\n    }\n}\n", model);
    DiagramController controller(model);
    const auto idNamed = [&](const std::string &name) {
        for (ElementId id = 1; id < 100; ++id)
            if (model.contains(id) && model.element(id).name() == name)
                return key(id);
        return QString();
    };
    const auto texts = [](const QVariantList &list) {
        QStringList result;
        for (const QVariant &entry: list)
            result.append(entry.toMap().value("text").toString());
        return result;
    };

    // a port: port defs only, filtered by the input, "~" kept
    CHECK(texts(controller.typeSuggestions(idNamed("p"), "")) == QStringList({"KraftPort", "Signal"}));
    CHECK(texts(controller.typeSuggestions(idNamed("p"), "kr")) == QStringList({"KraftPort"}));
    CHECK(texts(controller.typeSuggestions(idNamed("p"), "~Kr")) == QStringList({"~KraftPort"}));
    CHECK(texts(controller.portTypeSuggestions("sig")) == QStringList({"Signal"}));
    // an attribute: attribute and enum defs first, then library types
    const QStringList attribute = texts(controller.typeSuggestions(idNamed("m"), ""));
    CHECK(attribute.size() == 12);
    CHECK(attribute.mid(0, 3) == QStringList({"Mass", "Colour", "ScalarValues::Real"}));
    CHECK(texts(controller.typeSuggestions(idNamed("m"), "torque")) == QStringList({"ISQ::TorqueValue"}));
    // a part: part defs
    CHECK(texts(controller.typeSuggestions(idNamed("vehicle"), "")) == QStringList({"Motor"}));

    // known or not
    CHECK(controller.typeStatus(idNamed("p"), "KraftPort").value("known").toBool());
    CHECK(controller.typeStatus(idNamed("p"), "~KraftPort").value("known").toBool());
    CHECK(controller.typeStatus(idNamed("p"), "").value("known").toBool());
    CHECK(controller.typeStatus(idNamed("m"), "Real").value("known").toBool());
    CHECK(controller.typeStatus(idNamed("m"), "ISQ::MassValue").value("known").toBool()); // qualified: a library
    const QVariantMap unknown = controller.typeStatus(idNamed("p"), "~KrafPort");
    CHECK(!unknown.value("known").toBool());
    CHECK_EQ(unknown.value("name").toString(), QString("KrafPort"));
    CHECK_EQ(unknown.value("keyword").toString(), QString("port def"));
    CHECK_EQ(controller.portTypeStatus("Fuel").value("keyword").toString(), QString("port def"));
    CHECK_EQ(controller.typeStatus(idNamed("vehicle"), "Car").value("keyword").toString(), QString("part def"));
}

TEST_CASE("Controller: an unknown type is created as definition in the package") {
    SemanticModel model;
    SysMLTextImporter::importText("package P {\n    part vehicle {\n        port p;\n    }\n}\n", model);
    DiagramController controller(model);
    ElementId port = 0;
    ElementId vehicle = 0;
    for (ElementId id = 1; id < 20; ++id) {
        if (model.contains(id) && model.element(id).name() == "p")
            port = id;
        if (model.contains(id) && model.element(id).name() == "vehicle")
            vehicle = id;
    }
    QVariantMap values = controller.elementDetails(key(port));
    values["typeName"] = "~FuelPort";
    values["createType"] = true;
    CHECK(controller.updateElement(key(port), values).isEmpty());
    controller.syncNow();
    CHECK_EQ(generated(model), std::string("package P {\n    part vehicle {\n        port p : ~FuelPort;\n    }\n"
                                           "    port def FuelPort;\n}\n"));
    // known now: not created a second time
    values["typeName"] = "FuelPort";
    CHECK(controller.updateElement(key(port), values).isEmpty());
    CHECK_EQ(model.elementCount(), std::size_t(4));

    // without the tick nothing is created
    QVariantMap part = controller.elementDetails(key(vehicle));
    part["typeName"] = "Vehicle";
    CHECK(controller.updateElement(key(vehicle), part).isEmpty());
    CHECK_EQ(model.elementCount(), std::size_t(4));
    part["createType"] = true;
    CHECK(controller.updateElement(key(vehicle), part).isEmpty());
    CHECK_EQ(model.elementCount(), std::size_t(5));
    CHECK(generated(model).find("    part def Vehicle;\n") != std::string::npos);
}

TEST_CASE("Controller: ports are listed, added, renamed and removed in the edit dialog") {
    SemanticModel model;
    SysMLTextImporter::importText("package P {\n    port def KraftPort;\n    part motor {\n        port a : KraftPort;\n"
                                  "        port b;\n    }\n    interface def I {\n        end port e;\n    }\n}\n", model);
    DiagramController controller(model);
    ElementId motor = 0;
    ElementId interfaceDefinition = 0;
    for (ElementId id = 1; id < 20; ++id) {
        if (model.contains(id) && model.element(id).name() == "motor")
            motor = id;
        if (model.contains(id) && model.element(id).name() == "I")
            interfaceDefinition = id;
    }
    QVariantMap details = controller.elementDetails(key(motor));
    CHECK(details.value("hasPorts").toBool());
    QVariantList ports = details.value("ports").toList();
    CHECK_EQ(ports.size(), qsizetype(2));
    CHECK_EQ(ports[0].toMap().value("typeName").toString(), QString("KraftPort"));
    CHECK(!controller.elementDetails(key(interfaceDefinition)).value("hasPorts").toBool());

    // rename a, drop b, add two new ones (one with a new port def)
    QVariantMap first = ports[0].toMap();
    first["name"] = "abtrieb";
    ports = {first, QVariantMap{{"id", ""}, {"name", "kuehlung"}, {"typeName", "~CoolantPort"}, {"createType", true}},
             QVariantMap{{"id", ""}, {"name", "signal"}, {"typeName", ""}}};
    details["ports"] = ports;
    CHECK(controller.updateElement(key(motor), details).isEmpty());
    controller.syncNow();
    CHECK_EQ(generated(model), std::string("package P {\n    port def KraftPort;\n    part motor {\n"
                                           "        port abtrieb : KraftPort;\n        port kuehlung : ~CoolantPort;\n"
                                           "        port signal;\n    }\n    interface def I {\n        end port e;\n    }\n"
                                           "    port def CoolantPort;\n}\n"));

    // invalid entries change nothing
    details = controller.elementDetails(key(motor));
    QVariantList twice = details.value("ports").toList();
    twice.append(QVariantMap{{"id", ""}, {"name", "signal"}, {"typeName", ""}});
    details["ports"] = twice;
    CHECK(!controller.updateElement(key(motor), details).isEmpty());
    details["ports"] = QVariantList{QVariantMap{{"id", ""}, {"name", "x"}, {"typeName", "a port"}}};
    CHECK(!controller.updateElement(key(motor), details).isEmpty());
    details["ports"] = QVariantList{QVariantMap{{"id", ""}, {"name", ""}, {"typeName", ""}}};
    CHECK(!controller.updateElement(key(motor), details).isEmpty());
    CHECK(generated(model).find("port kuehlung") != std::string::npos);
}

TEST_CASE("Controller: port labels avoid other ports, labels and nodes") {
    SemanticModel model;
    SysMLTextImporter::importText("part a {\n    port p1;\n    port p2;\n}\npart b;\n", model);
    DiagramController controller(model);
    controller.syncNow();
    ElementId a = 0, b = 0, p1 = 0, p2 = 0;
    for (ElementId id = 1; id < 20; ++id) {
        if (!model.contains(id))
            continue;
        const std::string &name = model.element(id).name();
        (name == "a" ? a : name == "b" ? b : name == "p1" ? p1 : name == "p2" ? p2 : id) = id;
    }
    controller.setNodeSize(key(a), 160, 100);
    controller.setNodeSize(key(b), 160, 60);
    controller.moveNode(key(a), 100, 100);
    controller.moveNode(key(b), 500, 100);
    // both ports on the right border, close to each other
    controller.moveNode(key(p1), 253, 120);
    controller.moveNode(key(p2), 253, 140);
    const int revision = controller.geometryRevision();
    const QRectF r1 = controller.nodeRect(key(p1));
    const QRectF r2 = controller.nodeRect(key(p2));
    CHECK(r1.center().x() == 260.0 && r2.center().x() == 260.0);
    const QPointF o1 = controller.portLabelOffset(key(p1));
    const QPointF o2 = controller.portLabelOffset(key(p2));
    const QRectF l1(r1.topLeft() + o1, QSizeF(30, 16));
    const QRectF l2(r2.topLeft() + o2, QSizeF(30, 16));
    CHECK(o1.x() > 0); // outside, right of the square
    CHECK(!l1.intersects(l2));
    CHECK(!l1.intersects(r2) && !l2.intersects(r1));

    // a node right beside the port: its label moves away from it
    controller.moveNode(key(b), 282, 104);
    CHECK(controller.geometryRevision() > revision);
    const QPointF moved = controller.portLabelOffset(key(p1));
    const QRectF label(controller.nodeRect(key(p1)).topLeft() + moved, QSizeF(30, 16));
    CHECK(!label.intersects(controller.nodeRect(key(b))));

    // a port removed from the model but not yet from the nodes (before the next sync)
    model.removeElement(p2);
    controller.moveNode(key(b), 500, 100);
    CHECK(controller.portLabelOffset(key(p1)).x() > 0);
}

TEST_CASE("Controller: ports that do not fit on the side continue along the bottom border") {
    SemanticModel model;
    SysMLTextImporter::importText("part a {\n    port p1;\n    port p2;\n    port p3;\n    port p4;\n}\n", model);
    DiagramController controller(model);
    controller.syncNow();
    ElementId a = 0;
    std::vector<ElementId> ports;
    for (ElementId id = 1; id < 20; ++id) {
        if (!model.contains(id))
            continue;
        if (model.element(id).name() == "a")
            a = id;
        else
            ports.push_back(id);
    }
    CHECK_EQ(controller.minimumNodeHeight(key(a)), 30.0 + 3 * 24.0 + 7.0 + 10.0); // QML makes it this tall
    controller.setNodeSize(key(a), 160, 80); // here (a size from elsewhere) room for two ports on the right border
    controller.layoutAll();
    const QRectF part = controller.nodeRect(key(a));
    std::vector<QRectF> rects;
    for (const ElementId port: ports)
        rects.push_back(controller.nodeRect(key(port)));
    for (std::size_t i = 0; i < rects.size(); ++i)
        for (std::size_t j = i + 1; j < rects.size(); ++j)
            CHECK(!rects[i].intersects(rects[j]));
    CHECK(rects[0].center().x() == part.right());
    CHECK(rects[1].center().x() == part.right());
    CHECK(rects[2].center().y() == part.bottom());
    CHECK(rects[3].center().y() == part.bottom());

    // grown to its minimum height (as QML does): all on the right border; a port moved by hand stays where it is
    controller.moveNode(key(ports[3]), part.center().x(), part.top() - 7);
    controller.setNodeSize(key(a), 160, controller.minimumNodeHeight(key(a)));
    const QRectF grown = controller.nodeRect(key(a));
    for (std::size_t i = 0; i < 3; ++i)
        CHECK(controller.nodeRect(key(ports[i])).center().x() == grown.right());
    CHECK(controller.nodeRect(key(ports[3])).center().y() == grown.top());
}

namespace {
    const DiagramEdge *edgeOfKind(const DiagramController &controller, int kind) {
        for (const auto &edge: controller.edges()->edges()) {
            if (edge.Kind == kind)
                return &edge;
        }
        return nullptr;
    }
}

TEST_CASE("Controller: package imports nested as dashed packages (table 3), recursive imports as «import» arrows") {
    SemanticModel model;
    SysMLTextImporter::importText("package Definitions { part def Vehicle; }\n"
                                  "package Parts { part def Wheel; }\n"
                                  "package Usages {\n    private import Definitions::*;\n    private import ISQ::*;\n"
                                  "    public import Parts::**;\n    part vehicle : Vehicle;\n}\n",
                                  model);
    DiagramController controller(model);
    // all imports nested: also "Parts::**", marked **
    controller.setNotationMode("imports", "nested");
    bool recursiveNested = false;
    for (const auto &node: controller.nodes()->nodes())
        recursiveNested = recursiveNested || (node.Kind == DiagramTypes::LibraryNode && node.Title == "Parts" && node.Keyword == "**");
    CHECK(recursiveNested);
    CHECK(edgeOfKind(controller, DiagramTypes::Import) == nullptr);
    controller.setNotationMode("imports", "individual"); // the default: table 3 "Import (recursive)" is an arrow

    // "import Definitions::*" and "import ISQ::*": the imported package inside the importing one, «private» Definitions
    QStringList nested;
    for (const auto &node: controller.nodes()->nodes()) {
        if (node.Kind == DiagramTypes::LibraryNode) {
            nested.append(node.Keyword + QLatin1Char(' ') + node.Title);
            CHECK_EQ(model.element(node.Id).owner(), model.children(RootId).at(2));
        }
    }
    nested.sort();
    CHECK_EQ(nested, QStringList({QString("«private» Definitions"), QString("«private» ISQ")}));
    // "import Parts::**" (recursive): an «import» arrow between the packages
    const auto *import = edgeOfKind(controller, DiagramTypes::Import);
    CHECK(import != nullptr);
    if (!import)
        return;
    CHECK_EQ(import->Label, QString("«public import» **"));
    CHECK_EQ(model.element(import->Source).name(), std::string("Usages"));
    CHECK_EQ(model.element(import->Target).name(), std::string("Parts"));
    // vehicle : Vehicle: only in the name (table 3), with "Typing lines" also drawn as "defined by" arrow
    CHECK(edgeOfKind(controller, DiagramTypes::DefinedBy) == nullptr);
    const QString importKey = import->Key;
    controller.setNotationMode("typing", "line"); // (the edges are new)
    const auto *definedBy = edgeOfKind(controller, DiagramTypes::DefinedBy);
    CHECK(definedBy != nullptr);
    if (!definedBy)
        return;
    CHECK_EQ(model.element(definedBy->Target).name(), std::string("Vehicle"));

    // deleting the arrows removes the import and the type
    controller.deleteEdge(importKey);
    CHECK(generated(model).find("import Parts::**") == std::string::npos);
    const auto *typed = edgeOfKind(controller, DiagramTypes::DefinedBy);
    CHECK(typed != nullptr);
    if (!typed)
        return;
    controller.deleteEdge(QString(typed->Key));
    CHECK(generated(model).find("part vehicle;") != std::string::npos);
}

TEST_CASE("Controller: drawing «import», defined-by and perform arrows writes the SysML") {
    SemanticModel model;
    SysMLTextImporter::importText("package A { part def Engine; action def Start; }\npackage B {\n    part engine;\n"
                                  "    action start;\n}\n",
                                  model);
    DiagramController controller(model);
    const ElementId a = model.children(RootId).at(0);
    const ElementId b = model.children(RootId).at(1);
    const ElementId engineDef = model.children(a).at(0);
    const ElementId engine = model.children(b).at(0);
    const ElementId start = model.children(b).at(1);

    controller.setNotationMode("typing", "line");
    CHECK(controller.createEdge(DiagramTypes::Import, key(b), key(a)));
    CHECK(controller.createEdge(DiagramTypes::DefinedBy, key(engine), key(engineDef)));
    CHECK(controller.createEdge(DiagramTypes::Performs, key(engine), key(start)));
    const std::string text = generated(model);
    CHECK(text.find("private import A::*;") != std::string::npos);
    CHECK(text.find("part engine : Engine {") != std::string::npos);
    CHECK(text.find("perform start;") != std::string::npos);
    // "import A::*": A drawn in B as dashed package (nested notation of table 3), no arrow
    CHECK(edgeOfKind(controller, DiagramTypes::Import) == nullptr);
    bool nested = false;
    for (const auto &node: controller.nodes()->nodes())
        nested = nested || (node.Kind == DiagramTypes::LibraryNode && node.Title == "A" && model.element(node.Id).owner() == b);
    CHECK(nested);
    CHECK(edgeOfKind(controller, DiagramTypes::DefinedBy) != nullptr);
    CHECK(edgeOfKind(controller, DiagramTypes::Performs) != nullptr);

    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
}

TEST_CASE("Controller: ref parts are aggregation, redefined parts get a redefinition arrow") {
    SemanticModel model;
    SysMLTextImporter::importText("part vehicle {\n    ref part driver;\n    part engine;\n}\n"
                                  "part vehicle2 :> vehicle {\n    part redefines engine;\n}\n",
                                  model);
    DiagramController controller(model);
    CHECK(edgeOfKind(controller, DiagramTypes::Aggregation) != nullptr);
    const auto *redefinition = edgeOfKind(controller, DiagramTypes::Redefinition);
    CHECK(redefinition != nullptr);
    if (!redefinition)
        return;
    CHECK_EQ(model.element(redefinition->Target).name(), std::string("engine"));
    CHECK(model.element(redefinition->Target).owner() == model.children(RootId).at(0));
}

TEST_CASE("Controller: inherited features with ^, perform actions, exhibit states and state actions (slides 28, 63, 85)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "part def Vehicle {\n"
            "    attribute mass;\n"
            "    attribute velocity;\n"
            "    port fuelCmdPort;\n"
            "    perform providePower;\n"
            "    exhibit state vehicleStates;\n"
            "    assert constraint { mass <= 2000 }\n"
            "}\n"
            "action providePower;\n"
            "state def VehicleStates {\n"
            "    entry action1;\n"
            "    do action2;\n"
            "    exit action3;\n"
            "}\n"
            "action action1;\n"
            "action action2;\n"
            "action action3;\n"
            "part vehicle_b : Vehicle {\n"
            "    attribute redefines mass = 1500;\n"
            "    part fuelTank;\n"
            "}\n"
            "state vehicleStates : VehicleStates {\n"
            "    do action2b :>> action2;\n"
            "}\n", model).lossless());
    DiagramController controller(model);
    const auto node = [&](const std::string &name) -> const DiagramNode & {
        for (const ElementId child: model.children(RootId))
            if (model.element(child).name() == name)
                return *controller.nodes()->find(child);
        return controller.nodes()->nodes().front();
    };
    const DiagramNode &vehicle = node("Vehicle");
    CHECK_EQ(entryTexts(vehicle, "perform actions"), QStringList({QString("providePower")}));
    CHECK_EQ(entryTexts(vehicle, "exhibit states"), QStringList({QString("vehicleStates")}));
    CHECK_EQ(entryTexts(vehicle, "assert constraints").size(), 1);

    const DiagramNode &vehicleB = node("vehicle_b");
    CHECK_EQ(entryTexts(vehicleB, "attributes"), QStringList({QString("^velocity"), QString("redefines mass = 1500")}));
    CHECK_EQ(entryTexts(vehicleB, "ports"), QStringList({QString("^fuelCmdPort")}));
    CHECK_EQ(entryTexts(vehicleB, "perform actions"), QStringList({QString("^providePower")}));
    CHECK_EQ(entryTexts(vehicleB, "exhibit states"), QStringList({QString("^vehicleStates")}));
    CHECK_EQ(entryTexts(vehicleB, "assert constraints").size(), 1);
    CHECK(entryTexts(vehicleB, "assert constraints").front().startsWith("^"));

    const DiagramNode &states = node("VehicleStates");
    CHECK_EQ(entryTexts(states, "state actions"),
             QStringList({QString("entry action1"), QString("do action2"), QString("exit action3")}));
    CHECK(entryTexts(states, "actions").isEmpty()); // not "perform action1"
    const DiagramNode &usage = node("vehicleStates");
    CHECK_EQ(entryTexts(usage, "state actions"),
             QStringList({QString("entry ^action1"), QString("do action2b :>> action2"), QString("exit ^action3")}));
}

TEST_CASE("Controller: calc def parameters with return, calc usage with bound values (slides 79, 80)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "calc def Force {\n"
            "    in attribute m :> ISQ::mass;\n"
            "    in attribute a :> ISQ::acceleration;\n"
            "    return f :> ISQ::force = m * a;\n"
            "}\n"
            "part vehicle {\n"
            "    calc force : Force {\n"
            "        in m = 1500 [kg];\n"
            "        in a = 9.806 [m / s ^ 2];\n"
            "    }\n"
            "}\n", model).lossless());
    DiagramController controller(model);
    const DiagramNode &force = *controller.nodes()->find(model.children(RootId).at(0));
    CHECK_EQ(entryTexts(force, "parameters"), QStringList({QString("in m :> ISQ::mass"), QString("in a :> ISQ::acceleration")}));
    CHECK_EQ(entryTexts(force, "result"), QStringList({QString("return f :> ISQ::force = m * a")}));
    const DiagramNode &vehicle = *controller.nodes()->find(model.children(RootId).at(1));
    CHECK_EQ(entryTexts(vehicle, "calcs"), QStringList({QString("force : Force(m = 1500 [kg], a = 9.806 [m / s ^ 2])")}));
}

TEST_CASE("Controller: perform arrows to actions of an action tree (slide 51)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "part vehicle_b {\n"
            "    perform providePower;\n"
            "    part engine {\n"
            "        perform providePower.generateTorque;\n"
            "    }\n"
            "}\n"
            "action providePower {\n"
            "    action generateTorque;\n"
            "}\n", model).lossless());
    DiagramController controller(model);
    int performs = 0;
    for (const auto &edge: controller.edges()->edges()) {
        if (edge.Kind != DiagramTypes::Performs)
            continue;
        ++performs;
        const std::string target = model.element(edge.Target).name();
        CHECK(target == "providePower" || target == "generateTorque");
        CHECK(model.element(edge.Target).kind() == ElementKind::ActionUsage);
    }
    CHECK_EQ(performs, 2);
}

TEST_CASE("Interconnection: a part with its parts, items and ports nested, connections between them (slides 39, 40)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "port def FuelOutPort {\n    out item fuel : Fuel;\n}\n"
            "item def Fuel;\n"
            "part def Vehicle;\n"
            "part vehicle_b : Vehicle {\n"
            "    part fuelTank {\n"
            "        ref item fuel : Fuel;\n"
            "        port p1 : FuelOutPort;\n"
            "    }\n"
            "    part engine {\n"
            "        port p2 : ~FuelOutPort;\n"
            "    }\n"
            "    connect fuelTank.p1 to engine.p2;\n"
            "    flow of Fuel from fuelTank.p1.fuel to engine.p2.fuel;\n"
            "}\n"
            "part other;\n", model).lossless());
    DiagramController view(model, nullptr, true);
    CHECK(view.interconnection());
    CHECK_EQ(view.contexts().size(), 3); // vehicle_b, fuelTank, engine
    const ElementId vehicle = model.children(RootId).at(3);
    view.setContextId(key(vehicle));
    CHECK_EQ(view.contextId(), key(vehicle));
    // vehicle_b, fuelTank, fuel, p1, engine, p2 (not "other", no definitions)
    CHECK_EQ(view.nodes()->count(), 6);
    const ElementId fuelTank = model.children(vehicle).at(0);
    const ElementId engine = model.children(vehicle).at(1);
    const ElementId fuel = model.children(fuelTank).at(0);
    const auto *vehicleNode = view.nodes()->find(vehicle);
    const auto *tankNode = view.nodes()->find(fuelTank);
    CHECK(vehicleNode->Container);
    CHECK(tankNode->Container);
    CHECK(!view.nodes()->find(engine)->Container);
    CHECK(vehicleNode->Rect.contains(tankNode->Rect));
    CHECK(vehicleNode->Rect.contains(view.nodes()->find(engine)->Rect));
    CHECK(tankNode->Rect.contains(view.nodes()->find(fuel)->Rect));
    CHECK(view.nodes()->find(fuel)->Dashed); // ref item
    CHECK_EQ(view.nodes()->find(fuel)->Keyword, QString("«item»"));
    int connections = 0;
    int flows = 0;
    for (const auto &edge: view.edges()->edges()) {
        CHECK(edge.Kind == DiagramTypes::Connection || edge.Kind == DiagramTypes::Flow);
        connections += edge.Kind == DiagramTypes::Connection ? 1 : 0;
        flows += edge.Kind == DiagramTypes::Flow ? 1 : 0;
    }
    CHECK_EQ(connections, 1);
    CHECK_EQ(flows, 1);
    // moving the part moves what is inside
    const QRectF before = view.nodes()->find(fuel)->Rect;
    view.moveNode(key(fuelTank), tankNode->Rect.x() + 20, tankNode->Rect.y() + 10);
    CHECK(view.nodes()->find(fuel)->Rect.topLeft() == before.topLeft() + QPointF(20, 10));
    // drawing: a part dropped into engine becomes its part
    const QRectF engineRect = view.nodes()->find(engine)->Rect;
    const QString added = view.addElement(DiagramTypes::ToolPart, engineRect.center().x(), engineRect.center().y());
    CHECK(!added.isEmpty());
    CHECK_EQ(model.element(idOf(added)).owner(), engine);
    view.syncNow();
    CHECK(view.nodes()->find(engine)->Container);
}

TEST_CASE("Interconnection: inherited ports on the border of the parts, interfaces and messages between them") {
    const std::string text =
            "port def FuelPort {\n    out item fuel : Fuel;\n}\n"
            "item def Fuel;\n"
            "interface def FuelLink {\n    end supply : FuelPort;\n    end demand : ~FuelPort;\n}\n"
            "part def Tank {\n    port fuelOut : FuelPort;\n    port drain : FuelPort;\n}\n"
            "part def Engine {\n    port fuelIn : ~FuelPort;\n}\n"
            "part def Vehicle {\n"
            "    part tank : Tank;\n"
            "    part engine : Engine;\n"
            "    interface link : FuelLink connect tank.fuelOut to engine.fuelIn;\n"
            "    flow of Fuel from tank.fuelOut.fuel to engine.fuelIn.fuel;\n"
            "    message m of Fuel from tank to engine;\n"
            "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    const ElementId vehicle = model.children(RootId).at(5);
    const ElementId tank = model.children(vehicle).at(0);
    const ElementId engine = model.children(vehicle).at(1);
    DiagramController view(model, nullptr, true);
    view.setContextId(key(vehicle));
    view.syncNow();
    // fuelOut, drain (of Tank) on tank, fuelIn (of Engine) on engine: nodes of their own on the border
    std::map<QString, const DiagramNode *> ports;
    for (const auto &node: view.nodes()->nodes()) {
        if (node.Kind == DiagramTypes::PortNode)
            ports[node.Title] = &node;
    }
    CHECK_EQ(ports.size(), std::size_t(3));
    CHECK(ports.count("fuelOut : FuelPort") > 0);
    CHECK(ports.count("fuelIn : ~FuelPort") > 0);
    if (ports.size() != 3)
        return;
    const auto *fuelOut = ports.at("fuelOut : FuelPort");
    const auto *fuelIn = ports.at("fuelIn : ~FuelPort");
    const auto *drain = ports.at("drain : FuelPort");
    const QRectF tankRect = view.nodes()->find(tank)->Rect;
    const QRectF engineRect = view.nodes()->find(engine)->Rect;
    CHECK(tankRect.adjusted(-8, -8, 8, 8).contains(fuelOut->Rect.center()));
    CHECK(!tankRect.adjusted(8, 8, -8, -8).contains(fuelOut->Rect.center())); // on the border
    CHECK(engineRect.adjusted(-8, -8, 8, 8).contains(fuelIn->Rect.center()));
    CHECK_EQ(fuelOut->Keyword, QString("out"));
    CHECK_EQ(fuelIn->Keyword, QString("in"));
    CHECK_EQ(view.portSide(key(fuelIn->Id)), QString("left")); // incoming: left border
    // no "^fuelOut" in a compartment of tank anymore: it is drawn on the border
    for (const auto &compartment: view.nodes()->find(tank)->Compartments)
        CHECK(compartment.toMap().value("title").toString() != QString("ports"));
    int interfaces = 0;
    int flows = 0;
    int messages = 0;
    for (const auto &edge: view.edges()->edges()) {
        if (edge.Kind == DiagramTypes::Interface || edge.Kind == DiagramTypes::Flow) {
            CHECK_EQ(edge.Source, fuelOut->Id);
            CHECK_EQ(edge.Target, fuelIn->Id);
            CHECK(!EdgeRouting::routeOf(*view.nodes(), view.edges()->edges(), edge).empty());
        }
        if (edge.Kind == DiagramTypes::Message) {
            CHECK_EQ(edge.Source, tank);
            CHECK_EQ(edge.Target, engine);
        }
        interfaces += edge.Kind == DiagramTypes::Interface ? 1 : 0;
        flows += edge.Kind == DiagramTypes::Flow ? 1 : 0;
        messages += edge.Kind == DiagramTypes::Message ? 1 : 0;
    }
    CHECK_EQ(interfaces, 1);
    CHECK_EQ(flows, 1);
    CHECK_EQ(messages, 1);
    // an inherited port can be moved along the border, not selected or deleted as an element
    view.moveNode(key(drain->Id), tankRect.center().x(), tankRect.bottom());
    CHECK(view.nodeRect(key(drain->Id)).isValid());
    // drawing a connection between inherited ports: "connect tank.drain to engine.fuelIn;"
    const QVariantList kinds = view.allowedEdgeKinds(key(drain->Id), key(fuelIn->Id));
    CHECK(kinds.contains(int(DiagramTypes::Connection)));
    CHECK(kinds.contains(int(DiagramTypes::Interface)));
    CHECK(!kinds.contains(int(DiagramTypes::Specialization)));
    CHECK(view.createEdge(DiagramTypes::Connection, key(drain->Id), key(fuelIn->Id)));
    const std::string generated = SysMLCodeGenerator().generate(model);
    CHECK(generated.find("connect tank.drain to engine.fuelIn;") != std::string::npos);
    // the same once more: it exists already
    CHECK(!view.allowedEdgeKinds(key(drain->Id), key(fuelIn->Id)).contains(int(DiagramTypes::Connection)));
    // the new connection runs between the two port nodes, and the text reads back the same
    view.syncNow();
    int connections = 0;
    for (const auto &edge: view.edges()->edges())
        connections += edge.Kind == DiagramTypes::Connection ? 1 : 0;
    CHECK_EQ(connections, 1);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated, again).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(again), generated);
}

TEST_CASE("Controller: attributes and ports on package level, all compartments on request") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "package P {\n"
            "    port def FuelPort;\n"
            "    attribute mass : Real = 5;\n"
            "    port fuel : FuelPort;\n"
            "    requirement r1;\n"
            "    action drive;\n"
            "    part def Vehicle {\n"
            "        part engine {\n"
            "            port p : FuelPort;\n"
            "            perform drive;\n"
            "        }\n"
            "        part tank {\n"
            "            port q : FuelPort;\n"
            "        }\n"
            "        connect tank.q to engine.p;\n"
            "        flow from tank.q to engine.p;\n"
            "        satisfy r1 by engine;\n"
            "        individual part v1;\n"
            "        snapshot part s1;\n"
            "    }\n"
            "}\n", model).lossless());
    DiagramController view(model);
    view.syncNow();
    const ElementId package = model.children(RootId).at(0);
    const ElementId mass = model.children(package).at(1);
    const ElementId fuel = model.children(package).at(2);
    const ElementId drive = model.children(package).at(4);
    const ElementId vehicle = model.children(package).at(5);
    const ElementId engine = model.children(vehicle).at(0);
    // package level: rounded usages of their own (not a text node, not a port on a border)
    CHECK_EQ(view.nodes()->find(mass)->Kind, DiagramTypes::PartNode);
    CHECK_EQ(view.nodes()->find(mass)->Keyword, QString("«attribute»"));
    CHECK_EQ(view.nodes()->find(mass)->Title, QString("mass : Real = 5"));
    CHECK_EQ(view.nodes()->find(fuel)->Kind, DiagramTypes::PartNode);
    CHECK_EQ(view.nodes()->find(fuel)->Keyword, QString("«port»"));
    // the further compartments only when switched on
    CHECK(entryTexts(*view.nodes()->find(vehicle), "connections").isEmpty());
    view.setAllCompartments(true);
    CHECK(view.allCompartments());
    CHECK_EQ(entryTexts(*view.nodes()->find(vehicle), "connections"), QStringList({QString("connect tank.q to engine.p")}));
    CHECK_EQ(entryTexts(*view.nodes()->find(vehicle), "flows"), QStringList({QString("flow from tank.q to engine.p")}));
    CHECK_EQ(entryTexts(*view.nodes()->find(vehicle), "individuals"), QStringList({QString("part v1")}));
    CHECK_EQ(entryTexts(*view.nodes()->find(vehicle), "snapshots"), QStringList({QString("s1")}));
    const QStringList relationships = entryTexts(*view.nodes()->find(engine), "relationships");
    CHECK(relationships.contains(QString("satisfies r1")));
    CHECK(relationships.contains(QString("performs drive")));
    CHECK(entryTexts(*view.nodes()->find(drive), "relationships").contains(QString("performed by engine")));
    view.setAllCompartments(false);
    CHECK(entryTexts(*view.nodes()->find(vehicle), "connections").isEmpty());
}

TEST_CASE("Structure view: doc as note, with all compartments as compartment doc (table 2)") {
    SemanticModel model;
    const std::string text =
            "package P {\n"
            "    doc /* about the package */\n"
            "    part def Part1;\n"
            "    port def P1;\n"
            "    part part1 : Part1 {\n"
            "        doc /* documentation-text */\n"
            "        doc Document1 locale \"fr_BE\" /* Ceci n'est pas une pipe. */\n"
            "        port p1 : P1 {\n"
            "            doc /* of the port */\n"
            "        }\n"
            "    }\n"
            "}\n";
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController view(model);
    view.syncNow();
    const auto notes = [&] {
        int count = 0;
        for (const auto &node: view.nodes()->nodes())
            count += node.Kind == DiagramTypes::NoteNode ? 1 : 0;
        return count;
    };
    const ElementId package = model.children(RootId).at(0);
    const ElementId part1 = model.children(package).at(3);
    CHECK_EQ(notes(), 4); // package doc, two docs of part1, doc of the port
    CHECK(entryTexts(*view.nodes()->find(part1), "doc").isEmpty());
    view.setAllCompartments(true);
    // the docs of part1 are its compartment 'doc'; the package doc and the doc of the port stay notes
    CHECK_EQ(notes(), 2);
    CHECK_EQ(entryTexts(*view.nodes()->find(part1), "doc"),
             QStringList({QString("documentation-text"), QString("Document1 locale \"fr_BE\""),
                          QString("Ceci n'est pas une pipe.")}));
    CHECK_EQ(view.nodes()->find(part1)->Compartments.first().toMap().value("title").toString(), QString("doc"));
    for (const auto &edge: view.edges()->edges())
        CHECK(!(edge.Kind == DiagramTypes::Annotation && edge.Target == part1));
    view.setAllCompartments(false);
    CHECK_EQ(notes(), 4);
    CHECK_EQ(generated(model), text);
}

TEST_CASE("Structure view: moving a note of a calc def keeps it in the calc def (no reparenting)") {
    SemanticModel model;
    const std::string text =
            "package P {\n"
            "    calc def square {\n"
            "        in attribute x : Real;\n"
            "        rep square language \"Python\" /* return x**2 */\n"
            "    }\n"
            "    part def A;\n"
            "}\n"
            "part def B;\n";
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController view(model);
    view.syncNow();
    const ElementId package = model.children(RootId).at(0);
    const ElementId square = model.children(package).at(0);
    const ElementId rep = model.children(square).at(1);
    const ElementId a = model.children(package).at(1);
    const ElementId b = model.children(RootId).at(1);
    // the note dragged far out of the package frame, onto the empty canvas
    view.moveNode(key(rep), 3000, 3000);
    view.finishMove(key(rep));
    CHECK_EQ(model.element(rep).owner(), square);
    CHECK_EQ(generated(model), text);
    CHECK(std::any_of(view.edges()->edges().begin(), view.edges()->edges().end(), [&](const DiagramEdge &edge) {
        return edge.Kind == DiagramTypes::Annotation && edge.Source == rep && edge.Target == square;
    }));
    // what a package owns still goes where it is dropped: A out of the package, B into it
    view.moveNode(key(a), 3000, 3000);
    view.finishMove(key(a));
    CHECK_EQ(model.element(a).owner(), RootId);
    const QRectF frame = view.nodeRect(key(package));
    view.moveNode(key(b), frame.x() + 30, frame.y() + 60);
    view.finishMove(key(b));
    CHECK_EQ(model.element(b).owner(), package);
}

TEST_CASE("Structure view: a comment about an inherited feature ends at its entry ^name") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "part def Part2 {\n"
            "    attribute a1;\n"
            "}\n"
            "comment C2 about part2::a1 /* inherited */\n"
            "part part2 : Part2;\n", model).lossless());
    DiagramController view(model);
    view.syncNow();
    const ElementId part2 = model.children(RootId).at(2);
    const ElementId a1 = model.children(model.children(RootId).at(0)).at(0);
    CHECK(entryTexts(*view.nodes()->find(part2), "attributes").contains(QString("^a1")));
    CHECK(std::any_of(view.edges()->edges().begin(), view.edges()->edges().end(), [&](const DiagramEdge &edge) {
        return edge.Kind == DiagramTypes::Annotation && edge.Target == part2 && edge.TargetEntry == key(a1);
    }));
}

TEST_CASE("Structure view: the line of a doc note ends between the ports of its part") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "part part1 {\n"
            "    doc /* documentation-text */\n"
            "    port p1;\n"
            "    port p2;\n"
            "}\n", model).lossless());
    DiagramController view(model);
    view.syncNow();
    const ElementId part1 = model.children(RootId).at(0);
    const ElementId doc = model.children(part1).at(0);
    const QRectF part = view.nodeRect(key(part1));
    // the note right of the part, at the height of the first port
    const QRectF port1 = view.nodeRect(key(model.children(part1).at(1)));
    view.moveNode(key(doc), part.right() + 120, port1.center().y() - 20);
    view.setNodeSize(key(doc), 160, 40);
    for (const auto &edge: view.edges()->edges()) {
        if (edge.Kind != DiagramTypes::Annotation)
            continue;
        const auto route = EdgeRouting::routeOf(*view.nodes(), view.edges()->edges(), edge);
        CHECK_EQ(route.size(), std::size_t(2));
        if (route.size() != 2)
            continue;
        CHECK(std::abs(route.back().x() - part.right()) < 0.5); // on the border facing the note
        for (const ElementId port: {model.children(part1).at(1), model.children(part1).at(2)})
            CHECK(!view.nodeRect(key(port)).adjusted(-4, -4, 4, 4).contains(route.back()));
    }
}

TEST_CASE("Structure view: a comment dragged onto a part offers the part and its features") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "part def Part1 {\n"
            "    attribute inheritedOne;\n"
            "}\n"
            "comment c1 /* note */\n"
            "part part1 : Part1 {\n"
            "    attribute attribute1;\n"
            "    port p1;\n"
            "}\n", model).lossless());
    DiagramController view(model);
    view.syncNow();
    const QString note = key(model.children(RootId).at(1));
    const QString part1 = key(model.children(RootId).at(2));
    const QVariantList targets = view.annotationTargets(note, part1);
    QStringList paths;
    for (const auto &target: targets)
        paths.append(target.toMap().value("path").toString());
    CHECK_EQ(paths, QStringList({QString(), QString("attribute1"), QString("p1"), QString("inheritedOne")}));
    CHECK(targets.at(3).toMap().value("label").toString().startsWith(QString("^")));
    // the attribute: "comment c1 about part1::attribute1"
    CHECK(view.createAnnotation(note, part1, "attribute1"));
    CHECK(generated(model).find("comment c1 about part1::attribute1 /* note */") != std::string::npos);
    // and an inherited one besides it
    CHECK(view.createAnnotation(note, part1, "inheritedOne"));
    CHECK(generated(model).find("comment c1 about part1::attribute1, part1::inheritedOne /* note */") != std::string::npos);
    view.syncNow();
    int toEntries = 0;
    for (const auto &edge: view.edges()->edges())
        toEntries += edge.Kind == DiagramTypes::Annotation && !edge.TargetEntry.isEmpty() ? 1 : 0;
    CHECK_EQ(toEntries, 2);
    // from a part nothing is offered (a part is not a note)
    CHECK(view.annotationTargets(part1, note).isEmpty());
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated(model), again).lossless());
    CHECK_EQ(generated(again), generated(model));
}

TEST_CASE("Grid and browser view: relationship matrix and membership tree (table 34)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "package R {\n"
            "    requirement r1;\n"
            "    requirement r2;\n"
            "    part vehicle {\n"
            "        part engine;\n"
            "        satisfy r1 by engine;\n"
            "    }\n"
            "}\n", model).lossless());
    DiagramController view(model);
    CHECK(!view.matrixKinds().isEmpty());
    QVariantMap matrix = view.relationshipMatrix(DiagramTypes::Satisfy);
    const QVariantList rows = matrix.value("rows").toList();
    const QVariantList columns = matrix.value("columns").toList();
    CHECK_EQ(rows.size(), 2);    // r1, r2
    CHECK_EQ(columns.size(), 2); // vehicle, engine
    const auto cell = [&](const QVariantMap &m, int row, int column) {
        return m.value("cells").toList().at(row).toList().at(column).toBool();
    };
    CHECK_EQ(rows.at(0).toMap().value("title").toString(), QString("R::r1"));
    CHECK_EQ(columns.at(1).toMap().value("title").toString(), QString("R::vehicle::engine"));
    CHECK(cell(matrix, 0, 1));  // r1 satisfied by engine
    CHECK(!cell(matrix, 1, 0));
    // a click into a cell: "satisfy r2 by vehicle;" - and once more: removed again
    const QString r2 = rows.at(1).toMap().value("id").toString();
    const QString vehicle = columns.at(0).toMap().value("id").toString();
    CHECK(view.toggleMatrixCell(DiagramTypes::Satisfy, r2, vehicle).isEmpty());
    CHECK(cell(view.relationshipMatrix(DiagramTypes::Satisfy), 1, 0));
    CHECK(generated(model).find("satisfy r2 by vehicle;") != std::string::npos);
    CHECK(view.toggleMatrixCell(DiagramTypes::Satisfy, r2, vehicle).isEmpty());
    CHECK(!cell(view.relationshipMatrix(DiagramTypes::Satisfy), 1, 0));
    CHECK(view.relationshipMatrix(4711).isEmpty());
    // browser: depth first with the depth of nesting
    const QVariantList tree = view.browserTree();
    CHECK_EQ(tree.size(), 5); // R, r1, r2, vehicle, engine (the satisfy is a relationship)
    CHECK_EQ(tree.at(0).toMap().value("name").toString(), QString("R"));
    CHECK_EQ(tree.at(0).toMap().value("depth").toInt(), 0);
    CHECK_EQ(tree.at(4).toMap().value("name").toString(), QString("engine"));
    CHECK_EQ(tree.at(4).toMap().value("depth").toInt(), 2);
    CHECK_EQ(tree.at(3).toMap().value("keyword").toString(), QString("part"));
}

TEST_CASE("Controller: action def parameters, port def directed features (slide 24)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "port def FuelCmdPort {\n"
            "    in item fuelCmd : FuelCmd;\n"
            "    in attribute x : Real;\n"
            "    attribute y : Real;\n"
            "}\n"
            "action def ProvidePower {\n"
            "    in pwrCmd : PwrCmd;\n"
            "    out torqueToWheels : Torque[*];\n"
            "}\n", model).lossless());
    DiagramController controller(model);
    const DiagramNode &port = *controller.nodes()->find(model.children(RootId).at(0));
    CHECK_EQ(entryTexts(port, "directed features"), QStringList({QString("in item fuelCmd : FuelCmd"), QString("in attribute x : Real")}));
    CHECK_EQ(entryTexts(port, "attributes"), QStringList({QString("y : Real")}));
    const DiagramNode &action = *controller.nodes()->find(model.children(RootId).at(1));
    CHECK_EQ(entryTexts(action, "parameters"), QStringList({QString("in pwrCmd : PwrCmd"), QString("out torqueToWheels : Torque [*]")}));
}

TEST_CASE("Controller: comments, doc, rep and metadata are notes with annotation lines (tables 2, 26)") {
    SemanticModel model;
    const std::string text =
            "package P {\n"
            "    part def Part1 {\n"
            "        attribute attribute1 : Real;\n"
            "        doc /* documentation-text */\n"
            "    }\n"
            "    part part1 : Part1;\n"
            "    comment c1 about part1 /* About part1. */\n"
            "    comment c2 about Part1::attribute1 /* The annotated element is attribute1. */\n"
            "    /* A comment of the package. */\n"
            "    // not an element: no note\n"
            "    metadata def Tool { attribute toolName : String; }\n"
            "    metadata Tool about part1 {\n"
            "        toolName = \"X\";\n"
            "    }\n"
            "}\n";
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    int notes = 0;
    for (const auto &node: controller.nodes()->nodes())
        notes += node.Kind == DiagramTypes::NoteNode ? 1 : 0;
    CHECK_EQ(notes, 5); // doc, c1, c2, the bare comment, the metadata
    QStringList annotations;
    const auto edgesBefore = controller.edges()->edges(); // a copy: setEntryRect below replaces the edges
    for (const auto &edge: edgesBefore) {
        if (edge.Kind != DiagramTypes::Annotation)
            continue;
        // at an entry of a compartment the line ends at that entry (table 2), its id names the annotated element
        annotations.append(QString::fromStdString(model.element(edge.Target).name()) +
                           (edge.TargetEntry.isEmpty() ? QString()
                            : QStringLiteral(".") + QString::fromStdString(model.element(edge.TargetEntry.toULongLong()).name())));
        if (!edge.TargetEntry.isEmpty()) {
            CHECK(edge.Label.isEmpty());
            CHECK(controller.annotatedEntries(key(edge.Target)).contains(edge.TargetEntry));
            // where the node reports the entry, the line ends: at the side of the node, at the height of the entry
            controller.setEntryRect(key(edge.Target), edge.TargetEntry, QRectF(4, 40, 100, 18));
        }
    }
    annotations.sort();
    CHECK_EQ(annotations, QStringList({"Part1", "Part1.attribute1", "part1", "part1"}));
    for (const auto &edge: controller.edges()->edges()) {
        if (edge.TargetEntry.isEmpty())
            continue;
        const auto route = EdgeRouting::routeOf(*controller.nodes(), controller.edges()->edges(), edge);
        const QRectF target = controller.nodes()->find(edge.Target)->Rect;
        CHECK(!route.empty());
        if (!route.empty())
            CHECK(std::abs(route.back().y() - (target.top() + 49)) < 0.5);
    }
    const ElementId package = model.children(RootId).front();
    const ElementId c1 = model.children(package).at(2);
    const auto *note = controller.nodes()->find(c1);
    CHECK_EQ(note->Keyword, QString("«comment»"));
    CHECK_EQ(note->Title, QString("c1"));
    CHECK_EQ(DiagramNodeModel::entryCount(*note, ""), 1);

    // a new comment on the canvas, drawn to part1: "about part1"; the line removed again
    const QString comment = controller.addElement(DiagramTypes::ToolComment, 900, 900);
    CHECK(!comment.isEmpty());
    const QString part1 = key(model.children(package).at(1));
    CHECK(controller.allowedEdgeKinds(comment, part1).contains(int(DiagramTypes::Annotation)));
    CHECK(controller.createEdge(DiagramTypes::Annotation, comment, part1));
    CHECK(generated(model).find("comment comment1 about P::part1 /* Comment */") != std::string::npos);
    const QString key = QStringLiteral("a:") + comment + QStringLiteral(":0");
    CHECK(controller.edges()->find(key) != nullptr);
    controller.deleteEdge(key);
    CHECK(generated(model).find("comment comment1 /* Comment */") != std::string::npos);
    // a doc dropped onto the part definition
    controller.addElement(DiagramTypes::ToolDocumentation, controller.nodeRect(part1).center().x(), controller.nodeRect(part1).center().y());
    CHECK(generated(model).find("    part part1 : Part1 {\n        doc /* Documentation */\n    }\n") != std::string::npos);

    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated(model), again).lossless());
    CHECK_EQ(generated(again), generated(model));
}

TEST_CASE("Controller: exhibit, assert, assume, require, references, n-ary relationships and end multiplicities") {
    SemanticModel model;
    const std::string text =
            "package P {\n"
            "    state s1;\n"
            "    constraint c1 {\n"
            "        true\n"
            "    }\n"
            "    requirement def R;\n"
            "    requirement r1 : R;\n"
            "    requirement r2 {\n"
            "        assume c1;\n"
            "        require r1;\n"
            "    }\n"
            "    part a;\n"
            "    part b;\n"
            "    part c;\n"
            "    part sys {\n"
            "        part x;\n"
            "        part y;\n"
            "        part z;\n"
            "        satisfy requirement rs : R references r1;\n"
            "        exhibit s1;\n"
            "        assert c1;\n"
            "        connect [0..1] x to [1] y;\n"
            "        connect (x, y, z);\n"
            "    }\n"
            "    dependency a, b to c;\n"
            "}\n";
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    CHECK_EQ(generated(model), text);
    DiagramController controller(model);
    std::map<int, int> kinds;
    int junctions = 0;
    QStringList endLabels;
    for (const auto &edge: controller.edges()->edges()) {
        ++kinds[int(edge.Kind)];
        junctions += edge.Junction != 0 ? 1 : 0;
        if (!edge.SourceLabel.isEmpty())
            endLabels << edge.SourceLabel << edge.TargetLabel;
    }
    CHECK_EQ(kinds[int(DiagramTypes::Exhibit)], 1);
    CHECK_EQ(kinds[int(DiagramTypes::Assert)], 1);
    CHECK_EQ(kinds[int(DiagramTypes::Assume)], 1);
    CHECK_EQ(kinds[int(DiagramTypes::Require)], 1);
    CHECK_EQ(kinds[int(DiagramTypes::ReferenceSubsetting)], 1);
    CHECK_EQ(junctions, 6); // the dependency: two clients, one supplier; the three ends of "connect (x, y, z)"
    CHECK_EQ(endLabels, QStringList({"[0..1]", "[1]"}));
    endLabels.clear();
    DiagramController inside(model, nullptr, true);
    const ElementId sys = model.children(model.children(RootId).front()).at(8);
    inside.setContextId(key(sys));
    int connectionJunctions = 0;
    for (const auto &edge: inside.edges()->edges()) {
        connectionJunctions += edge.Junction == 1 ? 1 : 0;
        if (!edge.SourceLabel.isEmpty())
            endLabels << edge.SourceLabel << edge.TargetLabel;
    }
    CHECK_EQ(connectionJunctions, 3);
    CHECK_EQ(endLabels, QStringList({"[0..1]", "[1]"}));

    // drawn: an exhibit arrow from a part to a state, the further end of a dependency in the edge dialog
    const ElementId package = model.children(RootId).front();
    const QString a = key(model.children(package).at(5));
    const QString s1 = key(model.children(package).at(0));
    CHECK(controller.allowedEdgeKinds(a, s1).contains(int(DiagramTypes::Exhibit)));
    CHECK(controller.createEdge(DiagramTypes::Exhibit, a, s1));
    CHECK(generated(model).find("    part a {\n        exhibit s1;\n    }\n") != std::string::npos);
    QString dependencyKey;
    for (const auto &edge: controller.edges()->edges())
        if (edge.Kind == DiagramTypes::Dependency)
            dependencyKey = edge.Key;
    QVariantMap details = controller.edgeDetails(dependencyKey);
    CHECK(details.value("hasClients").toBool());
    CHECK_EQ(details.value("moreSources").toString(), QString("b"));
    CHECK(controller.updateEdge(dependencyKey, {{"name", ""}, {"typeName", ""}, {"moreSources", ""}, {"moreTargets", "sys"}}).isEmpty());
    CHECK(generated(model).find("    dependency a to c, sys;\n") != std::string::npos);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated(model), again).lossless());
    CHECK_EQ(generated(again), generated(model));
}

TEST_CASE("Edge routing: lines go around parts, notes and package frames, never over them") {
    DiagramNodeModel nodes;
    const auto add = [&](std::uint64_t id, DiagramTypes::NodeKind kind, const QRectF &rect) {
        DiagramNode node;
        node.Id = id;
        node.Kind = kind;
        node.Rect = rect;
        nodes.append(node);
    };
    add(1, DiagramTypes::PartNode, QRectF(0, 100, 120, 80));     // source
    add(2, DiagramTypes::PartNode, QRectF(600, 100, 120, 80));   // target
    add(3, DiagramTypes::PartNode, QRectF(180, 60, 100, 160));   // a part in between
    add(4, DiagramTypes::NoteNode, QRectF(320, 40, 80, 200));    // a note in between
    add(5, DiagramTypes::PackageNode, QRectF(430, 0, 120, 280)); // a package frame in between
    add(6, DiagramTypes::PartNode, QRectF(440, 30, 100, 60));    // and a part in it
    std::vector<DiagramEdge> edges;
    for (const auto kind: {DiagramTypes::Connection, DiagramTypes::Dependency, DiagramTypes::Annotation}) {
        DiagramEdge edge;
        edge.Kind = kind;
        edge.Source = 1;
        edge.Target = 2;
        edge.Key = QStringLiteral("r:%1").arg(int(kind));
        edges = {edge};
        const auto route = EdgeRouting::routeOf(nodes, edges, edge);
        CHECK(route.size() >= 2);
        const std::vector<QRectF> between{QRectF(180, 60, 100, 160), QRectF(320, 40, 80, 200), QRectF(430, 0, 120, 280)};
        CHECK_EQ(EdgeRouting::crossings(route, between), 0);
        // it starts at the source and ends at the target
        if (route.size() >= 2) {
            CHECK(QRectF(0, 100, 120, 80).adjusted(-1, -1, 1, 1).contains(route.front()));
            CHECK(QRectF(600, 100, 120, 80).adjusted(-1, -1, 1, 1).contains(route.back()));
        }
    }
    // a package around both ends is no obstacle: the line runs inside it
    add(7, DiagramTypes::PackageNode, QRectF(-50, -50, 900, 400));
    DiagramEdge edge;
    edge.Kind = DiagramTypes::Connection;
    edge.Source = 1;
    edge.Target = 2;
    edge.Key = QStringLiteral("r:9");
    const auto route = EdgeRouting::routeOf(nodes, {edge}, edge);
    CHECK_EQ(EdgeRouting::crossings(route, {QRectF(180, 60, 100, 160), QRectF(320, 40, 80, 200), QRectF(430, 0, 120, 280)}), 0);
    for (const auto &point: route)
        CHECK(QRectF(-50, -50, 900, 400).contains(point));
}

TEST_CASE("Structure view: a line between a doc and a part moves the doc into the part (either way)") {
    const std::string text = "package Package1 {\n"
                             "    doc /* Documentation */\n"
                             "    part part1 {\n"
                             "        attribute attribute1;\n"
                             "    }\n"
                             "    part part2;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId package = model.children(RootId).front();
    const ElementId doc = model.children(package).at(0);
    const ElementId part1 = model.children(package).at(1);
    const ElementId part2 = model.children(package).at(2);
    // drawn from the part to the doc: the only choice is the part, the doc moves into it
    CHECK(controller.allowedEdgeKinds(key(part1), key(doc)).contains(int(DiagramTypes::Annotation)));
    CHECK_EQ(controller.annotationTargets(key(part1), key(doc)).size(), 1);
    CHECK(controller.createEdge(DiagramTypes::Annotation, key(part1), key(doc)));
    CHECK_EQ(model.element(doc).owner(), part1);
    // the doc of part1 now: no line to part1 again, but from the doc to part2 it moves on
    CHECK(!controller.allowedEdgeKinds(key(doc), key(part1)).contains(int(DiagramTypes::Annotation)));
    CHECK(controller.createAnnotation(key(doc), key(part2), QString()));
    CHECK_EQ(model.element(doc).owner(), part2);
    const std::string generated = SysMLCodeGenerator().generate(model);
    CHECK(generated.find("part part2 {") != std::string::npos);
    CHECK(generated.find("about") == std::string::npos);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated, again).lossless());
    // the line from the doc to its part
    bool line = false;
    for (const auto &edge: controller.edges()->edges())
        line = line || (edge.Kind == DiagramTypes::Annotation && edge.Source == doc && edge.Target == part2);
    CHECK(line);
}

TEST_CASE("Structure view: the doc tool for the box puts the doc into the part, shown in its compartment doc") {
    const std::string text = "package P {\n"
                             "    part part1 {\n"
                             "        attribute a1;\n"
                             "    }\n"
                             "    part part2 {\n"
                             "        doc /* Old */\n"
                             "    }\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId package = model.children(RootId).front();
    const ElementId part1 = model.children(package).at(0);
    const ElementId part2 = model.children(package).at(1);
    const auto notes = [&] {
        int count = 0;
        for (const auto &node: controller.nodes()->nodes())
            count += node.Kind == DiagramTypes::NoteNode ? 1 : 0;
        return count;
    };
    CHECK_EQ(notes(), 1); // the doc of part2 as note
    const QPointF at = controller.nodeRect(key(part1)).center();
    const QString doc = controller.addElement(DiagramTypes::ToolDocumentationInside, at.x(), at.y());
    CHECK(!doc.isEmpty());
    CHECK_EQ(model.element(idOf(doc)).owner(), part1);
    CHECK(controller.docInside(key(part1)));
    CHECK_EQ(notes(), 1); // no note for it, no line
    CHECK_EQ(entryTexts(*controller.nodes()->find(part1), "doc"), QStringList({QString("Documentation")}));
    // a part that has a doc already: shown in the box from now on
    const QPointF at2 = controller.nodeRect(key(part2)).center();
    CHECK(!controller.addElement(DiagramTypes::ToolDocumentationInside, at2.x(), at2.y()).isEmpty());
    CHECK_EQ(notes(), 0);
    CHECK_EQ(entryTexts(*controller.nodes()->find(part2), "doc"), QStringList({QString("Old")}));
    // the canvas or a package: not possible
    CHECK(controller.addElement(DiagramTypes::ToolDocumentationInside, 3000, 3000).isEmpty());
    // kept with the layout (not in the text): a new controller shows it in the box again
    const QJsonObject layout = controller.layoutState();
    CHECK(layout.contains("@notations"));
    const std::string generated = SysMLCodeGenerator().generate(model);
    CHECK(generated.find("doc /* Documentation */") != std::string::npos);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated, again).lossless());
    DiagramController restored(again);
    restored.restoreLayout(layout);
    CHECK(restored.docInside(key(model.children(model.children(RootId).front()).at(0))));
    int restoredNotes = 0;
    for (const auto &node: restored.nodes()->nodes())
        restoredNotes += node.Kind == DiagramTypes::NoteNode ? 1 : 0;
    CHECK_EQ(restoredNotes, 0);
    // back to a note
    controller.setDocInside(key(part1), false);
    CHECK_EQ(notes(), 1);
}

TEST_CASE("Controller: a package dragged into a package fits into it, the package around grows if needed") {
    SemanticModel model;
    DiagramController controller(model);
    const QString outer = controller.addElement(DiagramTypes::ToolPackage, 120, 115);
    const QString inner = controller.addElement(DiagramTypes::ToolPackage, 1200, 115);
    const QRectF outerRect = controller.nodeRect(outer);
    const QRectF innerBefore = controller.nodeRect(inner);
    CHECK(innerBefore.size() == outerRect.size()); // both of the default size
    // a part in the dragged package moves along
    const QString part = dropPart(controller, innerBefore.x() + 120, innerBefore.y() + 80);
    CHECK_EQ(model.element(idOf(part)).owner(), idOf(inner));
    const QPointF partOffset = controller.nodeRect(part).topLeft() - innerBefore.topLeft();
    // dropped with its middle onto the outer package
    controller.moveNode(inner, outerRect.center().x() - innerBefore.width() / 2, outerRect.center().y() - innerBefore.height() / 2);
    controller.finishMove(inner);
    CHECK_EQ(model.element(idOf(inner)).owner(), idOf(outer));
    const QRectF innerAfter = controller.nodeRect(inner);
    const QRectF outerAfter = controller.nodeRect(outer);
    CHECK(innerAfter.width() < outerAfter.width());
    CHECK(innerAfter.height() < outerAfter.height());
    CHECK(outerAfter.contains(innerAfter));
    // the body of the outer package (below its tab) around the inner one with a margin
    CHECK(innerAfter.top() >= outerAfter.top() + 24 + 8);
    // the part is still inside its package, at the same place in it
    CHECK(innerAfter.contains(controller.nodeRect(part)));
    CHECK(controller.nodeRect(part).topLeft() - innerAfter.topLeft() == partOffset);

    CHECK_EQ(model.element(idOf(part)).owner(), idOf(inner));
}

TEST_CASE("Structure view: an alias of a package is a package symbol, «alias» Package2Alias for Package2 (table 3)") {
    const std::string text = "package Package1 {\n"
                             "    alias Package2Alias for Package2;\n"
                             "    alias PartAlias for Package2::Part2;\n"
                             "}\n"
                             "package Package2 {\n"
                             "    part def Part2;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId package1 = model.children(RootId).at(0);
    const auto *packageAlias = controller.nodes()->find(model.children(package1).at(0));
    const auto *partAlias = controller.nodes()->find(model.children(package1).at(1));
    CHECK(packageAlias != nullptr);
    CHECK(partAlias == nullptr); // an alias of a part def: in its name compartment (table 4), no symbol
    if (!packageAlias)
        return;
    CHECK(packageAlias->Folder);
    CHECK_EQ(packageAlias->Keyword, QString("«alias»"));
    CHECK_EQ(packageAlias->Title, QString("Package2Alias"));
    CHECK_EQ(packageAlias->Subtitle, QString("for Package2"));
    // it is no package: nothing can be dropped into it
    CHECK(packageAlias->Kind != DiagramTypes::PackageNode);
    // the part def it names carries it in its name compartment
    const auto *part2 = controller.nodes()->find(model.children(model.children(RootId).at(1)).front());
    CHECK(part2 != nullptr);
    if (part2)
        CHECK_EQ(part2->Subtitle, QString("«alias» Package1::PartAlias"));
    CHECK(SysMLCodeGenerator().generate(model).find("alias Package2Alias for Package2;") != std::string::npos);
}

TEST_CASE("Controller: packages numbered through the model, a new package in a package fits into it, moving out") {
    SemanticModel model;
    DiagramController controller(model);
    const QString outer = controller.addElement(DiagramTypes::ToolPackage, 120, 115);
    const QRectF outerRect = controller.nodeRect(outer);
    // dropped into Package1: Package2 (not a second Package1), inside it and smaller
    const QString inner = controller.addElement(DiagramTypes::ToolPackage, outerRect.center().x(), outerRect.center().y());
    CHECK_EQ(model.element(idOf(inner)).name(), std::string("Package2"));
    CHECK_EQ(model.element(idOf(inner)).owner(), idOf(outer));
    const QRectF innerRect = controller.nodeRect(inner);
    const QRectF outerAfter = controller.nodeRect(outer);
    CHECK(outerAfter.contains(innerRect));
    CHECK(innerRect.width() < outerAfter.width());
    // dragged out again: on the top level, the text says so
    controller.moveNode(inner, 1500, 900);
    controller.finishMove(inner);
    CHECK_EQ(model.element(idOf(inner)).owner(), RootId);
    CHECK(generated(model).find("package Package1;") != std::string::npos);
    CHECK(generated(model).find("package Package2;") != std::string::npos);
}

TEST_CASE("Controller: a package that cannot leave its package (name taken) is drawn in it again") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("package Package1 {\n    package Package1;\n}\n", model).lossless());
    DiagramController controller(model);
    controller.layoutAll();
    const ElementId outer = model.children(RootId).front();
    const ElementId inner = model.children(outer).front();
    QString message;
    QObject::connect(&controller, &DiagramController::statusMessage, [&](const QString &text) { message = text; });
    controller.moveNode(key(inner), 1500, 900);
    controller.finishMove(key(inner));
    CHECK_EQ(model.element(inner).owner(), outer);
    CHECK(message.contains("rename"));
    CHECK(controller.nodeRect(key(outer)).contains(controller.nodeRect(key(inner))));
}

TEST_CASE("Edit dialog: an alias names an element that exists, chosen from a list") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("package Package1 {\n    alias Package2Alias for Package2;\n}\n"
                                        "package Package2 {\n    part def Engine;\n}\n",
                                        model)
                  .lossless());
    DiagramController controller(model);
    const ElementId alias = model.children(model.children(RootId).front()).front();
    QStringList offered;
    for (const auto &value: controller.targetSuggestions(key(alias), QString()))
        offered.append(value.toMap().value("text").toString());
    CHECK(offered.contains("Package1"));
    CHECK(offered.contains("Package2"));
    CHECK(offered.contains("Package2::Engine"));
    CHECK(!offered.contains("Package2::*"));
    CHECK(controller.targetStatus(key(alias), "Package2").value("known").toBool());
    CHECK(controller.targetStatus(key(alias), "ScalarValues::Real").value("known").toBool());
    CHECK(!controller.targetStatus(key(alias), "Package4").value("known").toBool());
    CHECK(!controller.targetStatus(key(alias), "Package2::*").value("known").toBool());
    // not applied: the alias keeps its target
    QVariantMap values = controller.elementDetails(key(alias));
    values["target"] = "Package4";
    CHECK(!controller.updateElement(key(alias), values).isEmpty());
    CHECK_EQ(model.element(alias).target(), std::string("Package2"));
    values["target"] = "Package2::Engine";
    CHECK(controller.updateElement(key(alias), values).isEmpty());
    CHECK_EQ(model.element(alias).target(), std::string("Package2::Engine"));
    // an import offers the packages with ::* and ::**
    SemanticModel second;
    CHECK(SysMLTextImporter::importText("package A {\n    private import B::*;\n}\npackage B;\n", second).lossless());
    DiagramController other(second);
    const ElementId import = second.children(second.children(RootId).front()).front();
    QStringList imports;
    for (const auto &value: other.targetSuggestions(key(import), "B"))
        imports.append(value.toMap().value("text").toString());
    CHECK(imports.contains("B::**"));
}

namespace {
    const DiagramNode *nodeTitled(const DiagramController &controller, const QString &title) {
        for (const auto &node: controller.nodes()->nodes()) {
            if (node.Title == title)
                return &node;
        }
        return nullptr;
    }

    QStringList compartmentTexts(const DiagramNode &node, const QString &title) {
        QStringList texts;
        for (const auto &value: node.Compartments) {
            const auto compartment = value.toMap();
            if (compartment.value("title").toString() != title)
                continue;
            for (const auto &entryValue: compartment.value("entries").toList())
                texts.append(entryValue.toMap().value("text").toString());
        }
        return texts;
    }
}

TEST_CASE("Notations: packages, imports and aliases drawn as chosen (table 3), for all or per element") {
    const std::string text = "package Package0 {\n"
                             "    package Package1;\n"
                             "    alias Package1Alias for Package1;\n"
                             "}\n"
                             "package Package2 {\n"
                             "    private import Package0::*;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId package0 = model.children(RootId).at(0);
    const ElementId package1 = model.children(package0).at(0);
    const ElementId alias = model.children(package0).at(1);
    const ElementId import = model.children(model.children(RootId).at(1)).at(0);
    // the groups and their modes: "individual" at first
    const QVariantList groups = controller.notationGroups();
    CHECK_EQ(groups.size(), 13);
    for (const auto &group: groups)
        CHECK_EQ(group.toMap().value("mode").toString(), QString("individual"));
    // defaults: nested package, alias symbol, import nested
    CHECK(nodeTitled(controller, "Package1") != nullptr);
    CHECK(nodeTitled(controller, "Package1Alias") != nullptr);
    CHECK(controller.nodes()->find(import) != nullptr);
    CHECK_EQ(controller.nodes()->find(import)->Kind, int(DiagramTypes::LibraryNode));

    // per element: Package1 beside its package with the ⊕ line, the alias as line with its name
    controller.setNotation(key(package1), "packages", "line");
    controller.setNotation(key(alias), "aliases", "line");
    controller.setNotation(key(import), "imports", "line");
    CHECK(edgeOfKind(controller, DiagramTypes::Membership) != nullptr);
    const auto *aliasLine = edgeOfKind(controller, DiagramTypes::AliasMembership);
    CHECK(aliasLine != nullptr);
    if (aliasLine) {
        CHECK_EQ(aliasLine->Label, QString("Package1Alias"));
        CHECK_EQ(aliasLine->Source, package0);
        CHECK_EQ(aliasLine->Target, package1);
        CHECK_EQ(controller.notationSubject(aliasLine->Key), key(alias));
    }
    CHECK(controller.nodes()->find(alias) == nullptr);
    CHECK(edgeOfKind(controller, DiagramTypes::Import) != nullptr);
    // Package1 drawn outside Package0, and moving it does not change its owner
    controller.layoutAll();
    CHECK(!controller.nodeRect(key(package0)).intersects(controller.nodeRect(key(package1))));
    controller.moveNode(key(package1), 2000, 2000);
    controller.finishMove(key(package1));
    CHECK_EQ(model.element(package1).owner(), package0);

    // as text in the compartments of the frame
    controller.setNotation(key(package1), "packages", "compartment");
    controller.setNotation(key(import), "imports", "compartment");
    CHECK(controller.nodes()->find(package1) == nullptr);
    CHECK(controller.nodes()->find(import) == nullptr);
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(package0), "packages"), QStringList({QString("package Package1")}));
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(model.children(RootId).at(1)), "imports"),
             QStringList({QString("private import Package0::*")}));
    // the alias names Package1, which is not shown now: its symbol again
    CHECK(controller.nodes()->find(alias) != nullptr);

    // one notation for all: overrides the choices of the elements, which come back with "individual"
    controller.setNotationMode("packages", "nested");
    CHECK(controller.nodes()->find(package1) != nullptr);
    CHECK_EQ(controller.notationChoices(key(package1)).front().toMap().value("mode").toString(), QString("nested"));
    controller.setNotationMode("packages", "individual");
    CHECK(controller.nodes()->find(package1) == nullptr);

    // kept with the layout, not in the text
    const QJsonObject layout = controller.layoutState();
    CHECK(layout.contains("@notations"));
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    DiagramController restored(model);
    restored.restoreLayout(layout);
    CHECK(restored.nodes()->find(package1) == nullptr);
    CHECK_EQ(restored.notationOf(import, "imports"), QString("compartment"));
    restored.setNotationMode("aliases", "name");
    const QJsonObject withMode = restored.layoutState();
    CHECK_EQ(withMode.value("@notationModes").toObject().value("aliases").toString(), QString("name"));
}

TEST_CASE("Notations: the parts and ports of a part as nodes, inside it or as text (table 9); type line; doc") {
    const std::string text = "package P {\n"
                             "    part def Vehicle {\n"
                             "        doc /* A vehicle */\n"
                             "        port fuel;\n"
                             "        part engine;\n"
                             "        part wheel;\n"
                             "    }\n"
                             "    part car : Vehicle;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId package = model.children(RootId).front();
    const ElementId vehicle = model.children(package).at(0);
    const ElementId car = model.children(package).at(1);
    const ElementId engine = model.children(vehicle).at(2);
    const ElementId fuel = model.children(vehicle).at(1);
    // default: part nodes with ◆ lines, ports on the border, type in the name, doc as note
    CHECK(controller.nodes()->find(engine) != nullptr);
    CHECK(edgeOfKind(controller, DiagramTypes::Composition) != nullptr);
    CHECK(controller.nodes()->find(fuel) != nullptr);
    CHECK(edgeOfKind(controller, DiagramTypes::DefinedBy) == nullptr);
    QStringList groups;
    for (const auto &choice: controller.notationChoices(key(vehicle)))
        groups.append(choice.toMap().value("group").toString());
    CHECK_EQ(groups, QStringList({QString("qualified"), QString("features"), QString("ports"), QString("relationships"),
                                  QString("docs")}));

    // inside the part (graphical compartment): no ◆ line, the part drawn around them
    controller.setNotation(key(vehicle), "features", "nested");
    controller.layoutAll();
    CHECK(edgeOfKind(controller, DiagramTypes::Composition) == nullptr);
    CHECK(controller.nodes()->find(vehicle)->Container);
    CHECK(controller.nodeRect(key(vehicle)).contains(controller.nodeRect(key(engine))));
    // as text: the compartments parts and ports
    controller.setNotation(key(vehicle), "features", "compartment");
    controller.setNotation(key(vehicle), "ports", "compartment");
    CHECK(controller.nodes()->find(engine) == nullptr);
    CHECK(controller.nodes()->find(fuel) == nullptr);
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(vehicle), "parts"), QStringList({QString("engine"), QString("wheel")}));
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(vehicle), "ports"), QStringList({QString("fuel")}));
    // the type as line, the doc in its compartment
    controller.setNotation(key(car), "typing", "line");
    CHECK(edgeOfKind(controller, DiagramTypes::DefinedBy) != nullptr);
    controller.setNotation(key(vehicle), "docs", "compartment");
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(vehicle), "doc"), QStringList({QString("A vehicle")}));
    // the text stays as it was
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
}

TEST_CASE("Notations: asked right after creating, only while the group is individual") {
    SemanticModel model;
    DiagramController controller(model);
    const QString outer = controller.addElement(DiagramTypes::ToolPackage, 120, 115);
    const QRectF frame = controller.nodeRect(outer);
    const QString inner = controller.addElement(DiagramTypes::ToolPackage, frame.center().x(), frame.center().y());
    const QVariantList choices = controller.creationChoices(inner);
    CHECK_EQ(choices.size(), 1);
    CHECK_EQ(choices.front().toMap().value("group").toString(), QString("packages"));
    CHECK_EQ(choices.front().toMap().value("options").toList().size(), 3);
    controller.setNotationMode("packages", "compartment");
    CHECK(controller.creationChoices(inner).isEmpty());
    // a part added to a part: how that part shows its parts, asked once
    controller.setNotationMode("packages", "individual");
    const QString vehicle = dropPart(controller, 900, 600, DiagramTypes::ToolPartDefinition);
    const QRectF vehicleRect = controller.nodeRect(vehicle);
    const QString engine = dropPart(controller, vehicleRect.center().x(), vehicleRect.center().y());
    CHECK_EQ(model.element(idOf(engine)).owner(), idOf(vehicle));
    const QVariantList partChoices = controller.creationChoices(engine);
    CHECK_EQ(partChoices.size(), 1);
    CHECK_EQ(partChoices.front().toMap().value("subject").toString(), vehicle);
    controller.setNotation(vehicle, "features", "line");
    CHECK(controller.creationChoices(engine).isEmpty());
}

TEST_CASE("Notations: «alias» in the name compartment only for definitions and usages, not for packages (table 3, 4)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("package Package2 {\n    alias Package3Alias for Package3;\n    alias CarAlias for Car;\n}\n"
                                        "package Package3;\npart def Car;\n", model)
                  .lossless());
    DiagramController controller(model);
    const ElementId package2 = model.children(RootId).front();
    const ElementId alias = model.children(package2).front();
    const ElementId carAlias = model.children(package2).at(1);
    const ElementId package3 = model.children(RootId).at(1);
    const ElementId car = model.children(RootId).at(2);
    // an alias of a package: the symbol or the line, «alias» at the element is not offered
    QStringList values;
    for (const auto &option: controller.notationChoices(key(alias)).front().toMap().value("options").toList())
        values.append(option.toMap().value("value").toString());
    CHECK_EQ(values, QStringList({QString("nested"), QString("line")}));
    controller.setNotationMode("aliases", "name");
    CHECK(controller.nodes()->find(alias) != nullptr); // stays a symbol
    CHECK(controller.nodes()->find(package3)->Subtitle.isEmpty());
    // an alias of a part def: in its name compartment, qualified (another package), clickable
    CHECK(controller.nodes()->find(carAlias) == nullptr);
    CHECK_EQ(controller.nodes()->find(car)->Subtitle, QString("«alias» Package2::CarAlias"));
    CHECK_EQ(controller.aliasEntries(key(car)), QStringList({key(carAlias)}));
    // also offered on the package owning it and on the element it names
    bool offered = false;
    for (const auto &choice: controller.notationChoices(key(package2)))
        offered = offered || choice.toMap().value("subject").toString() == key(carAlias);
    CHECK(offered);
    const QVariantList choices = controller.notationChoices(key(car));
    CHECK_EQ(choices.size(), 1);
    CHECK_EQ(choices.front().toMap().value("subject").toString(), key(carAlias));
    controller.setNotationMode("aliases", "individual");
    // the symbol is for packages only: the line with the alias name instead (table 3, membership)
    controller.setNotation(key(carAlias), "aliases", "nested");
    CHECK(controller.nodes()->find(carAlias) == nullptr);
    controller.setNotation(key(carAlias), "aliases", "line");
    CHECK(edgeOfKind(controller, DiagramTypes::AliasMembership) != nullptr);
    // and when the element it names does not exist: the symbol stays
    SemanticModel missing;
    CHECK(SysMLTextImporter::importText("package Package2 {\n    alias CarAlias for Car;\n}\n", missing).lossless());
    DiagramController other(missing);
    const ElementId lonely = missing.children(missing.children(RootId).front()).front();
    other.setNotationMode("aliases", "name");
    CHECK(other.nodes()->find(lonely) != nullptr);
}

TEST_CASE("Notations: the members of a package as text in its compartment members (table 3)") {
    const std::string text = "package Package1 {\n"
                             "    part def PartDef1;\n"
                             "    part def PartDef2;\n"
                             "    part part1 : PartDef1;\n"
                             "    part part2 : PartDef2;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId package = model.children(RootId).front();
    CHECK_EQ(controller.nodes()->count(), 5);
    controller.setNotation(key(package), "members", "compartment");
    CHECK_EQ(controller.nodes()->count(), 1);
    const auto *frame = controller.nodes()->find(package);
    CHECK(frame != nullptr);
    if (!frame)
        return;
    CHECK_EQ(compartmentTexts(*frame, "members"), QStringList({QString("part def PartDef1"), QString("part def PartDef2"),
                                                                 QString("part part1 : PartDef1"), QString("part part2 : PartDef2")}));
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    controller.setNotationMode("members", "nested");
    CHECK_EQ(controller.nodes()->count(), 5);
}

TEST_CASE("Notations: aliases in the name compartment, qualified where they are in another package (table 4)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("part def PartDef0;\nabstract part def PartDef1 :> PartDef0;\n"
                                        "package P {\n    alias PartDefAlias1 for PartDef1;\n}\n"
                                        "package Q {\n    alias PartDefAlias2 for PartDef1;\n}\n", model).lossless());
    DiagramController controller(model);
    controller.setNotationMode("aliases", "name");
    const auto *partDef = nodeTitled(controller, "PartDef1");
    CHECK(partDef != nullptr);
    if (partDef)
        CHECK_EQ(partDef->Subtitle, QString("«alias» P::PartDefAlias1, Q::PartDefAlias2"));
}

TEST_CASE("Notations: ':> PartDef0' in the name, an element alone with its qualified name (table 4)") {
    const std::string text = "part def PartDef0;\n"
                             "abstract part def PartDef1 :> PartDef0;\n"
                             "package Package1 {\n"
                             "    package Package2 {\n"
                             "        package Package3 {\n"
                             "            part def PartDef3;\n"
                             "        }\n"
                             "    }\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId partDef1 = model.children(RootId).at(1);
    const ElementId package1 = model.children(RootId).at(2);
    const ElementId package2 = model.children(package1).front();
    const ElementId package3 = model.children(package2).front();
    const ElementId partDef3 = model.children(package3).front();
    // the specialization: a line, or in the name
    CHECK(edgeOfKind(controller, DiagramTypes::Specialization) != nullptr);
    controller.setNotation(key(partDef1), "specialization", "name");
    CHECK(edgeOfKind(controller, DiagramTypes::Specialization) == nullptr);
    CHECK_EQ(controller.nodes()->find(partDef1)->Title, QString("PartDef1 :> PartDef0"));
    // three packages deep: alone, "Package1::Package2::Package3::PartDef3", the empty packages not drawn
    controller.setNotation(key(partDef3), "qualified", "qualified");
    CHECK(controller.nodes()->find(package1) == nullptr);
    CHECK(controller.nodes()->find(package2) == nullptr);
    CHECK(controller.nodes()->find(package3) == nullptr);
    CHECK(controller.nodes()->find(partDef3) != nullptr);
    CHECK_EQ(controller.nodes()->find(partDef3)->Title, QString("Package1::Package2::Package3::PartDef3"));
    controller.layoutAll();
    // moved anywhere: still in its package; back with "Inside its package"
    controller.moveNode(key(partDef3), 1200, 800);
    controller.finishMove(key(partDef3));
    CHECK_EQ(model.element(partDef3).owner(), package3);
    controller.setNotation(key(partDef3), "qualified", "nested");
    CHECK(controller.nodes()->find(package1) != nullptr);
    CHECK(controller.nodes()->find(package3) != nullptr);
    CHECK_EQ(controller.nodes()->find(partDef3)->Title, QString("PartDef3"));
    // for all: the whole model flat with qualified names
    controller.setNotationMode("qualified", "qualified");
    CHECK(controller.nodes()->find(package1) == nullptr);
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
}

TEST_CASE("Notations: usages - qualified part of a part, subsets / redefines in the name compartment (table 4)") {
    const std::string text = "part def PartDef3;\n"
                             "part part1 {\n"
                             "    part part2 {\n"
                             "        part part3 : PartDef3;\n"
                             "    }\n"
                             "}\n"
                             "part def Base {\n"
                             "    attribute m : ScalarValues::Integer;\n"
                             "    part part1 : PartDef1[*];\n"
                             "    part part1S : PartDef1S[m] subsets part1;\n"
                             "}\n"
                             "part def Special :> Base {\n"
                             "    part part2 : PartDef1R;\n"
                             "    part part1R : PartDef1R[m] redefines part1 = part2;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId part1 = model.children(RootId).at(1);
    const ElementId part2 = model.children(part1).front();
    const ElementId part3 = model.children(part2).front();
    const ElementId base = model.children(RootId).at(2);
    const ElementId special = model.children(RootId).at(3);
    const ElementId subsetted = model.children(base).at(2);
    const ElementId redefined = model.children(special).at(1);
    // "part1::part2::part3 : PartDef3" alone, no composition line to part2
    controller.setNotation(key(part3), "qualified", "qualified");
    CHECK_EQ(controller.nodes()->find(part3)->Title, QString("part1::part2::part3 : PartDef3"));
    for (const auto &edge: controller.edges()->edges())
        CHECK(!(edge.Kind == DiagramTypes::Composition && edge.Target == part3));
    // subsets / redefines in the name: no lines
    controller.setNotation(key(subsetted), "specialization", "name");
    controller.setNotation(key(redefined), "specialization", "name");
    CHECK_EQ(controller.nodes()->find(subsetted)->Title, QString("part1S : PartDef1S [m]\nsubsets part1"));
    CHECK_EQ(controller.nodes()->find(redefined)->Title, QString("part1R : PartDef1R [m]\nredefines part1 = part2"));
    for (const auto &edge: controller.edges()->edges()) {
        CHECK(!(edge.Source == subsetted && edge.Kind == DiagramTypes::Specialization));
        CHECK(!(edge.Source == redefined && edge.Kind == DiagramTypes::Redefinition));
    }
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
}

TEST_CASE("Notations: back to nested - a package and its members are drawn inside their package again") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("package Package1 {\n    package Package2 {\n        part def PartDef3;\n    }\n}\n", model)
                  .lossless());
    DiagramController controller(model);
    controller.layoutAll();
    const ElementId package1 = model.children(RootId).front();
    const ElementId package2 = model.children(package1).front();
    const ElementId partDef3 = model.children(package2).front();
    controller.moveNode(key(package1), 100, 100); // a layout of the user (no automatic layout any more)
    const auto inside = [&](ElementId outer, ElementId inner) {
        return controller.nodeRect(key(outer)).contains(controller.nodeRect(key(inner)));
    };
    for (const char *mode: {"compartment", "line", "nested", "line", "compartment", "nested"}) {
        controller.setNotationMode("packages", mode);
        controller.setNotationMode("members", QString(mode) == "compartment" ? "compartment" : "nested");
    }
    CHECK(inside(package1, package2));
    CHECK(inside(package2, partDef3));
    // per element as well, from beside it
    controller.setNotationMode("packages", "individual");
    controller.setNotation(key(package2), "packages", "line");
    CHECK(!inside(package1, package2));
    controller.setNotation(key(package2), "packages", "nested");
    CHECK(inside(package1, package2));
    CHECK(inside(package2, partDef3));
}

TEST_CASE("Notations: a top-level package shows its packages and members as text (table 3 'Package with owned package')") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("package Package1 {\n    package Package2 {\n        part def PartDef3;\n    }\n}\n", model)
                  .lossless());
    DiagramController controller(model);
    const ElementId package1 = model.children(RootId).front();
    const ElementId package2 = model.children(package1).front();
    // Package1 has only a package in it: the choice is offered all the same
    bool offered = false;
    for (const auto &choice: controller.notationChoices(key(package1)))
        offered = offered || choice.toMap().value("group").toString() == "members";
    CHECK(offered);
    controller.setNotation(key(package1), "members", "compartment");
    CHECK(controller.nodes()->find(package2) == nullptr);
    // in the compartment packages (members holds definitions and usages only), with what is in it (indented)
    CHECK(compartmentTexts(*controller.nodes()->find(package1), "members").isEmpty());
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(package1), "packages"),
             QStringList({QString("package Package2"), QString("part def PartDef3")}));
    controller.setNotation(key(package1), "members", "nested");
    CHECK(controller.nodes()->find(package2) != nullptr);
    CHECK(controller.nodeRect(key(package1)).contains(controller.nodeRect(key(package2))));
}

TEST_CASE("Containment: a definition dragged onto a part goes into it (owned member, ⊕ line); parts back inside their part") {
    const std::string text = "part part1 {\n"
                             "    part part2 {\n"
                             "        part part3;\n"
                             "    }\n"
                             "}\n"
                             "part def <part3> Part3;\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    controller.layoutAll();
    const ElementId part1 = model.children(RootId).at(0);
    const ElementId part2 = model.children(part1).front();
    const ElementId part3 = model.children(part2).front();
    const ElementId definition = model.children(RootId).at(1);
    // onto part2: "part part2 { part part3; part def <part3> Part3; }"
    const QRectF target = controller.nodeRect(key(part2));
    const QRectF moved = controller.nodeRect(key(definition));
    controller.moveNode(key(definition), target.center().x() - moved.width() / 2, target.center().y() - moved.height() / 2);
    controller.finishMove(key(definition));
    CHECK_EQ(model.element(definition).owner(), part2);
    CHECK(SysMLCodeGenerator().generate(model).find("        part def <part3> Part3;") != std::string::npos);
    bool line = false;
    for (const auto &edge: controller.edges()->edges())
        line = line || (edge.Kind == DiagramTypes::Membership && edge.Source == part2 && edge.Target == definition);
    CHECK(line);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(SysMLCodeGenerator().generate(model), again).lossless());
    // part1 drawn around its parts, with a layout of the user: they are put inside it; dragged out of it: out of it
    controller.moveNode(key(part1), 600, 600);
    controller.setNotation(key(part1), "features", "nested");
    CHECK(controller.nodeRect(key(part1)).contains(controller.nodeRect(key(part2))));
    controller.moveNode(key(part2), 2000, 2000);
    controller.finishMove(key(part2));
    CHECK_EQ(model.element(part2).owner(), RootId);
    CHECK_EQ(model.element(part3).owner(), part2);
}

TEST_CASE("Containment: a part of a part dropped onto another part becomes its part; dropped elsewhere it stays") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("part part1 {\n    part part2;\n}\npart def Part3 {\n    part part3 : Part3;\n}\n", model)
                  .lossless());
    DiagramController controller(model);
    controller.layoutAll();
    const ElementId part1 = model.children(RootId).at(0);
    const ElementId part2 = model.children(part1).front();
    const ElementId definition = model.children(RootId).at(1);
    const ElementId part3 = model.children(definition).front();
    // dropped on the empty canvas: still a part of Part3
    controller.moveNode(key(part3), 1500, 1500);
    controller.finishMove(key(part3));
    CHECK_EQ(model.element(part3).owner(), definition);
    // dropped onto part2 (also when part1 is drawn around part2): a part of part2
    controller.setNotation(key(part1), "features", "nested");
    const QRectF target = controller.nodeRect(key(part2));
    const QRectF moved = controller.nodeRect(key(part3));
    controller.moveNode(key(part3), target.center().x() - moved.width() / 2, target.center().y() - moved.height() / 2);
    controller.finishMove(key(part3));
    CHECK_EQ(model.element(part3).owner(), part2);
    CHECK(SysMLCodeGenerator().generate(model).find("part part2 {\n        part part3 : Part3;\n    }") != std::string::npos);
}

TEST_CASE("Notations: usages - subsets / redefines as lines as in the doc figure 'Redefinition' (table 4)") {
    const std::string text = "part part1 : Part1[0..*] {\n"
                             "    part part2 : Part2[0..*];\n"
                             "}\n"
                             "part part1S : Part1S[1] subsets part1 {\n"
                             "    part part2R : Part2R redefines part2;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId part1 = model.children(RootId).at(0);
    const ElementId part2 = model.children(part1).front();
    const ElementId part1S = model.children(RootId).at(1);
    const ElementId part2R = model.children(part1S).front();
    CHECK_EQ(controller.nodes()->find(part1S)->Title, QString("part1S : Part1S [1]"));
    CHECK_EQ(controller.nodes()->find(part2R)->Title, QString("part2R : Part2R"));
    const auto has = [&](DiagramTypes::EdgeKind kind, ElementId source, ElementId target) {
        for (const auto &edge: controller.edges()->edges())
            if (edge.Kind == kind && edge.Source == source && edge.Target == target)
                return true;
        return false;
    };
    CHECK(has(DiagramTypes::Composition, part1, part2));
    CHECK(has(DiagramTypes::Composition, part1S, part2R));
    CHECK(has(DiagramTypes::Redefinition, part2R, part2));
    CHECK(has(DiagramTypes::Specialization, part1S, part1));
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
}

TEST_CASE("Notations: inherited parts as nodes ^part1, marked as drawn only (table 4 'Inherited Usage')") {
    const std::string text = "part def PartDef0 {\n"
                             "    part part1 : PartDef1;\n"
                             "}\n"
                             "part def PartDef1;\n"
                             "part def PartDef2 :> PartDef0;\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId special = model.children(RootId).at(2);
    const auto count = controller.nodes()->count();
    CHECK(controller.nodes()->find(special)->Compartments.size() > 0);
    controller.setNotation(key(special), "inherited", "nodes");
    CHECK_EQ(controller.nodes()->count(), count + 1);
    if (controller.nodes()->count() != count + 1)
        return;
    const auto &derived = controller.nodes()->nodes().back();
    CHECK(derived.Derived);
    CHECK_EQ(derived.Title, QString("^part1 : PartDef1"));
    bool composed = false;
    for (const auto &edge: controller.edges()->edges())
        composed = composed || (edge.Kind == DiagramTypes::Composition && edge.Source == special && edge.Target == derived.Id);
    CHECK(composed);
    controller.layoutAll();
    // drawn only: nothing of the editor changes the model through it
    const QString derivedKey = key(derived.Id);
    const auto rect = derived.Rect;
    controller.moveNode(derivedKey, rect.x() + 30, rect.y() + 10);
    controller.finishMove(derivedKey);
    controller.setNodeSize(derivedKey, 200, 90);
    CHECK(controller.elementDetails(derivedKey).isEmpty() || true);
    controller.notationChoices(derivedKey);
    controller.creationChoices(derivedKey);
    controller.aliasEntries(derivedKey);
    controller.kindLabel(derivedKey);
    controller.minimumNodeHeight(derivedKey);
    controller.allowedEdgeKinds(derivedKey, key(special));
    CHECK(!controller.createEdge(int(DiagramTypes::Specialization), derivedKey, key(special)));
    controller.select(derivedKey, false);
    controller.deleteSelection();
    controller.deleteElement(derivedKey);
    // a part dropped onto it does not go into it
    const ElementId def1 = model.children(RootId).at(1);
    const auto *derivedNode = controller.nodes()->find(derived.Id);
    CHECK(derivedNode != nullptr);
    if (derivedNode) {
        controller.moveNode(key(def1), derivedNode->Rect.center().x() - 10, derivedNode->Rect.center().y() - 10);
        controller.finishMove(key(def1));
    }
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    controller.setNotation(key(special), "inherited", "compartment");
    CHECK_EQ(controller.nodes()->count(), count);
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
}

namespace {
    /// The compartments of a node as "title: entry | entry", the keyword in bold as *keyword*.
    QStringList compartmentTexts(const DiagramNode &node) {
        QStringList result;
        for (const auto &compartment: node.Compartments) {
            const auto map = compartment.toMap();
            QStringList entries;
            for (const auto &value: map.value("entries").toList()) {
                const auto entryMap = value.toMap();
                QString text = entryMap.value("text").toString();
                const QString keyword = entryMap.value("keyword").toString();
                if (!keyword.isEmpty() && text.startsWith(keyword))
                    text = '*' + keyword + '*' + text.mid(keyword.size());
                entries.append(text);
            }
            result.append(map.value("title").toString() + ": " + entries.join(" | "));
        }
        return result;
    }
}

TEST_CASE("Notations: variants compartment of a variation (8.2.3 'Variants Compartment', three forms)") {
    const std::string text = "variation item itemChoice : ItemDef {\n"
                             "    variant part1;\n"
                             "    variant item1;\n"
                             "}\n"
                             "variation item itemChoice1 : ItemDef {\n"
                             "    variant part references part1;\n"
                             "    variant item references item1;\n"
                             "}\n"
                             "variation item itemChoice2 : ItemDef {\n"
                             "    variant part part1 : Part1;\n"
                             "    variant item item1 : Item1;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId choice = model.children(RootId).at(0);
    const ElementId choice1 = model.children(RootId).at(1);
    const ElementId choice2 = model.children(RootId).at(2);
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(choice)), QStringList({"variants: part1 | item1"}));
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(choice1)),
             QStringList({"variants: *part references* part1 | *item references* item1"}));
    // default, as in the table: both in the compartment, no node, no line
    const ElementId part1 = model.children(choice2).at(0);
    CHECK(controller.nodes()->find(part1) == nullptr);
    CHECK(edgeOfKind(controller, DiagramTypes::VariantMembership) == nullptr);
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(choice2)),
             QStringList({"variants: *part* part1 : Part1 | *item* item1 : Item1"}));
    // on request: the variant part a node with a line (circle with a cross) at the variation, the item listed
    controller.setNotation(key(choice2), "variants", "line");
    CHECK(controller.nodes()->find(part1) != nullptr);
    CHECK(edgeOfKind(controller, DiagramTypes::VariantMembership) != nullptr);
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(choice2)), QStringList({"variants: *item* item1 : Item1"}));
    CHECK_EQ(SysMLCodeGenerator().generate(model), SysMLCodeGenerator().generate(model)); // stable
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(SysMLCodeGenerator().generate(model), again).lossless());
}

TEST_CASE("Notations: variant parts compartment of a variation part (8.2.3 'Variant Parts Compartment')") {
    const std::string text = "variation part partChoice : PartDef {\n"
                             "    variant part part1 : PartDef1;\n"
                             "    variant part part2 : PartDef2;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId choice = model.children(RootId).at(0);
    CHECK_EQ(controller.nodes()->count(), 1);
    CHECK(controller.edges()->edges().empty());
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(choice)),
             QStringList({"variant parts: part1 : PartDef1 | part2 : PartDef2"}));
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    controller.setNotation(key(choice), "variants", "line");
    CHECK_EQ(controller.nodes()->count(), 3);
    CHECK(edgeOfKind(controller, DiagramTypes::VariantMembership) != nullptr);
}

TEST_CASE("Notations: relationships compartment instead of lines (8.2.3 'Relationships Compartment')") {
    const std::string text = "part def PartDef1;\n"
                             "part def PartDef2 :> PartDef1;\n"
                             "part part1 : PartDef1;\n"
                             "part part2 : PartDef2 :> part1;\n"
                             "connect part2 to part3;\n"
                             "part part3;\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId partDef2 = model.children(RootId).at(1);
    const ElementId part2 = model.children(RootId).at(3);
    CHECK(edgeOfKind(controller, DiagramTypes::Specialization) != nullptr);
    CHECK(edgeOfKind(controller, DiagramTypes::Connection) != nullptr);
    CHECK(controller.nodes()->find(part2)->Compartments.isEmpty());
    const std::string generated = SysMLCodeGenerator().generate(model);
    controller.setNotationMode("relationships", "compartment");
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(partDef2)),
             QStringList({"relationships: *defines* part2 | *specializes* PartDef1"}));
    CHECK_EQ(compartmentTexts(*controller.nodes()->find(part2)),
             QStringList({"relationships: *defined by* PartDef2 | *subsets* part1 | *connect to* part3"}));
    CHECK(edgeOfKind(controller, DiagramTypes::Specialization) == nullptr);
    CHECK(edgeOfKind(controller, DiagramTypes::Connection) == nullptr);
    CHECK_EQ(SysMLCodeGenerator().generate(model), generated); // the notation does not change the text
    // per element: only part2 as text, the line between the definitions stays
    controller.setNotationMode("relationships", "individual");
    controller.setNotation(key(part2), "relationships", "compartment");
    CHECK(edgeOfKind(controller, DiagramTypes::Connection) == nullptr);
    CHECK(edgeOfKind(controller, DiagramTypes::Specialization) != nullptr);
    CHECK(controller.nodes()->find(partDef2)->Compartments.isEmpty());
}

TEST_CASE("Compartments: keywords and the brackets of the multiplicity in bold, names and units as written") {
    const std::string text = "part def Vehicle {\n"
                             "    ref part driver : Person[0..1];\n"
                             "    attribute mass : Real[1] = 1500 [kg];\n"
                             "    part 'part' : Engine;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId vehicle = model.children(RootId).at(0);
    controller.setNotation(key(vehicle), "features", "compartment");
    QStringList rich;
    for (const auto &compartment: controller.nodes()->find(vehicle)->Compartments)
        for (const auto &value: compartment.toMap().value("entries").toList())
            rich.append(value.toMap().value("rich").toString());
    CHECK(rich.contains("mass : Real <b>[</b>1<b>]</b> <b>=</b> 1500 [kg]"));
    CHECK(rich.contains("<b>ref</b> driver : Person <b>[</b>0..1<b>]</b>"));
    CHECK(rich.contains("part : Engine"));
}

TEST_CASE("Edit dialog: subsets / specializes, ordered / nonunique and the prefixes of the standard") {
    const std::string text = "part def Vehicle {\n"
                             "    attribute mass : Real[1];\n"
                             "    attribute dryMass : Real[0..1];\n"
                             "}\n"
                             "part def Car;\n"
                             "variation part choice : Vehicle {\n"
                             "    variant part a : Car;\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    DiagramController controller(model);
    const ElementId vehicle = model.children(RootId).at(0);
    const ElementId dryMass = model.children(vehicle).at(1);
    const ElementId car = model.children(RootId).at(1);
    const ElementId variant = model.children(model.children(RootId).at(2)).at(0);

    auto details = controller.elementDetails(key(dryMass));
    CHECK(details.value("hasSpecializations").toBool());
    CHECK_EQ(details.value("specializationWord").toString(), QString("subsets"));
    CHECK(details.value("prefixOptions").toStringList().contains("constant"));
    CHECK(!details.value("prefixOptions").toStringList().contains("variant"));
    CHECK(controller.elementDetails(key(variant)).value("prefixOptions").toStringList().contains("variant"));
    CHECK(controller.elementDetails(key(variant)).value("prefixFlags").toMap().value("variant").toBool());
    CHECK_EQ(controller.elementDetails(key(car)).value("prefixOptions").toStringList(),
             QStringList({"abstract", "variation", "individual"}));

    // "derived constant attribute dryMass : Real[0..1] ordered nonunique subsets mass, ISQ::mass;"
    QVariantMap values{{"name", "dryMass"}, {"typeName", "Real"}, {"multiplicity", "0..1"}, {"value", ""},
                       {"prefixes", "derived constant"}, {"ordered", true}, {"nonunique", true},
                       {"specializes", "mass, ISQ::mass"}, {"specializationKeyword", "subsets"},
                       {"redefines", ""}, {"redefinitionKeyword", ":>>"}};
    CHECK_EQ(controller.updateElement(key(dryMass), values), QString());
    const std::string generated = SysMLCodeGenerator().generate(model);
    CHECK(generated.find("derived constant attribute dryMass : Real[0..1] ordered nonunique subsets mass, ISQ::mass;") !=
          std::string::npos);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated, again).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(again), generated);
    details = controller.elementDetails(key(dryMass));
    CHECK_EQ(details.value("specializations").toString(), QString("mass, ISQ::mass"));
    CHECK(details.value("ordered").toBool());
    CHECK(details.value("prefixFlags").toMap().value("constant").toBool());
    // listed in the compartment as written (the line between entries cannot be drawn)
    controller.setNotation(key(vehicle), "features", "compartment");
    bool listed = false;
    for (const auto &compartment: controller.nodes()->find(vehicle)->Compartments)
        for (const auto &value: compartment.toMap().value("entries").toList())
            listed = listed || value.toMap().value("text").toString() ==
                                       "readonly /dryMass : Real [0..1] ordered nonunique subsets mass, ISQ::mass";
    CHECK(listed);
    // abstract and variation exclude each other; a definition specializes
    values["prefixes"] = "abstract variation";
    CHECK(!controller.updateElement(key(dryMass), values).isEmpty());
    QVariantMap definition{{"name", "Car"}, {"typeName", ""}, {"multiplicity", ""}, {"value", ""}, {"prefixes", "abstract"},
                           {"specializes", "Vehicle"}, {"specializationKeyword", ":>"}};
    CHECK_EQ(controller.updateElement(key(car), definition), QString());
    CHECK(SysMLCodeGenerator().generate(model).find("abstract part def Car :> Vehicle;") != std::string::npos);
    CHECK(edgeOfKind(controller, DiagramTypes::Specialization) != nullptr);
    definition["specializes"] = "";
    CHECK_EQ(controller.updateElement(key(car), definition), QString());
    CHECK(SysMLCodeGenerator().generate(model).find("abstract part def Car;") != std::string::npos);
}

TEST_CASE("Editor: items on the canvas, variants of a fitting kind or declared elsewhere, references (7.6.3, 7.6.7)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("part part1;\nitem def ItemDef;\n", model).lossless());
    DiagramController controller(model);
    // an item on the empty canvas: "item item1;" on package level
    const QString item = controller.addElement(DiagramTypes::ToolItem, 600, 400);
    CHECK(!item.isEmpty());
    if (item.isEmpty())
        return;
    const ElementId itemId = item.toULongLong();
    CHECK(model.element(itemId).kind() == ElementKind::ItemUsage);
    CHECK_EQ(model.element(itemId).owner(), RootId);
    // made a variation: "variation item item1 : ItemDef"
    QVariantMap values{{"name", "itemChoice"}, {"typeName", "ItemDef"}, {"multiplicity", ""}, {"value", ""},
                       {"prefixes", "variation"}};
    CHECK_EQ(controller.updateElement(item, values), QString());
    const auto *node = controller.nodes()->find(itemId);
    CHECK(node != nullptr);
    if (!node)
        return;
    const QPointF center = node->Rect.center();
    // the variant tool on it: a new item or part, or part1 declared elsewhere
    const QVariantList choices = controller.variantChoices(center.x(), center.y());
    QStringList texts;
    for (const auto &choice: choices)
        texts.append(choice.toMap().value("text").toString());
    CHECK_EQ(texts, QStringList({"New variant item", "New variant part", "Existing: part1"}));
    CHECK(controller.variantChoices(10, 10).isEmpty());
    CHECK(!controller.addVariant(item, "", "part1").isEmpty());
    CHECK(!controller.addVariant(item, "part", "").isEmpty());
    const std::string text = SysMLCodeGenerator().generate(model);
    CHECK(text.find("variation item itemChoice : ItemDef {\n    variant part1;\n    variant part part2;\n}") != std::string::npos);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    // a reference subsetting from the dialog: "item copy references part1;"
    const ElementId variant = model.children(itemId).at(1);
    CHECK(controller.elementDetails(key(variant)).value("hasReferences").toBool());
    QVariantMap reference{{"name", "part2"}, {"typeName", ""}, {"multiplicity", ""}, {"value", ""}, {"prefixes", "variant"},
                          {"references", "part1"}, {"referencesKeyword", "::>"}};
    CHECK_EQ(controller.updateElement(key(variant), reference), QString());
    CHECK(SysMLCodeGenerator().generate(model).find("variant part part2 ::> part1;") != std::string::npos);
    reference["references"] = "";
    CHECK_EQ(controller.updateElement(key(variant), reference), QString());
    CHECK(SysMLCodeGenerator().generate(model).find("variant part part2;") != std::string::npos);
}

TEST_CASE("Editor: a variation holds only its variants (8.3.6: all memberships are variant memberships)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("variation item itemChoice : ItemDef;\npart loose;\npart def Defined;\n", model).lossless());
    DiagramController controller(model);
    const ElementId choice = model.children(RootId).at(0);
    const ElementId loose = model.children(RootId).at(1);
    const ElementId defined = model.children(RootId).at(2);
    const auto center = [&](ElementId id) { return controller.nodes()->find(id)->Rect.center(); };
    // the part tool onto it: a variant part
    const QString added = controller.addElement(DiagramTypes::ToolPart, center(choice).x(), center(choice).y());
    CHECK(!added.isEmpty());
    if (added.isEmpty())
        return;
    CHECK(model.element(added.toULongLong()).kind() == ElementKind::PartUsage);
    CHECK_EQ(model.element(added.toULongLong()).owner(), choice);
    CHECK(SysMLCodeGenerator().generate(model).find("    variant part part1;") != std::string::npos);
    // a part definition is not taken
    CHECK(controller.addElement(DiagramTypes::ToolPartDefinition, center(choice).x(), center(choice).y()).isEmpty());
    // a part moved into it becomes a variant, moved out again no variant any more
    controller.setNotation(key(choice), "variants", "line");
    controller.moveNode(key(loose), center(choice).x() - 20, center(choice).y() - 10);
    controller.finishMove(key(loose));
    CHECK_EQ(model.element(loose).owner(), choice);
    CHECK(SysMLCodeGenerator().generate(model).find("    variant part loose;") != std::string::npos);
    // like a part on a composition line it stays when moved; removing its line takes it out
    QString lineKey;
    for (const auto &edge: controller.edges()->edges())
        if (edge.Kind == DiagramTypes::VariantMembership && edge.Target == loose)
            lineKey = edge.Key;
    CHECK(!lineKey.isEmpty());
    controller.deleteEdge(lineKey);
    CHECK_EQ(model.element(loose).owner(), RootId);
    CHECK(SysMLCodeGenerator().generate(model).find("\npart loose;") != std::string::npos);
    // a definition moved onto it stays where it is
    controller.moveNode(key(defined), center(choice).x() - 20, center(choice).y() - 10);
    controller.finishMove(key(defined));
    CHECK_EQ(model.element(defined).owner(), RootId);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(SysMLCodeGenerator().generate(model), again).lossless());
}

TEST_CASE("Editor: enum values with name, value and unit, with or without 'enum' (8.2.2.8, table 6)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("enum def DiameterChoice;\npart def Engine;\n", model).lossless());
    DiagramController controller(model);
    const ElementId choice = model.children(RootId).at(0);
    const ElementId engine = model.children(RootId).at(1);
    const auto center = [&](ElementId id) { return controller.nodes()->find(id)->Rect.center(); };
    // only onto an enum def
    CHECK(controller.addElement(DiagramTypes::ToolEnumValue, center(engine).x(), center(engine).y()).isEmpty());
    const QString first = controller.addElement(DiagramTypes::ToolEnumValue, center(choice).x(), center(choice).y());
    const QString second = controller.addElement(DiagramTypes::ToolEnumValue, center(choice).x(), center(choice).y());
    const QString third = controller.addElement(DiagramTypes::ToolEnumValue, center(choice).x(), center(choice).y());
    CHECK(!first.isEmpty());
    CHECK(!third.isEmpty());
    if (first.isEmpty() || third.isEmpty())
        return;
    CHECK(controller.elementDetails(first).value("isEnumValue").toBool());
    CHECK(controller.elementDetails(first).value("mayBeUnnamed").toBool());
    // "enum small = 60 [mm];", "= 70 [mm];" (no name, no keyword), "large;"
    CHECK_EQ(controller.updateElement(first, {{"name", "small"}, {"enumValue", "60"}, {"enumUnit", "mm"}, {"enumKeyword", true}}),
             QString());
    CHECK_EQ(controller.updateElement(second, {{"name", ""}, {"enumValue", "70"}, {"enumUnit", "mm"}, {"enumKeyword", false}}),
             QString());
    CHECK_EQ(controller.updateElement(third, {{"name", "large"}, {"enumValue", ""}, {"enumUnit", ""}, {"enumKeyword", false}}),
             QString());
    // neither name nor value; a unit without value
    CHECK(!controller.updateElement(third, {{"name", ""}, {"enumValue", ""}, {"enumUnit", ""}, {"enumKeyword", true}}).isEmpty());
    CHECK(!controller.updateElement(third, {{"name", "large"}, {"enumValue", ""}, {"enumUnit", "mm"}, {"enumKeyword", true}}).isEmpty());
    const std::string text = SysMLCodeGenerator().generate(model);
    CHECK(text.find("enum def DiameterChoice {\n    enum small = 60 [mm];\n    = 70 [mm];\n    large;\n}") != std::string::npos);
    const auto details = controller.elementDetails(first);
    CHECK_EQ(details.value("enumValue").toString(), QString("60"));
    CHECK_EQ(details.value("enumUnit").toString(), QString("mm"));
    CHECK(details.value("enumKeyword").toBool());
    CHECK(!controller.elementDetails(second).value("enumKeyword").toBool());
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(again), text);
}
