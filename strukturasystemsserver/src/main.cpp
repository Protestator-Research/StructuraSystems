//
// Created by Moritz Herzog on 25.04.24.
//

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QResource>
#include <QTcpServer>
#include <QHttpServer>

#include "Controller/AuthenticationController.hpp"
#include "Controller/ProjectController.hpp"
#include "Controller/CommitController.hpp"
#include "Controller/DataBaseController.h"
#include "Controller/ElementsController.hpp"
#define SCHEME "http"
#define HOST "127.0.0.1"
#define PORT 8088

int main(int argc, char *argv[]) {
    QCoreApplication app(argc,argv);

    QCoreApplication::setApplicationName("Structura Systems Server");
    QCoreApplication::setApplicationVersion("1.0");
    QCoreApplication::setOrganizationName("Working Group Cyber Physical Systems");
    QCoreApplication::setOrganizationDomain("https://cps.cs.rptu.de/");

    QCommandLineParser parser;
    parser.addOption( {"port", QCoreApplication::translate("main", "The port the server listens on."), "port"} );
    parser.addOption({ "dbAddress", QCoreApplication::translate("main","The mongodb instance the server connects to."),"dbAddress" });
	parser.addOption({ "dbUsername", QCoreApplication::translate("main","The mongodb username the server connects with."),"dbUsername" });
    parser.addOption({ "dbPassword", QCoreApplication::translate("main","The mongodb password the server connects with."),"dbPassword" });
	parser.addHelpOption();
    parser.process(app);

    std::string dbAddressArg = "localhost:27017";
    if (!parser.value("dbAddress").isEmpty())
        dbAddressArg = parser.value("dbAddress").toUShort();

    std::string dbUsernameArg = "";
    if (!parser.value("dbUsername").isEmpty())
        dbUsernameArg = parser.value("dbUsername").toUShort();

    std::string dbPasswordArg = "";
    if (!parser.value("dbPassword").isEmpty())
        dbPasswordArg = parser.value("dbPassword").toUShort();

    [[maybe_unused]] auto dbController = StructuraSystems::Server::DataBaseController::createInstance(dbAddressArg, dbUsernameArg, dbPasswordArg);

    quint16 portArg = PORT;
    if (!parser.value("port").isEmpty())
        portArg = parser.value("port").toUShort();

    QHttpServer* httpServer = new QHttpServer();

    const auto versionInformation = []() {
        qDebug() << "Call to /version";
        return "3.0.alpha";
    };
    httpServer->route("/version", versionInformation);
    httpServer->route("/version/", versionInformation);

    httpServer->addAfterRequestHandler(httpServer, [](const QHttpServerRequest&, QHttpServerResponse& resp) {
        auto h = resp.headers();
        h.append(QHttpHeaders::WellKnownHeader::Server, "Structura Systems Server");
        resp.setHeaders(std::move(h));
    });

    [[maybe_unused]] auto projectController = new StructuraSystems::Server::ProjectController(httpServer);
    [[maybe_unused]] auto commitController = new StructuraSystems::Server::CommitController(httpServer);
    [[maybe_unused]] auto elementsController = new StructuraSystems::Server::ElementsController(httpServer);
    [[maybe_unused]] auto authController = new StructuraSystems::Server::AuthenticationController(httpServer);

    auto tcpserver = std::make_unique<QTcpServer>();
    if (!tcpserver->listen(QHostAddress::Any, portArg)) {
        qDebug() << QCoreApplication::translate("StructuraSystemsServer",
            "Server failed to listen on a port.");
        return 0;
    }

    httpServer->bind(tcpserver.get());
    quint16 port = tcpserver->serverPort();
    tcpserver.release();

    qDebug() << QCoreApplication::translate(
        "StructuraSystemsServer",
        "Running on http://127.0.0.1:%1/ (Press CTRL+C to quit)")
        .arg(port);


    return app.exec();
}