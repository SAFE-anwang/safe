#include <univalue.h>

#include "safevote.h"
#include "init.h"
#include "validation.h"
#include "utilmoneystr.h"
#include "rpc/server.h"
#include "wallet/wallet.h"
#include "main.h"
#include "masternode-sync.h"
#include "primitives/transaction.h"
#include "script/sign.h"


#include <boost/assign/list_of.hpp>





using namespace std;

void EnsureWalletIsUnlocked();
bool EnsureWalletIsAvailable(bool avoidException);

UniValue regsupernodecandidate(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 5)
        throw runtime_error(
            "regsupernodecandidate \"scBpPubkey\" dividendRatio \"bpName\" \"bpURL\" [{\"safeTxHash\":\"txid\",\"safeTxOutIdx\":n},...]\n"
            "\nCreate transaction which registration campaign super node.\n"
            "\nArguments:\n"
            "1. \"scBpPubkey\"      (string, required) The Superblock's block public key\n"
            "2. dividendRatio       (numeric, required) Percentage of dividends for BP returns, an integer of [0, 100]\n"
            "3. \"bpName\"          (string, required) The BP name\n"
            "4. \"bpURL\"           (string, required) The URL of the BP principal\n"
            "5. \"regInfoes\"       (string, required) A json array of objects. Each object the safeTxHash (string) safeTxOutIdx (numeric)\n"
            "     [                 (json array of json objects)\n"
            "       {\n"
            "         \"safeTxHash\":\"id\",    (string) The transaction id\n"
            "         \"safeTxOutIdx\": n       (numeric) The output number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("regsupernodecandidate", "\"EOS6PiiPK2nD4GvVEJuvbhigvthYuz65YtQR5hRGu6NhmVsKTb9mC\" 50 \"testbp\" \"http://www.testbp.com/\" \"[{\\\"safeTxHash\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"safeTxOutIdx\\\":1}]\"")
            + HelpExampleRpc("regsupernodecandidate", "\"EOS6PiiPK2nD4GvVEJuvbhigvthYuz65YtQR5hRGu6NhmVsKTb9mC\", 50, \"testbp\", \"http://www.testbp.com/\", \"[{\\\"safeTxHash\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"safeTxOutIdx\\\":1}]\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    std::string strscBpPubkey = TrimString(params[0].get_str());
    if (strscBpPubkey.length() > MAX_SCBPPUBKEY_SIZE)
        throw JSONRPCError(INVALID_SCBPPUBKEY_SIZE, "Invalid scBpPubkey size");

    uint16_t ndividendRatio = (uint16_t)params[1].get_int();
    if (ndividendRatio > MAX_DIVIDENDRATIO_VALUE)
        throw JSONRPCError(INVALID_DIVIDENDRATIO_VALUE, "Invalid dividendRatio value");

    std::string strbpName = TrimString(params[2].get_str());
    if (strbpName.length() > MAX_BPNAME_SIZE)
        throw JSONRPCError(INVALID_BPNAME_SIZE, "Invalid bpName size");

    std::string strbpURL = TrimString(params[3].get_str());
    if (strbpURL.length() > MAX_BPURL_SIZE)
        throw JSONRPCError(INVALID_BPURL_SIZE, "Invalid bpURL size");

    UniValue outputs = params[4].get_array();
    unsigned int nArrayNum = outputs.size();
    if (nArrayNum != ARRAY_NUM)
        throw JSONRPCError(INVALID_ARRAY_NUM, "Invalid regInfoes size");

    std::vector<COutPoint> vecOutPoint;
    for (unsigned int idx = 0; idx < nArrayNum; idx++) {
        const UniValue& output = outputs[idx];
        if (!output.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
 
        const UniValue& o = output.get_obj();

        RPCTypeCheckObj(o, boost::assign::map_list_of("safeTxHash", UniValue::VSTR)("safeTxOutIdx", UniValue::VNUM));

        string txid = find_value(o, "safeTxHash").get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        int nOutput = find_value(o, "safeTxOutIdx").get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        COutPoint outpt(uint256S(txid), nOutput);
        vecOutPoint.push_back(outpt);
    }

    bool fFoundPXTAsset = false;
    bool fFoundDMN = false;
    bool fFoundLockTx = false;

    std::map<COutPoint,CDeterministicMasternode_IndexValue> mapAllDMN;
    GetAllDMNData(mapAllDMN);

    CRegSuperNodeCandidate tempregSuperNodeCandidate;
    tempregSuperNodeCandidate.strscBpPubkey = strscBpPubkey;
    tempregSuperNodeCandidate.ndividendRatio = ndividendRatio;
    tempregSuperNodeCandidate.strbpName = strbpName;
    tempregSuperNodeCandidate.strbpURL = strbpURL;

    vector<CBitcoinAddress> vecaddress;

    std::vector<COutPoint>::iterator it = vecOutPoint.begin();
    for (; it != vecOutPoint.end(); ++it)
    {
        COutPoint tempOutPoint = *it;

        CRegInfo tempregInfo;
        CTransaction tx;
        uint256 hashBlock;
        if (!GetTransaction(tempOutPoint.hash, tx, Params().GetConsensus(), hashBlock, true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

        const CTxOut& temptxout = tx.vout[tempOutPoint.n];
        CTxDestination dest;
        if(!ExtractDestination(temptxout.scriptPubKey, dest))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, failed to get address");

        CBitcoinAddress address(dest);
        CKeyID keyid;
        CPubKey vchPubKey;

        std::string straddress = address.ToString();
        LogPrintf("address:%s\n", straddress);

        if (!address.GetKeyID(keyid))
        {
            LogPrintf("address:%s\n", address.ToString());
            throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");
        }

        if (!pwalletMain->GetPubKey(keyid, vchPubKey))
        {
            LogPrintf("address:%s\n", address.ToString());
            throw JSONRPCError(RPC_WALLET_ERROR, "pubKey key for address is not known");
        }

        vecaddress.push_back(address);
    
        CDataStream ssPubKey(SER_DISK, CLIENT_VERSION);
        ssPubKey.reserve(1000);
        ssPubKey << vchPubKey;
        string serialPubKey = ssPubKey.str();

        tempregInfo.strsafeTxHash = tempOutPoint.hash.ToString();
        tempregInfo.nsafeTxOutIdx = tempOutPoint.n;
        strncpy(tempregInfo.vchpubkey, serialPubKey.c_str(), sizeof(tempregInfo.vchpubkey));
        tempregSuperNodeCandidate.mapsignature[straddress] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

        std::map<COutPoint,CDeterministicMasternode_IndexValue>::iterator ittemp = mapAllDMN.find(tempOutPoint);
        if (ittemp != mapAllDMN.end())
        {
            fFoundDMN = true;
            tempregInfo.nutxoType = 1;
            tempregSuperNodeCandidate.vecRegInfo.push_back(tempregInfo);
            continue;
        }

        if (temptxout.IsAsset())
        {
            CAppHeader header;
            vector<unsigned char> vData;
            if (ParseReserve(temptxout.vReserve, header, vData))
            {
                if (header.nAppCmd == TRANSFER_ASSET_CMD)
                {
                    CCommonData commonData;
                    if (ParseCommonData(vData, commonData))
                    {
                        if (commonData.assetId.IsNull())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, assetId is null");

                        if (commonData.assetId != uint256S(g_strPXTAssetID))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, assetId is not pxt asset");

                        if (temptxout.nUnlockedHeight == 0)
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, assetId is not locked");

                        tempregInfo.nutxoType = 3;
                        tempregSuperNodeCandidate.vecRegInfo.push_back(tempregInfo);
                        fFoundPXTAsset = true;
                        continue;
                    }
                }
            }
        }

        if (temptxout.IsSafeOnly())
        {
            if(mapBlockIndex.count(hashBlock) == 0 || mapBlockIndex[hashBlock]->nHeight < g_SafeVoteStartHeight)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, tx height before g_SafeVoteStartHeight");

            if (temptxout.nValue != 10000 * COIN)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, txout Value before g_SafeVoteStartHeight");

            if (temptxout.nUnlockedHeight == 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, txout is not locked");

            tempregInfo.nutxoType = 2;
            tempregSuperNodeCandidate.vecRegInfo.push_back(tempregInfo);
            fFoundLockTx = true;
            continue;
        }
    }

    if (!fFoundPXTAsset || !fFoundDMN || !fFoundLockTx)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "tx type error");

    CBitcoinAddress adminSafeAddress(g_stradminSafeAddress);

    string strExtendData = "";

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeVoteAppID), CREATE_EXTEND_TX_CMD);
    std::vector<unsigned char> vecdata;
    vecdata = FillRegSuperNodeCandidate(tempregSuperNodeCandidate);

    std::vector<unsigned char>::iterator itdata = vecdata.begin();
    for (; itdata != vecdata.end(); ++itdata)
    {
        strExtendData.push_back(*itdata);
    }

    CExtendData extendData(REG_SUPER_NODE_CANDIDATE_CMD, strExtendData);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(APP_OUT_VALUE >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CRecipient userRecipient = {GetScriptForDestination(adminSafeAddress.Get()), APP_OUT_VALUE, 0, false, false};
    vecSend.push_back(userRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, (void*)&extendData, vecSend, &adminSafeAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, false))
    {
        if(APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    COutPoint prevout = wtx.vin[0].prevout;

    CHashWriter ssrowHash(SER_GETHASH, PROTOCOL_VERSION);
    ssrowHash << prevout.hash;
    ssrowHash << prevout.n;
    ssrowHash << tempregSuperNodeCandidate.strscBpPubkey;
    ssrowHash << tempregSuperNodeCandidate.ndividendRatio;
    ssrowHash << tempregSuperNodeCandidate.strbpName;
    ssrowHash << tempregSuperNodeCandidate.strbpURL;

    std::vector<CRegInfo>::iterator itreginfo = tempregSuperNodeCandidate.vecRegInfo.begin();
    for (; itreginfo != tempregSuperNodeCandidate.vecRegInfo.end(); ++itreginfo)
    {
        uint32_t nvchpubkeyLength = sizeof(itreginfo->vchpubkey);
        for (uint32_t i = 0; i < nvchpubkeyLength; i++)
            ssrowHash << itreginfo->vchpubkey[i];
        ssrowHash << itreginfo->strsafeTxHash;
        ssrowHash << itreginfo->nsafeTxOutIdx;
        ssrowHash << itreginfo->nutxoType;
    }

    uint256 rowHash = ssrowHash.GetHash();
    LogPrintf("rowHash:%s\n", rowHash.ToString());

    tempregSuperNodeCandidate.mapsignature.clear();
    vector<CBitcoinAddress>::iterator itaddress = vecaddress.begin();
    for (; itaddress != vecaddress.end(); ++itaddress)
    {
        CKeyID keyid;
        CBitcoinAddress tempAddress = *itaddress;
        std::string strtempAddress = tempAddress.ToString();

        if (!tempAddress.GetKeyID(keyid))
            throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");
    
        CKey vchSecret;
        if (!pwalletMain->GetKey(keyid, vchSecret))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address is not known");

        std::vector<unsigned char> vchSig;
        if (!vchSecret.Sign(rowHash, vchSig))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for sign error");

        std::string strSig = "";
        std::vector<unsigned char>::iterator itch = vchSig.begin();
        for (; itch != vchSig.end(); ++itch)
            strSig.push_back(*itch);

        tempregSuperNodeCandidate.mapsignature[strtempAddress] = strSig;

        LogPrintf("strtempAddress:%s ------- strSig:%s\n", strtempAddress, strSig);
    }

    std::vector<unsigned char> vecrealdata;
    vecrealdata = FillRegSuperNodeCandidate(tempregSuperNodeCandidate);
    std::string strRealExtendData = "";

    std::vector<unsigned char>::iterator itrealdata = vecrealdata.begin();
    for (; itrealdata != vecrealdata.end(); ++itrealdata)
    {
        strRealExtendData.push_back(*itrealdata);
    }

    CExtendData extendRealData(REG_SUPER_NODE_CANDIDATE_CMD, strRealExtendData);

    CMutableTransaction tempmtx = CMutableTransaction(wtx);

    for (unsigned int i = 0; i < tempmtx.vout.size(); i++)
    {
        const CTxOut& txout = tempmtx.vout[i];

        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData))
            continue;

        if (header.nAppCmd != CREATE_EXTEND_TX_CMD)
            continue;

        CExtendData extendData;
        if (ParseExtendData(vData, extendData))
        {
            if (extendData.nAuth == REG_SUPER_NODE_CANDIDATE_CMD)
            {
                tempmtx.vout[i].vReserve = FillExtendData(appHeader, extendRealData);
                break;
            }
        }
    }

    // Sign
    int nIn = 0;
    CTransaction txNewConst(tempmtx);
    BOOST_FOREACH(const CTxIn& txin, tempmtx.vin)
    {
        bool signSuccess;
        const CScript& scriptPubKey = txin.prevPubKey;
        CScript& scriptSigRes = tempmtx.vin[nIn].scriptSig;
        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &txNewConst, nIn, SIGHASH_ALL), scriptPubKey, scriptSigRes);

        if (!signSuccess)
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: Signing transaction failed");
        }
        nIn++;
    }

    CWalletTx wRealtx;
    wRealtx.fTimeReceivedIsTxTime = true;
    wRealtx.BindWallet(pwalletMain);
    wRealtx.fFromMe = true;

    // Embed the constructed transaction data in wRealtx.
    *static_cast<CTransaction*>(&wRealtx) = CTransaction(tempmtx);

    if(!pwalletMain->CommitTransaction(wRealtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Create extenddata transaction failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wRealtx.GetHash().GetHex()));

    return ret;
}

UniValue unregsupernodecandidate(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "unregsupernodecandidate \"txid\" \n"
            "\nCreate transaction which logout campaign super node.\n"
            "\nArguments:\n"
            "1.\"txid\"      (string, required) The transaction id which regTxHash\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("unregsupernodecandidate", "\"ae05096c00a3101ec099f43e1bd8ee76603dd660199f14ce6e43968ef24a5e57\"")
            + HelpExampleRpc("unregsupernodecandidate", "\"ae05096c00a3101ec099f43e1bd8ee76603dd660199f14ce6e43968ef24a5e57\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint256 txhash = ParseHashV(params[0], "parameter 1");
    string strTxHex = txhash.ToString();

    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txhash, tx, Params().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No information available about transaction");

    const CTxOut& temptxout = tx.vout[0];
    CTxDestination dest;
    if(!ExtractDestination(temptxout.scriptPubKey, dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, failed to get address");

    CBitcoinAddress address(dest);
    std::string strAddress = "";
    strAddress = address.ToString();
    
    CKeyID keyid;
    if (!address.GetKeyID(keyid))
        throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");

    CUnRegSuperNodeCandidate tempunRegSuperNodeCandidate;
    tempunRegSuperNodeCandidate.strregTxHash = strTxHex;

    tempunRegSuperNodeCandidate.mapsignature[strAddress] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    string strExtendData = "";

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeVoteAppID), CREATE_EXTEND_TX_CMD);

    std::vector<unsigned char> vecdata;
    vecdata = FillUnregSuperNodeCandidate(tempunRegSuperNodeCandidate);

    std::vector<unsigned char>::iterator itdata = vecdata.begin();
    for (; itdata != vecdata.end(); ++itdata)
    {
        strExtendData.push_back(*itdata);
    }

    CExtendData extendData(UN_REG_SUPER_NODE_CANDIDATE_CMD, strExtendData);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(APP_OUT_VALUE >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    CBitcoinAddress adminSafeAddress(g_stradminSafeAddress);

    vector<CRecipient> vecSend;
    CRecipient userRecipient = {GetScriptForDestination(adminSafeAddress.Get()), APP_OUT_VALUE, 0, false, false};
    vecSend.push_back(userRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, (void*)&extendData, vecSend, &adminSafeAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, false))
    {
        if(APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    COutPoint prevout = wtx.vin[0].prevout;

    CHashWriter ssrowHash(SER_GETHASH, PROTOCOL_VERSION);
    ssrowHash << prevout.hash;
    ssrowHash << prevout.n;
    ssrowHash << strTxHex;

    uint256 rowHash = ssrowHash.GetHash();
    LogPrintf("rowHash:%s\n", rowHash.ToString());

    tempunRegSuperNodeCandidate.mapsignature.clear();

    CKey vchSecret;
    if (!pwalletMain->GetKey(keyid, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address is not known");

    std::vector<unsigned char> vchSig;
    if (!vchSecret.Sign(rowHash, vchSig))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for sign error");

    std::string strSig = "";
    std::vector<unsigned char>::iterator itch = vchSig.begin();
    for (; itch != vchSig.end(); ++itch)
        strSig.push_back(*itch);

    tempunRegSuperNodeCandidate.mapsignature[strAddress] = strSig;

    LogPrintf("strAddress:%s ------- strSig:%s\n", strAddress, strSig);

    std::vector<unsigned char> vecrealdata;
    vecrealdata = FillUnregSuperNodeCandidate(tempunRegSuperNodeCandidate);
    std::string strRealExtendData = "";

    std::vector<unsigned char>::iterator itrealdata = vecrealdata.begin();
    for (; itrealdata != vecrealdata.end(); ++itrealdata)
    {
        strRealExtendData.push_back(*itrealdata);
    }

    CExtendData extendRealData(UN_REG_SUPER_NODE_CANDIDATE_CMD, strRealExtendData);

    CMutableTransaction tempmtx = CMutableTransaction(wtx);

    for (unsigned int i = 0; i < tempmtx.vout.size(); i++)
    {
        const CTxOut& txout = tempmtx.vout[i];

        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData))
            continue;

        if (header.nAppCmd != CREATE_EXTEND_TX_CMD)
            continue;

        CExtendData extendData;
        if (ParseExtendData(vData, extendData))
        {
            if (extendData.nAuth == UN_REG_SUPER_NODE_CANDIDATE_CMD)
            {
                tempmtx.vout[i].vReserve = FillExtendData(appHeader, extendRealData);
                break;
            }
        }
    }

    // Sign
    int nIn = 0;
    CTransaction txNewConst(tempmtx);
    BOOST_FOREACH(const CTxIn& txin, tempmtx.vin)
    {
        bool signSuccess;
        const CScript& scriptPubKey = txin.prevPubKey;
        CScript& scriptSigRes = tempmtx.vin[nIn].scriptSig;
        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &txNewConst, nIn, SIGHASH_ALL), scriptPubKey, scriptSigRes);

        if (!signSuccess)
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: Signing transaction failed");
        }
        nIn++;
    }

    CWalletTx wRealtx;
    wRealtx.fTimeReceivedIsTxTime = true;
    wRealtx.BindWallet(pwalletMain);
    wRealtx.fFromMe = true;

    // Embed the constructed transaction data in wRealtx.
    *static_cast<CTransaction*>(&wRealtx) = CTransaction(tempmtx);

    if(!pwalletMain->CommitTransaction(wRealtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Create extenddata transaction failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wRealtx.GetHash().GetHex()));

    return ret;
}

UniValue updatesupernodecandidate(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 2)
        throw runtime_error(
            "updatesupernodecandidate \"txid\" \n"
            "\nCreate transaction which update super node candidate.\n"
            "\nArguments:\n"
            "1.\"txid\"      (string, required) The transaction id which regTxHash\n"
            "2. \"updateInfoes\"  (string, required) A json objects\n"
            "    {\n"
            "       \"dividendRatio\":n,        (numeric, required) Percentage of dividends for BP returns, an integer of [0, 100]\n"
            "       \"bpName\":\"testbpName\",  (string, required) The BP name\n"
            "       \"bpURL\": \"testbpURL\",   (string, required) The URL of the BP principal\n"
            "    }\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("updatesupernodecandidate", "\"ae05096c00a3101ec099f43e1bd8ee76603dd660199f14ce6e43968ef24a5e57\" \"{\\\"dividendRatio\\\":50,\\\"bpName\\\":\\\"testbp\\\",\\\"bpURL\\\":\\\"http://www.testbp.com\\\"}\"")
            + HelpExampleRpc("updatesupernodecandidate", "\"ae05096c00a3101ec099f43e1bd8ee76603dd660199f14ce6e43968ef24a5e57\", \"{\\\"dividendRatio\\\":50,\\\"bpName\\\":\\\"testbp\\\",\\\"bpURL\\\":\\\"http://www.testbp.com\\\"}\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint256 txhash = ParseHashV(params[0], "parameter 1");
    string strTxHex = txhash.ToString();

    CSuperNodeUpdateInfo tempsuperNodeUpdateInfo;

    if (!params[1].isObject())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "params[1] is expected to be a json objects");

    const UniValue& o = params[1].get_obj();

    RPCTypeCheckObj(o, boost::assign::map_list_of("dividendRatio", UniValue::VNUM)("bpName", UniValue::VSTR)("bpURL", UniValue::VSTR));

    uint16_t ndividendRatio = (uint16_t)find_value(o, "dividendRatio").get_int();
    if (ndividendRatio > MAX_DIVIDENDRATIO_VALUE)
        throw JSONRPCError(INVALID_DIVIDENDRATIO_VALUE, "Invalid dividendRatio value");

    std::string strbpName = find_value(o, "bpName").get_str();
    if (strbpName.length() > MAX_BPNAME_SIZE)
        throw JSONRPCError(INVALID_BPNAME_SIZE, "Invalid bpName size");

    std::string strbpURL = find_value(o, "bpURL").get_str();
    if (strbpURL.length() > MAX_BPURL_SIZE)
        throw JSONRPCError(INVALID_BPURL_SIZE, "Invalid bpURL size");

    tempsuperNodeUpdateInfo.ndividendRatio = ndividendRatio;
    tempsuperNodeUpdateInfo.strbpName = strbpName;
    tempsuperNodeUpdateInfo.strbpURL = strbpURL;

    LogPrintf("ndividendRatio:%d---strbpName:%s-----strbpURL:%s\n", ndividendRatio, strbpName, strbpURL);
        
    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txhash, tx, Params().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No information available about transaction");

    const CTxOut& temptxout = tx.vout[0];
    CTxDestination dest;
    if(!ExtractDestination(temptxout.scriptPubKey, dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, failed to get address");

    CBitcoinAddress address(dest);
    std::string strAddress = "";
    strAddress = address.ToString();
    
    CKeyID keyid;
    if (!address.GetKeyID(keyid))
        throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");

    CUpdateSuperNodeCandidate tempupdateSuperNodeCandidate;
    tempupdateSuperNodeCandidate.strregTxHash = strTxHex;
    tempupdateSuperNodeCandidate.vecSuperNodeUpdateInfo.push_back(tempsuperNodeUpdateInfo);

    tempupdateSuperNodeCandidate.mapsignature[strAddress] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    string strExtendData = "";

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeVoteAppID), CREATE_EXTEND_TX_CMD);

    std::vector<unsigned char> vecdata;
    vecdata = FillUpdateSuperNodeCandidate(tempupdateSuperNodeCandidate);

    std::vector<unsigned char>::iterator itdata = vecdata.begin();
    for (; itdata != vecdata.end(); ++itdata)
    {
        strExtendData.push_back(*itdata);
    }

    CExtendData extendData(UPDATE_SUPER_NODE_CANDIDATE_CMD, strExtendData);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(APP_OUT_VALUE >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    CBitcoinAddress adminSafeAddress(g_stradminSafeAddress);

    vector<CRecipient> vecSend;
    CRecipient userRecipient = {GetScriptForDestination(adminSafeAddress.Get()), APP_OUT_VALUE, 0, false, false};
    vecSend.push_back(userRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, (void*)&extendData, vecSend, &adminSafeAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, false))
    {
        if(APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    COutPoint prevout = wtx.vin[0].prevout;

    CHashWriter ssrowHash(SER_GETHASH, PROTOCOL_VERSION);
    ssrowHash << prevout.hash;
    ssrowHash << prevout.n;
    ssrowHash << strTxHex;

    std::vector<CSuperNodeUpdateInfo>::iterator itsuperNodeUpdateInfo = tempupdateSuperNodeCandidate.vecSuperNodeUpdateInfo.begin();
    for (; itsuperNodeUpdateInfo != tempupdateSuperNodeCandidate.vecSuperNodeUpdateInfo.end(); ++itsuperNodeUpdateInfo)
    {
        ssrowHash << itsuperNodeUpdateInfo->ndividendRatio;
        ssrowHash << itsuperNodeUpdateInfo->strbpName;
        ssrowHash << itsuperNodeUpdateInfo->strbpURL;
    }

    uint256 rowHash = ssrowHash.GetHash();
    LogPrintf("rowHash:%s\n", rowHash.ToString());

    tempupdateSuperNodeCandidate.mapsignature.clear();

    CKey vchSecret;
    if (!pwalletMain->GetKey(keyid, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address is not known");

    std::vector<unsigned char> vchSig;
    if (!vchSecret.Sign(rowHash, vchSig))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for sign error");

    std::string strSig = "";
    std::vector<unsigned char>::iterator itch = vchSig.begin();
    for (; itch != vchSig.end(); ++itch)
        strSig.push_back(*itch);

    tempupdateSuperNodeCandidate.mapsignature[strAddress] = strSig;

    LogPrintf("strAddress:%s ------- strSig:%s\n", strAddress, strSig);

    std::vector<unsigned char> vecrealdata;
    vecrealdata = FillUpdateSuperNodeCandidate(tempupdateSuperNodeCandidate);
    std::string strRealExtendData = "";

    std::vector<unsigned char>::iterator itrealdata = vecrealdata.begin();
    for (; itrealdata != vecrealdata.end(); ++itrealdata)
    {
        strRealExtendData.push_back(*itrealdata);
    }

    CExtendData extendRealData(UPDATE_SUPER_NODE_CANDIDATE_CMD, strRealExtendData);

    CMutableTransaction tempmtx = CMutableTransaction(wtx);

    for (unsigned int i = 0; i < tempmtx.vout.size(); i++)
    {
        const CTxOut& txout = tempmtx.vout[i];

        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData))
            continue;

        if (header.nAppCmd != CREATE_EXTEND_TX_CMD)
            continue;

        CExtendData extendData;
        if (ParseExtendData(vData, extendData))
        {
            if (extendData.nAuth == UPDATE_SUPER_NODE_CANDIDATE_CMD)
            {
                tempmtx.vout[i].vReserve = FillExtendData(appHeader, extendRealData);
                break;
            }
        }
    }

    // Sign
    int nIn = 0;
    CTransaction txNewConst(tempmtx);
    BOOST_FOREACH(const CTxIn& txin, tempmtx.vin)
    {
        bool signSuccess;
        const CScript& scriptPubKey = txin.prevPubKey;
        CScript& scriptSigRes = tempmtx.vin[nIn].scriptSig;
        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &txNewConst, nIn, SIGHASH_ALL), scriptPubKey, scriptSigRes);

        if (!signSuccess)
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: Signing transaction failed");
        }
        nIn++;
    }

    CWalletTx wRealtx;
    wRealtx.fTimeReceivedIsTxTime = true;
    wRealtx.BindWallet(pwalletMain);
    wRealtx.fFromMe = true;

    // Embed the constructed transaction data in wRealtx.
    *static_cast<CTransaction*>(&wRealtx) = CTransaction(tempmtx);

    if(!pwalletMain->CommitTransaction(wRealtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Create extenddata transaction failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wRealtx.GetHash().GetHex()));

    return ret;
}

UniValue holdervote(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 2)
        throw runtime_error(
            "holdervote \"txid\" [{\"safeTxHash\":\"txid\",\"safeTxOutIdx\":n},...]\n"
            "\nCreate transaction which registration campaign super node.\n"
            "\nArguments:\n"
            "1.\"txid\"             (string, required) The transaction id which regTxHash\n"
            "2. \"voteInfoes\"       (string, required) A json array of objects. Each object the safeTxHash (string) safeTxOutIdx (numeric)\n"
            "     [                 (json array of json objects)\n"
            "       {\n"
            "         \"safeTxHash\":\"id\",    (string) The transaction id\n"
            "         \"safeTxOutIdx\": n       (numeric) The output number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("holdervote", "\"5185f1281d8dfed562543dc8d8e3af7db4a4b4eec5e3201ab0ef9fc681bd8019\" \"[{\\\"safeTxHash\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"safeTxOutIdx\\\":1}]\"")
            + HelpExampleRpc("holdervote", "\"5185f1281d8dfed562543dc8d8e3af7db4a4b4eec5e3201ab0ef9fc681bd8019\", \"[{\\\"safeTxHash\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"safeTxOutIdx\\\":1}]\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint256 txhash = ParseHashV(params[0], "parameter 1");
    string strTxHex = txhash.ToString();

    CHolderVote tempholderVote;
    tempholderVote.strregTxHash = strTxHex;

    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txhash, tx, Params().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

    UniValue outputs = params[1].get_array();
    unsigned int nArrayNum = outputs.size();

    std::vector<COutPoint> vecOutPoint;
    for (unsigned int idx = 0; idx < nArrayNum; idx++) {
        const UniValue& output = outputs[idx];
        if (!output.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
 
        const UniValue& o = output.get_obj();

        RPCTypeCheckObj(o, boost::assign::map_list_of("safeTxHash", UniValue::VSTR)("safeTxOutIdx", UniValue::VNUM));

        string txid = find_value(o, "safeTxHash").get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        int nOutput = find_value(o, "safeTxOutIdx").get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        COutPoint outpt(uint256S(txid), nOutput);
        vecOutPoint.push_back(outpt);
    }

    std::map<COutPoint,CDeterministicMasternode_IndexValue> mapAllDMN;
    GetAllDMNData(mapAllDMN);

    vector<CBitcoinAddress> vecaddress;

    std::vector<COutPoint>::iterator it = vecOutPoint.begin();
    for (; it != vecOutPoint.end(); ++it)
    {
        COutPoint tempOutPoint = *it;

        CVoteInfo tempvoteInfo;
        CTransaction tx;
        uint256 hashBlock;
        if (!GetTransaction(tempOutPoint.hash, tx, Params().GetConsensus(), hashBlock, true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

        const CTxOut& temptxout = tx.vout[tempOutPoint.n];
        CTxDestination dest;
        if(!ExtractDestination(temptxout.scriptPubKey, dest))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, failed to get address");

        CBitcoinAddress address(dest);
        CKeyID keyid;
        CPubKey vchPubKey;

        std::string straddress = address.ToString();
        LogPrintf("address:%s\n", straddress);

        if (!address.GetKeyID(keyid))
        {
            LogPrintf("address:%s\n", address.ToString());
            throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");
        }

        if (!pwalletMain->GetPubKey(keyid, vchPubKey))
        {
            LogPrintf("address:%s\n", address.ToString());
            throw JSONRPCError(RPC_WALLET_ERROR, "pubKey key for address is not known");
        }

        vecaddress.push_back(address);
    
        CDataStream ssPubKey(SER_DISK, CLIENT_VERSION);
        ssPubKey.reserve(1000);
        ssPubKey << vchPubKey;
        string serialPubKey = ssPubKey.str();

        strncpy(tempvoteInfo.vchpubkey, serialPubKey.c_str(), sizeof(tempvoteInfo.vchpubkey));
        tempvoteInfo.strsafeTxHash = tempOutPoint.hash.ToString();
        tempvoteInfo.nsafeTxOutIdx = tempOutPoint.n;
        tempvoteInfo.nsafeAmount = temptxout.nValue;

        tempholderVote.mapsignature[straddress] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

        std::map<COutPoint,CDeterministicMasternode_IndexValue>::iterator ittemp = mapAllDMN.find(tempOutPoint);
        if (ittemp != mapAllDMN.end())
        {
            tempvoteInfo.nutxoType = 1;
            tempholderVote.vecVoteInfo.push_back(tempvoteInfo);
            continue;
        }

        if (temptxout.IsAsset())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, OutPoint is asset");

        if (temptxout.IsSafeOnly())
        {
            if (temptxout.nUnlockedHeight == 0 || mapBlockIndex[hashBlock]->nHeight <= chainActive.Height())
            {
                tempvoteInfo.nutxoType = 3;
                tempholderVote.vecVoteInfo.push_back(tempvoteInfo);
                continue;
            }

            if (temptxout.nUnlockedHeight != 0 && mapBlockIndex[hashBlock]->nHeight > chainActive.Height())
            {
                tempvoteInfo.nutxoType = 2;
                tempholderVote.vecVoteInfo.push_back(tempvoteInfo);
                continue;
            }
        }
    }

    CBitcoinAddress adminSafeAddress(g_stradminSafeAddress);

    string strExtendData = "";

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeVoteAppID), CREATE_EXTEND_TX_CMD);
    std::vector<unsigned char> vecdata;
    vecdata = FillHolderVote(tempholderVote);

    std::vector<unsigned char>::iterator itdata = vecdata.begin();
    for (; itdata != vecdata.end(); ++itdata)
    {
        strExtendData.push_back(*itdata);
    }

    CExtendData extendData(HOLDER_VOTE_CMD, strExtendData);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(APP_OUT_VALUE >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CRecipient userRecipient = {GetScriptForDestination(adminSafeAddress.Get()), APP_OUT_VALUE, 0, false, false};
    vecSend.push_back(userRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, (void*)&extendData, vecSend, &adminSafeAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, false))
    {
        if(APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    COutPoint prevout = wtx.vin[0].prevout;

    CHashWriter ssrowHash(SER_GETHASH, PROTOCOL_VERSION);
    ssrowHash << prevout.hash;
    ssrowHash << prevout.n;
    ssrowHash << strTxHex;

    std::vector<CVoteInfo>::iterator itvoteInfo = tempholderVote.vecVoteInfo.begin();
    for (; itvoteInfo != tempholderVote.vecVoteInfo.end(); ++itvoteInfo)
    {
        uint32_t nvchpubkeyLength = sizeof(itvoteInfo->vchpubkey);
        for (uint32_t i = 0; i < nvchpubkeyLength; i++)
            ssrowHash << itvoteInfo->vchpubkey[i];
        ssrowHash << itvoteInfo->strsafeTxHash;
        ssrowHash << itvoteInfo->nsafeTxOutIdx;
        ssrowHash << itvoteInfo->nsafeAmount;
        ssrowHash << itvoteInfo->nutxoType;
    }

    uint256 rowHash = ssrowHash.GetHash();
    LogPrintf("rowHash:%s\n", rowHash.ToString());

    tempholderVote.mapsignature.clear();
    vector<CBitcoinAddress>::iterator itaddress = vecaddress.begin();
    for (; itaddress != vecaddress.end(); ++itaddress)
    {
        CKeyID keyid;
        CBitcoinAddress tempAddress = *itaddress;
        std::string strtempAddress = tempAddress.ToString();

        if (!tempAddress.GetKeyID(keyid))
            throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");
    
        CKey vchSecret;
        if (!pwalletMain->GetKey(keyid, vchSecret))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address is not known");

        std::vector<unsigned char> vchSig;
        if (!vchSecret.Sign(rowHash, vchSig))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for sign error");

        std::string strSig = "";
        std::vector<unsigned char>::iterator itch = vchSig.begin();
        for (; itch != vchSig.end(); ++itch)
            strSig.push_back(*itch);

        tempholderVote.mapsignature[strtempAddress] = strSig;

        LogPrintf("strtempAddress:%s ------- strSig:%s\n", strtempAddress, strSig);
    }

    std::vector<unsigned char> vecrealdata;
    vecrealdata = FillHolderVote(tempholderVote);
    std::string strRealExtendData = "";

    std::vector<unsigned char>::iterator itrealdata = vecrealdata.begin();
    for (; itrealdata != vecrealdata.end(); ++itrealdata)
    {
        strRealExtendData.push_back(*itrealdata);
    }

    CExtendData extendRealData(HOLDER_VOTE_CMD, strRealExtendData);

    CMutableTransaction tempmtx = CMutableTransaction(wtx);

    for (unsigned int i = 0; i < tempmtx.vout.size(); i++)
    {
        const CTxOut& txout = tempmtx.vout[i];

        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData))
            continue;

        if (header.nAppCmd != CREATE_EXTEND_TX_CMD)
            continue;

        CExtendData extendData;
        if (ParseExtendData(vData, extendData))
        {
            if (extendData.nAuth == HOLDER_VOTE_CMD)
            {
                tempmtx.vout[i].vReserve = FillExtendData(appHeader, extendRealData);
                break;
            }
        }
    }

    // Sign
    int nIn = 0;
    CTransaction txNewConst(tempmtx);
    BOOST_FOREACH(const CTxIn& txin, tempmtx.vin)
    {
        bool signSuccess;
        const CScript& scriptPubKey = txin.prevPubKey;
        CScript& scriptSigRes = tempmtx.vin[nIn].scriptSig;
        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &txNewConst, nIn, SIGHASH_ALL), scriptPubKey, scriptSigRes);

        if (!signSuccess)
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: Signing transaction failed");
        }
        nIn++;
    }

    CWalletTx wRealtx;
    wRealtx.fTimeReceivedIsTxTime = true;
    wRealtx.BindWallet(pwalletMain);
    wRealtx.fFromMe = true;

    // Embed the constructed transaction data in wRealtx.
    *static_cast<CTransaction*>(&wRealtx) = CTransaction(tempmtx);

    if(!pwalletMain->CommitTransaction(wRealtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Create extenddata transaction failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wRealtx.GetHash().GetHex()));

    return ret;
}

UniValue ivote(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 2)
        throw runtime_error(
            "ivote \"txid\" [{\"safeaddress\":\"xxx\",\"amount\":xxxx},...] \n"
            "\nTry to send an amount to many given address \n"
            + HelpRequiringPassphrase() +
            "\nArguments:\n"
            "1.\"txid\"             (string, required) The transaction id which regTxHash\n"
            "2. \"transferinfo\"    (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"safeaddress\":\"xxx\",      (string, required) The safe address to send to.\n"
            "         \"amount\":n,                 (numeric or string, required) The amount in " + CURRENCY_UNIT + " to send. eg 0.1\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("ivote", "\"5185f1281d8dfed562543dc8d8e3af7db4a4b4eec5e3201ab0ef9fc681bd8019\" \"[{\\\"safeaddress\\\":\\\"Xy2m1dQCatw23HasWwmEp84woBS1sfoGDH\\\",\\\"amount\\\":0.1},{\\\"safeaddress\\\":\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\",\\\"amount\\\":0.1}]\"")
            + HelpExampleRpc("ivote", "\"5185f1281d8dfed562543dc8d8e3af7db4a4b4eec5e3201ab0ef9fc681bd8019\", \"[{\\\"safeaddress\\\":\\\"Xy2m1dQCatw23HasWwmEp84woBS1sfoGDH\\\",\\\"amount\\\":0.1},{\\\"safeaddress\\\":\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\",\\\"amount\\\":0.1}]\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    uint256 txhash = ParseHashV(params[0], "parameter 1");
    string strTxHex = txhash.ToString();

    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txhash, tx, Params().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No information available about transaction");

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

        const UniValue& vsafeaddress = find_value(o, "safeaddress");
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

        CRecipient recipient = {scriptPubKey, nSendAmount, 0, false, false, strTxHex, 1011};
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

UniValue revokevtxo(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "revokevtxo [{\"safeTxHash\":\"txid\",\"safeTxOutIdx\":n},...]\n"
            "\nCreate transaction which registration campaign super node.\n"
            "\nArguments:\n"
            "1. \"revokeInfoes\"       (string, required) A json array of objects. Each object the safeTxHash (string) safeTxOutIdx (numeric)\n"
            "     [                 (json array of json objects)\n"
            "       {\n"
            "         \"safeTxHash\":\"id\",    (string) The transaction id\n"
            "         \"safeTxOutIdx\": n       (numeric) The output number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("revokevtxo", "\"[{\\\"safeTxHash\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"safeTxOutIdx\\\":1}]\"")
            + HelpExampleRpc("revokevtxo", "\"[{\\\"safeTxHash\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"safeTxOutIdx\\\":1}]\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    UniValue outputs = params[0].get_array();
    unsigned int nArrayNum = outputs.size();

    std::vector<COutPoint> vecOutPoint;
    for (unsigned int idx = 0; idx < nArrayNum; idx++) {
        const UniValue& output = outputs[idx];
        if (!output.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
 
        const UniValue& o = output.get_obj();

        RPCTypeCheckObj(o, boost::assign::map_list_of("safeTxHash", UniValue::VSTR)("safeTxOutIdx", UniValue::VNUM));

        string txid = find_value(o, "safeTxHash").get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        int nOutput = find_value(o, "safeTxOutIdx").get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        COutPoint outpt(uint256S(txid), nOutput);
        vecOutPoint.push_back(outpt);
    }

    vector<CBitcoinAddress> vecaddress;

    CRevokeVtxo temprevokeVtxo;
    std::vector<COutPoint>::iterator it = vecOutPoint.begin();
    for (; it != vecOutPoint.end(); ++it)
    {
        COutPoint tempOutPoint = *it;

        CRevokeInfo temprevokeInfo;
        CTransaction tx;
        uint256 hashBlock;
        if (!GetTransaction(tempOutPoint.hash, tx, Params().GetConsensus(), hashBlock, true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

        const CTxOut& temptxout = tx.vout[tempOutPoint.n];
        CTxDestination dest;
        if(!ExtractDestination(temptxout.scriptPubKey, dest))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, failed to get address");

        CBitcoinAddress address(dest);
        CKeyID keyid;
        CPubKey vchPubKey;

        std::string straddress = address.ToString();
        LogPrintf("address:%s\n", straddress);

        if (!address.GetKeyID(keyid))
        {
            LogPrintf("address:%s\n", address.ToString());
            throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");
        }

        if (!pwalletMain->GetPubKey(keyid, vchPubKey))
        {
            LogPrintf("address:%s\n", address.ToString());
            throw JSONRPCError(RPC_WALLET_ERROR, "pubKey key for address is not known");
        }

        vecaddress.push_back(address);
    
        CDataStream ssPubKey(SER_DISK, CLIENT_VERSION);
        ssPubKey.reserve(1000);
        ssPubKey << vchPubKey;
        string serialPubKey = ssPubKey.str();

        strncpy(temprevokeInfo.vchpubkey, serialPubKey.c_str(), sizeof(temprevokeInfo.vchpubkey));
        temprevokeInfo.strsafeTxHash = tempOutPoint.hash.ToString();
        temprevokeInfo.nsafeTxOutIdx = tempOutPoint.n;
        temprevokeVtxo.mapsignature[straddress] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        temprevokeVtxo.vecRevokeInfo.push_back(temprevokeInfo);

    }

    CBitcoinAddress adminSafeAddress(g_stradminSafeAddress);

    string strExtendData = "";

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeVoteAppID), CREATE_EXTEND_TX_CMD);
    std::vector<unsigned char> vecdata;
    vecdata = FillRevokeVtxo(temprevokeVtxo);

    std::vector<unsigned char>::iterator itdata = vecdata.begin();
    for (; itdata != vecdata.end(); ++itdata)
    {
        strExtendData.push_back(*itdata);
    }

    CExtendData extendData(REVOKE_VTXO_CMD, strExtendData);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(APP_OUT_VALUE >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CRecipient userRecipient = {GetScriptForDestination(adminSafeAddress.Get()), APP_OUT_VALUE, 0, false, false};
    vecSend.push_back(userRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, (void*)&extendData, vecSend, &adminSafeAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, false))
    {
        if(APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    COutPoint prevout = wtx.vin[0].prevout;

    CHashWriter ssrowHash(SER_GETHASH, PROTOCOL_VERSION);
    ssrowHash << prevout.hash;
    ssrowHash << prevout.n;

    std::vector<CRevokeInfo>::iterator itrevokeInfo = temprevokeVtxo.vecRevokeInfo.begin();
    for (; itrevokeInfo != temprevokeVtxo.vecRevokeInfo.end(); ++itrevokeInfo)
    {
        uint32_t nvchpubkeyLength = sizeof(itrevokeInfo->vchpubkey);
        for (uint32_t i = 0; i < nvchpubkeyLength; i++)
            ssrowHash << itrevokeInfo->vchpubkey[i];
        ssrowHash << itrevokeInfo->strsafeTxHash;
        ssrowHash << itrevokeInfo->nsafeTxOutIdx;
    }

    uint256 rowHash = ssrowHash.GetHash();
    LogPrintf("rowHash:%s\n", rowHash.ToString());

    temprevokeVtxo.mapsignature.clear();
    vector<CBitcoinAddress>::iterator itaddress = vecaddress.begin();
    for (; itaddress != vecaddress.end(); ++itaddress)
    {
        CKeyID keyid;
        CBitcoinAddress tempAddress = *itaddress;
        std::string strtempAddress = tempAddress.ToString();

        if (!tempAddress.GetKeyID(keyid))
            throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");
    
        CKey vchSecret;
        if (!pwalletMain->GetKey(keyid, vchSecret))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address is not known");

        std::vector<unsigned char> vchSig;
        if (!vchSecret.Sign(rowHash, vchSig))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for sign error");

        std::string strSig = "";
        std::vector<unsigned char>::iterator itch = vchSig.begin();
        for (; itch != vchSig.end(); ++itch)
            strSig.push_back(*itch);

        temprevokeVtxo.mapsignature[strtempAddress] = strSig;

        LogPrintf("strtempAddress:%s ------- strSig:%s\n", strtempAddress, strSig);
    }

    std::vector<unsigned char> vecrealdata;
    vecrealdata = FillRevokeVtxo(temprevokeVtxo);
    std::string strRealExtendData = "";

    std::vector<unsigned char>::iterator itrealdata = vecrealdata.begin();
    for (; itrealdata != vecrealdata.end(); ++itrealdata)
    {
        strRealExtendData.push_back(*itrealdata);
    }

    CExtendData extendRealData(REVOKE_VTXO_CMD, strRealExtendData);

    CMutableTransaction tempmtx = CMutableTransaction(wtx);

    for (unsigned int i = 0; i < tempmtx.vout.size(); i++)
    {
        const CTxOut& txout = tempmtx.vout[i];

        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData))
            continue;

        if (header.nAppCmd != CREATE_EXTEND_TX_CMD)
            continue;

        CExtendData extendData;
        if (ParseExtendData(vData, extendData))
        {
            if (extendData.nAuth == REVOKE_VTXO_CMD)
            {
                tempmtx.vout[i].vReserve = FillExtendData(appHeader, extendRealData);
                break;
            }
        }
    }

    // Sign
    int nIn = 0;
    CTransaction txNewConst(tempmtx);
    BOOST_FOREACH(const CTxIn& txin, tempmtx.vin)
    {
        bool signSuccess;
        const CScript& scriptPubKey = txin.prevPubKey;
        CScript& scriptSigRes = tempmtx.vin[nIn].scriptSig;
        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &txNewConst, nIn, SIGHASH_ALL), scriptPubKey, scriptSigRes);

        if (!signSuccess)
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: Signing transaction failed");
        }
        nIn++;
    }

    CWalletTx wRealtx;
    wRealtx.fTimeReceivedIsTxTime = true;
    wRealtx.BindWallet(pwalletMain);
    wRealtx.fFromMe = true;

    // Embed the constructed transaction data in wRealtx.
    *static_cast<CTransaction*>(&wRealtx) = CTransaction(tempmtx);

    if(!pwalletMain->CommitTransaction(wRealtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Create extenddata transaction failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wRealtx.GetHash().GetHex()));

    return ret;
}

UniValue bindvotersafecodeaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "bindvotersafecodeaccount [{\"safeaddress\":\"safeaddress\",\"scAccount\":\"scAccount\"},...]\n"
            "\nCreate transaction which bind voter safe code account.\n"
            "\nArguments:\n"
            "1. \"bindInfoes\"       (string, required) A json array of objects. Each object the safeaddress (string) scAccount (string)\n"
            "     [                  (json array of json objects)\n"
            "       {\n"
            "         \"safeaddress\":\"safeaddress\",    (string) The safe address\n"
            "         \"scAccount\": \"scAccount\"        (string) The sc Account\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "\nResult:\n"
            "{\n"
            "  \"txId\" : \"xxxxx\" (string) The transaction id\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("bindvotersafecodeaccount", "\"[{\\\"safeaddress\\\":\\\"XrKZKjsUZN99wPY8dzE7DpEdgf8S2QHDWo\\\",\\\"scAccount\\\":\\\"testscAccount\\\"}]\"")
            + HelpExampleRpc("bindvotersafecodeaccount", "\"[{\\\"safeaddress\\\":\\\"XrKZKjsUZN99wPY8dzE7DpEdgf8S2QHDWo\\\",\\\"scAccount\\\":\\\"testscAccount\\\"}]\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!masternodeSync.IsBlockchainSynced())
        throw JSONRPCError(SYNCING_BLOCK, "Synchronizing block data");

    UniValue outputs = params[0].get_array();
    unsigned int nArrayNum = outputs.size();

    CBindVoterSafecodeAccount tempbindVoterSafecodeAccount;
    vector<CBitcoinAddress> vecaddress;

    for (unsigned int idx = 0; idx < nArrayNum; idx++) {
        const UniValue& output = outputs[idx];
        if (!output.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
 
        const UniValue& o = output.get_obj();

        RPCTypeCheckObj(o, boost::assign::map_list_of("safeaddress", UniValue::VSTR)("scAccount", UniValue::VSTR));

        const UniValue& vsafeaddress = find_value(o, "safeaddress");
        if (!vsafeaddress.isStr())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing safeaddress key");

        string strAddress = vsafeaddress.get_str();
        CBitcoinAddress address(strAddress);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Safe address");

        CKeyID keyid;
        CPubKey vchPubKey;

        if (!address.GetKeyID(keyid))
        {
            LogPrintf("address:%s\n", address.ToString());
            throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");
        }

        if (!pwalletMain->GetPubKey(keyid, vchPubKey))
        {
            LogPrintf("address:%s\n", address.ToString());
            throw JSONRPCError(RPC_WALLET_ERROR, "pubKey key for address is not known");
        }

        string strscAccount = find_value(o, "scAccount").get_str();

        CDataStream ssPubKey(SER_DISK, CLIENT_VERSION);
        ssPubKey.reserve(1000);
        ssPubKey << vchPubKey;
        string serialPubKey = ssPubKey.str();

        CBindInfo tempbindInfo;

        strncpy(tempbindInfo.vchpubkey, serialPubKey.c_str(), sizeof(tempbindInfo.vchpubkey));
        tempbindInfo.strscAccount = strscAccount;
        tempbindVoterSafecodeAccount.mapsignature[strAddress] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        tempbindVoterSafecodeAccount.vecBindInfo.push_back(tempbindInfo);

        vecaddress.push_back(address);
    }

    CBitcoinAddress adminSafeAddress(g_stradminSafeAddress);

    string strExtendData = "";

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeVoteAppID), CREATE_EXTEND_TX_CMD);
    std::vector<unsigned char> vecdata;
    vecdata = FillBindVoterSafecodeAccount(tempbindVoterSafecodeAccount);

    std::vector<unsigned char>::iterator itdata = vecdata.begin();
    for (; itdata != vecdata.end(); ++itdata)
    {
        strExtendData.push_back(*itdata);
    }

    CExtendData extendData(BIND_VOTER_SAFE_CODE_ACCOUNT, strExtendData);

    EnsureWalletIsUnlocked();

    if (pwalletMain->GetBroadcastTransactions() && !g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if(APP_OUT_VALUE >= pwalletMain->GetBalance())
        throw JSONRPCError(INSUFFICIENT_SAFE, "Insufficient safe funds");

    vector<CRecipient> vecSend;
    CRecipient userRecipient = {GetScriptForDestination(adminSafeAddress.Get()), APP_OUT_VALUE, 0, false, false};
    vecSend.push_back(userRecipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    if(!pwalletMain->CreateAppTransaction(&appHeader, (void*)&extendData, vecSend, &adminSafeAddress, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, true, ALL_COINS, false))
    {
        if(APP_OUT_VALUE + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: Insufficient safe funds, this transaction requires a transaction fee of at least %1!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    COutPoint prevout = wtx.vin[0].prevout;

    CHashWriter ssrowHash(SER_GETHASH, PROTOCOL_VERSION);
    ssrowHash << prevout.hash;
    ssrowHash << prevout.n;

    std::vector<CBindInfo>::iterator itbindInfo = tempbindVoterSafecodeAccount.vecBindInfo.begin();
    for (; itbindInfo != tempbindVoterSafecodeAccount.vecBindInfo.end(); ++itbindInfo)
    {
        uint32_t nvchpubkeyLength = sizeof(itbindInfo->vchpubkey);
        for (uint32_t i = 0; i < nvchpubkeyLength; i++)
            ssrowHash << itbindInfo->vchpubkey[i];
        ssrowHash << itbindInfo->strscAccount;
    }

    uint256 rowHash = ssrowHash.GetHash();
    LogPrintf("rowHash:%s\n", rowHash.ToString());

    tempbindVoterSafecodeAccount.mapsignature.clear();
    vector<CBitcoinAddress>::iterator itaddress = vecaddress.begin();
    for (; itaddress != vecaddress.end(); ++itaddress)
    {
        CKeyID keyid;
        CBitcoinAddress tempAddress = *itaddress;
        std::string strtempAddress = tempAddress.ToString();

        if (!tempAddress.GetKeyID(keyid))
            throw JSONRPCError(RPC_WALLET_ERROR, "keyid for address is not known");
    
        CKey vchSecret;
        if (!pwalletMain->GetKey(keyid, vchSecret))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address is not known");

        std::vector<unsigned char> vchSig;
        if (!vchSecret.Sign(rowHash, vchSig))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for sign error");

        std::string strSig = "";
        std::vector<unsigned char>::iterator itch = vchSig.begin();
        for (; itch != vchSig.end(); ++itch)
            strSig.push_back(*itch);

        tempbindVoterSafecodeAccount.mapsignature[strtempAddress] = strSig;

        LogPrintf("strtempAddress:%s ------- strSig:%s\n", strtempAddress, strSig);
    }

    std::vector<unsigned char> vecrealdata;
    vecrealdata = FillBindVoterSafecodeAccount(tempbindVoterSafecodeAccount);
    std::string strRealExtendData = "";

    std::vector<unsigned char>::iterator itrealdata = vecrealdata.begin();
    for (; itrealdata != vecrealdata.end(); ++itrealdata)
    {
        strRealExtendData.push_back(*itrealdata);
    }

    CExtendData extendRealData(BIND_VOTER_SAFE_CODE_ACCOUNT, strRealExtendData);

    CMutableTransaction tempmtx = CMutableTransaction(wtx);

    for (unsigned int i = 0; i < tempmtx.vout.size(); i++)
    {
        const CTxOut& txout = tempmtx.vout[i];

        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData))
            continue;

        if (header.nAppCmd != CREATE_EXTEND_TX_CMD)
            continue;

        CExtendData extendData;
        if (ParseExtendData(vData, extendData))
        {
            if (extendData.nAuth == BIND_VOTER_SAFE_CODE_ACCOUNT)
            {
                tempmtx.vout[i].vReserve = FillExtendData(appHeader, extendRealData);
                break;
            }
        }
    }

    // Sign
    int nIn = 0;
    CTransaction txNewConst(tempmtx);
    BOOST_FOREACH(const CTxIn& txin, tempmtx.vin)
    {
        bool signSuccess;
        const CScript& scriptPubKey = txin.prevPubKey;
        CScript& scriptSigRes = tempmtx.vin[nIn].scriptSig;
        signSuccess = ProduceSignature(TransactionSignatureCreator(pwalletMain, &txNewConst, nIn, SIGHASH_ALL), scriptPubKey, scriptSigRes);

        if (!signSuccess)
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: Signing transaction failed");
        }
        nIn++;
    }

    CWalletTx wRealtx;
    wRealtx.fTimeReceivedIsTxTime = true;
    wRealtx.BindWallet(pwalletMain);
    wRealtx.fFromMe = true;

    // Embed the constructed transaction data in wRealtx.
    *static_cast<CTransaction*>(&wRealtx) = CTransaction(tempmtx);

    if(!pwalletMain->CommitTransaction(wRealtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Create extenddata transaction failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wRealtx.GetHash().GetHex()));

    return ret;
}
