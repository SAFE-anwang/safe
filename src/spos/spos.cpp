#include "spos.h"
#include "spos.pb.h"
#include "messagesigner.h"
#include "clientversion.h"
#include "netbase.h"
#include "net.h"
#include "masternode.h"
#include "wallet/wallet.h"
#include "init.h"
#include "masternode-sync.h"
#include "utilmoneystr.h"
#include "streams.h"

bool CheckDeterminedMasternode(CDeterminedMasternodeData &dmn, std::string &strMessage, std::string &strError)
{
    //1.check ip
    CService service;
    if (!Lookup(dmn.strIP.c_str(), service, 0, false))
    {
        strError = strprintf(_("Invalid address %s for masternode when lookup."), dmn.strIP);
        return false;
    }

    bool bValidService = service.IsIPv4() && IsReachable(service) && service.IsRoutable();
    if(!bValidService)
    {
        strError = strprintf(_("Invalid address %s for masternode."), service.ToStringIP());
        return false;
    }

    //2.check port
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN){
        if (dmn.nPort != mainnetDefaultPort)
        {
            strError = strprintf(_("Invalid port %u for masternode %s, only %u is supported on mainnet."), dmn.nPort,
                                 dmn.strIP,mainnetDefaultPort);
            return false;
        }
    }else if (service.GetPort() == mainnetDefaultPort)
    {
        strError = strprintf(_("Invalid port %u for masternode %s, only %u is supported on mainnet."), dmn.nPort,
                             dmn.strIP,mainnetDefaultPort);
        return false;
    }

    //3.check strTxid
    CTransaction tx;
    uint256 hash = uint256S(dmn.strTxid);
    COutPoint outpoint(hash,dmn.nOutputIndex);
    CMasternode::CollateralStatus err = CMasternode::CheckCollateral(outpoint);
    if (err != CMasternode::COLLATERAL_OK)
    {
        strError = strprintf(_("Invalid txid,check collateral fail.ip:%s"),dmn.strIP);
        return false;
    }

    if(!GetTransaction(outpoint.hash, tx, Params().GetConsensus(), hash, true))
    {
        strError = strprintf(_("Invalid output index,get transaction fail.ip:%s"),dmn.strIP);
        return false;
    }

    //4.check nOutputIndex
    uint16_t nOutSize = tx.vout.size();
    if(dmn.nOutputIndex<0||dmn.nOutputIndex>=nOutSize)
    {
        strError = strprintf(_("Invalid nOutputIndex,tx vout size:%u.ip:%s"),nOutSize,dmn.strIP);
        return false;
    }

    //5.check collateral address
    CBitcoinAddress collateralAddress(dmn.strCollateralAddress);
    if(!collateralAddress.IsValid())
    {
        strError = strprintf(_("Invalid collateral address %s.ip:%s"),dmn.strCollateralAddress,dmn.strIP);
        return false;
    }
    CKeyID keyID;
    if(!collateralAddress.GetKeyID(keyID))
    {
        strError = strprintf(_("Invalid collateral address %s,get key id fail.ip:%s"),dmn.strCollateralAddress,dmn.strIP);
        return false;
    }
    CScript payee = GetScriptForDestination(keyID);
    if(payee != tx.vout[dmn.nOutputIndex].scriptPubKey)
    {
        strError = strprintf(_("Collateral address is not meet txout.ip:%s"),dmn.strIP);
        return false;
    }

    //6.check strSerialPubKeyId and strSignedMsg
    std::vector<unsigned char> vchMasternodeKeyId;
    for(unsigned int i=0;i<dmn.strSerialPubKeyId.size();i++)
        vchMasternodeKeyId.push_back(dmn.strSerialPubKeyId[i]);

    CKeyID pubKeyMasternodeID;
    CDataStream ssKey(vchMasternodeKeyId, SER_DISK, CLIENT_VERSION);
    ssKey >> pubKeyMasternodeID;

    std::vector<unsigned char> vchSig;
    for(unsigned int i=0;i<dmn.strSignedMsg.size();i++)
        vchSig.push_back(dmn.strSignedMsg[i]);

    if(strMessage.empty())
    {
        for(unsigned int i=0;i<dmn.strIP.size();i++)
            strMessage.push_back(dmn.strIP[i]);

        const unsigned char* pPort = (const unsigned char*)&dmn.nPort;
        strMessage.push_back(pPort[0]);
        strMessage.push_back(pPort[1]);

        for(unsigned int i=0;i<dmn.strCollateralAddress.size();i++)
            strMessage.push_back(dmn.strCollateralAddress[i]);

        for(unsigned int i=0;i<dmn.strTxid.size();i++)
            strMessage.push_back(dmn.strTxid[i]);

        const unsigned char* pOutputIndex = (const unsigned char*)&dmn.nOutputIndex;
        strMessage.push_back(pOutputIndex[0]);
        strMessage.push_back(pOutputIndex[1]);

        for(unsigned int i=0;i<dmn.strSerialPubKeyId.size();i++)
            strMessage.push_back(dmn.strSerialPubKeyId[i]);
    }
    if(!CMessageSigner::VerifyMessage(pubKeyMasternodeID, vchSig, strMessage, strError))
    {
        strError = strprintf(_("Verify determined masternode sign fail.ip:%s"),dmn.strIP);
        return false;
    }

    return true;
}

bool BuildDeterminedMasternode(CDeterminedMasternodeData &dmn, const std::string &strPrivKey, std::string &strError)
{
    CPubKey pubKeyMasternode;
    CKey keyMasternode;
    if (!CMessageSigner::GetKeysFromSecret(strPrivKey, keyMasternode, pubKeyMasternode))
    {
        strError = strprintf(_("Invalid masternode key %s.ip:%s"), strPrivKey,dmn.strIP);
        return false;
    }

    //serial pub key id
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(1000);
    ssKey << pubKeyMasternode.GetID();
    std::string strSerialPubKeyId = ssKey.str();

    //sign msg
    std::string strMessage= "";
    for(unsigned int i=0;i<dmn.strIP.size();i++)
        strMessage.push_back(dmn.strIP[i]);

    const unsigned char* pPort = (const unsigned char*)&dmn.nPort;
    strMessage.push_back(pPort[0]);
    strMessage.push_back(pPort[1]);

    for(unsigned int i=0;i<dmn.strCollateralAddress.size();i++)
        strMessage.push_back(dmn.strCollateralAddress[i]);

    for(unsigned int i=0;i<dmn.strTxid.size();i++)
        strMessage.push_back(dmn.strTxid[i]);

    const unsigned char* pOutputIndex = (const unsigned char*)&dmn.nOutputIndex;
    strMessage.push_back(pOutputIndex[0]);
    strMessage.push_back(pOutputIndex[1]);

    for(unsigned int i=0;i<strSerialPubKeyId.size();i++)
        strMessage.push_back(strSerialPubKeyId[i]);

    std::vector<unsigned char> vchSig;
    if(!CMessageSigner::SignMessage(strMessage, vchSig, keyMasternode))
    {
        strError = strprintf(_("Sign determined masternode fail.ip:%s"),dmn.strIP);
        return false;
    }

    std::string strSignedMsg = "";
    for(unsigned int i=0;i<vchSig.size();i++)
        strSignedMsg.push_back(vchSig[i]);

    //9.init dmn strSerialPubKeyId and strSignedMsg
    dmn.initPubkeyIdAndSign(strSerialPubKeyId,strSignedMsg);

    return CheckDeterminedMasternode(dmn,strMessage,strError);
}

//SQTODO
/*
bool RegisterDeterminedMasternodes(std::vector<CDeterminedMasternodeData> &dmnVec, std::string &strError)
{
    if (!pwalletMain)
        return false;

    LOCK2(cs_main,pwalletMain->cs_wallet);
    if(!masternodeSync.IsBlockchainSynced())
    {
        strError = _("Synchronizing block data");
        return false;
    }

    std::vector<CRecipient> vecSend;
    std::vector<CDeterminedMasternodeData> dmnVecSend;
    BOOST_FOREACH(const CDeterminedMasternodeData& dmn, dmnVec)
    {
        CAmount amount = 0.0001 * COIN;
        CRecipient assetRecipient = {GetScriptForDestination(CBitcoinAddress(dmn.strCollateralAddress).Get()), amount, 0, false, false,""};
        vecSend.push_back(assetRecipient);
        dmnVecSend.push_back(dmn);
    }

    //send transaction
    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    bool fUseInstantSend = false;
    bool bSign = true;
    if(!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError, NULL, bSign,
                                              ALL_COINS, fUseInstantSend,&dmnVec))
    {
        if(nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf(_("Insufficient safe funds, this transaction requires a transaction fee of at least %s!"),FormatMoney(nFeeRequired));
        return false;
    }

    if(!pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get()))
    {
        strError = _("Register determinated masternode fail,please check your wallet and try again later!");
        return false;
    }

    return true;
}
*/

void FillSPOSHeader(const CSposHeader& header, std::vector<unsigned char>& vHeader)
{
    // 1. flag: safe
    vHeader.push_back('s');
    vHeader.push_back('a');
    vHeader.push_back('f');
    vHeader.push_back('e');

    // 2. flag: spos
    vHeader.push_back('s');
    vHeader.push_back('p');
    vHeader.push_back('o');
    vHeader.push_back('s');

    // 3. version (2 bytes)
    const unsigned char* pVersion = (const unsigned char*)&header.nVersion;
    vHeader.push_back(pVersion[0]);
    vHeader.push_back(pVersion[1]);
}

void ParseSPOSHeader(const std::vector<unsigned char>& vData, CSposHeader& header, unsigned int& nOffset)
{
    nOffset = TXOUT_RESERVE_MIN_SIZE+4;//safe+spos

    // 1. version (2 bytes)
    header.nVersion = *(uint16_t*)&vData[nOffset];
    nOffset += sizeof(header.nVersion);
}

std::vector<unsigned char> FillDeterminedMasternode(const CSposHeader& header, const CDeterminedMasternodeData& dmn)
{
    std::vector<unsigned char> vData;
    FillSPOSHeader(header, vData);

    Spos::DeterminedMasternodeData data;
    data.set_ip(dmn.strIP);
    data.set_port((const unsigned char*)&dmn.nPort,sizeof(dmn.nPort));
    data.set_collateraladdress(dmn.strCollateralAddress);
    data.set_txid(dmn.strTxid);
    data.set_outputindex((const unsigned char*)&dmn.nOutputIndex,sizeof(dmn.nOutputIndex));
    data.set_serialpubkeyid(dmn.strSerialPubKeyId);
    data.set_signedmsg(dmn.strSignedMsg);

    std::string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

std::vector<unsigned char> FillDeterminedMNCoinbaseData(const CSposHeader& header,const CDeterminedMNCoinbaseData& determinedMNCoinbaseData, bool& bRet)
{
    std::vector<unsigned char> vData;
    FillSPOSHeader(header, vData);

    Spos::DeterminedMNCoinbaseData data;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(1000);
    ssKey << determinedMNCoinbaseData.keyIDMasternode;
    std::string strSerialPubKeyId = ssKey.str();
    data.set_pubkeyid(strSerialPubKeyId);
    
    data.set_officialmnnum((const unsigned char*)&determinedMNCoinbaseData.nOfficialMNNum,sizeof(determinedMNCoinbaseData.nOfficialMNNum));
    data.set_generalmnnum((const unsigned char*)&determinedMNCoinbaseData.nGeneralMNNum,sizeof(determinedMNCoinbaseData.nGeneralMNNum));
    data.set_randomnum((const unsigned char*)&determinedMNCoinbaseData.nRandomNum,sizeof(determinedMNCoinbaseData.nRandomNum));
    data.set_firstblock((const unsigned char*)&determinedMNCoinbaseData.nFirstBlock,sizeof(determinedMNCoinbaseData.nFirstBlock));

    //--------add sign--------
    std::string strSignMessage= "";
    const unsigned char* pOfficialMNNum = (const unsigned char*)&determinedMNCoinbaseData.nOfficialMNNum;
    strSignMessage.push_back(pOfficialMNNum[0]);
    strSignMessage.push_back(pOfficialMNNum[1]);

    const unsigned char* pGeneralMNNum = (const unsigned char*)&determinedMNCoinbaseData.nGeneralMNNum;
    strSignMessage.push_back(pGeneralMNNum[0]);
    strSignMessage.push_back(pGeneralMNNum[1]);

    const unsigned char* pRandomNum = (const unsigned char*)&determinedMNCoinbaseData.nRandomNum;
    strSignMessage.push_back(pRandomNum[0]);
    strSignMessage.push_back(pRandomNum[1]);
    strSignMessage.push_back(pRandomNum[2]);
    strSignMessage.push_back(pRandomNum[3]);

    const unsigned char* pnFirstBlock = (const unsigned char*)&determinedMNCoinbaseData.nFirstBlock;
    strSignMessage.push_back(pnFirstBlock[0]);
    strSignMessage.push_back(pnFirstBlock[1]);

    //Signature and verification signature
    std::vector<unsigned char> vchSig;
    if (!CMessageSigner::SignMessage(strSignMessage, vchSig, determinedMNCoinbaseData.keyMasternode))
    {
        bRet = false;
        LogPrintf("SPOS_Error:SignDeterminedMasternodeMessage() failed\n");
        return vData;
    }

    std::string strError;
    if(!CMessageSigner::VerifyMessage(determinedMNCoinbaseData.pubKeyMasternode, vchSig, strSignMessage, strError))
    {
        bRet = false;
        LogPrintf("SPOS_Error:VerifyDeterminedMasternodeMessage() failed, error: %s\n", strError);
        return vData;
    }

    std::string strSignedMsg = "";
    for(unsigned int i=0;i<vchSig.size();i++)
        strSignedMsg.push_back(vchSig[i]);

    data.set_signmsg(strSignedMsg);

    std::string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}
bool ParseSposReserve(const std::vector<unsigned char>& vReserve, CSposHeader& header, std::vector<unsigned char>& vData)
{
    if(vReserve.size() <= TXOUT_RESERVE_MIN_SIZE + 4 + sizeof(uint16_t))
        return false;

    unsigned int nStartSPOSOffset = TXOUT_RESERVE_MIN_SIZE;
    std::vector<unsigned char> vchConAlg;
    for(unsigned int k = 0; k < 4; k++)
        vchConAlg.push_back(vReserve[nStartSPOSOffset++]);

    if (vchConAlg[0] != 's' || vchConAlg[1] != 'p' || vchConAlg[2] != 'o' || vchConAlg[3] != 's')
        return false;

    unsigned int nOffset = 0;
    ParseSPOSHeader(vReserve, header, nOffset);

    for(unsigned int i = nOffset; i < vReserve.size(); i++)
        vData.push_back(vReserve[i]);

    return true;
}

bool ParseDeterminedMasternode(const std::vector<unsigned char> &vDMNData, CDeterminedMasternodeData &dmn)
{
    Spos::DeterminedMasternodeData data;
    if(!data.ParseFromArray(&vDMNData[0], vDMNData.size()))
        return false;
    dmn.strIP = data.ip();
    dmn.nPort = *(uint16_t*)data.port().data();
    dmn.strCollateralAddress = data.collateraladdress();
    dmn.strTxid = data.txid();
    dmn.nOutputIndex = *(uint16_t*)data.outputindex().data();
    dmn.strSerialPubKeyId = data.serialpubkeyid();
    dmn.strSignedMsg = data.signedmsg();
    return true;
}

bool ParseDeterminedMNCoinbaseData(const std::vector<unsigned char>& vData, CDeterminedMNCoinbaseData& determinedMNCoinbaseData)
{
    Spos::DeterminedMNCoinbaseData data;
    if(!data.ParseFromArray(&vData[0], vData.size()))
        return false;

    std::string strkeyID = data.pubkeyid();
    std::vector<unsigned char> vchKeyId;
    for (unsigned int i = 0; i < strkeyID.length(); i++)
        vchKeyId.push_back(strkeyID[i]);
   
    CKeyID keyID;
    CDataStream ssKey(vchKeyId, SER_DISK, CLIENT_VERSION);
    ssKey >> keyID;
    determinedMNCoinbaseData.keyIDMasternode = keyID;
    determinedMNCoinbaseData.nOfficialMNNum = *(uint16_t*)data.officialmnnum().data();
    determinedMNCoinbaseData.nGeneralMNNum = *(uint16_t*)data.generalmnnum().data();
    determinedMNCoinbaseData.nRandomNum = *(uint32_t*)data.randomnum().data();
    determinedMNCoinbaseData.nFirstBlock = *(uint16_t*)data.firstblock().data();

    std::string strSignedMsg = "";
    strSignedMsg = data.signmsg();
    std::vector<unsigned char> vchSignMsg;
    for (unsigned int j = 0; j < strSignedMsg.length(); j++)
        vchSignMsg.push_back(strSignedMsg[j]);

    std::string strSignMessage= "";
    const unsigned char* pOfficialMNNum = (const unsigned char*)&determinedMNCoinbaseData.nOfficialMNNum;
    strSignMessage.push_back(pOfficialMNNum[0]);
    strSignMessage.push_back(pOfficialMNNum[1]);

    const unsigned char* pGeneralMNNum = (const unsigned char*)&determinedMNCoinbaseData.nGeneralMNNum;
    strSignMessage.push_back(pGeneralMNNum[0]);
    strSignMessage.push_back(pGeneralMNNum[1]);

    const unsigned char* pRandomNum = (const unsigned char*)&determinedMNCoinbaseData.nRandomNum;
    strSignMessage.push_back(pRandomNum[0]);
    strSignMessage.push_back(pRandomNum[1]);
    strSignMessage.push_back(pRandomNum[2]);
    strSignMessage.push_back(pRandomNum[3]);

    const unsigned char* pnFirstBlock = (const unsigned char*)&determinedMNCoinbaseData.nFirstBlock;
    strSignMessage.push_back(pnFirstBlock[0]);
    strSignMessage.push_back(pnFirstBlock[1]);

    std::string strError;
    if (!CMessageSigner::VerifyMessage(keyID, vchSignMsg, strSignMessage, strError))
    {
        LogPrintf("SPOS_Error:VerifyDeterminedMasternodeMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}
