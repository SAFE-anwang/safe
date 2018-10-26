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

#define MIN_MN_LOCKED_MONTH     6

class CBlock;
class CBlockHeader;
class CBlockIndex;
class CTxOut;
class uint256;
class CTransaction;

extern int g_nChainHeight;
extern int g_nCriticalHeight;
extern int g_nAnWwangDiffOffset;
extern CAmount g_nCriticalReward;
extern std::string g_strCancelledMoneroCandyAddress;
extern std::string g_strCancelledSafeAddress;
extern std::string g_strCancelledAssetAddress;
extern std::string g_strPutCandyAddress;

extern int g_nStartAddamountHeight;
extern CAmount g_nCriticalEffective;

inline bool IsCriticalHeight(int nHeight) { return nHeight == g_nCriticalHeight; }

inline bool IsLockedMonthRange(int nMonth) { return (nMonth >= 1 && nMonth <= 120); }

inline bool IsCancelledRange(const CAmount& nAmount) { return (nAmount >= 50 * COIN && nAmount <= 500 * COIN); }

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev);

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev, const int nVersion, const unsigned int nTime, const unsigned int nBits);

bool CheckCriticalBlock(const CBlockHeader& block);

int GetTxHeight(const uint256& txHash, uint256* pBlockHash = NULL);

bool IsLockedTxOut(const uint256& txHash, const CTxOut& txout);

int GetLockedMonth(const uint256& txHash, const CTxOut& txout);

CAmount GetCancelledAmount(const int& nHeight);

CAmount GetTxAdditionalFee(const CTransaction& tx);

#endif
