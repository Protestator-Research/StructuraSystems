//
// Created by Moritz Herzog on 22.04.25.
//

#ifndef STRUCTURASYSTEMS_STRUCTURASYSTEMSPARSER_H
#define STRUCTURASYSTEMS_STRUCTURASYSTEMSPARSER_H

#include <QString>
#include <vector>
#include <sysmlv2/rest/entities/Element.h>

namespace StructuraSystems::Client {
    class StructuraSystemsParser {
    public:
        StructuraSystemsParser();
        ~StructuraSystemsParser();

        std::vector<std::shared_ptr<SysMLv2::Entities::Element>> readFile(QString filePath);


    private:
        std::vector<std::shared_ptr<SysMLv2::Entities::Element>> readKerMLFile(QString filePath);
        std::vector<std::shared_ptr<SysMLv2::Entities::Element>> readSysMLv2File(QString filePath);
        std::vector<std::shared_ptr<SysMLv2::Entities::Element>> readXmlFile(QString filePath);
        std::vector<std::shared_ptr<SysMLv2::Entities::Element>> readMarkdownFile(QString filePath);
        std::vector<std::shared_ptr<SysMLv2::Entities::Element>> readJsonFile(QString filePath);
    };
} // StructuraSystems::Client

#endif //STRUCTURASYSTEMS_STRUCTURASYSTEMSPARSER_H
