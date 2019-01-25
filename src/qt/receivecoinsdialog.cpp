// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "receivecoinsdialog.h"
#include "ui_receivecoinsdialog.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "receiverequestdialog.h"
#include "recentrequeststablemodel.h"
#include "walletmodel.h"
#include "transactiontablemodel.h"
#include "validation.h"
#include "init.h"
#include "clientmodel.h"

#include <string>
#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>
#include <QCompleter>
#include <boost/foreach.hpp>
using std::string;
extern QString gStrSafe;

ReceiveCoinsDialog::ReceiveCoinsDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ReceiveCoinsDialog),
    columnResizingFixer(0),
    clientModel(0),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
    QString theme = GUIUtil::getThemeName();

    if (!platformStyle->getImagesOnButtons()) {
        ui->clearButton->setIcon(QIcon());
        ui->receiveButton->setIcon(QIcon());
        ui->showRequestButton->setIcon(QIcon());
        ui->removeRequestButton->setIcon(QIcon());
    } else {
        ui->clearButton->setIcon(QIcon(":/icons/" + theme + "/remove"));
        ui->receiveButton->setIcon(QIcon(":/icons/" + theme + "/receiving_addresses"));
        ui->showRequestButton->setIcon(QIcon(":/icons/" + theme + "/edit"));
        ui->removeRequestButton->setIcon(QIcon(":/icons/" + theme + "/remove"));
    }

    // context menu actions
    QAction *copyURIAction = new QAction(tr("Copy URI"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyMessageAction = new QAction(tr("Copy message"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyURIAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyMessageAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->setStyleSheet("font-size:12px;");

    // context menu signals
    connect(ui->recentRequestsView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
    connect(copyURIAction, SIGNAL(triggered()), this, SLOT(copyURI()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyMessageAction, SIGNAL(triggered()), this, SLOT(copyMessage()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));

    connect(ui->assetsComboBox,SIGNAL(currentIndexChanged(const QString&)),this,SLOT(updateCurrentAsset(const QString&)));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(ui->reqLabel, SIGNAL(textChanged(QString)), this, SLOT(on_reqLabel_textChanged(QString)));
    connect(ui->reqMessage, SIGNAL(textChanged(QString)), this, SLOT(on_reqMessage_textChanged(QString)));
    fAssets = false;
    assetDecimal = 0;
    strAssetUnit = "";

    completer = new QCompleter;
    stringListModel = new QStringListModel;
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setModel(stringListModel);
    completer->popup()->setStyleSheet("font: 12px;");
    ui->assetsComboBox->setCompleter(completer);
    ui->assetsComboBox->setEditable(true);
    ui->assetsComboBox->setStyleSheet("QComboBox{background-color:#FFFFFF;border:1px solid #82C3E6;font-size:12px;}");
    ui->assetsComboBox->addItem(gStrSafe);
    setMouseTracking(true);
    ui->frame->setMouseTracking(true);
    ui->frame2->setMouseTracking(true);

    QRegExp regExpReqLabelEdit;
    regExpReqLabelEdit.setPattern("[a-zA-Z\u4e00-\u9fa5][a-zA-Z0-9-+*/。，$%^&*,!?.()#_\u4e00-\u9fa5 ]{1,150}");
    ui->reqLabel->setValidator (new QRegExpValidator(regExpReqLabelEdit, this));
    QRegExp regExpReqMessageEdit;
    regExpReqMessageEdit.setPattern("[a-zA-Z\u4e00-\u9fa5][a-zA-Z0-9-+*/。，$%^&*,!?.()#_\u4e00-\u9fa5 ]{1,150}");
    ui->reqMessage->setValidator (new QRegExpValidator(regExpReqMessageEdit, this));
    initWidget();
}

void ReceiveCoinsDialog::initWidget()
{
#if QT_VERSION >= 0x040700
    ui->reqLabel->setPlaceholderText(tr("Maximum 30 characters"));
    ui->reqMessage->setPlaceholderText(tr("Maximum 150 characters"));
#endif
}

ReceiveCoinsDialog::~ReceiveCoinsDialog()
{
    delete ui;
}

void ReceiveCoinsDialog::updateCurrentAsset(const QString &currText)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    if(currText==gStrSafe)
    {
        fAssets = false;
    }
    else
    {
        fAssets = true;
        if(!assetDataMap.contains(currText))
            return;
    }
    ui->checkUseInstantSend->setVisible(!fAssets);
    if(fAssets)
    {
        if(assetDataMap.contains(currText))
        {
            CAssetData& assetData = assetDataMap[currText];
            assetDecimal = assetData.nDecimals;
            strAssetUnit = QString::fromStdString(assetData.strAssetUnit);
        }
    }
    ui->reqAmount->updateAssetUnit(strAssetUnit,fAssets,assetDecimal);
}

void ReceiveCoinsDialog::updateAssetsFound(const QString &assetName)
{
    updateAssetsInfo(assetName);
}

void ReceiveCoinsDialog::updateAssetsInfo(const QString &assetName)
{
//    LOCK2(cs_main, pwalletMain->cs_wallet);
    bool addOneAsset = !assetName.isEmpty();
    std::vector<uint256>  assetIdVec;
    if(addOneAsset)
    {
        uint256 assetId;
        if(!GetAssetIdByAssetName(assetName.toStdString(),assetId))
            return;
        assetIdVec.push_back(assetId);
    }else
    {
        if (!GetAssetListInfo(assetIdVec))
            return;
    }

    BOOST_FOREACH(const uint256& assetId,assetIdVec)
    {
        if(assetId.IsNull()){
            continue;
        }
        CAssetId_AssetInfo_IndexValue assetInfo;
        if(!GetAssetInfoByAssetId(assetId, assetInfo)){
            return;
        }
        QString assetName = QString::fromStdString(assetInfo.assetData.strAssetName);
        if(!assetDataMap.contains(assetName))
            assetDataMap[assetName] = assetInfo.assetData;
    }

    disconnect(ui->assetsComboBox,SIGNAL(currentIndexChanged(const QString&)),this,SLOT(updateCurrentAsset(const QString&)));
    QString currText = ui->assetsComboBox->currentText();
    ui->assetsComboBox->clear();
    QStringList stringList;
    stringList.append(gStrSafe);
    //QMap<QString,QString>& assetNamesUnits = model->getAssetsNamesUnits();
    stringList.append(assetDataMap.keys());

    stringListModel->setStringList(stringList);
    completer->setModel(stringListModel);
    completer->popup()->setStyleSheet("font: 12px;");
    ui->assetsComboBox->addItems(stringList);
    ui->assetsComboBox->setCompleter(completer);

    if(assetDataMap.contains(currText))
        ui->assetsComboBox->setCurrentText(currText);
    connect(ui->assetsComboBox,SIGNAL(currentIndexChanged(const QString&)),this,SLOT(updateCurrentAsset(const QString&)));
}

void ReceiveCoinsDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
        model->getRecentRequestsTableModel()->sort(RecentRequestsTableModel::Date, Qt::DescendingOrder);
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        QTableView* tableView = ui->recentRequestsView;

        tableView->verticalHeader()->hide();
        tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tableView->setModel(model->getRecentRequestsTableModel());
        tableView->setAlternatingRowColors(true);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);
        tableView->setColumnWidth(RecentRequestsTableModel::Date, DATE_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Label, LABEL_COLUMN_WIDTH);

        connect(tableView->selectionModel(),
            SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this,
            SLOT(recentRequestsView_selectionChanged(QItemSelection, QItemSelection)));
        // Last 2 columns are set by the columnResizingFixer, when the table geometry is ready.
        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, AMOUNT_MINIMUM_COLUMN_WIDTH, DATE_COLUMN_WIDTH, this);
    }
}

void ReceiveCoinsDialog::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel){
        connect(clientModel,SIGNAL(assetFound(QString)),this,SLOT(updateAssetsFound(QString)));
    }
}

void ReceiveCoinsDialog::clear()
{
    ui->reqAmount->clear();
    ui->reqLabel->setText("");
    ui->reqMessage->setText("");
    ui->reuseAddress->setChecked(false);
    updateDisplayUnit();
}

void ReceiveCoinsDialog::reject()
{
    clear();
}

void ReceiveCoinsDialog::accept()
{
    clear();
}

void ReceiveCoinsDialog::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        ui->reqAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

void ReceiveCoinsDialog::on_reqLabel_textChanged(const QString &address)
{
    while(ui->reqLabel->text().trimmed().toStdString().size() > 30)
        ui->reqLabel->setText(ui->reqLabel->text().left(ui->reqLabel->text().trimmed().length()-1));
}

void ReceiveCoinsDialog::on_reqMessage_textChanged(const QString &address)
{
    while(ui->reqMessage->text().trimmed().toStdString().size() > 150)
        ui->reqMessage->setText(ui->reqMessage->text().left(ui->reqMessage->text().trimmed().length()-1));
}

void ReceiveCoinsDialog::on_receiveButton_clicked()
{
    if(!model || !model->getOptionsModel() || !model->getAddressTableModel() || !model->getRecentRequestsTableModel())
        return;

    string strreqLabel =ui->reqLabel->text().trimmed().toStdString();
    if(strreqLabel.size() > 30)
    {
        QMessageBox::warning(this, tr("Receive"),tr("Label input too long"),tr("Ok"));
        return;
    }

    string strreqMessage =ui->reqMessage->text().trimmed().toStdString();
    if(strreqMessage.size() > 150)
    {
        QMessageBox::warning(this, tr("Receive"),tr("Message input too long"),tr("Ok"));
        return;
    }

    QString currText = ui->assetsComboBox->currentText().trimmed();
    if(currText.isEmpty())
    {
        QMessageBox::warning(this, tr("Receive"),tr("Please select asset"),tr("Ok"));
        return;
    }
    int index = ui->assetsComboBox->findText(currText);
    if(index<0)
    {
        QMessageBox::warning(this,tr("Receive"),tr("Invalid asset name"),tr("Ok"));
        return;
    }

    QString address;
    QString label = ui->reqLabel->text();
    if(ui->reuseAddress->isChecked())
    {
        /* Choose existing receiving address */
        AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::ReceivingTab, this);
        dlg.setModel(model->getAddressTableModel());
        if(dlg.exec())
        {
            address = dlg.getReturnValue();
            if(label.isEmpty()) /* If no label provided, use the previously used label */
            {
                label = model->getAddressTableModel()->labelForAddress(address);
            }
        } else {
            return;
        }
    } else {
        /* Generate new receiving address */
        address = model->getAddressTableModel()->addRow(AddressTableModel::Receive, label, "");
    }
    SendCoinsRecipient info(address, label,ui->reqAmount->value(), 0, ui->reqMessage->text(),"",fAssets,ui->assetsComboBox->currentText().trimmed(),assetDecimal,strAssetUnit);
    if(fAssets)
        info.fUseInstantSend = false;
    else
        info.fUseInstantSend = ui->checkUseInstantSend->isChecked();
    info.fAsset = fAssets;
    ReceiveRequestDialog *dialog = new ReceiveRequestDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModel(model->getOptionsModel());
    dialog->setInfo(info);
    dialog->show();
    clear();

    /* Store request for later reference */
    model->getRecentRequestsTableModel()->addNewRequest(info);
}

void ReceiveCoinsDialog::on_recentRequestsView_doubleClicked(const QModelIndex &index)
{
    const RecentRequestsTableModel *submodel = model->getRecentRequestsTableModel();
    ReceiveRequestDialog *dialog = new ReceiveRequestDialog(this);
    dialog->setModel(model->getOptionsModel());
    dialog->setInfo(submodel->entry(index.row()).recipient);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void ReceiveCoinsDialog::recentRequestsView_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    // Enable Show/Remove buttons only if anything is selected.
    bool enable = !ui->recentRequestsView->selectionModel()->selectedRows().isEmpty();
    ui->showRequestButton->setEnabled(enable);
    ui->removeRequestButton->setEnabled(enable);
}

void ReceiveCoinsDialog::on_showRequestButton_clicked()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return;
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();

    Q_FOREACH (const QModelIndex& index, selection) {
        on_recentRequestsView_doubleClicked(index);
    }
}

void ReceiveCoinsDialog::on_removeRequestButton_clicked()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return;
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();
    if(selection.empty())
        return;
    // correct for selection mode ContiguousSelection
    QModelIndex firstIndex = selection.at(0);
    model->getRecentRequestsTableModel()->removeRows(firstIndex.row(), selection.length(), firstIndex.parent());
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void ReceiveCoinsDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(RecentRequestsTableModel::Message);
}

void ReceiveCoinsDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return)
    {
        // press return -> submit form
        if (ui->reqLabel->hasFocus() || ui->reqAmount->hasFocus() || ui->reqMessage->hasFocus())
        {
            event->ignore();
            on_receiveButton_clicked();
            return;
        }
    }

    this->QDialog::keyPressEvent(event);
}

QModelIndex ReceiveCoinsDialog::selectedRow()
{
    if(!model || !model->getRecentRequestsTableModel() || !ui->recentRequestsView->selectionModel())
        return QModelIndex();
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();
    if(selection.empty())
        return QModelIndex();
    // correct for selection mode ContiguousSelection
    QModelIndex firstIndex = selection.at(0);
    return firstIndex;
}

// copy column of selected row to clipboard
void ReceiveCoinsDialog::copyColumnToClipboard(int column)
{
    QModelIndex firstIndex = selectedRow();
    if (!firstIndex.isValid()) {
        return;
    }
    GUIUtil::setClipboard(model->getRecentRequestsTableModel()->data(firstIndex.child(firstIndex.row(), column), Qt::EditRole).toString());
}

// context menu
void ReceiveCoinsDialog::showMenu(const QPoint &point)
{
    if (!selectedRow().isValid()) {
        return;
    }
    contextMenu->exec(QCursor::pos());
}

// context menu action: copy URI
void ReceiveCoinsDialog::copyURI()
{
    QModelIndex sel = selectedRow();
    if (!sel.isValid()) {
        return;
    }

    const RecentRequestsTableModel * const submodel = model->getRecentRequestsTableModel();
    const QString uri = GUIUtil::formatBitcoinURI(submodel->entry(sel.row()).recipient);
    GUIUtil::setClipboard(uri);
}

// context menu action: copy label
void ReceiveCoinsDialog::copyLabel()
{
    copyColumnToClipboard(RecentRequestsTableModel::Label);
}

// context menu action: copy message
void ReceiveCoinsDialog::copyMessage()
{
    copyColumnToClipboard(RecentRequestsTableModel::Message);
}

// context menu action: copy amount
void ReceiveCoinsDialog::copyAmount()
{
    copyColumnToClipboard(RecentRequestsTableModel::Amount);
}
