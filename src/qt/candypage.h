#ifndef CANDYPAGE_H
#define CANDYPAGE_H

#include "platformstyle.h"
#include "walletmodel.h"
#include "validation.h"
#include "amount.h"
#include <QWidget>
#include <QTimer>
#include <QCompleter>
#include <QStringListModel>
#include <QThread>
#include <QMessageBox>
#include <vector>

namespace Ui {
    class CandyPage;
}

class QVBoxLayout;
class CandyView;
class ClientModel;
class WalletModel;
class CCandy_BlockTime_Info;
class QPushButton;
class GetCandyWorker;
struct CCandy_BlockTime_InfoVec;

/** Widget that shows a list of sending or receiving candys
  */
class CandyPage : public QWidget
{
    Q_OBJECT

    friend class GetCandyWorker;
public:
    explicit CandyPage();
    ~CandyPage();
    void setGetHistoryTabLayout(QVBoxLayout* layout);
    void setClientModel(ClientModel *model);
    void setModel(WalletModel *model);
    bool amountFromString(const std::string& valueStr,const QString& msgboxTitle,int decimal,CAmount& amount);
    void setThreadUpdateData(bool update){fThreadUpdateData = update;}
    void setThreadNoticeSlot(bool notice){fThreadNoticeSlot = notice;}
    bool getThreadUpdateData(){return fThreadUpdateData;}
    bool getThreadNoticeSlot(){return fThreadNoticeSlot;}
    void setAssetStringList(QStringList stringList){assetStringList = stringList;}

private:
    bool putCandy();
    void updateCandyValue();
    void clear();
    void getCandy(QPushButton* btn);
    bool invalidInput();
    void removeRow(QPushButton* btn);
    bool existedCandy(QWidget* widget,const QString &strAssetId, const qint64 &candyAmount
                      ,const quint16 &nExpired, const QString &strHash, const quint32 &nIndex);
    void updatePage(bool gotoLastPage=false);
    void copyVec();
    void updateCurrentPage();
    void eraseCandy(int rowNum);

Q_SIGNALS:
    void stopThread();

public Q_SLOTS:
    void handlerGetCandyResult(const bool result, const QString errorStr, const int rowNum, const CAmount nFeeRequired);
    void updateAssetsInfo();

private Q_SLOTS:
    void on_okButton_clicked();
    void updateCandyInfo(const QString &assetName);
    void on_candyRatioSlider_valueChanged(int value);
    void getCandy();
    void updateGetCandyList();
    void updateCandyListWidget(const QString& strAssetId,const QString& assetName,const quint8& nDecimals,const qint64& candyAmount,const quint16& nExpired,
                                                                    const QString& dateStr,const QString& strHash,const quint32& nIndex,int currRow=-1);

    void on_firstBtn_clicked();

    void on_nextBtn_clicked();

    void on_priorBtn_clicked();

    void on_lastBtn_clicked();

    void on_skipBtn_clicked();

    void on_spinBox_editingFinished();

    void handlerExpireLineEditTextChange(const QString &text);

private:
    Ui::CandyPage *ui;
    ClientModel *clientModel;
    WalletModel *model;
    int commentsMaxLength;
    QString strPutCandy;
    QString strGetCandy;
    CAmount currAssetTotalAmount;
    int currAssetDecimal;
    int nAssetNameColumn;
    int nCandyAmountColumn;
    int nExpiredColumn;
    int nBtnColumn;
    QCompleter* completer;
    QStringListModel* stringListModel;
    int sliderFixedHeight;
    int nPageCount;
    int nCurrPage;
    std::vector<CCandy_BlockTime_Info> tmpAllCandyInfoVec;
    QThread* getCandyThread;
    GetCandyWorker *candyWorker;
    QMessageBox *msgbox;

    bool isUnlockByGlobal;
    bool fThreadUpdateData;
    bool fThreadNoticeSlot;
    QStringList assetStringList;
Q_SIGNALS:
    void runGetCandy();
    void refreshAssetsInfo();
};

class GetCandyWorker: public QObject {
    Q_OBJECT
public:
    GetCandyWorker(QObject* parent) {
        parnt = (CandyPage *)parent;
    }
    void init(uint256 _assetId,const COutPoint& _out, int _nTxHeight, const CAmount &_nTotalSafe,
              const CCandyInfo &_candyInfo, const CAssetId_AssetInfo_IndexValue &_assetInfo, int _rowNum)
    {
        assetId = _assetId;
        out = _out;
        nTxHeight = _nTxHeight;
        rowNum = _rowNum;
        nTotalSafe = _nTotalSafe;
        candyInfo = _candyInfo;
        assetInfo = _assetInfo;
    }
    static const int uselessArg = -1; //when resultReady's parameter pass uselessArg, it means the parameter should not be used in slot funcs.

Q_SIGNALS:
    void resultReady(const bool result, const QString errstr, const int rowNum, const CAmount nFeeRequired);

public Q_SLOTS:
    void doGetCandy();

private:
    QPushButton *btn;
    CandyPage *parnt;
    uint256 assetId;
    COutPoint out;
    int nTxHeight;
    CAmount nTotalSafe;
    CCandyInfo candyInfo;
    CAssetId_AssetInfo_IndexValue assetInfo;
    int rowNum;
};

#endif // CANDYPAGE_H
