#ifndef ASSETSDISTRIBUTERECORDMODEL_H
#define ASSETSDISTRIBUTERECORDMODEL_H

#include "transactiontablemodel.h"

class AssetsDistributeRecordModel:public TransactionTableModel
{
    Q_OBJECT

public:
    explicit AssetsDistributeRecordModel(const PlatformStyle *platformStyle, CWallet* wallet, int showType, WalletModel *parent = 0);
    ~AssetsDistributeRecordModel();

    enum AssetsDistributeRecordColumnIndex {
        AssetsDistributeColumnStatus = 0,
        AssetsDistributeColumnWatchonly = 1,
        AssetsDistributeColumnDate = 2,
        AssetsDistributeColumnAssetsName=3,
        AssetsDistributeColumnType=4,
        AssetsDistributeColumnToAddress=5,
        AssetsDistributeColumnAmount = 6
    };

    virtual QVariant data(const QModelIndex &index, int role) const;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const;
};

#endif // ASSETSDISTRIBUTERECORDMODEL_H
