#ifndef SAFECCTA_H
#define SAFECCTA_H

#include "uint256.h"
#include "app/app.h"
#include <vector>


extern CAmount BCCTA_OUT_VALUE;


class CCCTAInfo
{
public:
    std::string strscAccount;
    std::string strtargetChain;
    std::string strsepChar;
    std::string strbizRemarks;

    CCCTAInfo()
    {
        SetNull();
    }

    CCCTAInfo(const std::string& strscAccountIn, const std::string& strtargetChainIn, const std::string& strsepCharIn, const std::string& strbizRemarksIn)
                :strscAccount(strscAccountIn), strtargetChain(strtargetChainIn), strsepChar(strsepCharIn), strbizRemarks(strbizRemarksIn)
    {
    }

    CCCTAInfo& operator=(const CCCTAInfo& data)
    {
        if(this == &data)
            return *this;

        strscAccount = data.strscAccount;
        strtargetChain = data.strtargetChain;
        strsepChar = data.strsepChar;
        strbizRemarks = data.strbizRemarks;

        return *this;
    }
    
    void SetNull()
    {
        strscAccount.clear();
        strtargetChain.clear();
        strsepChar.clear();
        strbizRemarks.clear();
    }
};

class CRegCastCoinInfoes
{
public:
    std::string strscTxHash;
    std::string strassetName;
    std::string strassetId;
    uint64_t nquantity;
    std::string strsafeUser;

    CRegCastCoinInfoes()
    {
        SetNull();
    }

    CRegCastCoinInfoes(const std::string& strscTxHashIn, const std::string& strassetNameIn, const std::string& strassetIdIn, const uint64_t& nquantityIn, const std::string& strsafeUserIn)
                           :strscTxHash(strscTxHashIn), strassetName(strassetNameIn), strassetId(strassetIdIn), nquantity(nquantityIn), strsafeUser(strsafeUserIn)
    {
    }

    CRegCastCoinInfoes& operator=(const CRegCastCoinInfoes& data)
    {
        if(this == &data)
            return *this;

        strscTxHash = data.strscTxHash;
        strassetName = data.strassetName;
        strassetId = data.strassetId;
        nquantity = data.nquantity;
        strsafeUser = data.strsafeUser;

        return *this;
    }
    
    void SetNull()
    {
        strscTxHash.clear();
        strassetName.clear();
        strassetId.clear();
        nquantity = 0;
        strsafeUser.clear();
    }
};

class CBcctaCastCoin
{
public:
    std::vector<CRegCastCoinInfoes> vecRegInfo;

    CBcctaCastCoin()
    {
        SetNull();
    }

    CBcctaCastCoin(const std::vector<CRegCastCoinInfoes>& vecRegInfoIn):vecRegInfo(vecRegInfoIn)
    {
    }

    CBcctaCastCoin& operator=(const CBcctaCastCoin& data)
    {
        if(this == &data)
            return *this;

        vecRegInfo = data.vecRegInfo;

        return *this;
    }

    void SetNull()
    {
        vecRegInfo.clear();
    }
};

std::string FillCCCTAInfo(const CCCTAInfo& CCTAInfoIn);
std::string FillCBcctaCastCoin(const CBcctaCastCoin& BcctaCastCoinIn);


#endif

