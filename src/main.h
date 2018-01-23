#ifndef MAIN_H
#define MAIN_H

#include <vector>

#define SAFE_TX_VERSION     101
#define TXOUT_RESERVE_MIN_SIZE      4
#define TXOUT_RESERVE_MAX_SIZE      3000

class CBlock;
class CBlockHeader;
class CBlockIndex;

extern int g_nChainHeight;
extern int g_nCriticalHeight;
extern int g_nAnWwangDiffOffset;

inline bool IsCriticalHeight(int nHeight) { return nHeight == g_nCriticalHeight; }

CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev);
CBlock CreateCriticalBlock(const CBlockIndex* pindexPrev, const int nVersion, const unsigned int nTime, const unsigned int nBits);

bool CheckCriticalBlock(const CBlockHeader& block);

int VectorEqual(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b);

#endif
