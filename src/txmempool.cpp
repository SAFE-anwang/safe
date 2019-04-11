// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txmempool.h"

#include "clientversion.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "validation.h"
#include "policy/fees.h"
#include "random.h"
#include "streams.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "version.h"
#include "base58.h"
#include "main.h"
#include "app/app.h"
#include <boost/thread.hpp>


using namespace std;

CTxMemPoolEntry::CTxMemPoolEntry(const CTransaction& _tx, const CAmount& _nFee,
                                 int64_t _nTime, double _entryPriority, unsigned int _entryHeight,
                                 bool poolHasNoInputsOf, CAmount _inChainInputValue,
                                 bool _spendsCoinbase, unsigned int _sigOps, LockPoints lp):
    tx(_tx), nFee(_nFee), nTime(_nTime), entryPriority(_entryPriority), entryHeight(_entryHeight),
    hadNoDependencies(poolHasNoInputsOf), inChainInputValue(_inChainInputValue),
    spendsCoinbase(_spendsCoinbase), sigOpCount(_sigOps), lockPoints(lp)
{
    nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    nModSize = tx.CalculateModifiedSize(nTxSize);
    nUsageSize = RecursiveDynamicUsage(tx);

    nCountWithDescendants = 1;
    nSizeWithDescendants = nTxSize;
    nModFeesWithDescendants = nFee;
    CAmount nValueIn = tx.GetValueOut()+nFee;
    assert(inChainInputValue <= nValueIn);

    feeDelta = 0;
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTxMemPoolEntry& other)
{
    *this = other;
}

double
CTxMemPoolEntry::GetPriority(unsigned int currentHeight) const
{
    double deltaPriority = ((double)(currentHeight-entryHeight)*inChainInputValue)/nModSize;
    double dResult = entryPriority + deltaPriority;
    if (dResult < 0) // This should only happen if it was called with a height below entry height
        dResult = 0;
    return dResult;
}

void CTxMemPoolEntry::UpdateFeeDelta(int64_t newFeeDelta)
{
    nModFeesWithDescendants += newFeeDelta - feeDelta;
    feeDelta = newFeeDelta;
}

void CTxMemPoolEntry::UpdateLockPoints(const LockPoints& lp)
{
    lockPoints = lp;
}

// Update the given tx for any in-mempool descendants.
// Assumes that setMemPoolChildren is correct for the given tx and all
// descendants.
bool CTxMemPool::UpdateForDescendants(txiter updateIt, int maxDescendantsToVisit, cacheMap &cachedDescendants, const std::set<uint256> &setExclude)
{
    // Track the number of entries (outside setExclude) that we'd need to visit
    // (will bail out if it exceeds maxDescendantsToVisit)
    int nChildrenToVisit = 0;

    setEntries stageEntries, setAllDescendants;
    stageEntries = GetMemPoolChildren(updateIt);

    while (!stageEntries.empty()) {
        const txiter cit = *stageEntries.begin();
        if (cit->IsDirty()) {
            // Don't consider any more children if any descendant is dirty
            return false;
        }
        setAllDescendants.insert(cit);
        stageEntries.erase(cit);
        const setEntries &setChildren = GetMemPoolChildren(cit);
        BOOST_FOREACH(const txiter childEntry, setChildren) {
            cacheMap::iterator cacheIt = cachedDescendants.find(childEntry);
            if (cacheIt != cachedDescendants.end()) {
                // We've already calculated this one, just add the entries for this set
                // but don't traverse again.
                BOOST_FOREACH(const txiter cacheEntry, cacheIt->second) {
                    // update visit count only for new child transactions
                    // (outside of setExclude and stageEntries)
                    if (setAllDescendants.insert(cacheEntry).second &&
                            !setExclude.count(cacheEntry->GetTx().GetHash()) &&
                            !stageEntries.count(cacheEntry)) {
                        nChildrenToVisit++;
                    }
                }
            } else if (!setAllDescendants.count(childEntry)) {
                // Schedule for later processing and update our visit count
                if (stageEntries.insert(childEntry).second && !setExclude.count(childEntry->GetTx().GetHash())) {
                        nChildrenToVisit++;
                }
            }
            if (nChildrenToVisit > maxDescendantsToVisit) {
                return false;
            }
        }
    }
    // setAllDescendants now contains all in-mempool descendants of updateIt.
    // Update and add to cached descendant map
    int64_t modifySize = 0;
    CAmount modifyFee = 0;
    int64_t modifyCount = 0;
    BOOST_FOREACH(txiter cit, setAllDescendants) {
        if (!setExclude.count(cit->GetTx().GetHash())) {
            modifySize += cit->GetTxSize();
            modifyFee += cit->GetModifiedFee();
            modifyCount++;
            cachedDescendants[updateIt].insert(cit);
        }
    }
    mapTx.modify(updateIt, update_descendant_state(modifySize, modifyFee, modifyCount));
    return true;
}

// vHashesToUpdate is the set of transaction hashes from a disconnected block
// which has been re-added to the mempool.
// for each entry, look for descendants that are outside hashesToUpdate, and
// add fee/size information for such descendants to the parent.
void CTxMemPool::UpdateTransactionsFromBlock(const std::vector<uint256> &vHashesToUpdate)
{
    LOCK(cs);
    // For each entry in vHashesToUpdate, store the set of in-mempool, but not
    // in-vHashesToUpdate transactions, so that we don't have to recalculate
    // descendants when we come across a previously seen entry.
    cacheMap mapMemPoolDescendantsToUpdate;

    // Use a set for lookups into vHashesToUpdate (these entries are already
    // accounted for in the state of their ancestors)
    std::set<uint256> setAlreadyIncluded(vHashesToUpdate.begin(), vHashesToUpdate.end());

    // Iterate in reverse, so that whenever we are looking at at a transaction
    // we are sure that all in-mempool descendants have already been processed.
    // This maximizes the benefit of the descendant cache and guarantees that
    // setMemPoolChildren will be updated, an assumption made in
    // UpdateForDescendants.
    BOOST_REVERSE_FOREACH(const uint256 &hash, vHashesToUpdate) {
        // we cache the in-mempool children to avoid duplicate updates
        setEntries setChildren;
        // calculate children from mapNextTx
        txiter it = mapTx.find(hash);
        if (it == mapTx.end()) {
            continue;
        }
        std::map<COutPoint, CInPoint>::iterator iter = mapNextTx.lower_bound(COutPoint(hash, 0));
        // First calculate the children, and update setMemPoolChildren to
        // include them, and update their setMemPoolParents to include this tx.
        for (; iter != mapNextTx.end() && iter->first.hash == hash; ++iter) {
            const uint256 &childHash = iter->second.ptx->GetHash();
            txiter childIter = mapTx.find(childHash);
            assert(childIter != mapTx.end());
            // We can skip updating entries we've encountered before or that
            // are in the block (which are already accounted for).
            if (setChildren.insert(childIter).second && !setAlreadyIncluded.count(childHash)) {
                UpdateChild(it, childIter, true);
                UpdateParent(childIter, it, true);
            }
        }
        if (!UpdateForDescendants(it, 100, mapMemPoolDescendantsToUpdate, setAlreadyIncluded)) {
            // Mark as dirty if we can't do the calculation.
            mapTx.modify(it, set_dirty());
        }
    }
}

bool CTxMemPool::CalculateMemPoolAncestors(const CTxMemPoolEntry &entry, setEntries &setAncestors, uint64_t limitAncestorCount, uint64_t limitAncestorSize, uint64_t limitDescendantCount, uint64_t limitDescendantSize, std::string &errString, bool fSearchForParents /* = true */)
{
    setEntries parentHashes;
    const CTransaction &tx = entry.GetTx();

    if (fSearchForParents) {
        // Get parents of this transaction that are in the mempool
        // GetMemPoolParents() is only valid for entries in the mempool, so we
        // iterate mapTx to find parents.
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            boost::this_thread::interruption_point();
            txiter piter = mapTx.find(tx.vin[i].prevout.hash);
            if (piter != mapTx.end()) {
                parentHashes.insert(piter);
                if (parentHashes.size() + 1 > limitAncestorCount) {
                    errString = strprintf("too many unconfirmed parents [limit: %u]", limitAncestorCount);
                    return false;
                }
            }
        }
    } else {
        // If we're not searching for parents, we require this to be an
        // entry in the mempool already.
        txiter it = mapTx.iterator_to(entry);
        parentHashes = GetMemPoolParents(it);
    }

    size_t totalSizeWithAncestors = entry.GetTxSize();

    while (!parentHashes.empty()) {
        boost::this_thread::interruption_point();
        txiter stageit = *parentHashes.begin();

        setAncestors.insert(stageit);
        parentHashes.erase(stageit);
        totalSizeWithAncestors += stageit->GetTxSize();

        if (stageit->GetSizeWithDescendants() + entry.GetTxSize() > limitDescendantSize) {
            errString = strprintf("exceeds descendant size limit for tx %s [limit: %u]", stageit->GetTx().GetHash().ToString(), limitDescendantSize);
            return false;
        } else if (stageit->GetCountWithDescendants() + 1 > limitDescendantCount) {
            errString = strprintf("too many descendants for tx %s [limit: %u]", stageit->GetTx().GetHash().ToString(), limitDescendantCount);
            return false;
        } else if (totalSizeWithAncestors > limitAncestorSize) {
            errString = strprintf("exceeds ancestor size limit [limit: %u]", limitAncestorSize);
            return false;
        }

        const setEntries & setMemPoolParents = GetMemPoolParents(stageit);
        BOOST_FOREACH(const txiter &phash, setMemPoolParents) {
            boost::this_thread::interruption_point();
            // If this is a new ancestor, add it.
            if (setAncestors.count(phash) == 0) {
                parentHashes.insert(phash);
            }
            if (parentHashes.size() + setAncestors.size() + 1 > limitAncestorCount) {
                errString = strprintf("too many unconfirmed ancestors [limit: %u]", limitAncestorCount);
                return false;
            }
        }
    }

    return true;
}

void CTxMemPool::UpdateAncestorsOf(bool add, txiter it, setEntries &setAncestors)
{
    setEntries parentIters = GetMemPoolParents(it);
    // add or remove this tx as a child of each parent
    BOOST_FOREACH(txiter piter, parentIters) {
        UpdateChild(piter, it, add);
    }
    const int64_t updateCount = (add ? 1 : -1);
    const int64_t updateSize = updateCount * it->GetTxSize();
    const CAmount updateFee = updateCount * it->GetModifiedFee();
    BOOST_FOREACH(txiter ancestorIt, setAncestors) {
        mapTx.modify(ancestorIt, update_descendant_state(updateSize, updateFee, updateCount));
    }
}

void CTxMemPool::UpdateChildrenForRemoval(txiter it)
{
    const setEntries &setMemPoolChildren = GetMemPoolChildren(it);
    BOOST_FOREACH(txiter updateIt, setMemPoolChildren) {
        UpdateParent(updateIt, it, false);
    }
}

void CTxMemPool::UpdateForRemoveFromMempool(const setEntries &entriesToRemove)
{
    // For each entry, walk back all ancestors and decrement size associated with this
    // transaction
    const uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
    BOOST_FOREACH(txiter removeIt, entriesToRemove) {
        setEntries setAncestors;
        const CTxMemPoolEntry &entry = *removeIt;
        std::string dummy;
        // Since this is a tx that is already in the mempool, we can call CMPA
        // with fSearchForParents = false.  If the mempool is in a consistent
        // state, then using true or false should both be correct, though false
        // should be a bit faster.
        // However, if we happen to be in the middle of processing a reorg, then
        // the mempool can be in an inconsistent state.  In this case, the set
        // of ancestors reachable via mapLinks will be the same as the set of
        // ancestors whose packages include this transaction, because when we
        // add a new transaction to the mempool in addUnchecked(), we assume it
        // has no children, and in the case of a reorg where that assumption is
        // false, the in-mempool children aren't linked to the in-block tx's
        // until UpdateTransactionsFromBlock() is called.
        // So if we're being called during a reorg, ie before
        // UpdateTransactionsFromBlock() has been called, then mapLinks[] will
        // differ from the set of mempool parents we'd calculate by searching,
        // and it's important that we use the mapLinks[] notion of ancestor
        // transactions as the set of things to update for removal.
        CalculateMemPoolAncestors(entry, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);
        // Note that UpdateAncestorsOf severs the child links that point to
        // removeIt in the entries for the parents of removeIt.  This is
        // fine since we don't need to use the mempool children of any entries
        // to walk back over our ancestors (but we do need the mempool
        // parents!)
        UpdateAncestorsOf(false, removeIt, setAncestors);
    }
    // After updating all the ancestor sizes, we can now sever the link between each
    // transaction being removed and any mempool children (ie, update setMemPoolParents
    // for each direct child of a transaction being removed).
    BOOST_FOREACH(txiter removeIt, entriesToRemove) {
        UpdateChildrenForRemoval(removeIt);
    }
}

void CTxMemPoolEntry::SetDirty()
{
    nCountWithDescendants = 0;
    nSizeWithDescendants = nTxSize;
    nModFeesWithDescendants = GetModifiedFee();
}

void CTxMemPoolEntry::UpdateState(int64_t modifySize, CAmount modifyFee, int64_t modifyCount)
{
    if (!IsDirty()) {
        nSizeWithDescendants += modifySize;
        assert(int64_t(nSizeWithDescendants) > 0);
        nModFeesWithDescendants += modifyFee;
        nCountWithDescendants += modifyCount;
        assert(int64_t(nCountWithDescendants) > 0);
    }
}

CTxMemPool::CTxMemPool(const CFeeRate& _minReasonableRelayFee) :
    nTransactionsUpdated(0)
{
    _clear(); //lock free clear

    // Sanity checks off by default for performance, because otherwise
    // accepting transactions becomes O(N^2) where N is the number
    // of transactions in the pool
    nCheckFrequency = 0;

    minerPolicyEstimator = new CBlockPolicyEstimator(_minReasonableRelayFee);
    minReasonableRelayFee = _minReasonableRelayFee;
}

CTxMemPool::~CTxMemPool()
{
    delete minerPolicyEstimator;
}

void CTxMemPool::pruneSpent(const uint256 &hashTx, CCoins &coins)
{
    LOCK(cs);

    std::map<COutPoint, CInPoint>::iterator it = mapNextTx.lower_bound(COutPoint(hashTx, 0));

    // iterate over all COutPoints in mapNextTx whose hash equals the provided hashTx
    while (it != mapNextTx.end() && it->first.hash == hashTx) {
        coins.Spend(it->first.n); // and remove those outputs from coins
        it++;
    }
}

unsigned int CTxMemPool::GetTransactionsUpdated() const
{
    LOCK(cs);
    return nTransactionsUpdated;
}

void CTxMemPool::AddTransactionsUpdated(unsigned int n)
{
    LOCK(cs);
    nTransactionsUpdated += n;
}

bool CTxMemPool::addUnchecked(const uint256& hash, const CTxMemPoolEntry &entry, const CCoinsViewCache &view, setEntries &setAncestors, bool fCurrentEstimate)
{
    // Add to memory pool without checking anything.
    // Used by main.cpp AcceptToMemoryPool(), which DOES do
    // all the appropriate checks.
    LOCK(cs);
    indexed_transaction_set::iterator newit = mapTx.insert(entry).first;
    mapLinks.insert(make_pair(newit, TxLinks()));

    // Update transaction for any feeDelta created by PrioritiseTransaction
    // TODO: refactor so that the fee delta is calculated before inserting
    // into mapTx.
    std::map<uint256, std::pair<double, CAmount> >::const_iterator pos = mapDeltas.find(hash);
    if (pos != mapDeltas.end()) {
        const std::pair<double, CAmount> &deltas = pos->second;
        if (deltas.second) {
            mapTx.modify(newit, update_fee_delta(deltas.second));
        }
    }

    // Update cachedInnerUsage to include contained transaction's usage.
    // (When we update the entry for in-mempool parents, memory usage will be
    // further updated.)
    cachedInnerUsage += entry.DynamicMemoryUsage();

    const CTransaction& tx = newit->GetTx();
    std::set<uint256> setParentTransactions;
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        CCoins coins;
        if(view.GetCoins(txin.prevout.hash, coins))
        {
            const CTxOut& in_txout = coins.vout[txin.prevout.n];
            uint32_t nAppCmd = 0;
            if(in_txout.IsAsset(&nAppCmd) && nAppCmd == PUT_CANDY_CMD && txin.scriptSig.empty())
            {
                CTxDestination dest;
                if(ExtractDestination(in_txout.scriptPubKey, dest))
                {
                    CBitcoinAddress address(dest);
                    if(address.IsValid() && address.ToString() == g_strPutCandyAddress)
                        continue;
                }
            }
        }
        mapNextTx[txin.prevout] = CInPoint(&tx, i);
        setParentTransactions.insert(txin.prevout.hash);
    }
    // Don't bother worrying about child transactions of this one.
    // Normal case of a new transaction arriving is that there can't be any
    // children, because such children would be orphans.
    // An exception to that is if a transaction enters that used to be in a block.
    // In that case, our disconnect block logic will call UpdateTransactionsFromBlock
    // to clean up the mess we're leaving here.

    // Update ancestors with information about this tx
    BOOST_FOREACH (const uint256 &phash, setParentTransactions) {
        txiter pit = mapTx.find(phash);
        if (pit != mapTx.end()) {
            UpdateParent(newit, pit, true);
        }
    }
    UpdateAncestorsOf(true, newit, setAncestors);

    nTransactionsUpdated++;
    totalTxSize += entry.GetTxSize();
    minerPolicyEstimator->processTransaction(entry, fCurrentEstimate);

    return true;
}

void CTxMemPool::addAddressIndex(const CTxMemPoolEntry &entry, const CCoinsViewCache &view)
{
    LOCK(cs);
    const CTransaction& tx = entry.GetTx();
    std::vector<CMempoolAddressDeltaKey> inserted;

    uint256 txhash = tx.GetHash();
    for (unsigned int j = 0; j < tx.vin.size(); j++) {
        const CTxIn input = tx.vin[j];
        const CTxOut &prevout = view.GetOutputFor(input);
        if(prevout.IsAsset())
            continue;
        if (prevout.scriptPubKey.IsPayToScriptHash()) {
            vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22);
            CMempoolAddressDeltaKey key(2, uint160(hashBytes), txhash, j, 1);
            CMempoolAddressDelta delta(entry.GetTime(), prevout.nValue * -1, input.prevout.hash, input.prevout.n);
            mapAddress.insert(make_pair(key, delta));
            inserted.push_back(key);
        } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
            vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23);
            CMempoolAddressDeltaKey key(1, uint160(hashBytes), txhash, j, 1);
            CMempoolAddressDelta delta(entry.GetTime(), prevout.nValue * -1, input.prevout.hash, input.prevout.n);
            mapAddress.insert(make_pair(key, delta));
            inserted.push_back(key);
        } else if (prevout.scriptPubKey.IsPayToPublicKey()) {
            uint160 hashBytes(Hash160(prevout.scriptPubKey.begin()+1, prevout.scriptPubKey.end()-1));
            CMempoolAddressDeltaKey key(1, hashBytes, txhash, j, 1);
            CMempoolAddressDelta delta(entry.GetTime(), prevout.nValue * -1, input.prevout.hash, input.prevout.n);
            mapAddress.insert(make_pair(key, delta));
            inserted.push_back(key);
        }
    }

    for (unsigned int k = 0; k < tx.vout.size(); k++) {
        const CTxOut &out = tx.vout[k];
        if(out.IsAsset())
            continue;
        if (out.scriptPubKey.IsPayToScriptHash()) {
            vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);
            CMempoolAddressDeltaKey key(2, uint160(hashBytes), txhash, k, 0);
            mapAddress.insert(make_pair(key, CMempoolAddressDelta(entry.GetTime(), out.nValue)));
            inserted.push_back(key);
        } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
            vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);
            CMempoolAddressDeltaKey key(1, uint160(hashBytes), txhash, k, 0);
            mapAddress.insert(make_pair(key, CMempoolAddressDelta(entry.GetTime(), out.nValue)));
            inserted.push_back(key);
        } else if (out.scriptPubKey.IsPayToPublicKey()) {
            uint160 hashBytes(Hash160(out.scriptPubKey.begin()+1, out.scriptPubKey.end()-1));
            CMempoolAddressDeltaKey key(1, hashBytes, txhash, k, 0);
            mapAddress.insert(make_pair(key, CMempoolAddressDelta(entry.GetTime(), out.nValue)));
            inserted.push_back(key);
        }
    }

    mapAddressInserted.insert(make_pair(txhash, inserted));
}

bool CTxMemPool::getAddressIndex(std::vector<std::pair<uint160, int> > &addresses,
                                 std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> > &results)
{
    LOCK(cs);
    for (std::vector<std::pair<uint160, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {
        addressDeltaMap::iterator ait = mapAddress.lower_bound(CMempoolAddressDeltaKey((*it).second, (*it).first));
        while (ait != mapAddress.end() && (*ait).first.addressBytes == (*it).first && (*ait).first.type == (*it).second) {
            results.push_back(*ait);
            ait++;
        }
    }
    return true;
}

bool CTxMemPool::removeAddressIndex(const uint256 txhash)
{
    LOCK(cs);
    addressDeltaMapInserted::iterator it = mapAddressInserted.find(txhash);

    if (it != mapAddressInserted.end()) {
        std::vector<CMempoolAddressDeltaKey> keys = (*it).second;
        for (std::vector<CMempoolAddressDeltaKey>::iterator mit = keys.begin(); mit != keys.end(); mit++) {
            mapAddress.erase(*mit);
        }
        mapAddressInserted.erase(it);
    }

    return true;
}

void CTxMemPool::addSpentIndex(const CTxMemPoolEntry &entry, const CCoinsViewCache &view)
{
    LOCK(cs);

    const CTransaction& tx = entry.GetTx();
    std::vector<CSpentIndexKey> inserted;

    uint256 txhash = tx.GetHash();
    for (unsigned int j = 0; j < tx.vin.size(); j++) {
        const CTxIn input = tx.vin[j];
        const CTxOut &prevout = view.GetOutputFor(input);
        uint160 addressHash;
        int addressType;

        if (prevout.scriptPubKey.IsPayToScriptHash()) {
            addressHash = uint160(vector<unsigned char> (prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22));
            addressType = 2;
        } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
            addressHash = uint160(vector<unsigned char> (prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23));
            addressType = 1;
        } else if (prevout.scriptPubKey.IsPayToPublicKey()) {
            addressHash = Hash160(prevout.scriptPubKey.begin()+1, prevout.scriptPubKey.end()-1);
            addressType = 1;
        } else {
            addressHash.SetNull();
            addressType = 0;
        }

        CSpentIndexKey key = CSpentIndexKey(input.prevout.hash, input.prevout.n);
        CSpentIndexValue value = CSpentIndexValue(txhash, j, -1, prevout.nValue, addressType, addressHash);

        mapSpent.insert(make_pair(key, value));
        inserted.push_back(key);

    }

    mapSpentInserted.insert(make_pair(txhash, inserted));
}

bool CTxMemPool::getSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value)
{
    LOCK(cs);
    mapSpentIndex::iterator it;

    it = mapSpent.find(key);
    if (it != mapSpent.end()) {
        value = it->second;
        return true;
    }
    return false;
}

bool CTxMemPool::removeSpentIndex(const uint256 txhash)
{
    LOCK(cs);
    mapSpentIndexInserted::iterator it = mapSpentInserted.find(txhash);

    if (it != mapSpentInserted.end()) {
        std::vector<CSpentIndexKey> keys = (*it).second;
        for (std::vector<CSpentIndexKey>::iterator mit = keys.begin(); mit != keys.end(); mit++) {
            mapSpent.erase(*mit);
        }
        mapSpentInserted.erase(it);
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////
void CTxMemPool::addAppInfoIndex(const CTxMemPoolEntry& entry, const CCoinsViewCache& view)
{
    LOCK(cs);
    const CTransaction& tx = entry.GetTx();
    std::vector<uint256> appId_inserted;
    std::vector<std::string> appName_inserted;

    uint256 txhash = tx.GetHash();
    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CAppHeader header;
        std::vector<unsigned char> vData;
        if(ParseReserve(txout.vReserve, header, vData))
        {
            CTxDestination dest;
            if(!ExtractDestination(txout.scriptPubKey, dest))
                continue;

            if(header.nAppCmd == REGISTER_APP_CMD)
            {
                CAppData appData;
                if(ParseRegisterData(vData, appData))
                {
                    mapAppId_AppInfo.insert(make_pair(header.appId, CAppId_AppInfo_IndexValue(CBitcoinAddress(dest).ToString(), appData)));
                    appId_inserted.push_back(header.appId);

                    mapAppName_AppId.insert(make_pair(ToLower(appData.strAppName), CName_Id_IndexValue(header.appId)));
                    appName_inserted.push_back(ToLower(appData.strAppName));
                }
            }
        }
    }

    mapAppId_AppInfo_Inserted.insert(make_pair(txhash, appId_inserted));
    mapAppName_AppId_Inserted.insert(make_pair(txhash, appName_inserted));
}

bool CTxMemPool::getAppInfoByAppId(const uint256& appId, CAppId_AppInfo_IndexValue& appInfo)
{
    LOCK(cs);
    mapAppId_AppInfo_Index::iterator it = mapAppId_AppInfo.find(appId);
    if(it == mapAppId_AppInfo.end())
        return false;

    appInfo = it->second;
    return true;
}

bool CTxMemPool::getAppIdByAppName(const std::string& strAppName, CName_Id_IndexValue& value)
{
    LOCK(cs);
    mapAppName_AppId_Index::iterator it = mapAppName_AppId.find(ToLower(strAppName));
    if(it == mapAppName_AppId.end())
        return false;

    value = it->second;
    return true;
}

bool CTxMemPool::getAppList(std::vector<uint256>& vAppId)
{
    LOCK(cs);
    for(mapAppId_AppInfo_Index::iterator it = mapAppId_AppInfo.begin(); it != mapAppId_AppInfo.end(); it++)
        vAppId.push_back(it->first);
    return vAppId.size();
}

bool CTxMemPool::removeAppInfoIndex(const uint256& txhash)
{
    LOCK(cs);
    mapAppId_AppInfo_IndexInserted::iterator it = mapAppId_AppInfo_Inserted.find(txhash);
    if(it != mapAppId_AppInfo_Inserted.end())
    {
        std::vector<uint256> keys = (*it).second;
        for(std::vector<uint256>::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            if(mapAppId_AppInfo.find(*mit) != mapAppId_AppInfo.end())
                mapAppId_AppInfo.erase(*mit);
        }
        mapAppId_AppInfo_Inserted.erase(it);
    }

    mapAppName_AppId_IndexInserted::iterator it2 = mapAppName_AppId_Inserted.find(txhash);
    if(it2 != mapAppName_AppId_Inserted.end())
    {
        std::vector<std::string> keys = (*it2).second;
        for(std::vector<std::string>::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            if(mapAppName_AppId.find(*mit) != mapAppName_AppId.end())
                mapAppName_AppId.erase(*mit);
        }
        mapAppName_AppId_Inserted.erase(it2);
    }

    return true;
}

bool CAppTx_IndexKeyCompare::operator()(const CAppTx_IndexKey& a, const CAppTx_IndexKey& b) const
{
    if(a.appId == b.appId)
    {
        if(a.strAddress == b.strAddress)
        {
            if(a.nTxClass == b.nTxClass)
                return a.out < b.out;
            return a.nTxClass < b.nTxClass;
        }
        return a.strAddress < b.strAddress;
    }
    return a.appId < b.appId;
}

void CTxMemPool::add_AppTx_Index(const CTxMemPoolEntry& entry, const CCoinsViewCache& view)
{
    LOCK(cs);
    const CTransaction& tx = entry.GetTx();
    std::vector<CAppTx_IndexKey> inserted;

    uint256 txhash = tx.GetHash();
    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CAppHeader header;
        std::vector<unsigned char> vData;
        if(ParseReserve(txout.vReserve, header, vData))
        {
            CTxDestination dest;
            if(!ExtractDestination(txout.scriptPubKey, dest))
                continue;

            uint8_t nTxClass = 0;
            if(header.nAppCmd == REGISTER_APP_CMD)
                nTxClass = REGISTER_TXOUT;
            else if(header.nAppCmd == ADD_AUTH_CMD)
                nTxClass = ADD_AUTH_TXOUT;
            else if(header.nAppCmd == DELETE_AUTH_CMD)
                nTxClass = DELETE_AUTH_TXOUT;
            else if(header.nAppCmd == CREATE_EXTEND_TX_CMD)
                nTxClass = CREATE_EXTENDDATA_TXOUT;
            else
                continue;

            CAppTx_IndexKey key(header.appId, CBitcoinAddress(dest).ToString(), nTxClass, COutPoint(txhash, i));
            mapAppTx.insert(make_pair(key, -1));
            inserted.push_back(key);
        }
    }

    mapAppTx_Inserted.insert(make_pair(txhash, inserted));
}

bool CTxMemPool::get_AppTx_Index(const uint256& appId, std::vector<COutPoint>& vOut)
{
    LOCK(cs);
    for(mapAppTx_Index::const_iterator it = mapAppTx.begin(); it != mapAppTx.end(); it++)
    {
        if(it->first.appId == appId)
            vOut.push_back(it->first.out);
    }
    return vOut.size();
}

bool CTxMemPool::get_AppTx_Index(const uint256& appId, const std::string& strAddress, std::vector<COutPoint>& vOut)
{
    LOCK(cs);
    for(mapAppTx_Index::const_iterator it = mapAppTx.begin(); it != mapAppTx.end(); it++)
    {
        if(it->first.appId == appId && it->first.strAddress == strAddress)
            vOut.push_back(it->first.out);
    }
    return vOut.size();
}

bool CTxMemPool::getAppList(const std::string& strAddress, std::vector<uint256>& vAppId)
{
    LOCK(cs);
    for(mapAppTx_Index::const_iterator it = mapAppTx.begin(); it != mapAppTx.end(); it++)
    {
        if(it->first.strAddress == strAddress)
            vAppId.push_back(it->first.appId);
    }
    return vAppId.size();
}

bool CTxMemPool::remove_AppTx_Index(const uint256& txhash)
{
    LOCK(cs);
    mapAppTx_IndexInserted::iterator it = mapAppTx_Inserted.find(txhash);
    if(it != mapAppTx_Inserted.end())
    {
        std::vector<CAppTx_IndexKey> keys = (*it).second;
        for(std::vector<CAppTx_IndexKey>::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            if(mapAppTx.find(*mit) != mapAppTx.end())
                mapAppTx.erase(*mit);
        }
        mapAppTx_Inserted.erase(it);
    }
    return true;
}

bool CAuth_IndexKeyCompare::operator()(const CAuth_IndexKey& a, const CAuth_IndexKey& b) const
{
    if(a.appId == b.appId)
    {
        if(a.strAddress == b.strAddress)
            return a.nAuth < b.nAuth;
        return a.strAddress < b.strAddress;
    }
    return a.appId < b.appId;
}

void CTxMemPool::add_Auth_Index(const CTxMemPoolEntry& entry, const CCoinsViewCache& view)
{
    LOCK(cs);
    const CTransaction& tx = entry.GetTx();
    std::vector<CAuth_IndexKey> inserted;

    uint256 txhash = tx.GetHash();
    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CAppHeader header;
        std::vector<unsigned char> vData;
        if(ParseReserve(txout.vReserve, header, vData))
        {
            if(header.nAppCmd == ADD_AUTH_CMD || header.nAppCmd == DELETE_AUTH_CMD)
            {
                CAuthData authData;
                if(ParseAuthData(vData, authData))
                {
                    CAuth_IndexKey key(header.appId, authData.strUserAddress, authData.nAuth);
                    mapAuth.insert(make_pair(key, -1));
                    inserted.push_back(key);
                }
            }
        }
    }

    mapAuth_Inserted.insert(make_pair(txhash, inserted));
}

bool CTxMemPool::get_Auth_Index(const uint256& appId, const std::string& strAddress, std::vector<uint32_t>& vAuth)
{
    LOCK(cs);
    for(mapAuth_Index::const_iterator it = mapAuth.begin(); it != mapAuth.end(); it++)
    {
        if(it->first.appId == appId && it->first.strAddress == strAddress)
            vAuth.push_back(it->first.nAuth);
    }

    return vAuth.size();
}

bool CTxMemPool::remove_Auth_Index(const uint256& txhash)
{
    LOCK(cs);
    mapAuth_IndexInserted::iterator it = mapAuth_Inserted.find(txhash);
    if(it != mapAuth_Inserted.end())
    {
        std::vector<CAuth_IndexKey> keys = (*it).second;
        for(std::vector<CAuth_IndexKey>::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            if(mapAuth.find(*mit) != mapAuth.end())
                mapAuth.erase(*mit);
        }
        mapAuth_Inserted.erase(it);
    }
    return true;
}

void CTxMemPool::addAssetInfoIndex(const CTxMemPoolEntry& entry, const CCoinsViewCache& view)
{
    LOCK(cs);
    const CTransaction& tx = entry.GetTx();
    std::vector<uint256> assetId_inserted;
    std::vector<std::string> shortName_inserted;
    std::vector<std::string> assetName_inserted;

    uint256 txhash = tx.GetHash();
    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CAppHeader header;
        std::vector<unsigned char> vData;
        if(ParseReserve(txout.vReserve, header, vData))
        {
            CTxDestination dest;
            if(!ExtractDestination(txout.scriptPubKey, dest))
                continue;

            if(header.nAppCmd == ISSUE_ASSET_CMD)
            {
                CAssetData assetData;
                if(ParseIssueData(vData, assetData))
                {
                    uint256 assetId = assetData.GetHash();

                    mapAssetId_AssetInfo.insert(make_pair(assetId, CAssetId_AssetInfo_IndexValue(CBitcoinAddress(dest).ToString(), assetData, -1)));
                    assetId_inserted.push_back(assetId);

                    mapShortName_AssetId.insert(make_pair(ToLower(assetData.strShortName), assetId));
                    shortName_inserted.push_back(ToLower(assetData.strShortName));

                    mapAssetName_AssetId.insert(make_pair(ToLower(assetData.strAssetName), assetId));
                    assetName_inserted.push_back(ToLower(assetData.strAssetName));
                }
            }
        }
    }

    mapAssetId_AssetInfo_Inserted.insert(make_pair(txhash, assetId_inserted));
    mapShortName_AssetId_Inserted.insert(make_pair(txhash, shortName_inserted));
    mapAssetName_AssetId_Inserted.insert(make_pair(txhash, assetName_inserted));
}

bool CTxMemPool::getAssetInfoByAssetId(const uint256& assetId, CAssetId_AssetInfo_IndexValue& assetInfo)
{
    LOCK(cs);
    mapAssetId_AssetInfo_Index::iterator it = mapAssetId_AssetInfo.find(assetId);
    if(it == mapAssetId_AssetInfo.end())
        return false;

    assetInfo = it->second;
    return true;
}

bool CTxMemPool::getAssetList(std::vector<uint256>& vAssetId)
{
    LOCK(cs);
    for (mapAssetId_AssetInfo_Index::iterator it = mapAssetId_AssetInfo.begin(); it != mapAssetId_AssetInfo.end(); it++)
        vAssetId.push_back(it->first);
    return vAssetId.size();
}

bool CTxMemPool::getAssetIdByShortName(const std::string& strShortName, CName_Id_IndexValue& value)
{
    LOCK(cs);
    mapShortName_AssetId_Index::iterator it = mapShortName_AssetId.find(ToLower(strShortName));
    if(it == mapShortName_AssetId.end())
        return false;

    value = it->second;
    return true;
}

bool CTxMemPool::getAssetIdByAssetName(const std::string& strAssetName, CName_Id_IndexValue& value)
{
    LOCK(cs);
    mapAssetName_AssetId_Index::iterator it = mapAssetName_AssetId.find(ToLower(strAssetName));
    if(it == mapAssetName_AssetId.end())
        return false;

    value = it->second;
    return true;
}

bool CTxMemPool::removeAssetInfoIndex(const uint256& txhash)
{
    LOCK(cs);
    mapAssetId_AssetInfo_IndexInserted::iterator it = mapAssetId_AssetInfo_Inserted.find(txhash);
    if(it != mapAssetId_AssetInfo_Inserted.end())
    {
        std::vector<uint256> keys = (*it).second;
        for(std::vector<uint256>::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            if(mapAssetId_AssetInfo.find(*mit) != mapAssetId_AssetInfo.end())
                mapAssetId_AssetInfo.erase(*mit);
        }
        mapAssetId_AssetInfo_Inserted.erase(it);
    }

    mapShortName_AssetId_IndexInserted::iterator it2 = mapShortName_AssetId_Inserted.find(txhash);
    if(it2 != mapShortName_AssetId_Inserted.end())
    {
        std::vector<std::string> keys = (*it2).second;
        for(std::vector<std::string>::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            if(mapShortName_AssetId.find(*mit) != mapShortName_AssetId.end())
                mapShortName_AssetId.erase(*mit);
        }
        mapShortName_AssetId_Inserted.erase(it2);
    }

    mapAssetName_AssetId_IndexInserted::iterator it3 = mapAssetName_AssetId_Inserted.find(txhash);
    if(it3 != mapAssetName_AssetId_Inserted.end())
    {
        std::vector<std::string> keys = (*it3).second;
        for(std::vector<std::string>::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            if(mapAssetName_AssetId.find(*mit) != mapAssetName_AssetId.end())
                mapAssetName_AssetId.erase(*mit);
        }
        mapAssetName_AssetId_Inserted.erase(it3);
    }

    return true;
}

bool CAssetTx_IndexKeyCompare::operator()(const CAssetTx_IndexKey& a, const CAssetTx_IndexKey& b) const
{
    if(a.assetId == b.assetId)
    {
        if(a.strAddress == b.strAddress)
        {
            if(a.nTxClass == b.nTxClass)
                return a.out < b.out;
            return a.nTxClass < b.nTxClass;
        }
        return a.strAddress < b.strAddress;
    }
    return a.assetId < b.assetId;
}

void CTxMemPool::add_AssetTx_Index(const CTxMemPoolEntry& entry, const CCoinsViewCache& view)
{
    LOCK(cs);
    const CTransaction& tx = entry.GetTx();
    std::vector<CAssetTx_IndexKey> inserted;

    uint256 txhash = tx.GetHash();
    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CAppHeader header;
        std::vector<unsigned char> vData;
        if(ParseReserve(txout.vReserve, header, vData))
        {
            CTxDestination dest;
            if(!ExtractDestination(txout.scriptPubKey, dest))
                continue;

            if(header.nAppCmd == ISSUE_ASSET_CMD)
            {
                CAssetData assetData;
                if(ParseIssueData(vData, assetData))
                {
                    CAssetTx_IndexKey key(assetData.GetHash(), CBitcoinAddress(dest).ToString(), ISSUE_TXOUT, COutPoint(txhash, i));
                    mapAssetTx.insert(make_pair(key, -1));
                    inserted.push_back(key);
                }
            }
            else if(header.nAppCmd == ADD_ASSET_CMD || header.nAppCmd == TRANSFER_ASSET_CMD || header.nAppCmd == DESTORY_ASSET_CMD)
            {
                CCommonData commonData;
                if(ParseCommonData(vData, commonData))
                {
                    if (header.nAppCmd == ADD_ASSET_CMD)
                    {
                        CAssetTx_IndexKey key(commonData.assetId, CBitcoinAddress(dest).ToString(), ADD_ISSUE_TXOUT, COutPoint(txhash, i));
                        mapAssetTx.insert(make_pair(key, -1));
                        inserted.push_back(key);
                    }
                    else if (header.nAppCmd == DESTORY_ASSET_CMD)
                    {
                        CAssetTx_IndexKey key(commonData.assetId, CBitcoinAddress(dest).ToString(), DESTORY_TXOUT, COutPoint(txhash, i));
                        mapAssetTx.insert(make_pair(key, -1));
                        inserted.push_back(key);
                    }
                    else if(header.nAppCmd == TRANSFER_ASSET_CMD)
                    {
                        if(txout.nUnlockedHeight > 0)
                        {
                            CAssetTx_IndexKey key(commonData.assetId, CBitcoinAddress(dest).ToString(), LOCKED_TXOUT, COutPoint(txhash, i));
                            mapAssetTx.insert(make_pair(key, -1));
                            inserted.push_back(key);
                        }
                        else
                        {
                            CAssetTx_IndexKey key(commonData.assetId, CBitcoinAddress(dest).ToString(), TRANSFER_TXOUT, COutPoint(txhash, i));
                            mapAssetTx.insert(make_pair(key, -1));
                            inserted.push_back(key);
                        }
                    }
                }
            }
            else if(header.nAppCmd == PUT_CANDY_CMD)
            {
                CPutCandyData candyData;
                if(ParsePutCandyData(vData, candyData))
                {
                    CAssetTx_IndexKey key(candyData.assetId, CBitcoinAddress(dest).ToString(), PUT_CANDY_TXOUT, COutPoint(txhash, i));
                    mapAssetTx.insert(make_pair(key, -1));
                    inserted.push_back(key);
                }
            }
            else if(header.nAppCmd == GET_CANDY_CMD)
            {
                CGetCandyData candyData;
                if(ParseGetCandyData(vData, candyData))
                {
                    CAssetTx_IndexKey key(candyData.assetId, CBitcoinAddress(dest).ToString(), GET_CANDY_TXOUT, COutPoint(txhash, i));
                    mapAssetTx.insert(make_pair(key, -1));
                    inserted.push_back(key);
                }
            }
        }
    }

    mapAssetTx_Inserted.insert(make_pair(txhash, inserted));
}

bool CTxMemPool::get_AssetTx_Index(const uint256& assetId, const uint8_t& nTxClass, std::vector<COutPoint>& vOut)
{
    LOCK(cs);
    multimap<int, COutPoint> tmpMap;
    for(mapAssetTx_Index::const_iterator it = mapAssetTx.begin(); it != mapAssetTx.end(); it++)
    {
        if(it->first.assetId != assetId)
            continue;
        if(nTxClass == ALL_TXOUT)
        {
            tmpMap.insert(make_pair(it->second, it->first.out));
        }
        else if(nTxClass == UNLOCKED_TXOUT)
        {
            if(it->first.nTxClass == LOCKED_TXOUT)
                continue;
            tmpMap.insert(make_pair(it->second, it->first.out));
        }
        else if(it->first.nTxClass == nTxClass)
        {
            tmpMap.insert(make_pair(it->second, it->first.out));
        }
    }

    for(multimap<int, COutPoint>::iterator iter = tmpMap.begin(); iter != tmpMap.end(); ++iter)
        vOut.push_back(iter->second);

    multimap<int,COutPoint>().swap(tmpMap);

    return vOut.size();
}

bool CTxMemPool::get_AssetTx_Index(const uint256& assetId, const std::string& strAddress, const uint8_t& nTxClass, std::vector<COutPoint>& vOut)
{
    LOCK(cs);
    multimap<int, COutPoint> tmpMap;
    for(mapAssetTx_Index::const_iterator it = mapAssetTx.begin(); it != mapAssetTx.end(); it++)
    {
        if(it->first.assetId != assetId || it->first.strAddress != strAddress)
            continue;

        if(nTxClass == ALL_TXOUT)
        {
            tmpMap.insert(make_pair(it->second, it->first.out));
        }
        else if(nTxClass == UNLOCKED_TXOUT)
        {
            if(it->first.nTxClass == LOCKED_TXOUT)
                continue;
            tmpMap.insert(make_pair(it->second, it->first.out));
        }
        else if(it->first.nTxClass == nTxClass)
        {
            tmpMap.insert(make_pair(it->second, it->first.out));
        }
    }

    for(multimap<int, COutPoint>::iterator iter = tmpMap.begin(); iter != tmpMap.end(); ++iter){
        vOut.push_back(iter->second);
    }

    multimap<int, COutPoint>().swap(tmpMap);

    return vOut.size();
}

bool CTxMemPool::getAssetList(const std::string& strAddress, std::vector<uint256>& vAssetId)
{
    LOCK(cs);
    for(mapAssetTx_Index::const_iterator it = mapAssetTx.begin(); it != mapAssetTx.end(); it++)
    {
        if (it->first.strAddress == strAddress)
            vAssetId.push_back(it->first.assetId);
    }
    return vAssetId.size();
}

bool CTxMemPool::remove_AssetTx_Index(const uint256& txhash)
{
    LOCK(cs);
    mapAssetTx_IndexInserted::iterator it = mapAssetTx_Inserted.find(txhash);
    if(it != mapAssetTx_Inserted.end())
    {
        std::vector<CAssetTx_IndexKey> keys = (*it).second;
        for(std::vector<CAssetTx_IndexKey>::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            if(mapAssetTx.find(*mit) != mapAssetTx.end())
                mapAssetTx.erase(*mit);
        }
        mapAssetTx_Inserted.erase(it);
    }
    return true;
}

int CTxMemPool::get_PutCandy_count(const uint256 &assetId)
{
    LOCK(cs);
    int nCount = 0;
    for(mapAssetTx_Index::const_iterator it = mapAssetTx.begin(); it != mapAssetTx.end(); it++)
    {
        if(it->first.assetId != assetId || it->first.strAddress != g_strPutCandyAddress || it->first.nTxClass != PUT_CANDY_TXOUT)
            continue;
        nCount++;
    }
    return nCount;
}

bool CGetCandy_IndexKeyCompare::operator()(const CGetCandy_IndexKey& a, const CGetCandy_IndexKey& b) const
{
    if(a.assetId == b.assetId)
    {
        if(a.out == b.out)
        {
            return a.strAddress < b.strAddress;
        }
        return a.out < b.out;
    }
    return a.assetId < b.assetId;
}

bool CGetCandyCount_IndexKeyCompare::operator ()(const CGetCandyCount_IndexKey& a, const CGetCandyCount_IndexKey& b) const
{
    if(a.assetId == b.assetId)
        return a.out < b.out;
    return a.assetId < b.assetId;
}

void CTxMemPool::add_GetCandy_Index(const CTxMemPoolEntry& entry, const CCoinsViewCache& view)
{
    LOCK(cs);
    const CTransaction& tx = entry.GetTx();
    std::vector<CGetCandy_IndexKey> getCandy_inserted;

    uint256 txhash = tx.GetHash();
    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CAppHeader header;
        std::vector<unsigned char> vData;
        if(ParseReserve(txout.vReserve, header, vData))
        {
            CTxDestination dest;
            if(!ExtractDestination(txout.scriptPubKey, dest))
                continue;
            if(header.nAppCmd == GET_CANDY_CMD)
            {
                CGetCandyData candyData;
                if(ParseGetCandyData(vData, candyData))
                {
                    for(unsigned int m = 0; m < tx.vin.size(); m++)
                    {
                        const CTxIn& txin = tx.vin[m];
                        const CTxOut& in_txout = view.GetOutputFor(txin);
                        CTxDestination in_dest;
                        if(!ExtractDestination(in_txout.scriptPubKey, in_dest))
                            continue;
                        if(CBitcoinAddress(in_dest).ToString() == g_strPutCandyAddress)
                        {
                            CGetCandy_IndexKey key(candyData.assetId, txin.prevout, CBitcoinAddress(dest).ToString());
                            mapGetCandy.insert(make_pair(key, CGetCandy_IndexValue(candyData.nAmount)));
                            getCandy_inserted.push_back(key);
                        }
                    }
                }
            }
        }
    }

    mapGetCandy_Inserted.insert(make_pair(txhash, getCandy_inserted));
}

bool CTxMemPool::get_GetCandy_Index(const uint256& assetId, const COutPoint& out, const std::string& strAddress, CAmount& nAmount)
{
    LOCK(cs);
    for(mapGetCandy_Index::const_iterator it = mapGetCandy.begin(); it != mapGetCandy.end(); it++)
    {
        const CGetCandy_IndexKey& key = it->first;
        if(key.assetId == assetId && key.out == out && key.strAddress == strAddress)
        {
            nAmount = it->second.nAmount;
            return true;
        }
    }

    return false;
}

bool CTxMemPool::remove_GetCandy_Index(const uint256& txhash)
{
    LOCK(cs);
    mapGetCandy_IndexInserted::iterator it = mapGetCandy_Inserted.find(txhash);

    if(it != mapGetCandy_Inserted.end())
    {
        std::vector<CGetCandy_IndexKey> keys = (*it).second;
        for(std::vector<CGetCandy_IndexKey>::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            if(mapGetCandy.find(*mit) != mapGetCandy.end())
                mapGetCandy.erase(*mit);
        }
        mapGetCandy_Inserted.erase(it);
    }

    return true;
}

void CTxMemPool::add_GetCandyCount_Index(const CTxMemPoolEntry& entry, const CCoinsViewCache& view)
{
    LOCK(cs);
    const CTransaction& tx = entry.GetTx();
    std::vector<std::pair<CGetCandyCount_IndexKey,CGetCandyCount_IndexValue> > getCandyCount_inserted;

    uint256 txhash = tx.GetHash();
    for(unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CAppHeader header;
        std::vector<unsigned char> vData;
        if(ParseReserve(txout.vReserve, header, vData))
        {
            CTxDestination dest;
            if(!ExtractDestination(txout.scriptPubKey, dest))
                continue;
            if(header.nAppCmd == GET_CANDY_CMD)
            {
                CGetCandyData candyData;
                if(ParseGetCandyData(vData, candyData))
                {
                    for(unsigned int m = 0; m < tx.vin.size(); m++)
                    {
                        const CTxIn& txin = tx.vin[m];
                        const CTxOut& in_txout = view.GetOutputFor(txin);
                        CTxDestination in_dest;
                        if(!ExtractDestination(in_txout.scriptPubKey, in_dest))
                            continue;
                        if(CBitcoinAddress(in_dest).ToString() == g_strPutCandyAddress)
                        {
                            CGetCandyCount_IndexKey key(candyData.assetId,txin.prevout);
                            CGetCandyCount_IndexValue& value = mapGetCandyCount[key];
                            value.nGetCandyCount += candyData.nAmount;
                            getCandyCount_inserted.push_back(std::make_pair(key,CGetCandyCount_IndexValue(candyData.nAmount)));
                            LogPrint("asset","check-getcandy:mempool_add_get_candy:%s,%s,currAmount:%d,totalAmount:%d\n",key.assetId.ToString(),key.out.ToString()
                                      ,candyData.nAmount,value.nGetCandyCount);
                        }
                    }
                }
            }
        }
    }

    mapGetCandyCount_Inserted.insert(make_pair(txhash, getCandyCount_inserted));
}

bool CTxMemPool::get_GetCandyCount_Index(const uint256 &assetId, const COutPoint &out, CGetCandyCount_IndexValue &value)
{
    LOCK(cs);
    for(mapGetCandyCount_Index::const_iterator it = mapGetCandyCount.begin(); it != mapGetCandyCount.end(); it++)
    {
        const CGetCandyCount_IndexKey& key = it->first;
        if(key.assetId == assetId && key.out == out)
        {
            const CGetCandyCount_IndexValue& poolValue = it->second;
            value.nGetCandyCount = poolValue.nGetCandyCount;
            return true;
        }
    }

    return false;
}

bool CTxMemPool::remove_GetCandyCount_Index(const uint256& txhash)
{
    LOCK(cs);
    mapGetCandyCount_IndexInserted::iterator it = mapGetCandyCount_Inserted.find(txhash);

    if(it != mapGetCandyCount_Inserted.end())
    {
        std::vector<std::pair<CGetCandyCount_IndexKey,CGetCandyCount_IndexValue> > keys = (*it).second;
        for(std::vector<std::pair<CGetCandyCount_IndexKey,CGetCandyCount_IndexValue> >::iterator mit = keys.begin(); mit != keys.end(); mit++)
        {
            const CGetCandyCount_IndexKey& key = mit->first;
            if(mapGetCandyCount.find(key) != mapGetCandyCount.end())
            {
                CGetCandyCount_IndexValue& value = mapGetCandyCount[key];
                value.nGetCandyCount -= mit->second.nGetCandyCount;
                LogPrint("asset","check-getcandy:mempool_remove_candy:%s,%s,currAmount:%d,totalAmount:%d\n",key.assetId.ToString(),key.out.ToString()
                          ,mit->second.nGetCandyCount,value.nGetCandyCount);
                if(value.nGetCandyCount==0)
                    mapGetCandyCount.erase(key);
            }
        }
        mapGetCandyCount_Inserted.erase(it);
    }

    return true;
}

void CTxMemPool::removeUnchecked(txiter it)
{
    const uint256 hash = it->GetTx().GetHash();
    BOOST_FOREACH(const CTxIn& txin, it->GetTx().vin)
        mapNextTx.erase(txin.prevout);

    totalTxSize -= it->GetTxSize();
    cachedInnerUsage -= it->DynamicMemoryUsage();
    cachedInnerUsage -= memusage::DynamicUsage(mapLinks[it].parents) + memusage::DynamicUsage(mapLinks[it].children);
    mapLinks.erase(it);
    mapTx.erase(it);
    nTransactionsUpdated++;
    minerPolicyEstimator->removeTx(hash);
    removeAddressIndex(hash);
    removeSpentIndex(hash);
    removeAppInfoIndex(hash);
    remove_AppTx_Index(hash);
    remove_Auth_Index(hash);
    removeAssetInfoIndex(hash);
    remove_AssetTx_Index(hash);
    remove_GetCandy_Index(hash);
    remove_GetCandyCount_Index(hash);
}

////////////////////////////////////////////////////////////////////////////////////////
// Calculates descendants of entry that are not already in setDescendants, and adds to
// setDescendants. Assumes entryit is already a tx in the mempool and setMemPoolChildren
// is correct for tx and all descendants.
// Also assumes that if an entry is in setDescendants already, then all
// in-mempool descendants of it are already in setDescendants as well, so that we
// can save time by not iterating over those entries.
void CTxMemPool::CalculateDescendants(txiter entryit, setEntries &setDescendants)
{
    setEntries stage;
    if (setDescendants.count(entryit) == 0) {
        stage.insert(entryit);
    }
    // Traverse down the children of entry, only adding children that are not
    // accounted for in setDescendants already (because those children have either
    // already been walked, or will be walked in this iteration).
    while (!stage.empty()) {
        txiter it = *stage.begin();
        setDescendants.insert(it);
        stage.erase(it);

        const setEntries &setChildren = GetMemPoolChildren(it);
        BOOST_FOREACH(const txiter &childiter, setChildren) {
            if (!setDescendants.count(childiter)) {
                stage.insert(childiter);
            }
        }
    }
}

void CTxMemPool::remove(const CTransaction &origTx, std::list<CTransaction>& removed, bool fRecursive)
{
    // Remove transaction from memory pool
    {
        LOCK(cs);
        setEntries txToRemove;
        txiter origit = mapTx.find(origTx.GetHash());
        if (origit != mapTx.end()) {
            txToRemove.insert(origit);
        } else if (fRecursive) {
            // If recursively removing but origTx isn't in the mempool
            // be sure to remove any children that are in the pool. This can
            // happen during chain re-orgs if origTx isn't re-accepted into
            // the mempool for any reason.
            for (unsigned int i = 0; i < origTx.vout.size(); i++) {
                std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(origTx.GetHash(), i));
                if (it == mapNextTx.end())
                    continue;
                txiter nextit = mapTx.find(it->second.ptx->GetHash());
                assert(nextit != mapTx.end());
                txToRemove.insert(nextit);
            }
        }
        setEntries setAllRemoves;
        if (fRecursive) {
            BOOST_FOREACH(txiter it, txToRemove) {
                CalculateDescendants(it, setAllRemoves);
            }
        } else {
            setAllRemoves.swap(txToRemove);
        }
        BOOST_FOREACH(txiter it, setAllRemoves) {
            removed.push_back(it->GetTx());
        }
        RemoveStaged(setAllRemoves);
    }
}

void CTxMemPool::removeForReorg(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight, int flags)
{
    // Remove transactions spending a coinbase which are now immature and no-longer-final transactions
    LOCK(cs);
    list<CTransaction> transactionsToRemove;
    for (indexed_transaction_set::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        const CTransaction& tx = it->GetTx();
        LockPoints lp = it->GetLockPoints();
        bool validLP =  TestLockPointValidity(&lp);
        if (!CheckFinalTx(tx, flags) || !CheckSequenceLocks(tx, flags, &lp, validLP)) {
            // Note if CheckSequenceLocks fails the LockPoints may still be invalid
            // So it's critical that we remove the tx and not depend on the LockPoints.
            transactionsToRemove.push_back(tx);
        } else if (it->GetSpendsCoinbase()) {
            BOOST_FOREACH(const CTxIn& txin, tx.vin) {
                indexed_transaction_set::const_iterator it2 = mapTx.find(txin.prevout.hash);
                if (it2 != mapTx.end())
                    continue;
                const CCoins *coins = pcoins->AccessCoins(txin.prevout.hash);
        if (nCheckFrequency != 0) assert(coins);
                if (!coins || (coins->IsCoinBase() && !coins->IsMaturity((signed long)nMemPoolHeight))) {
                    transactionsToRemove.push_back(tx);
                    break;
                }
            }
        }
        if (!validLP) {
            mapTx.modify(it, update_lock_points(lp));
        }
    }
    BOOST_FOREACH(const CTransaction& tx, transactionsToRemove) {
        list<CTransaction> removed;
        remove(tx, removed, true);
    }
}

void CTxMemPool::removeConflicts(const CTransaction &tx, std::list<CTransaction>& removed)
{
    // Remove transactions which depend on inputs of tx, recursively
    list<CTransaction> result;
    LOCK(cs);
    BOOST_FOREACH(const CTxIn &txin, tx.vin) {
        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const CTransaction &txConflict = *it->second.ptx;
            if (txConflict != tx)
            {
                remove(txConflict, removed, true);
                ClearPrioritisation(txConflict.GetHash());
            }
        }
    }
}

/**
 * Called when a block is connected. Removes from mempool and updates the miner fee estimator.
 */
void CTxMemPool::removeForBlock(const std::vector<CTransaction>& vtx, unsigned int nBlockHeight,
                                std::list<CTransaction>& conflicts, bool fCurrentEstimate)
{
    LOCK(cs);
    std::vector<CTxMemPoolEntry> entries;
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        uint256 hash = tx.GetHash();

        indexed_transaction_set::iterator i = mapTx.find(hash);
        if (i != mapTx.end())
            entries.push_back(*i);
    }
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        std::list<CTransaction> dummy;
        remove(tx, dummy, false);
        removeConflicts(tx, conflicts);
        ClearPrioritisation(tx.GetHash());
    }
    // After the txs in the new block have been removed from the mempool, update policy estimates
    minerPolicyEstimator->processBlock(nBlockHeight, entries, fCurrentEstimate);
    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = true;
}

void CTxMemPool::_clear()
{
    mapLinks.clear();
    mapTx.clear();
    mapNextTx.clear();
    totalTxSize = 0;
    cachedInnerUsage = 0;
    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = false;
    rollingMinimumFeeRate = 0;
    ++nTransactionsUpdated;
}

void CTxMemPool::clear()
{
    LOCK(cs);
    _clear();
}

void CTxMemPool::check(const CCoinsViewCache *pcoins) const
{
    if (nCheckFrequency == 0)
        return;

    if (insecure_rand() >= nCheckFrequency)
        return;

    LogPrint("mempool", "Checking mempool with %u transactions and %u inputs\n", (unsigned int)mapTx.size(), (unsigned int)mapNextTx.size());

    uint64_t checkTotal = 0;
    uint64_t innerUsage = 0;

    CCoinsViewCache mempoolDuplicate(const_cast<CCoinsViewCache*>(pcoins));

    LOCK(cs);
    list<const CTxMemPoolEntry*> waitingOnDependants;
    for (indexed_transaction_set::const_iterator it = mapTx.begin(); it != mapTx.end(); it++) {
        unsigned int i = 0;
        checkTotal += it->GetTxSize();
        innerUsage += it->DynamicMemoryUsage();
        const CTransaction& tx = it->GetTx();
        txlinksMap::const_iterator linksiter = mapLinks.find(it);
        assert(linksiter != mapLinks.end());
        const TxLinks &links = linksiter->second;
        innerUsage += memusage::DynamicUsage(links.parents) + memusage::DynamicUsage(links.children);
        bool fDependsWait = false;
        setEntries setParentCheck;
        BOOST_FOREACH(const CTxIn &txin, tx.vin) {
            // Check that every mempool transaction's inputs refer to available coins, or other mempool tx's.
            indexed_transaction_set::const_iterator it2 = mapTx.find(txin.prevout.hash);
            if (it2 != mapTx.end()) {
                const CTransaction& tx2 = it2->GetTx();
                assert(tx2.vout.size() > txin.prevout.n && !tx2.vout[txin.prevout.n].IsNull());
                fDependsWait = true;
                setParentCheck.insert(it2);
            } else {
                const CCoins* coins = pcoins->AccessCoins(txin.prevout.hash);
                assert(coins && coins->IsAvailable(txin.prevout.n));
            }
            // Check whether its inputs are marked in mapNextTx.
            std::map<COutPoint, CInPoint>::const_iterator it3 = mapNextTx.find(txin.prevout);
            assert(it3 != mapNextTx.end());
            assert(it3->second.ptx == &tx);
            assert(it3->second.n == i);
            i++;
        }
        assert(setParentCheck == GetMemPoolParents(it));
        // Check children against mapNextTx
        CTxMemPool::setEntries setChildrenCheck;
        std::map<COutPoint, CInPoint>::const_iterator iter = mapNextTx.lower_bound(COutPoint(it->GetTx().GetHash(), 0));
        int64_t childSizes = 0;
        CAmount childModFee = 0;
        for (; iter != mapNextTx.end() && iter->first.hash == it->GetTx().GetHash(); ++iter) {
            txiter childit = mapTx.find(iter->second.ptx->GetHash());
            assert(childit != mapTx.end()); // mapNextTx points to in-mempool transactions
            if (setChildrenCheck.insert(childit).second) {
                childSizes += childit->GetTxSize();
                childModFee += childit->GetModifiedFee();
            }
        }
        assert(setChildrenCheck == GetMemPoolChildren(it));
        // Also check to make sure size is greater than sum with immediate children.
        // just a sanity check, not definitive that this calc is correct...
        if (!it->IsDirty()) {
            assert(it->GetSizeWithDescendants() >= childSizes + it->GetTxSize());
        } else {
            assert(it->GetSizeWithDescendants() == it->GetTxSize());
            assert(it->GetModFeesWithDescendants() == it->GetModifiedFee());
        }

        if (fDependsWait)
            waitingOnDependants.push_back(&(*it));
        else {
            CValidationState state;
            assert(CheckInputs(tx, state, mempoolDuplicate, false, 0, false, NULL));
            UpdateCoins(tx, state, mempoolDuplicate, 1000000);
        }
    }
    unsigned int stepsSinceLastRemove = 0;
    while (!waitingOnDependants.empty()) {
        const CTxMemPoolEntry* entry = waitingOnDependants.front();
        waitingOnDependants.pop_front();
        CValidationState state;
        if (!mempoolDuplicate.HaveInputs(entry->GetTx())) {
            waitingOnDependants.push_back(entry);
            stepsSinceLastRemove++;
            assert(stepsSinceLastRemove < waitingOnDependants.size());
        } else {
            assert(CheckInputs(entry->GetTx(), state, mempoolDuplicate, false, 0, false, NULL));
            UpdateCoins(entry->GetTx(), state, mempoolDuplicate, 1000000);
            stepsSinceLastRemove = 0;
        }
    }
    for (std::map<COutPoint, CInPoint>::const_iterator it = mapNextTx.begin(); it != mapNextTx.end(); it++) {
        uint256 hash = it->second.ptx->GetHash();
        indexed_transaction_set::const_iterator it2 = mapTx.find(hash);
        const CTransaction& tx = it2->GetTx();
        assert(it2 != mapTx.end());
        assert(&tx == it->second.ptx);
        assert(tx.vin.size() > it->second.n);
        assert(it->first == it->second.ptx->vin[it->second.n].prevout);
    }

    assert(totalTxSize == checkTotal);
    assert(innerUsage == cachedInnerUsage);
}

void CTxMemPool::queryHashes(vector<uint256>& vtxid)
{
    vtxid.clear();

    LOCK(cs);
    vtxid.reserve(mapTx.size());
    for (indexed_transaction_set::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
        vtxid.push_back(mi->GetTx().GetHash());
}

bool CTxMemPool::lookup(uint256 hash, CTransaction& result) const
{
    LOCK(cs);
    indexed_transaction_set::const_iterator i = mapTx.find(hash);
    if (i == mapTx.end()) return false;
    result = i->GetTx();
    return true;
}

CFeeRate CTxMemPool::estimateFee(int nBlocks) const
{
    LOCK(cs);
    return minerPolicyEstimator->estimateFee(nBlocks);
}
CFeeRate CTxMemPool::estimateSmartFee(int nBlocks, int *answerFoundAtBlocks) const
{
    LOCK(cs);
    return minerPolicyEstimator->estimateSmartFee(nBlocks, answerFoundAtBlocks, *this);
}
double CTxMemPool::estimatePriority(int nBlocks) const
{
    LOCK(cs);
    return minerPolicyEstimator->estimatePriority(nBlocks);
}
double CTxMemPool::estimateSmartPriority(int nBlocks, int *answerFoundAtBlocks) const
{
    LOCK(cs);
    return minerPolicyEstimator->estimateSmartPriority(nBlocks, answerFoundAtBlocks, *this);
}

bool
CTxMemPool::WriteFeeEstimates(CAutoFile& fileout) const
{
    try {
        LOCK(cs);
        fileout << 120000;
        fileout << CLIENT_VERSION; // version that wrote the file
        minerPolicyEstimator->Write(fileout);
    }
    catch (const std::exception&) {
        LogPrintf("CTxMemPool::WriteFeeEstimates(): unable to write policy estimator data (non-fatal)\n");
        return false;
    }
    return true;
}

bool
CTxMemPool::ReadFeeEstimates(CAutoFile& filein)
{
    try {
        int nVersionRequired, nVersionThatWrote;
        filein >> nVersionRequired >> nVersionThatWrote;
        if (nVersionRequired > CLIENT_VERSION)
            return error("CTxMemPool::ReadFeeEstimates(): up-version (%d) fee estimate file", nVersionRequired);

        LOCK(cs);
        minerPolicyEstimator->Read(filein);
    }
    catch (const std::exception&) {
        LogPrintf("CTxMemPool::ReadFeeEstimates(): unable to read policy estimator data (non-fatal)\n");
        return false;
    }
    return true;
}

void CTxMemPool::PrioritiseTransaction(const uint256 hash, const string strHash, double dPriorityDelta, const CAmount& nFeeDelta)
{
    {
        LOCK(cs);
        std::pair<double, CAmount> &deltas = mapDeltas[hash];
        deltas.first += dPriorityDelta;
        deltas.second += nFeeDelta;
        txiter it = mapTx.find(hash);
        if (it != mapTx.end()) {
            mapTx.modify(it, update_fee_delta(deltas.second));
            // Now update all ancestors' modified fees with descendants
            setEntries setAncestors;
            uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
            std::string dummy;
            CalculateMemPoolAncestors(*it, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);
            BOOST_FOREACH(txiter ancestorIt, setAncestors) {
                mapTx.modify(ancestorIt, update_descendant_state(0, nFeeDelta, 0));
            }
        }
    }
    LogPrintf("PrioritiseTransaction: %s priority += %f, fee += %d\n", strHash, dPriorityDelta, FormatMoney(nFeeDelta));
}

void CTxMemPool::ApplyDeltas(const uint256 hash, double &dPriorityDelta, CAmount &nFeeDelta) const
{
    LOCK(cs);
    std::map<uint256, std::pair<double, CAmount> >::const_iterator pos = mapDeltas.find(hash);
    if (pos == mapDeltas.end())
        return;
    const std::pair<double, CAmount> &deltas = pos->second;
    dPriorityDelta += deltas.first;
    nFeeDelta += deltas.second;
}

void CTxMemPool::ClearPrioritisation(const uint256 hash)
{
    LOCK(cs);
    mapDeltas.erase(hash);
}

bool CTxMemPool::HasNoInputsOf(const CTransaction &tx) const
{
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        if (exists(tx.vin[i].prevout.hash))
            return false;
    return true;
}

CCoinsViewMemPool::CCoinsViewMemPool(CCoinsView *baseIn, CTxMemPool &mempoolIn) : CCoinsViewBacked(baseIn), mempool(mempoolIn) { }

bool CCoinsViewMemPool::GetCoins(const uint256 &txid, CCoins &coins) const {
    // If an entry in the mempool exists, always return that one, as it's guaranteed to never
    // conflict with the underlying cache, and it cannot have pruned entries (as it contains full)
    // transactions. First checking the underlying cache risks returning a pruned entry instead.
    CTransaction tx;
    if (mempool.lookup(txid, tx)) {
        coins = CCoins(tx, MEMPOOL_HEIGHT);
        return true;
    }
    return (base->GetCoins(txid, coins) && !coins.IsPruned());
}

bool CCoinsViewMemPool::HaveCoins(const uint256 &txid) const {
    return mempool.exists(txid) || base->HaveCoins(txid);
}

size_t CTxMemPool::DynamicMemoryUsage() const {
    LOCK(cs);
    // Estimate the overhead of mapTx to be 12 pointers + an allocation, as no exact formula for boost::multi_index_contained is implemented.
    return memusage::MallocUsage(sizeof(CTxMemPoolEntry) + 12 * sizeof(void*)) * mapTx.size() + memusage::DynamicUsage(mapNextTx) + memusage::DynamicUsage(mapDeltas) + memusage::DynamicUsage(mapLinks) + cachedInnerUsage;
}

void CTxMemPool::RemoveStaged(setEntries &stage) {
    AssertLockHeld(cs);
    UpdateForRemoveFromMempool(stage);
    BOOST_FOREACH(const txiter& it, stage) {
        removeUnchecked(it);
    }
}

int CTxMemPool::Expire(int64_t time) {
    LOCK(cs);
    indexed_transaction_set::nth_index<2>::type::iterator it = mapTx.get<2>().begin();
    setEntries toremove;
    while (it != mapTx.get<2>().end() && it->GetTime() < time) {
        toremove.insert(mapTx.project<0>(it));
        it++;
    }
    setEntries stage;
    BOOST_FOREACH(txiter removeit, toremove) {
        CalculateDescendants(removeit, stage);
    }
    RemoveStaged(stage);
    return stage.size();
}

bool CTxMemPool::addUnchecked(const uint256&hash, const CTxMemPoolEntry &entry, const CCoinsViewCache &view, bool fCurrentEstimate)
{
    LOCK(cs);
    setEntries setAncestors;
    uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    CalculateMemPoolAncestors(entry, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy);
    return addUnchecked(hash, entry, view, setAncestors, fCurrentEstimate);
}

void CTxMemPool::UpdateChild(txiter entry, txiter child, bool add)
{
    setEntries s;
    if (add && mapLinks[entry].children.insert(child).second) {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    } else if (!add && mapLinks[entry].children.erase(child)) {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }
}

void CTxMemPool::UpdateParent(txiter entry, txiter parent, bool add)
{
    setEntries s;
    if (add && mapLinks[entry].parents.insert(parent).second) {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    } else if (!add && mapLinks[entry].parents.erase(parent)) {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }
}

const CTxMemPool::setEntries & CTxMemPool::GetMemPoolParents(txiter entry) const
{
    assert (entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.parents;
}

const CTxMemPool::setEntries & CTxMemPool::GetMemPoolChildren(txiter entry) const
{
    assert (entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.children;
}

CFeeRate CTxMemPool::GetMinFee(size_t sizelimit) const {
    LOCK(cs);
    if (!blockSinceLastRollingFeeBump || rollingMinimumFeeRate == 0)
        return CFeeRate(rollingMinimumFeeRate);

    int64_t time = GetTime();
    if (time > lastRollingFeeUpdate + 10) {
        double halflife = ROLLING_FEE_HALFLIFE;
        if (DynamicMemoryUsage() < sizelimit / 4)
            halflife /= 4;
        else if (DynamicMemoryUsage() < sizelimit / 2)
            halflife /= 2;

        rollingMinimumFeeRate = rollingMinimumFeeRate / pow(2.0, (time - lastRollingFeeUpdate) / halflife);
        lastRollingFeeUpdate = time;

        if (rollingMinimumFeeRate < minReasonableRelayFee.GetFeePerK() / 2) {
            rollingMinimumFeeRate = 0;
            return CFeeRate(0);
        }
    }
    return std::max(CFeeRate(rollingMinimumFeeRate), minReasonableRelayFee);
}

void CTxMemPool::UpdateMinFee(const CFeeRate& _minReasonableRelayFee)
{
    LOCK(cs);
    delete minerPolicyEstimator;
    minerPolicyEstimator = new CBlockPolicyEstimator(_minReasonableRelayFee);
    minReasonableRelayFee = _minReasonableRelayFee;
}

void CTxMemPool::trackPackageRemoved(const CFeeRate& rate) {
    AssertLockHeld(cs);
    if (rate.GetFeePerK() > rollingMinimumFeeRate) {
        rollingMinimumFeeRate = rate.GetFeePerK();
        blockSinceLastRollingFeeBump = false;
    }
}

void CTxMemPool::TrimToSize(size_t sizelimit, std::vector<uint256>* pvNoSpendsRemaining) {
    LOCK(cs);

    unsigned nTxnRemoved = 0;
    CFeeRate maxFeeRateRemoved(0);
    while (DynamicMemoryUsage() > sizelimit) {
        indexed_transaction_set::nth_index<1>::type::iterator it = mapTx.get<1>().begin();

        // We set the new mempool min fee to the feerate of the removed set, plus the
        // "minimum reasonable fee rate" (ie some value under which we consider txn
        // to have 0 fee). This way, we don't allow txn to enter mempool with feerate
        // equal to txn which were removed with no block in between.
        CFeeRate removed(it->GetModFeesWithDescendants(), it->GetSizeWithDescendants());
        removed += minReasonableRelayFee;
        trackPackageRemoved(removed);
        maxFeeRateRemoved = std::max(maxFeeRateRemoved, removed);

        setEntries stage;
        CalculateDescendants(mapTx.project<0>(it), stage);
        nTxnRemoved += stage.size();

        std::vector<CTransaction> txn;
        if (pvNoSpendsRemaining) {
            txn.reserve(stage.size());
            BOOST_FOREACH(txiter it, stage)
                txn.push_back(it->GetTx());
        }
        RemoveStaged(stage);
        if (pvNoSpendsRemaining) {
            BOOST_FOREACH(const CTransaction& tx, txn) {
                BOOST_FOREACH(const CTxIn& txin, tx.vin) {
                    if (exists(txin.prevout.hash))
                        continue;
                    std::map<COutPoint, CInPoint>::iterator it = mapNextTx.lower_bound(COutPoint(txin.prevout.hash, 0));
                    if (it == mapNextTx.end() || it->first.hash != txin.prevout.hash)
                        pvNoSpendsRemaining->push_back(txin.prevout.hash);
                }
            }
        }
    }

    if (maxFeeRateRemoved > CFeeRate(0))
        LogPrint("mempool", "Removed %u txn, rolling minimum fee bumped to %s\n", nTxnRemoved, maxFeeRateRemoved.ToString());
}
