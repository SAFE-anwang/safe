#include "app.h"
#include "app.pb.h"
#include "main.h"
#include "validation.h"
#include "utilstrencodings.h"

#include <algorithm>

using namespace std;

uint16_t g_nAppHeaderVersion = 1;
uint16_t g_nAppDataVersion = 1;
string g_strSafeAssetId = "cfe2450bf016e2ad8130e4996960a32e0686c1704b62a6ad02e49ee805a9b288";
string g_strSafePayId = "a4bea6705cd38d535e873da1c9ad897048b6bbc8e286ca9b28bd18bb22eedcc9";
CAmount APP_OUT_VALUE = 0.0001 * COIN;

static string szKeyWords[] = {
    "安资", "安聊", "安投", "安付", "安資",
    "SafeAsset", "SafeChat", "SafeVote", "SafePay", "AnWang", "BankLedger", "ElectionChain", "SafeNetSpace", "DarkNetSpace",
    "SAFE", "ELT", "DNC", "DNC2",
    "BTC", "ETH", "EOS", "LTC", "DASH", "ETC",
    "Bitcoin", "Ethereum", "LiteCoin", "Ethereum Classic",
    "人民币", "港元", "港币", "澳门元", "澳门币", "新台币", "RMB", "CNY", "HKD", "MOP", "TWD", "人民幣", "港幣", "澳門元", "澳門幣", "新台幣", "澳门幣",
    "mSAFE", "μSAFE", "duffs", "tSAFE", "mtSAFE", "μtSAFE", "tduffs", "AnYou", "SafeGame"
};

static string szSimilarKeyWords[] = {
    "安网", "银链", "安網", "銀鏈", "銀链", "银鏈", "安游", "安遊"
};

string TrimString(const string& strValue)
{
    string strTemp = strValue;
    strTemp.erase(0, strTemp.find_first_not_of(" "));
    strTemp.erase(strTemp.find_last_not_of(" ") + 1);

    return strTemp;
}

string ToLower(const string& strValue)
{
    string strTemp = strValue;
    transform(strTemp.begin(), strTemp.end(), strTemp.begin(), (int (*)(int))tolower);

    return strTemp;
}

bool IsKeyWord(const string& strValue)
{
    string strTempValue = ToLower(strValue);

    // compare with fixed keywords
    for(unsigned int i = 0; i < sizeof(szKeyWords) / sizeof(string); i++)
    {
        string strTemp = ToLower(szKeyWords[i]);
        if(strTempValue == strTemp)
            return true;
    }

    // compare with similar keyword
    for(unsigned int i = 0; i < sizeof(szSimilarKeyWords) / sizeof(string); i++)
    {
        string strTemp = ToLower(szSimilarKeyWords[i]);
        if(strTempValue.find(strTemp) != string::npos)
            return true;
    }

    return false;
}

bool IsContainSpace(const string& strValue)
{
    return strValue.find(' ') != string::npos || strValue.find('\t') != string::npos;
}

bool IsValidUrl(const string& strUrl)
{
    if(strUrl == "http://" || strUrl == "https://")
        return false;
    return ((strUrl.find("http://") == 0) || (strUrl.find("https://") == 0));
}

static void FillHeader(const CAppHeader& header, vector<unsigned char>& vHeader)
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

vector<unsigned char> FillRegisterData(const string& strAdminAddress, const CAppHeader& header, const CAppData& appData)
{
    vector<unsigned char> vData;
    FillHeader(header, vData);

    App::RegisterData data;
    uint16_t nVersion = g_nAppDataVersion;
    data.set_version((const unsigned char*)&nVersion, sizeof(nVersion));
    data.set_adminaddress(strAdminAddress);
    data.set_appname(appData.strAppName);
    data.set_appdesc(appData.strAppDesc);
    data.set_devtype((const unsigned char*)&appData.nDevType, sizeof(appData.nDevType));
    data.set_devname(appData.strDevName);
    data.set_weburl(appData.strWebUrl);
    data.set_logourl(appData.strLogoUrl);
    data.set_coverurl(appData.strCoverUrl);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillAuthData(const string& strAdminAddress, const CAppHeader& header, const CAuthData& authData)
{
    vector<unsigned char> vData;
    FillHeader(header, vData);

    App::AuthData data;
    uint16_t nVersion = g_nAppDataVersion;
    data.set_version((const unsigned char*)&nVersion, sizeof(nVersion));
    data.set_settype((const unsigned char*)&authData.nSetType, sizeof(authData.nSetType));
    data.set_adminaddress(strAdminAddress);
    data.set_useraddress(authData.strUserAddress);
    data.set_auth(authData.nAuth);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillExtendData(const CAppHeader& header, const CExtendData& extendData)
{
    vector<unsigned char> vData;
    FillHeader(header, vData);

    App::ExtendData data;
    uint16_t nVersion = g_nAppDataVersion;
    data.set_version((const unsigned char*)&nVersion, sizeof(nVersion));
    data.set_auth(extendData.nAuth);
    data.set_extenddata(extendData.strExtendData);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillIssueData(const CAppHeader& header, const CAssetData& assetData)
{
    vector<unsigned char> vData;
    FillHeader(header, vData);

    App::IssueData data;
    uint16_t nVersion = g_nAppDataVersion;
    data.set_version((const unsigned char*)&nVersion, sizeof(nVersion));
    data.set_shortname(assetData.strShortName);
    data.set_assetname(assetData.strAssetName);
    data.set_assetdesc(assetData.strAssetDesc);
    data.set_assetunit(assetData.strAssetUnit);
    data.set_totalamount(assetData.nTotalAmount);
    data.set_firstissueamount(assetData.nFirstIssueAmount);
    data.set_firstactualamount(assetData.nFirstActualAmount);
    data.set_decimals((const unsigned char*)&assetData.nDecimals, sizeof(assetData.nDecimals));
    data.set_destory(assetData.bDestory);
    data.set_paycandy(assetData.bPayCandy);
    data.set_candyamount(assetData.nCandyAmount);
    data.set_candyexpired((const unsigned char*)&assetData.nCandyExpired, sizeof(assetData.nCandyExpired));
    data.set_remarks(assetData.strRemarks);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillCommonData(const CAppHeader& header, const CCommonData& commonData)
{
    vector<unsigned char> vData;
    FillHeader(header, vData);

    App::CommonData data;
    uint16_t nVersion = g_nAppDataVersion;
    data.set_version((const unsigned char*)&nVersion, sizeof(nVersion));
    data.set_assetid(commonData.assetId.begin(), commonData.assetId.size());
    data.set_amount(commonData.nAmount);
    data.set_remarks(commonData.strRemarks);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillPutCandyData(const CAppHeader& header, const CPutCandyData& candyData)
{
    vector<unsigned char> vData;
    FillHeader(header, vData);

    App::PutCandyData data;
    uint16_t nVersion = g_nAppDataVersion;
    data.set_version((const unsigned char*)&nVersion, sizeof(nVersion));
    data.set_assetid(candyData.assetId.begin(), candyData.assetId.size());
    data.set_amount(candyData.nAmount);
    data.set_expired((const unsigned char*)&candyData.nExpired, sizeof(candyData.nExpired));
    data.set_remarks(candyData.strRemarks);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillGetCandyData(const CAppHeader& header, const CGetCandyData& candyData)
{
    vector<unsigned char> vData;
    FillHeader(header, vData);

    App::GetCandyData data;
    uint16_t nVersion = g_nAppDataVersion;
    data.set_version((const unsigned char*)&nVersion, sizeof(nVersion));
    data.set_assetid(candyData.assetId.begin(), candyData.assetId.size());
    data.set_amount(candyData.nAmount);
    data.set_remarks(candyData.strRemarks);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

vector<unsigned char> FillTransferSafeData(const CAppHeader& header, const CTransferSafeData& safeData)
{
    vector<unsigned char> vData;
    FillHeader(header, vData);

    App::TransferSafeData data;
    uint16_t nVersion = g_nAppDataVersion;
    data.set_version((const unsigned char*)&nVersion, sizeof(nVersion));
    data.set_remarks(safeData.strRemarks);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

static void ParseHeader(const vector<unsigned char>& vData, CAppHeader& header, unsigned int& nOffset)
{
    nOffset = TXOUT_RESERVE_MIN_SIZE;

    // 1. version (2 bytes)
    header.nVersion = *(uint16_t*)&vData[nOffset];
    nOffset += sizeof(header.nVersion);

    // 2. app id (32 bytes)
    vector<unsigned char> vAppId;
    for(unsigned int i = 0; i < header.appId.size(); i++)
        vAppId.push_back(vData[nOffset++]);
    header.appId = uint256(vAppId);

    // 3. app command (4 bytes)
    header.nAppCmd = *(uint32_t*)&vData[nOffset];
    nOffset += sizeof(header.nAppCmd);
}

bool ParseReserve(const vector<unsigned char>& vReserve, CAppHeader& header, vector<unsigned char>& vData)
{
    if(vReserve.size() <= TXOUT_RESERVE_MIN_SIZE + sizeof(uint16_t) + 32 + sizeof(uint32_t))
        return false;

    //SPOS no need to parse 
    unsigned int nStartSPOSOffset = TXOUT_RESERVE_MIN_SIZE;
    std::vector<unsigned char> vchConAlg;
    for(unsigned int k = 0; k < 4; k++)
        vchConAlg.push_back(vReserve[nStartSPOSOffset++]);

    if (vchConAlg[0] == 's' && vchConAlg[1] == 'p' && vchConAlg[2] == 'o' && vchConAlg[3] == 's')
        return false;

    unsigned int nOffset = 0;
    ParseHeader(vReserve, header, nOffset);

    for(unsigned int i = nOffset; i < vReserve.size(); i++)
        vData.push_back(vReserve[i]);

    return true;
}

bool ParseRegisterData(const vector<unsigned char>& vAppData, CAppData& appData, string* pAdminAddress)
{
    App::RegisterData data;
    if(!data.ParseFromArray(&vAppData[0], vAppData.size()))
        return false;

    if(pAdminAddress) *pAdminAddress = data.adminaddress();
    appData.strAppName = data.appname();
    appData.strAppDesc = data.appdesc();
    appData.nDevType = *(uint8_t*)data.devtype().data();
    appData.strDevName = data.devname();
    appData.strWebUrl = data.weburl();
    appData.strLogoUrl = data.logourl();
    appData.strCoverUrl = data.coverurl();

    return true;
}

bool ParseAuthData(const vector<unsigned char>& vAuthData, CAuthData& authData, string* pAdminAddress)
{
    App::AuthData data;
    if(!data.ParseFromArray(&vAuthData[0], vAuthData.size()))
        return false;

    if(pAdminAddress) *pAdminAddress = data.adminaddress();
    authData.nSetType = *(uint8_t*)data.settype().data();
    authData.strUserAddress = data.useraddress();
    authData.nAuth = data.auth();

    return true;
}

bool ParseExtendData(const vector<unsigned char>& vExtendData, CExtendData& extendData)
{
    App::ExtendData data;
    if(!data.ParseFromArray(&vExtendData[0], vExtendData.size()))
        return false;

    extendData.nAuth = data.auth();
    extendData.strExtendData = data.extenddata();

    return true;
}

bool ParseIssueData(const std::vector<unsigned char>& vAssetData, CAssetData& assetData)
{
    App::IssueData data;
    if(!data.ParseFromArray(&vAssetData[0], vAssetData.size()))
        return false;

    assetData.strShortName = data.shortname();
    assetData.strAssetName = data.assetname();
    assetData.strAssetDesc = data.assetdesc();
    assetData.strAssetUnit = data.assetunit();
    assetData.nTotalAmount = data.totalamount();
    assetData.nFirstIssueAmount = data.firstissueamount();
    assetData.nFirstActualAmount = data.firstactualamount();
    assetData.nDecimals = *(uint8_t*)data.decimals().data();
    assetData.bDestory = data.destory();
    assetData.bPayCandy = data.paycandy();
    assetData.nCandyAmount = data.candyamount();
    assetData.nCandyExpired = *(uint16_t*)data.candyexpired().data();
    assetData.strRemarks = data.remarks();

    return true;
}

bool ParseCommonData(const std::vector<unsigned char>& vCommonData, CCommonData& commonData)
{
    App::CommonData data;
    if(!data.ParseFromArray(&vCommonData[0], vCommonData.size()))
        return false;

    std::vector<unsigned char> vAssetId;
    unsigned int nSize = data.assetid().size();
    unsigned char* pAssetId = (unsigned char*)data.assetid().data();
    for(unsigned int i = 0; i < nSize; i++)
        vAssetId.push_back(pAssetId[i]);
    commonData.assetId = uint256(vAssetId);

    commonData.nAmount = data.amount();
    commonData.strRemarks = data.remarks();

    return true;
}

bool ParsePutCandyData(const std::vector<unsigned char>& vCandyData, CPutCandyData& candyData)
{
    App::PutCandyData data;
    if(!data.ParseFromArray(&vCandyData[0], vCandyData.size()))
        return false;

    std::vector<unsigned char> vAssetId;
    unsigned int nSize = data.assetid().size();
    unsigned char* pAssetId = (unsigned char*)data.assetid().data();
    for(unsigned int i = 0; i < nSize; i++)
        vAssetId.push_back(pAssetId[i]);
    candyData.assetId = uint256(vAssetId);

    candyData.nAmount = data.amount();
    candyData.nExpired = *(uint16_t*)data.expired().data();
    candyData.strRemarks = data.remarks();

    return true;
}

bool ParseGetCandyData(const std::vector<unsigned char>& vCandyData, CGetCandyData& candyData)
{
    App::GetCandyData data;
    if(!data.ParseFromArray(&vCandyData[0], vCandyData.size()))
        return false;

    std::vector<unsigned char> vAssetId;
    unsigned int nSize = data.assetid().size();
    unsigned char* pAssetId = (unsigned char*)data.assetid().data();
    for(unsigned int i = 0; i < nSize; i++)
        vAssetId.push_back(pAssetId[i]);
    candyData.assetId = uint256(vAssetId);

    candyData.nAmount = data.amount();
    candyData.strRemarks = data.remarks();

    return true;
}

bool ParseTransferSafeData(const vector<unsigned char>& vSafeData, CTransferSafeData& safeData)
{
    App::TransferSafeData data;
    if(!data.ParseFromArray(&vSafeData[0], vSafeData.size()))
        return false;

    safeData.strRemarks = data.remarks();

    return true;
}

bool ExistAppName(const string& strAppName, const bool fWithMempool)
{
    uint256 appId;
    return GetAppIdByAppName(strAppName, appId, fWithMempool);
}

bool ExistAppId(const uint256& appId, const bool fWithMempool)
{
    CAppId_AppInfo_IndexValue appInfo;
    return GetAppInfoByAppId(appId, appInfo, fWithMempool);
}

bool ExistShortName(const std::string& strShortName, const bool fWithMempool)
{
    uint256 assetId;
    return GetAssetIdByShortName(strShortName, assetId, fWithMempool);
}

bool ExistAssetName(const std::string& strAssetName, const bool fWithMempool)
{
    uint256 assetId;
    return GetAssetIdByAssetName(strAssetName, assetId, fWithMempool);
}

bool ExistAssetId(const uint256& assetId, const bool fWithMempool)
{
    CAssetId_AssetInfo_IndexValue assetInfo;
    return GetAssetInfoByAssetId(assetId, assetInfo, fWithMempool);
}
