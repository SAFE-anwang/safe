// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2018 The Safe Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sendcoinsentry.h"
#include "ui_sendcoinsentry.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "../app/app.h"

#include <QApplication>
#include <QClipboard>

#define MAX_ADRESS_LABEL_SIZE 60

SendCoinsEntry::SendCoinsEntry(const PlatformStyle *platformStyle, QWidget *parent, bool showLocked) :
    QStackedWidget(parent),
    ui(new Ui::SendCoinsEntry),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);

    setCurrentWidget(ui->SendCoins);

    if (platformStyle->getUseExtraSpacing())
        ui->payToLayout->setSpacing(4);
    //ui->addAsLabel->setMaxLength(MAX_ADRESS_LABEL_SIZE);
#if QT_VERSION >= 0x040700
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));
    ui->memoLineEdit->setPlaceholderText(tr("The memo content will be written into the block chain, which everyone can see"));
#endif
    ui->messageLineEdit->setEnabled(false);
    ui->messageLabel->hide();
    ui->messageLineEdit->hide();
    QString theme = GUIUtil::getThemeName();

    // These icons are needed on Mac also!
    ui->addressBookButton->setIcon(QIcon(":/icons/" + theme + "/address-book"));
    ui->pasteButton->setIcon(QIcon(":/icons/" + theme + "/editpaste"));
    ui->deleteButton->setIcon(QIcon(":/icons/" + theme + "/remove"));
    ui->deleteButton_is->setIcon(QIcon(":/icons/" + theme + "/remove"));
    ui->deleteButton_s->setIcon(QIcon(":/icons/" + theme + "/remove"));
      
    // normal safe address field
    GUIUtil::setupAddressWidget(ui->payTo, this);
    // just a label for displaying safe address(es)
    ui->payTo_is->setFont(GUIUtil::fixedPitchFont());

    ui->lockedMonthCheckBox->setChecked(showLocked);
    ui->lockedMonthLabel->setVisible(showLocked);
    ui->lockedMonth->setVisible(showLocked);
    ui->payAmount->setStyleSheet("QComboBox{font-size:12px;}");
    // Connect signals
    connect(ui->payAmount, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
    connect(ui->checkboxSubtractFeeFromAmount, SIGNAL(toggled(bool)), this, SIGNAL(subtractFeeFromAmountChanged()));
    connect(ui->deleteButton, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_is, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_s, SIGNAL(clicked()), this, SLOT(deleteClicked()));

    QRegExp addExpReqLabelEdit;
    addExpReqLabelEdit.setPattern("[a-zA-Z\u4e00-\u9fa5][a-zA-Z0-9-+*/。，$%^&*,!?.()#_\u4e00-\u9fa5 ]{1,150}");
    ui->addAsLabel->setValidator (new QRegExpValidator(addExpReqLabelEdit, this));
    fAssets = false;
    nAssetDecimals = 0;
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui;
}

void SendCoinsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendCoinsEntry::on_payTo_textChanged(const QString &address)
{
    updateLabel(address);
}

void SendCoinsEntry::on_addAsLabel_textChanged(const QString &/*address*/)
{
    while(ui->addAsLabel->text().toStdString().size() > MAX_ADRESS_LABEL_SIZE)
        ui->addAsLabel->setText(ui->addAsLabel->text().left(ui->addAsLabel->text().length()-1));
}

void SendCoinsEntry::on_memoLineEdit_textChanged(const QString &/*address*/)
{
    while(ui->memoLineEdit->text().toStdString().size() > MAX_REMARKS_SIZE)
        ui->memoLineEdit->setText(ui->memoLineEdit->text().left(ui->memoLineEdit->text().length()-1));
}

void SendCoinsEntry::setModel(WalletModel *model)
{
    this->model = model;

    if (model && model->getOptionsModel())
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    clear();
}

void SendCoinsEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    ui->lockedMonth->setValue(6);
    ui->lockedMonthCheckBox->setChecked(false);
    ui->checkboxSubtractFeeFromAmount->setCheckState(Qt::Unchecked);
    ui->messageLineEdit->clear();
    ui->messageLineEdit->hide();
    ui->messageLabel->hide();
    ui->memoLineEdit->clear();
    // clear UI elements for unauthenticated payment request
    ui->payTo_is->clear();
    ui->memoTextLabel_is->clear();
    ui->payAmount_is->clear();
    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();
    ui->payAmount_s->clear();

    // update the display unit, to not use the default ("SAFE")
    updateDisplayUnit();
    on_lockedMonthCheckBox_clicked();
}

void SendCoinsEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

bool SendCoinsEntry::validate()
{
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

    // Skip checks for payment request
    if (recipient.paymentRequest.IsInitialized())
        return retval;

    if (!model->validateAddress(ui->payTo->text()))
    {
        ui->payTo->setValid(false);
        retval = false;
    }

    if (!ui->payAmount->validate())
    {
        retval = false;
    }

    // Sending a zero amount is invalid
    if (ui->payAmount->value(0) <= 0)
    {
        ui->payAmount->setValid(false);
        retval = false;
    }

    // Reject dust outputs:
    if (!fAssets && retval && GUIUtil::isDust(ui->payTo->text(), ui->payAmount->value())) {
        ui->payAmount->setValid(false);
        retval = false;
    }

    if(!ui->lockedMonth->validate())
        retval = false;

    return retval;
}

SendCoinsRecipient SendCoinsEntry::getValue(bool fAssets)
{
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    // Normal payment
    recipient.address = ui->payTo->text();
    recipient.label = ui->addAsLabel->text();
    if(fAssets)
    {
        recipient.strAssetAmount = ui->payAmount->textValue().trimmed();
        recipient.amount = ui->payAmount->value();
    }
    else
        recipient.amount = ui->payAmount->value();
    recipient.nLockedMonth = (ui->lockedMonthCheckBox->checkState() == Qt::Checked ? ui->lockedMonth->value() : 0);
    recipient.message = ui->messageLineEdit->text();
    recipient.strMemo = ui->memoLineEdit->text();
    recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked);

    return recipient;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    QWidget *w = ui->payAmount->setupTabChain(ui->addAsLabel);
    QWidget::setTabOrder(w, ui->checkboxSubtractFeeFromAmount);
    QWidget::setTabOrder(ui->checkboxSubtractFeeFromAmount, ui->addressBookButton);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    return ui->deleteButton;
}

void SendCoinsEntry::setValue(const SendCoinsRecipient &value)
{
    recipient = value;

    if (recipient.paymentRequest.IsInitialized()) // payment request
    {
        if (recipient.authenticatedMerchant.isEmpty()) // unauthenticated
        {
            ui->payTo_is->setText(recipient.address);
            ui->memoTextLabel_is->setText(recipient.message);
            ui->payAmount_is->setValue(recipient.amount);
            ui->payAmount_is->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        }
        else // authenticated
        {
            ui->payTo_s->setText(recipient.authenticatedMerchant);
            ui->memoTextLabel_s->setText(recipient.message);
            ui->payAmount_s->setValue(recipient.amount);
            ui->payAmount_s->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        }
    }
    else // normal payment
    {
        // message
        ui->messageLineEdit->setText(recipient.message);
        ui->messageLineEdit->setVisible(!recipient.message.isEmpty());
        ui->messageLabel->setVisible(!recipient.message.isEmpty());

        ui->addAsLabel->clear();
        ui->payTo->setText(recipient.address); // this may set a label from addressbook
        if (!recipient.label.isEmpty()) // if a label had been set from the addressbook, don't overwrite with an empty label
            ui->addAsLabel->setText(recipient.label);
        ui->payAmount->setValue(recipient.amount);
        ui->lockedMonth->setValue(recipient.nLockedMonth);
    }
}

void SendCoinsEntry::setAddress(const QString &address)
{
    ui->payTo->setText(address);
    ui->payAmount->setFocus();
}

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty();
}

bool SendCoinsEntry::isShowLocked()
{
    return ui->lockedMonthCheckBox->isChecked();
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendCoinsEntry::updateAssetUnit(const QString &unitName, bool fAssets, int decimal)
{
    ui->payAmount->updateAssetUnit(unitName,fAssets,decimal);
    ui->checkboxSubtractFeeFromAmount->setVisible(!fAssets);
    this->fAssets = fAssets;
    this->nAssetDecimals  = decimal;
}

void SendCoinsEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update payAmount with the current unit
        ui->payAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_is->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_s->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

bool SendCoinsEntry::updateLabel(const QString &address)
{
    if(!model)
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    ui->addAsLabel->setText(associatedLabel);
    return true;
}

void SendCoinsEntry::on_lockedMonthCheckBox_clicked()
{
    if(ui->lockedMonthCheckBox->isChecked())
    {
        ui->lockedMonthLabel->setVisible(true);
        ui->lockedMonth->setVisible(true);
    }
    else
    {
        ui->lockedMonthLabel->setVisible(false);
        ui->lockedMonth->setVisible(false);
    }
    if(fAssets)
        ui->lockedMonth->setValue(1);
    else
        ui->lockedMonth->setValue(6);
}
