// Copyright (c) 2018-2018 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>

#include "app.h"
#include "init.h"
#include "spork.h"
#include "txdb.h"
#include "validation.h"
#include "utilmoneystr.h"
#include "rpc/server.h"
#include "wallet/wallet.h"
#include "script/sign.h"
#include "main.h"
#include "masternode-sync.h"
#include "txmempool.h"
#include <boost/regex.hpp>

using namespace std;

extern std::mutex g_mutexAllCandyInfo;
extern std::vector<CCandy_BlockTime_Info> gAllCandyInfoVec;

void EnsureWalletIsUnlocked();
bool EnsureWalletIsAvailable(bool avoidException);

UniValue issueasset(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 9 || params.size() > 12)
        throw runtime_error(
            "issueasset \"assetShortName\" \"assetName\" \"assetDesc\" \"assetUnit\" assetTotalAmount firstIssueAmount assetDecimals isDestory isPayCandy assetCandyAmount candyExpired \"remarks\"\n"
            "\nIssue asset.\n"
            "\nArguments:\n"
            "1. \"assetShortName\"      (string, required) The asset short name\n"
            "2. \"assetName\"           (string, required) The asset name\n"
            "3. \"assetDesc\"           (string, required) The asset description\n"
            "4. \"assetUnit\"           (string, required) The asset unit\n"
            "5. assetTotalAmount        (numeric, required) The total asset amount\n"
            "6. firstIssueAmount        (numeric, required) The first issued asset amount\n"
            "7. assetDecimals           (numeric, required) The asset deciamls\n"
            "8. isDestory               (bool, required) Can destory asset or not\n"
            "9. isPayCandy              (bool, required) Pay candy or not\n"
            "10. assetCandyAmount       (numeric, optional) The asset's candy amount\n"
            "11. candyExpired           (numeric, optional) The candy's expired month start from issued time\n"
            "12. \"remarks\"            (string, optional) The remarks\n"
            "\nResult:\n"
            "{\n"
            "  \"assetId\": \"xxxxx\",  (string) The asset id\n"
            "  \"txId\": \"xxxxx\"      (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("issueasset", "\"gold\" \"gold\" \"gold is an asset\" \"g\" 100000 30000 4 false false")
            + HelpExampleCli("issueasset", "\"gold\" \"gold\" \"gold is an asset\" \"g\" 100000 30000 4 false false 0 0")
            + HelpExampleCli("issueasset", "\"gold\" \"gold\" \"gold is an asset\" \"g\" 100000 30000 4 false true 10000 1")
            + HelpExampleCli("issueasset", "\"gold\" \"gold\" \"gold is an asset\" \"g\" 100000 30000 4 false true 10000 1 \"This is a test\"")
            + HelpExampleRpc("issueasset", "\"gold\", \"gold\", \"gold is an asset\", \"g\", 100000, 30000, 4, false, true, 10000, 1")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    boost::regex regname("[a-zA-Z\u4e00-\u9fa5][a-zA-Z0-9\u4e00-\u9fa5]{1,20}");
    string strShortName = TrimString(params[0].get_str());
    if(strShortName.empty() || strShortName.size() > MAX_SHORTNAME_SIZE || IsContainSpace(strShortName) || !boost::regex_match(strShortName, regname))
        throw JSONRPCError(INVALID_SHORTNAME_SIZE, "Invalid asset short name");

    if(IsKeyWord(strShortName))
        throw JSONRPCError(INVALID_SHORTNAME_SIZE, "Asset short name is internal reserved words, not allowed to use");

    if(ExistShortName(strShortName))
        throw JSONRPCError(EXISTENT_SHORTNAME, "Existent asset short name");

    string strAssetName = TrimString(params[1].get_str());
    if(strAssetName.empty() || strAssetName.size() > MAX_ASSETNAME_SIZE || !boost::regex_match(strAssetName, regname))
        throw JSONRPCError(INVALID_ASSETNAME_SIZE, "Invalid asset name");

    if(IsKeyWord(strAssetName))
        throw JSONRPCError(INVALID_ASSETNAME_SIZE, "Asset name is internal reserved words, not allowed to use");

    if(ExistAssetName(strAssetName))
        throw JSONRPCError(EXISTENT_ASSETNAME, "Existent asset name");

    string strAssetDesc = TrimString(params[2].get_str());
    if(strAssetDesc.empty() || strAssetDesc.size() > MAX_ASSETDESC_SIZE)
        throw JSONRPCError(INVALID_ASSETDESC_SIZE, "Invalid asset description");

    boost::regex regunit("[a-zA-Z\u4e00-\u9fa5]+");
    string strAssetUnit = TrimString(params[3].get_str());
    if(strAssetUnit.empty() || strAssetUnit.size() > MAX_ASSETUNIT_SIZE || !boost::regex_match(strAssetUnit, regunit))
        throw JSONRPCError(INVALID_ASSETUNIT_SIZE, "Invalid asset unit");
    if(IsKeyWord(strAssetUnit))
        throw JSONRPCError(INVALID_ASSETUNIT_SIZE, "Asset unit is internal reserved words, not allowed to use");

    uint8_t nAssetDecimals = (uint8_t)params[6].get_int();
    if(nAssetDecimals < MIN_ASSETDECIMALS_VALUE || nAssetDecimals > MAX_ASSETDECIMALS_VALUE)
        throw JSONRPCError(INVALID_DECIMALS, "Invalid asset decimals");

    CAmount nAssetTotalAmount = AmountFromValue(params[4], nAssetDecimals, true);
    if(nAssetTotalAmount > MAX_ASSETS)
        throw JSONRPCError(INVALID_TOTAL_AMOUNT, strprintf("Invalid asset total amount (min: 100, max: %lld)", MAX_ASSETS / pow(10, nAssetDecimals)));

    CAmount nFirstIssueAmount = AmountFromValue(params[5], nAssetDecimals, true);
    if(nFirstIssueAmount <= 0 || nFirstIssueAmount > MAX_ASSETS)
        throw JSONRPCError(INVALID_FIRST_AMOUNT, "Invalid first issue amount");
    if(nFirstIssueAmount > nAssetTotalAmount)
        throw JSONRPCError(FIRST_EXCEED_TOTAL, "First issue amount exceed total amount");

    bool bDestory = params[7].get_bool();
    bool bPayCandy = params[8].get_bool();

    CAmount nCandyAmount = 0;
    uint16_t nCandyExpired = 0;
    if(bPayCandy)
    {
        if (!IsStartLockFeatureHeight(g_nChainHeight))
            throw JSONRPCError(INVALID_CANCELLED_SAFE, strprintf("This feature is enabled when the block height is %d", g_nStartSPOSHeight + g_nSPOSAStartLockHeight));
    
        if(params.size() < 11)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Need candy information");

        nCandyAmount = AmountFromValue(params[9], nAssetDecimals, true);
        char totalAmountStr[64] = "",candyAmountStr[64]="";
        snprintf(totalAmountStr,sizeof(totalAmountStr),"%" PRId64,nAssetTotalAmount);
        snprintf(candyAmountStr,sizeof(candyAmountStr),"%" PRId64,nCandyAmount);
        string candyMinStr = numtofloatstring(totalAmountStr,3); // 1‰
        string candyMaxStr = numtofloatstring(totalAmountStr,1); // 10%
        if(compareFloatString(candyAmountStr,candyMinStr)<0 || compareFloatString(candyAmountStr,candyMaxStr)>0)
            throw JSONRPCError(INVALID_CANDYAMOUNT, "Candy amount out of range (min: 0.001 * total, max: 0.1 * total)");
        if(nCandyAmount >= nFirstIssueAmount)
            throw JSONRPCError(CANDY_EXCEED_FIRST, "Candy amout exceed first issue amount");

        nCandyExpired = (uint16_t)params[10].get_int();
        if(nCandyExpired < MIN_CANDYEXPIRED_VALUE || nCandyExpired > MAX_CANDYEXPIRED_VALUE)
            throw JSONRPCError(INVALID_CANDYEXPIRED, "Invalid candy expired (min: 1, max: 6)");
    }

    CAmount nFirstActualAmount = nFirstIssueAmount - nCandyAmount;
    if(nFirstActualAmount < AmountFromValue("100", nAssetDecimals, true) || nFirstActualAmount > nFirstIssueAmount)
        throw JSONRPCError(INVALID_FIRST_ACTUAL_AMOUNT, "Invalid first actual amount (min: 100)");

    string strRemarks = "";
    if(params.size() > 11)
    {
        strRemarks = TrimString(params[11].get_str());
        if(strRemarks.size() > MAX_REMARKS_SIZE)
            throw JSONRPCError(INVALID_REMARKS_SIZE, "Invalid remarks");
    }

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), ISSUE_ASSET_CMD);
    CAssetData assetData(strShortName, strAssetName, strAssetDesc, strAssetUnit, nAssetTotalAmount, nFirstIssueAmount, nFirstIssueAmount - nCandyAmount, nAssetDecimals, bDestory, bPayCandy, nCandyAmount, nCandyExpired, strRemarks);

    uint256 assetId = assetData.GetHash();
    if(ExistAssetId(assetId))
        throw JSONRPCError(EXISTENT_ASSETID, "Existent asset");

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    int nOffset = g_nChainHeight - g_nProtocolV2Height;
    if (nOffset < 0)
        throw JSONRPCError(INVALID_CANCELLED_SAFE, strprintf("This feature is enabled when the block height is %d", g_nProtocolV2Height));

    CAmount nCancelledValue = GetCancelledAmount(g_nChainHeight);
    if(!IsCancelledRange(nCancelledValue))
        throw JSONRPCError(INVALID_CANCELLED_SAFE, "Invalid cancelled safe amount");
    if(nCancelledValue >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;

    // safe
    CBitcoinAddress cancelledAddress(g_strCancelledSafeAddress);
    CScript cancelledScriptPubKey = GetScriptForDestination(cancelledAddress.Get());
    CRecipient cancelledRecipient = {cancelledScriptPubKey, nCancelledValue, 0, false, false};
    vecSend.push_back(cancelledRecipient);

    // asset
    if(bPayCandy)
    {
        CRecipient assetRecipient = {CScript(), assetData.nFirstActualAmount, 0, false, true};
        vecSend.push_back(assetRecipient);
    }
    else
    {
        CRecipient assetRecipient = {CScript(), assetData.nFirstIssueAmount, 0, false, true};
        vecSend.push_back(assetRecipient);
    }

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &assetData, vecSend, NULL, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        if(nCancelledValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Issue asset failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("assetId", assetId.GetHex()));
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue addissueasset(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "addissueasset \"assetId\" addAmount \"remarks\"\n"
            "\nAdd issue.\n"
            "\nArguments:\n"
            "1. \"assetId\"         (string, required) The asset id\n"
            "2. addAmount           (numeric, required) The amount\n"
            "3. \"remarks\"         (string, optional) The remarks\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\": \"xxxxx\"  (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("addissueasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 400")
            + HelpExampleCli("addissueasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 400 \"This is a test\"")
            + HelpExampleRpc("addissueasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", 400")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint256 assetId = uint256S(TrimString(params[0].get_str()));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo, false))
        throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

    CBitcoinAddress adminAddress(assetInfo.strAdminAddress);
    if(!IsMine(*pwalletMain, adminAddress.Get()))
        throw JSONRPCError(INSUFFICIENT_AUTH_FOR_APPCMD, "You are not the admin");

    CAmount nAmount = AmountFromValue(params[1], assetInfo.assetData.nDecimals, true);
    if(nAmount <= 0 || nAmount > MAX_ASSETS)
        throw JSONRPCError(INVALID_ASSET_AMOUNT, "Invalid asset amount");
    if(assetInfo.assetData.nTotalAmount - assetInfo.assetData.nFirstIssueAmount < nAmount + GetAddedAmountByAssetId(assetId))
        throw JSONRPCError(EXCEED_TOTAL_AMOUNT, "Added amount exceed total amount");

    string strRemarks = "";
    if(params.size() > 2)
    {
        strRemarks = TrimString(params[2].get_str());
        if(strRemarks.size() > MAX_REMARKS_SIZE)
            throw JSONRPCError(INVALID_REMARKS_SIZE, "Invalid remarks");
    }

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), ADD_ASSET_CMD);
    CCommonData addData(assetId, nAmount, strRemarks);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(pwalletMain->GetBalance() <= 0)
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CRecipient assetRecipient = {GetScriptForDestination(adminAddress.Get()), nAmount, 0, false, true};
    vecSend.push_back(assetRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &addData, vecSend, &adminAddress, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        if(nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Add-issue asset failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue transferasset(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 5)
        throw runtime_error(
            "transferasset \"safeAddress\" \"assetId\" assetAmount lockTime \"remarks\"\n"
            "\nTransfer asset to someone.\n"
            "\nArguments:\n"
            "1. \"safeAddress\"     (string, required) The receiver's address\n"
            "2. \"assetId\"         (string, required) The asset id\n"
            "3. assetAmount         (numeric, required) The asset amount\n"
            "4. lockTime            (numeric, optional) The locked monthes\n"
            "5. \"remarks\"         (string, optional) The remarks\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\": \"xxxxx\"  (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("transferasset", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" \"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 400 1")
            + HelpExampleCli("transferasset", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" \"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 400 1 \"This is a test\"")
            + HelpExampleRpc("transferasset", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\", \"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", 400, 0")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    CBitcoinAddress recvAddress(params[0].get_str());
    if(!recvAddress.IsValid())
        throw JSONRPCError(INVALID_ADDRESS, "Invalid receiver's address");

    uint256 assetId = uint256S(TrimString(params[1].get_str()));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo, false))
        throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

    CAmount nAmount = AmountFromValue(params[2], assetInfo.assetData.nDecimals, true);
    if(nAmount <= 0)
        throw JSONRPCError(INVALID_ASSET_AMOUNT, "Invalid asset amount");

    int nLockedMonth = 0;
    string strRemarks = "";

    if (params.size() > 3)
    {
        if (!IsStartLockFeatureHeight(g_nChainHeight))
            throw JSONRPCError(INVALID_CANCELLED_SAFE, strprintf("This feature is enabled when the block height is %d", g_nStartSPOSHeight + g_nSPOSAStartLockHeight));
    
        nLockedMonth = params[3].get_int();
        if (nLockedMonth != 0 && !IsLockedMonthRange(nLockedMonth))
            throw JSONRPCError(INVALID_LOCKEDMONTH, "Invalid locked month (min: 0, max: 120)");

        if (params.size() > 4)
        {
            strRemarks = TrimString(params[4].get_str());
            if(strRemarks.size() > MAX_REMARKS_SIZE)
                throw JSONRPCError(INVALID_REMARKS_SIZE, "Invalid remarks");
        }
    }

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), TRANSFER_ASSET_CMD);
    CCommonData transferData(assetId, nAmount, strRemarks);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(pwalletMain->GetBalance() <= 0)
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    if(pwalletMain->GetBalance(true, &assetId) < nAmount)
        throw JSONRPCError(INSUFFICIENT_ASSET, "Insufficient asset funds");

    vector<CRecipient> vecSend;
    CRecipient recvRecipient = {GetScriptForDestination(recvAddress.Get()), nAmount, nLockedMonth, false, true, strRemarks};
    vecSend.push_back(recvRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &transferData, vecSend, NULL, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        if(nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Transfer asset failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue destoryasset(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "destoryasset \"assetId\" assetAmount \"remarks\"\n"
            "\nDestory asset.\n"
            "\nArguments:\n"
            "1. \"assetId\"         (string, required) The asset id\n"
            "2. assetAmount         (numeric, required) The asset amount\n"
            "3. \"remarks\"         (string, optional) The remarks\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\": \"xxxxx\"  (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("destoryasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 600")
            + HelpExampleCli("destoryasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 600 \"This is a test\"")
            + HelpExampleRpc("destoryasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", 600")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint256 assetId = uint256S(TrimString(params[0].get_str()));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo, false))
        throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

    if(!assetInfo.assetData.bDestory)
        throw JSONRPCError(DISABLE_DESTORY, "Disable to destory asset");

    CAmount nAmount = AmountFromValue(params[1], assetInfo.assetData.nDecimals, true);
    if(nAmount <= 0)
        throw JSONRPCError(INVALID_ASSET_AMOUNT, "Invalid asset amount");

    string strRemarks = "";
    if(params.size() > 2)
    {
        strRemarks = TrimString(params[2].get_str());
        if(strRemarks.size() > MAX_REMARKS_SIZE)
            throw JSONRPCError(INVALID_REMARKS_SIZE, "Invalid remarks");
    }

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), DESTORY_ASSET_CMD);
    CCommonData destoryData(assetId, nAmount, strRemarks);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(pwalletMain->GetBalance() <= 0)
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    if(pwalletMain->GetBalance(true, &assetId) < nAmount)
        throw JSONRPCError(INSUFFICIENT_ASSET, "Insufficient asset funds");

    vector<CRecipient> vecSend;
    CRecipient assetRecipient = {GetScriptForDestination(CBitcoinAddress(g_strCancelledAssetAddress).Get()), nAmount, 0, false, true};
    vecSend.push_back(assetRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &destoryData, vecSend, NULL, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        if(nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Destory asset failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue putcandy(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "putcandy \"assetId\" assetCandyAmount candyExpired \"remarks\"\n"
            "\nDestory asset.\n"
            "\nArguments:\n"
            "1. \"assetId\"         (string, required) The asset id\n"
            "2. assetCandyAmount    (numeric, required) The candy amount\n"
            "3. candyExpired        (numeric, required) The candy's expired month start from issued time\n"
            "4. \"remarks\"         (string, optional) The remarks\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\": \"xxxxx\"  (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("putcandy", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 600 1")
            + HelpExampleCli("putcandy", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 600 1 \"This is a test\"")
            + HelpExampleRpc("putcandy", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", 600, 1")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    if (!IsStartLockFeatureHeight(g_nChainHeight))
        throw JSONRPCError(INVALID_CANCELLED_SAFE, strprintf("This feature is enabled when the block height is %d", g_nStartSPOSHeight + g_nSPOSAStartLockHeight));

    uint256 assetId = uint256S(TrimString(params[0].get_str()));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo, false))
        throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

    CBitcoinAddress adminAddress(assetInfo.strAdminAddress);
    if(!IsMine(*pwalletMain, adminAddress.Get()))
        throw JSONRPCError(INSUFFICIENT_AUTH_FOR_APPCMD, "You are not the admin");

    CAmount nAmount = AmountFromValue(params[1], assetInfo.assetData.nDecimals, true);
    char totalAmountStr[64] = "",candyAmountStr[64]="";
    snprintf(totalAmountStr,sizeof(totalAmountStr),"%" PRId64,assetInfo.assetData.nTotalAmount);
    snprintf(candyAmountStr,sizeof(candyAmountStr),"%" PRId64,nAmount);
    string candyMinStr = numtofloatstring(totalAmountStr,3); // 1‰
    string candyMaxStr = numtofloatstring(totalAmountStr,1); // 10%
    if(compareFloatString(candyAmountStr,candyMinStr)<0 || compareFloatString(candyAmountStr,candyMaxStr)>0)
        throw JSONRPCError(INVALID_CANDYAMOUNT, "Candy amount out of range (min: 0.001 * total, max: 0.1 * total)");

    uint16_t nExpired = (uint16_t)params[2].get_int();
    if(nExpired < MIN_CANDYEXPIRED_VALUE || nExpired > MAX_CANDYEXPIRED_VALUE)
        throw JSONRPCError(INVALID_CANDYEXPIRED, "Invalid candy expired (min: 1, max: 6)");

    string strRemarks = "";
    if(params.size() > 3)
    {
        strRemarks = TrimString(params[3].get_str());
        if(strRemarks.size() > MAX_REMARKS_SIZE)
            throw JSONRPCError(INVALID_REMARKS_SIZE, "Invalid remarks");
    }

    int dbPutCandyCount = 0;
    map<COutPoint, CCandyInfo> mapCandyInfo;
    if(GetAssetIdCandyInfo(assetId, mapCandyInfo))
        dbPutCandyCount = mapCandyInfo.size();
    int memoryPutCandyCount = mempool.get_PutCandy_count(assetId);
    int putCandyCount = memoryPutCandyCount + dbPutCandyCount;
    if(putCandyCount >= MAX_PUTCANDY_VALUE)
        throw JSONRPCError(INVALID_CANDYAMOUNT, "put candy times used up");

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), PUT_CANDY_CMD);
    CPutCandyData candyData(assetId, nAmount, nExpired, strRemarks);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(pwalletMain->GetBalance() <= 0)
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    if(pwalletMain->GetBalance(true, &assetId, &adminAddress) < nAmount)
        throw JSONRPCError(INSUFFICIENT_ASSET, "Insufficient admin's asset funds");

    vector<CRecipient> vecSend;
    CRecipient assetRecipient = {GetScriptForDestination(CBitcoinAddress(g_strPutCandyAddress).Get()), nAmount, 0, false, true};
    vecSend.push_back(assetRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &candyData, vecSend, NULL, &adminAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        if(nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Put candy failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue getcandy(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getcandy \"assetId\"\n"
            "\nGet candy by specified asset id.\n"
            "\nArguments:\n"
            "1. \"assetId\"         (string, required) The asset id\n"
            "\nResult:\n"
            "{\n"
            "    \"txId\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"\n"
            "    \"assetAmount\": xxxxx\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getcandy", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\"")
            + HelpExampleRpc("getcandy", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint256 assetId = uint256S(TrimString(params[0].get_str()));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo, false))
        throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

    // get candy tx
    map<COutPoint, CCandyInfo> mapCandyInfo;
    GetAssetIdCandyInfo(assetId, mapCandyInfo);
    if(mapCandyInfo.size() == 0)
        throw JSONRPCError(NONEXISTENT_ASSETCANDY, "Non-existent asset candy");

    EnsureWalletIsUnlocked();

    if(pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    map<CKeyID, int64_t> mapKeyBirth;
    pwalletMain->GetKeyBirthTimes(mapKeyBirth);

    vector<CKeyID> vKeyID;
    for(map<CKeyID, int64_t>::const_iterator it = mapKeyBirth.begin(); it != mapKeyBirth.end(); it++)
        vKeyID.push_back(it->first);

    int nCurrentHeight = g_nChainHeight;

    UniValue ret(UniValue::VARR);
    for(map<COutPoint, CCandyInfo>::const_iterator it = mapCandyInfo.begin(); it != mapCandyInfo.end(); it++)
    {
        const COutPoint& out = it->first;
        const CCandyInfo& candyInfo = it->second;

        if(candyInfo.nAmount <= 0)
            continue;

        uint256 blockHash;
        int nTxHeight = GetTxHeight(out.hash, &blockHash);
        if(blockHash.IsNull())
            continue;

        if (nTxHeight >= g_nStartSPOSHeight)
        {
            if(nTxHeight + SPOS_BLOCKS_PER_DAY > nCurrentHeight)
                continue;

            if(candyInfo.nExpired * SPOS_BLOCKS_PER_MONTH + nTxHeight < nCurrentHeight)
                continue;
        }
        else
        {
            if(nTxHeight + BLOCKS_PER_DAY > nCurrentHeight)
                continue;

            if(candyInfo.nExpired * BLOCKS_PER_MONTH + nTxHeight < nCurrentHeight)
                continue;
        }

        CAmount nTotalSafe = 0;
        if(!GetTotalAmountByHeight(nTxHeight, nTotalSafe))
        {
            if(ret.empty())
                throw JSONRPCError(GET_ALL_ADDRESS_SAFE_FAILED, "Error: get all address safe amount failed");
            else
                return ret;
        }

        if(nTotalSafe <= 0)
        {
             if(ret.empty())
                throw JSONRPCError(INVALID_TOTAL_SAFE, "Error: get total safe amount failed");
            else
                return ret;
        }

        CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), GET_CANDY_CMD);
        CPutCandy_IndexKey assetIdCandyInfo;
        assetIdCandyInfo.assetId = assetId;
        assetIdCandyInfo.out = out;

        CAmount dbamount = 0;
        CAmount memamount = 0;
        if (!GetGetCandyTotalAmount(assetId, out, dbamount, memamount))
            throw JSONRPCError(GET_GET_CANDY_TOTAL_FAILED, "Error: Failed to get the number of candy already received");

        CAmount nGetCandyAmount =  dbamount + memamount;
        CAmount nNowGetCandyTotalAmount = 0;

        vector<CRecipient> vecSend;
        for(unsigned int i = 0; i < vKeyID.size(); i++)
        {
            string strAddress = CBitcoinAddress(vKeyID[i]).ToString();

            CAmount nTempAmount = 0;
            if(GetGetCandyAmount(assetId, out, strAddress, nTempAmount)) // got candy
                continue;

            CBitcoinAddress recvAddress(strAddress);

            CAmount nSafe = 0;
            if(!GetAddressAmountByHeight(nTxHeight, strAddress, nSafe))
                continue;

            if(nSafe < 1 * COIN || nSafe > nTotalSafe)
                continue;

            CAmount nCandyAmount = (CAmount)(1.0 * nSafe / nTotalSafe * candyInfo.nAmount);
            if(nCandyAmount < AmountFromValue("0.0001", assetInfo.assetData.nDecimals, true))
                continue;

            if (nCandyAmount + nGetCandyAmount > candyInfo.nAmount)
                throw JSONRPCError(EXCEED_PUT_CANDY_TOTAL_AMOUNT, "Error: The candy has been picked up");

            nNowGetCandyTotalAmount += nCandyAmount;

            LogPrint("asset", "rpc-getcandy: candy-height: %d, address: %s, total_safe: %lld, user_safe: %lld, total_candy_amount: %lld, can_get_candy_amount: %lld, out: %s\n", nTxHeight, strAddress, nTotalSafe, nSafe, candyInfo.nAmount, nCandyAmount, out.ToString());
            CRecipient recvRecipient = {GetScriptForDestination(recvAddress.Get()), nCandyAmount, 0, false, true};
            vecSend.push_back(recvRecipient);
        }

        if (nNowGetCandyTotalAmount + nGetCandyAmount > candyInfo.nAmount)
            throw JSONRPCError(EXCEED_PUT_CANDY_TOTAL_AMOUNT, "Error: The candy has been picked up");

        if(vecSend.size() == 0)
            continue;

        unsigned int nTxCount = vecSend.size() / GET_CANDY_TXOUT_SIZE;
        if(vecSend.size() % GET_CANDY_TXOUT_SIZE != 0)
            nTxCount++;

        for(unsigned int i = 0; i < nTxCount; i++)
        {
            vector<CRecipient> vecNowSend;
            if(i != nTxCount - 1)
            {
                for(int m = 0; m < GET_CANDY_TXOUT_SIZE; m++)
                    vecNowSend.push_back(vecSend[i * GET_CANDY_TXOUT_SIZE + m]);
            }
            else
            {
                int nCount = vecSend.size() - i * GET_CANDY_TXOUT_SIZE;
                for(int m = 0; m < nCount; m++)
                    vecNowSend.push_back(vecSend[i * GET_CANDY_TXOUT_SIZE + m]);
            }

            CWalletTx wtx;
            CReserveKey reservekey(pwalletMain);
            CAmount nFeeRequired;
            int nChangePosRet = -1;
            string strError;
            if(!pwalletMain->CreateAssetTransaction(&appHeader, &assetIdCandyInfo, vecNowSend, NULL, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
            {
                if(nFeeRequired > pwalletMain->GetBalance())
                    strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
                 if(ret.empty())
                    throw JSONRPCError(RPC_WALLET_ERROR, strError);
                 else
                    return ret;
            }

            if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
            {
                if(ret.empty())
                    throw JSONRPCError(RPC_WALLET_ERROR, "Error: Get candy failed, please check your wallet and try again later!");
                else
                    return ret;
            }

            for(unsigned int m = 0; m < wtx.vout.size(); m++)
            {
                const CTxOut& txout = wtx.vout[m];
                if(!txout.IsAsset())
                    continue;

                UniValue txData(UniValue::VOBJ);
                txData.push_back(Pair("txId", strprintf("%s-%u", wtx.GetHash().GetHex(), m)));
                txData.push_back(Pair("assetAmount", StrValueFromAmount(txout.nValue, assetInfo.assetData.nDecimals)));
                ret.push_back(txData);
            }
        }

        //erase candy
        std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
        int size = gAllCandyInfoVec.size();
        bool found = false;
        for(int i=0;i<size;i++)
        {
            CCandy_BlockTime_Info& info = gAllCandyInfoVec[i];
            if(assetId!=info.assetId)
                continue;
            if(candyInfo.nAmount!=info.candyinfo.nAmount)
                continue;
            if(candyInfo.nExpired!=info.candyinfo.nExpired)
                continue;
            if(assetIdCandyInfo.out.n!=info.outpoint.n)
                continue;
            if(assetIdCandyInfo.out.hash!=info.outpoint.hash)
                continue;
            if(nTxHeight!=info.nHeight)
                continue;
            found = true;
            gAllCandyInfoVec.erase(gAllCandyInfoVec.begin()+i);
            break;
        }
        if(!found){
            LogPrintf("erase candy not found,height:%d,assetId:%s\n", nTxHeight,assetId.ToString());
        }
    }

    return ret;
}

UniValue getassetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getassetinfo \"assetId\"\n"
            "\nReturns asset information by specified asset id.\n"
            "\nArguments:\n"
            "1. \"assetId\"             (string, required) The asset id for information lookup\n"
            "\nResult:\n"
            "{\n"
            "    \"assetShortName\": \"xxxxx\"\n"
            "    \"assetName\": \"xxxxx\"\n"
            "    \"assetDesc\": \"xxxxx\"\n"
            "    \"assetUnit\": \"xxxxx\"\n"
            "    \"assetTotalAmount\": \"xxxxx\"\n"
            "    \"firstIssueAmount\": \"xxxxx\"\n"
            "    \"firstActualAmount\": \"xxxxx\"\n"
            "    \"alreadyIssueAmount\": \"xxxxx\"\n"
            "    \"assetDecimals\": xxxxx\n"
            "    \"isDestory\": true or false\n"
            "    \"isPayCandy\": true or false\n"
            "    \"candyTotalAmount\": \"xxxxx\"\n"
            "    \"destoryTotalAmout\": \"xxxxx\"\n"
            "    \"candyExpired\": xxxxx\n"
            "    \"remarks\": \"xxxxx\"\n"
            "    \"issueTime\": xxxxx\n"
            "    \"adminSafeAddress\": \"xxxxx\"\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getassetinfo", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\"")
            + HelpExampleRpc("getassetinfo", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\"")
        );

    LOCK(cs_main);

    uint256 assetId = uint256S(TrimString(params[0].get_str()));

    CAssetId_AssetInfo_IndexValue assetInfo;
    if (assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo))
        throw JSONRPCError(GET_ASSETINFO_FAILED, "No asset available about asset id");

    uint32_t nTime = 0;
    vector<COutPoint> vOut;
    uint8_t nTxClass = (uint8_t)ISSUE_TXOUT;
    if (GetTxInfoByAssetIdTxClass(assetId, nTxClass, vOut))
    {
        std::vector<uint256> vTx;
        BOOST_FOREACH(const COutPoint& out, vOut)
        {
            if(find(vTx.begin(), vTx.end(), out.hash) == vTx.end())
                vTx.push_back(out.hash);
        }

        if (!vTx.empty())
        {
            int nTxHeight = GetTxHeight(vTx[0]);
            if (nTxHeight <= chainActive.Height())
                nTime = chainActive[nTxHeight]->GetBlockTime();
        }
    }

    CAmount value = 0;
    vector<COutPoint> tempvOut;
    uint8_t ntempTxClass = (uint8_t)DESTORY_TXOUT;
    if (GetTxInfoByAssetIdTxClass(assetId, ntempTxClass, tempvOut))
    {
        std::vector<uint256> vTx;
        BOOST_FOREACH(const COutPoint& out, tempvOut)
        {
            if(find(vTx.begin(), vTx.end(), out.hash) == vTx.end())
                vTx.push_back(out.hash);
        }

        std::vector<uint256>::iterator it = vTx.begin();
        for (; it != vTx.end(); it++)
        {
            CTransaction tx;
            uint256 hashBlock;
            if (!GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
                continue;

            BOOST_FOREACH(const CTxOut& txout, tx.vout)
            {
                if (!txout.IsAsset())
                    continue;

                CAppHeader header;
                vector<unsigned char> vData;
                if (ParseReserve(txout.vReserve, header, vData))
                {
                    if (header.nAppCmd == DESTORY_ASSET_CMD)
                    {
                        CCommonData commonData;
                        if (ParseCommonData(vData, commonData))
                        {
                            if (assetId == commonData.assetId)
                                value += commonData.nAmount;
                        }
                    }
                }
           }
        }
    }

    CAmount candyTotalAmount = 0;
    map<COutPoint, CCandyInfo> mapCandyInfo;
    GetAssetIdCandyInfo(assetId, mapCandyInfo);
    if (mapCandyInfo.empty())
        candyTotalAmount = assetInfo.assetData.nCandyAmount;
    else
    {
        map<COutPoint, CCandyInfo>::iterator it = mapCandyInfo.begin();
        for (; it != mapCandyInfo.end(); it++)
        {
            if (it->second.nAmount <= 0)
                continue;

            candyTotalAmount += it->second.nAmount;
        }
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("assetShortName", assetInfo.assetData.strShortName));
    ret.push_back(Pair("assetName", assetInfo.assetData.strAssetName));
    ret.push_back(Pair("assetDesc", assetInfo.assetData.strAssetDesc));
    ret.push_back(Pair("assetUnit", assetInfo.assetData.strAssetUnit));
    ret.push_back(Pair("assetTotalAmount", StrValueFromAmount(assetInfo.assetData.nTotalAmount, assetInfo.assetData.nDecimals)));
    ret.push_back(Pair("firstIssueAmount", StrValueFromAmount(assetInfo.assetData.nFirstIssueAmount, assetInfo.assetData.nDecimals)));
    ret.push_back(Pair("firstActualAmount", StrValueFromAmount(assetInfo.assetData.nFirstActualAmount, assetInfo.assetData.nDecimals)));
    ret.push_back(Pair("alreadyIssueAmount", StrValueFromAmount(assetInfo.assetData.nFirstIssueAmount + GetAddedAmountByAssetId(assetId), assetInfo.assetData.nDecimals)));
    ret.push_back(Pair("assetDecimals", assetInfo.assetData.nDecimals));
    ret.push_back(Pair("isDestory", assetInfo.assetData.bDestory));
    ret.push_back(Pair("isPayCandy", assetInfo.assetData.bPayCandy));
    ret.push_back(Pair("candyTotalAmount", StrValueFromAmount(candyTotalAmount, assetInfo.assetData.nDecimals)));
    ret.push_back(Pair("destoryTotalAmout", StrValueFromAmount(value, assetInfo.assetData.nDecimals)));
    ret.push_back(Pair("candyExpired", assetInfo.assetData.nCandyExpired));
    ret.push_back(Pair("remarks", assetInfo.assetData.strRemarks));
    ret.push_back(Pair("issueTime", (int64_t)nTime));
    ret.push_back(Pair("adminSafeAddress", assetInfo.strAdminAddress));

    return ret;
}

UniValue getlocalassetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getlocalassetinfo \"assetId\"\n"
            "\nReturns asset information by specified asset id.\n"
            "\nArguments:\n"
            "1. \"assetId\"             (string, required) The asset id for information lookup\n"
            "\nResult:\n"
            "{\n"
            "    \"assetShortName\": \"xxxxx\"\n"
            "    \"assetName\": \"xxxxx\"\n"
            "    \"assetDesc\": \"xxxxx\"\n"
            "    \"assetUnit\": \"xxxxx\"\n"
            "    \"assetDecimals\": xxxxx\n"
            "    \"issueTime\": xxxxx\n"
            "    \"assetAvailAmount\": \"xxxxx\"\n"
            "    \"assetWaitAmount\": \"xxxxx\"\n"
            "    \"assetLockAmount\": \"xxxxx\"\n"
            "    \"assetLocalTotalAmount\": \"xxxxx\"\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getlocalassetinfo", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\"")
            + HelpExampleRpc("getlocalassetinfo", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 assetId = uint256S(TrimString(params[0].get_str()));

    CAssetId_AssetInfo_IndexValue assetInfo;
    if (assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo))
        throw JSONRPCError(GET_ASSETINFO_FAILED, "No asset available about asset id");

    bool found = false;
    for(map<uint256, CWalletTx>::const_iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); it++)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (!pcoin->IsTrusted())
            continue;
        for(unsigned int i = 0; i < pcoin->vout.size(); i++)
        {
            if(pwalletMain->IsSpent(it->first, i))
                continue;

            const CTxOut& txout = pcoin->vout[i];
            if(!txout.IsAsset())
                continue;

            CAppHeader header;
            std::vector<unsigned char> vData;
            if(!ParseReserve(txout.vReserve, header, vData))
                continue;
            if(header.nAppCmd == ADD_ASSET_CMD || header.nAppCmd==CHANGE_ASSET_CMD || header.nAppCmd==TRANSFER_ASSET_CMD || header.nAppCmd==DESTORY_ASSET_CMD)
            {
                CCommonData commonData;
                if(!ParseCommonData(vData, commonData))
                    continue;
                if(assetId != commonData.assetId)
                    continue;
                found = true;
                break;
            }else if(header.nAppCmd == GET_CANDY_CMD)
            {
                CGetCandyData getCandyData;
                if(!ParseGetCandyData(vData,getCandyData))
                    continue;
                if(assetId != getCandyData.assetId)
                    continue;
                found = true;
                break;
            }else if(header.nAppCmd == ISSUE_ASSET_CMD)
            {
                CAssetData assetData;
                if(!ParseIssueData(vData, assetData))
                    continue;
                if(assetData.GetHash()!=assetInfo.assetData.GetHash())
                    continue;
                found = true;
                break;
            }
        }
        if(found)
            break;
    }

    if(!found)
        throw JSONRPCError(NOT_OWN_ASSET, "Not own this asset");

    uint32_t nTime = 0;
    vector<COutPoint> vOut;
    uint8_t nTxClass = (uint8_t)ISSUE_TXOUT;
    if (GetTxInfoByAssetIdTxClass(assetId, nTxClass, vOut))
    {
        std::vector<uint256> vTx;
        BOOST_FOREACH(const COutPoint& out, vOut)
        {
            if(find(vTx.begin(), vTx.end(), out.hash) == vTx.end())
                vTx.push_back(out.hash);
        }

        if (!vTx.empty())
        {
            int nTxHeight = GetTxHeight(vTx[0]);
            if (nTxHeight <= chainActive.Height())
                nTime = chainActive[nTxHeight]->GetBlockTime();
        }
    }

    CAmount amount = pwalletMain->GetBalance(true,&assetId);
    CAmount unconfirmAmount = pwalletMain->GetUnconfirmedBalance(true,&assetId);
    CAmount lockedAmount = pwalletMain->GetLockedBalance(true,&assetId);
    CAmount totalAmount = amount+unconfirmAmount+lockedAmount;

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("assetShortName", assetInfo.assetData.strShortName));
    ret.push_back(Pair("assetName", assetInfo.assetData.strAssetName));
    ret.push_back(Pair("assetDesc", assetInfo.assetData.strAssetDesc));
    ret.push_back(Pair("assetUnit", assetInfo.assetData.strAssetUnit));
    ret.push_back(Pair("assetDecimals", assetInfo.assetData.nDecimals));
    ret.push_back(Pair("issueTime", (int64_t)nTime));
    ret.push_back(Pair("assetAvailAmount", StrValueFromAmount(amount, assetInfo.assetData.nDecimals)));
    ret.push_back(Pair("assetWaitAmount", StrValueFromAmount(unconfirmAmount, assetInfo.assetData.nDecimals)));
    ret.push_back(Pair("assetLockAmount", StrValueFromAmount(lockedAmount, assetInfo.assetData.nDecimals)));
    ret.push_back(Pair("assetLocalTotalAmount", StrValueFromAmount(totalAmount, assetInfo.assetData.nDecimals)));

    return ret;
}

UniValue getassetidtxids(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getassetidtxids \"assetId\" txClass\n"
            "\nReturns list of transactions by specified asset id and transaction type.\n"
            "\nArguments:\n"
            "1. \"assetId\"             (string, required) The asset id for transaction lookup\n"
            "2. txClass                 (numeric, required) The transaction type (1=all, 2=normal, 3=locked)\n"
            "\nResult:\n"
            "{\n"
            "    \"txList\":\n"
            "    [\n"
            "        \"txId\"\n"
            "        ,...\n"
            "    ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getassetidtxids", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 1")
            + HelpExampleRpc("getassetidtxids", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", 3")
        );

    LOCK(cs_main);

    uint256 assetId = uint256S(TrimString(params[0].get_str()));
    uint8_t nTxClass = (uint8_t)params[1].get_int();
    if(nTxClass < 1 || nTxClass > sporkManager.GetSporkValue(SPORK_105_TX_CLASS_MAX_VALUE))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid type of transaction");

    vector<COutPoint> vOut;
    if(!GetTxInfoByAssetIdTxClass(assetId, nTxClass, vOut))
        throw JSONRPCError(GET_TXID_FAILED, "No transaction available about asset");

    std::vector<uint256> vTx;
    BOOST_FOREACH(const COutPoint& out, vOut)
    {
        if(find(vTx.begin(), vTx.end(), out.hash) == vTx.end())
            vTx.push_back(out.hash);
    }

    UniValue ret(UniValue::VOBJ);
    UniValue transactionList(UniValue::VARR);
    for(unsigned int i = 0; i < vTx.size(); i++)
        transactionList.push_back(vTx[i].GetHex());
    ret.push_back(Pair("txList", transactionList));

    return ret;
}

UniValue getaddrassettxids(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "getaddrassettxids \"safeAddress\" \"assetId\" txClass\n"
            "\nReturns list of transactions by specified address, asset id and transaction type.\n"
            "\nArguments:\n"
            "1. \"safeAddress\"         (string, required) The Safe address for transaction lookup\n"
            "2. \"assetId\"             (string, required) The asset id for transaction lookup\n"
            "3. txClass                 (numeric, required) The transaction type (1=all, 2=normal, 3=locked)\n"
            "\nResult:\n"
            "{\n"
            "    \"txList\":\n"
            "    [\n"
            "        \"txId\"\n"
            "        ,...\n"
            "    ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddrassettxids", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" \"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 1")
            + HelpExampleRpc("getaddrassettxids", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\", \"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", 3")
        );

    LOCK(cs_main);

    string strAddress = TrimString(params[0].get_str());
    uint256 assetId = uint256S(TrimString(params[1].get_str()));
    uint8_t nTxClass = (uint8_t)params[2].get_int();
    if(nTxClass < 1 || nTxClass > sporkManager.GetSporkValue(SPORK_105_TX_CLASS_MAX_VALUE))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid type of transaction");

    vector<COutPoint> vOut;
    if(!GetTxInfoByAssetIdAddressTxClass(assetId, strAddress, nTxClass, vOut))
        throw JSONRPCError(GET_TXID_FAILED, "No transaction available about asset with specified address");

    UniValue ret(UniValue::VOBJ);
    UniValue transactionList(UniValue::VARR);

    vector<uint256> vHash;
    BOOST_FOREACH(const COutPoint& out, vOut)
    {
        if(find(vHash.begin(), vHash.end(), out.hash) == vHash.end())
            vHash.push_back(out.hash);
    }
    for(unsigned int i = 0; i < vHash.size(); i++)
        transactionList.push_back(vHash[i].GetHex());
    ret.push_back(Pair("txList", transactionList));

    return ret;
}

UniValue getaddrassetbalance(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getaddrassetbalance \"safeAddress\" \"assetId\" \n"
            "\nReturns balance by specified address, asset id andy transaction type.\n"
            "\nArguments:\n"
            "1. \"safeAddress\"         (string, required) The Safe address for transaction lookup\n"
            "2. \"assetId\"             (string, required) The asset id for transaction lookup\n"
            "\nResult:\n"
            "{\n"
            "    \"ReceiveAmount\":\"xxxxx\"\n"
            "    \"SendAmount\":\"xxxxx\"\n"
            "    \"totalAmount\":\"xxxxx\"\n"
            "    \"lockAmount\":\"xxxxx\"\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddrassetbalance", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" \"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\"")
            + HelpExampleRpc("getaddrassetbalance", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\", \"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\"")
        );

    LOCK(cs_main);

    string strAddress = TrimString(params[0].get_str());
    CBitcoinAddress address(strAddress);
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

    uint256 assetId = uint256S(TrimString(params[1].get_str()));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if (assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo))
        throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

    vector<COutPoint> vOut;
    if (!GetTxInfoByAssetIdAddressTxClass(assetId, strAddress, 1, vOut))
        throw JSONRPCError(GET_TXID_FAILED, "No transaction available about asset with specified address");

    std::string TotalSendAmount = "";
    std::string TotalReceiveAmount = "";
    std::string TotalLockingAmount = "";
    std::string Totalbalance = "";

    vector<uint256> vHash;
    BOOST_FOREACH(const COutPoint& out, vOut)
    {
        if(find(vHash.begin(), vHash.end(), out.hash) == vHash.end())
            vHash.push_back(out.hash);
    }

    vector<uint256>::iterator it = vHash.begin();
    for (; it != vHash.end(); it++)
    {
        CTransaction tx;
        uint256 hashBlock;
        if (GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
        {
            BOOST_FOREACH(const CTxOut& txout, tx.vout)
            {
                if (!txout.IsAsset())
                    continue;

                CTxDestination tempdest;
                if (!ExtractDestination(txout.scriptPubKey, tempdest))
                    continue;

                if (!CBitcoinAddress(tempdest).IsValid())
                    continue;

                std::string strtempAddress = CBitcoinAddress(tempdest).ToString();
                if (strtempAddress == strAddress)
                {
                    char ctempdata[64] = {0};
                    sprintf(ctempdata, "%" PRId64, txout.nValue);
                    TotalReceiveAmount = plusstring(TotalReceiveAmount, ctempdata);

                    if (txout.nUnlockedHeight > chainActive.Height())
                        TotalLockingAmount = plusstring(TotalLockingAmount, ctempdata);
                }
            }

            if (!tx.IsCoinBase())
            {
                BOOST_FOREACH(const CTxIn& txin, tx.vin)
                {
                    CTransaction temptx;
                    uint256 hashBlock;
                    if (!GetTransaction(txin.prevout.hash, temptx, Params().GetConsensus(), hashBlock, true))
                        continue;

                    const CTxOut& txout = temptx.vout[txin.prevout.n];
                    if (!txout.IsAsset())
                        continue;

                    CTxDestination tempdest;
                    if (!ExtractDestination(txout.scriptPubKey, tempdest))
                    {
                        continue;
                    }

                    if (!CBitcoinAddress(tempdest).IsValid())
                        continue;

                    std::string strtempAddress = CBitcoinAddress(tempdest).ToString();
                    if (strtempAddress == strAddress)
                    {
                        char ctempdata[64] = {0};
                        sprintf(ctempdata, "%" PRId64, txout.nValue);
                        TotalSendAmount = plusstring(TotalSendAmount, ctempdata);
                    }
                }
            }
        }
    }

    Totalbalance = minusstring(TotalReceiveAmount, TotalSendAmount);

    UniValue ret(UniValue::VOBJ);
    if (TotalReceiveAmount.length() <= assetInfo.assetData.nDecimals)
        ret.push_back(Pair("ReceiveAmount",  StrValueFromAmount(atol(TotalReceiveAmount.c_str()), assetInfo.assetData.nDecimals)));
    else
        ret.push_back(Pair("ReceiveAmount",  numtofloatstring(TotalReceiveAmount, assetInfo.assetData.nDecimals)));

    if (TotalSendAmount.length() <= assetInfo.assetData.nDecimals)
        ret.push_back(Pair("SendAmount",  StrValueFromAmount(atol(TotalSendAmount.c_str()), assetInfo.assetData.nDecimals)));
    else
        ret.push_back(Pair("SendAmount",  numtofloatstring(TotalSendAmount, assetInfo.assetData.nDecimals)));

    if (Totalbalance.length() <= assetInfo.assetData.nDecimals)
        ret.push_back(Pair("totalAmount",  StrValueFromAmount(atol(Totalbalance.c_str()), assetInfo.assetData.nDecimals)));
    else
         ret.push_back(Pair("totalAmount",  numtofloatstring(Totalbalance, assetInfo.assetData.nDecimals)));

    if (TotalLockingAmount.length() <= assetInfo.assetData.nDecimals)
        ret.push_back(Pair("lockAmount",  StrValueFromAmount(atol(TotalLockingAmount.c_str()), assetInfo.assetData.nDecimals)));
    else
        ret.push_back(Pair("lockAmount",  numtofloatstring(TotalLockingAmount, assetInfo.assetData.nDecimals)));

    return ret;
}

UniValue getassetdetails(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getassetdetails \"txId\"\n"
            "\nReturns transaction details by specified transaction id.\n"
            "\nArguments:\n"
            "1. \"txId\"            (string, required) The transaction id\n"
            "\nResult:\n"
            "{\n"
            "    \"txData\":\n"
            "    [\n"
            "       {\n"
            "           \"adminSafeAddress\": \"xxxxx\"\n"
            "           \"assetShortName\": \"xxxxx\"\n"
            "           \"assetName\": \"xxxxx\"\n"
            "           \"assetDesc\": \"xxxxx\"\n"
            "           \"assetUnit\": \"xxxxx\"\n"
            "           \"assetTotalAmount\": \"xxxxx\"\n"
            "           \"firstIssueAmount\": \"xxxxx\"\n"
            "           \"assetDecimals\": xxxxx\n"
            "           \"isDestory\": true or false\n"
            "           \"isPayCandy\": true or false\n"
            "           \"assetCandyAmount\": \"xxxxx\"\n"
            "           \"candyExpired\": xxxxx\n"
            "           \"remarks\": \"xxxxx\"\n"
            "       }\n"
            "       ,{\n"
            "           \"adminSafeAddress\": \"xxxxx\"\n"
            "           \"assetId\": \"xxxxx\"\n"
            "           \"addAmount\": \"xxxxx\"\n"
            "           \"remarks\": \"xxxxx\"\n"
            "       }\n"
            "       ,{\n"
            "           \"safeAddress\": \"xxxxx\"\n"
            "           \"assetId\": \"xxxxx\"\n"
            "           \"assetAmount\": \"xxxxx\"\n"
            "           \"lockTime\": xxxxx\n"
            "           \"remarks\": \"xxxxx\"\n"
            "       }\n"
            "       ,{\n"
            "           \"assetId\": \"xxxxx\"\n"
            "           \"assetAmount\": \"xxxxx\"\n"
            "           \"remarks\": \"xxxxx\"\n"
            "       }\n"
            "       ,{\n"
            "           \"assetId\": \"xxxxx\"\n"
            "           \"assetCandyAmount\": \"xxxxx\"\n"
            "           \"candyExpired\": xxxxx\n"
            "           \"remarks\": \"xxxxx\"\n"
            "       }\n"
            "       ,{\n"
            "           \"assetId\": \"xxxxx\"\n"
            "           \"assetAmount\": \"xxxxx\"\n"
            "           \"remarks\": \"xxxxx\"\n"
            "       }\n"
            "        ,...\n"
            "    ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getassetdetails", "\"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\"")
            + HelpExampleRpc("getassetdetails", "\"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\"")
        );

    LOCK(cs_main);

    uint256 txId = uint256S(TrimString(params[0].get_str()));
    CTransaction tx;
    uint256 hashBlock;
    if(!GetTransaction(txId, tx, Params().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No information available about transaction");

    UniValue ret(UniValue::VOBJ);
    UniValue txData(UniValue::VARR);

    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CAppHeader header;
        vector<unsigned char> vData;
        if(ParseReserve(txout.vReserve, header, vData))
        {
            CTxDestination dest;
            if(!ExtractDestination(txout.scriptPubKey, dest))
                continue;

            string strAddress = CBitcoinAddress(dest).ToString();
            if(header.nAppCmd == ISSUE_ASSET_CMD)
            {
                CAssetData assetData;
                if(ParseIssueData(vData, assetData))
                {
                    UniValue issueData(UniValue::VOBJ);
                    issueData.push_back(Pair("adminSafeAddress", strAddress));
                    issueData.push_back(Pair("assetShortName", assetData.strShortName));
                    issueData.push_back(Pair("assetName", assetData.strAssetName));
                    issueData.push_back(Pair("asetDesc", assetData.strAssetDesc));
                    issueData.push_back(Pair("assetUnit", assetData.strAssetUnit));
                    issueData.push_back(Pair("assetTotalAmount", StrValueFromAmount(assetData.nTotalAmount, assetData.nDecimals)));
                    issueData.push_back(Pair("firstIssueAmount", StrValueFromAmount(assetData.nFirstIssueAmount, assetData.nDecimals)));
                    issueData.push_back(Pair("assetDecimals", assetData.nDecimals));
                    issueData.push_back(Pair("isDestory", assetData.bDestory));
                    issueData.push_back(Pair("isPayCandy", assetData.bPayCandy));
                    issueData.push_back(Pair("assetCandyAmount", StrValueFromAmount(assetData.nCandyAmount, assetData.nDecimals)));
                    issueData.push_back(Pair("candyExpired", assetData.nCandyExpired));
                    issueData.push_back(Pair("remarks", assetData.strRemarks));

                    txData.push_back(issueData);
                }
            }
            else if(header.nAppCmd == ADD_ASSET_CMD)
            {
                CCommonData commonData;
                if(ParseCommonData(vData, commonData))
                {
                    CAssetId_AssetInfo_IndexValue assetInfo;
                    if(commonData.assetId.IsNull() || !GetAssetInfoByAssetId(commonData.assetId, assetInfo))
                        continue;

                    UniValue addData(UniValue::VOBJ);
                    addData.push_back(Pair("adminSafeAddress", strAddress));
                    addData.push_back(Pair("assetId", commonData.assetId.GetHex()));
                    addData.push_back(Pair("addAmount", StrValueFromAmount(commonData.nAmount, assetInfo.assetData.nDecimals)));
                    addData.push_back(Pair("remarks", commonData.strRemarks));

                    txData.push_back(addData);
                }
            }
            else if(header.nAppCmd == TRANSFER_ASSET_CMD)
            {
                CCommonData commonData;
                if(ParseCommonData(vData, commonData))
                {
                    CAssetId_AssetInfo_IndexValue assetInfo;
                    if(commonData.assetId.IsNull() || !GetAssetInfoByAssetId(commonData.assetId, assetInfo))
                        continue;

                    UniValue transferData(UniValue::VOBJ);
                    transferData.push_back(Pair("safeAddress", strAddress));
                    transferData.push_back(Pair("assetId", commonData.assetId.GetHex()));
                    transferData.push_back(Pair("assetAmount", StrValueFromAmount(commonData.nAmount, assetInfo.assetData.nDecimals)));
                    transferData.push_back(Pair("lockTime", GetLockedMonth(txId, txout)));
                    transferData.push_back(Pair("remarks", commonData.strRemarks));

                    txData.push_back(transferData);
                }
            }
            else if(header.nAppCmd == DESTORY_ASSET_CMD)
            {
                CCommonData commonData;
                if(ParseCommonData(vData, commonData))
                {
                    CAssetId_AssetInfo_IndexValue assetInfo;
                    if(commonData.assetId.IsNull() || !GetAssetInfoByAssetId(commonData.assetId, assetInfo))
                        continue;

                    UniValue destoryData(UniValue::VOBJ);
                    destoryData.push_back(Pair("assetId", commonData.assetId.GetHex()));
                    destoryData.push_back(Pair("destory-assetAmount", StrValueFromAmount(commonData.nAmount, assetInfo.assetData.nDecimals)));
                    destoryData.push_back(Pair("remarks", commonData.strRemarks));

                    txData.push_back(destoryData);
                }
            }
            else if(header.nAppCmd == CHANGE_ASSET_CMD)
            {
                CCommonData commonData;
                if(ParseCommonData(vData, commonData))
                {
                    CAssetId_AssetInfo_IndexValue assetInfo;
                    if(commonData.assetId.IsNull() || !GetAssetInfoByAssetId(commonData.assetId, assetInfo))
                        continue;

                    UniValue changeData(UniValue::VOBJ);
                    changeData.push_back(Pair("assetId", commonData.assetId.GetHex()));
                    changeData.push_back(Pair("change-assetAmount", StrValueFromAmount(commonData.nAmount, assetInfo.assetData.nDecimals)));
                    changeData.push_back(Pair("remarks", commonData.strRemarks));

                    txData.push_back(changeData);
                }
            }
            else if(header.nAppCmd == PUT_CANDY_CMD)
            {
                CPutCandyData candyData;
                if(ParsePutCandyData(vData, candyData))
                {
                    CAssetId_AssetInfo_IndexValue assetInfo;
                    if(candyData.assetId.IsNull() || !GetAssetInfoByAssetId(candyData.assetId, assetInfo))
                        continue;

                    UniValue putCandyData(UniValue::VOBJ);
                    putCandyData.push_back(Pair("assetId", candyData.assetId.GetHex()));
                    putCandyData.push_back(Pair("assetCandyAmount", StrValueFromAmount(candyData.nAmount, assetInfo.assetData.nDecimals)));
                    putCandyData.push_back(Pair("candyExpired", candyData.nExpired));
                    putCandyData.push_back(Pair("remarks", candyData.strRemarks));

                    txData.push_back(putCandyData);
                }
            }
            else if(header.nAppCmd == GET_CANDY_CMD)
            {
                CGetCandyData candyData;
                if(ParseGetCandyData(vData, candyData))
                {
                    CAssetId_AssetInfo_IndexValue assetInfo;
                    if(candyData.assetId.IsNull() || !GetAssetInfoByAssetId(candyData.assetId, assetInfo))
                        continue;

                    UniValue getCandyData(UniValue::VOBJ);
                    getCandyData.push_back(Pair("assetId", candyData.assetId.GetHex()));
                    getCandyData.push_back(Pair("assetCandyAmount", StrValueFromAmount(candyData.nAmount, assetInfo.assetData.nDecimals)));
                    getCandyData.push_back(Pair("remarks", candyData.strRemarks));

                    txData.push_back(getCandyData);
                }
            }
        }
    }

    ret.push_back(Pair("txData", txData));

    return ret;
}

UniValue getassetlist(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getassetlist\n"
            "Returns an object that contains an asset list.\n"
            "\nResult:\n"
            "{\n"
            "   \"assetList\":\n"
            "   [\n"
            "       \"assetId\"   (string) The asset id\n"
            "        ....\n"
            "   ]\n"
            "}\n"
            + HelpExampleCli("getassetlist", "")
            + HelpExampleRpc("getassetlist", "")
        );

    LOCK(cs_main);

    std::vector<uint256>  tempassetvector;
    GetAssetListInfo(tempassetvector);

    UniValue ret(UniValue::VOBJ);
    UniValue assetList(UniValue::VARR);
    for (unsigned int i = 0; i < tempassetvector.size(); i++)
    {
        assetList.push_back(tempassetvector[i].GetHex());
    }

    ret.push_back(Pair("assetList", assetList));

    return ret;
}

UniValue getassetlistbyaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    throw runtime_error(
        "getassetlistbyaddress \"safeAddress\" \n"
        "\nArguments:\n"
        "1. \"safeAddress\"         (string, required) The Safe address for transaction lookup\n"
        "Returns an object that contains an asset list.\n"
        "\nResult:\n"
        "{\n"
        "   \"assetList\":\n"
        "   [\n"
        "       \"assetId\"   (string) The asset id\n"
        "        ....\n"
        "   ]\n"
        "}\n"
        "\nExamples:\n"
        + HelpExampleCli("getassetlistbyaddress", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\"")
        + HelpExampleRpc("getassetlistbyaddress", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\"")
    );

    LOCK(cs_main);

    string strAddress = TrimString(params[0].get_str());
    CBitcoinAddress address(strAddress);
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

    std::vector<uint256> assetidlist;
    GetAssetIdByAddress(strAddress, assetidlist);

    UniValue ret(UniValue::VOBJ);
    UniValue assetList(UniValue::VARR);
    for (unsigned int i = 0; i < assetidlist.size(); i++)
        assetList.push_back(assetidlist[i].GetHex());

    ret.push_back(Pair("assetList", assetList));

    return ret;
}

UniValue getaddressamountbyheight(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getaddressamountbyheight height address\n"
            "\nReturns list of address and amount by specified height.\n"
            "\nArguments:\n"
            "1. \"height\"          (numeric, required) The height\n"
            "2. \"address\"         (string, required) The address\n"
            "\nResult:\n"
            "{\n"
            "   \"amount\": xxxxx\n"
            "   \"totalAmount\":xxxxx\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressamountbyheight", "700 \"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\"")
            + HelpExampleRpc("getaddressamountbyheight", "700, \"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\"")
        );

    LOCK(cs_main);

    int nHeight = params[0].get_int();
    if (nHeight <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid height");

    string strAddress = TrimString(params[1].get_str());
    if(!CBitcoinAddress(strAddress).IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");

    CAmount nAmount = 0;
    CAmount nTotalAmount = 0;
    if(!GetAddressAmountByHeight(nHeight, strAddress, nAmount) || !GetTotalAmountByHeight(nHeight, nTotalAmount))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No safe or total safe available about specified address and height");

    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("amount", ValueFromAmount(nAmount)));
    entry.push_back(Pair("totalAmount", ValueFromAmount(nTotalAmount)));

    return entry;
}

UniValue getallcandyheight(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getallcandyheight\n"
            "\nReturns all candy height.\n"
            "\nResult:\n"
            "{\n"
            "   heightList:\n"
            "   [\n"
            "       height\n"
            "       ...\n"
            "   ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getallcandyheight", "")
            + HelpExampleRpc("getallcandyheight", "")
        );

    LOCK(cs_main);

    vector<int> vHeight;
    if(pblocktree->Read_CandyHeight_TotalAmount_Index(vHeight))
        sort(vHeight.begin(), vHeight.end());

    UniValue entry(UniValue::VOBJ);
    UniValue details(UniValue::VARR);
    BOOST_FOREACH(const int& nHeight, vHeight)
        details.push_back(nHeight);

    entry.push_back(Pair("heightList", details));

    return entry;
}

UniValue getaddresscandylist(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2 || params.size() < 1)
        throw runtime_error(
            "getaddresscandylist \"assetId\" \"safeAddress\" \n"
            "\nReturns candy list by specified address, asset id .\n"
            "\nArguments:\n"
            "1. \"assetId\"         (string, required) The Safe address for transaction lookup\n"
            "2. \"safeAddress\"     (string, optional) The asset id for transaction lookup\n"
            "\nResult:\n"
            "{\n"
            "   \"candyBlockTime\":xxxxx\n"
            "   \"details\": \n"
            "   [\n"
            "       {\n"
            "           \"safeAddress\": \"xxxxx\"\n"
            "           \"candyAmount\": \"xxxxx\"\n"
            "       }\n"
            "       ...\n"
            "       {\n"
            "           \"safeAddress\": \"xxxxx\"\n"
            "           \"candyAmount\": \"xxxxx\"\n"
            "       }\n"
            "   ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddresscandylist", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" \"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\"")
            + HelpExampleRpc("getaddresscandylist", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", \"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\"")
        );

    LOCK(cs_main);

    uint256 assetId = uint256S(TrimString(params[0].get_str()));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if (assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo))
        throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

    int nCurrentHeight = g_nChainHeight;
    UniValue entry(UniValue::VOBJ);
    if (params.size() == 1)
    {
        std::map<COutPoint, std::vector<std::string>> moutpointaddress;
        GetCOutPointAddress(assetId, moutpointaddress);

        std::map<COutPoint, std::vector<std::string>>::iterator it = moutpointaddress.begin();
        for (; it != moutpointaddress.end(); it++)
        {
            int nTxHeight = GetTxHeight(it->first.hash);
            if (nTxHeight >= nCurrentHeight)
                continue;

            int64_t nTimeBegin = chainActive[nTxHeight]->GetBlockTime();
            entry.push_back(Pair("candyBlockTime", nTimeBegin));

            UniValue details(UniValue::VARR);
            std::vector<std::string>::iterator tempit = it->second.begin();
            for (; tempit != it->second.end(); tempit++)
            {
                CAmount candyamount = 0;
                if (!GetGetCandyAmount(assetId, it->first, *tempit, candyamount))
                    continue;

                UniValue addresscandyamount(UniValue::VOBJ);
                addresscandyamount.push_back(Pair("safeAddress", *tempit));
                addresscandyamount.push_back(Pair("candyAmount", StrValueFromAmount(candyamount, assetInfo.assetData.nDecimals)));

                details.push_back(addresscandyamount);
            }
            entry.push_back(Pair("details", details));
        }

    }
    else if (params.size() == 2)
    {
        string strAddress = TrimString(params[1].get_str());
        CBitcoinAddress address(strAddress);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

        std::vector<COutPoint> vcoutpoint;
        GetCOutPointList(assetId, strAddress, vcoutpoint);
        std::vector<COutPoint>::iterator it = vcoutpoint.begin();
        for (; it != vcoutpoint.end(); it++)
        {
            int nTxHeight = GetTxHeight(it->hash);
            if (nTxHeight >= nCurrentHeight)
                continue;

            int64_t nTimeBegin = chainActive[nTxHeight]->GetBlockTime();
            entry.push_back(Pair("candyBlockTime", nTimeBegin));
            CAmount candyamount = 0;

            if (!GetGetCandyAmount(assetId, *it, strAddress, candyamount))
                continue;

            UniValue details(UniValue::VARR);
            UniValue addresscandyamount(UniValue::VOBJ);
            addresscandyamount.push_back(Pair("safeAddress", strAddress));
            addresscandyamount.push_back(Pair("candyAmount", StrValueFromAmount(candyamount, assetInfo.assetData.nDecimals)));

            details.push_back(addresscandyamount);
            entry.push_back(Pair("details", details));
        }
    }

    return entry;
}

UniValue getavailablecandylist(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getavailablecandylist\n"
            "\nReturns available candy list which can be gotten.\n"
            "\nResult:\n"
            "{\n"
            "   \"candyList\": \n"
            "   [\n"
            "       {\n"
            "           \"putTime\": \"xxxxx\"\n"
            "           \"assetId\": \"xxxxx\"\n"
            "           \"assetCandyAmount\": xxxxx\n"
            "           \"candyExpired\": xxxxx\n"
            "       }\n"
            "       ...\n"
            "       {\n"
            "           \"putTime\": \"xxxxx\"\n"
            "           \"assetId\": \"xxxxx\"\n"
            "           \"assetCandyAmount\": xxxxx\n"
            "           \"candyExpired\": xxxxx\n"
            "       }\n"
            "   ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getavailablecandylist", "")
            + HelpExampleRpc("getavailablecandylist", "")
        );

    vector<CCandy_BlockTime_Info> tempAllCandyInfoVec;
    {
        std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
        tempAllCandyInfoVec.assign(gAllCandyInfoVec.begin(), gAllCandyInfoVec.end());
    }

    if(tempAllCandyInfoVec.empty())
        throw JSONRPCError(COLLECTING_CANDYLIST, "Collecting available candy list, please wait...");

    UniValue ret(UniValue::VOBJ);
    UniValue candyList(UniValue::VARR);
    for(unsigned int i = 0; i < tempAllCandyInfoVec.size(); i++)
    {
        const CCandy_BlockTime_Info& info = tempAllCandyInfoVec[i];

        UniValue candyObj(UniValue::VOBJ);
        candyObj.push_back(Pair("putTime", info.blocktime));
        candyObj.push_back(Pair("assetId", info.assetId.GetHex()));
        candyObj.push_back(Pair("assetCandyAmount", StrValueFromAmount(info.candyinfo.nAmount, info.assetData.nDecimals)));
        candyObj.push_back(Pair("candyExpired", info.candyinfo.nExpired));
        candyList.push_back(candyObj);
    }

    ret.push_back(Pair("candyList", candyList));
    return ret;
}

UniValue getlocalassetlist(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getlocalassetlist\n"
            "Returns an object that contains a local asset list.\n"
            "\nResult:\n"
            "{\n"
            "   \"assetList\":\n"
            "   [\n"
            "       \"assetId\"   (string) The asset id\n"
            "        ....\n"
            "   ]\n"
            "}\n"
            + HelpExampleCli("getlocalassetlist", "")
            + HelpExampleRpc("getlocalassetlist", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::vector<uint256> assetidlist;

    for(map<uint256, CWalletTx>::const_iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); it++)
    {
        const CWalletTx* pcoin = &(*it).second;

        if (!pcoin->IsTrusted())
            continue;

        for (unsigned int i = 0; i < pcoin->vout.size(); i++)
        {
            if(pwalletMain->IsSpent(it->first, i))
                continue;

            const CTxOut& txout = pcoin->vout[i];
            if (!txout.IsAsset())
                continue;

            uint256 assetId;
            CAppHeader header;
            vector<unsigned char> vData;
            if (ParseReserve(txout.vReserve, header, vData))
            {
                if (header.nAppCmd == ISSUE_ASSET_CMD)
                {
                    CAssetData assetData;
                    if(ParseIssueData(vData, assetData))
                        assetId = assetData.GetHash();
                }
                else if (header.nAppCmd == ADD_ASSET_CMD)
                {
                    CCommonData commonData;
                    if (ParseCommonData(vData, commonData))
                        assetId = commonData.assetId;
                }
                else if (header.nAppCmd == TRANSFER_ASSET_CMD)
                {
                    CCommonData commonData;
                    if (ParseCommonData(vData, commonData))
                        assetId = commonData.assetId;
                }
                else if (header.nAppCmd == DESTORY_ASSET_CMD)
                {
                    CCommonData commonData;
                    if(ParseCommonData(vData, commonData))
                        assetId = commonData.assetId;
                }
                else if (header.nAppCmd == CHANGE_ASSET_CMD)
                {
                    CCommonData commonData;
                    if (ParseCommonData(vData, commonData))
                        assetId = commonData.assetId;
                }
                else if (header.nAppCmd == PUT_CANDY_CMD)
                {
                    CPutCandyData candyData;
                    if (ParsePutCandyData(vData, candyData))
                        assetId = candyData.assetId;
                }
                else if (header.nAppCmd == GET_CANDY_CMD)
                {
                    CGetCandyData candyData;
                    if (ParseGetCandyData(vData, candyData))
                        assetId = candyData.assetId;
                }

                if (!assetId.IsNull())
                {
                    if (find(assetidlist.begin(), assetidlist.end(), assetId) == assetidlist.end())
                        assetidlist.push_back(assetId);
                }
            }
       }
   }

    UniValue ret(UniValue::VOBJ);
    UniValue assetList(UniValue::VARR);
    for (unsigned int i = 0; i < assetidlist.size(); i++)
        assetList.push_back(assetidlist[i].GetHex());

    ret.push_back(Pair("assetList", assetList));

    return ret;
}

UniValue transfermanyasset(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 2)
        throw runtime_error(
            "transfermanyasset \"assetId\" [{\"safeAddress\":\"xxx\",\"assetAmount\":xxxx, \"lockTime\":xxxx, \"remarks\":xxx},...]\n"
            "\nTransfer asset to multiple people.\n"
            "\nArguments:\n"
            "1. \"assetId\"            (string, required) The asset id for transfer\n"
            "2. \"receiveinfo\"        (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"safeAddress\":\"xxx\",          (string, required) The receiver's address\n"
            "         \"assetAmount\":,                 (numeric, required) The asset amount\n"
            "         \"lockTime\":n,                   (numeric, optional) The locked monthes\n"
            "         \"remarks\":                      (string, optional) The remarks\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\": \"xxxxx\"  (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("transfermanyasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" \"[{\\\"safeAddress\\\":\\\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\\\",\\\"assetAmount\\\":1000,\\\"lockTime\\\":6,\\\"remarks\\\":\\\"This is a test\\\"},{\\\"safeAddress\\\":\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\",\\\"assetAmount\\\":1000,\\\"lockTime\\\":6,\\\"remarks\\\":\\\"This is a test\\\"}]\"")
            + HelpExampleCli("transfermanyasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" \"[{\\\"safeAddress\\\":\\\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\\\",\\\"assetAmount\\\":1000},{\\\"safeAddress\\\":\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\",\\\"assetAmount\\\":1000}]\"")
            + HelpExampleRpc("transfermanyasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", \"[{\\\"safeAddress\\\":\\\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\\\",\\\"assetAmount\\\":1000,\\\"lockTime\\\":6,\\\"remarks\\\":\\\"This is a test\\\"},{\\\"safeAddress\\\":\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\",\\\"assetAmount\\\":1000,\\\"lockTime\\\":6,\\\"remarks\\\":\\\"This is a test\\\"}]\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint256 assetId = uint256S(TrimString(params[0].get_str()));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo, false))
        throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

    UniValue receiveinfo = params[1].get_array();
    CAmount totalassetamount = 0;
    string strtempRemarks = "";

    vector<CRecipient> vecSend;
    for (unsigned int i = 0; i < receiveinfo.size(); i++)
    {
        const UniValue& input = receiveinfo[i];
        const UniValue& o = input.get_obj();

        const UniValue& vsafeaddress = find_value(o, "safeAddress");
        if (!vsafeaddress.isStr())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing safeAddress key");

        string strAddress = vsafeaddress.get_str();
        CBitcoinAddress address(strAddress);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Safe address");

        const UniValue& vamount = find_value(o, "assetAmount");
        CAmount nAmount = AmountFromValue(vamount, assetInfo.assetData.nDecimals, true);
        if (nAmount <= 0)
            throw JSONRPCError(INVALID_ASSET_AMOUNT, "Invalid asset amount");
        totalassetamount += nAmount;

        const UniValue& vlockmonth = find_value(o, "lockTime");
        int nLockedMonth = 0;
        if (!vlockmonth.isNull())
        {
            if (!vlockmonth.isNum())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter:lockTime");
            nLockedMonth = vlockmonth.get_int();
            if (nLockedMonth != 0 && !IsLockedMonthRange(nLockedMonth))
                throw JSONRPCError(INVALID_LOCKEDMONTH, "Invalid locked month (min: 0, max: 120)");
        }

        string strRemarks = "";
        const UniValue& vremarks = find_value(o, "remarks");
        if (!vremarks.isNull())
        {
            if (!vremarks.isStr())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter:remarks");

            if (!vremarks.get_str().empty())
                strRemarks = vremarks.get_str();
        }

        CRecipient recvRecipient = {GetScriptForDestination(address.Get()), nAmount, nLockedMonth, false, true, strRemarks};
        vecSend.push_back(recvRecipient);
    }

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), TRANSFER_ASSET_CMD);
    CCommonData transferData(assetId, totalassetamount, strtempRemarks);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(pwalletMain->GetBalance() <= 0)
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    if(pwalletMain->GetBalance(true, &assetId) < totalassetamount)
        throw JSONRPCError(INSUFFICIENT_ASSET, "Insufficient asset funds");

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &transferData, vecSend, NULL, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        if(nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Transfer asset failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue getassetlocaltxlist(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getassetlocaltxlist \"assetId\" txClass\n"
            "\nReturns list of local transactions by specified asset id and transaction type.\n"
            "\nArguments:\n"
            "1. \"assetId\"             (string, required) The asset id for transaction lookup\n"
            "2. txClass                 (numeric, required) The transaction type (1=all, 2=normal, 3=locked,4=issue,5=addissue,6=destory)\n"
            "\nResult:\n"
            "{\n"
            "    \"txList\":\n"
            "    [\n"
            "        \"txId\"\n"
            "        ,...\n"
            "    ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getassetlocaltxlist", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 1")
            + HelpExampleRpc("getassetlocaltxlist", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", 3")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 assetId = uint256S(TrimString(params[0].get_str()));
    uint8_t nTxClass = (uint8_t)params[1].get_int();
    if(nTxClass < 1 || nTxClass > sporkManager.GetSporkValue(SPORK_105_TX_CLASS_MAX_VALUE))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid type of transaction");

    vector<COutPoint> vOut;
    if(!GetTxInfoByAssetIdTxClass(assetId, nTxClass, vOut))
        throw JSONRPCError(GET_TXID_FAILED, "No transaction available about asset");

    std::vector<uint256> vTx;
    BOOST_FOREACH(const COutPoint& out, vOut)
    {
        if(find(vTx.begin(), vTx.end(), out.hash) == vTx.end())
            vTx.push_back(out.hash);
    }

    std::vector<uint256> vlocalTx;
    std::vector<uint256>::iterator it = vTx.begin();
    for (; it != vTx.end(); it++)
    {
        std::map<uint256, CWalletTx>::iterator tempit = pwalletMain->mapWallet.find(*it);
        if (tempit != pwalletMain->mapWallet.end())
            vlocalTx.push_back(*it);
    }

    UniValue ret(UniValue::VOBJ);
    UniValue transactionList(UniValue::VARR);
    for(unsigned int i = 0; i < vlocalTx.size(); i++)
        transactionList.push_back(vlocalTx[i].GetHex());
    ret.push_back(Pair("txList", transactionList));

    return ret;
}
