#ifndef ASSETSREGISTER_H
#define ASSETSREGISTER_H

#include <QWidget>

namespace Ui {
    class ApplicationsRegistry;
}

class WalletModel;
class ApplicationsPage;
class string;
class QLineEdit;

class ApplicationsRegistry:public QWidget
{
    Q_OBJECT

public:
    explicit ApplicationsRegistry(ApplicationsPage* applicationPage);
    ~ApplicationsRegistry();
    void setWalletModel(WalletModel *model);

private:
    bool existAppName(const std::string& appName);
    bool applicationRegist();
    void clearDisplay();
    bool invalidInput();

private Q_SLOTS:
    void on_devTypeComboBox_currentIndexChanged(int index);
    void on_detectExistButton_clicked();
    void on_registerButton_clicked();

private:
    Ui::ApplicationsRegistry *ui;
    ApplicationsPage* applicationPage;
    WalletModel *walletModel;
    int assetsType;
};

#endif // ASSETSREGISTER_H
