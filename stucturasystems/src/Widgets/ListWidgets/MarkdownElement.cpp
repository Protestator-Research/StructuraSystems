#include "MarkdownElement.h"
#include "ui_MarkdownElement.h"

#include <QMenu>
#include <QAbstractTextDocumentLayout>
#include <QContextMenuEvent>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QtMath>
#include <yaml-cpp/yaml.h>
#include <kerml/root/annotations/TextualRepresentation.h>


namespace StructuraSystems::Client {
    MarkdownElement::MarkdownElement(std::shared_ptr<KerML::Entities::TextualRepresentation> element, QWidget *parent)
            : QWidget(parent), ui(new Ui::MarkdownElement) {
        ui->setupUi(this);
        ui->retranslateUi(this);
        Element = element;
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui->TextBrowser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui->TextEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui->TextBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->TextBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->TextEditor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->TextEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->MoveElementDown->setIcon(QIcon(":/icons/arrows/DownGreen"));
        ui->MoveElementUp->setIcon(QIcon(":/icons/arrows/UpGreen"));
        redecorateMarkdownElement();
        makeConnections();
        updateSizeToContent();
    }

    MarkdownElement::~MarkdownElement() {
        delete ui;
    }

    std::shared_ptr<KerML::Entities::TextualRepresentation> MarkdownElement::getElement() const {
        return Element;
    }

    void MarkdownElement::redecorateMarkdownElement() {
        if(Element->language() == "YaML") {
            const auto yamlValue = QString::fromStdString(Element->body()).remove("---\n");

            if (yamlValue.isEmpty()) {
                ui->TextBrowser->clear();
            } else {
                YAML::Node node = YAML::Load(yamlValue.toStdString());
                QString value = QString::fromStdString("<div class=\"header\"><h1>"
                        +node["name"].as<std::string>() + "</h1>"
                        +"<h3>Author: "+node["maintainer"].as<std::string>() +"</h3></div>");
                ui->TextBrowser->setHtml(value);
            }
            ui->LanguageCombobox->setCurrentIndex(1);
        }else if((Element->language() == "SysML")||(Element->language() == "SysMLv2")||(Element->language() == "SysMD")) {
            ui->LanguageCombobox->setCurrentIndex(2);
            ui->TextBrowser->setText(QString::fromStdString(Element->body()));
        }else if((Element->language() == "KerML")) {
            ui->LanguageCombobox->setCurrentIndex(3);
            ui->TextBrowser->setText(QString::fromStdString(Element->body()));
        }else {
            ui->TextBrowser->setMarkdown(QString::fromStdString(Element->body()));
        }
        ui->TextEditor->setVisible(false);
        ui->LanguageCombobox->setVisible(false);
        updateSizeToContent();
    }

    void MarkdownElement::makeConnections() {
        connect(ui->TextBrowser, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(customContextMenuRequested(QPoint)));
        connect(ui->TextEditor, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(customContextMenuRequested(QPoint)));
        connect(ui->actionEdit, SIGNAL(triggered(bool)),this, SLOT(onEditClicked()));
        connect(ui->actionAdd_Element_Below, SIGNAL(triggered(bool)),this, SIGNAL(addElementBelow()));
        connect(ui->actionAddElementAbove, SIGNAL(triggered(bool)), this, SIGNAL(addElementAbove()));
        connect(ui->actionDelete, SIGNAL(triggered(bool)), this, SIGNAL(elementDeleteTriggered()));
        connect(ui->MoveElementUp, SIGNAL(clicked(bool)), this, SIGNAL(moveElementUp()));
        connect(ui->MoveElementDown, SIGNAL(clicked(bool)), this, SIGNAL(moveElementDown()));
        connect(ui->LanguageCombobox, SIGNAL(currentIndexChanged(int)), this, SLOT(editElementLanguage()));
        connect(ui->TextBrowser->document()->documentLayout(),
                &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this](const QSizeF &) { scheduleSizeUpdate(); });
        connect(ui->TextEditor->document()->documentLayout(),
                &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this](const QSizeF &) { scheduleSizeUpdate(); });
    }

    void MarkdownElement::contextMenuEvent(QContextMenuEvent *event) {
        openContextMenu(event->globalPos());

        QWidget::contextMenuEvent(event);
    }

    void MarkdownElement::resizeEvent(QResizeEvent *event) {
        QWidget::resizeEvent(event);
        scheduleSizeUpdate();
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

    void MarkdownElement::onEditClicked() {
        if(!EditationState) {
            ui->TextBrowser->setVisible(false);
            ui->TextEditor->setText(QString::fromStdString(Element->body()));
            ui->TextEditor->setVisible(true);
            ui->actionEdit->setText(tr("Edit finished"));
            ui->LanguageCombobox->setEnabled(true);
            ui->LanguageCombobox->setVisible(true);
            EditationState = true;
            updateSizeToContent();
        } else if(EditationState) {
            Element->setBody(ui->TextEditor->toPlainText().toStdString());
            ui->TextBrowser->setVisible(true);
            ui->actionEdit->setText(tr("Edit"));
            EditationState = false;
            ui->LanguageCombobox->setEnabled(false);
            ui->LanguageCombobox->setVisible(false);

            redecorateMarkdownElement();
            updateSizeToContent();
            emit elementEdited();
        }
    }

    void MarkdownElement::scheduleSizeUpdate() {
        if (SizeUpdatePending)
            return;

        SizeUpdatePending = true;
        QTimer::singleShot(0, this, [this]() {
            SizeUpdatePending = false;
            updateSizeToContent();
        });
    }

    void MarkdownElement::updateSizeToContent() {
        if (SizeUpdateInProgress)
            return;

        QScopedValueRollback<bool> updateGuard(SizeUpdateInProgress, true);
        QTextEdit *textWidget = EditationState
                                ? ui->TextEditor
                                : static_cast<QTextEdit *>(ui->TextBrowser);
        const int viewportWidth = textWidget->viewport()->width();
        if (viewportWidth <= 0)
            return;

        QTextDocument *document = textWidget->document();
        if (qAbs(document->textWidth() - viewportWidth) > 0.5)
            document->setTextWidth(viewportWidth);

        const int chromeHeight = textWidget->height() - textWidget->viewport()->height();
        const int contentHeight = qCeil(document->documentLayout()->documentSize().height());
        const int targetHeight = qMax(contentHeight + chromeHeight,
                                      textWidget->fontMetrics().height() + chromeHeight);

        if (textWidget->height() != targetHeight)
            textWidget->setFixedHeight(targetHeight);

        if (layout())
            layout()->invalidate();
        updateGeometry();

        if (QWidget *container = parentWidget()) {
            if (container->layout())
                container->layout()->invalidate();
            container->updateGeometry();
        }
    }

    void MarkdownElement::editElementLanguage() {
        switch (ui->LanguageCombobox->currentIndex()) {
            case 0:
            default:
                Element->setLanguage("Markdown");
                break;
            case 1:
                Element->setLanguage("YaML");
                break;
            case 2:
                Element->setLanguage("SysMLv2");
                break;
            case 3:
                Element->setLanguage("KerML");
                break;
        }
    }
}
