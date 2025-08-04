//
// Created by Moritz Herzog on 25.04.24.
//

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QResource>

#define SCHEME "http"
#define HOST "127.0.0.1"
#define PORT 8080

int main(int argc, char *argv[]) {
    QCoreApplication app(argc,argv);

    QCoreApplication::setApplicationName("Structura Systems Server");
    QCoreApplication::setApplicationVersion("1.0");
    QCoreApplication::setOrganizationName("Working Group Cyber Physical Systems");
    QCoreApplication::setOrganizationDomain("https://cps.cs.rptu.de/");

    QCommandLineParser parser;
    parser.addOptions({ {"port", QCoreApplication::translate("main", "The port the server listens on."), "port"} });
    parser.addHelpOption();
    parser.process(app);

    quint16 portArg = PORT;
    if (!parser.value("port").isEmpty())
        portArg = parser.value("port").toUShort();



    return app.exec();
}