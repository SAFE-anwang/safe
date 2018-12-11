#include "contract_db.h"
#include "../app/app.pb.h"
#include "validation.h"

using namespace std;
CDBBaseImpl contract_db_imp;
CDBBase &contract_db = contract_db_imp;

CDBBaseImpl::CDBBaseImpl()
{
    mysql_library_init(0,NULL,NULL);
    mysql_init(&mysqlInstance);
    // mysql_options(&mysqlInstance,MYSQL_SET_CHARSET_NAME,"gbk");
    createDatabase(RDBNAME);
    excuteSQL("create table " RDBTABLE_VIRTUALACCOUNT " (safeaddress VARCHAR(50) NOT NULL PRIMARY KEY, name VARCHAR(30) NOT NULL unique, accountinfo blob NOT NULL)");
}

bool CDBBaseImpl::connectMySQL(const std::string server, const std::string username, const std::string password, const std::string database,int port)
{
    if(mysql_real_connect(&mysqlInstance,server.c_str(),username.c_str(),password.c_str(),database.c_str(),port,0,0) != NULL)
        return true;
    errorIntoMySQL();
    return false;
}

bool CDBBaseImpl::createDatabase(const std::string& dbname)
{
    if (!connectMySQL(RDBSERVER, RDBUSERNAME, RDBPASSWD, "", RDBPORT)) {
        closeMySQL();
        return false;
    }

    std::string queryStr = "create database if not exists ";
    queryStr += dbname;
    if (0 == mysql_query(&mysqlInstance,queryStr.c_str()))
    {
        queryStr = "use ";
        queryStr += dbname;
        if (0 == mysql_query(&mysqlInstance,queryStr.c_str()))
        {
            closeMySQL();
            return true;
        }
    }
    errorIntoMySQL();
    closeMySQL();
    return false;
}

bool CDBBaseImpl::excuteSQL(const std::string& query)
{
    if (!connectMySQL(RDBSERVER, RDBUSERNAME, RDBPASSWD, RDBNAME, RDBPORT)) {
        closeMySQL();
        return false;
    }
    if (0 == mysql_query(&mysqlInstance,query.c_str()))
    {
        closeMySQL();
        return true;
    }
    errorIntoMySQL();
    closeMySQL();
    return false;
}

bool CDBBaseImpl::getDatafromDB(string queryStr, std::vector<std::vector<std::string> >& data)
{
    if (!connectMySQL(RDBSERVER, RDBUSERNAME, RDBPASSWD, RDBNAME, RDBPORT)) {
        closeMySQL();
        return false;
    }

    if(0!=mysql_query(&mysqlInstance,queryStr.c_str()))
    {
        errorIntoMySQL();
        closeMySQL();
        return false;
    }

    result=mysql_store_result(&mysqlInstance);

    int row=mysql_num_rows(result);
    int field=mysql_num_fields(result);

    MYSQL_ROW line=NULL;
    line=mysql_fetch_row(result);

    int j=0;
    std::string temp;
    while(NULL!=line)
    {
        std::vector<std::string> linedata;
        for(int i=0; i<field;i++)
        {
            if(line[i])
            {
                temp = line[i];
                linedata.push_back(temp);
            }
            else
            {
                temp = "";
                linedata.push_back(temp);
            }
        }
        line=mysql_fetch_row(result);
        data.push_back(linedata);
    }
    closeMySQL();
    return true;
}

void CDBBaseImpl::errorIntoMySQL()
{
    errorNum=mysql_errno(&mysqlInstance);
    errorInfo=mysql_error(&mysqlInstance);
}

void CDBBaseImpl::closeMySQL()
{
    mysql_close(&mysqlInstance);
}


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
    virtualAccountData.active = data.active();

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

bool CDBBaseImpl::Write_VirtualAccountInfo_BySQL(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect)
{
    if (!connectMySQL(RDBSERVER, RDBUSERNAME, RDBPASSWD, RDBNAME, RDBPORT)) {
        closeMySQL();
        return false;
    }

    int virtualAccountObjectSize = sizeof(CVirtualAccountId_Accountinfo_IndexValue) * 2 + 2;
    for (auto ite = vect.begin(); ite != vect.end(); ++ite) {
        constexpr auto data_size = sizeof(CVirtualAccountId_Accountinfo_IndexValue);
        char chunk[2*data_size + 1];

        CDataStream ssData(SER_DISK, CLIENT_VERSION);
        ssData.reserve(ssData.GetSerializeSize(ite->second));
        ssData << ite->second;
        mysql_real_escape_string(&mysqlInstance, chunk, (char *)&ssData[0], data_size);
        const char *const st =  "insert into " RDBTABLE_VIRTUALACCOUNT " VALUES('%s', '%s', '%s')";
        const auto st_len = strlen(st);
        char query[st_len + 2*data_size + 1];
        auto len = snprintf(query, st_len + 2*data_size + 1, st, ite->second.virtualAcountData.strSafeAddress.c_str(),
            ite->second.virtualAcountData.strVirtualAccountName.c_str(), chunk);
        if (mysql_real_query(&mysqlInstance, query, len)!=0)
        {
            errorIntoMySQL();
            closeMySQL();
            return false;
        }
    }
    closeMySQL();
    return true;
}

bool CDBBaseImpl::Read_VirtualAccountInfo_BySQL(const DBQueryType type, const std::string& key, CVirtualAccountId_Accountinfo_IndexValue& virtualAccountInfo)
{
    if (!connectMySQL(RDBSERVER, RDBUSERNAME, RDBPASSWD, RDBNAME, RDBPORT)) {
        closeMySQL();
        return false;
    }

    string query;
    if (type == DBQueryType::SAFE_ADDRESS)
        query = "select accountinfo from " RDBTABLE_VIRTUALACCOUNT " where safeaddress='" + key + "'";
    else
        query = "select accountinfo from " RDBTABLE_VIRTUALACCOUNT " where name='" + key + "'";
    if (mysql_query(&mysqlInstance, query.c_str()) != 0)
    {
        errorIntoMySQL();
        closeMySQL();
        return false;
    }
    MYSQL_RES *result = mysql_store_result(&mysqlInstance);

    if (result == NULL)
    {
        errorIntoMySQL();
        closeMySQL();
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    unsigned long *lengths = mysql_fetch_lengths(result);

    if (lengths == NULL)
    {
        errorIntoMySQL();
        closeMySQL();
        return false;
    }
    CDataStream ssData(SER_DISK, CLIENT_VERSION);

    try {
        CDataStream ssValue(row[0], row[0] + *lengths, SER_DISK, CLIENT_VERSION);
        ssValue >> virtualAccountInfo;
    } catch (const std::exception&) {
        closeMySQL();
        return false;
    }
    closeMySQL();
    return true;
}

bool CDBBaseImpl::Read_VirtualAccountInfoList_BySQL(const std::string& name, std::map<std::string, CVirtualAccountId_Accountinfo_IndexValue>& mVirtualAccountInfo, const int limit)
{
    if (!connectMySQL(RDBSERVER, RDBUSERNAME, RDBPASSWD, RDBNAME, RDBPORT)) {
        return false;
    }

    string query = "select accountinfo from " RDBTABLE_VIRTUALACCOUNT " where name >= '" + name + "' limit " + to_string(limit);
    if (mysql_query(&mysqlInstance, query.c_str()) != 0)
    {
        errorIntoMySQL();
        closeMySQL();
        return false;
    }

    MYSQL_RES *result = mysql_store_result(&mysqlInstance);

    if (result == NULL)
    {
        errorIntoMySQL();
        closeMySQL();
        return false;
    }
    int row=mysql_num_rows(result);
    int field=mysql_num_fields(result);
    if (field != 1)
    {
        errorIntoMySQL();
        closeMySQL();
        return false;
    }

    MYSQL_ROW line=NULL;
    line=mysql_fetch_row(result);
    unsigned long *lengths = mysql_fetch_lengths(result);

    for( int i = 0; i < row; ++i )
    {
        try {
            if (line[0])
            {
                CVirtualAccountId_Accountinfo_IndexValue data;
                CDataStream ssValue(line[0], line[0] + *lengths, SER_DISK, CLIENT_VERSION);
                ssValue >> data;
                mVirtualAccountInfo.insert(make_pair(data.virtualAcountData.strVirtualAccountName, data));
            }
        } catch (const std::exception&) {
            closeMySQL();
            return false;
        }
        line=mysql_fetch_row(result);
        lengths = mysql_fetch_lengths(result);
    }
    closeMySQL();
    return true;
}

bool CDBBaseImpl::Erase_VirtualAccountInfo_BySQL(const std::vector<std::pair<uint256, CVirtualAccountId_Accountinfo_IndexValue> >& vect)
{
    if (!connectMySQL(RDBSERVER, RDBUSERNAME, RDBPASSWD, RDBNAME, RDBPORT)) {
        closeMySQL();
        return false;
    }

    for (auto ite = vect.begin(); ite != vect.end(); ++ite) {
        string query = "delete from " RDBTABLE_VIRTUALACCOUNT " where safeaddress='" + ite->second.virtualAcountData.strSafeAddress + "'";
        if (mysql_query(&mysqlInstance, query.c_str()) != 0) {
            errorIntoMySQL();
            closeMySQL();
            return false;
        }
    }

    closeMySQL();
    return true;
}