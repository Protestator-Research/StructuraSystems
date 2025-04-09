//
// Created by Moritz Herzog on 09.04.25.
//

#ifndef DIGITALTWIN_CODEWIDGET_H
#define DIGITALTWIN_CODEWIDGET_H

#include <QWidget>

namespace StructuraSystems::Client {
    QT_BEGIN_NAMESPACE
    namespace Ui { class CodeWidget; }
    QT_END_NAMESPACE

    class CodeWidget : public QWidget {
    Q_OBJECT

    public:
        explicit CodeWidget(QWidget *parent = nullptr);

        ~CodeWidget() override;

    private:
        Ui::CodeWidget *ui;
    };
} // StructuraSystems::Client

#endif //DIGITALTWIN_CODEWIDGET_H
