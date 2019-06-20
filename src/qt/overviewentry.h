// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OVERVIEWENTRY_H
#define OVERVIEWENTRY_H

#include "walletmodel.h"
#include "app/app.h"

#include <QStackedWidget>

class WalletModel;
class PlatformStyle;
class QNetworkAccessManager;
class QNetworkReply;

namespace Ui {
    class OverViewEntry;
}

class OverViewEntry : public QStackedWidget
{
    Q_OBJECT

public:
    explicit OverViewEntry(const PlatformStyle *platformStyle, QWidget *parent,const QString& assetName,const CAmount &balance, const CAmount &unconfirmedBalance
                           , const CAmount &lockedBalance,const QString& strUnit,int nDecimal,const QString& logoURL="");
    ~OverViewEntry();

    void setModel(WalletModel *model);

    void setFocus();

    const QString& getAssetName()const;

    void setAssetsInfo(CAmount& amount,CAmount& unconfirmAmount,CAmount& lockedBalance,int& nDecimals,QString& strUnit);
    void updateAssetsInfo();

public Q_SLOTS:
    void clear();
    void replyFinished(QNetworkReply* reply);

Q_SIGNALS:

    void payAmountChanged();
    void subtractFeeFromAmountChanged();

private Q_SLOTS:
    void deleteClicked();
    void updateDisplayUnit();

private:
    SendCoinsRecipient recipient;
    Ui::OverViewEntry *ui;
    WalletModel *model;
    const PlatformStyle *platformStyle;
    QString strAssetName;
    CAmount balance;
    CAmount unconfirmedBalance;
    CAmount lockedBalance;
    QString strAssetUnit;
    int nDecimals;
    QString logoURL;
    QNetworkAccessManager* manager;
    int pixmapWidth;
    int pixmapHeight;
};


#endif // OVERVIEWENTRY_H



