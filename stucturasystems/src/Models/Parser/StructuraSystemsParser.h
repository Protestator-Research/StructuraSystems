//
// Created by Moritz Herzog on 22.04.25.
//

#ifndef STRUCTURASYSTEMS_STRUCTURASYSTEMSPARSER_H
#define STRUCTURASYSTEMS_STRUCTURASYSTEMSPARSER_H

#include <QString>
#include <vector>
#include <kerml/root/elements/Element.h>

namespace StructuraSystems::Client {
    class StructuraSystemsParser {
    public:
        StructuraSystemsParser();
        ~StructuraSystemsParser();

        std::vector<std::shared_ptr<KerML::Entities::Element>> readFile(QString filePath);

        void writeFile(QString filePath, std::vector<std::shared_ptr<KerML::Entities::Element>> elements);

    private:
        std::vector<std::shared_ptr<KerML::Entities::Element>> readKerMLFile(QString filePath);
        std::vector<std::shared_ptr<KerML::Entities::Element>> readSysMLv2File(QString filePath);
        std::vector<std::shared_ptr<KerML::Entities::Element>> readXmlFile(QString filePath);
        std::vector<std::shared_ptr<KerML::Entities::Element>> readMarkdownFile(QString filePath);
        std::vector<std::shared_ptr<KerML::Entities::Element>> readJsonFile(QString filePath);

        void writeKerMLFile(QString filePath, std::vector<std::shared_ptr<KerML::Entities::Element>> elements);
        void writeSysMLv2File(QString filePath, std::vector<std::shared_ptr<KerML::Entities::Element>> elements);
        void writeXmlFile(QString filePath, std::vector<std::shared_ptr<KerML::Entities::Element>> elements);
        void writeMarkdownFile(QString filePath, std::vector<std::shared_ptr<KerML::Entities::Element>> elements);
        void writeJsonFile(QString filePath, std::vector<std::shared_ptr<KerML::Entities::Element>> elements);
    };
} // StructuraSystems::Client

#endif //STRUCTURASYSTEMS_STRUCTURASYSTEMSPARSER_H
