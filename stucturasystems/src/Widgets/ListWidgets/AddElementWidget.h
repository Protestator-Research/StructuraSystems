#ifndef ADDELEMENTWIDGET_H
#define ADDELEMENTWIDGET_H

#include <QWidget>
namespace StructuraSystems::Client {
    namespace Ui {
        class AddElementWidget;
    }

    class AddElementWidget : public QWidget {
    Q_OBJECT

    public:
        explicit AddElementWidget(QWidget *parent = nullptr);

        ~AddElementWidget();

    private:
        Ui::AddElementWidget *ui;
    };
}
#endif // ADDELEMENTWIDGET_H
