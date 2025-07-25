//
// Created by Moritz Herzog on 09.04.25.
//

#ifndef DIGITALTWIN_CODEWIDGET_H
#define DIGITALTWIN_CODEWIDGET_H

#include <QWidget>
#include <QAbstractItemModel>
#include <QToolButton>
#include <QListWidget>
#include "../Models/CodeWidgetModel.h"

namespace SysMLv2::REST{
    class Project;
    class Commit;
    class Element;
}

namespace StructuraSystems::Client {
    class HTMLDelegate;

    QT_BEGIN_NAMESPACE
    namespace Ui { class CodeWidget; }
    QT_END_NAMESPACE

    class CodeWidget : public QWidget {
    Q_OBJECT

    public:
        explicit CodeWidget(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, QWidget *parent = nullptr);
        explicit CodeWidget(std::shared_ptr<SysMLv2::REST::Project> project, std::vector<std::shared_ptr<SysMLv2::REST::Element>> entities, QWidget *parent = nullptr);
        ~CodeWidget() override;

        QListWidget* getListWidget();

        CodeWidgetModel* getModel() const;

        void safeFile(std::string basePath);

    private:
        void decorateWidget();

        Ui::CodeWidget *ui;
        CodeWidgetModel* Model;
        HTMLDelegate* HtmlDelegate;
    };
} // StructuraSystems::Client

#endif //DIGITALTWIN_CODEWIDGET_H
