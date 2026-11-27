#pragma once

#include <memory>
#include <vector>
#include <string>


namespace KerML::Entities{
    /*
    Element, Forward declaration for Base class for KerML elements.
    TextualRepresentation, Forward declaration for Class representing textual document blocks (Markdown, SysML,...).
    */
    class Element;
    class TextualRepresentation;
}

namespace StructuraSystems::Client{
    /**
     * @brief Returns the textual document blocks in document order.
     * @param elements All elements of the document.
     * @param warnings Optional container for warnings.
     * @return Ordered textual representations.
     */
    std::vector<std::shared_ptr<KerML::Entities::TextualRepresentation>> orderedDocumentBlocks(
        const std::vector<std::shared_ptr<KerML::Entities::Element>>& elements, std::vector<std::string>* warnings = nullptr
    );
    /**
     * @brief Finds a root package that owns document blocks, not an unrelated model package.
     * @param elements Elements of the downloaded document.
     * @return Root package, or nullptr if no document root is identifiable.
     */
    std::shared_ptr<KerML::Entities::Element> findDocumentRootPackage(
        const std::vector<std::shared_ptr<KerML::Entities::Element>>& elements
    );
    /**
     * @brief Creates or rebuilds the local document structure in block order.
     * @param packageName Name of a newly created root package.
     * @param blocks Textual representations in document order.
     * @param previousRoot Optional existing document root; its UUID and the memberships of surviving blocks are reused.
     * @return Root package, owning memberships and linked copies of the blocks. An empty document still has a local root package.
     * @throws std::invalid_argument If a block is null or duplicated.
     */
    std::vector<std::shared_ptr<KerML::Entities::Element>>
    createDocumentStructure(
        const std::string& packageName,
        const std::vector<std::shared_ptr<KerML::Entities::TextualRepresentation>>& blocks,
        const std::shared_ptr<KerML::Entities::Element>& previousRoot = nullptr
    );
}