// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2018 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation.h"

#include "alert.h"
#include "arith_uint256.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "hash.h"
#include "init.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "timedata.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "spork.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "versionbits.h"
#include "base58.h"
#include "main.h"
#include "rpc/server.h"
#include "masternode-sync.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include "instantx.h"
#include "masternodeman.h"
#include "masternode-payments.h"

#include <sstream>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/thread.hpp>

using namespace std;

#if defined(NDEBUG)
# error "Safe Core cannot be compiled without assertions."
#endif

#define BATCH_COUNT         10000
#define HANDLE_COUNT        20000

/**
 * Global state
 */

CCriticalSection cs_main;

BlockMap mapBlockIndex;
CChain chainActive;
CBlockIndex *pindexBestHeader = NULL;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
int nScriptCheckThreads = 0;
bool fImporting = false;
bool fReindex = false;
bool fTxIndex = true;
bool fAddressIndex = false;
bool fTimestampIndex = false;
bool fSpentIndex = false;
bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = DEFAULT_PERMIT_BAREMULTISIG;
bool fRequireStandard = true;
bool fHaveGUI = false;
unsigned int nBytesPerSigOp = DEFAULT_BYTES_PER_SIGOP;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = DEFAULT_CHECKPOINTS_ENABLED;
size_t nCoinCacheUsage = 5000 * 300;
uint64_t nPruneTarget = 0;
bool fAlerts = DEFAULT_ALERTS;
bool fEnableReplacement = DEFAULT_ENABLE_REPLACEMENT;
bool fGetCandyInfoStart = false;
std::mutex g_mutexAllCandyInfo;
std::vector<CCandy_BlockTime_Info> gAllCandyInfoVec;
std::mutex g_mutexTmpAllCandyInfo;
std::vector<CCandy_BlockTime_Info> gTmpAllCandyInfoVec;
bool fUpdateAllCandyInfoFinished = false;
unsigned int nCandyPageCount = 20;//display 20 candy info per page

const static int M = 2000; //Maximum number of digits
int numA[M];
int numB[M];

std::atomic<bool> fDIP0001WasLockedIn{false};
std::atomic<bool> fDIP0001ActiveAtTip{false};

struct CompareCandyInfo
{
    bool operator()(const std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue>& l, const std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue>& r)
    {
        return l.second.nHeight < r.second.nHeight;
    }
};

uint256 hashAssumeValid;

/** Fees smaller than this (in duffs) are considered zero fee (for relaying, mining and transaction creation) */
CFeeRate minRelayTxFee = CFeeRate(DEFAULT_LEGACY_MIN_RELAY_TX_FEE);

CTxMemPool mempool(::minRelayTxFee);
map<uint256, int64_t> mapRejectedBlocks GUARDED_BY(cs_main);

/**
 * Returns true if there are nRequired or more blocks of minVersion or above
 * in the last Consensus::Params::nMajorityWindow blocks, starting at pstart and going backwards.
 */
static bool IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned nRequired, const Consensus::Params& consensusParams);
static void CheckBlockIndex(const Consensus::Params& consensusParams);

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const string strMessageMagic = "DarkCoin Signed Message:\n";

std::mutex g_mutexChangeFile;

std::mutex g_mutexChangeInfo;
static std::list<CChangeInfo> g_listChangeInfo;
static int g_nListChangeInfoLimited = 20;
int GetChangeInfoListSize()
{
    std::lock_guard<std::mutex> lock(g_mutexChangeInfo);
    return int(g_listChangeInfo.size());
}
bool CompareChangeInfo(const CChangeInfo& a1, const CChangeInfo& a2)
{
    return a1.nHeight < a2.nHeight;
}

std::mutex g_mutexCandyHeight;
static std::list<int> listCandyHeight;

// Internal stuff
namespace {

    struct CBlockIndexWorkComparator
    {
        bool operator()(CBlockIndex *pa, CBlockIndex *pb) const {
            // First sort by most total work, ...
            if (pa->nChainWork > pb->nChainWork) return false;
            if (pa->nChainWork < pb->nChainWork) return true;

            // ... then by earliest time received, ...
            if (pa->nSequenceId < pb->nSequenceId) return false;
            if (pa->nSequenceId > pb->nSequenceId) return true;

            // Use pointer address as tie breaker (should only happen with blocks
            // loaded from disk, as those all have id 0).
            if (pa < pb) return false;
            if (pa > pb) return true;

            // Identical blocks.
            return false;
        }
    };

    CBlockIndex *pindexBestInvalid;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */
    set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
    /** All pairs A->B, where A (or one of its ancestors) misses transactions, but B has transactions.
     * Pruned nodes may have entries where B is missing data.
     */
    multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

    CCriticalSection cs_LastBlockFile;
    std::vector<CBlockFileInfo> vinfoBlockFile;
    int nLastBlockFile = 0;
    /** Global flag to indicate we should check to see if there are
     *  block/undo files that should be deleted.  Set on startup
     *  or if we allocate more file space when we're in prune mode
     */
    bool fCheckForPruning = false;

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    CCriticalSection cs_nBlockSequenceId;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    uint32_t nBlockSequenceId = 1;

    /** Dirty block index entries. */
    set<CBlockIndex*> setDirtyBlockIndex;

    /** Dirty block file entries. */
    set<int> setDirtyFileInfo;
} // anon namespace

CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    BOOST_FOREACH(const uint256& hash, locator.vHave) {
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end())
        {
            CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}

CCoinsViewCache *pcoinsTip = NULL;
CBlockTreeDB *pblocktree = NULL;

enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

// See definition for documentation
bool static FlushStateToDisk(CValidationState &state, FlushStateMode mode);

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = chainActive.Height() + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST)
                             ? chainActive.Tip()->GetMedianTimePast()
                             : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

/**
 * Calculates the block height and previous block's median time past at
 * which the transaction will be considered final in the context of BIP 68.
 * Also removes from the vector of input heights any entries which did not
 * correspond to sequence locked inputs as they do not affect the calculation.
 */
static std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2
                      && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

static bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

bool TestLockPointValidity(const LockPoints* lp)
{
    AssertLockHeld(cs_main);
    assert(lp);
    // If there are relative lock times then the maxInputBlock will be set
    // If there are no relative lock times, the LockPoints don't depend on the chain
    if (lp->maxInputBlock) {
        // Check whether chainActive is an extension of the block at which the LockPoints
        // calculation was valid.  If not LockPoints are no longer valid
        if (!chainActive.Contains(lp->maxInputBlock)) {
            return false;
        }
    }

    // LockPoints still valid
    return true;
}

bool CheckSequenceLocks(const CTransaction &tx, int flags, LockPoints* lp, bool useExistingLockPoints)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(mempool.cs);

    CBlockIndex* tip = chainActive.Tip();
    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses chainActive.Height()+1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than chainActive.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints) {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    }
    else {
        // pcoinsTip contains the UTXO set for chainActive.Tip()
        CCoinsViewMemPool viewMemPool(pcoinsTip, mempool);
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
            const CTxIn& txin = tx.vin[txinIndex];
            CCoins coins;
            if (!viewMemPool.GetCoins(txin.prevout.hash, coins)) {
                return error("%s: Missing input", __func__);
            }
            if (coins.nHeight == MEMPOOL_HEIGHT) {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            } else {
                prevheights[txinIndex] = coins.nHeight;
            }
        }
        lockPair = CalculateSequenceLocks(tx, flags, &prevheights, index);
        if (lp) {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            BOOST_FOREACH(int height, prevheights) {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight+1) {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return EvaluateSequenceLocks(index, lockPair);
}


unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const CTxOut &prevout = inputs.GetOutputFor(tx.vin[i]);
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

bool GetUTXOCoins(const COutPoint& outpoint, CCoins& coins)
{
    LOCK(cs_main);
    return !(!pcoinsTip->GetCoins(outpoint.hash, coins) ||
             (unsigned int)outpoint.n>=coins.vout.size() ||
             coins.vout[outpoint.n].IsNull());
}

int GetUTXOHeight(const COutPoint& outpoint)
{
    // -1 means UTXO is yet unknown or already spent
    CCoins coins;
    return GetUTXOCoins(outpoint, coins) ? coins.nHeight : -1;
}

int GetUTXOConfirmations(const COutPoint& outpoint)
{
    // -1 means UTXO is yet unknown or already spent
    LOCK(cs_main);
    int nPrevoutHeight = GetUTXOHeight(outpoint);
    return (nPrevoutHeight > -1 && chainActive.Tip()) ? chainActive.Height() - nPrevoutHeight + 1 : -1;
}

bool GetTxOutAddress(const CTxOut& txout, string* pAddress)
{
    CTxDestination dest;
    if(!ExtractDestination(txout.scriptPubKey, dest))
        return false;

    CBitcoinAddress address(dest);
    if(!address.IsValid())
        return false;

    string strAddress = address.ToString();
    if(pAddress)
        *pAddress = strAddress;

    return true;
}

bool CheckTransaction(const CTransaction& tx, CValidationState &state, const enum CTxSrcType& nType, const int& nHeight)
{
    int nTxHeight = 0;
    uint256 blockHash;
    if(nType == FROM_BLOCK)
        nTxHeight = nHeight;
    else
        nTxHeight = GetTxHeight(tx.GetHash(), &blockHash);

    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    // Size limits
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_LEGACY_BLOCK_SIZE)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    // Check tx version
    if(nType == FROM_BLOCK) // tx comes from old block or new block
    {
        if(IsProtocolV0(nTxHeight)) // tx.nVersion = 1 or 2 or 101
        {
            if(tx.nVersion > SAFE_TX_VERSION_1)
                return state.DoS(50, false, REJECT_INVALID, "block_tx: bad tx version in protocol v0");
        }
        else // tx.nVersion = 101 or 102
        {
            if(tx.nVersion < SAFE_TX_VERSION_1)
                return state.DoS(50, false, REJECT_INVALID, "block_tx: bad tx version in protocol v1");
        }
    }
    else if(nType == FROM_WALLET)
    {
        if(!blockHash.IsNull())
        {
            if(IsProtocolV0(nTxHeight)) // tx.nVersion = 1 or 2 or 101
            {
                if(tx.nVersion > SAFE_TX_VERSION_1)
                    return state.DoS(50, false, REJECT_INVALID, "wallet_tx: bad tx version in protocol v0");
            }
            else // tx.nVersion = 101 or 102
            {
                if(tx.nVersion < SAFE_TX_VERSION_1)
                    return state.DoS(50, false, REJECT_INVALID, "wallet_tx: bad tx version in protocol v1");
            }
        }
    }
    else if(nType == FROM_NEW) // tx from new tx
    {
        if(tx.nVersion < SAFE_TX_VERSION_2) // all new tx must be 102
            return state.DoS(50, false, REJECT_INVALID, "new_tx: bad tx version");
    }

    // Check for negative or overflow output values
    // Check unlocked height and reserve
    CAmount nValueOut = 0;
    CAmount nAssetValueOut = 0;
    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");

        if(txout.IsAsset())
        {
            if (txout.nValue > MAX_ASSETS)
                return state.DoS(100, false, REJECT_INVALID, "asset_txout: value is too large");
            nAssetValueOut += txout.nValue;
            if (!AssetsRange(nAssetValueOut))
                return state.DoS(100, false, REJECT_INVALID, "asset_txout: total value is too large");
        }
        else
        {
            if (txout.nValue > MAX_MONEY)
                return state.DoS(100, false, REJECT_INVALID, "safe_txout: value is too large");
            nValueOut += txout.nValue;
            if (!MoneyRange(nValueOut))
                return state.DoS(100, false, REJECT_INVALID, "safe_txout: total value is too large");
        }

        if(txout.nUnlockedHeight > 0)
        {
            if(nType == FROM_BLOCK)
            {
                int64_t nOffset = txout.nUnlockedHeight - nTxHeight;
                if(nOffset <= 28 * BLOCKS_PER_DAY || nOffset > 120 * BLOCKS_PER_MONTH)
                    return state.DoS(50, false, REJECT_INVALID, "block_tx: invalid txout unlocked height");
            }
            else if(nType == FROM_WALLET)
            {
                if(!blockHash.IsNull())
                {
                    int64_t nOffset = txout.nUnlockedHeight - nTxHeight;
                    if(nOffset <= 28 * BLOCKS_PER_DAY || nOffset > 120 * BLOCKS_PER_MONTH)
                        return state.DoS(50, false, REJECT_INVALID, "wallet_tx: invalid txout unlocked height");
                }
            }
            else
            {
                if(masternodeSync.IsBlockchainSynced())
                {
                    int64_t nOffset = txout.nUnlockedHeight - nTxHeight;
                    if(nOffset <= 28 * BLOCKS_PER_DAY || nOffset > 120 * BLOCKS_PER_MONTH)
                        return state.DoS(50, false, REJECT_INVALID, "new_tx: invalid txout unlocked height");
                }
            }
        }

        const std::vector<unsigned char>& vReserve = txout.vReserve;
        if(tx.IsCoinBase())
        {
            if(IsProtocolV0(nTxHeight))
                continue;
            if(tx.nVersion >= SAFE_TX_VERSION_1)
            {
                if(txout.nUnlockedHeight != 0 ||
                   vReserve.size() != TXOUT_RESERVE_MIN_SIZE ||
                   vReserve[0] != 's' || vReserve[1] != 'a' || vReserve[2] != 'f' || vReserve[3] != 'e')
                    return state.DoS(50, false, REJECT_INVALID, "coinsbase: invalid txout reserve");
            }
        }
        else
        {
            if(tx.nVersion == SAFE_TX_VERSION_1)
            {
                if(txout.nUnlockedHeight != 0 ||
                   vReserve.size() != TXOUT_RESERVE_MIN_SIZE ||
                   vReserve[0] != 's' || vReserve[1] != 'a' || vReserve[2] != 'f' || vReserve[3] != 'e')
                    return state.DoS(50, false, REJECT_INVALID, "tx: invalid txout reserve in protocol v1");
            }
            else if(tx.nVersion >= SAFE_TX_VERSION_2)
            {
                const std::vector<unsigned char>& vReserve = txout.vReserve;
                if(txout.nUnlockedHeight < 0 ||
                   vReserve.size() < TXOUT_RESERVE_MIN_SIZE || vReserve.size() > TXOUT_RESERVE_MAX_SIZE ||
                   vReserve[0] != 's' || vReserve[1] != 'a' || vReserve[2] != 'f' || vReserve[3] != 'e')
                    return state.DoS(50, false, REJECT_INVALID, "tx: invalid txout reserve in protocol v2");
            }
        }
    }

    // Check for duplicate inputs
    set<COutPoint> vInOutPoints;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        if (vInOutPoints.count(txin.prevout))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        vInOutPoints.insert(txin.prevout);
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    }
    else
    {
        BOOST_FOREACH(const CTxIn& txin, tx.vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}

bool CheckAppTransaction(const CTransaction& tx, CValidationState &state, const CCoinsViewCache& view, const bool fWithMempool)
{
    if(tx.IsCoinBase())
        return true;

    // check vout
    map<uint256, int> mapAppId;
    map<uint256, int> mapAssetId;
    uint256 appId;
    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];
        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData))
            continue;

        if(header.appId.IsNull())
            return state.DoS(50, false, REJECT_INVALID, "app_tx/asset_tx: app id is null");
        appId = header.appId;
        mapAppId[appId]++;

        if(header.nAppCmd == ISSUE_ASSET_CMD)
        {
            CAssetData assetData;
            if(!ParseIssueData(vData, assetData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse issue txout reserve failed");
            uint256 assetId = assetData.GetHash();
            if(assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "issue_asset: asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapAssetId[assetId]++;
        }
        else if(header.nAppCmd == ADD_ASSET_CMD)
        {
            CCommonData addData;
            if(!ParseCommonData(vData, addData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse add txout reserve failed");
            if(addData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "add_asset: asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapAssetId[addData.assetId]++;
        }
        else if(header.nAppCmd == TRANSFER_ASSET_CMD)
        {
            CCommonData transferData;
            if(!ParseCommonData(vData, transferData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse transfer txout reserve failed");
            if(transferData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "transfer_asset: asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapAssetId[transferData.assetId]++;
        }
        else if(header.nAppCmd == DESTORY_ASSET_CMD)
        {
            CCommonData destoryData;
            if(!ParseCommonData(vData, destoryData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse destory txout reserve failed");
            if(destoryData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "destory_asset: asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapAssetId[destoryData.assetId]++;
        }
        else if(header.nAppCmd == CHANGE_ASSET_CMD)
        {
            CCommonData changeData;
            if(!ParseCommonData(vData, changeData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse change txout reserve failed");
            if(changeData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "change_asset: asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapAssetId[changeData.assetId]++;
        }
        else if(header.nAppCmd == PUT_CANDY_CMD)
        {
            CPutCandyData putData;
            if(!ParsePutCandyData(vData, putData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse putcandy txout reserve failed");
            if(putData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "put_candy: asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapAssetId[putData.assetId]++;
        }
        else if(header.nAppCmd == GET_CANDY_CMD)
        {
            CGetCandyData getData;
            if(!ParseGetCandyData(vData, getData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse getcandy txout reserve failed");
            if(getData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "get_candy: asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapAssetId[getData.assetId]++;
        }
    }

    if(mapAppId.size() == 0 || appId.IsNull()) // safe tx(without safe-pay tx)
        return true;
    if(mapAppId.size() != 1)
        return state.DoS(50, false, REJECT_INVALID, "tx contain different app id");
    if(mapAssetId.size() > 1)
        return state.DoS(50, false, REJECT_INVALID, "tx contain different asset id");
    if(appId.GetHex() == g_strSafeAssetId) // safe-asset tx
    {
        if(mapAssetId.size() != 1)
            return state.DoS(50, false, REJECT_INVALID, "asset_tx: need 1 asset at least");
    }
    else
    {
        if(mapAssetId.size() != 0)
            return state.DoS(50, false, REJECT_INVALID, "app_tx: cannot contain asset txout");
    }
    if(mapAssetId.size() == 1)
    {
        if(appId.GetHex() != g_strSafeAssetId)
            return state.DoS(50, false, REJECT_INVALID, "asset_tx: invalid safe-asset app id");
    }

    // check fees
    CAmount nFees = view.GetValueIn(tx) - tx.GetValueOut();
    unsigned int nBytes = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    CAmount nMinRelayFee = ::minRelayTxFee.GetFee(nBytes);
    if(appId.GetHex() == g_strSafePayId)
    {
        if(nFees == 0 && nBytes > MAX_FREE_TRANSACTION_CREATE_SIZE)
            return state.DoS(50, false, REJECT_INVALID, "safepay_tx: must have fees in the tx");

        if(nFees > 0)
        {
            CAmount nAdditionalFee = GetTxAdditionalFee(tx);
            if(nFees < nMinRelayFee + nAdditionalFee)
                return state.DoS(50, false, REJECT_INVALID, strprintf("safepay_tx: invalid tx(size: %u) fee, need additional fee, %ld < %ld + %ld", nBytes, nFees, nMinRelayFee, nAdditionalFee));
        }

        return true;
    }

    // check additional fees
    CAmount nAdditionalFee = GetTxAdditionalFee(tx);
    if(nFees < nMinRelayFee + nAdditionalFee)
        return state.DoS(50, false, REJECT_INVALID, strprintf("app_tx/asset_tx: invalid tx(size: %u) fee, need additional fee, %ld < %ld + %ld", nBytes, nFees, nMinRelayFee, nAdditionalFee));

    // check vin
    map<uint256, int> mapInAssetId; // all asset id which beyond destory-asset and put-candy txout in vin
    map<uint256, int> mapInAssetId2; // all asset id which comes from put-candy txout in vin
    for(unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const CTxIn& txin = tx.vin[i];
        const CCoins* coins = view.AccessCoins(txin.prevout.hash);
        if(!coins || !coins->IsAvailable(txin.prevout.n))
            return state.Invalid(false, REJECT_DUPLICATE, "3-bad-txns-inputs-spent");
        const CTxOut& txout = coins->vout[txin.prevout.n];

        if(!txout.IsAsset())
            continue;

        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData))
            continue;

        if(header.appId.GetHex() != g_strSafeAssetId)
            return state.DoS(50, false, REJECT_INVALID, "asset_tx: invalid safe-asset app id in header, " + header.appId.GetHex());

        if(header.nAppCmd == ISSUE_ASSET_CMD)
        {
            CAssetData assetData;
            if(!ParseIssueData(vData, assetData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse txin reserve failed");
            const uint256 assetId = assetData.GetHash();
            if(assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: txin asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapInAssetId[assetId]++;
        }
        else if(header.nAppCmd == ADD_ASSET_CMD)
        {
            CCommonData addData;
            if(!ParseCommonData(vData, addData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse add txin reserve failed");
            if(addData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: txin asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapInAssetId[addData.assetId]++;
        }
        else if(header.nAppCmd == TRANSFER_ASSET_CMD)
        {
            CCommonData transferData;
            if(!ParseCommonData(vData, transferData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse transfer txin reserve failed");
            if(transferData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: txin asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapInAssetId[transferData.assetId]++;
        }
        else if(header.nAppCmd == DESTORY_ASSET_CMD)
        {
            return state.DoS(50, false, REJECT_INVALID, "asset_tx: destoried asset cannot be a txin");
        }
        else if(header.nAppCmd == CHANGE_ASSET_CMD)
        {
            CCommonData changeData;
            if(!ParseCommonData(vData, changeData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse change txin reserve failed");
            if(changeData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: txin asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapInAssetId[changeData.assetId]++;
        }
        else if(header.nAppCmd == PUT_CANDY_CMD)
        {
            CPutCandyData putData;
            if(!ParsePutCandyData(vData, putData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse putcandy txin reserve failed");
            if(putData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: txin asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapInAssetId2[putData.assetId]++;
        }
        else if(header.nAppCmd == GET_CANDY_CMD)
        {
            CGetCandyData getData;
            if(!ParseGetCandyData(vData, getData))
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: parse getcandy txin reserve failed");
            if(getData.assetId.IsNull())
                return state.DoS(50, false, REJECT_INVALID, "asset_tx: txin asset id is null, " + strprintf("%s-%d", tx.GetHash().GetHex(), i));
            mapInAssetId[getData.assetId]++;
        }
    }
    if(mapInAssetId.size() > 1 || mapInAssetId2.size() > 1)
        return state.DoS(50, false, REJECT_INVALID, "asset_tx: vin can contain 1 asset only, " + tx.GetHash().GetHex());

    int nTxHeight = GetTxHeight(tx.GetHash());

    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut txout = tx.vout[i];
        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData)) // safe txout
            continue;

        string strAddress = "";
        if(!GetTxOutAddress(txout, &strAddress))
            return state.DoS(10, false, REJECT_INVALID, "invalid txout address, " + txout.ToString());

        if(header.nAppCmd == REGISTER_APP_CMD)
        {
            if(txout.nUnlockedHeight > 0)
                return state.DoS(50, false, REJECT_INVALID, "register_app: conflict with locked txout");

            if(txout.nValue != APP_OUT_VALUE)
                return state.DoS(50, false, REJECT_INVALID, "register_app: invalid txout value");

            CAppData appData;
            string strAdminAddress = "";
            if(!ParseRegisterData(vData, appData, &strAdminAddress))
                return state.DoS(50, false, REJECT_INVALID, "register_app: parse reserve failed");
            if(strAddress != strAdminAddress)
                return state.DoS(50, false, REJECT_INVALID, "register_app: txout address is different from admin address, " + strAddress + " != " + strAdminAddress);

            const CTxIn& txin = tx.vin[0];
            const CTxOut& in_txout = view.GetOutputFor(txin);
            string strInAddress = "";
            if(!GetTxOutAddress(in_txout, &strInAddress))
                return state.DoS(50, false, REJECT_INVALID, "register_app: invalid txin address: " + txin.ToString());
            if(strInAddress != strAdminAddress)
                return state.DoS(50, false, REJECT_INVALID, "register_app: txin address is different from admin address, " + strInAddress + " != " + strAdminAddress);

            if(header.appId != appData.GetHash())
                return state.DoS(50, false, REJECT_INVALID, "register_app: app id is conflicted with app data");
            if(ExistAppId(header.appId, fWithMempool))
                return state.DoS(10, false, REJECT_INVALID, "register_app: existent app, " + header.appId.GetHex());

            if(appData.strAppName.empty() || appData.strAppName.size() > MAX_APPNAME_SIZE || IsKeyWord(appData.strAppName))
                return state.DoS(10, false, REJECT_INVALID, "register_app: invalid app name or contain keyword, " + appData.strAppName);
            if(ExistAppName(appData.strAppName, fWithMempool))
                return state.DoS(10, false, REJECT_INVALID, "register_app: existent app name, " + appData.strAppName);

            if(appData.strAppDesc.empty() || appData.strAppDesc.size() > MAX_APPDESC_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "register_app: invalid app desc");

            if(appData.nDevType < MIN_DEVTYPE_VALUE || appData.nDevType > sporkManager.GetSporkValue(SPORK_101_DEV_TYPE_MAX_VALUE))
                return state.DoS(10, false, REJECT_INVALID, "register_app: invalid developer type, " + appData.nDevType);

            if(appData.strDevName.empty() || appData.strDevName.size() > MAX_DEVNAME_SIZE || IsKeyWord(appData.strDevName))
                return state.DoS(10, false, REJECT_INVALID, "register_app: invalid developer name or contain keyword, " + appData.strDevName);

            if(appData.nDevType == 1) // company
            {
                if(appData.strWebUrl.empty() || appData.strWebUrl.size() > MAX_WEBURL_SIZE || !IsValidUrl(appData.strWebUrl))
                    return state.DoS(10, false, REJECT_INVALID, "register_app: invalid web url, " + appData.strWebUrl);

                if(appData.strLogoUrl.empty() || appData.strLogoUrl.size() > MAX_LOGOURL_SIZE || !IsValidUrl(appData.strLogoUrl))
                    return state.DoS(10, false, REJECT_INVALID, "register_app: invalid app logo url, " + appData.strLogoUrl);

                if(appData.strCoverUrl.empty() || appData.strCoverUrl.size() > MAX_COVERURL_SIZE || !IsValidUrl(appData.strCoverUrl))
                    return state.DoS(10, false, REJECT_INVALID, "register_app: invalid app cover url, " + appData.strCoverUrl);
            }
            else
            {
                if(!appData.strWebUrl.empty())
                    return state.DoS(10, false, REJECT_INVALID, "register_app: personal web url must be empty, " + appData.strWebUrl);

                if(!appData.strLogoUrl.empty())
                    return state.DoS(10, false, REJECT_INVALID, "register_app: personal app logo url, " + appData.strLogoUrl);

                if(!appData.strCoverUrl.empty())
                    return state.DoS(10, false, REJECT_INVALID, "register_app: personal app cover url, " + appData.strCoverUrl);
            }

            // check other txout and cancelled txout
            CAmount nCancelledAmount = 0;
            unsigned int nCount = 0;
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                const CTxOut& tempTxOut = tx.vout[m];
                uint32_t nAppCmd = 0;
                if(!(tempTxOut.IsSafeOnly(&nAppCmd) && nAppCmd != TRANSFER_SAFE_CMD)) // non-simple-safe txout
                    return state.DoS(10, false, REJECT_INVALID, "register_app: tx can contain register txout and simple safe txout only, " + txout.ToString());

                string strTempAddress = "";
                if(!GetTxOutAddress(tempTxOut, &strTempAddress))
                    return state.DoS(10, false, REJECT_INVALID, "register_app: cannot parse txout address, " + txout.ToString());
                if(strTempAddress != g_strCancelledSafeAddress)
                    continue;
                if(++nCount > 1)
                    return state.DoS(10, false, REJECT_INVALID, "register_app: cancel safe repeatedly");
                nCancelledAmount = tempTxOut.nValue;
            }
            if(nCount == 0)
                return state.DoS(50, false, REJECT_INVALID, "register_app: need cancel safe");
            if(!IsCancelledRange(nCancelledAmount) || (fWithMempool && masternodeSync.IsBlockchainSynced() && nCancelledAmount != GetCancelledAmount(nTxHeight)))
                return state.DoS(10, false, REJECT_INVALID, "register_app: invalid safe cancelld amount");

            // check vin
            for(unsigned int m = 0; m < tx.vin.size(); m++)
            {
                const CTxIn& txin = tx.vin[m];
                const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                if (!coins)
                    return state.DoS(10, false, REJECT_INVALID, "register_app: missing txin, " + txin.ToString());

                const CTxOut& in_txout = coins->vout[txin.prevout.n];
                if(in_txout.IsAsset()) // forbid asset txin
                    return state.DoS(50, false, REJECT_INVALID, "register_app: txin cannot be asset txout, " + txin.ToString());
            }
        }
        else if(header.nAppCmd == ADD_AUTH_CMD || header.nAppCmd == DELETE_AUTH_CMD)
        {
            if(txout.nUnlockedHeight > 0)
                return state.DoS(50, false, REJECT_INVALID, "set_auth: conflict with locked txout");

            if(txout.nValue != APP_OUT_VALUE)
                return state.DoS(50, false, REJECT_INVALID, "set_auth: invalid safe amount");

            CAppId_AppInfo_IndexValue appInfo;
            if(!GetAppInfoByAppId(header.appId, appInfo, false))
                return state.DoS(10, false, REJECT_INVALID, "set_auth: non-existent app");
            string strAdminAddress = appInfo.strAdminAddress;
            if(strAddress != strAdminAddress)
                return state.DoS(50, false, REJECT_INVALID, "set_auth: txout address is different from admin address" + strAddress + " != " + strAdminAddress);

            CAuthData authData;
            if(!ParseAuthData(vData, authData))
                return state.DoS(50, false, REJECT_INVALID, "set_auth: parse reserve failed");

            if(authData.nSetType < MIN_SETTYPE_VALUE || authData.nSetType > sporkManager.GetSporkValue(SPORK_102_SET_TYPE_MAX_VALUE))
                return state.DoS(10, false, REJECT_INVALID, "set_auth: invalid set type");
            if((authData.nSetType == 1 && header.nAppCmd != ADD_AUTH_CMD) || (authData.nSetType == 2 && header.nAppCmd != DELETE_AUTH_CMD))
                return state.DoS(10, false, REJECT_INVALID, "set_auth: set type is conflicted with app cmd");

            if(authData.strUserAddress != "ALL_USER" && !CBitcoinAddress(authData.strUserAddress).IsValid())
                return state.DoS(10, false, REJECT_INVALID, "set_auth: invalid user address");
            if(authData.strUserAddress == strAdminAddress)
                return state.DoS(10, false, REJECT_INVALID, "set_auth: admin don't need to set auth");

            if(fWithMempool)
            {
                vector<uint32_t> vMempoolAuth;
                if(GetAuthByAppIdAddressFromMempool(header.appId, authData.strUserAddress, vMempoolAuth))
                {
                    if(authData.nAuth == 0 || authData.nAuth == 1)
                    {
                        if(vMempoolAuth.end() != find(vMempoolAuth.begin(), vMempoolAuth.end(), 0) || vMempoolAuth.end() != find(vMempoolAuth.begin(), vMempoolAuth.end(), 1))
                            return state.DoS(10, false, REJECT_INVALID, "set_auth: don't set auth(0 or 1) repeatedlly util next block is comming");
                    }
                    else
                    {
                        if(vMempoolAuth.end() != find(vMempoolAuth.begin(), vMempoolAuth.end(), authData.nAuth))
                            return state.DoS(10, false, REJECT_INVALID, "set_auth: don't set auth repeatedlly util next block is comming");
                    }
                }
            }

            map<uint32_t, int> mapAuth;
            GetAuthByAppIdAddress(header.appId, authData.strUserAddress, mapAuth);

            bool fExist = false;
            if(!fWithMempool)
            {
                for(map<uint32_t, int>::iterator it = mapAuth.begin(); it != mapAuth.end(); it++)
                {
                    if(it->second > g_nChainHeight)
                    {
                        fExist = true;
                        break;
                    }
                }
            }
            if(!fExist)
            {
                if(header.nAppCmd == ADD_AUTH_CMD && mapAuth.count(authData.nAuth) != 0)
                    return state.DoS(10, false, REJECT_INVALID, "set_auth: existent auth, don't need to add it");
                if(header.nAppCmd == DELETE_AUTH_CMD && mapAuth.count(authData.nAuth) == 0)
                    return state.DoS(10, false, REJECT_INVALID, "set_auth: non-existent auth, cann't delete it");
            }

            // check other txout
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                uint32_t nAppCmd = 0;
                if(!(tx.vout[m].IsSafeOnly(&nAppCmd) && nAppCmd != TRANSFER_SAFE_CMD)) // non-simple-safe txout
                    return state.DoS(10, false, REJECT_INVALID, "set_auth: tx can contain set_auth txout and simple safe txout only, " + txout.ToString());
            }

            // check vin
            for(unsigned int m = 0; m < tx.vin.size(); m++)
            {
                const CTxIn& txin = tx.vin[m];
                const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                if (!coins)
                    return state.DoS(10, false, REJECT_INVALID, "set_auth: missing txin, " + txin.ToString());

                const CTxOut& in_txout = coins->vout[txin.prevout.n];
                if(in_txout.IsAsset()) // forbid asset txin
                    return state.DoS(50, false, REJECT_INVALID, "set_auth: txin cannot be asset txout, " + txin.ToString());

                string strInAddress = "";
                if(!GetTxOutAddress(in_txout, &strInAddress))
                    return state.DoS(10, false, REJECT_INVALID, "set_auth: cannot parse txin address");
                if(strInAddress != strAdminAddress)
                    return state.DoS(10, false, REJECT_INVALID, "set_auth: txin address is different from admin address, " + strInAddress + " != " + strAdminAddress);
            }
        }
        else if(header.nAppCmd == CREATE_EXTEND_TX_CMD)
        {
            if(txout.nUnlockedHeight > 0)
                return state.DoS(50, false, REJECT_INVALID, "extenddata: conflict with locked txout");

            if(txout.nValue != APP_OUT_VALUE)
                return state.DoS(50, false, REJECT_INVALID, "extenddata: invalid safe amount");

            CAppId_AppInfo_IndexValue appInfo;
            if(!GetAppInfoByAppId(header.appId, appInfo, false))
                return state.DoS(10, false, REJECT_INVALID, "extenddata: non-existent app");

            CExtendData extendData;
            if(!ParseExtendData(vData, extendData))
                return state.DoS(50, false, REJECT_INVALID, "extenddata: parse reserve failed");

            if(extendData.nAuth < MIN_AUTH_VALUE)
                return state.DoS(10, false, REJECT_INVALID, "extenddata: invalid auth");

            if(strAddress != appInfo.strAdminAddress) // non-admin need check auth
            {
                if(fWithMempool)
                {
                    vector<uint32_t> vMempoolAllUserAuth;
                    if(GetAuthByAppIdAddressFromMempool(header.appId, "ALL_USER", vMempoolAllUserAuth))
                    {
                        if(find(vMempoolAllUserAuth.begin(), vMempoolAllUserAuth.end(), 0) != vMempoolAllUserAuth.end())
                            return state.DoS(10, false, REJECT_INVALID, "extenddata: all users's permission (0) are unconfirmed in memory pool");
                        if(find(vMempoolAllUserAuth.begin(), vMempoolAllUserAuth.end(), 1) != vMempoolAllUserAuth.end())
                            return state.DoS(10, false, REJECT_INVALID, "extenddata: all users's permission (1) are unconfirmed in memory pool");
                        if(find(vMempoolAllUserAuth.begin(), vMempoolAllUserAuth.end(), extendData.nAuth) != vMempoolAllUserAuth.end())
                            return state.DoS(10, false, REJECT_INVALID, strprintf("extenddata: all users's permission (%u) are unconfirmed in memory pool", extendData.nAuth));
                    }
                    vector<uint32_t> vMempoolUserAuth;
                    if(GetAuthByAppIdAddressFromMempool(header.appId, strAddress, vMempoolUserAuth))
                    {
                        if(find(vMempoolUserAuth.begin(), vMempoolUserAuth.end(), 0) != vMempoolUserAuth.end())
                            return state.DoS(10, false, REJECT_INVALID, "extenddata: current user's permission (0) are unconfirmed in memory pool");
                        if(find(vMempoolUserAuth.begin(), vMempoolUserAuth.end(), 1) != vMempoolUserAuth.end())
                            return state.DoS(10, false, REJECT_INVALID, "extenddata: current user's permission (1) are unconfirmed in memory pool");
                        if(find(vMempoolUserAuth.begin(), vMempoolUserAuth.end(), extendData.nAuth) != vMempoolUserAuth.end())
                            return state.DoS(10, false, REJECT_INVALID, strprintf("extenddata: current user's permission (%u) are unconfirmed in memory pool", extendData.nAuth));
                    }
                }

                map<uint32_t, int> mapAllUserAuth;
                GetAuthByAppIdAddress(header.appId, "ALL_USER", mapAllUserAuth);

                map<uint32_t, int> mapUserAuth;
                GetAuthByAppIdAddress(header.appId, strAddress, mapUserAuth);

                bool fExist = false;
                if(!fWithMempool)
                {
                    for(map<uint32_t, int>::iterator it = mapAllUserAuth.begin(); it != mapAllUserAuth.end(); it++)
                    {
                        if(it->second > g_nChainHeight)
                        {
                            fExist = true;
                            break;
                        }
                    }

                    if(!fExist)
                    {
                        for(map<uint32_t, int>::iterator it = mapUserAuth.begin(); it != mapUserAuth.end(); it++)
                        {
                            if(it->second > g_nChainHeight)
                            {
                                fExist = true;
                                break;
                            }
                        }
                    }
                }

                if(!fExist)
                {
                    if(mapAllUserAuth.count(0) != 0)
                        return state.DoS(10, false, REJECT_INVALID, "extenddata: all users's all permissions are denied");
                    else
                    {
                        if(mapAllUserAuth.count(1) == 0)
                        {
                            if(mapUserAuth.count(0) != 0)
                                return state.DoS(10, false, REJECT_INVALID, "extenddata: current user's all permissions are denied");
                            else
                            {
                                if(mapUserAuth.count(1) == 0)
                                {
                                    if(mapAllUserAuth.count(extendData.nAuth) == 0)
                                    {
                                        if(mapUserAuth.count(extendData.nAuth) == 0)
                                            return state.DoS(10, false, REJECT_INVALID, "extenddata: current user's permission is denied");
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if(extendData.strExtendData.empty() || extendData.strExtendData.size() > MAX_EXTENDDATAT_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "extenddata: invalid extend data");

            // check other txout
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                uint32_t nAppCmd = 0;
                if(!(tx.vout[m].IsSafeOnly(&nAppCmd) && nAppCmd != TRANSFER_SAFE_CMD)) // non-simple-safe txout
                    return state.DoS(10, false, REJECT_INVALID, "extenddata: tx can contain extenddata txout and simple safe txout only, " + txout.ToString());
            }

            // check vin
            for(unsigned int m = 0; m < tx.vin.size(); m++)
            {
                const CTxIn& txin = tx.vin[m];
                const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                if (!coins)
                    return state.DoS(10, false, REJECT_INVALID, "extenddata: missing txin, " + txin.ToString());

                const CTxOut& in_txout = coins->vout[txin.prevout.n];
                if(in_txout.IsAsset()) // forbid asset txin
                    return state.DoS(50, false, REJECT_INVALID, "extenddata: txin cannot be asset txout, " + txin.ToString());

                string strInAddress = "";
                if(!GetTxOutAddress(in_txout, &strInAddress))
                    return state.DoS(10, false, REJECT_INVALID, "extenddata: cannot parse txin address");
                if(strInAddress != strAddress)
                    return state.DoS(10, false, REJECT_INVALID, "extenddata: txin address is different from txout address, " + strInAddress + " != " + strAddress);
            }
        }
        else if(header.nAppCmd == ISSUE_ASSET_CMD)
        {
            if(txout.nUnlockedHeight > 0)
                return state.DoS(50, false, REJECT_INVALID, "issue_asset: conflict with locked txout");

            if(header.appId.GetHex() != g_strSafeAssetId)
                return state.DoS(50, false, REJECT_INVALID, "issue_asset: invalid safe-asset app id in header, " + header.appId.GetHex());

            const CTxIn& txin = tx.vin[0];
            const CTxOut& in_txout = view.GetOutputFor(txin);
            string strInAddress = "";
            if(!GetTxOutAddress(in_txout, &strInAddress))
                return state.DoS(50, false, REJECT_INVALID, "issue_asset: cannot parse txin address, " + txin.ToString());
            if(strInAddress != strAddress)
                return state.DoS(50, false, REJECT_INVALID, "issue_asset: txin address is different from txout address, " + strInAddress + " != " + strAddress);

            CAssetData assetData;
            if(!ParseIssueData(vData, assetData))
                return state.DoS(50, false, REJECT_INVALID, "issue_asset: parse reserve failed");

            uint256 assetId = assetData.GetHash();
            if(ExistAssetId(assetId, fWithMempool))
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: existent asset");

            if(assetData.strShortName.empty() || assetData.strShortName.size() > MAX_SHORTNAME_SIZE || IsKeyWord(assetData.strShortName) || IsContainSpace(assetData.strShortName))
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid asset short name");
            if(ExistShortName(assetData.strShortName, fWithMempool))
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: existent asset short name");

            if(assetData.strAssetName.empty() || assetData.strAssetName.size() > MAX_ASSETNAME_SIZE || IsKeyWord(assetData.strAssetName))
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid asset name");
            if(ExistAssetName(assetData.strAssetName, fWithMempool))
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: existent asset name");

            if(assetData.strAssetDesc.empty() || assetData.strAssetDesc.size() > MAX_ASSETDESC_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid asset description");

            if(assetData.strAssetUnit.empty() || assetData.strAssetUnit.size() > MAX_ASSETUNIT_SIZE || IsKeyWord(assetData.strAssetUnit))
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid asset unit");

            if(assetData.nTotalAmount > MAX_ASSETS)
                return state.DoS(10, false, REJECT_INVALID, strprintf("issue_asset: invalid asset total amount (min: 100, max: %lld)", MAX_ASSETS / pow(10, assetData.nDecimals)));

            if(assetData.nFirstIssueAmount <= 0 || assetData.nFirstIssueAmount > assetData.nTotalAmount)
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid asset first issue amount");

            if(assetData.nFirstActualAmount < AmountFromValue("100", assetData.nDecimals, true) || assetData.nFirstActualAmount > assetData.nFirstIssueAmount)
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid asset first actual amount (min: 100)");

            if(assetData.nDecimals < MIN_ASSETDECIMALS_VALUE || assetData.nDecimals > MAX_ASSETDECIMALS_VALUE)
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid asset decimals");

            if(assetData.bPayCandy)
            {
                char totalAmountStr[64] = "",candyAmountStr[64]="";
                snprintf(totalAmountStr,sizeof(totalAmountStr),"%lld",assetData.nTotalAmount);
                snprintf(candyAmountStr,sizeof(candyAmountStr),"%lld",assetData.nCandyAmount);
                string candyMinStr = numtofloatstring(totalAmountStr,3); // 1‰
                string candyMaxStr = numtofloatstring(totalAmountStr,1); // 10%
                if(compareFloatString(candyAmountStr,candyMinStr)<0 || compareFloatString(candyAmountStr,candyMaxStr)>0)
                    return state.DoS(10, false, REJECT_INVALID, "issue_asset: candy amount out of range (min: 0.001 * total, max: 0.1 * total)");
                if(assetData.nCandyAmount >= assetData.nFirstIssueAmount)
                    return state.DoS(10, false, REJECT_INVALID, "issue_asset: candy amount exceed first issue amount");
                if(assetData.nCandyAmount != assetData.nFirstIssueAmount - assetData.nFirstActualAmount)
                    return state.DoS(10, false, REJECT_INVALID, "issue_asset: candy amount is conflicted with first issue amount and first actual amount");
                if(assetData.nCandyExpired < MIN_CANDYEXPIRED_VALUE || assetData.nCandyExpired > MAX_CANDYEXPIRED_VALUE)
                    return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid candy expired (min: 1, max: 6)");
            }
            else
            {
                if(assetData.nCandyAmount != 0)
                    return state.DoS(10, false, REJECT_INVALID, "issue_asset: candy is disabled, don't need candy amount");
                if(assetData.nCandyExpired != 0)
                    return state.DoS(10, false, REJECT_INVALID, "issue_asset: candy is disabled, don't need candy expired");
            }

            if(assetData.strRemarks.size() > MAX_REMARKS_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid remarks");

            if(view.GetValueIn(tx, true) != 0)
                return state.DoS(50, false, REJECT_INVALID, "issue_asset: vin asset amount must be 0");

            // check other txout and cancelled txout
            CAmount nCancelledAmount = 0;
            unsigned int nCancelledCount = 0;
            unsigned int nCandyCount = 0;
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                const CTxOut& tempTxOut = tx.vout[m];
                uint32_t nAppCmd = 0;
                if(!tempTxOut.IsSafeOnly(&nAppCmd)) // non-safe txout
                {
                    if(nAppCmd == PUT_CANDY_CMD) // put candy txout
                    {
                        if(assetData.bPayCandy)
                        {
                            if(++nCandyCount > 1)
                                return state.DoS(10, false, REJECT_INVALID, "issue_asset: put candy txout repeatedly");
                            if(assetData.nCandyAmount != tempTxOut.nValue)
                                return state.DoS(10, false, REJECT_INVALID, "issue_asset: candy amount is different put candy txout value");
                        }
                    }
                    else
                        return state.DoS(10, false, REJECT_INVALID, "issue_asset: tx can contain issue txout, put candy txout and simple safe txout only, " + txout.ToString());
                }
                else
                {
                    if(nAppCmd == TRANSFER_SAFE_CMD) // complex-safe txout
                        return state.DoS(10, false, REJECT_INVALID, "issue_asset: tx cannot contain complex safe txout, " + txout.ToString());

                    string strTempAddress = "";
                    if(!GetTxOutAddress(tempTxOut, &strTempAddress))
                        return state.DoS(10, false, REJECT_INVALID, "issue_asset: cannot parse txout address, " + txout.ToString());
                    if(strTempAddress != g_strCancelledSafeAddress)
                        continue;
                    if(++nCancelledCount > 1)
                        return state.DoS(10, false, REJECT_INVALID, "issue_asset: cancel safe repeatedly");
                    nCancelledAmount = tempTxOut.nValue;
                }
            }
            if(nCancelledCount == 0)
                return state.DoS(50, false, REJECT_INVALID, "issue_asset: need cancel safe");
            if(!IsCancelledRange(nCancelledAmount) || (fWithMempool && masternodeSync.IsBlockchainSynced() && nCancelledAmount != GetCancelledAmount(nTxHeight)))
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: invalid safe cancelld amount");
            if(assetData.bPayCandy && nCandyCount == 0)
                return state.DoS(10, false, REJECT_INVALID, "issue_asset: tx need put candy txout when paycandy is opened");

            // check vin
            for(unsigned int m = 0; m < tx.vin.size(); m++)
            {
                const CTxIn& txin = tx.vin[m];
                const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                if (!coins)
                    return state.DoS(10, false, REJECT_INVALID, "issue_asset: missing txin, " + txin.ToString());

                const CTxOut& in_txout = coins->vout[txin.prevout.n];
                if(in_txout.IsAsset()) // forbid asset txin
                    return state.DoS(50, false, REJECT_INVALID, "issue_asset: txin cannot be asset txout, " + txin.ToString());
            }
        }
        else if(header.nAppCmd == ADD_ASSET_CMD)
        {
            if(txout.nUnlockedHeight > 0)
                return state.DoS(50, false, REJECT_INVALID, "add_asset: conflict with locked txout");

            if(header.appId.GetHex() != g_strSafeAssetId)
                return state.DoS(50, false, REJECT_INVALID, "add_asset: invalid safe-asset app id in header, " + header.appId.GetHex());

            CCommonData addData;
            if(!ParseCommonData(vData, addData))
                return state.DoS(50, false, REJECT_INVALID, "add_asset: parse reserve failed");

            CAssetId_AssetInfo_IndexValue assetInfo;
            if(!GetAssetInfoByAssetId(addData.assetId, assetInfo, false))
                return state.DoS(10, false, REJECT_INVALID, "add_asset: non-existent asset");
            string strAdminAddress = assetInfo.strAdminAddress;
            if(strAddress != strAdminAddress)
                return state.DoS(10, false, REJECT_INVALID, "add_asset: txout address is different from admin address, " + strAddress + " != " + strAdminAddress);

            if(addData.nAmount <= 0 || addData.nAmount > MAX_ASSETS)
                return state.DoS(10, false, REJECT_INVALID, "add_asset: invalid add amount");
            if(assetInfo.assetData.nTotalAmount - assetInfo.assetData.nFirstIssueAmount < addData.nAmount + GetAddedAmountByAssetId(addData.assetId, fWithMempool))
                return state.DoS(10, false, REJECT_INVALID, "add_asset: added amount exceed total amount");

            if(addData.strRemarks.size() > MAX_REMARKS_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "add_asset: invalid remarks");

            if(view.GetValueIn(tx, true) != 0)
                return state.DoS(50, false, REJECT_INVALID, "add_asset: vin asset amount must be 0");

            // check other txout
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                uint32_t nAppCmd = 0;
                if(!(tx.vout[m].IsSafeOnly(&nAppCmd) && nAppCmd != TRANSFER_SAFE_CMD)) // non-simple-safe txout
                    return state.DoS(10, false, REJECT_INVALID, "add_asset: tx can contain add txout and simple safe txout only, " + txout.ToString());
            }

            // check vin
            for(unsigned int m = 0; m < tx.vin.size(); m++)
            {
                const CTxIn& txin = tx.vin[m];
                const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                if (!coins)
                    return state.DoS(10, false, REJECT_INVALID, "add_asset: missing txin, " + txin.ToString());

                const CTxOut& in_txout = coins->vout[txin.prevout.n];
                if(in_txout.IsAsset()) // forbid asset txin
                    return state.DoS(50, false, REJECT_INVALID, "add_asset: txin cannot be asset txout, " + txin.ToString());

                string strInAddress = "";
                if(!GetTxOutAddress(in_txout, &strInAddress))
                    return state.DoS(10, false, REJECT_INVALID, "add_asset: cannot parse txin address");
                if(strInAddress != strAdminAddress)
                    return state.DoS(10, false, REJECT_INVALID, "add_asset: txin address is different from admin address, " + strInAddress + " != " + strAdminAddress);
            }
        }
        else if(header.nAppCmd == TRANSFER_ASSET_CMD)
        {
            if(header.appId.GetHex() != g_strSafeAssetId)
                return state.DoS(50, false, REJECT_INVALID, "transfer_asset: invalid safe-asset app id in header, " + header.appId.GetHex());

            CCommonData transferData;
            if(!ParseCommonData(vData, transferData))
                return state.DoS(50, false, REJECT_INVALID, "transfer_asset: parse reserve failed");

            CAssetId_AssetInfo_IndexValue assetInfo;
            if(!GetAssetInfoByAssetId(transferData.assetId, assetInfo, false))
                return state.DoS(10, false, REJECT_INVALID, "transfer_asset: non-existent asset");

            if(transferData.nAmount <= 0 || transferData.nAmount > MAX_ASSETS)
                return state.DoS(10, false, REJECT_INVALID, "transfer_asset: invalid transfer amount");

            if(transferData.strRemarks.size() > MAX_REMARKS_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "transfer_asset: invalid remarks");

            if(view.GetValueIn(tx, true) != tx.GetValueOut(true))
                return state.DoS(50, false, REJECT_INVALID, "transfer_asset: vin amount is different from vout amount");

            // check other txout
            unsigned int nChangeCount = 0;
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                const CTxOut& tempTxOut = tx.vout[m];
                uint32_t nAppCmd = 0;
                if(!tempTxOut.IsSafeOnly(&nAppCmd)) // non-safe txout
                {
                    if(nAppCmd == CHANGE_ASSET_CMD) // change asset txout
                    {
                        if(++nChangeCount > 1)
                            return state.DoS(10, false, REJECT_INVALID, "transfer_asset: change txout repeatedly");
                    }
                    else if(nAppCmd != TRANSFER_ASSET_CMD)
                        return state.DoS(10, false, REJECT_INVALID, "transfer_asset: tx can contain transfer txout, change asset and simple safe txout only, " + txout.ToString());
                }
                else
                {
                    if(nAppCmd == TRANSFER_SAFE_CMD) // complex-safe txout
                        return state.DoS(10, false, REJECT_INVALID, "transfer_asset: tx cannot contain complex safe txout, " + txout.ToString());
                }
            }

            // check vin
            if(mapInAssetId.size() != 1 || mapInAssetId.begin()->first != mapAssetId.begin()->first)
                return state.DoS(50, false, REJECT_INVALID, "transfer_asset: asset id must be same between vin and vout");
            if(!mapInAssetId2.empty())
                return state.DoS(50, false, REJECT_INVALID, "transfer_asset: vin cannot be destory-asset and put-candy txout");
        }
        else if(header.nAppCmd == DESTORY_ASSET_CMD)
        {
            if(txout.nUnlockedHeight > 0)
                return state.DoS(50, false, REJECT_INVALID, "destory_asset: conflict with locked txout");

            if(header.appId.GetHex() != g_strSafeAssetId)
                return state.DoS(50, false, REJECT_INVALID, "destory_asset: invalid safe-asset app id in header, " + header.appId.GetHex());

            if(strAddress != g_strCancelledAssetAddress)
                return state.DoS(50, false, REJECT_INVALID, "destory_asset: invalid asset cancelled address");

            CCommonData destoryData;
            if(!ParseCommonData(vData, destoryData))
                return state.DoS(50, false, REJECT_INVALID, "destory_asset: parse reserve failed");

            CAssetId_AssetInfo_IndexValue assetInfo;
            if(!GetAssetInfoByAssetId(destoryData.assetId, assetInfo, false))
                return state.DoS(10, false, REJECT_INVALID, "destory_asset: non-existent asset");

            if(!assetInfo.assetData.bDestory)
                return state.DoS(10, false, REJECT_INVALID, "destory_asset: disable to destory asset");

            if(destoryData.nAmount <= 0 || destoryData.nAmount > MAX_ASSETS)
                return state.DoS(10, false, REJECT_INVALID, "destory_asset: invalid destory amount");

            if(destoryData.strRemarks.size() > MAX_REMARKS_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "destory_asset: invalid remarks");

            if(view.GetValueIn(tx, true) != tx.GetValueOut(true))
                return state.DoS(50, false, REJECT_INVALID, "destory_asset: vin amount is different from vout amount");

            // check other txout
            unsigned int nChangeCount = 0;
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                const CTxOut& tempTxOut = tx.vout[m];
                uint32_t nAppCmd = 0;
                if(!tempTxOut.IsSafeOnly(&nAppCmd)) // non-safe txout
                {
                    if(nAppCmd == CHANGE_ASSET_CMD) // change asset txout
                    {
                        if(++nChangeCount > 1)
                            return state.DoS(10, false, REJECT_INVALID, "destory_asset: change txout repeatedly");
                    }
                    else
                        return state.DoS(10, false, REJECT_INVALID, "destory_asset: tx can contain destory txout, change txout and simple safe txout only, " + txout.ToString());
                }
                else
                {
                    if(nAppCmd == TRANSFER_SAFE_CMD) // complex-safe txout
                        return state.DoS(10, false, REJECT_INVALID, "destory_asset: tx cannot contain complex safe txout, " + txout.ToString());
                }
            }

            // check vin
            if(mapInAssetId.size() != 1 || mapInAssetId.begin()->first != mapAssetId.begin()->first)
                return state.DoS(50, false, REJECT_INVALID, "destory_asset: asset id must be same between vin and vout");
            if(!mapInAssetId2.empty())
                return state.DoS(50, false, REJECT_INVALID, "destory_asset: vin cannot be destory-asset and put-candy txout");
        }
        else if(header.nAppCmd == CHANGE_ASSET_CMD)
        {
            if(txout.nUnlockedHeight > 0)
                return state.DoS(50, false, REJECT_INVALID, "change_asset: conflict with locked txout");

            if(header.appId.GetHex() != g_strSafeAssetId)
                return state.DoS(50, false, REJECT_INVALID, "change_asset: invalid safe-asset app id in header, " + header.appId.GetHex());

            CCommonData changeData;
            if(!ParseCommonData(vData, changeData))
                return state.DoS(50, false, REJECT_INVALID, "change_asset: parse reserve failed");

            CAssetId_AssetInfo_IndexValue assetInfo;
            if(!GetAssetInfoByAssetId(changeData.assetId, assetInfo, false))
                return state.DoS(10, false, REJECT_INVALID, "change_asset: non-existent asset");

            if(changeData.nAmount <= 0 || changeData.nAmount > MAX_ASSETS)
                return state.DoS(10, false, REJECT_INVALID, "change_asset: invalid destory amount");

            if(changeData.strRemarks.size() > MAX_REMARKS_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "change_asset: invalid remarks");

            if(view.GetValueIn(tx, true) != tx.GetValueOut(true))
                return state.DoS(50, false, REJECT_INVALID, "change_asset: vin amount is different from vout amount");

            // check other txout
            unsigned int nCount = 0;
            unsigned int nTransferCount = 0;
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                const CTxOut& tempTxOut = tx.vout[m];
                uint32_t nAppCmd = 0;
                if(!tempTxOut.IsSafeOnly(&nAppCmd)) // non-safe txout
                {
                    if(nAppCmd == DESTORY_ASSET_CMD || nAppCmd == PUT_CANDY_CMD) // destory txout or put candy txout
                    {
                        if(++nCount > 1)
                            return state.DoS(10, false, REJECT_INVALID, "change_asset: invalid other txouts");
                    }
                    else if(nAppCmd == TRANSFER_ASSET_CMD)
                    {
                        nTransferCount++;
                    }
                    else
                        return state.DoS(10, false, REJECT_INVALID, "change_asset: tx can contain change txout, transfer/destory/putcandy txout and simple safe txout only, " + txout.ToString());
                }
                else
                {
                    if(nAppCmd == TRANSFER_SAFE_CMD) // complex-safe txout
                        return state.DoS(10, false, REJECT_INVALID, "change_asset: tx cannot contain complex safe txout, " + txout.ToString());
                }
            }
            if(nCount == 0 && nTransferCount == 0)
                return state.DoS(50, false, REJECT_INVALID, "change_asset: tx must contain transfer/destory/putcandy txout at least, " + txout.ToString());
            else if(nCount != 0 && nTransferCount != 0)
                return state.DoS(50, false, REJECT_INVALID, "change_asset: tx contain change txout, transfer txout and destory/putcandy txout in same time");

            // check vin
            if(mapInAssetId.size() != 1 || mapInAssetId.begin()->first != mapAssetId.begin()->first)
                return state.DoS(50, false, REJECT_INVALID, "change_asset: asset id must be same between vin and vout");
            if(!mapInAssetId2.empty())
                return state.DoS(50, false, REJECT_INVALID, "change_asset: vin cannot be destory-asset and put-candy txout");
        }
        else if(header.nAppCmd == PUT_CANDY_CMD)
        {
            if(txout.nUnlockedHeight > 0)
                return state.DoS(50, false, REJECT_INVALID, "put_candy: conflict with locked txout");

            if(header.appId.GetHex() != g_strSafeAssetId)
                return state.DoS(50, false, REJECT_INVALID, "put_candy: invalid safe-asset app id in header, " + header.appId.GetHex());

            if(strAddress != g_strPutCandyAddress)
                return state.DoS(10, false, REJECT_INVALID, "put_candy: invalid candy put address, " + strAddress);

            CPutCandyData candyData;
            if(!ParsePutCandyData(vData, candyData))
                return state.DoS(50, false, REJECT_INVALID, "put_candy: parse reserve failed");

            if(candyData.nExpired < MIN_CANDYEXPIRED_VALUE || candyData.nExpired > MAX_CANDYEXPIRED_VALUE)
                return state.DoS(10, false, REJECT_INVALID, "put_candy: invalid candy expired");

            if(candyData.strRemarks.size() > MAX_REMARKS_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "put_candy: invalid remarks");

            int dbPutCandyCount = 0;
            map<COutPoint, CCandyInfo> mapCandyInfo;
            if(GetAssetIdCandyInfo(candyData.assetId, mapCandyInfo))
                dbPutCandyCount = mapCandyInfo.size();
            int putCandyCount = dbPutCandyCount;
            if(fWithMempool)
                putCandyCount += mempool.get_PutCandy_count(candyData.assetId);
            if(putCandyCount>=MAX_PUTCANDY_VALUE)
                return state.DoS(10, false, REJECT_INVALID, "put_candy: put candy times used up");

            // check other txout
            bool fWithIssue = false;
            CAssetData assetData;
            string strAdminAddress = "";
            unsigned int nCount = 0;
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                const CTxOut& tempTxOut = tx.vout[m];
                uint32_t nAppCmd = 0;
                if(!tempTxOut.IsSafeOnly(&nAppCmd)) // non-safe txout
                {
                    if(nAppCmd == ISSUE_ASSET_CMD)
                    {
                        CAppHeader tempHeader;
                        vector<unsigned char> vTempData;
                        if(!ParseReserve(tempTxOut.vReserve, tempHeader, vTempData))
                            return state.DoS(50, false, REJECT_INVALID, "put_candy: parse issue txout failed");
                        if(tempHeader.nAppCmd != nAppCmd)
                            return state.DoS(50, false, REJECT_INVALID, "put_candy: app cmd is conflicted");
                        if(!ParseIssueData(vTempData, assetData))
                            return state.DoS(50, false, REJECT_INVALID, "put_candy: parse issue reserve failed");
                        fWithIssue = true;
                        if(++nCount > 1)
                            return state.DoS(10, false, REJECT_INVALID, "put_candy: repeated issue txout or change txout");
                        if(!GetTxOutAddress(tempTxOut, &strAdminAddress))
                            return state.DoS(10, false, REJECT_INVALID, "put_candy: cannot parse issue txout address");
                    }
                    else if(nAppCmd == CHANGE_ASSET_CMD)
                    {
                        if(++nCount > 1)
                            return state.DoS(10, false, REJECT_INVALID, "put_candy: repeated change txout or issue txout");
                    }
                    else
                        return state.DoS(10, false, REJECT_INVALID, "put_candy: tx can contain put candy txout, issue/change txout and simple safe txout only, " + txout.ToString());
                }
                else
                {
                    if(nAppCmd == TRANSFER_SAFE_CMD) // complex-safe txout
                        return state.DoS(10, false, REJECT_INVALID, "put_candy: tx cannot contain complex safe txout, " + txout.ToString());
                }
            }

            if(fWithIssue)
            {
                if(!assetData.bPayCandy)
                    return state.DoS(10, false, REJECT_INVALID, "put_candy: put candy is closed when issue asset");

                if(candyData.nAmount != assetData.nCandyAmount)
                    return state.DoS(10, false, REJECT_INVALID, "put_candy: candy amount is different from candy amount of asset data");

                char totalAmountStr[64] = "",candyAmountStr[64]="";
                snprintf(totalAmountStr,sizeof(totalAmountStr),"%lld",assetData.nTotalAmount);
                snprintf(candyAmountStr,sizeof(candyAmountStr),"%lld",assetData.nCandyAmount);
                string candyMinStr = numtofloatstring(totalAmountStr,3); // 1‰
                string candyMaxStr = numtofloatstring(totalAmountStr,1); // 10%
                if(compareFloatString(candyAmountStr,candyMinStr)<0 || compareFloatString(candyAmountStr,candyMaxStr)>0)
                    return state.DoS(10, false, REJECT_INVALID, "put_candy: candy amount out of range (min: 0.001 * total, max: 0.1 * total)");

                if(candyData.nAmount != assetData.nFirstIssueAmount - assetData.nFirstActualAmount)
                    return state.DoS(10, false, REJECT_INVALID, "put_candy: candy amount is conflicted with first issue amount and first actual amount");

                if(candyData.nExpired != assetData.nCandyExpired)
                    return state.DoS(10, false, REJECT_INVALID, "put_candy: candy expired is different from candy expired of asset data");
            }
            else
            {
                CAssetId_AssetInfo_IndexValue assetInfo;
                if(!GetAssetInfoByAssetId(candyData.assetId, assetInfo, false))
                    return state.DoS(10, false, REJECT_INVALID, "put_candy: non-existent asset");

                char totalAmountStr[64] = "",candyAmountStr[64]="";
                snprintf(totalAmountStr,sizeof(totalAmountStr),"%lld",assetInfo.assetData.nTotalAmount);
                snprintf(candyAmountStr,sizeof(candyAmountStr),"%lld",candyData.nAmount);
                string candyMinStr = numtofloatstring(totalAmountStr,3); // 1‰
                string candyMaxStr = numtofloatstring(totalAmountStr,1); // 10%
                if(compareFloatString(candyAmountStr,candyMinStr)<0||compareFloatString(candyAmountStr,candyMaxStr)>0)
                    return state.DoS(10, false, REJECT_INVALID, "put_candy: candy amount out of range (min: 0.001 * total, max: 0.1 * total)");

                strAdminAddress = assetInfo.strAdminAddress;

                if(view.GetValueIn(tx, true) != tx.GetValueOut(true))
                    return state.DoS(50, false, REJECT_INVALID, "put_candy: vin amount is different from vout amount");
            }

            // check vin
            if(!fWithIssue && (mapInAssetId.size() != 1 || mapInAssetId.begin()->first != mapAssetId.begin()->first))
                return state.DoS(50, false, REJECT_INVALID, "put_candy: asset id must be same between vin and vout");
            for(unsigned int m = 0; m < tx.vin.size(); m++)
            {
                const CTxIn& txin = tx.vin[m];
                const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                if (!coins)
                    return state.DoS(10, false, REJECT_INVALID, "put_candy: missing txin, " + txin.ToString());

                const CTxOut& in_txout = coins->vout[txin.prevout.n];
                uint32_t nAppCmd = 0;
                if(in_txout.IsAsset(&nAppCmd)) // forbid invalid asset txin
                {
                    if(fWithIssue)
                        return state.DoS(10, false, REJECT_INVALID, "put_candy: txin must be safe txout when tx is an issue record, " + txin.ToString());

                    if(nAppCmd == DESTORY_ASSET_CMD || nAppCmd == PUT_CANDY_CMD) // invalid asset txin
                        return state.DoS(50, false, REJECT_INVALID, "put_candy: txin cannot be destory-asset and put-candy txout, " + txin.ToString());

                    string strInAddress = "";
                    if(!GetTxOutAddress(in_txout, &strInAddress))
                        return state.DoS(10, false, REJECT_INVALID, "put_candy: cannot parse txin address");
                    if(strInAddress != strAdminAddress)
                        return state.DoS(10, false, REJECT_INVALID, "put_candy: txin address is different from admin address, " + strInAddress + " != " + strAdminAddress);
                }
            }
        }
        else if(header.nAppCmd == GET_CANDY_CMD)
        {
            if(txout.nUnlockedHeight > 0)
                return state.DoS(50, false, REJECT_INVALID, "get_candy: conflict with locked txout");

            if(header.appId.GetHex() != g_strSafeAssetId)
                return state.DoS(50, false, REJECT_INVALID, "get_candy: invalid safe-asset app id in header, " + header.appId.GetHex());

            const CTxIn& txin = tx.vin[tx.vin.size() - 1];
            const CTxOut& in_txout = view.GetOutputFor(txin);
            string strInAddress = "";
            if(!GetTxOutAddress(in_txout, &strInAddress))
                return state.DoS(50, false, REJECT_INVALID, "get_candy: cannot parse last txin address, " + txin.ToString());
            if(strInAddress != g_strPutCandyAddress)
                return state.DoS(10, false, REJECT_INVALID, "get_candy: invalid candy put address, " + strInAddress);

            CGetCandyData candyData;
            if(!ParseGetCandyData(vData, candyData))
                return state.DoS(50, false, REJECT_INVALID, "get_candy: parse reserve failed");

            CAssetId_AssetInfo_IndexValue assetInfo;
            if(!GetAssetInfoByAssetId(candyData.assetId, assetInfo, false))
                return state.DoS(10, false, REJECT_INVALID, "get_candy: non-existent asset");

            if(candyData.nAmount <= 0 || candyData.nAmount > MAX_ASSETS)
                return state.DoS(10, false, REJECT_INVALID, "get_candy: invalid candy amount");
            if(candyData.nAmount != txout.nValue)
                return state.DoS(10, false, REJECT_INVALID, "get_candy: candy amount is different from txout value");

            const COutPoint& out = tx.vin[tx.vin.size() - 1].prevout;
            CCandyInfo candyInfo;
            if(!GetAssetIdCandyInfo(candyData.assetId, out, candyInfo))
                return state.DoS(10, false, REJECT_INVALID, "get_candy: non-existent candy, out: %s" + out.ToString());

            CAmount nAmount = 0;
            if(GetGetCandyAmount(candyData.assetId, out, strAddress, nAmount, fWithMempool))
                return state.DoS(10, false, REJECT_INVALID, "get_candy: current user got candy already, " + out.ToString());

            int nPrevTxHeight = GetTxHeight(out.hash);
            if(fWithMempool)
            {
                if(nPrevTxHeight >= nTxHeight)
                    return state.DoS(10, false, REJECT_INVALID, "get_candy: invalid candy txin");

                if(nPrevTxHeight+BLOCKS_PER_DAY>nTxHeight)
                    return state.DoS(10, false, REJECT_INVALID, "get_candy: get candy need wait");

                if(candyInfo.nExpired * BLOCKS_PER_MONTH + nPrevTxHeight < nTxHeight)
                    return state.DoS(10, false, REJECT_INVALID, "get_candy: candy is expired");
            }

            CAmount nSafe = 0;
            if(!GetAddressAmountByHeight(nPrevTxHeight, strAddress, nSafe))
            {
                LogPrint("asset", "check-getcandy: cannot get safe amount of address[%s] at %d\n", strAddress, nPrevTxHeight);
                return state.DoS(10, false, REJECT_INVALID, strprintf("get_candy: cannot get safe amount of address[%s] at %d", strAddress, nPrevTxHeight));
            }
            if(nSafe < 1 * COIN || nSafe > MAX_MONEY)
            {
                LogPrint("asset", "check-getcandy: invalid safe amount(at least 1 safe) of address[%s] at %d", strAddress, nPrevTxHeight);
                return state.DoS(10, false, REJECT_INVALID, strprintf("get_candy: invalid safe amount of address[%s] at %d", strAddress, nPrevTxHeight));
            }

            CAmount nTotalSafe = 0;
            if(fWithMempool)
            {
                if(!GetTotalAmountByHeight(nPrevTxHeight, nTotalSafe))
                {
                    LogPrint("asset", "check-getcandy: get total safe amount failed at %d\n", nPrevTxHeight);
                    return state.DoS(10, false, REJECT_INVALID, strprintf("get_candy: get total safe amount failed at %d\n", nPrevTxHeight));
                }
            }
            else
            {
                while(!GetTotalAmountByHeight(nPrevTxHeight, nTotalSafe)) // Waitting for candy block handle finished, when program is downloading block
                {
                    boost::this_thread::interruption_point();
                    if(ShutdownRequested())
                        return false;
                    MilliSleep(1000);
                }
            }
            if(nTotalSafe <= 0 || nTotalSafe > MAX_MONEY)
            {
                LogPrint("asset", "check-getcandy: invalid total safe amount at %d", nPrevTxHeight);
                return state.DoS(10, false, REJECT_INVALID, strprintf("get_candy: invalid total safe amount at %d", nPrevTxHeight));
            }

            if(nTotalSafe < nSafe)
            {
                LogPrint("asset", "check-getcandy: safe amount of address[%s] is more than total safe amount at %d\n", strAddress, nPrevTxHeight);
                return state.DoS(10, false, REJECT_INVALID, strprintf("get_candy: safe amount of address[%s] is more than total safe amount at %d\n", strAddress, nPrevTxHeight));
            }

            CAmount nCandyAmount = (CAmount)(1.0 * nSafe / nTotalSafe * candyInfo.nAmount);
            if(nCandyAmount < AmountFromValue("0.0001", assetInfo.assetData.nDecimals, true))
            {
                LogPrint("asset", "check-getcandy: candy-height: %d, address: %s, total_safe: %lld, user_safe: %lld, total_candy_amount: %lld, can_get_candy_amount: %lld, out: %s\n", nPrevTxHeight, strAddress, nTotalSafe, nSafe, candyInfo.nAmount, nCandyAmount, out.ToString());
                return state.DoS(10, false, REJECT_INVALID, "get_candy: candy amount which can be gotten is too little");
            }
            if(txout.nValue != nCandyAmount)
            {
                LogPrint("asset", "check-getcandy: candy-height: %d, address: %s, total_safe: %lld, user_safe: %lld, total_candy_amount: %lld, can_get_candy_amount: %lld, out: %s\n", nPrevTxHeight, strAddress, nTotalSafe, nSafe, candyInfo.nAmount, nCandyAmount, out.ToString());
                return state.DoS(10, false, REJECT_INVALID, "get_candy: candy amount which can get is different from txout value");
            }

            if(candyData.strRemarks.size() > MAX_REMARKS_SIZE)
                return state.DoS(10, false, REJECT_INVALID, "get_candy: invalid remarks");

            if(view.GetValueIn(tx, true) < tx.GetValueOut(true))
                return state.DoS(100, false, REJECT_INVALID, "get_candy: invalid get candy tx");

            // check other txout
            for(unsigned int m = 0; m < tx.vout.size(); m++)
            {
                if(m == i)
                    continue;

                const CTxOut& tempTxOut = tx.vout[m];
                uint32_t nAppCmd = 0;
                if(!tempTxOut.IsSafeOnly(&nAppCmd)) // non-safe txout
                {
                    if(nAppCmd != GET_CANDY_CMD) // non-get-candy txout
                        return state.DoS(10, false, REJECT_INVALID, "get_candy: tx can contain get candy txout and simple safe txout only, " + txout.ToString());
                }
                else
                {
                    if(nAppCmd == TRANSFER_SAFE_CMD) // complex-safe txout
                        return state.DoS(10, false, REJECT_INVALID, "get_candy: tx cannot contain complex safe txout, " + txout.ToString());
                }
            }

            // check vin
            unsigned int nIndex = 0;
            int nCount = 0;
            for(unsigned int m = 0; m < tx.vin.size(); m++)
            {
                const CTxIn& txin = tx.vin[m];
                const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                if (!coins)
                    return state.DoS(10, false, REJECT_INVALID, "get_candy: missing txin, " + txin.ToString());

                const CTxOut& in_txout = coins->vout[txin.prevout.n];
                if(in_txout.IsAsset()) // forbid invalid asset txin
                {
                    CAppHeader in_header;
                    vector<unsigned char> in_vData;
                    if(!ParseReserve(in_txout.vReserve, in_header, in_vData))
                        return state.DoS(50, false, REJECT_INVALID, "get_candy: parse txin reserve failed, " + txin.ToString());
                    if(in_header.nAppCmd != PUT_CANDY_CMD)
                        return state.DoS(50, false, REJECT_INVALID, "get_candy: txin must be put-candy, " + txin.ToString());
                    CPutCandyData putData;
                    if(!ParsePutCandyData(in_vData, putData))
                        return state.DoS(50, false, REJECT_INVALID, "get_candy: parse put-candy txin failed, " + txin.ToString());
                    if(putData.assetId != candyData.assetId)
                        return state.DoS(50, false, REJECT_INVALID, "get_candy: asset id must be same between vin and vout, " + txin.ToString());
                    if(++nCount > 1)
                        return state.DoS(50, false, REJECT_INVALID, "get_candy: txin contain put candy repeatedlly");
                    string strInAddress = "";
                    if(!GetTxOutAddress(in_txout, &strInAddress))
                        return state.DoS(50, false, REJECT_INVALID, "get_candy: cannot parse txin address");
                    if(strInAddress != g_strPutCandyAddress)
                        return state.DoS(50, false, REJECT_INVALID, "get_candy: txin address is different from put candy address, " + strInAddress + " != " + g_strPutCandyAddress);
                    nIndex = m;
                }
            }
            if(nCount == 0)
                return state.DoS(50, false, REJECT_INVALID, "get_candy: missing put candy txin");
            if(nIndex != tx.vin.size() - 1)
                return state.DoS(50, false, REJECT_INVALID, "get_candy: index of put candy in vin is error");
        }
    }

    return true;
}

bool ContextualCheckTransaction(const CTransaction& tx, CValidationState &state, CBlockIndex * const pindexPrev)
{
    bool fDIP0001Active_context = (VersionBitsState(pindexPrev, Params().GetConsensus(), Consensus::DEPLOYMENT_DIP0001, versionbitscache) == THRESHOLD_ACTIVE);

    // Size limits
    if (fDIP0001Active_context && ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_STANDARD_TX_SIZE)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    return true;
}

void LimitMempoolSize(CTxMemPool& pool, size_t limit, unsigned long age) {
    int expired = pool.Expire(GetTime() - age);
    if (expired != 0)
        LogPrint("mempool", "Expired %i transactions from the memory pool\n", expired);

    std::vector<uint256> vNoSpendsRemaining;
    pool.TrimToSize(limit, &vNoSpendsRemaining);
    BOOST_FOREACH(const uint256& removed, vNoSpendsRemaining)
        pcoinsTip->Uncache(removed);
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)",
        state.GetRejectReason(),
        state.GetDebugMessage().empty() ? "" : ", "+state.GetDebugMessage(),
        state.GetRejectCode());
}

bool ExistForbidTxin(const int nHeight, const std::vector<int>& prevheights)
{
    if(nHeight < g_nProtocolV2Height)
        return false;

    for(unsigned int i = 0; i < prevheights.size(); i++)
    {
        if(prevheights[i] < g_nCriticalHeight)
            return !error("transaction contain forbidden txin[%d]", i);
    }

    return false;
}

bool AcceptToMemoryPoolWorker(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,
                              bool* pfMissingInputs, bool fOverrideMempoolLimit, bool fRejectAbsurdFee,
                              std::vector<uint256>& vHashTxnToUncache, bool fDryRun)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    if (!CheckTransaction(tx, state, FROM_NEW) || !ContextualCheckTransaction(tx, state, chainActive.Tip()))
        return false;

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "coinbase");

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    string reason;
    if (fRequireStandard && !IsStandardTx(tx, reason))
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);

    // Don't relay version 2 transactions until CSV is active, and we can be
    // sure that such transactions will be mined (unless we're on
    // -testnet/-regtest).
    const CChainParams& chainparams = Params();
    if (fRequireStandard && tx.nVersion >= 2 && tx.nVersion < SAFE_TX_VERSION_2 && VersionBitsTipState(chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV) != THRESHOLD_ACTIVE) {
        return state.DoS(0, false, REJECT_NONSTANDARD, "premature-version2-tx");
    }

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");

    // is it already in the memory pool?
    uint256 hash = tx.GetHash();
    if (pool.exists(hash))
        return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-in-mempool");

    // If this is a Transaction Lock Request check to see if it's valid
    if(instantsend.HasTxLockRequest(hash) && !CTxLockRequest(tx).IsValid())
        return state.DoS(10, error("AcceptToMemoryPool : CTxLockRequest %s is invalid", hash.ToString()),
                            REJECT_INVALID, "bad-txlockrequest");

    // Check for conflicts with a completed Transaction Lock
    BOOST_FOREACH(const CTxIn &txin, tx.vin)
    {
        uint256 hashLocked;
        if(instantsend.GetLockedOutPointTxHash(txin.prevout, hashLocked) && hash != hashLocked)
            return state.DoS(10, error("AcceptToMemoryPool : Transaction %s conflicts with completed Transaction Lock %s",
                                    hash.ToString(), hashLocked.ToString()),
                            REJECT_INVALID, "tx-txlock-conflict");
    }

    // Check for conflicts with in-memory transactions
    set<uint256> setConflicts;
    {
    LOCK(pool.cs); // protect pool.mapNextTx
    BOOST_FOREACH(const CTxIn &txin, tx.vin)
    {
        if (pool.mapNextTx.count(txin.prevout))
        {
            const CTransaction *ptxConflicting = pool.mapNextTx[txin.prevout].ptx;
            if (!setConflicts.count(ptxConflicting->GetHash()))
            {
                // InstantSend txes are not replacable
                if(instantsend.HasTxLockRequest(ptxConflicting->GetHash())) {
                    // this tx conflicts with a Transaction Lock Request candidate
                    return state.DoS(0, error("AcceptToMemoryPool : Transaction %s conflicts with Transaction Lock Request %s",
                                            hash.ToString(), ptxConflicting->GetHash().ToString()),
                                    REJECT_INVALID, "tx-txlockreq-mempool-conflict");
                } else if (instantsend.HasTxLockRequest(hash)) {
                    // this tx is a tx lock request and it conflicts with a normal tx
                    return state.DoS(0, error("AcceptToMemoryPool : Transaction Lock Request %s conflicts with transaction %s",
                                            hash.ToString(), ptxConflicting->GetHash().ToString()),
                                    REJECT_INVALID, "txlockreq-tx-mempool-conflict");
                }
                // Allow opt-out of transaction replacement by setting
                // nSequence >= maxint-1 on all inputs.
                //
                // maxint-1 is picked to still allow use of nLockTime by
                // non-replacable transactions. All inputs rather than just one
                // is for the sake of multi-party protocols, where we don't
                // want a single party to be able to disable replacement.
                //
                // The opt-out ignores descendants as anyone relying on
                // first-seen mempool behavior should be checking all
                // unconfirmed ancestors anyway; doing otherwise is hopelessly
                // insecure.
                bool fReplacementOptOut = true;
                if (fEnableReplacement)
                {
                    BOOST_FOREACH(const CTxIn &txin, ptxConflicting->vin)
                    {
                        if (txin.nSequence < std::numeric_limits<unsigned int>::max()-1)
                        {
                            fReplacementOptOut = false;
                            break;
                        }
                    }
                }
                if (fReplacementOptOut)
                    return state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");

                setConflicts.insert(ptxConflicting->GetHash());
            }
        }
    }
    }

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        LockPoints lp;
        {
        LOCK(pool.cs);
        CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
        view.SetBackend(viewMemPool);

        // do we already have it?
        bool fHadTxInCache = pcoinsTip->HaveCoinsInCache(hash);
        if (view.HaveCoins(hash)) {
            if (!fHadTxInCache)
                vHashTxnToUncache.push_back(hash);
            return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-known");
        }

        // do all inputs exist?
        // Note that this does not check for the presence of actual outputs (see the next check for that),
        // and only helps with filling in pfMissingInputs (to determine missing vs spent).
        BOOST_FOREACH(const CTxIn txin, tx.vin) {
            if (!pcoinsTip->HaveCoinsInCache(txin.prevout.hash))
                vHashTxnToUncache.push_back(txin.prevout.hash);
            if (!view.HaveCoins(txin.prevout.hash)) {
                if (pfMissingInputs)
                    *pfMissingInputs = true;
                return false; // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
            }
        }

        // are the actual inputs available?
        if (!view.HaveInputs(tx))
            return state.Invalid(false, REJECT_DUPLICATE, "bad-txns-inputs-spent");

        // Bring the best block into scope
        view.GetBestBlock();

        nValueIn = view.GetValueIn(tx);

        // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
        view.SetBackend(dummy);

        // Only accept BIP68 sequence locked transactions that can be mined in the next
        // block; we don't want our mempool filled up with transactions that can't
        // be mined yet.
        // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
        // CoinsViewCache instead of create its own
        if (!CheckSequenceLocks(tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp))
            return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (fRequireStandard && !AreInputsStandard(tx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "1-bad-txns-nonstandard-inputs");

        unsigned int nSigOps = GetLegacySigOpCount(tx);
        nSigOps += GetP2SHSigOpCount(tx, view);

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn-nValueOut;
        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees;
        double nPriorityDummy = 0;
        pool.ApplyDeltas(hash, nPriorityDummy, nModifiedFees);

        CAmount inChainInputValue;
        double dPriority = view.GetPriority(tx, chainActive.Height(), inChainInputValue);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        BOOST_FOREACH(const CTxIn &txin, tx.vin) {
            const CCoins *coins = view.AccessCoins(txin.prevout.hash);
            if (coins->IsCoinBase()) {
                fSpendsCoinbase = true;
                break;
            }
        }

        // Check previous tx is locked or not
        std::vector<int> prevheights;
        BOOST_FOREACH(const CTxIn &txin, tx.vin)
        {
            const CCoins* coins = view.AccessCoins(txin.prevout.hash);
            if(!coins || !coins->IsAvailable(txin.prevout.n))
                return state.Invalid(false, REJECT_DUPLICATE, "2-bad-txns-inputs-spent");

            prevheights.push_back(coins->nHeight);

            const CTxOut& txout = coins->vout[txin.prevout.n];
            if(txout.nUnlockedHeight <= 0)
                continue;

            if(txout.nUnlockedHeight <= g_nChainHeight) // unlocked
                continue;

            int64_t nOffset = txout.nUnlockedHeight - coins->nHeight;
            if(nOffset <= 28 * BLOCKS_PER_DAY || nOffset > 120 * BLOCKS_PER_MONTH)
                continue;

            return state.DoS(100, false, REJECT_NONSTANDARD, "invalid-txin-locked");
        }

        if(ExistForbidTxin( (uint32_t)g_nChainHeight + 1, prevheights))
            return state.DoS(50, error("%s: contain forbidden transaction(%s) txin", __func__, hash.GetHex()), REJECT_INVALID, "bad-txns-forbid");

        if(!CheckAppTransaction(tx, state, view, true))
            return false;

        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainActive.Height(), pool.HasNoInputsOf(tx), inChainInputValue, fSpendsCoinbase, nSigOps, lp);
        unsigned int nSize = entry.GetTxSize();

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_STANDARD_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        if ((nSigOps > MAX_STANDARD_TX_SIGOPS) || (nBytesPerSigOp && nSigOps > nSize / nBytesPerSigOp))
            return state.DoS(0, false, REJECT_NONSTANDARD, "bad-txns-too-many-sigops", false,
                strprintf("%d", nSigOps));

        CAmount mempoolRejectFee = pool.GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nSize);
        if (mempoolRejectFee > 0 && nModifiedFees < mempoolRejectFee) {
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool min fee not met", false, strprintf("%d < %d", nFees, mempoolRejectFee));
        } else if (GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY) && nModifiedFees < ::minRelayTxFee.GetFee(nSize) && !AllowFree(entry.GetPriority(chainActive.Height() + 1))) {
            // Require that free transactions have sufficient priority to be mined in the next block.
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
        }

        // Continuously rate-limit free (really, very-low-fee) transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (fLimitFree && nModifiedFees < ::minRelayTxFee.GetFee(nSize))
        {
            static CCriticalSection csFreeLimiter;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            LOCK(csFreeLimiter);

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;
            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            if (dFreeCount >= GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY) * 10 * 1000)
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "rate limited free transaction");
            LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
            dFreeCount += nSize;
        }

        if (fRejectAbsurdFee && nFees > ::minRelayTxFee.GetFee(nSize) * 10000)
            return state.Invalid(false,
                REJECT_HIGHFEE, "absurdly-high-fee",
                strprintf("%d > %d", nFees, ::minRelayTxFee.GetFee(nSize) * 10000));

        // Calculate in-mempool ancestors, up to a limit.
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
        std::string errString;
        if (!pool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants, nLimitDescendantSize, errString)) {
            return state.DoS(0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString);
        }

        // A transaction that spends outputs that would be replaced by it is invalid. Now
        // that we have the set of all ancestors we can detect this
        // pathological case by making sure setConflicts and setAncestors don't
        // intersect.
        BOOST_FOREACH(CTxMemPool::txiter ancestorIt, setAncestors)
        {
            const uint256 &hashAncestor = ancestorIt->GetTx().GetHash();
            if (setConflicts.count(hashAncestor))
            {
                return state.DoS(10, error("AcceptToMemoryPool: %s spends conflicting transaction %s",
                                           hash.ToString(),
                                           hashAncestor.ToString()),
                                 REJECT_INVALID, "bad-txns-spends-conflicting-tx");
            }
        }

        // Check if it's economically rational to mine this transaction rather
        // than the ones it replaces.
        CAmount nConflictingFees = 0;
        size_t nConflictingSize = 0;
        uint64_t nConflictingCount = 0;
        CTxMemPool::setEntries allConflicting;

        // If we don't hold the lock allConflicting might be incomplete; the
        // subsequent RemoveStaged() and addUnchecked() calls don't guarantee
        // mempool consistency for us.
        LOCK(pool.cs);
        if (setConflicts.size())
        {
            CFeeRate newFeeRate(nModifiedFees, nSize);
            set<uint256> setConflictsParents;
            const int maxDescendantsToVisit = 100;
            CTxMemPool::setEntries setIterConflicting;
            BOOST_FOREACH(const uint256 &hashConflicting, setConflicts)
            {
                CTxMemPool::txiter mi = pool.mapTx.find(hashConflicting);
                if (mi == pool.mapTx.end())
                    continue;

                // Save these to avoid repeated lookups
                setIterConflicting.insert(mi);

                // If this entry is "dirty", then we don't have descendant
                // state for this transaction, which means we probably have
                // lots of in-mempool descendants.
                // Don't allow replacements of dirty transactions, to ensure
                // that we don't spend too much time walking descendants.
                // This should be rare.
                if (mi->IsDirty()) {
                    return state.DoS(0,
                            error("AcceptToMemoryPool: rejecting replacement %s; cannot replace tx %s with untracked descendants",
                                hash.ToString(),
                                mi->GetTx().GetHash().ToString()),
                            REJECT_NONSTANDARD, "too many potential replacements");
                }

                // Don't allow the replacement to reduce the feerate of the
                // mempool.
                //
                // We usually don't want to accept replacements with lower
                // feerates than what they replaced as that would lower the
                // feerate of the next block. Requiring that the feerate always
                // be increased is also an easy-to-reason about way to prevent
                // DoS attacks via replacements.
                //
                // The mining code doesn't (currently) take children into
                // account (CPFP) so we only consider the feerates of
                // transactions being directly replaced, not their indirect
                // descendants. While that does mean high feerate children are
                // ignored when deciding whether or not to replace, we do
                // require the replacement to pay more overall fees too,
                // mitigating most cases.
                CFeeRate oldFeeRate(mi->GetModifiedFee(), mi->GetTxSize());
                if (newFeeRate <= oldFeeRate)
                {
                    return state.DoS(0,
                            error("AcceptToMemoryPool: rejecting replacement %s; new feerate %s <= old feerate %s",
                                  hash.ToString(),
                                  newFeeRate.ToString(),
                                  oldFeeRate.ToString()),
                            REJECT_INSUFFICIENTFEE, "insufficient fee");
                }

                BOOST_FOREACH(const CTxIn &txin, mi->GetTx().vin)
                {
                    setConflictsParents.insert(txin.prevout.hash);
                }

                nConflictingCount += mi->GetCountWithDescendants();
            }
            // This potentially overestimates the number of actual descendants
            // but we just want to be conservative to avoid doing too much
            // work.
            if (nConflictingCount <= maxDescendantsToVisit) {
                // If not too many to replace, then calculate the set of
                // transactions that would have to be evicted
                BOOST_FOREACH(CTxMemPool::txiter it, setIterConflicting) {
                    pool.CalculateDescendants(it, allConflicting);
                }
                BOOST_FOREACH(CTxMemPool::txiter it, allConflicting) {
                    nConflictingFees += it->GetModifiedFee();
                    nConflictingSize += it->GetTxSize();
                }
            } else {
                return state.DoS(0,
                        error("AcceptToMemoryPool: rejecting replacement %s; too many potential replacements (%d > %d)\n",
                            hash.ToString(),
                            nConflictingCount,
                            maxDescendantsToVisit),
                        REJECT_NONSTANDARD, "too many potential replacements");
            }

            for (unsigned int j = 0; j < tx.vin.size(); j++)
            {
                // We don't want to accept replacements that require low
                // feerate junk to be mined first. Ideally we'd keep track of
                // the ancestor feerates and make the decision based on that,
                // but for now requiring all new inputs to be confirmed works.
                if (!setConflictsParents.count(tx.vin[j].prevout.hash))
                {
                    // Rather than check the UTXO set - potentially expensive -
                    // it's cheaper to just check if the new input refers to a
                    // tx that's in the mempool.
                    if (pool.mapTx.find(tx.vin[j].prevout.hash) != pool.mapTx.end())
                        return state.DoS(0, error("AcceptToMemoryPool: replacement %s adds unconfirmed input, idx %d",
                                                  hash.ToString(), j),
                                         REJECT_NONSTANDARD, "replacement-adds-unconfirmed");
                }
            }

            // The replacement must pay greater fees than the transactions it
            // replaces - if we did the bandwidth used by those conflicting
            // transactions would not be paid for.
            if (nModifiedFees < nConflictingFees)
            {
                return state.DoS(0, error("AcceptToMemoryPool: rejecting replacement %s, less fees than conflicting txs; %s < %s",
                                          hash.ToString(), FormatMoney(nModifiedFees), FormatMoney(nConflictingFees)),
                                 REJECT_INSUFFICIENTFEE, "insufficient fee");
            }

            // Finally in addition to paying more fees than the conflicts the
            // new transaction must pay for its own bandwidth.
            CAmount nDeltaFees = nModifiedFees - nConflictingFees;
            if (nDeltaFees < ::minRelayTxFee.GetFee(nSize))
            {
                return state.DoS(0,
                        error("AcceptToMemoryPool: rejecting replacement %s, not enough additional fees to relay; %s < %s",
                              hash.ToString(),
                              FormatMoney(nDeltaFees),
                              FormatMoney(::minRelayTxFee.GetFee(nSize))),
                        REJECT_INSUFFICIENTFEE, "insufficient fee");
            }
        }

        // If we aren't going to actually accept it but just were verifying it, we are fine already
        if(fDryRun) return true;

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        if (!CheckInputs(tx, state, view, true, STANDARD_SCRIPT_VERIFY_FLAGS, true))
            return false;

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        if (!CheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true))
        {
            return error("%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s, %s",
                __func__, hash.ToString(), FormatStateMessage(state));
        }

        // Remove conflicting transactions from the mempool
        BOOST_FOREACH(const CTxMemPool::txiter it, allConflicting)
        {
            LogPrint("mempool", "replacing tx %s with %s for %s SAFE additional fees, %d delta bytes\n",
                    it->GetTx().GetHash().ToString(),
                    hash.ToString(),
                    FormatMoney(nModifiedFees - nConflictingFees),
                    (int)nSize - (int)nConflictingSize);
        }
        pool.RemoveStaged(allConflicting);

        // Store transaction in memory
        pool.addUnchecked(hash, entry, view, setAncestors, !IsInitialBlockDownload());

        // Add memory address index
        if (fAddressIndex) {
            pool.addAddressIndex(entry, view);
        }

        // Add memory spent index
        if (fSpentIndex) {
            pool.addSpentIndex(entry, view);
        }

        pool.addAppInfoIndex(entry, view);
        pool.add_AppTx_Index(entry, view);
        pool.add_Auth_Index(entry, view);
        pool.addAssetInfoIndex(entry, view);
        pool.add_AssetTx_Index(entry, view);
        pool.add_GetCandy_Index(entry, view);

        // trim mempool and check if tx was trimmed
        if (!fOverrideMempoolLimit) {
            LimitMempoolSize(pool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
            if (!pool.exists(hash))
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool full");
        }
    }

    if(!fDryRun)
        GetMainSignals().SyncTransaction(tx, NULL);

    return true;
}

bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,
                        bool* pfMissingInputs, bool fOverrideMempoolLimit, bool fRejectAbsurdFee, bool fDryRun)
{
    std::vector<uint256> vHashTxToUncache;
    bool res = AcceptToMemoryPoolWorker(pool, state, tx, fLimitFree, pfMissingInputs, fOverrideMempoolLimit, fRejectAbsurdFee, vHashTxToUncache, fDryRun);
    if (!res || fDryRun) {
        if(!res) LogPrint("mempool", "%s: %s %s\n", __func__, tx.GetHash().ToString(), state.GetRejectReason());
        BOOST_FOREACH(const uint256& hashTx, vHashTxToUncache)
            pcoinsTip->Uncache(hashTx);
    }
    // After we've (potentially) uncached entries, ensure our coins cache is still within its size limits
    CValidationState stateDummy;
    FlushStateToDisk(stateDummy, FLUSH_STATE_PERIODIC);
    return res;
}

bool GetTimestampIndex(const unsigned int &high, const unsigned int &low, std::vector<uint256> &hashes)
{
    if (!fTimestampIndex)
        return error("Timestamp index not enabled");

    if (!pblocktree->ReadTimestampIndex(high, low, hashes))
        return error("Unable to get hashes for timestamps");

    return true;
}

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value)
{
    if (!fSpentIndex)
        return false;

    if (mempool.getSpentIndex(key, value))
        return true;

    if (!pblocktree->ReadSpentIndex(key, value))
        return false;

    return true;
}

bool GetAddressIndex(uint160 addressHash, int type,
                     std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex, int start, int end)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressIndex(addressHash, type, addressIndex, start, end))
        return error("unable to get txids for address");

    return true;
}

bool GetAddressUnspent(uint160 addressHash, int type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressUnspentIndex(addressHash, type, unspentOutputs))
        return error("unable to get txids for address");

    return true;
}

/** Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256 &hash, CTransaction &txOut, const Consensus::Params& consensusParams, uint256 &hashBlock, bool fAllowSlow)
{
    CBlockIndex *pindexSlow = NULL;

    LOCK(cs_main);

    if (mempool.lookup(hash, txOut))
    {
        return true;
    }

    if (fTxIndex) {
        CDiskTxPos postx;
        if (pblocktree->ReadTxIndex(hash, postx)) {
            CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
            if (file.IsNull())
                return error("%s: OpenBlockFile failed", __func__);
            CBlockHeader header;
            try {
                file >> header;
                fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                file >> txOut;
            } catch (const std::exception& e) {
                return error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetHash();
            if (txOut.GetHash() != hash)
                return error("%s: txid mismatch", __func__);
            return true;
        }
    }

    if (fAllowSlow) { // use coin database to locate block that contains transaction, and scan it
        int nHeight = -1;
        {
            CCoinsViewCache &view = *pcoinsTip;
            const CCoins* coins = view.AccessCoins(hash);
            if (coins)
                nHeight = coins->nHeight;
        }
        if (nHeight > 0)
            pindexSlow = chainActive[nHeight];
    }

    if (pindexSlow) {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow, consensusParams)) {
            BOOST_FOREACH(const CTransaction &tx, block.vtx) {
                if (tx.GetHash() == hash) {
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    return true;
                }
            }
        }
    }

    return false;
}






//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("WriteBlockToDisk: OpenBlockFile failed");

    // Write index header
    unsigned int nSize = fileout.GetSerializeSize(block);
    fileout << FLATDATA(messageStart) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("WriteBlockToDisk: ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const Consensus::Params& consensusParams)
{
    block.SetNull();

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("ReadBlockFromDisk: OpenBlockFile failed for %s", pos.ToString());

    // Read block
    try {
        filein >> block;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s at %s", __func__, e.what(), pos.ToString());
    }

    // Check the header
    if (!CheckCriticalBlock(block) && !CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
        return error("ReadBlockFromDisk: Errors in block header at %s", pos.ToString());

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos(), consensusParams))
        return false;
    if (block.GetHash() != pindex->GetBlockHash())
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                pindex->ToString(), pindex->GetBlockPos().ToString());
    return true;
}

double ConvertBitsToDouble(unsigned int nBits)
{
    int nShift = (nBits >> 24) & 0xff;

    double dDiff = (double)0x0000ffff / (double)(nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

/*
NOTE:   unlike bitcoin we are using PREVIOUS block height here,
        might be a good idea to change this to use prev bits
        but current height to avoid confusion.
*/
CAmount GetBlockSubsidy(int nPrevBits, int nPrevHeight, const Consensus::Params& consensusParams, bool fSuperblockPartOnly)
{
    if(IsCriticalHeight(nPrevHeight + 1))
        return g_nCriticalReward;
    double dDiff;
    CAmount nSubsidyBase;

    if (nPrevHeight <= 4500 && Params().NetworkIDString() == CBaseChainParams::MAIN) {
        /* a bug which caused diff to not be correctly calculated */
        dDiff = (double)0x0000ffff / (double)(nPrevBits & 0x00ffffff);
    } else {
        dDiff = ConvertBitsToDouble(nPrevBits);
    }

    if (nPrevHeight < 5465) {
        // Early ages...
        // 1111/((x+1)^2)
        nSubsidyBase = (1111.0 / (pow((dDiff+1.0),2.0)));
        if(nSubsidyBase > 500) nSubsidyBase = 500;
        else if(nSubsidyBase < 1) nSubsidyBase = 1;
    } else if (nPrevHeight < 17000 || (dDiff <= 75 && nPrevHeight < 24000)) {
        // CPU mining era
        // 11111/(((x+51)/6)^2)
        nSubsidyBase = (11111.0 / (pow((dDiff+51.0)/6.0,2.0)));
        if(nSubsidyBase > 500) nSubsidyBase = 500;
        else if(nSubsidyBase < 25) nSubsidyBase = 25;
    } else {
        // GPU/ASIC mining era
        // 2222222/(((x+2600)/9)^2)
        nSubsidyBase = (2222222.0 / (pow((dDiff+2600.0)/9.0,2.0)));
        if(nPrevHeight + 1 > g_nCriticalHeight)
        {
            if(nSubsidyBase > 5)
                nSubsidyBase = 5;
            else if(nSubsidyBase < 5)
                nSubsidyBase = 5;
        }
        else
        {
            if(nSubsidyBase > 25) nSubsidyBase = 25;
            else if(nSubsidyBase < 5) nSubsidyBase = 5;
        }
    }

    // LogPrintf("height %u diff %4.2f reward %d\n", nPrevHeight, dDiff, nSubsidyBase);
    CAmount nSubsidy = nSubsidyBase * COIN;

    // yearly decline of production by ~7.1% per year, projected ~18M coins max by year 2050+.
    for (int i = consensusParams.nSubsidyHalvingInterval; i <= nPrevHeight; i += consensusParams.nSubsidyHalvingInterval) {
        nSubsidy -= nSubsidy/14;
    }

    // Hard fork to reduce the block reward by 10 extra percent (allowing budget/superblocks)
    CAmount nSuperblockPart = (nPrevHeight > consensusParams.nBudgetPaymentsStartBlock) ? nSubsidy/10 : 0;

    return fSuperblockPartOnly ? nSuperblockPart : nSubsidy - nSuperblockPart;
}

CAmount GetMasternodePayment(int nHeight, CAmount blockValue)
{
    CAmount ret = blockValue/5; // start at 20%

    int nMNPIBlock = Params().GetConsensus().nMasternodePaymentsIncreaseBlock;
    int nMNPIPeriod = Params().GetConsensus().nMasternodePaymentsIncreasePeriod;

                                                                      // mainnet:
    if(nHeight > nMNPIBlock)                  ret += blockValue / 20; // 158000 - 25.0% - 2014-10-24
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 1)) ret += blockValue / 20; // 175280 - 30.0% - 2014-11-25
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 2)) ret += blockValue / 20; // 192560 - 35.0% - 2014-12-26
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 3)) ret += blockValue / 40; // 209840 - 37.5% - 2015-01-26
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 4)) ret += blockValue / 40; // 227120 - 40.0% - 2015-02-27
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 5)) ret += blockValue / 40; // 244400 - 42.5% - 2015-03-30
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 6)) ret += blockValue / 40; // 261680 - 45.0% - 2015-05-01
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 7)) ret += blockValue / 40; // 278960 - 47.5% - 2015-06-01
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 9)) ret += blockValue / 40; // 313520 - 50.0% - 2015-08-03

    return ret;
}

bool IsInitialBlockDownload()
{
    static bool lockIBDState = false;
    if (lockIBDState)
        return false;
    if (fImporting || fReindex)
        return true;
    LOCK(cs_main);
    const CChainParams& chainParams = Params();
    if (chainActive.Tip() == NULL)
        return true;
    if (chainActive.Tip()->nChainWork < UintToArith256(chainParams.GetConsensus().nMinimumChainWork))
        return true;
    if (chainActive.Tip()->GetBlockTime() < (GetTime() - chainParams.MaxTipAge()))
        return true;
    lockIBDState = true;
    return false;
}

bool fLargeWorkForkFound = false;
bool fLargeWorkInvalidChainFound = false;
CBlockIndex *pindexBestForkTip = NULL, *pindexBestForkBase = NULL;

void CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before finishing our initial sync)
    if (IsInitialBlockDownload())
        return;

    // If our best fork is no longer within 72 blocks (+/- 3 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= 72)
        pindexBestForkTip = NULL;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork > chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * 6)))
    {
        if (!fLargeWorkForkFound && pindexBestForkBase)
        {
            if(pindexBestForkBase->phashBlock){
                std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                    pindexBestForkBase->phashBlock->ToString() + std::string("'");
                CAlert::Notify(warning, true);
            }
        }
        if (pindexBestForkTip && pindexBestForkBase)
        {
            if(pindexBestForkBase->phashBlock){
                LogPrintf("%s: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n", __func__,
                       pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(),
                       pindexBestForkTip->nHeight, pindexBestForkTip->phashBlock->ToString());
                fLargeWorkForkFound = true;
            }
        }
        else
        {
            if(pindexBestInvalid->nHeight > chainActive.Height() + 6)
                LogPrintf("%s: Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.\n", __func__);
            else
                LogPrintf("%s: Warning: Found invalid chain which has higher work (at least ~6 blocks worth of work) than our best chain.\nChain state database corruption likely.\n", __func__);
            fLargeWorkInvalidChainFound = true;
        }
    }
    else
    {
        fLargeWorkForkFound = false;
        fLargeWorkInvalidChainFound = false;
    }
}

void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip)
{
    AssertLockHeld(cs_main);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex* pfork = pindexNewForkTip;
    CBlockIndex* plonger = chainActive.Tip();
    while (pfork && pfork != plonger)
    {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition where we should warn the user about as a fork of at least 7 blocks
    // with a tip within 72 blocks (+/- 3 hours if no one mines it) of ours
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
            pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
            chainActive.Height() - pindexNewForkTip->nHeight < 72)
    {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LogPrintf("%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
      log(pindexNew->nChainWork.getdouble())/log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
      pindexNew->GetBlockTime()));
    CBlockIndex *tip = chainActive.Tip();
    assert (tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      tip->GetBlockHash().ToString(), chainActive.Height(), log(tip->nChainWork.getdouble())/log(2.0),
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckForkWarningConditions();
}

void static InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state) {
    if (!state.CorruptionPossible()) {
        pindex->nStatus |= BLOCK_FAILED_VALID;
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
}

void UpdateCoins(const CTransaction& tx, CValidationState &state, CCoinsViewCache &inputs, CTxUndo &txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase()) {
        txundo.vprevout.reserve(tx.vin.size());
        BOOST_FOREACH(const CTxIn &txin, tx.vin) {
            CCoinsModifier coins = inputs.ModifyCoins(txin.prevout.hash);
            unsigned nPos = txin.prevout.n;

            if (nPos >= coins->vout.size() || coins->vout[nPos].IsNull())
                assert(false);

            uint32_t nAppCmd = 0;
            if(coins->vout[nPos].IsAsset(&nAppCmd) && nAppCmd == PUT_CANDY_CMD && txin.scriptSig.empty())
            {
                string strAddress = "";
                if(GetTxOutAddress(coins->vout[nPos], &strAddress) && strAddress == g_strPutCandyAddress)
                    continue;
            }

            // mark an outpoint spent, and construct undo information
            txundo.vprevout.push_back(CTxInUndo(coins->vout[nPos]));
            coins->Spend(nPos);
            if (coins->vout.size() == 0) {
                CTxInUndo& undo = txundo.vprevout.back();
                undo.nHeight = coins->nHeight;
                undo.fCoinBase = coins->fCoinBase;
                undo.nVersion = coins->nVersion;
            }
        }
        // add outputs
        inputs.ModifyNewCoins(tx.GetHash())->FromTx(tx, nHeight);
    }
    else {
        // add outputs for coinbase tx
        // In this case call the full ModifyCoins which will do a database
        // lookup to be sure the coins do not already exist otherwise we do not
        // know whether to mark them fresh or not.  We want the duplicate coinbases
        // before BIP30 to still be properly overwritten.
        inputs.ModifyCoins(tx.GetHash())->FromTx(tx, nHeight);
    }
}

void UpdateCoins(const CTransaction& tx, CValidationState &state, CCoinsViewCache &inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, state, inputs, txundo, nHeight);
}

bool CScriptCheck::operator()() {
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    if (!VerifyScript(scriptSig, scriptPubKey, nFlags, CachingTransactionSignatureChecker(ptxTo, nIn, cacheStore), &error)) {
        return false;
    }
    return true;
}

int GetSpendHeight(const CCoinsViewCache& inputs)
{
    LOCK(cs_main);
    CBlockIndex* pindexPrev = mapBlockIndex.find(inputs.GetBestBlock())->second;
    return pindexPrev->nHeight + 1;
}

namespace Consensus {
bool CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight)
{
        // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
        // for an attacker to attempt to split the network.
        if (!inputs.HaveInputs(tx))
            return state.Invalid(false, 0, "", "Inputs unavailable");

        CAmount nValueIn = 0;
        CAmount nFees = 0;
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            const COutPoint &prevout = tx.vin[i].prevout;
            const CCoins *coins = inputs.AccessCoins(prevout.hash);
            assert(coins);

            // If prev is coinbase, check that it's matured
            if (coins->IsCoinBase()) {
                if (nSpendHeight - coins->nHeight < COINBASE_MATURITY)
                    return state.Invalid(false,
                        REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                        strprintf("tried to spend coinbase at depth %d", nSpendHeight - coins->nHeight));
            }

            const CTxOut& in_txout = coins->vout[prevout.n];
            CAppHeader header;
            vector<unsigned char> vData;
            if(ParseReserve(in_txout.vReserve, header, vData))
            {
                if(header.nAppCmd == REGISTER_APP_CMD || header.nAppCmd == ADD_AUTH_CMD || header.nAppCmd == DELETE_AUTH_CMD || header.nAppCmd == CREATE_EXTEND_TX_CMD || header.nAppCmd == TRANSFER_SAFE_CMD)
                {
                    nValueIn += in_txout.nValue;
                    if (!MoneyRange(in_txout.nValue) || !MoneyRange(nValueIn))
                        return state.DoS(100, false, REJECT_INVALID, "app_txout: bad-txns-inputvalues-outofrange");
                }

                if(header.nAppCmd == REGISTER_APP_CMD || header.nAppCmd == ADD_AUTH_CMD || header.nAppCmd == DELETE_AUTH_CMD || header.nAppCmd == ISSUE_ASSET_CMD || header.nAppCmd == ADD_ASSET_CMD || header.nAppCmd == DESTORY_ASSET_CMD || header.nAppCmd == PUT_CANDY_CMD || header.nAppCmd == GET_CANDY_CMD)
                {
                    if(coins->nHeight <= 0)
                        return state.DoS(10, false, REJECT_INVALID, "app_tx/asset_tx: txin need 1 confirmation at least, " + prevout.ToString());
                }
            }
            else
            {
                // Check for negative or overflow input values
                nValueIn += in_txout.nValue;
                if (!MoneyRange(in_txout.nValue) || !MoneyRange(nValueIn))
                    return state.DoS(100, false, REJECT_INVALID, "safe_txout: bad-txns-inputvalues-outofrange");
            }
        }

        if (nValueIn < tx.GetValueOut())
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(tx.GetValueOut())));

        // Tally transaction fees
        CAmount nTxFee = nValueIn - tx.GetValueOut();
        if (nTxFee < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-negative");
        nFees += nTxFee;
        if (!MoneyRange(nFees))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    return true;
}
}// namespace Consensus

bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheStore, std::vector<CScriptCheck> *pvChecks)
{
    if (!tx.IsCoinBase())
    {
        if (!Consensus::CheckTxInputs(tx, state, inputs, GetSpendHeight(inputs)))
            return false;

        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip script verification when connecting blocks under the
        // assumedvalid block. Assuming the assumedvalid block is valid this
        // is safe because block merkle hashes are still computed and checked,
        // Of course, if an assumed valid block is invalid due to false scriptSigs
        // this optimization would allow an invalid chain to be accepted.
        if (fScriptChecks) {
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const COutPoint &prevout = tx.vin[i].prevout;
                const CCoins* coins = inputs.AccessCoins(prevout.hash);
                assert(coins);

                {
                    CAppHeader in_header;
                    vector<unsigned char> vInData;
                    const CTxOut& in_txout = coins->vout[prevout.n];
                    uint256 in_assetId;
                    if(ParseReserve(in_txout.vReserve, in_header, vInData))
                    {
                        if(in_header.nAppCmd == PUT_CANDY_CMD && in_header.appId.GetHex() == g_strSafeAssetId)
                        {
                            string strAddress = "";
                            if(GetTxOutAddress(in_txout, &strAddress) && strAddress == g_strPutCandyAddress)
                            {
                                CPutCandyData candyData;
                                if(ParsePutCandyData(vInData, candyData))
                                    in_assetId = candyData.assetId;
                            }
                        }
                    }

                    if(!in_assetId.IsNull())
                    {
                        int nCount = 0;
                        bool fPass = true;
                        for(unsigned int m = 0; m < tx.vout.size(); m++)
                        {
                            const CTxOut& txout = tx.vout[m];
                            uint32_t nAppCmd = 0;
                            if(txout.IsSafeOnly(&nAppCmd))
                            {
                                if(nAppCmd == TRANSFER_SAFE_CMD)
                                {
                                    fPass = false;
                                    break;
                                }
                            }
                            else
                            {
                                if(nAppCmd != GET_CANDY_CMD)
                                {
                                    fPass = false;
                                    break;
                                }
                                else
                                {
                                    CAppHeader header;
                                    vector<unsigned char> vData;
                                    if(!ParseReserve(txout.vReserve, header, vData))
                                    {
                                        fPass = false;
                                        break;
                                    }

                                    if(header.nAppCmd != GET_CANDY_CMD)
                                    {
                                        fPass = false;
                                        break;
                                    }

                                    CGetCandyData candyData;
                                    if(!ParseGetCandyData(vData, candyData))
                                    {
                                        fPass = false;
                                        break;
                                    }

                                    if(candyData.assetId != in_assetId)
                                    {
                                        fPass = false;
                                        break;
                                    }
                                    nCount++;
                                }
                            }
                        }

                        if(nCount != 0 && fPass) // candy input pass sign check
                            continue;
                    }
                }

                // Verify signature
                CScriptCheck check(*coins, tx, i, flags, cacheStore);
                if (pvChecks) {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        CScriptCheck check2(*coins, tx, i,
                                flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheStore);
                        if (check2())
                            return state.Invalid(false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after a soft-fork
                    // super-majority vote has passed.
                    return state.DoS(100,false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
                }
            }
        }
    }

    return true;
}

namespace {

bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: OpenUndoFile failed", __func__);

    // Write index header
    unsigned int nSize = fileout.GetSerializeSize(blockundo);
    fileout << FLATDATA(messageStart) << nSize;

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("%s: ftell failed", __func__);
    pos.nPos = (unsigned int)fileOutPos;
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

bool UndoReadFromDisk(CBlockUndo& blockundo, const CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to read
    CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s: OpenBlockFile failed", __func__);

    // Read block
    uint256 hashChecksum;
    try {
        filein >> blockundo;
        filein >> hashChecksum;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    if (hashChecksum != hasher.GetHash())
        return error("%s: Checksum mismatch", __func__);

    return true;
}

/** Abort with a message */
bool AbortNode(const std::string& strMessage, const std::string& userMessage="")
{
    strMiscWarning = strMessage;
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage="")
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

} // anon namespace

/**
 * Apply the undo operation of a CTxInUndo to the given chain state.
 * @param undo The undo object.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return True on success.
 */
static bool ApplyTxInUndo(const CTxInUndo& undo, CCoinsViewCache& view, const COutPoint& out)
{
    bool fClean = true;

    CCoinsModifier coins = view.ModifyCoins(out.hash);
    if (undo.nHeight != 0) {
        // undo data contains height: this is the last output of the prevout tx being spent
        if (!coins->IsPruned())
            fClean = fClean && error("%s: undo data overwriting existing transaction", __func__);
        coins->Clear();
        coins->fCoinBase = undo.fCoinBase;
        coins->nHeight = undo.nHeight;
        coins->nVersion = undo.nVersion;
    } else {
        if (coins->IsPruned())
            fClean = fClean && error("%s: undo data adding output to missing transaction", __func__);
    }
    if (coins->IsAvailable(out.n))
        fClean = fClean && error("%s: undo data overwriting existing output", __func__);
    if (coins->vout.size() < out.n+1)
        coins->vout.resize(out.n+1);
    coins->vout[out.n] = undo.txout;

    return fClean;
}

bool DisconnectBlock(const CBlock& block, CValidationState& state, const CBlockIndex* pindex, CCoinsViewCache& view, bool* pfClean)
{
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    if (pfClean)
        *pfClean = false;

    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return error("DisconnectBlock(): no undo data available");
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash()))
        return error("DisconnectBlock(): failure reading undo data");

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
        return error("DisconnectBlock(): block and undo data inconsistent");

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<uint256, CAppId_AppInfo_IndexValue> > appId_appInfo_index;
    std::vector<std::pair<std::string, CName_Id_IndexValue> > appName_appId_index;
    std::vector<std::pair<CAppTx_IndexKey, int> > appTx_index;
    std::vector<std::pair<uint256, CAssetId_AssetInfo_IndexValue> > assetId_assetInfo_index;
    std::vector<std::pair<std::string, CName_Id_IndexValue> > shortName_assetId_index;
    std::vector<std::pair<std::string, CName_Id_IndexValue> > assetName_assetId_index;
    std::vector<std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue> > putCandy_index;
    std::vector<std::pair<CGetCandy_IndexKey, CGetCandy_IndexValue> > getCandy_index;
    std::vector<std::pair<CAssetTx_IndexKey, int> > assetTx_index;

    // undo transactions in reverse order
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const CTransaction &tx = block.vtx[i];
        uint256 hash = tx.GetHash();

        if (fAddressIndex) {

            for (unsigned int k = tx.vout.size(); k-- > 0;) {
                const CTxOut &out = tx.vout[k];

                if(out.IsAsset())
                    continue;

                if (out.scriptPubKey.IsPayToScriptHash()) {
                    vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);

                    // undo receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, i, hash, k, false), out.nValue));

                    // undo unspent index
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(2, uint160(hashBytes), hash, k), CAddressUnspentValue()));

                } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                    vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);

                    // undo receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, i, hash, k, false), out.nValue));

                    // undo unspent index
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(1, uint160(hashBytes), hash, k), CAddressUnspentValue()));

                } else if (out.scriptPubKey.IsPayToPublicKey()) {
                    uint160 hashBytes(Hash160(out.scriptPubKey.begin()+1, out.scriptPubKey.end()-1));

                    // undo receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(1, hashBytes, pindex->nHeight, i, hash, k, false), out.nValue));

                    // undo unspent index
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(1, hashBytes, hash, k), CAddressUnspentValue()));
                } else {
                    continue;
                }

            }

        }

        // Check that all outputs are available and match the outputs in the block itself
        // exactly.
        {
        CCoinsModifier outs = view.ModifyCoins(hash);
        outs->ClearUnspendable();

        CCoins outsBlock(tx, pindex->nHeight);
        // The CCoins serialization does not serialize negative numbers.
        // No network rules currently depend on the version here, so an inconsistency is harmless
        // but it must be corrected before txout nversion ever influences a network rule.
        if (outsBlock.nVersion < 0)
            outs->nVersion = outsBlock.nVersion;

        if (*outs != outsBlock)
            fClean = fClean && error("DisconnectBlock(): added transaction mismatch? database corrupted");

        // remove outputs
        outs->Clear();
        }

        // restore inputs
        if (i > 0) { // not coinbases
            const CTxUndo &txundo = blockUndo.vtxundo[i-1];
            if (txundo.vprevout.size() != tx.vin.size())
            {
                bool fPutCandy = false;
                int nPutCandyTxInCount = 0;
                for(unsigned int j = tx.vin.size(); j-- > 0;)
                {
                    const CTxIn& input = tx.vin[j];
                    const COutPoint& out = input.prevout;
                    CCoins coins;
                    if(!GetUTXOCoins(out, coins))
                        continue;

                    uint32_t nAppCmd = 0;
                    if(coins.vout[out.n].IsAsset(&nAppCmd) && nAppCmd == PUT_CANDY_CMD && input.scriptSig.empty())
                    {
                        string strAddress = "";
                        if(GetTxOutAddress(coins.vout[out.n], &strAddress) && strAddress == g_strPutCandyAddress)
                        {
                            if(++nPutCandyTxInCount > 1)
                            {
                                fPutCandy = false;
                                break;
                            }

                            fPutCandy = true;
                            continue;
                        }
                    }
                }
                if(!fPutCandy)
                    return error("DisconnectBlock(): transaction and undo data inconsistent");
            }
            for (unsigned int j = tx.vin.size(); j-- > 0;) {
                const CTxIn& input = tx.vin[j];
                const COutPoint &out = input.prevout;

                CCoins coins;
                if(GetUTXOCoins(out, coins))
                {
                    uint32_t nAppCmd = 0;
                    if(coins.vout[out.n].IsAsset(&nAppCmd) && nAppCmd == PUT_CANDY_CMD && input.scriptSig.empty())
                    {
                        string strAddress = "";
                        if(GetTxOutAddress(coins.vout[out.n], &strAddress) && strAddress == g_strPutCandyAddress)
                            continue;
                    }
                }

                const CTxInUndo &undo = txundo.vprevout[j];
                if (!ApplyTxInUndo(undo, view, out))
                    fClean = false;

                if (fSpentIndex) {
                    // undo and delete the spent index
                    spentIndex.push_back(std::make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue()));
                }

                const CTxOut &prevout = view.GetOutputFor(input);
                if(prevout.IsAsset())
                    continue;

                if (fAddressIndex) {
                    if (prevout.scriptPubKey.IsPayToScriptHash()) {
                        vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22);

                        // undo spending activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, i, hash, j, true), prevout.nValue * -1));

                        // restore unspent index
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(2, uint160(hashBytes), input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));


                    } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
                        vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23);

                        // undo spending activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, i, hash, j, true), prevout.nValue * -1));

                        // restore unspent index
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(1, uint160(hashBytes), input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));

                    } else if (prevout.scriptPubKey.IsPayToPublicKey()) {
                        uint160 hashBytes(Hash160(prevout.scriptPubKey.begin()+1, prevout.scriptPubKey.end()-1));

                        // undo spending activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(1, hashBytes, pindex->nHeight, i, hash, j, true), prevout.nValue * -1));

                        // restore unspent index
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(1, hashBytes, input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));
                    } else {
                        continue;
                    }
                }
            }
        }

        for(unsigned int m = tx.vout.size(); m-- > 0;)
        {
            const CTxOut& txout = tx.vout[m];

            CAppHeader header;
            std::vector<unsigned char> vData;
            if(ParseReserve(txout.vReserve, header, vData))
            {
                CTxDestination dest;
                if(!ExtractDestination(txout.scriptPubKey, dest))
                    continue;

                std::string strAddress = CBitcoinAddress(dest).ToString();

                if(header.nAppCmd == REGISTER_APP_CMD)
                {
                    CAppData appData;
                    if(ParseRegisterData(vData, appData))
                    {
                        appId_appInfo_index.push_back(make_pair(header.appId, CAppId_AppInfo_IndexValue()));
                        appName_appId_index.push_back(make_pair(appData.strAppName, CName_Id_IndexValue()));
                        appTx_index.push_back(make_pair(CAppTx_IndexKey(header.appId, strAddress, REGISTER_TXOUT, COutPoint(hash, m)), -1));
                    }
                }
                else if(header.nAppCmd == ADD_AUTH_CMD || header.nAppCmd == DELETE_AUTH_CMD)
                {
                    CAuthData authData;
                    if(ParseAuthData(vData, authData))
                        appTx_index.push_back(make_pair(CAppTx_IndexKey(header.appId, strAddress, header.nAppCmd == ADD_AUTH_CMD ? ADD_AUTH_TXOUT : DELETE_AUTH_TXOUT, COutPoint(hash, m)), -1));
                }
                else if(header.nAppCmd == CREATE_EXTEND_TX_CMD)
                {
                    appTx_index.push_back(make_pair(CAppTx_IndexKey(header.appId, strAddress, CREATE_EXTENDDATA_TXOUT, COutPoint(hash, m)), -1));
                }
                else if(header.nAppCmd == ISSUE_ASSET_CMD)
                {
                    CAssetData assetData;
                    if(ParseIssueData(vData, assetData))
                    {
                        uint256 assetId = assetData.GetHash();
                        assetId_assetInfo_index.push_back(make_pair(assetId, CAssetId_AssetInfo_IndexValue()));
                        shortName_assetId_index.push_back(make_pair(assetData.strShortName, CName_Id_IndexValue()));
                        assetName_assetId_index.push_back(make_pair(assetData.strAssetName, CName_Id_IndexValue()));
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(assetId, strAddress, ISSUE_TXOUT, COutPoint(hash, m)), -1));
                    }
                }
                else if(header.nAppCmd == ADD_ASSET_CMD)
                {
                    CCommonData addData;
                    if(ParseCommonData(vData, addData))
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(addData.assetId, strAddress, ADD_ISSUE_TXOUT, COutPoint(hash, m)), -1));
                }
                else if(header.nAppCmd == TRANSFER_ASSET_CMD)
                {
                    CCommonData transferData;
                    if(ParseCommonData(vData, transferData))
                    {
                        if(txout.nUnlockedHeight > 0)
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(transferData.assetId, strAddress, LOCKED_TXOUT, COutPoint(hash, m)), -1));
                        else
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(transferData.assetId, strAddress, TRANSFER_TXOUT, COutPoint(hash, m)), -1));

                        for(unsigned int x = 0; x < tx.vin.size(); x++)
                        {
                            const CTxIn& txin = tx.vin[x];
                            const CTxOut& in_txout = view.GetOutputFor(txin);
                            if(!in_txout.IsAsset())
                                continue;
                            std::string strInAddress = "";
                            if(!GetTxOutAddress(in_txout, &strInAddress))
                                continue;
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(transferData.assetId, strInAddress, TRANSFER_TXOUT, COutPoint(hash, -1)), -1));
                        }
                    }
                }
                else if(header.nAppCmd == DESTORY_ASSET_CMD)
                {
                    CCommonData destoryData;
                    if(ParseCommonData(vData, destoryData))
                    {
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(destoryData.assetId, strAddress, DESTORY_TXOUT, COutPoint(hash, m)), -1));
                        for(unsigned int x = 0; x < tx.vin.size(); x++)
                        {
                            const CTxIn& txin = tx.vin[x];
                            const CTxOut& in_txout = view.GetOutputFor(txin);
                            if(!in_txout.IsAsset())
                                continue;
                            std::string strInAddress = "";
                            if(!GetTxOutAddress(in_txout, &strInAddress))
                                continue;
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(destoryData.assetId, strInAddress, DESTORY_TXOUT, COutPoint(hash, -1)), -1));
                        }
                    }
                }
                else if(header.nAppCmd == PUT_CANDY_CMD)
                {
                    CPutCandyData candyData;
                    if(ParsePutCandyData(vData, candyData))
                    {
                        putCandy_index.push_back(make_pair(CPutCandy_IndexKey(candyData.assetId, COutPoint(hash, m), CCandyInfo(candyData.nAmount, candyData.nExpired)), CPutCandy_IndexValue()));
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(candyData.assetId, strAddress, PUT_CANDY_TXOUT, COutPoint(hash, m)), -1));

                        CAssetId_AssetInfo_IndexValue assetInfo;
                        if(GetAssetInfoByAssetId(candyData.assetId, assetInfo))
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(candyData.assetId, assetInfo.strAdminAddress, PUT_CANDY_TXOUT, COutPoint(hash, -1)), -1));
                    }
                }
                else if(header.nAppCmd == GET_CANDY_CMD)
                {
                    CGetCandyData candyData;
                    if(ParseGetCandyData(vData, candyData))
                    {
                        getCandy_index.push_back(make_pair(CGetCandy_IndexKey(candyData.assetId, tx.vin.back().prevout, strAddress), CGetCandy_IndexValue()));
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(candyData.assetId, strAddress, GET_CANDY_TXOUT, COutPoint(hash, m)), -1));
                    }
                }
            }
        }
    }

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    if (pfClean) {
        *pfClean = fClean;
        return true;
    }

    if (fAddressIndex) {
        if (!pblocktree->EraseAddressIndex(addressIndex)) {
            return AbortNode(state, "Failed to delete address index");
        }
        if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex)) {
            return AbortNode(state, "Failed to write address unspent index");
        }
    }

    if (fSpentIndex)
        if (!pblocktree->UpdateSpentIndex(spentIndex))
            return AbortNode(state, "Failed to delete spent index");

    if(appId_appInfo_index.size() && !pblocktree->Erase_AppId_AppInfo_Index(appId_appInfo_index))
        return AbortNode(state, "Failed to delete appId_appInfo index");

    if(appName_appId_index.size() && !pblocktree->Erase_AppName_AppId_Index(appName_appId_index))
        return AbortNode(state, "Failed to delete appName_appId index");

    if(appTx_index.size() && !pblocktree->Erase_AppTx_Index(appTx_index))
        return AbortNode(state, "Failed to delete appTx index");

    if(assetId_assetInfo_index.size() && !pblocktree->Erase_AssetId_AssetInfo_Index(assetId_assetInfo_index))
        return AbortNode(state, "Failed to delete assetId_assetInfo index");

    if(shortName_assetId_index.size() && !pblocktree->Erase_ShortName_AssetId_Index(shortName_assetId_index))
        return AbortNode(state, "Failed to delete shortName_assetId index");

    if(assetName_assetId_index.size() && !pblocktree->Erase_AssetName_AssetId_Index(assetName_assetId_index))
        return AbortNode(state, "Failed to delete assetName_assetId index");

    if(putCandy_index.size() && !pblocktree->Erase_PutCandy_Index(putCandy_index))
        return AbortNode(state, "Failed to delete putcandy index");

    if(getCandy_index.size() && !pblocktree->Erase_GetCandy_Index(getCandy_index))
        return AbortNode(state, "Failed to delete getCandy index");

    if(assetTx_index.size() && !pblocktree->Erase_AssetTx_Index(assetTx_index))
        return AbortNode(state, "Failed to delete assetTx index");

    return fClean;
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);

static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void ThreadScriptCheck() {
    RenameThread("safe-scriptch");
    scriptcheckqueue.Thread();
}

// Protected by cs_main
VersionBitsCache versionbitscache;

int32_t ComputeBlockVersion(const CBlockIndex* pindexPrev, const Consensus::Params& params, bool fAssumeMasternodeIsUpgraded)
{
    LOCK(cs_main);
    int32_t nVersion = VERSIONBITS_TOP_BITS;

    for (int i = 0; i < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; i++) {
        Consensus::DeploymentPos pos = Consensus::DeploymentPos(i);
        ThresholdState state = VersionBitsState(pindexPrev, params, pos, versionbitscache);
        const struct BIP9DeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
        if (vbinfo.check_mn_protocol && state == THRESHOLD_STARTED && !fAssumeMasternodeIsUpgraded) {
            CScript payee;
            masternode_info_t mnInfo;
            if (!mnpayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
                // no votes for this block
                continue;
            }
            if (!mnodeman.GetMasternodeInfo(payee, mnInfo)) {
                // unknown masternode
                continue;
            }
            if (mnInfo.nProtocolVersion < DIP0001_PROTOCOL_VERSION) {
                // masternode is not upgraded yet
                continue;
            }
        }
        if (state == THRESHOLD_LOCKED_IN || state == THRESHOLD_STARTED) {
            nVersion |= VersionBitsMask(params, (Consensus::DeploymentPos)i);
        }
    }

    return nVersion;
}

bool GetBlockHash(uint256& hashRet, int nBlockHeight)
{
    LOCK(cs_main);
    if(chainActive.Tip() == NULL) return false;
    if(nBlockHeight < -1 || nBlockHeight > chainActive.Height()) return false;
    if(nBlockHeight == -1) nBlockHeight = chainActive.Height();
    hashRet = chainActive[nBlockHeight]->GetBlockHash();
    return true;
}

/**
 * Threshold condition checker that triggers when unknown versionbits are seen on the network.
 */
class WarningBitsConditionChecker : public AbstractThresholdConditionChecker
{
private:
    int bit;

public:
    WarningBitsConditionChecker(int bitIn) : bit(bitIn) {}

    int64_t BeginTime(const Consensus::Params& params) const { return 0; }
    int64_t EndTime(const Consensus::Params& params) const { return std::numeric_limits<int64_t>::max(); }
    int Period(const Consensus::Params& params) const { return params.nMinerConfirmationWindow; }
    int Threshold(const Consensus::Params& params) const { return params.nRuleChangeActivationThreshold; }

    bool Condition(const CBlockIndex* pindex, const Consensus::Params& params) const
    {
        return ((pindex->nVersion & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS) &&
               ((pindex->nVersion >> bit) & 1) != 0 &&
               ((ComputeBlockVersion(pindex->pprev, params) >> bit) & 1) == 0;
    }
};

// Protected by cs_main
static ThresholdConditionCache warningcache[VERSIONBITS_NUM_BITS];

static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;
static bool PutChangeInfoToList(const int& nHeight, const CAmount& nReward, const bool fCandy, const map<string, CAmount>& mapAddressAmount);
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, bool fJustCheck)
{
    const CChainParams& chainparams = Params();
    AssertLockHeld(cs_main);

    int64_t nTimeStart = GetTimeMicros();

    // Check it again in case a previous version let a bad block in
    if (!CheckBlock(block, pindex->nHeight, state, !fJustCheck, !fJustCheck))
        return false;

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == NULL ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

    uint256 blockHash = block.GetHash();

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (blockHash == chainparams.GetConsensus().hashGenesisBlock) {
        if (!fJustCheck)
            view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }

    bool fScriptChecks = true;
    if (!hashAssumeValid.IsNull()) {
        // We've been configured with the hash of a block which has been externally verified to have a valid history.
        // A suitable default value is included with the software and updated from time to time.  Because validity
        //  relative to a piece of software is an objective fact these defaults can be easily reviewed.
        // This setting doesn't force the selection of any particular chain but makes validating some faster by
        //  effectively caching the result of part of the verification.
        BlockMap::const_iterator  it = mapBlockIndex.find(hashAssumeValid);
        if (it != mapBlockIndex.end()) {
            if (it->second->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->nChainWork >= UintToArith256(chainparams.GetConsensus().nMinimumChainWork)) {
                // This block is a member of the assumed verified chain and an ancestor of the best header.
                // The equivalent time check discourages hashpower from extorting the network via DOS attack
                //  into accepting an invalid block through telling users they must manually set assumevalid.
                //  Requiring a software change or burying the invalid block, regardless of the setting, makes
                //  it hard to hide the implication of the demand.  This also avoids having release candidates
                //  that are hardly doing any signature verification at all in testing without having to
                //  artificially set the default assumed verified block further back.
                // The test against nMinimumChainWork prevents the skipping when denied access to any chain at
                //  least as good as the expected chain.
                fScriptChecks = (GetBlockProofEquivalentTime(*pindexBestHeader, *pindex, *pindexBestHeader, chainparams.GetConsensus()) <= 60 * 60 * 24 * 7 * 2);
            }
        }
    }

    int64_t nTime1 = GetTimeMicros(); nTimeCheck += nTime1 - nTimeStart;
    LogPrint("bench", "    - Sanity checks: %.2fms [%.2fs]\n", 0.001 * (nTime1 - nTimeStart), nTimeCheck * 0.000001);

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    // If such overwrites are allowed, coinbases and transactions depending upon those
    // can be duplicated to remove the ability to spend the first instance -- even after
    // being sent to another address.
    // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
    // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
    // already refuses previously-known transaction ids entirely.
    // This rule was originally applied to all blocks with a timestamp after March 15, 2012, 0:00 UTC.
    // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
    // two in the chain that violate it. This prevents exploiting the issue against nodes during their
    // initial block download.
    bool fEnforceBIP30 = (!pindex->phashBlock) || // Enforce on CreateNewBlock invocations which don't have a hash.
                          !((pindex->nHeight==91842 && pindex->GetBlockHash() == uint256S("0x00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec")) ||
                           (pindex->nHeight==91880 && pindex->GetBlockHash() == uint256S("0x00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721")));

    // Once BIP34 activated it was not possible to create new duplicate coinbases and thus other than starting
    // with the 2 existing duplicate coinbase pairs, not possible to create overwriting txs.  But by the
    // time BIP34 activated, in each of the existing pairs the duplicate coinbase had overwritten the first
    // before the first had been spent.  Since those coinbases are sufficiently buried its no longer possible to create further
    // duplicate transactions descending from the known pairs either.
    // If we're on the known chain at height greater than where BIP34 activated, we can save the db accesses needed for the BIP30 check.
    CBlockIndex *pindexBIP34height = pindex->pprev->GetAncestor(chainparams.GetConsensus().BIP34Height);
    //Only continue to enforce if we're below BIP34 activation height or the block hash at that height doesn't correspond.
    fEnforceBIP30 = fEnforceBIP30 && (!pindexBIP34height || !(pindexBIP34height->GetBlockHash() == chainparams.GetConsensus().BIP34Hash));

    if (fEnforceBIP30) {
        BOOST_FOREACH(const CTransaction& tx, block.vtx) {
            const CCoins* coins = view.AccessCoins(tx.GetHash());
            if (coins && !coins->IsPruned())
                return state.DoS(100, error("ConnectBlock(): tried to overwrite transaction"),
                                 REJECT_INVALID, "bad-txns-BIP30");
        }
    }

    // BIP16 didn't become active until Apr 1 2012
    int64_t nBIP16SwitchTime = 1333238400;
    bool fStrictPayToScriptHash = (pindex->GetBlockTime() >= nBIP16SwitchTime);

    unsigned int flags = fStrictPayToScriptHash ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE;

    // Start enforcing the DERSIG (BIP66) rules, for block.nVersion=3 blocks,
    // when 75% of the network has upgraded:
    if (block.nVersion >= 3 && IsSuperMajority(3, pindex->pprev, chainparams.GetConsensus().nMajorityEnforceBlockUpgrade, chainparams.GetConsensus())) {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Start enforcing CHECKLOCKTIMEVERIFY, (BIP65) for block.nVersion=4
    // blocks, when 75% of the network has upgraded:
    if (block.nVersion >= 4 && IsSuperMajority(4, pindex->pprev, chainparams.GetConsensus().nMajorityEnforceBlockUpgrade, chainparams.GetConsensus())) {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindex->pprev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV, versionbitscache) == THRESHOLD_ACTIVE) {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
        nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    int64_t nTime2 = GetTimeMicros(); nTimeForks += nTime2 - nTime1;
    LogPrint("bench", "    - Fork checks: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

    CBlockUndo blockundo;

    CCheckQueueControl<CScriptCheck> control(fScriptChecks && nScriptCheckThreads ? &scriptcheckqueue : NULL);

    std::vector<int> prevheights;
    std::vector<int> calprevheights;
    CAmount nFees = 0;
    int nInputs = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;
    std::vector<std::pair<uint256, CAppId_AppInfo_IndexValue> > appId_appInfo_index;
    std::vector<std::pair<std::string, CName_Id_IndexValue> > appName_appId_index;
    std::vector<std::pair<CAuth_IndexKey, int> > auth_index;
    std::vector<std::pair<CAppTx_IndexKey, int> > appTx_index;
    std::vector<std::pair<uint256, CAssetId_AssetInfo_IndexValue> > assetId_assetInfo_index;
    std::vector<std::pair<std::string, CName_Id_IndexValue> > shortName_assetId_index;
    std::vector<std::pair<std::string, CName_Id_IndexValue> > assetName_assetId_index;
    std::vector<std::pair<CPutCandy_IndexKey, CPutCandy_IndexValue> > putCandy_index;
    std::vector<std::pair<CGetCandy_IndexKey, CGetCandy_IndexValue> > getCandy_index;
    std::vector<std::pair<CAssetTx_IndexKey, int> > assetTx_index;

    map<string, CAmount> mapAddressAmount;

    bool fDIP0001Active_context = (VersionBitsState(pindex->pprev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_DIP0001, versionbitscache) == THRESHOLD_ACTIVE);

    for (unsigned int i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction& tx = block.vtx[i];
        const uint256& txhash = tx.GetHash();

        if(!fJustCheck && !CheckAppTransaction(tx, state, view, false))
            return error("ConnectBlock(): CheckAppTransaction on %s failed with %s", txhash.ToString(), FormatStateMessage(state));

        nInputs += tx.vin.size();
        nSigOps += GetLegacySigOpCount(tx);
        if (nSigOps > MaxBlockSigOps(fDIP0001Active_context))
            return state.DoS(100, error("ConnectBlock(): too many sigops"),
                             REJECT_INVALID, "bad-blk-sigops");

        if (!tx.IsCoinBase())
        {
            if (!view.HaveInputs(tx))
                return state.DoS(100, error("ConnectBlock(): inputs missing/spent"),
                                 REJECT_INVALID, "bad-txns-inputs-missingorspent");

            // Check that transaction is BIP68 final
            // BIP68 lock checks (as opposed to nLockTime checks) must
            // be in ConnectBlock because they require the UTXO set
            prevheights.resize(tx.vin.size());
            calprevheights.resize(tx.vin.size());
            for (size_t j = 0; j < tx.vin.size(); j++) {
                prevheights[j] = view.AccessCoins(tx.vin[j].prevout.hash)->nHeight;
                calprevheights[j] = view.AccessCoins(tx.vin[j].prevout.hash)->nHeight;
            }

            if(!fJustCheck && ExistForbidTxin((uint32_t)g_nChainHeight, calprevheights))
                return state.DoS(100, error("%s: contain forbidden transaction(%s) txin", __func__, txhash.GetHex()), REJECT_INVALID, "bad-txns-forbid");

            if (!SequenceLocks(tx, nLockTimeFlags, &prevheights, *pindex)) {
                return state.DoS(100, error("%s: contains a non-BIP68-final transaction", __func__),
                                 REJECT_INVALID, "bad-txns-nonfinal");
            }

            for (size_t j = 0; j < tx.vin.size(); j++) {
                const CTxIn input = tx.vin[j];
                const CTxOut& prevout = view.GetOutputFor(input);

                if (pindex->nHeight >= g_nCriticalHeight)
                {
                    string strAddress = "";
                    if (GetTxOutAddress(prevout, &strAddress))
                    {
                        if (!prevout.IsAsset() && calprevheights[j] >= g_nCriticalHeight)
                        {
                            if (mapAddressAmount.count(strAddress))
                            {
                                mapAddressAmount[strAddress] += -prevout.nValue;
                                if (mapAddressAmount[strAddress] == 0)
                                    mapAddressAmount.erase(strAddress);
                            }
                            else
                                mapAddressAmount[strAddress] = -prevout.nValue;
                        }
                    }
                }

                if (fAddressIndex || fSpentIndex)
                {
                    uint160 hashBytes;
                    int addressType;

                    if (prevout.scriptPubKey.IsPayToScriptHash()) {
                        hashBytes = uint160(vector <unsigned char>(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22));
                        addressType = 2;
                    } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
                        hashBytes = uint160(vector <unsigned char>(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23));
                        addressType = 1;
                    } else if (prevout.scriptPubKey.IsPayToPublicKey()) {
                        hashBytes = Hash160(prevout.scriptPubKey.begin()+1, prevout.scriptPubKey.end()-1);
                        addressType = 1;
                    } else {
                        hashBytes.SetNull();
                        addressType = 0;
                    }

                    if (!prevout.IsAsset() && fAddressIndex && addressType > 0) {
                        // record spending activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(addressType, hashBytes, pindex->nHeight, i, txhash, j, true), prevout.nValue * -1));

                        // remove address from unspent index
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(addressType, hashBytes, input.prevout.hash, input.prevout.n), CAddressUnspentValue()));
                    }

                    if (fSpentIndex) {
                        // add the spent index to determine the txid and input that spent an output
                        // and to find the amount and address from an input
                        spentIndex.push_back(make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue(txhash, j, pindex->nHeight, prevout.nValue, addressType, hashBytes)));
                    }
                }
            }

            if (fStrictPayToScriptHash)
            {
                // Add in sigops done by pay-to-script-hash inputs;
                // this is to prevent a "rogue miner" from creating
                // an incredibly-expensive-to-validate block.
                nSigOps += GetP2SHSigOpCount(tx, view);
                if (nSigOps > MaxBlockSigOps(fDIP0001Active_context))
                    return state.DoS(100, error("ConnectBlock(): too many sigops"),
                                     REJECT_INVALID, "bad-blk-sigops");
            }

            nFees += view.GetValueIn(tx)-tx.GetValueOut();

            std::vector<CScriptCheck> vChecks;
            bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
            if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, nScriptCheckThreads ? &vChecks : NULL))
                return error("ConnectBlock(): CheckInputs on %s failed with %s",
                    txhash.ToString(), FormatStateMessage(state));
            control.Add(vChecks);
        }

        for (unsigned int k = 0; k < tx.vout.size(); k++) {
            const CTxOut &out = tx.vout[k];
            if (out.IsAsset())
                continue;

            string strAddress = "";
            if (pindex->nHeight >= g_nCriticalHeight && GetTxOutAddress(out, &strAddress))
            {
                if(mapAddressAmount.count(strAddress))
                {
                    mapAddressAmount[strAddress] += out.nValue;
                    if(mapAddressAmount[strAddress] == 0)
                        mapAddressAmount.erase(strAddress);
                }
                else
                    mapAddressAmount[strAddress] = out.nValue;
            }

            if (fAddressIndex) {
                if (out.scriptPubKey.IsPayToScriptHash()) {
                    vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);

                    // record receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, i, txhash, k, false), out.nValue));

                    // record unspent output
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(2, uint160(hashBytes), txhash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight)));

                } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                    vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);

                    // record receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, i, txhash, k, false), out.nValue));

                    // record unspent output
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(1, uint160(hashBytes), txhash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight)));

                } else if (out.scriptPubKey.IsPayToPublicKey()) {
                    uint160 hashBytes(Hash160(out.scriptPubKey.begin()+1, out.scriptPubKey.end()-1));

                    // record receiving activity
                    addressIndex.push_back(make_pair(CAddressIndexKey(1, hashBytes, pindex->nHeight, i, txhash, k, false), out.nValue));

                    // record unspent output
                    addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(1, hashBytes, txhash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight)));
                } else {
                    continue;
                }
            }
        }


        for(unsigned int m = 0; m < tx.vout.size(); m++)
        {
            const CTxOut& txout = tx.vout[m];

            CAppHeader header;
            std::vector<unsigned char> vData;
            if(ParseReserve(txout.vReserve, header, vData))
            {
                CTxDestination dest;
                if(!ExtractDestination(txout.scriptPubKey, dest))
                    continue;

                std::string strAddress = CBitcoinAddress(dest).ToString();

                if(header.nAppCmd == REGISTER_APP_CMD)
                {
                    CAppData appData;
                    if(ParseRegisterData(vData, appData))
                    {
                        appId_appInfo_index.push_back(make_pair(header.appId, CAppId_AppInfo_IndexValue(strAddress, appData, pindex->nHeight)));
                        appName_appId_index.push_back(make_pair(appData.strAppName, CName_Id_IndexValue(header.appId, pindex->nHeight)));
                        appTx_index.push_back(make_pair(CAppTx_IndexKey(header.appId, strAddress, REGISTER_TXOUT, COutPoint(txhash, m)), pindex->nHeight));
                    }
                }
                else if(header.nAppCmd == ADD_AUTH_CMD)
                {
                    CAuthData authData;
                    if(ParseAuthData(vData, authData))
                    {
                        appTx_index.push_back(make_pair(CAppTx_IndexKey(header.appId, strAddress, ADD_AUTH_TXOUT, COutPoint(txhash, m)), pindex->nHeight));

                        std::map<uint32_t, int> mapAuth;
                        GetAuthByAppIdAddress(header.appId, authData.strUserAddress, mapAuth);
                        if(authData.nAuth == 0)
                        {
                            if(mapAuth.count(1) != 0)
                                auth_index.push_back(make_pair(CAuth_IndexKey(header.appId, authData.strUserAddress, 1), -1));
                            auth_index.push_back(make_pair(CAuth_IndexKey(header.appId, authData.strUserAddress, 0), pindex->nHeight));
                        }
                        else if(authData.nAuth == 1)
                        {
                            if(mapAuth.count(0) != 0)
                                auth_index.push_back(make_pair(CAuth_IndexKey(header.appId, authData.strUserAddress, 0), -1));
                            auth_index.push_back(make_pair(CAuth_IndexKey(header.appId, authData.strUserAddress, 1), pindex->nHeight));
                        }
                        else
                        {
                            auth_index.push_back(make_pair(CAuth_IndexKey(header.appId, authData.strUserAddress, authData.nAuth), pindex->nHeight));
                        }
                    }
                }
                else if(header.nAppCmd == DELETE_AUTH_CMD)
                {
                    CAuthData authData;
                    if(ParseAuthData(vData, authData))
                    {
                        appTx_index.push_back(make_pair(CAppTx_IndexKey(header.appId, strAddress, DELETE_AUTH_TXOUT, COutPoint(txhash, m)), pindex->nHeight));

                        std::map<uint32_t, int> mapAuth;
                        GetAuthByAppIdAddress(header.appId, authData.strUserAddress, mapAuth);
                        if(mapAuth.count(authData.nAuth) != 0)
                            auth_index.push_back(make_pair(CAuth_IndexKey(header.appId, authData.strUserAddress, authData.nAuth), -1));
                    }
                }
                else if(header.nAppCmd == CREATE_EXTEND_TX_CMD)
                {
                    appTx_index.push_back(make_pair(CAppTx_IndexKey(header.appId, strAddress, CREATE_EXTENDDATA_TXOUT, COutPoint(txhash, m)), pindex->nHeight));
                }
                else if(header.nAppCmd == ISSUE_ASSET_CMD)
                {
                    CAssetData assetData;
                    if(ParseIssueData(vData, assetData))
                    {
                        uint256 assetId = assetData.GetHash();
                        assetId_assetInfo_index.push_back(make_pair(assetId, CAssetId_AssetInfo_IndexValue(strAddress, assetData, pindex->nHeight)));
                        shortName_assetId_index.push_back(make_pair(assetData.strShortName, CName_Id_IndexValue(assetId, pindex->nHeight)));
                        assetName_assetId_index.push_back(make_pair(assetData.strAssetName, CName_Id_IndexValue(assetId, pindex->nHeight)));
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(assetId, strAddress, ISSUE_TXOUT, COutPoint(txhash, m)), pindex->nHeight));
                    }
                }
                else if(header.nAppCmd == ADD_ASSET_CMD)
                {
                    CCommonData addData;
                    if(ParseCommonData(vData, addData))
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(addData.assetId, strAddress, ADD_ISSUE_TXOUT, COutPoint(txhash, m)), pindex->nHeight));
                }
                else if (header.nAppCmd == CHANGE_ASSET_CMD)
                {
                    CCommonData changeData;
                    if(ParseCommonData(vData, changeData))
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(changeData.assetId, strAddress, CHANGE_ASSET_TXOUT, COutPoint(txhash, m)), pindex->nHeight));
                }
                else if(header.nAppCmd == TRANSFER_ASSET_CMD)
                {
                    CCommonData transferData;
                    if(ParseCommonData(vData, transferData))
                    {
                        if(txout.nUnlockedHeight > 0)
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(transferData.assetId, strAddress, LOCKED_TXOUT, COutPoint(txhash, m)), pindex->nHeight));
                        else
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(transferData.assetId, strAddress, TRANSFER_TXOUT, COutPoint(txhash, m)), pindex->nHeight));

                        for(unsigned int x = 0; x < tx.vin.size(); x++)
                        {
                            const CTxIn& txin = tx.vin[x];
                            const CTxOut& in_txout = view.GetOutputFor(txin);
                            if(!in_txout.IsAsset())
                                continue;
                            std::string strInAddress = "";
                            if(!GetTxOutAddress(in_txout, &strInAddress))
                                continue;
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(transferData.assetId, strInAddress, TRANSFER_TXOUT, COutPoint(txhash, -1)), pindex->nHeight));
                        }
                    }
                }
                else if(header.nAppCmd == DESTORY_ASSET_CMD)
                {
                    CCommonData destoryData;
                    if(ParseCommonData(vData, destoryData))
                    {
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(destoryData.assetId, strAddress, DESTORY_TXOUT, COutPoint(txhash, m)), pindex->nHeight));
                        for(unsigned int x = 0; x < tx.vin.size(); x++)
                        {
                            const CTxIn& txin = tx.vin[x];
                            const CTxOut& in_txout = view.GetOutputFor(txin);
                            if(!in_txout.IsAsset())
                                continue;
                            std::string strInAddress = "";
                            if(!GetTxOutAddress(in_txout, &strInAddress))
                                continue;
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(destoryData.assetId, strInAddress, DESTORY_TXOUT, COutPoint(txhash, -1)), pindex->nHeight));
                        }
                    }
                }
                else if(header.nAppCmd == PUT_CANDY_CMD)
                {
                    CPutCandyData candyData;
                    if(ParsePutCandyData(vData, candyData))
                    {
                        putCandy_index.push_back(make_pair(CPutCandy_IndexKey(candyData.assetId, COutPoint(txhash, m), CCandyInfo(candyData.nAmount, candyData.nExpired)), CPutCandy_IndexValue(pindex->nHeight, blockHash, i)));
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(candyData.assetId, strAddress, PUT_CANDY_TXOUT, COutPoint(txhash, m)), pindex->nHeight));

                        CAssetId_AssetInfo_IndexValue assetInfo;
                        if(GetAssetInfoByAssetId(candyData.assetId, assetInfo))
                            assetTx_index.push_back(make_pair(CAssetTx_IndexKey(candyData.assetId, assetInfo.strAdminAddress, PUT_CANDY_TXOUT, COutPoint(txhash, -1)), pindex->nHeight));
                    }
                }
                else if(header.nAppCmd == GET_CANDY_CMD)
                {
                    CGetCandyData candyData;
                    if(ParseGetCandyData(vData, candyData))
                    {
                        getCandy_index.push_back(make_pair(CGetCandy_IndexKey(candyData.assetId, tx.vin.back().prevout, strAddress), CGetCandy_IndexValue(candyData.nAmount, pindex->nHeight, blockHash, i)));
                        assetTx_index.push_back(make_pair(CAssetTx_IndexKey(candyData.assetId, strAddress, GET_CANDY_TXOUT, COutPoint(txhash, m)), pindex->nHeight));
                    }
                }
            }
        }

        CTxUndo undoDummy;
        if (i > 0) {
            blockundo.vtxundo.push_back(CTxUndo());
        }
        UpdateCoins(tx, state, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        vPos.push_back(std::make_pair(txhash, pos));
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }
    int64_t nTime3 = GetTimeMicros(); nTimeConnect += nTime3 - nTime2;
    LogPrint("bench", "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)block.vtx.size(), 0.001 * (nTime3 - nTime2), 0.001 * (nTime3 - nTime2) / block.vtx.size(), nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs-1), nTimeConnect * 0.000001);

    // SAFE : MODIFIED TO CHECK MASTERNODE PAYMENTS AND SUPERBLOCKS

    // It's possible that we simply don't have enough data and this could fail
    // (i.e. block itself could be a correct one and we need to store it),
    // that's why this is in ConnectBlock. Could be the other way around however -
    // the peer who sent us this block is missing some data and wasn't able
    // to recognize that block is actually invalid.
    // TODO: resync data (both ways?) and try to reprocess this block later.
    CAmount blockReward = 0;
    if(CheckCriticalBlock(block))
        blockReward = nFees + g_nCriticalReward;
    else
        blockReward = nFees + GetBlockSubsidy(pindex->pprev->nBits, pindex->pprev->nHeight, chainparams.GetConsensus());
    std::string strError = "";
    if (!IsBlockValueValid(block, pindex->nHeight, blockReward, strError)) {
        return state.DoS(0, error("ConnectBlock(SAFE): %s", strError), REJECT_INVALID, "bad-cb-amount");
    }

    if (!IsBlockPayeeValid(block.vtx[0], pindex->nHeight, blockReward)) {
        mapRejectedBlocks.insert(make_pair(blockHash, GetTime()));
        return state.DoS(0, error("ConnectBlock(SAFE): couldn't find masternode or superblock payments"),
                                REJECT_INVALID, "bad-cb-payee");
    }
    // END SAFE

    if (!control.Wait())
        return state.DoS(100, false);
    int64_t nTime4 = GetTimeMicros(); nTimeVerify += nTime4 - nTime2;
    LogPrint("bench", "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime4 - nTime2), nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs-1), nTimeVerify * 0.000001);

    if (fJustCheck)
        return true;

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        if (pindex->GetUndoPos().IsNull()) {
            CDiskBlockPos pos;
            if (!FindUndoPos(state, pindex->nFile, pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("ConnectBlock(): FindUndoPos failed");
            if (!UndoWriteToDisk(blockundo, pos, pindex->pprev->GetBlockHash(), chainparams.MessageStart()))
                return AbortNode(state, "Failed to write undo data");

            // update nUndoPos in block index
            pindex->nUndoPos = pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }

    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    if (fAddressIndex) {
        if (!pblocktree->WriteAddressIndex(addressIndex)) {
            return AbortNode(state, "Failed to write address index");
        }

        if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex)) {
            return AbortNode(state, "Failed to write address unspent index");
        }
    }

    if (fSpentIndex)
        if (!pblocktree->UpdateSpentIndex(spentIndex))
            return AbortNode(state, "Failed to write transaction index");

    if (fTimestampIndex)
        if (!pblocktree->WriteTimestampIndex(CTimestampIndexKey(pindex->nTime, pindex->GetBlockHash())))
            return AbortNode(state, "Failed to write timestamp index");

    if(appId_appInfo_index.size() && !pblocktree->Write_AppId_AppInfo_Index(appId_appInfo_index))
        return AbortNode(state, "Failed to write appId_appInfo index");

    if(appName_appId_index.size() && !pblocktree->Write_AppName_AppId_Index(appName_appId_index))
        return AbortNode(state, "Failed to write appName_appId index");

    if(auth_index.size() && !pblocktree->Update_Auth_Index(auth_index))
        return AbortNode(state, "Failed to update auth index");

    if(appTx_index.size() && !pblocktree->Write_AppTx_Index(appTx_index))
        return AbortNode(state, "Failed to write appTx index");

    if(assetId_assetInfo_index.size() && !pblocktree->Write_AssetId_AssetInfo_Index(assetId_assetInfo_index))
        return AbortNode(state, "Failed to write assetId_assetInfo index");

    if(shortName_assetId_index.size() && !pblocktree->Write_ShortName_AssetId_Index(shortName_assetId_index))
        return AbortNode(state, "Failed to write shortName_assetId index");

    if(assetName_assetId_index.size() && !pblocktree->Write_AssetName_AssetId_Index(assetName_assetId_index))
        return AbortNode(state, "Failed to write assetName_assetId index");

    if(putCandy_index.size() && !pblocktree->Write_PutCandy_Index(putCandy_index))
        return AbortNode(state, "Failed to write putcandy index");

    if(getCandy_index.size() && !pblocktree->Write_GetCandy_Index(getCandy_index))
        return AbortNode(state, "Failed to write getCandy index");

    if(assetTx_index.size() && !pblocktree->Write_AssetTx_Index(assetTx_index))
        return AbortNode(state, "Failed to write assetTx index");

    while(GetChangeInfoListSize() >= g_nListChangeInfoLimited)
    {
        boost::this_thread::interruption_point();
        if(ShutdownRequested())
            break;
    }
    if(!PutChangeInfoToList(pindex->nHeight, blockReward - nFees, !putCandy_index.empty(), mapAddressAmount))
        return AbortNode(state, "Failed to write change info");

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    if(masternodeSync.IsBlockchainSynced())
    {
        for(std::vector<std::pair<std::string, CName_Id_IndexValue> >::const_iterator it = assetName_assetId_index.begin(); it != assetName_assetId_index.end(); it++)
            uiInterface.AssetFound(it->first);
    }

    int64_t nTime5 = GetTimeMicros(); nTimeIndex += nTime5 - nTime4;
    LogPrint("bench", "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime5 - nTime4), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction.
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0].GetHash();

    int64_t nTime6 = GetTimeMicros(); nTimeCallbacks += nTime6 - nTime5;
    LogPrint("bench", "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime6 - nTime5), nTimeCallbacks * 0.000001);

    return true;
}

/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're in prune mode and are deleting files.
 */
bool static FlushStateToDisk(CValidationState &state, FlushStateMode mode) {
    const CChainParams& chainparams = Params();
    LOCK2(cs_main, cs_LastBlockFile);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    std::set<int> setFilesToPrune;
    bool fFlushForPrune = false;
    try {
    if (fPruneMode && fCheckForPruning && !fReindex) {
        FindFilesToPrune(setFilesToPrune, chainparams.PruneAfterHeight());
        fCheckForPruning = false;
        if (!setFilesToPrune.empty()) {
            fFlushForPrune = true;
            if (!fHavePruned) {
                pblocktree->WriteFlag("prunedblockfiles", true);
                fHavePruned = true;
            }
        }
    }
    int64_t nNow = GetTimeMicros();
    // Avoid writing/flushing immediately after startup.
    if (nLastWrite == 0) {
        nLastWrite = nNow;
    }
    if (nLastFlush == 0) {
        nLastFlush = nNow;
    }
    if (nLastSetChain == 0) {
        nLastSetChain = nNow;
    }
    size_t cacheSize = pcoinsTip->DynamicMemoryUsage();
    // The cache is large and close to the limit, but we have time now (not in the middle of a block processing).
    bool fCacheLarge = mode == FLUSH_STATE_PERIODIC && cacheSize * (10.0/9) > nCoinCacheUsage;
    // The cache is over the limit, we have to write now.
    bool fCacheCritical = mode == FLUSH_STATE_IF_NEEDED && cacheSize > nCoinCacheUsage;
    // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash.
    bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
    // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
    bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
    // Combine all conditions that result in a full cache flush.
    bool fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheLarge || fCacheCritical || fPeriodicFlush || fFlushForPrune;
    // Write blocks and block index to disk.
    if (fDoFullFlush || fPeriodicWrite) {
        // Depend on nMinDiskSpace to ensure we can write block index
        if (!CheckDiskSpace(0))
            return state.Error("out of disk space");
        // First make sure all block and undo data is flushed to disk.
        FlushBlockFile();
        // Then update all block file information (which may refer to block and undo files).
        {
            std::vector<std::pair<int, const CBlockFileInfo*> > vFiles;
            vFiles.reserve(setDirtyFileInfo.size());
            for (set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
                vFiles.push_back(make_pair(*it, &vinfoBlockFile[*it]));
                setDirtyFileInfo.erase(it++);
            }
            std::vector<const CBlockIndex*> vBlocks;
            vBlocks.reserve(setDirtyBlockIndex.size());
            for (set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
                vBlocks.push_back(*it);
                setDirtyBlockIndex.erase(it++);
            }
            if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks)) {
                return AbortNode(state, "Files to write to block index database");
            }
        }
        // Finally remove any pruned files
        if (fFlushForPrune)
            UnlinkPrunedFiles(setFilesToPrune);
        nLastWrite = nNow;
    }
    // Flush best chain related state. This can only be done if the blocks / block index write was also done.
    if (fDoFullFlush) {
        // Typical CCoins structures on disk are around 128 bytes in size.
        // Pushing a new one to the database can cause it to be written
        // twice (once in the log, and once in the tables). This is already
        // an overestimation, as most will delete an existing entry or
        // overwrite one. Still, use a conservative safety factor of 2.
        if (!CheckDiskSpace(128 * 2 * 2 * pcoinsTip->GetCacheSize()))
            return state.Error("out of disk space");
        // Flush the chainstate (which may refer to block index entries).
        if (!pcoinsTip->Flush())
            return AbortNode(state, "Failed to write to coin database");
        nLastFlush = nNow;
    }
    if (fDoFullFlush || ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) && nNow > nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000)) {
        // Update best block in wallet (so we can detect restored wallets).
        GetMainSignals().SetBestChain(chainActive.GetLocator());
        nLastSetChain = nNow;
    }
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk() {
    CValidationState state;
    FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
}

void PruneAndFlush() {
    CValidationState state;
    fCheckForPruning = true;
    FlushStateToDisk(state, FLUSH_STATE_NONE);
}

/** Update chainActive and related internal data structures. */
void static UpdateTip(CBlockIndex *pindexNew) {
    const CChainParams& chainParams = Params();
    chainActive.SetTip(pindexNew);

    // New best block
    mempool.AddTransactionsUpdated(1);

    g_nChainHeight = chainActive.Height();
    LogPrintf("%s: new best=%s  height=%d  log2_work=%.8g  tx=%lu  date=%s progress=%f  cache=%.1fMiB(%utx)\n", __func__,
      chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(), log(chainActive.Tip()->nChainWork.getdouble())/log(2.0), (unsigned long)chainActive.Tip()->nChainTx,
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
      Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip()), pcoinsTip->DynamicMemoryUsage() * (1.0 / (1<<20)), pcoinsTip->GetCacheSize());

    cvBlockChange.notify_all();

    // Check the version of the last 100 blocks to see if we need to upgrade:
    static bool fWarned = false;
    if (!IsInitialBlockDownload())
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = chainActive.Tip();
        for (int bit = 0; bit < VERSIONBITS_NUM_BITS; bit++) {
            WarningBitsConditionChecker checker(bit);
            ThresholdState state = checker.GetStateFor(pindex, chainParams.GetConsensus(), warningcache[bit]);
            if (state == THRESHOLD_ACTIVE || state == THRESHOLD_LOCKED_IN) {
                if (state == THRESHOLD_ACTIVE) {
                    strMiscWarning = strprintf(_("Warning: unknown new rules activated (versionbit %i)"), bit);
                    if (!fWarned) {
                        CAlert::Notify(strMiscWarning, true);
                        fWarned = true;
                    }
                } else {
                    LogPrintf("%s: unknown new rules are about to activate (versionbit %i)\n", __func__, bit);
                }
            }
        }
        for (int i = 0; i < 100 && pindex != NULL; i++)
        {
            int32_t nExpectedVersion = ComputeBlockVersion(pindex->pprev, chainParams.GetConsensus(), true);
            if (pindex->nVersion > VERSIONBITS_LAST_OLD_BLOCK_VERSION && (pindex->nVersion & ~nExpectedVersion) != 0)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            LogPrintf("%s: %d of last 100 blocks have unexpected version\n", __func__, nUpgraded);
        if (nUpgraded > 100/2)
        {
            // strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            strMiscWarning = _("Warning: Unknown block versions being mined! It's possible unknown rules are in effect");
            if (!fWarned) {
                CAlert::Notify(strMiscWarning, true);
                fWarned = true;
            }
        }
    }
}

/** Disconnect chainActive's tip. You probably want to call mempool.removeForReorg and manually re-limit mempool size after this, with cs_main held. */
bool static DisconnectTip(CValidationState& state, const Consensus::Params& consensusParams)
{
    CBlockIndex *pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete, consensusParams))
        return AbortNode(state, "Failed to read block");
    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        if (!DisconnectBlock(block, state, pindexDelete, view))
            return error("DisconnectTip(): DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        assert(view.Flush());
    }
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
        return false;
    // Resurrect mempool transactions from the disconnected block.
    std::vector<uint256> vHashUpdate;
    BOOST_FOREACH(const CTransaction &tx, block.vtx) {
        // ignore validation errors in resurrected transactions
        list<CTransaction> removed;
        CValidationState stateDummy;
        if (tx.IsCoinBase() || !AcceptToMemoryPool(mempool, stateDummy, tx, false, NULL, true)) {
            mempool.remove(tx, removed, true);
        } else if (mempool.exists(tx.GetHash())) {
            vHashUpdate.push_back(tx.GetHash());
        }
    }
    // AcceptToMemoryPool/addUnchecked all assume that new mempool entries have
    // no in-mempool children, which is generally not true when adding
    // previously-confirmed transactions back to the mempool.
    // UpdateTransactionsFromBlock finds descendants of any transactions in this
    // block that were added back and cleans up the mempool state.
    mempool.UpdateTransactionsFromBlock(vHashUpdate);
    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    BOOST_FOREACH(const CTransaction &tx, block.vtx) {
        GetMainSignals().SyncTransaction(tx, NULL);
    }
    return true;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/**
 * Connect a new block to chainActive. pblock is either NULL or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 */
bool static ConnectTip(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindexNew, const CBlock* pblock)
{
    assert(pindexNew->pprev == chainActive.Tip());
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    CBlock block;
    if (!pblock) {
        if (!ReadBlockFromDisk(block, pindexNew, chainparams.GetConsensus()))
            return AbortNode(state, "Failed to read block");
        pblock = &block;
    }
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros(); nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint("bench", "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(pcoinsTip);
        bool rv = ConnectBlock(*pblock, state, pindexNew, view);
        GetMainSignals().BlockChecked(*pblock, state);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state);
            return error("ConnectTip(): ConnectBlock %s failed", pindexNew->GetBlockHash().ToString());
        }
        nTime3 = GetTimeMicros(); nTimeConnectTotal += nTime3 - nTime2;
        LogPrint("bench", "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        assert(view.Flush());
    }
    int64_t nTime4 = GetTimeMicros(); nTimeFlush += nTime4 - nTime3;
    LogPrint("bench", "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
        return false;
    int64_t nTime5 = GetTimeMicros(); nTimeChainState += nTime5 - nTime4;
    LogPrint("bench", "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);
    // Remove conflicting transactions from the mempool.
    list<CTransaction> txConflicted;
    mempool.removeForBlock(pblock->vtx, pindexNew->nHeight, txConflicted, !IsInitialBlockDownload());
    // Update chainActive & related variables.
    UpdateTip(pindexNew);
    // Tell wallet about transactions that went from mempool
    // to conflicted:
    BOOST_FOREACH(const CTransaction &tx, txConflicted) {
        GetMainSignals().SyncTransaction(tx, NULL);
    }
    // ... and about transactions that got confirmed:
    BOOST_FOREACH(const CTransaction &tx, pblock->vtx) {
        GetMainSignals().SyncTransaction(tx, pblock);
    }

    int64_t nTime6 = GetTimeMicros(); nTimePostConnect += nTime6 - nTime5; nTimeTotal += nTime6 - nTime1;
    LogPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}

bool DisconnectBlocks(int blocks)
{
    LOCK(cs_main);

    CValidationState state;
    const CChainParams& chainparams = Params();

    LogPrintf("DisconnectBlocks -- Got command to replay %d blocks\n", blocks);
    for(int i = 0; i < blocks; i++) {
        if(!DisconnectTip(state, chainparams.GetConsensus()) || !state.IsValid()) {
            return false;
        }
    }

    return true;
}

void ReprocessBlocks(int nBlocks)
{
    LOCK(cs_main);

    std::map<uint256, int64_t>::iterator it = mapRejectedBlocks.begin();
    while(it != mapRejectedBlocks.end()){
        //use a window twice as large as is usual for the nBlocks we want to reset
        if((*it).second  > GetTime() - (nBlocks*60*5)) {
            BlockMap::iterator mi = mapBlockIndex.find((*it).first);
            if (mi != mapBlockIndex.end() && (*mi).second) {

                CBlockIndex* pindex = (*mi).second;
                LogPrintf("ReprocessBlocks -- %s\n", (*it).first.ToString());

                CValidationState state;
                ReconsiderBlock(state, pindex);
            }
        }
        ++it;
    }

    DisconnectBlocks(nBlocks);

    CValidationState state;
    ActivateBestChain(state, Params());
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
static CBlockIndex* FindMostWorkChain() {
    do {
        CBlockIndex *pindexNew = NULL;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return NULL;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (pindexBestInvalid == NULL || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
                    pindexBestInvalid = pindexNew;
                CBlockIndex *pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setBlockIndexCandidates again.
                        mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while(true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates() {
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex*, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either NULL or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindexMostWork, const CBlock* pblock, bool& fInvalidFound)
{
    AssertLockHeld(cs_main);
    const CBlockIndex *pindexOldTip = chainActive.Tip();
    const CBlockIndex *pindexFork = chainActive.FindFork(pindexMostWork);

    // Disconnect active blocks which are no longer in the best chain.
    bool fBlocksDisconnected = false;
    while (chainActive.Tip() && chainActive.Tip() != pindexFork) {
        if (!DisconnectTip(state, chainparams.GetConsensus()))
            return false;
        fBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex*> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        BOOST_REVERSE_FOREACH(CBlockIndex *pindexConnect, vpindexToConnect) {
            if (!ConnectTip(state, chainparams, pindexConnect, pindexConnect == pindexMostWork ? pblock : NULL)) {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back());
                    state = CValidationState();
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                } else {
                    // A system error occurred (disk space, database error, ...).
                    return false;
                }
            } else {
                PruneBlockIndexCandidates();
                if (!pindexOldTip || chainActive.Tip()->nChainWork > pindexOldTip->nChainWork) {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    fContinue = false;
                    break;
                }
            }
        }
    }

    if (fBlocksDisconnected) {
        mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
        LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
    }
    mempool.check(pcoinsTip);

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
    else
        CheckForkWarningConditions();

    return true;
}

static void NotifyHeaderTip() {
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    static CBlockIndex* pindexHeaderOld = NULL;
    CBlockIndex* pindexHeader = NULL;
    {
        LOCK(cs_main);
        pindexHeader = pindexBestHeader;

        if (pindexHeader != pindexHeaderOld) {
            fNotify = true;
            fInitialBlockDownload = IsInitialBlockDownload();
            pindexHeaderOld = pindexHeader;
        }
    }
    // Send block tip changed notifications without cs_main
    if (fNotify) {
        uiInterface.NotifyHeaderTip(fInitialBlockDownload, pindexHeader);
        GetMainSignals().NotifyHeaderTip(pindexHeader, fInitialBlockDownload);
    }
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState &state, const CChainParams& chainparams, const CBlock *pblock) {
    CBlockIndex *pindexMostWork = NULL;
    CBlockIndex *pindexNewTip = NULL;
    do {
        boost::this_thread::interruption_point();
        if (ShutdownRequested())
            break;

        const CBlockIndex *pindexFork;
        bool fInitialDownload;
        {
            LOCK(cs_main);
            CBlockIndex *pindexOldTip = chainActive.Tip();
            if (pindexMostWork == NULL) {
                pindexMostWork = FindMostWorkChain();
            }

            // Whether we have anything to do at all.
            if (pindexMostWork == NULL || pindexMostWork == chainActive.Tip())
                return true;

            bool fInvalidFound = false;
            if (!ActivateBestChainStep(state, chainparams, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : NULL, fInvalidFound))
                return false;

            if (fInvalidFound) {
                // Wipe cache, we may need another branch now.
                pindexMostWork = NULL;
            }
            pindexNewTip = chainActive.Tip();
            pindexFork = chainActive.FindFork(pindexOldTip);
            fInitialDownload = IsInitialBlockDownload();
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main

        // Notify external listeners about the new tip.
        GetMainSignals().UpdatedBlockTip(pindexNewTip, pindexFork, fInitialDownload);

        // Always notify the UI if a new block tip was connected
        if (pindexFork != pindexNewTip) {
            uiInterface.NotifyBlockTip(fInitialDownload, pindexNewTip);
        }
    } while (pindexNewTip != pindexMostWork);
    CheckBlockIndex(chainparams.GetConsensus());

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC)) {
        return false;
    }

    return true;
}

bool InvalidateBlock(CValidationState& state, const Consensus::Params& consensusParams, CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);

    while (chainActive.Contains(pindex)) {
        CBlockIndex *pindexWalk = chainActive.Tip();
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
        setDirtyBlockIndex.insert(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state, consensusParams)) {
            mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
            return false;
        }
    }

    LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add it again.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && !setBlockIndexCandidates.value_comp()(it->second, chainActive.Tip())) {
            setBlockIndexCandidates.insert(it->second);
        }
        it++;
    }

    InvalidChainFound(pindex);
    mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->pprev);
    return true;
}

bool ReconsiderBlock(CValidationState& state, CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (!it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex) {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && setBlockIndexCandidates.value_comp()(chainActive.Tip(), it->second)) {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = NULL;
            }
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != NULL) {
        if (pindex->nStatus & BLOCK_FAILED_MASK) {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

CBlockIndex* AddToBlockIndex(const CBlockHeader& block)
{
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (pindexBestHeader == NULL || pindexBestHeader->nChainWork < pindexNew->nChainWork)
        pindexBestHeader = pindexNew;

    setDirtyBlockIndex.insert(pindexNew);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock &block, CValidationState& state, CBlockIndex *pindexNew, const CDiskBlockPos& pos)
{
    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == NULL || pindexNew->pprev->nChainTx) {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chainActive.Tip() == NULL || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

bool FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown) {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE) {
            nFile++;
            if (vinfoBlockFile.size() <= nFile) {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if ((int)nFile != nLastBlockFile) {
        if (!fKnown) {
            LogPrintf("Leaving block file %i: %s\n", nLastBlockFile, vinfoBlockFile[nLastBlockFile].ToString());
        }
        FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown) {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (fPruneMode)
                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE *file = OpenBlockFile(pos);
                if (file) {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            }
            else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize)
{
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    unsigned int nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (fPruneMode)
            fCheckForPruning = true;
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE *file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
            return state.Error("out of disk space");
    }

    return true;
}

bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, bool fCheckPOW)
{
    if(CheckCriticalBlock(block))
        return true;

    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, Params().GetConsensus()))
        return state.DoS(50, error("CheckBlockHeader(): proof of work failed"),
                         REJECT_INVALID, "high-hash");

    // Check timestamp
    if (block.GetBlockTime() > GetAdjustedTime() + 2 * 60 * 60)
        return state.Invalid(error("CheckBlockHeader(): block timestamp too far in the future"),
                             REJECT_INVALID, "time-too-new");

    return true;
}

bool CheckBlock(const CBlock& block, const int& nHeight, CValidationState& state, bool fCheckPOW, bool fCheckMerkleRoot)
{
    // These are checks that are independent of context.

    if (block.fChecked)
        return true;

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, fCheckPOW))
        return false;

    // Check the merkle root.
    if (fCheckMerkleRoot) {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, error("CheckBlock(): hashMerkleRoot mismatch"),
                             REJECT_INVALID, "bad-txnmrklroot", true);

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, error("CheckBlock(): duplicate transaction"),
                             REJECT_INVALID, "bad-txns-duplicate", true);
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits (relaxed)
    if (block.vtx.empty() || block.vtx.size() > MaxBlockSize(true) || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > MaxBlockSize(true))
        return state.DoS(100, error("%s: size limits failed", __func__),
                         REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, error("CheckBlock(): first tx is not coinbase"),
                         REJECT_INVALID, "bad-cb-missing");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i].IsCoinBase())
            return state.DoS(100, error("CheckBlock(): more than one coinbase"),
                             REJECT_INVALID, "bad-cb-multiple");


    // SAFE : CHECK TRANSACTIONS FOR INSTANTSEND

    if(sporkManager.IsSporkActive(SPORK_3_INSTANTSEND_BLOCK_FILTERING)) {
        // We should never accept block which conflicts with completed transaction lock,
        // that's why this is in CheckBlock unlike coinbase payee/amount.
        // Require other nodes to comply, send them some data in case they are missing it.
        BOOST_FOREACH(const CTransaction& tx, block.vtx) {
            // skip coinbase, it has no inputs
            if (tx.IsCoinBase()) continue;
            // LOOK FOR TRANSACTION LOCK IN OUR MAP OF OUTPOINTS
            BOOST_FOREACH(const CTxIn& txin, tx.vin) {
                uint256 hashLocked;
                if(instantsend.GetLockedOutPointTxHash(txin.prevout, hashLocked) && hashLocked != tx.GetHash()) {
                    // The node which relayed this will have to swtich later,
                    // relaying instantsend data won't help it.
                    LOCK(cs_main);
                    mapRejectedBlocks.insert(make_pair(block.GetHash(), GetTime()));
                    return state.DoS(0, error("CheckBlock(SAFE): transaction %s conflicts with transaction lock %s",
                                                tx.GetHash().ToString(), hashLocked.ToString()),
                                     REJECT_INVALID, "conflict-tx-lock");
                }
            }
        }
    } else {
        LogPrintf("CheckBlock(SAFE): spork is off, skipping transaction locking checks\n");
    }

    // END SAFE

    // Check transactions
    BOOST_FOREACH(const CTransaction& tx, block.vtx)
        if (!CheckTransaction(tx, state, FROM_BLOCK, nHeight))
            return error("CheckBlock(): CheckTransaction of %s failed with %s",
                tx.GetHash().ToString(),
                FormatStateMessage(state));

    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTransaction& tx, block.vtx)
    {
        nSigOps += GetLegacySigOpCount(tx);
    }
    // sigops limits (relaxed)
    if (nSigOps > MaxBlockSigOps(true))
        return state.DoS(100, error("CheckBlock(): out-of-bounds SigOpCount"),
                         REJECT_INVALID, "bad-blk-sigops");

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}

static bool CheckIndexAgainstCheckpoint(const CBlockIndex* pindexPrev, CValidationState& state, const CChainParams& chainparams, const uint256& hash)
{
    if (*pindexPrev->phashBlock == chainparams.GetConsensus().hashGenesisBlock)
        return true;

    int nHeight = pindexPrev->nHeight+1;
    // Don't accept any forks from the main chain prior to last checkpoint
    CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
    if (pcheckpoint && nHeight < pcheckpoint->nHeight)
        return state.DoS(100, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight));

    return true;
}

bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex * const pindexPrev)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    /*
    int nHeight = pindexPrev->nHeight + 1;
    // Check proof of work
    if(Params().NetworkIDString() == CBaseChainParams::MAIN && nHeight <= 68589){
        // architecture issues with DGW v1 and v2)
        unsigned int nBitsNext = GetNextWorkRequired(pindexPrev, &block, consensusParams);
        double n1 = ConvertBitsToDouble(block.nBits);
        double n2 = ConvertBitsToDouble(nBitsNext);

        if (abs(n1-n2) > n1*0.5)
            return state.DoS(100, error("%s : incorrect proof of work (DGW pre-fork) - %f %f %f at %d", __func__, abs(n1-n2), n1, n2, nHeight),
                            REJECT_INVALID, "bad-diffbits");
    } else {
        if (!CheckCriticalBlock(block) && block.nBits != GetNextWorkRequired(pindexPrev, &block, consensusParams))
            return state.DoS(100, error("%s : incorrect proof of work at %d", __func__, nHeight),
                            REJECT_INVALID, "bad-diffbits");
    }
    */

    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast())
        return state.Invalid(error("%s: block's timestamp is too early", __func__),
                             REJECT_INVALID, "time-too-old");

    // Reject block.nVersion=1 blocks when 95% (75% on testnet) of the network has upgraded:
    if (block.nVersion < 2 && IsSuperMajority(2, pindexPrev, consensusParams.nMajorityRejectBlockOutdated, consensusParams))
        return state.Invalid(error("%s: rejected nVersion=1 block", __func__),
                             REJECT_OBSOLETE, "bad-version");

    // Reject block.nVersion=2 blocks when 95% (75% on testnet) of the network has upgraded:
    if (block.nVersion < 3 && IsSuperMajority(3, pindexPrev, consensusParams.nMajorityRejectBlockOutdated, consensusParams))
        return state.Invalid(error("%s: rejected nVersion=2 block", __func__),
                             REJECT_OBSOLETE, "bad-version");

    // Reject block.nVersion=3 blocks when 95% (75% on testnet) of the network has upgraded:
    if (block.nVersion < 4 && IsSuperMajority(4, pindexPrev, consensusParams.nMajorityRejectBlockOutdated, consensusParams))
        return state.Invalid(error("%s : rejected nVersion=3 block", __func__),
                             REJECT_OBSOLETE, "bad-version");

    return true;
}

bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex * const pindexPrev)
{
    const int nHeight = pindexPrev == NULL ? 0 : pindexPrev->nHeight + 1;
    const Consensus::Params& consensusParams = Params().GetConsensus();

    // Start enforcing BIP113 (Median Time Past) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_CSV, versionbitscache) == THRESHOLD_ACTIVE) {
        nLockTimeFlags |= LOCKTIME_MEDIAN_TIME_PAST;
    }

    int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST)
                              ? pindexPrev->GetMedianTimePast()
                              : block.GetBlockTime();

    bool fDIP0001Active_context = (VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_DIP0001, versionbitscache) == THRESHOLD_ACTIVE);

    // Size limits
    unsigned int nMaxBlockSize = MaxBlockSize(fDIP0001Active_context);
    if (block.vtx.empty() || block.vtx.size() > nMaxBlockSize || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > nMaxBlockSize)
        return state.DoS(100, error("%s: size limits failed", __func__),
                         REJECT_INVALID, "bad-blk-length");

    // Check that all transactions are finalized and not over-sized
    // Also count sigops
    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTransaction& tx, block.vtx) {
        if (!IsFinalTx(tx, nHeight, nLockTimeCutoff)) {
            return state.DoS(10, error("%s: contains a non-final transaction", __func__), REJECT_INVALID, "bad-txns-nonfinal");
        }
        if (fDIP0001Active_context && ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_STANDARD_TX_SIZE) {
            return state.DoS(100, error("%s: contains an over-sized transaction", __func__), REJECT_INVALID, "bad-txns-oversized");
        }
        nSigOps += GetLegacySigOpCount(tx);
    }

    // Check sigops
    if (nSigOps > MaxBlockSigOps(fDIP0001Active_context))
        return state.DoS(100, error("%s: out-of-bounds SigOpCount", __func__),
                         REJECT_INVALID, "bad-blk-sigops");

    // Enforce block.nVersion=2 rule that the coinbase starts with serialized block height
    // if 750 of the last 1,000 blocks are version 2 or greater (51/100 if testnet):
    if (block.nVersion >= 2 && IsSuperMajority(2, pindexPrev, consensusParams.nMajorityEnforceBlockUpgrade, consensusParams))
    {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0].vin[0].scriptSig.size() < expect.size() ||
            !std::equal(expect.begin(), expect.end(), block.vtx[0].vin[0].scriptSig.begin())) {
            return state.DoS(100, error("%s: block height mismatch in coinbase", __func__), REJECT_INVALID, "bad-cb-height");
        }
    }

    return true;
}

static bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex)
{
    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator miSelf = mapBlockIndex.find(hash);
    CBlockIndex *pindex = NULL;

    // TODO : ENABLE BLOCK CACHE IN SPECIFIC CASES
    if (hash != chainparams.GetConsensus().hashGenesisBlock) {

        if (miSelf != mapBlockIndex.end()) {
            // Block header is already known.
            pindex = miSelf->second;
            if (ppindex)
                *ppindex = pindex;
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return state.Invalid(error("%s: block is marked invalid", __func__), 0, "duplicate");
            return true;
        }

        if (!CheckBlockHeader(block, state))
            return false;

        // Get prev block index
        CBlockIndex* pindexPrev = NULL;
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.end())
            return state.DoS(10, error("%s: prev block not found", __func__), 0, "bad-prevblk");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
            return state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");

        assert(pindexPrev);
        if (fCheckpointsEnabled && !CheckIndexAgainstCheckpoint(pindexPrev, state, chainparams, hash))
            return error("%s: CheckIndexAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());

        if (!ContextualCheckBlockHeader(block, state, pindexPrev))
            return false;
    }
    if (pindex == NULL)
        pindex = AddToBlockIndex(block);

    if (ppindex)
        *ppindex = pindex;

    CheckBlockIndex(chainparams.GetConsensus());

    // Notify external listeners about accepted block header
    GetMainSignals().AcceptedBlockHeader(pindex);

    return true;
}

// Exposed wrapper for AcceptBlockHeader
bool ProcessNewBlockHeaders(const std::vector<CBlockHeader>& headers, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex)
{
    {
        LOCK(cs_main);
        for (const CBlockHeader& header : headers) {
            if (!AcceptBlockHeader(header, state, chainparams, ppindex)) {
                return false;
            }
        }
    }
    NotifyHeaderTip();
    return true;
}

/** Store block on disk. If dbp is non-NULL, the file is known to already reside on disk */
static bool AcceptBlock(const CBlock& block, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex, bool fRequested, const CDiskBlockPos* dbp, bool* fNewBlock)
{
    if (fNewBlock) *fNewBlock = false;
    AssertLockHeld(cs_main);

    CBlockIndex *pindexDummy = NULL;
    CBlockIndex *&pindex = ppindex ? *ppindex : pindexDummy;

    if (!AcceptBlockHeader(block, state, chainparams, &pindex))
        return false;

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    bool fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
    bool fHasMoreWork = (chainActive.Tip() ? pindex->nChainWork > chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead = (pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP));

    // TODO: Decouple this function from the block download logic by removing fRequested
    // This requires some new chain datastructure to efficiently look up if a
    // block is in a chain leading to a candidate for best tip, despite not
    // being such a candidate itself.

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (fAlreadyHave) return true;
    if (!fRequested) {  // If we didn't ask for it:
        if (pindex->nTx != 0) return true;  // This is a previously-processed block that was pruned
        if (!fHasMoreWork) return true;     // Don't process less-work chains
        if (fTooFarAhead) return true;      // Block height is too high
    }
    if (fNewBlock) *fNewBlock = true;

    if ((!CheckBlock(block, pindex->nHeight, state)) || !ContextualCheckBlock(block, state, pindex->pprev)) {
        if (state.IsInvalid() && !state.CorruptionPossible()) {
            pindex->nStatus |= BLOCK_FAILED_VALID;
            setDirtyBlockIndex.insert(pindex);
        }
        return false;
    }

    int nHeight = pindex->nHeight;

    // Write block to history file
    try {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != NULL)
            blockPos = *dbp;
        if (!FindBlockPos(state, blockPos, nBlockSize+8, nHeight, block.GetBlockTime(), dbp != NULL))
            return error("AcceptBlock(): FindBlockPos failed");
        if (dbp == NULL)
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                AbortNode(state, "Failed to write block");
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    if (fCheckForPruning)
        FlushStateToDisk(state, FLUSH_STATE_NONE); // we just allocated more disk space for block files

    return true;
}

static bool IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned nRequired, const Consensus::Params& consensusParams)
{
    unsigned int nFound = 0;
    for (int i = 0; i < consensusParams.nMajorityWindow && nFound < nRequired && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }
    return (nFound >= nRequired);
}


bool ProcessNewBlock(const CChainParams& chainparams, const CBlock* pblock, bool fForceProcessing, const CDiskBlockPos* dbp, bool *fNewBlock)
{
    {
        LOCK(cs_main);

        // Store to disk
        CBlockIndex *pindex = NULL;
        if (fNewBlock) *fNewBlock = false;
        CValidationState state;
        bool ret = AcceptBlock(*pblock, state, chainparams, &pindex, fForceProcessing, dbp, fNewBlock);
        CheckBlockIndex(chainparams.GetConsensus());
        if (!ret) {
            GetMainSignals().BlockChecked(*pblock, state);
            return error("%s: AcceptBlock FAILED", __func__);
        }
    }

    NotifyHeaderTip();

    CValidationState state; // Only used to report errors, not invalidity - ignore it
    if (!ActivateBestChain(state, chainparams, pblock))
        return error("%s: ActivateBestChain failed", __func__);

    LogPrintf("%s : ACCEPTED\n", __func__);
    return true;
}

bool TestBlockValidity(CValidationState& state, const CChainParams& chainparams, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev && pindexPrev == chainActive.Tip());
    if (fCheckpointsEnabled && !CheckIndexAgainstCheckpoint(pindexPrev, state, chainparams, block.GetHash()))
        return error("%s: CheckIndexAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return false;
    if (!CheckBlock(block, indexDummy.nHeight, state, fCheckPOW, fCheckMerkleRoot))
        return false;
    if (!ContextualCheckBlock(block, state, pindexPrev))
        return false;
    if (!ConnectBlock(block, state, &indexDummy, viewNew, true))
        return false;
    assert(state.IsValid());

    return true;
}

/**
 * BLOCK PRUNING CODE
 */

/* Calculate the amount of disk space the block & undo files currently use */
uint64_t CalculateCurrentUsage()
{
    uint64_t retval = 0;
    BOOST_FOREACH(const CBlockFileInfo &file, vinfoBlockFile) {
        retval += file.nSize + file.nUndoSize;
    }
    return retval;
}

/* Prune a block file (modify associated database entries)*/
void PruneOneBlockFile(const int fileNumber)
{
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); ++it) {
        CBlockIndex* pindex = it->second;
        if (pindex->nFile == fileNumber) {
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pindex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second) {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator it = range.first;
                range.first++;
                if (it->second == pindex) {
                    mapBlocksUnlinked.erase(it);
                }
            }
        }
    }

    vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
}


void UnlinkPrunedFiles(std::set<int>& setFilesToPrune)
{
    for (set<int>::iterator it = setFilesToPrune.begin(); it != setFilesToPrune.end(); ++it) {
        CDiskBlockPos pos(*it, 0);
        boost::filesystem::remove(GetBlockPosFilename(pos, "blk"));
        boost::filesystem::remove(GetBlockPosFilename(pos, "rev"));
        LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, *it);
    }
}

/* Calculate the block/rev files that should be deleted to remain under target*/
void FindFilesToPrune(std::set<int>& setFilesToPrune, uint64_t nPruneAfterHeight)
{
    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == NULL || nPruneTarget == 0) {
        return;
    }
    if ((uint64_t)chainActive.Tip()->nHeight <= nPruneAfterHeight) {
        return;
    }

    unsigned int nLastBlockWeCanPrune = chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP;
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count=0;

    if (nCurrentUsage + nBuffer >= nPruneTarget) {
        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize + vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0)
                continue;

            if (nCurrentUsage + nBuffer < nPruneTarget)  // are we below our target?
                break;

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the main chain's tip but keep scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
                continue;

            PruneOneBlockFile(fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint("prune", "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
           nPruneTarget/1024/1024, nCurrentUsage/1024/1024,
           ((int64_t)nPruneTarget - (int64_t)nCurrentUsage)/1024/1024,
           nLastBlockWeCanPrune, count);
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = boost::filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return NULL;
    boost::filesystem::path path = GetBlockPosFilename(pos, prefix);
    boost::filesystem::create_directories(path.parent_path());
    FILE* file = fopen(path.string().c_str(), "rb+");
    if (!file && !fReadOnly)
        file = fopen(path.string().c_str(), "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return NULL;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return NULL;
        }
    }
    return file;
}

FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex * InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return NULL;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw runtime_error("InsertBlockIndex(): new CBlockIndex failed");
    mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool static LoadBlockIndexDB()
{
    const CChainParams& chainparams = Params();
    if (!pblocktree->LoadBlockIndexGuts())
        return false;

    boost::this_thread::interruption_point();

    // Calculate nChainWork
    vector<pair<int, CBlockIndex*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    BOOST_FOREACH(const PAIRTYPE(uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        vSortedByHeight.push_back(make_pair(pindex->nHeight, pindex));
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    BOOST_FOREACH(const PAIRTYPE(int, CBlockIndex*)& item, vSortedByHeight)
    {
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        if (pindex->nTx > 0) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                } else {
                    pindex->nChainTx = 0;
                    mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == NULL))
            setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == NULL || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;
    }

    // Load block file info
    pblocktree->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (pblocktree->ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        } else {
            break;
        }
    }

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    set<int> setBlkDataFiles;
    BOOST_FOREACH(const PAIRTYPE(uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }
    for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
    {
        CDiskBlockPos pos(*it, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) {
            return false;
        }
    }

    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if (fHavePruned)
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    fReindex |= fReindexing;

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n", __func__, fTxIndex ? "enabled" : "disabled");

    // Check whether we have an address index
    pblocktree->ReadFlag("addressindex", fAddressIndex);
    LogPrintf("%s: address index %s\n", __func__, fAddressIndex ? "enabled" : "disabled");

    // Check whether we have a timestamp index
    pblocktree->ReadFlag("timestampindex", fTimestampIndex);
    LogPrintf("%s: timestamp index %s\n", __func__, fTimestampIndex ? "enabled" : "disabled");

    // Check whether we have a spent index
    pblocktree->ReadFlag("spentindex", fSpentIndex);
    LogPrintf("%s: spent index %s\n", __func__, fSpentIndex ? "enabled" : "disabled");

    // Load pointer to end of best chain
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end())
        return true;
    chainActive.SetTip(it->second);

    PruneBlockIndexCandidates();

    g_nChainHeight = chainActive.Height();
    LogPrintf("%s: hashBestChain=%s height=%d date=%s progress=%f\n", __func__,
        chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
        Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), chainActive.Tip()));

    return true;
}

CVerifyDB::CVerifyDB()
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0);
}

CVerifyDB::~CVerifyDB()
{
    uiInterface.ShowProgress("", 100);
}

bool CVerifyDB::VerifyDB(const CChainParams& chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    LOCK(cs_main);
    if (chainActive.Tip() == NULL || chainActive.Tip()->pprev == NULL)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > chainActive.Height())
        nCheckDepth = chainActive.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex* pindexState = chainActive.Tip();
    CBlockIndex* pindexFailure = NULL;
    int nGoodTransactions = 0;
    CValidationState state;
    for (CBlockIndex* pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        boost::this_thread::interruption_point();
        uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100)))));
        if (pindex->nHeight < chainActive.Height()-nCheckDepth)
            break;
        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, pindex->nHeight, state))
            return error("VerifyDB(): *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull()) {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState && (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage) {
            bool fClean = true;
            if (!DisconnectBlock(block, state, pindex, coins, &fClean))
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            pindexState = pindex->pprev;
            if (!fClean) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else
                nGoodTransactions += block.vtx.size();
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error("VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip()) {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, pindex, coins))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        }
    }

    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n", chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}

// May NOT be used after any connections are up as much
// of the peer-processing logic assumes a consistent
// block index state
void UnloadBlockIndex()
{
    LOCK(cs_main);
    setBlockIndexCandidates.clear();
    chainActive.SetTip(NULL);
    pindexBestInvalid = NULL;
    pindexBestHeader = NULL;
    mempool.clear();
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    nBlockSequenceId = 1;
    setDirtyBlockIndex.clear();
    setDirtyFileInfo.clear();
    versionbitscache.Clear();
    for (int b = 0; b < VERSIONBITS_NUM_BITS; b++) {
        warningcache[b].clear();
    }

    BOOST_FOREACH(BlockMap::value_type& entry, mapBlockIndex) {
        delete entry.second;
    }
    mapBlockIndex.clear();
    fHavePruned = false;
}

bool LoadBlockIndex()
{
    // Load block index from databases
    if (!fReindex && !LoadBlockIndexDB())
        return false;
    return true;
}

bool InitBlockIndex(const CChainParams& chainparams)
{
    LOCK(cs_main);

    // Check whether we're already initialized
    if (chainActive.Genesis() != NULL)
        return true;

    // Use the provided setting for -txindex in the new database
    fTxIndex = GetBoolArg("-txindex", DEFAULT_TXINDEX);
    pblocktree->WriteFlag("txindex", fTxIndex);

    // Use the provided setting for -addressindex in the new database
    fAddressIndex = GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX);
    pblocktree->WriteFlag("addressindex", fAddressIndex);

    // Use the provided setting for -timestampindex in the new database
    fTimestampIndex = GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX);
    pblocktree->WriteFlag("timestampindex", fTimestampIndex);

    fSpentIndex = GetBoolArg("-spentindex", DEFAULT_SPENTINDEX);
    pblocktree->WriteFlag("spentindex", fSpentIndex);

    LogPrintf("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!fReindex) {
        try {
            CBlock &block = const_cast<CBlock&>(chainparams.GenesisBlock());
            // Start new block file
            unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!FindBlockPos(state, blockPos, nBlockSize+8, 0, block.GetBlockTime()))
                return error("%s: FindBlockPos failed", __func__);
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                return error("%s: writing genesis block to disk failed", __func__);
            CBlockIndex *pindex = AddToBlockIndex(block);
            if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
                return error("%s: genesis block not accepted", __func__);
            if (!ActivateBestChain(state, chainparams, &block))
                return error("%s: genesis block cannot be activated", __func__);
            // Force a chainstate write so that when we VerifyDB in a moment, it doesn't check stale data
            return FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
        } catch (const std::runtime_error& e) {
            return error("%s: failed to initialize block database: %s", __func__, e.what());
        }
    }

    return true;
}

bool LoadExternalBlockFile(const CChainParams& chainparams, FILE* fileIn, CDiskBlockPos *dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try {
        unsigned int nMaxBlockSize = MaxBlockSize(true);
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2*nMaxBlockSize, nMaxBlockSize+8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(chainparams.MessageStart()[0]);
                nRewind = blkdat.GetPos()+1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainparams.MessageStart(), MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > nMaxBlockSize)
                    continue;
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                CBlock block;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (hash != chainparams.GetConsensus().hashGenesisBlock && mapBlockIndex.find(block.hashPrevBlock) == mapBlockIndex.end()) {
                    LogPrint("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                            block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if (mapBlockIndex.count(hash) == 0 || (mapBlockIndex[hash]->nStatus & BLOCK_HAVE_DATA) == 0) {
                    LOCK(cs_main);
                    CValidationState state;
                    if (AcceptBlock(block, state, chainparams, NULL, true, dbp, NULL))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (hash != chainparams.GetConsensus().hashGenesisBlock && mapBlockIndex[hash]->nHeight % 1000 == 0) {
                    LogPrint("reindex", "Block Import: already had block %s at height %d\n", hash.ToString(), mapBlockIndex[hash]->nHeight);
                }

                // Activate the genesis block so normal node progress can continue
                if (hash == chainparams.GetConsensus().hashGenesisBlock) {
                    CValidationState state;
                    if (!ActivateBestChain(state, chainparams)) {
                        break;
                    }
                }

                NotifyHeaderTip();

                // Recursively process earlier encountered successors of this block
                deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        if (ReadBlockFromDisk(block, it->second, chainparams.GetConsensus()))
                        {
                            LogPrint("reindex", "%s: Processing out of order child %s of %s\n", __func__, block.GetHash().ToString(),
                                    head.ToString());
                            LOCK(cs_main);
                            CValidationState dummy;
                            if (AcceptBlock(block, dummy, chainparams, NULL, true, &it->second, NULL))
                            {
                                nLoaded++;
                                queue.push_back(block.GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                        NotifyHeaderTip();
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    } catch (const std::runtime_error& e) {
        AbortNode(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

void static CheckBlockIndex(const Consensus::Params& consensusParams)
{
    if (!fCheckBlockIndex) {
        return;
    }

    LOCK(cs_main);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (chainActive.Height() < 0) {
        assert(mapBlockIndex.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex*,CBlockIndex*> forward;
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        forward.insert(std::make_pair(it->second->pprev, it->second));
    }

    assert(forward.size() == mapBlockIndex.size());

    std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeGenesis = forward.equal_range(NULL);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent NULL.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = NULL; // Oldest ancestor of pindex which is invalid.
    CBlockIndex* pindexFirstMissing = NULL; // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex* pindexFirstNeverProcessed = NULL; // Oldest ancestor of pindex for which nTx == 0.
    CBlockIndex* pindexFirstNotTreeValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex* pindexFirstNotTransactionsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    CBlockIndex* pindexFirstNotChainValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex* pindexFirstNotScriptsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    while (pindex != NULL) {
        nNodes++;
        if (pindexFirstInvalid == NULL && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == NULL && !(pindex->nStatus & BLOCK_HAVE_DATA)) pindexFirstMissing = pindex;
        if (pindexFirstNeverProcessed == NULL && pindex->nTx == 0) pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTreeValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTransactionsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS) pindexFirstNotTransactionsValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotChainValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) pindexFirstNotChainValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotScriptsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == NULL) {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == consensusParams.hashGenesisBlock); // Genesis block's hash must match.
            assert(pindex == chainActive.Genesis()); // The current active chain's genesis block must be this block.
        }
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId == 0);  // nSequenceId can't be set for blocks that aren't linked
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred).
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!fHavePruned) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx > 0
            assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else {
            // If we have pruned, then we can only say that HAVE_DATA implies nTx > 0
            if (pindex->nStatus & BLOCK_HAVE_DATA) assert(pindex->nTx > 0);
        }
        if (pindex->nStatus & BLOCK_HAVE_UNDO) assert(pindex->nStatus & BLOCK_HAVE_DATA);
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0)); // This is pruning-independent.
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstNeverProcessed != NULL) == (pindex->nChainTx == 0)); // nChainTx != 0 is used to signal that all parent blocks have been processed (but may have been pruned).
        assert((pindexFirstNotTransactionsValid != NULL) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        assert(pindex->pprev == NULL || pindex->nChainWork >= pindex->pprev->nChainWork); // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight))); // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == NULL); // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == NULL); // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == NULL); // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == NULL); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == NULL) {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && pindexFirstNeverProcessed == NULL) {
            if (pindexFirstInvalid == NULL) {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned.
                if (pindexFirstMissing == NULL || pindex == chainActive.Tip()) {
                    assert(setBlockIndexCandidates.count(pindex));
                }
                // If some parent is missing, then it could be that this block was in
                // setBlockIndexCandidates but had to be removed because of the missing data.
                // In this case it must be in mapBlocksUnlinked -- see test below.
            }
        } else { // If this block sorts worse than the current tip or some ancestor's block has never been seen, it cannot be in setBlockIndexCandidates.
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != NULL && pindexFirstInvalid == NULL) {
            // If this block has block data available, some parent was never received, and has no invalid parents, it must be in mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        if (!(pindex->nStatus & BLOCK_HAVE_DATA)) assert(!foundInUnlinked); // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if (pindexFirstMissing == NULL) assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked.
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == NULL && pindexFirstMissing != NULL) {
            // We HAVE_DATA for this block, have received data for all parents at some point, but we're currently missing data for some parent.
            assert(fHavePruned); // We must have pruned.
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between chainActive and the
            //    tip.
            // So if this block is itself better than chainActive.Tip() and it wasn't in
            // setBlockIndexCandidates, then it must be in mapBlocksUnlinked.
            if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && setBlockIndexCandidates.count(pindex) == 0) {
                if (pindexFirstInvalid == NULL) {
                    assert(foundInUnlinked);
                }
            }
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = NULL;
            if (pindex == pindexFirstMissing) pindexFirstMissing = NULL;
            if (pindex == pindexFirstNeverProcessed) pindexFirstNeverProcessed = NULL;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = NULL;
            if (pindex == pindexFirstNotTransactionsValid) pindexFirstNotTransactionsValid = NULL;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = NULL;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = NULL;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

std::string GetWarnings(const std::string& strFor)
{
    int nPriority = 0;
    string strStatusBar;
    string strRPC;
    string strGUI;

    if (!CLIENT_VERSION_IS_RELEASE) {
        strStatusBar = "This is a pre-release test build - use at your own risk - do not use for mining or merchant applications";
        strGUI = _("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");
    }

    if (GetBoolArg("-testsafemode", DEFAULT_TESTSAFEMODE))
        strStatusBar = strRPC = strGUI = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        nPriority = 1000;
        strStatusBar = strGUI = strMiscWarning;
    }

    if (fLargeWorkForkFound)
    {
        nPriority = 2000;
        strStatusBar = strRPC = "Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.";
        strGUI = _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    }
    else if (fLargeWorkInvalidChainFound)
    {
        nPriority = 2000;
        strStatusBar = strRPC = "Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.";
        strGUI = _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
    }

    // Alerts
    {
        LOCK(cs_mapAlerts);
        BOOST_FOREACH(PAIRTYPE(const uint256, CAlert)& item, mapAlerts)
        {
            const CAlert& alert = item.second;
            if (alert.AppliesToMe() && alert.nPriority > nPriority)
            {
                nPriority = alert.nPriority;
                strStatusBar = strGUI = alert.strStatusBar;
            }
        }
    }

    if (strFor == "gui")
        return strGUI;
    else if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings(): invalid parameter");
    return "error";
}
 std::string CBlockFileInfo::ToString() const {
     return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
 }

ThresholdState VersionBitsTipState(const Consensus::Params& params, Consensus::DeploymentPos pos)
{
    AssertLockHeld(cs_main);
    return VersionBitsState(chainActive.Tip(), params, pos, versionbitscache);
}

class CMainCleanup
{
public:
    CMainCleanup() {}
    ~CMainCleanup() {
        // block headers
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();
    }
} instance_of_cmaincleanup;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool GetAppInfoByAppId(const uint256& appId, CAppId_AppInfo_IndexValue& appInfo, const bool fWithMempool)
{
    if(pblocktree->Read_AppId_AppInfo_Index(appId, appInfo))
        return true;
    return fWithMempool && mempool.getAppInfoByAppId(appId, appInfo);
}

bool GetAppIdByAppName(const string& strAppName, uint256& appId, const bool fWithMempool)
{
    CName_Id_IndexValue value;
    if(pblocktree->Read_AppName_AppId_Index(strAppName, value))
    {
        appId = value.id;
        return true;
    }

    if(fWithMempool && mempool.getAppIdByAppName(strAppName, value))
    {
        appId = value.id;
        return true;
    }

    return false;
}

bool GetTxInfoByAppId(const uint256& appId, vector<COutPoint>& vOut, const bool fWithMempool)
{
    if(!fWithMempool)
        return pblocktree->Read_AppTx_Index(appId, vOut);

    vector<COutPoint> vMempoolOut;
    mempool.get_AppTx_Index(appId, vMempoolOut);
    pblocktree->Read_AppTx_Index(appId, vOut);
    BOOST_FOREACH(const COutPoint& out, vMempoolOut)
    {
        if(find(vOut.begin(), vOut.end(), out) == vOut.end())
            vOut.push_back(out);
    }

    return vOut.size();
}

bool GetTxInfoByAppIdAddress(const uint256& appId, const string& strAddress, vector<COutPoint>& vOut, const bool fWithMempool)
{
    if(!fWithMempool)
        return pblocktree->Read_AppTx_Index(appId, strAddress, vOut);

    vector<COutPoint> vMempoolOut;
    mempool.get_AppTx_Index(appId, strAddress, vMempoolOut);
    pblocktree->Read_AppTx_Index(appId, strAddress, vOut);
    BOOST_FOREACH(const COutPoint& out, vMempoolOut)
    {
        if(find(vOut.begin(), vOut.end(), out) == vOut.end())
            vOut.push_back(out);
    }

    return vOut.size();
}

bool GetAppListInfo(std::vector<uint256>& vAppId, const bool fWithMempool)
{
    if(!fWithMempool)
        return pblocktree->Read_AppList_Index(vAppId);

    std::vector<uint256> vMempoolAppId;
    mempool.getAppList(vMempoolAppId);
    pblocktree->Read_AppList_Index(vAppId);
    BOOST_FOREACH(const uint256& appId, vMempoolAppId)
        if (find(vAppId.begin(), vAppId.end(), appId) == vAppId.end())
           vAppId.push_back(appId);

    return vAppId.size();
}

bool GetAppIDListByAddress(const std::string& strAddress, std::vector<uint256>& vAppId, const bool fWithMempool)
{
    if (!fWithMempool)
        return pblocktree->Read_AppList_Index(strAddress, vAppId);

    vector<uint256> vMempoolAppId;
    mempool.getAppList(strAddress, vMempoolAppId);
    pblocktree->Read_AppList_Index(strAddress, vAppId);

    BOOST_FOREACH(const uint256& appId, vMempoolAppId)
        if(find(vAppId.begin(), vAppId.end(), appId) == vAppId.end())
            vAppId.push_back(appId);

    return vAppId.size();
}

bool GetExtendDataByTxId(const uint256& txId, vector<pair<uint256, string> > &vExtendData)
{
    CTransaction tx;
    uint256 hashBlock;
    if(!GetTransaction(txId, tx, Params().GetConsensus(), hashBlock, true))
        return false;

    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CAppHeader header;
        vector<unsigned char> vData;
        if(!ParseReserve(txout.vReserve, header, vData))
            continue;

        if(header.nAppCmd != CREATE_EXTEND_TX_CMD)
            continue;

        CExtendData extendData;
        if(ParseExtendData(vData, extendData))
            vExtendData.push_back(make_pair(header.appId, extendData.strExtendData));
    }

    return true;
}

bool GetAuthByAppIdAddress(const uint256& appId, const string& strAddress, map<uint32_t, int> &mapAuth)
{
    return pblocktree->Read_Auth_Index(appId, strAddress, mapAuth);
}

bool GetAuthByAppIdAddressFromMempool(const uint256& appId, const string& strAddress, vector<uint32_t>& vAuth)
{
    return mempool.get_Auth_Index(appId, strAddress, vAuth);
}

bool GetAssetInfoByAssetId(const uint256& assetId, CAssetId_AssetInfo_IndexValue& assetInfo, const bool fWithMempool)
{
    if(pblocktree->Read_AssetId_AssetInfo_Index(assetId, assetInfo))
        return true;
    return fWithMempool && mempool.getAssetInfoByAssetId(assetId, assetInfo);
}

bool GetAssetIdByShortName(const string& strShortName, uint256& assetId, const bool fWithMempool)
{
    CName_Id_IndexValue value;
    if(pblocktree->Read_ShortName_AssetId_Index(strShortName, value))
    {
        assetId = value.id;
        return true;
    }

    if(fWithMempool && mempool.getAssetIdByShortName(strShortName, value))
    {
        assetId = value.id;
        return true;
    }

    return false;
}

bool GetAssetIdByAssetName(const string& strAssetName, uint256& assetId, const bool fWithMempool)
{
    CName_Id_IndexValue value;
    if(pblocktree->Read_AssetName_AssetId_Index(strAssetName, value))
    {
        assetId = value.id;
        return true;
    }

    if(fWithMempool && mempool.getAssetIdByAssetName(strAssetName, value))
    {
        assetId = value.id;
        return true;
    }

    return false;
}

bool GetTxInfoByAssetIdTxClass(const uint256& assetId, const uint8_t& nTxClass, vector<COutPoint>& vOut, const bool fWithMempool)
{
    if(!fWithMempool)
        return pblocktree->Read_AssetTx_Index(assetId, nTxClass, vOut);

    vector<COutPoint> vMempoolOut;
    mempool.get_AssetTx_Index(assetId, nTxClass, vMempoolOut);
    pblocktree->Read_AssetTx_Index(assetId, nTxClass, vOut);
    BOOST_FOREACH(const COutPoint& out, vMempoolOut)
    {
        if(find(vOut.begin(), vOut.end(), out) == vOut.end())
            vOut.push_back(out);
    }

    return vOut.size();
}

bool GetTxInfoByAssetIdAddressTxClass(const uint256& assetId, const string& strAddress, const uint8_t& nTxClass, vector<COutPoint>& vOut, const bool fWithMempool)
{
    if(!fWithMempool)
        return pblocktree->Read_AssetTx_Index(assetId, strAddress, nTxClass, vOut);

    vector<COutPoint> vMempoolOut;
    mempool.get_AssetTx_Index(assetId, strAddress, nTxClass, vMempoolOut);
    pblocktree->Read_AssetTx_Index(assetId, strAddress, nTxClass, vOut);
    BOOST_FOREACH(const COutPoint& out, vMempoolOut)
    {
        if(find(vOut.begin(), vOut.end(), out) == vOut.end())
            vOut.push_back(out);
    }

    return vOut.size();
}

bool GetAssetIdCandyInfo(const uint256& assetId, map<COutPoint, CCandyInfo>& mapCandyInfo)
{
    return pblocktree->Read_PutCandy_Index(assetId, mapCandyInfo);
}

bool GetAssetIdCandyInfo(const uint256& assetId, const COutPoint& out, CCandyInfo& candyInfo)
{
    return pblocktree->Read_PutCandy_Index(assetId, out, candyInfo);
}

bool GetAssetIdCandyInfoList(std::map<CPutCandy_IndexKey, CPutCandy_IndexValue>& mapCandy)
{
    return pblocktree->Read_PutCandy_Index(mapCandy);
}

bool GetAssetIdByAddress(const std::string & strAddress, std::vector<uint256> &assetIdlist, const bool fWithMempool)
{
    if(!fWithMempool)
        return pblocktree->Read_AssetList_Index(strAddress, assetIdlist);

    vector<uint256> vMemassetIdlist;
    mempool.getAssetList(strAddress, vMemassetIdlist);
    pblocktree->Read_AssetList_Index(strAddress, assetIdlist);

    BOOST_FOREACH(const uint256& assetId, vMemassetIdlist)
    {
        if(find(assetIdlist.begin(), assetIdlist.end(), assetId) == assetIdlist.end())
            assetIdlist.push_back(assetId);
    }

    return assetIdlist.size();
}

bool GetGetCandyAmount(const uint256& assetId, const COutPoint& out, const std::string& strAddress, CAmount& amount, const bool fWithMempool)
{
    if(fWithMempool)
    {
        if(mempool.get_GetCandy_Index(assetId, out, strAddress, amount))
            return true;
    }

    return pblocktree->Read_GetCandy_Index(assetId, out, strAddress, amount);
}

bool GetAssetListInfo(std::vector<uint256> &vAssetId, const bool fWithMempool)
{
    if(!fWithMempool)
        return pblocktree->Read_AssetList_Index(vAssetId);

    std::vector<uint256> vMempoolAssetId;
    mempool.getAssetList(vMempoolAssetId);
    pblocktree->Read_AssetList_Index(vAssetId);
    BOOST_FOREACH(const uint256& assetId, vMempoolAssetId)
    {
        if(find(vAssetId.begin(), vAssetId.end(), assetId) == vAssetId.end())
        {
           vAssetId.push_back(assetId);
        }
    }

    return vAssetId.size();
}

CAmount GetAddedAmountByAssetId(const uint256& assetId, const bool fWithMempool)
{
    if (assetId.IsNull())
        return 0;

    CAmount value = 0;
    vector<COutPoint> tempvOut;
    GetTxInfoByAssetIdTxClass(assetId, ADD_ISSUE_TXOUT, tempvOut, fWithMempool);
    if (tempvOut.empty())
        return 0;

    std::vector<uint256> vTx;
    BOOST_FOREACH(const COutPoint& out, tempvOut)
    {
        if (find(vTx.begin(), vTx.end(), out.hash) == vTx.end())
            vTx.push_back(out.hash);
    }

    std::vector<uint256>::iterator it = vTx.begin();
    for (; it != vTx.end(); it++)
    {
        CTransaction txTmp;
        uint256 hashBlock;
        if (GetTransaction(*it, txTmp, Params().GetConsensus(), hashBlock, true))
        {
            BOOST_FOREACH(const CTxOut& txout, txTmp.vout)
            {
                if (!txout.IsAsset())
                    continue;

                CAppHeader header;
                vector<unsigned char> vData;
                if (ParseReserve(txout.vReserve, header, vData))
                {
                    if (header.nAppCmd == ADD_ASSET_CMD)
                    {
                        CCommonData commonData;
                        if (ParseCommonData(vData, commonData))
                        {
                            if (assetId == commonData.assetId)
                                value += commonData.nAmount;
                        }
                    }
                }
           }
        }
    }

    return value;
}

bool GetRangeChangeHeight(const int nCandyHeight, vector<int>& vChangeHeight)
{
    vChangeHeight.clear();

    vector<int> vHeight;
    if(pblocktree->Read_CandyHeight_Index(vHeight))
        sort(vHeight.begin(), vHeight.end());

    if(vHeight.empty() || find(vHeight.begin(), vHeight.end(), nCandyHeight) == vHeight.end())
        return false;

    for(unsigned int i = 0; i < vHeight.size(); i++)
    {
        if(vHeight[i] < nCandyHeight)
            continue;
        vChangeHeight.push_back(vHeight[i]);
    }

    return true;
}

static int BinarySearchFromFile(const string& strFile, const string& strAddress, CAmount& nAmount, long* pPos = NULL);
bool GetAddressAmountByHeight(const int& nHeight, const std::string& strAddress, CAmount& nAmount)
{
    std::lock_guard<std::mutex> lock(g_mutexChangeFile);

    uint64_t nDetailFileSize = boost::filesystem::file_size(GetDataDir() / "height/detail.dat");
    if(nDetailFileSize / sizeof(CBlockDetail) + g_nCriticalHeight - 1 - nHeight > 3 * BLOCKS_PER_MONTH)
        return error("%s: cannot get address amount out of 3 months", __func__);

    vector<int> vChangeHeight;
    if(!GetRangeChangeHeight(nHeight, vChangeHeight))
        return error("%s: get change files failed at %d", __func__, nHeight);

    // 1. search address from all.dat
    if(BinarySearchFromFile("all.dat", strAddress, nAmount) < 0)
        return error("%s: search %s from all.dat failed at %d", __func__, strAddress, nHeight);

    // 2. search address from change file
    string strFile = "";
    BOOST_FOREACH(const int& nChangeHeight, vChangeHeight)
    {
        strFile = itostr(nChangeHeight) + ".change";
        CAmount nChangeAmount = 0;
        if(BinarySearchFromFile(strFile, strAddress, nChangeAmount) < 0)
            return error("%s: search %s from %s failed at %d", __func__, strAddress, strFile, nHeight);
        nAmount -= nChangeAmount;
    }

    if(nAmount < 0)
        return false;

    return true;
}

bool GetTotalAmountByHeight(const int& nHeight, CAmount& nTotalAmount)
{
    return pblocktree->Read_CandyHeight_TotalAmount_Index(nHeight, nTotalAmount);
}

bool GetCOutPointAddress(const uint256& assetId, std::map<COutPoint, std::vector<std::string>> &moutpointaddress)
{
    if (assetId.IsNull())
        return false;

    return pblocktree->Read_GetCandy_Index(assetId, moutpointaddress);
}

bool GetCOutPointList(const uint256& assetId, const std::string& strAddress, std::vector<COutPoint> &vcoutpoint)
{
    if (assetId.IsNull() || strAddress.empty())
        return false;

    return pblocktree->Read_GetCandy_Index(assetId, strAddress, vcoutpoint);
}

bool GetIssueAssetInfo(std::map<uint256, CAssetData>& mapissueassetinfo)
{
    if (!pwalletMain)
        return false;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin();
    for (; it != pwalletMain->mapWallet.end(); it++)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (!CheckFinalTx(*pcoin))
            continue;
        if(!pcoin->IsInMainChain())
            continue;

        for (unsigned int i = 0; i < pcoin->vout.size(); i++)
        {
            if (!pcoin->vout[i].IsAsset())
                continue;

            CAppHeader header;
            std::vector<unsigned char> vData;
            if(!ParseReserve(pcoin->vout[i].vReserve, header, vData))
                continue;

            if(header.nAppCmd == ISSUE_ASSET_CMD)
            {
                CAssetData assetData;
                if(!ParseIssueData(vData, assetData))
                    continue;

                uint256 assetid = assetData.GetHash();
                if (assetid.IsNull())
                    continue;

                std::map<uint256, CAssetData>::iterator tempit = mapissueassetinfo.find(assetid);
                if (tempit != mapissueassetinfo.end())
                    continue;

                mapissueassetinfo[assetid] = assetData;
            }
        }
    }

    return true;
}

static bool GetAllCandyInfo()
{
    unsigned int icounter = 0;

    bool fRet = false;
    std::map<CPutCandy_IndexKey, CPutCandy_IndexValue> mapCandy;
    if (!GetAssetIdCandyInfoList(mapCandy))
        return fRet;

    if (mapCandy.empty())
        return fRet;

    vector<pair<CPutCandy_IndexKey, CPutCandy_IndexValue>> vallassetidcandyinfolist(mapCandy.begin(), mapCandy.end());
    sort(vallassetidcandyinfolist.begin(), vallassetidcandyinfolist.end(), CompareCandyInfo());

    map<CKeyID, int64_t> mapKeyBirth;
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->GetKeyBirthTimes(mapKeyBirth);
    }

    int keyBirthCount = 0;
    std::vector<std::string> vaddress;
    for (map<CKeyID, int64_t>::const_iterator tempit = mapKeyBirth.begin(); tempit != mapKeyBirth.end(); tempit++)
    {
        boost::this_thread::interruption_point();
        if(++keyBirthCount==1000)
        {
            keyBirthCount = 0;
            MilliSleep(10);
        }
        std::string saddress = CBitcoinAddress(tempit->first).ToString();
        vaddress.push_back(saddress);
    }

    int nCurrentHeight = g_nChainHeight;

    int candyListSize = vallassetidcandyinfolist.size();
    for (int candyCount = 0; candyCount<candyListSize;candyCount++)
    {
        boost::this_thread::interruption_point();
        if(candyCount%500==0&&candyCount!=0)
            MilliSleep(10);
        const CPutCandy_IndexKey& candyInfoIndex = vallassetidcandyinfolist[candyCount].first;
        CPutCandy_IndexValue& candyInfoValue = vallassetidcandyinfolist[candyCount].second;
        CAssetId_AssetInfo_IndexValue assetInfo;
        const uint256& assetId = candyInfoIndex.assetId;
        if (assetId.IsNull() || !GetAssetInfoByAssetId(assetId, assetInfo, false))
            continue;

        const COutPoint& out = candyInfoIndex.out;
        const CCandyInfo& candyInfo = candyInfoIndex.candyInfo;

        if (candyInfo.nAmount <= 0)
            continue;

        int nTxHeight = candyInfoValue.nHeight;
        if (nTxHeight > g_nChainHeight)
            continue;

        BlockMap::iterator mi = mapBlockIndex.find(candyInfoValue.blockHash);
        if (mi == mapBlockIndex.end())
            continue;

        int64_t nTimeBegin = mapBlockIndex[candyInfoValue.blockHash]->GetBlockTime();

        if (candyInfo.nExpired * BLOCKS_PER_MONTH + nTxHeight < nCurrentHeight)
            continue;

        CAmount nTotalSafe = 0;
        if(!GetTotalAmountByHeight(nTxHeight, nTotalSafe))
            continue;

        if (nTotalSafe <= 0)
            continue;

        bool relust = false;
        int addressSize = vaddress.size();
        for (int addrCount = 0; addrCount<addressSize;addrCount++)
        {
            boost::this_thread::interruption_point();
            if(addrCount%50==0&&addrCount!=0)
                MilliSleep(10);
            CAmount nSafe = 0;
            if(!GetAddressAmountByHeight(nTxHeight, vaddress[addrCount], nSafe))
                continue;
            if (nSafe < 1 * COIN || nSafe > nTotalSafe)
                continue;

            CAmount nTempAmount = 0;
            CAmount nCandyAmount = (CAmount)(1.0 * nSafe / nTotalSafe * candyInfo.nAmount);
            if (nCandyAmount >= AmountFromValue("0.0001", assetInfo.assetData.nDecimals, true) && !GetGetCandyAmount(assetId, out, vaddress[addrCount], nTempAmount))
            {
                relust = true;
                break;
            }
        }

        if (relust)
        {
            fRet = true;
            CCandy_BlockTime_Info tempcandybolcktimeinfo(assetId,assetInfo.assetData,candyInfo,out,nTimeBegin,nTxHeight);

            icounter++;
            if (icounter <= nCandyPageCount && fHaveGUI)
                uiInterface.CandyVecPut();

            {
                std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
                gAllCandyInfoVec.push_back(tempcandybolcktimeinfo);
            }
            if(icounter%100==0)
                MilliSleep(10);
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_mutexTmpAllCandyInfo);
        bool sendToFirstPage = false;
        unsigned int vecSize = gAllCandyInfoVec.size();
        if(vecSize>0&&vecSize<nCandyPageCount)
            sendToFirstPage = true;
        int64_t lastBlockTime = 0;
        int lastBlockIndex = vecSize-1;
        if(vecSize>0)
        {
            lastBlockTime = gAllCandyInfoVec[vecSize-1].blocktime;
            bool foundIndex = false;
            for(int i = lastBlockIndex;i>=0;i--)
            {
                if(lastBlockTime!=gAllCandyInfoVec[i].blocktime)
                {
                    foundIndex = true;
                    lastBlockIndex = i;
                    break;
                }
            }
            if(!foundIndex)
                lastBlockIndex = 0;
        }
        if(gTmpAllCandyInfoVec.size()>0 && fRet)
        {
            for(unsigned int i=0;i<gTmpAllCandyInfoVec.size();i++)
            {
                CCandy_BlockTime_Info& tmpInfo = gTmpAllCandyInfoVec[i];
                if(tmpInfo.blocktime<lastBlockTime)
                    continue;
                bool exist = false;
                if(tmpInfo.blocktime==lastBlockTime)
                {
                    //compare exists
                    for(unsigned int i=lastBlockIndex;i<gAllCandyInfoVec.size();i++)
                    {
                        CCandy_BlockTime_Info& info = gAllCandyInfoVec[i];
                        if(info.assetId==tmpInfo.assetId&&info.outpoint==tmpInfo.outpoint&&info.candyinfo==tmpInfo.candyinfo
                                &&info.assetData.strAssetName==tmpInfo.assetData.strAssetName)
                        {
                            exist = true;
                            break;
                        }
                    }
                }
                if(!exist)
                    gAllCandyInfoVec.push_back(gTmpAllCandyInfoVec[i]);
            }
        }

        if(sendToFirstPage && fHaveGUI)
            uiInterface.CandyVecPut();
    }

    return fRet;
}

void ThreadGetAllCandyInfo()
{
    SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
    RenameThread("safe-get-allcandy");

    static bool fOneThread;
    if (fOneThread)
        return;
    fOneThread = true;

    while(true)
    {
        boost::this_thread::interruption_point();
        if(!fGetCandyInfoStart)
        {
            MilliSleep(100);
            continue;
        }
        if(GetAllCandyInfo())
        {
            fUpdateAllCandyInfoFinished = true;
            LogPrintf("Message:GetAllCandyInfo finished\n");
            break;
        }
        MilliSleep(1000);
    }
}

bool LoadCandyHeightToList()
{
    listCandyHeight.clear();

    std::vector<int> vHeight;
    if(pblocktree->Read_CandyHeight_TotalAmount_Index(vHeight))
        std::sort(vHeight.begin(), vHeight.end());

    std::vector<int> vCandyHeight;
    if (pblocktree->Read_CandyHeight_Index(vCandyHeight))
    {
        std::sort(vCandyHeight.begin(), vCandyHeight.end());
        for (std::vector<int>::iterator it = vCandyHeight.begin(); it != vCandyHeight.end(); it++)
        {
            boost::this_thread::interruption_point();
            if (vHeight.size() != 0 && *it <= vHeight.back())
                continue;
            listCandyHeight.push_back(*it);
        }
    }

    return true;
}

static bool PutCandyHeightToList(const int& nCandyHeight)
{
    std::lock_guard<std::mutex> lock(g_mutexCandyHeight);

    list<int>::iterator it = find(listCandyHeight.begin(), listCandyHeight.end(), nCandyHeight);
    if(it != listCandyHeight.end())
        return true;

    listCandyHeight.push_back(nCandyHeight);
    listCandyHeight.sort();
    return pblocktree->Write_CandyHeight_Index(nCandyHeight);
}

static bool GetCandyHeightFromList(int& nCandyHeight)
{
    std::lock_guard<std::mutex> lock(g_mutexCandyHeight);

    if (listCandyHeight.empty())
        return false;

    std::vector<int> vHeight;
    if(pblocktree->Read_CandyHeight_TotalAmount_Index(vHeight))
    {
        std::sort(vHeight.begin(), vHeight.end());
        for(list<int>::iterator it = listCandyHeight.begin(); it != listCandyHeight.end();)
        {
            if (find(vHeight.begin(), vHeight.end(), nCandyHeight) != vHeight.end())
                it = listCandyHeight.erase(it);
            else
                it++;
        }
    }

    if(listCandyHeight.empty())
        return false;

    nCandyHeight = listCandyHeight.front();
    listCandyHeight.pop_front();
    return true;
}


static void CloseFiles(vector<FILE**> vFiles)
{
    for(vector<FILE**>::iterator it = vFiles.begin(); it != vFiles.end(); it++)
    {
        if(**it)
        {
            fclose(**it);
            **it = NULL;
        }
    }
}

static int BinarySearchFromFile(FILE* pFile, const string& strAddress, CAmount& nAmount, long* pPos = NULL);
static bool MergeFileAndMap(const string& strSrcFile, const map<string, CAmount>& mapAddressAmount, const string& strDestFile)
{
    FILE *pSrcFile, *pDestFile;
    pSrcFile = pDestFile = NULL;
    vector<FILE**> vFiles;
    vFiles.push_back(&pSrcFile);
    vFiles.push_back(&pDestFile);

    if(!(pSrcFile = fopen(strSrcFile.data(), "rb")))
        return error("%s: open %s failed", __func__, strSrcFile);

    if(!(pDestFile = fopen(strDestFile.data(), "wb")))
    {
        CloseFiles(vFiles);
        return error("%s: open %s failed", __func__, strDestFile);
    }

    CAmount nAmount = 0;
    long nPos = 0;
    long nNextPos = 0;
    int nRet = 0;
    CAddressAmount arr[BATCH_COUNT];
    map<string, CAmount>::const_iterator it = mapAddressAmount.begin();
    for(; it != mapAddressAmount.end(); it++)
    {
        nRet = BinarySearchFromFile(pSrcFile, it->first, nAmount, &nPos);
        if(nRet == -1)
        {
            CloseFiles(vFiles);
            return error("%s: search %s from %s failed", __func__, it->first, strSrcFile);
        }

        if(fseek(pSrcFile, nNextPos * sizeof(CAddressAmount), SEEK_SET))
        {
            CloseFiles(vFiles);
            return error("%s: fseek %s failed", __func__, strSrcFile);
        }

        long nCount = nPos - nNextPos;
        if(nCount < 0)
        {
            CloseFiles(vFiles);
            return error("%s: unorder data from %s and map", __func__, strSrcFile);
        }

        for(long i = 0; i < nCount / BATCH_COUNT; i++)
        {
            if(fread(arr, sizeof(CAddressAmount), BATCH_COUNT, pSrcFile) != BATCH_COUNT)
            {
                CloseFiles(vFiles);
                return error("%s: 1-read %s failed", __func__, strSrcFile);
            }
            if(fwrite(arr, sizeof(CAddressAmount), BATCH_COUNT, pDestFile) != BATCH_COUNT)
            {
                CloseFiles(vFiles);
                return error("%s: 1-write %s failed", __func__, strDestFile);
            }
        }
        size_t nSize = nCount % BATCH_COUNT;
        if(nSize)
        {
            if(fread(arr, sizeof(CAddressAmount), nSize, pSrcFile) != nSize)
            {
                CloseFiles(vFiles);
                return error("%s: 2-read %s failed", __func__, strSrcFile);
            }
            if(fwrite(arr, sizeof(CAddressAmount), nSize, pDestFile) != nSize)
            {
                CloseFiles(vFiles);
                return error("%s: 2-write %s failed", __func__, strDestFile);
            }
        }

        CAddressAmount temp;
        if(nRet == 0)
        {
            nNextPos = nPos + 1;
            if(fread(&temp, sizeof(CAddressAmount), 1, pSrcFile) != 1)
            {
                CloseFiles(vFiles);
                return error("%s: 3-read: %s failed", __func__, strSrcFile);
            }
            temp.nAmount += it->second;
            if(temp.nAmount == 0)
                continue;
        }
        else if(nRet == 1)
        {
            temp = CAddressAmount(it->first, it->second);
            nNextPos = nPos;
        }
        else
            break;

        if(fwrite(&temp, sizeof(CAddressAmount), 1, pDestFile) != 1)
        {
            CloseFiles(vFiles);
            return error("%s: 3-write %s failed", __func__, strDestFile);
        }
    }

    if(nRet == 2)
    {
        for(; it != mapAddressAmount.end(); it++)
        {
            CAddressAmount temp(it->first, it->second);
            if(fwrite(&temp, sizeof(CAddressAmount), 1, pDestFile) != 1)
            {
                CloseFiles(vFiles);
                return error("%s: 5-write %s failed", __func__, strDestFile);
            }
        }
    }
    else
    {
        size_t nSize = 0;
        while(!feof(pSrcFile))
        {
            if((nSize = fread(arr, sizeof(CAddressAmount), BATCH_COUNT, pSrcFile)) == 0)
            {
                if(feof(pSrcFile))
                    break;
                CloseFiles(vFiles);
                return error("%s: 6-read from %s failed", __func__, strSrcFile);
            }
            if(nSize != fwrite(arr, sizeof(CAddressAmount), nSize, pDestFile))
            {
                CloseFiles(vFiles);
                return error("%s: 6-write to %s failed", __func__, strDestFile);
            }
        }
    }

    CloseFiles(vFiles);
    return true;
}

/*
 * error: < 0
 * found: = 0
 * none: > 0 (2: strAdress is more than all of file, 1: strAddress is a median)
 */
static int BinarySearchFromFile(const string& strFile, const string& strAddress, CAmount& nAmount, long* pPos)
{
    const string strFullName = GetDataDir().string() + "/height/" + strFile;
    FILE* pFile = fopen(strFullName.data(), "rb");
    if(!pFile)
        return false;

    int nRet = BinarySearchFromFile(pFile, strAddress, nAmount, pPos);
    fclose(pFile);
    return nRet;
}

static int BinarySearchFromFile(FILE* pFile, const string& strAddress, CAmount& nAmount, long* pPos)
{
    if(!pFile) // error
        return -1;

    if(fseek(pFile, 0L, SEEK_END))
    {
        LogPrintf("%s: fseek failed\n", __func__);
        return -1;
    }

    long nFileLen = ftell(pFile);
    if(nFileLen < 0) // error
    {
        LogPrintf("%s: ftell failed\n", __func__);
        return -1;
    }

    CAddressAmount data;

    long high = nFileLen / sizeof(CAddressAmount);
    long low = 0;
    long mid = 0;
    int nCmp = 0;
    while(low <= high)
    {
        mid = (low + high) / 2;

        if(fseek(pFile, mid * sizeof(CAddressAmount), SEEK_SET))
        {
            LogPrintf("%s: fseek failed\n", __func__);
            return -1;
        }

        if(1 != fread(&data, sizeof(CAddressAmount), 1, pFile))
        {
            if(feof(pFile))
            {
                if(pPos) *pPos = high;
                return 2;
            }

            LogPrintf("%s: fread failed\n", __func__);
            return -1;
        }

        nCmp = strcmp(data.szAddress, strAddress.data());
        if(nCmp < 0)
            low = mid + 1;
        else if(nCmp > 0)
            high = mid - 1;
        else // found
        {
            nAmount = data.nAmount;
            if(pPos) *pPos = mid;
            return 0;
        }
    }

    if(pPos) *pPos = low;
    return 1;
}

static bool GetChangeFilterAmount(const int& nStartHeight, const int& nEndHeight, CAmount& nChangeAmount, CAmount& nFilterAmount)
{
    string strDetailFile = GetDataDir().string() + "/height/detail.dat";
    FILE* pDetailFile = fopen(strDetailFile.data(), "rb");
    if(!pDetailFile)
        return error("%s: open detail.dat failed", __func__);

    if(fseek(pDetailFile, (nStartHeight - g_nCriticalHeight) * sizeof(CBlockDetail), SEEK_SET))
    {
        fclose(pDetailFile);
        return error("%s: fseek detail.dat to height: %d failed", __func__, nStartHeight);
    }

    CAmount nTempChangeAmount = 0;
    CAmount nTempFilterAmount = 0;

    CBlockDetail detail;
    int nCurHeight = nStartHeight;

    uint32_t nHandleCount = 0;
    while(fread(&detail, sizeof(CBlockDetail), 1, pDetailFile))
    {
        boost::this_thread::interruption_point();

        if(++nHandleCount % HANDLE_COUNT == 0)
        {
            nHandleCount = 0;
            MilliSleep(20);
        }

        if(nCurHeight > nEndHeight)
            break;

        if(detail.nHeight != nCurHeight)
        {
            fclose(pDetailFile);
            return error("%s: detail.dat is disordered", __func__);
        }

        nTempChangeAmount += detail.nReward;
        nTempFilterAmount += detail.nFilterAmount;
        nCurHeight++;
    }

    if(nCurHeight <= nEndHeight)
    {
        fclose(pDetailFile);
        return error("%s: detail.dat miss candy height from %d to %d", __func__, nCurHeight, nEndHeight);
    }

    fclose(pDetailFile);

    nChangeAmount = nTempChangeAmount;
    nFilterAmount = nTempFilterAmount;
    return true;
}

static bool GetHeightAddressAmount(const int& nCandyHeight)
{
    int nLastCandyHeight = 0;
    vector<int> vHeight;
    if(pblocktree->Read_CandyHeight_TotalAmount_Index(vHeight))
    {
        sort(vHeight.begin(), vHeight.end());
        nLastCandyHeight = vHeight.back();
    }

    if(nCandyHeight <= nLastCandyHeight)
        return true;

    CAmount nLastTotalAmount = 0;
    if(nLastCandyHeight != 0 && !GetTotalAmountByHeight(nLastCandyHeight, nLastTotalAmount))
        return error("%s: get total amount from %d failed", __func__, nLastCandyHeight);

    CAmount nChangeTotalAmount = 0;
    CAmount nFilterAmount = 0;
    int nStartHeight = nLastCandyHeight == 0 ? g_nCriticalHeight : nLastCandyHeight + 1;
    if(!GetChangeFilterAmount(nStartHeight, nCandyHeight, nChangeTotalAmount, nFilterAmount))
        return error("%s: get change-amount and filter-amount from %d to %d failed", __func__, nStartHeight, nCandyHeight);

    CAmount nTotalAmount = nLastTotalAmount + nChangeTotalAmount - nFilterAmount;
    if (!pblocktree->Write_CandyHeight_TotalAmount_Index(nCandyHeight, nTotalAmount))
        return error("%s: write finnal candy height index failed at %d", __func__, nCandyHeight);

    CBlock candyBlock;
    while(true)
    {
        boost::this_thread::interruption_point();

        if(nCandyHeight > chainActive.Height())
        {
            MilliSleep(1000);
            continue;
        }

        CBlockIndex* pindex = chainActive[nCandyHeight];
        if (ReadBlockFromDisk(candyBlock, pindex, Params().GetConsensus()))
            break;
        MilliSleep(1000);
    }

    bool fUpdateUI = false;

    map<CKeyID, int64_t> mapKeyBirth;
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->GetKeyBirthTimes(mapKeyBirth);
    }

    std::vector<std::string> vaddress;
    for (map<CKeyID, int64_t>::const_iterator tempit = mapKeyBirth.begin(); tempit != mapKeyBirth.end(); tempit++)
    {
        boost::this_thread::interruption_point();
        std::string saddress = CBitcoinAddress(tempit->first).ToString();
        vaddress.push_back(saddress);
    }

    int nCurrentHeight = g_nChainHeight;
    BOOST_FOREACH(const CTransaction& tx, candyBlock.vtx)
    {
        for(unsigned int i = 0; i < tx.vout.size(); i++)
        {
            boost::this_thread::interruption_point();

            const CTxOut& txout = tx.vout[i];

            CAppHeader header;
            std::vector<unsigned char> vData;
            if(!ParseReserve(txout.vReserve, header, vData) || header.nAppCmd != PUT_CANDY_CMD)
                continue;

            CPutCandyData candyData;
            if(!ParsePutCandyData(vData, candyData))
                continue;

            const uint256& assetId = candyData.assetId;
            if(assetId.IsNull())
                continue;

            CAssetId_AssetInfo_IndexValue assetInfo;
            if(!GetAssetInfoByAssetId(assetId, assetInfo, false))
                continue;

            COutPoint out(tx.GetHash(), i);
            CCandy_BlockTime_Info candyblocktimeinfo(assetId, assetInfo.assetData, CCandyInfo(candyData.nAmount, candyData.nExpired),out , candyBlock.nTime, nCandyHeight);

            if (candyData.nExpired * BLOCKS_PER_MONTH + nCandyHeight < nCurrentHeight)
                continue;

            if(nCandyHeight > nCurrentHeight)
                continue;

            bool result = false;
            for (std::vector<std::string>::iterator addit = vaddress.begin(); addit != vaddress.end(); addit++)
            {
                boost::this_thread::interruption_point();

                CAmount nSafe = 0;
                if(!GetAddressAmountByHeight(nCandyHeight, *addit, nSafe))
                    continue;
                if (nSafe < 1 * COIN || nSafe > nTotalAmount)
                    continue;

                CAmount nTempAmount = 0;
                CAmount nCandyAmount = (CAmount)(1.0 * nSafe / nTotalAmount * candyData.nAmount);
                if (nCandyAmount >= AmountFromValue("0.0001", assetInfo.assetData.nDecimals, true) && !GetGetCandyAmount(assetId, out, *addit, nTempAmount,false))
                {
                    result = true;
                    break;
                }
            }

            if(!result)
                continue;

            if(fUpdateAllCandyInfoFinished)
            {
                if(gAllCandyInfoVec.size()<nCandyPageCount)
                    fUpdateUI = true;
                std::lock_guard<std::mutex> lock(g_mutexAllCandyInfo);
                gAllCandyInfoVec.push_back(candyblocktimeinfo);
            }
            else
            {
                std::lock_guard<std::mutex> lock(g_mutexTmpAllCandyInfo);
                gTmpAllCandyInfoVec.push_back(candyblocktimeinfo);
            }
        }
    }
    if(fUpdateUI && fHaveGUI)
        uiInterface.CandyVecPut();

    return true;
}

void ThreadCalculateAddressAmount()
{
    SetThreadPriority(THREAD_PRIORITY_NORMAL);
    RenameThread("safe-calculate-amount");

    while (true)
    {
        boost::this_thread::interruption_point();
        int nCandyHeight = 0;
        if (GetCandyHeightFromList(nCandyHeight))
        {
            while(true)
            {
                boost::this_thread::interruption_point();
                if(GetHeightAddressAmount(nCandyHeight))
                {
                    MilliSleep(100);
                    break;
                }
                MilliSleep(1000);
            }
        }
        else
            MilliSleep(1000);
    }
}

static bool CheckDetailFile(FILE* pFile, const int& nHeight, int& nLastCandyHeight, bool& fValid)
{
    if(!pFile)
        return false;

    if(fseek(pFile, 0L, SEEK_END))
        return error("%s: fseek detail.dat failed", __func__);

    long nFileLen = ftell(pFile);
    if(nFileLen < 0)
        return error("%s: ftell detail.dat failed", __func__);

    int nLastHeight = nFileLen / sizeof(CBlockDetail) + g_nCriticalHeight - 1;

    if(nLastHeight < nHeight - 1 - g_nListChangeInfoLimited)
    {
        fValid = false;
        return error("%s: missing %d in detail.dat at least", __func__, nHeight - 1 - g_nListChangeInfoLimited);
    }

    if(nLastHeight >= nHeight)
    {
        if(fseek(pFile, (nHeight - g_nCriticalHeight) * sizeof(CBlockDetail), SEEK_SET))
            return error("%s: fseek %d from detail.dat failed", __func__, nHeight);

        CBlockDetail detail;
        if(1 != fread(&detail, sizeof(CBlockDetail), 1, pFile))
            return error("%s: fread %d from detail.dat failed", __func__, nHeight);

        nLastCandyHeight = detail.nLastCandyHeight;
    }

    return true;
}

static bool WriteDetailFile(const string& strFile, const CBlockDetail& detail)
{
    FILE* pFile = fopen(strFile.data(), "ab+");
    if(!pFile)
        return error("%s: open detail.dat failed", __func__);

    int nLastCandyHeight = -1;
    bool fValid = true;
    if(!CheckDetailFile(pFile, detail.nHeight, nLastCandyHeight, fValid))
    {
        fclose(pFile);
        if(!fValid)
            throw std::runtime_error(strprintf("%s: Corrupt detail.dat, missing block information", __func__));
        return error("%s: check detail.dat with %d failed", __func__, detail.nHeight);
    }

    if(nLastCandyHeight >= 0)
    {
        if(!TruncateFile(pFile, (detail.nHeight - g_nCriticalHeight) * sizeof(CBlockDetail)) || fflush(pFile))
        {
            fclose(pFile);
            return error("%s: truncate detail.dat start from %d failed", __func__, detail.nHeight);
        }
    }

    if(fseek(pFile, 0L, SEEK_END))
    {
        fclose(pFile);
        return error("%s: fseek detail.dat failed", __func__);
    }

    bool bRet = (fwrite(&detail, sizeof(CBlockDetail), 1, pFile) == 1);
    fclose(pFile);
    return bRet;
}

static bool WriteChangeFile(const string& strFile, const map<string, CAmount>& mapAddressAmount)
{
    string strTempFile = strFile + ".temp";
    boost::filesystem::remove(strTempFile);
    if(!MergeFileAndMap(strFile, mapAddressAmount, strTempFile))
    {
        boost::filesystem::remove(strTempFile);
        return error("%s: merge address information to %s failed", __func__, strFile);
    }

    string strBakFile = strFile + ".bak";
    boost::filesystem::remove(strBakFile);
    if(!RenameOver(strFile, strBakFile))
    {
        boost::filesystem::remove(strTempFile);
        return error("%s: backup %s to %s failed", __func__, strFile, strBakFile);
    }

    if(!RenameOver(strTempFile, strFile))
    {
        boost::filesystem::remove(strTempFile);
        if(!RenameOver(strBakFile, strFile))
            throw std::runtime_error(strprintf("%s: Restore %s to %s faield, missing address information", __func__, strBakFile, strFile));
        boost::filesystem::remove(strBakFile);
        return error("%s: rename %s to %s failed", __func__, strTempFile, strFile);
    }

    boost::filesystem::remove(strBakFile);
    return true;
}

static int g_nLastCandyHeight = 0;
bool LoadChangeInfoToList()
{
    boost::filesystem::path heightDir = GetDataDir() / "height";

    string strAllFile = heightDir.string() + "/all.dat";
    if(!boost::filesystem::exists(strAllFile))
    {
        FILE* pAllFile = fopen(strAllFile.data(), "ab+");
        if(!pAllFile)
            return error("%s: create empty all file failed", __func__);
        fclose(pAllFile);
    }

    g_listChangeInfo.clear();

    string strFile = heightDir.string() + "/detail.dat";
    FILE* pFile = fopen(strFile.data(), "ab+");
    if(!pFile)
        return error("%s: open detail.dat failed", __func__);
    if(fseek(pFile, 0L, SEEK_END))
    {
        fclose(pFile);
        return error("%s: fseek detail.dat failed", __func__);
    }
    long nLen = ftell(pFile);
    if(nLen < 0)
    {
        fclose(pFile);
        return error("%s: ftell detail.dat failed", __func__);
    }

    int nLastHeight = nLen / sizeof(CBlockDetail) + g_nCriticalHeight - 1;
    if(nLastHeight != g_nCriticalHeight - 1)
    {
        if(fseek(pFile, -1 * sizeof(CBlockDetail), SEEK_END))
        {
            fclose(pFile);
            return error("%s: fseek detail.dat failed at %d", __func__, nLastHeight);
        }

        CBlockDetail detail;
        if(fread(&detail, sizeof(CBlockDetail), 1, pFile) != 1)
        {
            fclose(pFile);
            return error("%s: read detail.dat failed at %d", __func__, nLastHeight);
        }

        if(detail.fCandy)
            g_nLastCandyHeight = detail.nHeight;
        else
            g_nLastCandyHeight = detail.nLastCandyHeight;
    }

    for(int nHeight = nLastHeight + 1; nHeight <= chainActive.Height(); nHeight++)
    {
        CBlockIndex* pindex = chainActive[nHeight];

        CBlock block;
        if(!ReadBlockFromDisk(block, pindex, Params().GetConsensus()))
        {
            fclose(pFile);
            return error("%s: read block from disk failed at %d, hash=%s", __func__, pindex->nHeight, pindex->GetBlockHash().ToString());
        }

        map<string, CAmount> mapAddressAmount;
        string strAddress = "";
        bool bExistCandy = false;

        for(size_t i = 0; i < block.vtx.size(); i++)
        {
            const CTransaction& tx = block.vtx[i];

            // vin
            if(!tx.IsCoinBase())
            {
                for(size_t j = 0; j < tx.vin.size(); j++)
                {
                    const CTxIn& txin = tx.vin[j];

                    CTransaction in_tx;
                    uint256 in_blockHash;
                    if(!GetTransaction(txin.prevout.hash, in_tx, Params().GetConsensus(), in_blockHash, true) || in_blockHash.IsNull())
                    {
                        fclose(pFile);
                        return error("%s: read txin transaction failed, hash=%s", __func__, txin.prevout.hash.ToString());
                    }

                    const CTxOut& in_txout = in_tx.vout[txin.prevout.n];
                    if(in_txout.IsAsset() || !GetTxOutAddress(in_txout, &strAddress))
                        continue;

                    if(mapBlockIndex.count(in_blockHash) == 0 || mapBlockIndex[in_blockHash]->nHeight < g_nCriticalHeight)
                        continue;

                    if(mapAddressAmount.count(strAddress))
                    {
                        mapAddressAmount[strAddress] += -in_txout.nValue;
                        if(mapAddressAmount[strAddress] == 0)
                            mapAddressAmount.erase(strAddress);
                    }
                    else
                        mapAddressAmount[strAddress] = -in_txout.nValue;
                }
            }

            for(size_t j = 0; j < tx.vout.size(); j++)
            {
                const CTxOut& txout = tx.vout[j];
                if(txout.IsAsset() || !GetTxOutAddress(txout, &strAddress))
                    continue;

                if(mapAddressAmount.count(strAddress))
                {
                    mapAddressAmount[strAddress] += txout.nValue;
                    if(mapAddressAmount[strAddress] == 0)
                        mapAddressAmount.erase(strAddress);
                }
                else
                    mapAddressAmount[strAddress] = txout.nValue;
            }

            for(size_t j = 0; j < tx.vout.size(); j++)
            {
                const CTxOut& txout = tx.vout[j];
                uint32_t nAppCmd = 0;
                if(!txout.IsAsset(&nAppCmd))
                    continue;
                if(nAppCmd == PUT_CANDY_CMD)
                {
                    bExistCandy = true;
                    break;
                }
            }
        }

        CAmount blockReward = 0;
        if(CheckCriticalBlock(block))
            blockReward = g_nCriticalReward;
        else
            blockReward = GetBlockSubsidy(pindex->pprev->nBits, pindex->pprev->nHeight, Params().GetConsensus());

        g_listChangeInfo.push_back(CChangeInfo(pindex->nHeight, g_nLastCandyHeight, blockReward, bExistCandy, mapAddressAmount));

        if(bExistCandy)
            g_nLastCandyHeight = pindex->nHeight;
    }

    fclose(pFile);
    return true;
}

static bool PutChangeInfoToList(const int& nHeight, const CAmount& nReward, const bool fCandy, const map<string, CAmount>& mapAddressAmount)
{
    if (nHeight < g_nCriticalHeight)
        return true;

    std::lock_guard<std::mutex> lock(g_mutexChangeInfo);

    if(!g_listChangeInfo.empty() && (nHeight < g_listChangeInfo.front().nHeight))
        return true;

    bool fExist = false;
    list<CChangeInfo>::iterator it = g_listChangeInfo.begin();
    for(; it != g_listChangeInfo.end(); it++)
    {
        if(it->nHeight == nHeight)
        {
            fExist = true;
            break;
        }
    }

    if(fExist)
    {
        while(it != g_listChangeInfo.end())
            it = g_listChangeInfo.erase(it);
    }

    string strFile = GetDataDir().string() + "/height/detail.dat";
    FILE* pFile = fopen(strFile.data(), "ab+");
    if(!pFile)
        return error("%s: open detail.dat failed", __func__);

    int nLastCandyHeight = -1;
    bool fValid = true;
    if(!CheckDetailFile(pFile, nHeight, nLastCandyHeight, fValid))
    {
        fclose(pFile);
        if(!fValid)
            throw std::runtime_error(strprintf("%s: 1-Corrupt detail.dat, missing block information", __func__));
        return error("%s: check detail.dat failed at %d", __func__, nHeight);
    }

    if(nLastCandyHeight < 0)
    {
        if(nHeight <= g_nLastCandyHeight) // after invoke invalidateblock, need reset g_nLastCandyHeight
        {
            if(nHeight <= g_nCriticalHeight)
            {
                g_nLastCandyHeight = 0;
            }
            else
            {
                if(fseek(pFile, 0L, SEEK_END))
                {
                    fclose(pFile);
                    return error("%s: fseek detail.dat failed", __func__);
                }
                long nLen = ftell(pFile);
                if(nLen < 0)
                {
                    fclose(pFile);
                    return error("%s: ftell detail.dat failed", __func__);
                }
                int nLastHeight = nLen / sizeof(CBlockDetail) + g_nCriticalHeight - 1;
                if(nLastHeight == g_nCriticalHeight - 1 || nLastHeight != nHeight - 1)
                    throw std::runtime_error(strprintf("%s: 2-Corrupt detail.dat, missing block information", __func__));
                else
                {
                    if(fseek(pFile, -1 * sizeof(CBlockDetail), SEEK_END))
                    {
                        fclose(pFile);
                        return error("%s: fseek detail.dat failed at %d", __func__, nLastHeight);
                    }
                    CBlockDetail detail;
                    if(fread(&detail, sizeof(CBlockDetail), 1, pFile) != 1)
                    {
                        fclose(pFile);
                        return error("%s: read detail.dat failed at %d", __func__, nLastHeight);
                    }
                    if(detail.fCandy)
                        g_nLastCandyHeight = detail.nHeight;
                    else
                        g_nLastCandyHeight = detail.nLastCandyHeight;
                }
            }
        }
    }
    fclose(pFile);

    if(nLastCandyHeight >= 0) // existent, maybe need rebuild candy index again
    {
        if(fCandy && !PutCandyHeightToList(nHeight))
            return error("%s: put candy height %d to list failed", __func__, nHeight);
        return true;
    }

    g_listChangeInfo.push_back(CChangeInfo(nHeight, g_nLastCandyHeight, nReward, fCandy, mapAddressAmount));

    if(fCandy)
        g_nLastCandyHeight = nHeight;

    return true;
}

static bool GetChangeInfoFromList(CChangeInfo& changeInfo)
{
    if(GetChangeInfoListSize() < g_nListChangeInfoLimited)
        return false;

    std::lock_guard<std::mutex> lock(g_mutexChangeInfo);
    changeInfo = g_listChangeInfo.front();
    g_listChangeInfo.pop_front();
    return true;
}

static void DeleteFilesToHeight(const int& nHeight)
{
    if(nHeight < 0)
        return;

    boost::filesystem::path heightDir = GetDataDir() / "height";
    if(!boost::filesystem::exists(heightDir) || !boost::filesystem::is_directory(heightDir))
        return;

    string strFileName = "";
    boost::filesystem::directory_iterator end_iter;
    for(boost::filesystem::directory_iterator iter(heightDir); iter != end_iter; ++iter)
    {
        boost::this_thread::interruption_point();

        if(!boost::filesystem::is_regular_file(iter->status()))
            continue;

        strFileName = iter->path().filename().string();

        int nTempHeight = atoi(strFileName);
        if(nTempHeight == 0 && (strFileName[0] > '9' || strFileName[0] < '0'))
            continue;

        if(nTempHeight >= nHeight)
            continue;

        boost::filesystem::remove(iter->path());
    }
}

static bool WriteChangeInfo(const CChangeInfo& changeInfo)
{
    if(changeInfo.nHeight <= 0 || changeInfo.nReward <= 0)
        return false;

    std::lock_guard<std::mutex> lock(g_mutexChangeFile);

    boost::filesystem::path heightDir = GetDataDir() / "height";

    static int nStep = 0;

    // 1. write all.dat
    if(nStep == 0)
    {
        string strAllFile = heightDir.string() + "/all.dat";
        try {
            if(!WriteChangeFile(strAllFile, changeInfo.mapAddressAmount))
                return error("%s: write all.dat at %d failed", __func__, changeInfo.nHeight);
        } catch (const boost::filesystem::filesystem_error& e) {
            return error("%s: write all.dat at %d throw exception", __func__, changeInfo.nHeight);
        }
        nStep = 1;
    }

    // 2. write change file
    if(nStep == 1)
    {
        if(changeInfo.nLastCandyHeight > 0)
        {
            string strChangeFile = heightDir.string() + "/" + itostr(changeInfo.nLastCandyHeight) + ".change";
            if(changeInfo.nHeight == changeInfo.nLastCandyHeight + 1)
            {
                FILE* pChangeFile = fopen(strChangeFile.data(), "ab+");
                if(!pChangeFile)
                    return error("%s: create empty change file %s at %d failed", strChangeFile, changeInfo.nHeight);
                fclose(pChangeFile);
            }
            try {
                if(!WriteChangeFile(strChangeFile, changeInfo.mapAddressAmount))
                    return error("%s: write changed address amount in block %d to %s failed", __func__, changeInfo.nHeight, strChangeFile);
            } catch (const boost::filesystem::filesystem_error& e) {
                return error("%s: write changed address amount in block %d to %s throw exception", __func__, changeInfo.nHeight, strChangeFile);
            }
        }
        nStep = 2;
    }

    // 3. write detail.dat
    if(nStep == 2)
    {
        string strDetailFile = heightDir.string() + "/detail.dat";

        CAmount nFilterAmount = 0;
        map<string, CAmount>::const_iterator it1 = changeInfo.mapAddressAmount.find(g_strCancelledMoneroCandyAddress);
        if(it1 != changeInfo.mapAddressAmount.end())
            nFilterAmount += it1->second;
        map<string, CAmount>::const_iterator it2 = changeInfo.mapAddressAmount.find(g_strCancelledSafeAddress);
        if(it2 != changeInfo.mapAddressAmount.end())
            nFilterAmount += it2->second;
        map<string, CAmount>::const_iterator it3 = changeInfo.mapAddressAmount.find(g_strCancelledAssetAddress);
        if(it3 != changeInfo.mapAddressAmount.end())
            nFilterAmount += it3->second;
        map<string, CAmount>::const_iterator it4 = changeInfo.mapAddressAmount.find(g_strPutCandyAddress);
        if(it4 != changeInfo.mapAddressAmount.end())
            nFilterAmount += it4->second;

        CBlockDetail detail(changeInfo.nHeight, changeInfo.nLastCandyHeight, changeInfo.nReward, nFilterAmount, changeInfo.fCandy);
        if(!WriteDetailFile(strDetailFile, detail))
            return error("%s: write %d to detail.dat failed", __func__, changeInfo.nHeight);
        nStep = 3;
    }

    // 4. write candy information
    if(nStep == 3)
    {
        if(changeInfo.fCandy && !PutCandyHeightToList(changeInfo.nHeight))
            return error("%s: put candy height %d to list failed", __func__, changeInfo.nHeight);
        nStep = 4;
    }

    // 5. remove change file before 3 month
    if(nStep == 4)
    {
        int nEndHeight = changeInfo.nHeight - 3 * BLOCKS_PER_MONTH;
        if(nEndHeight >= changeInfo.nLastCandyHeight)
            nEndHeight = changeInfo.nLastCandyHeight;
        DeleteFilesToHeight(nEndHeight);
    }

    nStep = 0;
    return true;
}

void ThreadWriteChangeInfo()
{
    SetThreadPriority(THREAD_PRIORITY_NORMAL);
    RenameThread("safe-change");

    while(true)
    {
        boost::this_thread::interruption_point();
        CChangeInfo changeInfo;
        if(GetChangeInfoFromList(changeInfo))
        {
            while(true)
            {
                boost::this_thread::interruption_point();
                if(WriteChangeInfo(changeInfo))
                    break;
                MilliSleep(100);
            }
        }
        else
            MilliSleep(10);
    }
}

void resetNumA(std::string numAStr)
{
    memset(numA, 0, M * sizeof(int));

    for (uint32_t i = 0; i < numAStr.length(); i++)
        numA[i] = numAStr[numAStr.length() - i - 1] - '0';
}

void resetNumB(std::string numBStr)
{
    memset(numB, 0, M * sizeof(int));

    for (uint32_t i = 0; i < numBStr.length(); i++)
        numB[i] = numBStr[numBStr.length()-i-1] - '0';
}

std::string getNumString(int* num)
{
    std::string numString = "";
    bool isBegin = false;
    for (int i = M - 1; i >= 0 ; i--)
    {
        if(num[i] != 0)
            isBegin = true;

        if (isBegin)
            numString += num[i] + '0';
    }
    return numString;
}

int comparestring(std::string numAStr, std::string numBStr)
{
    if (numAStr.length() > numBStr.length())
        return 1;
    else if (numAStr.length() < numBStr.length())
        return -1;
    else
    {
        for (uint32_t i = 0; i < numAStr.length(); i++)
        {
            if (numAStr[i]>numBStr[i])
                return 1;

            if (numAStr[i]<numBStr[i])
                return -1;
        }

        return 0;
    }
}

int compareFloatString(const std::string& numAStr,const std::string& numBStr,bool fOnlyCompareInt)
{
    int posA = numAStr.find('.');
    int posB = numBStr.find('.');

    string posAIntStr = numAStr;
    if(posA>0)
        posAIntStr = numAStr.substr(0,posA);

    string posBIntStr = numBStr;
    if(posB>0)
        posBIntStr = numBStr.substr(0,posB);

    if(fOnlyCompareInt)
        return comparestring(posAIntStr,posBIntStr);

    string posAFloatStr = "0",posBFloatStr="0";
    if(posA>0)
    {
        bool skipFirstZero = true;
        for(unsigned int i=posA+1;i<numAStr.size();i++)
        {
            if(skipFirstZero&&numAStr[i]=='0')
                continue;
            if(skipFirstZero)
            {
                skipFirstZero = false;
                posAFloatStr.clear();
            }
            posAFloatStr.push_back(numAStr[i]);
        }
    }
    if(posB>0)
    {
        bool skipFirstZero = true;
        for(unsigned int i=posB+1;i<numBStr.size();i++)
        {
            if(skipFirstZero&&numBStr[i]=='0')
                continue;
            if(skipFirstZero)
            {
                skipFirstZero = false;
                posBFloatStr.clear();
            }
            posBFloatStr.push_back(numBStr[i]);
        }
    }
    int aFloatSize = numAStr.size()-posA-1;
    int bFloatSize = numBStr.size()-posB-1;
    int size = std::abs(aFloatSize-bFloatSize);
    for(int i=0;i<size;i++)
    {
        if(aFloatSize<bFloatSize)
            posAFloatStr.push_back('0');
        else
            posBFloatStr.push_back('0');
    }
    return comparestring(posAFloatStr,posBFloatStr);
}

std::string plusstring(std::string numAStr, std::string numBStr)
{
    resetNumA(numAStr);
    resetNumB(numBStr);

    for (int i = 0; i < M; i++)
    {
        numA[i] += numB[i];

        if(numA[i] > 9)
        {
            numA[i] -= 10;
            numA[i+1]++;
        }
    }

    return getNumString(numA);
}

std::string minusstring(std::string numAStr, std::string numBStr)
{
    bool isNegative = false;

    if (comparestring(numAStr,numBStr)==-1)
    {
        isNegative = true;
        string temp = numAStr;
        numAStr = numBStr;
        numBStr = temp;
    }
    else if (comparestring(numAStr,numBStr)==0)
        return "0";

    resetNumA(numAStr);
    resetNumB(numBStr);

    for (int i = 0; i < M; i++)
    {
        if (numA[i]<numB[i])
        {
            numA[i] = numA[i]+10-numB[i];
            numA[i+1]--;
        }
        else
            numA[i] -= numB[i];
    }

    if (isNegative)
        return "-"+  getNumString(numA);
    else
        return getNumString(numA);
}

std::string mulstring(std::string numAStr, std::string numBStr)
{
    resetNumA(numAStr);
    resetNumB(numBStr);

    vector<string> vnums;
    for (uint32_t i = 0; i < numBStr.length(); i++)
    {
        int tempnum[M];
        memset(tempnum, 0, M * sizeof(int));

        for (uint32_t j = i; j < numAStr.length() + i; j++)
        {
            tempnum[j] += numA[j - i] * numB[i] % 10;
            tempnum[j + 1] = numA[j - i] * numB[i] /10;
            if (tempnum[j] > 9)
            {
                tempnum[j] -= 10;
                tempnum[j + 1]++;
            }
        }

        vnums.push_back(getNumString(tempnum));
    }

    string strresult = vnums[0];
    for (uint32_t i = 1; i < vnums.size(); i++)
        strresult = plusstring(strresult, vnums[i]);

    return strresult;
}

std::string numtofloatstring(std::string numstr, int32_t Decimals)
{
    if (numstr.empty())
        return "";

    int32_t istrlength = numstr.length();
    if (istrlength <= Decimals)
    {
        return "";
    }

    std::string strlastfloat = "";
    std::string strnumber = numstr.substr(0, numstr.length() - Decimals);
    std::string strfloat = numstr.substr(numstr.length() - Decimals);
    strlastfloat = strnumber + "." + strfloat;

    return strlastfloat;
}

bool VerifyDetailFile()
{
    boost::filesystem::path heightDir = GetDataDir() / "height";

    if(!boost::filesystem::exists(heightDir) || !boost::filesystem::is_directory(heightDir))
    {
        try {
            if(!TryCreateDirectory(heightDir))
                return error("1-specified data directory height cannot be created.");
        } catch (const boost::filesystem::filesystem_error&) {
            return error("2-specified data directory height cannot be created.");
        }
    }

    string strFile = heightDir.string() + "/detail.dat";
    FILE* pFile = fopen(strFile.data(), "ab+");
    if(!pFile)
        return error("%s: open detail.dat failed", __func__);

    if(fseek(pFile, 0L, SEEK_SET))
    {
        fclose(pFile);
        return error("%s: fseek detail.dat failed", __func__);
    }

    CBlockDetail detail;
    int nHeight = g_nCriticalHeight;
    while(true)
    {
        if(fread(&detail, sizeof(CBlockDetail), 1, pFile) != 1)
        {
            if(feof(pFile))
                break;
            fclose(pFile);
            return error("%s: read detail.dat failed", __func__);
        }

        if(detail.nHeight != nHeight)
        {
            fclose(pFile);
            uiInterface.ThreadSafeQuestion(
                _("detail.dat is corrupted, please restart with -reindex=1 or set reindex=1 in safe.conf if you want to repaire it."),
                "detail.dat is corrupted, please restart with -reindex=1 or set reindex=1 in safe.conf if you want to repaire it.",
                "",
                CClientUIInterface::MSG_ERROR);
            return false;
        }

        nHeight++;
    }

    fclose(pFile);
    return true;
}
