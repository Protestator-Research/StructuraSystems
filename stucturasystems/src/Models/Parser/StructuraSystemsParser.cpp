//
// Created by Moritz Herzog on 22.04.25.
//

#include "StructuraSystemsParser.h"
#include "Markdown/MarkdownParser.h"
#include <QString>
#include <md4c.h>
#include <stdexcept>
#include <QFile>

namespace StructuraSystems::Client {
    StructuraSystemsParser::StructuraSystemsParser() {

    }

    StructuraSystemsParser::~StructuraSystemsParser() {

    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readFile(QString filePath) {
        if(filePath.contains(".md"))
            return readMarkdownFile(filePath);

        if(filePath.contains(".kerml"))
            return readKerMLFile(filePath);

        if(filePath.contains(".xml"))
            return readXmlFile(filePath);

        if(filePath.contains(".json"))
            return readJsonFile(filePath);

        if(filePath.contains(".sysml") || filePath.contains(".sml"))
            return readSysMLv2File(filePath);

        throw std::runtime_error("No Valid Filepath.");
    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readMarkdownFile(QString filePath) {
        MarkdownParser parser;
        parser.parseMarkdownFile(filePath);
        return parser.getElementsOfProject();
    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readXmlFile(QString ) {

        return std::vector<std::shared_ptr<SysMLv2::Entities::Element>>();
    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readSysMLv2File(QString filePath) {
        QFile file = QFile(filePath);
        file.open(QIODevice::ReadOnly);
        QByteArray array = file.readAll();
        file.close();
        std::string body = QString(array.data()).toStdString();

        std::shared_ptr<SysMLv2::Entities::Element> element = std::make_shared<SysMLv2::Entities::Element>();
        element->setLanguage("SysMLv2");
        element->setBody(body);
        return {element};
    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readKerMLFile(QString filePath) {
        QFile file = QFile(filePath);
        file.open(QIODevice::ReadOnly);
        QByteArray array = file.readAll();
        file.close();
        std::string body = QString(array.data()).toStdString();

        std::shared_ptr<SysMLv2::Entities::Element> element = std::make_shared<SysMLv2::Entities::Element>();
        element->setLanguage("KerML");
        element->setBody(body);
        return {element};
    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readJsonFile(QString ) {
        return std::vector<std::shared_ptr<SysMLv2::Entities::Element>>();
    }

} // StructuraSystems::Client