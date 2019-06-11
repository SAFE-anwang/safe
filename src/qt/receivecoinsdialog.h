// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_RECEIVECOINSDIALOG_H
#define BITCOIN_QT_RECEIVECOINSDIALOG_H

#include "guiutil.h"
#include "app/app.h"

#include <QDialog>
#include <QHeaderView>
#include <QItemSelection>
#include <QKeyEvent>
#include <QMenu>
#include <QPoint>
#include <QVariant>
#include <QTimer>
#include <QCompleter>
#include <QStringListModel>
#include <QStack>

class OptionsModel;
class PlatformStyle;
class WalletModel;
class ClientModel;

namespace Ui {
    class ReceiveCoinsDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Dialog for requesting payment of bitcoins */
class ReceiveCoinsDialog : public QDialog
{
    Q_OBJECT

public:
    enum ColumnWidths {
        DATE_COLUMN_WIDTH = 130,
        LABEL_COLUMN_WIDTH = 120,
        AMOUNT_MINIMUM_COLUMN_WIDTH = 160,
        MINIMUM_COLUMN_WIDTH = 130
    };

    explicit ReceiveCoinsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~ReceiveCoinsDialog();

    void setModel(WalletModel *model);
    void setClientModel(ClientModel* clientModel);
    bool getAssetFound(){return fAssetsFound;}
    void setThreadUpdateData(bool update){fThreadUpdateData = update;}
    bool getThreadUpdateData(){return fThreadUpdateData;}
    void setAssetStringList(QStringList stringList){assetStringList = stringList;}

Q_SIGNALS:
    void refreshAssetsInfo();

public Q_SLOTS:
    void clear();
    void reject();
    void accept();
    void updateAssetsInfo(const QString&assetName = "");
    void updateAssetsFound(const QString& assetName);

    void on_reqLabel_textChanged(const QString &address);
    void on_reqMessage_textChanged(const QString &address);

protected:
    virtual void keyPressEvent(QKeyEvent *event);

public:
    QStack<QString> assetToUpdate;
    QMap<QString,CAssetData> assetDataMap;

private:
    Ui::ReceiveCoinsDialog *ui;
    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;
    ClientModel *clientModel;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;
    bool fAssets;
    int assetDecimal;
    QString strAssetUnit;
    QCompleter* completer;
    QStringListModel* stringListModel;
    bool fAssetsFound;
    bool fThreadUpdateData;
    QStringList assetStringList;

    QModelIndex selectedRow();
    void copyColumnToClipboard(int column);
    virtual void resizeEvent(QResizeEvent *event);
    void initWidget();

private Q_SLOTS:
    void on_receiveButton_clicked();
    void on_showRequestButton_clicked();
    void on_removeRequestButton_clicked();
    void on_recentRequestsView_doubleClicked(const QModelIndex &index);
    void recentRequestsView_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void updateDisplayUnit();
    void showMenu(const QPoint &point);
    void copyURI();
    void copyLabel();
    void copyMessage();
    void copyAmount();    
    void updateCurrentAsset(const QString&);

};

#endif // BITCOIN_QT_RECEIVECOINSDIALOG_H
