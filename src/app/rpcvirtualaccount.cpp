
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
#include "../contract/virtual_account.h"
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

    if (fHelp || params.size() != 4)
        throw runtime_error(
            "createvirtualaccount \"safeaddress\" \"virtualaccountname\" \"ownerkey\" \"activekey\"\n"
            "\nreturn transaction id.\n"
            "\nArguments:\n"
            "1. \"safeaddress\"      (string, required) The Safe address associated with the virtual account\n"
            "2. \"virtualaccountname\"           (string, required) The virtual account name\n"
            "3. \"ownerkey\"           (string, required) public key that represents the virtual account owner\n"
            "4. \"activekey\"           (string, required) active key value\n"
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

    string activeks = TrimString(params[3].get_str());
    if (activeks.size() != 0 && !_isAddressOrPubKey(activeks))
        throw JSONRPCError(INVALID_ADDRESS, "Invalid active key.");

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

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
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

    uint256 accountId;
    if(!GetAccountIdBySafeAddress(strSafeAddress, accountId))
        throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, "There is no corresponding virtual account for this address.");

    CVirtualAccountId_Accountinfo_IndexValue virtualAccountInfo;
    if(!GetVirtualInfoByVirtualAccountId(accountId, virtualAccountInfo))
        throw JSONRPCError(NO_VIRTUAL_ACCOUNT_EXIST, "There is no corresponding virtual account for this address!");


    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("virtualAccountName", virtualAccountInfo.virtualAcountData.strVirtualAccountName));
    ret.push_back(Pair("owner", virtualAccountInfo.virtualAcountData.owner));
    ret.push_back(Pair("active", virtualAccountInfo.virtualAcountData.active));

    return ret;
}
