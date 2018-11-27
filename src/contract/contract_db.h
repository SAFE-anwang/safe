#include <vector>
#include "../app/app.h"
#include "virtual_account.h"
#include "../txdb.h"
#include "../validation.h"

struct CName_Id_IndexValue;

class CDBBase
{
public:
    virtual std::vector<unsigned char> FillVirtualAccountData(const CAppHeader& header, const CVirtualAccountData& vitrualAccountData) = 0;
    virtual bool ParseVirtualAccountData(const std::vector<unsigned char>& vVirtualAccountData, CVirtualAccountData& virtualAccountData) = 0;
    virtual bool Write_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect) = 0;
    virtual bool Write_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect) = 0;
    virtual bool Write_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect) = 0;
    virtual bool Read_VirtualAccountName_AccountId_Index(const std::string& strVirtualAccountName, CName_Id_IndexValue& value) = 0;
    virtual bool Read_VirtualAccountId_Accountinfo_Index(const uint256& virtualAccountId, CVirtualAccountId_Accountinfo_IndexValue& appInfo) = 0;
    virtual bool Read_SafeAdress_AccountId_Index(const std::string& safeAddress, CName_Id_IndexValue& value) = 0;
    virtual bool Erase_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect) = 0;
    virtual bool Erase_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect) = 0;
    virtual bool Erase_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect) = 0;
    virtual ~CDBBase() {}
};


class CDBBaseImpl : public CDBBase
{
public:
    virtual std::vector<unsigned char> FillVirtualAccountData(const CAppHeader& header, const CVirtualAccountData& vitrualAccountData) override;
    virtual bool ParseVirtualAccountData(const std::vector<unsigned char>& vVirtualAccountData, CVirtualAccountData& virtualAccountData) override;
    bool Write_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Write_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect);
    bool Write_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Read_VirtualAccountName_AccountId_Index(const std::string& strVirtualAccountName, CName_Id_IndexValue& value);
    bool Read_VirtualAccountId_Accountinfo_Index(const uint256& virtualAccountId, CVirtualAccountId_Accountinfo_IndexValue& appInfo);
    bool Read_SafeAdress_AccountId_Index(const std::string& safeAddress, CName_Id_IndexValue& value);
    bool Erase_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Erase_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect);
    bool Erase_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    ~CDBBaseImpl() {}
};

extern CDBBase &contract_db; 