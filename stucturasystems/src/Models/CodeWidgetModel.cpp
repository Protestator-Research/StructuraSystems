//
// Created by Moritz Herzog on 09.04.25.
//
#include <boost/uuid/uuid.hpp>
#include <kerml/root/elements/Element.h>
#include <kerml/root/annotations/TextualRepresentation.h>
#include <sysmlv2/rest/entities/JSONEntities.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Identification.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/Parser.h>
#include <utility>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <memory>
#include <iostream>

#include "CodeWidgetModel.h"

#include <QLayout>
#include <QInputDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QFrame>
#include <QString>
#include <QPushButton>
#include <QHBoxLayout>
#include <QToolButton>
#include <QStyle>
#include <QHBoxLayout>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/service/online/SysMLAPIImplementation.h>

#include "../Widgets/CodeWidget.h"
#include "Parser/Markdown/MarkdownParser.h"
#include "../Widgets/ListWidgets/MarkdownElement.h"
#include "../Widgets/ListWidgets/AddElementWidget.h"
#include "Parser/StructuraSystemsParser.h"
#include "../Services/BECommunicationService.h"


namespace StructuraSystems::Client {
	CodeWidgetModel::CodeWidgetModel(StructuraSystems::Client::CodeWidget* codeWidget,
		std::shared_ptr<SysMLv2::REST::Project>& project,
		std::shared_ptr<SysMLv2::REST::Commit>& commit) :
		QObject(codeWidget),
		Project(project),
		Commit(commit),
		_CodeWidget(codeWidget),
		ElementService(std::make_unique<SysMLv2::API::ElementNavigationService>()),
		_CommunicationService(nullptr) {
		updateItemView(project, commit);
	}

	CodeWidgetModel::CodeWidgetModel(StructuraSystems::Client::CodeWidget* codeWidget, std::shared_ptr<SysMLv2::REST::Project>& project,
		CommunicationService* communicationService, std::shared_ptr<SysMLv2::REST::Commit>& commit) :
		QObject(codeWidget),
		Project(project),
		Commit(commit),
		_CodeWidget(codeWidget),
		ElementService(nullptr),
		_CommunicationService(communicationService) {
		updateItemView(project, Commit);
	}

	void CodeWidgetModel::updateItemView(std::shared_ptr<SysMLv2::REST::Project>& project,
		std::shared_ptr<SysMLv2::REST::Commit>& commit) {

        const auto scrollAreaWidget = _CodeWidget->getScrollAreaWidget();
        auto* layout = scrollAreaWidget->layout();

        while (layout->count() > 0) {
            QLayoutItem* item = layout->takeAt(0);

            if (item == nullptr)
                continue;
            if (QWidget* widget = item->widget()) {
                widget->hide();
                delete widget;
            }
            delete item;
        }    
        
        if (Elements.empty() && commit != nullptr) {
			if (_CommunicationService != nullptr) {
				Elements = _CommunicationService->getAllElements(commit->getId(), project->getId());
			}
			else if (ElementService != nullptr) {
				Elements = ElementService->getElements(project, commit);
			}
		}

        for (const auto &element: Elements) {
            auto type = element->getType();
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            if (type == SysMLv2::REST::TEXTUAL_REPRESENTATION_TYPE) {
                const auto textualRepresentation = std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(element);

				auto markdownElement = new MarkdownElement(textualRepresentation, scrollAreaWidget);
				scrollAreaWidget->layout()->addWidget(markdownElement);
				markdownElement->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
				markdownElement->repaint();


				connect(markdownElement, SIGNAL(elementEdited()), this, SLOT(elementEdited()));
			}
		}
		scrollAreaWidget->repaint();
	}

	void CodeWidgetModel::elementEdited() {
		_CodeWidget->setWindowModified(true);
		emit tabEdited();
	}

	void CodeWidgetModel::createCommit(CommunicationService* communicationService) {
		bool userAcceptedDialog;
		QString commitDescription = QInputDialog::getText(_CodeWidget, tr("Commit Description"), tr("Please give a Commit Descriptions"), QLineEdit::Normal, tr(""), &userAcceptedDialog);
		if (userAcceptedDialog) {
			std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> requestedChage;
			for (const auto& element : Elements) {
				auto dataVersion = std::make_shared<SysMLv2::REST::DataVersion>(boost::uuids::random_generator()(), element);
				requestedChage.push_back(dataVersion);
			}

			auto commitRequest = std::make_shared<SysMLv2::REST::CommitRequest>(commitDescription.toStdString(), requestedChage);

			Commit = communicationService->postCommitWithId(Project->getId(), commitRequest);
		}
	}

	void CodeWidgetModel::saveFile(std::string basePath) {
		StructuraSystemsParser parser;
		auto path = QString::fromStdString(basePath + "/" + Project->getName());
		parser.writeFile(path, Elements);
	}

	void CodeWidgetModel::parseKerMLSysMLModel() {
		//deleting in case of old parser info
		ParserErrors.clear();
		InstaceElements.clear();
		for (const auto& element : Elements) {
			if (std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(element) != nullptr) {
				const auto textualRepresentation = std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(element);
				std::pair<std::vector<std::shared_ptr<KerML::Entities::Element>>, std::vector<std::shared_ptr<SysMLv2::Files::ParserError>>> parsedModel;

				const auto text = textualRepresentation->body();

				if (textualRepresentation->language() == "KerML")
					parsedModel = SysMLv2::Files::Parser::parseKerML(text);

				else if (textualRepresentation->language() == "SysMLv2")
					parsedModel = SysMLv2::Files::Parser::parseSysMLv2(text);

				if ((textualRepresentation->language() == "SysMLv2") || (textualRepresentation->language() == "KerML"))
				{
					for (const auto& elem : parsedModel.second)
						ParserErrors.push_back(elem);
					for (const auto& elem : parsedModel.first)
						InstaceElements.push_back(elem);
				}
			}
		}

		std::cout << "Number of Errors: " << ParserErrors.size() << std::endl;
		std::cout << "Number of Elements: " << InstaceElements.size() << std::endl;
		showParserResult();
	}

	std::shared_ptr<SysMLv2::REST::Commit> CodeWidgetModel::getCommit() const {
		return Commit;
	}

	std::shared_ptr<SysMLv2::REST::Project> CodeWidgetModel::getProject() const {
		return Project;
	}

	void CodeWidgetModel::setDialogView(bool dialogView) {
		DialogView = dialogView;
		updateItemView(Project, Commit);
	}

	std::vector<std::shared_ptr<KerML::Entities::Element>> CodeWidgetModel::getSelectedElements() const {
		//const auto codeDisplayWidget = CodeWidget->getListWidget();

		//const auto selectedItems = codeDisplayWidget->selectedItems();

		std::vector<std::shared_ptr<KerML::Entities::Element>> result;
		//for (const auto& index : selectedItems) {
		//    const auto& markdownWidget = dynamic_cast<MarkdownElement*>(index);
		//    result.push_back(markdownWidget->getElement());
		//}


		return result;
	}

	void CodeWidgetModel::createProjectAndCommit(CommunicationService* communicationService) {
		Project = communicationService->postProject(Project->getName(), Project->getDescription(), "Main");

		std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> requestedChage;
		for (const auto& element : Elements) {
			auto dataVersion = std::make_shared<SysMLv2::REST::DataVersion>(boost::uuids::random_generator()(), element);
			requestedChage.push_back(dataVersion);
		}

		auto commitRequest = std::make_shared<SysMLv2::REST::CommitRequest>("Upload from Local Project, by Structura Systems", requestedChage);

		Commit = communicationService->postCommitWithId(Project->getId(), commitRequest);
	}

    void CodeWidgetModel::pullFromBackend(CommunicationService* communicationService) {
        if (communicationService == nullptr)
            throw std::runtime_error("Backend connection not available.");

        const auto branches = communicationService->getAllBranchesForProjectWithID(Project->getId());
        std::shared_ptr<SysMLv2::REST::Branch> mainBranch;

        for (const auto &branch : branches){
            if (branch->getId() == Project -> getDefaultBranch()->getId()){
                mainBranch = branch;
                break;
            }
        }

        if (mainBranch == nullptr)
            throw std::runtime_error("Default branch not found.");
        if (mainBranch->getHead() == nullptr)
            throw std::runtime_error("Default branch has no head commit.");

        const auto latestCommit = communicationService->getCommitWithId(Project->getId(), mainBranch->getHead()->getId());

        if (latestCommit == nullptr)
            throw std::runtime_error("Head commit could not be downloaded.");

        const auto latestElements = communicationService->getAllElements(latestCommit->getId(), Project->getId());

        Commit = latestCommit;
        Elements = latestElements;
        updateItemView(Project, Commit);
		//pull invalidates parser, therefore nullptr
		if (_ParserResult != nullptr) {
			delete _ParserResult;
			_ParserResult = nullptr;
		}

		_CodeWidget->getErrorDockWidget_2()->hide();

		ParserErrors.clear();
		InstaceElements.clear();

        _CodeWidget->setWindowModified(false);
    }

	void CodeWidgetModel::showParserResult(){
		QWidget* container = _CodeWidget->getErrorDockWidget_2();
		if (_ParserResult != nullptr){
			delete _ParserResult;
			_ParserResult = nullptr;
		}
		auto* resultParser = new QFrame(container);
		resultParser->setObjectName("ParserResult");
		resultParser->setAttribute(Qt::WA_StyledBackground, true);
		const bool hasErrors = !ParserErrors.empty();
		resultParser->setStyleSheet(
			hasErrors
				?	R"(QFrame#ParserResult{
					background: qlineargradient(
					x1:0, y1:0, x2:1, y2:0,
					stop: 0 rgba(200,44,44,100),
					stop: 1 rgba(252,112,112,180));
					border: 1px solid #efefef;
					border-radius: 6px;})"
				:	R"(QFrame#ParserResult{
					background: qlineargradient(
					x1:0, y1:0, x2:1, y2:0,
					stop: 0 rgba(44,200,44,100),
					stop: 1 rgba(112,252,112,180));
					border: 1px solid #efefef;
					border-radius: 6px;})");

		auto* resultLayout = new QVBoxLayout(resultParser);
		auto* headerLayout = new QHBoxLayout();
		auto* title = new QLabel("Parser Results",resultParser);
		auto* closeButton = new QToolButton(resultParser);

		closeButton->setIcon(resultParser->style()->standardIcon(QStyle::SP_TitleBarCloseButton));

		closeButton->setAutoRaise(true);
		closeButton->setCursor(Qt::PointingHandCursor);
		closeButton->setToolTip("Close parser results");
		closeButton->setFixedSize(20, 20);

		headerLayout->addWidget(title);
		headerLayout->addStretch();
		headerLayout->addWidget(closeButton);

		connect(closeButton,&QPushButton::clicked,container,&QWidget::hide);



		auto* errors = new QLabel(QString("Number of Errors: %1").arg(ParserErrors.size()), resultParser);
		auto elements = new QLabel(QString("Number of Elements: %1").arg(InstaceElements.size()),resultParser);

		const QString textColor = hasErrors ? "#7F1D1D" : "#14532D";
		title->setStyleSheet(QString("color: %1;").arg(textColor));
		errors->setStyleSheet(QString("color: %1;").arg(textColor));
		elements->setStyleSheet(QString("color: %1;").arg(textColor));

		resultLayout->addLayout(headerLayout);
		resultLayout->addWidget(errors);
		resultLayout->addWidget(elements);
		

		container->layout()->addWidget(resultParser);
        _ParserResult = resultParser;
        container->show();
	}
}
