//
// SysML / KerML text -> SemanticModel.
//

#include "../TestFramework.h"

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/RelationshipRules.h>

#include <functional>
#include <map>
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
    // an import is an element of its own (phase 8): its target as written, the visibility as prefix
    const auto &import = model.element(model.children(RootId).at(1));
    CHECK(import.kind() == ElementKind::Import);
    CHECK_EQ(import.target(), std::string("ScalarValues::*"));
    CHECK(import.prefixes() == std::vector<std::string>{"private"});
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

namespace {
    bool isPartLike(ElementKind kind) {
        return kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage;
    }
}

TEST_CASE("Importer: random models survive generate -> import -> generate") {
    const std::string names[] = {"a", "b", "engine", "Vehicle", "part", "my part", "x1", "'quoted'", "def", "Ä"};
    const std::string types[] = {"", "", "Engine", "ISQ::MassValue", "Real"};
    std::mt19937 random(7);
    int shadowed = 0;
    std::map<RelationshipKind, int> relationshipKinds;
    int unnamed = 0;
    int shortNames = 0;
    for (int run = 0; run < 1000; ++run) {
        SemanticModel model;
        std::vector<ElementId> ids;
        for (int step = 0; step < 25; ++step) {
            const auto kind = static_cast<ElementKind>(random() % 47); // every kind but Textual
            const ElementId owner = ids.empty() || random() % 3 == 0 ? RootId : ids[random() % ids.size()];
            std::string name = names[random() % std::size(names)] + std::to_string(random() % 3);
            if ((mayBeUnnamed(kind) && random() % 5 == 0) || isAnonymousKind(kind) ||
                (kind == ElementKind::MetadataUsage && random() % 2))
                name.clear(); // "require constraint { ... }", "objective { ... }", "import a::*;", "@Safety;"
            try {
                const ElementId id = model.addElement(kind, name, owner);
                ids.push_back(id);
                if (kind != ElementKind::Package && !isAnonymousKind(kind) && random() % 6 == 0 &&
                    !(name.empty() && kind == ElementKind::RequirementUsage))
                    model.setShortName(id, random() % 2 ? "R1" : "'1.2'");
                if (kind == ElementKind::RequirementUsage && owner != RootId && isCaseKind(model.element(owner).kind()) &&
                    random() % 2)
                    model.setDeclaredKeyword(id, std::string("objective"));
                if (kind == ElementKind::ConstraintUsage && owner != RootId &&
                    isRequirementKind(model.element(owner).kind()) && random() % 2)
                    model.setDeclaredKeyword(id, std::string(random() % 2 ? "require constraint" : "assume constraint"));
                if (kind == ElementKind::UseCaseUsage && owner != RootId && isUseCaseKind(model.element(owner).kind()) &&
                    random() % 2)
                    model.setDeclaredKeyword(id, std::string("include use case"));
                if (kind == ElementKind::Actor && owner != RootId && isRequirementKind(model.element(owner).kind()) &&
                    random() % 2)
                    model.setDeclaredKeyword(id, std::string("stakeholder"));
                if ((kind == ElementKind::Subject || kind == ElementKind::Actor || isRequirementKind(kind) ||
                     kind == ElementKind::UseCaseUsage || kind == ElementKind::VerificationUsage) && !isDefinitionKind(kind) &&
                    random() % 2)
                    model.setTypeName(id, types[random() % std::size(types)]);
                if ((kind == ElementKind::Subject || kind == ElementKind::Actor) && random() % 3 == 0)
                    model.setMultiplicity(id, Multiplicity::range(0, 4));
                if (kind == ElementKind::PortUsage && random() % 2)
                    model.setTypeName(id, random() % 2 ? "~FuelPort" : "Ports::'Fuel Port'");
                if (kind == ElementKind::ItemUsage && random() % 2)
                    model.setPrefixes(id, {random() % 2 ? "in" : "out"});
                if (kind == ElementKind::ControlNode)
                    model.setDeclaredKeyword(id, std::vector<std::string>{"decide", "merge", "fork", "join"}[random() % 4]);
                if (kind == ElementKind::StateUsage && random() % 3 == 0)
                    model.setDeclaredKeyword(id, std::string("exhibit state"));
                if (kind == ElementKind::ActionUsage && owner != RootId &&
                    model.element(owner).kind() == ElementKind::StateUsage && random() % 2)
                    model.setDeclaredKeyword(id, std::string(random() % 2 ? "entry action" : "do action"));
                if (kind == ElementKind::EventOccurrence && random() % 3 == 0)
                    model.setTypeName(id, "SetSpeed");
                // snapshots and time slices
                if (kind == ElementKind::OccurrenceUsage && random() % 2)
                    model.setDeclaredKeyword(id, std::string(random() % 2 ? "snapshot" : "timeslice"));
                if ((kind == ElementKind::PartUsage || kind == ElementKind::ItemUsage) && owner != RootId && random() % 5 == 0)
                    model.setDeclaredKeyword(id, std::string(random() % 2 ? "snapshot " : "timeslice ") +
                                                 (kind == ElementKind::PartUsage ? "part" : "item"));
                if (kind == ElementKind::OccurrenceUsage && random() % 3 == 0) {
                    model.setMultiplicity(id, Multiplicity::range(0, std::nullopt));
                    model.setMultiplicityQualifiers(id, random() % 2 ? "ordered" : "ordered nonunique");
                }
                if (kind == ElementKind::OccurrenceUsage && random() % 4 == 0)
                    model.setValue(id, "=", random() % 2 ? "done" : "start");
                if (kind == ElementKind::EnumerationLiteral && random() % 2)
                    model.setDeclaredKeyword(id, std::string());
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
                // variations with their variants ("variant a;" or "variant part a;")
                if ((kind == ElementKind::PartDefinition || kind == ElementKind::AttributeDefinition ||
                     kind == ElementKind::PartUsage) && random() % 4 == 0) {
                    auto prefixes = model.element(id).prefixes();
                    prefixes.insert(prefixes.begin(), "variation");
                    model.setPrefixes(id, prefixes);
                }
                if (owner != RootId && (kind == ElementKind::PartUsage || kind == ElementKind::Attribute)) {
                    const auto &ownerPrefixes = model.element(owner).prefixes();
                    if (std::find(ownerPrefixes.begin(), ownerPrefixes.end(), "variation") != ownerPrefixes.end() &&
                        random() % 2) {
                        model.setPrefixes(id, {"variant"});
                        // "variant '4cylEngine';" declares a variant of the kind of its variation
                        const ElementKind variationKind = model.element(owner).kind();
                        const bool sameKind = kind == ElementKind::Attribute ? variationKind == ElementKind::AttributeDefinition
                                                                             : isPartLike(variationKind);
                        if (sameKind && random() % 2 && !model.element(id).declaredKeyword())
                            model.setDeclaredKeyword(id, std::string());
                    }
                }
                if ((kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage) && random() % 6 == 0) {
                    auto prefixes = model.element(id).prefixes();
                    prefixes.insert(prefixes.begin(), "individual");
                    model.setPrefixes(id, prefixes);
                }
                if (kind == ElementKind::Attribute && owner != RootId && isAnalysisKind(model.element(owner).kind()) &&
                    random() % 2) {
                    model.setPrefixes(id, {random() % 2 ? "in" : "return"});
                    model.setDeclaredKeyword(id, std::string()); // "return result : Real = ...;"
                }
                if (kind == ElementKind::Subject && random() % 4 == 0)
                    model.setValue(id, "=", "vehicle_c1");
                if (kind == ElementKind::PartUsage && owner != RootId &&
                    model.element(owner).kind() == ElementKind::AllocationDefinition && random() % 2) {
                    auto prefixes = model.element(id).prefixes();
                    prefixes.push_back("end");
                    model.setPrefixes(id, prefixes);
                    model.setDeclaredKeyword(id, std::string()); // "end logical : LogicalElement;"
                }
                if ((kind == ElementKind::Attribute || kind == ElementKind::PartUsage) && random() % 3 == 0)
                    model.setValue(id, random() % 2 ? "=" : ":=", "f(a, b) * 2.0");
                if (kind == ElementKind::PartDefinition && random() % 3 == 0)
                    model.setExternalSpecializations(id, {"Library::Base"});
                if (isConstraintOrCalc(kind)) {
                    if (random() % 3 != 0)
                        model.setExpression(id, random() % 2 ? "x > 0" : "a +\nb // sum\n* 2");
                    if (kind == ElementKind::ConstraintUsage && random() % 2 && !model.element(id).declaredKeyword() &&
                        !name.empty())
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
                // phase 8: metadata, imports, aliases, filters, views, concerns and viewpoints
                if (kind == ElementKind::MetadataUsage) {
                    model.setTypeName(id, random() % 2 ? "Safety" : "SysML::PartUsage");
                    if (random() % 2)
                        model.setDeclaredKeyword(id, std::string("@"));
                    if (random() % 3 == 0)
                        model.setAbout(id, {"a", "Lib::'b c'"});
                }
                if (isTargetKind(kind)) {
                    const char *imports[] = {"ISQ::*", "a::**[@Safety and (as Safety).isMandatory]", "Lib::'x y'"};
                    model.setTarget(id, kind == ElementKind::Import ? imports[random() % 3]
                                        : kind == ElementKind::Alias ? "Lib::Thing"
                                        : kind == ElementKind::Expose ? "vehicle::**"
                                        : kind == ElementKind::Render ? "asTreeDiagram" : "@Safety and x > 1");
                    if (kind == ElementKind::Import && random() % 4 == 0)
                        model.setDeclaredKeyword(id, std::string("import all"));
                    if ((kind == ElementKind::Import || kind == ElementKind::Alias) && random() % 2)
                        model.setPrefixes(id, {random() % 2 ? "private" : "public"});
                }
                if (kind == ElementKind::RequirementUsage && !model.element(id).declaredKeyword() && random() % 3 == 0)
                    model.setDeclaredKeyword(id, std::string(random() % 2 ? "concern" : "viewpoint"));
                if (kind == ElementKind::RequirementDefinition && random() % 3 == 0)
                    model.setDeclaredKeyword(id, std::string(random() % 2 ? "concern def" : "viewpoint def"));
                // phase 7: redefinitions, actions with a form (send, if, loop, ...), parallel states, connection and
                // flow definitions with their ends
                const auto &added = model.element(id);
                if (kind == ElementKind::ActionUsage && random() % 3 == 0) {
                    const ActionForm forms[] = {ActionForm::Send, ActionForm::Accept, ActionForm::Assign, ActionForm::Terminate,
                                                ActionForm::If, ActionForm::While, ActionForm::Loop, ActionForm::For};
                    const ActionForm form = forms[random() % std::size(forms)];
                    const char *accepts[] = {"sig : Signal via p", "after 5 [s]", "when x > 0", "at t0"};
                    const std::string clause = form == ActionForm::Send ? "Ping(\"a;b\") to b"
                                               : form == ActionForm::Accept ? accepts[random() % 4]
                                               : form == ActionForm::Assign ? "x := x + 1"
                                               : form == ActionForm::Terminate ? (random() % 2 ? "" : "a.b")
                                               : form == ActionForm::If ? "x > 0"
                                               : form == ActionForm::While ? "n < 3"
                                               : form == ActionForm::For ? "i in 1..3" : "";
                    model.setActionForm(id, form, clause);
                    if ((form == ActionForm::While || form == ActionForm::Loop) && random() % 2)
                        model.setUntilCondition(id, "n >= 3");
                    if (isControlStructure(form) && !name.empty() && random() % 2)
                        model.setNamedInBody(id, true);
                    if (added.declaredKeyword() == "entry action" || added.declaredKeyword() == "do action")
                        model.setDeclaredKeyword(id, added.declaredKeyword()->substr(0, added.declaredKeyword()->find(' ')));
                }
                if ((kind == ElementKind::ActionUsage || kind == ElementKind::ControlNode) && random() % 2)
                    model.setDeclaredWithThen(id, true);
                if ((kind == ElementKind::StateUsage || kind == ElementKind::StateDefinition) && random() % 4 == 0)
                    model.setParallel(id, true);
                if (owner != RootId && isConnectionDefinitionKind(model.element(owner).kind()) &&
                    (kind == ElementKind::PartUsage || kind == ElementKind::PortUsage) && random() % 2) {
                    auto prefixes = added.prefixes();
                    prefixes.push_back("end");
                    if (random() % 2)
                        prefixes.push_back(random() % 2 ? "[1]" : "[0..*]"); // "end [1] part bead : TireBead;"
                    model.setPrefixes(id, prefixes);
                }
                if (kind == ElementKind::Attribute && owner != RootId &&
                    model.element(owner).kind() == ElementKind::FlowDefinition && !name.empty() && random() % 2) {
                    model.setPrefixes(id, {"ref"}); // "ref :>> payload : Fuel;"
                    model.setDeclaredKeyword(id, std::string());
                    model.setRedefinitions(id, ":>>", {SysMLCodeGenerator::formatName(name)}, true);
                }
                if (isUsageKind(kind) && kind != ElementKind::EnumerationLiteral && kind != ElementKind::ControlNode &&
                    !name.empty() && added.redefinitions().empty() && added.actionForm() == ActionForm::Plain &&
                    !(added.declaredKeyword() == std::string() && kind != ElementKind::Attribute) && random() % 5 == 0) {
                    // "part redefines cyl[4];", ":>> mass = 2;" (named like the redefined feature) or
                    // "part smallEng : SmallEngine redefines eng;"
                    const bool own = random() % 2;
                    const std::string keyword = random() % 2 ? ":>>" : "redefines";
                    if (own)
                        model.setRedefinitions(id, keyword, {"base", "Lib::'other one'"}, false);
                    else
                        model.setRedefinitions(id, keyword, {"Vehicle::" + SysMLCodeGenerator::formatName(name)}, true);
                    if (!own && kind == ElementKind::Attribute && random() % 2)
                        model.setDeclaredKeyword(id, std::string()); // ":>> mass = 2;"
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
        // else branches of if actions ("} else { ... }", "} else if c { ... }")
        for (const ElementId id: ids) {
            try {
                if (model.contains(id) && random() % 2)
                    model.setElseBranch(id, true);
            } catch (const ModelError &) {
            }
        }
        for (int step = 0; step < 20 && ids.size() > 1; ++step) {
            ElementId source = ids[random() % ids.size()];
            ElementId target = ids[random() % ids.size()];
            // steps 13 - 15: connection, interface, binding (rare by chance with so many kinds)
            if (step >= 10) {
                // satisfy / verify / include need special ends: pick them on purpose
                const auto pick = [&](auto accept) {
                    std::vector<ElementId> candidates;
                    for (const ElementId id: ids)
                        if (model.contains(id) && accept(model.element(id).kind()))
                            candidates.push_back(id);
                    return candidates.empty() ? ElementId(RootId) : candidates[random() % candidates.size()];
                };
                const auto requirement = [](ElementKind kind) { return kind == ElementKind::RequirementUsage; };
                const auto connectable = [](ElementKind kind) { return kind == ElementKind::PartUsage || kind == ElementKind::PortUsage; };
                const auto port = [](ElementKind kind) { return kind == ElementKind::PortUsage; };
                const auto attribute = [](ElementKind kind) { return kind == ElementKind::Attribute; };
                const auto messageEnd = [](ElementKind kind) { return isMessageEnd(kind); };
                const auto usage = [](ElementKind kind) {
                    return isUsageKind(kind) && kind != ElementKind::EnumerationLiteral && kind != ElementKind::ControlNode;
                };
                const auto any = [](ElementKind kind) { return kind != ElementKind::Textual; };
                target = pick(step == 19 ? +requirement
                              : step == 12 ? [](ElementKind kind) { return kind == ElementKind::UseCaseUsage; }
                              : step == 13 ? +connectable : step == 14 ? +port : step == 15 ? +attribute
                              : step == 16 ? +messageEnd : step == 17 ? +usage : step == 18 ? +any
                                           : +requirement);
                source = pick(step == 19 ? [](ElementKind kind) { return isRequirementKind(kind); }
                              : step == 10 ? [](ElementKind kind) { return kind == ElementKind::PartUsage || isViewKind(kind); }
                              : step == 11 ? [](ElementKind kind) { return isVerificationKind(kind); }
                              : step == 12 ? [](ElementKind kind) { return isUseCaseKind(kind); }
                              : step == 13 ? +connectable : step == 14 ? +port : step == 15 ? +attribute
                              : step == 16 ? +messageEnd : step == 17 ? +usage : +any);
                if (source == RootId || target == RootId)
                    continue;
            }
            try {
                const auto relationship = model.addRelationship(
                        step == 10 ? RelationshipKind::Satisfy : step == 11 ? RelationshipKind::Verify
                        : step == 12 ? RelationshipKind::Include
                        : step == 13 ? RelationshipKind::Connection : step == 14 ? RelationshipKind::Interface
                        : step == 15 ? RelationshipKind::Binding : step == 16 ? RelationshipKind::Message
                        : step == 17 ? RelationshipKind::Allocation : step == 18 ? RelationshipKind::Dependency
                        : step == 19 ? RelationshipKind::Frame
                        : static_cast<RelationshipKind>(random() % 14), source, target);
                const auto kind = model.relationship(relationship).Kind;
                if (kind == RelationshipKind::Succession && random() % 2)
                    model.setBehaviorDetails(relationship, random() % 2 ? "else" : "x > 0", "", "");
                if (kind == RelationshipKind::Succession && random() % 4 == 0)
                    model.setRelationshipLabel(relationship, "step", "");
                if (kind == RelationshipKind::Transition && random() % 2)
                    model.setBehaviorDetails(relationship, random() % 2 ? "ok" : "", "Start", random() % 2 ? "action go" : "");
                if ((kind == RelationshipKind::Connection || kind == RelationshipKind::Flow ||
                     kind == RelationshipKind::Interface || kind == RelationshipKind::Message ||
                     kind == RelationshipKind::Allocation) && random() % 2)
                    model.setRelationshipLabel(relationship, random() % 2 ? "link" : "", random() % 2 ? "Fuel" : "");
                if (kind == RelationshipKind::Dependency && random() % 2)
                    model.setRelationshipLabel(relationship, "uses", "");
                if (kind == RelationshipKind::Message && random() % 3 == 0)
                    model.setSequenced(relationship, true);
                if (kind == RelationshipKind::Flow && random() % 2)
                    model.setFlowDefinition(relationship, "FuelFlow");
                if (kind == RelationshipKind::Satisfy && random() % 2)
                    model.setRelationshipContext(relationship, model.relationship(relationship).Source); // "satisfy r;"
                if ((kind == RelationshipKind::Message || kind == RelationshipKind::Allocation ||
                     kind == RelationshipKind::Dependency) && random() % 3 == 0)
                    model.setRelationshipContext(relationship, RootId); // declared on the top level
                if (kind == RelationshipKind::Allocation && random() % 3 == 0) {
                    // refining an earlier top-level allocation (written in its body)
                    for (const auto &other: model.relationships()) {
                        if (other.Kind == RelationshipKind::Allocation && other.Id != relationship && !other.Parent) {
                            model.setParentRelationship(relationship, other.Id);
                            break;
                        }
                    }
                }
            } catch (const ModelError &) {
            }
        }

        for (const auto &relationship: model.relationships())
            ++relationshipKinds[relationship.Kind];
        for (const ElementId id: ids) {
            if (model.contains(id)) {
                unnamed += model.element(id).name().empty() ? 1 : 0;
                shortNames += model.element(id).shortName().empty() ? 0 : 1;
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
            // an objective written for "verify" without one is an element of its own after reading
            const auto withoutObjectives = [](const SemanticModel &counted) {
                std::size_t count = 0;
                std::function<void(ElementId)> visit = [&](ElementId owner) {
                    for (const ElementId child: counted.children(owner)) {
                        const auto &element = counted.element(child);
                        if (!(element.kind() == ElementKind::RequirementUsage && element.declaredKeyword() == "objective" &&
                              element.ownedElements().empty()))
                            ++count;
                        visit(child);
                    }
                };
                visit(RootId);
                return count;
            };
            // a relationship whose end is shadowed (a top-level name cannot be written from below a namesake) is
            // kept as written (as text, an allocation together with the ones refining it); nothing else changes
            const std::size_t lostRelationships = model.relationships().size() - imported.relationships().size();
            CHECK(imported.relationships().size() <= model.relationships().size());
            const auto elements = withoutObjectives(imported);
            CHECK(elements >= withoutObjectives(model));
            CHECK(elements - withoutObjectives(model) <= lostRelationships);
            CHECK((elements > withoutObjectives(model)) == (lostRelationships > 0));
            ++shadowed;
        }
    }
    CHECK(shadowed < 50); // the rare exception, not the rule
    // every relationship kind, unnamed elements and short names were part of the test
    CHECK(relationshipKinds.size() >= 8);
    CHECK(relationshipKinds[RelationshipKind::Satisfy] > 20);
    CHECK(relationshipKinds[RelationshipKind::Verify] > 20);
    CHECK(relationshipKinds[RelationshipKind::Include] > 10);
    CHECK(relationshipKinds[RelationshipKind::Message] > 20);
    CHECK(relationshipKinds[RelationshipKind::Allocation] > 20);
    CHECK(relationshipKinds[RelationshipKind::Dependency] > 20);
    CHECK(relationshipKinds[RelationshipKind::Frame] > 10);
    CHECK(unnamed > 20);
    CHECK(shortNames > 100);
}

TEST_CASE("Importer: interactions with events, messages and successions between lifelines") {
    // "Introduction to the SysML v2 Language - Textual Notation", Event Occurrences / Messages
    const std::string text =
            "occurrence def CruiseControlInteraction {\n"
            "    ref part driver : Driver {\n"
            "        event occurrence setSpeedSent;\n"
            "    }\n"
            "    ref part vehicle : Vehicle {\n"
            "        part cruiseController : CruiseController {\n"
            "            event occurrence setSpeedReceived;\n"
            "            then event occurrence sensedSpeedReceived;\n"
            "            then event occurrence fuelCommandSent;\n"
            "        }\n"
            "        part speedometer : Speedometer {\n"
            "            event occurrence sensedSpeedSent;\n"
            "        }\n"
            "        part engine : Engine {\n"
            "            event occurrence fuelCommandReceived;\n"
            "        }\n"
            "    }\n"
            "    message setSpeedMessage of SetSpeed from driver.setSpeedSent to vehicle.cruiseController.setSpeedReceived;\n"
            "    then message sensedSpeedMessage of SensedSpeed from vehicle.speedometer.sensedSpeedSent to "
            "vehicle.cruiseController.sensedSpeedReceived;\n"
            "    message fuelCommandMessage of FuelCommand from vehicle.cruiseController.fuelCommandSent to "
            "vehicle.engine.fuelCommandReceived;\n"
            "    first driver.setSpeedSent then vehicle.speedometer.sensedSpeedSent;\n"
            "}\n";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    std::map<RelationshipKind, int> kinds;
    for (const auto &relationship: model.relationships())
        ++kinds[relationship.Kind];
    CHECK_EQ(kinds[RelationshipKind::Message], 3);
    CHECK_EQ(kinds[RelationshipKind::Succession], 3); // two on the lifeline of the cruise controller, one across
    const ElementId interaction = model.children(RootId).front();
    CHECK(model.element(interaction).kind() == ElementKind::OccurrenceDefinition);
    for (const auto &relationship: model.relationships()) {
        if (relationship.Kind == RelationshipKind::Message) {
            CHECK(model.element(relationship.Source).kind() == ElementKind::EventOccurrence);
            CHECK(relationship.Context == std::optional<ElementId>(interaction)); // declared in the interaction
            CHECK_EQ(relationship.Sequenced, relationship.Name == "sensedSpeedMessage");
        }
    }
    // written back exactly (the messages stay in the interaction, "then event occurrence" stays)
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
}

TEST_CASE("Importer: allocations with refining allocations, allocation definitions and dependencies") {
    const std::string text =
            "package LogicalModel {\n"
            "    part def LogicalElement;\n"
            "    part torqueGenerator : LogicalElement {\n"
            "        perform action generateTorque;\n"
            "    }\n"
            "}\n"
            "package PhysicalModel {\n"
            "    part def PhysicalElement;\n"
            "    part powerTrain : PhysicalElement {\n"
            "        part engine : Engine {\n"
            "            perform action generateTorque;\n"
            "        }\n"
            "    }\n"
            "    allocation def LogicalToPhysical {\n"
            "        end logical : LogicalElement;\n"
            "        end 'physical element' : PhysicalElement;\n"
            "    }\n"
            "    allocate LogicalModel::torqueGenerator to powerTrain {\n"
            "        allocate LogicalModel::torqueGenerator.generateTorque to powerTrain.engine.generateTorque;\n"
            "        allocation detail : LogicalToPhysical allocate LogicalModel::torqueGenerator to powerTrain.engine;\n"
            "    }\n"
            "    dependency from powerTrain.engine to LogicalModel;\n"
            "}\n"
            // kept as text (several suppliers): statements kept as text come before the relationships of a body
            "dependency Schemata from PhysicalModel to LogicalModel::LogicalElement, LogicalModel::torqueGenerator;\n"
            "allocation torqueGenAlloc : PhysicalModel::LogicalToPhysical allocate LogicalModel::torqueGenerator to "
            "PhysicalModel::powerTrain;\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    std::map<RelationshipKind, int> kinds;
    int refining = 0;
    for (const auto &relationship: model.relationships()) {
        ++kinds[relationship.Kind];
        refining += relationship.Parent ? 1 : 0;
    }
    CHECK_EQ(kinds[RelationshipKind::Allocation], 4);
    CHECK_EQ(refining, 2);
    CHECK_EQ(kinds[RelationshipKind::Dependency], 1); // several suppliers: kept as written
    const ElementId physical = model.children(RootId)[1];
    const ElementId definition = model.children(physical)[2];
    CHECK(model.element(definition).kind() == ElementKind::AllocationDefinition);
    CHECK_EQ(model.children(definition).size(), std::size_t(2));
    CHECK_EQ(model.element(model.children(definition)[1]).name(), std::string("physical element"));
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);

    // an allocation whose refining allocation cannot be resolved is kept as text as a whole
    SemanticModel partial;
    const auto report = SysMLTextImporter::importText(
            "part a { perform action x; }\npart b;\nallocate a to b {\n    allocate a.x to b.missing;\n}\n", partial);
    CHECK(report.lossless());
    CHECK(partial.relationships().empty());
    CHECK(partial.element(partial.children(RootId).back()).kind() == ElementKind::Textual);
}

namespace {
    /// The first element (in id order) with that name and kind.
    ElementId findElement(const SemanticModel &model, const std::string &name, ElementKind kind) {
        for (ElementId id = 1; id < 10000; ++id) {
            if (model.contains(id) && model.element(id).name() == name && model.element(id).kind() == kind)
                return id;
        }
        return RootId;
    }
}

TEST_CASE("Importer: redefinitions, parallel states, triggers, connection and flow definitions") {
    // "Introduction to the SysML v2 Language - Textual Notation": Redefinition, Variants, Snapshots, Local Clocks,
    // Concurrent States, Change and Time Triggers, Connections, Flow Definitions
    const std::string text = R"(part def Vehicle {
    part eng : Engine;
    attribute mass : Real;
    ref part driver : Driver;
}
part def SmallVehicle :> Vehicle {
    part smallEng : SmallEngine redefines eng;
}
part def BigVehicle :> Vehicle {
    part bigEng : BigEngine :>> eng;
}
part def Engine {
    part cyl : Cylinder[4..6];
}
part def SmallEngine :> Engine {
    part redefines cyl[4];
}
part vehicle_b : Vehicle {
    :>> mass = 2000.0;
    ref part :>> driver;
    snapshot :>> start {
        :>> mass = 1500.0;
    }
}
part vehicleChoice : Vehicle {
    part redefines eng = eng::'4cylEngine';
}
part def Server {
    part :>> localClock = new Time::Clock();
    attribute today : String;
    port requestPort;
    state ServerBehavior {
        state off;
        state waiting;
        transition first off accept Start via requestPort then waiting;
        transition first waiting accept at new Time::Iso8601DateTime(today + "11:59:00") then off;
        transition first waiting accept after 5 [SI::min] then waiting;
    }
}
state def VehicleStates;
state vehicleStates : VehicleStates parallel {
    state operationalStates;
    state healthStates {
        in vehicle : Vehicle;
        in controller : VehicleController;
        do senseTemperature;
        state normal;
        state maintenance {
            entry assign vehicle.maintenanceTime := vehicle.maintenanceTime + vehicle.maintenanceInterval;
        }
        state degraded;
        transition first normal accept at vehicle.maintenanceTime then maintenance;
        transition first normal accept when senseTemperature.temp > vehicle.maxTemperature do send new OverTemp() to controller then degraded;
    }
}
connection def PressureSeat {
    end [1] part bead : TireBead;
    end [1] part mountingRim : TireMountingRim;
}
flow def FuelFlow {
    ref :>> payload : Fuel;
    end port fuelOut : FuelOutPort;
    end port fuelIn : FuelInPort;
}
part vehicle : Vehicle {
    part tankAssy : FuelTankAssembly {
        port fuelTankPort;
    }
    part eng : Engine {
        port engineFuelPort;
    }
    flow : FuelFlow of Fuel from tankAssy.fuelTankPort to eng.engineFuelPort;
}
)";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    for (ElementId id = 1; id < 10000; ++id)
        CHECK(!model.contains(id) || model.element(id).kind() != ElementKind::Textual);

    const auto &smallEngine = model.element(findElement(model, "smallEng", ElementKind::PartUsage));
    CHECK(smallEngine.redefinitions() == std::vector<std::string>{"eng"});
    CHECK_EQ(smallEngine.redefinitionKeyword(), std::string("redefines"));
    CHECK(!smallEngine.nameFromRedefinition());
    CHECK_EQ(smallEngine.typeName(), std::string("SmallEngine"));
    const auto &bigEngine = model.element(findElement(model, "bigEng", ElementKind::PartUsage));
    CHECK_EQ(bigEngine.redefinitionKeyword(), std::string(":>>"));
    // "part redefines cyl[4];": named like the redefined feature
    const auto &cylinders = model.element(model.children(findElement(model, "SmallEngine", ElementKind::PartDefinition)).front());
    CHECK_EQ(cylinders.name(), std::string("cyl"));
    CHECK(cylinders.nameFromRedefinition());
    CHECK(cylinders.multiplicity() == std::optional<Multiplicity>(Multiplicity::exactly(4)));
    // ":>> mass = 2000.0;": an attribute without keyword
    const auto &mass = model.element(model.children(findElement(model, "vehicle_b", ElementKind::PartUsage)).front());
    CHECK(mass.kind() == ElementKind::Attribute);
    CHECK_EQ(mass.name(), std::string("mass"));
    CHECK(mass.declaredKeyword() == std::optional<std::string>(std::string()));
    CHECK_EQ(mass.value(), std::string("2000.0"));
    const auto &clock = model.element(findElement(model, "localClock", ElementKind::PartUsage));
    CHECK(clock.nameFromRedefinition());
    CHECK_EQ(clock.value(), std::string("new Time::Clock()"));

    CHECK(model.element(findElement(model, "vehicleStates", ElementKind::StateUsage)).isParallel());
    const auto &perform = model.element(findElement(model, "senseTemperature", ElementKind::ActionPerformance));
    CHECK(perform.declaredKeyword() == std::optional<std::string>("do"));
    const ElementId maintenance = findElement(model, "maintenance", ElementKind::StateUsage);
    const auto &entry = model.element(model.children(maintenance).front());
    CHECK(entry.actionForm() == ActionForm::Assign);
    CHECK(entry.declaredKeyword() == std::optional<std::string>("entry"));
    int timeTriggers = 0;
    for (const auto &relationship: model.relationships())
        timeTriggers += relationship.Kind == RelationshipKind::Transition &&
                        (relationship.Trigger.rfind("at ", 0) == 0 || relationship.Trigger.rfind("after ", 0) == 0 ||
                         relationship.Trigger.rfind("when ", 0) == 0);
    CHECK_EQ(timeTriggers, 4);

    const ElementId seat = findElement(model, "PressureSeat", ElementKind::ConnectionDefinition);
    CHECK(seat != RootId);
    CHECK(model.element(model.children(seat).front()).prefixes() == (std::vector<std::string>{"end", "[1]"}));
    const ElementId fuelFlow = findElement(model, "FuelFlow", ElementKind::FlowDefinition);
    const auto &payload = model.element(model.children(fuelFlow).front());
    CHECK_EQ(payload.name(), std::string("payload"));
    CHECK(payload.prefixes() == std::vector<std::string>{"ref"});
    bool flow = false;
    for (const auto &relationship: model.relationships()) {
        if (relationship.Kind == RelationshipKind::Flow) {
            flow = true;
            CHECK_EQ(relationship.Definition, std::string("FuelFlow"));
            CHECK_EQ(relationship.TypeName, std::string("Fuel"));
        }
    }
    CHECK(flow);

    // a new name of its own: written with the name and the redefinition
    model.rename(cylinders.id(), "cylinders");
    CHECK(SysMLCodeGenerator().generate(model).find("part cylinders[4] redefines cyl;") != std::string::npos);
}

TEST_CASE("Importer: send, accept, assign, terminate, if, while, loop and for actions") {
    // "Introduction to the SysML v2 Language - Textual Notation": Control Structures, Terminate Actions, Assignment
    // Actions, Asynchronous Messaging, Opaque Actions
    const std::string text = R"(action def ChargeBattery {
    loop action charging {
        action monitor : MonitorBattery {
            out charge;
        }
        then if monitor.charge < 100 {
            action addCharge : AddCharge {
                in charge = monitor.charge;
            }
        }
    } until charging.monitor.charge >= 100;
    then action endCharging : EndCharging;
    then done;
}
action def MonitoredActivity {
    first start;
    then fork;
    then performCriticalActivity;
    then waitForTimeOut;
    action performCriticalActivity {
        perform monitorCriticalActivity;
        perform criticalActivity;
        then terminate;
    }
    then stop;
    action waitForTimeOut;
    then stop;
    action stop terminate;
}
action terminateProcessing {
    in processor : Processor;
    terminate processor.workflowProcess;
    terminate processor;
}
action def ComputeMotion {
    in attribute powerProfile[*] :> ISQ::power;
    out attribute positions[*] :> ISQ::length := ( );
    private attribute position := initialPosition;
    for vehiclePower in powerProfile {
        perform action dynamics : StraightLineDynamics {
            in power = vehiclePower;
            out x_out;
        }
        then assign position := dynamics.x_out;
        then assign positions := positions->including(position);
    }
}
part camera : Camera {
    port viewPort;
    port displayPort;
    action takePicture : TakePicture {
        action trigger accept scene : Scene via viewPort;
        then action focus : Focus {
            in scene = trigger.scene;
            out image;
        }
        flow from focus.image to shoot.image;
        then action shoot : Shoot {
            in image;
            out picture;
        }
        then send new Show(shoot.picture) via displayPort;
    }
}
action def Decide {
    in x : Real;
    if x > 1 {
        action big;
    } else if x < 0 {
        action negative;
    } else action other {
        action rest;
    }
    while x < 10 action counting {
        assign x := x + 1;
    } until x == 5;
}
action def UpdateSensors {
    in sensors : Sensor[*];
    language "Alf"
        /*
         * for (sensor in sensors) {
         *      if (sensor.ready) {
         *             Update(sensor);
         *      }
         * }
         */
}
)";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);

    const ElementId charging = findElement(model, "charging", ElementKind::ActionUsage);
    CHECK(model.element(charging).actionForm() == ActionForm::Loop);
    CHECK(model.element(charging).namedInBody());
    CHECK_EQ(model.element(charging).untilCondition(), std::string("charging.monitor.charge >= 100"));
    const ElementId condition = model.children(charging).back();
    CHECK(model.element(condition).actionForm() == ActionForm::If);
    CHECK(model.element(condition).name().empty());
    CHECK_EQ(model.element(condition).actionClause(), std::string("monitor.charge < 100"));
    bool thenIf = false;
    for (const auto &relationship: model.relationships())
        thenIf = thenIf || (relationship.Kind == RelationshipKind::Succession && relationship.Target == condition);
    CHECK(thenIf); // "then if ...": a succession from monitor to the if action

    const ElementId monitored = findElement(model, "MonitoredActivity", ElementKind::ActionDefinition);
    int forks = 0;
    for (const ElementId child: model.children(monitored))
        forks += model.element(child).kind() == ElementKind::ControlNode && model.element(child).name().empty();
    CHECK_EQ(forks, 1); // "then fork;"
    const auto &stop = model.element(findElement(model, "stop", ElementKind::ActionUsage));
    CHECK(stop.actionForm() == ActionForm::Terminate);
    CHECK(stop.actionClause().empty());
    const auto &trigger = model.element(findElement(model, "trigger", ElementKind::ActionUsage));
    CHECK(trigger.actionForm() == ActionForm::Accept);
    CHECK_EQ(trigger.actionClause(), std::string("scene : Scene via viewPort"));
    const auto &dynamics = model.element(findElement(model, "dynamics", ElementKind::ActionPerformance));
    CHECK_EQ(dynamics.ownedElements().size(), std::size_t(2)); // in power, out x_out

    // if / else if / else
    const ElementId decide = findElement(model, "Decide", ElementKind::ActionDefinition);
    const ElementId first = model.children(decide)[1];
    CHECK(model.element(first).actionForm() == ActionForm::If);
    const ElementId elseIf = model.children(first).back();
    CHECK(model.element(elseIf).isElseBranch());
    CHECK(model.element(elseIf).actionForm() == ActionForm::If);
    const ElementId last = model.children(elseIf).back();
    CHECK(model.element(last).isElseBranch());
    CHECK_EQ(model.element(last).name(), std::string("other"));
    const auto &counting = model.element(findElement(model, "counting", ElementKind::ActionUsage));
    CHECK(counting.actionForm() == ActionForm::While);
    CHECK_EQ(counting.actionClause(), std::string("x < 10"));
    CHECK_EQ(counting.untilCondition(), std::string("x == 5"));

    // an else branch cannot be connected, an unnamed action only follows the one before it
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Succession, model.children(decide)[0], last));
    CHECK_THROWS_AS(model.setActionForm(last, ActionForm::Send, "s to t"), ModelError);
    CHECK_THROWS_AS(model.setActionForm(first, ActionForm::If, "x; y"), ModelError);
    // the form edited: written back accordingly
    model.setActionForm(first, ActionForm::While, "x > 1");
    CHECK(!model.element(elseIf).isElseBranch()); // no else for a while action
    const std::string edited = SysMLCodeGenerator().generate(model);
    CHECK(edited.find("    while x > 1 {\n") != std::string::npos);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(edited, again).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(again), edited);
}

TEST_CASE("Importer: metadata, imports, aliases, filters, concerns, viewpoints and views") {
    // "Introduction to the SysML v2 Language - Textual Notation": Packages, Metadata, Element Import Filtering,
    // Stakeholders and Concerns, Viewpoints, Views
    const std::string text = R"(package 'Package Example' {
    public import ISQ::TorqueValue;
    private import ScalarValues::*;
    private part def Automobile;
    public alias Car for Automobile;
    alias Torque for ISQ::TorqueValue;
}
part vehicle {
    part interior {
        part alarm;
        part seatBelt[2] {
            @Safety {
                isMandatory = true;
            }
        }
    }
}
metadata def SafetyFeature;
metadata def SecurityFeature {
    :> annotatedElement : SysML::PartDefinition;
    :> annotatedElement : SysML::PartUsage;
}
metadata SafetyFeature about vehicle::interior::seatBelt, vehicle::interior::alarm;
action computeDynamics {
    private import AnalysisTooling::*;
    metadata ToolExecution {
        toolName = "ModelCenter";
        uri = "aserv://localhost/Vehicle/Equation1";
    }
    in dt : ISQ::TimeValue {
        @ToolVariable {
            name = "deltaT";
        }
    }
}
package 'Safety Features' {
    public import vehicle::**;
    filter @Safety;
}
package 'Mandatory Safety Features' {
    public import vehicle::**[@Safety and (as Safety).isMandatory];
}
concern 'system breakdown' {
    doc /* To ensure ... */
    subject system;
    stakeholder se : 'systems engineer';
}
viewpoint 'system structure perspective' {
    require constraint {
        doc /* A system structure view shall show ... */
    }
    frame 'system breakdown';
}
view def 'Part Structure View' {
    filter @SysML::PartUsage;
    satisfy 'system structure perspective';
}
view 'vehicle structure view' : 'Part Structure View' {
    expose vehicle::**;
    render asTreeDiagram;
}
)";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);

    const ElementId package = model.children(RootId).front();
    const auto &publicImport = model.element(model.children(package)[0]);
    CHECK(publicImport.kind() == ElementKind::Import);
    CHECK_EQ(publicImport.target(), std::string("ISQ::TorqueValue"));
    CHECK(publicImport.prefixes() == std::vector<std::string>{"public"});
    const auto &car = model.element(model.children(package)[3]);
    CHECK(car.kind() == ElementKind::Alias);
    CHECK_EQ(car.name(), std::string("Car"));
    CHECK_EQ(car.target(), std::string("Automobile"));

    // "@Safety { isMandatory = true; }" in seatBelt
    const ElementId seatBelt = findElement(model, "seatBelt", ElementKind::PartUsage);
    const auto &safety = model.element(model.children(seatBelt).front());
    CHECK(safety.kind() == ElementKind::MetadataUsage);
    CHECK(safety.declaredKeyword() == std::optional<std::string>("@"));
    CHECK_EQ(safety.typeName(), std::string("Safety"));
    const auto &mandatory = model.element(safety.ownedElements().front());
    CHECK_EQ(mandatory.name(), std::string("isMandatory"));
    CHECK_EQ(mandatory.value(), std::string("true"));
    // "metadata SafetyFeature about a, b;"
    bool about = false;
    for (const ElementId id: model.children(RootId)) {
        const auto &element = model.element(id);
        if (element.kind() == ElementKind::MetadataUsage) {
            about = true;
            CHECK_EQ(element.about().size(), std::size_t(2));
        }
    }
    CHECK(about);
    CHECK(findElement(model, "SecurityFeature", ElementKind::MetadataDefinition) != RootId);
    // "in dt : ISQ::TimeValue { @ToolVariable { ... } }": metadata of a parameter
    const ElementId deltaT = findElement(model, "dt", ElementKind::Attribute);
    CHECK(model.element(model.children(deltaT).front()).kind() == ElementKind::MetadataUsage);

    const ElementId safetyFeatures = findElement(model, "Safety Features", ElementKind::Package);
    const auto &filter = model.element(model.children(safetyFeatures)[1]);
    CHECK(filter.kind() == ElementKind::Filter);
    CHECK_EQ(filter.target(), std::string("@Safety"));

    const ElementId concern = findElement(model, "system breakdown", ElementKind::RequirementUsage);
    CHECK(model.element(concern).declaredKeyword() == std::optional<std::string>("concern"));
    const ElementId viewpoint = findElement(model, "system structure perspective", ElementKind::RequirementUsage);
    const ElementId viewDefinition = findElement(model, "Part Structure View", ElementKind::ViewDefinition);
    const ElementId view = findElement(model, "vehicle structure view", ElementKind::ViewUsage);
    CHECK(viewDefinition != RootId);
    CHECK_EQ(model.element(view).typeName(), std::string("'Part Structure View'"));
    std::map<RelationshipKind, int> kinds;
    for (const auto &relationship: model.relationships()) {
        ++kinds[relationship.Kind];
        if (relationship.Kind == RelationshipKind::Frame) {
            CHECK_EQ(relationship.Source, viewpoint);
            CHECK_EQ(relationship.Target, concern);
        }
        if (relationship.Kind == RelationshipKind::Satisfy) {
            CHECK_EQ(relationship.Source, viewDefinition);
            CHECK(relationship.Context == std::optional<ElementId>(viewDefinition)); // "satisfy vp;" in the view def
        }
    }
    CHECK_EQ(kinds[RelationshipKind::Frame], 1);
    CHECK_EQ(kinds[RelationshipKind::Satisfy], 1);
    CHECK(model.element(model.children(view)[0]).kind() == ElementKind::Expose);
    CHECK(model.element(model.children(view)[1]).kind() == ElementKind::Render);

    // rules: an import has no name, expose only in a view, a frame from a requirement to a concern
    CHECK_THROWS_AS(model.addElement(ElementKind::Import, "x", package), ModelError);
    CHECK_THROWS_AS(model.addElement(ElementKind::Expose, "", package), ModelError);
    CHECK_THROWS_AS(model.setTarget(model.children(package)[0], "a; b"), ModelError);
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Frame, view, concern));
}

TEST_CASE("Importer: snapshots, time slices, individuals, variations and analysis cases") {
    // "Introduction to the SysML v2 Language - Textual Notation": Occurrences, Individuals, Variations, Analysis Cases
    const std::string text =
            "part def Vehicle {\n"
            "    timeslice assembly;\n"
            "    then snapshot delivery {\n"
            "        attribute deliveryDate : Date;\n"
            "    }\n"
            "    then timeslice ownership[*] ordered {\n"
            "        snapshot sale = start;\n"
            "        ref item owner : Person[1];\n"
            "    }\n"
            "    snapshot junked = done;\n"
            "}\n"
            "individual part def Vehicle_1 :> Vehicle {\n"
            "    snapshot part vehicle_1_t0 : Vehicle_1;\n"
            "    then snapshot part vehicle_1_t1 : Vehicle_1;\n"
            "}\n"
            "variation attribute def DiameterChoices :> Diameter {\n"
            "    variant attribute diameterSmall = 70[mm];\n"
            "    variant attribute diameterLarge = 100[mm];\n"
            "}\n"
            "variation part def EngineChoices :> Engine {\n"
            "    variant '4cylEngine';\n"
            "    variant '6cylEngine';\n"
            "}\n"
            "abstract part vehicleFamily : Vehicle {\n"
            "    part engine : EngineChoices[1];\n"
            "    variation part transmission : Transmission[1] {\n"
            "        variant manualTransmission;\n"
            "        variant automaticTransmission;\n"
            "    }\n"
            "    assert constraint {\n"
            "        engine == engine::'4cylEngine' xor transmission == transmission::automaticTransmission\n"
            "    }\n"
            "}\n"
            "analysis def FuelEconomyAnalysis {\n"
            "    subject vehicle : Vehicle;\n"
            "    objective fuelEconomyAnalysisObjective {\n"
            "        require constraint {\n"
            "            fuelEconomyResult > 30[mi / gallon]\n"
            "        }\n"
            "    }\n"
            "    in attribute scenario : WayPoint[*];\n"
            "    return fuelEconomyResult : DistancePerVolumeValue = solveForFuelEconomy.fuelEconomy;\n"
            "}\n"
            "part context {\n"
            "    analysis cityAnalysis : FuelEconomyAnalysis {\n"
            "        subject vehicle = vehicle_c1;\n"
            "        in scenario = cityScenario;\n"
            "    }\n"
            "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    // nothing kept as text
    std::function<int(ElementId)> textual = [&](ElementId owner) {
        int count = 0;
        for (const ElementId child: model.children(owner))
            count += (model.element(child).kind() == ElementKind::Textual ? 1 : 0) + textual(child);
        return count;
    };
    CHECK_EQ(textual(RootId), 0);
    const auto vehicle = find(model, RootId, "Vehicle");
    const auto ownership = find(model, *vehicle, "ownership");
    CHECK(model.element(*ownership).kind() == ElementKind::OccurrenceUsage);
    CHECK(model.element(*ownership).declaredKeyword() == std::optional<std::string>("timeslice"));
    CHECK_EQ(model.element(*ownership).multiplicityQualifiers(), std::string("ordered"));
    int successions = 0;
    for (const auto &relationship: model.relationships())
        successions += relationship.Kind == RelationshipKind::Succession ? 1 : 0;
    CHECK_EQ(successions, 3); // assembly -> delivery -> ownership, t0 -> t1
    const auto choices = find(model, RootId, "EngineChoices");
    const auto variant = find(model, *choices, "4cylEngine");
    CHECK(model.element(*variant).kind() == ElementKind::PartUsage);
    CHECK(model.element(*variant).prefixes() == std::vector<std::string>{"variant"});
    const auto analysis = find(model, RootId, "FuelEconomyAnalysis");
    CHECK(model.element(*analysis).kind() == ElementKind::AnalysisDefinition);
    const auto result = find(model, *analysis, "fuelEconomyResult");
    CHECK(model.element(*result).prefixes() == std::vector<std::string>{"return"});
}

TEST_CASE("Importer: parameters with a short name and without a keyword") {
    const std::string text = "calc def Total {\n"
                             "    in <'P1'> a : Real;\n"
                             "    return <R1> result[1..*];\n"
                             "    a * 2\n"
                             "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    const ElementId total = model.children(RootId).front();
    CHECK_EQ(model.children(total).size(), std::size_t(2));
    for (const ElementId parameter: model.children(total)) {
        CHECK(model.element(parameter).kind() == ElementKind::Attribute);
        CHECK(!model.element(parameter).shortName().empty());
    }
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
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
    // an unnamed asserted constraint: a constraint of its own (name empty)
    const auto &anonymous = model.element(model.children(*engine).at(2));
    CHECK(anonymous.kind() == ElementKind::ConstraintUsage);
    CHECK(anonymous.name().empty());
    CHECK_EQ(anonymous.expression(), std::string("torque > 0"));
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

TEST_CASE("Importer: attribute, enum, item, port and interface definitions, ports and interfaces") {
    const std::string text =
            "package Fuel {\n"
            "    attribute def Temperature :> ISQ::TemperatureValue;\n"
            "    attribute def Pressure {\n"
            "        attribute value : Real;\n"
            "    }\n"
            "    enum def PumpState {\n"
            "        enum off;\n"
            "        enum running;\n"
            "        fault;\n"
            "    }\n"
            "    item def Fuel {\n"
            "        attribute temperature : Temperature;\n"
            "    }\n"
            "    item def Diesel :> Fuel;\n"
            "    port def FuelPort {\n"
            "        out item fuel : Fuel;\n"
            "        in attribute pressure : Pressure;\n"
            "    }\n"
            "    interface def FuelInterface {\n"
            "        end supplier : FuelPort;\n"
            "        end port consumer : ~FuelPort;\n"
            "    }\n"
            "}\n"
            "part tank {\n"
            "    port fuelOut : Fuel::FuelPort;\n"
            "    item stored : Fuel::Diesel[0..1];\n"
            "}\n"
            "part engine {\n"
            "    port fuelIn : ~Fuel::FuelPort {\n"
            "        port drain;\n"
            "    }\n"
            "}\n"
            "part pump;\n"
            "connect tank.fuelOut to engine.fuelIn;\n"
            "connection pipe : Hose connect pump to tank;\n"
            "interface : Fuel::FuelInterface connect tank.fuelOut to engine.fuelIn;\n"
            "interface line connect engine.fuelIn.drain to tank.fuelOut;\n"
            "flow of Fuel::Fuel from tank.fuelOut to engine.fuelIn;\n"
            "flow supply from tank to engine;\n";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    CHECK(report.lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);
    CHECK_EQ(report.ImportedRelationships, std::size_t(7)); // Diesel :> Fuel + 6 connections

    const auto package = find(model, RootId, "Fuel");
    CHECK(model.element(*find(model, *package, "Temperature")).kind() == ElementKind::AttributeDefinition);
    const auto state = find(model, *package, "PumpState");
    CHECK(model.element(*state).kind() == ElementKind::EnumerationDefinition);
    CHECK_EQ(model.children(*state).size(), std::size_t(3));
    CHECK(model.element(model.children(*state).at(2)).kind() == ElementKind::EnumerationLiteral);
    const auto interface = find(model, *package, "FuelInterface");
    CHECK(model.element(model.children(*interface).at(0)).kind() == ElementKind::PortUsage);
    const auto engine = find(model, RootId, "engine");
    const auto fuelIn = find(model, *engine, "fuelIn");
    CHECK_EQ(model.element(*fuelIn).typeName(), std::string("~Fuel::FuelPort"));
    int interfaces = 0;
    for (const auto &relationship: model.relationships())
        interfaces += relationship.Kind == RelationshipKind::Interface ? 1 : 0;
    CHECK_EQ(interfaces, 2);

    // an interface needs two ports, an enum value no value: kept as text
    SemanticModel other;
    CHECK(SysMLTextImporter::importText("part a;\npart b;\ninterface connect a to b;\n"
                                        "enum def E {\n    enum x = 1;\n}\n", other).lossless());
    CHECK_EQ(other.relationships().size(), std::size_t(0));
    CHECK_EQ(roundTrip(SysMLCodeGenerator().generate(other)), SysMLCodeGenerator().generate(other));
}

TEST_CASE("Importer: actions, control nodes, states, successions and transitions") {
    const std::string text =
            "action def ProvidePower {\n"
            "    in item fuel : Fuel;\n"
            "    in load : Real;\n"
            "    first start;\n"
            "    then check;\n"
            "    action check;\n"
            "    then decision;\n"
            "    decide decision;\n"
            "    if load > 0.8 then boost;\n"
            "    else normal;\n"
            "    action boost : Boost {\n"
            "        action prepare;\n"
            "    }\n"
            "    then joined;\n"
            "    action normal;\n"
            "    then joined;\n"
            "    merge joined;\n"
            "    then split;\n"
            "    fork split;\n"
            "    then left;\n"
            "    then right;\n"
            "    action left;\n"
            "    then sync;\n"
            "    action right;\n"
            "    then sync;\n"
            "    join sync;\n"
            "    then done;\n"
            "    succession last first check then normal;\n"
            "}\n"
            "state def EngineStates {\n"
            "    entry;\n"
            "    then off;\n"
            "    state off;\n"
            "    state on {\n"
            "        entry action warmUp;\n"
            "        do action run;\n"
            "        exit action coolDown;\n"
            "    }\n"
            "    transition switchOn first off accept StartSignal if fuelLevel > 0 do action ignite then on;\n"
            "    transition first on accept StopSignal then off;\n"
            "    transition first on if overheated then on;\n"
            "}\n"
            "part engine {\n"
            "    exhibit state states : EngineStates;\n"
            "    perform action providePower;\n"
            "}\n";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    if (!report.lossless())
        std::cout << report.details() << "\n";
    CHECK(report.lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(model), text);

    const auto provide = find(model, RootId, "ProvidePower");
    CHECK(model.element(*provide).kind() == ElementKind::ActionDefinition);
    const auto decision = find(model, *provide, "decision");
    CHECK(model.element(*decision).kind() == ElementKind::ControlNode);
    CHECK_EQ(*model.element(*decision).declaredKeyword(), std::string("decide"));
    int successions = 0;
    int transitions = 0;
    std::string guard;
    for (const auto &relationship: model.relationships()) {
        successions += relationship.Kind == RelationshipKind::Succession ? 1 : 0;
        transitions += relationship.Kind == RelationshipKind::Transition ? 1 : 0;
        if (relationship.Source == *decision && guard.empty())
            guard = relationship.Guard;
    }
    CHECK_EQ(successions, 14);
    CHECK_EQ(transitions, 3);
    CHECK_EQ(guard, std::string("load > 0.8"));
    const auto states = find(model, RootId, "EngineStates");
    const auto on = find(model, *states, "on");
    CHECK_EQ(*model.element(model.children(*on).at(1)).declaredKeyword(), std::string("do action"));

    // other spellings mean the same and are written in the form above
    SemanticModel other;
    CHECK(SysMLTextImporter::importText("action def A {\n    action a;\n    action b;\n    first a then b;\n"
                                        "    first start then a;\n}\n", other).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(other),
             std::string("action def A {\n    action a;\n    then b;\n    action b;\n    first start;\n    then a;\n}\n"));
    CHECK_EQ(roundTrip(SysMLCodeGenerator().generate(other)), SysMLCodeGenerator().generate(other));
}

TEST_CASE("Importer: requirements, use cases, verifications, satisfy, verify and include") {
    const std::string text =
            "package Requirements {\n"
            "    requirement def <'R1'> MassRequirement {\n"
            "        doc /* The mass of the vehicle shall be below the limit. */\n"
            "        subject vehicle : Vehicle;\n"
            "        stakeholder driver : Person;\n"
            "        attribute massLimit : MassValue;\n"
            "        require constraint {\n"
            "            vehicle.mass <= massLimit\n"
            "        }\n"
            "        assume constraint fuelled {\n"
            "            vehicle.fuel > 0\n"
            "        }\n"
            "    }\n"
            "    requirement <'1.1'> vehicleMass : MassRequirement {\n"
            "        requirement dryMass : MassRequirement;\n"
            "    }\n"
            "}\n"
            "part def Vehicle;\n"
            "part vehicle : Vehicle {\n"
            "    port fuelIn;\n"
            "}\n"
            "use case def Drive {\n"
            "    subject vehicle : Vehicle;\n"
            "    actor driver : Person;\n"
            "    actor passengers : Person[0..4];\n"
            "    objective {\n"
            "        doc /* The driver reaches the destination. */\n"
            "    }\n"
            "    include use case refuel : Refuel;\n"
            "    include park;\n"
            "}\n"
            "use case def Refuel;\n"
            "use case park : Park;\n"
            "use case drive : Drive;\n"
            "verification def MassTest {\n"
            "    subject vehicle : Vehicle;\n"
            "    objective {\n"
            "        verify Requirements::vehicleMass;\n"
            "    }\n"
            "}\n"
            "verification def MassTest2 :> MassTest {\n"
            "    objective {\n"
            "        verify Requirements::vehicleMass.dryMass;\n"
            "    }\n"
            "}\n"
            "satisfy Requirements::vehicleMass by vehicle;\n"
            "satisfy Requirements::vehicleMass.dryMass by vehicle.fuelIn;\n";
    SemanticModel model;
    const auto report = SysMLTextImporter::importText(text, model);
    if (!report.lossless())
        std::cout << report.details() << "\n";
    CHECK(report.lossless());
    const std::string generated = SysMLCodeGenerator().generate(model);
    if (generated != text)
        std::cout << generated;
    CHECK_EQ(generated, text);

    const auto requirements = find(model, RootId, "Requirements");
    const auto massRequirement = find(model, *requirements, "MassRequirement");
    CHECK(model.element(*massRequirement).kind() == ElementKind::RequirementDefinition);
    CHECK_EQ(model.element(*massRequirement).shortName(), std::string("'R1'"));
    const auto &parts = model.children(*massRequirement);
    CHECK(model.element(parts.at(0)).kind() == ElementKind::Textual); // doc
    CHECK(model.element(parts.at(1)).kind() == ElementKind::Subject);
    CHECK(model.element(parts.at(2)).kind() == ElementKind::Actor);
    CHECK_EQ(*model.element(parts.at(2)).declaredKeyword(), std::string("stakeholder"));
    CHECK(model.element(parts.at(4)).kind() == ElementKind::ConstraintUsage);
    CHECK(model.element(parts.at(4)).name().empty());
    CHECK_EQ(*model.element(parts.at(4)).declaredKeyword(), std::string("require constraint"));
    CHECK_EQ(model.element(parts.at(4)).expression(), std::string("vehicle.mass <= massLimit"));
    const auto drive = find(model, RootId, "Drive");
    CHECK(model.element(*drive).kind() == ElementKind::UseCaseDefinition);
    CHECK(model.element(model.children(*drive).at(3)).kind() == ElementKind::RequirementUsage);
    CHECK(model.element(model.children(*drive).at(3)).name().empty());
    CHECK_EQ(*model.element(model.children(*drive).at(4)).declaredKeyword(), std::string("include use case"));
    int satisfy = 0;
    int verify = 0;
    int include = 0;
    for (const auto &relationship: model.relationships()) {
        satisfy += relationship.Kind == RelationshipKind::Satisfy ? 1 : 0;
        verify += relationship.Kind == RelationshipKind::Verify ? 1 : 0;
        include += relationship.Kind == RelationshipKind::Include ? 1 : 0;
    }
    CHECK_EQ(satisfy, 2);
    CHECK_EQ(verify, 2);
    CHECK_EQ(include, 1);

    // a verify without objective gets one; what does not resolve stays text
    SemanticModel drawn;
    const ElementId requirement = drawn.addElement(ElementKind::RequirementUsage, "r");
    const ElementId test = drawn.addElement(ElementKind::VerificationDefinition, "Test");
    drawn.addRelationship(RelationshipKind::Verify, test, requirement);
    CHECK_EQ(SysMLCodeGenerator().generate(drawn),
             std::string("requirement r;\nverification def Test {\n    objective {\n        verify r;\n    }\n}\n"));
    CHECK_EQ(roundTrip(SysMLCodeGenerator().generate(drawn)), SysMLCodeGenerator().generate(drawn));
    const std::string unresolved = "satisfy missing by vehicle;\nuse case def U {\n    include nothing;\n}\n"
                                   "part p {\n    subject s;\n}\n";
    CHECK_EQ(roundTrip(unresolved), unresolved);
    // an unnamed requirement cannot be the target of satisfy
    SemanticModel unnamed;
    const ElementId anonymous = unnamed.addElement(ElementKind::RequirementUsage, "");
    const ElementId part = unnamed.addElement(ElementKind::PartUsage, "p");
    CHECK(!RelationshipRules::canRelate(unnamed, RelationshipKind::Satisfy, part, anonymous));
}

TEST_CASE("Importer: doc text of requirements") {
    CHECK_EQ(SysMLTextImporter::commentBody("doc /* The mass shall be low. */"), std::string("The mass shall be low."));
    CHECK_EQ(SysMLTextImporter::commentBody("doc /*\n * first\n *\n * second\n */"), std::string("first\n\nsecond"));
    CHECK_EQ(SysMLTextImporter::docStatement("one line"), std::string("doc /* one line */"));
    CHECK_EQ(SysMLTextImporter::docStatement("a\n\nb"), std::string("doc /*\n * a\n *\n * b\n */"));
    CHECK_EQ(SysMLTextImporter::commentBody(SysMLTextImporter::docStatement("a */ b")), std::string("a * / b"));
    // written into a requirement and read again
    SemanticModel model;
    const ElementId requirement = model.addElement(ElementKind::RequirementDefinition, "R");
    const ElementId doc = model.addElement(ElementKind::Textual, "", requirement);
    model.setText(doc, SysMLTextImporter::docStatement("first\nsecond"), "doc", "");
    const std::string text = SysMLCodeGenerator().generate(model);
    CHECK_EQ(text, std::string("requirement def R {\n    doc /*\n     * first\n     * second\n     */\n}\n"));
    CHECK_EQ(roundTrip(text), text);
    SemanticModel again;
    SysMLTextImporter::importText(text, again);
    const auto found = SysMLTextImporter::docOf(again, again.children(RootId).front());
    CHECK(found.has_value());
    CHECK_EQ(SysMLTextImporter::commentBody(again.element(*found).text()), std::string("first\nsecond"));
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
