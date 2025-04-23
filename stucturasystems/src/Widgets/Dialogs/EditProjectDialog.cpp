#include "EditProjectDialog.h"
#include "ui_EditProjectDialog.h"

EditProjectDialog::EditProjectDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EditProjectDialog)
{
    ui->setupUi(this);
}

EditProjectDialog::~EditProjectDialog()
{
    delete ui;
}
