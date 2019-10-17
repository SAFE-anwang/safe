#include <univalue.h>

#include "safeccta.h"
#include "init.h"
#include "validation.h"
#include "utilmoneystr.h"
#include "rpc/server.h"
#include "wallet/wallet.h"
#include "main.h"
#include "masternode-sync.h"
#include "primitives/transaction.h"
#include "script/sign.h"
#include "app/app.h"



#include <boost/assign/list_of.hpp>


using namespace std;

void EnsureWalletIsUnlocked();
bool EnsureWalletIsAvailable(bool avoidException);

UniValue cctasafe(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 5)
        throw runtime_error(
            "cctasafe amount \"scAccount\" \"targetChain\" \"sepChar\" \"bizRemarks\" \n"
            "\nDestruction cross-chain safe assets \n"
            + HelpRequiringPassphrase() +
            "\nArguments:\n"
            "1.\"amount\"             (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "2.\"scAccount\"          (string, required) The account name\n"
            "3.\"targetChain\"        (string, required) The target chain name\n"
            "4.\"sepChar\"            (string, required) The sepChar\n"
            "5.\"bizRemarks\"         (string, required) The remarks"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cctasafe", "100 \"scAccount\" \"targetChain\" \"sepChar\" \"bizRemarks\"")
            + HelpExampleRpc("cctasafe", "100, \"scAccount\", \"targetChain\", \"sepChar\", \"bizRemarks\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    CAmount nAmount = AmountFromValue(params[0]);
    if (nAmount <= 0 || nAmount > MAX_MONEY)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount, minimum: 0.00000001 SAFE, maximum: 37000000 SAFE");

    CAmount curBalance = pwalletMain->GetBalance();
    if (nAmount > curBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    CCCTAInfo tempCCTAInfo;
    tempCCTAInfo.strscAccount = TrimString(params[1].get_str());
    tempCCTAInfo.strtargetChain = TrimString(params[2].get_str());
    tempCCTAInfo.strsepChar = TrimString(params[3].get_str());
    tempCCTAInfo.strbizRemarks = TrimString(params[4].get_str());

    std::string strMemo = "";
    strMemo = FillCCCTAInfo(tempCCTAInfo);

    EnsureWalletIsUnlocked();

    vector<CRecipient> vecSend;
    CBitcoinAddress addrCCTA(g_straddrCCTA);
    CScript CCTAScriptPubKey = GetScriptForDestination(addrCCTA.Get());
    CRecipient CCTARecipient = {CCTAScriptPubKey, nAmount, 0, false, false, strMemo, 2000};
    vecSend.push_back(CCTARecipient);

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    int nChangePosRet = -1;
    CWalletTx wtx;
    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError)) {
        if (nAmount + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    if (!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue cctaasset(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 6)
        throw runtime_error(
            "cctaasset  \"assetId\" assetAmount \"scAccount\" \"targetChain\" \"sepChar\" \"bizRemarks\"\n"
            "\nDestruction of cross-chain security assets.\n"
            "\nArguments:\n"
            "1. \"assetId\"           (string, required) The asset id\n"
            "2. assetAmount           (numeric, required) The asset amount\n"
            "3.\"scAccount\"          (string, required) The account name\n"
            "4.\"targetChain\"        (string, required) The target chain name\n"
            "5.\"sepChar\"            (string, required) The sepChar\n"
            "6.\"bizRemarks\"         (string, required) The remarks"
            "\nResult:\n"
            "{\n"
            "  \"txId\": \"xxxxx\"  (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("cctaasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 400 \"scAccount\" \"targetChain\" \"sepChar\" \"bizRemarks\"")
            + HelpExampleRpc("cctaasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", 400, \"scAccount\", \"targetChain\", \"sepChar\", \"bizRemarks\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint256 assetId = uint256S(TrimString(params[0].get_str()));
    CAssetId_AssetInfo_IndexValue assetInfo;
    if(assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo, false))
        throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

    CAmount nAmount = AmountFromValue(params[1], assetInfo.assetData.nDecimals, true);
    if(nAmount <= 0)
        throw JSONRPCError(INVALID_ASSET_AMOUNT, "Invalid asset amount");

    CCCTAInfo tempCCTAInfo;
    tempCCTAInfo.strscAccount = TrimString(params[2].get_str());
    tempCCTAInfo.strtargetChain = TrimString(params[3].get_str());
    tempCCTAInfo.strsepChar = TrimString(params[4].get_str());
    tempCCTAInfo.strbizRemarks = TrimString(params[5].get_str());

    std::string strMemo = "";
    strMemo = FillCCCTAInfo(tempCCTAInfo);

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), TRANSFER_ASSET_CMD);
    CCommonData transferData(assetId, nAmount, strMemo);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(pwalletMain->GetBalance() <= 0)
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    if(pwalletMain->GetBalance(true, &assetId) < nAmount)
        throw JSONRPCError(INSUFFICIENT_ASSET, "Insufficient asset funds");

    vector<CRecipient> vecSend;
    CBitcoinAddress addrCCTA(g_straddrCCTA);
    CScript CCTAScriptPubKey = GetScriptForDestination(addrCCTA.Get());
    CRecipient recvRecipient = {CCTAScriptPubKey, nAmount, 0, false, true, strMemo, 2001};
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

UniValue regcrosschaincoinageinfo(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "regcrosschaincoinageinfo [{\"scTxHash\":\"xxx\",\"assetName\":\"xxx\",\"assetId\":\"xxx\",\"quantity\":xxxx,\"safeUser\":\"xxx\"},...] \n"
            "\nRegistration cross-chain coinage information\n"
            + HelpRequiringPassphrase() +
            "\nArguments:\n"
            "1. \"regInfoes\"    (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"scTxHash\":\"xxx\",      (string, required) The scTxHash.\n"
            "         \"assetName\":\"xxx\",     (string, required) The asset name.\n"
            "         \"assetId\":\"xxx\",       (string, optional) The assetId.\n"
            "         \"quantity\":n,            (numeric, required) The quantity.\n"
            "         \"safeUser\":\"xxx\",      (string, required) The safe user.\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("regcrosschaincoinageinfo", "\"[{\\\"scTxHash\\\":\\\"6fd3a56c4570bd003cbbe2baf20dbc3d466f661dbbd52cc3f986a0a379e170cc\\\",\\\"assetName\\\":\\\"PTX\\\",\\\"assetId\\\":\\\"dc1884981a18260303737d0a492b12de69b7928d8711399faf2d84ce394379b7\\\",\\\"quantity\\\":1000000, \\\"safeUser\\\":\\\"XhzgyQjY2KVCVN9svnLknkDMyUoYUiVtcJ\\\"}]\"")
            + HelpExampleRpc("regcrosschaincoinageinfo", "\"[{\\\"scTxHash\\\":\\\"6fd3a56c4570bd003cbbe2baf20dbc3d466f661dbbd52cc3f986a0a379e170cc\\\",\\\"assetName\\\":\\\"PTX\\\",\\\"assetId\\\":\\\"dc1884981a18260303737d0a492b12de69b7928d8711399faf2d84ce394379b7\\\",\\\"quantity\\\":1000000, \\\"safeUser\\\":\\\"XhzgyQjY2KVCVN9svnLknkDMyUoYUiVtcJ\\\"}]\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    UniValue inputs = params[0].get_array();

    CBcctaCastCoin tempBcctaCastCoin;

    for (unsigned int i = 0; i < inputs.size(); i++)
    {
        CRegCastCoinInfoes tempregCastCoinInfoes;
        const UniValue& input = inputs[i];
        const UniValue& o = input.get_obj();
        
        const UniValue& vscTxHash = find_value(o, "scTxHash");
        if (!vscTxHash.isStr())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing safeaddress key");

        tempregCastCoinInfoes.strscTxHash = vscTxHash.get_str();;

        const UniValue& vassetName = find_value(o, "assetName");
        if (!vassetName.isStr())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing safeaddress key");

        tempregCastCoinInfoes.strassetName = vassetName.get_str();

        const UniValue& vassetId = find_value(o, "assetId");
        if (!vassetId.empty())
        {
            if (!vassetId.isStr())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing safeaddress key");

            uint256 assetId = uint256S(TrimString(vassetId.get_str()));
            CAssetId_AssetInfo_IndexValue assetInfo;
            if(assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo, false))
                throw JSONRPCError(NONEXISTENT_ASSETID, "Non-existent asset id");

            tempregCastCoinInfoes.strassetId = vassetId.get_str();
        }

        const UniValue& vquantity = find_value(o, "quantity");
        tempregCastCoinInfoes.nquantity = vquantity.get_int64();

        const UniValue& vsafeUser = find_value(o, "safeUser");
        if (!vsafeUser.isStr())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing safeaddress key");

        tempregCastCoinInfoes.strassetName = vsafeUser.get_str();
        
        tempBcctaCastCoin.vecRegInfo.push_back(tempregCastCoinInfoes);
        LogPrintf("scTxHash:%s---assetName:%s---assetId:%s---quantity:%lld--safeUser:%s\n", tempregCastCoinInfoes.strscTxHash, tempregCastCoinInfoes.strassetName, tempregCastCoinInfoes.strassetId, tempregCastCoinInfoes.nquantity, tempregCastCoinInfoes.strassetName );
    }

    std::string strMemo = "";
    strMemo = FillCBcctaCastCoin(tempBcctaCastCoin);

    EnsureWalletIsUnlocked();

    vector<CRecipient> vecSend;
    CBitcoinAddress addrCCTA(g_strsafeCastCoin);
    CScript CCTAScriptPubKey = GetScriptForDestination(addrCCTA.Get());
    CRecipient CCTARecipient = {CCTAScriptPubKey, BCCTA_OUT_VALUE, 0, false, false, strMemo, 3000};
    vecSend.push_back(CCTARecipient);

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    int nChangePosRet = -1;
    CWalletTx wtx;
    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError)) {
        if (BCCTA_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    if (!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue getbcctasafe(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getbcctasafe \"txid\" \n"
            "\nreceive cross-chain safe assets\n"
            + HelpRequiringPassphrase() +
            "\nArguments:\n"
            "1. \"remarks\"         (string, required) The remarks\n"
            "2. \"transferinfo\"    (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"safeUser\":\"xxx\",         (string, required) The safe address to send to.\n"
            "         \"amount\":n,                 (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getbcctasafe", "\"5185f1281d8dfed562543dc8d8e3af7db4a4b4eec5e3201ab0ef9fc681bd8019\" \"[{\\\"safeUser\\\":\\\"Xy2m1dQCatw23HasWwmEp84woBS1sfoGDH\\\",\\\"amount\\\":0.1},{\\\"safeUser\\\":\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\",\\\"amount\\\":0.1}]\"")
            + HelpExampleRpc("getbcctasafe", "\"5185f1281d8dfed562543dc8d8e3af7db4a4b4eec5e3201ab0ef9fc681bd8019\", \"[{\\\"safeUser\\\":\\\"Xy2m1dQCatw23HasWwmEp84woBS1sfoGDH\\\",\\\"amount\\\":0.1},{\\\"safeUser\\\":\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\",\\\"amount\\\":0.1}]\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    string strRemarks = "";
    strRemarks = TrimString(params[0].get_str());

    EnsureWalletIsUnlocked();

    if (params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 2 must be non-null");

    UniValue inputs = params[1].get_array();
    CAmount totalmoney = 0;

    vector<CRecipient> vecSend;
    for (unsigned int addressid = 0; addressid < inputs.size(); addressid++)
    {
        const UniValue& input = inputs[addressid];
        const UniValue& o = input.get_obj();

        const UniValue& vsafeaddress = find_value(o, "safeUser");
        if (!vsafeaddress.isStr())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing safeaddress key");

        string strAddress = vsafeaddress.get_str();
        CBitcoinAddress address(strAddress);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Safe address");

        const UniValue& vamount = find_value(o, "amount");
        CAmount nSendAmount = AmountFromValue(vamount);
        if (nSendAmount <= 0 || nSendAmount > MAX_MONEY)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid locked amount, minimum: 0.00000001 SAFE, maximum: 37000000 SAFE");

        CAmount curBalance = pwalletMain->GetBalance();
        if (nSendAmount > curBalance)
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

        totalmoney += nSendAmount;

        CScript scriptPubKey = GetScriptForDestination(address.Get());

        CRecipient recipient = {scriptPubKey, nSendAmount, 0, false, false, strRemarks, 3001};
        vecSend.push_back(recipient);
    }

    if (totalmoney > pwalletMain->GetBalance())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    int nChangePosRet = -1;
    CWalletTx wtx;
    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError)) {
        if (totalmoney + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    if (!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}

UniValue getbcctaasset(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 3)
        throw runtime_error(
            "getbcctaasset \"assetId\" addAmount \"remarks\"\n"
            "\nAdd issue.\n"
            "\nArguments:\n"
            "1. \"assetId\"         (string, required) The asset id\n"
            "2. assetAmount         (numeric, required) The asset amount\n"
            "3. \"remarks\"         (string, required) The remarks\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\": \"xxxxx\"  (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getbcctaasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\" 400 \"4b345d23558b8a6a35f85b7b0ed914db9c54e315946c6ce462fed41b0526f02d\"")
            + HelpExampleRpc("getbcctaasset", "\"723468197263af02cdf836aa12033864df0de857780dcb7982262efface6afdd\", 400, \"4b345d23558b8a6a35f85b7b0ed914db9c54e315946c6ce462fed41b0526f02d\"")
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
    if (nAmount <= 0 || nAmount > MAX_ASSETS)
        throw JSONRPCError(INVALID_ASSET_AMOUNT, "Invalid asset amount");

    string strRemarks = "";
    strRemarks = TrimString(params[2].get_str());

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeAssetId), GET_BCCTA_ASSET_CMD);
    CCommonData bcctaAssetData(assetId, nAmount, strRemarks);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(pwalletMain->GetBalance() <= 0)
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CRecipient assetRecipient = {GetScriptForDestination(adminAddress.Get()), nAmount, 0, false, true, "", 3002};
    vecSend.push_back(assetRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAssetTransaction(&appHeader, &bcctaAssetData, vecSend, &adminAddress, NULL, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
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
