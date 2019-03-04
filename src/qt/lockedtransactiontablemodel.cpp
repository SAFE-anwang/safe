#include "lockedtransactiontablemodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "transactionrecord.h"
#include "walletmodel.h"
#include "addresstablemodel.h"
#include "main.h"

static int column_alignments_for_locked[] = {
        Qt::AlignLeft|Qt::AlignVCenter, /* status */
        Qt::AlignLeft|Qt::AlignVCenter, /* watchonly */
        Qt::AlignLeft|Qt::AlignVCenter, /* date */
        Qt::AlignLeft|Qt::AlignVCenter, /* type */
        Qt::AlignLeft|Qt::AlignVCenter, /* assets name */
        Qt::AlignLeft|Qt::AlignVCenter, /* address */
        Qt::AlignLeft|Qt::AlignVCenter, /* locked month */
        Qt::AlignLeft|Qt::AlignVCenter, /* unlocked height */
        Qt::AlignLeft|Qt::AlignVCenter, /* locked status */
        Qt::AlignRight|Qt::AlignVCenter /* amount */
    };

LockedTransactionTableModel::LockedTransactionTableModel(const PlatformStyle *platformStyle, CWallet *wallet, int showType, WalletModel *parent):
    TransactionTableModel(platformStyle,wallet,showType,parent)
{
    columns.clear();
    columns << QString() << QString() << tr("Date") << tr("Type") <<  tr("Assets name") << tr("Address / Label")  << tr("Locked Month") << tr("Unlocked Height") << tr("Locked Status") << BitcoinUnits::getAmountColumnTitle(walletModel->getOptionsModel()->getDisplayUnit());

    columnStatus = LockedTransactionTableModel::LockedColumnStatus;
    columnToAddress = LockedTransactionTableModel::LockedColumnToAddress;
    columnAmount = LockedTransactionTableModel::LockedColumnAmount;
}

LockedTransactionTableModel::~LockedTransactionTableModel()
{

}

QVariant LockedTransactionTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    TransactionRecord *rec = static_cast<TransactionRecord*>(index.internalPointer());

    if(!rec->bLocked)
        return QVariant();

    switch(role)
    {
    case RawDecorationRole:
        switch(index.column())
        {
        case LockedColumnStatus:
            return txStatusDecoration(rec);
        case LockedColumnWatchonly:
            return txWatchonlyDecoration(rec);
        case LockedColumnToAddress:
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
        case LockedColumnDate:
            return formatTxDate(rec);
        case LockedColumnType:
            return formatTxType(rec);
        case LockedColumnAssetsName:
            return formatAssetsName(rec);
        case LockedColumnToAddress:
            return formatTxToAddress(rec, false);
        case LockedColumnLockedMonth:
            return formatLockedMonth(rec);
        case LockedColumnUnlockedHeight:
            return formatUnlockedHeight(rec);
        case LockedColumnLockedStatus:
            return formatLockedStatus(rec);
        case LockedColumnAmount:
            return formatLockedTxAmount(rec, true, BitcoinUnits::separatorAlways);
        }
        break;
    }
    case Qt::EditRole:
    {
        // Edit role is used for sorting, so return the unformatted values
        switch(index.column())
        {
        case LockedColumnStatus:
            return QString::fromStdString(rec->status.sortKey);
        case LockedColumnDate:
            return rec->time;
        case LockedColumnType:
            return formatTxType(rec);
        case LockedColumnAssetsName:
            return formatAssetsName(rec);
        case LockedColumnWatchonly:
            return (rec->involvesWatchAddress ? 1 : 0);
        case LockedColumnToAddress:
            return formatTxToAddress(rec, true);
        case LockedColumnAmount:
            if(rec->bSAFETransaction)
                return qint64(rec->nLockedAmount);
            return qint64(rec->commonData.nAmount);
        case LockedColumnLockedMonth:
            return formatLockedMonth(rec);
        case LockedColumnUnlockedHeight:
            return qint64(rec->nUnlockedHeight);
        case LockedColumnLockedStatus:
            return formatLockedStatus(rec);
        }
    }
    case Qt::ToolTipRole:
        return formatTooltip(rec);
    case Qt::TextAlignmentRole:
    {
        return column_alignments_for_locked[index.column()];
    }
    case Qt::ForegroundRole:
    {
        // Use the "danger" color for abandoned transactions
        if(index.column() == LockedColumnToAddress)
            return addressColor(rec);
        if(index.column() == LockedColumnLockedStatus &&  rec->status.status == TransactionStatus::Conflicted)
            return COLOR_UNCONFIRMED;
        if(index.column() == LockedColumnLockedStatus && rec->nUnlockedHeight > g_nChainHeight)
            return COLOR_NEGATIVE;
        if(index.column() == LockedColumnLockedStatus && rec->nUnlockedHeight <= g_nChainHeight)
            return QColor(0, 128, 0);
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
        return priv->describe(rec, walletModel->getOptionsModel()->getDisplayUnit(),!rec->bSAFETransaction);
    case AddressRole:
        return QString::fromStdString(rec->address);
    case LabelRole:
        return walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));
    case AmountRole:
    {
        return qint64(rec->nLockedAmount);
    }
    case AssetsAmountRole:
        return qint64(rec->commonData.nAmount);
    case AmountUnitRole:
    {
        if(rec->bSAFETransaction)
            return QString("SAFE");
        return QString::fromStdString(rec->assetsData.strAssetUnit);
    }
    case SAFERole:
    {
        return rec->bSAFETransaction;
    }
    case AssetsDecimalsRole:
        return rec->assetsData.nDecimals;
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
            details.append(formatLockedTxAmount(rec, false, BitcoinUnits::separatorNever));
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

QVariant LockedTransactionTableModel::headerData(int section, Qt::Orientation orientation, int role) const
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
			if (section >= 0 && section < sizeof(column_alignments_for_locked) / sizeof(int))
			{
				return column_alignments_for_locked[section];
			}
        }
        else if (role == Qt::ToolTipRole)
        {
            switch(section)
            {
            case LockedColumnStatus:
                return tr("Locked transaction status. Hover over this field to show number of confirmations.");
            case LockedColumnWatchonly:
                return tr("Whether or not a watch-only address is involved in this locked transaction.");
            case LockedColumnDate:
                return tr("Date and time that the locked transaction was received.");
            case LockedColumnType:
                return tr("Type of locked transaction.");
            case LockedColumnToAddress:
                return tr("User-defined intent/purpose of the locked transaction.");
            case LockedColumnLockedMonth:
                return tr("Locked month of transaction.");
            case LockedColumnUnlockedHeight:
                return tr("Unlocked height of transaction.");
            case LockedColumnLockedStatus:
                return tr("Current locked status: locking or unlocked.");
            case LockedColumnAmount:
                return tr("Amount removed from or added to locked balance.");
            }
        }
    }
    return QVariant();
}


