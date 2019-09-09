#ifndef SPOS_H
#define SPOS_H

#include <vector>
#include <string>
#include "amount.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "key.h"

#define SPOS_VERSION_REGIST_MASTERNODE 100
#define SPOS_REGIST_MASTERNODE_MAX 50
#define SPOS_REGIST_MASTERNODE_OUT_VALUE 0.0001 * COIN

struct CDeterministicMasternode_IndexValue;

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

class CDeterministicMasternodeData
{
public:
    std::string strIP;
    uint16_t    nPort;
    std::string strCollateralAddress;
    std::string strDMNTxid;
    uint16_t    nDMNOutputIndex;
    std::string strSerialPubKeyId;
    std::string strSignedMsg;
    std::string strOfficialSignedMsg;

    CDeterministicMasternodeData()
    {
        SetNull();
    }

    void initPubkeyIdAndSign(const std::string &strSerialPubKeyId,const std::string &strSignedMsg,const std::string &strOfficialSignedMsg)
    {
        this->strSerialPubKeyId = strSerialPubKeyId;
        this->strSignedMsg = strSignedMsg;
        this->strOfficialSignedMsg = strOfficialSignedMsg;
    }

    CDeterministicMasternodeData(const std::string& strIP,const uint16_t& nPort,const std::string& strCollateralAddress,const std::string& strTxid,const uint16_t& nOutputIndex,
                                const std::string& strSerialPubKeyId,const std::string& strSignedMsg,const std::string& strOfficialSignedMsg)
                    : strIP(strIP), nPort(nPort), strCollateralAddress(strCollateralAddress), strDMNTxid(strTxid),nDMNOutputIndex(nOutputIndex),
                      strSerialPubKeyId(strSerialPubKeyId),strSignedMsg(strSignedMsg),strOfficialSignedMsg(strOfficialSignedMsg){
    }

    CDeterministicMasternodeData& operator=(const CDeterministicMasternodeData& data)
    {
        if(this == &data)
            return *this;

        strIP = data.strIP;
        nPort = data.nPort;
        strCollateralAddress = data.strCollateralAddress;
        strDMNTxid = data.strDMNTxid;
        nDMNOutputIndex = data.nDMNOutputIndex;
        strSerialPubKeyId = data.strSerialPubKeyId;
        strSignedMsg = data.strSignedMsg;
        strOfficialSignedMsg = data.strOfficialSignedMsg;
        return *this;
    }

    void SetNull()
    {
        strIP.clear();
        nPort = 0;
        strCollateralAddress.clear();
        strDMNTxid.clear();
        nDMNOutputIndex = 0;
        strSerialPubKeyId.clear();
        strSignedMsg.clear();
        strOfficialSignedMsg.clear();
    }
};

class CDeterministicMNCoinbaseData
{
public:
    CKey        keyMasternode{};
    CKeyID      keyIDMasternode{};
    uint16_t    nOfficialMNNum;
    uint16_t    nGeneralMNNum;
    uint32_t    nRandomNum;
    uint16_t    nFirstBlock;
    

    CDeterministicMNCoinbaseData()
    {
        SetNull();
    }

    CDeterministicMNCoinbaseData(const CKey& keyMasternodeIn, const CKeyID& keyIDMasternodeIn, const uint16_t& nOfficialMNNumIn,
                                        const uint16_t& nGeneralMNNumIn, const uint32_t& nRandomNumIn, const uint16_t& nFirstBlockIn)
                                        :keyMasternode(keyMasternodeIn), keyIDMasternode(keyIDMasternodeIn), nOfficialMNNum(nOfficialMNNumIn),
                                        nGeneralMNNum(nGeneralMNNumIn), nRandomNum(nRandomNumIn), nFirstBlock(nFirstBlockIn)
    {

    }

    CDeterministicMNCoinbaseData& operator=(const CDeterministicMNCoinbaseData& data)
    {
        if(this == &data)
            return *this;

        keyMasternode = data.keyMasternode;
        keyIDMasternode = data.keyIDMasternode;
        nOfficialMNNum = data.nOfficialMNNum;
        nGeneralMNNum = data.nGeneralMNNum;
        nRandomNum = data.nRandomNum;
        nFirstBlock = data.nFirstBlock;

        return *this;
    }

    void SetNull()
    {
        nOfficialMNNum = 0;
        nGeneralMNNum = 0;
        nRandomNum = 0;
        nFirstBlock = 0;
    }
};
bool CheckDeterministicMasternode(CDeterministicMasternodeData& dmn,std::string& strMsg,bool& fExist,std::string& strError,const bool& fWithMempool);

bool BuildDeterministicMasternode(CDeterministicMasternodeData& dmn,const std::string& strPrivKey,bool& fExist,std::string& strError);
bool RegisterDeterministicMasternodes(std::vector<CDeterministicMasternodeData>& dmnVec,std::string& strError);
std::vector<unsigned char> FillDeterministicCoinbaseData(const CSposHeader& header, const CDeterministicMNCoinbaseData& deterministicMNCoinbaseData, bool& bRet);
bool ParseDeterministicMNCoinbaseData(const std::vector<unsigned char>& vData, CDeterministicMNCoinbaseData& deterministicMNCoinbaseData);

std::vector<unsigned char> FillDeterministicMasternode(const CSposHeader& header,const CDeterministicMasternodeData& dmn);

bool ParseSposReserve(const std::vector<unsigned char>& vReserve, CSposHeader& header, std::vector<unsigned char>& vData);
bool ParseDMNReserve(const std::vector<unsigned char>& vReserve, CSposHeader& header, std::vector<unsigned char>& vData, const int& nHeight);

bool ParseDeterministicMasternode(const std::vector<unsigned char>& vDMNData, CDeterministicMasternodeData& dmn,const int& nHeight);

bool ExistDeterministicMasternode(const COutPoint& out, CDeterministicMasternode_IndexValue& dmn_IndexValue, const bool fWithMempool = true);

#endif // SPOS_H
