//
// Created by Moritz Herzog on 25.04.24.
//

#include <QGuiApplication>
#include <QIcon>
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
    QCoreApplication::setOrganizationName("Protestator-Research");
    QCoreApplication::setOrganizationDomain("https://www.protestator-research.com");

    QGuiApplication::setWindowIcon(QIcon(":/icons/sience/icons/4847335-science-and-technology/png/030-chip.png"));

    QQuickStyle::setStyle("Material");

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    QObject::connect(&engine, &QQmlApplicationEngine::quit,
                     &app, &QGuiApplication::quit);
    engine.loadFromModule("StructuraSystems", "Main");

    return app.exec();
}
