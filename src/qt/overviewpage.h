// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_OVERVIEWPAGE_H
#define BITCOIN_QT_OVERVIEWPAGE_H

#include "amount.h"
#include "guiutil.h"

#include <QWidget>
#include <memory>
#include <QStack>

class ClientModel;
class TransactionFilterProxy;
class TxViewDelegate;
class PlatformStyle;
class WalletModel;
class OverViewEntry;
class QToolButton;

namespace Ui {
    class OverviewPage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class AssetBalance
{
public:
    AssetBalance()
    {
      amount = 0;
      unconfirmAmount = 0;
      lockedAmount = 0;
      nDecimals = 0;
      strUnit = "SAFE";
    }
public:
    CAmount amount;
    CAmount unconfirmAmount;
    CAmount lockedAmount;
    int nDecimals;
    QString strUnit;
};

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~OverviewPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void showOutOfSyncWarning(bool fShow);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);
    OverViewEntry *insertEntry(const QString assetName,const CAmount& balance,const CAmount& unconfirmedBalance,const CAmount& lockedBalance,const QString& strAssetUnit,int nDecimals,const QString& logoURL="");

    void setThreadUpdateData(bool update){fThreadUpdateData = update;}
    void setThreadNoticeSlot(bool notice){fThreadNoticeSlot = notice;}
    void setUpdateAssetsInfo(bool update){fUpdateAssetInfo = update;}
    bool getThreadUpdateData(){return fThreadUpdateData;}
    bool getThreadNoticeSlot(){return fThreadNoticeSlot;}
    bool getUpdateAssetsInfo(){return fUpdateAssetInfo;}

    void setAssetStringList(QStringList stringList){assetNames = stringList;}
    void addAssetToUpdate(QString assetName);
    bool getCurrAssetInfoByName(const QString& strAssetName,CAmount& amount,CAmount& unconfirmAmount,CAmount& lockedAmount,int& nDecimals,QString& strUnit);

public Q_SLOTS:
    void privateSendStatus();
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& lockedAmount, const CAmount& anonymizedBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance, const CAmount& watchLockedBalance);
    void add();
    void clear();
    void updateTabsAndLabels();
    void updateAssetsInfo();

Q_SIGNALS:
    void transactionClicked(const QModelIndex &index);
    void outOfSyncWarningClicked();
    void testRefresh();
    void refreshAssetsInfo();

public:
    QStack<QString> assetToUpdate;
    QMap<QString,AssetBalance> assetBalanceMap;

private:
    QTimer *timer;
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    CAmount currentBalance;
    CAmount currentUnconfirmedBalance;
    CAmount currentImmatureBalance;
    CAmount currentLockedBalance;
    CAmount currentAnonymizedBalance;
    CAmount currentWatchOnlyBalance;
    CAmount currentWatchUnconfBalance;
    CAmount currentWatchImmatureBalance;
    CAmount currentWatchLockedBalance;
    int nDisplayUnit;
    bool fShowAdvancedPSUI;
    bool fUpdateAssetInfo;
    //GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;

    TxViewDelegate *txdelegate;
    const PlatformStyle *platformStyle;
    std::unique_ptr<TransactionFilterProxy> filter;
    bool fThreadUpdateData;
    bool fThreadNoticeSlot;
    QStringList assetNames;

    void SetupTransactionList(int nNumItems);
    void DisablePrivateSendCompletely();
    void initTableView();
    void updateToolBtnIcon(QToolButton* btn,const QString& theme,const QString& iconName);

private Q_SLOTS:
    void togglePrivateSend();
    void privateSendAuto();
    void privateSendReset();
    void privateSendInfo();
    void updateDisplayUnit();
    void updatePrivateSendProgress();
    void updateAdvancedPSUI(bool fShowAdvancedPSUI);
    void handleTransactionClicked(const QModelIndex &index);
    void updateAlerts(const QString &warnings);
    void updateWatchOnlyLabels(bool showWatchOnly);
    void handleOutOfSyncWarningClicks();
};

#endif // BITCOIN_QT_OVERVIEWPAGE_H
