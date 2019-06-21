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
#include "wallet/wallet.h"

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



//get transfer safe or app or asset data
bool TransactionRecord::decomposeAppAsset(const CWallet *wallet,
    const CWalletTx &wtx,
    TransactionRecord &sub,
    const CTxOut &txout,
    QMap<QString, AssetsDisplayInfo> &assetNamesUnits)
{
    std::vector<unsigned char> vData;
    if(ParseReserve(txout.vReserve, sub.appHeader, vData))
    {
        if(sub.appHeader.nAppCmd == REGISTER_APP_CMD) // application
        {
            if(ParseRegisterData(vData, sub.appData))
            {
                sub.assetDebit = 0;
                sub.assetCredit = 0;
                sub.type = TransactionRecord::SendToAddress;
                sub.bApp = true;
                sub.vtShowType.clear();
                sub.vtShowType.push_back(SHOW_APPLICATION_REGIST);
                return true;
            }
        }
        else if(sub.appHeader.nAppCmd == ISSUE_ASSET_CMD)//assets
        {
            if(ParseIssueData(vData, sub.assetsData))
            {
                sub.bAssets = true;
                sub.bIssueAsset = true;
                sub.bSAFETransaction = false;
                sub.assetCredit = 0;
                sub.assetDebit = txout.nValue;
                sub.type = TransactionRecord::FirstDistribute;

                AssetsDisplayInfo& displayInfo = assetNamesUnits[QString::fromStdString(sub.assetsData.strAssetName)];
                displayInfo.bInMainChain = wtx.IsInMainChain();
                displayInfo.strAssetsUnit = QString::fromStdString(sub.assetsData.strAssetUnit);
                displayInfo.txHash = wtx.GetHash();

                sub.vtShowType.push_back(SHOW_ASSETS_DISTRIBUTE);
                return true;
            }
        }
        else if(sub.appHeader.nAppCmd == ADD_ASSET_CMD || sub.appHeader.nAppCmd == TRANSFER_ASSET_CMD || sub.appHeader.nAppCmd == DESTORY_ASSET_CMD) // add assets
        {

            if(ParseCommonData(vData, sub.commonData))
            {
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(GetAssetInfoByAssetId(sub.commonData.assetId,assetInfo))
                {
                    sub.bAssets = true;
                    sub.bSAFETransaction = false;
                    sub.assetCredit = 0;
                    sub.assetDebit = -txout.nValue;
                    sub.assetsData = assetInfo.assetData;

                    AssetsDisplayInfo& displayInfo = assetNamesUnits[QString::fromStdString(sub.assetsData.strAssetName)];
                    displayInfo.bInMainChain = wtx.IsInMainChain();
                    displayInfo.strAssetsUnit = QString::fromStdString(sub.assetsData.strAssetUnit);
                    displayInfo.txHash = wtx.GetHash();

                    if (sub.appHeader.nAppCmd == ADD_ASSET_CMD)
                    {
                        sub.assetDebit = txout.nValue;
                        sub.type = TransactionRecord::AddDistribute;
                        sub.vtShowType.push_back(SHOW_ASSETS_DISTRIBUTE);
                    }

                    return true;
                }
            }
        }
        else if(sub.appHeader.nAppCmd == PUT_CANDY_CMD)
        {
            if(ParsePutCandyData(vData, sub.putCandyData))
            {
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(GetAssetInfoByAssetId(sub.putCandyData.assetId, assetInfo))
                {
                    sub.bPutCandy = true;
                    sub.bSAFETransaction = false;
                    sub.assetDebit = -txout.nValue;
                    sub.assetCredit = 0;
                    sub.assetsData = assetInfo.assetData;
                    sub.type = TransactionRecord::PUTCandy;
                    return true;
                }
            }
        }
        else if(sub.appHeader.nAppCmd == GET_CANDY_CMD)
        {
            if(ParseGetCandyData(vData,sub.getCandyData))
            {
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(GetAssetInfoByAssetId(sub.getCandyData.assetId, assetInfo))
                {
                    sub.assetCredit = txout.nValue;
                    sub.assetDebit = 0;
                    sub.assetsData = assetInfo.assetData;

                    AssetsDisplayInfo& displayInfo = assetNamesUnits[QString::fromStdString(sub.assetsData.strAssetName)];
                    displayInfo.bInMainChain = wtx.IsInMainChain();
                    displayInfo.strAssetsUnit = QString::fromStdString(sub.assetsData.strAssetUnit);
                    displayInfo.txHash = wtx.GetHash();

                    sub.type = TransactionRecord::GETCandy;
                    sub.bGetCandy = true;
                    sub.bSAFETransaction = false;
                    sub.nTxHeight = GetTxHeight(wtx.GetHash());
                    sub.vtShowType.push_back(SHOW_CANDY_TX);
                    return true;
                }
            }
        }
    }

    return false;
}

bool TransactionRecord::decomposeLockTx(const CWalletTx &wtx, TransactionRecord &sub, const CTxOut &txout)
{
	int nTxHeight = GetTxHeight(wtx.GetHash());
    if (nTxHeight >= txout.nUnlockedHeight)
    {
        return false;
    }

	sub.bLocked = true;
	sub.nLockedAmount = txout.nValue;
	sub.nUnlockedHeight = txout.nUnlockedHeight;
	sub.nTxHeight = nTxHeight;
	sub.updateLockedMonth();
	sub.vtShowType.push_back(SHOW_LOCKED_TX);

	return true;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
bool TransactionRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx, QList<TransactionRecord> &listTransaction, QMap<QString, AssetsDisplayInfo> &mapAssetInfo)
{
	bool fHasAssets = false;
	uint256 assetId;
	BOOST_FOREACH(const CTxOut& txout, wtx.vout)
	{
		if (txout.IsAsset())
		{
			fHasAssets = true;
		}

		CAppHeader header;
		std::vector<unsigned char> vData;
		if (ParseReserve(txout.vReserve, header, vData))
		{
			if (header.nAppCmd == ADD_ASSET_CMD || header.nAppCmd == CHANGE_ASSET_CMD || header.nAppCmd == TRANSFER_ASSET_CMD || header.nAppCmd == DESTORY_ASSET_CMD)
			{
				CCommonData commonData;
				if (!ParseCommonData(vData, commonData))
					continue;

				assetId = commonData.assetId;
				break;
			}
			else if (header.nAppCmd == GET_CANDY_CMD)
			{
				CGetCandyData getCandyData;
				if (!ParseGetCandyData(vData, getCandyData))
					continue;

				assetId = getCandyData.assetId;
				break;
			}
		}
	}

	CAmount nAssetCredit = 0;
	CAmount nAssetDebit = 0;
	CAmount nAssetNet = 0;
	if (fHasAssets)
	{
		nAssetCredit = wtx.GetCredit(ISMINE_ALL, true, &assetId);
		nAssetDebit = wtx.GetDebit(ISMINE_ALL, true, &assetId);
		nAssetNet = nAssetCredit - nAssetDebit;
	}


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
		int nIndex = 0;
		BOOST_FOREACH(const CTxOut& txout, wtx.vout)
		{
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

                if (wallet->IsChange(txout))
                {
                    continue;
                }

 /*               if (txout.IsApp() || txout.IsAsset())
				{
                    if (!decomposeAppAsset(wallet, wtx, listTransaction, sub, txout, nAssetDebit, nAssetCredit, mapAssetInfo))
                    {
                        continue;
                    }
				}
*/
				if (txout.nUnlockedHeight > 0)
				{
					decomposeLockTx(wtx, sub, txout);
				}

                listTransaction.append(sub);
			}
  /*          else
            {
                if (txout.IsApp() || txout.IsAsset())
                {
                    CTxDestination dest;
                    if(ExtractDestination(txout.scriptPubKey, dest))
                    {
                        CBitcoinAddress address(dest);
                        sub.address = address.ToString();
                    }

                    decomposeAppAsset(wallet, wtx, listTransaction, sub, txout, nAssetDebit, nAssetCredit, mapAssetInfo);

                    listTransaction.append(sub);
                }
            }
*/
			nIndex++;
		}
	}
	else
	{
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

            //int nTotalAssetCredit = 0; XJTODO remove it

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

					if (txout.IsApp() || txout.IsAsset())
					{
                        if (!decomposeAppAsset(wallet, wtx, sub, txout, mapAssetInfo))
                        {
                            continue;
                        }

//						if (txout.IsAsset() && sub.appHeader.nAppCmd != CHANGE_ASSET_CMD)
//						{
//							nTotalAssetCredit += sub.assetCredit;
//						}
					}
					else
					{
						if (wallet->IsCollateralAmount(txout.nValue)) sub.type = TransactionRecord::PrivateSendMakeCollaterals;
						if (wallet->IsDenominatedAmount(txout.nValue)) sub.type = TransactionRecord::PrivateSendCreateDenominations;
						if (nDebit - wtx.GetValueOut() == CPrivateSend::GetCollateralAmount()) sub.type = TransactionRecord::PrivateSendCollateralPayment;
					}

					if (txout.nUnlockedHeight > 0)
					{
						decomposeLockTx(wtx, sub, txout);
					}

                    if(nOut==0)
                    {
                        CTxDestination dest;
                        if(ExtractDestination(txout.scriptPubKey, dest))
                        {
                            CBitcoinAddress address(dest);
                            sub.address = address.ToString();
                        }
                    }
				}
			}
            //XJTODO remove it
//			sub.assetCredit = nTotalAssetCredit;
//			if (sub.appHeader.nAppCmd == ADD_ASSET_CMD)
//			{
//				sub.assetDebit = 0;
//			}
//			else
//			{
//				// from me to me transaction,result shuld be 0;
//				sub.assetDebit = -nTotalAssetCredit;
//			}

			CAmount nChange = wtx.GetChange(fHasAssets);

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

/*                if (wallet->IsChange(txout))
                {
                    continue;
                }
*/

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

				if (txout.IsApp() || txout.IsAsset())
				{
                    if (!decomposeAppAsset(wallet, wtx, sub, txout, mapAssetInfo))
                    {
                        continue;
                    }
				}

				if (txout.nUnlockedHeight > 0)
				{
					decomposeLockTx(wtx, sub, txout);
				}

                listTransaction.append(sub);
			}
		}
        else if (nAssetNet > 0)
        {
            for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];

                if (txout.IsSafeOnly())
                {
                    continue;
                }

                TransactionRecord sub(hash, nTime, wtx.nVersion);
                sub.idx = nOut;
                sub.involvesWatchAddress = wallet->IsMine(txout) & ISMINE_WATCH_ONLY;

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

                if (!decomposeAppAsset(wallet, wtx, sub, txout,mapAssetInfo))
                {
                    continue;
                }

                if (txout.nUnlockedHeight > 0)
                {
                    decomposeLockTx(wtx, sub, txout);
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
