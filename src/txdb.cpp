// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"

#include "chain.h"
#include "chainparams.h"
#include "hash.h"
#include "validation.h"
#include "pow.h"
#include "uint256.h"
#include "main.h"
#include "app/app.h"

#include <stdint.h>

#include <boost/thread.hpp>

using namespace std;

static const char DB_COINS = 'c';
static const char DB_BLOCK_FILES = 'f';
static const char DB_TXINDEX = 't';
static const char DB_ADDRESSINDEX = 'a';
static const char DB_ADDRESSUNSPENTINDEX = 'u';
static const char DB_TIMESTAMPINDEX = 's';
static const char DB_SPENTINDEX = 'p';
static const char DB_BLOCK_INDEX = 'b';

static const char DB_BEST_BLOCK = 'B';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';

static const string DB_APPID_APPINFO_INDEX = "appid_appinfo";
static const string DB_APPNAME_APPID_INDEX = "appname_appid";
static const string DB_APPTX_INDEX = "apptx";
static const string DB_AUTH_INDEX = "auth";
static const string DB_ASSETID_ASSETINFO_INDEX = "assetid_assetinfo";
static const string DB_SHORTNAME_ASSETID_INDEX = "shortname_assetid";
static const string DB_ASSETNAME_ASSETID_INDEX = "assetname_assetid";
static const string DB_ASSETTX_INDEX = "assettx";
static const string DB_PUTCANDY_INDEX = "putcandy";
static const string DB_GETCANDY_INDEX = "getcandy";
static const string DB_CANDYHEIGHT_TOTALAMOUNT_INDEX = "candyheight_totalamount";
static const string DB_CANDYHEIGHT_INDEX = "candyheight";
static const string DB_GETCANDYCOUNT_INDEX = "getcandycount";
static const string DB_VIRTUALACCOUNTID_ACCOUNTINFO_INDEX = "virtualaccountid_accountinfo";
static const string DB_VIRTUALACCOUNTNAME_ACCOUNTID_INDEX = "virtualaccountname_accountid";
static const string DB_SAFEADRESS_ACCOUNTID_INDEX = "safeadress_accountid";

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe, true)
{
}

bool CCoinsViewDB::GetCoins(const uint256 &txid, CCoins &coins) const {
    return db.Read(make_pair(DB_COINS, txid), coins);
}

bool CCoinsViewDB::HaveCoins(const uint256 &txid) const {
    return db.Exists(make_pair(DB_COINS, txid));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) {
    CDBBatch batch(&db.GetObfuscateKey());
    size_t count = 0;
    size_t changed = 0;
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            if (it->second.coins.IsPruned())
                batch.Erase(make_pair(DB_COINS, it->first));
            else
                batch.Write(make_pair(DB_COINS, it->first), it->second.coins);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }
    if (!hashBlock.IsNull())
        batch.Write(DB_BEST_BLOCK, hashBlock);

    LogPrint("coindb", "Committing %u changed transactions (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return db.WriteBatch(batch);
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe) {
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

bool CCoinsViewDB::GetStats(CCoinsStats &stats) const {
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&db)->NewIterator());
    pcursor->Seek(DB_COINS);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = GetBestBlock();
    ss << stats.hashBlock;
    CAmount nTotalAmount = 0;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        CCoins coins;
        if (pcursor->GetKey(key) && key.first == DB_COINS) {
            if (pcursor->GetValue(coins)) {
                stats.nTransactions++;
                for (unsigned int i=0; i<coins.vout.size(); i++) {
                    const CTxOut &out = coins.vout[i];
                    if (!out.IsNull()) {
                        stats.nTransactionOutputs++;
                        ss << VARINT(i+1);
                        ss << out;
                        nTotalAmount += out.nValue;
                    }
                }
                stats.nSerializedSize += 32 + pcursor->GetValueSize();
                ss << VARINT(0);
            } else {
                return error("CCoinsViewDB::GetStats() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    stats.hashSerialized = ss.GetHash();
    stats.nTotalAmount = nTotalAmount;
    return true;
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) {
    return Read(make_pair(DB_TXINDEX, txid), pos);
}

bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >&vect) {
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<uint256,CDiskTxPos> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value) {
    return Read(make_pair(DB_SPENTINDEX, key), value);
}

bool CBlockTreeDB::UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect) {
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<CSpentIndexKey,CSpentIndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(make_pair(DB_SPENTINDEX, it->first));
        } else {
            batch.Write(make_pair(DB_SPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect) {
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(make_pair(DB_ADDRESSUNSPENTINDEX, it->first));
        } else {
            batch.Write(make_pair(DB_ADDRESSUNSPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressUnspentIndex(uint160 addressHash, int type,
                                           std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ADDRESSUNSPENTINDEX, CAddressIndexIteratorKey(type, addressHash)));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CAddressUnspentKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ADDRESSUNSPENTINDEX && key.second.hashBytes == addressHash) {
            CAddressUnspentValue nValue;
            if (pcursor->GetValue(nValue)) {
                unspentOutputs.push_back(make_pair(key.second, nValue));
                pcursor->Next();
            } else {
                return error("failed to get address unspent value");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount > >&vect) {
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_ADDRESSINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount > >&vect) {
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_ADDRESSINDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressIndex(uint160 addressHash, int type,
                                    std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                                    int start, int end) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    if (start > 0 && end > 0) {
        pcursor->Seek(make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorHeightKey(type, addressHash, start)));
    } else {
        pcursor->Seek(make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorKey(type, addressHash)));
    }

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char,CAddressIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ADDRESSINDEX && key.second.hashBytes == addressHash) {
            if (end > 0 && key.second.blockHeight > end) {
                break;
            }
            CAmount nValue;
            if (pcursor->GetValue(nValue)) {
                addressIndex.push_back(make_pair(key.second, nValue));
                pcursor->Next();
            } else {
                return error("failed to get address index value");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteTimestampIndex(const CTimestampIndexKey &timestampIndex) {
    CDBBatch batch(&GetObfuscateKey());
    batch.Write(make_pair(DB_TIMESTAMPINDEX, timestampIndex), 0);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampIndex(const unsigned int &high, const unsigned int &low, std::vector<uint256> &hashes) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_TIMESTAMPINDEX, CTimestampIndexIteratorKey(low)));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, CTimestampIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_TIMESTAMPINDEX && key.second.timestamp <= high) {
            hashes.push_back(key.second.blockHash);
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts()
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_BLOCK_INDEX, uint256()));

    // Load mapBlockIndex
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex)) {
                // Construct block index object
                CBlockIndex* pindexNew = InsertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nTx            = diskindex.nTx;

                if(IsCriticalHeight(pindexNew->nHeight)) // critical block index is loading
                {
                    CBlock temp = CreateCriticalBlock(pindexNew->pprev, pindexNew->nVersion, pindexNew->nTime, pindexNew->nBits); // previous block header is null
                    if(pindexNew->GetBlockHash() == temp.GetHash())
                    {
                        pcursor->Next();
                        continue;
                    }
                }

                if (!CheckProofOfWork(pindexNew->GetBlockHash(), pindexNew->nBits, Params().GetConsensus()))
                    return error("%s: CheckProofOfWork failed: %s", __func__, pindexNew->ToString());

                pcursor->Next();
            } else {
                return error("%s: failed to read value", __func__);
            }
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::Write_AppId_AppInfo_Index(const std::vector<std::pair<uint256, CAppId_AppInfo_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<uint256, CAppId_AppInfo_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_APPID_APPINFO_INDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_AppId_AppInfo_Index(const std::vector<std::pair<uint256, CAppId_AppInfo_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<uint256, CAppId_AppInfo_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_APPID_APPINFO_INDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_AppId_AppInfo_Index(const uint256& appId, CAppId_AppInfo_IndexValue& appInfo)
{
    return Read(make_pair(DB_APPID_APPINFO_INDEX, appId), appInfo) && g_nChainHeight >= appInfo.nHeight;
}

bool CBlockTreeDB::Read_AppList_Index(std::vector<uint256>& vAppId)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    int nCurHeight = g_nChainHeight;
    pcursor->Seek(make_pair(DB_APPID_APPINFO_INDEX, uint256()));
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_APPID_APPINFO_INDEX)
        {
            CAppId_AppInfo_IndexValue value;
            if(pcursor->GetValue(value))
            {
                if(nCurHeight >= value.nHeight)
                    vAppId.push_back(key.second);
                pcursor->Next();
            }
            else
            {
                return error("failed to get appid_appinfo index value");
            }
        }
        else
        {
            break;
        }
    }

    return vAppId.size();
}

bool CBlockTreeDB::Write_AppName_AppId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_APPNAME_APPID_INDEX, ToLower(it->first)), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_AppName_AppId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_APPNAME_APPID_INDEX, ToLower(it->first)));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_AppName_AppId_Index(const std::string& strAppName, CName_Id_IndexValue& value)
{
    return Read(make_pair(DB_APPNAME_APPID_INDEX, ToLower(strAppName)), value) && g_nChainHeight >= value.nHeight;
}

bool CBlockTreeDB::Write_AppTx_Index(const std::vector<std::pair<CAppTx_IndexKey, int> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for(std::vector<std::pair<CAppTx_IndexKey, int> >::const_iterator it = vect.begin(); it != vect.end(); it++)
        batch.Write(make_pair(DB_APPTX_INDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_AppTx_Index(const std::vector<std::pair<CAppTx_IndexKey, int> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for(std::vector<std::pair<CAppTx_IndexKey, int> >::const_iterator it = vect.begin(); it != vect.end(); it++)
        batch.Erase(make_pair(DB_APPTX_INDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_AppTx_Index(const uint256& appId, std::vector<COutPoint>& vOut)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_APPTX_INDEX, CIterator_IdKey(appId)));

    int nCurHeight = g_nChainHeight;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CAppTx_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_APPTX_INDEX && key.second.appId == appId)
        {
            int nHeight;
            if(pcursor->GetValue(nHeight))
            {
                if(nCurHeight >= nHeight)
                    vOut.push_back(key.second.out);
                pcursor->Next();
            }
            else
            {
                return error("failed to get apptx index value");
            }
        }
        else
        {
            break;
        }
    }

    return vOut.size();
}

bool CBlockTreeDB::Read_AppTx_Index(const uint256& appId, const std::string& strAddress, std::vector<COutPoint>& vOut)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_APPTX_INDEX, CIterator_IdAddressKey(appId, strAddress)));

    int nCurHeight = g_nChainHeight;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CAppTx_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_APPTX_INDEX && key.second.appId == appId && key.second.strAddress == strAddress)
        {
            int nHeight;
            if(pcursor->GetValue(nHeight))
            {
                if(nCurHeight >= nHeight)
                    vOut.push_back(key.second.out);
                pcursor->Next();
            }
            else
            {
                return error("failed to get apptx index value");
            }
        }
        else
        {
            break;
        }
    }

    return vOut.size();
}

bool CBlockTreeDB::Read_AppList_Index(const std::string& strAddress, std::vector<uint256>& vAppId)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_APPTX_INDEX, CIterator_IdAddressKey()));

    int nCurHeight = g_nChainHeight;
    std::map<uint256, char> mapAppId;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CAppTx_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_APPTX_INDEX)
        {
            int nHeight;
            if(pcursor->GetValue(nHeight))
            {
                if(nCurHeight >= nHeight && key.second.strAddress == strAddress)
                    mapAppId[key.second.appId] = 1;
                pcursor->Next();
            }
            else
            {
                return error("failed to get apptx index value");
            }
        }
        else
        {
            break;
        }
    }

    for(std::map<uint256, char>::const_iterator it = mapAppId.begin(); it != mapAppId.end(); it++)
        vAppId.push_back(it->first);

    return vAppId.size();
}

bool CBlockTreeDB::Update_Auth_Index(const std::vector<std::pair<CAuth_IndexKey, int> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for(std::vector<std::pair<CAuth_IndexKey, int> >::const_iterator it = vect.begin(); it != vect.end(); it++)
    {
        if(it->second <= 0)
            batch.Erase(make_pair(DB_AUTH_INDEX, it->first));
        else
            batch.Write(make_pair(DB_AUTH_INDEX, it->first), it->second);
    }
    return WriteBatch(batch);
}
bool CBlockTreeDB::Read_Auth_Index(const uint256& appId, const std::string& strAddress, std::map<uint32_t, int>& mapAuth)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_AUTH_INDEX, CIterator_IdAddressKey(appId, strAddress)));

    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CAuth_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_AUTH_INDEX && key.second.appId == appId && key.second.strAddress == strAddress)
        {
            int nHeight;
            if(pcursor->GetValue(nHeight))
            {
                mapAuth.insert(make_pair(key.second.nAuth, nHeight));
                pcursor->Next();
            }
            else
            {
                return error("failed to get auth index value");
            }
        }
        else
        {
            break;
        }
    }

    return mapAuth.size();
}

bool CBlockTreeDB::Write_AssetId_AssetInfo_Index(const std::vector<std::pair<uint256, CAssetId_AssetInfo_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<uint256, CAssetId_AssetInfo_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_ASSETID_ASSETINFO_INDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_AssetId_AssetInfo_Index(const std::vector<std::pair<uint256, CAssetId_AssetInfo_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<uint256, CAssetId_AssetInfo_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_ASSETID_ASSETINFO_INDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_AssetId_AssetInfo_Index(const uint256& assetId, CAssetId_AssetInfo_IndexValue& assetInfo)
{
    return Read(make_pair(DB_ASSETID_ASSETINFO_INDEX, assetId), assetInfo) && g_nChainHeight >= assetInfo.nHeight;
}

bool CBlockTreeDB::Read_AssetList_Index(std::vector<uint256>& vAssetId)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ASSETID_ASSETINFO_INDEX, uint256()));

    int nCurHeight = g_nChainHeight;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_ASSETID_ASSETINFO_INDEX)
        {
            CAssetId_AssetInfo_IndexValue value;
            if(pcursor->GetValue(value))
            {
                if(nCurHeight >= value.nHeight)
                    vAssetId.push_back(key.second);
                pcursor->Next();
            }
            else
            {
                return error("failed to get assetid_assetinfo index value");
            }
        }
        else
        {
            break;
        }
    }

    return vAssetId.size();
}

bool CBlockTreeDB::Write_ShortName_AssetId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_SHORTNAME_ASSETID_INDEX, ToLower(it->first)), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_ShortName_AssetId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_SHORTNAME_ASSETID_INDEX, ToLower(it->first)));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_ShortName_AssetId_Index(const std::string& strShortName, CName_Id_IndexValue& value)
{
    return Read(make_pair(DB_SHORTNAME_ASSETID_INDEX, ToLower(strShortName)), value) && g_nChainHeight >= value.nHeight;
}

bool CBlockTreeDB::Write_AssetName_AssetId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_ASSETNAME_ASSETID_INDEX, ToLower(it->first)), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_AssetName_AssetId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_ASSETNAME_ASSETID_INDEX, ToLower(it->first)));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_AssetName_AssetId_Index(const std::string& strAssetName, CName_Id_IndexValue& value)
{
    return Read(make_pair(DB_ASSETNAME_ASSETID_INDEX, ToLower(strAssetName)), value) && g_nChainHeight >= value.nHeight;
}

bool CBlockTreeDB::Write_AssetTx_Index(const std::vector<std::pair<CAssetTx_IndexKey, int> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for(std::vector<std::pair<CAssetTx_IndexKey, int> >::const_iterator it = vect.begin(); it != vect.end(); it++)
        batch.Write(make_pair(DB_ASSETTX_INDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_AssetTx_Index(const std::vector<std::pair<CAssetTx_IndexKey, int> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for(std::vector<std::pair<CAssetTx_IndexKey, int> >::const_iterator it = vect.begin(); it != vect.end(); it++)
        batch.Erase(make_pair(DB_ASSETTX_INDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_AssetTx_Index(const uint256& assetId, const uint8_t& nTxClass, std::vector<COutPoint>& vOut)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ASSETTX_INDEX, CIterator_IdKey(assetId)));

    int nCurHeight = g_nChainHeight;
    multimap<int, COutPoint> tmpMap;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CAssetTx_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ASSETTX_INDEX && key.second.assetId == assetId)
        {
            int nHeight;
            if(pcursor->GetValue(nHeight))
            {
                if(nCurHeight >= nHeight)
                {
                    if(nTxClass == ALL_TXOUT)
                    {
                        tmpMap.insert(make_pair(nHeight, key.second.out));
                    }
                    else if(nTxClass == UNLOCKED_TXOUT)
                    {
                        if(key.second.nTxClass != LOCKED_TXOUT)
                            tmpMap.insert(make_pair(nHeight, key.second.out));
                    }
                    else if(key.second.nTxClass == nTxClass)
                    {
                         tmpMap.insert(make_pair(nHeight, key.second.out));
                    }
                }
                pcursor->Next();
            }
            else
            {
                return error("failed to get assettx index value");
            }
        }
        else
        {
            break;
        }
    }

    for(multimap<int, COutPoint>::iterator iter = tmpMap.begin(); iter != tmpMap.end(); ++iter)
        vOut.push_back(iter->second);

    multimap<int, COutPoint>().swap(tmpMap);

    return vOut.size();
}

bool CBlockTreeDB::Read_AssetTx_Index(const uint256& assetId, const std::string& strAddress, const uint8_t& nTxClass, std::vector<COutPoint>& vOut)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ASSETTX_INDEX, CIterator_IdAddressKey(assetId, strAddress)));

    int nCurHeight = g_nChainHeight;
    multimap<int, COutPoint> tmpMap;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CAssetTx_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ASSETTX_INDEX && key.second.assetId == assetId && key.second.strAddress == strAddress)
        {
            int nHeight;
            if(pcursor->GetValue(nHeight))
            {
                if(nCurHeight >= nHeight)
                {
                    if(nTxClass == ALL_TXOUT)
                    {
                        tmpMap.insert(make_pair(nHeight, key.second.out));
                    }
                    else if(nTxClass == UNLOCKED_TXOUT)
                    {
                        if(key.second.nTxClass != LOCKED_TXOUT)
                            tmpMap.insert(make_pair(nHeight, key.second.out));
                    }
                    else if(key.second.nTxClass == nTxClass)
                    {
                         tmpMap.insert(make_pair(nHeight, key.second.out));
                    }
                }
                pcursor->Next();
            }
            else
            {
                return error("failed to get assettx index value");
            }
        }
        else
        {
            break;
        }
    }

    for(multimap<int, COutPoint>::iterator iter = tmpMap.begin();iter != tmpMap.end();++iter)
        vOut.push_back(iter->second);

    multimap<int, COutPoint>().swap(tmpMap);

    return vOut.size();
}

bool CBlockTreeDB::Read_AssetList_Index(const std::string& strAddress, std::vector<uint256>& vAssetId)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ASSETTX_INDEX, CIterator_IdAddressKey()));

    int nCurHeight = g_nChainHeight;
    std::map<uint256, char> mapAssetId;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CAssetTx_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ASSETTX_INDEX)
        {
            int nHeight;
            if(pcursor->GetValue(nHeight))
            {
                if(nCurHeight >= nHeight && key.second.strAddress == strAddress)
                    mapAssetId[key.second.assetId] = 1;
                pcursor->Next();
            }
            else
            {
                return error("failed to get assettx index value");
            }
        }
        else
        {
            break;
        }
    }

    for(std::map<uint256, char>::iterator it = mapAssetId.begin(); it != mapAssetId.end(); it++)
        vAssetId.push_back(it->first);

    return vAssetId.size();
}

bool CBlockTreeDB::Write_PutCandy_Index(const std::vector<std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for(std::vector<std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue> >::const_iterator it = vect.begin(); it != vect.end(); it++)
        batch.Write(make_pair(DB_PUTCANDY_INDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_PutCandy_Index(const std::vector<std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for(std::vector<std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue> >::const_iterator it = vect.begin(); it != vect.end(); it++)
        batch.Erase(make_pair(DB_PUTCANDY_INDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_PutCandy_Index(const uint256& assetId, std::map<COutPoint, CCandyInfo>& mapCandyInfo)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_PUTCANDY_INDEX, CIterator_IdKey(assetId)));

    int nCurHeight = g_nChainHeight;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CPutCandy_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_PUTCANDY_INDEX && key.second.assetId == assetId)
        {
            CPutCandy_IndexValue value;
            if(pcursor->GetValue(value))
            {
                if(nCurHeight >= value.nHeight)
                    mapCandyInfo[key.second.out] = key.second.candyInfo;
                pcursor->Next();
            }
            else
            {
                return error("failed to get putcandy index value");
            }
        }
        else
        {
            break;
        }
    }

    return mapCandyInfo.size();
}

bool CBlockTreeDB::Read_PutCandy_Index(const uint256& assetId, const COutPoint& out, CCandyInfo& candyInfo)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_PUTCANDY_INDEX, CIterator_IdOutKey(assetId, out)));

    int nCurHeight = g_nChainHeight;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CPutCandy_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_PUTCANDY_INDEX && key.second.assetId == assetId && key.second.out == out)
        {
            CPutCandy_IndexValue value;
            if(pcursor->GetValue(value))
            {
                if(nCurHeight >= value.nHeight)
                {
                    candyInfo = key.second.candyInfo;
                    return true;
                }
                pcursor->Next();
            }
            else
            {
                return error("failed to get putcandy index value");
            }
        }
        else
        {
            break;
        }
    }

    return false;
}

bool CBlockTreeDB::Read_PutCandy_Index(std::map<CPutCandy_IndexKey, CPutCandy_IndexValue>& mapCandy)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_PUTCANDY_INDEX, CIterator_IdKey()));

    int nCurHeight = g_nChainHeight;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CPutCandy_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_PUTCANDY_INDEX)
        {
            CPutCandy_IndexValue value;
            if(pcursor->GetValue(value))
            {
                if(nCurHeight >= value.nHeight)
                    mapCandy.insert(make_pair(key.second, value));
                pcursor->Next();
            }
            else
            {
                return error("failed to get putcandy index value");
            }
        }
        else
        {
            break;
        }
    }

    return mapCandy.size();
}

bool CBlockTreeDB::Write_GetCandy_Index(const std::vector<std::pair<CGetCandy_IndexKey, CGetCandy_IndexValue> >& vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for(std::vector<std::pair<CGetCandy_IndexKey, CGetCandy_IndexValue> >::const_iterator it = vect.begin(); it != vect.end(); it++)
        batch.Write(make_pair(DB_GETCANDY_INDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_GetCandy_Index(const std::vector<std::pair<CGetCandy_IndexKey, CGetCandy_IndexValue> >& vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for(std::vector<std::pair<CGetCandy_IndexKey, CGetCandy_IndexValue> >::const_iterator it = vect.begin(); it != vect.end(); it++)
        batch.Erase(make_pair(DB_GETCANDY_INDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_GetCandy_Index(const uint256& assetId, const COutPoint& out, const std::string& strAddress, CAmount& nAmount)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_GETCANDY_INDEX, CGetCandy_IndexKey(assetId, out, strAddress)));

    int nCurHeight = g_nChainHeight;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CGetCandy_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_GETCANDY_INDEX && key.second.assetId == assetId && key.second.out == out && key.second.strAddress == strAddress)
        {
            CGetCandy_IndexValue value;
            if(pcursor->GetValue(value))
            {
                if(nCurHeight >= value.nHeight)
                {
                    nAmount = value.nAmount;
                    return true;
                }
                pcursor->Next();
            }
            else
            {
                return error("failed to get getcandy index value");
            }
        }
        else
        {
            break;
        }
    }

    return false;
}

bool CBlockTreeDB::Read_GetCandy_Index(const uint256& assetId, std::map<COutPoint, std::vector<std::string> > &mapOutAddress)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_GETCANDY_INDEX, CIterator_IdKey(assetId)));

    int nCurHeight = g_nChainHeight;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CGetCandy_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_GETCANDY_INDEX && key.second.assetId == assetId)
        {
            CGetCandy_IndexValue value;
            if(pcursor->GetValue(value))
            {
                if(nCurHeight >= value.nHeight)
                {
                    if (mapOutAddress.end() == mapOutAddress.find(key.second.out))
                    {
                        std::vector<std::string> vAddress;
                        vAddress.push_back(key.second.strAddress);
                        mapOutAddress[key.second.out] = vAddress;
                    }
                    else
                    {
                        if (mapOutAddress[key.second.out].end() == find(mapOutAddress[key.second.out].begin(), mapOutAddress[key.second.out].end(), key.second.strAddress))
                            mapOutAddress[key.second.out].push_back(key.second.strAddress);
                    }
                }
                pcursor->Next();
            }
            else
            {
                return error("failed to get getcandy index value");
            }
        }
        else
        {
            break;
        }
    }

    return mapOutAddress.size();
}

bool CBlockTreeDB::Read_GetCandy_Index(const uint256& assetId, const std::string& straddress, std::vector<COutPoint>& vOut)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_GETCANDY_INDEX, CIterator_IdKey(assetId)));

    int nCurHeight = g_nChainHeight;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CGetCandy_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_GETCANDY_INDEX && key.second.assetId == assetId)
        {
            CGetCandy_IndexValue value;
            if(pcursor->GetValue(value))
            {
                if(nCurHeight >= value.nHeight && key.second.strAddress == straddress)
                    vOut.push_back(key.second.out);
                pcursor->Next();
            }
            else
            {
                return error("failed to get getcandy index value");
            }
        }
        else
        {
            break;
        }
    }

    return vOut.size();
}

bool CBlockTreeDB::Write_CandyHeight_TotalAmount_Index(const int& nHeight, const CAmount& nAmount)
{
    CDBBatch batch(&GetObfuscateKey());
    batch.Write(make_pair(DB_CANDYHEIGHT_TOTALAMOUNT_INDEX, nHeight), nAmount);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_CandyHeight_TotalAmount_Index(const int& nHeight, CAmount& nAmount)
{
    return Read(make_pair(DB_CANDYHEIGHT_TOTALAMOUNT_INDEX, nHeight), nAmount);
}

bool CBlockTreeDB::Read_CandyHeight_TotalAmount_Index(std::vector<int>& vHeight)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_CANDYHEIGHT_TOTALAMOUNT_INDEX, CHeight_IndexKey()));
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, int> key;
        if (pcursor->GetKey(key) && key.first == DB_CANDYHEIGHT_TOTALAMOUNT_INDEX)
        {
            vHeight.push_back(key.second);
            pcursor->Next();
        }
        else
        {
            break;
        }
    }

    return vHeight.size();
}

bool CBlockTreeDB::Write_CandyHeight_Index(const int& nHeight)
{
    CDBBatch batch(&GetObfuscateKey());
    batch.Write(make_pair(DB_CANDYHEIGHT_INDEX, nHeight), 0);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_CandyHeight_Index(std::vector<int> &vHeight)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_CANDYHEIGHT_INDEX, CHeight_IndexKey()));
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, int> key;
        if (pcursor->GetKey(key) && key.first == DB_CANDYHEIGHT_INDEX)
        {
            vHeight.push_back(key.second);
            pcursor->Next();
        }
        else
            break;
    }

    return vHeight.size();
}

bool CBlockTreeDB::Write_GetCandyCount_Index(const CGetCandyCount_IndexKey& key,const CGetCandyCount_IndexValue& value)
{
    CDBBatch batch(&GetObfuscateKey());
    batch.Write(make_pair(DB_GETCANDYCOUNT_INDEX, key), value);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_GetCandyCount_Index(const CGetCandyCount_IndexKey &key)
{
    CDBBatch batch(&GetObfuscateKey());
    batch.Erase(make_pair(DB_GETCANDYCOUNT_INDEX, key));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Is_Exists_GetCandyCount_Key(const uint256& assetId, const COutPoint& out)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(make_pair(DB_GETCANDYCOUNT_INDEX, CGetCandyCount_IndexKey(assetId, out)));

    bool ret = false;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CGetCandyCount_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_GETCANDYCOUNT_INDEX && key.second.assetId == assetId && key.second.out == out)
        {
            ret = true;
            break;
        }
        else
        {
            break;
        }
    }

    return ret;
}

bool CBlockTreeDB::Read_GetCandyCount_Index(const uint256& assetId, const COutPoint& out,CGetCandyCount_IndexValue& getCandyCountvalue)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(make_pair(DB_GETCANDYCOUNT_INDEX, CGetCandyCount_IndexKey(assetId, out)));

    bool ret = false;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, CGetCandyCount_IndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_GETCANDYCOUNT_INDEX && key.second.assetId == assetId && key.second.out == out)
        {
            if(pcursor->GetValue(getCandyCountvalue))
            {
                ret = true;
                break;
            }
            else
            {
                return error("failed to get getcandy index value");
            }
        }
        else
        {
            break;
        }
    }

    return ret;
}

bool CBlockTreeDB::Read_VirtualAccountName_AccountId_Index(const std::string& strVirtualAccountName, CName_Id_IndexValue& value)
{
    return Read(make_pair(DB_VIRTUALACCOUNTNAME_ACCOUNTID_INDEX, ToLower(strVirtualAccountName)), value) && g_nChainHeight >= value.nHeight;
}

bool CBlockTreeDB::Read_VirtualAccountId_Accountinfo_Index(const uint256& virtualAccountId, CVirtualAccountId_Accountinfo_IndexValue& virtualAccountInfo)
{
    return Read(make_pair(DB_VIRTUALACCOUNTID_ACCOUNTINFO_INDEX, virtualAccountId),virtualAccountInfo) && g_nChainHeight >= virtualAccountInfo.nHeight;
}

bool CBlockTreeDB::Read_SafeAdress_AccountId_Index(const std::string& safeAddress, CName_Id_IndexValue& value)
{
    return Read(make_pair(DB_SAFEADRESS_ACCOUNTID_INDEX, safeAddress),value) && g_nChainHeight >= value.nHeight;
}

bool CBlockTreeDB::Write_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_VIRTUALACCOUNTNAME_ACCOUNTID_INDEX, ToLower(it->first)), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_VIRTUALACCOUNTNAME_ACCOUNTID_INDEX, ToLower(it->first)));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Write_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_VIRTUALACCOUNTID_ACCOUNTINFO_INDEX, it->first), it->second);
    return WriteBatch(batch);
}


bool CBlockTreeDB::Erase_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_VIRTUALACCOUNTID_ACCOUNTINFO_INDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Write_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_SAFEADRESS_ACCOUNTID_INDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::Erase_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> > &vect)
{
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_SAFEADRESS_ACCOUNTID_INDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::Read_VirtualAccountList_Index(std::map<std::string, uint256>& mVirtualAccountId)
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
    std::string strVirtualAccountName = "";

    pcursor->Seek(make_pair(DB_VIRTUALACCOUNTNAME_ACCOUNTID_INDEX, CIterator_VirtualAccountNameKey(strVirtualAccountName)));

    int nCurHeight = g_nChainHeight;
    std::map<uint256, char> mapAppId;
    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::string, std::string> key;
        if (pcursor->GetKey(key) && key.first == DB_VIRTUALACCOUNTNAME_ACCOUNTID_INDEX)
        {
            uint256 nVirutalAccountId;
            if(pcursor->GetValue(nVirutalAccountId))
            {
                mVirtualAccountId.insert(make_pair(key.second, nVirutalAccountId));
                pcursor->Next();
            }
            else
            {
                return error("failed to get virtual account list.");
            }
        }
        else
        {
            break;
        }
    }

    return mVirtualAccountId.size();
}
