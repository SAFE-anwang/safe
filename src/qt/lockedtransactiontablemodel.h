#ifndef LOCKEDTRANSACTIONTABLEMODEL_H
#define LOCKEDTRANSACTIONTABLEMODEL_H

#include "transactiontablemodel.h"

class LockedTransactionTableModel:public TransactionTableModel
{
    Q_OBJECT

public:
    explicit LockedTransactionTableModel(const PlatformStyle *platformStyle, CWallet* wallet, int showType, WalletModel *parent = 0);
    ~LockedTransactionTableModel();

    enum LockedColumnIndex {
        LockedColumnStatus = 0,
        LockedColumnWatchonly = 1,
        LockedColumnDate = 2,
        LockedColumnType = 3,
        LockedColumnAssetsName = 4,
        LockedColumnToAddress = 5,
        LockedColumnLockedMonth = 6,
        LockedColumnUnlockedHeight = 7,
        LockedColumnLockedStatus = 8,
        LockedColumnAmount = 9
    };

    virtual QVariant data(const QModelIndex &index, int role) const;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const;
};

#endif // LOCKEDTRANSACTIONTABLEMODEL_H
