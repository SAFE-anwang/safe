#include "main.h"
#include "base58.h"
#include "validation.h"
#include "consensus/merkle.h"
#include "init.h"
#include "masternode.h"

#include <string>

int g_nCriticalHeight = 175;
int g_nAnWwangDiffOffset = 100;
CAmount g_nCriticalReward = 21000000 * COIN;
static std::string g_strCriticalAddress = "XuVvTuxikYC1Cu9rtcvbZQmuXxKCfhdb5U";

std::string g_strCancelledMoneroCandyAddress = "XagqqFetxiDb9wbartKDrXgnqLah6SqX2S"; // monero's safe candy hold address (hash160: 0x0000...00)
std::string g_strCancelledSafeAddress = "XagqqFetxiDb9wbartKDrXgnqLah9fKoTx"; // safe's black hold address (hash160: 0x0000...01)
std::string g_strCancelledAssetAddress = "XagqqFetxiDb9wbartKDrXgnqLahHSe2VE"; // asset's black hold address (hash160: 0x0000...02)
std::string g_strPutCandyAddress = "XagqqFetxiDb9wbartKDrXgnqLahUovwfs"; // candy's black hold address (hash160: 0x0000...03)
int g_nStartSPOSHeight = 32205;//XJTODO
unsigned int g_nMasternodeSPosCount = 3;
unsigned int g_nMasternodeMinOnlineTime = 86400*3;
int64_t g_nStartNewLoopTime = 0;
int64_t g_nSposIndex = 0;
int64_t g_nLastSelectMasterNodeHeight = 0;
std::vector<CMasternode> g_vecResultMasternodes;


CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev)
{
    CBlock block;

    if(!pindexPrev)
        return block;

    const CChainParams& chainparams = Params();

    CMutableTransaction txNew;
    txNew.nVersion = SAFE_TX_VERSION_1;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = CScript() << g_nCriticalHeight << OP_0;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = GetScriptForDestination(CBitcoinAddress(g_strCriticalAddress).Get());
    txNew.vout[0].nValue = g_nCriticalReward;

    block.vtx.push_back(txNew);

    block.nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    block.hashPrevBlock = pindexPrev->GetBlockHash();
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nTime = pindexPrev->nTime + 30;
    block.nBits = 0x1f0ffff0;
    block.nNonce = 0;

    return block;
}

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev, const int nVersion, const unsigned int nTime, const unsigned int nBits)
{
    CBlock block;

    if(!pindexPrev)
        return block;

    CMutableTransaction txNew;
    txNew.nVersion = SAFE_TX_VERSION_1;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = CScript() << g_nCriticalHeight << OP_0;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = GetScriptForDestination(CBitcoinAddress(g_strCriticalAddress).Get());
    txNew.vout[0].nValue = g_nCriticalReward;

    block.vtx.push_back(txNew);

    block.nVersion = nVersion;
    block.hashPrevBlock = pindexPrev->GetBlockHash();
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nTime = nTime;
    block.nBits = nBits;
    block.nNonce = 0;

    return block;
}

int GetPrevBlockHeight(const uint256& hash)
{
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if(mi != mapBlockIndex.end())
        return mi->second->nHeight;

    return -1;
}

bool CheckCriticalBlock(const CBlockHeader& block)
{
    int nHeight = GetPrevBlockHeight(block.hashPrevBlock) + 1;
    //printf("block header: %s, height: %d\n", CBlock(block).ToString().data(), nHeight);
    if(IsCriticalHeight(nHeight))
    {
        CBlock temp = CreateCriticalBlock(mapBlockIndex[block.hashPrevBlock]);
        if(block.GetHash() == temp.GetHash())
            return true;
    }

    return false;
}

int GetTxHeight(const uint256& txHash, uint256* pBlockHash)
{
    CTransaction txTmp;
    uint256 hashBlock = uint256();
    if(GetTransaction(txHash, txTmp, Params().GetConsensus(), hashBlock, true) && hashBlock != uint256())
    {
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if(mi != mapBlockIndex.end() && (*mi).second)
        {
            if(pBlockHash)
                *pBlockHash = hashBlock;
            return (*mi).second->nHeight;
        }
    }
    return g_nChainHeight + 1;
}

bool IsLockedTxOut(const uint256& txHash, const CTxOut& txout)
{
    if(txout.nUnlockedHeight <= 0)
        return false;

    if(txout.nUnlockedHeight <= g_nChainHeight) // unlocked
        return false;

	int nTxheight = GetTxHeight(txHash);
    int64_t nOffset = txout.nUnlockedHeight - nTxheight;	
    if (nTxheight >= g_nStartSPOSHeight)
    {
        if(nOffset <= 28 * SPOS_BLOCKS_PER_DAY || nOffset > 120 * SPOS_BLOCKS_PER_MONTH) // invalid
            return false;
    }
    else
    {
        if(nOffset <= 28 * BLOCKS_PER_DAY || nOffset > 120 * BLOCKS_PER_MONTH) // invalid
            return false;
    }

    return true;

}

bool IsLockedTxOutByHeight(const int& nheight, const CTxOut& txout)
{
    if(txout.nUnlockedHeight <= 0)
        return false;

    if(txout.nUnlockedHeight <= g_nChainHeight) // unlocked
        return false;

    int64_t nOffset = txout.nUnlockedHeight - nheight;
	
	if (nheight >= g_nStartSPOSHeight)
    {
        if(nOffset <= 28 * SPOS_BLOCKS_PER_DAY || nOffset > 120 * SPOS_BLOCKS_PER_MONTH) // invalid
            return false;
    }
	else
	{
		if(nOffset <= 28 * BLOCKS_PER_DAY || nOffset > 120 * BLOCKS_PER_MONTH) // invalid
        	return false;
	}
    return true;
}

int GetLockedMonth(const uint256& txHash, const CTxOut& txout)
{
    if(txout.nUnlockedHeight <= 0)
        return 0;

    int nHeight = GetTxHeight(txHash);
    if(txout.nUnlockedHeight < nHeight)
        return 0;

    int m1 = 0;
    int m2= 0;

    if (nHeight >= g_nStartSPOSHeight)
    {
        m1 = (txout.nUnlockedHeight - nHeight) / SPOS_BLOCKS_PER_MONTH;
        m2 = (txout.nUnlockedHeight - nHeight) % SPOS_BLOCKS_PER_MONTH;
    }
    else
    {
        m1 = (txout.nUnlockedHeight - nHeight) / BLOCKS_PER_MONTH;
        m2 = (txout.nUnlockedHeight - nHeight) % BLOCKS_PER_MONTH;
    }

    if(m2 != 0)
        m1++;

    if(!IsLockedMonthRange(m1))
        throw std::runtime_error("GetLockMonth() : locked month out of range");

    return m1;
}

int GetLockedMonthByHeight(const int& nheight, const CTxOut& txout)
{
    if(txout.nUnlockedHeight <= 0)
        return 0;

    if(txout.nUnlockedHeight < nheight)
        return 0;

	int m1 = 0;
	int m2 = 0;

	if (nheight >= g_nStartSPOSHeight)
    {
        m1 = (txout.nUnlockedHeight - nheight) / SPOS_BLOCKS_PER_MONTH;
        m2 = (txout.nUnlockedHeight - nheight) % SPOS_BLOCKS_PER_MONTH;
    }
    else
    {
        m1 = (txout.nUnlockedHeight - nheight) / BLOCKS_PER_MONTH;
        m2 = (txout.nUnlockedHeight - nheight) % BLOCKS_PER_MONTH;
    }

    if(m2 != 0)
        m1++;

    if(!IsLockedMonthRange(m1))
        throw std::runtime_error("GetLockMonth() : locked month out of range");

    return m1;
}

CAmount GetCancelledAmount(const int &nHeight)
{
    int nOffset = nHeight - g_nProtocolV2Height;
    if(nOffset < 0)
        return 0;

    int nMonth = 0;
    if (nHeight >= g_nStartSPOSHeight)
        nMonth = nOffset / SPOS_BLOCKS_PER_MONTH;
    else
        nMonth = nOffset / BLOCKS_PER_MONTH;
    if (nMonth == 0)
        return 500 * COIN;

    double nLeft = 500.00;
    for(int i = 1; i <= nMonth; i++)
    {
        nLeft *=  0.95;

        // The decimal point precision is calculated to 2 decimal places
        uint32_t thirddata = 0;
        thirddata = (uint32_t)(nLeft * 1000) % 100 % 10;
        if (thirddata > 4)
            nLeft = (double)((uint32_t)(nLeft * 100) + 1) / 100;
        else
        {
            if (thirddata == 4)
            {
                uint32_t fouthdata =0;
                fouthdata = (uint32_t)(nLeft * 10000) % 1000 % 100 %10;
                if (fouthdata > 4)
                    nLeft = (double)((uint32_t)(nLeft * 100) + 1) / 100;
                else
                    nLeft = (double)((uint32_t)(nLeft * 100)) / 100;
            }
            else
                nLeft = (double)((uint32_t)(nLeft * 100)) / 100;
        }

        if (nLeft < 50)
            nLeft = 50.00;
    }

    CAmount value = nLeft * COIN;
    if (value % 1000000 == 999999)
        value += 1;

    return value;
}

CAmount GetTxAdditionalFee(const CTransaction& tx)
{
    CAmount nFee = 0;
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        const std::vector<unsigned char>& vReserve = txout.vReserve;
        unsigned int nSize = vReserve.size();
        if(nSize > TXOUT_RESERVE_MAX_SIZE)
            return -1;

        if(nSize <= TXOUT_RESERVE_MIN_SIZE)
            continue;

        if(nSize == 3000)
        {
            nFee += 0.001 * COIN;
            continue;
        }

        nFee += (nSize / 300 + (nSize % 300 ? 1 : 0)) * 0.0001 * COIN;
    }

    return nFee;
}
