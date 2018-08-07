#ifndef  CUSTOMSLIDER_H
#define  CUSTOMSLIDER_H
#include <QSlider>
class QLabel;
class QMouseEvent;

class CustomSlider : public QSlider
{
    Q_OBJECT

public:
    CustomSlider(QWidget *parent=0);
    ~CustomSlider();
    void initSlider(int min,int max,int value);
    void setSize(int w, int h);
    void initLabel(int fixedHeight=2);

public Q_SLOTS:
    void setValueLabel(int);

protected:
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseReleaseEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void wheelEvent(QWheelEvent *event);

private:
    int getLabelX();

private:
    QLabel*	valueLabel;
    int labelY;
    int sliderWidth;
    int sliderHeight;
};

#endif // CUSTOMSLIDER_H
