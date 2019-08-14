#ifndef SPOS_H
#define SPOS_H

#define SPOS_VERSION_REGIST_MASTERNODE 100
#define SPOS_VERSION_COINBASE_OLD 1
#define SPOS_VERSION_COINBASE_NEW 2

#include <vector>
#include <string>
#include "key.h"

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
    CKey        keyMasternode{};
    CPubKey     pubKeyMasternode{};

    CDeterminedMasternodeData()
    {
        SetNull();
    }

    CDeterminedMasternodeData(const std::string& strIP,const uint16_t& nPort,const std::string& strCollateralAddress,const std::string& strTxid,
                              const uint16_t& nOutputIndex,const CKey& keyMasternode,const CPubKey& pubKeyMasternode)
        : strIP(strIP), nPort(nPort), strCollateralAddress(strCollateralAddress), strTxid(strTxid),nOutputIndex(nOutputIndex),
          keyMasternode(keyMasternode),pubKeyMasternode(pubKeyMasternode){
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
        keyMasternode = data.keyMasternode;
        pubKeyMasternode = data.pubKeyMasternode;
        return *this;
    }

    void SetNull()
    {
        strIP.clear();
        nPort = 0;
        strCollateralAddress.clear();
        strTxid.clear();
        nOutputIndex = 0;
    }
};

std::vector<unsigned char> FillDeterminedMasternode(const CSposHeader& header,const CDeterminedMasternodeData& dmn,bool& bRet);

#endif // SPOS_H
