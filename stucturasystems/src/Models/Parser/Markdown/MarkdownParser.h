//
// Created by Moritz Herzog on 26.08.24.
//

#ifndef DIGITALTWIN_MARKDOWNPARSER_H
#define DIGITALTWIN_MARKDOWNPARSER_H

#include "Elements/IMarkdownElement.h"
#include "CommonmarkParser/cmark.h"

#include <QString>
#include <vector>
#include <memory>
#include <map>
#include <functional>

namespace KerML::Entities {
    class Element;
}

namespace StructuraSystems::Client {
    class MarkdownParser {
    public:

        MarkdownParser() = default;
        ~MarkdownParser();
        MarkdownParser(const MarkdownParser&) = delete;
        MarkdownParser& operator=(const MarkdownParser&) = delete;

        void parseMarkdownFile(QString path);
        void parseMarkdown(QString markdown);

        std::vector<std::shared_ptr<KerML::Entities::Element>> getElementsOfProject();
        QString getHTMLOfMarkdown();
        QString getMarkdownString();
        void setParseYamlFrontMatter(bool parse);
        QString getYamlFrontMatter();

    private:
        std::string MarkdownString;
        bool ParseYamlFrontMatter = true;
        std::string YamlFrontMatter;
        cmark_node* FrontMatterNode = nullptr;
        cmark_node* MarkdownDocument = nullptr;

        std::shared_ptr<KerML::Entities::Element> createElement(std::string language, std::string body);
        void parseInternally();
        bool extractYamlFrontMatter(std::string& markdownBody);
    };
}

#endif //DIGITALTWIN_MARKDOWNPARSER_H
