// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TRANSACTIONDESC_H
#define BITCOIN_QT_TRANSACTIONDESC_H

#include <QObject>
#include <QString>
#include "amount.h"

class TransactionRecord;

class CWallet;
class CWalletTx;

/** Provide a human-readable extended HTML description of a transaction.
 */
class TransactionDesc: public QObject
{
    Q_OBJECT

    enum DescColumn
    {
        DescColumnStatus,
        DescColumnDate,
        DescColumnCredit,
        DescColumnDebit,
        DescColumnNetAmount,
        DescColumnTransactionFee,
        DescColumnLockedAmount,
        DescColumnUnlockedHeight,
        DescColumnTransactionID,
        DescColumnTransactionTotalSize,
        DescColumnMessage,
        DescColumnMerchant,
        DescColumnInput,
        DescColumnComment,
        DescAppName,
        DescAppId,
        DescAppManagerAddress,
        DescAppDesc,
        DescAppDevType,
        DescAppDevName,
        DescAppWebUrl,
        DescAppLogoUrl,
        DescAppCoverUrl,
        DescAssetsId,
        DescAssetsName,
        DescAssetsShortName,
        DescAssetsUnit,
        DescAssetsTotalAmount,
        DescAssetsFirstIssueAmount,
        DescAssetsActualAmount,
        DescAssetsDecimals,
        DescAssetsDesc,
        DescAssetsRemarks,
        DescAssetsDestory,
        DescAssetsPayCandy,
        DescAssetsCandyAmount,
        DescAssetsCandyExpired,        
        DescCommonAssetsId,
        DescCommonAssetsAmount,
        DescCommonAssetsRemark,
        DescGetCandyAssetId,
        DescGetCandyAddress,
        DescGetCandyAmount,
        DescGetCandyRemark
    };

public:
    static QString toHTML(CWallet *wallet, CWalletTx &wtx, TransactionRecord *rec, int unit,int showType,bool fAssets=false);

private:
    TransactionDesc() {}

    static bool needDisplay(int descColumn,int showType,int type=0);

    static QString FormatTxStatus(const CWalletTx& wtx);
};

#endif // BITCOIN_QT_TRANSACTIONDESC_H
