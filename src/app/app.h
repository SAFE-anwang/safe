#ifndef APP_H
#define APP_H

#include "hash.h"
#include "uint256.h"
#include "serialize.h"
#include "amount.h"

#define REGISTER_TXOUT          4
#define ADD_AUTH_TXOUT          5
#define DELETE_AUTH_TXOUT       6
#define CREATE_EXTENDDATA_TXOUT 7

#define ALL_TXOUT               1
#define UNLOCKED_TXOUT          2
#define LOCKED_TXOUT            3
#define ISSUE_TXOUT             4
#define ADD_ISSUE_TXOUT         5
#define DESTORY_TXOUT           6
#define TRANSFER_TXOUT          7
#define PUT_CANDY_TXOUT         8
#define GET_CANDY_TXOUT         9
#define CHANGE_ASSET_TXOUT      10

#define REGISTER_APP_CMD        100
#define ADD_AUTH_CMD            101
#define DELETE_AUTH_CMD         102
#define CREATE_EXTEND_TX_CMD    103
#define ISSUE_ASSET_CMD         200
#define ADD_ASSET_CMD           201
#define TRANSFER_ASSET_CMD      202
#define DESTORY_ASSET_CMD       203
#define CHANGE_ASSET_CMD        204
#define PUT_CANDY_CMD           205
#define GET_CANDY_CMD           206
#define TRANSFER_SAFE_CMD       300

#define MAX_ADDRESS_SIZE        34

#define MAX_APPNAME_SIZE        50
#define MAX_APPDESC_SIZE        600
#define MIN_DEVTYPE_VALUE       1
#define MAX_DEVNAME_SIZE        100
#define MAX_WEBURL_SIZE         300
#define MAX_LOGOURL_SIZE        300
#define MAX_COVERURL_SIZE       300
#define MIN_SETTYPE_VALUE       1
#define MIN_AUTH_VALUE          1000
#define MAX_AUTH_VALUE          4294967295
#define MAX_EXTENDDATAT_SIZE    2956

#define MAX_SHORTNAME_SIZE      20
#define MAX_ASSETNAME_SIZE      20
#define MAX_ASSETDESC_SIZE      300
#define MAX_ASSETUNIT_SIZE      10
#define MIN_ASSETDECIMALS_VALUE 4
#define MAX_ASSETDECIMALS_VALUE 10
#define MIN_CANDYEXPIRED_VALUE  1
#define MAX_CANDYEXPIRED_VALUE  3
#define MAX_PUTCANDY_VALUE 5
#define MAX_REMARKS_SIZE        500
#define MIN_LOCKTIME_VALUE      1
#define MAX_LOCKTIME_VALUE      1200

#define GET_CANDY_TXOUT_SIZE    200

#define MIN_TOTALASSETS_VALUE   100
#define MAX_TOTALASSETS_VALUE   200000000000000

enum AppRpcError
{
    //! app errors
    NONEXISTENT_APPID               = -500,
    EXISTENT_APPID                  = -501,
    INSUFFICIENT_SAFE               = -502,
    INVALID_CANCELLED_SAFE          = -503,
    INVALID_APPNAME_SIZE            = -504,
    EXISTENT_APPNAME                = -505,
    INVALID_APPDESC_SIZE            = -506,
    INVALID_DEVTYPE_VALUE           = -507,
    INVALID_DEVNAME_SIZE            = -508,
    INVALID_WEBURL_SIZE             = -509,
    INVALID_LOGOURL_SIZE            = -510,
    INVALID_COVERURL_SIZE           = -511,
    GET_ADMIN_FAILED                = -512,
    INVALID_SETTYPE                 = -513,
    INVALID_ADDRESS                 = -514,
    NONEXISTENT_ADDRESS             = -515,
    INVALID_AUTH                    = -516,
    REPEAT_SET_AUTH                 = -517,
    EXISTENT_AUTH                   = -518,
    NONEXISTENT_AUTH                = -519,
    INSUFFICIENT_AUTH_FOR_APPCMD    = -520,
    INSUFFICIENT_AUTH_FOR_ADDRESS   = -521,
    INVALID_APPTXTYPE               = -522,
    INVALID_EXTENTDDATA             = -523,
    GET_EXTENTDDATAT_FAILED         = -524,
    GET_TXID_FAILED                 = -525,
    GET_APPINFO_FAILED              = -526,
    SYNCING_BLOCK                   = -527,
    COLLECTING_CANDYLIST            = -528,

    //! asset errors
    INVALID_SHORTNAME_SIZE          = -600,
    EXISTENT_SHORTNAME              = -601,
    INVALID_ASSETNAME_SIZE          = -602,
    EXISTENT_ASSETNAME              = -603,
    INVALID_ASSETDESC_SIZE          = -604,
    INVALID_ASSETUNIT_SIZE          = -605,
    INVALID_TOTAL_AMOUNT            = -606,
    INVALID_FIRST_AMOUNT            = -607,
    FIRST_EXCEED_TOTAL              = -608,
    INVALID_FIRST_ACTUAL_AMOUNT     = -609,
    INVALID_DECIMALS                = -610,
    INVALID_CANDYAMOUNT             = -611,
    CANDY_EXCEED_FIRST              = -612,
    INVALID_CANDYEXPIRED            = -613,
    INVALID_REMARKS_SIZE            = -614,
    EXISTENT_ASSETID                = -615,
    NONEXISTENT_ASSETID             = -616,
    INVALID_ASSET_AMOUNT            = -617,
    EXCEED_TOTAL_AMOUNT             = -618,
    DISABLE_DESTORY                 = -619,
    INSUFFICIENT_ASSET              = -620,
    INVALID_LOCKEDMONTH             = -621,
    NONEXISTENT_ASSETCANDY          = -622,
    GET_ALL_ADDRESS_SAFE_FAILED     = -623,
    INVALID_TOTAL_SAFE              = -624,
    GET_ASSETINFO_FAILED            = -625,
    GET_ALL_CANDY_HEIGHT_FAILED     = -626,
    NOT_OWN_ASSET                   = -627,
    EXCEED_PUT_CANDY_TOTAL_AMOUNT   = -628,
    GET_GET_CANDY_TOTAL_FAILED      = -629,
};

extern uint16_t g_nAppHeaderVersion;
extern uint16_t g_nAppDataVersion;
extern std::string g_strSafeAssetId;
extern std::string g_strSafePayId;
extern CAmount APP_OUT_VALUE;

class CAppHeader
{
public:
    uint16_t    nVersion;
    uint256     appId;
    uint32_t    nAppCmd;

    CAppHeader()
    {
        SetNull();
    }

    CAppHeader(const uint16_t& nVersionIn, const uint256& appIdIn, const uint32_t& nAppCmdIn)
        : nVersion(nVersionIn), appId(appIdIn), nAppCmd(nAppCmdIn) {
    }

    CAppHeader& operator=(const CAppHeader& header)
    {
        if(this == &header)
            return *this;

        nVersion = header.nVersion;
        appId = header.appId;
        nAppCmd = header.nAppCmd;
        return *this;
    }

    void SetNull()
    {
        nVersion = 0;
        appId.SetNull();
        nAppCmd = 0;
    }
};

class CAppData
{
public:
    std::string     strAppName;
    std::string     strAppDesc;
    uint8_t         nDevType;
    std::string     strDevName;
    std::string     strWebUrl;
    std::string     strLogoUrl;
    std::string     strCoverUrl;

    CAppData()
    {
        SetNull();
    }

    CAppData(const std::string& strAppNameIn,
        const std::string& strAppDescIn,
        const uint8_t& nDevTypeIn,
        const std::string& strDevNameIn,
        const std::string& strWebUrlIn,
        const std::string& strLogoUrlIn,
        const std::string& strCoverUrlIn)
        : strAppName(strAppNameIn),
          strAppDesc(strAppDescIn),
          nDevType(nDevTypeIn),
          strDevName(strDevNameIn),
          strWebUrl(strWebUrlIn),
          strLogoUrl(strLogoUrlIn),
          strCoverUrl(strCoverUrlIn) {
    }

    CAppData& operator=(const CAppData& data)
    {
        if(this == &data)
            return *this;

        strAppName = data.strAppName;
        strAppDesc = data.strAppDesc;
        nDevType = data.nDevType;
        strDevName = data.strDevName;
        strWebUrl = data.strWebUrl;
        strLogoUrl = data.strLogoUrl;
        strCoverUrl = data.strCoverUrl;
        return *this;
    }

    void SetNull()
    {
        strAppName.clear();
        strAppDesc.clear();
        nDevType = 0;
        strDevName.clear();
        strWebUrl.clear();
        strLogoUrl.clear();
        strCoverUrl.clear();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(LIMITED_STRING(strAppName, MAX_APPNAME_SIZE));
        READWRITE(LIMITED_STRING(strAppDesc, MAX_APPDESC_SIZE));
        READWRITE(nDevType);
        READWRITE(LIMITED_STRING(strDevName, MAX_DEVNAME_SIZE));
        READWRITE(LIMITED_STRING(strWebUrl, MAX_WEBURL_SIZE));
        READWRITE(LIMITED_STRING(strLogoUrl, MAX_LOGOURL_SIZE));
        READWRITE(LIMITED_STRING(strCoverUrl, MAX_COVERURL_SIZE));
    }

    uint256 GetHash()
    {
        return SerializeHash(*this);
    }
};

class CAuthData
{
public:
    uint8_t         nSetType;
    std::string     strUserAddress;
    uint32_t        nAuth;

    CAuthData()
    {
        SetNull();
    }

    CAuthData(const uint8_t& nSetTypeIn, const std::string& strUserAddressIn, const uint32_t& nAuthIn)
        : nSetType(nSetTypeIn), strUserAddress(strUserAddressIn), nAuth(nAuthIn) {
    }

    CAuthData& operator=(const CAuthData& data)
    {
        if(this == &data)
            return *this;

        nSetType = data.nSetType;
        strUserAddress = data.strUserAddress;
        nAuth = data.nAuth;
        return *this;
    }

    void SetNull()
    {
        nSetType = 0;
        strUserAddress.clear();
        nAuth = 0;
    }
};

class CExtendData
{
public:
    uint32_t nAuth;
    std::string strExtendData;

    CExtendData()
    {
        SetNull();
    }

    CExtendData(const uint32_t& nAuthIn, const std::string& strExtendDataIn)
        : nAuth(nAuthIn), strExtendData(strExtendDataIn) {
    }

    CExtendData& operator=(const CExtendData& data)
    {
        if(this == &data)
            return *this;

        nAuth = data.nAuth;
        strExtendData = data.strExtendData;
        return *this;
    }

    void SetNull()
    {
        nAuth = 0;
        strExtendData.clear();
    }
};

class CAssetData
{
public:
    std::string     strShortName;
    std::string     strAssetName;
    std::string     strAssetDesc;
    std::string     strAssetUnit;
    CAmount         nTotalAmount;
    CAmount         nFirstIssueAmount;
    CAmount         nFirstActualAmount;
    uint8_t         nDecimals;
    bool            bDestory;
    bool            bPayCandy;
    CAmount         nCandyAmount;
    uint16_t        nCandyExpired;
    std::string     strRemarks;

    CAssetData()
    {
        SetNull();
    }

    CAssetData(const std::string& strShortNameIn,
        const std::string& strAssetNameIn,
        const std::string& strAssetDescIn,
        const std::string& strAssetUnitIn,
        const CAmount& nTotalAmountIn,
        const CAmount& nFirstIssueAmountIn,
        const CAmount& nFirstActualAmountIn,
        const uint8_t& nDecimalsIn,
        const bool bDestoryIn,
        const bool bPayCandyIn,
        const CAmount& nCandyAmountIn,
        const uint16_t& nCandyExpiredIn,
        const std::string& strRemarksIn)
        : strShortName(strShortNameIn),
          strAssetName(strAssetNameIn),
          strAssetDesc(strAssetDescIn),
          strAssetUnit(strAssetUnitIn),
          nTotalAmount(nTotalAmountIn),
          nFirstIssueAmount(nFirstIssueAmountIn),
          nFirstActualAmount(nFirstActualAmountIn),
          nDecimals(nDecimalsIn),
          bDestory(bDestoryIn),
          bPayCandy(bPayCandyIn),
          nCandyAmount(nCandyAmountIn),
          nCandyExpired(nCandyExpiredIn),
          strRemarks(strRemarksIn) {
    }		

    CAssetData& operator=(const CAssetData& data)
    {
        if(this == &data)
            return *this;

        strShortName = data.strShortName;
        strAssetName = data.strAssetName;
        strAssetDesc = data.strAssetDesc;
        strAssetUnit = data.strAssetUnit;
        nTotalAmount = data.nTotalAmount;
        nFirstIssueAmount = data.nFirstIssueAmount;
        nFirstActualAmount = data.nFirstActualAmount;
        nDecimals = data.nDecimals;
        bDestory = data.bDestory;
        bPayCandy = data.bPayCandy;
        nCandyAmount = data.nCandyAmount;
        nCandyExpired = data.nCandyExpired;
        strRemarks = data.strRemarks;
        return *this;
    }

    void SetNull()
    {
        strShortName.clear();
        strAssetName.clear();
        strAssetDesc.clear();
        strAssetUnit.clear();
        nTotalAmount = 0;
        nFirstIssueAmount = 0;
        nFirstActualAmount = 0;
        nDecimals = 0;
        bDestory = false;
        bPayCandy = false;
        nCandyAmount = 0;
        nCandyExpired = 0;
        strRemarks.clear();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(LIMITED_STRING(strShortName, MAX_SHORTNAME_SIZE));
        READWRITE(LIMITED_STRING(strAssetName, MAX_ASSETNAME_SIZE));
        READWRITE(LIMITED_STRING(strAssetDesc, MAX_ASSETDESC_SIZE));
        READWRITE(LIMITED_STRING(strAssetUnit, MAX_ASSETUNIT_SIZE));
        READWRITE(nTotalAmount);
        READWRITE(nFirstIssueAmount);
        READWRITE(nFirstActualAmount);
        READWRITE(nDecimals);
        READWRITE(bDestory);
        READWRITE(bPayCandy);
        READWRITE(nCandyAmount);
        READWRITE(nCandyExpired);
        READWRITE(LIMITED_STRING(strRemarks, MAX_REMARKS_SIZE));
    }

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

	friend inline bool operator==(const CAssetData& a, const CAssetData& b) { return a.strAssetName == b.strAssetName; }

	friend inline bool operator==(const CAssetData& a, const std::string& b) { return a.strAssetName == b; }

	friend inline bool operator==(const std::string& a, const CAssetData& b) { return a == b.strAssetName; }

	friend inline bool operator==(const CAssetData& a, const uint256& b) { return a.GetHash() == b; }

	friend inline bool operator==(const uint256& a, const CAssetData& b) { return a == b.GetHash(); }
};

class CCommonData // for asset add, transfer and destory
{
public:
    uint256         assetId;
    CAmount         nAmount;
    std::string     strRemarks;

    CCommonData()
    {
        SetNull();
    }

    CCommonData(const uint256& assetIdIn, const CAmount& nAmountIn, const std::string& strRemarksIn)
        : assetId(assetIdIn), nAmount(nAmountIn), strRemarks(strRemarksIn) {
    }

    CCommonData& operator=(const CCommonData& data)
    {
        if(this == &data)
            return *this;

        assetId =  data.assetId;
        nAmount = data.nAmount;
        strRemarks = data.strRemarks;
        return *this;
    }

    void SetNull()
    {
        assetId.SetNull();
        nAmount = 0;
        strRemarks.clear();
    }
};

class CPutCandyData
{
public:
    uint256         assetId;
    CAmount         nAmount;
    uint16_t        nExpired;
    std::string     strRemarks;

    CPutCandyData()
    {
        SetNull();
    }

    CPutCandyData(const uint256& assetIdIn, const CAmount& nAmountIn, const uint16_t& nExpiredIn, const std::string& strRemarksIn)
        : assetId(assetIdIn), nAmount(nAmountIn), nExpired(nExpiredIn), strRemarks(strRemarksIn) {
    }

    CPutCandyData& operator=(const CPutCandyData& data)
    {
        if(this == &data)
            return *this;

        assetId =  data.assetId;
        nAmount = data.nAmount;
        nExpired = data.nExpired;
        strRemarks = data.strRemarks;
        return *this;
    }

    void SetNull()
    {
        assetId.SetNull();
        nAmount = 0;
        nExpired = 0;
        strRemarks.clear();
    }
};

class CGetCandyData
{
public:
    uint256         assetId;
    CAmount         nAmount;
    std::string     strRemarks;

    CGetCandyData()
    {
        SetNull();
    }

    CGetCandyData(const uint256& assetIdIn, const CAmount& nAmountIn, const std::string& strRemarksIn)
        : assetId(assetIdIn), nAmount(nAmountIn), strRemarks(strRemarksIn) {
    }

    CGetCandyData& operator=(const CGetCandyData& data)
    {
        if(this == &data)
            return *this;

        assetId =  data.assetId;
        nAmount = data.nAmount;
        strRemarks = data.strRemarks;
        return *this;
    }

    void SetNull()
    {
        assetId.SetNull();
        nAmount = 0;
        strRemarks.clear();
    }
};

class CTransferSafeData
{
public:
    std::string     strRemarks;

    CTransferSafeData()
    {
        SetNull();
    }

    CTransferSafeData(const std::string& strRemarksIn)
        : strRemarks(strRemarksIn) {
    }

    CTransferSafeData& operator=(const CTransferSafeData& data)
    {
        if(this == &data)
            return *this;

        strRemarks = data.strRemarks;
        return *this;
    }

    void SetNull()
    {
        strRemarks.clear();
    }
};

std::string TrimString(const std::string& strValue);
std::string ToLower(const std::string& strValue);
bool IsKeyWord(const std::string& strValue);
bool IsContainSpace(const std::string& strValue);
bool IsValidUrl(const std::string& strUrl);

std::vector<unsigned char> FillRegisterData(const std::string& strAdminAddress, const CAppHeader& header, const CAppData& appData);
std::vector<unsigned char> FillAuthData(const std::string& strAdminAddress, const CAppHeader& header, const CAuthData& authData);
std::vector<unsigned char> FillExtendData(const CAppHeader& header, const CExtendData& extendData);
std::vector<unsigned char> FillIssueData(const CAppHeader& header, const CAssetData& assetData);
std::vector<unsigned char> FillCommonData(const CAppHeader& header, const CCommonData& commonData);
std::vector<unsigned char> FillPutCandyData(const CAppHeader& header, const CPutCandyData& candyData);
std::vector<unsigned char> FillGetCandyData(const CAppHeader& header, const CGetCandyData& candyData);
std::vector<unsigned char> FillTransferSafeData(const CAppHeader& header, const CTransferSafeData& safeData);

bool ParseReserve(const std::vector<unsigned char>& vReserve, CAppHeader& header, std::vector<unsigned char>& vData);
bool ParseRegisterData(const std::vector<unsigned char>& vAppData, CAppData& appData, std::string* pAdminAddress = NULL);
bool ParseAuthData(const std::vector<unsigned char>& vAuthData, CAuthData& authData, std::string* pAdminAddress = NULL);
bool ParseExtendData(const std::vector<unsigned char>& vExtendData, CExtendData& extendData);
bool ParseIssueData(const std::vector<unsigned char>& vAssetData, CAssetData& assetData);
bool ParseCommonData(const std::vector<unsigned char>& vCommonData, CCommonData& commonData);
bool ParsePutCandyData(const std::vector<unsigned char>& vCandyData, CPutCandyData& candyData);
bool ParseGetCandyData(const std::vector<unsigned char>& vCandyData, CGetCandyData& candyData);
bool ParseTransferSafeData(const std::vector<unsigned char>& vSafeData, CTransferSafeData& safeData);

bool ExistAppName(const std::string& strAppName, const bool fWithMempool = true);
bool ExistAppId(const uint256& appId, const bool fWithMempool = true);
bool ExistShortName(const std::string& strShortName, const bool fWithMempool = true);
bool ExistAssetName(const std::string& strAssetName, const bool fWithMempool = true);
bool ExistAssetId(const uint256& assetId, const bool fWithMempool = true);

#endif // APP_H
