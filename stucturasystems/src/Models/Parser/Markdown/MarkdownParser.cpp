//
// Created by Moritz Herzog on 26.08.24.
//

#include "MarkdownParser.h"


#include "CommonmarkParser/cmark.h"
#include <kerml/root/elements/Element.h>
#include <kerml/root/annotations/TextualRepresentation.h>

#include <QFile>
#include <regex>
#include <cstdlib>


namespace StructuraSystems::Client {
    void MarkdownParser::parseMarkdownFile(QString path) {
        QFile file = QFile(path);
        [[maybe_unused]] auto result = file.open(QIODevice::ReadOnly);
        QByteArray array = file.readAll();
        file.close();
        MarkdownString = QString(array.data()).toStdString();
        parseInternally();
    }

    void MarkdownParser::parseMarkdown(QString markdown)
    {
        MarkdownString = markdown.toStdString();
        parseInternally();
    }

    MarkdownParser::~MarkdownParser() {
        if (MarkdownDocument != nullptr)
            cmark_node_free(MarkdownDocument);
    }

    void MarkdownParser::setParseYamlFrontMatter(bool parse) {
        ParseYamlFrontMatter = parse;
    }

    QString MarkdownParser::getYamlFrontMatter() {
        return QString::fromStdString(YamlFrontMatter);
    }

    QString MarkdownParser::getHTMLOfMarkdown() {
        if (FrontMatterNode != nullptr)
            cmark_node_unlink(FrontMatterNode);
        char* html = cmark_render_html(MarkdownDocument,0);
        QString result = QString(html);
        free(html);
        if (FrontMatterNode != nullptr)
            cmark_node_prepend_child(MarkdownDocument, FrontMatterNode);
        return result;
    }

    QString MarkdownParser::getMarkdownString()
    {
        return QString::fromStdString(MarkdownString);
    }

    std::vector<std::shared_ptr<KerML::Entities::Element>> MarkdownParser::getElementsOfProject() {
        std::vector<std::shared_ptr<KerML::Entities::Element>> returnValue;
        cmark_node* child = nullptr;
        do {
            if (child == nullptr)
                child = cmark_node_first_child(MarkdownDocument);
            else
                child = cmark_node_next(child);

            switch (cmark_node_get_type(child)) {
                case CMARK_NODE_NONE:
                    break;
                case CMARK_NODE_HTML_BLOCK:
                case CMARK_NODE_CODE_BLOCK:
            case CMARK_NODE_CODE:
                {
                    std::string fenceInfo = "";
                    if (cmark_node_get_fence_info(child))
                        fenceInfo = cmark_node_get_fence_info(child);
                    else
                        fenceInfo = "SysMLv2";

                    returnValue.push_back(createElement(fenceInfo,cmark_node_get_literal(child)));
                    break;
                }
                case CMARK_NODE_CUSTOM_BLOCK:
                    if (child == FrontMatterNode)
                        returnValue.push_back(createElement("YAML", YamlFrontMatter));
                    else
                        returnValue.push_back(createElement("Markdown", cmark_render_commonmark(child,0,0)));
                    break;
                case CMARK_NODE_PARAGRAPH:
                case CMARK_NODE_HEADING:
                case CMARK_NODE_THEMATIC_BREAK:
                case CMARK_NODE_TEXT:
                case CMARK_NODE_SOFTBREAK:
                case CMARK_NODE_LINEBREAK:
                case CMARK_NODE_HTML_INLINE:
                case CMARK_NODE_CUSTOM_INLINE:
                case CMARK_NODE_EMPH:
                case CMARK_NODE_STRONG:
                case CMARK_NODE_LINK:
                case CMARK_NODE_IMAGE:
                case CMARK_NODE_DOCUMENT:
                case CMARK_NODE_BLOCK_QUOTE:
                case CMARK_NODE_LIST:
                case CMARK_NODE_ITEM:
                    returnValue.push_back(createElement("Markdown", cmark_render_commonmark(child,0,0)));
                    break;
            }
        }while(cmark_node_last_child(MarkdownDocument)!=child);
        return returnValue;
    }

    void MarkdownParser::parseInternally() {
        if (MarkdownDocument != nullptr) {
            cmark_node_free(MarkdownDocument);
            MarkdownDocument = nullptr;
        }
        FrontMatterNode = nullptr;

        std::string body = MarkdownString;
        bool hasFrontMatter = extractYamlFrontMatter(body);
        MarkdownDocument = cmark_parse_document(body.c_str(),body.size(),0);

        if (hasFrontMatter) {
            cmark_node* node = cmark_node_new(CMARK_NODE_CUSTOM_BLOCK);
            std::string literal = "---\n" + YamlFrontMatter + "---\n";
            cmark_node_set_on_enter(node, literal.c_str());
            cmark_node_set_on_exit(node, "");
            cmark_node_prepend_child(MarkdownDocument, node);
            FrontMatterNode = node;
        }
    }

    bool MarkdownParser::extractYamlFrontMatter(std::string& markdownBody) {
        YamlFrontMatter.clear();
        if (!ParseYamlFrontMatter)
            return false;

        const std::string& text = markdownBody;
        size_t pos = 0;
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
            pos = 3;

        // Returns the line at pos without line ending and moves pos behind it.
        auto nextLine = [&text](size_t& position) {
            size_t end = text.find('\n', position);
            std::string line;
            if (end == std::string::npos) {
                line = text.substr(position);
                position = text.size();
            } else {
                line = text.substr(position, end - position);
                position = end + 1;
            }
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return line;
        };
        auto trimmed = [](std::string line) {
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            return line;
        };

        if (pos >= text.size() || trimmed(nextLine(pos)) != "---")
            return false;

        std::string yaml;
        while (pos < text.size()) {
            std::string line = nextLine(pos);
            std::string delimiter = trimmed(line);
            if (delimiter == "---" || delimiter == "...") {
                YamlFrontMatter = yaml;
                markdownBody = text.substr(pos);
                return true;
            }
            yaml += line + "\n";
        }
        return false;
    }

    std::shared_ptr<KerML::Entities::Element> MarkdownParser::createElement(std::string language, std::string body) {

        std::shared_ptr<KerML::Entities::TextualRepresentation> element = std::make_shared<KerML::Entities::TextualRepresentation>(language,body);
        return element;
    }
}