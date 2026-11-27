//
// SysMLBlockTarget on top of a DocumentModel: the generated code lives in one ordinary SysMLv2 row of the
// document, so saving, parsing, commit and upload keep working without any change.
// Uses only the public API of DocumentModel (the same calls the element cards use).
//

#ifndef STRUCTURASYSTEMS_DOCUMENTBLOCKTARGET_H
#define STRUCTURASYSTEMS_DOCUMENTBLOCKTARGET_H

#include "SysMLBlockTarget.h"

#include <QPointer>
#include <QString>

namespace StructuraSystems::Client {
    class DocumentModel;

    class DocumentBlockTarget : public SysMLBlockTarget {
    public:
        /// elementId: a fixed row to edit (any SysMLv2/KerML block); empty = the generated block (found by its marker).
        explicit DocumentBlockTarget(DocumentModel *document, const QString &elementId = QString());

        [[nodiscard]] std::optional<std::string> readGeneratedBlock() override;
        void writeGeneratedBlock(const std::string &body) override;
        [[nodiscard]] bool isKerML() override;

        /// Row of the generated block in the document, -1 if there is none.
        [[nodiscard]] int blockRow();

    private:
        [[nodiscard]] QString elementIdAt(int row) const;
        [[nodiscard]] QString bodyAt(int row) const;

        QPointer<DocumentModel> Document;
        /// Id of the row this target wrote or found last; keeps the block even if its marker line was damaged by hand.
        QString BlockElementId;
        /// The block was chosen by the user: never switch to another block found by its marker.
        bool Fixed = false;
    };
}

#endif //STRUCTURASYSTEMS_DOCUMENTBLOCKTARGET_H
