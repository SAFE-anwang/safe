// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2018 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "validation.h"
#include "messagesigner.h"
#include "net_processing.h"
#include "spork.h"
#include "masternode-sync.h"


#include <boost/lexical_cast.hpp>

class CSporkMessage;
class CSporkManager;

CSporkManager sporkManager;

std::map<uint256, CSporkMessage> mapSporks;
extern int g_nSelectGlobalDefaultValue;
extern int g_nPushForwardHeight;
extern unsigned int g_nMasternodeSPosCount;
extern unsigned int g_nMasternodeMinCount;

void CSporkManager::ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if(fLiteMode) return; // disable all Safe specific functionality

    if (strCommand == NetMsgType::SPORK) {

        CSporkMessage spork;
        vRecv >> spork;

        uint256 hash = spork.GetHash();

        std::string strLogMsg;
        {
            LOCK(cs_main);
            pfrom->setAskFor.erase(hash);
            if(!chainActive.Tip()) 
            {
                LogPrintf("CSporkManager::ProcessSpork chainActive.Tip() is NULL\n");
                return;
            }

            strLogMsg = strprintf("SPORK -- hash: %s id: %d value: %10d bestHeight: %d peer=%d", hash.ToString(), spork.nSporkID, spork.nValue, chainActive.Height(), pfrom->id);
        }

        bool fCheckAndSelectMaster = false;
        if (mapSporksActive.count(spork.nSporkID)) {
            if (spork.nSporkID == SPORK_6_SPOS_ENABLED)
            {
                if (!spork.CheckSignature()) {
                    LogPrintf("CSporkManager::ProcessSpork -- invalid signature\n");
                    Misbehaving(pfrom->GetId(), 100);
                    return;
                }

                std::string strErrMessage = "";
                if (!CheckSPORK_6_SPOSValue(spork.nSporkID, spork.nValue, strErrMessage))
                {
                    LogPrintf("SPOS_Warning: strErrMessage:%s, spork.nSporkID:%d, spork.nValue:%lld\n", strErrMessage, spork.nSporkID, spork.nValue);
                    return;
                }

                SelectMasterNodeForSpork(spork.nSporkID, spork.nValue);
                fCheckAndSelectMaster = true;
            }
        
            if (mapSporksActive[spork.nSporkID].nTimeSigned >= spork.nTimeSigned) {
                LogPrint("spork", "%s seen\n", strLogMsg);
                return;
            } else {
                LogPrintf("%s updated\n", strLogMsg);
            }
        } else {
            LogPrintf("%s new\n", strLogMsg);
        }

        if (!fCheckAndSelectMaster)
        {
            if(!spork.CheckSignature()) {
                LogPrintf("CSporkManager::ProcessSpork -- invalid signature\n");
                Misbehaving(pfrom->GetId(), 100);
                return;
            }
        }

        std::string strErrMessage = "";
        if (!CheckSPORK_6_SPOSValue(spork.nSporkID, spork.nValue, strErrMessage))
        {
            LogPrintf("SPOS_Warning: strErrMessage:%s, spork.nSporkID:%d, spork.nValue:%lld\n", strErrMessage, spork.nSporkID, spork.nValue);
            return;
        }

        mapSporks[hash] = spork;
        mapSporksActive[spork.nSporkID] = spork;
        spork.Relay(connman);

        if (!fCheckAndSelectMaster)
            SelectMasterNodeForSpork(spork.nSporkID, spork.nValue);

        //does a task if needed
        ExecuteSpork(spork.nSporkID, spork.nValue);
    } else if (strCommand == NetMsgType::GETSPORKS) {

        std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();

        while(it != mapSporksActive.end()) {
            connman.PushMessage(pfrom, NetMsgType::SPORK, it->second);
            it++;
        }
    }

}

void CSporkManager::ExecuteSpork(int nSporkID, int nValue)
{
    //correct fork via spork technology
    if(nSporkID == SPORK_12_RECONSIDER_BLOCKS && nValue > 0) {
        // allow to reprocess 24h of blocks max, which should be enough to resolve any issues
        int64_t nMaxBlocks = 576;
        // this potentially can be a heavy operation, so only allow this to be executed once per 10 minutes
        int64_t nTimeout = 10 * 60;

        static int64_t nTimeExecuted = 0; // i.e. it was never executed before

        if(GetTime() - nTimeExecuted < nTimeout) {
            LogPrint("spork", "CSporkManager::ExecuteSpork -- ERROR: Trying to reconsider blocks, too soon - %d/%d\n", GetTime() - nTimeExecuted, nTimeout);
            return;
        }

        if(nValue > nMaxBlocks) {
            LogPrintf("CSporkManager::ExecuteSpork -- ERROR: Trying to reconsider too many blocks %d/%d\n", nValue, nMaxBlocks);
            return;
        }


        LogPrintf("CSporkManager::ExecuteSpork -- Reconsider Last %d Blocks\n", nValue);

        ReprocessBlocks(nValue);
        nTimeExecuted = GetTime();
    }
}

bool CSporkManager::UpdateSpork(int nSporkID, int64_t nValue, CConnman& connman)
{
    CSporkMessage spork = CSporkMessage(nSporkID, nValue, GetAdjustedTime());

    if(spork.Sign(strMasterPrivKey)) {
        spork.Relay(connman);
        mapSporks[spork.GetHash()] = spork;
        mapSporksActive[nSporkID] = spork;
        return true;
    }

    return false;
}

// grab the spork, otherwise say it's off
bool CSporkManager::IsSporkActive(int nSporkID)
{
    int64_t r = -1;

    if(mapSporksActive.count(nSporkID)){
        r = mapSporksActive[nSporkID].nValue;
    } else {
        switch (nSporkID) {
            case SPORK_2_INSTANTSEND_ENABLED:               r = SPORK_2_INSTANTSEND_ENABLED_DEFAULT; break;
            case SPORK_3_INSTANTSEND_BLOCK_FILTERING:       r = SPORK_3_INSTANTSEND_BLOCK_FILTERING_DEFAULT; break;
            case SPORK_5_INSTANTSEND_MAX_VALUE:             r = SPORK_5_INSTANTSEND_MAX_VALUE_DEFAULT; break;
            case SPORK_6_SPOS_ENABLED:                      r = SPORK_6_SPOS_ENABLEDE_DEFAULT; break;
            case SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT:    r = SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT; break;
            case SPORK_9_SUPERBLOCKS_ENABLED:               r = SPORK_9_SUPERBLOCKS_ENABLED_DEFAULT; break;
            case SPORK_10_MASTERNODE_PAY_UPDATED_NODES:     r = SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT; break;
            case SPORK_12_RECONSIDER_BLOCKS:                r = SPORK_12_RECONSIDER_BLOCKS_DEFAULT; break;
            case SPORK_13_OLD_SUPERBLOCK_FLAG:              r = SPORK_13_OLD_SUPERBLOCK_FLAG_DEFAULT; break;
            case SPORK_14_REQUIRE_SENTINEL_FLAG:            r = SPORK_14_REQUIRE_SENTINEL_FLAG_DEFAULT; break;
            case SPORK_101_DEV_TYPE_MAX_VALUE:              r = SPORK_101_DEV_TYPE_MAX_VALUE_DEFAULT; break;
            case SPORK_102_SET_TYPE_MAX_VALUE:              r = SPORK_102_SET_TYPE_MAX_VALUE_DEFAULT; break;
            case SPORK_103_AUTH_CMD_MAX_VALUE:              r = SPORK_103_AUTH_CMD_MAX_VALUE_DEFAULT; break;
            case SPORK_104_APP_TX_TYPE_MAX_VALUE:           r = SPORK_104_APP_TX_TYPE_MAX_VALUE_DEFAULT; break;
            case SPORK_105_TX_CLASS_MAX_VALUE:              r = SPORK_105_TX_CLASS_MAX_VALUE_DEFAULT; break;
            default:
                LogPrint("spork", "CSporkManager::IsSporkActive -- Unknown Spork ID %d\n", nSporkID);
                r = 4070908800ULL; // 2099-1-1 i.e. off by default
                break;
        }
    }

    return r < GetAdjustedTime();
}

// grab the value of the spork on the network, or the default
int64_t CSporkManager::GetSporkValue(int nSporkID)
{
    if (mapSporksActive.count(nSporkID))
        return mapSporksActive[nSporkID].nValue;

    switch (nSporkID) {
        case SPORK_2_INSTANTSEND_ENABLED:               return SPORK_2_INSTANTSEND_ENABLED_DEFAULT;
        case SPORK_3_INSTANTSEND_BLOCK_FILTERING:       return SPORK_3_INSTANTSEND_BLOCK_FILTERING_DEFAULT;
        case SPORK_5_INSTANTSEND_MAX_VALUE:             return SPORK_5_INSTANTSEND_MAX_VALUE_DEFAULT;
        case SPORK_6_SPOS_ENABLED:                      return SPORK_6_SPOS_ENABLEDE_DEFAULT;
        case SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT:    return SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT;
        case SPORK_9_SUPERBLOCKS_ENABLED:               return SPORK_9_SUPERBLOCKS_ENABLED_DEFAULT;
        case SPORK_10_MASTERNODE_PAY_UPDATED_NODES:     return SPORK_10_MASTERNODE_PAY_UPDATED_NODES_DEFAULT;
        case SPORK_12_RECONSIDER_BLOCKS:                return SPORK_12_RECONSIDER_BLOCKS_DEFAULT;
        case SPORK_13_OLD_SUPERBLOCK_FLAG:              return SPORK_13_OLD_SUPERBLOCK_FLAG_DEFAULT;
        case SPORK_14_REQUIRE_SENTINEL_FLAG:            return SPORK_14_REQUIRE_SENTINEL_FLAG_DEFAULT;
        case SPORK_101_DEV_TYPE_MAX_VALUE:              return SPORK_101_DEV_TYPE_MAX_VALUE_DEFAULT;
        case SPORK_102_SET_TYPE_MAX_VALUE:              return SPORK_102_SET_TYPE_MAX_VALUE_DEFAULT;
        case SPORK_103_AUTH_CMD_MAX_VALUE:              return SPORK_103_AUTH_CMD_MAX_VALUE_DEFAULT;
        case SPORK_104_APP_TX_TYPE_MAX_VALUE:           return SPORK_104_APP_TX_TYPE_MAX_VALUE_DEFAULT;
        case SPORK_105_TX_CLASS_MAX_VALUE:              return SPORK_105_TX_CLASS_MAX_VALUE_DEFAULT;
        default:
            LogPrint("spork", "CSporkManager::GetSporkValue -- Unknown Spork ID %d\n", nSporkID);
            return -1;
    }

}

int CSporkManager::GetSporkIDByName(std::string strName)
{
    if (strName == "SPORK_2_INSTANTSEND_ENABLED")               return SPORK_2_INSTANTSEND_ENABLED;
    if (strName == "SPORK_3_INSTANTSEND_BLOCK_FILTERING")       return SPORK_3_INSTANTSEND_BLOCK_FILTERING;
    if (strName == "SPORK_5_INSTANTSEND_MAX_VALUE")             return SPORK_5_INSTANTSEND_MAX_VALUE;
    if (strName == "SPORK_6_SPOS_ENABLED")                      return SPORK_6_SPOS_ENABLED;
    if (strName == "SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT")    return SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT;
    if (strName == "SPORK_9_SUPERBLOCKS_ENABLED")               return SPORK_9_SUPERBLOCKS_ENABLED;
    if (strName == "SPORK_10_MASTERNODE_PAY_UPDATED_NODES")     return SPORK_10_MASTERNODE_PAY_UPDATED_NODES;
    if (strName == "SPORK_12_RECONSIDER_BLOCKS")                return SPORK_12_RECONSIDER_BLOCKS;
    if (strName == "SPORK_13_OLD_SUPERBLOCK_FLAG")              return SPORK_13_OLD_SUPERBLOCK_FLAG;
    if (strName == "SPORK_14_REQUIRE_SENTINEL_FLAG")            return SPORK_14_REQUIRE_SENTINEL_FLAG;
    if (strName == "SPORK_101_DEV_TYPE_MAX_VALUE")              return SPORK_101_DEV_TYPE_MAX_VALUE;
    if (strName == "SPORK_102_SET_TYPE_MAX_VALUE")              return SPORK_102_SET_TYPE_MAX_VALUE;
    if (strName == "SPORK_103_AUTH_CMD_MAX_VALUE")              return SPORK_103_AUTH_CMD_MAX_VALUE;
    if (strName == "SPORK_104_APP_TX_TYPE_MAX_VALUE")           return SPORK_104_APP_TX_TYPE_MAX_VALUE;
    if (strName == "SPORK_105_TX_CLASS_MAX_VALUE")              return SPORK_105_TX_CLASS_MAX_VALUE;

    LogPrint("spork", "CSporkManager::GetSporkIDByName -- Unknown Spork name '%s'\n", strName);
    return -1;
}

std::string CSporkManager::GetSporkNameByID(int nSporkID)
{
    switch (nSporkID) {
        case SPORK_2_INSTANTSEND_ENABLED:               return "SPORK_2_INSTANTSEND_ENABLED";
        case SPORK_3_INSTANTSEND_BLOCK_FILTERING:       return "SPORK_3_INSTANTSEND_BLOCK_FILTERING";
        case SPORK_5_INSTANTSEND_MAX_VALUE:             return "SPORK_5_INSTANTSEND_MAX_VALUE";
        case SPORK_6_SPOS_ENABLED:                      return "SPORK_6_SPOS_ENABLED";
        case SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT:    return "SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT";
        case SPORK_9_SUPERBLOCKS_ENABLED:               return "SPORK_9_SUPERBLOCKS_ENABLED";
        case SPORK_10_MASTERNODE_PAY_UPDATED_NODES:     return "SPORK_10_MASTERNODE_PAY_UPDATED_NODES";
        case SPORK_12_RECONSIDER_BLOCKS:                return "SPORK_12_RECONSIDER_BLOCKS";
        case SPORK_13_OLD_SUPERBLOCK_FLAG:              return "SPORK_13_OLD_SUPERBLOCK_FLAG";
        case SPORK_14_REQUIRE_SENTINEL_FLAG:            return "SPORK_14_REQUIRE_SENTINEL_FLAG";
        case SPORK_101_DEV_TYPE_MAX_VALUE:              return "SPORK_101_DEV_TYPE_MAX_VALUE";
        case SPORK_102_SET_TYPE_MAX_VALUE:              return "SPORK_102_SET_TYPE_MAX_VALUE";
        case SPORK_103_AUTH_CMD_MAX_VALUE:              return "SPORK_103_AUTH_CMD_MAX_VALUE";
        case SPORK_104_APP_TX_TYPE_MAX_VALUE:           return "SPORK_104_APP_TX_TYPE_MAX_VALUE";
        case SPORK_105_TX_CLASS_MAX_VALUE:              return "SPORK_105_TX_CLASS_MAX_VALUE";
        default:
            LogPrint("spork", "CSporkManager::GetSporkNameByID -- Unknown Spork ID %d\n", nSporkID);
            return "Unknown";
    }
}

bool CSporkManager::SetPrivKey(std::string strPrivKey)
{
    CSporkMessage spork;

    spork.Sign(strPrivKey);

    if(spork.CheckSignature()){
        // Test signing successful, proceed
        LogPrintf("CSporkManager::SetPrivKey -- Successfully initialized as spork signer\n");
        strMasterPrivKey = strPrivKey;
        return true;
    } else {
        return false;
    }
}

void CSporkManager::SelectMasterNodeForSpork(int nSporkID, int64_t nValue)
{
    LogPrintf("SPOS_Message:SelectMasterNodeForSpork -- nSporkID:%d---chainActive height:%d----nValue:%lld\n", nSporkID, chainActive.Height(), nValue);
    if (IsSporkActive(SPORK_6_SPOS_ENABLED))
        LogPrintf("SPOS_Message:SPORK_6_SPOS_ENABLED is open\n");
    else
        LogPrintf("SPOS_Message:SPORK_6_SPOS_ENABLED is close\n");//SQTODO

    if (nSporkID == SPORK_6_SPOS_ENABLED && IsSporkActive(SPORK_6_SPOS_ENABLED))
    {
        std::string strSporkValue = boost::lexical_cast<std::string>(nValue);
        std::string strHeight = strSporkValue.substr(0, strSporkValue.length() - 1);
        std::string strOfficialMasterNodeCount = strSporkValue.substr(strSporkValue.length() - 1);
        int nHeight = boost::lexical_cast<int>(strHeight);
        int nOfficialMasterNodeCount = boost::lexical_cast<int>(strOfficialMasterNodeCount);
        LogPrintf("SPOS_Message: SelectMasterNodeForSpork() strHeight:%s---strOfficialMasterNodeCount:%s---nHeight:%d--nOfficialMasterNodeCount:%d--chainActive.Height:%d\n",
                  strHeight, strOfficialMasterNodeCount, nHeight, nOfficialMasterNodeCount, chainActive.Height());

        if (chainActive.Height() == nHeight)
        {
            std::vector<CMasternode> vecMasternodes;
            bool bClearVec=false;
            int nSelectMasterNodeRet=g_nSelectGlobalDefaultValue,nSposGeneratedIndex=g_nSelectGlobalDefaultValue;
            int64_t nStartNewLoopTime=g_nSelectGlobalDefaultValue;
            int64_t nGeneralStartNewLoopTime = g_nSelectGlobalDefaultValue;
            SPORK_SELECT_LOOP nSporkSelectLoop = NO_SPORK_SELECT_LOOP;
            int nForwardHeight = 0,nScoreHeight = 0;
            updateForwardHeightAndScoreHeight(chainActive.Height(),nForwardHeight,nScoreHeight);
            CBlockIndex* forwardIndex = chainActive[nForwardHeight];
            if(forwardIndex==NULL)
            {
                LogPrintf("SPOS_Warning:spork forwardIndex is NULL,height:%d\n",nForwardHeight);
                return;
            }
            CBlockIndex* scoreIndex = chainActive[nScoreHeight];
            if(scoreIndex==NULL)
            {
                LogPrintf("SPOS_Warning:spork scoreIndex is NULL,height:%d\n",nScoreHeight);
                return;
            }

            if (nOfficialMasterNodeCount <= 0 || nOfficialMasterNodeCount > g_nMasternodeSPosCount)
            {
                LogPrintf("SPOS_Warning: SelectMasterNodeForSpork() nOfficialMasterNodeCount is error,height:%d, nOfficialMasterNodeCount:%d, g_nMasternodeSPosCount:%d\n",nHeight, nOfficialMasterNodeCount, g_nMasternodeSPosCount);
                return;
            }
            nSporkSelectLoop = SPORK_SELECT_LOOP_1;
            SelectMasterNodeByPayee(chainActive.Height(), forwardIndex->nTime,scoreIndex->nTime, true, true,vecMasternodes, bClearVec
                                        ,nSelectMasterNodeRet,nSposGeneratedIndex,nStartNewLoopTime, false, nOfficialMasterNodeCount, nSporkSelectLoop, false);

            nSporkSelectLoop = SPORK_SELECT_LOOP_2;
            if (g_nMasternodeSPosCount - nOfficialMasterNodeCount > 0 && nSelectMasterNodeRet > 0)
                SelectMasterNodeByPayee(chainActive.Height(), forwardIndex->nTime,scoreIndex->nTime, false, true,vecMasternodes,bClearVec
                                    ,nSelectMasterNodeRet,nSposGeneratedIndex,nGeneralStartNewLoopTime, false, g_nMasternodeSPosCount - nOfficialMasterNodeCount, nSporkSelectLoop, true);

            UpdateMasternodeGlobalData(vecMasternodes,bClearVec,nSelectMasterNodeRet,nSposGeneratedIndex,nStartNewLoopTime);
        }
    }
}

bool CSporkManager::CheckSPORK_6_SPOSValue(const int& nSporkID, const int64_t& nValue, std::string &strErrMessage)
{
    if (nSporkID == SPORK_6_SPOS_ENABLED)
    {
        if (!masternodeSync.IsSynced())
        {
            strErrMessage = "masternode is syncing";
            return false;
        }

        if (masternodeSync.IsSynced() && nValue < chainActive.Height())
        {
            strErrMessage = "value less than the current height"
            return false;
        }
    
        if (nValue == SPORK_6_SPOS_ENABLEDE_DEFAULT)
            return true;
        else
        {
            std::string strSporkValue = boost::lexical_cast<std::string>(nValue);
            std::string strHeight = strSporkValue.substr(0, strSporkValue.length() - 1);
            std::string strOfficialMasterNodeCount = strSporkValue.substr(strSporkValue.length() - 1);
            int nHeight = boost::lexical_cast<int>(strHeight);
            int nOfficialMasterNodeCount = boost::lexical_cast<int>(strOfficialMasterNodeCount);

            const std::vector<COutPointData> &vtempOutPointData = Params().COutPointDataS();

            if (nOfficialMasterNodeCount <= 0 || nOfficialMasterNodeCount > vtempOutPointData.size())
            {
                strErrMessage = "value less than or equal to 0 or greater than the total number of official master nodes";
                return false;
            }            
        }
    }

    return true;
}

bool CSporkMessage::Sign(std::string strSignKey)
{
    CKey key;
    CPubKey pubkey;
    std::string strError = "";
    std::string strMessage = boost::lexical_cast<std::string>(nSporkID) + boost::lexical_cast<std::string>(nValue) + boost::lexical_cast<std::string>(nTimeSigned);

    if(!CMessageSigner::GetKeysFromSecret(strSignKey, key, pubkey)) {
        LogPrintf("CSporkMessage::Sign -- GetKeysFromSecret() failed, invalid spork key %s\n", strSignKey);
        return false;
    }

    if(!CMessageSigner::SignMessage(strMessage, vchSig, key)) {
        LogPrintf("CSporkMessage::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(pubkey, vchSig, strMessage, strError)) {
        LogPrintf("CSporkMessage::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CSporkMessage::CheckSignature()
{
    //note: need to investigate why this is failing
    std::string strError = "";
    std::string strMessage = boost::lexical_cast<std::string>(nSporkID) + boost::lexical_cast<std::string>(nValue) + boost::lexical_cast<std::string>(nTimeSigned);
    CPubKey pubkey(ParseHex(Params().SporkPubKey()));

    if(!CMessageSigner::VerifyMessage(pubkey, vchSig, strMessage, strError)) {
        LogPrintf("CSporkMessage::CheckSignature -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

void CSporkMessage::Relay(CConnman& connman)
{
    CInv inv(MSG_SPORK, GetHash());
    connman.RelayInv(inv);
}
