#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "weapon.h"

#include <QPen>
#include <QBrush>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QRandomGenerator>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Scene 초기화
    scene = new QGraphicsScene(this);
    scene->setSceneRect(0,0,ui->mainView->width(),ui->mainView->height());
    ui->mainView->setScene(scene);
    ui->mainView->setRenderHint(QPainter::Antialiasing);
    ui->mainView->viewport()->installEventFilter(this);
    ui->mainView->setMouseTracking(true);

    // 무기 드랍 타이머
    QTimer *dropTimer = new QTimer(this);
    connect(dropTimer, &QTimer::timeout, this, &MainWindow::dropRandomWeapon);
    dropTimer->start(5000);

    // 플레이어 초기 위치
    playerPos = QPointF(scene->width()/2, scene->height()/2);
    playerItem = scene->addEllipse(playerPos.x()-playerRadius, playerPos.y()-playerRadius,
                                   playerRadius*2, playerRadius*2,
                                   QPen(Qt::blue), QBrush(Qt::blue));

    // 조준선
    aimLineItem = scene->addLine(QLineF(), QPen(Qt::green,2,Qt::DashLine));
    aimLineItem->setZValue(-1);
    aimLineItem->setVisible(aimVisible);

    // 이동 타이머
    moveTimer = new QTimer(this);
    connect(moveTimer, &QTimer::timeout, this, &MainWindow::updateMovement);
    moveTimer->start(16);

    // 무기 이름 배열
    weaponNames = QStringList({"기본총","따발총","파워총","유도총","샷건"});
    ui->textBrowser->append("무기 목록:");
    for (auto& n: weaponNames) ui->textBrowser->append(" - " + n);

    // 기본총 세팅
    basicWeapon = std::make_unique<Weapon>(Weapon::BASIC,"기본총",10.0,30,1200,10,1200.0);
    currentWeaponPtr = basicWeapon.get();
    updateWeaponUi();
}

MainWindow::~MainWindow(){ delete ui; }

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == ui->mainView->viewport())
    {
        if(event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            mousePos = ui->mainView->mapToScene(me->pos());
            if(aimVisible) {
                QLineF line(playerPos, mousePos);
                line.setLength(1000);
                aimLineItem->setLine(line);
            }
            return true;
        } else if(event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if(me->button()==Qt::LeftButton) {
                aimVisible = !aimVisible;
                aimLineItem->setVisible(aimVisible);
                return true;
            } else if(me->button()==Qt::RightButton) {
                if(currentWeaponPtr) {
                    if(!currentWeaponPtr->fire(scene, playerPos, mousePos, this))
                        ui->textBrowser->setText("발사 실패: 탄 없음/장전중 (R)");
                    else updateWeaponUi();
                }
            }
        }
    }
    return QMainWindow::eventFilter(obj,event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if(event->isAutoRepeat()) return;
    switch(event->key()) {
    case Qt::Key_W: movingUp=true; break;
    case Qt::Key_S: movingDown=true; break;
    case Qt::Key_A: movingLeft=true; break;
    case Qt::Key_D: movingRight=true; break;

    case Qt::Key_1: // 기본총 전환
        currentWeaponPtr = basicWeapon.get();
        updateWeaponUi();
        break;
    case Qt::Key_2: // 아이템 총 전환
        if(itemWeapon) { currentWeaponPtr=itemWeapon.get(); updateWeaponUi(); }
        else ui->textBrowser->setText("아이템 총 없음 (F로 습득)");
        break;
    case Qt::Key_R: // 장전
        if(currentWeaponPtr)
            currentWeaponPtr->reload(this,[this](const QString&s){ui->textBrowser->setText(s);});
        break;
    case Qt::Key_F: // 습득
        for(auto it=droppedWeapons.begin(); it!=droppedWeapons.end();) {
            auto* p=*it;
            if(playerItem->collidesWithItem(p->item)) {
                if(p->kind==Weapon::MULTI)
                    itemWeapon=std::make_unique<Weapon>(Weapon::MULTI,"따발총",9.0,20,1400,8,900);
                else if(p->kind==Weapon::POWER)
                    itemWeapon=std::make_unique<Weapon>(Weapon::POWER,"파워총",14.0,6,2000,25,1400);
                else if(p->kind==Weapon::HOMING)
                    itemWeapon=std::make_unique<Weapon>(Weapon::HOMING,"유도총",10.0,10,1600,12,1200);
                else
                    itemWeapon=std::make_unique<Weapon>(Weapon::SHOTGUN,"샷건",12.0,8,1800,7,700);

                itemWeapon->onPicked([this](const QString&s){ui->textBrowser->setText(s);});
                currentWeaponPtr=itemWeapon.get();
                updateWeaponUi();

                scene->removeItem(p->item); delete p->item; delete p;
                it=droppedWeapons.erase(it);
            } else ++it;
        }
        break;
    case Qt::Key_Q: // 버리기
        if(currentWeaponPtr==basicWeapon.get()) break;
        if(itemWeapon) {
            itemWeapon->onDropped([this](const QString&s){ui->textBrowser->setText(s);});
            QColor color=Qt::red;
            if(itemWeapon->kind()==Weapon::POWER) color=Qt::blue;
            else if(itemWeapon->kind()==Weapon::HOMING) color=Qt::green;
            else if(itemWeapon->kind()==Weapon::SHOTGUN) color=QColor("#ff9500");

            auto* e=scene->addEllipse(-8,-8,16,16,QPen(),QBrush(color));
            e->setPos(playerPos);
            droppedWeapons.push_back(new WeaponPickup{e,itemWeapon->kind()});

            itemWeapon.reset();
            currentWeaponPtr=basicWeapon.get();
            updateWeaponUi();
        }
        break;
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event) {
    if(event->isAutoRepeat()) return;
    if(event->key()==Qt::Key_W) movingUp=false;
    else if(event->key()==Qt::Key_S) movingDown=false;
    else if(event->key()==Qt::Key_A) movingLeft=false;
    else if(event->key()==Qt::Key_D) movingRight=false;
}

void MainWindow::updateMovement() {
    qreal dx=0,dy=0, speed=5;
    if(movingUp) dy-=speed;
    if(movingDown) dy+=speed;
    if(movingLeft) dx-=speed;
    if(movingRight) dx+=speed;

    if(dx||dy) {
        playerPos+=QPointF(dx,dy);
        QRectF bounds=scene->sceneRect();
        if(playerPos.x()-playerRadius<bounds.left()) playerPos.setX(bounds.left()+playerRadius);
        if(playerPos.x()+playerRadius>bounds.right()) playerPos.setX(bounds.right()-playerRadius);
        if(playerPos.y()-playerRadius<bounds.top()) playerPos.setY(bounds.top()+playerRadius);
        if(playerPos.y()+playerRadius>bounds.bottom()) playerPos.setY(bounds.bottom()-playerRadius);

        playerItem->setRect(playerPos.x()-playerRadius,playerPos.y()-playerRadius,playerRadius*2,playerRadius*2);

        if(aimVisible){ QLineF line(playerPos,mousePos); line.setLength(1000); aimLineItem->setLine(line); }
    }
}

void MainWindow::dropRandomWeapon() {
    int t=QRandomGenerator::global()->bounded(1,5);
    QPointF pos(QRandomGenerator::global()->bounded(50,scene->width()-50),
                QRandomGenerator::global()->bounded(50,scene->height()-50));
    QColor color=Qt::red; Weapon::Kind kind=Weapon::MULTI;
    if(t==1){kind=Weapon::MULTI;color=Qt::red;}
    else if(t==2){kind=Weapon::POWER;color=Qt::blue;}
    else if(t==3){kind=Weapon::HOMING;color=Qt::green;}
    else {kind=Weapon::SHOTGUN;color=QColor("#ff9500");}

    auto* ellipse=scene->addEllipse(-8,-8,16,16,QPen(),QBrush(color));
    ellipse->setPos(pos);
    droppedWeapons.push_back(new WeaponPickup{ellipse,kind});
}

void MainWindow::updateWeaponUi() {
    if(!currentWeaponPtr){ ui->textBrowser->setText("무기 없음"); return; }
    ui->textBrowser->setText(
        QString("무기:%1 | 탄:%2/%3 | 데미지:%4 | 속도:%5 | 사거리:%6")
            .arg(currentWeaponPtr->name())
            .arg(currentWeaponPtr->ammo()).arg(currentWeaponPtr->magSize())
            .arg(currentWeaponPtr->damage())
            .arg(currentWeaponPtr->bulletSpeed())
            .arg(currentWeaponPtr->rangePx())
        );
}
