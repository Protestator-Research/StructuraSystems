#include "MarkdownElement.h"
#include "ui_MarkdownElement.h"

#include <QMenu>
#include <QContextMenuEvent>
#include <yaml-cpp/yaml.h>


namespace StructuraSystems::Client {
    MarkdownElement::MarkdownElement(std::shared_ptr<SysMLv2::Entities::Element> element, QWidget *parent)
            : QWidget(parent), ui(new Ui::MarkdownElement) {
        ui->setupUi(this);
        ui->retranslateUi(this);
        Element = element;
        ui->MoveElementDown->setIcon(QIcon(":/icons/arrows/DownGreen"));
        ui->MoveElementUp->setIcon(QIcon(":/icons/arrows/UpGreen"));
        redecorateMarkdownElement();
        makeConnections();
    }

    MarkdownElement::~MarkdownElement() {
        delete ui;
    }

    void MarkdownElement::redecorateMarkdownElement() {
        if(Element->language() == "YaML") {
            const auto yamlValue = QString::fromStdString(Element->body()).remove("---\n");
            YAML::Node node = YAML::Load(yamlValue.toStdString());
            QString value = QString::fromStdString("<div class=\"header\"><h1>"
                    +node["name"].as<std::string>() + "</h1>"
                    +"<h3>Author: "+node["maintainer"].as<std::string>() +"</h3></div>");
            ui->TextBrowser->setHtml(value);
            ui->LanguageCombobox->setCurrentIndex(1);
        }
        if(Element->language() != "YaML") {
            ui->TextBrowser->setMarkdown(QString::fromStdString(Element->getMarkdownString()));
            ui->TextEditor->setText(QString::fromStdString(Element->getMarkdownString()));
        }
        if((Element->language() == "SysML")||(Element->language() == "SysMLv2")||(Element->language() == "SysMD")) {
            ui->LanguageCombobox->setCurrentIndex(2);
        }
        if((Element->language() == "KerML")) {
            ui->LanguageCombobox->setCurrentIndex(3);
        }
        ui->TextEditor->setVisible(false);
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

    void MarkdownElement::onEditClicked() {
        if(!EditationState) {
            ui->TextBrowser->setVisible(false);
            ui->TextEditor->setText(QString::fromStdString(Element->body()));
            ui->TextEditor->setVisible(true);
            ui->actionEdit->setText(tr("Edit finished"));
            ui->LanguageCombobox->setEnabled(true);
            EditationState = true;
        } else if(EditationState) {
            Element->setBody(ui->TextEditor->toPlainText().toStdString());
            ui->TextBrowser->setMarkdown(QString::fromStdString(Element->getMarkdownString()));
            ui->TextEditor->setVisible(false);
            ui->TextBrowser->setVisible(true);
            ui->actionEdit->setText(tr("Edit"));
            EditationState = false;
            ui->LanguageCombobox->setEnabled(false);
            redecorateMarkdownElement();
            emit elementEdited();
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