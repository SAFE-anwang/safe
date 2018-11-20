/* author: lemengbin
 * date: 2017-10-20
 */

#ifndef TITLE_BAR_H
#define TITLE_BAR_H

#include <QWidget>
#include <QLabel>
#include <QMenu>
#include <QPushButton>


enum ButtonType
{
    MIN_BUTTON = 0,
    MIN_MAX_BUTTON ,
    ONLY_CLOSE_BUTTON
};

class TitleBar : public QWidget
{
    Q_OBJECT

public:
    TitleBar(QWidget *parent);
    ~TitleBar();

    void createFileMenu(QMenu* menu);
    void createSettingsMenu(QMenu* menu);
    void createToolsMenu(QMenu* menu);
    void createHelpMenu(QMenu* menu);

    void setBackgroundColor(int r, int g, int b);
    void setContent(QString content);
    void setWidth(int width);
    void setButtonType(ButtonType buttonType);
    void setBorderWidth(int borderWidth);

    void saveRestoreInfo(const QPoint point, const QSize size);
    void getRestoreInfo(QPoint& point, QSize& size);
    void setFullScreen();

private:
    void paintEvent(QPaintEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);

    void initControl();
    void initConnections();
    void setQSS();

Q_SIGNALS:
    void sigMinButtonClicked();
    void sigRestoreButtonClicked();
    void sigMaxButtonClicked();
    void sigCloseButtonClicked();

private Q_SLOTS:
    void onMinButtonClicked();
    void onRestoreButtonClicked();
    void onMaxButtonClicked();
    void onCloseButtonClicked();

private:
    QWidget* fatherWidget;
    QWidget* bottomWidget;
    QLabel* iconLabel;
    QLabel* contentLabel;
    QLabel* bottomLabel;
    QPushButton* fileButton;
    QPushButton* settingsButton;
    QPushButton* toolsButton;
    QPushButton* helpButton;
    QPushButton* minButton;
    QPushButton* restoreButton;
    QPushButton* maxButton;
    QPushButton* closeButton;

    int red;
    int green;
    int bule;

    QPoint restorePos;
    QSize restoreSize;
    bool fPressed;
    QPoint startPos;
    QString content;
    ButtonType buttonType;
    int borderWidth;
};

#endif // TITLE_BAR_H
