// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2019 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewentry.h"
#include "ui_overviewentry.h"

#include "guiutil.h"
#include "platformstyle.h"
#include "optionsmodel.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslConfiguration>

OverViewEntry::OverViewEntry(const PlatformStyle *platformStyle, QWidget *parent, const QString &assetName, const CAmount &balance, const CAmount &unconfirmedBalance, const CAmount &lockedAmount, const QString &strUnit, int nDecimals, const QString &logoURL):
    QStackedWidget(parent),
    ui(new Ui::OverViewEntry),
    model(0),
    platformStyle(platformStyle),
    strAssetName(assetName),
    balance(balance),
    unconfirmedBalance(unconfirmedBalance),
    lockedBalance(lockedAmount),
    strAssetUnit(strUnit),
    nDecimals(nDecimals),
    logoURL(logoURL)
{
    ui->setupUi(this);

    setCurrentWidget(ui->OverView);

    //ui->labelOtherTotal->setFont(GUIUtil::fixedPitchFont());

    pixmapWidth = 40;
    pixmapHeight = 40;
    manager = new QNetworkAccessManager(this);
    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(replyFinished(QNetworkReply*)));
    QNetworkRequest request;
    if(!logoURL.isEmpty())
    {
        request.setUrl(QUrl(logoURL));
        QSslConfiguration config = QSslConfiguration::defaultConfiguration();
        config.setProtocol(QSsl::TlsV1_0);
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        request.setSslConfiguration(config);
        manager->get(request);
    }else
    {
        QString theme = GUIUtil::getThemeName();
        ui->iconLabel->setPixmap(QIcon(":/icons/" + theme + "/about").pixmap(30,30));
    }

    int indent = 20;
    ui->labelOtherAvailable->setIndent(indent);
    ui->labelOtherPending->setIndent(indent);
    ui->labelOtherLocked->setIndent(indent);
    ui->labelOtherTotal->setIndent(indent);
    ui->assetsLabel->setText(GUIUtil::HtmlEscape2(assetName));
    setMouseTracking(true);
}

OverViewEntry::~OverViewEntry()
{
    delete ui;
}

void OverViewEntry::replyFinished(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError)
    {
        QByteArray bytes = reply->readAll();
        QString url = reply->url().toString();
        int index = url.lastIndexOf(".");
        QString type = "";
        if(index>0){
            type = url.right(url.size()-index-1).trimmed();
        }
        QPixmap pixmap;
        pixmap.loadFromData(bytes,type.toStdString().c_str());
        ui->iconLabel->setPixmap(QIcon(pixmap).pixmap(pixmapHeight,pixmapHeight));
    }
    else
    {
        LogPrintf("OverViewEntry:reply error: %s\n",reply->errorString().toStdString());
    }
}

void OverViewEntry::setAssetsInfo(CAmount &amount, CAmount &unconfirmAmount, CAmount &lockedAmount, int &nDecimals, QString &strUnit)
{
    this->balance = amount;
    this->unconfirmedBalance = unconfirmAmount;
    this->lockedBalance = lockedAmount;
    this->nDecimals = nDecimals;
    this->strAssetUnit = strUnit;
}

void OverViewEntry::updateAssetsInfo()
{
    ui->labelOtherAvailable->setText(QString("%1 %2").arg(BitcoinUnits::format(nDecimals,balance,false,BitcoinUnits::separatorAlways,true)).arg(strAssetUnit));
    ui->labelOtherTotal->setText(QString("%1 %2").arg(BitcoinUnits::format(nDecimals,balance+unconfirmedBalance+lockedBalance,false,BitcoinUnits::separatorAlways,true)).arg(strAssetUnit));
    ui->labelOtherPending->setText(QString("%1 %2").arg(BitcoinUnits::format(nDecimals,unconfirmedBalance,false,BitcoinUnits::separatorAlways,true)).arg(strAssetUnit));
    ui->labelOtherLocked->setText(QString("%1 %2").arg(BitcoinUnits::format(nDecimals,lockedBalance,false,BitcoinUnits::separatorAlways,true)).arg(strAssetUnit));
}

void OverViewEntry::setModel(WalletModel *model)
{
    this->model = model;

    if (model && model->getOptionsModel())
    {
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateAssetsInfo();
    }
    clear();
}

void OverViewEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        updateAssetsInfo();
    }
}

const QString& OverViewEntry::getAssetName()const
{
    return strAssetName;
}

void OverViewEntry::deleteClicked()
{

}

void OverViewEntry::clear()
{

}

void OverViewEntry::setFocus()
{

}
