#ifndef CUSTOMDOUBLEVALIDATOR_H
#define CUSTOMDOUBLEVALIDATOR_H

#include <QValidator>
#include <math.h>

//to fix QDoubleValidate not take effect
class CustomDoubleValidator: public QDoubleValidator
{
public:
    explicit CustomDoubleValidator(QObject * parent = 0)
        :QDoubleValidator(parent)
    {
    }

    CustomDoubleValidator(double bottom, double top, int decimals=0, QObject *parent = 0)
        :QDoubleValidator(bottom,top,decimals,parent)
    {
    }

    virtual State validate(QString &str, int &) const
    {
        if (str.isEmpty())
            return QValidator::Intermediate;

        bool cOK = false;
        double val = str.toDouble(&cOK);

        if (!cOK)
            return QValidator::Invalid;

        int dotPos = str.indexOf(".");
        if (dotPos > 0)
            if (str.right(str.length() - dotPos-1).length() > decimals())
                return QValidator::Invalid;
        //reject put decimal before num;eg .4568
        if (dotPos == 0)
            if (str.right(str.length() - 1).length() > 0)
                return QValidator::Invalid;
        //reject muti-zero befor decimal inter part,like 00.1
        int nPow = pow(10,dotPos-1);
        if (dotPos > 1)
            if (val < nPow)
                return QValidator::Invalid;

        if (val<= top() && val >= bottom())
            return QValidator::Acceptable;

        return QValidator::Invalid;
    }
};

#endif // CUSTOMDOUBLEVALIDATOR_H
