#ifndef CUSTOMLINEEDIT_H
#define CUSTOMLINEEDIT_H
#include <QLineEdit>
#include <QString>
#include <QMessageBox>
#include <QDebug>
#include "assetspage.h"

class CustomLineEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit CustomLineEdit(QWidget *parent);
    ~CustomLineEdit();

protected Q_SLOTS:
    virtual void editingFinishDone();

private:
    AssetsPage *assetsPage;
    QString msgboxTitle;

};
#endif // CUSTOMLINEEDIT_H
