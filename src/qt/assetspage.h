#ifndef ASSETSPAGE_H
#define ASSETSPAGE_H

#include "platformstyle.h"

#include <QWidget>

namespace Ui {
    class AssetsPage;
}

class QVBoxLayout;
class ClientModel;
class WalletModel;
class OverViewEntry;
class ApplicationsRegistry;
class AssetsDistribute;
class AssetsLogin;

/** Widget that shows a list of assets
  */
class AssetsPage : public QWidget
{
    Q_OBJECT

public:
    explicit AssetsPage();
    ~AssetsPage();
    void setClientModel(ClientModel *model);
    void setWalletModel(WalletModel *model);
    void setDistributeRecordLayout(QVBoxLayout * layout);
    void updateAssetsInfo();
    WalletModel* getWalletModel() const;
private:
    Ui::AssetsPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    AssetsDistribute *assetsDistribute;
};

#endif // ASSETSPAGE_H
