#ifndef MARKDOWNELEMENT_H
#define MARKDOWNELEMENT_H

#include <QWidget>

namespace KerML::Entities {
    class TextualRepresentation;
}

namespace StructuraSystems::Client {
    namespace Ui {
        class MarkdownElement;
    }

    class MarkdownElement : public QWidget {
    Q_OBJECT

    public:
        explicit MarkdownElement(std::shared_ptr<KerML::Entities::TextualRepresentation> element, QWidget *parent = nullptr);
        ~MarkdownElement();

        std::shared_ptr<KerML::Entities::TextualRepresentation> getElement() const;

    signals:
        void moveElementUp();
        void moveElementDown();
        void addElementAbove();
        void addElementBelow();
        void elementEdited();
        void elementDeleteTriggered();

    protected:
        void contextMenuEvent(QContextMenuEvent *event) override;

    private slots:
        void customContextMenuRequested(QPoint pos);
        void onEditClicked();
        void editElementLanguage();

    private:
        void openContextMenu(QPoint pos);
        void makeConnections();
        void redecorateMarkdownElement();
        Ui::MarkdownElement *ui;
        std::shared_ptr<KerML::Entities::TextualRepresentation> Element;
        bool EditationState=false;
    };
}
#endif // MARKDOWNELEMENT_H
