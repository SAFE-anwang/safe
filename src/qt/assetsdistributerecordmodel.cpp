#include "assetsdistributerecordmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "transactionrecord.h"
#include "walletmodel.h"
#include "addresstablemodel.h"

static int column_alignments_for_assets_distribute[] = {
        Qt::AlignLeft|Qt::AlignVCenter, /* status */
        Qt::AlignLeft|Qt::AlignVCenter, /* watchonly */
        Qt::AlignLeft|Qt::AlignVCenter, /* date */
        Qt::AlignLeft|Qt::AlignVCenter, /* assets name */
        Qt::AlignLeft|Qt::AlignVCenter, /* type */
        Qt::AlignLeft|Qt::AlignVCenter, /* address */
        Qt::AlignRight|Qt::AlignVCenter, /* amount */
    };

AssetsDistributeRecordModel::AssetsDistributeRecordModel(const PlatformStyle *platformStyle, CWallet *wallet, int showType,WalletModel *parent):
    TransactionTableModel(platformStyle,wallet,showType,parent)
{
    columns.clear();
    columns << QString() << QString() << tr("Date") <<  tr("Assets name") << tr("Type") << tr("Address / Label")  <<  BitcoinUnits::getAmountColumnTitle(walletModel->getOptionsModel()->getDisplayUnit());

    columnStatus = AssetsDistributeRecordModel::AssetsDistributeColumnStatus;
    columnToAddress = AssetsDistributeRecordModel::AssetsDistributeColumnToAddress;
    columnAmount = AssetsDistributeRecordModel::AssetsDistributeColumnAmount;
}

AssetsDistributeRecordModel::~AssetsDistributeRecordModel()
{

}

QVariant AssetsDistributeRecordModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    TransactionRecord *rec = static_cast<TransactionRecord*>(index.internalPointer());

    switch(role)
    {
    case RawDecorationRole:
        switch(index.column())
        {
        case AssetsDistributeColumnStatus:
            return txStatusDecoration(rec);
        case AssetsDistributeColumnWatchonly:
            return txWatchonlyDecoration(rec);
        case AssetsDistributeColumnToAddress:
            return txAddressDecoration(rec);
        }
        break;
    case Qt::DecorationRole:
    {
        return qvariant_cast<QIcon>(index.data(RawDecorationRole));
    }
    case Qt::DisplayRole:
    {
        switch(index.column())
        {
        case AssetsDistributeColumnDate:
            return formatTxDate(rec);
        case AssetsDistributeColumnAssetsName:
            return formatAssetsName(rec);
        case AssetsDistributeColumnToAddress:
            return formatAssetsAddress(rec);
        case AssetsDistributeColumnType:
            return formatAssetsDistributeType(rec);
        case AssetsDistributeColumnAmount:
            return formatAssetsAmount(rec, true, BitcoinUnits::separatorAlways);
        }
        break;
    }
    case Qt::EditRole:
    {
        // Edit role is used for sorting, so return the unformatted values
        switch(index.column())
        {
        case AssetsDistributeColumnStatus:
            return QString::fromStdString(rec->status.sortKey);
        case AssetsDistributeColumnWatchonly:
            return (rec->involvesWatchAddress ? 1 : 0);
        case AssetsDistributeColumnDate:
            return rec->time;
        case AssetsDistributeColumnAssetsName:
            return formatAssetsName(rec);
        case AssetsDistributeColumnToAddress:
            return formatAssetsAddress(rec);
        case AssetsDistributeColumnType:
            return formatAssetsDistributeType(rec);
        case AssetsDistributeColumnAmount:
            return formatAssetsAmount(rec, true, BitcoinUnits::separatorAlways);;
        }
        break;
    }
    case Qt::ToolTipRole:
        return formatTooltip(rec);
    case Qt::TextAlignmentRole:
    {
        return column_alignments_for_assets_distribute[index.column()];
    }
    case Qt::ForegroundRole:
    {
        // Use the "danger" color for abandoned transactions
        if(index.column() == AssetsDistributeRecordModel::AssetsDistributeColumnToAddress)
            return addressColor(rec);
        if(rec->status.status == TransactionStatus::Conflicted)
            return COLOR_UNCONFIRMED;
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
        return priv->describe(rec, walletModel->getOptionsModel()->getDisplayUnit(),true);
    case AddressRole:
        return formatAssetsAddress(rec);
    case LabelRole:
        return walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));
    case AmountRole:
        return 0;
    case AssetsAmountRole:
    {
        if(rec->type == TransactionRecord::FirstDistribute)
            return qint64(rec->assetsData.nFirstIssueAmount);
        else if(rec->type == TransactionRecord::AddDistribute)
            return qint64(rec->commonData.nAmount);
    }
    case AmountUnitRole:
        return QString::fromStdString(rec->assetsData.strAssetUnit);
    case AssetsIDRole:
    {
        if(rec->type==TransactionRecord::FirstDistribute)
            return QString::fromStdString(rec->assetsData.GetHash().ToString());
        else if(rec->type == TransactionRecord::AddDistribute)
            return QString::fromStdString(rec->commonData.assetId.ToString());
    }
    case AssetsNameRole:
        return formatAssetsName(rec);
    case AssetsDecimalsRole:
        return rec->assetsData.nDecimals;
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
        return formatAssetsAmount(rec, true, BitcoinUnits::separatorNever);
    }
    case StatusRole:
        return rec->status.status;
    case Qt::FontRole:
    {
        QFont font;
        font.setPixelSize(12);
        return font;
    }
    }
    return QVariant();
}

QVariant AssetsDistributeRecordModel::headerData(int section, Qt::Orientation orientation, int role) const
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
			if (section >= 0 && sizeof(column_alignments_for_assets_distribute) / sizeof(int))
			{
				return column_alignments_for_assets_distribute[section];
			}
        }
        else if (role == Qt::ToolTipRole)
        {
            switch(section)
            {
            case AssetsDistributeColumnDate:
                return tr("Date and time that the assets was distributed.");
            case AssetsDistributeColumnAssetsName:
                return tr("Assets name of distribute.");
            case AssetsDistributeColumnType:
                return tr("Type of assets distribute.");
            case AssetsDistributeColumnToAddress:
                return tr("User-defined intent/purpose of the assets distribute.");
            case AssetsDistributeColumnAmount:
                return tr("Amount removed from or added to balance.");
            }
        }
    }
    return QVariant();
}

