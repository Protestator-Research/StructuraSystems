//
// SemanticModel -> SysML text.
//

#include "../TestFramework.h"

#include <Diagram/Conversion/SysMLCodeGenerator.h>

using namespace StructuraSystems::Diagram;

namespace {
    /// The "Decomposition View of vehicle" example: vehicle with transmission and engine,
    /// engine with six cylinders. Composition = ownership.
    SemanticModel buildVehicle() {
        SemanticModel model;
        const auto vehicle = model.addElement(ElementKind::PartUsage, "vehicle");
        model.addElement(ElementKind::Attribute, "mass", vehicle);
        model.addElement(ElementKind::ActionPerformance, "providePower", vehicle);

        const auto transmission = model.addElement(ElementKind::PartUsage, "transmission", vehicle);
        model.addElement(ElementKind::Attribute, "mass", transmission);
        model.addElement(ElementKind::ActionPerformance, "amplifyTorque", transmission);

        const auto engine = model.addElement(ElementKind::PartUsage, "engine", vehicle);
        model.addElement(ElementKind::Attribute, "mass", engine);
        model.addElement(ElementKind::ActionPerformance, "generateTorque", engine);

        const auto cylinders = model.addElement(ElementKind::PartUsage, "cylinders", engine);
        model.setMultiplicity(cylinders, Multiplicity::exactly(6));
        return model;
    }
}

TEST_CASE("Generator: vehicle decomposition example") {
    const SemanticModel model = buildVehicle();
    const std::string expected =
            "part vehicle {\n"
            "    attribute mass;\n"
            "    perform action providePower;\n"
            "    part transmission {\n"
            "        attribute mass;\n"
            "        perform action amplifyTorque;\n"
            "    }\n"
            "    part engine {\n"
            "        attribute mass;\n"
            "        perform action generateTorque;\n"
            "        part cylinders[6];\n"
            "    }\n"
            "}\n";
    CHECK_EQ(SysMLCodeGenerator().generate(model), expected);
}

TEST_CASE("Generator: empty model gives empty text") {
    CHECK_EQ(SysMLCodeGenerator().generate(SemanticModel()), std::string());
}

TEST_CASE("Generator: connection and flow are placed in the common owner") {
    SemanticModel model = buildVehicle();
    const auto vehicle = model.children(RootId).at(0);
    const auto transmission = model.children(vehicle).at(2);
    const auto engine = model.children(vehicle).at(3);
    const auto cylinders = model.children(engine).at(2);

    model.addRelationship(RelationshipKind::Connection, engine, transmission);
    model.addRelationship(RelationshipKind::Flow, cylinders, transmission);

    const std::string text = SysMLCodeGenerator().generate(model);
    const std::string expectedTail =
            "        part cylinders[6];\n"
            "    }\n"
            "    connect engine to transmission;\n"
            "    flow from engine.cylinders to transmission;\n"
            "}\n";
    CHECK(text.size() >= expectedTail.size());
    CHECK_EQ(text.substr(text.size() - expectedTail.size()), expectedTail);
}

TEST_CASE("Generator: top-level connection between two parts") {
    SemanticModel model;
    const auto a = model.addElement(ElementKind::PartUsage, "a");
    const auto b = model.addElement(ElementKind::PartUsage, "b");
    model.addRelationship(RelationshipKind::Connection, a, b);
    CHECK_EQ(SysMLCodeGenerator().generate(model), std::string("part a;\npart b;\nconnect a to b;\n"));
}

TEST_CASE("Generator: specialization, typing and packages") {
    SemanticModel model;
    const auto package = model.addElement(ElementKind::Package, "Vehicles");
    const auto car = model.addElement(ElementKind::PartDefinition, "Car", package);
    const auto sportsCar = model.addElement(ElementKind::PartDefinition, "SportsCar", package);
    model.addRelationship(RelationshipKind::Specialization, sportsCar, car);

    const auto myCar = model.addElement(ElementKind::PartUsage, "myCar");
    model.setTypeName(myCar, "Vehicles::SportsCar");
    const auto mass = model.addElement(ElementKind::Attribute, "mass", myCar);
    model.setTypeName(mass, "ISQ::MassValue");
    model.addElement(ElementKind::Package, "Empty");

    const std::string expected =
            "package Vehicles {\n"
            "    part def Car;\n"
            "    part def SportsCar :> Car;\n"
            "}\n"
            "part myCar : Vehicles::SportsCar {\n"
            "    attribute mass : ISQ::MassValue;\n"
            "}\n"
            "package Empty;\n";
    CHECK_EQ(SysMLCodeGenerator().generate(model), expected);
}

TEST_CASE("Generator: specialization across owners uses the qualified name") {
    SemanticModel model;
    const auto base = model.addElement(ElementKind::Package, "Base");
    const auto vehicle = model.addElement(ElementKind::PartDefinition, "Vehicle", base);
    const auto car = model.addElement(ElementKind::PartDefinition, "Car");
    model.addRelationship(RelationshipKind::Specialization, car, vehicle);
    CHECK_EQ(SysMLCodeGenerator().generate(model),
             std::string("package Base {\n    part def Vehicle;\n}\npart def Car :> Base::Vehicle;\n"));
}

TEST_CASE("Generator: perform style reference") {
    SemanticModel model;
    const auto vehicle = model.addElement(ElementKind::PartUsage, "vehicle");
    const auto action = model.addElement(ElementKind::ActionPerformance, "providePower", vehicle);
    model.setPerformStyle(action, PerformStyle::ReferenceAction);
    CHECK_EQ(SysMLCodeGenerator().generate(model), std::string("part vehicle {\n    perform providePower;\n}\n"));
}

TEST_CASE("Generator: multiplicities") {
    CHECK_EQ(Multiplicity::exactly(6).toString(), std::string("[6]"));
    CHECK_EQ(Multiplicity::range(0, 1).toString(), std::string("[0..1]"));
    CHECK_EQ(Multiplicity::range(1, std::nullopt).toString(), std::string("[1..*]"));
    CHECK_EQ(Multiplicity::range(0, std::nullopt).toString(), std::string("[*]"));
    CHECK_THROWS_AS(Multiplicity::range(3, 2), std::invalid_argument);
}

TEST_CASE("Generator: names that are not plain identifiers are quoted") {
    CHECK_EQ(SysMLCodeGenerator::formatName("engine"), std::string("engine"));
    CHECK_EQ(SysMLCodeGenerator::formatName("engine_2"), std::string("engine_2"));
    CHECK_EQ(SysMLCodeGenerator::formatName("front axle"), std::string("'front axle'"));
    CHECK_EQ(SysMLCodeGenerator::formatName("2ndGear"), std::string("'2ndGear'"));
    CHECK_EQ(SysMLCodeGenerator::formatName("part"), std::string("'part'"));
    CHECK_EQ(SysMLCodeGenerator::formatName("it's"), std::string("'it\\'s'"));
}

TEST_CASE("Generator: custom indentation") {
    SemanticModel model;
    const auto vehicle = model.addElement(ElementKind::PartUsage, "vehicle");
    model.addElement(ElementKind::Attribute, "mass", vehicle);
    SysMLCodeGenerator::Options options;
    options.Indent = "\t";
    CHECK_EQ(SysMLCodeGenerator(options).generate(model), std::string("part vehicle {\n\tattribute mass;\n}\n"));
}
