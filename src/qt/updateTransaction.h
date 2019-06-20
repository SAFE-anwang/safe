#ifndef UPDATE_TRANSACTION_H
#define UPDATE_TRANSACTION_H

#include "wallet/wallet.h"
#include <qthread.h>
#include <qmutex.h>
#include <qstring.h>
#include <qlist.h>
#include "overviewpage.h"
#include "transactionrecord.h"
#include <map>
using namespace std;

class NewTxData
{
public:
    QString strHash;
    int nStatus;
    bool bShowTx;
};

class CUpdateTransaction : public QThread
{
    Q_OBJECT

public:
    CUpdateTransaction(QThread* pParent = NULL);

    virtual ~CUpdateTransaction();

    void setWallet(CWallet* pWallet);

    void subscribeToCoreSignals();

    void unsubscribeFromCoreSignals();

    void startMonitor();

    void stopMonitor();

	bool processingQueuedTransactions();

    bool RefreshAssetData(const std::map<uint256, CAssetData>& mapIssueAssetMap);

    bool RefreshCandyPageData(const std::map<uint256, CAssetData>& mapIssueAssetMap);


Q_SIGNALS:
    void updateOverviePage(QMap<QString, AssetBalance> mapAssetBalance);

    void updateAssetPage(QStringList listAsset);

	void updateCandyPage(QStringList listAsset);

	void updateTransactionModel(uint256 hash, QList<TransactionRecord> lsitNew, int status, bool showTransaction);

	void updateAssetTransactionModel(uint256 hash, QList<TransactionRecord> lsitNew, int status, bool showTransaction);

	void updateAppTransactionModel(uint256 hash, QList<TransactionRecord> lsitNew, int status, bool showTransaction);

	void updateCandyTransactionModel(uint256 hash, QList<TransactionRecord> lsitNew, int status, bool showTransaction);

	void updateLockTransactionModel(uint256 hash, QList<TransactionRecord> lsitNew, int status, bool showTransaction);

	void updateAssetDisplayInfo(QMap<QString, AssetsDisplayInfo> mapAssetDisplay);

public Q_SLOTS:

    /* New transaction, or transaction changed status */
    void updateTransaction(const QString& hash, int status, bool showTransaction);

	/* Needed to update fProcessingQueuedTransactions through a QueuedConnection */
	void setProcessingQueuedTransactions(bool value);


protected:
    void run();

private:
    bool RefreshOverviewPageData(const QString& strAssetName);
	

private:
    CWallet* m_pWallet;
    QVector<NewTxData> m_vtNewTx;
    QMutex m_txLock;
    bool m_bIsExit;
	bool m_bProcessingQueuedTransactions;
};


#endif
