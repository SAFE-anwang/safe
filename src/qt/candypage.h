#ifndef CANDYPAGE_H
#define CANDYPAGE_H

#include "platformstyle.h"
#include "amount.h"
#include <QWidget>
#include <QTimer>
#include <QCompleter>
#include <QStringListModel>

namespace Ui {
    class CandyPage;
}

class QVBoxLayout;
class CandyView;
class ClientModel;
class WalletModel;
class CCandy_BlockTime_Info;
class QPushButton;
struct CCandy_BlockTime_InfoVec;

/** Widget that shows a list of sending or receiving candys
  */
class CandyPage : public QWidget
{
    Q_OBJECT

public:
    explicit CandyPage();
    ~CandyPage();
    void setGetHistoryTabLayout(QVBoxLayout* layout);
    void setClientModel(ClientModel *model);
    void setModel(WalletModel *model);
    void updateAssetsInfo();
    bool amountFromString(const std::string& valueStr,const QString& msgboxTitle,int decimal,CAmount& amount);

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
};

#endif // CANDYPAGE_H
