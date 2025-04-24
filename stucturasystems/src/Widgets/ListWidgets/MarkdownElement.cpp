#include "MarkdownElement.h"
#include "ui_MarkdownElement.h"

#include <QMenu>
#include <QContextMenuEvent>


namespace StructuraSystems::Client {
    MarkdownElement::MarkdownElement(std::shared_ptr<SysMLv2::Entities::Element> element, QWidget *parent)
            : QWidget(parent), ui(new Ui::MarkdownElement) {
        ui->setupUi(this);
        ui->retranslateUi(this);
        Element = element;
        redecorateMarkdownElement();
        makeConnections();
    }

    MarkdownElement::~MarkdownElement() {
        delete ui;
    }

    void MarkdownElement::redecorateMarkdownElement() {
        ui->TextBrowser->setMarkdown(QString::fromStdString(Element->getMarkdownString()));
        ui->TextEditor->setText(QString::fromStdString(Element->getMarkdownString()));
        ui->TextEditor->setVisible(false);
        ui->MoveElementDown->setIcon(QIcon(":/icons/arrows/DownGreen"));
        ui->MoveElementUp->setIcon(QIcon(":/icons/arrows/UpGreen"));
    }

    void MarkdownElement::makeConnections() {
        connect(ui->TextBrowser, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(customContextMenuRequested(QPoint)));
    }

    void MarkdownElement::contextMenuEvent(QContextMenuEvent *event) {
        openContextMenu(event->globalPos());

        QWidget::contextMenuEvent(event);
    }

    void MarkdownElement::customContextMenuRequested(QPoint pos) {
        openContextMenu(ui->TextBrowser->mapToGlobal(pos));
    }

    void MarkdownElement::openContextMenu(QPoint pos) {
        QMenu contextMenu;
        contextMenu.addAction(ui->actionEdit);
        contextMenu.addSeparator();
        contextMenu.addAction(ui->actionAddElementAbove);
        contextMenu.addAction(ui->actionAdd_Element_Below);
        contextMenu.addSeparator();
        contextMenu.addAction(ui->actionDelete);
        contextMenu.exec(pos);
    }
}