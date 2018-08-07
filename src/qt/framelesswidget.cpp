#include "framelesswidget.h"
#include <QMouseEvent>
#include <QApplication>
#include <QDesktopWidget>
#include <QMessageBox>

FramelessWidget::FramelessWidget(QWidget *parent)
    :QMainWindow(parent)
{
    setMouseTracking(true);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint);
    resizeBorderWidth = 5;
    dragFlag = false;
    moveFlag = false;
    dragPos = QPoint(0,0);
    resizeRegion = Default;
    mouseDownRect = QRect();
    bMaximum = false;
}

FramelessWidget::~FramelessWidget()
{

}

void FramelessWidget::mousePressEvent(QMouseEvent *event)
{
    if(bMaximum)
        return;
    if (event->button() == Qt::LeftButton) {
        dragFlag = true;
        dragPos = event->pos();
        resizeDownPos = event->globalPos();
        mouseDownRect = rect();
    }
}

void FramelessWidget::mouseMoveEvent(QMouseEvent * event)
{
    if(bMaximum)
        return;
    if (resizeRegion != Default)
    {
        handleResize();
        return;
    }
    if(moveFlag) {
        move(event->globalPos() - dragPos);
        return;
    }
    QPoint clientCursorPos = event->pos();
    QRect r = rect();
    QRect resizeInnerRect(resizeBorderWidth, resizeBorderWidth, r.width() - 2*resizeBorderWidth, r.height() - 2*resizeBorderWidth);
    if(r.contains(clientCursorPos) && !resizeInnerRect.contains(clientCursorPos)) { //adjust
        ResizeRegion resizeReg = getResizeRegion(clientCursorPos);
        setResizeCursor(resizeReg);
        if (dragFlag && (event->buttons() & Qt::LeftButton)) {
            resizeRegion = resizeReg;
            handleResize();
        }
    }
    else { //move
        setCursor(Qt::ArrowCursor);
        if (dragFlag && (event->buttons() & Qt::LeftButton)) {
            moveFlag = true;
            move(event->globalPos() - dragPos);
        }
    }
}

void FramelessWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if(bMaximum)
        return;
    dragFlag = false;
    if(moveFlag) {
        moveFlag = false;
        handleMove(event->globalPos());
    }
    resizeRegion = Default;
    setCursor(Qt::ArrowCursor);
}

void FramelessWidget::setResizeCursor(ResizeRegion region)
{
    switch (region)
    {
    case North:
    case South:
        setCursor(Qt::SizeVerCursor);
        break;
    case East:
    case West:
        setCursor(Qt::SizeHorCursor);
        break;
    case NorthWest:
    case SouthEast:
        setCursor(Qt::SizeFDiagCursor);
        break;
    default:
        setCursor(Qt::SizeBDiagCursor);
        break;
    }
}

ResizeRegion FramelessWidget::getResizeRegion(QPoint clientPos)
{
    if (clientPos.y() <= resizeBorderWidth) {
        if (clientPos.x() <= resizeBorderWidth)
            return NorthWest;
        else if (clientPos.x() >= width() - resizeBorderWidth)
            return NorthEast;
        else
            return North;
    }
    else if (clientPos.y() >= height() - resizeBorderWidth) {
        if (clientPos.x() <= resizeBorderWidth)
            return SouthWest;
        else if (clientPos.x() >= width() - resizeBorderWidth)
            return SouthEast;
        else
            return South;
    } else {
        if (clientPos.x() <= resizeBorderWidth)
            return West;
        else
            return East;
    }
}
void FramelessWidget::handleMove(QPoint pt)
{
    QPoint currentPos = pt - dragPos;
    QDesktopWidget* desktop = QApplication::desktop();
    if(currentPos.x()<desktop->x()) {
        currentPos.setX(desktop->x());
    }
    else if (currentPos.x()+width()>desktop->width()) {
        currentPos.setX(desktop->width()-width());
    }
    if(currentPos.y()<desktop->y()) {
        currentPos.setY(desktop->y());
    }
    move(currentPos);
}
void FramelessWidget::handleResize()
{
    int xdiff = QCursor::pos().x() - resizeDownPos.x();
    int ydiff = QCursor::pos().y() - resizeDownPos.y();
    switch (resizeRegion)
    {
    case East:
    {
        resize(mouseDownRect.width()+xdiff, height());
        break;
    }
    case West:
    {
        resize(mouseDownRect.width()-xdiff, height());
        move(resizeDownPos.x()+xdiff, y());
        break;
    }
    case South:
    {
        resize(width(),mouseDownRect.height()+ydiff);
        break;
    }
    case North:
    {
        resize(width(),mouseDownRect.height()-ydiff);
        move(x(), resizeDownPos.y()+ydiff);
        break;
    }
    case SouthEast:
    {
        resize(mouseDownRect.width() + xdiff, mouseDownRect.height() + ydiff);
        break;
    }
    case NorthEast:
    {
        resize(mouseDownRect.width()+xdiff, mouseDownRect.height()-ydiff);
        move(x(), resizeDownPos.y()+ydiff);
        break;
    }
    case NorthWest:
    {
        resize(mouseDownRect.width()-xdiff, mouseDownRect.height()-ydiff);
        move(resizeDownPos.x()+xdiff, resizeDownPos.y()+ydiff);
        break;
    }
    case SouthWest:
    {
        resize(mouseDownRect.width()-xdiff, mouseDownRect.height()+ydiff);
        move(resizeDownPos.x()+xdiff, y());
        break;
    }
    default:
    {
        break;
    }
    }
}
