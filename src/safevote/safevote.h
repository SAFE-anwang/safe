#ifndef SAFEVOTE_H
#define SAFEVPTE_H


#include "uint256.h"
#include "app/app.h"
#include "pubkey.h"
#include <vector>
#include <map>

#define REG_SUPER_NODE_CANDIDATE_CMD       1000
#define UN_REG_SUPER_NODE_CANDIDATE_CMD    1001
#define UPDATE_SUPER_NODE_CANDIDATE_CMD    1002
#define HOLDER_VOTE_CMD                    1010
#define REVOKE_VTXO_CMD                    1012
#define BIND_VOTER_SAFE_CODE_ACCOUNT       1020

#define MAX_SCBPPUBKEY_SIZE                60
#define MAX_DIVIDENDRATIO_VALUE            100
#define MAX_BPNAME_SIZE                    32
#define MAX_BPURL_SIZE                     256
#define ARRAY_NUM                          3



enum SafeVoteRpcError
{
    INVALID_SCBPPUBKEY_SIZE         = -500,
    INVALID_DIVIDENDRATIO_VALUE     = -501,
    INVALID_BPNAME_SIZE             = -502,
    INVALID_BPURL_SIZE              = -503,
    INVALID_ARRAY_NUM               = -504,
    INVALID_UNLOCKEDHEIGHT          = -505,
};



extern std::string g_strSafeVoteAppID;
extern std::string g_strPXTAssetID;
extern std::string g_stradminSafeAddress;
extern int g_SafeVoteStartHeight;



class CRegInfo
{
public:
    std::string strsafePubkey;
    std::string strsafeTxHash;
    uint32_t nsafeTxOutIdx;
    uint8_t nutxoType;

    CRegInfo()
    {
        SetNull();
    }

    CRegInfo(const std::string& strsafePubkeyIn, const std::string& strsafeTxHashIn, const uint32_t& nsafeTxOutIdxIn, const uint8_t& nutxoTypeIn, const CPubKey& vchPubKeyIn)
               : strsafePubkey(strsafePubkeyIn), strsafeTxHash(strsafeTxHashIn), nsafeTxOutIdx(nsafeTxOutIdxIn), nutxoType(nutxoTypeIn)
    {
    }

    CRegInfo& operator=(const CRegInfo& data)
    {
        if(this == &data)
            return *this;

        strsafePubkey = data.strsafePubkey;
        strsafeTxHash = data.strsafeTxHash;
        nsafeTxOutIdx = data.nsafeTxOutIdx;
        nutxoType = data.nutxoType;

        return *this;
    }
    
    void SetNull()
    {
        strsafePubkey.clear();
        strsafeTxHash.clear();
        nsafeTxOutIdx = 0;
        nutxoType = 0;
    }
};

class CRegSuperNodeCandidate
{
public:
    std::string strscBpPubkey;
    uint16_t ndividendRatio;
    std::string strbpName;
    std::string strbpURL;

    std::vector<CRegInfo> vecRegInfo;
    std::map<std::string,std::string> mapsignature;

    CRegSuperNodeCandidate()
    {
        SetNull();
    }

    CRegSuperNodeCandidate(const std::string& strscBpPubkeyIn, const uint16_t& ndividendRatioIn, const std::string& strbpNameIn, const std::string& strbpURLIn, const std::vector<CRegInfo>& vecRegInfoIn, const std::map<std::string,std::string>& mapsignatureIn = (std::map<std::string,std::string>()))
                                :strscBpPubkey(strscBpPubkeyIn), ndividendRatio(ndividendRatioIn), strbpName(strbpNameIn), vecRegInfo(vecRegInfoIn), mapsignature(mapsignatureIn)
    {
    }

    CRegSuperNodeCandidate& operator=(const CRegSuperNodeCandidate& data)
    {
        if(this == &data)
            return *this;

        strscBpPubkey = data.strscBpPubkey;
        ndividendRatio = data.ndividendRatio;
        strbpName = data.strbpName;
        strbpURL = data.strbpURL;
        vecRegInfo = data.vecRegInfo;
        mapsignature = data.mapsignature;

        return *this;
    }

    void SetNull()
    {
        strscBpPubkey.clear();
        ndividendRatio = 0;
        strbpName.clear();
        strbpURL.clear();
        vecRegInfo.clear();
        mapsignature.clear();
    }
};

class CUnRegSuperNodeCandidate
{
public:
    std::string strregTxHash;
    std::map<std::string,std::string> mapsignature;
    
    CUnRegSuperNodeCandidate()
    {
        SetNull();
    }

    CUnRegSuperNodeCandidate(const std::string& strregTxHashIn,        const std::map<std::string,std::string>& mapsignatureIn = (std::map<std::string,std::string>()))
                                    :strregTxHash(strregTxHashIn), mapsignature(mapsignatureIn)
    {
    }

    CUnRegSuperNodeCandidate& operator=(const CUnRegSuperNodeCandidate& data)
    {
        if(this == &data)
            return *this;

        strregTxHash = data.strregTxHash;
        mapsignature = data.mapsignature;

        return *this;
    }

    void SetNull()
    {
        strregTxHash.clear();
        mapsignature.clear();
    }
};

class CSuperNodeUpdateInfo
{
public:
    uint16_t ndividendRatio;
    std::string strbpName;
    std::string strbpURL;

    CSuperNodeUpdateInfo()
    {
        SetNull();
    }

    CSuperNodeUpdateInfo(const uint16_t& ndividendRatioIn, const std::string& strbpNameIn, const std::string& strbpURLIn)
                              :ndividendRatio(ndividendRatioIn), strbpName(strbpNameIn), strbpURL(strbpURLIn)
    {
    }

    CSuperNodeUpdateInfo& operator=(const CSuperNodeUpdateInfo& data)
    {
        if(this == &data)
            return *this;

        ndividendRatio = data.ndividendRatio;
        strbpName = data.strbpName;
        strbpURL = data.strbpURL;

        return *this;
    }
                              
    void SetNull()
    {
        ndividendRatio = 0;
        strbpName.clear();
        strbpURL.clear();
    }
    
};

class CUpdateSuperNodeCandidate
{
public:
    std::string strregTxHash;
    std::vector<CSuperNodeUpdateInfo> vecSuperNodeUpdateInfo;
    std::map<std::string, std::string> mapsignature;

    CUpdateSuperNodeCandidate()
    {
        SetNull();
    }

    CUpdateSuperNodeCandidate(const std::string& strregTxHashIn, const std::vector<CSuperNodeUpdateInfo>& vecSuperNodeUpdateInfoIn,        const std::map<std::string,std::string>& mapsignatureIn = (std::map<std::string,std::string>()))
                                     :strregTxHash(strregTxHashIn), vecSuperNodeUpdateInfo(vecSuperNodeUpdateInfoIn), mapsignature(mapsignatureIn)
    {
    }

    CUpdateSuperNodeCandidate& operator=(const CUpdateSuperNodeCandidate& data)
    {
        if(this == &data)
            return *this;

        strregTxHash = data.strregTxHash;
        vecSuperNodeUpdateInfo = data.vecSuperNodeUpdateInfo;
        mapsignature = data.mapsignature;

        return *this;
    }

    void SetNull()
    {
        strregTxHash.clear();
        vecSuperNodeUpdateInfo.clear();
        mapsignature.clear();
    }
};

class CVoteInfo
{
public:
    std::string strsafePubkey;
    std::string strsafeTxHash;
    int32_t nsafeTxOutIdx;
    CAmount nsafeAmount;
    uint8_t nutxoType;

    CVoteInfo()
    {
        SetNull();
    }

    CVoteInfo(const std::string& strsafePubkeyIn, const std::string& strsafeTxHashIn, const int32_t& nsafeTxOutIdxIn, const CAmount& nsafeAmountIn, const uint8_t& nutxoTypeIn)
                :strsafePubkey(strsafePubkeyIn), strsafeTxHash(strsafeTxHashIn), nsafeTxOutIdx(nsafeTxOutIdxIn), nsafeAmount(nsafeAmountIn), nutxoType(nutxoTypeIn)
    {
    }
    
    CVoteInfo& operator=(const CVoteInfo& data)
    {
        if(this == &data)
            return *this;

        strsafePubkey = data.strsafePubkey;
        strsafeTxHash = data.strsafeTxHash;
        nsafeTxOutIdx = data.nsafeTxOutIdx;
        nsafeAmount = data.nsafeAmount;
        nutxoType = data.nutxoType;

        return *this;
    }

    void SetNull()
    {
        strsafePubkey.clear();
        strsafeTxHash.clear();
        nsafeTxOutIdx = 0;
        nsafeAmount = 0;
        nutxoType = 0;
    }
};

class CHolderVote
{
public:
    std::string strregTxHash;
    std::vector<CVoteInfo> vecVoteInfo;
    std::map<std::string, std::string> mapsignature;

    CHolderVote()
    {
        SetNull();
    }

    CHolderVote(const std::string& strregTxHashIn, const std::vector<CVoteInfo>& vecVoteInfoIn, const std::map<std::string,std::string>& mapsignatureIn = (std::map<std::string,std::string>()))
                   :strregTxHash(strregTxHashIn), vecVoteInfo(vecVoteInfoIn), mapsignature(mapsignatureIn)
    {
    }

    CHolderVote& operator=(const CHolderVote& data)
    {
        if(this == &data)
            return *this;

        strregTxHash = data.strregTxHash;
        vecVoteInfo = data.vecVoteInfo;
        mapsignature = data.mapsignature;

        return *this;
    }

    void SetNull()
    {
        strregTxHash.clear();
        vecVoteInfo.clear();
        mapsignature.clear();
    }

};

class CIVote
{
public:
    std::string strregTxHash;

    CIVote()
    {
        SetNull();
    }

    CIVote(const std::string& strregTxHashIn) : strregTxHash(strregTxHashIn)
    {
    }

    CIVote& operator=(const CIVote& data)
    {
        if(this == &data)
            return *this;

        strregTxHash = data.strregTxHash;

        return *this;
    }

    void SetNull()
    {
        strregTxHash.clear();
    }
    
};

class CRevokeInfo
{
public:
    std::string strsafePubkey;
    std::string strsafeTxHash;
    uint32_t nsafeTxOutIdx;

    CRevokeInfo()
    {
        SetNull();
    }

    CRevokeInfo(const std::string& strsafePubkeyIn, const std::string& strsafeTxHashIn, const uint32_t& nsafeTxOutIdxIn)
                :strsafePubkey(strsafePubkeyIn), strsafeTxHash(strsafeTxHashIn), nsafeTxOutIdx(nsafeTxOutIdxIn)
    {
    }
    
    CRevokeInfo& operator=(const CRevokeInfo& data)
    {
        if(this == &data)
            return *this;

        strsafePubkey = data.strsafePubkey;
        strsafeTxHash = data.strsafeTxHash;
        nsafeTxOutIdx = data.nsafeTxOutIdx;

        return *this;
    }

    void SetNull()
    {
        strsafePubkey.clear();
        strsafeTxHash.clear();
        nsafeTxOutIdx = 0;
    }
};

class CRevokeVtxo
{
public:
    std::vector<CRevokeInfo> vecRevokeInfo;
    std::map<std::string, std::string> mapsignature;

    CRevokeVtxo()
    {
        SetNull();
    }

    CRevokeVtxo(const std::vector<CRevokeInfo>& vecRevokeInfoIn, const std::map<std::string,std::string>& mapsignatureIn = (std::map<std::string,std::string>()))
                   : vecRevokeInfo(vecRevokeInfoIn), mapsignature(mapsignatureIn)
    {
    }

    CRevokeVtxo& operator=(const CRevokeVtxo& data)
    {
        if(this == &data)
            return *this;

        vecRevokeInfo = data.vecRevokeInfo;
        mapsignature = data.mapsignature;

        return *this;
    }

    void SetNull()
    {
        vecRevokeInfo.clear();
        mapsignature.clear();
    }

};

class CBindInfo 
{
public:
    std::string strsafePubkey;
    std::string strscAccount;

    CBindInfo()
    {
        SetNull();
    }

    CBindInfo(const std::string& strsafePubkeyIn, const std::string& strscAccountIn)
                :strsafePubkey(strsafePubkeyIn), strscAccount(strscAccountIn)
    {
    }
    
    CBindInfo& operator=(const CBindInfo& data)
    {
        if(this == &data)
            return *this;

        strsafePubkey = data.strsafePubkey;
        strscAccount = data.strscAccount;

        return *this;
    }

    void SetNull()
    {
        strsafePubkey.clear();
        strscAccount.clear();
    }
};

class CBindVoterSafecodeAccount
{
public:
    std::vector<CBindInfo> vecBindInfo;
    std::map<std::string, std::string> mapsignature;

    CBindVoterSafecodeAccount()
    {
        SetNull();
    }

    CBindVoterSafecodeAccount(const std::vector<CBindInfo>& vecBindInfoIn, const std::map<std::string,std::string>& mapsignatureIn = (std::map<std::string,std::string>()))
                   : vecBindInfo(vecBindInfoIn), mapsignature(mapsignatureIn)
    {
    }

    CBindVoterSafecodeAccount& operator=(const CBindVoterSafecodeAccount& data)
    {
        if(this == &data)
            return *this;

        vecBindInfo = data.vecBindInfo;
        mapsignature = data.mapsignature;

        return *this;
    }

    void SetNull()
    {
        vecBindInfo.clear();
        mapsignature.clear();
    }
};


std::vector<unsigned char> FillRegSuperNodeCandidate(const CAppHeader& header, const CRegSuperNodeCandidate& regSuperNodeCandidate);
std::vector<unsigned char> FillUnregSuperNodeCandidate(const CAppHeader& header, const CUnRegSuperNodeCandidate& unRegSuperNodeCandidate);
std::vector<unsigned char> FillUpdateSuperNodeCandidate(const CAppHeader& header, const CUpdateSuperNodeCandidate& updateSuperNodeCandidate);
std::vector<unsigned char> FillHolderVote(const CAppHeader& header, const CHolderVote& holderVote);
std::vector<unsigned char> FillIVote(const CAppHeader& header, const CIVote& iVote);
std::vector<unsigned char> FillRevokeVtxo(const CAppHeader& header, const CRevokeVtxo& revokeVtxo);
std::vector<unsigned char> FillBindVoterSafecodeAccount(const CAppHeader& header, const CBindVoterSafecodeAccount& bindVoterSafecodeAccount);



#endif

