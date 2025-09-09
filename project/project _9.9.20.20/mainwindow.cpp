#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "weapon.h"
#include "connectiondialog.h"

#include <QPen>
#include <QBrush>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>

// 중요: QGraphics 아이템 실제 선언 포함 (incomplete type 에러 해결)
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // --- 1) 시작 시 네트워크 설정 다이얼로그 ---
    ConnectionDialog dlg(this);
    if(dlg.exec()!=QDialog::Accepted){
        close(); return;
    }
    localPort = dlg.localPort();
    peerPort  = dlg.peerPort();
    peerAddr  = QHostAddress(dlg.peerIp());

    udp = new QUdpSocket(this);
    if(!udp->bind(QHostAddress::AnyIPv4, localPort)) {
        ui->statusbar->showMessage("UDP 바인드 실패: 포트 사용중일 수 있음");
    }
    connect(udp, &QUdpSocket::readyRead, this, &MainWindow::udpDataReceived);

    // --- 2) Scene/UI ---
    scene = new QGraphicsScene(this);
    scene->setSceneRect(0,0,ui->mainView->width(),ui->mainView->height());
    ui->mainView->setScene(scene);
    ui->mainView->setRenderHint(QPainter::Antialiasing);
    ui->mainView->viewport()->installEventFilter(this);
    ui->mainView->setMouseTracking(true);

    // 내 플레이어
    playerPos = QPointF(scene->width()/2, scene->height()/2);
    playerItem = scene->addEllipse(playerPos.x()-playerRadius, playerPos.y()-playerRadius,
                                   playerRadius*2, playerRadius*2,
                                   QPen(Qt::blue), QBrush(Qt::blue));

    // 상대 플레이어(초기 표시)
    peerPos = QPointF(playerPos.x()+60, playerPos.y());
    peerItem = scene->addEllipse(peerPos.x()-peerRadius, peerPos.y()-peerRadius,
                                 peerRadius*2, peerRadius*2,
                                 QPen(Qt::darkMagenta), QBrush(Qt::darkMagenta));

    // 조준선
    aimLineItem = scene->addLine(QLineF(), QPen(Qt::green,2,Qt::DashLine));
    aimLineItem->setZValue(-1);
    aimLineItem->setVisible(aimVisible);

    // 이동 타이머
    moveTimer = new QTimer(this);
    connect(moveTimer, &QTimer::timeout, this, &MainWindow::updateMovement);
    moveTimer->start(16);

    // 무기 이름
    weaponNames = QStringList({"기본총","따발총","저격총","샷건"});
    ui->textBrowser->append("무기 목록:");
    for (auto& n: weaponNames) ui->textBrowser->append(" - " + n);

    // 기본총
    basicWeapon = std::make_unique<Weapon>(Weapon::BASIC,"기본총",10.0,30,1200,10,1200.0);
    currentWeaponPtr = basicWeapon.get();
    updateWeaponUi();

    // 아이템 드랍 타이머
    QTimer *dropTimer = new QTimer(this);
    connect(dropTimer, &QTimer::timeout, this, &MainWindow::dropRandomWeapon);
    dropTimer->start(5000);

    // 첫 상태 브로드캐스트(선택)
    QJsonObject hello{{"type","hello"},{"msg","joined"},{"port", (int)localPort}};
    udp->writeDatagram(QJsonDocument(hello).toJson(QJsonDocument::Compact), peerAddr, peerPort);
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
                    else {
                        updateWeaponUi();
                        // 발사 이벤트 송신
                        if(udp){
                            QJsonObject o{{"type","fire"},
                                          {"x", playerPos.x()},
                                          {"y", playerPos.y()},
                                          {"tx", mousePos.x()},
                                          {"ty", mousePos.y()},
                                          {"weapon", currentWeaponPtr->name()}};
                            udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
                        }
                    }
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

    case Qt::Key_1:
        currentWeaponPtr = basicWeapon.get();
        updateWeaponUi();
        break;
    case Qt::Key_2:
        if(itemWeapon) { currentWeaponPtr=itemWeapon.get(); updateWeaponUi(); }
        else ui->textBrowser->setText("아이템 총 없음 (F로 습득)");
        break;
    case Qt::Key_R:
        if(currentWeaponPtr)
            currentWeaponPtr->reload(this,[this](const QString&s){ui->textBrowser->setText(s);});
        break;

    case Qt::Key_F: {
        for(auto it=droppedWeapons.begin(); it!=droppedWeapons.end();) {
            auto* p=*it;
            if(playerItem->collidesWithItem(p->item)) {
                // 여기서 자유 함수 없이 직접 매핑
                if(p->kind==WeaponPickup::Kind::MULTI)
                    itemWeapon=std::make_unique<Weapon>(Weapon::MULTI,"따발총",9.0,20,1400,8,900);
                else if(p->kind==WeaponPickup::Kind::저격총)
                    itemWeapon=std::make_unique<Weapon>(Weapon::저격총,"저격총",20.0,5,2500,40,2000);
                else // SHOTGUN
                    itemWeapon=std::make_unique<Weapon>(Weapon::SHOTGUN,"샷건",20.0,8,1800,7,200);

                itemWeapon->onPicked([this](const QString&s){ui->textBrowser->setText(s);});
                currentWeaponPtr=itemWeapon.get();
                updateWeaponUi();

                scene->removeItem(p->item); delete p->item; delete p;
                it=droppedWeapons.erase(it);
            } else ++it;
        }
        break;
    }

    case Qt::Key_Q:
        if(currentWeaponPtr==basicWeapon.get()) break;
        if(itemWeapon) {
            itemWeapon->onDropped([this](const QString&s){ui->textBrowser->setText(s);});
            QColor color=Qt::red;
            if(itemWeapon->kind()==Weapon::저격총) color=Qt::blue;
            else if(itemWeapon->kind()==Weapon::SHOTGUN) color=QColor("#ff9500");

            auto* e=scene->addEllipse(-8,-8,16,16,QPen(),QBrush(color));
            e->setPos(playerPos);

            auto kind = WeaponPickup::Kind::MULTI;
            if(itemWeapon->kind()==Weapon::저격총) kind = WeaponPickup::Kind::저격총;
            else if(itemWeapon->kind()==Weapon::SHOTGUN) kind = WeaponPickup::Kind::SHOTGUN;
            droppedWeapons.push_back(new WeaponPickup{e, kind});

            itemWeapon.reset();
            currentWeaponPtr=basicWeapon.get();
            updateWeaponUi();
        }
        break;
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) return;
    if (event->key() == Qt::Key_W)      movingUp = false;
    else if (event->key() == Qt::Key_S) movingDown = false;
    else if (event->key() == Qt::Key_A) movingLeft = false;   // ← 추가
    else if (event->key() == Qt::Key_D) movingRight = false;
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

        // 위치 송신
        if(udp){
            QJsonObject o{{"type","pos"},{"x",playerPos.x()},{"y",playerPos.y()}};
            udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
        }
    }
}

void MainWindow::dropRandomWeapon() {
    int t=QRandomGenerator::global()->bounded(1,4); // 1~3
    QPointF pos(
        QRandomGenerator::global()->bounded(50, static_cast<int>(scene->width() - 50)),
        QRandomGenerator::global()->bounded(50, static_cast<int>(scene->height() - 50))
        );

    QColor color=Qt::red; WeaponPickup::Kind kind=WeaponPickup::Kind::MULTI;
    if(t==1){kind=WeaponPickup::Kind::MULTI;color=Qt::red;}
    else if(t==2){kind=WeaponPickup::Kind::저격총;color=Qt::blue;}
    else {kind=WeaponPickup::Kind::SHOTGUN;color=QColor("#ff9500");}

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

void MainWindow::udpDataReceived() {
    while(udp && udp->hasPendingDatagrams()){
        QByteArray buf; buf.resize(int(udp->pendingDatagramSize()));
        QHostAddress from; quint16 port;
        udp->readDatagram(buf.data(), buf.size(), &from, &port);

        const auto doc = QJsonDocument::fromJson(buf);
        if(!doc.isObject()) continue;
        const auto o = doc.object();
        const auto type = o.value("type").toString();

        if(type=="pos"){
            peerPos.setX(o.value("x").toDouble(peerPos.x()));
            peerPos.setY(o.value("y").toDouble(peerPos.y()));
            peerItem->setRect(peerPos.x()-peerRadius, peerPos.y()-peerRadius, peerRadius*2, peerRadius*2);
        } else if(type=="fire"){
            ui->statusbar->showMessage(QString("상대 발사 @ (%1,%2)").arg(o.value("x").toDouble()).arg(o.value("y").toDouble()), 500);
        } else if(type=="hello"){
            ui->statusbar->showMessage("상대 접속 확인", 1000);
        }
    }
}
