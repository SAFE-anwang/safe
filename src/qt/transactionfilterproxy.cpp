// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionfilterproxy.h"

#include "transactiontablemodel.h"
#include "transactionrecord.h"
#include "math.h"
#include "utilstrencodings.h"
#include <iostream>
#include <cstdlib>

#include <QDateTime>

// Earliest date that can be represented (far in the past)
const QDateTime TransactionFilterProxy::MIN_DATE = QDateTime::fromTime_t(0);
// Last date that can be represented (far in the future)
const QDateTime TransactionFilterProxy::MAX_DATE = QDateTime::fromTime_t(0xFFFFFFFF);

TransactionFilterProxy::TransactionFilterProxy(QObject *parent) :
    QSortFilterProxyModel(parent),
    dateFrom(MIN_DATE),
    dateTo(MAX_DATE),
    addrPrefix(),
    assetsNamePrefix(),
    applicationsIdPrefix(),
    typeFilter(COMMON_TYPES),
    watchOnlyFilter(WatchOnlyFilter_All),
    minAmount(0),
    minAssetsMountStr(""),
    limitRows(-1),
    showInactive(true),
    bFilterType(true)
{
}

bool TransactionFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    QString assetsName = index.data(TransactionTableModel::AssetsNameRole).toString();
    int type = index.data(TransactionTableModel::TypeRole).toInt();
    QDateTime datetime = index.data(TransactionTableModel::DateRole).toDateTime();
    bool involvesWatchAddress = index.data(TransactionTableModel::WatchonlyRole).toBool();
    QString address = index.data(TransactionTableModel::AddressRole).toString();
    QString applicationsName = index.data(TransactionTableModel::ApplicationsNameRole).toString();
    QString applicationsId = index.data(TransactionTableModel::ApplicationsIdRole).toString();
    QString label = index.data(TransactionTableModel::LabelRole).toString();
    qint64 amount = llabs(index.data(TransactionTableModel::AmountRole).toLongLong());
    qint64 assetsAmount = llabs(index.data(TransactionTableModel::AssetsAmountRole).toLongLong());
    int status = index.data(TransactionTableModel::StatusRole).toInt();
    int decimals = index.data(TransactionTableModel::AssetsDecimalsRole).toInt();
    bool bSAFETransaction = index.data(TransactionTableModel::SAFERole).toBool();

    //input add decimal will over MAX_MONEYS or MAX_ASSETS
    CAmount minAssetsAmount = 0;
    if(!bSAFETransaction&&!minAssetsMountStr.isEmpty())
    {
        while(decimals>=0)
        {
            if(ParseFixedPoint(minAssetsMountStr.toStdString(),decimals,&minAssetsAmount))
                break;
            decimals--;
        }
    }

    if(!showInactive && status == TransactionStatus::Conflicted)
        return false;
    if(!(TYPE(type) & typeFilter)&&bFilterType)
        return false;
    if (involvesWatchAddress && watchOnlyFilter == WatchOnlyFilter_No)
        return false;
    if (!involvesWatchAddress && watchOnlyFilter == WatchOnlyFilter_Yes)
        return false;
    if(datetime < dateFrom || datetime > dateTo)
        return false;
    if (!address.contains(addrPrefix, Qt::CaseInsensitive) && !label.contains(addrPrefix, Qt::CaseInsensitive))
        return false;
    if (!applicationsId.contains(applicationsIdPrefix, Qt::CaseInsensitive) && !label.contains(applicationsIdPrefix, Qt::CaseInsensitive))
        return false;
    if (!applicationsName.contains(applicationsNamePrefix, Qt::CaseInsensitive) && !label.contains(applicationsNamePrefix, Qt::CaseInsensitive))
        return false;
    if (!assetsName.contains(assetsNamePrefix, Qt::CaseInsensitive))
        return false;
    if(bSAFETransaction)
    {
        if(amount < minAmount)
            return false;
    }else
    {
        if(assetsAmount < minAssetsAmount)
            return false;
    }

    return true;
}

void TransactionFilterProxy::setDateRange(const QDateTime &from, const QDateTime &to)
{
    this->dateFrom = from;
    this->dateTo = to;
    invalidateFilter();
}

void TransactionFilterProxy::setAddressPrefix(const QString &addrPrefix)
{
    this->addrPrefix = addrPrefix;
    invalidateFilter();
}

void TransactionFilterProxy::setAssetsNamePrefix(const QString &assetsNamePrefix)
{
    this->assetsNamePrefix = assetsNamePrefix;
    invalidateFilter();
}

void TransactionFilterProxy::setApplicationsNamePrefix(const QString &applicationsNamePrefix)
{
    this->applicationsNamePrefix = applicationsNamePrefix;
    invalidateFilter();
}

void TransactionFilterProxy::setApplicationsIdPrefix(const QString &applicationsIdPrefix)
{
    this->applicationsIdPrefix = applicationsIdPrefix;
    invalidateFilter();
}

void TransactionFilterProxy::setTypeFilter(quint32 modes)
{
    this->typeFilter = modes;
    invalidateFilter();
}

void TransactionFilterProxy::setMinAmount(const CAmount &minimum, const QString &assetsAmountStr)
{
    this->minAmount = minimum;
    this->minAssetsMountStr = assetsAmountStr;
    invalidateFilter();
}

void TransactionFilterProxy::setMinAssetsAmountStr(const QString &assetsAmountStr)
{
    this->minAssetsMountStr = assetsAmountStr;
    invalidateFilter();
}

void TransactionFilterProxy::setWatchOnlyFilter(WatchOnlyFilter filter)
{
    this->watchOnlyFilter = filter;
    invalidateFilter();
}

void TransactionFilterProxy::setFilterType(bool filterType)
{
    bFilterType = filterType;
}

void TransactionFilterProxy::setLimit(int limit)
{
    this->limitRows = limit;
}

void TransactionFilterProxy::setShowInactive(bool showInactive)
{
    this->showInactive = showInactive;
    invalidateFilter();
}

int TransactionFilterProxy::rowCount(const QModelIndex &parent) const
{
    if(limitRows != -1)
    {
        return std::min(QSortFilterProxyModel::rowCount(parent), limitRows);
    }
    else
    {
        return QSortFilterProxyModel::rowCount(parent);
    }
}
