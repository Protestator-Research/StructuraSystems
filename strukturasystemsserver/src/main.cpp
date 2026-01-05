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
    parser.addOptions({ {"port", QCoreApplication::translate("main", "The port the server listens on."), "port"} });
    parser.addHelpOption();
    parser.process(app);

    [[maybe_unused]] auto dbController = StructuraSystems::Server::DataBaseController::createInstance("localhost:27017", "root", "password");

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