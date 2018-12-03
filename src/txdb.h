// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include "coins.h"
#include "dbwrapper.h"
#include "validation.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CBlockFileInfo;
class CBlockIndex;
struct CDiskTxPos;
struct CAddressUnspentKey;
struct CAddressUnspentValue;
struct CAddressIndexKey;
struct CAddressIndexIteratorKey;
struct CAddressIndexIteratorHeightKey;
struct CTimestampIndexKey;
struct CTimestampIndexIteratorKey;
struct CSpentIndexKey;
struct CSpentIndexValue;
class uint256;

struct CName_Id_IndexValue;
struct CAppId_AppInfo_IndexValue;
struct CAuth_IndexKey;
struct CAppTx_IndexKey;
struct CAssetId_AssetInfo_IndexValue;
struct CAssetTx_IndexKey;
struct CPutCandy_IndexKey;
struct CPutCandy_IndexValue;
struct CGetCandy_IndexKey;
struct CGetCandy_IndexValue;

//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 100;
//! max. -dbcache in (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache in (MiB)
static const int64_t nMinDbCache = 4;

/** CCoinsView backed by the coin database (chainstate/) */
class CCoinsViewDB : public CCoinsView
{
protected:
    CDBWrapper db;
public:
    CCoinsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool GetCoins(const uint256 &txid, CCoins &coins) const;
    bool HaveCoins(const uint256 &txid) const;
    uint256 GetBestBlock() const;
    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock);
    bool GetStats(CCoinsStats &stats) const;
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CDBWrapper
{
public:
    CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
private:
    CBlockTreeDB(const CBlockTreeDB&);
    void operator=(const CBlockTreeDB&);
public:
    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list);
    bool ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value);
    bool UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect);
    bool UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect);
    bool ReadAddressUnspentIndex(uint160 addressHash, int type,
                                 std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &vect);
    bool WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    bool EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    bool ReadAddressIndex(uint160 addressHash, int type,
                          std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                          int start = 0, int end = 0);
    bool WriteTimestampIndex(const CTimestampIndexKey &timestampIndex);
    bool ReadTimestampIndex(const unsigned int &high, const unsigned int &low, std::vector<uint256> &vect);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    bool LoadBlockIndexGuts();

    bool Write_AppId_AppInfo_Index(const std::vector<std::pair<uint256, CAppId_AppInfo_IndexValue> > &vect);
    bool Erase_AppId_AppInfo_Index(const std::vector<std::pair<uint256, CAppId_AppInfo_IndexValue> > &vect);
    bool Read_AppId_AppInfo_Index(const uint256& appId, CAppId_AppInfo_IndexValue& appInfo);
    bool Read_AppList_Index(std::vector<uint256>& vAppId);

    bool Write_AppName_AppId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect);
    bool Erase_AppName_AppId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect);
    bool Read_AppName_AppId_Index(const std::string& strAppName, CName_Id_IndexValue& value);

    bool Write_AppTx_Index(const std::vector<std::pair<CAppTx_IndexKey, int> > &vect);
    bool Erase_AppTx_Index(const std::vector<std::pair<CAppTx_IndexKey, int> > &vect);
    bool Read_AppTx_Index(const uint256& appId, std::vector<COutPoint>& vOut);
    bool Read_AppTx_Index(const uint256& appId, const std::string& strAddress, std::vector<COutPoint>& vOut);
    bool Read_AppList_Index(const std::string& strAddress, std::vector<uint256>& vAppId);

    bool Update_Auth_Index(const std::vector<std::pair<CAuth_IndexKey, int> > &vect);
    bool Read_Auth_Index(const uint256& appId, const std::string& strAddress, std::map<uint32_t, int>& mapAuth);

    bool Write_AssetId_AssetInfo_Index(const std::vector<std::pair<uint256, CAssetId_AssetInfo_IndexValue> > &vect);
    bool Erase_AssetId_AssetInfo_Index(const std::vector<std::pair<uint256, CAssetId_AssetInfo_IndexValue> > &vect);
    bool Read_AssetId_AssetInfo_Index(const uint256& assetId, CAssetId_AssetInfo_IndexValue& assetInfo);
    bool Read_AssetList_Index(std::vector<uint256>& vAssetId);

    bool Write_ShortName_AssetId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect);
    bool Erase_ShortName_AssetId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect);
    bool Read_ShortName_AssetId_Index(const std::string& strShortName, CName_Id_IndexValue& value);

    bool Write_AssetName_AssetId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect);
    bool Erase_AssetName_AssetId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect);
    bool Read_AssetName_AssetId_Index(const std::string& strAssetName, CName_Id_IndexValue& value);

    bool Write_AssetTx_Index(const std::vector<std::pair<CAssetTx_IndexKey, int> > &vect);
    bool Erase_AssetTx_Index(const std::vector<std::pair<CAssetTx_IndexKey, int> > &vect);
    bool Read_AssetTx_Index(const uint256& assetId, const uint8_t& nTxClass, std::vector<COutPoint>& vOut);
    bool Read_AssetTx_Index(const uint256& assetId, const std::string& strAddress, const uint8_t& nTxClass, std::vector<COutPoint>& vOut);
    bool Read_AssetList_Index(const std::string& strAddress, std::vector<uint256>& vAssetId);

    bool Write_PutCandy_Index(const std::vector<std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue> > &vect);
    bool Erase_PutCandy_Index(const std::vector<std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue> > &vect);
    bool Read_PutCandy_Index(const uint256& assetId, std::map<COutPoint, CCandyInfo>& mapCandyInfo);
    bool Read_PutCandy_Index(const uint256& assetId, const COutPoint& out, CCandyInfo& candyInfo);
    bool Read_PutCandy_Index(std::map<CPutCandy_IndexKey, CPutCandy_IndexValue>& mapCandy);

    bool Write_GetCandy_Index(const std::vector<std::pair<CGetCandy_IndexKey, CGetCandy_IndexValue> >& vect);
    bool Erase_GetCandy_Index(const std::vector<std::pair<CGetCandy_IndexKey, CGetCandy_IndexValue> >& vect);
    bool Read_GetCandy_Index(const uint256& assetId, const COutPoint& out, const std::string& strAddress, CAmount& amount);
    bool Read_GetCandy_Index(const uint256& assetId, std::map<COutPoint, std::vector<std::string> > &mapOutAddress);
    bool Read_GetCandy_Index(const uint256& assetId, const std::string& straddress, std::vector<COutPoint>& vOut);

    bool Write_CandyHeight_TotalAmount_Index(const int& nHeight, const CAmount& nAmount);
    bool Read_CandyHeight_TotalAmount_Index(const int& nHeight, CAmount& nAmount);
    bool Read_CandyHeight_TotalAmount_Index(std::vector<int>& vHeight);

    bool Write_CandyHeight_Index(const int& nHeight);
    bool Read_CandyHeight_Index(std::vector<int>& vHeight);

    bool Write_GetCandyCount_Index(const CGetCandyCount_IndexKey& key,const CGetCandyCount_IndexValue& value);
    bool Erase_GetCandyCount_Index(const CGetCandyCount_IndexKey& key);
    bool Read_GetCandyCount_Index(const uint256& assetId, const COutPoint& out,CGetCandyCount_IndexValue& getCandyCountvalue);
    bool Is_Exists_GetCandyCount_Key(const uint256& assetId, const COutPoint& out);

    bool Read_VirtualAccountName_AccountId_Index(const std::string& strVirtualAccountName, CName_Id_IndexValue& value);
    bool Read_VirtualAccountId_Accountinfo_Index(const uint256& virtualAccountId, CVirtualAccountId_Accountinfo_IndexValue& appInfo);
    bool Read_SafeAdress_AccountId_Index(const std::string& safeAddress, CName_Id_IndexValue& value);
    bool Write_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Erase_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Write_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect);
    bool Erase_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect);
    bool Write_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Erase_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Read_VirtualAccountList_Index(std::map<std::string, uint256>& vVirtualAccountId);
};

#endif // BITCOIN_TXDB_H
