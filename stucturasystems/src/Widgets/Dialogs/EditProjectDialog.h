#pragma once

#include <QDialog>

namespace StructuraSystems::Client
{
    namespace Ui {
        class EditProjectDialog;
    }

    class EditProjectDialog : public QDialog
    {
        Q_OBJECT

    public:
        explicit EditProjectDialog(QWidget *parent = nullptr, bool isOnline = false, std::string projectName = "", std::string description = "");
        ~EditProjectDialog();

        std::string getProjectName();
        std::string getProjectDescription();
        std::string getProjectPath();

        bool isOnlineProject();
        std::string projectVisibility();

    private slots:
        void onPathSelectionButtonClicked();
        void redecorateState();

    private:
        void makeConnections();


        Ui::EditProjectDialog *ui;

        bool IsOnline = false;
        std::string ProjectName;
        std::string Description;
        std::string ProjectPath;
    };
}
