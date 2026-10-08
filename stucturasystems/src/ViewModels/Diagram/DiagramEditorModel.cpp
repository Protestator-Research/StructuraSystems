//
// View model of one diagram editor window.
//

#include "DiagramEditorModel.h"

#include <utility>

namespace StructuraSystems::Client {

    using Diagram::GeneratedBlockGuard;
    using Diagram::SysMLTextImporter;

    DiagramEditorModel::DiagramEditorModel(QObject *parent) :
            DiagramEditorModel(nullptr, parent) {
    }

    DiagramEditorModel::DiagramEditorModel(std::unique_ptr<SysMLBlockTarget> target, QObject *parent) :
            QObject(parent),
            Controller(new DiagramController(Model, this)) {
        // Many changes in a row (e.g. while editing) result in one regeneration.
        RegenerateTimer.setSingleShot(true);
        RegenerateTimer.setInterval(250);
        connect(&RegenerateTimer, &QTimer::timeout, this, &DiagramEditorModel::regenerate);
        connect(Controller, &DiagramController::statusMessage, this, &DiagramEditorModel::statusMessage);
        Parametric = new ParametricController(Model, Controller, this);
        connect(Parametric, &ParametricController::statusMessage, this, &DiagramEditorModel::statusMessage);

        ListenerToken = Model.addListener([this](const Diagram::ModelChange &) {
            if (!Loading)
                RegenerateTimer.start();
        });
        if (target) {
            setTarget(std::move(target));
            loadFromBlock();
        }
    }

    DiagramEditorModel::~DiagramEditorModel() {
        Model.removeListener(ListenerToken);
        delete Parametric; // both listen to Model, which is destroyed before the QObject children
        Parametric = nullptr;
        delete Controller;
        Controller = nullptr;
    }

    // --- binding ---------------------------------------------------------------------------------

    void DiagramEditorModel::classBegin() {
        Completing = true;
    }

    void DiagramEditorModel::componentComplete() {
        Completing = false;
        if (Document && !Target)
            bindDocument();
    }

    void DiagramEditorModel::setDocument(QObject *document) {
        if (Document == document)
            return;
        if (Document) {
            emit statusMessage(tr("The diagram editor is already bound to a document."));
            return;
        }
        Document = document;
        if (document) {
            // String based, so that this class needs no DocumentModel (and no sysmllib) at compile time.
            connect(document, SIGNAL(edited()), this, SLOT(recheckBlock()));
            connect(document, &QObject::destroyed, this, [this] {
                Target.reset();
                emit documentClosed();
            });
            if (!Completing)
                bindDocument();
        }
        emit documentChanged();
    }

    void DiagramEditorModel::setBlockElementId(const QString &elementId) {
        if (BlockElementId == elementId)
            return;
        if (Target) {
            emit statusMessage(tr("The block of a diagram editor cannot be changed."));
            return;
        }
        BlockElementId = elementId;
        emit blockElementIdChanged();
    }

    void DiagramEditorModel::bindDocument() {
        setTarget(createDocumentBlockTarget(Document, BlockElementId));
        loadFromBlock();
    }

    void DiagramEditorModel::setTarget(std::unique_ptr<SysMLBlockTarget> target) {
        Target = std::move(target);
        OwnsBlock = false;
        Guard.reset();
    }

    // --- loading (text -> diagram) ----------------------------------------------------------------

    void DiagramEditorModel::loadFromBlock() {
        Loading = true;
        RegenerateTimer.stop();
        SysMLTextImporter::clear(Model);
        LoadReport = {};
        OwnsBlock = false;

        const auto body = Target ? Target->readGeneratedBlock() : std::nullopt;
        if (body) {
            const bool generated = GeneratedBlockGuard::isGeneratedBlock(*body);
            const std::string text = generated ? GeneratedBlockGuard::extractCode(*body).value_or(std::string()) : *body;
            LoadReport = SysMLTextImporter::importText(text, Model, Target->isKerML() ? SysMLTextImporter::Dialect::KerML
                                                                                      : SysMLTextImporter::Dialect::SysML);
            // An unchanged generated block that was read completely: the diagram is the block.
            if (generated && Guard.check(body) == GeneratedBlockGuard::SyncState::InSync && LoadReport.lossless())
                OwnsBlock = true;
        }
        Loading = false;

        Controller->layoutAll();
        Parametric->layoutAll();
        const std::string code = Generator.generate(Model);
        if (code != LastCode) {
            LastCode = code;
            emit codeChanged();
        }
        setState(evaluateBlock(body));
        updatePendingChanges(body);
        emit loadedChanged();
    }

    void DiagramEditorModel::reloadFromBlock() {
        loadFromBlock();
        emit statusMessage(tr("Diagram loaded from the text of the block."));
    }

    QString DiagramEditorModel::hiddenSummary() const {
        return QString::fromStdString(LoadReport.summary());
    }

    int DiagramEditorModel::hiddenCount() const {
        return int(LoadReport.SkippedConstructs.size() + LoadReport.Comments);
    }

    // --- writing (diagram -> text) ------------------------------------------------------------------

    QString DiagramEditorModel::currentCode() const {
        return QString::fromStdString(LastCode);
    }

    void DiagramEditorModel::setWriteLocked(bool locked) {
        if (WriteLocked == locked)
            return;
        WriteLocked = locked;
        emit writeLockedChanged();
        if (!WriteLocked && Mode == Live && PendingChanges)
            writeBlockIfAllowed();
    }

    void DiagramEditorModel::setGenerationMode(GenerationMode mode) {
        if (Mode == mode)
            return;
        Mode = mode;
        emit generationModeChanged();
        if (Mode == Live && PendingChanges)
            writeBlockIfAllowed();
    }

    void DiagramEditorModel::generateNow() {
        RegenerateTimer.stop();
        regenerate();
        if (WriteLocked) {
            emit statusMessage(tr("The document is busy. The code is written as soon as it is free again."));
            return;
        }
        writeBlockIfAllowed();
        if (State == InSync && !PendingChanges)
            emit statusMessage(tr("SysML block updated."));
    }

    void DiagramEditorModel::overwriteWithDiagram() {
        RegenerateTimer.stop();
        regenerate();
        OwnsBlock = true;
        if (WriteLocked) {
            emit statusMessage(tr("The document is busy. The code is written as soon as it is free again."));
            return;
        }
        writeBlock();
        LoadReport = {};
        emit loadedChanged();
        emit statusMessage(tr("Block replaced with the diagram."));
    }

    void DiagramEditorModel::recheckBlock() {
        if (!Target)
            return;
        const auto body = Target->readGeneratedBlock();
        setState(evaluateBlock(body));
        updatePendingChanges(body);
    }

    void DiagramEditorModel::regenerate() {
        const std::string code = Generator.generate(Model);
        if (code != LastCode) {
            LastCode = code;
            emit codeChanged();
        }

        if (Target)
            updatePendingChanges(Target->readGeneratedBlock());
        else
            updatePendingChanges(std::nullopt);

        if (Mode == Live)
            writeBlockIfAllowed();
    }

    void DiagramEditorModel::writeBlockIfAllowed() {
        if (!Target || WriteLocked)
            return;
        const auto body = Target->readGeneratedBlock();
        const BlockState state = evaluateBlock(body);
        setState(state);
        if (state == EditedByHand || state == NotLoaded || state == HandWritten)
            return;
        if (!body.has_value() && LastCode.empty())
            return; // nothing drawn yet: do not create an empty block
        writeBlock();
    }

    void DiagramEditorModel::writeBlock() {
        if (!Target || WriteLocked)
            return;
        const std::string body = GeneratedBlockGuard::wrap(LastCode);
        // Set before writing: the document synchronously notifies recheckBlock().
        OwnsBlock = true;
        Guard.recordWrite();
        const auto current = Target->readGeneratedBlock();
        if (!current.has_value() || *current != body)
            Target->writeGeneratedBlock(body);
        setState(InSync);
        updatePendingChanges(body);
    }

    DiagramEditorModel::BlockState DiagramEditorModel::evaluateBlock(const std::optional<std::string> &body) const {
        if (body.has_value() && !GeneratedBlockGuard::isGeneratedBlock(*body))
            return OwnsBlock ? EditedByHand : HandWritten;
        switch (Guard.check(body)) {
            case GeneratedBlockGuard::SyncState::NoBlockYet:
            case GeneratedBlockGuard::SyncState::BlockMissing:
                return NoBlock;
            case GeneratedBlockGuard::SyncState::ModifiedByUser:
                return EditedByHand;
            case GeneratedBlockGuard::SyncState::InSync:
                break;
        }
        // Unchanged generated block - but if the diagram does not contain it completely,
        // overwriting would silently drop parts of it.
        if (!OwnsBlock) {
            const auto code = GeneratedBlockGuard::extractCode(*body);
            if (code.has_value() && GeneratedBlockGuard::fingerprint(*code) != GeneratedBlockGuard::fingerprint(""))
                return NotLoaded;
        }
        return InSync;
    }

    void DiagramEditorModel::setState(BlockState state) {
        if (State == state)
            return;
        State = state;
        emit blockStateChanged();
    }

    void DiagramEditorModel::updatePendingChanges(const std::optional<std::string> &body) {
        bool pending;
        if (!body.has_value()) {
            pending = !LastCode.empty();
        } else {
            const auto code = GeneratedBlockGuard::extractCode(*body);
            pending = !code.has_value() ||
                      GeneratedBlockGuard::fingerprint(*code) != GeneratedBlockGuard::fingerprint(LastCode);
        }
        if (PendingChanges == pending)
            return;
        PendingChanges = pending;
        emit pendingChangesChanged();
    }
}
