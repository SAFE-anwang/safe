#include "walletmodel.h"
#include "applicationsregistry.h"
#include "ui_applicationsregistry.h"
#include "applicationspage.h"
#include "app/app.h"
#include "guiconstants.h"
#include "validation.h"
#include "init.h"
#include "wallet/wallet.h"
#include "net.h"
#include "utilmoneystr.h"
#include "transactionrecord.h"
#include "guiutil.h"
#include "main.h"
#include "masternode-sync.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"
#include <QMessageBox>
#include <string.h>
#include <vector>
using std::string;
using std::vector;

static const CAmount ASSETS_APP_OUT_VALUE = 0.0001 * COIN;
extern bool gInitByDefault;

ApplicationsRegistry::ApplicationsRegistry(ApplicationsPage *applicationPage):
    ui(new Ui::ApplicationsRegistry)
{
    ui->setupUi(this);
    this->applicationPage = applicationPage;
    assetsType = ApplicationStepTypeRegistry;
    ui->companyNameLabel->setVisible(true);
    ui->personalNameLabel->setVisible(false);

    //first letter not num,allow cn en num space;
    QRegExp regExpApplicationNameEdit;
    regExpApplicationNameEdit.setPattern("[a-zA-Z\u4e00-\u9fa5][a-zA-Z0-9\u4e00-\u9fa5 ]{1,50}");
    ui->applicationNameEdit->setValidator (new QRegExpValidator(regExpApplicationNameEdit, this));
    ui->applicationNameEdit->setMaxLength(MAX_APPNAME_SIZE);
    ui->devNameEdit->setMaxLength(MAX_DEVNAME_SIZE);
    //^((http)?:\/\/)[^\s]+   ^((http[s])?:\\/\\/)[^\\s\\S]+
    QRegExp regExpURLFormart;
    regExpURLFormart.setPattern("^((http[s]?):\\/\\/)[\\s\\S]+");
    ui->urlEdit->setValidator (new QRegExpValidator(regExpURLFormart, this));
    ui->urlEdit->setMaxLength(MAX_WEBURL_SIZE);
    ui->logoURLEdit->setValidator (new QRegExpValidator(regExpURLFormart, this));
    ui->logoURLEdit->setMaxLength(MAX_LOGOURL_SIZE);
    ui->coverURLEdit->setValidator (new QRegExpValidator(regExpURLFormart, this));
    ui->coverURLEdit->setMaxLength(MAX_COVERURL_SIZE);
    ui->applicationDescEdit->setMaxLength(MAX_APPDESC_SIZE);
    ui->devTypeComboBox->setStyleSheet("QComboBox{font-size:12px;}");
    ui->registerHelp->setOpenExternalLinks(true);
    ui->registerHelp->setText(tr("* Application Use Reference <a href=https://www.anwang.com/download/API.pdf>Safe3 Application Protocol</a>"));
    ui->registerHelp->setWordWrap(false);


#if QT_VERSION >= 0x040700
    ui->applicationNameEdit->setPlaceholderText(tr("Maximum %1 characters").arg(MAX_APPNAME_SIZE));
    ui->devNameEdit->setPlaceholderText(tr("Maximum %1 characters").arg(MAX_DEVNAME_SIZE));
    ui->urlEdit->setPlaceholderText(tr("Maximum %1 characters ,URL start with http://").arg(MAX_WEBURL_SIZE));
    ui->logoURLEdit->setPlaceholderText(tr("Maximum %1 characters ,URL start with http://").arg(MAX_LOGOURL_SIZE));
    ui->coverURLEdit->setPlaceholderText(tr("Maximum %1 characters ,URL start with http://").arg(MAX_COVERURL_SIZE));
    ui->applicationDescEdit->setPlaceholderText(tr("Maximum %1 characters").arg(MAX_APPDESC_SIZE));
#endif

    if(gInitByDefault)
    {
        ui->applicationNameEdit->setText("111");
        ui->devNameEdit->setText("222");
        ui->urlEdit->setText("https://www.baidu.com/");
        ui->logoURLEdit->setText("https://timgsa.baidu.com/timg?image&quality=80&size=b9999_10000&sec=1523869700414&di=da3d30d9949b815be691d8d77e01a6cb&imgtype=0&src=http%3A%2F%2Fpic.paopaoche.net%2Fup%2F2013-4%2F2013414151755725.jpg");
        ui->coverURLEdit->setText("http://www.163.com/");
        ui->applicationDescEdit->setText("123");
    }
    walletModel = NULL;
    setMouseTracking(true);
}

ApplicationsRegistry::~ApplicationsRegistry()
{

}

void ApplicationsRegistry::setWalletModel(WalletModel *model)
{
    walletModel = model;
}

void ApplicationsRegistry::on_devTypeComboBox_currentIndexChanged(int index)
{
    int type = index+1;
    if(type==APP_Dev_Type_Company)
    {
        ui->companyNameLabel->setVisible(true);
        ui->personalNameLabel->setVisible(false);
        ui->urlLabel->setVisible(true);
        ui->urlEdit->setVisible(true);
        ui->logoURLLabel->setVisible(true);
        ui->logoURLEdit->setVisible(true);
        ui->coverURLLabel->setVisible(true);
        ui->coverURLEdit->setVisible(true);
    }else
    {
        ui->companyNameLabel->setVisible(false);
        ui->personalNameLabel->setVisible(true);
        ui->urlLabel->setVisible(false);
        ui->urlEdit->setVisible(false);
        ui->logoURLLabel->setVisible(false);
        ui->logoURLEdit->setVisible(false);
        ui->coverURLLabel->setVisible(false);
        ui->coverURLEdit->setVisible(false);
    }
}

bool ApplicationsRegistry::existAppName(const std::string &appName)
{
    bool exist = ExistAppName(appName);
    if (exist)
    {
        ui->applicationNameEdit->setStyleSheet(STYLE_INVALID);
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application name existed!"),tr("Ok"));
    }
    else
    {
        ui->applicationNameEdit->setStyleSheet("QComboBox{font-size:12px;}");
    }
    return exist;
}

void ApplicationsRegistry::on_detectExistButton_clicked()
{
    if(GUIUtil::invalidInput(ui->applicationNameEdit))
        return;
    std::string appName = ui->applicationNameEdit->text().trimmed().toStdString();
    if(IsKeyWord(appName))
    {
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application name is internal reserved words, not allowed to use"),tr("Ok"));
        GUIUtil::setValidInput(ui->applicationNameEdit,false);
        return;
    }
    if(appName.empty() || appName.size() > MAX_APPNAME_SIZE)
    {
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application name input too long"),tr("Ok"));
        GUIUtil::setValidInput(ui->applicationNameEdit,false);
        return;
    }
    if(!existAppName(appName))
        QMessageBox::information(applicationPage, tr("Application registry"),tr("Application name is available!"),tr("Ok"));
}

bool ApplicationsRegistry::applicationRegist()
{
    if (!pwalletMain)
    {
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Wallet unavailable"),tr("Ok"));
        return false;
    }
    LOCK2(cs_main, pwalletMain->cs_wallet);
    if(!masternodeSync.IsBlockchainSynced())
    {
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Synchronizing block data"),tr("Ok"));
        return false;
    }

    if(GUIUtil::invalidInput(ui->applicationNameEdit))
        return false;
    std::string strAppName = ui->applicationNameEdit->text().trimmed().toStdString();
    if(IsKeyWord(strAppName))
    {
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application name is internal reserved words, not allowed to use"),tr("Ok"));
        GUIUtil::setValidInput(ui->applicationNameEdit,false);
        return false;
    }
    if(strAppName.empty() || strAppName.size() > MAX_APPNAME_SIZE)
    {
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application name input too long"),tr("Ok"));
        GUIUtil::setValidInput(ui->applicationNameEdit,false);
        return false;
    }
    GUIUtil::setValidInput(ui->applicationNameEdit,true);
    if(existAppName(strAppName))
        return false;

    uint8_t nDevType = ui->devTypeComboBox->currentIndex()+1;

    std::string strDevName = ui->devNameEdit->text().trimmed().toStdString();
    if(GUIUtil::invalidInput(ui->devNameEdit))
        return false;
    if(IsKeyWord(strDevName))
    {
        if(nDevType==APP_Dev_Type_Company)
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Company name is internal reserved words, not allowed to use"),tr("Ok"));
        else
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Personal name is internal reserved words, not allowed to use"),tr("Ok"));
        GUIUtil::setValidInput(ui->devNameEdit,false);
        return false;
    }
    if(strDevName.empty() || strDevName.size() > MAX_DEVNAME_SIZE)
    {
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Company name input too long"),tr("Ok"));
        GUIUtil::setValidInput(ui->devNameEdit,false);
        return false;
    }

    std::string strWebUrl = "";
    std::string strLogoUrl = "";
    std::string strCoverUrl = "";

    if(nDevType==APP_Dev_Type_Company)
    {
        QString webUrl = ui->urlEdit->text().trimmed();
        if(GUIUtil::invalidInput(ui->urlEdit))
            return false;
        strWebUrl = webUrl.toStdString();
        if(IsKeyWord(strWebUrl))
        {
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Official URL is internal reserved words, not allowed to use"),tr("Ok"));
            GUIUtil::setValidInput(ui->urlEdit,false);
            return false;
        }
        if(strWebUrl.empty() || strWebUrl.size() > MAX_WEBURL_SIZE)
        {
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Official URL input too long"),tr("Ok"));
            GUIUtil::setValidInput(ui->urlEdit,false);
            return false;
        }

        if(!IsValidUrl(strWebUrl))
        {
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Invalid URL"),tr("Ok"));
            GUIUtil::setValidInput(ui->urlEdit,false);
            return false;
        }

        QString logoUrl = ui->logoURLEdit->text().trimmed();
        if(GUIUtil::invalidInput(ui->logoURLEdit))
            return false;
        strLogoUrl = logoUrl.toStdString();
        if(IsKeyWord(strLogoUrl))
        {
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application logo URL is internal reserved words, not allowed to use"),tr("Ok"));
            GUIUtil::setValidInput(ui->logoURLEdit,false);
            return false;
        }
        if(strLogoUrl.empty() || strLogoUrl.size() > MAX_LOGOURL_SIZE)
        {
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application logo URL input too long"),tr("Ok"));
            GUIUtil::setValidInput(ui->logoURLEdit,false);
            return false;
        }

        if(!IsValidUrl(strLogoUrl))
        {
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Invalid URL"),tr("Ok"));
            GUIUtil::setValidInput(ui->logoURLEdit,false);
            return false;
        }

        QString coverUrl = ui->coverURLEdit->text().trimmed();
        if(GUIUtil::invalidInput(ui->coverURLEdit))
            return false;
        strCoverUrl = coverUrl.toStdString();
        if(IsKeyWord(strCoverUrl))
        {
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application cover URL is internal reserved words, not allowed to use"),tr("Ok"));
            GUIUtil::setValidInput(ui->coverURLEdit,false);
            return false;
        }
        if(strCoverUrl.empty() || strCoverUrl.size() > MAX_COVERURL_SIZE)
        {
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application cover URL input too long"),tr("Ok"));
            GUIUtil::setValidInput(ui->coverURLEdit,false);
            return false;
        }

        if(!IsValidUrl(strCoverUrl))
        {
            QMessageBox::warning(applicationPage, tr("Application registry"),tr("Invalid URL"),tr("Ok"));
            GUIUtil::setValidInput(ui->coverURLEdit,false);
            return false;
        }
    }
    GUIUtil::setValidInput(ui->urlEdit,true);
    GUIUtil::setValidInput(ui->logoURLEdit,true);
    GUIUtil::setValidInput(ui->coverURLEdit,true);

    if(GUIUtil::invalidInput(ui->applicationDescEdit))
        return false;
    std::string strAppDesc = ui->applicationDescEdit->text().trimmed().toStdString();
    if(strAppDesc.empty() || strAppDesc.size() > MAX_APPDESC_SIZE)
    {
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application description input too long"),tr("Ok"));
        GUIUtil::setValidInput(ui->applicationDescEdit,false);
        return false;
    }

    CAppData appData(strAppName, strAppDesc, nDevType, strDevName, strWebUrl, strLogoUrl, strCoverUrl);
    uint256 appId = appData.GetHash();
    //exists app id
    if(ExistAppId(appId))
    {
        QMessageBox::warning(applicationPage, tr("Application registry"),tr("Application ID existed!"),tr("Ok"));
        return false;
    }
    CAppHeader appHeader(g_nAppHeaderVersion, appId, REGISTER_APP_CMD);

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
    {
        QMessageBox::warning(applicationPage,tr("Application registry"), tr("Peer-to-peer functionality missing or disabled!"),tr("Ok"));
        return false;
    }

    CAmount nCancelledValue = GetCancelledAmount(g_nChainHeight);
    if(!IsCancelledRange(nCancelledValue))
    {
        QMessageBox::warning(applicationPage,tr("Application registry"), tr("Invalid cancelled safe amount"),tr("Ok"));
        return false;
    }
    CAmount neededAmount = nCancelledValue + ASSETS_APP_OUT_VALUE;
    if(neededAmount >= pwalletMain->GetBalance())
    {
        QString strNeededAmount = BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nCancelledValue + 0.01 * COIN);
        QMessageBox::warning(applicationPage,tr("Application registry"), tr("Insufficient safe funds,register applications need to consume %1").arg(strNeededAmount),tr("Ok"));
        return false;
    }

    vector<CRecipient> vecSend;
    CBitcoinAddress cancelledAddress(g_strCancelledSafeAddress);
    CScript cancelledScriptPubKey = GetScriptForDestination(cancelledAddress.Get());
    CRecipient cancelledRecipient = {cancelledScriptPubKey, nCancelledValue, 0, false,false,""};
    CRecipient adminRecipient = {CScript(), ASSETS_APP_OUT_VALUE, 0, false,false,""};
    vecSend.push_back(cancelledRecipient);
    vecSend.push_back(adminRecipient);

    QString costMoneyStr = QString::fromStdString(FormatMoney(GetCancelledAmount(g_nChainHeight)));
    QString str = tr("Register applications need to consume %1 SAFE, are you sure to register?").arg(costMoneyStr);
    QMessageBox box(QMessageBox::Question,  tr("Application registry"),str);
    QPushButton* okButton = box.addButton(tr("Yes"), QMessageBox::YesRole);
    box.addButton(tr("Cancel"),QMessageBox::NoRole);
    box.exec();
    if ((QPushButton*)box.clickedButton() != okButton){
        return false;
    }

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    std::string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, &appData, vecSend, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        QString errorStr = QString::fromStdString(strError);
        if(nCancelledValue + ASSETS_APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            errorStr = tr("Insufficient safe funds, this transaction requires a transaction fee of at least %1!").arg(QString::fromStdString(FormatMoney(nFeeRequired)));
        QMessageBox::warning(applicationPage,tr("Application registry"), QString::fromStdString(strError),tr("Ok"));
        return false;
    }

    // get admin address
    CTxDestination dest;
    if(!ExtractDestination(wtx.vin[0].prevPubKey, dest))
    {
        QMessageBox::warning(applicationPage,tr("Application registry"), tr("Get admin address failed!"),tr("Ok"));
        return false;
    }

    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
    {
        QMessageBox::warning(applicationPage,tr("Application registry"), tr("Register application failed, please check your wallet and try again later!"),tr("Ok"));
        return false;
    }
    return true;
}

bool ApplicationsRegistry::invalidInput()
{
    if(GUIUtil::invalidInput(ui->applicationNameEdit))
        return false;
    if(GUIUtil::invalidInput(ui->devNameEdit))
        return false;
    uint8_t nDevType = ui->devTypeComboBox->currentIndex()+1;
    if(nDevType==APP_Dev_Type_Company)
    {
        if(GUIUtil::invalidInput(ui->urlEdit))
            return false;
        if(GUIUtil::invalidInput(ui->logoURLEdit))
            return false;
        if(GUIUtil::invalidInput(ui->coverURLEdit))
            return false;
    }
    if(GUIUtil::invalidInput(ui->applicationDescEdit))
        return false;
    return true;
}

void ApplicationsRegistry::clearDisplay()
{
    ui->applicationNameEdit->clear();
    ui->devTypeComboBox->setCurrentIndex(0);
    ui->devNameEdit->clear();
    ui->urlEdit->clear();
    ui->logoURLEdit->clear();
    ui->coverURLEdit->clear();
    ui->applicationDescEdit->clear();
}

void ApplicationsRegistry::on_registerButton_clicked()
{
    WalletModel* model = applicationPage->getWalletModel();
    WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
    if(encStatus == model->Locked || encStatus == model->UnlockedForMixingOnly)
    {
        if(!invalidInput())
            return;
        WalletModel::UnlockContext ctx(model->requestUnlock());
        if(!ctx.isValid())
            return;

        if(applicationRegist())
        {
            QMessageBox::information(applicationPage, tr("Application registry"),tr("Application register success!"),tr("Ok"));
            clearDisplay();
        }
        return;
    }

    if(applicationRegist())
    {
        QMessageBox::information(applicationPage, tr("Application registry"),tr("Application register success!"),tr("Ok"));
        clearDisplay();
    }

}
