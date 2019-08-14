#include "spos.h"
#include "spos.pb.h"
#include "messagesigner.h"
#include "streams.h"
#include "clientversion.h"
#include "util.h"

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

std::vector<unsigned char> FillDeterminedMasternode(const CSposHeader& header, const CDeterminedMasternodeData& dmn,bool& bRet)
{
    bRet = true;
    std::vector<unsigned char> vData;
    FillSPOSHeader(header, vData);

    Spos::DeterminedMasternodeList data;
    data.set_ip(dmn.strIP);
    data.set_port((const unsigned char*)&dmn.nPort,sizeof(dmn.nPort));
    data.set_collateraladdress(dmn.strCollateralAddress);
    data.set_txid(dmn.strTxid);
    data.set_outputindex((const unsigned char*)&dmn.nOutputIndex,sizeof(dmn.nOutputIndex));

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(1000);
    ssKey << dmn.pubKeyMasternode.GetID();
    std::string strSerialPubKeyId = ssKey.str();
    data.set_pubkeyid(strSerialPubKeyId);

    //--------add sign--------
    std::string strSignMessage= "";
    for(unsigned int i=0;i<dmn.strIP.size();i++)
        strSignMessage.push_back(dmn.strIP[i]);

    const unsigned char* pPort = (const unsigned char*)&dmn.nPort;
    strSignMessage.push_back(pPort[0]);
    strSignMessage.push_back(pPort[1]);

    for(unsigned int i=0;i<dmn.strCollateralAddress.size();i++)
        strSignMessage.push_back(dmn.strCollateralAddress[i]);

    for(unsigned int i=0;i<dmn.strTxid.size();i++)
        strSignMessage.push_back(dmn.strTxid[i]);

    const unsigned char* pOutputIndex = (const unsigned char*)&dmn.nOutputIndex;
    strSignMessage.push_back(pOutputIndex[0]);
    strSignMessage.push_back(pOutputIndex[1]);
    //------------------------

    std::vector<unsigned char> vchSig;
    if(!CMessageSigner::SignMessage(strSignMessage, vchSig, dmn.keyMasternode))
    {
        bRet = false;
        LogPrintf("SPOS_Error:SignDeterminedMasternodeMessage() failed\n");
        return vData;
    }

    std::string strError;
    if(!CMessageSigner::VerifyMessage(dmn.pubKeyMasternode, vchSig, strSignMessage, strError))
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
