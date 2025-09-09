#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QPen>
#include <QBrush>
#include <QLineF>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 씬 초기화
    scene = new QGraphicsScene(this);
    scene->setSceneRect(0,0,ui->mainView->width(),ui->mainView->height());
    ui->mainView->setScene(scene);
    ui->mainView->setRenderHint(QPainter::Antialiasing);
    ui->mainView->viewport()->installEventFilter(this);
    ui->mainView->setMouseTracking(true);

    // 초기 플레이어 좌표
    playerRadius = 10;  // 플레이어 반지름
    playerPos = QPointF(scene->width()/2, scene->height()/2);

    // 플레이어 원
    playerItem = scene->addEllipse(playerPos.x()-playerRadius, playerPos.y()-playerRadius,
                                   playerRadius*2, playerRadius*2,
                                   QPen(Qt::blue), QBrush(Qt::blue));

    // 조준선 생성
    aimLineItem = scene->addLine(QLineF(), QPen(Qt::green,2,Qt::DashLine));
    aimLineItem->setZValue(-1);
    aimLineItem->setVisible(aimVisible);

    // 이동 업데이트 타이머
    moveTimer = new QTimer(this);
    connect(moveTimer, &QTimer::timeout, this, &MainWindow::updateMovement);
    moveTimer->start(16);
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == ui->mainView->viewport())
    {
        if(event->type() == QEvent::MouseMove)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            mousePos = ui->mainView->mapToScene(mouseEvent->pos());

            if(aimVisible)
            {
                QLineF line(playerPos, mousePos);
                line.setLength(1000);
                aimLineItem->setLine(line);
            }
            return true;
        }
        else if(event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);

            if(mouseEvent->button() == Qt::LeftButton)
            {
                // 조준선 On/Off 토글
                aimVisible = !aimVisible;
                aimLineItem->setVisible(aimVisible);

                if(aimVisible)
                {
                    QLineF line(playerPos, mousePos);
                    line.setLength(1000);
                    aimLineItem->setLine(line);
                }
                return true;
            }
            else if(mouseEvent->button() == Qt::RightButton)
            {
                // 총알 발사
                auto bullet = scene->addEllipse(-5,-5,10,10,QPen(),QBrush(Qt::red));
                bullet->setPos(playerPos);

                QLineF direction(playerPos, mousePos);
                direction.setLength(10); // 속도
                QPointF velocity = direction.p2() - playerPos;

                QTimer *bulletTimer = new QTimer(this);
                connect(bulletTimer, &QTimer::timeout, [=]() mutable {
                    bullet->moveBy(velocity.x(), velocity.y());

                    // 맵 범위 벗어나면 삭제
                    if(!scene->sceneRect().contains(bullet->pos()))
                    {
                        scene->removeItem(bullet);
                        delete bullet;
                        bulletTimer->stop();
                        bulletTimer->deleteLater();
                    }
                });
                bulletTimer->start(16);
            }
        }
    }
    return QMainWindow::eventFilter(obj,event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if(event->isAutoRepeat()) return;
    switch(event->key())
    {
    case Qt::Key_W: movingUp = true; break;
    case Qt::Key_S: movingDown = true; break;
    case Qt::Key_A: movingLeft = true; break;
    case Qt::Key_D: movingRight = true; break;
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if(event->isAutoRepeat()) return;
    switch(event->key())
    {
    case Qt::Key_W: movingUp = false; break;
    case Qt::Key_S: movingDown = false; break;
    case Qt::Key_A: movingLeft = false; break;
    case Qt::Key_D: movingRight = false; break;
    }
}

void MainWindow::updateMovement()
{
    qreal dx = 0, dy = 0;
    qreal speed = 5.0;      // 스피드
    if(movingUp) dy -= speed;
    if(movingDown) dy += speed;
    if(movingLeft) dx -= speed;
    if(movingRight) dx += speed;

    if(dx != 0 || dy != 0)
    {
        playerPos += QPointF(dx, dy);

        // 맵 밖 제한
        QRectF bounds = scene->sceneRect();
        if(playerPos.x() - playerRadius < bounds.left()) playerPos.setX(bounds.left() + playerRadius);
        if(playerPos.x() + playerRadius > bounds.right()) playerPos.setX(bounds.right() - playerRadius);
        if(playerPos.y() - playerRadius < bounds.top()) playerPos.setY(bounds.top() + playerRadius);
        if(playerPos.y() + playerRadius > bounds.bottom()) playerPos.setY(bounds.bottom() - playerRadius);

        // 플레이어 원 업데이트
        playerItem->setRect(playerPos.x()-playerRadius, playerPos.y()-playerRadius,
                            playerRadius*2, playerRadius*2);

        // 조준선 업데이트
        if(aimVisible)
        {
            QLineF line(playerPos, mousePos);
            line.setLength(1000);
            aimLineItem->setLine(line);
        }
    }
}
