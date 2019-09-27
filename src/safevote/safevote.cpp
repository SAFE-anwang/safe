#if defined(HAVE_CONFIG_H)
#include "config/safe-chain.h"
#endif


#include "safevote.h"
#include "safevote.pb.h"



using namespace std;


#if SCN_CURRENT == SCN__main
string g_strSafeVoteAppID = "577e345eafbbc41a9cdb3d5e420ba337401c8e7353fe852b65f220f2470ee94a";
string g_strPXTAssetID = "dc1884981a18260303737d0a492b12de69b7928d8711399faf2d84ce394379b7";
int g_SafeVoteStartHeight = 1459779;
#elif SCN_CURRENT == SCN__dev
string g_strSafeVoteAppID = "577e345eafbbc41a9cdb3d5e420ba337401c8e7353fe852b65f220f2470ee94a";
string g_strPXTAssetID = "dc1884981a18260303737d0a492b12de69b7928d8711399faf2d84ce394379b7";
int g_SafeVoteStartHeight = 1000;
#elif SCN_CURRENT == SCN__test
string g_strSafeVoteAppID = "577e345eafbbc41a9cdb3d5e420ba337401c8e7353fe852b65f220f2470ee94a";
string g_strPXTAssetID = "dc1884981a18260303737d0a492b12de69b7928d8711399faf2d84ce394379b7";
int g_SafeVoteStartHeight = 1000;
#else
#error unsupported <safe chain name>
#endif


string g_strSafeVoteId = "a4bea6705cd38d535e873da1c9ad897048b6bbc8e286ca9b28bd18bb22eedcc9";


static void FillSafeVoteHeader(const CAppHeader& header, vector<unsigned char>& vHeader)
{
    // 1. flag: safe
    vHeader.push_back('s');
    vHeader.push_back('a');
    vHeader.push_back('f');
    vHeader.push_back('e');

    // 2. version (2 bytes)
    const unsigned char* pVersion = (const unsigned char*)&header.nVersion;
    vHeader.push_back(pVersion[0]);
    vHeader.push_back(pVersion[1]);

    // 3. app id (32 bytes)
    const unsigned char* pAppId = header.appId.begin();
    for(unsigned int i = 0; i < header.appId.size(); i++)
        vHeader.push_back(pAppId[i]);

    // 4. app command (4 bytes)
    const unsigned char* pAppCmd = (const unsigned char*)&header.nAppCmd;
    vHeader.push_back(pAppCmd[0]);
    vHeader.push_back(pAppCmd[1]);
    vHeader.push_back(pAppCmd[2]);
    vHeader.push_back(pAppCmd[3]);
}

vector<unsigned char> FillRegSuperNodeCandidate(const CAppHeader& header, const CRegSuperNodeCandidate& regSuperNodeCandidate)
{
    vector<unsigned char> vData;
    FillSafeVoteHeader(header, vData);

    Safevote::RegSuperNodeCandidate data;
    Safevote::RegInfo* datereginfo;
    
    data.set_scbppubkey(regSuperNodeCandidate.strscBpPubkey);
    data.set_dividendratio((const unsigned char*)&(regSuperNodeCandidate.ndividendRatio), sizeof(regSuperNodeCandidate.ndividendRatio));
    data.set_bpname(regSuperNodeCandidate.strbpName);
    data.set_bpurl(regSuperNodeCandidate.strbpURL);

    std::vector<CRegInfo>::const_iterator it = regSuperNodeCandidate.vecRegInfo.begin();
    for (; it != regSuperNodeCandidate.vecRegInfo.end(); ++it)
    {
        CRegInfo tempregInfo = *it;
        datereginfo = data.add_reginfo();
        datereginfo->set_safepubkey(tempregInfo.strsafePubkey);
        datereginfo->set_safetxhash(tempregInfo.strsafeTxHash);
        datereginfo->set_safetxoutidx((const unsigned char*)&(tempregInfo.nsafeTxOutIdx), sizeof(tempregInfo.nsafeTxOutIdx));
        datereginfo->set_utxoType((const unsigned char*)&(tempregInfo.nutxoType), sizeof(tempregInfo.nutxoType));
    }

    std::map<std::string,std::string>::const_iterator ittemp = regSuperNodeCandidate.mapsignature.begin();
    for (; ittemp != regSuperNodeCandidate.mapsignature.end(); ittemp++)
    {
        data.signature[ittemp->first] = ittemp.second;
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillUnregSuperNodeCandidate(const CAppHeader& header, const CUnRegSuperNodeCandidate& unRegSuperNodeCandidate)
{
    vector<unsigned char> vData;
    FillSafeVoteHeader(header, vData);

    Safevote::UnRegSuperNodeCandidate data;
    
    data.set_regtxhash(unRegSuperNodeCandidate.strregTxHash);

    std::map<std::string,std::string>::const_iterator it = unRegSuperNodeCandidate.mapsignature.begin();
    for (; it != unRegSuperNodeCandidate.mapsignature.end(); ++it)
    {
         data.signature[it->first] = it->second;   
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillUpdateSuperNodeCandidate(const CAppHeader& header, const CUpdateSuperNodeCandidate& updateSuperNodeCandidate)
{
    vector<unsigned char> vData;
    FillSafeVoteHeader(header, vData);

    Safevote::UpdateSuperNodeCandidate data;
    Safevote::SuperNodeUpdateInfo* datesupernodeupdateinfo;

    data.set_regtxhash(updateSuperNodeCandidate.strregTxHash);

    std::vector<CSuperNodeUpdateInfo>::const_iterator it = updateSuperNodeCandidate.vecSuperNodeUpdateInfo.begin();
    for (; it != updateSuperNodeCandidate.vecSuperNodeUpdateInfo.end(); ++it)
    {
        CSuperNodeUpdateInfo tempsuperNodeUpdateInfo = *it;
        datesupernodeupdateinfo = data.add_supernodeupdateinfo();
        datesupernodeupdateinfo->set_dividendratio((const unsigned char*)&(tempsuperNodeUpdateInfo.ndividendRatio), sizeof(tempsuperNodeUpdateInfo.ndividendRatio));
        datesupernodeupdateinfo->set_bpname(tempsuperNodeUpdateInfo.strbpName);
        datesupernodeupdateinfo->set_bpurl(tempsuperNodeUpdateInfo.strbpURL);
    }

    std::map<std::string,std::string>::const_iterator ittemp = updateSuperNodeCandidate.mapsignature.begin();
    for (; ittemp != updateSuperNodeCandidate.mapsignature.end(); ittemp++)
    {
        data.signature[ittemp->first] = ittemp.second;
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillHolderVote(const CAppHeader& header, const CHolderVote& holderVote)
{
    vector<unsigned char> vData;
    FillSafeVoteHeader(header, vData);

    Safevote::HolderVote data;
    Safevote::VoteInfo* datevoteinfo;

    data.set_regtxhash(holderVote.strregTxHash);

    std::vector<CVoteInfo>::const_iterator it = holderVote.vecVoteInfo.begin();
    for (; it != holderVote.vecVoteInfo.end(); ++it)
    {
        CVoteInfo tempvoteInfo = *it;
        datevoteinfo = data.add_voteinfo();
        datevoteinfo->set_safepubKey(tempvoteInfo.strsafePubkey)
        datevoteinfo->set_safetxhash(tempvoteInfo.strsafeTxHash);
        datevoteinfo->set_safetxoutidx((const unsigned char*)&(tempvoteInfo.nsafeTxOutIdx), sizeof(tempvoteInfo.nsafeTxOutIdx));
        datevoteinfo->set_safeamount((const unsigned char*)&(tempvoteInfo.nsafeAmount), sizeof(tempvoteInfo.nsafeAmount))
        datevoteinfo->set_utxotype((const unsigned char*)&(tempvoteInfo.nutxoType), sizeof(tempvoteInfo.nutxoType));
    }

    std::map<std::string,std::string>::const_iterator ittemp = holderVote.mapsignature.begin();
    for (; ittemp != holderVote.mapsignature.end(); ittemp++)
    {
        data.signature[ittemp->first] = ittemp.second;
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillIVote(const CAppHeader& header, const CIVote& iVote)
{
    vector<unsigned char> vData;
    FillSafeVoteHeader(header, vData);

    Safevote::IVote data;

    data.set_regtxhash(iVote.strregTxHash);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillRevokeVtxo(const CAppHeader& header, const CRevokeVtxo& revokeVtxo)
{
    vector<unsigned char> vData;
    FillSafeVoteHeader(header, vData);

    Safevote::RevokeVtxo data;
    Safevote::RevokeInfo* daterevokeinfo;

    std::vector<CRevokeInfo>::const_iterator it = revokeVtxo.vecRevokeInfo.begin();
    for (; it != revokeVtxo.vecRevokeInfo.end(); ++it)
    {
        CRevokeInfo temprevokeInfo = *it;
        daterevokeinfo = data.add_revokeinfo();
        daterevokeinfo->set_safepubKey(temprevokeInfo.strsafePubkey);
        daterevokeinfo->set_safetxhash(temprevokeInfo.strsafeTxHash);
        daterevokeinfo->set_safetxoutidx((const unsigned char*)&(temprevokeInfo.nsafeTxOutIdx), sizeof(temprevokeInfo.nsafeTxOutIdx));
    }

    std::map<std::string,std::string>::const_iterator ittemp = revokeVtxo.mapsignature.begin();
    for (; ittemp != revokeVtxo.mapsignature.end(); ittemp++)
    {
        data.signature[ittemp->first] = ittemp.second;
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillBindVoterSafecodeAccount(const CAppHeader& header, const CBindVoterSafecodeAccount& bindVoterSafecodeAccount)
{
    vector<unsigned char> vData;
    FillSafeVoteHeader(header, vData);

    Safevote::BindVoterSafecodeAccount data;
    Safevote::BindInfo* datebindinfo;

    std::vector<CBindInfo>::const_iterator it = bindVoterSafecodeAccount.vecBindInfo.begin();
    for (; it != revokeVtxo.vecRevokeInfo.end(); ++it)
    {
       CBindInfo tempbindInfo = *it;
       datebindinfo = data.add_bindinfo();
       datebindinfo->set_safepubKey(tempbindInfo.strsafePubkey);
       datebindinfo->set_scaccount(tempbindInfo.strscAccount);
    }

    std::map<std::string,std::string>::const_iterator ittemp = bindVoterSafecodeAccount.mapsignature.begin();
    for (; ittemp != bindVoterSafecodeAccount.mapsignature.end(); ittemp++)
    {
       data.signature[ittemp->first] = ittemp.second;
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
       vData.push_back(pData[i]);

    return vData;
}
