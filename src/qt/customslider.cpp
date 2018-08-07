#include "customslider.h"
#include <QLabel>
#include <QMouseEvent>

CustomSlider::CustomSlider(QWidget *parent)
    :QSlider(parent)
{
   setOrientation(Qt::Horizontal);
   sliderWidth=455,sliderHeight=40;
   setFixedSize(sliderWidth,sliderHeight);

    //background:qradialgradient(spread:pad,cx:0.5,cy:0.5,radius:0.5,fx:0.5,fy:0.5,stop:0.6 #444444,stop:0.8 #242424);
   setStyleSheet("QSlider::groove:horizontal{ \
                                                 border:1px solid #999999;\
                                                 height:7px;\
                                                 border-radius:4px;\
                                                 margin-bottom:0px;\
                                                 padding-bottom:0px;\
                                                 } \
                             QSlider::handle:horizontal{ \
                                                 width:10px;\
                                                 margin: -6px 0px -6px 0px;\
                                                 border-bottom:0px;\
                                                 padding-bottom:0px;\
                                                 border-radius:4px;\
                                                 border-image:url(':/icons/light/slider') 0 0 0 0 center no-repeat;\
                                                  }");

    valueLabel=new QLabel(this);
    valueLabel->setFixedSize(QSize(40,10));
    QString labelStyle = "QLabel{qproperty-alignment: 'AlignVCenter | AlignLeft';max-width:40px;max-height:10px}";
    valueLabel->setStyleSheet(labelStyle);
    initSlider(1,100,20);
    initLabel();
    connect(this, SIGNAL(valueChanged(int)), this, SLOT(setValueLabel(int)));
}

CustomSlider::~CustomSlider()
{

}

void CustomSlider::setSize(int w, int h)
{
    sliderWidth=w,sliderHeight=h;
    setFixedSize(sliderWidth,sliderHeight);
}

void CustomSlider::initSlider(int min, int max, int value)
{
    setMinimum(min);
    setMaximum(max);
    setValue(value);
}

void CustomSlider::initLabel(int fixedHeight)
{
    labelY = sliderHeight/2+fixedHeight;
    valueLabel->setText(QString("%1%").arg(value()));
    valueLabel->move(getLabelX(),labelY);
}

int CustomSlider::getLabelX()
{
    int v = value();
    int w = width();
    double percent = (double)v/(maximum()-minimum()+1);
    int x  = percent*w;
    if(v<10)
        x = x-5;
    else if(v<97)
        x = x-10;
    else if(v<99)
        x = x-18;
    else if(v<100)
        x = x-23;
    else
        x = x-35;
    return x;
}

void CustomSlider::mousePressEvent(QMouseEvent *event)
{
    QSlider::mousePressEvent(event);
    valueLabel->move(getLabelX(),labelY);
    valueLabel->setText(QString("%1%").arg(value()));
}

void CustomSlider::mouseReleaseEvent(QMouseEvent *event)
{
    QSlider::mouseReleaseEvent(event);
    valueLabel->move(getLabelX(),labelY);
    valueLabel->setText(QString("%1%").arg(value()));
}

void CustomSlider::mouseMoveEvent(QMouseEvent *event)
{
    valueLabel->move(getLabelX(),labelY);
    valueLabel->setText(QString("%1%").arg(value()));
    QSlider::mouseMoveEvent(event);
}

void CustomSlider::wheelEvent(QWheelEvent *event)
{
    QSlider::wheelEvent(event);
    valueLabel->move(getLabelX(),labelY);
    valueLabel->setText(QString("%1%").arg(value()));
}


void CustomSlider::setValueLabel(int)
{
    valueLabel->move(getLabelX(),labelY);
    valueLabel->setText(QString("%1%").arg(value()));
}
