//
// ParametricController: values, calls, constraints and their lines - read from the model and written back.
//

#include "../TestFramework.h"
#include "QtPrinting.h"

#include <DiagramController.h>
#include <ParametricController.h>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/SemanticModel.h>

using namespace StructuraSystems::Client;
using namespace StructuraSystems::Diagram;

namespace {
    const char *FuelTrim = R"(part def SHORT_TIME_INJECTION_CORRECTION_1_3 {
#measurable attribute VariableA : Integer;
#measurable attribute VariableB : Integer;
assert constraint ByteRange {
VariableA >= 0 and VariableA <= 255 and
VariableB >= 0 and VariableB <= 255
}
function decodeFuelTrim {
in feature raw : Integer;
return feature trimPercent : Rational = raw * 100.0 / 128.0 - 100.0;
}
derived #measurable attribute PercentA : Rational = decodeFuelTrim(VariableA);
derived #measurable attribute PercentB : Rational = decodeFuelTrim(VariableB);
assert constraint CorrectionRange {
PercentA >= -100.0 and PercentA <= 99.22 and
PercentB >= -100.0 and PercentB <= 99.22
}
}
)";

    std::optional<ElementId> find(const SemanticModel &model, ElementId owner, const std::string &name) {
        for (const ElementId id: model.children(owner)) {
            if (model.element(id).name() == name)
                return id;
        }
        return std::nullopt;
    }

    QString key(ElementId id) {
        return QString::number(id);
    }

    int countEdges(const ParametricController &controller, DiagramTypes::EdgeKind kind) {
        int count = 0;
        for (const auto &edge: controller.edges()->edges())
            count += edge.Kind == kind ? 1 : 0;
        return count;
    }

    const DiagramNode &node(const ParametricController &controller, std::uint64_t id) {
        return *controller.nodes()->find(id);
    }

    QString pinName(const DiagramNode &node, int pin, bool output = false) {
        return (output ? node.OutPins : node.InPins).at(pin).toMap().value("name").toString();
    }

    std::string generated(const SemanticModel &model) {
        return SysMLCodeGenerator().generate(model);
    }

    /// Imports the generated text again: the same lines must come out of it.
    void checkRoundTrip(const SemanticModel &model, const ParametricController &controller) {
        SemanticModel again;
        CHECK(SysMLTextImporter::importText(generated(model), again).lossless());
        DiagramController structure(again);
        ParametricController view(again, &structure);
        view.setContextId(QString::number(*find(again, RootId, again.element(controller.contextId().toULongLong()).name())));
        view.syncNow();
        CHECK_EQ(view.nodes()->count(), controller.nodes()->count());
        CHECK_EQ(view.edges()->count(), controller.edges()->count());
    }

    struct Fixture {
        SemanticModel Model;
        DiagramController Structure{Model};
        ParametricController View{Model, &Structure};

        explicit Fixture(const std::string &text) {
            CHECK(SysMLTextImporter::importText(text, Model).lossless());
            View.syncNow();
        }

        ElementId context() const { return ElementId(View.contextId().toULongLong()); }
        ElementId operator[](const std::string &name) const { return *find(Model, context(), name); }
    };
}

TEST_CASE("Parametric: Timo's fuel trim block as values, calls and constraints") {
    Fixture f(FuelTrim);
    CHECK_EQ(f.Model.element(f.context()).name(), std::string("SHORT_TIME_INJECTION_CORRECTION_1_3"));
    // 4 values, 2 calls, 2 constraints (the function definition is in the palette, not a node)
    CHECK_EQ(f.View.nodes()->count(), 8);
    CHECK_EQ(f.View.definitions().size(), 1);

    const auto &callA = node(f.View, ParametricController::computationNodeId(f["PercentA"]));
    CHECK(callA.Kind == DiagramTypes::CallNode);
    CHECK_EQ(callA.Keyword, QString("«function»"));
    CHECK_EQ(callA.Title, QString("decodeFuelTrim"));
    CHECK_EQ(pinName(callA, 0), QString("raw"));
    CHECK_EQ(pinName(callA, 0, true), QString("trimPercent"));
    CHECK_EQ(node(f.View, f["VariableA"]).Keyword, QString("«measurable»"));
    CHECK_EQ(node(f.View, f["PercentA"]).Title, QString("/PercentA : Rational"));
    CHECK_EQ(node(f.View, f["ByteRange"]).Keyword, QString("«assert constraint»"));

    CHECK_EQ(countEdges(f.View, DiagramTypes::Binding), 2);   // VariableA -> raw, VariableB -> raw
    CHECK_EQ(countEdges(f.View, DiagramTypes::ValueFlow), 2); // calls -> PercentA / PercentB
    CHECK_EQ(countEdges(f.View, DiagramTypes::Reference), 4); // values used in the two constraints
    CHECK(f.View.edges()->find(QString("a:%1:0").arg(f["PercentA"])) != nullptr);
    checkRoundTrip(f.Model, f.View);
}

TEST_CASE("Parametric: a calc def dropped onto the canvas and connected by lines") {
    Fixture f("part def Sensor {\n"
              "    attribute VariableA : Integer;\n"
              "    attribute VariableB : Integer;\n"
              "    calc def decode {\n"
              "        in raw : Integer;\n"
              "        in offset : Integer;\n"
              "        return value : Real = raw - offset;\n"
              "    }\n"
              "}\n");
    const QString definition = f.View.definitions().at(0).toMap().value("id").toString();
    const QString created = f.View.dropDefinition(definition, 400, 300);
    CHECK(!created.isEmpty());
    const ElementId value = created.toULongLong();
    CHECK_EQ(f.Model.element(value).name(), std::string("value"));
    CHECK_EQ(f.Model.element(value).value(), std::string("decode(raw, offset)"));
    CHECK_EQ(f.Model.element(value).typeName(), std::string("Real"));
    const auto call = ParametricController::computationNodeId(value);
    CHECK(node(f.View, call).InPins.at(0).toMap().value("open").toBool()); // not connected yet

    // VariableA -> raw, VariableB -> offset (dragged the other way round: from the pin to the value)
    const QVariantMap options = f.View.connectOptions(key(f["VariableA"]), -1, true, key(call), 0, false);
    CHECK_EQ(options.value("options").toList().size(), 1);
    CHECK(f.View.connectPins(key(f["VariableA"]), -1, true, key(call), 0, false, "argument").isEmpty());
    CHECK(f.View.connectPins(key(call), 1, false, key(f["VariableB"]), -1, false, "argument").isEmpty());
    CHECK_EQ(f.Model.element(value).value(), std::string("decode(VariableA, VariableB)"));
    CHECK(generated(f.Model).find("    derived attribute value : Real = decode(VariableA, VariableB);\n") != std::string::npos);
    CHECK_EQ(countEdges(f.View, DiagramTypes::Binding), 2);
    checkRoundTrip(f.Model, f.View);

    // a value cannot be computed from itself
    CHECK(!f.View.connectPins(key(value), -1, true, key(call), 0, false, "argument").isEmpty());

    // deleting the line leaves the parameter open again
    f.View.deleteEdge(QString("a:%1:1").arg(value));
    CHECK_EQ(f.Model.element(value).value(), std::string("decode(VariableA, offset)"));
    // deleting the call node removes the value expression
    f.View.select(key(call), false);
    f.View.deleteSelection();
    CHECK(f.Model.element(value).value().empty());
    CHECK(f.View.nodes()->find(call) == nullptr);
}

TEST_CASE("Parametric: a calc def dropped onto a value computes it") {
    Fixture f("part def P {\n"
              "    attribute a : Real;\n"
              "    attribute b : Real;\n"
              "    calc def twice { in x : Real; return y : Real = x * 2; }\n"
              "}\n");
    const QRectF target = f.View.nodeRect(key(f["b"]));
    const QString definition = f.View.definitions().at(0).toMap().value("id").toString();
    f.View.dropDefinition(definition, target.center().x(), target.center().y());
    CHECK_EQ(f.Model.element(f["b"]).value(), std::string("twice(x)"));
    CHECK(f.View.connectPins(key(f["a"]), -1, true, key(ParametricController::computationNodeId(f["b"])), 0, false,
                         "argument").isEmpty());
    CHECK_EQ(f.Model.element(f["b"]).value(), std::string("twice(a)"));
}

TEST_CASE("Parametric: constraint def with parameters bound to values") {
    Fixture f("package Limits {\n"
              "    constraint def Range {\n"
              "        in x : Real;\n"
              "        in limit : Real;\n"
              "        x >= 0 and x <= limit\n"
              "    }\n"
              "}\n"
              "part def Engine {\n"
              "    attribute temperature : Real;\n"
              "    attribute maximum : Real = 120;\n"
              "}\n");
    CHECK_EQ(f.Model.element(f.context()).name(), std::string("Engine"));
    const QString definition = f.View.definitions().at(0).toMap().value("id").toString();
    const QString usage = f.View.dropDefinition(definition, 600, 100);
    const ElementId constraint = usage.toULongLong();
    CHECK_EQ(f.Model.element(constraint).typeName(), std::string("Limits::Range"));
    CHECK_EQ(node(f.View, constraint).InPins.size(), 2);
    CHECK_EQ(pinName(node(f.View, constraint), 1), QString("limit"));

    CHECK(f.View.connectPins(key(f["maximum"]), -1, true, usage, 1, false, "parameter").isEmpty());
    CHECK(f.View.connectPins(key(f["temperature"]), -1, true, usage, 0, false, "parameter").isEmpty());
    CHECK(generated(f.Model).find("    assert constraint range : Limits::Range {\n"
                                  "        in x = temperature;\n"
                                  "        in limit = maximum;\n"
                                  "    }\n") != std::string::npos);
    CHECK_EQ(countEdges(f.View, DiagramTypes::Binding), 2);
    checkRoundTrip(f.Model, f.View);

    // a constraint without parameter: only via its expression
    const QVariantMap body = f.View.connectOptions(key(f["maximum"]), -1, true, usage, -1, false);
    CHECK(body.value("options").toList().isEmpty());
    CHECK(!body.value("error").toString().isEmpty());

    f.View.deleteEdge(QString("p:%1").arg(*find(f.Model, constraint, "x")));
    CHECK(generated(f.Model).find("        in x;\n") != std::string::npos);
}

TEST_CASE("Parametric: value to value as value or binding, calc usage results and expressions") {
    Fixture f("part def P {\n"
              "    attribute a : Real;\n"
              "    attribute b : Real;\n"
              "    attribute c : Real;\n"
              "    attribute d : Real = a * 2 + b;\n"
              "}\n");
    // expression: lines from the values it uses
    const auto expression = ParametricController::computationNodeId(f["d"]);
    CHECK(node(f.View, expression).Kind == DiagramTypes::ExpressionNode);
    CHECK_EQ(node(f.View, expression).Title, QString("a * 2 + b"));
    CHECK_EQ(countEdges(f.View, DiagramTypes::Reference), 2);
    CHECK(!f.View.connectPins(key(expression), -1, true, key(f["c"]), -1, false, "value").isEmpty());

    const QVariantMap options = f.View.connectOptions(key(f["a"]), -1, true, key(f["b"]), -1, false);
    CHECK_EQ(options.value("options").toList().size(), 2);
    CHECK(f.View.connectPins(key(f["a"]), -1, true, key(f["b"]), -1, false, "bind").isEmpty());
    CHECK(f.View.connectPins(key(f["a"]), -1, true, key(f["c"]), -1, false, "value").isEmpty());
    CHECK(generated(f.Model).find("    attribute c : Real = a;\n") != std::string::npos);
    CHECK(generated(f.Model).find("    bind b = a;\n") != std::string::npos);
    checkRoundTrip(f.Model, f.View);

    // a calculation of the part and its result as value
    const QString calc = f.View.addUsage(DiagramTypes::ToolCalc, 500, 400);
    CHECK(!calc.isEmpty());
    CHECK_EQ(node(f.View, calc.toULongLong()).OutPins.size(), 1);
    CHECK(f.View.connectPins(calc, 0, true, key(f["c"]), -1, false, "value").isEmpty());
    CHECK(generated(f.Model).find("    attribute c : Real = calc1;\n") != std::string::npos);
    CHECK(f.View.connectPins(key(f["a"]), -1, true, calc, 0, false, "parameter").isEmpty());
    checkRoundTrip(f.Model, f.View);

    // deleting lines
    for (const auto &edge: f.View.edges()->edges()) {
        if (edge.Key.startsWith("b:")) {
            f.View.deleteEdge(edge.Key);
            break;
        }
    }
    CHECK(generated(f.Model).find("bind") == std::string::npos);
    f.View.deleteEdge(QString("v:%1").arg(f["c"]));
    CHECK(f.Model.element(f["c"]).value().empty());
}

TEST_CASE("Parametric: new values, layout and choosing the part") {
    Fixture f("part def A {\n    attribute x;\n}\npart def B;\n");
    CHECK_EQ(f.View.contexts().size(), 2);
    const QString value = f.View.addValue(300, 200);
    CHECK_EQ(f.Model.element(value.toULongLong()).name(), std::string("value"));
    CHECK(f.View.nodeRect(value).topLeft() == QPointF(240, 180));
    f.View.layoutAll();
    CHECK_EQ(f.View.nodeRect(value).left(), 40.0);

    const QVariantMap hit = f.View.hitTest(f.View.nodeRect(value).center().x(), f.View.nodeRect(value).center().y());
    CHECK_EQ(hit.value("node").toString(), value);
    CHECK_EQ(hit.value("pin").toInt(), -1);

    // a value dropped onto a constraint is a new parameter of it
    const QString constraint = f.View.addUsage(DiagramTypes::ToolConstraint, 600, 100);
    const QRectF box = f.View.nodeRect(constraint);
    const QString parameter = f.View.addValue(box.center().x(), box.center().y());
    CHECK_EQ(f.Model.element(parameter.toULongLong()).owner(), constraint.toULongLong());
    CHECK_EQ(f.View.nodes()->find(constraint.toULongLong())->InPins.size(), 1);

    f.View.setContextId(f.View.contexts().at(1).toMap().value("id").toString());
    CHECK_EQ(f.View.nodes()->count(), 0);
}
