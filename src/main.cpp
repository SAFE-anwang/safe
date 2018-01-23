#include "main.h"
#include "base58.h"
#include "validation.h"
#include "consensus/merkle.h"
#include "init.h"

#include <string>

int g_nChainHeight = -1;
int g_nCriticalHeight = 807085;
int g_nAnWwangDiffOffset = 100;
static std::string g_strCriticalAddress = "Xx7fUGPeMLr7gyYfWEF5nC2AXaar95sZnQ";

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev)
{
    CBlock block;

    if(!pindexPrev)
        return block;

    const CChainParams& chainparams = Params();

    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = CScript() << g_nCriticalHeight << OP_0;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = GetScriptForDestination(CBitcoinAddress(g_strCriticalAddress).Get());
    txNew.vout[0].nValue = MAX_MONEY;

    block.vtx.push_back(txNew);

    block.nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    block.hashPrevBlock = pindexPrev->GetBlockHash();
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nTime = pindexPrev->nTime + 30;
    block.nBits = 0x1e0ffff0;
    block.nNonce = 0;

    return block;
}

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev, const int nVersion, const unsigned int nTime, const unsigned int nBits)
{
    CBlock block;

    if(!pindexPrev)
        return block;

    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = CScript() << g_nCriticalHeight << OP_0;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = GetScriptForDestination(CBitcoinAddress(g_strCriticalAddress).Get());
    txNew.vout[0].nValue = MAX_MONEY;

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

int VectorEqual(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b)
{
    unsigned int a_size = a.size();
    unsigned int b_size = b.size();

    if(a_size != b_size) // non-equal length
        return false;

    if(a_size == 0) // all are empty
        return true;

    for(std::vector<unsigned char>::const_iterator it_a = a.begin(), it_b = b.begin();
                                                   it_a != a.end(), it_b != b.end();
                                                   it_a++, it_b++)
    {
        if(*it_a != *it_b)
            return false;
    }

    return true;
}

