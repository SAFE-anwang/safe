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

class WalletModel;

class CUpdateTransaction : public QThread
{
    Q_OBJECT

public:
    CUpdateTransaction(QThread* pParent = NULL);

    virtual ~CUpdateTransaction();

    void subscribeToCoreSignals();

    void unsubscribeFromCoreSignals();

    void startMonitor();

    void stopMonitor();

	bool processingQueuedTransactions();

    bool RefreshAssetData(const QMap<uint256, CAssetData>& mapIssueAsset);

    bool RefreshCandyPageData(const QMap<uint256, CAssetData>& mapIssueAsset);

	bool RefreshOverviewPageData(const QList<QString>& listAssetName);

	void init(const WalletModel *pWalletModel, const CWallet *pWallet);

	void uninit();


Q_SIGNALS:
    void updateOverviePage(QMap<QString, AssetBalance> mapAssetBalance);

    void updateAssetPage(QStringList listAsset);

	void updateCandyPage(QStringList listAsset);

	void updateAssetDisplayInfo(QMap<QString, AssetsDisplayInfo> mapAssetDisplay);

	void updateAllTransaction(const QMap<uint256, QList<TransactionRecord> > &mapDecTransaction, const QMap<uint256, NewTxData> &mapTransactionStatus);


public Q_SLOTS:

    /* New transaction, or transaction changed status */
    void updateTransaction(const QString& hash, int status, bool showTransaction);

	/* Needed to update fProcessingQueuedTransactions through a QueuedConnection */
	void setProcessingQueuedTransactions(bool value);


protected:
    void run();

private:
    CWallet* m_pWallet;
    QVector<NewTxData> m_vtNewTx;
	CCriticalSection m_txLock;
    bool m_bIsExit;
	bool m_bProcessingQueuedTransactions;
	WalletModel *m_pWalletModel;
};


#endif
