
// Copyright (c) 2018-2018 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>

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
#include "coincontrol.h"
#include "../contract/virtual_account.h"
#include "../contract/contract_db.h"
#include <boost/regex.hpp>

using namespace std;

void EnsureWalletIsUnlocked();
bool EnsureWalletIsAvailable(bool avoidException);

bool _isAddressOrPubKey(const string &ks)
{
#ifdef ENABLE_WALLET
        // Case 1: Safe address and we have full public key:
        CBitcoinAddress address(ks);
        if (pwalletMain && address.IsValid())
        {
            CKeyID keyID;
            if (!address.GetKeyID(keyID))
                return false;
            CPubKey vchPubKey;
            if (!pwalletMain->GetPubKey(keyID, vchPubKey))
                return false;
            if (!vchPubKey.IsFullyValid())
                return false;
            return true;
        }

        // Case 2: hex public key
        else
#endif
        if (IsHex(ks))
        {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid())
                return false;
            return true;
        }
        return false;
}

UniValue createvirtualaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 4 )
        throw runtime_error(
            "createvirtualaccount \"safeaddress\" \"virtualaccountname\" \"ownerkey\" \"activekey\"\n"
            "\nreturn transaction id.\n"
            "\nArguments:\n"
            "1. \"safeaddress\"      (string, required) The Safe address associated with the virtual account\n"
            "2. \"virtualaccountname\"           (string, required) The virtual account name\n"
            "3. \"ownerkey\"           (string, required) public key that represents the virtual account owner\n"
            "4. \"activekey\"           (string, optional) active key value\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\": \"xxxxx\"  (string) The transaction id\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("createvirtualaccount", "\"XhMGn2Rdwk2nETZSQJFNEYiTHfNx242YRe\" \"safe.hello\" \"Xp6TrKebQWmyKTqWjCcXmM9a8Z1jumdxiy\" \"XdCUAkguQ2nWGcjzRLqWPncS1bLWePfVpn\"") + 
            HelpExampleCli("createvirtualaccount", "\"XhMGn2Rdwk2nETZSQJFNEYiTHfNx242YRe\" \"safe.hello\" \"Xp6TrKebQWmyKTqWjCcXmM9a8Z1jumdxiy\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");
    
    string strSafeAddress = TrimString(params[0].get_str());
    if(!CBitcoinAddress(strSafeAddress).IsValid())
        throw JSONRPCError(INVALID_ADDRESS, "Invalid safeaddress");
    
    uint256 virtualAccountId;
    if(GetAccountIdBySafeAddress(strSafeAddress, virtualAccountId))
        throw JSONRPCError(EXISTENT_VIRTUAL_ACCOUNT, "Safeadress already has a corresponding virtual account.");
        
    string strVirtualAccountName = TrimString(params[1].get_str());
    boost::regex regvirtualaccountname("[a-zA-Z\u4e00-\u9fa5][a-zA-Z0-9\u4e00-\u9fa5.]{1,50}");
    if (strVirtualAccountName.size() < MIN_VIRTAUL_ACCOUNT_NAME_SIZE ||
        strVirtualAccountName.size() > MAX_VIRTAUL_ACCOUNT_NAME_SIZE ||
        !boost::regex_match(strVirtualAccountName, regvirtualaccountname))
        throw JSONRPCError(INVALID_VIRTUAL_ACCOUNT_NAME_SIZE, "Invalid virtual account name.");

    if (ExistVirtualAccountName(strVirtualAccountName))
        throw JSONRPCError(EXISTENT_VIRTUAL_ACCOUNT_NAME, "Existent virtual account name.");

    string ownerks = TrimString(params[2].get_str());
    if (!_isAddressOrPubKey(ownerks))
        throw JSONRPCError(INVALID_ADDRESS, "Invalid owner key.");

    string activeks;
    if (params.size() > 2)
    {
        activeks = TrimString(params[3].get_str());
        if (activeks.size() != 0 && !_isAddressOrPubKey(activeks))
            throw JSONRPCError(INVALID_ADDRESS, "Invalid active key.");
    }

    // if(IsKeyWord(strAppName))
        // throw JSONRPCError(INVALID_APPNAME_SIZE, "Application name is internal reserved words, not allowed to use");

    CVirtualAccountData virtualAcountData(strSafeAddress, strVirtualAccountName, ownerks, activeks);
    virtualAccountId = virtualAcountData.GetHash();
    if(ExistVirtualAccountId(virtualAccountId))
        throw JSONRPCError(EXISTENT_VIRTUAL_ACCOUNT_ID, "Existent virtual account id.");

    CAppHeader appHeader(g_nAppHeaderVersion, virtualAccountId, CREATE_VIRUTAL_ACCOUNT_CMD);

    EnsureWalletIsUnlocked();
    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    // int nOffset = g_nChainHeight - g_nProtocolV2Height;
    // if (nOffset < 0)
        // throw JSONRPCError(INVALID_CANCELLED_SAFE, strprintf("This feature is enabled when the block height is %d", g_nProtocolV2Height));

    CAmount nCancelledValue = 0.001 * COIN;
    if(nCancelledValue >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CBitcoinAddress cancelledAddress(g_strCancelledSafeAddress);
    CScript cancelledScriptPubKey = GetScriptForDestination(cancelledAddress.Get());
    CRecipient cancelledRecipient = {cancelledScriptPubKey, nCancelledValue, 0, false, false};
    CRecipient adminRecipient = {CScript(), nCancelledValue, 0, false, false};
    vecSend.push_back(cancelledRecipient);
    vecSend.push_back(adminRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateVirtualAccountTransaction(&appHeader, &virtualAcountData, vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS))
    {
        if(nCancelledValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Create virtual account failed, please check your wallet and try again later!");

    return wtx.GetHash().GetHex();
}

UniValue getvirtualaccount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getvirtualaccount \"safeaddress\"\n"
            "\nreturn virtual account info.\n"
            "\nArguments:\n"
            "1. \"safeaddress\"      (string, required) The Safe address associated with the virtual account\n"
            "\nResult:\n"
            "{\n"
            "  \"safeAddress\": \"xxxxx\"  (string) The Safe address associated with the virtual account\n"
            "  \"virtualAccountName\": \"xxxxx\"  (string) The virtual account name associated with the virtual account\n"
            "  \"owner\": \"xxxxx\"  (string) owner key\n"
            "  \"active\": \"xxxxx\"  (string) active key\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getvirtualaccount", "\"XhMGn2Rdwk2nETZSQJFNEYiTHfNx242YRe\"")
            );

    LOCK(cs_main);

    string strSafeAddress = TrimString(params[0].get_str());
    if(!CBitcoinAddress(strSafeAddress).IsValid())
        throw JSONRPCError(INVALID_ADDRESS, "Invalid safeaddress");

    // read from level db
    // uint256 accountId;
    // if(!GetAccountIdBySafeAddress(strSafeAddress, accountId))
    //     throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, "There is no corresponding virtual account for this address.");

    // CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    // if(!GetVirtualInfoByVirtualAccountId(accountId, virtualAccountInfo))
    //     throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, "There is no corresponding virtual account for this address!");

    // read from mysql
    CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    if(!GetVirtualInfoBySQL(DBQueryType::SAFE_ADDRESS, strSafeAddress, virtualAccountInfo))
        throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, "There is no corresponding virtual account for this address!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("safeAddress", virtualAccountInfo.virtualAcountData.strSafeAddress));
    ret.push_back(Pair("virtualAccountName", virtualAccountInfo.virtualAcountData.strVirtualAccountName));
    ret.push_back(Pair("owner", virtualAccountInfo.virtualAcountData.owner));
    ret.push_back(Pair("active", virtualAccountInfo.virtualAcountData.active));

    return ret;
}

UniValue getvirtualaccountbyname(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getvirtualaccountbyname \"virtualaccountname\"\n"
            "\nreturn virtual account info.\n"
            "\nArguments:\n"
            "1. \"virtualaccountname\"      (string, required) The virtual account name associated with the virtual account\n"
            "\nResult:\n"
            "{\n"
            "  \"safeAddress\": \"xxxxx\"  (string) The Safe address associated with the virtual account\n"
            "  \"virtualAccountName\": \"xxxxx\"  (string) The virtual account name associated with the virtual account\n"
            "  \"owner\": \"xxxxx\"  (string) owner key\n"
            "  \"active\": \"xxxxx\"  (string) active key\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getvirtualaccountbyname", "\"jack\"")
            );

    LOCK(cs_main);

    std::string vaccount = TrimString(params[0].get_str());

    // read from level db
    // uint256 accountId;
    // if(!GetVirtualAccountIdByAccountName(vaccount, accountId))
    //     throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, strprintf("There is no corresponding virtual account for name %s.", vaccount.c_str()));
    // CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    // if(!GetVirtualInfoByVirtualAccountId(accountId, virtualAccountInfo))
    //     throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, strprintf("There is no corresponding virtual account for name %s.", vaccount.c_str()));

    // read from mysql
    CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    if(!GetVirtualInfoBySQL(DBQueryType::VIRTUAL_ACCOUNT_NAME, vaccount, virtualAccountInfo))
        throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, "There is no corresponding virtual account for this address!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("safeAddress", virtualAccountInfo.virtualAcountData.strSafeAddress));
    ret.push_back(Pair("virtualAccountName", virtualAccountInfo.virtualAcountData.strVirtualAccountName));
    ret.push_back(Pair("owner", virtualAccountInfo.virtualAcountData.owner));
    ret.push_back(Pair("active", virtualAccountInfo.virtualAcountData.active));

    return ret;
}

UniValue sendtovirtualaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 7)
        throw runtime_error(
            "sendtovirtualaccount \"virtualAccount\" amount ( \"comment\" \"comment-to\" subtractfeefromamount use_is use_ps )\n"
            "\nSend an amount to a given address.\n"
            + HelpRequiringPassphrase() +
            "\nArguments:\n"
            "1. \"virtualAccount\" (string, required) The virtual account to send to.\n"
            "2. \"amount\"      (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "3. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
            "                             This is not part of the transaction, just kept in your wallet.\n"
            "4. \"comment-to\"  (string, optional) A comment to store the name of the person or organization \n"
            "                             to which you're sending the transaction. This is not part of the \n"
            "                             transaction, just kept in your wallet.\n"
            "5. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
            "                             The recipient will receive less amount of Safe than you enter in the amount field.\n"
            "6. \"use_is\"      (bool, optional) Send this transaction as InstantSend (default: false)\n"
            "7. \"use_ps\"      (bool, optional) Use anonymized funds only (default: false)\n"
            "\nResult:\n"
            "\"transactionid\"  (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("sendtovirtualaccount", "\"lily\" 0.1")
            + HelpExampleCli("sendtovirtualaccount", "\"joy\" 0.1 \"donation\" \"seans outpost\"")
            + HelpExampleCli("sendtovirtualaccount", "\"joy\" 0.1 \"\" \"\" true")
            + HelpExampleRpc("sendtovirtualaccount", "\"joy\", 0.1, \"donation\", \"seans outpost\"")
        );

    UniValue p(UniValue::VARR);
    std::string vaccount = TrimString(params[0].get_str());
    // uint256 accountId;
    // if(!GetVirtualAccountIdByAccountName(vaccount, accountId))
    //     throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, strprintf("There is no corresponding virtual account for name %s.", vaccount.c_str()));
    // CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    // if(!GetVirtualInfoByVirtualAccountId(accountId, virtualAccountInfo))
    //     throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, strprintf("There is no corresponding virtual account for name %s.", vaccount.c_str()));

    // read from mysql
    CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    if(!GetVirtualInfoBySQL(DBQueryType::VIRTUAL_ACCOUNT_NAME, vaccount, virtualAccountInfo))
        throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, "There is no corresponding virtual account for this address!");

    p.push_back(virtualAccountInfo.virtualAcountData.strSafeAddress);
    for (unsigned int idx = 1; idx < params.size(); ++idx) {
        p.push_back(params[idx]);
    }

    return sendtoaddress(p, false);
}

static void SendMoneyFromAddress(const CBitcoinAddress *pSafeAddress, const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew, bool fUseInstantSend=false, bool fUsePrivateSend=false)
{
    CAmount curBalance = pwalletMain->GetBalance(false, NULL, pSafeAddress);

    // Check amount
    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > curBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient amount on the address(" + pSafeAddress->ToString() + ")");

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    // Parse Safe address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    CRecipient recipient = {scriptPubKey, nValue, 0, fSubtractFeeFromAmount};
    vecSend.push_back(recipient);

    CCoinControl coinControl;
    coinControl.destChange = pSafeAddress->Get();
    if (!pwalletMain->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired, nChangePosRet,
                                         strError, &coinControl, true, fUsePrivateSend ? ONLY_DENOMINATED : ALL_COINS, fUseInstantSend, pSafeAddress)) {
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if (!pwalletMain->CommitTransaction(wtxNew, reservekey, g_connman.get(), fUseInstantSend ? NetMsgType::TXLOCKREQUEST : NetMsgType::TX))
    {
        std::vector<int> prevheights;
        BOOST_FOREACH(const CTxIn &txin, wtxNew.vin)
            prevheights.push_back(GetTxHeight(txin.prevout.hash));

        if(ExistForbidTxin((uint32_t)g_nChainHeight + 1, prevheights))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! The transaction (partial) amount has been sealed.");
        else
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
    }
}

UniValue sendfromvirtualaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 8)
        throw runtime_error(
            "sendfromvirtualaccount \"virtualAccount\" amount ( \"comment\" \"comment-to\" subtractfeefromamount use_is use_ps )\n"
            "\nSend an amount to a given address.\n"
            + HelpRequiringPassphrase() +
            "\nArguments:\n"
            "1. \"virtualAccount\" (string, required) The virtual account to send coins.\n"
            "2. \"safeaddress\" (string, required) The safe address to send to.\n"
            "3. \"amount\"      (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "4. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
            "                             This is not part of the transaction, just kept in your wallet.\n"
            "5. \"comment-to\"  (string, optional) A comment to store the name of the person or organization \n"
            "                             to which you're sending the transaction. This is not part of the \n"
            "                             transaction, just kept in your wallet.\n"
            "6. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
            "                             The recipient will receive less amount of Safe than you enter in the amount field.\n"
            "7. \"use_is\"      (bool, optional) Send this transaction as InstantSend (default: false)\n"
            "8. \"use_ps\"      (bool, optional) Use anonymized funds only (default: false)\n"
            "\nResult:\n"
            "\"transactionid\"  (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("sendfromvirtualaccount", "safe.hello \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 0.1")
            + HelpExampleCli("sendfromvirtualaccount", "safe.hello \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 0.1 \"donation\" \"seans outpost\"")
            + HelpExampleCli("sendfromvirtualaccount", "safe.hello \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 0.1 \"\" \"\" true")
            + HelpExampleRpc("sendfromvirtualaccount", "safe.hello \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\", 0.1, \"donation\", \"seans outpost\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);
    std::string vaccount = TrimString(params[0].get_str());
    // uint256 accountId;
    // if(!GetVirtualAccountIdByAccountName(vaccount, accountId))
    //     throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, strprintf("There is no corresponding virtual account for name %s.", vaccount.c_str()));
    // CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    // if(!GetVirtualInfoByVirtualAccountId(accountId, virtualAccountInfo))
    //     throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, strprintf("There is no corresponding virtual account for name %s.", vaccount.c_str()));

    // read from mysql
    CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    if(!GetVirtualInfoBySQL(DBQueryType::VIRTUAL_ACCOUNT_NAME, vaccount, virtualAccountInfo))
        throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, "There is no corresponding virtual account for this address!");

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    CBitcoinAddress address(params[1].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Safe address");

    // Amount
    CAmount nAmount = AmountFromValue(params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
        wtx.mapValue["comment"] = params[3].get_str();
    if (params.size() > 4 && !params[4].isNull() && !params[4].get_str().empty())
        wtx.mapValue["to"]      = params[4].get_str();

    bool fSubtractFeeFromAmount = false;
    if (params.size() > 5)
        fSubtractFeeFromAmount = params[5].get_bool();

    bool fUseInstantSend = false;
    bool fUsePrivateSend = false;
    if (params.size() > 6)
        fUseInstantSend = params[6].get_bool();
    if (params.size() > 7)
        fUsePrivateSend = params[7].get_bool();

    EnsureWalletIsUnlocked();

    CBitcoinAddress safeAddress(virtualAccountInfo.virtualAcountData.strSafeAddress);
    if (!safeAddress.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Safe address");
    SendMoneyFromAddress(&safeAddress, address.Get(), nAmount, fSubtractFeeFromAmount, wtx, fUseInstantSend, fUsePrivateSend);

    return wtx.GetHash().GetHex();
}

UniValue listvirtualaccount(const UniValue& params, bool fHelp)
{
     if (fHelp || params.size() > 2)
        throw runtime_error(
            "listvirtualaccount \"lower_bound\" limit\n"
            "\nReturn to virtual account list\n"
            "\nArguments:\n"
            "1. \"lower_bound\"    (string, optional) Specify the lower limit of the list, default value is ""\n"
            "2. \"limit\"     (numeric, optional) Maximum number of results, defualt value is 20\n"
            "\nResult:\n"
            "[\n"
            "   {\n"
            "     \"safeAddress:\"\"...\"\n"
            "     \"virtualAccountName:\"\"...\"\n"
            "     \"owner:\"\"...\"\n"
            "     \"active:\"\"...\"\n"
            "   }\n"
            "   ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listvirtualaccount", "")
            + HelpExampleRpc("listvirtualaccount", "safe")
            + HelpExampleRpc("listvirtualaccount", "safe 10")
        );

    LOCK(cs_main);
    std::string vaccount;
    if (params.size() > 0)
        vaccount = params[0].get_str();

    int limit = 20;
    if (params.size() > 1)
        limit =  stoi(params[1].get_str());
    if (limit < 0 || limit > 100)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid limit");

    //read from level db
    // std::map<std::string, uint256> virtualAccountIds;
    // if (!GetVirtualAccountsIdListByAccountName(virtualAccountIds))
    //     throw JSONRPCError(GET_VIRTUALACCOUNTID_FAILED,  strprintf("No virtual account virtual available for name %s.", vaccount.c_str()));

    // UniValue ret(UniValue::VARR);
    // int cnt = 0;
    // CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    // for(auto ite = virtualAccountIds.begin(); ite != virtualAccountIds.end(); ++ite)
    // {
    //     if (ite->first < vaccount)
    //         continue;
    //     if (!GetVirtualInfoByVirtualAccountId(ite->second, virtualAccountInfo))
    //         throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, strprintf("There is no corresponding virtual account for name %s.", vaccount.c_str()));
    //     UniValue obj(UniValue::VOBJ);
    //     obj.push_back(Pair("safeAddress", virtualAccountInfo.virtualAcountData.strSafeAddress));
    //     obj.push_back(Pair("virtualAccountName", virtualAccountInfo.virtualAcountData.strVirtualAccountName));
    //     obj.push_back(Pair("owner", virtualAccountInfo.virtualAcountData.owner));
    //     obj.push_back(Pair("active", virtualAccountInfo.virtualAcountData.active));
    //     ret.push_back(obj);
    //     if (++cnt >= limit)
    //         break;
    // }

    //read from mysql
    map<string, CVirtualAccountId_Accountinfo_IndexValue> mVirtualAccountInfo;
    GetVirtualInfoListBySQL(vaccount, mVirtualAccountInfo, limit);

    int cnt = 0;
    UniValue ret(UniValue::VARR);
    for(auto ite = mVirtualAccountInfo.begin(); ite != mVirtualAccountInfo.end(); ++ite)
    {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("safeAddress", ite->second.virtualAcountData.strSafeAddress));
        obj.push_back(Pair("virtualAccountName", ite->second.virtualAcountData.strVirtualAccountName));
        obj.push_back(Pair("owner", ite->second.virtualAcountData.owner));
        obj.push_back(Pair("active", ite->second.virtualAcountData.active));
        ret.push_back(obj);
        if (++cnt >= limit)
            break;
    }
    return ret;
}