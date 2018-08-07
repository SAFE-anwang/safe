#ifndef FRAMELESSWIDGET_H
#define FRAMELESSWIDGET_H

#include <QMainWindow>

class QMouseEvent;

enum ResizeRegion
{
    Default,
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West,
    NorthWest
};


class FramelessWidget : public QMainWindow
{
    Q_OBJECT
public:
    explicit FramelessWidget(QWidget *parent = 0);
    ~FramelessWidget();
    void setResizeCursor(ResizeRegion region);
    ResizeRegion getResizeRegion(QPoint clientPos);
    void handleMove(QPoint pt);
    virtual void handleResize();

public Q_SLOTS:
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent * event);
    virtual void mouseReleaseEvent(QMouseEvent *event);

public:
    bool dragFlag;
    bool moveFlag;
    QPoint lastPos;
    QPoint dragPos;
    QPoint resizeDownPos;
    int resizeBorderWidth;
    ResizeRegion resizeRegion;
    QRect mouseDownRect;
    bool bMaximum;
};

#endif // FRAMELESSWIDGET_H
