#include"customlineedit.h"

CustomLineEdit::CustomLineEdit(QWidget *parent):
    QLineEdit(parent),
    assetsPage(0)
{
    msgboxTitle = tr("Input info allert");
    connect(this, SIGNAL(textChanged(QString)), this, SLOT(editingFinishDone()));
    setStyleSheet("QLineEdit{qproperty-alignment: 'AlignVCenter | AlignLeft';\
                  min-width:160px;\
                  max-width:450px;\
                  min-height:26px;\
                  max-height:50px;\
                  font-size:12px;\
                  background-color:none;\
                  margin-right:5px;\
                  padding-right:5px\
                 };");
}

CustomLineEdit::~CustomLineEdit()
{

}

void CustomLineEdit::editingFinishDone()
{
    QLineEdit::editingFinished();
    int len = this->maxLength();
    int iInLen = this->text().trimmed().toLocal8Bit().length();
    if(iInLen > len)
    {
        QMessageBox::warning(assetsPage, msgboxTitle,tr("Input information too long"),tr("Ok"));
    }
}
