// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_ASKPASSPHRASEDIALOG_H
#define BITCOIN_QT_ASKPASSPHRASEDIALOG_H

#include <QDialog>
#include <QThread>
#include <QMessageBox>
#include "walletmodel.h"

class WalletModel;

namespace Ui {
    class AskPassphraseDialog;
}

/** Multifunctional dialog to ask for passphrases. Used for encryption, unlocking, and changing the passphrase.
 */
class AskPassphraseDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        Encrypt,    /**< Ask passphrase twice and encrypt */
        UnlockMixing,     /**< Ask passphrase and unlock only for mixing */
        Unlock,     /**< Ask passphrase and unlock */
        ChangePass, /**< Ask old passphrase + new passphrase twice */
        Decrypt     /**< Ask passphrase and decrypt wallet */
    };

    explicit AskPassphraseDialog(Mode mode, QWidget *parent);
    ~AskPassphraseDialog();

    void accept();

    void setModel(WalletModel *model);

private:
    Ui::AskPassphraseDialog *ui;
    Mode mode;
    WalletModel *model;
    bool fCapsLock;
    QThread encryThread;
    QMessageBox *msgbox;

private Q_SLOTS:
    void textChanged();
    void secureClearPassFields();

public Q_SLOTS:
    void handlerEncryptResult(const bool result);

protected:
    bool event(QEvent *event);
    bool eventFilter(QObject *object, QEvent *event);

Q_SIGNALS:
    void runEncrypt();

};

#endif // BITCOIN_QT_ASKPASSPHRASEDIALOG_H
