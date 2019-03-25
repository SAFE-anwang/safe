#include "candypage.h"
#include "ui_candypage.h"
#include "candyview.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "transactiontablemodel.h"
#include "validation.h"
#include "init.h"
#include "net.h"
#include "utilmoneystr.h"
#include "main.h"
#include "script/sign.h"
#include "masternode-sync.h"
#include "txmempool.h"
#include "askpassphrasedialog.h"
#include <string.h>
#include <map>
#include <vector>
#include <QPushButton>
#include <QCompleter>
using std::string;
using std::map;
using std::vector;

extern bool gInitByDefault;
extern std::vector<CCandy_BlockTime_Info> gAllCandyInfoVec;
extern std::mutex g_mutexAllCandyInfo;
extern unsigned int nCandyPageCount;

CandyPage::CandyPage():
    ui(new Ui::CandyPage)
{
    ui->setupUi(this);
    commentsMaxLength = MAX_REMARKS_SIZE;

    int columnReleaseTimeWidth = 200;
    int columnAssetsNameWidth = 120;
    int columnTotalCandyWidth = 180;
    int columnCandyExpireWidth = 130;
    int columnOperatorWidth = 130;
    ui->tableWidgetGetCandy->setColumnWidth(0, columnReleaseTimeWidth);
    ui->tableWidgetGetCandy->setColumnWidth(1, columnAssetsNameWidth);
    ui->tableWidgetGetCandy->setColumnWidth(2, columnTotalCandyWidth);
    ui->tableWidgetGetCandy->setColumnWidth(3, columnCandyExpireWidth);
    ui->tableWidgetGetCandy->setColumnWidth(4, columnOperatorWidth);
    ui->tableWidgetGetCandy->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->tableWidgetGetCandy->setStyleSheet("QTableWidget{font-size:12px;}");
    strPutCandy = tr("Put Candy");
    strGetCandy = tr("Get Candy");
    ui->expireLineEdit->setPlaceholderText(tr("The minimum %1 month, the maximum %2 months").arg(MIN_CANDYEXPIRED_VALUE).arg(MAX_CANDYEXPIRED_VALUE));
    currAssetTotalAmount = 0;
    currAssetDecimal = 0;
    nAssetNameColumn = 1;
    nCandyAmountColumn = 2;
    nExpiredColumn = 3;
    nBtnColumn = 4;
    ui->expireLineEdit->setValidator(new QIntValidator(MIN_CANDYEXPIRED_VALUE,MAX_CANDYEXPIRED_VALUE, this));
    connect(ui->expireLineEdit, SIGNAL(textChanged(const QString &)), this, SLOT(handlerExpireLineEditTextChange(const QString &)));
    connect(ui->assetsComboBox,SIGNAL(currentTextChanged(QString)),this,SLOT(updateCandyInfo(QString)));
    ui->candyRatioSlider->initSlider(1,100,20);
    sliderFixedHeight = 10;
    nPageCount = nCandyPageCount;
    nCurrPage = 1;
    ui->spinBox->setValue(1);
    ui->candyRatioSlider->initLabel(sliderFixedHeight);
    ui->labelTotal->setVisible(false);
    isUnlockByGlobal = false;

    completer = new QCompleter;
    stringListModel = new QStringListModel;
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setModel(stringListModel);
    completer->popup()->setStyleSheet("font: 12px;");
    ui->assetsComboBox->setCompleter(completer);
    ui->assetsComboBox->setEditable(true);
    ui->assetsComboBox->setStyleSheet("QComboBox{background-color:#FFFFFF;border:1px solid #82C3E6;font-size:12px;}");
    ui->candyValueLabel->setText("0");
    ui->candyValueLabel->setVisible(true);

    ui->tableWidgetGetCandy->setSortingEnabled(false);
    updateAssetsInfo();
    //test
    if(gInitByDefault)
    {
        ui->expireLineEdit->setText("1");
        ui->candyValueLabel->setText("1");
    }
    setMouseTracking(true);

    msgbox = new QMessageBox(QMessageBox::Information, tr("Get Candy"), tr("Getting candy, please wait."));
    msgbox->setStandardButtons(QMessageBox::NoButton);
    candyWorker = new GetCandyWorker(this);
    getCandyThread = new QThread;
    candyWorker->moveToThread(getCandyThread);
    connect(this, SIGNAL(runGetCandy()), candyWorker, SLOT(doGetCandy()));
    connect(getCandyThread, &QThread::finished, candyWorker, &QObject::deleteLater);
    connect(this, SIGNAL(stopThread()), getCandyThread, SLOT(quit()));
    connect(candyWorker, SIGNAL(resultReady(bool, QString, int, CAmount)), this, SLOT(handlerGetCandyResult(bool, QString, int, CAmount)));
}

CandyPage::~CandyPage()
{
    Q_EMIT stopThread();
    getCandyThread->wait();
}

void CandyPage::clear()
{
    ui->commentTextEdit->clear();
    int value = 20;
    ui->candyRatioSlider->initSlider(1,100,value);
    ui->candyRatioSlider->initLabel(sliderFixedHeight);
    ui->expireLineEdit->clear();
    on_candyRatioSlider_valueChanged(value);
    if (ui->assetsComboBox->count() == 0)
        ui->candyValueLabel->setText("");
}

bool CandyPage::existedCandy(QWidget* widget,const QString &strAssetId, const qint64 &candyAmount
                             ,const quint16 &nExpired, const QString &strHash, const quint32 &nIndex)
{
    if(!widget)
        return false;
    QLayout* layout = widget->layout();
    if(!layout)
        return false;
    int count = layout->count();
    if(count<1)
        return false;
    QLayoutItem *it = layout->itemAt(0);
    if(!it)
        return false;
    QPushButton* btn = (QPushButton*)it->widget();
    if(!btn)
        return false;

    QString btnStrAssetId = btn->property("assetId").toString();
    if(strAssetId!=btnStrAssetId)
        return false;
    QString btnStrHash = btn->property("hash").toString();
    if(strHash!=btnStrHash)
        return false;
    quint32 btnNIndex = btn->property("n").toUInt();
    if(nIndex!=btnNIndex)
        return false;
    quint16 btnNExpired = btn->property("nExpired").toUInt();
    if(nExpired!=btnNExpired)
        return false;
    qint64 btnCandyAmount = btn->property("candyAmount").toUInt();
    if(candyAmount!=btnCandyAmount)
        return false;
    return true;
}

void CandyPage::updateCandyListWidget(const QString &strAssetId, const QString &assetName, const quint8 &nDecimals, const qint64 &candyAmount, const quint16 &nExpired
                                      , const QString &dateStr, const QString &strHash, const quint32 &nIndex,int currRow)
{
    ui->labelTotal->setVisible(true);
    bool fFoundCandy = false;
    int rowCount = ui->tableWidgetGetCandy->rowCount();
    for(int i = 0; i < rowCount; i++)
    {
        if(!existedCandy(ui->tableWidgetGetCandy->cellWidget(i,nBtnColumn),strAssetId,candyAmount,nExpired,strHash,nIndex))
            continue;
        fFoundCandy = true;
        break;
    }
    if(fFoundCandy)//no add,no update
        return;

    if(rowCount<=currRow){
        ui->tableWidgetGetCandy->insertRow(currRow);
    }

    QTableWidgetItem* itemDate = new QTableWidgetItem(dateStr);
    itemDate->setTextAlignment(Qt::AlignCenter);
    ui->tableWidgetGetCandy->setItem(currRow,0,itemDate);

    QTableWidgetItem* itemAssetName = new QTableWidgetItem(assetName);
    itemAssetName->setTextAlignment(Qt::AlignCenter);
    ui->tableWidgetGetCandy->setItem(currRow,nAssetNameColumn,itemAssetName);

    QString candyAmountStr = BitcoinUnits::format(nDecimals,candyAmount,false,BitcoinUnits::separatorNever,true);
    QTableWidgetItem* itemCandyAmount = new QTableWidgetItem(candyAmountStr);
    itemCandyAmount->setTextAlignment(Qt::AlignCenter);
    ui->tableWidgetGetCandy->setItem(currRow,nCandyAmountColumn,itemCandyAmount);

    QTableWidgetItem* itemExpireTime = new QTableWidgetItem(QString::number(nExpired));
    itemExpireTime->setTextAlignment(Qt::AlignCenter);
    ui->tableWidgetGetCandy->setItem(currRow,nExpiredColumn,itemExpireTime);

    QWidget *pWidget = new QWidget;
    QPushButton *btn = new QPushButton(tr("Get"));
    QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget(btn);
    layout->setAlignment(Qt::AlignCenter);
    layout->setContentsMargins(0,0,0,0);
    pWidget->setLayout(layout);
    ui->tableWidgetGetCandy->setCellWidget(currRow,4,pWidget);
    btn->setProperty("assetId",strAssetId);
    btn->setProperty("hash",strHash);
    btn->setProperty("n",nIndex);
    btn->setProperty("nExpired",nExpired);
    btn->setProperty("candyAmount",candyAmountStr);
    btn->setProperty("rowNum",currRow);
    connect(btn,SIGNAL(clicked()),this,SLOT(getCandy()));
}

void CandyPage::getCandy(QPushButton *btn)
{
    std::string strHash = btn->property("hash").toString().toStdString();
    std::string strAssetId = btn->property("assetId").toString().toStdString();
    int nIndex = btn->property("n").toInt();
    int nExpired = btn->property("nExpired").toInt();
    int rowNum = btn->property("rowNum").toInt();
    std::string strCandyAmount = btn->property("candyAmount").toString().toStdString();

    LOCK2(cs_main, pwalletMain->cs_wallet);
    if(!masternodeSync.IsBlockchainSynced())
    {
        QMessageBox::warning(this, strGetCandy,tr("Synchronizing block data"),tr("Ok"));
        return;
    }

    uint256 assetId(uint256S(strAssetId));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(!GetAssetInfoByAssetId(assetId, assetInfo, false))
    {
        QMessageBox::warning(this, strGetCandy,tr("Non-existent asset id"),tr("Ok"));
        return;
    }

    if (pwalletMain->IsLocked())
    {
        WalletModel::UnlockContext ctx(model->requestUnlock());
        if(!ctx.isValid())
            return;
    }
    if(pwalletMain->GetBroadcastTransactions() && !g_connman)
    {
        QMessageBox::warning(this, strGetCandy,tr("Peer-to-peer functionality missing or disabled"),tr("Ok"));
        return;
    }

    int nCurrentHeight = g_nChainHeight;

    CAmount nCandyAmount = 0;
    amountFromString(strCandyAmount,strGetCandy,assetInfo.assetData.nDecimals,nCandyAmount);

    COutPoint out(uint256S(strHash),nIndex);
    CCandyInfo candyInfo(nCandyAmount,nExpired);
    if(candyInfo.nAmount <= 0)
    {
        QMessageBox::warning(this, strGetCandy,tr("Invalid candy amount"),tr("Ok"));
        return;
    }

    uint256 blockHash;
    int nTxHeight = GetTxHeight(out.hash, &blockHash);
    if(blockHash.IsNull())
    {
        QMessageBox::warning(this, strGetCandy,tr("Get candy transaction height fail"),tr("Ok"));
        return;
    }

    int neededHeight = 0;
    if (nTxHeight >= g_nStartSPOSHeight)
    {
        neededHeight = nTxHeight + SPOS_BLOCKS_PER_DAY;
    }
    else
    {
        if (nTxHeight + BLOCKS_PER_DAY >= g_nStartSPOSHeight)
        {
            int nSPOSLaveHeight = (nTxHeight + BLOCKS_PER_DAY - g_nStartSPOSHeight) * (Params().GetConsensus().nPowTargetSpacing / Params().GetConsensus().nSPOSTargetSpacing);
            neededHeight = g_nStartSPOSHeight + nSPOSLaveHeight;
        }
        else
            neededHeight = nTxHeight + BLOCKS_PER_DAY;
    }

    if(nCurrentHeight < neededHeight)
    {
        int seconds = 0;
        if (neededHeight >= g_nStartSPOSHeight)
            seconds = (neededHeight-nCurrentHeight)*Params().GetConsensus().nSPOSTargetSpacing;
        else
            seconds = (neededHeight-nCurrentHeight)*Params().GetConsensus().nPowTargetSpacing;
        int hour = seconds / 3600;
        int minute = seconds % 3600 / 60;
        if (hour == 0 && minute != 0)
            QMessageBox::warning(this, strGetCandy,tr("Get candy need wait about %1 minutes").arg(QString::number(minute)),tr("Ok"));
        else if (hour != 0 && minute == 0)
            QMessageBox::warning(this, strGetCandy,tr("Get candy need wait about %1 hours").arg(QString::number(hour)),tr("Ok"));
        else
            QMessageBox::warning(this, strGetCandy,tr("Get candy need wait about %1 hours %2 minutes").arg(QString::number(hour)).arg(QString::number(minute)),tr("Ok"));
        return;
    }

    if (nTxHeight >= g_nStartSPOSHeight)
    {
        if(candyInfo.nExpired * SPOS_BLOCKS_PER_MONTH + nTxHeight - 3 * SPOS_BLOCKS_PER_DAY < nCurrentHeight)
        {
            QMessageBox::warning(this, strGetCandy,tr("Candy expired"),tr("Ok"));
            updateCurrentPage();
            return;
        }
    }
    else
    {
        if (candyInfo.nExpired * BLOCKS_PER_MONTH + nTxHeight >= g_nStartSPOSHeight)
        {
            int nSPOSLaveHeight = (candyInfo.nExpired * BLOCKS_PER_MONTH + nTxHeight - g_nStartSPOSHeight) * (Params().GetConsensus().nPowTargetSpacing / Params().GetConsensus().nSPOSTargetSpacing);
            int nTrueBlockHeight = g_nStartSPOSHeight + nSPOSLaveHeight;
            if (nTrueBlockHeight < nCurrentHeight)
            {
                QMessageBox::warning(this, strGetCandy,tr("Candy expired"),tr("Ok"));
                updateCurrentPage();
                return;
            }
        }
        else
        {
            if(candyInfo.nExpired * BLOCKS_PER_MONTH + nTxHeight < nCurrentHeight)
            {
                QMessageBox::warning(this, strGetCandy,tr("Candy expired"),tr("Ok"));
                updateCurrentPage();
                return;
            }
        }
    }

    CAmount nTotalSafe = 0;
    if(!GetTotalAmountByHeight(nTxHeight, nTotalSafe))
    {
        QMessageBox::warning(this, strGetCandy,tr("get all address safe amount failed"),tr("Ok"));
        return;
    }

    if(nTotalSafe <= 0)
    {
        QMessageBox::warning(this, strGetCandy,tr("get total safe amount failed"),tr("Ok"));
        return;
    }

    //Put get candy operation into the thread to prevent the main thread from being suspended.
    candyWorker->init(assetId, out, nTxHeight, nTotalSafe, candyInfo, assetInfo,rowNum);
    getCandyThread->start();
    Q_EMIT runGetCandy();
    msgbox->show();
}

void CandyPage::handlerGetCandyResult(const bool result, const QString errorStr, const int rowNum, const CAmount nFeeRequired)
{
    if (!msgbox->close())
        msgbox->hide();

    if (isUnlockByGlobal)
        model->setWalletLocked(true);
    isUnlockByGlobal = false;

    if(result == false)
    {
        if (nFeeRequired != GetCandyWorker::uselessArg)
            QMessageBox::warning(this, strGetCandy, tr(errorStr.toStdString().c_str()).arg(QString::fromStdString(FormatMoney(nFeeRequired)),tr("Ok")));
        else
            QMessageBox::warning(this, strGetCandy, tr(errorStr.toStdString().c_str()),tr("Ok"));
        if (rowNum != GetCandyWorker::uselessArg)
            eraseCandy(rowNum);
        return;
    }

    QMessageBox::information(this, strGetCandy,tr("Send candy transaction successfully"),tr("Ok"));
    eraseCandy(rowNum);

}

void GetCandyWorker::doGetCandy()
{
    //CGetCandyInfo getCandyInfo(assetId,candyInfo,out,assetInfo.assetData.nDecimals,nTxHeight,nTotalSafe,true);
    //PutCandyDataToList(getCandyInfo);
    //btn->setText(tr("get candying..."));
    //btn->setEnabled(false);

    map<CKeyID, int64_t> mapKeyBirth;
    pwalletMain->GetKeyBirthTimes(mapKeyBirth);

    std::vector<std::string> vaddress;
    for (map<CKeyID, int64_t>::const_iterator tempit = mapKeyBirth.begin(); tempit != mapKeyBirth.end(); tempit++)
    {
        std::string saddress = CBitcoinAddress(tempit->first).ToString();
        vaddress.push_back(saddress);
    }

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), GET_CANDY_CMD);
    CPutCandy_IndexKey assetIdCandyInfo;
    assetIdCandyInfo.assetId = assetId;
    assetIdCandyInfo.out = out;

    CAmount dbamount = 0;
    CAmount memamount = 0;
    if (!GetGetCandyTotalAmount(assetId, out, dbamount, memamount))
    {
        QString errorStr = tr("Failed to get the number of candy already received");
        Q_EMIT resultReady(false, errorStr, rowNum, uselessArg);
        return;
    }

    CAmount nGetCandyAmount = dbamount + memamount;
    CAmount nNowGetCandyTotalAmount = 0;

    vector<CRecipient> vecSend;
    std::vector<std::string>::iterator addit = vaddress.begin();
    bool bGottenCandy = false;
    for (; addit != vaddress.end(); addit++)
    {
        CAmount nSafe = 0;
        if(!GetAddressAmountByHeight(nTxHeight, *addit, nSafe))
            continue;
        if (nSafe < 1 * COIN || nSafe > nTotalSafe)
            continue;

        CAmount nTempAmount = 0;
        CAmount nCandyAmount = (CAmount)(1.0 * nSafe / nTotalSafe * candyInfo.nAmount);
        CAmount amount;
        if(!parnt->amountFromString("0.0001", parnt->strGetCandy,assetInfo.assetData.nDecimals, amount))
        {
            if (!parnt->msgbox->close())
                parnt->msgbox->hide();

            if (parnt->isUnlockByGlobal)
                parnt->model->setWalletLocked(true);
            parnt->isUnlockByGlobal = false;

            return;
        }

        if (nCandyAmount + nGetCandyAmount > candyInfo.nAmount)
        {
            QString errorStr = tr("The candy has been picked up");
            Q_EMIT resultReady(false, errorStr, rowNum, uselessArg);
            return;
        }
        
        if(nCandyAmount < amount)
            continue;
        if(GetGetCandyAmount(assetId, out, *addit, nTempAmount))
        {
            bGottenCandy = true;
            continue;
        }
        else
            bGottenCandy = false;
        LogPrint("asset", "qt-getcandy: candy-height: %d, address: %s, total_safe: %lld, user_safe: %lld, total_candy_amount: %lld, can_get_candy_amount: %lld, out: %s\n", nTxHeight, *addit, nTotalSafe, nSafe, candyInfo.nAmount, nCandyAmount, out.ToString());
        CBitcoinAddress recvAddress(*addit);
        CRecipient recvRecipient = {GetScriptForDestination(recvAddress.Get()), nCandyAmount, 0, false, true,""};
        vecSend.push_back(recvRecipient);

        nNowGetCandyTotalAmount += nCandyAmount;
    }

    if (nNowGetCandyTotalAmount + nGetCandyAmount > candyInfo.nAmount)
    {
        QString errorStr = tr("The candy has been picked up");
        Q_EMIT resultReady(false, errorStr, rowNum, uselessArg);
        return;
    }

    if(vecSend.size() == 0)
    {
        if(bGottenCandy)
        {
            QString errorStr = tr("You have gotten this candy");
            Q_EMIT resultReady(false, errorStr, rowNum, uselessArg);
        }
        else
        {
            QString errorStr = tr("Invalid candy");
            Q_EMIT resultReady(false, errorStr, uselessArg, uselessArg);
        }
        return;
    }

    unsigned int nTxCount = vecSend.size() / GET_CANDY_TXOUT_SIZE;
    if(vecSend.size() % GET_CANDY_TXOUT_SIZE != 0)
        nTxCount++;

    for(unsigned int i = 0; i < nTxCount; i++)
    {
        vector<CRecipient> vecNowSend;
        if(i != nTxCount - 1)
        {
            for(int m = 0; m < GET_CANDY_TXOUT_SIZE; m++)
                vecNowSend.push_back(vecSend[i * GET_CANDY_TXOUT_SIZE + m]);
        }
        else
        {
            int nCount = vecSend.size() - i * GET_CANDY_TXOUT_SIZE;
            for(int m = 0; m < nCount; m++)
                vecNowSend.push_back(vecSend[i * GET_CANDY_TXOUT_SIZE + m]);
        }

        CWalletTx wtx;
        CReserveKey reservekey(pwalletMain);
        CAmount nFeeRequired;
        int nChangePosRet = -1;
        string strError;
        if(!pwalletMain->CreateAssetTransaction(&appHeader, &assetIdCandyInfo, vecNowSend, NULL, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
        {
            QString errorStr = QString::fromStdString(strError);
            if(nFeeRequired > pwalletMain->GetBalance())
                errorStr = tr("Insufficient safe funds, this transaction requires a transaction fee of at least %1!");
            Q_EMIT resultReady(false, errorStr, uselessArg, nFeeRequired);
            return;
        }

        if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        {
            QString errorStr = tr("Get candy failed, please check your wallet and try again later!");
            Q_EMIT resultReady(false, errorStr, uselessArg, uselessArg);
            return;
        }
    }
    Q_EMIT resultReady(true, tr(""), rowNum, uselessArg);
}

void CandyPage::eraseCandy(int rowNum)
{
    std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
    int index = (nCurrPage-1)*nPageCount+rowNum;
    int tmpSize = tmpAllCandyInfoVec.size();
    if(tmpSize<=index)
    {
        LogPrintf("invalid candy index:%d,tmpAllCandyInfoVec size:%d\n", index,tmpSize);
        return;
    }
    CCandy_BlockTime_Info& tmpInfo = tmpAllCandyInfoVec[index];
    int size = gAllCandyInfoVec.size();
    bool found = false;
    int removeIndex = 0;
    for(int i=0;i<size;i++)
    {
        CCandy_BlockTime_Info& info = gAllCandyInfoVec[i];
        if(tmpInfo.assetId!=info.assetId)
            continue;
        if(tmpInfo.assetData.strAssetName!=info.assetData.strAssetName)
            continue;
        if(tmpInfo.candyinfo.nAmount!=info.candyinfo.nAmount)
            continue;
        if(tmpInfo.candyinfo.nExpired!=info.candyinfo.nExpired)
            continue;
        if(tmpInfo.outpoint.n!=info.outpoint.n)
            continue;
        if(tmpInfo.outpoint.hash!=info.outpoint.hash)
            continue;
        if(tmpInfo.nHeight!=info.nHeight)
            continue;
        if(tmpInfo.blocktime!=info.blocktime)
            continue;
        found = true;
        removeIndex = i;
        break;
    }
    if(!found)
    {
        LogPrintf("erase candy not found,height:%d,assetName:%s\n", tmpInfo.nHeight,tmpInfo.assetData.strAssetName);
        updatePage();
        return;
    }
    gAllCandyInfoVec.erase(gAllCandyInfoVec.begin()+removeIndex);
    updatePage();
}

void CandyPage::getCandy()
{
    QPushButton *btn = (QPushButton *) sender();
    WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
    if(encStatus == model->Locked || encStatus == model->UnlockedForMixingOnly)
    {
//        WalletModel::UnlockContext ctx(model->requestUnlock());
//        if(!ctx.isValid())
//            return;
        if (model->getEncryptionStatus() == WalletModel::Locked || model->getEncryptionStatus() == WalletModel::UnlockedForMixingOnly)
        {
            AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
            dlg.setModel(model);
            int iresult = dlg.exec();
            if (iresult == 0)
                return;
            isUnlockByGlobal = true;
        }
        getCandy(btn);
        return;
    }
    getCandy(btn);
}

void CandyPage::updateGetCandyList()
{
    //LOCK2(cs_main, pwalletMain->cs_wallet);
    std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
    int size = gAllCandyInfoVec.size();
    if(size<=0||size>nPageCount)
        return;
    nCurrPage = 1;
    updatePage();
}

void CandyPage::setGetHistoryTabLayout(QVBoxLayout *layout)
{
    ui->tabGetHistory->setLayout(layout);
}

void CandyPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        connect(model,SIGNAL(candyPut(QString,QString,quint8,qint64,quint16,QString,QString,quint32)),this,SLOT(updateCandyListWidget(QString,QString,quint8,qint64,quint16,QString,QString,quint32)));
        connect(model,SIGNAL(candyPutVec()),this,SLOT(updateGetCandyList()));
    }
}

void CandyPage::setModel(WalletModel *model)
{
    this->model = model;
}

void CandyPage::updateAssetsInfo()
{
    disconnect(ui->assetsComboBox,SIGNAL(currentTextChanged(QString)),this,SLOT(updateCandyInfo(QString)));
    std::map<uint256, CAssetData> issueAssetMap;
    std::vector<std::string> assetNameVec;
    if(GetIssueAssetInfo(issueAssetMap))
    {
        for(std::map<uint256, CAssetData>::iterator iter = issueAssetMap.begin();iter != issueAssetMap.end();++iter)
        {
            uint256 assetId = (*iter).first;
            CAssetData& assetData = (*iter).second;

            int memoryPutCandyCount = mempool.get_PutCandy_count(assetId);
            int dbPutCandyCount = 0;
            map<COutPoint, CCandyInfo> mapCandyInfo;
            if(GetAssetIdCandyInfo(assetId, mapCandyInfo))
                dbPutCandyCount = mapCandyInfo.size();
            int putCandyCount = memoryPutCandyCount + dbPutCandyCount;
            if(putCandyCount<MAX_PUTCANDY_VALUE){
                assetNameVec.push_back(assetData.strAssetName);
            }
        }
        std::sort(assetNameVec.begin(),assetNameVec.end());
    }

    QString currText = ui->assetsComboBox->currentText();
    ui->assetsComboBox->clear();

    QStringList stringList;
    for(unsigned int i=0;i<assetNameVec.size();i++)
    {
        QString assetName = QString::fromStdString(assetNameVec[i]);
        stringList.append(assetName);
    }

    stringListModel->setStringList(stringList);
    completer->setModel(stringListModel);
    completer->popup()->setStyleSheet("font: 12px;");
    ui->assetsComboBox->addItems(stringList);
    ui->assetsComboBox->setCompleter(completer);

    if(stringList.contains(currText))
        ui->assetsComboBox->setCurrentText(currText);
    updateCandyInfo(ui->assetsComboBox->currentText());
    connect(ui->assetsComboBox,SIGNAL(currentTextChanged(QString)),this,SLOT(updateCandyInfo(QString)));
}

bool CandyPage::amountFromString(const std::string &valueStr, const QString &msgboxTitle, int decimal, CAmount &amount)
{
    if (!ParseFixedPoint(valueStr, decimal, &amount))
    {
        QMessageBox::warning(this, msgboxTitle,tr("Invalid amount"),tr("Ok"));
        return false;
    }
    if (!AssetsRange(amount))
    {
        QMessageBox::warning(this, msgboxTitle,tr("Amount out of range"),tr("Ok"));
        return false;
    }
    return true;
}

void CandyPage::on_okButton_clicked()
{
    QString commentsText = ui->commentTextEdit->toPlainText();
    int length = commentsText.length();
    if(length>commentsMaxLength){
        QMessageBox::critical(this, tr("Send candy failed"),tr("Remark maximum 500 character."));
        return;
    }
    WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
    if(encStatus == model->Locked || encStatus == model->UnlockedForMixingOnly)
    {
        if(invalidInput())
            return;
        WalletModel::UnlockContext ctx(model->requestUnlock());
        if(!ctx.isValid())
            return;
        if(putCandy())
        {
            QMessageBox::information(this, strPutCandy,tr("Put Candy success!"),tr("Ok"));
            clear();
        }
        return;
    }
    if(putCandy())
    {
        QMessageBox::information(this, strPutCandy,tr("Put Candy success!"),tr("Ok"));
        clear();
    }
}

void CandyPage::updateCandyValue()
{
    char amountStr[64] = "";
#ifdef WIN32
    snprintf(amountStr,sizeof(amountStr),"%lld",currAssetTotalAmount);
#else
    snprintf(amountStr,sizeof(amountStr),"%ld",currAssetTotalAmount);
#endif
    string multiStr = mulstring(amountStr,QString::number(ui->candyRatioSlider->value()).toStdString());
    int addDecimal = 3;//10% is 1,slider max 100 is 2,total 3
    int totalDecimal = currAssetDecimal+addDecimal;
    int size = multiStr.size();
    if( size <= totalDecimal)
        multiStr = "0" + multiStr;
    string resultStr = numtofloatstring(multiStr,totalDecimal);
    ui->candyValueLabel->setText(QString::fromStdString(resultStr.substr(0,resultStr.size()-addDecimal)));
    ui->candyValueLabel->setVisible(true);
}

void CandyPage::updateCandyInfo(const QString &text)
{
    if (!pwalletMain)
        return;
    LOCK(cs_main);
    std::string strAssetName = text.trimmed().toStdString();
    uint256 assetId;
    if(!GetAssetIdByAssetName(strAssetName,assetId)){
        ui->candyValueLabel->setText("");
        return;
    }

    CAssetId_AssetInfo_IndexValue assetInfo;
    if(assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo)){
        ui->candyValueLabel->setText("");
        return;
    }
    currAssetTotalAmount = assetInfo.assetData.nTotalAmount;
    currAssetDecimal = assetInfo.assetData.nDecimals;
    updateCandyValue();
}

void CandyPage::on_candyRatioSlider_valueChanged(int /*value*/)
{
    updateCandyValue();
}

bool CandyPage::invalidInput()
{
    int index = ui->assetsComboBox->findText(ui->assetsComboBox->currentText().trimmed());
    if(ui->assetsComboBox->currentText().toStdString().empty())
    {
        QMessageBox::warning(this, strPutCandy,tr("Please select asset name"),tr("Ok"));
        return true;
    }
    if(index<0)
    {
        QMessageBox::warning(this, strPutCandy,tr("Invalid asset name"),tr("Ok"));
        return true;
    }

    uint16_t nExpired = (uint16_t)ui->expireLineEdit->text().toShort();
    if(nExpired < MIN_CANDYEXPIRED_VALUE || nExpired > MAX_CANDYEXPIRED_VALUE)
    {
        QMessageBox::warning(this, strPutCandy,tr("Invalid candy expired (min: %1, max: %2)").arg(MIN_CANDYEXPIRED_VALUE).arg(MAX_CANDYEXPIRED_VALUE),tr("Ok"));
        return true;
    }
    return false;
}

bool CandyPage::putCandy()
{
    if (!pwalletMain)
        return false;
    LOCK2(cs_main,pwalletMain->cs_wallet);
    if(!masternodeSync.IsBlockchainSynced())
    {
        QMessageBox::warning(this, strPutCandy,tr("Synchronizing block data"),tr("Ok"));
        return false;
    }

    if (!IsStartLockFeatureHeight(g_nChainHeight))
    {
        QMessageBox::warning(this, strPutCandy, tr("This feature is enabled when the block height is %1").arg(g_nProtocolV3Height),tr("Ok"));
        return false;
    }

    std::string strAssetName = ui->assetsComboBox->currentText().trimmed().toStdString();
    uint256 assetId;
    if(ui->assetsComboBox->currentText().toStdString().empty())
    {
        QMessageBox::warning(this, strPutCandy,tr("Please select asset name"),tr("Ok"));
        return false;
    }
    if(!GetAssetIdByAssetName(strAssetName,assetId, false))
    {
        QMessageBox::warning(this, strPutCandy,tr("Invalid asset name"),tr("Ok"));
        return false;
    }

    int memoryPutCandyCount = mempool.get_PutCandy_count(assetId);
    int dbPutCandyCount = 0;
    map<COutPoint, CCandyInfo> mapCandyInfo;
    if(GetAssetIdCandyInfo(assetId, mapCandyInfo))
        dbPutCandyCount = mapCandyInfo.size();
    int putCandyCount = memoryPutCandyCount + dbPutCandyCount;
    if(putCandyCount>=MAX_PUTCANDY_VALUE)
    {
        QMessageBox::warning(this, strPutCandy,tr("Put candy times used up"),tr("Ok"));
        updateAssetsInfo();
        return false;
    }

    CAssetId_AssetInfo_IndexValue assetInfo;
    if(!GetAssetInfoByAssetId(assetId, assetInfo, false))
    {
        QMessageBox::warning(this, strPutCandy,tr("Non-existent asset id"),tr("Ok"));
        return false;
    }

    CBitcoinAddress adminAddress(assetInfo.strAdminAddress);
    if(!IsMine(*pwalletMain, adminAddress.Get()))
    {
        QMessageBox::warning(this, strPutCandy,tr("You are not the admin"),tr("Ok"));
        return false;
    }

    CAmount nPutCandyAmount = 0;
    std::string strCandyAmount = ui->candyValueLabel->text().trimmed().toStdString();
    if(!amountFromString(strCandyAmount,strPutCandy,assetInfo.assetData.nDecimals,nPutCandyAmount))
        return false;

    if(nPutCandyAmount <= 0)
    {
        QMessageBox::warning(this, strPutCandy,tr("Invalid asset candy amount"),tr("Ok"));
        return false;
    }

    uint16_t nExpired = (uint16_t)ui->expireLineEdit->text().toShort();
    if(nExpired < MIN_CANDYEXPIRED_VALUE || nExpired > MAX_CANDYEXPIRED_VALUE)
    {
        QMessageBox::warning(this, strPutCandy,tr("Invalid candy expired (min: %1, max: %2)").arg(MIN_CANDYEXPIRED_VALUE).arg(MAX_CANDYEXPIRED_VALUE),tr("Ok"));
        return false;
    }

    std::string strRemarks = ui->commentTextEdit->toPlainText().trimmed().toStdString();
    if(strRemarks.size() > MAX_REMARKS_SIZE)
    {
        QMessageBox::warning(this, strPutCandy,tr("Invalid remarks"),tr("Ok"));
        return false;
    }

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), PUT_CANDY_CMD);
    CPutCandyData candyData(assetId, nPutCandyAmount, nExpired, strRemarks);

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
    {
        QMessageBox::warning(this, strPutCandy,tr("Peer-to-peer functionality missing or disabled."),tr("Ok"));
        return false;
    }

    if(pwalletMain->GetBalance() <= 0)
    {
        QMessageBox::warning(this, strPutCandy,tr("Insufficient safe funds"),tr("Ok"));
        return false;
    }

    CAmount distributeAmount = pwalletMain->GetBalance(true,&assetId,&adminAddress);
    if(distributeAmount==0)
    {
        QMessageBox::warning(this, strPutCandy,tr("All distribute assets have been used, can not put candy"),tr("Ok"));
        return false;
    }
    int decimal = assetInfo.assetData.nDecimals;
    QString strDistributeAmount = BitcoinUnits::formatWithUnit(decimal,distributeAmount,false,BitcoinUnits::separatorAlways
                                                     ,true,QString::fromStdString(assetInfo.assetData.strAssetUnit));
    if(distributeAmount < nPutCandyAmount)
    {
        QString strPutCandyAmount = BitcoinUnits::formatWithUnit(decimal,nPutCandyAmount,false,BitcoinUnits::separatorAlways
                                                         ,true,QString::fromStdString(assetInfo.assetData.strAssetUnit));
        QMessageBox::warning(this, strPutCandy,tr("You are going put candy:%1,current available assets:%2,can not put candy").arg(strPutCandyAmount).arg(strDistributeAmount),tr("Ok"));
        return false;
    }

    QString strAddAmount = BitcoinUnits::formatWithUnit(decimal,nPutCandyAmount,false,BitcoinUnits::separatorAlways
                                                        ,true,QString::fromStdString(assetInfo.assetData.strAssetUnit));
    QString str = tr("Put candy max %1 times,current can put %2 times.Current distribute assets:%3,you are going put candy:%4,are you sure you wish to put candy?")
            .arg(MAX_PUTCANDY_VALUE).arg(MAX_PUTCANDY_VALUE-putCandyCount).arg(strDistributeAmount).arg(strAddAmount);
    QMessageBox box(QMessageBox::Question,  strPutCandy ,str);
    QPushButton* okButton = box.addButton(tr("Yes"), QMessageBox::YesRole);
    box.addButton(tr("Cancel"),QMessageBox::NoRole);
    box.exec();
    if ((QPushButton*)box.clickedButton() != okButton)
        return false;

    std::vector<CRecipient> vecSend;
    CRecipient assetRecipient = {GetScriptForDestination(CBitcoinAddress(g_strPutCandyAddress).Get()), nPutCandyAmount, 0, false, true,""};
    vecSend.push_back(assetRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    std::string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &candyData, vecSend, NULL, &adminAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        QString errorStr = QString::fromStdString(strError);
        if(nFeeRequired > pwalletMain->GetBalance())
            errorStr = tr("Insufficient safe funds, this transaction requires a transaction fee of at least %1!").arg(QString::fromStdString(FormatMoney(nFeeRequired)));
        QMessageBox::warning(this, strPutCandy,errorStr,tr("Ok"));
        return false;
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
    {
        QMessageBox::warning(this, strPutCandy,tr("Put candy failed, please check your wallet and try again later!"),tr("Ok"));
        return false;
    }

    if(MAX_PUTCANDY_VALUE-putCandyCount==1)
        updateAssetsInfo();
    return true;
}

void CandyPage::copyVec()
{
	{
		tmpAllCandyInfoVec.clear();
		std::vector<CCandy_BlockTime_Info>().swap(tmpAllCandyInfoVec);
	}

    int candyInfoVecSize = gAllCandyInfoVec.size();
    int nCurrentHeight = g_nChainHeight;
    for(int i=0;i<candyInfoVecSize;i++)
    {
        CCandy_BlockTime_Info& info = gAllCandyInfoVec[i];
        int displayHeight = info.nHeight+1;
        if(displayHeight>nCurrentHeight)
            continue;

        if (info.nHeight >= g_nStartSPOSHeight)
        {
            if(info.candyinfo.nExpired * SPOS_BLOCKS_PER_MONTH + info.nHeight  - 3 * SPOS_BLOCKS_PER_DAY < nCurrentHeight)
                continue;
        }
        else
        {
            if (info.candyinfo.nExpired * BLOCKS_PER_MONTH + info.nHeight >= g_nStartSPOSHeight)
            {
                int nSPOSLaveHeight = (info.candyinfo.nExpired * BLOCKS_PER_MONTH + info.nHeight - g_nStartSPOSHeight) * (Params().GetConsensus().nPowTargetSpacing / Params().GetConsensus().nSPOSTargetSpacing);
                int nTrueBlockHeight = g_nStartSPOSHeight + nSPOSLaveHeight;
                if (nTrueBlockHeight < nCurrentHeight)
                    continue;
            }
            else
            {
                if(info.candyinfo.nExpired * BLOCKS_PER_MONTH + info.nHeight < nCurrentHeight)
                    continue;
            }
        }

        tmpAllCandyInfoVec.push_back(info);
    }
}

void CandyPage::updateCurrentPage()
{
    std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
    if(gAllCandyInfoVec.size()<=0)
        return;
    updatePage();
}

void CandyPage::updatePage(bool gotoLastPage)
{
    ui->tableWidgetGetCandy->clearContents();
    copyVec();
    int candyInfoVecSize = tmpAllCandyInfoVec.size();
    if(candyInfoVecSize<=0)
        return;
    int lastPage = candyInfoVecSize / nPageCount;
    if(candyInfoVecSize%nPageCount!=0)
        lastPage++;
    if(gotoLastPage)
        nCurrPage = lastPage;
    if(nCurrPage>lastPage)
        nCurrPage = lastPage;
    int row = 0;

    for(int i=nPageCount*(nCurrPage-1);i<candyInfoVecSize&&i<nPageCount*nCurrPage;i++)
    {
        CCandy_BlockTime_Info& info = tmpAllCandyInfoVec[i];
        QString dateStr = GUIUtil::dateTimeStr(info.blocktime);
        updateCandyListWidget(QString::fromStdString(info.assetId.GetHex()),QString::fromStdString(info.assetData.strAssetName),info.assetData.nDecimals
                              ,info.candyinfo.nAmount,info.candyinfo.nExpired,dateStr,QString::fromStdString(info.outpoint.hash.GetHex()),info.outpoint.n,row);
        row++;
    }
    ui->labelTotal->setText(tr("Total %1").arg(QString::number(lastPage)));
    ui->spinBox->setValue(nCurrPage);
}

void CandyPage::on_firstBtn_clicked()
{
    std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
    if(gAllCandyInfoVec.size()<=0)
        return;
    nCurrPage = 1;
    updatePage();
}

void CandyPage::on_nextBtn_clicked()
{
    std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
    if(gAllCandyInfoVec.size()<=0)
        return;
    nCurrPage++;
    updatePage();
}

void CandyPage::on_priorBtn_clicked()
{
    std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
    if(gAllCandyInfoVec.size()<=0)
        return;
    if(nCurrPage==1)
        return;
    nCurrPage--;
    updatePage();
}

void CandyPage::on_lastBtn_clicked()
{
    std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
    if(gAllCandyInfoVec.size()<=0)
        return;
    updatePage(true);
}

void CandyPage::on_skipBtn_clicked()
{
    std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
    if(gAllCandyInfoVec.size()<=0)
        return;
    nCurrPage = ui->spinBox->value();
    updatePage();
}

void CandyPage::on_spinBox_editingFinished()
{
    on_skipBtn_clicked();
}

void CandyPage::handlerExpireLineEditTextChange(const QString &text)
{
    if(text.toInt() == 0)
        ui->expireLineEdit->clear();
}
