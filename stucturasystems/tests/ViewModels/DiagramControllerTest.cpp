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
    CHECK_EQ(controller.nodes()->nodes().front().Title, QString("engine : Engine[1..*]"));

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
    // the result is the last parameter, "return ..." (no compartment of its own)
    CHECK_EQ(entryTexts(decode, "parameters"), QStringList({QString("in raw : Integer"),
                                                            QString("return trimPercent : Rational = raw * 100.0 / 128.0 - 100.0")}));
    CHECK(entryTexts(decode, "result").isEmpty());

    const DiagramNode &twice = controller.nodes()->nodes().at(2);
    CHECK_EQ(twice.Keyword, QString("«calc def»"));
    CHECK_EQ(entryTexts(twice, "attributes"), QStringList({QString("factor : Real = 2")}));
    CHECK_EQ(entryTexts(twice, "expression"), QStringList({QString("x * factor")}));

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
    CHECK_EQ(entryTexts(*controller.nodes()->find(model.children(RootId).at(2)), "ports"),
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
    CHECK_EQ(controller.edges()->edges().front().Label, QString(": Fuel"));
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
    CHECK_EQ(titles, QStringList({"doc", "subject", "stakeholders", "constraints"}));
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
    CHECK(texts.contains("mass :>> mass = 120.0"));
    const ElementId vehicle = model.children(RootId)[2];
    const ElementId smallEngine = model.children(vehicle).front();
    CHECK_EQ(structure.nodes()->find(smallEngine)->Title, QString("smallEng : SmallEngine redefines eng"));
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
    CHECK(byKeyword.contains("«alias» Car for Automobile"));
    CHECK(byKeyword.contains("«metadata def» Safety"));
    CHECK(byKeyword.contains("«metadata» Safety"));
    CHECK(byKeyword.contains("«view def» StructureView"));
    CHECK(byKeyword.contains("«view» vehicleView : StructureView"));
    CHECK_EQ(DiagramNodeModel::entryCount(*byKeyword.value("«metadata» Safety"), "about"), 1);
    CHECK_EQ(DiagramNodeModel::entryCount(*byKeyword.value("«view» vehicleView : StructureView"), "expose"), 1);
    CHECK_EQ(DiagramNodeModel::entryCount(*byKeyword.value("«view» vehicleView : StructureView"), "render"), 1);
    CHECK_EQ(DiagramNodeModel::entryCount(*byKeyword.value("«view def» StructureView"), "filter"), 1);
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

TEST_CASE("Controller: «import» arrows between packages, library imports as dashed packages") {
    SemanticModel model;
    SysMLTextImporter::importText("package Definitions { part def Vehicle; }\n"
                                  "package Usages {\n    private import Definitions::*;\n    private import ISQ::*;\n"
                                  "    part vehicle : Vehicle;\n}\n",
                                  model);
    DiagramController controller(model);

    const auto *import = edgeOfKind(controller, DiagramTypes::Import);
    CHECK(import != nullptr);
    if (!import)
        return;
    CHECK_EQ(import->Label, QString("«import» *"));
    CHECK_EQ(model.element(import->Source).name(), std::string("Usages"));
    CHECK_EQ(model.element(import->Target).name(), std::string("Definitions"));
    bool library = false;
    for (const auto &node: controller.nodes()->nodes()) {
        if (node.Kind == DiagramTypes::LibraryNode) {
            library = true;
            CHECK_EQ(node.Title, QString("ISQ"));
            CHECK_EQ(node.Keyword, QString("private «import» *"));
        }
    }
    CHECK(library);
    // vehicle : Vehicle is drawn as "defined by" arrow
    const auto *definedBy = edgeOfKind(controller, DiagramTypes::DefinedBy);
    CHECK(definedBy != nullptr);
    if (!definedBy)
        return;
    CHECK_EQ(model.element(definedBy->Target).name(), std::string("Vehicle"));

    // deleting the arrows removes the import and the type
    controller.deleteEdge(QString(import->Key));
    CHECK(generated(model).find("import Definitions::*") == std::string::npos);
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

    CHECK(controller.createEdge(DiagramTypes::Import, key(b), key(a)));
    CHECK(controller.createEdge(DiagramTypes::DefinedBy, key(engine), key(engineDef)));
    CHECK(controller.createEdge(DiagramTypes::Performs, key(engine), key(start)));
    const std::string text = generated(model);
    CHECK(text.find("private import A::*;") != std::string::npos);
    CHECK(text.find("part engine : Engine {") != std::string::npos);
    CHECK(text.find("perform start;") != std::string::npos);
    CHECK(edgeOfKind(controller, DiagramTypes::Import) != nullptr);
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
    CHECK_EQ(entryTexts(vehicleB, "attributes"), QStringList({QString("mass redefines mass = 1500"), QString("^velocity")}));
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
             QStringList({QString("do action2b :>> action2"), QString("entry ^action1"), QString("exit ^action3")}));
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
    CHECK_EQ(entryTexts(force, "parameters"), QStringList({QString("in m :> ISQ::mass"), QString("in a :> ISQ::acceleration"),
                                                           QString("return f :> ISQ::force = m * a")}));
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
    CHECK_EQ(entryTexts(action, "parameters"), QStringList({QString("in pwrCmd : PwrCmd"), QString("out torqueToWheels : Torque[*]")}));
}
