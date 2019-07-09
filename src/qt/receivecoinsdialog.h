// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_RECEIVECOINSDIALOG_H
#define BITCOIN_QT_RECEIVECOINSDIALOG_H

#include "guiutil.h"
#include "app/app.h"
#include "validation.h"

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

	void clearData();

Q_SIGNALS:
    void refreshAssetsInfo(QMap<QString, CAssetId_AssetInfo_IndexValue> mapAssetInfo);

public Q_SLOTS:
    void clear();
    void reject();
    void accept();
    void updateAssetsInfo(QMap<QString, CAssetId_AssetInfo_IndexValue> mapAssetInfo);
    void updateAssetsFound(std::vector<uint256> listAssetId);

    void on_reqLabel_textChanged(const QString &address);
    void on_reqMessage_textChanged(const QString &address);

protected:
    virtual void keyPressEvent(QKeyEvent *event);

public:
    QStack<uint256> assetToUpdate;
    QMap<QString,CAssetData> assetDataMap;
	bool bFirstInit;

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

    QModelIndex selectedRow();
    void copyColumnToClipboard(int column);
    virtual void resizeEvent(QResizeEvent *event);
    void initWidget();

	void addSafeToCombox();

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
