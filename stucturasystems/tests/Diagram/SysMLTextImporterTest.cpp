//
// SysML / KerML text -> SemanticModel.
//

#include "../TestFramework.h"

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>

#include <random>

using namespace StructuraSystems::Diagram;

static std::ostream &operator<<(std::ostream &out, const std::vector<std::string> &list) {
    out << "{";
    for (std::size_t i = 0; i < list.size(); ++i)
        out << (i > 0 ? ", " : "") << '"' << list[i] << '"';
    return out << "}";
}

namespace {
    std::string roundTrip(const std::string &text) {
        SemanticModel model;
        const auto report = SysMLTextImporter::importText(text, model);
        CHECK(report.lossless());
        return SysMLCodeGenerator().generate(model);
    }

    std::optional<ElementId> find(const SemanticModel &model, ElementId owner, const std::string &name) {
        for (const ElementId id: model.children(owner)) {
            if (model.element(id).name() == name)
                return id;
        }
        return std::nullopt;
    }

    // The DTTester4Project blocks (constraint defs, part defs with metadata, functions and derived attributes).
    const std::string DTTester = R"(
constraint def NonNegativeReal {
    in x : Real;
    x >= 0.0
}

part def INJECTION_SYSTEM_STATUS {
    #measurable attribute VariableA : Integer;
    #measurable attribute VariableB : Integer;

    assert constraint ByteRange {
        VariableA >= 0 and VariableA <= 255 and
        VariableB >= 0 and VariableB <= 255
    }

    function translateIntoInjection_System_Status {
        in feature varA : Integer;
        in feature varB : Integer;
        return feature injection_system_status_String : String;
    }

    derived #measurable attribute Injection_System_Status_attr : String =
        translateIntoInjection_System_Status(VariableA, VariableB);
}

part def COOLING_FLUID_TEMPERATURE {
    #measurable attribute VariableA : Integer;

    function decodeCoolingFluidTemperature {
        in feature varA : Integer;
        return feature coolingFluidTemperature : celsiusTemperature =
            (varA - 40) * (1 ['°C']);
    }

    derived #measurable attribute Cooling_Fluid_Temp : celsiusTemperature =
        decodeCoolingFluidTemperature(VariableA);

    assert constraint CoolingFluidTemperatureRange {
        Cooling_Fluid_Temp >= -40 ['°C'] and
        Cooling_Fluid_Temp <= 215 ['°C']
    }
}
)";
}

TEST_CASE("Importer: generated vehicle text survives the round trip") {
    const std::string text =
            "part vehicle {\n"
            "    attribute mass : ISQ::MassValue;\n"
            "    perform action providePower;\n"
            "    part transmission {\n"
            "        attribute mass;\n"
            "        perform amplifyTorque;\n"
            "    }\n"
            "    part engine : Engine[1] {\n"
            "        attribute mass;\n"
            "        part cylinders[6];\n"
            "        part valves[1..*];\n"
            "        part spark[*];\n"
            "        part injectors[4..8];\n"
            "    }\n"
            "    connect engine to transmission;\n"
            "    flow from engine.cylinders to transmission;\n"
            "}\n";
    CHECK_EQ(roundTrip(text), text);
}

TEST_CASE("Importer: packages, definitions and specializations survive the round trip") {
    const std::string text =
            "package Vehicles {\n"
            "    part def Vehicle;\n"
            "    part def Car :> Vehicle;\n"
            "    package Parts {\n"
            "        part def Engine;\n"
            "        part def 'Diesel Engine' :> Engine;\n"
            "    }\n"
            "}\n"
            "part def Truck :> Vehicles::Vehicle, Vehicles::Car;\n"
            "part 'my car' :> truck;\n"
            "part truck;\n";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    CHECK_EQ(report.ImportedRelationships, std::size_t(5));
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
}

TEST_CASE("Importer: the DTTester blocks are kept completely") {
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(DTTester, model);
    CHECK(report.lossless());

    CHECK_EQ(model.children(RootId).size(), std::size_t(3)); // constraint def + two part defs
    const ElementId nonNegative = model.children(RootId).at(0);
    CHECK(model.element(nonNegative).kind() == ElementKind::ConstraintDefinition);
    CHECK_EQ(model.element(nonNegative).expression(), std::string("x >= 0.0"));
    CHECK_EQ(SysMLTextImporter::summarize(model, nonNegative).Display, std::string("NonNegativeReal(x : Real) { x >= 0.0 }"));
    const auto injection = find(model, RootId, "INJECTION_SYSTEM_STATUS");
    CHECK(injection.has_value());
    CHECK(model.element(*injection).kind() == ElementKind::PartDefinition);
    // VariableA, VariableB, assert constraint, function, derived attribute
    CHECK_EQ(model.children(*injection).size(), std::size_t(5));
    const auto variableA = find(model, *injection, "VariableA");
    CHECK_EQ(model.element(*variableA).prefixes().size(), std::size_t(1));
    CHECK_EQ(model.element(*variableA).prefixes().front(), std::string("#measurable"));
    const auto derived = find(model, *injection, "Injection_System_Status_attr");
    CHECK_EQ(model.element(*derived).typeName(), std::string("String"));
    CHECK_EQ(model.element(*derived).value(), std::string("translateIntoInjection_System_Status(VariableA, VariableB)"));
    CHECK_EQ(model.element(*derived).prefixes().front(), std::string("derived"));

    // generate -> import -> generate is stable and keeps every statement
    const std::string generated = SysMLCodeGenerator().generate(model);
    for (const char *part: {"constraint def NonNegativeReal", "assert constraint ByteRange", "function translateIntoInjection_System_Status",
                            "derived #measurable attribute Cooling_Fluid_Temp : celsiusTemperature = decodeCoolingFluidTemperature(VariableA);",
                            "(varA - 40) * (1 ['°C'])", "Cooling_Fluid_Temp <= 215 ['°C']"})
        CHECK(generated.find(part) != std::string::npos);
    CHECK_EQ(roundTrip(generated), generated);
}
TEST_CASE("Importer: references that are not in the block are kept as written") {
    SemanticModel model;
    const auto report = SysMLTextImporter::importText("part def Car :> Vehicles::Vehicle;\npart a;\nconnect a to b;\n", model);
    CHECK(report.lossless());
    CHECK(model.relationships().empty());
    const auto car = find(model, RootId, "Car");
    CHECK_EQ(model.element(*car).externalSpecializations().size(), std::size_t(1));
    CHECK_EQ(SysMLCodeGenerator().generate(model), std::string("part def Car :> Vehicles::Vehicle;\npart a;\nconnect a to b;\n"));
}
TEST_CASE("Importer: rule violations and duplicates are kept as text") {
    SemanticModel model;
    const std::string text = "attribute loose;\npart a {\n    part b;\n    part b;\n}\npart def D {\n    package P;\n}\n";
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    CHECK(model.element(model.children(RootId).at(0)).kind() == ElementKind::Textual);
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
}
TEST_CASE("Importer: comments, doc and imports are kept") {
    SemanticModel model;
    const std::string text = "// note\nprivate import ScalarValues::*;\npart def A {\n    doc /* the A */\n    attribute x;\n    /* block\n     * comment */\n}\n";
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    CHECK_EQ(model.element(model.children(RootId).at(0)).keyword(), std::string("comment"));
    CHECK_EQ(model.element(model.children(RootId).at(1)).keyword(), std::string("import"));
}
TEST_CASE("Importer: KerML classes and features") {
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(
            "package P {\n    class Base;\n    struct Point specializes Base {\n        feature x : Real;\n"
            "        feature y : Real;\n    }\n}\n",
            model, SysMLTextImporter::Dialect::KerML);
    CHECK(report.lossless());
    const auto package = find(model, RootId, "P");
    const auto point = find(model, *package, "Point");
    CHECK(model.element(*point).kind() == ElementKind::PartDefinition);
    CHECK_EQ(model.children(*point).size(), std::size_t(2));
    CHECK_EQ(model.relationships().size(), std::size_t(1));
}

TEST_CASE("Importer: broken and random text never throws") {
    const std::string broken[] = {"part", "part def", "part a {", "}}}", "part a : ;", "part a[", "part a[1..",
                                  "connect", "connect a to", "flow from", "package {", "'unterminated", "/* open",
                                  "part a = (1 + ;", "#", "@", "perform", "perform action", "part a :> ;"};
    for (const auto &text: broken) {
        SemanticModel model;
        static_cast<void>(SysMLTextImporter::importText(text, model));
    }

    const std::string pieces[] = {"part", "def", "attribute", "package", "{", "}", ";", ":", ":>", "[", "]", "1",
                                  "..", "*", "a", "b", "connect", "to", "flow", "from", "=", "(", ")", "#", "'x'",
                                  "perform", "action", "::", "."};
    std::mt19937 random(42);
    for (int run = 0; run < 300; ++run) {
        std::string text;
        for (int i = 0; i < 40; ++i) {
            text += pieces[random() % std::size(pieces)];
            text += ' ';
        }
        SemanticModel model;
        static_cast<void>(SysMLTextImporter::importText(text, model));
        static_cast<void>(SysMLCodeGenerator().generate(model)); // the result must still be valid for the generator
    }
}

TEST_CASE("Importer: clear empties a model") {
    SemanticModel model;
    SysMLTextImporter::importText("part a; part b; connect a to b; part def C;", model);
    CHECK(!model.relationships().empty());
    SysMLTextImporter::clear(model);
    CHECK_EQ(model.elementCount(), std::size_t(0));
    CHECK(model.relationships().empty());
}

TEST_CASE("Importer: random models survive generate -> import -> generate") {
    const std::string names[] = {"a", "b", "engine", "Vehicle", "part", "my part", "x1", "'quoted'", "def", "Ä"};
    const std::string types[] = {"", "", "Engine", "ISQ::MassValue", "Real"};
    std::mt19937 random(7);
    int shadowed = 0;
    for (int run = 0; run < 1000; ++run) {
        SemanticModel model;
        std::vector<ElementId> ids;
        for (int step = 0; step < 25; ++step) {
            const auto kind = static_cast<ElementKind>(random() % 9); // every kind but Textual
            const ElementId owner = ids.empty() || random() % 3 == 0 ? RootId : ids[random() % ids.size()];
            const std::string name = names[random() % std::size(names)] + std::to_string(random() % 3);
            try {
                const ElementId id = model.addElement(kind, name, owner);
                ids.push_back(id);
                if (kind == ElementKind::PartUsage || kind == ElementKind::Attribute) {
                    model.setTypeName(id, types[random() % std::size(types)]);
                    if (random() % 2)
                        model.setMultiplicity(id, Multiplicity::range(random() % 3, random() % 2 ? std::optional<std::size_t>()
                                                                                             : std::optional<std::size_t>(3 + random() % 3)));
                }
                if (kind == ElementKind::ActionPerformance && random() % 2)
                    model.setPerformStyle(id, PerformStyle::ReferenceAction);
                if (random() % 4 == 0 && kind != ElementKind::Package)
                    model.setPrefixes(id, random() % 2 ? std::vector<std::string>{"derived", "#measurable"}
                                                       : std::vector<std::string>{"#ISQ::quantity"});
                if ((kind == ElementKind::Attribute || kind == ElementKind::PartUsage) && random() % 3 == 0)
                    model.setValue(id, random() % 2 ? "=" : ":=", "f(a, b) * 2.0");
                if (kind == ElementKind::PartDefinition && random() % 3 == 0)
                    model.setExternalSpecializations(id, {"Library::Base"});
                if (isConstraintOrCalc(kind)) {
                    if (random() % 3 != 0)
                        model.setExpression(id, random() % 2 ? "x > 0" : "a +\nb // sum\n* 2");
                    if (kind == ElementKind::ConstraintUsage && random() % 2)
                        model.setDeclaredKeyword(id, "assert constraint");
                    if (kind == ElementKind::CalcDefinition && random() % 2)
                        model.setDeclaredKeyword(id, "function");
                    if ((kind == ElementKind::ConstraintUsage || kind == ElementKind::CalcUsage) && random() % 2)
                        model.setTypeName(id, types[random() % std::size(types)]);
                }
                if (kind == ElementKind::Attribute && owner != RootId && isConstraintOrCalc(model.element(owner).kind()) &&
                    random() % 3 != 0) {
                    const char *directions[] = {"in", "out", "inout", "return"};
                    model.setPrefixes(id, {directions[random() % 4]});
                    const int keyword = int(random() % 3);
                    if (keyword > 0)
                        model.setDeclaredKeyword(id, keyword == 1 ? std::string() : std::string("feature"));
                }
                if (random() % 4 == 0) {
                    const ElementId textual = model.addElement(ElementKind::Textual, "c", id == RootId ? RootId : owner);
                    const std::string statement = random() % 2 ? "assert constraint c {\n    x > 0\n}" : "// remark";
                    const auto [keyword, statementName] = SysMLTextImporter::describeStatement(statement);
                    model.setText(textual, statement, keyword, statementName);
                }
            } catch (const ModelError &) {
            }
        }
        for (int step = 0; step < 10 && ids.size() > 1; ++step) {
            const ElementId source = ids[random() % ids.size()];
            const ElementId target = ids[random() % ids.size()];
            try {
                model.addRelationship(static_cast<RelationshipKind>(random() % 4), source, target);
            } catch (const ModelError &) {
            }
        }

        const std::string text = SysMLCodeGenerator().generate(model);
        SemanticModel imported;
        const auto report = SysMLTextImporter::importText(text, imported);
        if (!report.lossless())
            std::cout << text << "\n--- " << report.details() << "\n";
        CHECK(report.lossless());
        const std::string again = SysMLCodeGenerator().generate(imported);
        if (again != text) {
            // Only possible when a top-level name is shadowed by an element of the same name in an inner
            // scope (SysML has no way to write "the top-level one"): the result must still be stable and complete.
            SemanticModel second;
            CHECK(SysMLTextImporter::importText(again, second).lossless());
            CHECK_EQ(SysMLCodeGenerator().generate(second), again);
            CHECK_EQ(imported.elementCount(), model.elementCount());
            CHECK_EQ(imported.relationships().size(), model.relationships().size());
            ++shadowed;
        }
    }
    CHECK(shadowed < 50); // the rare exception, not the rule
}

TEST_CASE("Importer: constraint and calc definitions and usages") {
    const std::string text =
            "package Calcs {\n"
            "    constraint def Positive {\n"
            "        in x : Real;\n"
            "        x > 0.0\n"
            "    }\n"
            "    constraint def Bounded :> Positive {\n"
            "        in x : Real;\n"
            "        in limit : Real;\n"
            "        x > 0.0 and\n"
            "        x <= limit\n"
            "    }\n"
            "    calc def Power {\n"
            "        in force : Real;\n"
            "        in speed : Real;\n"
            "        return power : Real = force * speed;\n"
            "    }\n"
            "    calc def Twice {\n"
            "        in attribute x : Real;\n"
            "        x * 2 // doubled\n"
            "    }\n"
            "}\n"
            "part def Engine {\n"
            "    attribute torque : Real;\n"
            "    constraint torqueLimit : Calcs::Positive;\n"
            "    assert constraint {\n"
            "        torque > 0\n"
            "    }\n"
            "    calc output : Calcs::Power;\n"
            "    calc doubled {\n"
            "        in value : Real;\n"
            "        value * 2\n"
            "    }\n"
            "}\n";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    CHECK_EQ(report.ImportedRelationships, std::size_t(1)); // Bounded :> Positive

    const auto calcs = find(model, RootId, "Calcs");
    const auto positive = find(model, *calcs, "Positive");
    CHECK(model.element(*positive).kind() == ElementKind::ConstraintDefinition);
    const auto power = find(model, *calcs, "Power");
    CHECK(model.element(*power).kind() == ElementKind::CalcDefinition);
    CHECK_EQ(SysMLTextImporter::summarize(model, *power).Display,
             std::string("Power(force : Real, speed : Real) : Real = force * speed"));
    const auto twice = find(model, *calcs, "Twice");
    CHECK_EQ(model.element(*twice).expression(), std::string("x * 2 // doubled"));
    CHECK_EQ(SysMLTextImporter::summarize(model, *twice).Display, std::string("Twice(x : Real) = x * 2"));
    const auto bounded = find(model, *calcs, "Bounded");
    CHECK_EQ(SysMLTextImporter::summarize(model, *bounded).Expression, std::string("x > 0.0 and x <= limit"));

    const auto engine = find(model, RootId, "Engine");
    const auto limit = find(model, *engine, "torqueLimit");
    CHECK(model.element(*limit).kind() == ElementKind::ConstraintUsage);
    CHECK_EQ(model.element(*limit).typeName(), std::string("Calcs::Positive"));
    // anonymous constraint: kept as text
    CHECK(model.element(model.children(*engine).at(2)).kind() == ElementKind::Textual);
    const auto doubled = find(model, *engine, "doubled");
    CHECK(model.element(*doubled).kind() == ElementKind::CalcUsage);
    CHECK_EQ(SysMLTextImporter::summarize(model, *doubled).Display, std::string("doubled(value : Real) = value * 2"));
}

TEST_CASE("Importer: unusual constraint bodies stay complete") {
    const std::string text =
            "constraint def A {\n"
            "    in x : Real;\n"
            "    x > 0 // trailing comment\n"
            "}\n"
            "calc def B {\n"
            "    in x : Real;\n"
            "    x;\n"
            "}\n"
            "calc def C {\n"
            "    x\n"
            "    y\n"
            "}\n"
            "constraint def D;\n"
            "assert constraint e : D;\n";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    const std::string generated = SysMLCodeGenerator().generate(model);
    SemanticModel second;
    CHECK(SysMLTextImporter::importText(generated, second).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(second), generated);
    // a comment after the expression stays with it
    CHECK_EQ(model.element(model.children(RootId).at(0)).expression(), std::string("x > 0 // trailing comment"));
    CHECK_EQ(generated.find("x > 0 // trailing comment\n}") != std::string::npos, true);
}

TEST_CASE("Importer: bindings between attributes and parameters") {
    const std::string text =
            "part def Engine {\n"
            "    attribute force : Real;\n"
            "    attribute speed : Real;\n"
            "    attribute power : Real;\n"
            "    calc p : Power {\n"
            "        in f : Real;\n"
            "        in v : Real;\n"
            "    }\n"
            "    bind p.f = force;\n"
            "    bind p.v = speed;\n"
            "    bind power = p.f;\n"
            "    bind unknown = force;\n"
            "}\n";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    CHECK_EQ(report.ImportedRelationships, std::size_t(3));
    const std::string generated = SysMLCodeGenerator().generate(model);
    // the unresolved binding is kept as text (written before the relationships)
    CHECK(generated.find("    bind unknown = force;\n    bind p.f = force;\n    bind p.v = speed;\n    bind power = p.f;\n}")
          != std::string::npos);
    CHECK_EQ(roundTrip(generated), generated);
}

TEST_CASE("Expressions: calls, references and their parts") {
    const auto call = SysMLTextImporter::parseCall("decodeFuelTrim(VariableA)");
    CHECK(call.has_value());
    CHECK_EQ(call->Function, std::string("decodeFuelTrim"));
    CHECK_EQ(call->Arguments.size(), std::size_t(1));
    CHECK_EQ(call->Arguments[0].Value, std::string("VariableA"));

    const auto nested = SysMLTextImporter::parseCall("Calcs::Power(raw = a.b, (x + 1) * 2, f(y), 'my value')");
    CHECK(nested.has_value());
    CHECK_EQ(nested->Function, std::string("Calcs::Power"));
    CHECK_EQ(nested->Arguments.size(), std::size_t(4));
    CHECK_EQ(nested->Arguments[0].Name, std::string("raw"));
    CHECK_EQ(nested->Arguments[0].Value, std::string("a.b"));
    CHECK_EQ(nested->Arguments[1].Value, std::string("(x + 1) * 2"));
    CHECK_EQ(nested->Arguments[3].Value, std::string("'my value'"));
    CHECK_EQ(SysMLTextImporter::formatCall(*nested), std::string("Calcs::Power(raw = a.b, (x + 1) * 2, f(y), 'my value')"));
    CHECK(SysMLTextImporter::parseCall("f()")->Arguments.empty());

    CHECK(!SysMLTextImporter::parseCall("f(a) + g(b)").has_value());
    CHECK(!SysMLTextImporter::parseCall("a + b").has_value());
    CHECK(!SysMLTextImporter::parseCall("not(a)").has_value());
    CHECK(!SysMLTextImporter::parseCall("f(a,)").has_value());

    CHECK_EQ(*SysMLTextImporter::parseReference("calc1.result"), std::string("calc1.result"));
    CHECK_EQ(*SysMLTextImporter::parseReference(" 'my value' "), std::string("'my value'"));
    CHECK(!SysMLTextImporter::parseReference("a + 1").has_value());
    CHECK(!SysMLTextImporter::parseReference("true").has_value());

    CHECK_EQ(SysMLTextImporter::references("VariableA >= 0 and VariableA <= 255 and VariableB >= 0 // x"),
             std::vector<std::string>({"VariableA", "VariableB"}));
    CHECK_EQ(SysMLTextImporter::references("Cooling >= -40 ['°C'] and f(a.b, raw = c) <= limit"),
             std::vector<std::string>({"Cooling", "a.b", "c", "limit"}));
    CHECK_EQ(SysMLTextImporter::splitReference("a::'b c'.d"), std::vector<std::string>({"a", "b c", "d"}));
}

TEST_CASE("Importer: Timo's fuel trim block is kept and summarized") {
    const std::string block = R"(part def SHORT_TIME_INJECTION_CORRECTION_1_3 {
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

derived #measurable attribute ShortTimeInjectionCorrection1_3PercentA : Rational = decodeFuelTrim(VariableA);
derived #measurable attribute ShortTimeInjectionCorrection1_3PercentB : Rational = decodeFuelTrim(VariableB);

assert constraint CorrectionRange {
ShortTimeInjectionCorrection1_3PercentA >= -100.0 and ShortTimeInjectionCorrection1_3PercentA <= 99.22 and
ShortTimeInjectionCorrection1_3PercentB >= -100.0 and ShortTimeInjectionCorrection1_3PercentB <= 99.22
}
}
)";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(block, model);
    CHECK(report.lossless());
    const ElementId definition = model.children(RootId).at(0);
    const auto &children = model.children(definition);
    CHECK_EQ(children.size(), std::size_t(7));

    const auto &byteRange = model.element(children.at(2));
    CHECK(byteRange.kind() == ElementKind::ConstraintUsage);
    CHECK_EQ(*byteRange.declaredKeyword(), std::string("assert constraint"));
    const auto constraint = SysMLTextImporter::summarize(model, children.at(2));
    CHECK(constraint.Kind == StatementSummary::Category::Constraint);
    CHECK_EQ(constraint.Display, std::string("assert ByteRange { VariableA >= 0 and VariableA <= 255 and VariableB >= 0 and VariableB <= 255 }"));

    const auto &decode = model.element(children.at(3));
    CHECK(decode.kind() == ElementKind::CalcDefinition);
    CHECK_EQ(*decode.declaredKeyword(), std::string("function"));
    CHECK_EQ(decode.ownedElements().size(), std::size_t(2)); // in raw, return trimPercent
    const auto function = SysMLTextImporter::summarize(model, children.at(3));
    CHECK(function.Kind == StatementSummary::Category::Function);
    CHECK_EQ(function.Display, std::string("decodeFuelTrim(raw : Integer) : Rational = raw * 100.0 / 128.0 - 100.0"));
    CHECK_EQ(function.Expression, std::string("raw * 100.0 / 128.0 - 100.0"));

    const auto &percentA = model.element(children.at(4));
    CHECK_EQ(percentA.value(), std::string("decodeFuelTrim(VariableA)"));

    const std::string generated = SysMLCodeGenerator().generate(model);
    CHECK(generated.find("    function decodeFuelTrim {\n        in feature raw : Integer;\n"
                         "        return feature trimPercent : Rational = raw * 100.0 / 128.0 - 100.0;\n    }\n") != std::string::npos);
    CHECK(generated.find("    derived #measurable attribute ShortTimeInjectionCorrection1_3PercentB : Rational = decodeFuelTrim(VariableB);\n")
          != std::string::npos);
    CHECK_EQ(roundTrip(generated), generated);
}

TEST_CASE("Importer: statement descriptions") {
    CHECK(SysMLTextImporter::describeStatement("assert constraint ByteRange { x }") ==
          std::make_pair(std::string("assert constraint"), std::string("ByteRange")));
    CHECK(SysMLTextImporter::describeStatement("private import ISQ::*;") ==
          std::make_pair(std::string("import"), std::string("ISQ")));
    CHECK(SysMLTextImporter::describeStatement("// note").first == "comment");
    CHECK(SysMLTextImporter::describeStatement("calc def Power { in t : Real; t * 2 }") ==
          std::make_pair(std::string("calc def"), std::string("Power")));
}
