#ifndef ASSETSREGISTRECORDMODEL_H
#define ASSETSREGISTRECORDMODEL_H

#include "transactiontablemodel.h"

class ApplicationsRegistRecordModel:public TransactionTableModel
{
    Q_OBJECT

public:
    explicit ApplicationsRegistRecordModel(const PlatformStyle *platformStyle, CWallet* wallet, int showType, WalletModel *parent = 0);
    ~ApplicationsRegistRecordModel();

    enum ApplicationsRegistRecordColumnIndex {
        ApplicationsRegistColumnStatus = 0,
        ApplicationsRegistColumnWatchonly = 1,
        ApplicationsRegistColumnDate = 2,
        ApplicationsRegistColumnApplicationId=3,
        ApplicationsRegistColumnManagerAddress=4
    };

    virtual QVariant data(const QModelIndex &index, int role) const;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const;
};

#endif // ASSETSREGISTRECORDMODEL_H
