#include "customslider.h"
#include "customlineedit.h"
#include "assetsdistribute.h"
#include "ui_assetsdistribute.h"
#include "assetspage.h"
#include "customdoublevalidator.h"
#include "utilmoneystr.h"
#include "main.h"
#include "app/app.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "validation.h"
#include "init.h"
#include "wallet/wallet.h"
#include "net.h"
#include "transactionrecord.h"
#include "transactiondesc.h"
#include "rpc/server.h"
#include "bitcoinunits.h"
#include "masternode-sync.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include <QMessageBox>
#include <string>
#include <vector>
#include <QCompleter>
using std::string;
using std::vector;
extern CAmount gFilterAmountMaxNum;
extern bool gInitByDefault;

double gMaxAsset = 200000000000000.0000;

AssetsDistribute::AssetsDistribute(AssetsPage *assetsPage):
    QWidget(assetsPage),
    ui(new Ui::AssetsDistribute)
{
    ui->setupUi(this);
    this->assetsPage = assetsPage;
    ui->assetsCandyRatioSlider->initSlider(1,100,20);
    displayFirstDistribute(true);

    //first letter not num,allow cn en num,not allow space;
    QRegExp regExpShortNameEdit;
    regExpShortNameEdit.setPattern("[a-zA-Z\u4e00-\u9fa5][a-zA-Z0-9\u4e00-\u9fa5]{1,20}");
    ui->assetsShortNameEdit->setValidator (new QRegExpValidator(regExpShortNameEdit, this));
    ui->assetsShortNameEdit->setMaxLength(MAX_SHORTNAME_SIZE);

    //first letter not num,allow cn en num space;[a-zA-Z\u4e00-\u9fa5][a-zA-Z0-9\u4e00-\u9fa5 ]
    QRegExp regExpAssetsNameEdit;
    regExpAssetsNameEdit.setPattern("[a-zA-Z\u4e00-\u9fa5][a-zA-Z0-9\u4e00-\u9fa5 ]{1,20}");
    ui->assetsNameEdit->setValidator (new QRegExpValidator(regExpAssetsNameEdit, this));
    ui->assetsNameEdit->setMaxLength(MAX_ASSETNAME_SIZE);

    ui->assetsUnitEdit->setMaxLength(MAX_ASSETUNIT_SIZE);

    QRegExp regExp;
    regExp.setPattern("[a-zA-Z\u4e00-\u9fa5]+");
    QValidator *validator = new QRegExpValidator(regExp, ui->assetsUnitEdit);
    ui->assetsUnitEdit->setValidator (validator);

    QRegExp regExpDecimal;
    regExpDecimal.setPattern("^([4-9]|(10))$");
    ui->decimalEdit->setValidator(new QRegExpValidator(regExpDecimal, this));

    ui->totalAssetsEdit->setValidator(new CustomDoubleValidator(0,gMaxAsset,MAX_ASSETDECIMALS_VALUE));
    ui->firstAssetsEdit->setValidator(new CustomDoubleValidator(1,gMaxAsset,MAX_ASSETDECIMALS_VALUE));
    //ui->decimalEdit->setValidator(new QIntValidator(MIN_ASSETDECIMALS_VALUE,MAX_ASSETDECIMALS_VALUE));
    ui->assetsDescEdit->setMaxLength(MAX_ASSETDESC_SIZE);
    ui->commentsEdit->setMaxLength(MAX_REMARKS_SIZE);
    ui->expireLineEdit->setValidator(new QIntValidator(MIN_CANDYEXPIRED_VALUE,MAX_CANDYEXPIRED_VALUE));

    ui->assetsCandyRatioSlider->initLabel();
    ui->distributeComboBox->setStyleSheet("QComboBox{font-size:12px;margin-bottom:0px;padding-bottom:0px;}");
    ui->distributeTypeLabel->setStyleSheet("QLabel{font-size:12px;margin-top:0px;padding-top:0px;}");
    connect(ui->expireLineEdit, SIGNAL(textChanged(const QString &)), this, SLOT(handlerExpireLineEditTextChange(const QString &)));
    clearDisplay();
    msgboxTitle = tr("Assets distribute");
    completer = new QCompleter;
    stringListModel = new QStringListModel;
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setModel(stringListModel);
    completer->popup()->setStyleSheet("font: 12px;");
    ui->assetsNameComboBox->setCompleter(completer);
    ui->assetsNameComboBox->setEditable(true);
    ui->assetsNameComboBox->setStyleSheet("QComboBox{background-color:#FFFFFF;border:1px solid #82C3E6;font-size:12px;}");
    connect(ui->assetsNameComboBox, SIGNAL(currentTextChanged(QString)), this, SLOT(handlerAssetsNameComboBoxTextChange(QString)));

    if(gInitByDefault)
    {
        ui->assetsNameEdit->setText("铂金");
        ui->assetsShortNameEdit->setText("bojin");
        ui->assetsUnitEdit->setText("克");
        ui->totalAssetsEdit->setText("9999999.9999999999");
        ui->firstAssetsEdit->setText("999999");
        ui->decimalEdit->setText("10");
        ui->assetsDescEdit->setText("test desc");
        ui->commentsEdit->setText("test comments");
        ui->expireLineEdit->setText("1");
        ui->destroyedCheckBox->setChecked(true);
        ui->distributeCheckBox->setChecked(true);
    }
    ui->expireLineEdit->setEnabled(ui->distributeCheckBox->isChecked());
    ui->assetsCandyRatioSlider->setEnabled(ui->distributeCheckBox->isChecked());
    initWidget();
    setMouseTracking(true);
}

AssetsDistribute::~AssetsDistribute()
{

}

void AssetsDistribute::updateAssetsInfo()
{
    std::map<uint256, CAssetData> issueAssetMap;
    std::vector<std::string> assetNameVec;
    if(GetIssueAssetInfo(issueAssetMap))
    {
        for(std::map<uint256, CAssetData>::iterator iter = issueAssetMap.begin();iter != issueAssetMap.end();++iter)
        {
            CAssetData& assetData = (*iter).second;
            assetNameVec.push_back(assetData.strAssetName);
        }
        std::sort(assetNameVec.begin(),assetNameVec.end());
    }

    ui->assetsNameComboBox->clear();
    QStringList stringList;
    for(unsigned int i=0;i<assetNameVec.size();i++)
        stringList.append(QString::fromStdString(assetNameVec[i]));

    ui->assetsNameComboBox->addItems(stringList);
    stringListModel->setStringList(stringList);
    completer->setModel(stringListModel);
    completer->popup()->setStyleSheet("font: 12px;");
    ui->assetsNameComboBox->setCompleter(completer);
}

void AssetsDistribute::on_assetsCandyRatioSlider_valueChanged(int /*value*/)
{
    updateCandyValue();
}


void AssetsDistribute::clearDisplay()
{
    disconnect(ui->totalAssetsEdit,SIGNAL(textChanged(QString)),this,SLOT(on_totalAssetsEdit_textChanged(QString)));
    disconnect(ui->firstAssetsEdit,SIGNAL(textChanged(QString)),this,SLOT(on_firstAssetsEdit_textChanged(QString)));
    ui->assetsNameEdit->clear();
    ui->assetsUnitEdit->clear();
    ui->assetsShortNameEdit->clear();
    ui->totalAssetsEdit->clear();
    ui->firstAssetsEdit->clear();
    ui->decimalEdit->clear();
    ui->assetsDescEdit->clear();
    ui->commentsEdit->clear();
    ui->destroyedCheckBox->setChecked(false);
    ui->distributeCheckBox->setChecked(false);
    ui->expireLineEdit->clear();
    ui->assetsCandyRatioSlider->setValue(20);
    ui->assetsCandyRatioSlider->initLabel();
    ui->expireLineEdit->setEnabled(ui->distributeCheckBox->isChecked());
    ui->assetsCandyRatioSlider->setEnabled(ui->distributeCheckBox->isChecked());
    ui->candyTotalValueLabel->clear();
    connect(ui->totalAssetsEdit,SIGNAL(textChanged(QString)),this,SLOT(on_totalAssetsEdit_textChanged(QString)));
    connect(ui->firstAssetsEdit,SIGNAL(textChanged(QString)),this,SLOT(on_firstAssetsEdit_textChanged(QString)));
}


void AssetsDistribute::handlerAssetsNameComboBoxTextChange(const QString &text)
{
    bool firstDistribute = (ui->distributeComboBox->currentIndex()+1)==ASSETS_FIRST_DISTRIBUTE;
    if (!firstDistribute)
        updateTotalAssetsEdit();
}

void AssetsDistribute::updateTotalAssetsEdit()
{
    bool strAvailableAmountDisplay = true;
    string strAssetName = ui->assetsNameComboBox->currentText().toStdString();
    if(IsKeyWord(strAssetName))
    {
        strAvailableAmountDisplay = false;
    }
    if(strAssetName.empty())
    {
        strAvailableAmountDisplay = false;
    }
    if(strAssetName.size() > MAX_ASSETNAME_SIZE)
    {
        strAvailableAmountDisplay = false;
    }
    uint256 assetId;
    if(!GetAssetIdByAssetName(strAssetName,assetId, false))
    {
        strAvailableAmountDisplay = false;
    }
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(!GetAssetInfoByAssetId(assetId, assetInfo, false))
    {
        strAvailableAmountDisplay = false;
    }
    int decimal = assetInfo.assetData.nDecimals;
    CAmount addedAmount = GetAddedAmountByAssetId(assetId);
    CAmount availableAmount = assetInfo.assetData.nTotalAmount - assetInfo.assetData.nFirstIssueAmount - addedAmount;
    if(availableAmount==0)
    {
        strAvailableAmountDisplay = false;
    }
    if (strAvailableAmountDisplay)
    {
        QString strAvailableAmount = BitcoinUnits::formatWithUnit(decimal,availableAmount,false,BitcoinUnits::separatorAlways
                                                         ,true,QString::fromStdString(assetInfo.assetData.strAssetUnit));
        ui->totalAssetsEdit->setPlaceholderText(tr("Current can add:%1").arg(strAvailableAmount));
    }
    else
    {
        ui->totalAssetsEdit->setPlaceholderText(tr(""));
    }
}

void AssetsDistribute::initWidget()
{
    bool firstDistribute = (ui->distributeComboBox->currentIndex()+1)==ASSETS_FIRST_DISTRIBUTE;
#if QT_VERSION >= 0x040700
    ui->assetsNameEdit->setPlaceholderText(tr("For example: Gold,Maximum %1 characters").arg(MAX_ASSETNAME_SIZE));
    ui->assetsShortNameEdit->setPlaceholderText(tr("Maximum %1 characters ").arg(MAX_SHORTNAME_SIZE));
    ui->assetsUnitEdit->setPlaceholderText(tr("For example: gram, the maximum %1 characters").arg(MAX_ASSETUNIT_SIZE));
    if(firstDistribute)
        ui->totalAssetsEdit->setPlaceholderText(tr("Between %1 ~ %2").arg(MIN_TOTALASSETS_VALUE).arg(MAX_TOTALASSETS_VALUE));
    else
        updateTotalAssetsEdit();
    ui->firstAssetsEdit->setPlaceholderText(tr("Can not exceed the total assets"));
    ui->decimalEdit->setPlaceholderText(tr("Decimals %1 to %2,for example:8 represent negative 8 times 10").arg(MIN_ASSETDECIMALS_VALUE).arg(MAX_ASSETDECIMALS_VALUE));
    ui->assetsDescEdit->setPlaceholderText(tr("Maximum %1 characters").arg(MAX_ASSETDESC_SIZE));
    ui->commentsEdit->setPlaceholderText(tr("Maximum %1 characters").arg(MAX_REMARKS_SIZE));
    ui->expireLineEdit->setPlaceholderText(tr("The minimum %1 month, the maximum %2 months").arg(MIN_CANDYEXPIRED_VALUE).arg(MAX_CANDYEXPIRED_VALUE));
#endif
    if(firstDistribute)
        ui->totalAssetsLabel->setText(tr("Total Assets:"));
    else
        ui->totalAssetsLabel->setText(tr("Add Total Assets:"));

    updateCandyValue();
}

void AssetsDistribute::updateCandyValue()
{
    if(!ui->distributeCheckBox->isChecked())
        return;
    int decimal = ui->decimalEdit->text().toInt();
    string totalAssetsStr = ui->totalAssetsEdit->text().toStdString();
    string memoryAssetsStr = "";
    int pos = totalAssetsStr.find(".");
    if(pos<0)
    {
        memoryAssetsStr = totalAssetsStr;
        for(int i=0;i<decimal;i++)
            memoryAssetsStr.push_back('0');
    }else
    {
        memoryAssetsStr = totalAssetsStr.substr(0,pos);
        int decimalIndex = pos+1;
        int strSize = totalAssetsStr.size();
        for(int i=0;i<decimal;i++)
        {
            if(decimalIndex<strSize)
                memoryAssetsStr.push_back(totalAssetsStr[decimalIndex]);
            else
                memoryAssetsStr.push_back('0');
            decimalIndex++;
        }
    }
    string memoryCandyStr = mulstring(memoryAssetsStr,QString::number(ui->assetsCandyRatioSlider->value()).toStdString());

    int addDecimal = 3;//10% is 1,slider max 100 is 2,total 3
    int totalDecimal = decimal+addDecimal;
    int size = memoryCandyStr.size();
    if(decimal==0||size==0)
    {
        ui->candyTotalValueLabel->clear();
        return;
    }
    if(size<=totalDecimal)
    {
        string resultStr = "0.";
        int zeroCount = totalDecimal-size;
        for(int i=0;i<zeroCount;i++)
            resultStr.push_back('0');
        resultStr.append(memoryCandyStr.substr(0,decimal-zeroCount));
        ui->candyTotalValueLabel->setText(QString::fromStdString(resultStr));
    }else
    {
        string resultStr = numtofloatstring(memoryCandyStr,totalDecimal);
        ui->candyTotalValueLabel->setText(QString::fromStdString(resultStr.substr(0,resultStr.size()-addDecimal)));
    }
}

void AssetsDistribute::initFirstDistribute()
{
    displayFirstDistribute(true);
    initWidget();
}

void AssetsDistribute::initAdditionalDistribute()
{
    updateAssetsInfo();
    displayFirstDistribute(false);
    initWidget();
}

void AssetsDistribute::displayFirstDistribute(bool dis)
{
    ui->detectExistButton->setVisible(dis);
    ui->assetsUnitLabel->setVisible(dis);
    ui->assetsUnitEdit->setVisible(dis);
    ui->assetsShortNameLabel->setVisible(dis);
    ui->assetsShortNameEdit->setVisible(dis);
    ui->firstAssetsLabel->setVisible(dis);
    ui->firstAssetsEdit->setVisible(dis);
    ui->decimalLabel->setVisible(dis);
    ui->decimalEdit->setVisible(dis);
    ui->assetsDescLabel->setVisible(dis);
    ui->assetsDescEdit->setVisible(dis);
    ui->destroyedCheckBox->setVisible(dis);
    ui->distributeCheckBox->setVisible(dis);
    ui->expireLabel->setVisible(dis);
    ui->expireLineEdit->setVisible(dis);
    ui->assetsCandyRatioLabel->setVisible(dis);
    ui->assetsCandyRatioSlider->setVisible(dis);
    ui->detectExistButton->setVisible(dis);
    ui->ratioMaxLabel->setVisible(dis);
    ui->ratioMinLabel->setVisible(dis);
    ui->candyTotalValueLabel->setVisible(dis);
    ui->assetsNameLabel->setVisible(dis);
    ui->assetsNameEdit->setVisible(dis);
    ui->addAssetsNameLabel->setVisible(!dis);
    ui->assetsNameComboBox->setVisible(!dis);
}

void AssetsDistribute::on_distributeComboBox_currentIndexChanged(int index)
{
    ui->totalAssetsEdit->clear();
    ui->commentsEdit->clear();
    int type = index+1;
    if(type==ASSETS_FIRST_DISTRIBUTE){
        initFirstDistribute();
    }else if(type==ASSETS_ADD_DISTIRBUE){
        initAdditionalDistribute();
    }
}

bool AssetsDistribute::existAssetsName(const std::string &assetsName)
{
    bool exist = ExistAssetName(assetsName);
    if (exist)
    {
        ui->assetsNameEdit->setStyleSheet(STYLE_INVALID);
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Assets name existed!"),tr("Ok"));
    }
    else
    {
        ui->assetsNameEdit->setStyleSheet("");
    }
    return exist;
}

void AssetsDistribute::on_detectExistButton_clicked()
{
    if(GUIUtil::invalidInput(ui->assetsNameEdit))
        return;
    string strAssetName = ui->assetsNameEdit->text().trimmed().toStdString();
    if(IsKeyWord(strAssetName))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset name is internal reserved words, not allowed to use"),tr("Ok"));
        GUIUtil::setValidInput(ui->assetsNameEdit,false);
        return;
    }
    if(strAssetName.empty() || strAssetName.size() > MAX_ASSETNAME_SIZE)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset name input too long"),tr("Ok"));
        GUIUtil::setValidInput(ui->assetsNameEdit,false);
        return;
    }
    if(!existAssetsName(strAssetName))
        QMessageBox::information(assetsPage, msgboxTitle,tr("Assets name is available."),tr("Ok"));
}

bool AssetsDistribute::invalidInput()
{
    bool firstDistribute = (ui->distributeComboBox->currentIndex()+1)==ASSETS_FIRST_DISTRIBUTE;
    if(firstDistribute)
    {
        if(GUIUtil::invalidInput(ui->assetsNameEdit))
            return true;
        if(GUIUtil::invalidInput(ui->assetsShortNameEdit))
            return true;
        if(GUIUtil::invalidInput(ui->assetsUnitEdit))
            return true;
    }
    if(GUIUtil::invalidInput(ui->totalAssetsEdit))
        return true;
    if(firstDistribute)
    {
        if(GUIUtil::invalidInput(ui->firstAssetsEdit))
            return true;
        if(GUIUtil::invalidInput(ui->decimalEdit))
            return true;
        if(GUIUtil::invalidInput(ui->assetsDescEdit))
            return true;
        if(ui->distributeCheckBox->isChecked())
            if(GUIUtil::invalidInput(ui->expireLineEdit))
                return true;
    }
    return false;
}

bool AssetsDistribute::amountFromString(const std::string &valueStr, int decimal, CAmount &amount, bool *invalidDecimal)
{
    bool bOverflow = false;
    if (!ParseFixedPoint(valueStr, decimal, &amount,&bOverflow))
    {
        if (bOverflow)
        {
            QMessageBox::warning(assetsPage, msgboxTitle,tr("Amount out of range"),tr("Ok"));
            return false;
        }
        QString str = QString::fromStdString(valueStr);
        if(str.size()>1&&str[0]=='0'){
            return false;
        }
        QStringList strList = str.split(".");
        if(strList.size()>1)
        {
            int currValueDecimal = strList[1].size();
            if(currValueDecimal!=decimal)
            {
                QMessageBox::warning(assetsPage, msgboxTitle,tr("The input amount is not matched decimal"),tr("Ok"));
                if(invalidDecimal){
                    *invalidDecimal = true;
                }
                return false;
            }
        }
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Total amount of decimal no more than 19"),tr("Ok"));
        return false;
    }
    return true;
}

bool AssetsDistribute::distributeAssets()
{
    if (!pwalletMain)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Wallet unavailable"),tr("Ok"));
        return false;
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Synchronizing block data"),tr("Ok"));
        return false;
    }

    string strAssetName = ui->assetsNameEdit->text().trimmed().toStdString();
    if(IsKeyWord(strAssetName))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset name is internal reserved words, not allowed to use"),tr("Ok"));
        return false;
    }
    if(strAssetName.empty() || strAssetName.size() > MAX_ASSETNAME_SIZE)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset name input too long"),tr("Ok"));
        return false;
    }
    if(ExistAssetName(strAssetName))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Existent asset name"),tr("Ok"));
        return false;
    }

    string strShortName = ui->assetsShortNameEdit->text().trimmed().toStdString();
    if(IsKeyWord(strShortName))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset short name is internal reserved words, not allowed to use"),tr("Ok"));
        return false;
    }
    if(strShortName.empty() || strShortName.size() > MAX_SHORTNAME_SIZE )
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset abbreviation input too long"),tr("Ok"));
        return false;
    }
    if(ExistShortName(strShortName))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Existent asset short name"),tr("Ok"));
        return false;
    }

    string strAssetDesc = ui->assetsDescEdit->text().trimmed().toStdString();
    if(strAssetDesc.empty() || strAssetDesc.size() > MAX_ASSETDESC_SIZE)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset description input too long"),tr("Ok"));
        return false;
    }

    string strAssetUnit = ui->assetsUnitEdit->text().trimmed().toStdString();
    if(IsKeyWord(strAssetUnit))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset unit is internal reserved words, not allowed to use"),tr("Ok"));
        return false;
    }
    if(strAssetUnit.empty() || strAssetUnit.size() > MAX_ASSETUNIT_SIZE)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset units input too long"),tr("Ok"));
        return false;
    }

    int decimal = ui->decimalEdit->text().toInt();
    CAmount nAssetTotalAmount = 0;
    bool invalidDecimal = false;
    if(!amountFromString(ui->totalAssetsEdit->text().trimmed().toStdString(),decimal,nAssetTotalAmount,&invalidDecimal))
    {
        if(invalidDecimal)
            GUIUtil::setValidInput(ui->decimalEdit,false);
        else
            GUIUtil::setValidInput(ui->totalAssetsEdit,false);
        return false;
    }
    GUIUtil::setValidInput(ui->decimalEdit,true);
    CAmount amount100 = 0;
    amountFromString("100",decimal,amount100);
    if(nAssetTotalAmount < amount100 || nAssetTotalAmount > MAX_ASSETS)
    {
        GUIUtil::setValidInput(ui->totalAssetsEdit,false);
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Invalid asset total amount"),tr("Ok"));
        return false;
    }
    GUIUtil::setValidInput(ui->totalAssetsEdit,true);

    CAmount nFirstIssueAmount = 0;
    if (!amountFromString(ui->firstAssetsEdit->text().trimmed().toStdString(), decimal, nFirstIssueAmount))
    {
        GUIUtil::setValidInput(ui->firstAssetsEdit,false);
        return false;
    }

    if(nFirstIssueAmount <= 0)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Invalid first issue amount"),tr("Ok"));
        return false;
    }
    if(nFirstIssueAmount > nAssetTotalAmount)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("First issue amount exceed total amount"),tr("Ok"));
        return false;
    }
    GUIUtil::setValidInput(ui->firstAssetsEdit,true);

    uint8_t nAssetDecimals = ui->decimalEdit->text().trimmed().toInt();
    if(nAssetDecimals < MIN_ASSETDECIMALS_VALUE || nAssetDecimals > MAX_ASSETDECIMALS_VALUE)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Invalid asset decimals"),tr("Ok"));
        GUIUtil::setValidInput(ui->decimalEdit,false);
        return false;
    }
    GUIUtil::setValidInput(ui->decimalEdit,true);

    bool bDestory = ui->destroyedCheckBox->isChecked();
    bool bPayCandy = ui->distributeCheckBox->isChecked();
    CAmount nCandyAmount = 0;
    if(bPayCandy)
    {
        if (!amountFromString(ui->candyTotalValueLabel->text().trimmed().toStdString(), decimal, nCandyAmount))
            return false;
    }
    if(nCandyAmount < 0)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Invalid candy amount"),tr("Ok"));
        return false;
    }
    if(nCandyAmount > nFirstIssueAmount)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Candy amount exceed first issue amount"),tr("Ok"));
        return false;
    }
    if(!bPayCandy && nCandyAmount > 0)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Candy is disabled"),tr("Ok"));
        return false;
    }

    uint16_t nCandyExpired = 0;
    if(bPayCandy)
    {
        nCandyExpired = ui->expireLineEdit->text().trimmed().toInt();
        if(nCandyExpired < MIN_CANDYEXPIRED_VALUE || nCandyExpired > MAX_CANDYEXPIRED_VALUE)
        {
            QMessageBox::warning(assetsPage, msgboxTitle,tr("Invalid candy expired (min: %1, max: %2)").arg(MIN_CANDYEXPIRED_VALUE).arg(MAX_CANDYEXPIRED_VALUE),tr("Ok"));
            return false;
        }
    }

    CAmount nFirstActualAmount = nFirstIssueAmount - nCandyAmount;
    amountFromString("100",decimal,amount100);
    if(nFirstActualAmount < amount100 || nFirstActualAmount > nFirstIssueAmount)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Invalid first actual amount (min: 100)"),tr("Ok"));
        return false;
    }

    string strRemarks = ui->commentsEdit->text().trimmed().toStdString();
    if(strRemarks.size() > MAX_REMARKS_SIZE)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Remark input too long"),tr("Ok"));
        GUIUtil::setValidInput(ui->commentsEdit,false);
        return false;
    }
    GUIUtil::setValidInput(ui->commentsEdit,true);

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), ISSUE_ASSET_CMD);

    CAssetData assetData(strShortName, strAssetName, strAssetDesc, strAssetUnit, nAssetTotalAmount, nFirstIssueAmount,
                         bPayCandy ? nFirstIssueAmount - nCandyAmount : nFirstIssueAmount, nAssetDecimals, bDestory,
                         bPayCandy, nCandyAmount, nCandyExpired, strRemarks);
    uint256 assetId = assetData.GetHash();

    if(ExistAssetId(assetId))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Existent asset"),tr("Ok"));
        return false;
    }

    if (pwalletMain->IsLocked())
    {
        WalletModel::UnlockContext ctx(assetsPage->getWalletModel()->requestUnlock());
        if(!ctx.isValid())
            return false;
    }

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Peer-to-peer functionality missing or disabled"),tr("Ok"));
        return false;
    }

    int nOffset = g_nChainHeight - g_nProtocolV2Height;
    if (nOffset < 0)
    {
        QMessageBox::warning(assetsPage,msgboxTitle, tr("This feature is enabled when the block height is %1").arg(g_nProtocolV2Height),tr("Ok"));
        return false;
    }

    CAmount nCancelledValue = GetCancelledAmount(g_nChainHeight);
    if(!IsCancelledRange(nCancelledValue))
    {
        QMessageBox::warning(assetsPage,msgboxTitle, tr("Invalid cancelled safe amount"),tr("Ok"));
        return false;
    }
    CAmount neededAmount = nCancelledValue + 0.01 * COIN;
    if(neededAmount >= pwalletMain->GetBalance())
    {
        QString strNeededAmount = BitcoinUnits::formatWithUnit(assetsPage->getWalletModel()->getOptionsModel()->getDisplayUnit(), neededAmount);
        QMessageBox::warning(assetsPage,msgboxTitle, tr("Insufficient safe funds,distribute the assets need to consume %1").arg(strNeededAmount),tr("Ok"));
        return false;
    }

    vector<CRecipient> vecSend;

    // safe
    CBitcoinAddress cancelledAddress(g_strCancelledSafeAddress);
    CScript cancelledScriptPubKey = GetScriptForDestination(cancelledAddress.Get());
    CRecipient cancelledRecipient = {cancelledScriptPubKey, nCancelledValue, 0, false, false,""};
    vecSend.push_back(cancelledRecipient);

    // asset
    if(bPayCandy)
    {
        CRecipient assetRecipient = {CScript(), assetData.nFirstActualAmount, 0, false, true,""};
        vecSend.push_back(assetRecipient);
    }
    else
    {
        CRecipient assetRecipient = {CScript(), assetData.nFirstIssueAmount, 0, false, true,""};
        vecSend.push_back(assetRecipient);
    }

    QString costMoneyStr = QString::fromStdString(FormatMoney(GetCancelledAmount(g_nChainHeight)));
    QString str = "";
    if(bPayCandy)
        str = str + tr("Put candy max %1 times,current can put %2 times.").arg(MAX_PUTCANDY_VALUE).arg(MAX_PUTCANDY_VALUE);

    if(nCandyAmount==nFirstIssueAmount)
    {
        QString firstAssetStr = BitcoinUnits::formatWithUnit(decimal,nFirstIssueAmount,false,BitcoinUnits::separatorAlways
                                                         ,true,QString::fromStdString(strAssetUnit));
        str = str + tr("You will issue the assets %1, all of which will be used for put candy!").arg(firstAssetStr);
        str = str +"\n";
    }
    str = str +  tr("Distribute assets need to consume %1 SAFE, are you sure to distribute?").arg(costMoneyStr);
    QMessageBox box(QMessageBox::Question,  msgboxTitle ,str);
    QPushButton* okButton = box.addButton(tr("Yes"), QMessageBox::YesRole);
    box.addButton(tr("Cancel"),QMessageBox::NoRole);
    box.exec();
    if ((QPushButton*)box.clickedButton() != okButton)
        return false;

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &assetData, vecSend, NULL, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        QString errStr = QString::fromStdString(strError);
        if(nCancelledValue + nFeeRequired > pwalletMain->GetBalance())
            errStr = tr("Insufficient safe funds, this transaction requires a transaction fee of at least %1!")
                    .arg(QString::fromStdString(FormatMoney(nFeeRequired)));
        QMessageBox::warning(assetsPage, msgboxTitle,errStr,tr("Ok"));
        return false;
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Issue asset failed, please check your wallet and try again later!"),tr("Ok"));
        return false;
    }
    return true;
}

bool AssetsDistribute::addAssets()
{
    bool firstDistribute = (ui->distributeComboBox->currentIndex()+1)==ASSETS_FIRST_DISTRIBUTE;
    if (!pwalletMain)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Wallet unavailable"),tr("Ok"));
        return false;
    }
    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
    {
        QMessageBox::warning(assetsPage, msgboxTitle, tr("Synchronizing block data"), tr("Ok"));
        return false;
    }

    string strAssetName = ui->assetsNameComboBox->currentText().toStdString();
    if(IsKeyWord(strAssetName))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset name is internal reserved words, not allowed to use"),tr("Ok"));
        return false;
    }
    if(strAssetName.empty())
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Please select asset name"),tr("Ok"));
        return false;
    }
    if(strAssetName.size() > MAX_ASSETNAME_SIZE)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Asset name input too long"),tr("Ok"));
        return false;
    }
    uint256 assetId;
    if(!GetAssetIdByAssetName(strAssetName,assetId, false))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Invalid asset name"),tr("Ok"));
        return false;
    }
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(!GetAssetInfoByAssetId(assetId, assetInfo, false))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Non-existent asset id"),tr("Ok"));
        return false;
    }

    CBitcoinAddress adminAddress(assetInfo.strAdminAddress);
    if(!IsMine(*pwalletMain, adminAddress.Get()))
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("You are not the admin"),tr("Ok"));
        return false;
    }

    int decimal = assetInfo.assetData.nDecimals;
    CAmount addAmount = 0;
    bool invalidDecimal = false;
    if(!firstDistribute)
    {
        if(ui->totalAssetsEdit->text().toDouble() == 0)
        {
            QMessageBox::warning(assetsPage, msgboxTitle,tr("The total amount of add assets cannot be 0"),tr("Ok"));
            return false;
        }
    }
    if (!amountFromString(ui->totalAssetsEdit->text().trimmed().toStdString(), decimal, addAmount,&invalidDecimal))
    {
        if(invalidDecimal)
            GUIUtil::setValidInput(ui->decimalEdit,false);
        else
            GUIUtil::setValidInput(ui->totalAssetsEdit,false);
        return false;
    }

    //CAmount distributeAmount = pwalletMain->GetBalance(true,&assetId,&adminAddress);
    CAmount addedAmount = GetAddedAmountByAssetId(assetId);
    CAmount availableAmount = assetInfo.assetData.nTotalAmount - assetInfo.assetData.nFirstIssueAmount - addedAmount;
    if(availableAmount==0)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("All the assets have been distributed, and they should not be added"),tr("Ok"));
        return false;
    }
    QString strAvailableAmount = BitcoinUnits::formatWithUnit(decimal,availableAmount,false,BitcoinUnits::separatorAlways
                                                     ,true,QString::fromStdString(assetInfo.assetData.strAssetUnit));
    QString strAddAmount = BitcoinUnits::formatWithUnit(decimal,addAmount,false,BitcoinUnits::separatorAlways
                                                        ,true,QString::fromStdString(assetInfo.assetData.strAssetUnit));
    if(addAmount < 0 || addAmount>availableAmount)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Current Available:%1,asset can not be added").arg(strAvailableAmount),tr("Ok"));
        return false;
    }
    string strRemarks = ui->commentsEdit->text().trimmed().toStdString();
    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), ADD_ASSET_CMD);
    CCommonData addData(assetId, addAmount, strRemarks);

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Peer-to-peer functionality missing or disabled"),tr("Ok"));
        return false;
    }

    if(pwalletMain->GetBalance() <= 0)
    {
        CAmount neededAmount = 0.01 * COIN;
        QString strNeededAmount = BitcoinUnits::formatWithUnit(assetsPage->getWalletModel()->getOptionsModel()->getDisplayUnit(), neededAmount);
        QMessageBox::warning(assetsPage, msgboxTitle, tr("Insufficient safe funds,add distribute the assets need to consume %1").arg(strNeededAmount),tr("Ok"));
        return false;
    }
    vector<CRecipient> vecSend;
    CRecipient assetRecipient = {GetScriptForDestination(adminAddress.Get()), addAmount, 0, false, true,""};
    vecSend.push_back(assetRecipient);

    if(pwalletMain->GetBalance(false, NULL, &adminAddress) < 0.01 * COIN)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Please transfer at least 0.01 SAFE to address(%1).").arg(QString::fromStdString(adminAddress.ToString())),tr("Ok"));
        return false;
    }

    QString str = tr("you are going to add:%1,are you sure you wish to add distribute?").arg(strAddAmount);
    QMessageBox box(QMessageBox::Question,  msgboxTitle ,str);
    QPushButton* okButton = box.addButton(tr("Yes"), QMessageBox::YesRole);
    box.addButton(tr("Cancel"),QMessageBox::NoRole);
    box.exec();
    if ((QPushButton*)box.clickedButton() != okButton)
        return false;

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &addData, vecSend, &adminAddress, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        QString errStr = tr(strError.c_str());
        if(nFeeRequired > pwalletMain->GetBalance())
            errStr = tr("Insufficient safe funds, this transaction requires a transaction fee of at least %1!")
                    .arg(QString::fromStdString(FormatMoney(nFeeRequired)));
        QMessageBox::warning(assetsPage, msgboxTitle,errStr,tr("Ok"));
        return false;
    }

    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
    {
        QString errStr = tr("Add-issue asset failed, please check your wallet and try again later!");
        QMessageBox::warning(assetsPage, msgboxTitle,errStr,tr("Ok"));
        return false;
    }

    return true;
}

void AssetsDistribute::on_distributeButton_clicked()
{
    bool firstDistribute = (ui->distributeComboBox->currentIndex()+1) == ASSETS_FIRST_DISTRIBUTE;
    if(firstDistribute)
    {
        if(ui->decimalEdit->text().toInt()<MIN_ASSETDECIMALS_VALUE){
            ui->decimalEdit->setText(QString::number(MIN_ASSETDECIMALS_VALUE));
        }
        updateCandyValue();
    }

    if(invalidInput())
        return;

    if(firstDistribute)
    {
        WalletModel* model = assetsPage->getWalletModel();
        WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
        if(encStatus == model->Locked || encStatus == model->UnlockedForMixingOnly)
        {
            WalletModel::UnlockContext ctx(model->requestUnlock());
            if(!ctx.isValid())
                return;
            if(distributeAssets())
            {
                QMessageBox::information(assetsPage, msgboxTitle,tr("Assets distribute success!"),tr("Ok"));
                clearDisplay();
            }
            return;
        }
        if(distributeAssets())
        {
            QMessageBox::information(assetsPage, msgboxTitle,tr("Assets distribute success!"),tr("Ok"));
            clearDisplay();
        }
    }else
    {
        WalletModel* model = assetsPage->getWalletModel();
        WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
        if(encStatus == model->Locked || encStatus == model->UnlockedForMixingOnly)
        {
            WalletModel::UnlockContext ctx(model->requestUnlock());
            if(!ctx.isValid())
                return;
            if(addAssets())
            {
                QMessageBox::information(assetsPage, msgboxTitle,tr("Add assets distribute success!"),tr("Ok"));
                clearDisplay();
                updateTotalAssetsEdit();
            }
            return;
        }
        if(addAssets())
        {
            QMessageBox::information(assetsPage, msgboxTitle,tr("Add assets distribute success!"),tr("Ok"));
            clearDisplay();
            updateTotalAssetsEdit();
        }
    }
}


void AssetsDistribute::on_firstAssetsEdit_textChanged(const QString &/*arg1*/)
{
    updateCandyValue();
}

void AssetsDistribute::on_distributeCheckBox_clicked(bool checked)
{
    ui->candyTotalValueLabel->setEnabled(checked);
    ui->expireLineEdit->setEnabled(checked);
    ui->assetsCandyRatioSlider->setEnabled(checked);
    if(checked)
        updateCandyValue();
    ui->candyTotalValueLabel->setVisible(checked);
    if(!checked)
        GUIUtil::setValidInput(ui->expireLineEdit,true);
}

void AssetsDistribute::on_totalAssetsEdit_textChanged(const QString &/*arg1*/)
{
    updateCandyValue();
}

void AssetsDistribute::handlerExpireLineEditTextChange(const QString &text)
{
    if(text.toInt() == 0)
        ui->expireLineEdit->clear();
}
