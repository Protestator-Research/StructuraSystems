//
// Created by Moritz Herzog on 22.04.25.
//

#include "StructuraSystemsParser.h"
#include "Markdown/MarkdownParser.h"
#include <QString>
#include <md4c.h>
#include <stdexcept>
#include <QFile>
#include <QTextStream>

namespace StructuraSystems::Client {
    StructuraSystemsParser::StructuraSystemsParser() {

    }

    StructuraSystemsParser::~StructuraSystemsParser() {

    }

    std::vector<std::shared_ptr<SysMLv2::REST::Element>> StructuraSystemsParser::readFile(QString filePath) {
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

    std::vector<std::shared_ptr<SysMLv2::REST::Element>> StructuraSystemsParser::readMarkdownFile(QString filePath) {
        MarkdownParser parser;
        parser.parseMarkdownFile(filePath);
        return parser.getElementsOfProject();
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Element>> StructuraSystemsParser::readXmlFile(QString ) {

        return std::vector<std::shared_ptr<SysMLv2::REST::Element>>();
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Element>> StructuraSystemsParser::readSysMLv2File(QString filePath) {
        QFile file = QFile(filePath);
        file.open(QIODevice::ReadOnly);
        QByteArray array = file.readAll();
        file.close();
        std::string body = QString(array.data()).toStdString();

        std::shared_ptr<SysMLv2::REST::Element> element = std::make_shared<SysMLv2::REST::Element>();
        element->setLanguage("SysMLv2");
        element->setBody(body);
        return {element};
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Element>> StructuraSystemsParser::readKerMLFile(QString filePath) {
        QFile file = QFile(filePath);
        file.open(QIODevice::ReadOnly);
        QByteArray array = file.readAll();
        file.close();
        std::string body = QString(array.data()).toStdString();

        std::shared_ptr<SysMLv2::REST::Element> element = std::make_shared<SysMLv2::REST::Element>();
        element->setLanguage("KerML");
        element->setBody(body);
        return {element};
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Element>> StructuraSystemsParser::readJsonFile(QString ) {
        return std::vector<std::shared_ptr<SysMLv2::REST::Element>>();
    }

    void StructuraSystemsParser::writeFile(QString filePath,
                                           std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements) {
        if(filePath.contains(".md"))
            writeMarkdownFile(filePath, elements);

        if(filePath.contains(".kerml"))
            writeKerMLFile(filePath, elements);

        if(filePath.contains(".xml"))
            writeXmlFile(filePath, elements);

        if(filePath.contains(".json"))
            writeJsonFile(filePath, elements);

        if(filePath.contains(".sysml") || filePath.contains(".sml"))
            writeSysMLv2File(filePath, elements);

    }

    void StructuraSystemsParser::writeJsonFile(QString ,
                                               std::vector<std::shared_ptr<SysMLv2::REST::Element>> ) {

    }

    void StructuraSystemsParser::writeXmlFile(QString , std::vector<std::shared_ptr<SysMLv2::REST::Element>> ) {

    }

    void StructuraSystemsParser::writeKerMLFile(QString filePath,
                                                std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements) {

        QFile file;
        file.setFileName(filePath);
        if (file.open(QIODevice::ReadWrite)) {
            QTextStream stream(&file);
            for(const auto & elem : elements) {
                QString value = QString::fromStdString(elem->body());
                stream << value << "\r\n";
            }
        }
        file.flush();
        file.close();
    }

    void StructuraSystemsParser::writeSysMLv2File(QString filePath,
                                                  std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements) {
        QFile file;
        file.setFileName(filePath);
        if (file.open(QIODevice::ReadWrite)) {
            QTextStream stream(&file);
            for(const auto & elem : elements)
                stream << QString::fromStdString(elem->body()) << "\r\n";
        }
        file.flush();
        file.close();
    }

    void StructuraSystemsParser::writeMarkdownFile(QString filePath,
                                                   std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements) {
        QFile file;
        file.setFileName(filePath);
        if (file.open(QIODevice::ReadWrite)) {
            QTextStream stream(&file);
            for(const auto & elem : elements)
                stream << QString::fromStdString(elem->getMarkdownString()) << "\r\n";
        }
        file.flush();
        file.close();
    }

} // StructuraSystems::Client