//
// Created by Moritz Herzog on 09.04.25.
//

#ifndef DIGITALTWIN_CODEWIDGET_H
#define DIGITALTWIN_CODEWIDGET_H

#include <QWidget>
#include <QAbstractItemModel>
#include "../Models/CodeWidgetModel.h"

namespace SysMLv2::Entities{
    class Project;
    class Commit;
}

namespace StructuraSystems::Client {
    QT_BEGIN_NAMESPACE
    namespace Ui { class CodeWidget; }
    QT_END_NAMESPACE

    class CodeWidget : public QWidget {
    Q_OBJECT

    public:
        explicit CodeWidget(std::shared_ptr<SysMLv2::Entities::Project> project, std::shared_ptr<SysMLv2::Entities::Commit> commit, QWidget *parent = nullptr);
        ~CodeWidget() override;

        void setMarkdownCodeEditingWidgetModel(QAbstractItemModel* model);
    private:
        Ui::CodeWidget *ui;
        CodeWidgetModel* Model;
    };
} // StructuraSystems::Client

#endif //DIGITALTWIN_CODEWIDGET_H
