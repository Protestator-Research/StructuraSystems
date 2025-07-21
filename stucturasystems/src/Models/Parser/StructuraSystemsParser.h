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

        std::vector<std::shared_ptr<SysMLv2::REST::Element>> readFile(QString filePath);

        void writeFile(QString filePath, std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements);

    private:
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> readKerMLFile(QString filePath);
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> readSysMLv2File(QString filePath);
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> readXmlFile(QString filePath);
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> readMarkdownFile(QString filePath);
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> readJsonFile(QString filePath);

        void writeKerMLFile(QString filePath, std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements);
        void writeSysMLv2File(QString filePath, std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements);
        void writeXmlFile(QString filePath, std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements);
        void writeMarkdownFile(QString filePath, std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements);
        void writeJsonFile(QString filePath, std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements);
    };
} // StructuraSystems::Client

#endif //STRUCTURASYSTEMS_STRUCTURASYSTEMSPARSER_H
