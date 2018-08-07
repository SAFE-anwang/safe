#ifndef ASSETSLOGIN_H
#define ASSETSLOGIN_H

#include <QWidget>

namespace Ui {
    class AssetsLogin;
}

class AssetsPage;


class AssetsLogin:public QWidget
{
    Q_OBJECT

public:
    explicit AssetsLogin(AssetsPage* assetsPage);
    ~AssetsLogin();

private Q_SLOTS:
    void on_nextButton_clicked();

    void on_previousButton_clicked();

private:
    Ui::AssetsLogin *ui;
    AssetsPage* assetsPage;
    int assetsStepType;
};

#endif // ASSETSLOGIN_H
