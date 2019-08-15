#ifndef SPOS_H
#define SPOS_H

#define SPOS_VERSION_REGIST_MASTERNODE 100
#define SPOS_REGIST_MASTERNODE_MAX 50

#include <vector>
#include <string>

class CSposHeader
{
public:
    uint16_t    nVersion;

    CSposHeader()
    {
        SetNull();
    }

    CSposHeader(const uint16_t& nVersionIn)
        : nVersion(nVersionIn) {
    }

    CSposHeader& operator=(const CSposHeader& header)
    {
        if(this == &header)
            return *this;

        nVersion = header.nVersion;
        return *this;
    }

    void SetNull()
    {
        nVersion = 0;
    }
};

class CDeterminedMasternodeData
{
public:
    std::string strIP;
    uint16_t    nPort;
    std::string strCollateralAddress;
    std::string strTxid;
    uint16_t    nOutputIndex;
    std::string strSerialPubKeyId;
    std::string strSignedMsg;

    CDeterminedMasternodeData()
    {
        SetNull();
    }

    void initPubkeyIdAndSign(const std::string &strSerialPubKeyId,const std::string &strSignedMsg)
    {
        this->strSerialPubKeyId = strSerialPubKeyId;
        this->strSignedMsg = strSignedMsg;
    }

    CDeterminedMasternodeData(const std::string& strIP,const uint16_t& nPort,const std::string& strCollateralAddress,const std::string& strTxid,
                              const uint16_t& nOutputIndex,const std::string& strSerialPubKeyId,const std::string& strSignedMsg)
        : strIP(strIP), nPort(nPort), strCollateralAddress(strCollateralAddress), strTxid(strTxid),nOutputIndex(nOutputIndex),
          strSerialPubKeyId(strSerialPubKeyId),strSignedMsg(strSignedMsg){
    }

    CDeterminedMasternodeData& operator=(const CDeterminedMasternodeData& data)
    {
        if(this == &data)
            return *this;

        strIP = data.strIP;
        nPort = data.nPort;
        strCollateralAddress = data.strCollateralAddress;
        strTxid = data.strTxid;
        nOutputIndex = data.nOutputIndex;
        strSerialPubKeyId = data.strSerialPubKeyId;
        strSignedMsg = data.strSignedMsg;
        return *this;
    }

    void SetNull()
    {
        strIP.clear();
        nPort = 0;
        strCollateralAddress.clear();
        strTxid.clear();
        nOutputIndex = 0;
        strSerialPubKeyId.clear();
        strSignedMsg.clear();
    }
};

bool CheckDeterminedMasternode(CDeterminedMasternodeData& dmn,std::string& strMessage,std::string& strError);

bool BuildDeterminedMasternode(CDeterminedMasternodeData& dmn,const std::string& strPrivKey,std::string& strError);
bool RegisterDeterminedMasternodes(std::vector<CDeterminedMasternodeData>& dmnVec,std::string& strError);

std::vector<unsigned char> FillDeterminedMasternode(const CSposHeader& header,const CDeterminedMasternodeData& dmn);

bool ParseSposReserve(const std::vector<unsigned char>& vReserve, CSposHeader& header, std::vector<unsigned char>& vData);
bool ParseDeterminedMasternode(const std::vector<unsigned char>& vDMNData, CDeterminedMasternodeData& dmn);

#endif // SPOS_H
