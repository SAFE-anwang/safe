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

	friend inline bool operator==(const AssetBalance& a, const AssetBalance& b) { return a.strAssetName == b.strAssetName; }

	friend inline bool operator==(const AssetBalance& a, const QString& b) { return a.strAssetName == b; }

	friend inline bool operator==(const QString& a, const AssetBalance& b) { return a == b.strAssetName; }

public:
    CAmount amount;
    CAmount unconfirmAmount;
    CAmount lockedAmount;
    int nDecimals;
    QString strUnit;
	QString strAssetName;
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

    OverViewEntry *insertEntry(const QString assetName,const CAmount& balance,const CAmount& unconfirmedBalance,const CAmount& lockedBalance,const QString& strAssetUnit,int nDecimals,const QString& logoURL="");

    bool getCurrAssetInfoByName(const QString& strAssetName,CAmount& amount,CAmount& unconfirmAmount,CAmount& lockedAmount,int& nDecimals,QString& strUnit);

public Q_SLOTS:
    void privateSendStatus();
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& lockedAmount, const CAmount& anonymizedBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance, const CAmount& watchLockedBalance);
    void add();
    void clear();
    void updateAssetsInfo(const QList<AssetBalance> &listAssetBalance);

Q_SIGNALS:
    void transactionClicked(const QModelIndex &index);
    void outOfSyncWarningClicked();

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
    //GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;

    TxViewDelegate *txdelegate;
    const PlatformStyle *platformStyle;
    std::unique_ptr<TransactionFilterProxy> filter;

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
