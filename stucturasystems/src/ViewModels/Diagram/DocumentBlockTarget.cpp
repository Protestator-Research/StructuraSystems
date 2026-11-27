//
// SysMLBlockTarget on top of a DocumentModel.
//

#include "DocumentBlockTarget.h"
#include "DocumentModel.h"

#include <Diagram/Conversion/GeneratedBlockGuard.h>

namespace StructuraSystems::Client {

    std::unique_ptr<SysMLBlockTarget> createDocumentBlockTarget(QObject *document, const QString &elementId) {
        auto *documentModel = qobject_cast<DocumentModel *>(document);
        if (documentModel == nullptr)
            return nullptr;
        return std::make_unique<DocumentBlockTarget>(documentModel, elementId);
    }

    DocumentBlockTarget::DocumentBlockTarget(DocumentModel *document, const QString &elementId) :
            Document(document),
            BlockElementId(elementId),
            Fixed(!elementId.isEmpty()) {
    }

    bool DocumentBlockTarget::isKerML() {
        const int row = blockRow();
        return row >= 0 &&
               Document->data(Document->index(row), DocumentModel::LanguageRole).toString() == QStringLiteral("KerML");
    }

    QString DocumentBlockTarget::elementIdAt(int row) const {
        return Document->data(Document->index(row), DocumentModel::ElementIdRole).toString();
    }

    QString DocumentBlockTarget::bodyAt(int row) const {
        return Document->data(Document->index(row), DocumentModel::BodyRole).toString();
    }

    int DocumentBlockTarget::blockRow() {
        if (!Document)
            return -1;
        const int count = Document->rowCount();

        // Still the row we know? (Also covers a block whose marker line was damaged by hand.)
        if (!BlockElementId.isEmpty()) {
            for (int row = 0; row < count; ++row) {
                if (elementIdAt(row) == BlockElementId)
                    return row;
            }
        }
        if (Fixed)
            return -1; // the chosen block was deleted

        BlockElementId.clear();
        for (int row = 0; row < count; ++row) {
            if (Diagram::GeneratedBlockGuard::isGeneratedBlock(bodyAt(row).toStdString())) {
                BlockElementId = elementIdAt(row);
                return row;
            }
        }
        return -1;
    }

    std::optional<std::string> DocumentBlockTarget::readGeneratedBlock() {
        const int row = blockRow();
        if (row < 0)
            return std::nullopt;
        return bodyAt(row).toStdString();
    }

    void DocumentBlockTarget::writeGeneratedBlock(const std::string &body) {
        if (!Document)
            return;
        const QString text = QString::fromStdString(body);

        int row = blockRow();
        if (row < 0) {
            row = Document->rowCount();
            Document->insertElement(row, QStringLiteral("SysMLv2"));
            if (Document->rowCount() <= row)
                return;
            BlockElementId = elementIdAt(row);
        } else if (Document->data(Document->index(row), DocumentModel::LanguageRole).toString() != QStringLiteral("SysMLv2")) {
            // A KerML block replaced with the diagram: the generated code is SysML v2.
            Document->setLanguage(row, QStringLiteral("SysMLv2"));
        }
        Document->setBody(row, text);
    }
}
