#include "applicationspage.h"
#include "ui_applicationspage.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "overviewentry.h"
#include "overviewpage.h"
#include "applicationsregistry.h"
#include "assetsdistribute.h"

ApplicationsPage::ApplicationsPage():
    ui(new Ui::ApplicationsPage)
{
    ui->setupUi(this);
    assetsRegistry = new ApplicationsRegistry(this);
    ui->mainLayout->addWidget(assetsRegistry);
    setMouseTracking(true);
}

ApplicationsPage::~ApplicationsPage()
{

}

void ApplicationsPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
}

void ApplicationsPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    assetsRegistry->setWalletModel(model);
}

void ApplicationsPage::setRegistRecordLayout(QVBoxLayout *layout)
{
    ui->tabRegistRecord->setLayout(layout);
}

WalletModel* ApplicationsPage::getWalletModel()const
{
    return walletModel;
}



















