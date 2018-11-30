#ifndef VIRTUAL_ACCOUNT_H
#define VIRTUAL_ACCOUNT_H 

#include "hash.h"
#include "uint256.h"
#include "serialize.h"
#include "../app/app.h"

#define MIN_VIRTAUL_ACCOUNT_NAME_SIZE        3
#define MAX_VIRTAUL_ACCOUNT_NAME_SIZE        13

class CVirtualAccountData 
{
public:
    std::string     strSafeAddress;
    std::string     strVirtualAccountName;
    std::string     owner;
    std::string     active;

    CVirtualAccountData()
    {
        SetNull();
    }

    CVirtualAccountData(const std::string& strSafeAddressIn,
        const std::string& strVirtualAccountNameIn, const std::string &ownerIn, const std::string &activeIn)
        : strVirtualAccountName(strVirtualAccountNameIn),
          strSafeAddress(strSafeAddressIn),
          owner(ownerIn), 
          active(activeIn) {
    }

    CVirtualAccountData& operator=(const CVirtualAccountData& data)
    {
        if(this == &data)
            return *this;

        strSafeAddress = data.strSafeAddress;
        strVirtualAccountName = data.strVirtualAccountName;
        owner = data.owner;
        active = data.active;
        return *this;
    }

    void SetNull()
    {
        strVirtualAccountName.clear();
        strSafeAddress.clear();
        owner.clear();
        active.clear();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(LIMITED_STRING(strSafeAddress, MAX_ADDRESS_SIZE));
        READWRITE(LIMITED_STRING(strVirtualAccountName, MAX_VIRTAUL_ACCOUNT_NAME_SIZE));
        READWRITE(LIMITED_STRING(owner, MAX_ADDRESS_SIZE));
        READWRITE(LIMITED_STRING(active, MAX_ADDRESS_SIZE));
    }

    uint256 GetHash()
    {
        return SerializeHash(*this);
    }
}; 


#endif