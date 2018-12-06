#ifndef MAIN_H
#define MAIN_H

#include <vector>
#include "amount.h"

// generate blocks per day = 24 * 60 * 60 / 150
#define BLOCKS_PER_DAY      576
// generate blcoks per month = 30 * BLOCKS_PER_DAY
#define BLOCKS_PER_MONTH    17280
// generate blocks per year = 12 * BLOCKS_PER_MONTH
#define BLOCKS_PER_YEAR     207360

// SPOS generate blocks per day = 24 * 60 * 60 / 5
#define SPOS_BLOCKS_PER_DAY      17280
// SPOS generate blcoks per month = 30 * SPOS_BLOCKS_PER_DAY
#define SPOS_BLOCKS_PER_MONTH    518400
// SPOS generate blocks per year = 12 * SPOS_BLOCKS_PER_MONTH
#define SPOS_BLOCKS_PER_YEAR     6220800


#define MIN_MN_LOCKED_MONTH     6

class CBlock;
class CBlockHeader;
class CBlockIndex;
class CTxOut;
class uint256;
class CTransaction;
class CMasternode;

extern int g_nChainHeight;
extern int g_nCriticalHeight;
extern int g_nAnWwangDiffOffset;
extern CAmount g_nCriticalReward;
extern std::string g_strCancelledMoneroCandyAddress;
extern std::string g_strCancelledSafeAddress;
extern std::string g_strCancelledAssetAddress;
extern std::string g_strPutCandyAddress;

extern int g_nStartSPOSHeight;
extern unsigned int g_nMasternodeSPosCount;
extern int64_t g_nStartNewLoopTime;
extern std::vector<CMasternode> g_vecResultMasternodes;





inline bool IsCriticalHeight(int nHeight) { return nHeight == g_nCriticalHeight; }

inline bool IsStartSPosHeight(int nHeight) { return nHeight >= g_nStartSPOSHeight; }

inline bool IsLockedMonthRange(int nMonth) { return (nMonth >= 1 && nMonth <= 120); }

inline bool IsCancelledRange(const CAmount& nAmount) { return (nAmount >= 50 * COIN && nAmount <= 500 * COIN); }

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev);

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev, const int nVersion, const unsigned int nTime, const unsigned int nBits);

bool CheckCriticalBlock(const CBlockHeader& block);

int GetTxHeight(const uint256& txHash, uint256* pBlockHash = NULL);

bool IsLockedTxOut(const uint256& txHash, const CTxOut& txout);

bool IsLockedTxOutByHeight(const int& nheight, const CTxOut& txout);

int GetLockedMonth(const uint256& txHash, const CTxOut& txout);

int GetLockedMonthByHeight(const int& nheight, const CTxOut& txout);


CAmount GetCancelledAmount(const int& nHeight);

CAmount GetTxAdditionalFee(const CTransaction& tx);

int GetPrevBlockHeight(const uint256& hash);


#endif
