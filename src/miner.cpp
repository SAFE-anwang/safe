// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2018 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "hash.h"
#include "validation.h"
#include "net.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
#include "validationinterface.h"
#include "masternodeman.h"
#include "main.h"
#include "activemasternode.h"
#include "messagesigner.h"

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <queue>

using namespace std;

//////////////////////////////////////////////////////////////////////////////
//
// SafeMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;
extern int g_nStartSPOSHeight;
extern CMasternodeMan mnodeman;
extern CActiveMasternode activeMasternode;
extern unsigned int g_nMasternodeSPosCount;
extern unsigned int g_nMasternodeMinOnlineTime;
extern int64_t g_nStartNewLoopTime;
extern std::vector<CMasternode> g_vecResultMasternodes;
extern std::map<CNetAddr, LocalServiceInfo> mapLocalHost;

class ScoreCompare
{
public:
    ScoreCompare() {}

    bool operator()(const CTxMemPool::txiter a, const CTxMemPool::txiter b)
    {
        return CompareTxMemPoolEntryByScore()(*b,*a); // Convert to less than
    }
};

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks&&!IsStartSPosHeight(pindexPrev->nHeight+1))
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}

CBlockTemplate* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn)
{
    // Create new block
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if(!pblocktemplate.get())
        return NULL;
    CBlock *pblock = &pblocktemplate->block; // pointer for convenience

    // Create coinbase tx
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;

    // Largest block you're willing to create:
    unsigned int nBlockMaxSize = GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
    // Limit to between 1K and MAX_BLOCK_SIZE-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MaxBlockSize(fDIP0001ActiveAtTip)-1000), nBlockMaxSize));

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
    nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    unsigned int nBlockMinSize = GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE);
    nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

    // Collect memory pool transactions into the block
    CTxMemPool::setEntries inBlock;
    CTxMemPool::setEntries waitSet;

    // This vector will be sorted into a priority queue:
    vector<TxCoinAgePriority> vecPriority;
    TxCoinAgePriorityCompare pricomparer;
    std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash> waitPriMap;
    typedef std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash>::iterator waitPriIter;
    double actualPriority = -1;

    std::priority_queue<CTxMemPool::txiter, std::vector<CTxMemPool::txiter>, ScoreCompare> clearedTxs;
    bool fPrintPriority = GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    uint64_t nBlockSize = 1000;
    uint64_t nBlockTx = 0;
    unsigned int nBlockSigOps = 100;
    int lastFewTxs = 0;
    CAmount nFees = 0;

    {
        LOCK(cs_main);

        CBlockIndex* pindexPrev = chainActive.Tip();
        const int nHeight = pindexPrev->nHeight + 1;
        pblock->nTime = GetAdjustedTime();
        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

        // Add our coinbase tx as first transaction
        pblock->vtx.push_back(txNew);
        pblocktemplate->vTxFees.push_back(-1); // updated at end
        pblocktemplate->vTxSigOps.push_back(-1); // updated at end
        pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
        // -regtest only: allow overriding block.nVersion with
        // -blockversion=N to test forking scenarios
        if (chainparams.MineBlocksOnDemand())
            pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

        int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                ? nMedianTimePast
                                : pblock->GetBlockTime();

        {
            LOCK(mempool.cs);

            bool fPriorityBlock = nBlockPrioritySize > 0;
            if (fPriorityBlock) {
                vecPriority.reserve(mempool.mapTx.size());
                for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
                     mi != mempool.mapTx.end(); ++mi)
                {
                    double dPriority = mi->GetPriority(nHeight);
                    CAmount dummy;
                    mempool.ApplyDeltas(mi->GetTx().GetHash(), dPriority, dummy);
                    vecPriority.push_back(TxCoinAgePriority(dPriority, mi));
                }
                std::make_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
            }

            CTxMemPool::indexed_transaction_set::nth_index<3>::type::iterator mi = mempool.mapTx.get<3>().begin();
            CTxMemPool::txiter iter;

            while (mi != mempool.mapTx.get<3>().end() || !clearedTxs.empty())
            {
                bool priorityTx = false;
                if (fPriorityBlock && !vecPriority.empty()) { // add a tx from priority queue to fill the blockprioritysize
                    priorityTx = true;
                    iter = vecPriority.front().second;
                    actualPriority = vecPriority.front().first;
                    std::pop_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                    vecPriority.pop_back();
                }
                else if (clearedTxs.empty()) { // add tx with next highest score
                    iter = mempool.mapTx.project<0>(mi);
                    mi++;
                }
                else {  // try to add a previously postponed child tx
                    iter = clearedTxs.top();
                    clearedTxs.pop();
                }

                if (inBlock.count(iter))
                    continue; // could have been added to the priorityBlock

                const CTransaction& tx = iter->GetTx();

                bool fOrphan = false;
                BOOST_FOREACH(CTxMemPool::txiter parent, mempool.GetMemPoolParents(iter))
                {
                    if (!inBlock.count(parent)) {
                        fOrphan = true;
                        break;
                    }
                }
                if (fOrphan) {
                    if (priorityTx)
                        waitPriMap.insert(std::make_pair(iter,actualPriority));
                    else
                        waitSet.insert(iter);
                    continue;
                }

                unsigned int nTxSize = iter->GetTxSize();
                if (fPriorityBlock &&
                    (nBlockSize + nTxSize >= nBlockPrioritySize || !AllowFree(actualPriority))) {
                    fPriorityBlock = false;
                    waitPriMap.clear();
                }
                if (!priorityTx &&
                    (iter->GetModifiedFee() < ::minRelayTxFee.GetFee(nTxSize) && nBlockSize >= nBlockMinSize)) {
                    break;
                }
                if (nBlockSize + nTxSize >= nBlockMaxSize) {
                    if (nBlockSize >  nBlockMaxSize - 100 || lastFewTxs > 50) {
                        break;
                    }
                    // Once we're within 1000 bytes of a full block, only look at 50 more txs
                    // to try to fill the remaining space.
                    if (nBlockSize > nBlockMaxSize - 1000) {
                        lastFewTxs++;
                    }
                    continue;
                }

                if (!IsFinalTx(tx, nHeight, nLockTimeCutoff))
                    continue;

                unsigned int nTxSigOps = iter->GetSigOpCount();
                unsigned int nMaxBlockSigOps = MaxBlockSigOps(fDIP0001ActiveAtTip);
                if (nBlockSigOps + nTxSigOps >= nMaxBlockSigOps) {
                    if (nBlockSigOps > nMaxBlockSigOps - 2) {
                        break;
                    }
                    continue;
                }

                CAmount nTxFees = iter->GetFee();
                // Added
                pblock->vtx.push_back(tx);
                pblocktemplate->vTxFees.push_back(nTxFees);
                pblocktemplate->vTxSigOps.push_back(nTxSigOps);
                nBlockSize += nTxSize;
                ++nBlockTx;
                nBlockSigOps += nTxSigOps;
                nFees += nTxFees;

                if (fPrintPriority)
                {
                    double dPriority = iter->GetPriority(nHeight);
                    CAmount dummy;
                    mempool.ApplyDeltas(tx.GetHash(), dPriority, dummy);
                    LogPrintf("priority %.1f fee %s txid %s\n",
                              dPriority , CFeeRate(iter->GetModifiedFee(), nTxSize).ToString(), tx.GetHash().ToString());
                }

                inBlock.insert(iter);

                // Add transactions that depend on this one to the priority queue
                BOOST_FOREACH(CTxMemPool::txiter child, mempool.GetMemPoolChildren(iter))
                {
                    if (fPriorityBlock) {
                        waitPriIter wpiter = waitPriMap.find(child);
                        if (wpiter != waitPriMap.end()) {
                            vecPriority.push_back(TxCoinAgePriority(wpiter->second,child));
                            std::push_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                            waitPriMap.erase(wpiter);
                        }
                    }
                    else {
                        if (waitSet.count(child)) {
                            clearedTxs.push(child);
                            waitSet.erase(child);
                        }
                    }
                }
            }

        }

        // NOTE: unlike in bitcoin, we need to pass PREVIOUS block height here
        CAmount blockReward = 0;
        if (nHeight >= g_nStartSPOSHeight)
            blockReward = nFees + GetSPOSBlockSubsidy(pindexPrev->nHeight, Params().GetConsensus());
        else
            blockReward = nFees + GetBlockSubsidy(pindexPrev->nBits, pindexPrev->nHeight, Params().GetConsensus());

        // Compute regular coinbase transaction.
        txNew.vout[0].nValue = blockReward;
        txNew.vin[0].scriptSig = CScript() << nHeight << OP_0;

        // Update coinbase transaction with additional info about masternode and governance payments,
        // get some info back to pass to getblocktemplate
        FillBlockPayments(txNew, nHeight, blockReward, pblock->txoutMasternode, pblock->voutSuperblock);
        // LogPrintf("CreateNewBlock -- nBlockHeight %d blockReward %lld txoutMasternode %s txNew %s",
        //             nHeight, blockReward, pblock->txoutMasternode.ToString(), txNew.ToString());

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        LogPrintf("CreateNewBlock(): total size %u txs: %u fees: %ld sigops %d\n", nBlockSize, nBlockTx, nFees, nBlockSigOps);

        // Update block coinbase
        pblock->vtx[0] = txNew;
        pblocktemplate->vTxFees[0] = -nFees;

        // Fill in header
        pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);

        //SPOS nBits set to 0
        if(IsStartSPosHeight(nHeight))
            pblock->nBits = 0;
        else
            pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());

        pblock->nNonce = 0;
        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(pblock->vtx[0]);

        if(!IsStartSPosHeight(nHeight))
        {
            CValidationState state;
            if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
                throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
            }
        }
    }

    return pblocktemplate.release();
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2

    //SPOS extra nonce set to zero
    if(IsStartSPosHeight(nHeight))
    {
        nExtraNonce = 0;
    }else
    {
        CMutableTransaction txCoinbase(pblock->vtx[0]);
        txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
        assert(txCoinbase.vin[0].scriptSig.size() <= 100);

        pblock->vtx[0] = txCoinbase;
        pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    }
}

/*
 * SPOS Coinbase add collateral address and the sign of the collateral address
*/
bool CoinBaseAddSPosExtraData(CBlock* pblock, const CBlockIndex* pindexPrev,CMasternode& mn)
{
    unsigned int nHeight = pindexPrev->nHeight+1;
    CMutableTransaction txCoinbase(pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(0)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    uint16_t nSPOSVersion = SPOS_VERSION;
    const unsigned char* pVersion = (const unsigned char*)&nSPOSVersion;
    txCoinbase.vout[0].vReserve.push_back(pVersion[0]);
    txCoinbase.vout[0].vReserve.push_back(pVersion[1]);

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(1000);
    ssKey << activeMasternode.pubKeyMasternode.GetID();
    string serialMasternode = ssKey.str();

    for(unsigned int i = 0; i < serialMasternode.size(); i++)
        txCoinbase.vout[0].vReserve.push_back(serialMasternode[i]);

    std::string strCollateralAddress = CBitcoinAddress(activeMasternode.pubKeyMasternode.GetID()).ToString();
    std::vector<unsigned char> vchSig;
    if(!CMessageSigner::SignMessage(strCollateralAddress, vchSig, activeMasternode.keyMasternode)) {
        LogPrintf("SPOS_Error:SignMessage() failed\n");
        return false;
    }

    std::string strError;
    if(!CMessageSigner::VerifyMessage(activeMasternode.pubKeyMasternode, vchSig, strCollateralAddress, strError)) {
        LogPrintf("SPOS_Error:VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    string strSig;
    for(unsigned int i=0;i<vchSig.size();i++)
    {
        txCoinbase.vout[0].vReserve.push_back(vchSig[i]);
        strSig.push_back(vchSig[i]);
    }

    LogPrintf("SPOS_Message:height:%d,coinbase extra data:strAddress:%s,vchSig:%s,vchSig size:%d\n",nHeight,strCollateralAddress,strSig, vchSig.size());

    pblock->vtx[0] = txCoinbase;
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);

    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//

// ***TODO*** ScanHash is not yet used in Safe
//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
//
//bool static ScanHash(const CBlockHeader *pblock, uint32_t& nNonce, uint256 *phash)
//{
//    // Write the first 76 bytes of the block header to a double-SHA256 state.
//    CHash256 hasher;
//    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
//    ss << *pblock;
//    assert(ss.size() == 80);
//    hasher.Write((unsigned char*)&ss[0], 76);

//    while (true) {
//        nNonce++;

//        // Write the last 4 bytes of the block header (the nonce) to a copy of
//        // the double-SHA256 state, and compute the result.
//        CHash256(hasher).Write((unsigned char*)&nNonce, 4).Finalize((unsigned char*)phash);

//        // Return the nonce if the hash has at least some zero bits,
//        // caller will check if it has enough to reach the target
//        if (((uint16_t*)phash)[15] == 0)
//            return true;

//        // If nothing found after trying for a while, return -1
//        if ((nNonce & 0xfff) == 0)
//            return false;
//    }
//}

static bool ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams)
{
    LogPrintf("%s\n", pblock->ToString());
    LogPrintf("generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("ProcessBlockFound -- generated block is stale");
    }

    // Inform about the new block
    GetMainSignals().BlockFound(pblock->GetHash());

    // Process this block the same as if we had received it from another node
    if (!ProcessNewBlock(chainparams, pblock, true, NULL, NULL))
        return error("ProcessBlockFound -- ProcessNewBlock() failed, block not accepted");

    return true;
}

static void ConsensusUsePow(const CChainParams& chainparams, CConnman& connman,CBlock *pblock,CBlockIndex* pindexPrev
                           ,boost::shared_ptr<CReserveScript>& coinbaseScript,unsigned int nTransactionsUpdatedLast)
{
    //
    // Search
    //
    int64_t nStart = GetTime();
    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
    while (true)
    {
        unsigned int nHashesDone = 0;

        uint256 hash;
        while (true)
        {
            hash = pblock->GetHash();
            if (UintToArith256(hash) <= hashTarget)
            {
                {
                    srand((unsigned int)time(NULL));
                    //int nTime = ((rand() % GetArg("-sleep_offset", 1)) + GetArg("-sleep_min", 24)) * 1000;
					int nTime = ((rand() % GetArg("-sleep_offset", 1)) + GetArg("-sleep_min", chainparams.GetConsensus().nPowTargetSpacing)) * 1000;
                    MilliSleep(nTime);
                }
                // Found a solution
                SetThreadPriority(THREAD_PRIORITY_NORMAL);
                LogPrintf("SafeMiner:\n  proof-of-work found\n  hash: %s\n  target: %s\n", hash.GetHex(), hashTarget.GetHex());
                ProcessBlockFound(pblock, chainparams);
                SetThreadPriority(THREAD_PRIORITY_LOWEST);
                coinbaseScript->KeepScript();

                // In regression test mode, stop mining after a block is found. This
                // allows developers to controllably generate a block on demand.
                if (chainparams.MineBlocksOnDemand())
                    throw boost::thread_interrupted();

                break;
            }
            pblock->nNonce += 1;
            nHashesDone += 1;
            if ((pblock->nNonce & 0xFF) == 0)
                break;
        }

        // Check for stop or if block needs to be rebuilt
        boost::this_thread::interruption_point();
        // Regtest mode doesn't require peers
        if (connman.GetNodeCount(CConnman::CONNECTIONS_ALL) == 0 && chainparams.MiningRequiresPeers())
            break;
        if (pblock->nNonce >= 0xffff0000)
            break;
        if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
            break;
        if (pindexPrev != chainActive.Tip())
            break;

        // Update nTime every few seconds
        if (UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev) < 0)
            break; // Recreate the block if the clock has run backwards,
                   // so that we can use the correct time.
        if (chainparams.GetConsensus().fPowAllowMinDifficultyBlocks)
        {
            // Changing pblock->nTime can change work required on testnet:
            hashTarget.SetCompact(pblock->nBits);
        }
    }
}

static void SelectMasterNode(unsigned int nCurHeight,unsigned int nNewBlockHeight,CBlock *pblock,int64_t& nNextTime)
{
//    if(nCurHeight == nNewBlockHeight)
//        return;

//    nCurHeight = nNewBlockHeight;
    unsigned int ret = nNewBlockHeight % g_nMasternodeSPosCount;
    if(ret != 0 )
        return;

    std::map<COutPoint, CMasternode> mapMasternodes;
    if (sporkManager.IsSporkActive(SPORK_6_SPOS_ENABLED))
    {
        std::map<COutPoint, CMasternode> fullmapMasternodes = mnodeman.GetFullMasternodeMap();

        const std::vector<COutPointData> &vtempOutPointData = Params().COutPointDataS();
        std::vector<COutPointData>::const_iterator it = vtempOutPointData.begin();
        for (;it != vtempOutPointData.end(); it++)
        {
            COutPointData tempcoutpointdata = *it;
            COutPoint tempcoutpoint;
            tempcoutpoint.hash = SerializeHash(tempcoutpointdata.strtx);
            tempcoutpoint.n = tempcoutpointdata.n;
             std::map<COutPoint, CMasternode>::iterator tempit = fullmapMasternodes.find(tempcoutpoint);
            if (tempit != fullmapMasternodes.end())
                mapMasternodes[tempcoutpoint] = tempit->second;
        }
    }
    else
        mapMasternodes = mnodeman.GetFullMasternodeMap();

    g_vecResultMasternodes.clear();
    std::vector<CMasternode>().swap(g_vecResultMasternodes);
    //1.3.3
    g_nStartNewLoopTime = GetTimeMillis();
    string strStartNewLoopTime = DateTimeStrFormat("%Y-%m-%d %H:%M:%S", g_nStartNewLoopTime/1000);
    string strBlockTime = DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pblock->nTime);
    if(g_nStartNewLoopTime < pblock->nTime*1000)
    {
        LogPrintf("SPOS_Warning:current start new loop time(%lld,%s) less than block time(%lld,%s)\n",g_nStartNewLoopTime/1000,strStartNewLoopTime
                  ,pblock->nTime,strBlockTime);
        return;
    }
    nNextTime = g_nStartNewLoopTime + Params().GetConsensus().nSPOSTargetSpacing * 1000;

    if(mapMasternodes.empty())
    {
        LogPrintf("SPOS_Error:mapMasternodes is empty\n");
        return;
    }

    std::map<uint256,CMasternode> scoreMasternodes;

    //sort by score
    for (std::map<COutPoint, CMasternode>::const_reverse_iterator mnpair = mapMasternodes.rbegin(); mnpair != mapMasternodes.rend(); ++mnpair)
    {
        const CMasternode& mn = (*mnpair).second;
        int64_t onlineTime = mn.lastPing.sigTime - mn.sigTime;
        //XJTODO Test codes can annotate this
        if(/*mn.nActiveState != CMasternode::MASTERNODE_ENABLED ||*/ onlineTime < g_nMasternodeMinOnlineTime)
            continue;

        uint256 hash = mn.pubKeyCollateralAddress.GetHash();
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << hash;
        ss << pblock->nTime;
        uint256 score = ss.GetHash();
        scoreMasternodes[score] = mn;
    }

    if(scoreMasternodes.empty())
    {
        LogPrintf("SPOS_Error:scoreMasternodes is empty\n");
        return;
    }

    unsigned int count = 0;
    for (auto& mnpair : scoreMasternodes)
    {
        CMasternode& mn = mnpair.second;
        g_vecResultMasternodes.push_back(mn);
        count++;
        if(count>=g_nMasternodeSPosCount)
            break;
    }

    //random the master node
    uint64_t now_hi = uint64_t(pblock->nTime) << 32;
    for( uint32_t i = 0; i < g_vecResultMasternodes.size(); ++i )
    {
        /// High performance random generator
        /// http://xorshift.di.unimi.it/
        uint64_t k = now_hi + uint64_t(i)*2685821657736338717ULL;
        k ^= (k >> 12);
        k ^= (k << 25);
        k ^= (k >> 27);
        k *= 2685821657736338717ULL;

        uint32_t jmax = g_vecResultMasternodes.size() - i;
        uint32_t j = i + k%jmax;
        std::swap( g_vecResultMasternodes[i],g_vecResultMasternodes[j] );
    }

    string localIpPortInfo = activeMasternode.service.ToString();
    uint32_t size = g_vecResultMasternodes.size();
    LogPrintf("SPOS:start new loop,local info:%s,currHeight:%d,startNewLoopTime:%lld(%s),blockTime:%lld(%s),select %d masternode\n"
              ,localIpPortInfo,nNewBlockHeight,g_nStartNewLoopTime,strStartNewLoopTime,pblock->nTime,strBlockTime,size);
    for( uint32_t i = 0; i < size; ++i )
    {
        CMasternode& mn = g_vecResultMasternodes[i];
        LogPrintf("SPOS_Message:masterNodeIP[%d]:%s\n", i, mn.addr.ToStringIP());
    }
}

/*
    Consensus Use Safe Pos
*/
static void ConsensusUseSPos(const CChainParams& chainparams,CConnman& connman,CBlockIndex* pindexPrev
                             ,unsigned int& nCurHeight,unsigned int nNewBlockHeight,CBlock *pblock
                             ,boost::shared_ptr<CReserveScript>& coinbaseScript
                             ,unsigned int nTransactionsUpdatedLast,int64_t& nNextTime)
{
    if(nCurHeight == nNewBlockHeight)
        return;

    nCurHeight = nNewBlockHeight;

    SelectMasterNode(nCurHeight,nNewBlockHeight,pblock,nNextTime);

    unsigned int masternodeSPosCount = g_vecResultMasternodes.size();
    if(masternodeSPosCount == 0)
    {
        LogPrintf("SPOS_Error:vecMasternodes is empty\n");
        return;
    }

    nCurHeight = nNewBlockHeight;

    //if masternodeSPosCount less than g_nMasternodeSPosCount,still continue,just % actual masternodeSPosCount
    if(masternodeSPosCount != g_nMasternodeSPosCount)
        LogPrintf("SPOS_Warning:system g_nMasternodeSPosCount:%d,curr vecMasternodes size:%d\n",g_nMasternodeSPosCount,masternodeSPosCount);

    //1.3
    int64_t nCurrTime = GetTimeMillis();
    if(nCurrTime < pblock->nTime*1000)
    {
        string strCurrTime = DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nCurrTime/1000);
        string strBlockTime = DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pblock->nTime);
        LogPrintf("SPOS_Warning:current time(%d,%s) less than new block time(%d,%s)\n",nCurrTime/1000,strCurrTime,pblock->nTime,strBlockTime);
        return;
    }

    int64_t interval = Params().GetConsensus().nSPOSTargetSpacing;
    nCurrTime += interval*1000;
    int64_t nTimeInerval = (nCurrTime - g_nStartNewLoopTime) / 1000;
    int index = nTimeInerval / interval % masternodeSPosCount;
    CMasternode& mn = g_vecResultMasternodes[index-1];
    string masterIP = mn.addr.ToStringIP();
    string localIP = activeMasternode.service.ToStringIP();

    nNextTime = g_nStartNewLoopTime + index*interval*1000;

    //XJTODO,Test codes can annotate this
//    if(localIP != masterIP)
//    {
//        LogPrintf("SPOS_Message:Wait MastnodeIP[%d]:%s to generate pos block:%d.\n",index-1,masterIP,nNewBlockHeight);
//        return;
//    }
//    if(mnodeman.GetFullMasternodeMap().count(activeMasternode.outpoint)<=0)
//    {
//        LogPrintf("SPOS_Error:output(%d:%s) not exist.\n",activeMasternode.outpoint.n,activeMasternode.outpoint.hash.ToString());
//        return;
//    }
//    CMasternode& mnLocal = mnodeman.GetFullMasternodeMap()[activeMasternode.outpoint];
//    if(mnLocal.GetInfo().pubKeyCollateralAddress != mn.GetInfo().pubKeyCollateralAddress)
//    {
//        LogPrintf("SPOS_Error:local collateral address:%s is different to worker masternode collateral address:%s\n"
//                  ,CBitcoinAddress(mnLocal.pubKeyCollateralAddress.GetID()).ToString()
//                  ,CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
//        return;
//    }

    int64_t nIntervalMS = 500;
    int64_t nActualTimeMillisInterval = std::abs(nNextTime - nCurrTime);
    if(nActualTimeMillisInterval > nIntervalMS && nNextTime!=0)
    {
        LogPrintf("SPOS_Warning:nActualTimeMillisInterval(%d) less than nIntervalMS(%d)\n",nActualTimeMillisInterval,nIntervalMS);
        return;
    }

    //it's turn to generate block
    LogPrintf("SPOS_Info:Self mastnodeIP[%d]:%s generate pos block:%d.\n",index-1,localIP,nNewBlockHeight);

    SetThreadPriority(THREAD_PRIORITY_NORMAL);
    //coin base add extra data
    if(!CoinBaseAddSPosExtraData(pblock,pindexPrev,mn))
        return;

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }

    ProcessBlockFound(pblock, chainparams);
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    coinbaseScript->KeepScript();

    // In regression test mode, stop mining after a block is found. This
    // allows developers to controllably generate a block on demand.
    if (chainparams.MineBlocksOnDemand())
        throw boost::thread_interrupted();

    // Check for stop or if block needs to be rebuilt
    boost::this_thread::interruption_point();
    // Regtest mode doesn't require peers
    if (connman.GetNodeCount(CConnman::CONNECTIONS_ALL) == 0 && chainparams.MiningRequiresPeers())
        return;
    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast /*&& GetTime() - nCurrTime > 60*/)//XJTODO remove annotate code
        return;
    if (pindexPrev != chainActive.Tip())
        return;

    // Update nTime every few seconds
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
}

// ***TODO*** that part changed in bitcoin, we are using a mix with old one here for now
void static BitcoinMiner(const CChainParams& chainparams, CConnman& connman)
{
    LogPrintf("SafeMiner -- started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("safe-miner");

    unsigned int nExtraNonce = 0;

    boost::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().ScriptForMining(coinbaseScript);

    try {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (!coinbaseScript || coinbaseScript->reserveScript.empty())
            throw std::runtime_error("No coinbase script available (mining requires a wallet)");

        g_nStartNewLoopTime = GetTimeMillis();
        unsigned int nCurHeight = 0;
        int64_t nNextBlockTime = 0;
        while (true) {
            if (chainparams.MiningRequiresPeers()) {
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                do {
                    bool fvNodesEmpty = connman.GetNodeCount(CConnman::CONNECTIONS_ALL) == 0;
                    if (!fvNodesEmpty && !IsInitialBlockDownload() && masternodeSync.IsSynced())
                        break;
                    MilliSleep(1000);
                } while (true);
            }

            //
            // Create new block
            //
            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex* pindexPrev = chainActive.Tip();
            if(!pindexPrev) break;

            std::unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlock(chainparams, coinbaseScript->reserveScript));
            if (!pblocktemplate.get())
            {
                LogPrintf("SafeMiner -- Keypool ran out, please call keypoolrefill before restarting the mining thread\n");
                return;
            }

            unsigned int nNewBlockHeight = chainActive.Height() + 1;

            CBlock *pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

            LogPrintf("SafeMiner -- Running miner with %u transactions in block (%u bytes)\n", pblock->vtx.size(),
                ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

            if(IsStartSPosHeight(nNewBlockHeight))
            {
                ConsensusUseSPos(chainparams,connman,pindexPrev,nCurHeight,nNewBlockHeight,pblock
                                 ,coinbaseScript,nTransactionsUpdatedLast,nNextBlockTime);

                MilliSleep(50);
            }else{
                ConsensusUsePow(chainparams,connman,pblock,pindexPrev,coinbaseScript,nTransactionsUpdatedLast);
            }
        }
    }
    catch (const boost::thread_interrupted&)
    {
        LogPrintf("SafeMiner -- terminated\n");
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("SafeMiner -- runtime error: %s\n", e.what());
        return;
    }
}

void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams& chainparams, CConnman& connman)
{
    if(!GetBoolArg("-lmb_gen", false))
        return;

    static boost::thread_group* minerThreads = NULL;

    if (nThreads < 0)
        nThreads = GetNumCores();

    if (minerThreads != NULL)
    {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || !fGenerate)
        return;

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(boost::bind(&BitcoinMiner, boost::cref(chainparams), boost::ref(connman)));
}
