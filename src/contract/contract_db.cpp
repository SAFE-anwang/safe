#include "contract_db.h"
#include "../app/app.pb.h"
#include "validation.h"

using namespace std;
CDBBaseImpl contract_db_imp;
CDBBase &contract_db = contract_db_imp;

vector<unsigned char> CDBBaseImpl::FillVirtualAccountData(const CAppHeader& header, const CVirtualAccountData& vitrualAccountData)
{
    vector<unsigned char> vData;
    FillHeader(header, vData);

    App::VirturalAccountData data;
    data.set_safeaddress(vitrualAccountData.strSafeAddress);
    data.set_accountname(vitrualAccountData.strVirtualAccountName);
    data.set_owner(vitrualAccountData.owner);
    data.set_active(vitrualAccountData.active);

    string strData;
    data.SerializeToString(&strData);

    unsigned int nSize = data.ByteSize();
    const unsigned char* pData = (const unsigned char*)strData.data();
    for(unsigned int i = 0; i < nSize; i++)
        vData.push_back(pData[i]);

    return vData;
}

bool CDBBaseImpl::ParseVirtualAccountData(const std::vector<unsigned char>& vVirtualAccountData, CVirtualAccountData& virtualAccountData)
{
    App::VirturalAccountData data;
    if(!data.ParseFromArray(&vVirtualAccountData[0], vVirtualAccountData.size()))
        return false;

    virtualAccountData.strSafeAddress = data.safeaddress();
    virtualAccountData.strVirtualAccountName = data.accountname();
    virtualAccountData.owner = data.owner();
    virtualAccountData.active = data.owner();

    return true;

}

bool CDBBaseImpl::Write_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect)
{
    return pblocktree->Write_VirtualAccountName_AccountId_Index(vect);
}

bool CDBBaseImpl::Write_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect)
{
    return pblocktree->Write_VirtualAccountId_Accountinfo_Index(vect);
}

bool CDBBaseImpl::Write_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect)
{
    return pblocktree->Write_SafeAdress_AccountId_Index(vect);
}

bool CDBBaseImpl::Read_VirtualAccountName_AccountId_Index(const std::string& strVirtualAccountName, CName_Id_IndexValue& value)
{
    return pblocktree->Read_VirtualAccountName_AccountId_Index(strVirtualAccountName, value);
}

bool CDBBaseImpl::Read_VirtualAccountId_Accountinfo_Index(const uint256& virtualAccountId, CVirtualAccountId_Accountinfo_IndexValue& virtualAccountInfo)
{
    return pblocktree->Read_VirtualAccountId_Accountinfo_Index(virtualAccountId, virtualAccountInfo);
}

bool CDBBaseImpl::Read_SafeAdress_AccountId_Index(const std::string& safeAddress, CName_Id_IndexValue& value)
{
    return pblocktree->Read_SafeAdress_AccountId_Index(safeAddress, value);
}

bool CDBBaseImpl::Erase_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect)
{
    return pblocktree->Erase_VirtualAccountName_AccountId_Index(vect);
}

bool CDBBaseImpl::Erase_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect)
{
    return pblocktree->Erase_VirtualAccountId_Accountinfo_Index(vect);
}

bool CDBBaseImpl::Erase_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect)
{
    return pblocktree->Erase_SafeAdress_AccountId_Index(vect);
}
