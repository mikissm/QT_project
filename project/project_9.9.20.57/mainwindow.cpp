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
#include <QDateTime>
#include <QtMath>

// QGraphics 아이템 실제 선언 포함
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>

static inline QPointF unitVec(const QPointF& v){
    const qreal len = std::hypot(v.x(), v.y());
    return (len > 0.0001) ? QPointF(v.x()/len, v.y()/len) : QPointF(1,0);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // --- 시작 시 네트워크 설정 다이얼로그 ---
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

    // --- Scene/UI ---
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
    aimLineItem = scene->addLine(QLineF(), QPen(Qt::gray,2,Qt::DashLine));
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

    // === RPM 기본값 설정 ===
    initDefaultRPMs();

    updateWeaponUi();

    // 아이템 드랍 타이머
    QTimer *dropTimer = new QTimer(this);
    connect(dropTimer, &QTimer::timeout, this, &MainWindow::dropRandomWeapon);
    dropTimer->start(500);

    // 총알 갱신 타이머
    bulletTimer = new QTimer(this);
    connect(bulletTimer, &QTimer::timeout, this, &MainWindow::updateBullets);
    bulletTimer->start(16);

    // 첫 상태 브로드캐스트(선택)
    QJsonObject hello{{"type","hello"},{"msg","joined"},{"port", (int)localPort}};
    udp->writeDatagram(QJsonDocument(hello).toJson(QJsonDocument::Compact), peerAddr, peerPort);

    updateHpUi();
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
                if(!currentWeaponPtr) return true;

                // === RPM 쿨다운 체크: 현재 무기명 기준 ===
                const QString wname = currentWeaponPtr->name();
                if(!rpmGateAllowsFire(wname)) {
                    ui->statusbar->showMessage("발사 대기 (RPM 제한)", 200);
                    return true;
                }

                // 무기 발사 시도
                if(!currentWeaponPtr->fire(scene, playerPos, mousePos, this)) {
                    ui->textBrowser->setText("발사 실패: 탄 없음/장전중 (R)");
                } else {
                    updateWeaponUi();

                    // 로컬 판정: 내 총알 생성해서 상대맞춤 판정
                    {
                        const qreal speed  = currentWeaponPtr->bulletSpeed();
                        const int   damage = currentWeaponPtr->damage();
                        const qreal range  = currentWeaponPtr->rangePx();

                        auto* nb = scene->addEllipse(-3,-3,6,6, QPen(Qt::NoPen), QBrush(Qt::blue));
                        nb->setOpacity(0.7);
                        nb->setPos(playerPos);

                        QLineF ray(playerPos, mousePos);
                        ray.setLength(range);
                        QPointF dir = (ray.p2() - ray.p1());
                        const qreal len = std::hypot(dir.x(), dir.y());
                        dir = (len > 1e-6) ? dir/len : QPointF(1,0);
                        QPointF vel = dir * speed; // 주의: speed 단위가 px/frame인지 확인

                        bullets.push_back(new Bullet{ nb, vel, range, damage, false /*fromPeer*/ });
                    }

                    // 발사 이벤트 송신 (상대가 우리 총알도 그려볼 수 있게)
                    if(udp){
                        QJsonObject o{{"type","fire"},
                                      {"x", playerPos.x()},
                                      {"y", playerPos.y()},
                                      {"tx", mousePos.x()},
                                      {"ty", mousePos.y()},
                                      {"weapon", wname}};
                        udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
                    }
                }
                return true;
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

    case Qt::Key_Plus:
    case Qt::Key_Equal: { // 일부 키보드에서 '+'가 '='로 들어올 수 있음
        setCurrentWeaponRPM(currentWeaponRPM() + 30); // 30RPM씩 증가
        ui->statusbar->showMessage(QString("RPM + : %1").arg(currentWeaponRPM()), 600);
        break;
    }
    case Qt::Key_Minus: {
        setCurrentWeaponRPM(currentWeaponRPM() - 30); // 30RPM씩 감소
        ui->statusbar->showMessage(QString("RPM - : %1").arg(currentWeaponRPM()), 600);
        break;
    }

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
                if(p->kind==WeaponPickup::Kind::MULTI)
                    itemWeapon=std::make_unique<Weapon>(Weapon::MULTI,"따발총",9.0,20,1400,8,900);
                else if(p->kind==WeaponPickup::Kind::저격총)
                    itemWeapon=std::make_unique<Weapon>(Weapon::저격총,"저격총",20.0,5,2500,40,2000);
                else // SHOTGUN
                    itemWeapon=std::make_unique<Weapon>(Weapon::SHOTGUN,"샷건",20.0,8,1800,7,200);

                // 새 무기 RPM 초기값 없으면 기본으로 채움
                if(!rpmMap.contains(itemWeapon->name()))
                    rpmMap[itemWeapon->name()] = (itemWeapon->name()=="저격총")? 60 : (itemWeapon->name()=="샷건")? 100 : 600;

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
    const QString wname = currentWeaponPtr->name();
    ui->textBrowser->setText(
        QString("무기:%1 | 탄:%2/%3 | 데미지:%4 | 속도:%5 | 사거리:%6 | RPM:%7 | HP:%8 / 상대:%9")
            .arg(wname)
            .arg(currentWeaponPtr->ammo()).arg(currentWeaponPtr->magSize())
            .arg(currentWeaponPtr->damage())
            .arg(currentWeaponPtr->bulletSpeed())
            .arg(currentWeaponPtr->rangePx())
            .arg(rpmMap.value(wname, 600))
            .arg(myHP).arg(peerHP)
        );
}

void MainWindow::updateHpUi() {
    ui->statusbar->showMessage(QString("내 HP: %1   |   상대 HP: %2").arg(myHP).arg(peerHP), 2000);
    updateWeaponUi();
}

void MainWindow::spawnPeerBullet(const QPointF& start, const QPointF& target, const QString& weaponName)
{
    // 무기별 파라미터 매핑 (px/frame, damage, range px)
    qreal speed = 10.0;
    int damage = 10;
    qreal range = 1000.0;

    if(weaponName=="저격총"){ speed=20.0; damage=40; range=2000.0; }
    else if(weaponName=="따발총"){ speed=9.0; damage=8; range=900.0; }
    else if(weaponName=="샷건"){ speed=20.0; damage=7; range=200.0; }

    auto* item = scene->addEllipse(-3,-3,6,6, QPen(Qt::NoPen), QBrush(Qt::darkRed));
    item->setPos(start);

    QPointF dir = unitVec(target - start);
    QPointF vel = dir * speed; // px/frame

    auto* b = new Bullet{ item, vel, range, damage, true /*fromPeer*/ };
    bullets.push_back(b);
}

void MainWindow::updateBullets()
{
    if(bullets.empty()) return;

    for(size_t i=0; i<bullets.size(); ){
        Bullet* b = bullets[i];

        b->item->moveBy(b->vel.x(), b->vel.y());
        const qreal step = std::hypot(b->vel.x(), b->vel.y());
        b->remaining -= step;

        bool remove = false;

        if(b->remaining <= 0){
            remove = true;
        } else {
            QGraphicsEllipseItem* target = b->fromPeer ? playerItem : peerItem;

            if(target && target->collidesWithItem(b->item)){
                if (b->fromPeer) {
                    myHP -= b->damage;
                    if (myHP < 0) myHP = 0;
                    updateHpUi();

                    if(udp){
                        QJsonObject o{{"type","hp"},{"hp", myHP}};
                        udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
                    }
                } else {
                    peerHP -= b->damage;
                    if (peerHP < 0) peerHP = 0;
                    updateHpUi();

                    if(udp){
                        QJsonObject o{{"type","hp"},{"hp", peerHP}};
                        udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
                    }
                }
                remove = true;
            }
        }

        if(remove){
            scene->removeItem(b->item);
            delete b->item;
            delete b;
            bullets.erase(bullets.begin()+i);
        }else{
            ++i;
        }
    }
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
            const QPointF start(o.value("x").toDouble(), o.value("y").toDouble());
            const QPointF target(o.value("tx").toDouble(), o.value("ty").toDouble());
            const QString wname = o.value("weapon").toString();
            spawnPeerBullet(start, target, wname);
            ui->statusbar->showMessage(QString("상대 발사 (%1)").arg(wname), 300);
        } else if(type=="hello"){
            ui->statusbar->showMessage("상대 접속 확인", 800);
        } else if(type=="hp"){
            peerHP = o.value("hp").toInt(peerHP);
            updateHpUi();
        }
    }
}

// === RPM 유틸 ===
void MainWindow::initDefaultRPMs(){
    // 기본값 예시: 따발총 600, 기본총 300, 샷건 100, 저격총 60
    rpmMap["따발총"] = 600;
    rpmMap["기본총"] = 300;
    rpmMap["샷건"]  = 100;
    rpmMap["저격총"] = 60;

    // 다음 허용시각 초기화: 바로 쏠 수 있게 현재시간
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = rpmMap.constBegin(); it != rpmMap.constEnd(); ++it)
        nextFireAtMs[it.key()] = now;
}

int MainWindow::currentWeaponRPM() const {
    if(!currentWeaponPtr) return 0;
    return rpmMap.value(currentWeaponPtr->name(), 600);
}

void MainWindow::setCurrentWeaponRPM(int rpm){
    if(!currentWeaponPtr) return;
    // 클램프 (예: 30 ~ 1200 RPM)
    if(rpm < 30) rpm = 30;
    if(rpm > 1200) rpm = 1200;
    rpmMap[currentWeaponPtr->name()] = rpm;
    updateWeaponUi();
}

bool MainWindow::rpmGateAllowsFire(const QString& wname){
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int rpm = rpmMap.value(wname, 600);
    // 발사 간격(ms) = 60000 / RPM
    const qint64 interval = (rpm > 0) ? (60000 / rpm) : 0;
    const qint64 allowAt = nextFireAtMs.value(wname, 0);

    if (now < allowAt) return false;

    nextFireAtMs[wname] = now + interval;
    return true;
}
