#ifndef EDITPROJECTDIALOG_H
#define EDITPROJECTDIALOG_H

#include <QDialog>

namespace Ui {
class EditProjectDialog;
}

class EditProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditProjectDialog(QWidget *parent = nullptr);
    ~EditProjectDialog();

private:
    Ui::EditProjectDialog *ui;
};

#endif // EDITPROJECTDIALOG_H
