#if defined(HAVE_CONFIG_H)
#include "config/safe-chain.h"
#endif


#include "safevote.h"
#include "safevote.pb.h"



using namespace std;


#if SCN_CURRENT == SCN__main
string g_strSafeVoteAppID = "577e345eafbbc41a9cdb3d5e420ba337401c8e7353fe852b65f220f2470ee94a";
string g_strPXTAssetID = "dc1884981a18260303737d0a492b12de69b7928d8711399faf2d84ce394379b7";
string g_stradminSafeAddress = "XnC9y9fowx441hhxKKfsher25LTS7PQV8M";
int g_SafeVoteStartHeight = 1459779;
#elif SCN_CURRENT == SCN__dev
string g_strSafeVoteAppID = "577e345eafbbc41a9cdb3d5e420ba337401c8e7353fe852b65f220f2470ee94a";
string g_strPXTAssetID = "dc1884981a18260303737d0a492b12de69b7928d8711399faf2d84ce394379b7";
string g_stradminSafeAddress = "XnC9y9fowx441hhxKKfsher25LTS7PQV8M";
int g_SafeVoteStartHeight = 1000;
#elif SCN_CURRENT == SCN__test
string g_strSafeVoteAppID = "577e345eafbbc41a9cdb3d5e420ba337401c8e7353fe852b65f220f2470ee94a";
string g_strPXTAssetID = "dc1884981a18260303737d0a492b12de69b7928d8711399faf2d84ce394379b7";
string g_stradminSafeAddress = "XnC9y9fowx441hhxKKfsher25LTS7PQV8M";
int g_SafeVoteStartHeight = 1000;
#else
#error unsupported <safe chain name>
#endif


string g_strSafeVoteId = "a4bea6705cd38d535e873da1c9ad897048b6bbc8e286ca9b28bd18bb22eedcc9";


vector<unsigned char> FillRegSuperNodeCandidate(const CRegSuperNodeCandidate& regSuperNodeCandidate)
{
    vector<unsigned char> vData;

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
        datereginfo = data.add_info();
        datereginfo->set_safepubkey(tempregInfo.vchpubkey);
        datereginfo->set_safetxhash(tempregInfo.strsafeTxHash);
        datereginfo->set_safetxoutidx(tempregInfo.nsafeTxOutIdx);
        datereginfo->set_utxotype((const char*)&(tempregInfo.nutxoType));
    }

    std::map<std::string,std::string>::const_iterator ittemp = regSuperNodeCandidate.mapsignature.begin();
    for (; ittemp != regSuperNodeCandidate.mapsignature.end(); ittemp++)
    {
        data.mutable_signature()->insert({ittemp->first, ittemp->second});
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillUnregSuperNodeCandidate(const CUnRegSuperNodeCandidate& unRegSuperNodeCandidate)
{
    vector<unsigned char> vData;

    Safevote::UnRegSuperNodeCandidate data;
    
    data.set_regtxhash(unRegSuperNodeCandidate.strregTxHash);

    std::map<std::string,std::string>::const_iterator it = unRegSuperNodeCandidate.mapsignature.begin();
    for (; it != unRegSuperNodeCandidate.mapsignature.end(); ++it)
    {
        data.mutable_signature()->insert({it->first, it->second});
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillUpdateSuperNodeCandidate(const CUpdateSuperNodeCandidate& updateSuperNodeCandidate)
{
    vector<unsigned char> vData;

    Safevote::UpdateSuperNodeCandidate data;
    Safevote::SuperNodeUpdateInfo* datesupernodeupdateinfo;

    data.set_regtxhash(updateSuperNodeCandidate.strregTxHash);

    std::vector<CSuperNodeUpdateInfo>::const_iterator it = updateSuperNodeCandidate.vecSuperNodeUpdateInfo.begin();
    for (; it != updateSuperNodeCandidate.vecSuperNodeUpdateInfo.end(); ++it)
    {
        CSuperNodeUpdateInfo tempsuperNodeUpdateInfo = *it;
        datesupernodeupdateinfo = data.add_updateinfo();
        datesupernodeupdateinfo->set_dividendratio((const unsigned char*)&(tempsuperNodeUpdateInfo.ndividendRatio), sizeof(tempsuperNodeUpdateInfo.ndividendRatio));
        datesupernodeupdateinfo->set_bpname(tempsuperNodeUpdateInfo.strbpName);
        datesupernodeupdateinfo->set_bpurl(tempsuperNodeUpdateInfo.strbpURL);
    }

    std::map<std::string,std::string>::const_iterator ittemp = updateSuperNodeCandidate.mapsignature.begin();
    for (; ittemp != updateSuperNodeCandidate.mapsignature.end(); ittemp++)
    {
        data.mutable_signature()->insert({ittemp->first, ittemp->second});
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillHolderVote(const CHolderVote& holderVote)
{
    vector<unsigned char> vData;

    Safevote::HolderVote data;
    Safevote::VoteInfo* datevoteinfo;

    data.set_regtxhash(holderVote.strregTxHash);

    std::vector<CVoteInfo>::const_iterator it = holderVote.vecVoteInfo.begin();
    for (; it != holderVote.vecVoteInfo.end(); ++it)
    {
        CVoteInfo tempvoteInfo = *it;
        datevoteinfo = data.add_voteinfo();
        datevoteinfo->set_safepubkey(tempvoteInfo.vchpubkey);
        datevoteinfo->set_safetxhash(tempvoteInfo.strsafeTxHash);
        datevoteinfo->set_safetxoutidx(tempvoteInfo.nsafeTxOutIdx);
        datevoteinfo->set_safeamount(tempvoteInfo.nsafeAmount);
        datevoteinfo->set_utxotype((const char*)&(tempvoteInfo.nutxoType));
    }

    std::map<std::string,std::string>::const_iterator ittemp = holderVote.mapsignature.begin();
    for (; ittemp != holderVote.mapsignature.end(); ittemp++)
    {
        data.mutable_signature()->insert({ittemp->first, ittemp->second});
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillIVote(const CIVote& iVote)
{
    vector<unsigned char> vData;

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

vector<unsigned char> FillRevokeVtxo(const CRevokeVtxo& revokeVtxo)
{
    vector<unsigned char> vData;

    Safevote::RevokeVtxo data;
    Safevote::RevokeInfo* daterevokeinfo;

    std::vector<CRevokeInfo>::const_iterator it = revokeVtxo.vecRevokeInfo.begin();
    for (; it != revokeVtxo.vecRevokeInfo.end(); ++it)
    {
        CRevokeInfo temprevokeInfo = *it;
        daterevokeinfo = data.add_revokeinfo();
        daterevokeinfo->set_safepubkey(temprevokeInfo.vchpubkey);
        daterevokeinfo->set_safetxhash(temprevokeInfo.strsafeTxHash);
        daterevokeinfo->set_safetxoutidx(temprevokeInfo.nsafeTxOutIdx);
    }

    std::map<std::string,std::string>::const_iterator ittemp = revokeVtxo.mapsignature.begin();
    for (; ittemp != revokeVtxo.mapsignature.end(); ittemp++)
    {
        data.mutable_signature()->insert({ittemp->first, ittemp->second});
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillBindVoterSafecodeAccount(const CBindVoterSafecodeAccount& bindVoterSafecodeAccount)
{
    vector<unsigned char> vData;

    Safevote::BindVoterSafecodeAccount data;
    Safevote::BindInfo* datebindinfo;

    std::vector<CBindInfo>::const_iterator it = bindVoterSafecodeAccount.vecBindInfo.begin();
    for (; it != bindVoterSafecodeAccount.vecBindInfo.end(); ++it)
    {
       CBindInfo tempbindInfo = *it;
       datebindinfo = data.add_bindinfo();
       datebindinfo->set_safepubkey(tempbindInfo.vchpubkey);
       datebindinfo->set_scaccount(tempbindInfo.strscAccount);
    }

    std::map<std::string,std::string>::const_iterator ittemp = bindVoterSafecodeAccount.mapsignature.begin();
    for (; ittemp != bindVoterSafecodeAccount.mapsignature.end(); ittemp++)
    {
        data.mutable_signature()->insert({ittemp->first, ittemp->second});
    }

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
       vData.push_back(pData[i]);

    return vData;
}
