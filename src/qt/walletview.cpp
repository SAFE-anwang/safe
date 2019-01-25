// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletview.h"

#include "addressbookpage.h"
#include "askpassphrasedialog.h"
#include "bitcoingui.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "masternodeconfig.h"
#include "optionsmodel.h"
#include "overviewpage.h"
#include "platformstyle.h"
#include "receivecoinsdialog.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "transactiontablemodel.h"
#include "transactionview.h"
#include "lockedtransactionview.h"
#include "walletmodel.h"
#include "candypage.h"
#include "candyview.h"
#include "assetspage.h"
#include "assetsdistributerecordview.h"
#include "applicationsregistrecordview.h"
#include "applicationspage.h"
#include "guiconstants.h"

#include "ui_interface.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <boost/thread.hpp>

extern boost::thread_group *g_threadGroup;
void RefreshWalletData(TransactionTableModel* txModel, WalletView *walletView);

WalletView::WalletView(const PlatformStyle *platformStyle, QWidget *parent):
    QStackedWidget(parent),
    clientModel(0),
    walletModel(0),
    platformStyle(platformStyle)
{
    // Create tabs
    overviewPage = new OverviewPage(platformStyle);

    /****************** transaction history ******************/
    transactionsPage = new QWidget(this);
    transactionsPage->setMouseTracking(true);
    QVBoxLayout *vbox = new QVBoxLayout();
    QHBoxLayout *hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(platformStyle, this);
    vbox->addWidget(transactionView);
    QPushButton *exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons()) {
        QString theme = GUIUtil::getThemeName();
        exportButton->setIcon(QIcon(":/icons/" + theme + "/export"));
    }
    hbox_buttons->addStretch();

    // Sum of selected transactions
    QLabel *transactionSumLabel = new QLabel(); // Label
    transactionSumLabel->setObjectName("transactionSumLabel"); // Label ID as CSS-reference
    transactionSumLabel->setText(tr("Selected amount:"));
    hbox_buttons->addWidget(transactionSumLabel);

    transactionSum = new QLabel(); // Amount
    transactionSum->setObjectName("transactionSum"); // Label ID as CSS-reference
    transactionSum->setMinimumSize(200, 8);
    transactionSum->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hbox_buttons->addWidget(transactionSum);

    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);
    transactionsPage->setLayout(vbox);

    /****************** locked transaction history ******************/
    lockedTransactionsPage = new QWidget(this);
    lockedTransactionsPage->setMouseTracking(true);
    QVBoxLayout *vbox_locked = new QVBoxLayout();
    QHBoxLayout *hbox_locked_buttons = new QHBoxLayout();
    lockedTransactionView = new LockedTransactionView(platformStyle, this);
    vbox_locked->addWidget(lockedTransactionView);
    QPushButton *lockedExportButton = new QPushButton(tr("&Export"), this);
    lockedExportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons()) {
        QString theme = GUIUtil::getThemeName();
        lockedExportButton->setIcon(QIcon(":/icons/" + theme + "/export"));
    }
    hbox_locked_buttons->addStretch();

    // Sum of selected transactions
    QLabel *lockedTransactionSumLabel = new QLabel(); // Label
    lockedTransactionSumLabel->setObjectName("lockedTransactionSumLabel"); // Label ID as CSS-reference
    lockedTransactionSumLabel->setText(tr("Selected amount:"));
    hbox_locked_buttons->addWidget(lockedTransactionSumLabel);

    lockedTransactionSum = new QLabel(); // Amount
    lockedTransactionSum->setObjectName("lockedTransactionSum"); // Label ID as CSS-reference
    lockedTransactionSum->setMinimumSize(200, 8);
    lockedTransactionSum->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hbox_locked_buttons->addWidget(lockedTransactionSum);

    hbox_locked_buttons->addWidget(lockedExportButton);
    vbox_locked->addLayout(hbox_locked_buttons);
    lockedTransactionsPage->setLayout(vbox_locked);

    receiveCoinsPage = new ReceiveCoinsDialog(platformStyle);
    sendCoinsPage = new SendCoinsDialog(platformStyle);

    usedSendingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    usedReceivingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);

    addWidget(overviewPage);
    addWidget(transactionsPage);
    addWidget(lockedTransactionsPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);

    candyPage = new CandyPage;
    candyView = new CandyView(platformStyle,this);
    QVBoxLayout *candyHistoryLayout = new QVBoxLayout();
    candyHistoryLayout->addWidget(candyView);
    QPushButton *candyExportButton = new QPushButton(tr("&Export"), this);
    candyExportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons()) {
        QString theme = GUIUtil::getThemeName();
        candyExportButton->setIcon(QIcon(":/icons/" + theme + "/export"));
    }
    QHBoxLayout *candy_hbox_buttons = new QHBoxLayout();
    candy_hbox_buttons->addStretch();

    // Sum of selected transactions
    QLabel *candyTransactionSumLabel = new QLabel(); // Label
    candyTransactionSumLabel->setObjectName("candyTransactionSumLabel"); // Label ID as CSS-reference
    candyTransactionSumLabel->setText(tr("Selected amount:"));
    candy_hbox_buttons->addWidget(candyTransactionSumLabel);

    candyTransactionSum = new QLabel(); // Amount
    candyTransactionSum->setObjectName("candyTransactionSum"); // Label ID as CSS-reference
    candyTransactionSum->setMinimumSize(200, 8);
    candyTransactionSum->setTextInteractionFlags(Qt::TextSelectableByMouse);
    candy_hbox_buttons->addWidget(candyTransactionSum);

    candy_hbox_buttons->addWidget(candyExportButton);
    candyHistoryLayout->addLayout(candy_hbox_buttons);
    candyPage->setGetHistoryTabLayout(candyHistoryLayout);
    addWidget(candyPage);

    assetsPage = new AssetsPage;
    // Tab Distribute Record
    assetsDistributeRecordView = new AssetsDistributeRecordView(platformStyle,this);
    QVBoxLayout *distributeLayout = new QVBoxLayout;
    distributeLayout->addWidget(assetsDistributeRecordView);
    QPushButton *assetsExportButton = new QPushButton(tr("&Export"), this);
    assetsExportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons()) {
        QString theme = GUIUtil::getThemeName();
        assetsExportButton->setIcon(QIcon(":/icons/" + theme + "/export"));
    }
    QHBoxLayout *assets_hbox_buttons = new QHBoxLayout();
    assets_hbox_buttons->addStretch();

    // Sum of selected transactions
    QLabel *assetsTransactionSumLabel = new QLabel(); // Label
    assetsTransactionSumLabel->setObjectName("assetsTransactionSumLabel"); // Label ID as CSS-reference
    assetsTransactionSumLabel->setText(tr("Selected amount:"));
    assets_hbox_buttons->addWidget(assetsTransactionSumLabel);

    assetsTransactionSum = new QLabel(); // Amount
    assetsTransactionSum->setObjectName("assetsTransactionSum"); // Label ID as CSS-reference
    assetsTransactionSum->setMinimumSize(200, 8);
    assetsTransactionSum->setTextInteractionFlags(Qt::TextSelectableByMouse);
    assets_hbox_buttons->addWidget(assetsTransactionSum);

    assets_hbox_buttons->addWidget(assetsExportButton);
    distributeLayout->addLayout(assets_hbox_buttons);
    assetsPage->setDistributeRecordLayout(distributeLayout);
    addWidget(assetsPage);

    applicationsPage = new ApplicationsPage;

    //Tab Regist Record
    applicationsView = new ApplicationsRegistRecordView(platformStyle,this);
    QVBoxLayout *appLayout = new QVBoxLayout;
    appLayout->addWidget(applicationsView);
    QPushButton *appExportButton = new QPushButton(tr("&Export"), this);
    appExportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons()) {
        QString theme = GUIUtil::getThemeName();
        appExportButton->setIcon(QIcon(":/icons/" + theme + "/export"));
    }
    QHBoxLayout *app_hbox_buttons = new QHBoxLayout();
    app_hbox_buttons->addStretch();
    app_hbox_buttons->addWidget(appExportButton);
    appLayout->addLayout(app_hbox_buttons);
    applicationsPage->setRegistRecordLayout(appLayout);
    addWidget(applicationsPage);

    QSettings settings;
    if (settings.value("fShowMasternodesTab").toBool()) {
        masternodeListPage = new MasternodeList(platformStyle);
        addWidget(masternodeListPage);
    }

    // Clicking on a transaction on the overview pre-selects the transaction on the transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));
    connect(overviewPage, SIGNAL(outOfSyncWarningClicked()), this, SLOT(requestedSyncWarningInfo()));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(lockedTransactionView, SIGNAL(doubleClicked(QModelIndex)), lockedTransactionView, SLOT(showDetails()));

    // Double-clicking on a candy on the candy page shows details
    connect(candyView, SIGNAL(doubleClicked(QModelIndex)), candyView, SLOT(showDetails()));

    // Double-clicking on a assets distribute record on the assets page shows details
    connect(assetsDistributeRecordView,SIGNAL(doubleClicked(QModelIndex)),assetsDistributeRecordView,SLOT(showDetails()));

    // Double-clicking on a assets distribute record on the assets page shows details
    connect(applicationsView,SIGNAL(doubleClicked(QModelIndex)),applicationsView,SLOT(showDetails()));

    // Update wallet with sum of selected transactions
    connect(transactionView, SIGNAL(trxAmount(QString)), this, SLOT(trxAmount(QString)));

    // Update wallet with sum of selected locked transactions
    connect(lockedTransactionView, SIGNAL(trxAmount(QString)), this, SLOT(lockedTrxAmount(QString)));

    // Update wallet with sum of selected assets distribute transactions
    connect(assetsDistributeRecordView,SIGNAL(trxAmount(QString)),this,SLOT(assetsTrxAmount(QString)));

    // Update wallet with sum of get candy transactions
    connect(candyView,SIGNAL(trxAmount(QString)),this,SLOT(candyTrxAmount(QString)));

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, SIGNAL(clicked()), transactionView, SLOT(exportClicked()));

    // Clicking on "Export" allows to export the locked transaction list
    connect(lockedExportButton, SIGNAL(clicked()), lockedTransactionView, SLOT(exportClicked()));

    // Clicking on "Export" allows to export the assets transaction list
    connect(assetsExportButton,SIGNAL(clicked()),assetsDistributeRecordView,SLOT(exportClicked()));

    // Clicking on "Export" allows to export the application registry list
    connect(appExportButton,SIGNAL(clicked()),applicationsView,SLOT(exportClicked()));

    // Clicking on "Export" allows to export the get candy list
    connect(candyExportButton,SIGNAL(clicked()),candyView,SLOT(exportClicked()));

    // Pass through messages from sendCoinsPage
    connect(sendCoinsPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

    // Pass through messages from transactionView
    connect(transactionView, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

    // Pass through messages from lockedTransactionView
    connect(lockedTransactionView, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

    // Pass through messages from candyView
    connect(candyView, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

    // Pass through messages from distributeRecordView
    connect(assetsDistributeRecordView,SIGNAL(message(QString,QString,uint)),this,SIGNAL(message(QString,QString,uint)));

    // Pass through messages from distributeRecordView
    connect(applicationsView,SIGNAL(message(QString,QString,uint)),this,SIGNAL(message(QString,QString,uint)));


	qRegisterMetaType<TransactionTableModel * >("TransactionTableModel *");

	connect(this, SIGNAL(refreshFinished(TransactionTableModel*)), this, SLOT(refreshFinished_slot(TransactionTableModel*)));

}

WalletView::~WalletView()
{
}

void WalletView::setBitcoinGUI(BitcoinGUI *gui)
{
    if (gui)
    {
        // Clicking on a transaction on the overview page simply sends you to transaction history page
        connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), gui, SLOT(gotoHistoryPage()));

        // Receive and report messages
        connect(this, SIGNAL(message(QString,QString,unsigned int)), gui, SLOT(message(QString,QString,unsigned int)));

        // Pass through encryption status changed signals
        connect(this, SIGNAL(encryptionStatusChanged(int)), gui, SLOT(setEncryptionStatus(int)));

        // Pass through transaction notifications
        connect(this, SIGNAL(incomingTransaction(QString,int,CAmount,QString,QString,QString,bool,QString,QString)), gui, SLOT(incomingTransaction(QString,int,CAmount,QString,QString,QString,bool,QString,QString)));

        // Connect HD enabled state signal
        connect(this, SIGNAL(hdEnabledStatusChanged(int)), gui, SLOT(setHDStatus(int)));
    }
}

void WalletView::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;

    overviewPage->setClientModel(clientModel);
    sendCoinsPage->setClientModel(clientModel);
    receiveCoinsPage->setClientModel(clientModel);
    candyPage->setClientModel(clientModel);
    assetsPage->setClientModel(clientModel);
    QSettings settings;
    if (settings.value("fShowMasternodesTab").toBool()) {
        masternodeListPage->setClientModel(clientModel);
    }
    if(clientModel)
        connect(clientModel,SIGNAL(updateForbitChanged(bool)),this,SLOT(updateAssetsDisplay(bool)));
}

void WalletView::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;

    // Put transaction list in tabs
    transactionView->setModel(walletModel);
    lockedTransactionView->setModel(walletModel);
    candyView->setModel(walletModel);
    assetsDistributeRecordView->setModel(walletModel);
    applicationsView->setModel(walletModel);
    overviewPage->setWalletModel(walletModel);
    QSettings settings;
    if (settings.value("fShowMasternodesTab").toBool()) {
        masternodeListPage->setWalletModel(walletModel);
    }
    receiveCoinsPage->setModel(walletModel);
    sendCoinsPage->setModel(walletModel);
    assetsPage->setWalletModel(walletModel);
    applicationsPage->setWalletModel(walletModel);
    candyPage->setModel(walletModel);
    usedReceivingAddressesPage->setModel(walletModel->getAddressTableModel());
    usedSendingAddressesPage->setModel(walletModel->getAddressTableModel());

    if (walletModel)
    {
        // Receive and pass through messages from wallet model
        connect(walletModel, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

        // Handle changes in encryption status
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SIGNAL(encryptionStatusChanged(int)));
        updateEncryptionStatus();

        // update HD status
        Q_EMIT hdEnabledStatusChanged(walletModel->hdEnabled());

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(processNewTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock(bool)), this, SLOT(unlockWallet(bool)));

        // Show progress dialog
        connect(walletModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));

        //tx,issue assets
        connect(walletModel->getLockedTransactionTableModel(),SIGNAL(updateAssets(int,bool,QString)),this,SLOT(updateAssetsInfo(int,bool,QString)));
        connect(walletModel->getTransactionTableModel(),SIGNAL(updateAssets(int,bool,QString)),this,SLOT(updateAssetsInfo(int,bool,QString)));
        connect(walletModel->getAssetsDistributeTableModel(),SIGNAL(updateAssets(int,bool,QString)),this,SLOT(updateAssetsInfo(int,bool,QString)));
        connect(walletModel->getCandyTableModel(),SIGNAL(updateAssets(int,bool,QString)),this,SLOT(updateAssetsInfo(int,bool,QString)));

        //update once
/*        overviewPage->updateAssetsInfo();
        receiveCoinsPage->updateAssetsInfo();
        sendCoinsPage->updateAssetsInfo();
        assetsPage->updateAssetsInfo();
        candyPage->updateAssetsInfo();
*/
    }
}

void WalletView::updateAssetsDisplay(bool updateAsset)
{
    walletModel->getTransactionTableModel()->refreshWallet();
    walletModel->getLockedTransactionTableModel()->refreshWallet();
    walletModel->getAssetsDistributeTableModel()->refreshWallet();
    walletModel->getCandyTableModel()->refreshWallet();
    if(updateAsset)
        updateAssetsInfo(SHOW_ALL);
}

void WalletView::updateAssetsInfo(int showType, bool bConfirmedNewAssets, const QString &strAssetName)
{
    if(showType==SHOW_APPLICATION_REGIST)
        return;

    bool bShowAll = showType==SHOW_ALL;

    overviewPage->updateAssetsInfo(strAssetName);

    if(bShowAll)
        receiveCoinsPage->updateAssetsInfo();

    //only issue asset,update the add asset combox and put candy combox
    if(bConfirmedNewAssets||bShowAll)
    {
        sendCoinsPage->updateAssetsInfo();
        assetsPage->updateAssetsInfo();
        candyPage->updateAssetsInfo();
    }
}

void WalletView::processNewTransaction(const QModelIndex& parent, int start, int /*end*/)
{
    // Prevent balloon-spam when initial block download is in progress
    if (!walletModel || !clientModel || clientModel->inInitialBlockDownload())
        return;

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    if (!ttm || ttm->processingQueuedTransactions())
        return;

    QString date = ttm->index(start, TransactionTableModel::TransactionColumnDate, parent).data().toString();
    qint64 amount = ttm->index(start, TransactionTableModel::TransactionColumnAmount, parent).data(Qt::EditRole).toULongLong();
    QString type = ttm->index(start, TransactionTableModel::TransactionColumnType, parent).data().toString();
    QModelIndex index = ttm->index(start, 0, parent);
    QString address = ttm->data(index, TransactionTableModel::AddressRole).toString();
    QString label = ttm->data(index, TransactionTableModel::LabelRole).toString();
    bool bSAFETransaction = ttm->data(index,TransactionTableModel::SAFERole).toBool();
    QString strAssetUnit = ttm->data(index,TransactionTableModel::AmountUnitRole).toString();
    QString strAssetName =  ttm->data(index,TransactionTableModel::AssetsNameRole).toString();
    int unit = 0;
    if(bSAFETransaction)
        unit = walletModel->getOptionsModel()->getDisplayUnit();
    else
        unit = ttm->data(index,TransactionTableModel::AssetsDecimalsRole).toInt();
    Q_EMIT incomingTransaction(date, unit, amount, type, address, label,bSAFETransaction,strAssetUnit,strAssetName);
}

void WalletView::gotoOverviewPage()
{
    setCurrentWidget(overviewPage);
}

void WalletView::gotoHistoryPage()
{
	if (!walletModel->getTransactionTableModel()->isRefreshWallet() && g_threadGroup != NULL)
	{
		walletModel->getTransactionTableModel()->setRefreshWalletFlag(true);
		g_threadGroup->create_thread(boost::bind(&RefreshWalletData, walletModel->getTransactionTableModel(), this));
	}
    setCurrentWidget(transactionsPage);
}

void WalletView::gotoLockedHistoryPage()
{
	if (!walletModel->getLockedTransactionTableModel()->isRefreshWallet() && g_threadGroup != NULL)
	{
		walletModel->getLockedTransactionTableModel()->setRefreshWalletFlag(true);
		g_threadGroup->create_thread(boost::bind(&RefreshWalletData, walletModel->getLockedTransactionTableModel(), this));
	}
    setCurrentWidget(lockedTransactionsPage);
}

void WalletView::gotoAssetsPage()
{
	if (!walletModel->getAssetsDistributeTableModel()->isRefreshWallet() && g_threadGroup != NULL)
	{
		walletModel->getAssetsDistributeTableModel()->setRefreshWalletFlag(true);
		g_threadGroup->create_thread(boost::bind(&RefreshWalletData, walletModel->getAssetsDistributeTableModel(), this));
	}
    setCurrentWidget(assetsPage);
}

void WalletView::gotoApplicationPage()
{
    setCurrentWidget(applicationsPage);
}

void WalletView::gotoCandyPage()
{
	if (!walletModel->getCandyTableModel()->isRefreshWallet() && g_threadGroup != NULL)
	{
		walletModel->getCandyTableModel()->setRefreshWalletFlag(true);
		g_threadGroup->create_thread(boost::bind(&RefreshWalletData, walletModel->getCandyTableModel(), this));
	}

    setCurrentWidget(candyPage);
}

void WalletView::gotoMasternodePage()
{
    QSettings settings;
    if (settings.value("fShowMasternodesTab").toBool()) {
        setCurrentWidget(masternodeListPage);
    }
}

void WalletView::gotoReceiveCoinsPage()
{
    setCurrentWidget(receiveCoinsPage);
}

void WalletView::gotoSendCoinsPage(QString addr)
{
    setCurrentWidget(sendCoinsPage);

    if (!addr.isEmpty())
        sendCoinsPage->setAddress(addr);
}

void WalletView::gotoSignMessageTab(QString addr)
{
    // calls show() in showTab_SM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_SM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void WalletView::gotoVerifyMessageTab(QString addr)
{
    // calls show() in showTab_VM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_VM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

bool WalletView::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    return sendCoinsPage->handlePaymentRequest(recipient);
}

void WalletView::showOutOfSyncWarning(bool fShow)
{
    overviewPage->showOutOfSyncWarning(fShow);
}

void WalletView::updateEncryptionStatus()
{
    Q_EMIT encryptionStatusChanged(walletModel->getEncryptionStatus());
}

void WalletView::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt : AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    updateEncryptionStatus();
}

void WalletView::backupWallet()
{
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Backup Wallet"), QString(),
        tr("Wallet Data (*.dat)"), NULL);

    if (filename.isEmpty())
        return;

    if (!walletModel->backupWallet(filename)) {
        Q_EMIT message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
        }
    else {
        Q_EMIT message(tr("Backup Successful"), tr("The wallet data was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void WalletView::unlockWallet(bool fForMixingOnly)
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model

    if (walletModel->getEncryptionStatus() == WalletModel::Locked || walletModel->getEncryptionStatus() == WalletModel::UnlockedForMixingOnly)
    {
        AskPassphraseDialog dlg(fForMixingOnly ? AskPassphraseDialog::UnlockMixing : AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void WalletView::lockWallet()
{
    if(!walletModel)
        return;

    walletModel->setWalletLocked(true);
}

void WalletView::usedSendingAddresses()
{
    if(!walletModel)
        return;

    usedSendingAddressesPage->show();
    usedSendingAddressesPage->raise();
    usedSendingAddressesPage->activateWindow();
}

void WalletView::usedReceivingAddresses()
{
    if(!walletModel)
        return;

    usedReceivingAddressesPage->show();
    usedReceivingAddressesPage->raise();
    usedReceivingAddressesPage->activateWindow();
}

void WalletView::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void WalletView::requestedSyncWarningInfo()
{
    Q_EMIT outOfSyncWarningClicked();
}

/** Update wallet with the sum of the selected transactions */
void WalletView::trxAmount(QString amount)
{
    transactionSum->setText(amount);
}

/** Update wallet with the sum of the selected locked transactions */
void WalletView::lockedTrxAmount(QString amount)
{
    lockedTransactionSum->setText(amount);
}

/** Update selected Assets amount from assetstransactionview */
void WalletView::assetsTrxAmount(QString amount)
{
    assetsTransactionSum->setText(amount);
}

/** Update selected Candy amount from candytransactionview */
void WalletView::candyTrxAmount(QString amount)
{
    candyTransactionSum->setText(amount);
}

int WalletView::getPageType()
{
	QWidget *pCurrentWidget = currentWidget();
	WalletModel::PageType pageType = WalletModel::NonePage;

	if (pCurrentWidget == transactionsPage)
	{
		pageType = WalletModel::TransactionPage;
	}
	else if (pCurrentWidget == lockedTransactionsPage)
	{
		pageType = WalletModel::LockPage;
	}
	else if (pCurrentWidget == candyPage)
	{
		pageType = WalletModel::CandyPage;
	}
	else if (pCurrentWidget == assetsPage)
	{
		pageType = WalletModel::AssetPage;
	}
	else if (pCurrentWidget == applicationsPage)
	{
		pageType = WalletModel::AppPage;
	}

	return pageType;
}

void RefreshWalletData(TransactionTableModel* txModel, WalletView *walletView)
{
	if (NULL == txModel)
	{
		return ;
	}

	RenameThread("RefreshWalletData");
	txModel->refreshWallet();
	if (txModel == walletView->getWalletMode()->getTransactionTableModel())
	{
		MilliSleep(2000);
	}

	Q_EMIT walletView->refreshFinished(txModel);
}

void ThreadUpdateBalanceChanged(WalletModel *walletModel)
{
	static bool fOneThread = false;
	if (fOneThread)
	{
		return;
	}

	fOneThread = true;
	if (NULL == walletModel)
	{
		return;
	}

	RenameThread("updateBalanceChangedThread");
	while (true)
	{
		boost::this_thread::interruption_point();
		walletModel->pollBalanceChanged();
		MilliSleep(MODEL_UPDATE_DELAY);
	}
}

void WalletView::ShowHistoryPage()
{
	if (!walletModel->getTransactionTableModel()->isRefreshWallet() && g_threadGroup != NULL)
	{
		walletModel->getTransactionTableModel()->setRefreshWalletFlag(true);
		g_threadGroup->create_thread(boost::bind(&RefreshWalletData, walletModel->getTransactionTableModel(), this));
	}
}

void WalletView::refreshFinished_slot(TransactionTableModel* txModel)
{
	if (txModel != NULL)
	{
		txModel->refreshPage();
	}

	if (txModel == walletModel->getTransactionTableModel())
	{
		overviewPage->updateAssetsInfo();
		receiveCoinsPage->updateAssetsInfo();
		sendCoinsPage->updateAssetsInfo();
		g_threadGroup->create_thread(boost::bind(&ThreadUpdateBalanceChanged, walletModel));
	}
	else if (txModel == walletModel->getAssetsDistributeTableModel())
	{
		assetsPage->updateAssetsInfo();
	}
	else if (txModel == walletModel->getCandyTableModel())
	{
		candyPage->updateAssetsInfo();
	}
}

WalletModel *WalletView::getWalletMode()
{
	return walletModel;
}