//
// Created by Moritz Herzog on 25.04.24.
//

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QCoreApplication>
#include <QUrl>

int main(int argc, char *argv[]) {
    // Must be set before the application object exists, otherwise Qt Quick Controls ignores it.
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_VARIANT"))
        qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");

    QGuiApplication app(argc, argv);

    QCoreApplication::setApplicationName("Structura Systems");
    QCoreApplication::setApplicationVersion("1.0");
    QCoreApplication::setOrganizationName("Working Group Cyber Physical Systems");
    QCoreApplication::setOrganizationDomain("https://cps.cs.rptu.de/");

    QQuickStyle::setStyle("Material");

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("StructuraSystems", "Main");

    return app.exec();
}
