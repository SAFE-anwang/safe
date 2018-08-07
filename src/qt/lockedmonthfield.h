#ifndef BITCOIN_QT_LOCKEDMONTHFIELD_H
#define BITCOIN_QT_LOCKEDMONTHFIELD_H

#include <QWidget>
#include <QSpinBox>

/** Widget for entering Safe locked month
  */
class LockedMonthField: public QWidget
{
    Q_OBJECT

    Q_PROPERTY(int value READ value WRITE setValue NOTIFY textChanged USER true)

public:
    explicit LockedMonthField(QWidget *parent = 0);

    int value(bool *valij=0) const;
    void setValue(int value);

    /** Mark current value as invalid in UI. */
    void setValid(bool valid);
    /** Perform input validation, mark field as invalid if entered value is not valid. */
    bool validate();

    /** Make field empty and ready for new input. */
    void clear();

    /** Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907),
        in these cases we have to set it up manually.
    */
    QWidget *setupTabChain(QWidget *prev);

Q_SIGNALS:
    void textChanged();

protected:
    /** Intercept focus-in event and ',' key presses */
    bool eventFilter(QObject *object, QEvent *event);

private:
    QSpinBox *amount;

    void setText(const QString &text);
    QString text() const;
};

#endif // BITCOIN_QT_LOCKEDMONTHFIELD_H
