#include "assetspage.h"
#include "ui_assetspage.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "overviewentry.h"
#include "overviewpage.h"
#include "applicationsregistry.h"
#include "assetsdistribute.h"
#include "assetslogin.h"

AssetsPage::AssetsPage():
    ui(new Ui::AssetsPage)
{
    ui->setupUi(this);
    assetsDistribute = new AssetsDistribute(this);
    ui->mainLayout->addWidget(assetsDistribute);
    setMouseTracking(true);
}

AssetsPage::~AssetsPage()
{

}

void AssetsPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
}

void AssetsPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
}

WalletModel* AssetsPage::getWalletModel() const
{
    return walletModel;
}

void AssetsPage::setDistributeRecordLayout(QVBoxLayout *layout)
{
    ui->tabDistributeRecord->setLayout(layout);
}

void AssetsPage::updateAssetsInfo()
{
    assetsDistribute->updateAssetsInfo();
}

void AssetsPage::setThreadUpdateData(bool update)
{
    assetsDistribute->setThreadUpdateData(update);
}

void AssetsPage::setThreadNoticeSlot(bool notice)
{
    assetsDistribute->setThreadNoticeSlot(notice);
}

















