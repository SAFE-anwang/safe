// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2019 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "base58.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "timedata.h"

#include "instantx.h"
#include "privatesend.h"
#include "app/app.h"
#include "main.h"
#include "chainparams.h"

#include <stdint.h>
#include <vector>
using std::vector;

#include <boost/foreach.hpp>

extern int g_nChainHeight;

extern int g_nStartSPOSHeight;


bool TRTimeGreaterCompartor(const TransactionRecord& a, const TransactionRecord& b)
{
	if (a.time > b.time)
	{
		return true;
	}

	return false;
}

bool TRTimeLessCompartor(const TransactionRecord& a, const TransactionRecord& b)
{
	if (a.time < b.time)
	{
		return true;
	}

	return false;
}


/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    if (wtx.IsCoinBase())
    {
        // Ensures we show generated coins / mined transactions at depth 1
        if (!wtx.IsInMainChain())
        {
            return false;
        }
    }
    return true;
}

int64_t TransactionRecord::getRealUnlockHeight() const
{
    int64_t nRealUnlockHeight = 0;
    if (nUnlockedHeight)
    {
        if (nVersion>= SAFE_TX_VERSION_3)
        {
            nRealUnlockHeight = nUnlockedHeight;
        }
        else
        {
             if (nTxHeight >=  g_nStartSPOSHeight)
             {
                nRealUnlockHeight = nUnlockedHeight * ConvertBlockNum();
             }
             else
             {
                if (nUnlockedHeight >= g_nStartSPOSHeight)
                {
                    int nSPOSLaveHeight = (nUnlockedHeight - g_nStartSPOSHeight) * ConvertBlockNum();
                    nRealUnlockHeight = g_nStartSPOSHeight + nSPOSLaveHeight;
                }
                else
                    nRealUnlockHeight = nUnlockedHeight;
             }
        }
    }
    return nRealUnlockHeight;
}


void TransactionRecord::addAssetDisplay(const CWalletTx &wtx, const CAssetData &stAssetData, QList<AssetsDisplayInfo> &listMyAsset)
{
	if (qFind(listMyAsset.begin(), listMyAsset.end(), QString::fromStdString(stAssetData.strAssetName)) == listMyAsset.end())
	{
		AssetsDisplayInfo displayInfo;
		displayInfo.strAssetName = QString::fromStdString(stAssetData.strAssetName);
		displayInfo.strAssetsUnit = QString::fromStdString(stAssetData.strAssetUnit);
		displayInfo.bInMainChain = wtx.IsInMainChain();
		listMyAsset.push_back(displayInfo);
	}
}

void TransactionRecord::addIssueAsset(const CAssetData &stAssetData, QList<CAssetData> &listIssueAsset)
{
	if (qFind(listIssueAsset.begin(), listIssueAsset.end(), stAssetData.strAssetName) == listIssueAsset.end())
	{
		listIssueAsset.push_back(stAssetData);
	}
}


//get transfer safe or app or asset data
bool TransactionRecord::decomposeAppAsset(const CWallet *wallet,
    const CWalletTx &wtx,
    TransactionRecord &sub,
    const CTxOut &txout,
	QList<AssetsDisplayInfo> &listMyAsset,
    QList<CAssetData> &listIssueAsset,
    isminetype fAllFromMe)
{
    std::vector<unsigned char> vData;
    if(ParseReserve(txout.vReserve, sub.appHeader, vData))
    {
        if(sub.appHeader.nAppCmd == REGISTER_APP_CMD) // application
        {
			ParseRegisterData(vData, sub.appData);
			sub.assetDebit = 0;
			sub.assetCredit = 0;
			sub.bSAFETransaction = false;
			sub.type = TransactionRecord::SendToAddress;
			sub.bApp = true;
			sub.vtShowType.clear();
			sub.vtShowType.push_back(SHOW_APPLICATION_REGIST);
			return true;            
        }
        else if(sub.appHeader.nAppCmd == ISSUE_ASSET_CMD)//assets
        {
			sub.bAssets = true;
			sub.bIssueAsset = true;
			sub.bSAFETransaction = false;
			sub.assetCredit = 0;
			sub.assetDebit = txout.nValue;
			sub.type = TransactionRecord::FirstDistribute;
			sub.vtShowType.push_back(SHOW_ASSETS_DISTRIBUTE);

            if(ParseIssueData(vData, sub.assetsData))
            {
				addAssetDisplay(wtx, sub.assetsData, listMyAsset);
				addIssueAsset(sub.assetsData, listIssueAsset);
            }

			return true;
        }
        else if(sub.appHeader.nAppCmd == ADD_ASSET_CMD || sub.appHeader.nAppCmd == TRANSFER_ASSET_CMD || sub.appHeader.nAppCmd == DESTORY_ASSET_CMD) // add assets
        {
			sub.bAssets = true;
			sub.bSAFETransaction = false;
			sub.assetCredit = 0;
			sub.assetDebit = -txout.nValue;

            if(sub.appHeader.nAppCmd == TRANSFER_ASSET_CMD&&!fAllFromMe)
            {
                sub.assetDebit = txout.nValue;
            }

			if (sub.appHeader.nAppCmd == ADD_ASSET_CMD)
			{
				sub.assetDebit = txout.nValue;
				sub.type = TransactionRecord::AddDistribute;
				sub.vtShowType.push_back(SHOW_ASSETS_DISTRIBUTE);
			}

            if(ParseCommonData(vData, sub.commonData))
            {
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(GetAssetInfoByAssetId(sub.commonData.assetId,assetInfo))
                {
                    sub.assetsData = assetInfo.assetData;
					addAssetDisplay(wtx, sub.assetsData, listMyAsset);
                }
            }


			return true;
        }
        else if(sub.appHeader.nAppCmd == PUT_CANDY_CMD)
        {
			sub.bPutCandy = true;
			sub.bSAFETransaction = false;
			sub.assetDebit = -txout.nValue;
			sub.assetCredit = 0;
			sub.type = TransactionRecord::PUTCandy;

            if(ParsePutCandyData(vData, sub.putCandyData))
            {
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(GetAssetInfoByAssetId(sub.putCandyData.assetId, assetInfo))
                { 
                    sub.assetsData = assetInfo.assetData;
					addAssetDisplay(wtx, sub.assetsData, listMyAsset);
                }
            }

			return true;
        }
        else if(sub.appHeader.nAppCmd == GET_CANDY_CMD)
        {
			sub.assetCredit = txout.nValue;
			sub.assetDebit = 0;			
			sub.type = TransactionRecord::GETCandy;
			sub.bGetCandy = true;
			sub.bSAFETransaction = false;
			sub.nTxHeight = wtx.nTxHeight;  // GetTxHeight(wtx.GetHash());
			sub.vtShowType.push_back(SHOW_CANDY_TX);

            if(ParseGetCandyData(vData,sub.getCandyData))
            {
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(GetAssetInfoByAssetId(sub.getCandyData.assetId, assetInfo))
                {  
					sub.assetsData = assetInfo.assetData;
					addAssetDisplay(wtx, sub.assetsData, listMyAsset);
                }
            }

			return true;
        }
    }

    return false;
}

bool TransactionRecord::decomposeLockTx(const CWalletTx &wtx, TransactionRecord &sub, const CTxOut &txout,isminetype fAllFromMe)
{
	int nTxHeight = wtx.nTxHeight; // GetTxHeight(wtx.GetHash());
	if (nTxHeight >= txout.nUnlockedHeight)
	{
		return false;
	}

	sub.bLocked = true;
    if(fAllFromMe)
    {
        sub.nLockedAmount = -txout.nValue;
    }
    else
    {
        sub.nLockedAmount = txout.nValue;
    }
	sub.nUnlockedHeight = txout.nUnlockedHeight;
	sub.nTxHeight = nTxHeight;
	sub.updateLockedMonth();
	sub.vtShowType.push_back(SHOW_LOCKED_TX);

	return true;
}

void TransactionRecord::setAddressType(isminetype fAllFromMe, isminetype fAllToMe, const CWalletTx &wtx, TransactionRecord &sub, const CTxOut &txout)
{
	std::map<std::string, std::string> mapValue = wtx.mapValue;

	if (fAllFromMe && fAllToMe)
	{
		CTxDestination address;
		if (ExtractDestination(txout.scriptPubKey, address))
		{
			sub.type = TransactionRecord::SendToSelf;
			sub.address = CBitcoinAddress(address).ToString();
		}
	}
	else if (fAllFromMe)
	{
		CTxDestination address;
		if (ExtractDestination(txout.scriptPubKey, address))
		{
			// Sent to Safe Address
			sub.type = TransactionRecord::SendToAddress;
			sub.address = CBitcoinAddress(address).ToString();
		}
		else
		{
			// Sent to IP, or other non-address transaction like OP_EVAL
			sub.type = TransactionRecord::SendToOther;
			sub.address = mapValue["to"];
		}
	}
	else
	{
		CTxDestination address;
		if (ExtractDestination(txout.scriptPubKey, address))
		{
			// Sent to Safe Address
			sub.type = TransactionRecord::RecvWithAddress;
			sub.address = CBitcoinAddress(address).ToString();
		}
		else
		{
			// Sent to IP, or other non-address transaction like OP_EVAL
			sub.type = TransactionRecord::RecvFromOther;
			sub.address = mapValue["from"];
		}
	}
}

bool TransactionRecord::decomposeAppAssetSafe(const CWallet *wallet, const CWalletTx &wtx, const CTxOut &txout, TransactionRecord &sub, int nOut,const CAmount &nDebit,
                                              isminetype fAllFromMe, isminetype fAllToMe,std::map<std::string, std::string>& mapValue)
{
    if (wallet->IsChange(txout))
    {
        return false;
    }

    if (wallet->IsMine(txout) && txout.IsSafeOnly())
    {
        // Ignore parts sent to self, as this is usually the change
        // from a transaction sent back to our own address.
        return false;
    }

    CAmount nTxFee = nDebit - wtx.GetValueOut();
    sub.idx = nOut;
    sub.involvesWatchAddress = wallet->IsMine(txout) & ISMINE_WATCH_ONLY;
    setAddressType(fAllFromMe, fAllToMe, wtx, sub, txout);

    if (wtx.IsForbid() && !wallet->IsSpent(wtx.GetHash(), nOut))
    {
        sub.bForbidDash = true;
    }

    CAmount nValue = txout.nValue;
    /* Add fee to first output */
    if (nTxFee > 0 && txout.IsSafeOnly())
    {
        nValue += nTxFee;
        nTxFee = 0;
    }
    sub.debit = -nValue;
    return true;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
bool TransactionRecord::decomposeTransaction(const CWallet *wallet,
	const CWalletTx &wtx,
	QList<TransactionRecord> &listTransaction,
	QList<AssetsDisplayInfo> &listMyAsset,
	QList<CAssetData> &listIssueAsset)
{
	int64_t nTime = wtx.GetTxTime();
	CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
	CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
	CAmount nNet = nCredit - nDebit;
	uint256 hash = wtx.GetHash();
	std::map<std::string, std::string> mapValue = wtx.mapValue;

    if (nNet > 0 || wtx.IsCoinBase())
	{
		//
		// Credit
		//
		for (int nIndex = 0; nIndex < wtx.vout.size(); nIndex++)
		{
			const CTxOut& txout = wtx.vout[nIndex];
			isminetype mine = wallet->IsMine(txout);
			if (mine)
            {
                TransactionRecord sub(hash, nTime, wtx.nVersion);

				CTxDestination address;
                sub.idx = nIndex; // sequence number
				sub.credit = txout.nValue;
				sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
				if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address))
				{
					// Received by Safe Address
					sub.type = TransactionRecord::RecvWithAddress;
					sub.address = CBitcoinAddress(address).ToString();
				}
				else
				{
					// Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
					sub.type = TransactionRecord::RecvFromOther;
					sub.address = mapValue["from"];
				}

				if (wtx.IsCoinBase())
				{
					// Generated
					sub.type = TransactionRecord::Generated;
				}

				if (wtx.IsForbid() && !wallet->IsSpent(wtx.GetHash(), nIndex))
				{
					sub.bForbidDash = true;
				}

				if (txout.nUnlockedHeight > 0)
				{
                    decomposeLockTx(wtx, sub, txout,ISMINE_NO);
				}

                listTransaction.append(sub);
			}

			nIndex++;
		}
	}
	else
	{
		bool bSafe = true;
		bool fAllFromMeDenom = true;
		int nFromMe = 0;
		bool involvesWatchAddress = false;
		isminetype fAllFromMe = ISMINE_SPENDABLE;
		BOOST_FOREACH(const CTxIn& txin, wtx.vin)
		{
			if (wallet->IsMine(txin)) {
				fAllFromMeDenom = fAllFromMeDenom && wallet->IsDenominated(txin.prevout);
				nFromMe++;
			}
			isminetype mine = wallet->IsMine(txin);
			if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
			if (fAllFromMe > mine) fAllFromMe = mine;
		}

		isminetype fAllToMe = ISMINE_SPENDABLE;
		bool fAllToMeDenom = true;
		int nToMe = 0;
		BOOST_FOREACH(const CTxOut& txout, wtx.vout) {
			if (wallet->IsMine(txout)) {
				fAllToMeDenom = fAllToMeDenom && wallet->IsDenominatedAmount(txout.nValue);
				nToMe++;
			}
			isminetype mine = wallet->IsMine(txout);
			if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
			if (fAllToMe > mine) fAllToMe = mine;
		}

        bool bAssetAppSafe = false;
		for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
		{
            const CTxOut& txout = wtx.vout[nOut];
            if (txout.IsSafeOnly())
            {
                continue;
            }
            bSafe = false;

            TransactionRecord sub(hash, nTime, wtx.nVersion);
            sub.idx = nOut;
            sub.involvesWatchAddress = wallet->IsMine(txout) & ISMINE_WATCH_ONLY;
            setAddressType(fAllFromMe, fAllToMe, wtx, sub, txout);
            if(decomposeAppAsset(wallet, wtx, sub, txout, listMyAsset, listIssueAsset,fAllFromMe))
            {
                if(sub.appHeader.nAppCmd == ISSUE_ASSET_CMD || sub.appHeader.nAppCmd == REGISTER_APP_CMD)
                {
                    bAssetAppSafe = true;
                }

                if (txout.nUnlockedHeight > 0)
                {
                    decomposeLockTx(wtx, sub, txout,fAllFromMe);
                }

                listTransaction.append(sub);
            }
		}

        if(bAssetAppSafe)
        {
            for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];
                if (!txout.IsSafeOnly())
                {
                    continue;
                }

                TransactionRecord sub(hash, nTime, wtx.nVersion);
                if(decomposeAppAssetSafe(wallet,wtx,txout,sub,nOut,nDebit,fAllFromMe,fAllToMe,mapValue))
                {
                    listTransaction.append(sub);
                }
            }
        }


		if (bSafe)
		{
			if (fAllFromMeDenom && fAllToMeDenom && nFromMe * nToMe) {
				TransactionRecord sub(hash, nTime, TransactionRecord::PrivateSendDenominate, "", -nDebit, nCredit, wtx.nVersion);
				int nOut = 0;
				BOOST_FOREACH(const CTxOut& txout, wtx.vout)
				{
					if (wtx.IsForbid() && !wallet->IsSpent(wtx.GetHash(), nOut))
					{
						sub.bForbidDash = true;
						break;
					}

					nOut++;
				}

				listTransaction.append(sub);
				listTransaction.last().involvesWatchAddress = false;   // maybe pass to TransactionRecord as constructor argument
			}
			else if (fAllFromMe && fAllToMe)
			{
				// Payment to self
				// TODO: this section still not accurate but covers most cases,
				// might need some additional work however
				TransactionRecord sub(hash, nTime, wtx.nVersion);
				// Payment to self by default
				sub.type = TransactionRecord::SendToSelf;
				sub.address = "";

				if (mapValue["DS"] == "1")
				{
					sub.type = TransactionRecord::PrivateSend;
					CTxDestination address;
					if (ExtractDestination(wtx.vout[0].scriptPubKey, address))
					{
						// Sent to Safe Address
						sub.address = CBitcoinAddress(address).ToString();
					}
					else
					{
						// Sent to IP, or other non-address transaction like OP_EVAL
						sub.address = mapValue["to"];
					}

					for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
					{
						const CTxOut& txout = wtx.vout[nOut];
						if (wtx.IsForbid() && !wallet->IsSpent(wtx.GetHash(), nOut))
						{
							sub.bForbidDash = true;
							break;
						}
					}
				}
				else
				{
					for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
					{
						const CTxOut& txout = wtx.vout[nOut];

						sub.idx = nOut;
						if (wallet->IsCollateralAmount(txout.nValue)) sub.type = TransactionRecord::PrivateSendMakeCollaterals;
						if (wallet->IsDenominatedAmount(txout.nValue)) sub.type = TransactionRecord::PrivateSendCreateDenominations;
						if (nDebit - wtx.GetValueOut() == CPrivateSend::GetCollateralAmount()) sub.type = TransactionRecord::PrivateSendCollateralPayment;

						if (txout.nUnlockedHeight > 0)
						{
                            decomposeLockTx(wtx, sub, txout,fAllFromMe);
						}
					}
				}

				CAmount nChange = wtx.GetChange();

				sub.debit = -(nDebit - nChange);
				sub.credit = nCredit - nChange;
				listTransaction.append(sub);
				listTransaction.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
			}
			else if (fAllFromMe)
			{
				//
				// Debit
				//
				CAmount nTxFee = nDebit - wtx.GetValueOut();

				for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
				{
                    const CTxOut& txout = wtx.vout[nOut];

					TransactionRecord sub(hash, nTime, wtx.nVersion);
					sub.idx = nOut;
					sub.involvesWatchAddress = involvesWatchAddress;

					if (wtx.IsForbid() && !wallet->IsSpent(wtx.GetHash(), nOut))
					{
						sub.bForbidDash = true;
					}

					if (wallet->IsMine(txout) && txout.IsSafeOnly())
					{
						// Ignore parts sent to self, as this is usually the change
						// from a transaction sent back to our own address.
						continue;
					}

					CTxDestination address;
					if (ExtractDestination(txout.scriptPubKey, address))
					{
						// Sent to Safe Address
						sub.type = TransactionRecord::SendToAddress;
						sub.address = CBitcoinAddress(address).ToString();
					}
					else
					{
						// Sent to IP, or other non-address transaction like OP_EVAL
						sub.type = TransactionRecord::SendToOther;
						sub.address = mapValue["to"];
					}

					if (mapValue["DS"] == "1")
					{
						sub.type = TransactionRecord::PrivateSend;
					}

					CAmount nValue = txout.nValue;
					/* Add fee to first output */
					if (nTxFee > 0 && txout.IsSafeOnly())
					{
						nValue += nTxFee;
						nTxFee = 0;
					}
					sub.debit = -nValue;

					if (txout.nUnlockedHeight > 0)
					{
                        decomposeLockTx(wtx, sub, txout,fAllFromMe);
					}

					listTransaction.append(sub);
				}
			}
			else
			{
				//
				// Mixed debit transaction, can't break down payees
				//
				TransactionRecord sub(hash, nTime, TransactionRecord::Other, "", nNet, 0, wtx.nVersion);
				if (wtx.IsForbid())
				{
					sub.bForbidDash = true;
				}

				listTransaction.append(sub);
				listTransaction.last().involvesWatchAddress = involvesWatchAddress;
			}
		}
	}

    return listTransaction.size() > 0 ? true : false;
}


bool TransactionRecord::updateStatus(const CWalletTx &wtx)
{
    AssertLockHeld(cs_main);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = NULL;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    bool bFirstMoreThanOneConfirmed = false;
    qint64 lastDepth = status.depth;
    status.depth = wtx.GetDepthInMainChain();
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0)&&status.depth>0;
    //first confirmed
    if(lastDepth==0&&status.depth>0){
        bFirstMoreThanOneConfirmed = true;
    }
    status.cur_num_blocks = chainActive.Height();
    status.cur_num_ix_locks = nCompleteTXLocks;

    TransactionStatus::Status s = status.status;
    if (!CheckFinalTx(wtx))
    {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.nLockTime - chainActive.Height();
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                status.matures_in = wtx.GetBlocksToMaturity();

                // Check if the block was requested by anyone
                if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.isAbandoned())
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    if(s!=status.status&&bFirstMoreThanOneConfirmed)
    {
        return true;
    }
    //last time lock,current unlock
    int64_t realUnlockHeight = getRealUnlockHeight();
    if(nLastLockStatus&&realUnlockHeight<=g_nChainHeight)
    {
        nLastLockStatus = false;
        return true;
    }
    if(realUnlockHeight>g_nChainHeight)
        nLastLockStatus = true;
    return false;
}

bool TransactionRecord::statusUpdateNeeded()
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.cur_num_ix_locks != nCompleteTXLocks;
}

QString TransactionRecord::getTxID() const
{
    return formatSubTxId(hash, idx);
}

QString TransactionRecord::formatSubTxId(const uint256 &hash, int vout)
{
    return QString::fromStdString(hash.ToString() + strprintf("-%03d", vout));
}

void TransactionRecord::updateLockedMonth()
{
    if (nUnlockedHeight > 0)
    {
        int m1 = 0;
        int m2 = 0;
        if (nVersion >= SAFE_TX_VERSION_3)
        {
            m1 = (nUnlockedHeight - nTxHeight) / SPOS_BLOCKS_PER_MONTH;
            m2 = (nUnlockedHeight - nTxHeight) % SPOS_BLOCKS_PER_MONTH;
        }
        else
        {
            if (nTxHeight >= g_nStartSPOSHeight)
            {
                int64_t nTrueUnlockedHeight = nUnlockedHeight * ConvertBlockNum();
                m1 = (nTrueUnlockedHeight - nTxHeight) / SPOS_BLOCKS_PER_MONTH;
                m2 = (nTrueUnlockedHeight - nTxHeight) % SPOS_BLOCKS_PER_MONTH;
            }
            else
            {
                m1 = (nUnlockedHeight - nTxHeight) / BLOCKS_PER_MONTH;
                m2 = (nUnlockedHeight - nTxHeight) % BLOCKS_PER_MONTH;
            }
        }

        if(m2 != 0)
            m1++;
        strLockedMonth = QString::number(m1);
    }
}
