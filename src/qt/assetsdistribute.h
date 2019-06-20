#ifndef ASSETSDISTRIBUTE_H
#define ASSETSDISTRIBUTE_H

#include <QWidget>
#include <QCompleter>
#include <QStringListModel>
#include "amount.h"

namespace Ui {
    class AssetsDistribute;
}

class AssetsPage;

class AssetsDistribute:public QWidget
{
    Q_OBJECT

public:
    explicit AssetsDistribute(AssetsPage* assetsPage);
    ~AssetsDistribute();

    void setThreadUpdateData(bool update){fThreadUpdateData = update;}
    bool getThreadUpdateData(){return fThreadUpdateData;}
    void setAssetStringList(QStringList stringList){assetStringList = stringList;}

Q_SIGNALS:
    void refreshAssetsInfo();

private:
    void clearDisplay();
    void initWidget();
    void initFirstDistribute();
    void initAdditionalDistribute();
    void displayFirstDistribute(bool dis);
    bool existAssetsName(const std::string& assetsName);
    bool invalidInput();
    bool distributeAssets();
    bool addAssets();
    void updateCandyValue();
    bool amountFromString(const std::string& valueStr,int decimal,CAmount& amount,bool* invalidDecimal=NULL);
    void updateTotalAssetsEdit();

public Q_SLOTS:
    void updateAssetsInfo();

private Q_SLOTS:
    void on_distributeComboBox_currentIndexChanged(int index);

    void on_distributeButton_clicked();

    void on_detectExistButton_clicked();

    void on_assetsCandyRatioSlider_valueChanged(int value);

    void on_firstAssetsEdit_textChanged(const QString &arg1);

    void on_distributeCheckBox_clicked(bool checked);

    void on_totalAssetsEdit_textChanged(const QString &arg1);

    void handlerExpireLineEditTextChange(const QString &text);

    void handlerAssetsNameComboBoxTextChange(const QString &text);
private:
    Ui::AssetsDistribute *ui;
    AssetsPage* assetsPage;
    QString msgboxTitle;
    QCompleter* completer;
    QStringListModel* stringListModel;
    bool fThreadUpdateData;
    QStringList assetStringList;
};

#endif // ASSETSDISTRIBUTE_H
