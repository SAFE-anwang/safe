// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TRANSACTIONTABLEMODEL_H
#define BITCOIN_QT_TRANSACTIONTABLEMODEL_H

#include "bitcoinunits.h"
#include "transactionrecord.h"
#include "uint256.h"
#include <QAbstractTableModel>
#include <QSet>

class PlatformStyle;
class TransactionTableModel;
class WalletModel;
class LockedTransactionTableModel;
class CandyTableModel;
class AssetsDistributeRecordModel;
class ApplicationsRegistRecordModel;
class CWallet;

// Private implementation
class TransactionTablePriv
{
public:
    TransactionTablePriv(CWallet *wallet, int showType, TransactionTableModel *parent) :
        wallet(wallet),
        showType(showType),
        parent(parent)
    {
    }

    CWallet *wallet;
    int showType;
    TransactionTableModel *parent;

    /* Local cache of wallet.
     * As it is in the same order as the CWallet, by definition
     * this is sorted by sha256.
     */
    QList<TransactionRecord> cachedWallet;

    void refreshWallet();

    /* Update our model of the wallet incrementally, to synchronize our model of the wallet
       with that of the core.

       Call with transaction that was added, removed or changed.
     */
    void updateWallet(const uint256 &hash, int status, bool showTransaction,bool& bUpdateAssets,QString& strAssetName);

    int size();

    TransactionRecord *index(int idx);

    QString describe(TransactionRecord *rec, int unit,bool fAssets=false);

    QString getTxHex(TransactionRecord *rec);
};

/** UI model for the transaction table of a wallet.
 */
class TransactionTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit TransactionTableModel(const PlatformStyle *platformStyle, CWallet* wallet, int showType, WalletModel *parent = 0);
    ~TransactionTableModel();

    enum ColumnIndex {
        TransactionColumnStatus = 0,
        TransactionColumnWatchonly = 1,
        TransactionColumnDate = 2,
        TransactionColumnType = 3,
        TransactionColumnToAddress = 4,
        TransactionColumnAssetsName=5,
        TransactionColumnAmount = 6
    };

    /** Roles to get specific information from a transaction row.
        These are independent of column.
    */
    enum RoleIndex {
        /** Type of transaction */
        TypeRole = Qt::UserRole,
        /** Date and time this transaction was created */
        DateRole,
        /** Watch-only boolean */
        WatchonlyRole,
        /** Watch-only icon */
        WatchonlyDecorationRole,
        /** Long description (HTML format) */
        LongDescriptionRole,
        /** Address of transaction */
        AddressRole,
        /** Label of address related to transaction */
        LabelRole,
        /** Net amount of transaction */
        AmountRole,
        /** Unique identifier */
        TxIDRole,
        /** Transaction hash */
        TxHashRole,
        /** Transaction data, hex-encoded */
        TxHexRole,
        /** Whole transaction as plain text */
        TxPlainTextRole,
        /** Is transaction confirmed? */
        ConfirmedRole,
        /** Formatted amount, without brackets when unconfirmed */
        FormattedAmountRole,
        /** Transaction status (TransactionRecord::Status) */
        StatusRole,
        /** Unprocessed icon */
        RawDecorationRole,
        /** Locked month */
        LockedMonthRole,
        /** Unlocked Height */
        UnlockedHeightRole,
        /** Locked transaction status */
        LockedStatusRole,
        /** Assets name*/
        AssetsNameRole,
        /** Applications Id*/
        ApplicationsIdRole,
        /** Applications Name*/
        ApplicationsNameRole,
        /** Assets Id*/
        AssetsIDRole,
        /** Assets decimal*/
        AssetsDecimalsRole,
        /** Assets Amount*/
        AssetsAmountRole,
        /** Assets Unit*/
        AmountUnitRole,
        /** SAFE Role*/
        SAFERole
    };

    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    virtual QVariant data(const QModelIndex &index, int role) const;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const;
    bool processingQueuedTransactions() { return fProcessingQueuedTransactions; }
    QList<TransactionRecord>& getTransactionRecord();
    QMap<QString,AssetsDisplayInfo>& getAssetsNamesUnits();
    void emitUpdateAsset(bool updateAll,bool bConfirmedNewAssets,const QString& strAssetName);
    void setUpdatingWallet(bool updatingWallet);
    bool getUpdatingWallet();
	bool isRefreshWallet();
	void setRefreshWalletFlag(bool flag);
	void refreshPage();

private:
    CWallet* wallet;
    WalletModel *walletModel;
    QStringList columns;
    TransactionTablePriv *priv;
    bool fProcessingQueuedTransactions;
    const PlatformStyle *platformStyle;
    int showType;
    int columnStatus;
    int columnToAddress;
    int columnAmount;
    bool fUpdatingWallet;

	bool bIsRefreshWallet;
	
	int nUpdateCount;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

    QString lookupAddress(const std::string &address, bool tooltip) const;
    QVariant addressColor(const TransactionRecord *wtx) const;
    QString formatTxStatus(const TransactionRecord *wtx) const;
    QString formatTxDate(const TransactionRecord *wtx) const;
    QString formatTxType(const TransactionRecord *wtx) const;
    QString formatTxToAddress(const TransactionRecord *wtx, bool tooltip) const;
    QString formatTxAmount(const TransactionRecord *wtx, bool showUnconfirmed=true, BitcoinUnits::SeparatorStyle separators=BitcoinUnits::separatorStandard) const;
    QString formatTooltip(const TransactionRecord *rec) const;
    QVariant txStatusDecoration(const TransactionRecord *wtx) const;
    QVariant txWatchonlyDecoration(const TransactionRecord *wtx) const;
    QVariant txAddressDecoration(const TransactionRecord *wtx) const;

    QString formatLockedTxAmount(const TransactionRecord *wtx, bool showUnconfirmed=true, BitcoinUnits::SeparatorStyle separators=BitcoinUnits::separatorStandard) const;
    QString formatLockedMonth(const TransactionRecord *wtx) const;
    QString formatUnlockedHeight(const TransactionRecord *wtx) const;
    QString formatLockedStatus(const TransactionRecord *wtx) const;

    QString formatAssetsDistributeType(const TransactionRecord *wtx) const;
    QString formatAssetsAmount(const TransactionRecord *wtx, bool showUnconfirmed=true, BitcoinUnits::SeparatorStyle separators=BitcoinUnits::separatorStandard) const;
    QString formatAssetsName(const TransactionRecord *wtx)const;
    QString formatAssetsAddress(const TransactionRecord *wtx)const;

    QString formatCandyAmount(const TransactionRecord *wtx, bool showUnconfirmed=true, BitcoinUnits::SeparatorStyle separators=BitcoinUnits::separatorStandard) const;

Q_SIGNALS:
    void updateAssets(int,bool,QString);

public Q_SLOTS:
    /* New transaction, or transaction changed status */
    void updateTransaction(const QString &hash, int status, bool showTransaction);
    void updateConfirmations();
    void updateDisplayUnit();
    /** Updates the column title to "Amount (DisplayUnit)" and emits headerDataChanged() signal for table headers to react. */
    void updateAmountColumnTitle();
    /* Needed to update fProcessingQueuedTransactions through a QueuedConnection */
    void setProcessingQueuedTransactions(bool value) { fProcessingQueuedTransactions = value; }
    void refreshWallet();

    friend class TransactionTablePriv;
    friend class CandyTableModel;
    friend class AssetsDistributeRecordModel;
    friend class ApplicationsRegistRecordModel;
    friend class LockedTransactionTableModel;
};

#endif // BITCOIN_QT_TRANSACTIONTABLEMODEL_H
