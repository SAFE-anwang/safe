#include "safeccta.h"
#include "safeccta.pb.h"

using namespace std;

CAmount BCCTA_OUT_VALUE = 0.0001 * COIN;


std::string FillCCCTAInfo(const CCCTAInfo& CCTAInfoIn)
{
    Safeccta::CCTA data;

    data.set_scaccount(CCTAInfoIn.strscAccount);
    data.set_targetchain(CCTAInfoIn.strtargetChain);
    data.set_sepchar(CCTAInfoIn.strsepChar);
    data.set_bizremarks(CCTAInfoIn.strbizRemarks);
    
    string strData;
    data.SerializeToString(&strData);

    return strData;
}

std::string FillCBcctaCastCoin(const CBcctaCastCoin& BcctaCastCoinIn)
{
    Safeccta::BcctaCastCoin data;
    Safeccta::regCastCoinInfoes* datereginfo;

    std::vector<CRegCastCoinInfoes>::const_iterator it = BcctaCastCoinIn.vecRegInfo.begin();
    for (; it != BcctaCastCoinIn.vecRegInfo.end(); ++it)
    {
        CRegCastCoinInfoes tempregInfo = *it;
        datereginfo = data.add_reginfoes();
        datereginfo->set_sctxhash(tempregInfo.strscTxHash);
        datereginfo->set_assetname(tempregInfo.strassetName);
        if (!tempregInfo.strassetId.empty())
            datereginfo->set_assetid(tempregInfo.strassetId);
        datereginfo->set_quantity(tempregInfo.nquantity);
        datereginfo->set_safeuser(tempregInfo.strsafeUser);
    }

    string strData;
    data.SerializeToString(&strData);

    return strData;
}

