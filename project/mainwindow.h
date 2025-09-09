#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QTimer>
#include <QKeyEvent>
#include <QPointF>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void updateMovement();

private:
    Ui::MainWindow *ui;
    QGraphicsScene *scene;

    // 플레이어
    QPointF playerPos;
    qreal playerRadius;
    QGraphicsEllipseItem *playerItem;

    // 이동 상태
    bool movingUp = false;
    bool movingDown = false;
    bool movingLeft = false;
    bool movingRight = false;

    // 조준선
    QPointF mousePos;
    bool aimVisible = true;
    QGraphicsLineItem *aimLineItem;

    // 이동 타이머
    QTimer *moveTimer;
};

#endif // MAINWINDOW_H
