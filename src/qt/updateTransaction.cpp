#include "updateTransaction.h"
#include "transactionrecord.h"
#include "validation.h"
#include "txmempool.h"
#include <qdebug.h>


extern CTxMemPool mempool;

// queue notifications to show a non freezing progress dialog e.g. for rescan
struct TransactionNotification {
public:
    TransactionNotification() {}
    TransactionNotification(uint256 hash, ChangeType status, bool showTransaction) : hash(hash), status(status), showTransaction(showTransaction) {}

    void invoke(QObject* tt)
    {
        QString strHash = QString::fromStdString(hash.GetHex());
        qDebug() << "NotifyTransactionChanged: " + strHash + " status= " + QString::number(status);
        QMetaObject::invokeMethod(tt, "updateTransaction", Qt::QueuedConnection,
            Q_ARG(QString, strHash),
            Q_ARG(int, status),
            Q_ARG(bool, showTransaction));
    }

private:
    uint256 hash;
    ChangeType status;
    bool showTransaction;
};


static bool fQueueNotifications = false;
static std::vector< TransactionNotification > vQueueNotifications;

static void NotifyTransactionChanged(CUpdateTransaction *tt, CWallet *wallet, const uint256 &hash, ChangeType status)
{
	// Find transaction in wallet
	std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(hash);
	// Determine whether to show transaction or not (determine this here so that no relocking is needed in GUI thread)
	bool inWallet = mi != wallet->mapWallet.end();

	bool showTransaction = (inWallet && TransactionRecord::showTransaction(mi->second));

	TransactionNotification notification(hash, status, showTransaction);

	if (fQueueNotifications)
	{
		vQueueNotifications.push_back(notification);
		return;
	}
	notification.invoke(tt);
}

static void ShowProgress(CUpdateTransaction *tt, const std::string &title, int nProgress)
{
	if (nProgress == 0)
		fQueueNotifications = true;

	if (nProgress == 100)
	{
		fQueueNotifications = false;
		if (vQueueNotifications.size() > 10) // prevent balloon spam, show maximum 10 balloons
			QMetaObject::invokeMethod(tt, "setProcessingQueuedTransactions", Qt::QueuedConnection, Q_ARG(bool, true));
		for (unsigned int i = 0; i < vQueueNotifications.size(); ++i)
		{
			if (vQueueNotifications.size() - i <= 10)
				QMetaObject::invokeMethod(tt, "setProcessingQueuedTransactions", Qt::QueuedConnection, Q_ARG(bool, false));

			vQueueNotifications[i].invoke(tt);
		}
		std::vector<TransactionNotification >().swap(vQueueNotifications); // clear
	}
}


CUpdateTransaction::CUpdateTransaction(QThread* pParent) : QThread(pParent)
{
    m_pWallet = NULL;
    m_bIsExit = true;
	m_bProcessingQueuedTransactions = false;
}

CUpdateTransaction::~CUpdateTransaction()
{
	uninit();
    stopMonitor();
}


void CUpdateTransaction::setWallet(CWallet* pWallet)
{
    m_pWallet = pWallet;
}

void CUpdateTransaction::subscribeToCoreSignals()
{
    // Connect signals to wallet
    if (m_pWallet != NULL)
	{
        m_pWallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
		m_pWallet->ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
    }
}

void CUpdateTransaction::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    if (m_pWallet != NULL)
	{
        m_pWallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
		m_pWallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
    }
}

void CUpdateTransaction::startMonitor()
{
    stopMonitor();
    m_bIsExit = false;
    start();
}

void CUpdateTransaction::stopMonitor()
{
	m_bIsExit = true;
    if (isRunning())
	{        
        wait();
    }
}

void CUpdateTransaction::updateTransaction(const QString& hash, int status, bool showTransaction)
{
    NewTxData stTxData;

    stTxData.strHash = hash;
    stTxData.nStatus = status;
    stTxData.bShowTx = showTransaction;

	LOCK(m_txLock);
    m_vtNewTx.push_back(stTxData);
}


bool CUpdateTransaction::RefreshOverviewPageData(const QString& strAssetName)
{
    if (strAssetName.isEmpty())
	{
        return false;
    }

    AssetBalance assetBalance;
    uint256 assetId;

    if (GetAssetIdByAssetName(strAssetName.toStdString(), assetId, false)) 
	{
        CAssetId_AssetInfo_IndexValue assetsInfo;
        if (GetAssetInfoByAssetId(assetId, assetsInfo, false))
		{
            assetBalance.amount = m_pWallet->GetBalance(true, &assetId, NULL, true);
            assetBalance.unconfirmAmount = m_pWallet->GetUnconfirmedBalance(true, &assetId, NULL, true);
            assetBalance.lockedAmount = m_pWallet->GetLockedBalance(true, &assetId, NULL, true);
            assetBalance.strUnit = QString::fromStdString(assetsInfo.assetData.strAssetUnit);
            assetBalance.nDecimals = assetsInfo.assetData.nDecimals;

			QMap<QString, AssetBalance> tempAssetBalance;
			tempAssetBalance.insert(strAssetName, assetBalance);
            Q_EMIT updateOverviePage(tempAssetBalance);
            return true;
        }
    }

    return false;
}

bool CUpdateTransaction::RefreshAssetData(const std::map<uint256, CAssetData>& mapIssueAssetMap)
{
    if (mapIssueAssetMap.size()<=0)
	{
        return false;
    }

    std::vector<std::string> assetNameVec;

    for (std::map<uint256, CAssetData>::const_iterator iter = mapIssueAssetMap.begin(); iter != mapIssueAssetMap.end(); ++iter)
	{
        const CAssetData& assetData = (*iter).second;
        assetNameVec.push_back(assetData.strAssetName);
    }

    std::sort(assetNameVec.begin(), assetNameVec.end());

    QStringList stringList;
    for (unsigned int i = 0; i < assetNameVec.size(); i++)
	{
        stringList.append(QString::fromStdString(assetNameVec[i]));
    }

    if (stringList.size() > 0)
	{
        Q_EMIT updateAssetPage(stringList);
        return true;
    }

    return false;
}

bool CUpdateTransaction::RefreshCandyPageData(const std::map<uint256, CAssetData>& mapIssueAssetMap)
{
    if (mapIssueAssetMap.size()<=0)
	{
        return false;
    }

    std::vector<std::string> assetNameVec;

    for (std::map<uint256, CAssetData>::const_iterator iter = mapIssueAssetMap.begin(); iter != mapIssueAssetMap.end(); ++iter)
	{
        uint256 assetId = (*iter).first;
        const CAssetData& assetData = (*iter).second;

        int memoryPutCandyCount = mempool.get_PutCandy_count(assetId);
        int dbPutCandyCount = 0;
        map<COutPoint, CCandyInfo> mapCandyInfo;
        if (GetAssetIdCandyInfo(assetId, mapCandyInfo))
		{
            dbPutCandyCount = mapCandyInfo.size();
        }

        int putCandyCount = memoryPutCandyCount + dbPutCandyCount;
        if (putCandyCount < MAX_PUTCANDY_VALUE)
		{
            assetNameVec.push_back(assetData.strAssetName);
        }
    }
    std::sort(assetNameVec.begin(), assetNameVec.end());

    QStringList stringList;
    for (unsigned int i = 0; i < assetNameVec.size(); i++)
	{
        QString assetName = QString::fromStdString(assetNameVec[i]);
        stringList.append(assetName);
    }
    
	if (stringList.size() > 0)
	{
		Q_EMIT updateCandyPage(stringList);
		return true;
	}

	return false;
}

void CUpdateTransaction::run()
{
	int nMax = 0;

    while (!m_bIsExit)
	{
        QMap<uint256, NewTxData> mapNewTxData;
		
		{
			LOCK(m_txLock);

			if (m_vtNewTx.size() <= 0)
			{
				m_txLock.unlock();
				msleep(1000);
				continue;
			}

			nMax = 500;
			if (m_vtNewTx.size() < 500)
			{
				nMax = m_vtNewTx.size();
			}

			for (int i = 0; i < nMax; i++)
			{
				uint256 updatedHash;
				updatedHash.SetHex(m_vtNewTx[i].strHash.toStdString());
				mapNewTxData.insert(updatedHash, m_vtNewTx[i]);
			}

			m_vtNewTx.erase(m_vtNewTx.begin(), m_vtNewTx.begin() + nMax);
		}

        
		QMap<QString, AssetsDisplayInfo> mapAssetList;
		QMap<uint256, QList<TransactionRecord> > mapListTx;

		{
			std::map<uint256, CWalletTx> mapTempWallet;

			{
				LOCK2(cs_main, m_pWallet->cs_wallet);
				for (QMap<uint256, NewTxData>::iterator itNew = mapNewTxData.begin(); itNew != mapNewTxData.end() && !m_bIsExit; itNew++)
				{
					std::map<uint256, CWalletTx>::iterator mi = m_pWallet->mapWallet.find(itNew.key());
					if (mi != m_pWallet->mapWallet.end())
					{
						mapTempWallet.insert(make_pair(mi->first, mi->second));
					}
				}
			}


			for (QMap<uint256, NewTxData>::iterator itNew = mapNewTxData.begin(); itNew != mapNewTxData.end() && !m_bIsExit; itNew++)
			{
				std::map<uint256, CWalletTx>::iterator mi = mapTempWallet.find(itNew.key());
				if (mi == mapTempWallet.end())
				{
					continue;
				}

				QList<TransactionRecord> listTemp;
				if (!TransactionRecord::decomposeTransaction(m_pWallet, mi->second, listTemp, mapAssetList))
				{
					continue;
				}

				mapListTx.insert(itNew.key(), listTemp);
			}
		}
		

		if (m_bIsExit)
		{
			break;
		}

		
		{
			QList<TransactionRecord> listTx;
			QList<TransactionRecord> listAssetTx;
			QList<TransactionRecord> listAppTx;
			QList<TransactionRecord> listCandyTx;
			QList<TransactionRecord> listLockTx;

			QMap<uint256, QList<TransactionRecord> >::iterator it;

			for (it = mapListTx.begin(); it != mapListTx.end(); it++)
			{
				const QList<TransactionRecord> &listToInsert = it.value();
				for (int i = 0; i < listToInsert.size(); i++)
				{
					for (int j = 0; j < listToInsert[i].vtShowType.size(); j++)
					{
						if (listToInsert[i].vtShowType[j] == SHOW_TX)
						{
							listTx.push_back(listToInsert[i]);
						}
						else if (listToInsert[i].vtShowType[j] == SHOW_ASSETS_DISTRIBUTE)
						{
							listAssetTx.push_back(listToInsert[i]);
						}
						else if (listToInsert[i].vtShowType[j] == SHOW_APPLICATION_REGIST)
						{
							listAppTx.push_back(listToInsert[i]);
						}
						else if (listToInsert[i].vtShowType[j] == SHOW_CANDY_TX)
						{
							listCandyTx.push_back(listToInsert[i]);
						}
						else if (listToInsert[i].vtShowType[j] == SHOW_LOCKED_TX)
						{
							listLockTx.push_back(listToInsert[i]);
						}
					}
				}


				QMap<uint256, NewTxData>::iterator itNew = mapNewTxData.find(it.key());
				if (itNew == mapNewTxData.end())
				{
					continue;
				}

				uint256 hash = itNew.key();
				const NewTxData &stTxData = itNew.value();

				if (listTx.size() > 0)
				{
					Q_EMIT updateTransactionModel(hash, listTx, stTxData.nStatus, stTxData.bShowTx);
				}

				if (listAssetTx.size() > 0)
				{
					Q_EMIT updateAssetTransactionModel(hash, listAssetTx, stTxData.nStatus, stTxData.bShowTx);
				}

				if (listAppTx.size() > 0)
				{
					Q_EMIT updateAppTransactionModel(hash, listAppTx, stTxData.nStatus, stTxData.bShowTx);
				}

				if (listCandyTx.size() > 0)
				{
					Q_EMIT updateCandyTransactionModel(hash, listCandyTx, stTxData.nStatus, stTxData.bShowTx);
				}

				if (listLockTx.size() > 0)
				{
					Q_EMIT updateLockTransactionModel(hash, listLockTx, stTxData.nStatus, stTxData.bShowTx);
				}
				
				msleep(100);
			}
		}


		if (mapAssetList.size() > 0)
		{
			Q_EMIT updateAssetDisplayInfo(mapAssetList);


			QString strAssetName = mapAssetList.begin().key();

			// update overview page
			RefreshOverviewPageData(strAssetName);

			std::map<uint256, CAssetData> mapIssueAsset;
			if (!GetIssueAssetInfo(mapIssueAsset))
			{
				msleep(1000);
				continue;
			}

			// update asset page
			RefreshAssetData(mapIssueAsset);

			// update candy page
			RefreshCandyPageData(mapIssueAsset);
		}
        
		
		msleep(1000);
    }
}

void CUpdateTransaction::setProcessingQueuedTransactions(bool value)
{ 
	m_bProcessingQueuedTransactions = value;
}

bool CUpdateTransaction::processingQueuedTransactions()
{
	return m_bProcessingQueuedTransactions;
}

void CUpdateTransaction::init()
{
	subscribeToCoreSignals();
}

void CUpdateTransaction::uninit()
{
	unsubscribeFromCoreSignals();
}