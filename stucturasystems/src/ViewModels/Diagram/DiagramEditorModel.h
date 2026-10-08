//
// View model of one diagram editor window: owns the semantic model, loads it from a block of the document
// (text -> graphic), generates SysML code from it and writes the code back into that block - live or on demand.
// Hand-written text is never overwritten automatically (GeneratedBlockGuard); the user decides.
//
// Created in QML (DiagramWindow) and bound to a document:
//     DiagramEditorModel { document: window.document; blockElementId: window.blockElementId }
// The document is passed as QObject (a DocumentModel), so this class does not depend on sysmllib.
//

#ifndef STRUCTURASYSTEMS_DIAGRAMEDITORMODEL_H
#define STRUCTURASYSTEMS_DIAGRAMEDITORMODEL_H

#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>
#include <string>

#include <Diagram/Conversion/GeneratedBlockGuard.h>
#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/SemanticModel.h>

#include "DiagramController.h"
#include "ParametricController.h"
#include "SysMLBlockTarget.h"

namespace StructuraSystems::Client {

    class DiagramEditorModel : public QObject, public QQmlParserStatus {
        Q_OBJECT
        Q_INTERFACES(QQmlParserStatus)
        QML_ELEMENT
        Q_PROPERTY(QObject *document READ document WRITE setDocument NOTIFY documentChanged)
        /// Block (element id of a document row) to edit; empty = the document's generated block.
        Q_PROPERTY(QString blockElementId READ blockElementId WRITE setBlockElementId NOTIFY blockElementIdChanged)
        Q_PROPERTY(StructuraSystems::Client::DiagramController *controller READ controller CONSTANT)
        /// Parametric view (values, calculations, constraints and their bindings) of one part.
        Q_PROPERTY(StructuraSystems::Client::ParametricController *parametric READ parametric CONSTANT)
        Q_PROPERTY(GenerationMode generationMode READ generationMode WRITE setGenerationMode NOTIFY generationModeChanged)
        Q_PROPERTY(BlockState blockState READ blockState NOTIFY blockStateChanged)
        Q_PROPERTY(bool pendingChanges READ hasPendingChanges NOTIFY pendingChangesChanged)
        Q_PROPERTY(QString code READ currentCode NOTIFY codeChanged)
        Q_PROPERTY(bool writeLocked READ writeLocked WRITE setWriteLocked NOTIFY writeLockedChanged)
        /// What of the loaded block the diagram does not show (would be lost on "Replace with diagram").
        Q_PROPERTY(QString hiddenSummary READ hiddenSummary NOTIFY loadedChanged)
        Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY loadedChanged)
        /// Every hidden construct with its line number (tooltip).
        Q_PROPERTY(QString hiddenDetails READ hiddenDetails NOTIFY loadedChanged)

    public:
        enum GenerationMode {
            Live,     ///< every change is written into the block
            OnDemand  ///< only "Generate SysML" writes into the block
        };
        Q_ENUM(GenerationMode)

        enum BlockState {
            NoBlock,      ///< no block yet: the first change creates the generated block
            InSync,       ///< block contains exactly what the editor generated
            EditedByHand, ///< generated block was changed in the text view -> automatic writing paused
            NotLoaded,    ///< generated block could not be loaded completely -> automatic writing paused
            HandWritten   ///< a hand-written block opened in the editor -> written only after the user decides
        };
        Q_ENUM(BlockState)

        explicit DiagramEditorModel(QObject *parent = nullptr);
        /// For tests: works directly on a target instead of a document. target may be nullptr (preview only).
        explicit DiagramEditorModel(std::unique_ptr<SysMLBlockTarget> target, QObject *parent = nullptr);
        ~DiagramEditorModel() override;

        void classBegin() override;
        void componentComplete() override;

        [[nodiscard]] QObject *document() const { return Document; }
        /// Binds the editor to a DocumentModel. Can be set once; the editor writes only into this document.
        void setDocument(QObject *document);
        [[nodiscard]] QString blockElementId() const { return BlockElementId; }
        /// Only before the document is bound (initial property of the window).
        void setBlockElementId(const QString &elementId);
        [[nodiscard]] DiagramController *controller() const { return Controller; }
        [[nodiscard]] ParametricController *parametric() const { return Parametric; }

        [[nodiscard]] Diagram::SemanticModel &semanticModel() { return Model; }
        [[nodiscard]] const Diagram::SemanticModel &semanticModel() const { return Model; }

        [[nodiscard]] GenerationMode generationMode() const { return Mode; }
        [[nodiscard]] BlockState blockState() const { return State; }
        [[nodiscard]] bool hasPendingChanges() const { return PendingChanges; }
        [[nodiscard]] bool writeLocked() const { return WriteLocked; }
        /// While locked (a commit, upload or parse works on the document) nothing is written; the changes stay pending.
        void setWriteLocked(bool locked);
        /// SysML text of the current diagram (without marker line).
        [[nodiscard]] QString currentCode() const;
        [[nodiscard]] QString hiddenSummary() const;
        [[nodiscard]] int hiddenCount() const;
        [[nodiscard]] QString hiddenDetails() const { return QString::fromStdString(LoadReport.details()); }

    public slots:
        void setGenerationMode(StructuraSystems::Client::DiagramEditorModel::GenerationMode mode);
        /// "Generate SysML": writes now, unless the block is paused (EditedByHand / NotLoaded / HandWritten).
        void generateNow();
        /// User decided: the diagram wins, the content of the block is replaced.
        void overwriteWithDiagram();
        /// User decided: the text wins, the diagram is loaded again from the block (unwritten changes are dropped).
        void reloadFromBlock();
        /// Call whenever the document's text may have changed (connected to DocumentModel::edited).
        void recheckBlock();

    signals:
        void documentChanged();
        void blockElementIdChanged();
        void generationModeChanged();
        /// Preview text changed (always live, independent of the generation mode).
        void codeChanged();
        void blockStateChanged();
        /// true while the diagram contains changes that are not yet in the document's block.
        void pendingChangesChanged();
        void writeLockedChanged();
        void loadedChanged();
        /// The bound document was closed; the editor window closes itself.
        void documentClosed();
        void statusMessage(const QString &message);

    private:
        void bindDocument();
        void setTarget(std::unique_ptr<SysMLBlockTarget> target);
        void loadFromBlock();
        void regenerate();
        void writeBlockIfAllowed();
        void writeBlock();
        [[nodiscard]] BlockState evaluateBlock(const std::optional<std::string> &body) const;
        void setState(BlockState state);
        void updatePendingChanges(const std::optional<std::string> &body);

        Diagram::SemanticModel Model;
        Diagram::SysMLCodeGenerator Generator;
        Diagram::GeneratedBlockGuard Guard;
        Diagram::ImportReport LoadReport;
        std::unique_ptr<SysMLBlockTarget> Target;
        Diagram::SemanticModel::ListenerToken ListenerToken = 0;
        /// Child QObject, deleted explicitly before Model (it listens to the model).
        DiagramController *Controller = nullptr;
        ParametricController *Parametric = nullptr;
        QPointer<QObject> Document;
        QString BlockElementId;
        bool Completing = false;
        bool Loading = false;

        GenerationMode Mode = Live;
        BlockState State = NoBlock;
        bool PendingChanges = false;
        bool WriteLocked = false;
        /// true once the diagram matches the block (loaded completely, or written by this editor).
        bool OwnsBlock = false;
        std::string LastCode;
        QTimer RegenerateTimer;
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAMEDITORMODEL_H
