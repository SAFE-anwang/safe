#include "titlebar.h"
#include "guiutil.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QFile>
#include <QMouseEvent>
#include <QMessageBox>
#include <QApplication>
#include <QDesktopWidget>
#include <iostream>

#define BUTTON_HEIGHT   35
#define BUTTON_WIDTH    40
#define TITLE_HEIGHT    43

#define LEFT_MARGIN     10
#define TOP_MARGIN      0
#define RIGHT_MARGIN    0
#define BOTTOM_MARGIN   1

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
    , fatherWidget(parent)
    , bottomWidget(0)
    , red(255)
    , green(255)
    , bule(255)
    , fPressed(false)
    , buttonType(MIN_MAX_BUTTON)
    , borderWidth()
{
    initControl();
    initConnections();
    setQSS();
    setMouseTracking(true);
}

TitleBar::~TitleBar()
{
}

void TitleBar::initControl()
{
    // 1. icon
    iconLabel = new QLabel;

    // 2. content
    contentLabel = new QLabel;
    contentLabel->setObjectName(QStringLiteral("TitleContent"));

    bottomLabel = new QLabel;

    QFont font;
    font.setPixelSize(gBaseFontSize + 3);
    contentLabel->setFont(font);

    // 3. menu button
    fileButton = new QPushButton(tr("&File"));
    settingsButton = new QPushButton(tr("&Settings"));
    toolsButton = new QPushButton(tr("&Tools"));
    helpButton = new QPushButton(tr("&Help"));

    QSize buttonSize(55,22);
    QRect screenRect = QApplication::desktop()->screenGeometry();
    if(screenRect.width()>3000){
        buttonSize.setWidth(buttonSize.width()+10);
    }

    QSize settingsButtonSize(buttonSize.width()+5,buttonSize.height());
    fileButton->setFixedSize(buttonSize);
    settingsButton->setFixedSize(settingsButtonSize);
    toolsButton->setFixedSize(buttonSize);
    helpButton->setFixedSize(buttonSize);

    font.setPixelSize(gBaseFontSize + 2);
    fileButton->setFont(font);
    settingsButton->setFont(font);
    helpButton->setFont(font);

    // 4. min/max/restore/close button
    minButton = new QPushButton;
    restoreButton = new QPushButton;
    maxButton = new QPushButton;
    closeButton = new QPushButton;

    minButton->setFixedSize(QSize(BUTTON_WIDTH, BUTTON_HEIGHT));
    restoreButton->setFixedSize(QSize(BUTTON_WIDTH, BUTTON_HEIGHT));
    maxButton->setFixedSize(QSize(BUTTON_WIDTH, BUTTON_HEIGHT));
    closeButton->setFixedSize(QSize(BUTTON_WIDTH, BUTTON_HEIGHT));

    minButton->setObjectName("MinButton");
    restoreButton->setObjectName("RestoreButton");
    maxButton->setObjectName("MaxButton");
    closeButton->setObjectName("CloseButton");

    minButton->setToolTip(tr("Minimize"));
    restoreButton->setToolTip(tr("Restore"));
    maxButton->setToolTip(tr("Maximize"));
    closeButton->setToolTip(tr("Close and exit"));

    // 5. layout
    QHBoxLayout* topLayout = new QHBoxLayout;
    topLayout->addWidget(iconLabel);
    iconLabel->setMouseTracking(true);
    topLayout->addWidget(contentLabel);
    contentLabel->setMouseTracking(true);
    contentLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    topLayout->addWidget(minButton);
    minButton->setMouseTracking(true);
    topLayout->addWidget(restoreButton);
    restoreButton->setMouseTracking(true);
    topLayout->addWidget(maxButton);
    maxButton->setMouseTracking(true);
    topLayout->addWidget(closeButton);
    closeButton->setMouseTracking(true);
    topLayout->setMargin(0);
    topLayout->setContentsMargins(QMargins());

    QHBoxLayout* bottomLayout = new QHBoxLayout;
    bottomWidget = new QWidget;
    bottomLayout->addWidget(fileButton);
    fileButton->setMouseTracking(true);
    bottomLayout->addWidget(settingsButton);
    settingsButton->setMouseTracking(true);
    bottomLayout->addWidget(toolsButton);
    toolsButton->setMouseTracking(true);
    bottomLayout->addWidget(helpButton);
    helpButton->setMouseTracking(true);
    bottomLayout->addWidget(bottomLabel);
    bottomLabel->setMouseTracking(true);
    bottomLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bottomLayout->setMargin(0);
    bottomLayout->setContentsMargins(QMargins(10,0,0,0));
    bottomWidget->setLayout(bottomLayout);
    bottomWidget->setMouseTracking(true);
    int bottomHeight = buttonSize.height();
    bottomWidget->setFixedHeight(bottomHeight);
    bottomWidget->setWindowFlags(Qt::FramelessWindowHint);

    QVBoxLayout* mainLayout = new QVBoxLayout;
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(bottomWidget);
    //mainLayout->setContentsMargins(LEFT_MARGIN, TOP_MARGIN, RIGHT_MARGIN, BOTTOM_MARGIN);
    mainLayout->setMargin(0);
    mainLayout->setContentsMargins(QMargins());

    this->setFixedHeight(BUTTON_HEIGHT+bottomHeight);
    setLayout(mainLayout);
    this->setWindowFlags(Qt::FramelessWindowHint);
}

void TitleBar::initConnections()
{
    connect(minButton, SIGNAL(clicked()), this, SLOT(onMinButtonClicked()));
    connect(restoreButton, SIGNAL(clicked()), this, SLOT(onRestoreButtonClicked()));
    connect(maxButton, SIGNAL(clicked()), this, SLOT(onMaxButtonClicked()));
    connect(closeButton, SIGNAL(clicked()), this, SLOT(onCloseButtonClicked()));
}

void TitleBar::setFullScreen()
{
    onMaxButtonClicked();
}

void TitleBar::setQSS()
{
    iconLabel->setPixmap(QIcon(":/icons/bitcoin").pixmap(20, 20));
    iconLabel->setStyleSheet("QLabel{padding-left:10px;padding-top:0px;padding-right:0px;border-top: 5px solid #fff;}");
    contentLabel->setStyleSheet("QLabel{padding-top:6px;padding-right:0px;border: 0px solid #fff;}");
    bottomWidget->setStyleSheet("QWidget{ background: #0f3258; border: 0px; padding: 0px;margin-top:0px;margin-bottom:0px;padding-top:0px;padding-bottom:0px;}"
                                                        "QPushButton{ border: 0px;padding: 0px;margin-top:0px;margin-bottom:0px;padding-top:0px;padding-bottom:0px;text-align: center;}"
                                                        "QPushButton:hover{ background: #1b4169; }"
                                                        );

    QString theme = GUIUtil::getThemeName();
    QString margin = "6";
    setStyleSheet("QWidget{ background: rgb(255, 255, 255); }"
                  "QPushButton::menu-indicator{ image: None; }"
                  "QPushButton{ border: 0px; padding: 0px; }"
                  "QPushButton:hover{ background: rgb(202, 232, 255);}"
                  "QPushButton:pressed{ background: rgb(145, 201, 247);}"
                  "QPushButton#MinButton{ border-image:url(:/icons/"+theme+"/min) 0 120 0 0 round;margin:"+margin+"px;}"
                  "QPushButton#MinButton:hover{ border-image:url(:/icons/"+theme+"/min) 0 80 0 40 round;margin:"+margin+"px;}"
                  "QPushButton#MinButton:pressed{ border-image:url(:/icons/"+theme+"/min) 0 40 0 80 round;margin:"+margin+"px;}"
                  "QPushButton#MaxButton{ border-image:url(:/icons/"+theme+"/max) 0 120 0 0; margin:"+margin+"px;}"
                  "QPushButton#MaxButton:hover{ border-image:url(:/icons/"+theme+"/max) 0 80 0 40; margin:"+margin+"px;}"
                  "QPushButton#MaxButton:pressed{ border-image:url(:/icons/"+theme+"/max) 0 40 0 80; margin:"+margin+"px;}"
                  "QPushButton#RestoreButton{ border-image:url(:/icons/"+theme+"/restore) 0 120 0 0; margin:"+margin+"px;}"
                  "QPushButton#RestoreButton:hover{ border-image:url(:/icons/"+theme+"/restore) 0 80 0 40; margin:"+margin+"px;}"
                  "QPushButton#RestoreButton:pressed{ border-image:url(:/icons/"+theme+"/restore) 0 40 0 80; margin:"+margin+"px;}"
                  "QPushButton#CloseButton{ border-image:url(:/icons/"+theme+"/close) 0 120 0 0 round; margin:"+margin+"px;}"
                  "QPushButton#CloseButton:hover{ border-image:url(:/icons/"+theme+"/close) 0 80 0 40 round;margin:"+margin+"px;}"
                  "QPushButton#CloseButton:pressed{ border-image:url(:/icons/"+theme+"/close) 0 40 0 80 round;margin:"+margin+"px;}"
                 );
}

void TitleBar::createFileMenu(QMenu* menu)
{
    fileButton->setMenu(menu);
}

void TitleBar::createSettingsMenu(QMenu* menu)
{
    settingsButton->setMenu(menu);
}

void TitleBar::createToolsMenu(QMenu *menu)
{
    toolsButton->setMenu(menu);
}

void TitleBar::createHelpMenu(QMenu* menu)
{
    helpButton->setMenu(menu);
}

void TitleBar::setBackgroundColor(int r, int g, int b)
{
    red = r;
    green = g;
    bule = b;
    update();
}

void TitleBar::setContent(QString content)
{
    contentLabel->setText(content);
    this->content = content;
}

void TitleBar::setWidth(int width)
{
    this->setFixedWidth(width);
}

void TitleBar::setButtonType(ButtonType buttonType)
{
    this->buttonType = buttonType;

    switch (buttonType)
    {
    case MIN_BUTTON:
        {
            restoreButton->setVisible(false);
            maxButton->setVisible(false);
        }
        break;
    case MIN_MAX_BUTTON:
        {
            restoreButton->setVisible(false);
        }
        break;
    case ONLY_CLOSE_BUTTON:
        {
            minButton->setVisible(false);
            restoreButton->setVisible(false);
            maxButton->setVisible(false);
        }
        break;
    default:
        break;
    }
}

void TitleBar::setBorderWidth(int borderWidth)
{
    this->borderWidth = borderWidth;
}

void TitleBar::saveRestoreInfo(const QPoint point, const QSize size)
{
    restorePos = point;
    restoreSize = size;
}

void TitleBar::getRestoreInfo(QPoint& point, QSize& size)
{
    point = restorePos;
    size = restoreSize;
}

void TitleBar::paintEvent(QPaintEvent *event)
{
    int w = fatherWidget->width() - borderWidth;
    QPainter painter(this);
    QPainterPath pathBack;
    pathBack.setFillRule(Qt::WindingFill);
    pathBack.addRoundedRect(QRect(0, 0, w, this->height()), 3, 3);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillPath(pathBack, QBrush(QColor(red, green, bule)));

    QPoint lineStartPos, lineEndPos;
    int height = this->height();

    lineStartPos.setX(0);
    lineStartPos.setY(height - BOTTOM_MARGIN);
    lineEndPos.setX(gToolBarWidth - 1);
    lineEndPos.setY(height - BOTTOM_MARGIN);
    painter.setPen(QColor(52, 73, 94));
    painter.drawLine(lineStartPos, lineEndPos);

    lineStartPos.setX(gToolBarWidth);
    lineStartPos.setY(height - BOTTOM_MARGIN);
    lineEndPos.setX(w);
    lineEndPos.setY(height - BOTTOM_MARGIN);
    painter.setPen(QColor(206, 206, 206));
    painter.drawLine(lineStartPos, lineEndPos);

    if (this->width() != (fatherWidget->width() - borderWidth))
    {
        setFixedWidth(fatherWidget->width() - borderWidth);
    }
    QWidget::paintEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (buttonType == MIN_MAX_BUTTON)
    {
        if (maxButton->isVisible())
        {
            onMaxButtonClicked();
        }
        else
        {
            onRestoreButtonClicked();
        }
    }

    return QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (buttonType == MIN_MAX_BUTTON)
    {
        if (maxButton->isVisible())
        {
            fPressed = true;
            startPos = event->globalPos();
        }
    }
    else
    {
        fPressed = true;
        startPos = event->globalPos();
    }

    return QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (fPressed)
    {
        QPoint movePoint = event->globalPos() - startPos;
        QPoint widgetPos = fatherWidget->pos();
        startPos = event->globalPos();
        fatherWidget->move(widgetPos.x() + movePoint.x(), widgetPos.y() + movePoint.y());
    }
    return QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    fPressed = false;
    return QWidget::mouseReleaseEvent(event);
}

void TitleBar::onMinButtonClicked()
{
    Q_EMIT sigMinButtonClicked();
}

void TitleBar::onRestoreButtonClicked()
{
    restoreButton->setVisible(false);
    maxButton->setVisible(true);
    Q_EMIT sigRestoreButtonClicked();
}

void TitleBar::onMaxButtonClicked()
{
    maxButton->setVisible(false);
    restoreButton->setVisible(true);
    Q_EMIT sigMaxButtonClicked();
}

void TitleBar::onCloseButtonClicked()
{
    Q_EMIT sigCloseButtonClicked();
}
