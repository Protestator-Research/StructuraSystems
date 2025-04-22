//
// Created by Moritz Herzog on 22.04.25.
//

#include "StructuraSystemsParser.h"
#include "Markdown/MarkdownParser.h"
#include <QString>
#include <md4c.h>

namespace StructuraSystems::Client {
    StructuraSystemsParser::StructuraSystemsParser() {

    }

    StructuraSystemsParser::~StructuraSystemsParser() {

    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readFile(QString filePath) {
        return readMarkdownFile(filePath);
    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readMarkdownFile(QString filePath) {
        MarkdownParser parser;
        parser.parseMarkdownFile(filePath);
        return parser.getElementsOfProject();
    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readXmlFile(QString ) {

        return std::vector<std::shared_ptr<SysMLv2::Entities::Element>>();
    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readSysMLv2File(QString ) {

        return std::vector<std::shared_ptr<SysMLv2::Entities::Element>>();
    }

    std::vector<std::shared_ptr<SysMLv2::Entities::Element>> StructuraSystemsParser::readKerMLFile(QString ) {
        return std::vector<std::shared_ptr<SysMLv2::Entities::Element>>();
    }


} // StructuraSystems::Client