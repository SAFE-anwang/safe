#ifndef APPLICATIONPAGE_H
#define APPLICATIONPAGE_H

#include "platformstyle.h"

#include <QWidget>

namespace Ui {
    class ApplicationsPage;
}

class QVBoxLayout;
class ClientModel;
class WalletModel;
class OverViewEntry;
class ApplicationsRegistry;
class AssetsDistribute;

enum ApplicationStepType{
    ApplicationStepTypeRegistry,
    ApplicationStepTypeDistribute,
    ApplicationStepTypeLogin
};

/** Widget that shows a list of applications
  */
class ApplicationsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ApplicationsPage();
    ~ApplicationsPage();
    void setClientModel(ClientModel *model);
    void setWalletModel(WalletModel *model);
    void setRegistRecordLayout(QVBoxLayout* layout);
    WalletModel* getWalletModel()const;

private:
    Ui::ApplicationsPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    ApplicationsRegistry *assetsRegistry;
};

#endif // APPLICATIONPAGE_H
