// Copyright (c) 2018-2018 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>

#include "app.h"
#include "init.h"
#include "spork.h"
#include "validation.h"
#include "utilmoneystr.h"
#include "rpc/server.h"
#include "wallet/wallet.h"
#include "main.h"
#include "masternode-sync.h"

using namespace std;

void EnsureWalletIsUnlocked();
bool EnsureWalletIsAvailable(bool avoidException);

UniValue registerapp(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 4 || params.size() > 7)
        throw runtime_error(
            "registerapp \"appName\" \"appDesc\" devType \"devName\" \"webUrl\" \"appLogoUrl\" \"appCoverUrl\"\n"
            "\nRegister app.\n"
            "\nArguments:\n"
            "1. \"appName\"         (string, required) The app name\n"
            "2. \"appDesc\"         (string, required) The app description\n"
            "3. devType             (numeric, required) The developer type\n"
            "4. \"devName\"         (string, required) The developer name\n"
            "5. \"webUrl\"          (string, optional) The company web url\n"
            "6. \"appLogoUrl\"      (string, optional) The company app logo url\n"
            "7. \"appCoverUrl\"     (string, optional) The company app cover url\n"
            "\nResult:\n"
            "{\n"
            "  \"adminSafeAddress\": \"xxxxx\",     (string) The admin's address\n"
            "  \"appId\": \"xxxxx\",    (string) The app id\n"
            "  \"txId\": \"xxxxx\"      (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("registerapp", "\"gold\" \"Gold is an app\" 2 \"Andy.Le\"")
            + HelpExampleCli("registerapp", "\"gold\" \"Gold is an app\" 1 \"Bankledger\" \"http://www.anwang.com/\" \"http://www.anwang.com/\" \"http://www.anwang.com/\"")
            + HelpExampleRpc("registerapp", "\"gold\" \"Gold is an app\" 1 \"Bankledger\" \"http://www.anwang.com/\" \"http://www.anwang.com/\" \"http://www.anwang.com/\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    string strAppName = TrimString(params[0].get_str());
    if(strAppName.empty() || strAppName.size() > MAX_APPNAME_SIZE)
        throw JSONRPCError(INVALID_APPNAME_SIZE, "Invalid app name");

    if(IsKeyWord(strAppName))
        throw JSONRPCError(INVALID_APPNAME_SIZE, "Application name is internal reserved words, not allowed to use");

    if(ExistAppName(strAppName))
        throw JSONRPCError(EXISTENT_APPNAME, "Existent app name");

    string strAppDesc = TrimString(params[1].get_str());
    if(strAppDesc.empty() || strAppDesc.size() > MAX_APPDESC_SIZE)
        throw JSONRPCError(INVALID_APPDESC_SIZE, "Invalid app description");

    uint8_t nDevType = (uint8_t)params[2].get_int();
    if(nDevType < MIN_DEVTYPE_VALUE || nDevType > sporkManager.GetSporkValue(SPORK_101_DEV_TYPE_MAX_VALUE))
        throw JSONRPCError(INVALID_DEVTYPE_VALUE, "Invalid developer type");

    string strDevName = TrimString(params[3].get_str());
    if(strDevName.empty() || strDevName.size() > MAX_DEVNAME_SIZE)
        throw JSONRPCError(INVALID_DEVNAME_SIZE, "Invalid developer name");

    if(IsKeyWord(strDevName))
        throw JSONRPCError(INVALID_DEVNAME_SIZE, "Invalid developer name");

    string strWebUrl = "";
    string strLogoUrl = "";
    string strCoverUrl = "";
    if(nDevType == 1) // company
    {
        if(params.size() != 7)
            throw JSONRPCError(RPC_INVALID_PARAMS, "Need more company information");

        strWebUrl = TrimString(params[4].get_str());
        if(strWebUrl.empty() || strWebUrl.size() > MAX_WEBURL_SIZE || !IsValidUrl(strWebUrl))
            throw JSONRPCError(INVALID_WEBURL_SIZE, "Invalid web url");

        strLogoUrl = TrimString(params[5].get_str());
        if(strLogoUrl.empty() || strLogoUrl.size() > MAX_LOGOURL_SIZE || !IsValidUrl(strLogoUrl))
            throw JSONRPCError(INVALID_LOGOURL_SIZE, "Invalid app logo url");

        strCoverUrl = TrimString(params[6].get_str());
        if(strCoverUrl.empty() || strCoverUrl.size() > MAX_COVERURL_SIZE || !IsValidUrl(strCoverUrl))
            throw JSONRPCError(INVALID_COVERURL_SIZE, "Invalid app cover url");
    }

    CAppData appData(strAppName, strAppDesc, nDevType, strDevName, strWebUrl, strLogoUrl, strCoverUrl);
    uint256 appId = appData.GetHash();
    if(ExistAppId(appId))
        throw JSONRPCError(EXISTENT_APPID, "Existent app id");

    CAppHeader appHeader(g_nAppHeaderVersion, appId, REGISTER_APP_CMD);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    CAmount nCancelledValue = GetCancelledAmount(g_nChainHeight);
    if(!IsCancelledRange(nCancelledValue))
        throw JSONRPCError(INVALID_CANCELLED_SAFE, "Invalid cancelled safe amount");
    if(nCancelledValue + APP_OUT_VALUE >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CBitcoinAddress cancelledAddress(g_strCancelledSafeAddress);
    CScript cancelledScriptPubKey = GetScriptForDestination(cancelledAddress.Get());
    CRecipient cancelledRecipient = {cancelledScriptPubKey, nCancelledValue, 0, false, false};
    CRecipient adminRecipient = {CScript(), APP_OUT_VALUE, 0, false, false};
    vecSend.push_back(cancelledRecipient);
    vecSend.push_back(adminRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, &appData, vecSend, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        if(nCancelledValue + APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    // get admin address
    CTxDestination dest;
    if(!ExtractDestination(wtx.vin[0].prevPubKey, dest))
        throw JSONRPCError(GET_ADMIN_FAILED, "Get admin address failed!");

    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Register application failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("adminSafeAddress", CBitcoinAddress(dest).ToString()));
    ret.push_back(Pair("appId", appData.GetHash().GetHex()));
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue setappauth(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 4)
        throw runtime_error(
            "setappauth setType \"appId\" \"userSafeAddress\" appAuthCmd\n"
            "\nSet app authority.\n"
            "\nArguments:\n"
            "1. setType             (numeric, required) The set type\n"
            "2. \"appId\"           (string, required) The app id\n"
            "3. \"userSafeAddress\" (string, required) The user's address or ALL_USER\n"
            "4. appAuthCmd          (numeric, required) The app auth command\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\": \"xxxxx\"  (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("setappauth", "1 \"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\" \"ALL_USER\" 1000")
            + HelpExampleCli("setappauth", "2 \"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\" \"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" 1000")
            + HelpExampleRpc("setappauth", "2 \"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\" \"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" 1000")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint8_t nSetType = (uint8_t)params[0].get_int();
    if(nSetType < MIN_SETTYPE_VALUE || nSetType > sporkManager.GetSporkValue(SPORK_102_SET_TYPE_MAX_VALUE))
        throw JSONRPCError(INVALID_SETTYPE, "Invalid set type");

    uint256 appId = uint256S(TrimString(params[1].get_str()));
    CAppId_AppInfo_IndexValue appInfo;
    if(appId.IsNull() || !GetAppInfoByAppId(appId, appInfo, false))
        throw JSONRPCError(NONEXISTENT_APPID, "Non-existent app id");

    CBitcoinAddress adminAddress(appInfo.strAdminAddress);
    if(!IsMine(*pwalletMain, adminAddress.Get()))
        throw JSONRPCError(INSUFFICIENT_AUTH_FOR_APPCMD, "You are not the admin");

    string strUserAddress = TrimString(params[2].get_str());
    if(strUserAddress != "ALL_USER" && !CBitcoinAddress(strUserAddress).IsValid())
        throw JSONRPCError(INVALID_ADDRESS, "Invalid user's address");
    if(strUserAddress == appInfo.strAdminAddress)
        throw JSONRPCError(INVALID_ADDRESS, "Invalid user's address: user address is app owner");

    int64_t nTempAuth = params[3].get_int64();
    if(nTempAuth < 0 || (nTempAuth > sporkManager.GetSporkValue(SPORK_103_AUTH_CMD_MAX_VALUE) && nTempAuth < MIN_AUTH_VALUE) || nTempAuth > MAX_AUTH_VALUE)
        throw JSONRPCError(INVALID_AUTH, "Invalid app authority command");
    uint32_t nAuth = (uint32_t)nTempAuth;
    if(nAuth > sporkManager.GetSporkValue(SPORK_103_AUTH_CMD_MAX_VALUE) && nAuth < MIN_AUTH_VALUE)
        throw JSONRPCError(INVALID_AUTH, "Invalid app authority command");

    vector<uint32_t> vMempoolAuth;
    if(GetAuthByAppIdAddressFromMempool(appId, strUserAddress, vMempoolAuth))
    {
        if(nAuth == 0 || nAuth == 1)
        {
            if(vMempoolAuth.end() != find(vMempoolAuth.begin(), vMempoolAuth.end(), 0) || vMempoolAuth.end() != find(vMempoolAuth.begin(), vMempoolAuth.end(), 1))
                throw JSONRPCError(REPEAT_SET_AUTH, "Disable set app authority(0 or 1) command repeatedlly");
        }
        else
        {
            if(vMempoolAuth.end() != find(vMempoolAuth.begin(), vMempoolAuth.end(), nAuth))
                throw JSONRPCError(REPEAT_SET_AUTH, "Disable set app authority command repeatedlly");
        }
    }

    int nAppCmd = ADD_AUTH_CMD;
    if(nSetType == 2)
        nAppCmd = DELETE_AUTH_CMD;

    map<uint32_t, int> mapAuth;
    GetAuthByAppIdAddress(appId, strUserAddress, mapAuth);
    if(nAppCmd == ADD_AUTH_CMD && mapAuth.count(nAuth) != 0)
        throw JSONRPCError(EXISTENT_AUTH, "Existent authority");
    if(nAppCmd == DELETE_AUTH_CMD && mapAuth.count(nAuth) == 0)
        throw JSONRPCError(NONEXISTENT_AUTH, "Non-existent authority");

    CAppHeader appHeader(g_nAppHeaderVersion, appId, nAppCmd);
    CAuthData authData(nSetType, strUserAddress, nAuth);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(APP_OUT_VALUE >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CRecipient adminRecipient = {GetScriptForDestination(adminAddress.Get()), APP_OUT_VALUE, 0, false, false};
    vecSend.push_back(adminRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, &authData, vecSend, &adminAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        if(APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Set application auth failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue createextenddatatx(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 5)
        throw runtime_error(
            "createextenddatatx appTxType \"userSafeAddress\" \"appId\" appAuthCmd \"extendData\"\n"
            "\nCreate transaction which contain extend data.\n"
            "\nArguments:\n"
            "1. appTxType           (numeric, required) The app transaction type\n"
            "2. \"userSafeAddress\" (string, required) The user's address\n"
            "3. \"appId\"           (string, required) The app id\n"
            "4. appAuthCmd          (numeric, required) The app auth command\n"
            "5. \"extendata\"       (string, required) The extend data\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("createextenddatatx", "1 \"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" \"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\" 1000 \"1231fad##33\"")
            + HelpExampleRpc("createextenddatatx", "2 \"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" \"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\" 1002 \"1231fad##33\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint8_t nAppTxType = (uint8_t)params[0].get_int();
    if(nAppTxType < 1 || nAppTxType > sporkManager.GetSporkValue(SPORK_104_APP_TX_TYPE_MAX_VALUE))
        throw JSONRPCError(INVALID_APPTXTYPE, "Invalid app transaction type");

    CBitcoinAddress userAddress(TrimString(params[1].get_str()));
    if(!userAddress.IsValid())
        throw JSONRPCError(INVALID_ADDRESS, "Invalid user's address");

    if(!IsMine(*pwalletMain, userAddress.Get()))
        throw JSONRPCError(NONEXISTENT_ADDRESS, "You don't have user address");

    uint256 appId = uint256S(TrimString(params[2].get_str()));
    CAppId_AppInfo_IndexValue appInfo;
    if(appId.IsNull() || !GetAppInfoByAppId(appId, appInfo, false))
        throw JSONRPCError(NONEXISTENT_APPID, "Non-existent app id");

    int64_t nTempAuth = params[3].get_int64();
    if(nTempAuth < 0 || (nTempAuth > sporkManager.GetSporkValue(SPORK_103_AUTH_CMD_MAX_VALUE) && nTempAuth < MIN_AUTH_VALUE) || nTempAuth > MAX_AUTH_VALUE)
        throw JSONRPCError(INVALID_AUTH, "Invalid app authority command");
    uint32_t nAuth = (uint32_t)nTempAuth;
    if(nAuth < MIN_AUTH_VALUE)
        throw JSONRPCError(INVALID_AUTH, "Invalid app authority command");

    if(appInfo.strAdminAddress != userAddress.ToString())
    {
        vector<uint32_t> vMempoolAllUserAuth;
        if(GetAuthByAppIdAddressFromMempool(appId, "ALL_USER", vMempoolAllUserAuth))
        {
            if(find(vMempoolAllUserAuth.begin(), vMempoolAllUserAuth.end(), 0) != vMempoolAllUserAuth.end())
                throw JSONRPCError(INVALID_AUTH, "extenddata: all users's permission (0) are unconfirmed in memory pool");
            if(find(vMempoolAllUserAuth.begin(), vMempoolAllUserAuth.end(), 1) != vMempoolAllUserAuth.end())
                throw JSONRPCError(INVALID_AUTH, "extenddata: all users's permission (1) are unconfirmed in memory pool");
            if(find(vMempoolAllUserAuth.begin(), vMempoolAllUserAuth.end(), nAuth) != vMempoolAllUserAuth.end())
                throw JSONRPCError(INVALID_AUTH, strprintf("extenddata: all users's permission (%u) are unconfirmed in memory pool", nAuth));
        }
        vector<uint32_t> vMempoolUserAuth;
        if(GetAuthByAppIdAddressFromMempool(appId, userAddress.ToString(), vMempoolUserAuth))
        {
            if(find(vMempoolUserAuth.begin(), vMempoolUserAuth.end(), 0) != vMempoolUserAuth.end())
                throw JSONRPCError(INVALID_AUTH, "extenddata: current user's permission (0) are unconfirmed in memory pool");
            if(find(vMempoolUserAuth.begin(), vMempoolUserAuth.end(), 1) != vMempoolUserAuth.end())
                throw JSONRPCError(INVALID_AUTH, "extenddata: current user's permission (1) are unconfirmed in memory pool");
            if(find(vMempoolUserAuth.begin(), vMempoolUserAuth.end(), nAuth) != vMempoolUserAuth.end())
                throw JSONRPCError(INVALID_AUTH, strprintf("extenddata: current user's permission (%u) are unconfirmed in memory pool", nAuth));
        }

        map<uint32_t, int> mapAllUserAuth;
        GetAuthByAppIdAddress(appId, "ALL_USER", mapAllUserAuth);
        if(mapAllUserAuth.count(0) != 0)
            throw JSONRPCError(INSUFFICIENT_AUTH_FOR_ADDRESS, "All user's permissions are denied");
        else
        {
            if(mapAllUserAuth.count(1) == 0)
            {
                map<uint32_t, int> mapUserAuth;
                GetAuthByAppIdAddress(appId, userAddress.ToString(), mapUserAuth);
                if(mapUserAuth.count(0) != 0)
                    throw JSONRPCError(INSUFFICIENT_AUTH_FOR_ADDRESS, "Current User's permissions are denied");
                else
                {
                    if(mapUserAuth.count(1) == 0)
                    {
                        if(mapAllUserAuth.count(nAuth) == 0)
                        {
                            if(mapUserAuth.count(nAuth) == 0)
                                throw JSONRPCError(INSUFFICIENT_AUTH_FOR_ADDRESS, "Current User's permission is denied");
                        }
                    }
                }
            }
        }
    }

    string strExtendData = TrimString(params[4].get_str());
    if(strExtendData.empty() || strExtendData.size() > MAX_EXTENDDATAT_SIZE)
        throw JSONRPCError(INVALID_EXTENTDDATA, "Invalid extend data");

    CAppHeader appHeader(g_nAppHeaderVersion, appId, CREATE_EXTEND_TX_CMD);
    CExtendData extendData(nAuth, strExtendData);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(APP_OUT_VALUE >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CRecipient userRecipient = {GetScriptForDestination(userAddress.Get()), APP_OUT_VALUE, 0, false, false};
    vecSend.push_back(userRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, (void*)&extendData, vecSend, &userAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, nAppTxType == 2))
    {
        if(APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get(), nAppTxType == 2 ? NetMsgType::TXLOCKREQUEST : NetMsgType::TX))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Create extenddata transaction failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue getappinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getappinfo \"appId\"\n"
            "\nReturns app information by specified app id.\n"
            "\nArguments:\n"
            "1. \"appId\"               (string, required) The app id for information lookup\n"
            "\nResult:\n"
            "{\n"
            "    \"appName\": \"xxxxx\"\n"
            "    \"appDesc\": \"xxxxx\"\n"
            "    \"devType\": xxxxx\n"
            "    \"devName\": \"xxxxx\"\n"
            "    \"webUrl\": \"xxxxx\"\n"
            "    \"appLogoUrl\": \"xxxxx\"\n"
            "    \"appCoverUrl\": \"xxxxx\"\n"
            "    \"adminSafeAddress\": \"xxxxx\"\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getappinfo", "\"d12271779b72ae64d338c0a9efb176f9eb7352af2ce0ac2c76ee8cd240d2596a\"")
            + HelpExampleRpc("getappinfo", "\"d12271779b72ae64d338c0a9efb176f9eb7352af2ce0ac2c76ee8cd240d2596a\"")
        );

    LOCK(cs_main);

    uint256 appId = uint256S(TrimString(params[0].get_str()));

    CAppId_AppInfo_IndexValue appInfo;
    if(!GetAppInfoByAppId(appId, appInfo))
        throw JSONRPCError(GET_APPINFO_FAILED, "No app available about app id");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("appName", appInfo.appData.strAppName));
    ret.push_back(Pair("appDesc", appInfo.appData.strAppDesc));
    ret.push_back(Pair("devType", appInfo.appData.nDevType));
    ret.push_back(Pair("devName", appInfo.appData.strDevName));
    ret.push_back(Pair("webUrl", appInfo.appData.strWebUrl));
    ret.push_back(Pair("appLogoUrl", appInfo.appData.strLogoUrl));
    ret.push_back(Pair("appCoverUrl", appInfo.appData.strCoverUrl));
    ret.push_back(Pair("adminSafeAddress", appInfo.strAdminAddress));

    return ret;
}

UniValue getextenddata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getextenddata \"txId\"\n"
            "\nReturns list of extend data by specified transaction id.\n"
            "\nArguments:\n"
            "1. \"txId\"    (string, required) The transaction id for extend data lookup\n"
            "\nResult:\n"
            "{\n"
            "    \"extendDataList\":\n"
            "    [\n"
            "        {\n"
            "            \"appId\": \"xxxxx\","
            "            \"appData\": \"xxxxx\","
            "        }\n"
            "        ,...\n"
            "    ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getextenddata", "\"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\"")
            + HelpExampleRpc("getextenddata", "\"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\"")
        );

    LOCK(cs_main);

    uint256 txId = uint256S(TrimString(params[0].get_str()));

    vector<pair<uint256, string> > vExtendData;
    if(!GetExtendDataByTxId(txId, vExtendData))
        throw JSONRPCError(GET_EXTENTDDATAT_FAILED, "No extend data available about transaction");

    UniValue ret(UniValue::VOBJ);
    UniValue extendDataList(UniValue::VARR);
    for(unsigned int i = 0; i < vExtendData.size(); i++)
    {
        UniValue extendData(UniValue::VOBJ);
        extendData.push_back(Pair("appId", vExtendData[i].first.GetHex()));
        extendData.push_back(Pair("appData", vExtendData[i].second));
        extendDataList.push_back(extendData);
    }
    ret.push_back(Pair("extendDataList", extendDataList));

    return ret;
}

UniValue getapptxids(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 3 || params.size() < 1)
        throw runtime_error(
            "getapptxids \"appId\"\n"
            "\nReturns list of transactions by specified app id.\n"
            "\nArguments:\n"
            "1. \"appId\"           (string, required) The app id for transaction lookup\n"
            "2. \"appIdtype\"       (numeric, optional) The app type for transaction lookup\n"
            "3. \"setType\"         (numeric, optional) The set type\n"
            "\nResult:\n"
            "{\n"
            "    \"txList\":\n"
            "    [\n"
            "        \"txId\"\n"
            "        ,...\n"
            "    ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getapptxids", "\"d12271779b72ae64d338c0a9efb176f9eb7352af2ce0ac2c76ee8cd240d2596a\"")
            + HelpExampleRpc("getapptxids", "\"d12271779b72ae64d338c0a9efb176f9eb7352af2ce0ac2c76ee8cd240d2596a\"")
        );

    LOCK(cs_main);

    uint256 appId = uint256S(TrimString(params[0].get_str()));

    int appIdtype = -1;
    int setType = -1;

    if (params.size() == 2)
    {
        appIdtype = params[1].get_int();
        if (appIdtype < 1 ||appIdtype > 4)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid type of transaction");
    }
    else if (params.size() == 3)
    {
        appIdtype = params[1].get_int();
        if (appIdtype != 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid type of transaction");

        setType = params[2].get_int();
        if(setType < MIN_SETTYPE_VALUE || setType > sporkManager.GetSporkValue(SPORK_102_SET_TYPE_MAX_VALUE))
            throw JSONRPCError(INVALID_SETTYPE, "Invalid set type");
    }

    vector<COutPoint> vOut;
    if (!GetTxInfoByAppId(appId, vOut))
        throw JSONRPCError(GET_TXID_FAILED, "No transaction available about app");

    std::vector<uint256> vTx;
    BOOST_FOREACH(const COutPoint& out, vOut)
    {
        if (find(vTx.begin(), vTx.end(), out.hash) == vTx.end())
            vTx.push_back(out.hash);
    }

    UniValue ret(UniValue::VOBJ);
    UniValue transactionList(UniValue::VARR);

    if (params.size() == 1 || appIdtype == 1)
    {
        for(unsigned int i = 0; i < vTx.size(); i++)
            transactionList.push_back(vTx[i].GetHex());
        ret.push_back(Pair("txList", transactionList));
    }
    else if (appIdtype == 2)
    {
        std::vector<uint256>::iterator it = vTx.begin();
        for (; it != vTx.end(); it++)
        {
            CTransaction tx;
            uint256 hashBlock;
            if (!GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
                continue;

            BOOST_FOREACH(const CTxOut& txout, tx.vout)
            {
                if (!txout.IsApp())
                    continue;

                CAppHeader header;
                vector<unsigned char> vData;
                if (!ParseReserve(txout.vReserve, header, vData))
                    continue;

                if (header.nAppCmd == REGISTER_APP_CMD && header.appId == appId)
                {
                    transactionList.push_back((*it).GetHex());
                    break;
                }
            }
        }

        ret.push_back(Pair("txList", transactionList));
    }
    else if (appIdtype == 3)
    {
        if (setType == 1)
        {
            std::vector<uint256>::iterator it = vTx.begin();
            for (; it != vTx.end(); it++)
            {
                CTransaction tx;
                uint256 hashBlock;
                if (!GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
                    continue;

                BOOST_FOREACH(const CTxOut& txout, tx.vout)
                {
                    if (!txout.IsApp())
                        continue;

                    CAppHeader header;
                    vector<unsigned char> vData;
                    if (!ParseReserve(txout.vReserve, header, vData))
                        continue;

                    if (header.nAppCmd == ADD_AUTH_CMD && header.appId == appId)
                    {
                        transactionList.push_back((*it).GetHex());
                        break;
                    }
                }
            }

            ret.push_back(Pair("txList", transactionList));
        }
        else if (setType == 2)
        {
            std::vector<uint256>::iterator it = vTx.begin();
            for (; it != vTx.end(); it++)
            {
                CTransaction tx;
                uint256 hashBlock;
                if (!GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
                    continue;

                BOOST_FOREACH(const CTxOut& txout, tx.vout)
                {
                    if (!txout.IsApp())
                        continue;

                    CAppHeader header;
                    vector<unsigned char> vData;
                    if(!ParseReserve(txout.vReserve, header, vData))
                        continue;

                    if (header.nAppCmd == DELETE_AUTH_CMD && header.appId == appId)
                    {
                        transactionList.push_back((*it).GetHex());
                        break;
                    }
                }
            }

            ret.push_back(Pair("txList", transactionList));
        }
    }
    else if (appIdtype == 4)
    {
        std::vector<uint256>::iterator it = vTx.begin();
        for (; it != vTx.end(); it++)
        {
            CTransaction tx;
            uint256 hashBlock;
            if (!GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
                continue;

            BOOST_FOREACH(const CTxOut& txout, tx.vout)
            {
                if (!txout.IsApp())
                    continue;

                CAppHeader header;
                vector<unsigned char> vData;
                if(!ParseReserve(txout.vReserve, header, vData))
                    continue;

                if (header.nAppCmd == CREATE_EXTEND_TX_CMD && header.appId == appId)
                {
                    transactionList.push_back((*it).GetHex());
                    break;
                }
            }
        }

        ret.push_back(Pair("txList", transactionList));
    }

    return ret;
}

UniValue getaddressapptxids(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 4 || params.size() < 2)
        throw runtime_error(
            "getaddressapptxids \"safeAddress\" \"appId\"\n"
            "\nReturns list of transactions by specified address and app id.\n"
            "\nArguments:\n"
            "1. \"safeAddress\"     (string, required) The Safe address for transaction lookup\n"
            "2. \"appId\"           (string, required) The app id for transaction lookup\n"
            "3. \"appIdtype\"       (numeric, optional) The app type for transaction lookup\n"
            "4. \"setType\"         (numeric, optional) The set type\n"
            "\nResult:\n"
            "{\n"
            "    \"txList\":\n"
            "    [\n"
            "        \"txId\"\n"
            "        ,...\n"
            "    ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressapptxids", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" \"d12271779b72ae64d338c0a9efb176f9eb7352af2ce0ac2c76ee8cd240d2596a\"")
            + HelpExampleRpc("getaddressapptxids", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" \"d12271779b72ae64d338c0a9efb176f9eb7352af2ce0ac2c76ee8cd240d2596a\"")
        );

    LOCK(cs_main);

    string strAddress = TrimString(params[0].get_str());
    CBitcoinAddress address(strAddress);
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");
    uint256 appId = uint256S(TrimString(params[1].get_str()));

    int appIdtype = -1;
    int setType = -1;

    if (params.size() == 3)
    {
        appIdtype = params[2].get_int();
        if (appIdtype < 1 ||appIdtype > 4)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid type of transaction");
    }
    else if (params.size() == 4)
    {
        appIdtype = params[2].get_int();
        if (appIdtype != 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid type of transaction");

        setType = params[3].get_int();
        if(setType < MIN_SETTYPE_VALUE || setType > sporkManager.GetSporkValue(SPORK_102_SET_TYPE_MAX_VALUE))
            throw JSONRPCError(INVALID_SETTYPE, "Invalid set type");
    }

    vector<COutPoint> vOut;
    if(!GetTxInfoByAppIdAddress(appId, strAddress, vOut))
        throw JSONRPCError(GET_TXID_FAILED, "No transaction available about app with specified address");

    std::vector<uint256> vTx;
    BOOST_FOREACH(const COutPoint& out, vOut)
    {
        if(find(vTx.begin(), vTx.end(), out.hash) == vTx.end())
            vTx.push_back(out.hash);
    }

    UniValue ret(UniValue::VOBJ);
    UniValue transactionList(UniValue::VARR);

    if (params.size() == 2 || appIdtype == 1)
    {
        for(unsigned int i = 0; i < vTx.size(); i++)
            transactionList.push_back(vTx[i].GetHex());
        ret.push_back(Pair("txList", transactionList));
    }
    else if (appIdtype == 2)
    {
        std::vector<uint256>::iterator it = vTx.begin();
        for (; it != vTx.end(); it++)
        {
            CTransaction tx;
            uint256 hashBlock;
            if (!GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
                continue;

            BOOST_FOREACH(const CTxOut& txout, tx.vout)
            {
                if (!txout.IsApp())
                    continue;

                CAppHeader header;
                vector<unsigned char> vData;
                if (!ParseReserve(txout.vReserve, header, vData))
                    continue;

                if (header.nAppCmd == REGISTER_APP_CMD && header.appId == appId)
                {
                    transactionList.push_back((*it).GetHex());
                    break;
                }
            }
        }

        ret.push_back(Pair("txList", transactionList));
    }
    else if (appIdtype == 3)
    {
        if (setType == 1)
        {
            std::vector<uint256>::iterator it = vTx.begin();
            for (; it != vTx.end(); it++)
            {
                CTransaction tx;
                uint256 hashBlock;
                if (!GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
                    continue;

                BOOST_FOREACH(const CTxOut& txout, tx.vout)
                {
                    if (!txout.IsApp())
                        continue;

                    CAppHeader header;
                    vector<unsigned char> vData;
                    if (!ParseReserve(txout.vReserve, header, vData))
                        continue;

                    if (header.nAppCmd == ADD_AUTH_CMD && header.appId == appId)
                    {
                        transactionList.push_back((*it).GetHex());
                        break;
                    }
                }
            }

            ret.push_back(Pair("txList", transactionList));
        }
        else if (setType == 2)
        {
            std::vector<uint256>::iterator it = vTx.begin();
            for (; it != vTx.end(); it++)
            {
                CTransaction tx;
                uint256 hashBlock;
                if (!GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
                    continue;

                BOOST_FOREACH(const CTxOut& txout, tx.vout)
                {
                    if (!txout.IsApp())
                        continue;

                    CAppHeader header;
                    vector<unsigned char> vData;
                    if(!ParseReserve(txout.vReserve, header, vData))
                        continue;

                    if (header.nAppCmd == DELETE_AUTH_CMD && header.appId == appId)
                    {
                        transactionList.push_back((*it).GetHex());
                        break;
                    }
                }
            }

            ret.push_back(Pair("txList", transactionList));
        }
    }
    else if (appIdtype == 4)
    {
        std::vector<uint256>::iterator it = vTx.begin();
        for (; it != vTx.end(); it++)
        {
            CTransaction tx;
            uint256 hashBlock;
            if (!GetTransaction(*it, tx, Params().GetConsensus(), hashBlock, true))
                continue;

            BOOST_FOREACH(const CTxOut& txout, tx.vout)
            {
                if (!txout.IsApp())
                    continue;

                CAppHeader header;
                vector<unsigned char> vData;
                if(!ParseReserve(txout.vReserve, header, vData))
                    continue;

                if (header.nAppCmd == CREATE_EXTEND_TX_CMD && header.appId == appId)
                {
                    transactionList.push_back((*it).GetHex());
                    break;
                }
            }
        }

        ret.push_back(Pair("txList", transactionList));
    }

    return ret;
}

UniValue getapplist(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getapplist\n"
            "Returns an object that contains an app list.\n"
            "\nResult:\n"
            "{\n"
            "   \"appList\":\n"
            "   [\n"
            "       \"appId\"   (string) The app id\n"
            "        ....\n"
            "   ]\n"
            "}\n"
            + HelpExampleCli("getapplist", "")
            + HelpExampleRpc("getapplist", "")
        );

    LOCK(cs_main);

    std::vector<uint256>  vapplist;
    GetAppListInfo(vapplist);

    UniValue ret(UniValue::VOBJ);
    UniValue appList(UniValue::VARR);
    for (unsigned int i = 0; i < vapplist.size(); i++)
    {
        appList.push_back(vapplist[i].GetHex());
    }

    ret.push_back(Pair("appList", appList));

    return ret;
}

UniValue getapplistbyaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getapplistbyaddress \"safeAddress\" \n"
            "\nArguments:\n"
            "1. \"safeAddress\"         (string, required) The Safe address for transaction lookup\n"
            "Returns an object that contains an app list.\n"
            "\nResult:\n"
            "{\n"
            "   \"appList\":\n"
            "   [\n"
            "       \"appId\"   (string) The app id\n"
            "        ....\n"
            "   ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getapplistbyaddress", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\"")
            + HelpExampleRpc("getapplistbyaddress", "\"Xg1wCDXKuv4rEfsR9Ldv2qmUHSS9Ds1VCL\" ")
    );

    LOCK(cs_main);

    string strAddress = TrimString(params[0].get_str());
    CBitcoinAddress address(strAddress);
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

    std::vector<uint256>  appidlist;
    GetAppIDListByAddress(strAddress, appidlist);

    UniValue ret(UniValue::VOBJ);
    UniValue apptList(UniValue::VARR);
    for (unsigned int i = 0; i < appidlist.size(); i++)
    {
        apptList.push_back(appidlist[i].GetHex());
    }

    ret.push_back(Pair("appList", apptList));

    return ret;
}
UniValue getappdetails(const UniValue& params, bool fHelp)
{
   if (fHelp || params.size() != 1)
        throw runtime_error(
            "getappdetails \"txId\"\n"
            "\nReturns transaction details by specified transaction id.\n"
            "\nArguments:\n"
            "1. \"txId\"            (string, required) The transaction id\n"
            "\nResult:\n"
            "{\n"
            "    \"txData\":\n"
            "    [\n"
            "       {\n"
            "           \"appName\": \"xxxxx\"\n"
            "           \"appDesc\": \"xxxxx\"\n"
            "           \"devType\": xxxxx\n"
            "           \"devName\": \"xxxxx\"\n"
            "           \"webUrl\": \"xxxxx\"\n"
            "           \"appLogoUrl\": \"xxxxx\"\n"
            "           \"appCoverUrl\": \"xxxxx\"\n"
            "       }\n"
            "       ,{\n"
            "           \"setType\": xxxxx\n"
            "           \"appId\": \"xxxxx\"\n"
            "           \"userSafeAddress\": \"xxxxx\"\n"
            "           \"appAuthCmd\": xxxxx\n"
            "       }\n"
            "       ,{\n"
            "           \"userSafeAddress\": \"xxxxx\"\n"
            "           \"appId\": \"xxxxx\"\n"
            "           \"appAuthCmd\": xxxxx\n"
            "           \"extendData\": \"xxxxx\"\n"
            "       }\n"
            "        ,...\n"
            "    ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getappdetails", "\"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\"")
            + HelpExampleRpc("getappdetails", "\"19429c79dbbaabaffe99535273ae3864df0de857780dcb7982262efface6afdd\"")
        );

    LOCK(cs_main);

    uint256 txId = uint256S(TrimString(params[0].get_str()));
    CTransaction tx;
    uint256 hashBlock;
    if(!GetTransaction(txId, tx, Params().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No information available about transaction");

    UniValue ret(UniValue::VOBJ);
    UniValue txData(UniValue::VARR);

    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        if (!txout.IsApp())
            continue;

        CAppHeader header;
        vector<unsigned char> vData;
        if (ParseReserve(txout.vReserve, header, vData))
        {
            CTxDestination dest;
            if (!ExtractDestination(txout.scriptPubKey, dest))
                continue;

            if (!CBitcoinAddress(dest).IsValid())
                continue;

            string strAddress = CBitcoinAddress(dest).ToString();

            if (header.nAppCmd == REGISTER_APP_CMD)
            {
                CAppData appData;
                if (ParseRegisterData(vData, appData))
                {
                    UniValue registerapp(UniValue::VOBJ);
                    registerapp.push_back(Pair("appName", appData.strAppName));
                    registerapp.push_back(Pair("appDesc", appData.strAppDesc));
                    registerapp.push_back(Pair("devType", appData.nDevType));
                    registerapp.push_back(Pair("devName", appData.strDevName));
                    registerapp.push_back(Pair("webUrl", appData.strWebUrl));
                    registerapp.push_back(Pair("appLogoUrl", appData.strLogoUrl));
                    registerapp.push_back(Pair("appCoverUrl", appData.strCoverUrl));

                    txData.push_back(registerapp);
                }
            }
            else if (header.nAppCmd == ADD_AUTH_CMD || header.nAppCmd == DELETE_AUTH_CMD)
            {
                CAuthData authData;
                if (ParseAuthData(vData, authData))
                {
                    UniValue setappauth(UniValue::VOBJ);
                    setappauth.push_back(Pair("setType", authData.nSetType));
                    setappauth.push_back(Pair("appId", header.appId.GetHex()));
                    setappauth.push_back(Pair("userSafeAddress", authData.strUserAddress));
                    setappauth.push_back(Pair("appAuthCmd", (int64_t)authData.nAuth));

                    txData.push_back(setappauth);
                }
            }
            else if (header.nAppCmd == CREATE_EXTEND_TX_CMD)
            {
                CExtendData extendData;
                if (ParseExtendData(vData, extendData))
                {
                    UniValue createextenddatatx(UniValue::VOBJ);
                    createextenddatatx.push_back(Pair("userSafeAddress", strAddress));
                    createextenddatatx.push_back(Pair("appId", header.appId.GetHex()));
                    createextenddatatx.push_back(Pair("appAuthCmd", (int64_t)extendData.nAuth));
                    createextenddatatx.push_back(Pair("extendData", extendData.strExtendData));

                    txData.push_back(createextenddatatx);
                }
            }
        }
    }

    ret.push_back(Pair("txData", txData));

    return ret;
}

UniValue getauthlist(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getauthlist \"appId\" \"address\"\n"
            "\nReturns auth list by specified app id and address.\n"
            "\nArguments:\n"
            "1. \"appId\"               (string, required) The app id for information lookup\n"
            "2. \"address\"             (string, required) The app id for information lookup\n"
            "\nResult:\n"
            "{\n"
            "  \"ALL_USER\" : [         (array of json objects)\n"
            "           auth            (numeric) The auth \n"
            "     ,...\n"
            "  ],\n"
            "  \"CURRENT_USER\" : [     (array of json objects)\n"
            "           auth            (numeric) The auth \n"
            "     ,...\n"
            "  ],\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getauthlist", "\"d12271779b72ae64d338c0a9efb176f9eb7352af2ce0ac2c76ee8cd240d2596a\" \"Xp8AEzDHagYzwpm3NcRqt8kmSpiUa3JNc2\" ")
            + HelpExampleRpc("getauthlist", "\"d12271779b72ae64d338c0a9efb176f9eb7352af2ce0ac2c76ee8cd240d2596a\" \"Xp8AEzDHagYzwpm3NcRqt8kmSpiUa3JNc2\" ")
        );

    LOCK(cs_main);

    int curHeight = g_nChainHeight;

    uint256 appId = uint256S(TrimString(params[0].get_str()));
    if (appId.IsNull())
        throw JSONRPCError(NONEXISTENT_APPID, "Non-existent app id");

    UniValue result(UniValue::VOBJ);
    string strAddress = TrimString(params[1].get_str());
    if (strAddress == "ALL_USER")
    {
        UniValue alluserauth(UniValue::VARR);

        std::vector<uint32_t> vMempoolauthlist;
        GetAuthByAppIdAddressFromMempool(appId, "ALL_USER", vMempoolauthlist);

        std::map<uint32_t, int> mapdbauthlist;
        GetAuthByAppIdAddress(appId, "ALL_USER", mapdbauthlist);

        std::vector<uint32_t> vdbauthlist;
        for(map<uint32_t, int>::iterator it = mapdbauthlist.begin(); it != mapdbauthlist.end(); it++)
        {
            if(it->second <= curHeight)
                vdbauthlist.push_back(it->first);
        }

        BOOST_FOREACH(const uint32_t& auth, vMempoolauthlist)
        {
            if (find(vdbauthlist.begin(), vdbauthlist.end(), auth) == vdbauthlist.end())
                vdbauthlist.push_back(auth);
        }

        std::vector<uint32_t>::iterator it = vdbauthlist.begin();
        for (; it != vdbauthlist.end(); it++)
            alluserauth.push_back((int)*it);

        result.push_back(Pair("ALL_USER", alluserauth));
    }
    else
    {
        CBitcoinAddress address(strAddress);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

        UniValue alluserauth(UniValue::VARR);

        std::vector<uint32_t> vMempoolauthlist;
        GetAuthByAppIdAddressFromMempool(appId, "ALL_USER", vMempoolauthlist);

        std::map<uint32_t, int> mapdbauthlist;
        GetAuthByAppIdAddress(appId, "ALL_USER", mapdbauthlist);

        std::vector<uint32_t> vdbauthlist;
        for(map<uint32_t, int>::iterator it = mapdbauthlist.begin(); it != mapdbauthlist.end(); it++)
        {
            if(it->second <= curHeight)
                vdbauthlist.push_back(it->first);
        }

        BOOST_FOREACH(const uint32_t& auth, vMempoolauthlist)
        {
            if (find(vdbauthlist.begin(), vdbauthlist.end(), auth) == vdbauthlist.end())
                vdbauthlist.push_back(auth);
        }

        std::vector<uint32_t>::iterator it = vdbauthlist.begin();
        for (; it != vdbauthlist.end(); it++)
            alluserauth.push_back((int)*it);

        result.push_back(Pair("ALL_USER", alluserauth));

        UniValue currentuserauth(UniValue::VARR);

        std::vector<uint32_t> vcuruserMempoolauthlist;
        GetAuthByAppIdAddressFromMempool(appId, strAddress, vcuruserMempoolauthlist);

        std::map<uint32_t, int> mapdbcuruserauthlist;
        GetAuthByAppIdAddress(appId, strAddress, mapdbcuruserauthlist);

        std::vector<uint32_t> vdbcuruserauthlist;
        for(map<uint32_t, int>::iterator it = mapdbcuruserauthlist.begin(); it != mapdbcuruserauthlist.end(); it++)
        {
            if(it->second <= curHeight)
                vdbcuruserauthlist.push_back(it->first);
        }

        BOOST_FOREACH(const uint32_t& auth, vcuruserMempoolauthlist)
        {
            if (find(vdbcuruserauthlist.begin(), vdbcuruserauthlist.end(), auth) == vdbcuruserauthlist.end())
                vdbcuruserauthlist.push_back(auth);
        }

        std::vector<uint32_t>::iterator tempit = vdbcuruserauthlist.begin();
        for (; tempit != vdbcuruserauthlist.end(); tempit++)
            currentuserauth.push_back((int)*tempit);

        result.push_back(Pair("CURRENT_USER", currentuserauth));
    }

    return result;
}
