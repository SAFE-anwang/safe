#ifndef CANDYTABLEMODEL_H
#define CANDYTABLEMODEL_H

#include "transactiontablemodel.h"

class CandyTableModel:public TransactionTableModel
{
    Q_OBJECT

public:
    explicit CandyTableModel(const PlatformStyle *platformStyle, CWallet* wallet, int showType, WalletModel *parent = 0);
    ~CandyTableModel();

    enum CandyColumnIndex {
        CandyColumnStatus = 0,
        CandyColumnWatchonly = 1,
        CandyColumnDate = 2,
        CandyColumnAssetsName=3,
        CandyColumnToAddress=4,
        CandyColumnAmount = 5
    };

    virtual QVariant data(const QModelIndex &index, int role) const;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const;
};

#endif // CANDYTABLEMODEL_H
