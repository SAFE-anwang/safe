#include "lockedmonthfield.h"

#include "guiconstants.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>

#define MAX_MN_LOCKED_MONTH 120

LockedMonthField::LockedMonthField(QWidget *parent):
        QWidget(parent), amount(0)
{
    amount = new QSpinBox(this);
    amount->setLocale(QLocale::c());
    amount->installEventFilter(this);
    amount->setRange(1, MAX_MN_LOCKED_MONTH);
    amount->setMaximumWidth(200);
    amount->setSingleStep(1);
    amount->setValue(6);
    amount->setWrapping(true);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->addWidget(amount);
    layout->addStretch(1);
    layout->setContentsMargins(0,0,0,0);

    setLayout(layout);

    setFocusPolicy(Qt::TabFocus);
    setFocusProxy(amount);

    // If one if the widgets changes, the combined content changes as well
    connect(amount, SIGNAL(valueChanged(QString)), this, SIGNAL(textChanged()));
}

void LockedMonthField::setText(const QString &text)
{
    if (text.isEmpty())
        amount->clear();
    else
        amount->setValue(text.toInt());
}

void LockedMonthField::clear()
{
    amount->clear();
}

bool LockedMonthField::validate()
{
    bool valid = true;
    if (amount->value() <= 0 || amount->value() > MAX_MN_LOCKED_MONTH)
        valid = false;

    setValid(valid);

    return valid;
}

void LockedMonthField::setValid(bool valid)
{
    if (valid)
        amount->setStyleSheet("");
    else
        amount->setStyleSheet(STYLE_INVALID);
}

QString LockedMonthField::text() const
{
    if (amount->text().isEmpty())
        return QString();
    else
        return amount->text();
}

bool LockedMonthField::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        // Clear invalid flag on focus
        setValid(true);
    }
    else if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Comma)
        {
            // Translate a comma into a period
            QKeyEvent periodKeyEvent(event->type(), Qt::Key_Period, keyEvent->modifiers(), ".", keyEvent->isAutoRepeat(), keyEvent->count());
            qApp->sendEvent(object, &periodKeyEvent);
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

QWidget *LockedMonthField::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, amount);
    return amount;
}

int LockedMonthField::value(bool *valid_out) const
{
    int val_out = text().toInt();
    if(valid_out)
    {
        *valid_out = true;
    }
    return val_out;
}

void LockedMonthField::setValue(int value)
{
    setText(QString::number(value));
}
