//
// Read-only diagram of one text block.
//

#include "DiagramPreviewModel.h"

#include <Diagram/Conversion/GeneratedBlockGuard.h>

namespace StructuraSystems::Client {

    using Diagram::GeneratedBlockGuard;
    using Diagram::SysMLTextImporter;

    DiagramPreviewModel::DiagramPreviewModel(QObject *parent) :
            QObject(parent),
            Controller(new DiagramController(Model, this)) {
        RebuildTimer.setSingleShot(true);
        RebuildTimer.setInterval(120);
        connect(&RebuildTimer, &QTimer::timeout, this, &DiagramPreviewModel::rebuild);
    }

    DiagramPreviewModel::~DiagramPreviewModel() {
        delete Controller; // listens to Model, which is destroyed before the QObject children
        Controller = nullptr;
    }

    void DiagramPreviewModel::setSource(const QString &source) {
        if (Source == source)
            return;
        Source = source;
        emit sourceChanged();
        RebuildTimer.start();
    }

    void DiagramPreviewModel::setLanguage(const QString &language) {
        if (Language == language)
            return;
        Language = language;
        emit languageChanged();
        RebuildTimer.start();
    }

    void DiagramPreviewModel::rebuild() {
        RebuildTimer.stop();
        SysMLTextImporter::clear(Model);
        std::string text = Source.toStdString();
        // The marker line of a generated block is not content.
        if (const auto code = GeneratedBlockGuard::extractCode(text))
            text = *code;
        Report = SysMLTextImporter::importText(text, Model, Language == QStringLiteral("KerML")
                                                            ? SysMLTextImporter::Dialect::KerML
                                                            : SysMLTextImporter::Dialect::SysML);
        Controller->layoutAll();
        emit updated();
    }
}
