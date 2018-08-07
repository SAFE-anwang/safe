#include "assetslogin.h"
#include "ui_assetslogin.h"
#include "assetspage.h"

AssetsLogin::AssetsLogin(AssetsPage *assetsPage):
    ui(new Ui::AssetsLogin)
{
    this->assetsPage = assetsPage;
    ui->setupUi(this);
}

AssetsLogin::~AssetsLogin()
{

}

void AssetsLogin::on_nextButton_clicked()
{
    //assetsPage->gotoNext(assetsStepType);
}

void AssetsLogin::on_previousButton_clicked()
{
    //assetsPage->gotoPrior(assetsStepType);
}
