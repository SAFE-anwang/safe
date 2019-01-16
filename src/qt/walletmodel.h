// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_WALLETMODEL_H
#define BITCOIN_QT_WALLETMODEL_H

#include "paymentrequestplus.h"
#include "walletmodeltransaction.h"

#include "wallet/wallet.h"
#include "support/allocators/secure.h"

#include <map>
#include <vector>
#include <QMap>

#include <QObject>

class AddressTableModel;
class OptionsModel;
class PlatformStyle;
class RecentRequestsTableModel;
class TransactionTableModel;
class WalletModelTransaction;
class AssetsDisplayInfo;
class UpdateConfirmWorker;
class WalletView;

class CCoinControl;
class CKeyID;
class COutPoint;
class COutput;
class CPubKey;
class CWallet;
class uint256;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class SendCoinsRecipient
{
public:
    explicit SendCoinsRecipient() : amount(0), strAssetAmount(""),nLockedMonth(0), message(""), strMemo(""),fAsset(false),assetDecimal(0),strAssetUnit(""),strAssetName(""),fSubtractFeeFromAmount(false), nVersion(SendCoinsRecipient::CURRENT_VERSION) { }
    explicit SendCoinsRecipient(const QString &addr, const QString &label, const CAmount& amount, const int nLockedMonth, const QString &message,const QString &memo,bool fAsset,const QString& assetName,int assetDecimal,const QString& assetUnit):
        address(addr), label(label), amount(amount), strAssetAmount(""), nLockedMonth(nLockedMonth), message(message), strMemo(memo),fAsset(fAsset),assetDecimal(assetDecimal),strAssetUnit(assetUnit),strAssetName(assetName),
        fSubtractFeeFromAmount(false), nVersion(SendCoinsRecipient::CURRENT_VERSION) {}

    // If from an unauthenticated payment request, this is used for storing
    // the addresses, e.g. address-A<br />address-B<br />address-C.
    // Info: As we don't need to process addresses in here when using
    // payment requests, we can abuse it for displaying an address list.
    // Todo: This is a hack, should be replaced with a cleaner solution!
    QString address;
    QString label;
    AvailableCoinsType inputType;
    bool fUseInstantSend;
    CAmount amount;
    QString strAssetAmount;
    int nLockedMonth;
    QString message;
    QString strMemo;
    bool fAsset;
    int assetDecimal;
    QString strAssetUnit;
    QString strAssetName;

    // If from a payment request, paymentRequest.IsInitialized() will be true
    PaymentRequestPlus paymentRequest;
    // Empty if no authentication or invalid signature/cert/etc.
    QString authenticatedMerchant;

    bool fSubtractFeeFromAmount; // memory only

    static const int CURRENT_VERSION = 2;
    int nVersion;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        std::string sAddress = address.toStdString();
        std::string sLabel = label.toStdString();
        std::string sMessage = message.toStdString();
        std::string sDecimal = QString::number(assetDecimal).toStdString();
        std::string sAssetUnit = strAssetUnit.toStdString();
        std::string sAssetName = strAssetName.toStdString();
        std::string sPaymentRequest;
        if (!ser_action.ForRead() && paymentRequest.IsInitialized())
            paymentRequest.SerializeToString(&sPaymentRequest);
        std::string sAuthenticatedMerchant = authenticatedMerchant.toStdString();

        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(sAddress);
        READWRITE(sLabel);
        READWRITE(amount);
        READWRITE(sMessage);
        READWRITE(sPaymentRequest);
        READWRITE(sAuthenticatedMerchant);

        if(nVersion>=CURRENT_VERSION)
        {
            READWRITE(nLockedMonth);
            READWRITE(sDecimal);
            READWRITE(sAssetUnit);
            READWRITE(sAssetName);
            READWRITE(fAsset);
            READWRITE(fUseInstantSend);
        }

        if (ser_action.ForRead())
        {
            address = QString::fromStdString(sAddress);
            label = QString::fromStdString(sLabel);
            message = QString::fromStdString(sMessage);
            assetDecimal = QString::fromStdString(sDecimal).toInt();
            strAssetUnit = QString::fromStdString(sAssetUnit);
            strAssetName = QString::fromStdString(sAssetName);
            if (!sPaymentRequest.empty())
                paymentRequest.parse(QByteArray::fromRawData(sPaymentRequest.data(), sPaymentRequest.size()));
            authenticatedMerchant = QString::fromStdString(sAuthenticatedMerchant);
        }
    }
};

/** Interface to Bitcoin wallet from Qt view code. */
class WalletModel : public QObject
{
    Q_OBJECT

public:
    explicit WalletModel(const PlatformStyle *platformStyle, CWallet *wallet, OptionsModel *optionsModel, QObject *parent = 0);
    ~WalletModel();

    enum StatusCode // Returned by sendCoins
    {
        OK,
        None,
        InvalidAmount,
        InvalidAddress,
        AmountExceedsBalance,
        AmountWithFeeExceedsBalance,
        DuplicateAddress,
        TransactionCreationFailed, // Error returned when wallet is still locked
        TransactionCommitFailed,
        AbsurdFee,
        PaymentRequestExpired,
        WalletUnavailable,
        InvalidAssetRecvAddress,
        AssetIdFail,
        InvalidAssetId,
        NonExistAssetId,
        AmountOutOfRange,
        InvalidAssetAmount,
        InvalidLockedMonth,
        InvalidRemarks,
        WalletLocked,
        P2PMissed,
        InsufficientSafeFunds,
        InsufficientAssetFunds,
        CreateAssetTransactionFail,
        CommitTransactionFail,
        TransactionAmountSealed
    };

    enum EncryptionStatus
    {
        Unencrypted,            // !wallet->IsCrypted()
        Locked,                 // wallet->IsCrypted() && wallet->IsLocked(true)
        UnlockedForMixingOnly,  // wallet->IsCrypted() && !wallet->IsLocked(true) && wallet->IsLocked()
        Unlocked,               // wallet->IsCrypted() && !wallet->IsLocked()
    };

	enum PageType
	{
		NonePage,
		TransactionPage,
		LockPage,
		CandyPage,
		AssetPage,
		AppPage
	};

    OptionsModel *getOptionsModel();
    AddressTableModel *getAddressTableModel();
    TransactionTableModel *getTransactionTableModel();
    TransactionTableModel *getLockedTransactionTableModel();
    TransactionTableModel *getCandyTableModel();
    TransactionTableModel *getAssetsDistributeTableModel();
    TransactionTableModel *getAssetsRegistTableModel();
    RecentRequestsTableModel *getRecentRequestsTableModel();

    CAmount getBalance(const CCoinControl *coinControl = NULL,const bool fAsset=false, const uint256* pAssetId=NULL, const CBitcoinAddress* pAddress=NULL,bool bLock=true) const;
    CAmount getUnconfirmedBalance(const bool fAsset=false, const uint256* pAssetId=NULL, const CBitcoinAddress* pAddress=NULL,bool bLock=true) const;
    CAmount getImmatureBalance(const bool fAsset=false, const uint256* pAssetId=NULL, const CBitcoinAddress* pAddress=NULL,bool bLock=true) const;
    CAmount getLockedBalance(const bool fAsset=false, const uint256* pAssetId=NULL, const CBitcoinAddress* pAddress=NULL,bool bLock=true) const;
    CAmount getAnonymizedBalance(bool bLock=true) const;
    bool haveWatchOnly() const;
    CAmount getWatchBalance(const bool fAsset=false, const uint256* pAssetId=NULL, const CBitcoinAddress* pAddress=NULL,bool bLock=true) const;
    CAmount getWatchUnconfirmedBalance(const bool fAsset=false, const uint256* pAssetId=NULL, const CBitcoinAddress* pAddress=NULL,bool bLock=true) const;
    CAmount getWatchImmatureBalance(const bool fAsset=false, const uint256* pAssetId=NULL, const CBitcoinAddress* pAddress=NULL,bool bLock=true) const;
    CAmount getWatchLockedBalance(const bool fAsset=false, const uint256* pAssetId=NULL, const CBitcoinAddress* pAddress=NULL,bool bLock=true) const;
    EncryptionStatus getEncryptionStatus() const;

    void getAssetsNames(bool needInMainChain,QStringList& lst);

    //assets name
    QMap<QString,AssetsDisplayInfo>& getAssetsNamesUnits();

    // Check address for validity
    bool validateAddress(const QString &address);

    // Return status record for SendCoins, contains error id + information
    struct SendCoinsReturn
    {
        SendCoinsReturn(StatusCode status = OK):
            status(status) {}
        StatusCode status;
    };

    // prepare transaction for getting txfee before sending coins
    SendCoinsReturn prepareTransaction(WalletModelTransaction &transaction, const CCoinControl *coinControl = NULL,bool fAssets=false,const QString& assetsName="");

    // Send assets to a list of recipients
    SendCoinsReturn sendAssets(WalletModelTransaction &transaction);

    // Send coins to a list of recipients
    SendCoinsReturn sendCoins(WalletModelTransaction &transaction);

    // Wallet encryption
    bool setWalletEncrypted(bool encrypted, const SecureString &passphrase);
    // Passphrase only needed when unlocking
    bool setWalletLocked(bool locked, const SecureString &passPhrase=SecureString(), bool fMixing=false);
    bool changePassphrase(const SecureString &oldPass, const SecureString &newPass);

    // Wallet backup
    bool backupWallet(const QString &filename);

    // RAI object for unlocking wallet, returned by requestUnlock()
    class UnlockContext
    {
    public:
        UnlockContext(WalletModel *wallet, bool valid, bool was_locked, bool was_mixing);
        ~UnlockContext();

        bool isValid() const { return valid; }

        // Copy operator and constructor transfer the context
        UnlockContext(const UnlockContext& obj) { CopyFrom(obj); }
        UnlockContext& operator=(const UnlockContext& rhs) { CopyFrom(rhs); return *this; }
    private:
        WalletModel *wallet;
        bool valid;
        mutable bool was_locked; // mutable, as it can be set to false by copying
        mutable bool was_mixing; // mutable, as it can be set to false by copying

        void CopyFrom(const UnlockContext& rhs);
    };

    UnlockContext requestUnlock(bool fForMixingOnly=false);

    bool getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;
    bool havePrivKey(const CKeyID &address) const;
    void getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs);
    bool isSpent(const COutPoint& outpoint) const;
    void listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const;

    bool isFrozenCoin(uint256 hash, unsigned int n) const;
    void freezeCoin(COutPoint& output);
    void unfreezeCoin(COutPoint& output);
    void listFrozenCoins(std::vector<COutPoint>& vOutpts);

    void loadReceiveRequests(std::vector<std::string>& vReceiveRequests);
    bool saveReceiveRequest(const std::string &sAddress, const int64_t nId, const std::string &sRequest);

    bool transactionCanBeAbandoned(uint256 hash) const;
    bool abandonTransaction(uint256 hash) const;

    bool hdEnabled() const;

	void setWalletView(WalletView *walletView);

private:
    CWallet *wallet;
    bool fHaveWatchOnly;
    bool fForceCheckBalanceChanged;

    // Wallet has an options model for wallet-specific options
    // (transaction fee, for example)
    OptionsModel *optionsModel;

    //assets name
    QMap<QString,AssetsDisplayInfo> assetsNamesInfo;

    AddressTableModel *addressTableModel;
    TransactionTableModel *transactionTableModel;
    TransactionTableModel *lockedTransactionTableModel;
    TransactionTableModel *candyTableModel;
    TransactionTableModel *assetsDistributeTableModel;
    TransactionTableModel *applicationsRegistTableModel;
    RecentRequestsTableModel *recentRequestsTableModel;

    // Cache some values to be able to detect changes
    CAmount cachedBalance;
    CAmount cachedUnconfirmedBalance;
    CAmount cachedImmatureBalance;
    CAmount cachedLockedBalance;
    CAmount cachedAnonymizedBalance;
    CAmount cachedWatchOnlyBalance;
    CAmount cachedWatchUnconfBalance;
    CAmount cachedWatchImmatureBalance;
    CAmount cachedWatchLockedBalance;
    EncryptionStatus cachedEncryptionStatus;
    int cachedNumBlocks;
    int cachedTxLocks;
    int cachedPrivateSendRounds;

	WalletView *pWalletView;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();
    void checkBalanceChanged(bool copyTmp=false);

Q_SIGNALS:
    // Signal that balance in wallet changed
    void balanceChanged(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& lockedBalance, const CAmount& anonymizedBalance,
                        const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance, const CAmount& watchLockedBalance);

    // Encryption status of wallet changed
    void encryptionStatusChanged(int status);

    // Signal emitted when wallet needs to be unlocked
    // It is valid behaviour for listeners to keep the wallet locked after this signal;
    // this means that the unlocking failed or was cancelled.
    void requireUnlock(bool fForMixingOnly=false);

    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);

    // Coins sent: from wallet, to recipient, in (serialized) transaction:
    void coinsSent(CWallet* wallet, SendCoinsRecipient recipient, QByteArray transaction);

    // Show progress dialog e.g. for rescan
    void showProgress(const QString &title, int nProgress);

    // Watch-only address added
    void notifyWatchonlyChanged(bool fHaveWatchonly);

    void updateConfirm();

public Q_SLOTS:
    /* Wallet status might have changed */
    void updateStatus();
    /* New transaction, or transaction changed status */
    void updateTransaction();
    /* New, updated or removed address book entry */
    void updateAddressBook(const QString &address, const QString &label, bool isMine, const QString &purpose, int status);
    /* Watch-only added */
    void updateWatchOnlyFlag(bool fHaveWatchonly);
    /* Current, immature or unconfirmed balance might have changed - emit 'balanceChanged' if so */
    void pollBalanceChanged();
    void updateAllBalanceChanged(bool copyTmp=false);
};

class EncryptWorker: public QObject {
    Q_OBJECT
public:
    EncryptWorker(QObject* parent, WalletModel *m, const SecureString &ph) {model = m; passphrase = ph;}

Q_SIGNALS:
    void resultReady(const bool result);
    void updateChange();

public Q_SLOTS:
    void doEncrypt();

private:
    WalletModel *model;
    SecureString passphrase;
};

#endif // BITCOIN_QT_WALLETMODEL_H
