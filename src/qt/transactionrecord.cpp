// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2018 The Safe Core developers
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

bool TransactionRecord::needParseAppAssetData(int appCmd, int showType)
{
    bool ret = false;
    switch (appCmd)
    {
    case REGISTER_APP_CMD:
        if(showType==SHOW_APPLICATION_REGIST||showType==SHOW_TX)
            ret = true;
        break;
    case ISSUE_ASSET_CMD:
        if(showType==SHOW_ASSETS_DISTRIBUTE||showType==SHOW_TX)
            ret = true;
        break;
    case ADD_ASSET_CMD:
        if(showType==SHOW_ASSETS_DISTRIBUTE||showType==SHOW_TX)
            ret = true;
        break;
    case TRANSFER_ASSET_CMD:
        if(showType==SHOW_TX||showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    case DESTORY_ASSET_CMD:
        if(showType==SHOW_TX)
            ret = true;
        break;
    case CHANGE_ASSET_CMD:
        if(showType==SHOW_TX||showType==SHOW_LOCKED_TX)
            ret = true;
        break;
    case PUT_CANDY_CMD:
        if(showType==SHOW_CANDY_TX||showType==SHOW_TX)
            ret = true;
        break;
    case GET_CANDY_CMD:
        if(showType==SHOW_CANDY_TX||showType==SHOW_TX)
            ret = true;
        break;
    default:
        break;
    }
    return ret;
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
bool TransactionRecord::getReserveData(const CWallet *wallet,const CWalletTx &wtx,QList<TransactionRecord> &parts, TransactionRecord &sub, const CTxOut &txout, int showType
                                         , QMap<QString, AssetsDisplayInfo> &assetNamesUnits,CAmount &nAssetDebit,bool getAddress)
{
    if(getAddress)
    {
        CTxDestination dest;
        if(!ExtractDestination(txout.scriptPubKey, dest))
            return false;
        CBitcoinAddress address(dest);
        sub.address = address.ToString();
    }

    std::vector<unsigned char> vData;
    if(ParseReserve(txout.vReserve, sub.appHeader, vData))
    {
        if(sub.appHeader.nAppCmd == TRANSFER_SAFE_CMD)
            ParseTransferSafeData(vData, sub.transferSafeData);

        if(!needParseAppAssetData(sub.appHeader.nAppCmd,showType))
            return false;

        if(sub.appHeader.nAppCmd != TRANSFER_SAFE_CMD)
        {
            sub.idx = parts.size();
            sub.assetDebit = nAssetDebit;
            sub.assetCredit = wallet->GetCredit(txout,ISMINE_ALL,true);
        }

        if(sub.appHeader.nAppCmd == REGISTER_APP_CMD)//application
        {
            if(ParseRegisterData(vData, sub.appData))
            {
                sub.type = TransactionRecord::SendToAddress;
                sub.bApp = true;
                return true;
            }
        }else if(sub.appHeader.nAppCmd == ISSUE_ASSET_CMD)//assets
        {
            sub.bAssets = true;
            sub.bIssueAsset = true;
            if(ParseIssueData(vData, sub.assetsData))
            {
                sub.type = TransactionRecord::FirstDistribute;
                AssetsDisplayInfo& displayInfo = assetNamesUnits[QString::fromStdString(sub.assetsData.strAssetName)];
                if(!displayInfo.bInMainChain)
                    displayInfo.bInMainChain = wtx.IsInMainChain();
                displayInfo.strAssetsUnit = QString::fromStdString(sub.assetsData.strAssetUnit);
                return true;
            }
        }else if((sub.appHeader.nAppCmd == ADD_ASSET_CMD || sub.appHeader.nAppCmd==CHANGE_ASSET_CMD || sub.appHeader.nAppCmd==TRANSFER_ASSET_CMD|| sub.appHeader.nAppCmd==DESTORY_ASSET_CMD)) // add assets
        {
            sub.bAssets = true;
            sub.bSAFETransaction = false;
            if(ParseCommonData(vData, sub.commonData))
            {
                if(sub.appHeader.nAppCmd == ADD_ASSET_CMD)
                    sub.type = TransactionRecord::AddDistribute;
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(GetAssetInfoByAssetId(sub.commonData.assetId,assetInfo))
                {
                    sub.assetsData = assetInfo.assetData;
                    AssetsDisplayInfo& displayInfo = assetNamesUnits[QString::fromStdString(sub.assetsData.strAssetName)];
                    if(!displayInfo.bInMainChain)
                        displayInfo.bInMainChain = wtx.IsInMainChain();
                    displayInfo.strAssetsUnit = QString::fromStdString(sub.assetsData.strAssetUnit);
                    return true;
                }
            }
        }else if(sub.appHeader.nAppCmd == PUT_CANDY_CMD)
        {
            sub.bPutCandy = true;
            sub.bSAFETransaction = false;
            if(ParsePutCandyData(vData, sub.putCandyData))
            {
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(GetAssetInfoByAssetId(sub.putCandyData.assetId, assetInfo))
                {
                    sub.assetsData = assetInfo.assetData;
                    sub.type = TransactionRecord::PUTCandy;
                    if(showType==SHOW_CANDY_TX)
                        return false;
                    return true;
                }
            }
        }else if(sub.appHeader.nAppCmd == GET_CANDY_CMD)
        {
            if(ParseGetCandyData(vData,sub.getCandyData))
            {
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(GetAssetInfoByAssetId(sub.getCandyData.assetId, assetInfo))
                {
                    sub.assetsData = assetInfo.assetData;
                    AssetsDisplayInfo& displayInfo = assetNamesUnits[QString::fromStdString(sub.assetsData.strAssetName)];
                    if(!displayInfo.bInMainChain)
                        displayInfo.bInMainChain = wtx.IsInMainChain();
                    displayInfo.strAssetsUnit = QString::fromStdString(sub.assetsData.strAssetUnit);
                    sub.type = TransactionRecord::GETCandy;
                    sub.bGetCandy = true;
                    sub.bSAFETransaction = false;
                    return true;
                }
            }
        }
    }
    return false;
}

void TransactionRecord::decomposeToMe(const CWallet *wallet, const CWalletTx &wtx, int showType, QList<TransactionRecord> &parts, int64_t nTime, const uint256 &hash,
                                      std::map<std::string, std::string> &mapValue, QMap<QString, AssetsDisplayInfo> &assetNamesUnits, CAmount &nAssetCredit, CAmount &nAssetDebit)
{
    //
    // Credit
    //
    if(showType == SHOW_TX)
    {
        unsigned i = 0;
        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
        {
            isminetype mine = wallet->IsMine(txout);
            if(mine)
            {
                TransactionRecord sub(hash, nTime, wtx.nVersion);
                CTxDestination address;
                sub.idx = parts.size(); // sequence number
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), i))
                    sub.bForbidDash = true;
                bool getAddress = true;
                if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address))
                {
                    // Received by Safe Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = CBitcoinAddress(address).ToString();
                    getAddress = false;
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                    getAddress = false;
                }
                if (wtx.IsCoinBase())
                {
                    // Generated
                    sub.idx = i;
                    sub.type = TransactionRecord::Generated;
                }
                if(getReserveData(wallet,wtx,parts,sub,txout,showType,assetNamesUnits,nAssetDebit,getAddress))
                    parts.append(sub);
                else if((sub.bSAFETransaction && !wallet->IsChange(txout)) || wtx.IsCoinBase())
                    parts.append(sub);
            }
            i++;
        }
    }else if(showType==SHOW_ASSETS_DISTRIBUTE || showType==SHOW_CANDY_TX)
    {
        for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
        {
            const CTxOut& txout = wtx.vout[nOut];
            TransactionRecord sub(hash, nTime, wtx.nVersion);
            if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut))
                sub.bForbidDash = true;
            if(getReserveData(wallet,wtx,parts,sub,txout,showType,assetNamesUnits,nAssetDebit))
                parts.append(sub);
        }
    }
    else if(showType==SHOW_LOCKED_TX)
    {
        int nTxHeight = GetTxHeight(wtx.GetHash());
        for(unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
        {
            const CTxOut& txout = wtx.vout[nOut];
            if(txout.nUnlockedHeight <= 0)
                continue;
            isminetype mine = wallet->IsMine(txout);
            if(mine)
            {
                TransactionRecord sub(hash, nTime, wtx.nVersion);
                if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut))
                    sub.bForbidDash = true;
                CTxDestination address;
                sub.idx = nOut;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                bool getAddress = true;
                if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address))
                {
                    // Received by Safe Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = CBitcoinAddress(address).ToString();
                    getAddress = false;
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                    getAddress = false;
                }
                if (wtx.IsCoinBase())
                {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }
                sub.bLocked = true;
                sub.nLockedAmount = txout.nValue;
                sub.nUnlockedHeight = txout.nUnlockedHeight;
                sub.nTxHeight = nTxHeight;
                sub.updateLockedMonth();
                if(getReserveData(wallet,wtx,parts,sub,txout,showType,assetNamesUnits,nAssetDebit,getAddress))
                    parts.append(sub);
                else if(sub.bSAFETransaction)
                    parts.append(sub);
            }
        }
    }
}

void TransactionRecord::decomposeFromMeToMe(const CWallet *wallet, const CWalletTx &wtx, int showType, QList<TransactionRecord> &parts, int64_t nTime, const uint256 &hash
                                            , std::map<std::string, std::string> &mapValue, CAmount &nCredit, CAmount &nDebit, CAmount &nAssetCredit, CAmount &nAssetDebit
                                            , bool involvesWatchAddress, QMap<QString, AssetsDisplayInfo> &assetNamesUnits,bool hasAsset)
{
    // Payment to self
    // TODO: this section still not accurate but covers most cases,
    // might need some additional work however

    TransactionRecord sub(hash, nTime, wtx.nVersion);
    // Payment to self by default
    sub.type = TransactionRecord::SendToSelf;
    sub.address = "";
    if(wtx.IsForbid())
        sub.bForbidDash = true;

    if(showType == SHOW_TX)
    {
        if(mapValue["DS"] == "1")
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

            for(unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];
                if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut)){
                    sub.bForbidDash = true;
                    break;
                }
            }
        }
        else
        {
            CAmount totalAssetCredit = 0;
            std::string address = "";
            CAppHeader header;
            vector<unsigned char> vData;

            bool fAllSafeOnly = true;
            for(unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];
                if(txout.IsApp() || txout.IsAsset())
                {
                    fAllSafeOnly = false;
                    break;
                }
            }

            bool fAllCollateralAmount = true;
            if(!wallet->IsCollateralAmount(nDebit))
                fAllCollateralAmount = false;
            else
            {
                for(unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
                {
                    const CTxOut& txout = wtx.vout[nOut];
                    if(!txout.IsSafeOnly())
                        continue;

                    if(!wallet->IsCollateralAmount(txout.nValue))
                    {
                        fAllCollateralAmount = false;
                        break;
                    }
                }
            }

            for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];
                sub.idx = parts.size();
                if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut)){
                    sub.bForbidDash = true;
                }
                if(fAllSafeOnly && txout.IsSafeOnly())
                {
                    if(wallet->IsCollateralAmount(txout.nValue))
                        sub.type = TransactionRecord::PrivateSendMakeCollaterals;
                    if(wallet->IsDenominatedAmount(txout.nValue))
                        sub.type = TransactionRecord::PrivateSendCreateDenominations;
                    if(fAllCollateralAmount && (nDebit - wtx.GetValueOut() == CPrivateSend::GetCollateralAmount()))
                        sub.type = TransactionRecord::PrivateSendCollateralPayment;
                }
                CAmount assetDebit = 0;
                getReserveData(wallet,wtx,parts,sub,txout,showType,assetNamesUnits,assetDebit,true);
                if(txout.IsAsset()&& ParseReserve(txout.vReserve, header, vData) && (header.nAppCmd != CHANGE_ASSET_CMD))//&&!wallet->IsChange(txout)
                    totalAssetCredit+=sub.assetCredit;
                if(nOut==0)
                    address = sub.address;
            }
            sub.assetCredit = totalAssetCredit;
            if(header.nAppCmd == ADD_ASSET_CMD)
            {
                sub.assetDebit = 0;
            }
            else
            {
                //from me to me transaction,result shuld be 0;
                sub.assetDebit = -totalAssetCredit;
            }
            sub.address = address;
        }

        CAmount nChange = wtx.GetChange(hasAsset);

        sub.debit = -(nDebit - nChange);
        sub.credit = nCredit - nChange;
        parts.append(sub);
        parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
    }else if(showType==SHOW_APPLICATION_REGIST  || showType==SHOW_ASSETS_DISTRIBUTE || showType==SHOW_CANDY_TX)
    {
        for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
        {
            const CTxOut& txout = wtx.vout[nOut];
            CAppHeader header;
            std::vector<unsigned char> vData;
            if(ParseReserve(txout.vReserve, header, vData))
            {
                CTxDestination dest;
                if(!ExtractDestination(txout.scriptPubKey, dest))
                    continue;
                TransactionRecord sub(hash, nTime, wtx.nVersion);
                CAmount assetDebit = -nAssetDebit;
                if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut)){
                    sub.bForbidDash = true;
                }
                if(getReserveData(wallet,wtx,parts,sub,txout,showType,assetNamesUnits,assetDebit))
                    parts.append(sub);
            }
        }
    }
    else if(showType==SHOW_LOCKED_TX)
    {
        if(mapValue["DS"] != "1")
        {
            int nTxHeight = GetTxHeight(wtx.GetHash());
            for(unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];
                if(txout.nUnlockedHeight <= 0)
                    continue;
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;
                sub.bLocked = true;
                sub.nLockedAmount = txout.nValue;
                sub.nUnlockedHeight = txout.nUnlockedHeight;
                sub.nTxHeight = nTxHeight;
                sub.updateLockedMonth();
                CAmount assetDebit = -nAssetDebit;
                if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut)){
                    sub.bForbidDash = true;
                }
                if(getReserveData(wallet,wtx,parts,sub,txout,showType,assetNamesUnits,assetDebit))
                    parts.append(sub);
                else if(sub.bSAFETransaction)
                    parts.append(sub);
            }
        }
    }
}

void TransactionRecord::decomposeFromMe(const CWallet *wallet, const CWalletTx &wtx, int showType, QList<TransactionRecord> &parts, int64_t nTime, const uint256 &hash
                                        , std::map<std::string, std::string> &mapValue, CAmount &nDebit, bool involvesWatchAddress, QMap<QString, AssetsDisplayInfo> &assetNamesUnits
                                        , CAmount &nAssetCredit, CAmount &nAssetDebit)
{
    //
    // Debit
    //
    CAmount assetDebit = -nAssetDebit;
    if(showType == SHOW_TX)
    {
        CAmount nTxFee = nDebit - wtx.GetValueOut();

        for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
        {
            const CTxOut& txout = wtx.vout[nOut];
            TransactionRecord sub(hash, nTime, wtx.nVersion);
            if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut)){
                sub.bForbidDash = true;
            }
            sub.idx = parts.size();
            sub.involvesWatchAddress = involvesWatchAddress;
            if(wallet->IsMine(txout))
            {
                // Ignore parts sent to self, as this is usually the change
                // from a transaction sent back to our own address.
                continue;
            }

            bool getAddress = true;
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address))
            {
                // Sent to Safe Address
                sub.type = TransactionRecord::SendToAddress;
                sub.address = CBitcoinAddress(address).ToString();
                getAddress = false;
            }
            else
            {
                // Sent to IP, or other non-address transaction like OP_EVAL
                sub.type = TransactionRecord::SendToOther;
                sub.address = mapValue["to"];
                getAddress = false;
            }

            if(mapValue["DS"] == "1")
            {
                sub.type = TransactionRecord::PrivateSend;
            }

            CAmount nValue = txout.nValue;
            /* Add fee to first output */
            if (nTxFee > 0 && !txout.IsAsset())
            {
                nValue += nTxFee;
                nTxFee = 0;
            }
            sub.debit = -nValue;
            if(getReserveData(wallet,wtx,parts,sub,txout,showType,assetNamesUnits,sub.debit,getAddress))
                parts.append(sub);
            else if(sub.bSAFETransaction)
                parts.append(sub);
        }
    }else if(showType==SHOW_APPLICATION_REGIST  || showType==SHOW_ASSETS_DISTRIBUTE || showType==SHOW_CANDY_TX)
    {
        for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
        {
            const CTxOut& txout = wtx.vout[nOut];
            CAppHeader header;
            std::vector<unsigned char> vData;
            if(ParseReserve(txout.vReserve, header, vData))
            {
                CTxDestination dest;
                if(!ExtractDestination(txout.scriptPubKey, dest))
                    continue;
                TransactionRecord sub(hash, nTime, wtx.nVersion);
                if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut)){
                    sub.bForbidDash = true;
                }
                if(getReserveData(wallet,wtx,parts,sub,txout,showType,assetNamesUnits,assetDebit))
                    parts.append(sub);
            }
        }
    }else if(showType==SHOW_LOCKED_TX)
    {
        if(mapValue["DS"] != "1")
        {
            int nTxHeight = GetTxHeight(wtx.GetHash());
            for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];
                if(txout.nUnlockedHeight <= 0)
                    continue;
                TransactionRecord sub(hash, nTime, wtx.nVersion);
                if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut)){
                    sub.bForbidDash = true;
                }
                sub.idx = parts.size();
                sub.involvesWatchAddress = involvesWatchAddress;
                bool getAddress = true;
                if(wallet->IsMine(txout))
                {
                    sub.type = TransactionRecord::SendToSelf;
                    sub.address = "";
                }
                else
                {
                    CTxDestination address;
                    if (ExtractDestination(txout.scriptPubKey, address))
                    {
                        // Sent to Safe Address
                        sub.type = TransactionRecord::SendToAddress;
                        sub.address = CBitcoinAddress(address).ToString();
                        getAddress = false;
                    }
                    else
                    {
                        // Sent to IP, or other non-address transaction like OP_EVAL
                        sub.type = TransactionRecord::SendToOther;
                        sub.address = mapValue["to"];
                        getAddress = false;
                    }
                }
                sub.bLocked = true;
                sub.nLockedAmount = txout.nValue;
                sub.nUnlockedHeight = txout.nUnlockedHeight;
                sub.nTxHeight = nTxHeight;
                sub.updateLockedMonth();
                if(getReserveData(wallet,wtx,parts,sub,txout,showType,assetNamesUnits,assetDebit))
                    parts.append(sub);
                else if(sub.bSAFETransaction)
                    parts.append(sub);
            }
        }
    }
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx, int showType, QMap<QString, AssetsDisplayInfo> &assetNamesUnits)
{

    bool fHasAssets = false;
    uint256 assetId;
    BOOST_FOREACH(const CTxOut& txout, wtx.vout)
    {
        if(txout.IsAsset())
            fHasAssets = true;
        CAppHeader header;
        std::vector<unsigned char> vData;
        if(ParseReserve(txout.vReserve, header, vData))
        {
            if(header.nAppCmd == ADD_ASSET_CMD || header.nAppCmd==CHANGE_ASSET_CMD || header.nAppCmd==TRANSFER_ASSET_CMD || header.nAppCmd==DESTORY_ASSET_CMD)
            {
                CCommonData commonData;
                if(!ParseCommonData(vData, commonData))
                    continue;
                assetId = commonData.assetId;
                break;
            }else if(header.nAppCmd == GET_CANDY_CMD)
            {
                CGetCandyData getCandyData;
                if(!ParseGetCandyData(vData,getCandyData))
                    continue;
                assetId = getCandyData.assetId;
                break;
            }
        }
    }
    CAmount nAssetCredit = 0;
    CAmount nAssetDebit = 0;
    CAmount nAssetNet = 0;
    if(fHasAssets)
    {
        nAssetCredit = wtx.GetCredit(ISMINE_ALL,true,&assetId);
        nAssetDebit = wtx.GetDebit(ISMINE_ALL,true,&assetId);
        nAssetNet = nAssetCredit - nAssetDebit;
    }

    QList<TransactionRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    if (nNet > 0 || wtx.IsCoinBase())
    {
        decomposeToMe(wallet,wtx,showType,parts,nTime,hash,mapValue,assetNamesUnits,nAssetCredit,nAssetDebit);
    }
    else
    {
        bool fAllFromMeDenom = true;
        int nFromMe = 0;
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        BOOST_FOREACH(const CTxIn& txin, wtx.vin)
        {
            if(wallet->IsMine(txin))
            {
                fAllFromMeDenom = fAllFromMeDenom && wallet->IsDenominated(txin.prevout);
                nFromMe++;
            }
            isminetype mine = wallet->IsMine(txin);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        bool fAllToMeDenom = true;
        int nToMe = 0;
        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
        {
            if(wallet->IsMine(txout)) {
                fAllToMeDenom = fAllToMeDenom && wallet->IsDenominatedAmount(txout.nValue);
                nToMe++;
            }
            isminetype mine = wallet->IsMine(txout);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllToMe > mine) fAllToMe = mine;
        }

        if(fAllFromMeDenom && fAllToMeDenom && nFromMe * nToMe)
        {
            if(showType == SHOW_TX)
            {
                TransactionRecord sub(hash, nTime, TransactionRecord::PrivateSendDenominate, "", -nDebit, nCredit, wtx.nVersion);
                int nOut = 0;
                BOOST_FOREACH(const CTxOut& txout, wtx.vout)
                {
                    if(wtx.IsForbid()&&!wallet->IsSpent(wtx.GetHash(), nOut))
                    {
                        sub.bForbidDash = true;
                        break;
                    }
                    nOut++;
                }
                parts.append(sub);
                parts.last().involvesWatchAddress = false;   // maybe pass to TransactionRecord as constructor argument
            }
        }
        else if (fAllFromMe && fAllToMe)
        {
            decomposeFromMeToMe(wallet,wtx,showType,parts,nTime,hash,mapValue,nCredit,nDebit,nAssetCredit,nAssetDebit,involvesWatchAddress,assetNamesUnits,fHasAssets);
        }
        else if (fAllFromMe)
        {
            decomposeFromMe(wallet,wtx,showType,parts,nTime,hash,mapValue,nDebit,involvesWatchAddress,assetNamesUnits,nAssetCredit,nAssetDebit);
        }else if(nToMe && nAssetNet > 0 && fHasAssets)//assets,candy
        {
            decomposeToMe(wallet,wtx,showType,parts,nTime,hash,mapValue,assetNamesUnits,nAssetCredit,nAssetDebit);
        }
        else
        {
            //
            // Mixed debit transaction, can't break down payees
            //
            if(showType == SHOW_TX)
            {
                TransactionRecord sub(hash, nTime, TransactionRecord::Other, "", nNet, 0, wtx.nVersion);
                if(wtx.IsForbid())
                    sub.bForbidDash = true;
                parts.append(sub);
                parts.last().involvesWatchAddress = involvesWatchAddress;
            }
        }
    }

    return parts;
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
    if(lastDepth==0&&status.depth>0&&status.depth<RecommendedNumConfirmations*2){
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

