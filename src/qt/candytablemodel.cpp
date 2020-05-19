#include "candytablemodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "transactionrecord.h"
#include "walletmodel.h"
#include "addresstablemodel.h"

static int column_alignments_for_candy[] = {
        Qt::AlignLeft|Qt::AlignVCenter, /* status */
        Qt::AlignLeft|Qt::AlignVCenter, /* watchonly */
        Qt::AlignLeft|Qt::AlignVCenter, /* date */
        Qt::AlignLeft|Qt::AlignVCenter, /* assets name */
        Qt::AlignLeft|Qt::AlignVCenter, /* address */
        Qt::AlignRight|Qt::AlignVCenter, /* amount */
    };

CandyTableModel::CandyTableModel(const PlatformStyle *platformStyle, CWallet *wallet, int showType, WalletModel *parent):
    TransactionTableModel(platformStyle,wallet,showType,parent)
{
    columns.clear();
    columns << QString() << QString() << tr("Date") <<  tr("Assets name") << tr("Address / Label")  << tr("Assets amount");

    columnStatus = CandyTableModel::CandyColumnStatus;
    columnToAddress = CandyTableModel::CandyColumnToAddress;
    columnAmount = CandyTableModel::CandyColumnAmount;
}

CandyTableModel::~CandyTableModel()
{

}

QVariant CandyTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    TransactionRecord *rec = static_cast<TransactionRecord*>(index.internalPointer());

    switch(role)
    {
    case RawDecorationRole:
        switch(index.column())
        {
        case CandyColumnStatus:
            return txStatusDecoration(rec);
        case CandyColumnWatchonly:
            return txWatchonlyDecoration(rec);
        case CandyColumnToAddress:
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
        case CandyColumnDate:
            return formatTxDate(rec);
        case CandyColumnAssetsName:
            return formatAssetsName(rec);
        case CandyColumnToAddress:
            return formatAssetsAddress(rec);
        case CandyColumnAmount:
            return formatCandyAmount(rec, true, BitcoinUnits::separatorAlways);
        }
        break;
    }
    case Qt::EditRole:
    {
        // Edit role is used for sorting, so return the unformatted values
        switch(index.column())
        {
        case CandyColumnStatus:
            return QString::fromStdString(rec->status.sortKey);
        case CandyColumnDate:
            return rec->time;
        case CandyColumnAssetsName:
            return formatAssetsName(rec);
        case CandyColumnToAddress:
            return formatAssetsAddress(rec);
        case CandyColumnAmount:
            return formatCandyAmount(rec, true, BitcoinUnits::separatorAlways);
        }
        break;
    }
    case Qt::ToolTipRole:
        return formatTooltip(rec);
    case Qt::TextAlignmentRole:
    {
        return column_alignments_for_candy[index.column()];
    }
    case Qt::ForegroundRole:
    {
        // Use the "danger" color for abandoned transactions
        if(index.column() == CandyColumnToAddress)
            return addressColor(rec);
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
    case LabelRole:
        return walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));
    case AmountRole:
        return qint64(rec->getCandyData.nAmount);
    case AssetsAmountRole:
        return qint64(rec->getCandyData.nAmount);
    case AssetsNameRole:
        return formatAssetsName(rec);
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
        return formatCandyAmount(rec, false, BitcoinUnits::separatorNever);
    }
    case AmountUnitRole:
    {
        if(rec->bSAFETransaction)
            return QString("SAFE");
        return QString::fromStdString(rec->assetsData.strAssetUnit);
    }
    case SAFERole:
        return rec->bSAFETransaction;
    case AssetsDecimalsRole:
        return rec->assetsData.nDecimals;
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

QVariant CandyTableModel::headerData(int section, Qt::Orientation orientation, int role) const
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
			if (section >= 0 && section < sizeof(column_alignments_for_candy) / sizeof(int))
			{
				return column_alignments_for_candy[section];
			}
        }
        else if (role == Qt::ToolTipRole)
        {
            switch(section)
            {
            case CandyColumnDate:
                return tr("Date and time that the candy was received.");
            case CandyColumnAssetsName:
                return tr("The Assets name of the candy.");
            case CandyColumnToAddress:
                return tr("User-defined intent/purpose of the candy receive.");
            case CandyColumnAmount:
                return tr("Amount removed from or added to balance.");
            }
        }
    }
    return QVariant();
}

