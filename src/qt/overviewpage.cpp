// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2018 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "init.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "utilitydialog.h"
#include "walletmodel.h"

#include "instantx.h"
#include "darksendconfig.h"
#include "masternode-sync.h"
#include "privatesend-client.h"
#include "overviewentry.h"
#include "assetsdistributerecordmodel.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QScrollBar>
#include <QToolButton>
#include <boost/thread.hpp>

#define ICON_OFFSET 16
#define DECORATION_SIZE 54
#define NUM_ITEMS 5
#define NUM_ITEMS_ADV 7

extern boost::thread_group *g_threadGroup;

CCriticalSection cs_overview;

void RefreshOverviewPageData(WalletModel* walletModel,OverviewPage* overviewPage)
{
    RenameThread("RefreshCandyPageData");
    while(true)
    {
        boost::this_thread::interruption_point();
        bool fThreadUpdateData = overviewPage->getThreadUpdateData();
        bool fThreadNoticeSlot = overviewPage->getThreadNoticeSlot();
        if(!fThreadUpdateData&&!fThreadNoticeSlot)
        {
            MilliSleep(100);
            continue;
        }

        if(fThreadUpdateData)
            overviewPage->setThreadUpdateData(false);
        if(fThreadNoticeSlot)
            overviewPage->setThreadNoticeSlot(false);
        {
            LOCK(cs_overview);
            QStringList assetsNames;
            //tranfer asset,get candy will recv new assets
            walletModel->getAssetsNames(false,assetsNames);
            overviewPage->setAssetStringList(assetsNames);
            if(overviewPage->assetToUpdate.isEmpty())
            {
                //update all asset
                for(int i=0;i<assetsNames.size();i++)
                {
                    boost::this_thread::interruption_point();
                    QString strAssetName = assetsNames[i];
                    AssetBalance& assetBalance = overviewPage->assetBalanceMap[strAssetName];
                    if(!overviewPage->getCurrAssetInfoByName(strAssetName,assetBalance.amount,assetBalance.unconfirmAmount,assetBalance.lockedAmount,
                                                                   assetBalance.nDecimals,assetBalance.strUnit))
                        continue;
                }
            }


            //update some asset
            for(int i=0;i<overviewPage->assetToUpdate.size();i++)
            {
                boost::this_thread::interruption_point();
                QString strAssetName = overviewPage->assetToUpdate[i];
                AssetBalance assetBalance;
                if(!overviewPage->getCurrAssetInfoByName(strAssetName,assetBalance.amount,assetBalance.unconfirmAmount,assetBalance.lockedAmount,
                                           assetBalance.nDecimals,assetBalance.strUnit))
                {
                    overviewPage->setThreadUpdateData(fThreadUpdateData);
                    overviewPage->setThreadNoticeSlot(fThreadNoticeSlot);
                    continue;
                }

                overviewPage->assetToUpdate.remove(i);
                overviewPage->assetBalanceMap[strAssetName] = assetBalance;
                overviewPage->setUpdateAssetsInfo(true);
            }

            if(fThreadNoticeSlot)
                Q_EMIT overviewPage->refreshAssetsInfo();

        }//end of lock
    }//end of while
}

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::SAFE),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        mainRect.moveLeft(ICON_OFFSET);
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace - ICON_OFFSET, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::floorWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
#include "overviewpage.moc"

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentLockedBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    currentWatchLockedBalance(-1),
    txdelegate(new TxViewDelegate(platformStyle, this)),
    timer(nullptr),
    platformStyle(platformStyle),
    fThreadUpdateData(false),
    fThreadNoticeSlot(false),
    fUpdateAssetInfo(false)
    //columnResizingFixer(0)
{
    ui->setupUi(this);

/*
//remove
    QString theme = GUIUtil::getThemeName();
    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    // Note: minimum height of listTransactions will be set later in updateAdvancedPSUI() to reflect actual settings
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");
*/

/*
 //table view display
    ui->otherBalanceView->setItemDelegate(txdelegate);
    ui->otherBalanceView->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    // Note: minimum height of listTransactions will be set later in updateAdvancedPSUI() to reflect actual settings
    ui->otherBalanceView->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->otherBalanceView, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));
    //ui->otherBalanceView->setText("(" + tr("out of sync") + ")");
*/

    // init "out of sync" warning labels
    //ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelPrivateSendSyncStatus->setText("(" + tr("out of sync") + ")");

    // hide PS frame (helps to preserve saved size)
    // we'll setup and make it visible in updateAdvancedPSUI() later if we are not in litemode
    ui->framePrivateSend->setVisible(false);

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);

    // that's it for litemode
    if(fLiteMode) return;

    QString theme = GUIUtil::getThemeName();
    // Disable any PS UI for masternode or when autobackup is disabled or failed for whatever reason
    if(fMasterNode || nWalletBackups <= 0){
        DisablePrivateSendCompletely();
        if (nWalletBackups <= 0) {
            ui->labelPrivateSendEnabled->setToolTip(tr("Automatic backups are disabled, no mixing available!"));
        }
    } else {
        if(!privateSendClient.fEnablePrivateSend)
        {
            ui->togglePrivateSend->setText(tr("Start Mixing"));
            ui->togglePrivateSend->setIcon(QIcon(QPixmap(":/icons/" + theme + "/start")));
        } else {
            ui->togglePrivateSend->setText(tr("Stop Mixing"));
            ui->togglePrivateSend->setIcon(QIcon(QPixmap(":/icons/" + theme + "/stop")));
        }
        // Disable privateSendClient builtin support for automatic backups while we are in GUI,
        // we'll handle automatic backups and user warnings in privateSendStatus()
        privateSendClient.fCreateAutoBackups = false;

        timer = new QTimer(this);
        connect(timer, SIGNAL(timeout()), this, SLOT(privateSendStatus()));
        timer->start(1000);
    }
    QPalette pal(palette());
    pal.setColor(QPalette::Background, Qt::white);
    setAutoFillBackground(true);
    setPalette(pal);
    //ui->OverviewScrollArea->setStyleSheet("background-color:#FFFFFF;");

    pal.setColor(QPalette::Background,Qt::blue);

    updateToolBtnIcon(ui->labelAvailable,theme,"/available");
    updateToolBtnIcon(ui->labelPending,theme,"/pending");
    updateToolBtnIcon(ui->labelImmature,theme,"/immature");
    updateToolBtnIcon(ui->labelLocked,theme,"/locked");
    updateToolBtnIcon(ui->labelTotal,theme,"/total");
    ui->labelAlerts->setVisible(false);
    setMouseTracking(true);
    ui->frame->setMouseTracking(true);
    ui->frameMix->setMouseTracking(true);
    ui->frameOtherAssets->setMouseTracking(true);
    ui->framePrivateSend->setMouseTracking(true);
    ui->scrollAreaWidgetContents->setMouseTracking(true);
}

void OverviewPage::updateToolBtnIcon(QToolButton *btn, const QString &theme, const QString &iconName)
{
    int iconWidth = 20,iconHeight = 25;
    QPixmap pixmap(":/icons/" + theme + iconName);
    pixmap = pixmap.scaled(QSize(iconWidth,iconHeight),Qt::KeepAspectRatio);
    btn->setIcon(QIcon(pixmap));
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    if(timer) disconnect(timer, SIGNAL(timeout()), this, SLOT(privateSendStatus()));
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& lockedBalance, const CAmount& anonymizedBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance, const CAmount& watchLockedBalance)
{
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentLockedBalance = lockedBalance;
    currentAnonymizedBalance = anonymizedBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    currentWatchLockedBalance = watchLockedBalance;
//    ui->labelBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, balance, false, BitcoinUnits::separatorAlways));
//    ui->labelUnconfirmed->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, immatureBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelLocked->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, lockedBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelAnonymized->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, anonymizedBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, balance + unconfirmedBalance + immatureBalance + lockedBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelWatchAvailable->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelWatchPending->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelWatchImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelWatchLocked->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchLockedBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelWatchTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance + watchLockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelAvailable->setText(tr("Available:")+"\n"+BitcoinUnits::floorWithUnit(nDisplayUnit, balance, false, BitcoinUnits::separatorAlways));
    ui->labelPending->setText(tr("Pending:")+"\n"+BitcoinUnits::floorWithUnit(nDisplayUnit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(tr("Immature:")+"\n"+BitcoinUnits::floorWithUnit(nDisplayUnit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelLocked->setText(tr("Locked:")+"\n"+BitcoinUnits::floorWithUnit(nDisplayUnit, lockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(tr("Total:")+"\n"+BitcoinUnits::floorWithUnit(nDisplayUnit, balance + unconfirmedBalance + immatureBalance + lockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelAnonymized->setText(BitcoinUnits::floorWithUnit(nDisplayUnit, anonymizedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchAvailable->setText(BitcoinUnits::floorWithUnit(nDisplayUnit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::floorWithUnit(nDisplayUnit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::floorWithUnit(nDisplayUnit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchLocked->setText(BitcoinUnits::floorWithUnit(nDisplayUnit, watchLockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::floorWithUnit(nDisplayUnit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance + watchLockedBalance, false, BitcoinUnits::separatorAlways));



    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance

    updatePrivateSendProgress();

    static int cachedTxLocks = 0;

    if(cachedTxLocks != nCompleteTXLocks){
        cachedTxLocks = nCompleteTXLocks;
        //ui->listTransactions->update();
        //ui->otherBalanceView->update();
    }
}

bool OverviewPage::getCurrAssetInfoByName(const QString &strAssetName, CAmount &amount, CAmount &unconfirmAmount, CAmount &lockedAmount, int &nDecimals, QString &strUnit)
{
    strUnit = "SAFE";
    uint256 assetId;
    if(GetAssetIdByAssetName(strAssetName.toStdString(),assetId, false))
    {
        CAssetId_AssetInfo_IndexValue assetsInfo;
        if(GetAssetInfoByAssetId(assetId,assetsInfo, false))
        {
            bool fAssets = true;
            amount = walletModel->getBalance(NULL,true,&assetId);
            unconfirmAmount = walletModel->getUnconfirmedBalance(fAssets,&assetId);
            lockedAmount = walletModel->getLockedBalance(fAssets,&assetId);
            strUnit = QString::fromStdString(assetsInfo.assetData.strAssetUnit);
            nDecimals = assetsInfo.assetData.nDecimals;
            return true;
        }
    }
    return false;
}

void OverviewPage::updateAssetsInfo()
{
    QList<QString> entryList;
    //update existed assets entry
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        boost::this_thread::interruption_point();
        OverViewEntry *entry = qobject_cast<OverViewEntry*>(ui->entries->itemAt(i)->widget());
        QString strEntryAssetName = entry->getAssetName();
        if(!assetBalanceMap.contains(strEntryAssetName))
            continue;
        AssetBalance& assetBalance = assetBalanceMap[strEntryAssetName];
        entry->setAssetsInfo(assetBalance.amount,assetBalance.unconfirmAmount,assetBalance.lockedAmount,assetBalance.nDecimals,assetBalance.strUnit);
        entry->updateAssetsInfo();
        entryList.push_back(strEntryAssetName);
    }

    //insert new assets entry
    QMap<QString,AssetBalance>::iterator iter = assetBalanceMap.begin();
    for(;iter != assetBalanceMap.end();++iter)
    {
        boost::this_thread::interruption_point();
        QString assetName = iter.key();
        if(entryList.contains(assetName))
            continue;

        const AssetBalance& assetBalance = iter.value();
        insertEntry(assetName,assetBalance.amount,assetBalance.unconfirmAmount,assetBalance.lockedAmount,assetBalance.strUnit,assetBalance.nDecimals);
    }
}

void OverviewPage::addAssetToUpdate(QString assetName)
{
    LOCK(cs_overview);
    assetToUpdate.push_back(assetName);
}

OverViewEntry * OverviewPage::insertEntry(const QString assetName, const CAmount &balance, const CAmount &unconfirmedBalance, const CAmount &lockedBalance, const QString &strAssetUnit, int nDecimals, const QString &logoURL)
{
    OverViewEntry *entry = new OverViewEntry(platformStyle, this,assetName,balance,unconfirmedBalance,lockedBalance,strAssetUnit,nDecimals,logoURL);
    entry->setModel(walletModel);
    ui->entries->insertWidget(0,entry);

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
//    qApp->processEvents();
//    QScrollBar* bar = ui->otherAssetsScrollArea->verticalScrollBar();
//    if(bar)
//        bar->setSliderPosition(bar->maximum());

    updateTabsAndLabels();
    return entry;
}

void OverviewPage::add()
{
    insertEntry("name",0,0,0,"SAFE",4,"https://timgsa.baidu.com/timg?image&quality=80&size=b9999_10000&sec=1523869700414&di=da3d30d9949b815be691d8d77e01a6cb&imgtype=0&src=http%3A%2F%2Fpic.paopaoche.net%2Fup%2F2013-4%2F2013414151755725.jpg");
}

void OverviewPage::clear()
{
    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }

    updateTabsAndLabels();
}

void OverviewPage::updateTabsAndLabels()
{
 //   setupTabChain(0);
//    coinControlUpdateLabels();
}

QWidget * OverviewPage::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        OverViewEntry *entry = qobject_cast<OverViewEntry*>(ui->entries->itemAt(i)->widget());
        if(entry){
            prev = entry->setupTabChain(prev);
        }
    }
    //QWidget::setTabOrder(prev, ui->sendButton);
    //QWidget::setTabOrder(ui->sendButton, ui->clearButton);
    //QWidget::setTabOrder(ui->clearButton, ui->addButton);
    //return ui->addButton;
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchLocked->setVisible(showWatchOnly);    // show watch-only locked balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly){
        ui->labelWatchImmature->hide();
    }
    else{
//        int indent = 20;
//        ui->labelBalance->setIndent(indent);
//        ui->labelUnconfirmed->setIndent(indent);
//        ui->labelImmature->setIndent(indent);
//        ui->labelImmature->setIndent(indent);
//        ui->labelTotal->setIndent(indent);
    }
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {	
        // update the display unit, to not use the default ("SAFE")
        updateDisplayUnit();
        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(), model->getLockedBalance(), model->getAnonymizedBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance(), model->getWatchLockedBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(privateSendRoundsChanged()), this, SLOT(updatePrivateSendProgress()));
        connect(model->getOptionsModel(), SIGNAL(privateSentAmountChanged()), this, SLOT(updatePrivateSendProgress()));
        connect(model->getOptionsModel(), SIGNAL(advancedPSUIChanged(bool)), this, SLOT(updateAdvancedPSUI(bool)));
        // explicitly update PS frame and transaction list to reflect actual settings
        updateAdvancedPSUI(model->getOptionsModel()->getShowAdvancedPSUI());

        connect(ui->privateSendAuto, SIGNAL(clicked()), this, SLOT(privateSendAuto()));
        connect(ui->privateSendReset, SIGNAL(clicked()), this, SLOT(privateSendReset()));
        connect(ui->privateSendInfo, SIGNAL(clicked()), this, SLOT(privateSendInfo()));
        connect(ui->togglePrivateSend, SIGNAL(clicked()), this, SLOT(togglePrivateSend()));
        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));

        connect(this,SIGNAL(refreshAssetsInfo()),this,SLOT(updateAssetsInfo()));
    }
    if(model && g_threadGroup)
        g_threadGroup->create_thread(boost::bind(&RefreshOverviewPageData,walletModel,this));
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance, currentLockedBalance, currentAnonymizedBalance,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance, currentWatchLockedBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = nDisplayUnit;

        //ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    //this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    //ui->labelWalletStatus->setVisible(fShow);
    ui->labelPrivateSendSyncStatus->setVisible(fShow);
    //ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::updatePrivateSendProgress()
{
    if(!masternodeSync.IsBlockchainSynced() || ShutdownRequested()) return;

    if(!pwalletMain) return;

    QString strAmountAndRounds;
    QString strPrivateSendAmount = BitcoinUnits::formatHtmlWithUnit(nDisplayUnit, privateSendClient.nPrivateSendAmount * COIN, false, BitcoinUnits::separatorAlways);

    if(currentBalance == 0)
    {
        ui->privateSendProgress->setValue(0);
        ui->privateSendProgress->setToolTip(tr("No inputs detected"));

        // when balance is zero just show info from settings
        strPrivateSendAmount = strPrivateSendAmount.remove(strPrivateSendAmount.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = strPrivateSendAmount + " / " + tr("%n Rounds", "", privateSendClient.nPrivateSendRounds);

        ui->labelAmountRounds->setToolTip(tr("No inputs detected"));
        ui->labelAmountRounds->setText(strAmountAndRounds);
        return;
    }

    CAmount nAnonymizableBalance = pwalletMain->GetAnonymizableBalance(false, false);

    CAmount nMaxToAnonymize = nAnonymizableBalance + currentAnonymizedBalance;

    // If it's more than the anon threshold, limit to that.
    if(nMaxToAnonymize > privateSendClient.nPrivateSendAmount*COIN) nMaxToAnonymize = privateSendClient.nPrivateSendAmount*COIN;

    if(nMaxToAnonymize == 0) return;

    if(nMaxToAnonymize >= privateSendClient.nPrivateSendAmount * COIN) {
        ui->labelAmountRounds->setToolTip(tr("Found enough compatible inputs to anonymize %1")
                                          .arg(strPrivateSendAmount));
        strPrivateSendAmount = strPrivateSendAmount.remove(strPrivateSendAmount.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = strPrivateSendAmount + " / " + tr("%n Rounds", "", privateSendClient.nPrivateSendRounds);
    } else {
        QString strMaxToAnonymize = BitcoinUnits::formatHtmlWithUnit(nDisplayUnit, nMaxToAnonymize, false, BitcoinUnits::separatorAlways);
        ui->labelAmountRounds->setToolTip(tr("Not enough compatible inputs to anonymize <span style='color:red;'>%1</span>,<br>"
                                             "will anonymize <span style='color:red;'>%2</span> instead")
                                          .arg(strPrivateSendAmount)
                                          .arg(strMaxToAnonymize));
        strMaxToAnonymize = strMaxToAnonymize.remove(strMaxToAnonymize.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = "<span style='color:red;'>" +
                QString(BitcoinUnits::factor(nDisplayUnit) == 1 ? "" : "~") + strMaxToAnonymize +
                " / " + tr("%n Rounds", "", privateSendClient.nPrivateSendRounds) + "</span>";
    }
    ui->labelAmountRounds->setText(strAmountAndRounds);

    if (!fShowAdvancedPSUI) return;

    CAmount nDenominatedConfirmedBalance;
    CAmount nDenominatedUnconfirmedBalance;
    CAmount nNormalizedAnonymizedBalance;
    float nAverageAnonymizedRounds;

    nDenominatedConfirmedBalance = pwalletMain->GetDenominatedBalance();
    nDenominatedUnconfirmedBalance = pwalletMain->GetDenominatedBalance(true);
    nNormalizedAnonymizedBalance = pwalletMain->GetNormalizedAnonymizedBalance();
    nAverageAnonymizedRounds = pwalletMain->GetAverageAnonymizedRounds();

    // calculate parts of the progress, each of them shouldn't be higher than 1
    // progress of denominating
    float denomPart = 0;
    // mixing progress of denominated balance
    float anonNormPart = 0;
    // completeness of full amount anonymization
    float anonFullPart = 0;

    CAmount denominatedBalance = nDenominatedConfirmedBalance + nDenominatedUnconfirmedBalance;
    denomPart = (float)denominatedBalance / nMaxToAnonymize;
    denomPart = denomPart > 1 ? 1 : denomPart;
    denomPart *= 100;

    anonNormPart = (float)nNormalizedAnonymizedBalance / nMaxToAnonymize;
    anonNormPart = anonNormPart > 1 ? 1 : anonNormPart;
    anonNormPart *= 100;

    anonFullPart = (float)currentAnonymizedBalance / nMaxToAnonymize;
    anonFullPart = anonFullPart > 1 ? 1 : anonFullPart;
    anonFullPart *= 100;

    // apply some weights to them ...
    float denomWeight = 1;
    float anonNormWeight = privateSendClient.nPrivateSendRounds;
    float anonFullWeight = 2;
    float fullWeight = denomWeight + anonNormWeight + anonFullWeight;
    // ... and calculate the whole progress
    float denomPartCalc = ceilf((denomPart * denomWeight / fullWeight) * 100) / 100;
    float anonNormPartCalc = ceilf((anonNormPart * anonNormWeight / fullWeight) * 100) / 100;
    float anonFullPartCalc = ceilf((anonFullPart * anonFullWeight / fullWeight) * 100) / 100;
    float progress = denomPartCalc + anonNormPartCalc + anonFullPartCalc;
    if(progress >= 100) progress = 100;

    ui->privateSendProgress->setValue(progress);

    QString strToolPip = ("<b>" + tr("Overall progress") + ": %1%</b><br/>" +
                          tr("Denominated") + ": %2%<br/>" +
                          tr("Mixed") + ": %3%<br/>" +
                          tr("Anonymized") + ": %4%<br/>" +
                          tr("Denominated inputs have %5 of %n rounds on average", "", privateSendClient.nPrivateSendRounds))
            .arg(progress).arg(denomPart).arg(anonNormPart).arg(anonFullPart)
            .arg(nAverageAnonymizedRounds);
    ui->privateSendProgress->setToolTip(strToolPip);
}

void OverviewPage::updateAdvancedPSUI(bool fShowAdvancedPSUI) {
    this->fShowAdvancedPSUI = fShowAdvancedPSUI;
    int nNumItems = (fLiteMode || !fShowAdvancedPSUI) ? NUM_ITEMS : NUM_ITEMS_ADV;
    SetupTransactionList(nNumItems);

    if (fLiteMode) return;

    ui->framePrivateSend->setVisible(true);
    //ui->labelCompletitionText->setVisible(fShowAdvancedPSUI);
    ui->privateSendProgress->setVisible(fShowAdvancedPSUI);
    ui->labelSubmittedDenomText->setVisible(fShowAdvancedPSUI);
    ui->labelSubmittedDenom->setVisible(fShowAdvancedPSUI);
    ui->privateSendAuto->setVisible(fShowAdvancedPSUI);
    ui->privateSendReset->setVisible(fShowAdvancedPSUI);
    ui->privateSendInfo->setVisible(false);
    ui->labelPrivateSendLastMessage->setVisible(fShowAdvancedPSUI);
}

void OverviewPage::privateSendStatus()
{
    if(!masternodeSync.IsBlockchainSynced() || ShutdownRequested()) return;

    static int64_t nLastDSProgressBlockTime = 0;
    int nBestHeight = clientModel->getNumBlocks();

    // We are processing more then 1 block per second, we'll just leave
    if(((nBestHeight - privateSendClient.nCachedNumBlocks) / (GetTimeMillis() - nLastDSProgressBlockTime + 1) > 1)) return;
    nLastDSProgressBlockTime = GetTimeMillis();

    QString strKeysLeftText(tr("keys left: %1").arg(pwalletMain->nKeysLeftSinceAutoBackup));
    if(pwalletMain->nKeysLeftSinceAutoBackup < PRIVATESEND_KEYS_THRESHOLD_WARNING) {
        strKeysLeftText = "<span style='color:red;'>" + strKeysLeftText + "</span>";
    }
    ui->labelPrivateSendEnabled->setToolTip(strKeysLeftText);

    if (!privateSendClient.fEnablePrivateSend) {
        if (nBestHeight != privateSendClient.nCachedNumBlocks) {
            privateSendClient.nCachedNumBlocks = nBestHeight;
            updatePrivateSendProgress();
        }

        ui->labelPrivateSendLastMessage->setText("");
        ui->togglePrivateSend->setText(tr("Start Mixing"));

        QString strEnabled = tr("Disabled");
        // Show how many keys left in advanced PS UI mode only
        if (fShowAdvancedPSUI) strEnabled += ", " + strKeysLeftText;
        ui->labelPrivateSendEnabled->setText(strEnabled);

        return;
    }

    // Warn user that wallet is running out of keys
    // NOTE: we do NOT warn user and do NOT create autobackups if mixing is not running
    if (nWalletBackups > 0 && pwalletMain->nKeysLeftSinceAutoBackup < PRIVATESEND_KEYS_THRESHOLD_WARNING) {
        QSettings settings;
        if(settings.value("fLowKeysWarning").toBool()) {
            QString strWarn =   tr("Very low number of keys left since last automatic backup!") + "<br><br>" +
                                tr("We are about to create a new automatic backup for you, however "
                                   "<span style='color:red;'> you should always make sure you have backups "
                                   "saved in some safe place</span>!") + "<br><br>" +
                                tr("Note: You turn this message off in options.");
            ui->labelPrivateSendEnabled->setToolTip(strWarn);
            LogPrintf("OverviewPage::privateSendStatus -- Very low number of keys left since last automatic backup, warning user and trying to create new backup...\n");
            QMessageBox::warning(this, tr("PrivateSend"), strWarn, tr("Ok"));
        } else {
            LogPrintf("OverviewPage::privateSendStatus -- Very low number of keys left since last automatic backup, skipping warning and trying to create new backup...\n");
        }

        std::string strBackupWarning;
        std::string strBackupError;
        if(!AutoBackupWallet(pwalletMain, "", strBackupWarning, strBackupError)) {
            if (!strBackupWarning.empty()) {
                // It's still more or less safe to continue but warn user anyway
                LogPrintf("OverviewPage::privateSendStatus -- WARNING! Something went wrong on automatic backup: %s\n", strBackupWarning);

                QMessageBox::warning(this, tr("PrivateSend"),
                    tr("WARNING! Something went wrong on automatic backup") + ":<br><br>" + strBackupWarning.c_str(),
                                     tr("Ok"));
            }
            if (!strBackupError.empty()) {
                // Things are really broken, warn user and stop mixing immediately
                LogPrintf("OverviewPage::privateSendStatus -- ERROR! Failed to create automatic backup: %s\n", strBackupError);

                QMessageBox::warning(this, tr("PrivateSend"),
                    tr("ERROR! Failed to create automatic backup") + ":<br><br>" + strBackupError.c_str() + "<br>" +
                    tr("Mixing is disabled, please close your wallet and fix the issue!"),
                    tr("Ok"));
            }
        }
    }

    QString strEnabled = privateSendClient.fEnablePrivateSend ? tr("Enabled") : tr("Disabled");
    // Show how many keys left in advanced PS UI mode only
    if(fShowAdvancedPSUI) strEnabled += ", " + strKeysLeftText;
    ui->labelPrivateSendEnabled->setText(strEnabled);

    if(nWalletBackups == -1) {
        // Automatic backup failed, nothing else we can do until user fixes the issue manually
        DisablePrivateSendCompletely();

        QString strError =  tr("ERROR! Failed to create automatic backup") + ", " +
                            tr("see debug.log for details.") + "<br><br>" +
                            tr("Mixing is disabled, please close your wallet and fix the issue!");
        ui->labelPrivateSendEnabled->setToolTip(strError);

        return;
    } else if(nWalletBackups == -2) {
        // We were able to create automatic backup but keypool was not replenished because wallet is locked.
        QString strWarning = tr("WARNING! Failed to replenish keypool, please unlock your wallet to do so.");
        ui->labelPrivateSendEnabled->setToolTip(strWarning);
    }

    // check darksend status and unlock if needed
    if(nBestHeight != privateSendClient.nCachedNumBlocks) {
        // Balance and number of transactions might have changed
        privateSendClient.nCachedNumBlocks = nBestHeight;
        updatePrivateSendProgress();
    }

    QString strStatus = QString(privateSendClient.GetStatus().c_str());

    QString s = tr("Last PrivateSend message:") + strStatus;

    if(s != ui->labelPrivateSendLastMessage->text())
        LogPrintf("OverviewPage::privateSendStatus -- Last PrivateSend message: %s\n", strStatus.toStdString());

    ui->labelPrivateSendLastMessage->setText(s);

    if(privateSendClient.nSessionDenom == 0){
        ui->labelSubmittedDenom->setText(tr("N/A"));
    } else {
        QString strDenom(CPrivateSend::GetDenominationsToString(privateSendClient.nSessionDenom).c_str());
        ui->labelSubmittedDenom->setText(strDenom);
    }

}

void OverviewPage::privateSendAuto(){
    privateSendClient.DoAutomaticDenominating(*g_connman);
}

void OverviewPage::privateSendReset(){
    privateSendClient.ResetPool();

    QMessageBox::warning(this, tr("PrivateSend"),
        tr("PrivateSend was successfully reset."),
        tr("Ok"));
}

void OverviewPage::privateSendInfo(){
    HelpMessageDialog dlg(this, HelpMessageDialog::pshelp);
    dlg.exec();
}

void OverviewPage::togglePrivateSend(){
    QSettings settings;
    // Popup some information on first mixing
    QString hasMixed = settings.value("hasMixed").toString();
    if(hasMixed.isEmpty()){
        QMessageBox::information(this, tr("PrivateSend"),
                tr("If you don't want to see internal PrivateSend fees/transactions select \"Most Common\" as Type on the \"Transactions\" tab."),
                tr("Ok"));
        settings.setValue("hasMixed", "hasMixed");
    }
    if(!privateSendClient.fEnablePrivateSend){
        const CAmount nMinAmount = CPrivateSend::GetSmallestDenomination() + CPrivateSend::GetMaxCollateralAmount();
        if(currentBalance < nMinAmount){
            QString strMinAmount(BitcoinUnits::formatWithUnit(nDisplayUnit, nMinAmount));
            QMessageBox::warning(this, tr("PrivateSend"),
                tr("PrivateSend requires at least %1 to use.").arg(strMinAmount),
                tr("Ok"));
            return;
        }

        // if wallet is locked, ask for a passphrase
        if (walletModel->getEncryptionStatus() == WalletModel::Locked)
        {
            WalletModel::UnlockContext ctx(walletModel->requestUnlock(true));
            if(!ctx.isValid())
            {
                //unlock was cancelled
                privateSendClient.nCachedNumBlocks = std::numeric_limits<int>::max();
                QMessageBox::warning(this, tr("PrivateSend"),
                    tr("Wallet is locked and user declined to unlock. Disabling PrivateSend."),
                    tr("Ok"));
                LogPrint("privatesend", "OverviewPage::togglePrivateSend -- Wallet is locked and user declined to unlock. Disabling PrivateSend.\n");
                return;
            }
        }
    }

    privateSendClient.fEnablePrivateSend = !privateSendClient.fEnablePrivateSend;
    privateSendClient.nCachedNumBlocks = std::numeric_limits<int>::max();

    QString theme = GUIUtil::getThemeName();
    if(!privateSendClient.fEnablePrivateSend){
        ui->togglePrivateSend->setText(tr("Start Mixing"));
        ui->togglePrivateSend->setIcon(QIcon(QPixmap(":/icons/" + theme + "/start")));
        privateSendClient.UnfreezeCoins();
    } else {
        ui->togglePrivateSend->setText(tr("Stop Mixing"));
        ui->togglePrivateSend->setIcon(QIcon(QPixmap(":/icons/" + theme + "/stop")));

        /* show darksend configuration if client has defaults set */

        if(privateSendClient.nPrivateSendAmount == 0){
            DarksendConfig dlg(this);
            dlg.setModel(walletModel);
            dlg.exec();
        }

    }
}

void OverviewPage::SetupTransactionList(int nNumItems) {
    //ui->listTransactions->setMinimumHeight(nNumItems * (DECORATION_SIZE + 2));
    //ui->otherBalanceView->setMinimumHeight(nNumItems * (DECORATION_SIZE + 2));

    if(walletModel && walletModel->getOptionsModel()) {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(walletModel->getTransactionTableModel());
        filter->setLimit(nNumItems);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::TransactionColumnStatus, Qt::DescendingOrder);

        //ui->listTransactions->setModel(filter.get());
        //ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        //ui->otherBalanceView->setModel(filter.get());
        //remove header and grid
        //ui->otherBalanceView->horizontalHeader()->setVisible(false);
        //ui->otherBalanceView->setShowGrid(false);

        //initTableView();
    }
}

void OverviewPage::DisablePrivateSendCompletely() {
    ui->togglePrivateSend->setText("(" + tr("Disabled") + ")");
    ui->privateSendAuto->setText("(" + tr("Disabled") + ")");
    ui->privateSendReset->setText("(" + tr("Disabled") + ")");
    ui->framePrivateSend->setEnabled(false);
    if (nWalletBackups <= 0) {
        ui->labelPrivateSendEnabled->setText("<span style='color:red;'>(" + tr("Disabled") + ")</span>");
    }
    privateSendClient.fEnablePrivateSend = false;
}

void OverviewPage::initTableView()
{
    //connect(ui->otherBalanceView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
//    QTableView* tableView = ui->otherBalanceView;

//    tableView->verticalHeader()->hide();
//    tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
//    //tableView->setModel(walletModel->getRecentRequestsTableModel());
//    tableView->setAlternatingRowColors(true);
//    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
//    tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);
//    tableView->setColumnWidth(RecentRequestsTableModel::Date, DATE_COLUMN_WIDTH);
//    tableView->setColumnWidth(RecentRequestsTableModel::Label, LABEL_COLUMN_WIDTH);
//    tableView->setColumnWidth(RecentRequestsTableModel::Message,AMOUNT_MINIMUM_COLUMN_WIDTH);

//    connect(tableView->selectionModel(),SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this,SLOT(recentRequestsView_selectionChanged(QItemSelection, QItemSelection)));
    // Last 2 columns are set by the columnResizingFixer, when the table geometry is ready.,XJTODO
    //columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, AMOUNT_MINIMUM_COLUMN_WIDTH, DATE_COLUMN_WIDTH, this);
}
