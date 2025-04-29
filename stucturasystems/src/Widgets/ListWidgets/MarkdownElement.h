#ifndef MARKDOWNELEMENT_H
#define MARKDOWNELEMENT_H

#include <QWidget>
#include <sysmlv2/rest/entities/Element.h>

namespace StructuraSystems::Client {
    namespace Ui {
        class MarkdownElement;
    }

    class MarkdownElement : public QWidget {
    Q_OBJECT

    public:
        explicit MarkdownElement(std::shared_ptr<SysMLv2::Entities::Element> element, QWidget *parent = nullptr);
        ~MarkdownElement();

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

    private:
        void openContextMenu(QPoint pos);
        void makeConnections();
        void redecorateMarkdownElement();
        Ui::MarkdownElement *ui;
        std::shared_ptr<SysMLv2::Entities::Element> Element;
        bool EditationState=false;
    };
}
#endif // MARKDOWNELEMENT_H
