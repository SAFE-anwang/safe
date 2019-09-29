#include <univalue.h>

#include "safevote.h"
#include "init.h"
#include "spork.h"
#include "validation.h"
#include "utilmoneystr.h"
#include "rpc/server.h"
#include "wallet/wallet.h"
#include "main.h"
#include "masternode-sync.h"
#include "primitives/transaction.h"



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

    vector<CPubKey> vecpubkey;

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
        
        if (address.GetKeyID(keyid))
        {
            if (!pwalletMain->GetPubKey(keyid, vchPubKey))
                throw JSONRPCError(RPC_WALLET_ERROR, "PubKey key for address is not known");

            vecpubkey.push_back(vchPubKey);
        
            CDataStream ssPubKey(SER_DISK, CLIENT_VERSION);
            ssPubKey.reserve(1000);
            ssPubKey << vchPubKey;
            string serialPubKey = ssPubKey.str();

            tempregInfo.strsafeTxHash = tempOutPoint.hash;
            tempregInfo.nsafeTxOutIdx = tempOutPoint.n;
            tempregInfo.strsafePubkey = serialPubKey;
            tempregSuperNodeCandidate.mapsignature[serialPubKey] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        }

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

    CAppId_AppInfo_IndexValue appInfo;
    if(!GetAppInfoByAppId(uint256S(g_strSafeVoteAppID), appInfo, false))
        throw JSONRPCError(NONEXISTENT_APPID, "Non-existent app id");

    CBitcoinAddress adminSafeAddress(appInfo.strAdminAddress);

    string strExtendData = "";

    std::vector<unsigned char> vecdata;
    vecdata = FillRegSuperNodeCandidate(appHeader, tempregSuperNodeCandidate);

    std::vector<unsigned char>::iterator itdata = vecdata.begin();
    for (; itdata ! = vecdata.end(); ++itdata)
    {
        strExtendData.push_back(*itdata);
    }

    CAppHeader appHeader(g_nAppHeaderVersion, uint256S(g_strSafeVoteAppID), CREATE_EXTEND_TX_CMD);
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
        ssrowHash << itreginfo->strsafePubkey;
        ssrowHash << itreginfo->strsafeTxHash;
        ssrowHash << itreginfo->nsafeTxOutIdx;
        ssrowHash << itreginfo->nutxoType;
    }

    uint256 rowHash = ssrowHash.GetHash();

    tempregSuperNodeCandidate.mapsignature.clear();
    vector<CPubKey>::iterator itpubkey = vecpubkey.begin();
    for (; itpubkey != vecpubkey.end(); ++itkeyid)
    {
        CPubKey temppubkey = *itpubkey;
        CKey vchSecret;
        if (!pwalletMain->GetKey(temppubkey.GetID(), vchSecret))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address is not known");

        std::vector<unsigned char> vchSig;
        if (!vchSecret.Sign(rowHash, vchSig))
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for sign error");

        std::string strSig = "";
        std::vector<unsigned char>::iterator itch = vchSig.begin();
        for (; itch != vchSig.end(); ++itch)
            strSig.push_back(*itch);

        CDataStream ssPubKey(SER_DISK, CLIENT_VERSION);
        ssPubKey.reserve(1000);
        ssPubKey << temppubkey;
        string serialPubKey = ssPubKey.str();
        tempregSuperNodeCandidate.mapsignature[serialPubKey] = strSig;

        LogPrintf("serialPubKey:%s ------- strSig:%s\n", serialPubKey, strSig);
    }

    std::vector<unsigned char> vecrealdata;
    vecdata = FillRegSuperNodeCandidate(appHeader, tempregSuperNodeCandidate);
    std::string strRealExtendData = "";

    std::vector<unsigned char>::iterator vecrealdata = vecrealdata.begin();
    for (; itdata ! = vecrealdata.end(); ++itdata)
    {
        strRealExtendData.push_back(*itdata);
    }

    CExtendData extendRealData(REG_SUPER_NODE_CANDIDATE_CMD, strRealExtendData);

    wtx.vout[0].vReserve = FillExtendData(appHeader, extendRealData);

    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Create extenddata transaction failed, please check your wallet and try again later!");

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("txId", wtx.GetHash().GetHex()));

    return ret;
}
