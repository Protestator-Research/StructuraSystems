//
// Created by Moritz Herzog on 25.04.24.
//

#include <QApplication>
#include "Widgets/StructuraMainWindow.h"
#include <QResource>

int main(int argc, char *argv[]) {
    QApplication app(argc,argv);

    QCoreApplication::setApplicationName("Structura Systems");
    QCoreApplication::setApplicationVersion("1.0");
    QCoreApplication::setOrganizationName("Working Group Cyber Physical Systems");
    QCoreApplication::setOrganizationDomain("https://cps.cs.rptu.de/");

    QResource::registerResource("../resources/Resources.qrc");

    StructuraSystems::Client::StructuraMainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}