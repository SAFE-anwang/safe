#include <vector>
#include "../app/app.h"
#include "virtual_account.h"
#include "../txdb.h"
#include "../validation.h"
#include<mysql/mysql.h>

#define RDBSERVER "localhost"
#define RDBUSERNAME "root"
#define RDBPASSWD ""
#define RDBPORT 0
#define RDBNAME "safe"
#define RDBTABLE_VIRTUALACCOUNT "virtualaccount"

struct CName_Id_IndexValue;

class CDBBase
{
public:
    CDBBase() : errorNum(0),errorInfo("ok") {}
    virtual bool connectMySQL(const std::string server, const std::string username, const std::string password, const std::string database,int port) = 0;
    virtual bool createDatabase(const std::string& dbname) = 0;
    virtual bool excuteSQL(const std::string& query)= 0;

    virtual void errorIntoMySQL() = 0;
    virtual bool getDatafromDB(std::string queryStr, std::vector<std::vector<std::string> >& data) = 0;
    virtual void closeMySQL() = 0;

public:
    virtual std::vector<unsigned char> FillVirtualAccountData(const CAppHeader& header, const CVirtualAccountData& vitrualAccountData) = 0;
    virtual bool ParseVirtualAccountData(const std::vector<unsigned char>& vVirtualAccountData, CVirtualAccountData& virtualAccountData) = 0;
    virtual bool Write_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect) = 0;
    virtual bool Write_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect) = 0;
    virtual bool Write_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect) = 0;
    virtual bool Read_VirtualAccountName_AccountId_Index(const std::string& strVirtualAccountName, CName_Id_IndexValue& value) = 0;
    virtual bool Read_VirtualAccountId_Accountinfo_Index(const uint256& virtualAccountId, CVirtualAccountId_Accountinfo_IndexValue& virtualAccountInfo) = 0;
    virtual bool Read_SafeAdress_AccountId_Index(const std::string& safeAddress, CName_Id_IndexValue& value) = 0;
    virtual bool Erase_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect) = 0;
    virtual bool Erase_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect) = 0;
    virtual bool Erase_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect) = 0;

    virtual bool Write_VirtualAccountInfo_BySQL(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect) = 0;
    virtual bool Read_VirtualAccountInfo_BySQL(const DBQueryType type, const std::string& key, CVirtualAccountId_Accountinfo_IndexValue& virtualAccountInfo) = 0;
    virtual bool Read_VirtualAccountInfoList_BySQL(const std::string& name, std::map<std::string, CVirtualAccountId_Accountinfo_IndexValue>& vVirtualAccountInfo, const int limit) = 0;
    virtual bool Erase_VirtualAccountInfo_BySQL(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect) = 0;
    virtual ~CDBBase() {}
public:
    int errorNum;
    const char* errorInfo;
};


class CDBBaseImpl : public CDBBase
{
public:
    CDBBaseImpl();
    bool connectMySQL(const std::string server, const std::string username, const std::string password, const std::string database,int port);
    bool createDatabase(const std::string& dbname);
    bool excuteSQL(const std::string& query);
    void errorIntoMySQL();
    bool getDatafromDB(std::string queryStr, std::vector<std::vector<std::string> >& data);
    void closeMySQL();
public:
    virtual std::vector<unsigned char> FillVirtualAccountData(const CAppHeader& header, const CVirtualAccountData& vitrualAccountData) override;
    virtual bool ParseVirtualAccountData(const std::vector<unsigned char>& vVirtualAccountData, CVirtualAccountData& virtualAccountData) override;

    // level db interface
    bool Write_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Write_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect);
    bool Write_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Read_VirtualAccountName_AccountId_Index(const std::string& strVirtualAccountName, CName_Id_IndexValue& value);
    bool Read_VirtualAccountId_Accountinfo_Index(const uint256& virtualAccountId, CVirtualAccountId_Accountinfo_IndexValue& virtualAccountInfo);
    bool Read_SafeAdress_AccountId_Index(const std::string& safeAddress, CName_Id_IndexValue& value);
    bool Erase_VirtualAccountName_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);
    bool Erase_VirtualAccountId_Accountinfo_Index(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect);
    bool Erase_SafeAdress_AccountId_Index(const std::vector<std::pair<std::string, CName_Id_IndexValue> >& vect);

    //mysql interface
    bool Write_VirtualAccountInfo_BySQL(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect);
    bool Read_VirtualAccountInfo_BySQL(const DBQueryType type, const std::string& key, CVirtualAccountId_Accountinfo_IndexValue& virtualAccountInfo);
    bool Read_VirtualAccountInfoList_BySQL(const std::string& name, std::map<std::string, CVirtualAccountId_Accountinfo_IndexValue>& vVirtualAccountInfo, const int limit);
    bool Erase_VirtualAccountInfo_BySQL(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect);

    ~CDBBaseImpl() noexcept {}

private:
    MYSQL mysqlInstance;
    MYSQL_RES *result;

};

extern CDBBase &contract_db; 