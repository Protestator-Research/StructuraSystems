#include "EditProjectDialog.h"
#include "ui_EditProjectDialog.h"

#include <QFileDialog>


namespace StructuraSystems::Client
{
    EditProjectDialog::EditProjectDialog(QWidget *parent, bool isOnline, std::string projectName, std::string description)
        : QDialog(parent),
        ui(new Ui::EditProjectDialog),
        IsOnline(isOnline),
        ProjectName(projectName),
        Description(description)
    {
        ui->setupUi(this);
        redecorateState();
        makeConnections();
    }

    EditProjectDialog::~EditProjectDialog()
    {
        delete ui;
    }

    std::string EditProjectDialog::getProjectName()
    {
        return ui->ProjectNameLineEdit->text().toStdString();
    }

    std::string EditProjectDialog::getProjectDescription()
    {
        return ui->ProjectDescriptionLineEdit->text().toStdString();
    }

    std::string EditProjectDialog::getProjectPath()
    {
        return ProjectPath;
    }

    bool EditProjectDialog::isOnlineProject()
    {
        return ((ui->OnlineCombobox->currentText()=="Online")&&(ui->OnlineCombobox->isVisible()));
    }

    std::string EditProjectDialog::projectVisibility()
    {
        return ui->VisibilityCombobox->currentText().toStdString();
    }

    void EditProjectDialog::onPathSelectionButtonClicked()
    {
        const auto filename = QFileDialog::getSaveFileName(this,tr("Create new Project"), "",tr("Markdown File (*.md);;KerML File (*.kerml);;SysML File (*.sysml);;XML-File (*.xml);;JSON-File (*.json)"));
        ui->DestinationLineEdit->setText(filename);
        ProjectPath = filename.toStdString();
    }

    void EditProjectDialog::redecorateState()
    {
        ui->OnlineCombobox->setVisible(IsOnline);
        ui->OnlineSettingsGroupBox->setVisible(IsOnline);

        ui->DestinationLineEdit->setEnabled(!(IsOnline&&(ui->OnlineCombobox->currentText()=="Online")));
        ui->SelectFolderButton->setEnabled(!(IsOnline&&(ui->OnlineCombobox->currentText()=="Online")));

        ui->VisibilityCombobox->setEnabled((IsOnline&&(ui->OnlineCombobox->currentText()=="Online")));
    }

    void EditProjectDialog::makeConnections()
    {
        connect(ui->SelectFolderButton, SIGNAL(clicked()),this,SLOT(onPathSelectionButtonClicked()));
        connect(ui->OnlineCombobox,SIGNAL(currentIndexChanged(int)),this,SLOT(redecorateState()));
    }
}
