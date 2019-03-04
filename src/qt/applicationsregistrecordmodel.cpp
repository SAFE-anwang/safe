#include "applicationsregistrecordmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "transactionrecord.h"
#include "walletmodel.h"
#include "addresstablemodel.h"

static int column_alignments_for_applications_regist[] = {
        Qt::AlignLeft|Qt::AlignVCenter, /* status */
        Qt::AlignLeft|Qt::AlignVCenter, /* watchonly */
        Qt::AlignLeft|Qt::AlignVCenter, /* date */
        Qt::AlignLeft|Qt::AlignVCenter, /* application name */
        Qt::AlignLeft|Qt::AlignVCenter, /* application id */
        Qt::AlignLeft|Qt::AlignVCenter, /* manager address */
    };

ApplicationsRegistRecordModel::ApplicationsRegistRecordModel(const PlatformStyle *platformStyle, CWallet *wallet, int showType, WalletModel *parent):
    TransactionTableModel(platformStyle,wallet,showType,parent)
{
    columns.clear();
    columns << QString() << QString() << tr("Date") << tr("Application Name") << tr("Application ID") << tr("Manager Address");
    columnStatus = ApplicationsRegistRecordModel::ApplicationsRegistColumnStatus;
    columnToAddress = -1;
    columnAmount = -1;
}

ApplicationsRegistRecordModel::~ApplicationsRegistRecordModel()
{

}

QVariant ApplicationsRegistRecordModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    TransactionRecord *rec = static_cast<TransactionRecord*>(index.internalPointer());

    switch(role)
    {
    case RawDecorationRole:
    {
        switch(index.column())
        {
        case ApplicationsRegistRecordModel::ApplicationsRegistColumnStatus:
            return txStatusDecoration(rec);
        case ApplicationsRegistRecordModel::ApplicationsRegistColumnWatchonly:
            return txWatchonlyDecoration(rec);
        }
        break;
    }
    case Qt::DecorationRole:
    {
        return qvariant_cast<QIcon>(index.data(RawDecorationRole));
    }
    case Qt::DisplayRole:
    {
        switch(index.column())
        {
        case ApplicationsRegistRecordModel::ApplicationsRegistColumnDate:
            return formatTxDate(rec);
        case ApplicationsRegistRecordModel::ApplicationsRegistColumnApplicationName:
            return QString::fromStdString(rec->appData.strAppName);
        case ApplicationsRegistRecordModel::ApplicationsRegistColumnApplicationId:
            return QString::fromStdString(rec->appData.GetHash().GetHex());
        case ApplicationsRegistRecordModel::ApplicationsRegistColumnManagerAddress:
            return formatTxToAddress(rec, false);
        }
        break;
    }
    case Qt::EditRole:
    {
        // Edit role is used for sorting, so return the unformatted values
        switch(index.column())
        {
        case ApplicationsRegistRecordModel::ApplicationsRegistColumnStatus:
            return QString::fromStdString(rec->status.sortKey);
        case ApplicationsRegistRecordModel::ApplicationsRegistColumnWatchonly:
            return (rec->involvesWatchAddress ? 1 : 0);
        case ApplicationsRegistRecordModel::ApplicationsRegistColumnDate:
            return rec->time;
        }
        break;
    }
    case Qt::ToolTipRole:
        return formatTooltip(rec);
    case Qt::TextAlignmentRole:
    {
        return column_alignments_for_applications_regist[index.column()];
    }
    case Qt::ForegroundRole:
    {
        break;
    }
    case TypeRole:
        return rec->type;
    case DateRole:
        return formatTxDate(rec);
    case WatchonlyRole:
        return rec->involvesWatchAddress;
    case WatchonlyDecorationRole:
        return txWatchonlyDecoration(rec);
    case LongDescriptionRole:
        return priv->describe(rec, walletModel->getOptionsModel()->getDisplayUnit());
    case AddressRole:
        return QString::fromStdString(rec->address);
    case ApplicationsNameRole:
        return QString::fromStdString(rec->appData.strAppName);
    case ApplicationsIdRole:
        return QString::fromStdString(rec->appData.GetHash().GetHex());
    case LabelRole:
        return walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));
    case AmountRole:
    {
        return qint64(rec->nLockedAmount);
    }
    case TxIDRole:
        return rec->getTxID();
    case TxHashRole:
        return QString::fromStdString(rec->hash.ToString());
    case TxHexRole:
        return priv->getTxHex(rec);
    case TxPlainTextRole:
        {
            QString details;
            QString txLabel = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));

            details.append(formatTxDate(rec));
            details.append(" ");
            details.append(formatTxStatus(rec));
            details.append(". ");
            if(!formatTxType(rec).isEmpty()) {
                details.append(formatTxType(rec));
                details.append(" ");
            }
            if(!rec->address.empty()) {
                if(txLabel.isEmpty())
                    details.append(tr("(no label)") + " ");
                else {
                    details.append("(");
                    details.append(txLabel);
                    details.append(") ");
                }
                details.append(QString::fromStdString(rec->address));
                details.append(" ");
            }
            details.append(formatTxAmount(rec, false, BitcoinUnits::separatorNever));
            return details;
        }
    case ConfirmedRole:
        return rec->status.countsForBalance;
    case FormattedAmountRole:
    {
        // Used for copy/export, so don't include separators
        return formatLockedTxAmount(rec, false, BitcoinUnits::separatorNever);
    }
    case StatusRole:
        return rec->status.status;
    case LockedMonthRole:
        return formatLockedMonth(rec);
    case UnlockedHeightRole:
        return formatUnlockedHeight(rec);
    case LockedStatusRole:
        return formatLockedStatus(rec);
    case Qt::FontRole:
    {
        QFont font;
        font.setPixelSize(12);
        return font;
    }
    }
    return QVariant();
}

QVariant ApplicationsRegistRecordModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole)
        {
			if (section >= 0 && section < columns.size())
			{
				return columns[section];
			}
        }
        else if (role == Qt::TextAlignmentRole)
        {
			if (section >= 0 && section < sizeof(column_alignments_for_applications_regist) / sizeof(int))
			{
				return column_alignments_for_applications_regist[section];
			}
        }
        else if (role == Qt::ToolTipRole)
        {
            switch(section)
            {
            case ApplicationsRegistColumnDate:
                return tr("Date and time that the application was registed.");
            case ApplicationsRegistColumnApplicationName:
                return tr("Application Name of assets regist.");
            case ApplicationsRegistColumnApplicationId:
                return tr("Application ID of assets regist.");
            case ApplicationsRegistColumnManagerAddress:
                return tr("Manager address of assets regist.");
            }
        }
    }
    return QVariant();
}


