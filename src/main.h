#ifndef MAIN_H
#define MAIN_H

#include <vector>
#include "amount.h"
#include "spos/spos.h"


// generate blocks per day = 24 * 60 * 60 / 150
#define BLOCKS_PER_DAY      576
// generate blcoks per month = 30 * BLOCKS_PER_DAY
#define BLOCKS_PER_MONTH    17280
// generate blocks per year = 12 * BLOCKS_PER_MONTH
#define BLOCKS_PER_YEAR     207360


// SPOS generate blocks per day = 24 * 60 * 60 / 30
#define SPOS_BLOCKS_PER_DAY      2880
// SPOS generate blcoks per month = 30 * SPOS_BLOCKS_PER_DAY
#define SPOS_BLOCKS_PER_MONTH    86400
// SPOS generate blocks per year = 12 * SPOS_BLOCKS_PER_MONTH
#define SPOS_BLOCKS_PER_YEAR     1036800



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
extern int g_nSaveMasternodePayeeHeight;
extern unsigned int g_nMasternodeSPosCount;
extern int64_t g_nStartNewLoopTimeMS;
extern int g_nSposGeneratedIndex;
extern std::vector<CMasternode> g_vecResultMasternodes;
extern int g_nSelectMasterNodeRet;

extern int64_t g_nFirstSelectMasterNodeTime;
extern int64_t g_nAllowMasterNodeSyncErrorTime;

extern int g_nStartDeterministicMNHeight;
extern std::vector<CDeterministicMasternode_IndexValue> g_vecResultDeterministicMN;
extern std::vector<CDeterministicMasternode_IndexValue> g_vecReSelectResultMasternodes;
extern int g_nDeterministicMNTxMinConfirmNum;
extern bool g_fTimeoutThreetimes;
extern int g_nForbidStartDMN;
extern int g_nLocalStartSavePayeeHeightV2;
extern int g_nSaveMasternodePayeeHeightV2;







inline bool IsCriticalHeight(int nHeight) { return nHeight == g_nCriticalHeight; }

inline bool IsStartSPosHeight(int nHeight) { return nHeight >= g_nStartSPOSHeight; }

inline bool IsStartDeterministicMNHeight(int nHeight) {return nHeight >= g_nStartDeterministicMNHeight;}

inline bool IsLockedMonthRange(int nMonth) { return (nMonth >= 1 && nMonth <= 120); }

inline bool IsCancelledRange(const CAmount& nAmount) { return (nAmount >= 50 * COIN && nAmount <= 500 * COIN); }

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev);

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev, const int nVersion, const unsigned int nTime, const unsigned int nBits);

bool CheckCriticalBlock(const CBlockHeader& block);

int GetTxHeight(const uint256& txHash, uint256* pBlockHash = NULL, int32_t* pVersion = NULL);

bool IsLockedTxOut(const uint256& txHash, const CTxOut& txout);

bool IsLockedTxOutByHeight(const int& nHeight, const CTxOut& txout, const int32_t& nVersion);

int GetLockedMonth(const uint256& txHash, const CTxOut& txout);

int GetLockedMonthByHeight(const int& nHeight, const CTxOut& txout, const int32_t& nVersion);


CAmount GetCancelledAmount(const int& nHeight);

CAmount GetPowCancelledAmount(const int& nHeight);

CAmount GetSPOSCancelledAmount(const int& nHeight);



CAmount GetTxAdditionalFee(const CTransaction& tx);

int GetPrevBlockHeight(const uint256& hash);




#endif
