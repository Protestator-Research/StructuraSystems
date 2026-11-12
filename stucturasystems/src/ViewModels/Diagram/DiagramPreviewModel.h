//
// Read-only diagram of one text block, shown below a SysMLv2/KerML element card:
//     DiagramPreviewModel { source: card.body; language: card.language }
// Imports the text into its own semantic model and lays it out; nothing is ever written back.
//

#ifndef STRUCTURASYSTEMS_DIAGRAMPREVIEWMODEL_H
#define STRUCTURASYSTEMS_DIAGRAMPREVIEWMODEL_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/SemanticModel.h>

#include "DiagramController.h"

namespace StructuraSystems::Client {

    class DiagramPreviewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
        /// "SysMLv2" or "KerML" (as DocumentModel normalizes it)
        Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
        Q_PROPERTY(StructuraSystems::Client::DiagramController *controller READ controller CONSTANT)
        Q_PROPERTY(int elementCount READ elementCount NOTIFY updated)
        /// What of the text the diagram does not show, e.g. "constraint ByteRange, function ... (+3 more)".
        Q_PROPERTY(QString hiddenSummary READ hiddenSummary NOTIFY updated)
        Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY updated)
        /// Every hidden construct with its line number (tooltip).
        Q_PROPERTY(QString hiddenDetails READ hiddenDetails NOTIFY updated)

    public:
        explicit DiagramPreviewModel(QObject *parent = nullptr);
        ~DiagramPreviewModel() override;

        [[nodiscard]] QString source() const { return Source; }
        void setSource(const QString &source);
        [[nodiscard]] QString language() const { return Language; }
        void setLanguage(const QString &language);
        [[nodiscard]] DiagramController *controller() const { return Controller; }
        [[nodiscard]] int elementCount() const { return int(Model.elementCount()); }
        [[nodiscard]] QString hiddenSummary() const { return QString::fromStdString(Report.summary()); }
        [[nodiscard]] int hiddenCount() const { return int(Report.SkippedConstructs.size() + Report.Comments); }
        [[nodiscard]] QString hiddenDetails() const { return QString::fromStdString(Report.details()); }

        /// Imports the text now (normally done shortly after a change of source or language).
        void rebuild();

    signals:
        void sourceChanged();
        void languageChanged();
        void updated();

    private:
        Diagram::SemanticModel Model;
        Diagram::ImportReport Report;
        DiagramController *Controller = nullptr; ///< child, deleted explicitly before Model
        QString Source;
        QString Language = QStringLiteral("SysMLv2");
        QTimer RebuildTimer;
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAMPREVIEWMODEL_H
