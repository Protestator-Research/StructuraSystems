//
// Created by Moritz Herzog on 30.10.26.
//

#include "CompletionController.h"

#include <memory>

#include <QVariantList>

#include "CompletionEngine.h"
#include "DocumentModel.h"

namespace StructuraSystems::Client {
    namespace {
        QString contextName(ContextKind kind) {
            switch (kind) {
                case ContextKind::Keyword: return QStringLiteral("keyword");
                case ContextKind::Type: return QStringLiteral("type");
                case ContextKind::Member: return QStringLiteral("member");
                case ContextKind::Import: return QStringLiteral("import");
                case ContextKind::Declaration: return QStringLiteral("declaration");
                case ContextKind::Anything: break;
            }
            return QStringLiteral("anything");
        }

        QString kindName(CompletionKind kind) {
            switch (kind) {
                case CompletionKind::Keyword: return QStringLiteral("keyword");
                case CompletionKind::Snippet: return QStringLiteral("snippet");
                case CompletionKind::Definition: return QStringLiteral("definition");
                case CompletionKind::Usage: return QStringLiteral("usage");
                case CompletionKind::Package: return QStringLiteral("package");
                case CompletionKind::Other: break;
            }
            return QStringLiteral("other");
        }
    }

    CompletionController::CompletionController(QObject *parent) : QObject(parent) {
    }

    DocumentModel *CompletionController::document() const {
        return Document.data();
    }

    void CompletionController::setDocument(DocumentModel *document) {
        if (Document == document)
            return;
        Document = document;
        emit documentChanged();
    }

    QString CompletionController::language() const {
        return Language;
    }

    void CompletionController::setLanguage(const QString &language) {
        if (Language == language)
            return;
        Language = language;
        emit languageChanged();
    }

    QVariantMap CompletionController::complete(const QString &text, int cursor) const {
        // The names of the model are the ones of the last parse (null before the first one).
        const std::shared_ptr<const NameIndex> names = Document != nullptr ? Document->nameIndex() : nullptr;
        const NameIndex empty;

        CompletionContext context;
        const auto items = CompletionEngine::complete(text, cursor, Language, names != nullptr ? *names : empty, &context);

        QVariantList list;
        for (const auto &item : items) {
            list.append(QVariantMap{{QStringLiteral("label"), item.Label},
                                    {QStringLiteral("insertText"), item.InsertText},
                                    {QStringLiteral("detail"), item.Detail},
                                    {QStringLiteral("kind"), kindName(item.Kind)},
                                    {QStringLiteral("cursorOffset"), item.CursorOffset},
                                    {QStringLiteral("selectionLength"), item.SelectionLength}});
        }
        return QVariantMap{{QStringLiteral("valid"), context.Valid},
                           {QStringLiteral("replaceStart"), context.ReplaceStart},
                           {QStringLiteral("replaceEnd"), context.ReplaceEnd},
                           {QStringLiteral("kind"), contextName(context.Kind)},
                           {QStringLiteral("prefix"), context.Prefix},
                           {QStringLiteral("qualifier"), context.Qualifier},
                           {QStringLiteral("items"), list}};
    }
}
