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
#include <QTime>
#include <QtMath>
#include <cmath>

// QGraphics 실제 선언 포함
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>

// ===== 유틸 =====
static inline QPointF unitVec(const QPointF& v){
    const qreal len = std::hypot(v.x(), v.y());
    return (len > 0.0001) ? QPointF(v.x()/len, v.y()/len) : QPointF(1,0);
}

static QString kindFromName(const QString& wname){
    if (wname == "따발총") return "multi";
    if (wname == "저격총") return "sniper";
    if (wname == "샷건")   return "shotgun";
    if (wname == "기본총") return "basic";
    return "basic";
}

// kind → 탄속/데미지/사거리 (px/frame 가정)
static void paramsForKind(const QString& kind, qreal& speed, int& damage, qreal& range){
    if (kind == "sniper")    { speed=20.0; damage=40; range=2000.0; }
    else if (kind=="multi")  { speed=9.0;  damage=8;  range=900.0;  }
    else if (kind=="shotgun"){ speed=20.0; damage=7;  range=200.0;  }
    else /* basic */         { speed=10.0; damage=10; range=1000.0; }
}

static QPointF rotateVec(const QPointF& v, qreal deg){
    const qreal r = qDegreesToRadians(deg);
    return QPointF(v.x()*std::cos(r) - v.y()*std::sin(r),
                   v.x()*std::sin(r) + v.y()*std::cos(r));
}

// [유틸] a ≤ x < b 의 실수 난수
static inline qreal randRange(qreal a, qreal b) {
    return a + (b - a) * QRandomGenerator::global()->generateDouble();
}

// ===============================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("Battle Robit");// 제목 변경

    // --- 시작 시 네트워크 설정 다이얼로그 ---
    ConnectionDialog dlg(this);
    if(dlg.exec()!=QDialog::Accepted){
        close(); return;
    }
    localPort = dlg.localPort();
    peerPort  = dlg.peerPort();

    // 참가 모드면 상대 IP 지정, 호스트 모드면 비워둘 수 있음(자동 학습)
    if (!dlg.peerIp().trimmed().isEmpty())
        peerAddr = QHostAddress(dlg.peerIp().trimmed());
    else
        peerAddr = QHostAddress();

    // 팝업 선택값 사용 (기존의 localPort < peerPort 규칙 제거)
    isHost = dlg.isHost();

    udp = new QUdpSocket(this);
    if(!udp->bind(QHostAddress::AnyIPv4, localPort)) {
        ui->statusbar->showMessage("UDP 바인드 실패: 포트 사용중일 수 있음");
    }
    connect(udp, &QUdpSocket::readyRead, this, &MainWindow::udpDataReceived);

    // --- Scene/UI ---
    scene = new QGraphicsScene(this);
    // 맵 9배 확대: 가로/세로 ×3
    mapW = ui->mainView->width() * 3.0;
    mapH = ui->mainView->height() * 3.0;
    scene->setSceneRect(0,0,mapW,mapH);
    ui->mainView->setScene(scene);
    ui->mainView->setRenderHint(QPainter::Antialiasing);
    ui->mainView->viewport()->installEventFilter(this);
    ui->mainView->setMouseTracking(true);

    // scene 생성 직후(생성자에서 scene = new QGraphicsScene(this); 다음)
    connect(scene, &QGraphicsScene::sceneRectChanged, this, [this](const QRectF&){
        updateMiniMapView();
    });


    // 미니맵은 동일 scene을 사용 (전체 맵이 보이도록 fit)
    ui->graphicsView_3->setScene(scene);
    ui->graphicsView_3->setRenderHint(QPainter::Antialiasing);

    // 내 플레이어
    playerPos = QPointF(scene->width()/2, scene->height()/2);
    playerItem = scene->addEllipse(playerPos.x()-playerRadius, playerPos.y()-playerRadius,
                                   playerRadius*2, playerRadius*2,
                                   QPen(Qt::blue), QBrush(Qt::blue));

    // 상대 플레이어
    peerPos = QPointF(playerPos.x()+60, playerPos.y());
    peerItem = scene->addEllipse(peerPos.x()-peerRadius, peerPos.y()-peerRadius,
                                 peerRadius*2, peerRadius*2,
                                 QPen(Qt::darkMagenta), QBrush(Qt::darkMagenta));
    peerItem->setVisible(false);   // 처음엔 숨김

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
    updateWeaponUi();

    // 총알 갱신 타이머
    bulletTimer = new QTimer(this);
    connect(bulletTimer, &QTimer::timeout, this, &MainWindow::updateBullets);
    bulletTimer->start(16);

    // === 채팅 UI 설정 ===
    ui->pushButton->setText("보내기");
    ui->textBrowser_2->setReadOnly(true);           // 로그
    ui->textBrowser_3->setReadOnly(false);          // 입력
    ui->textBrowser_3->setAcceptRichText(false);
    ui->textBrowser_3->setPlaceholderText("메시지를 입력하고 Enter 또는 [보내기]를 누르세요");
    ui->textBrowser_3->installEventFilter(this);    // Enter 감지
    connect(ui->pushButton, &QPushButton::clicked, this, &MainWindow::sendChat);

    // 5초 스폰 타이머(호스트만 가동)
    spawnTimer = new QTimer(this);
    connect(spawnTimer, &QTimer::timeout, this, &MainWindow::timedSpawnTick);

    // 맵 시드 동기화
    if (isHost) {
        mapSeed = QRandomGenerator::global()->generate();
        buildRandomMap(mapSeed, mapW, mapH);
        setupMiniMap();
        //  내 스폰을 안전 위치로
        {
            QPointF safe = findSafeSpawn(playerRadius);
            teleportPlayerTo(safe, /*broadcast=*/true);
        }
        spawnTimer->start(5000);

        // 상대 주소를 알고 있을 때만 전송 (호스트에서 상대 미입력해도 OK)
        if (udp && !peerAddr.isNull() && peerPort) {
            QJsonObject o{{"type","map_seed"},{"seed",(double)mapSeed},{"w",mapW},{"h",mapH}};
            udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
        } else {
            ui->statusbar->showMessage("상대 주소 미설정: 첫 패킷 수신 시 자동 학습 후 맵 전송", 2000);
        }
    } else {
        appendChatMessage("시스템","맵 시드 수신 대기 중...");
    }

    // 인사 (상대 주소를 알고 있을 때만)
    if (udp && !peerAddr.isNull() && peerPort) {
        QJsonObject hello{{"type","hello"},{"msg","joined"},{"port", (int)localPort},{"host",isHost}};
        udp->writeDatagram(QJsonDocument(hello).toJson(QJsonDocument::Compact), peerAddr, peerPort);
    }

    updateHpUi();
    ui->mainView->centerOn(playerItem); // 시작시 카메라 센터
}


MainWindow::~MainWindow(){ delete ui; }

// --- 맵 생성(랜덤, 동기화된 seed 사용) ---
void MainWindow::buildRandomMap(quint32 seed, qreal W, qreal H)
{
    // 기존 맵 요소 제거
    for (auto* r: walls)   { scene->removeItem(r); delete r; }
    for (auto* e: waters)  { scene->removeItem(e); delete e; }
    for (auto* e: bushes)  { scene->removeItem(e); delete e; }
    walls.clear(); waters.clear(); bushes.clear();

    scene->setSceneRect(0,0,W,H);

    QRandomGenerator gen(seed);

    // 외곽 벽(두께)
    const qreal t=24.0;
    auto addWall = [&](qreal x,qreal y,qreal w,qreal h){
        auto* r = scene->addRect(x,y,w,h,QPen(Qt::NoPen),QBrush(QColor("Black")));
        r->setZValue(-2);
        walls.push_back(r);
    };
    addWall(0,0,W,t);
    addWall(0,H-t,W,t);
    addWall(0,t,t,H-2*t);
    addWall(W-t,t,t,H-2*t);

    // 랜덤 내부 벽 (8~12개)
    int wallN = int(gen.bounded(30, 40));
    for(int i=0;i<wallN;i++){
        qreal ww = gen.bounded(140, 360);
        qreal hh = gen.bounded(20,  60);
        if (gen.bounded(0,2)) std::swap(ww,hh); // 세로 긴 벽 섞기
        qreal x = gen.bounded(int(t+20), int(W-t-ww-20));
        qreal y = gen.bounded(int(t+20), int(H-t-hh-20));
        addWall(x,y,ww,hh);
    }

    // 물 웅덩이(사람 통과 불가, 총알 통과) 6~10개 (엘립스)
    int waterN = int(gen.bounded(6,11));
    for(int i=0;i<waterN;i++){
        qreal r = gen.bounded(40,90);
        qreal x = gen.bounded(int(t+ r+10), int(W-t- r-10));
        qreal y = gen.bounded(int(t+ r+10), int(H-t- r-10));
        auto* e = scene->addEllipse(x-r,y-r,2*r,2*r, QPen(Qt::NoPen), QBrush(QColor(70,170,255,160)));
        e->setZValue(-1.5);
        waters.push_back(e);
    }

    // 덤불(사람 통과 가능, 총알 막힘, 숨기) 10~16개 (엘립스)
    int bushN = int(gen.bounded(10,17));
    for(int i=0;i<bushN;i++){
        qreal r = gen.bounded(35,80);
        qreal x = gen.bounded(int(t+ r+10), int(W-t- r-10));
        qreal y = gen.bounded(int(t+ r+10), int(H-t- r-10));
        auto* e = scene->addEllipse(x-r,y-r,2*r,2*r, QPen(Qt::NoPen), QBrush(QColor(40,180,70,140)));
        e->setZValue(-1.2);
        bushes.push_back(e);
    }

    mapReady = true;

    if (!mainViewRectItem) {
        mainViewRectItem = scene->addRect(QRectF(), QPen(QColor(255,255,255,220), 2, Qt::DashLine), Qt::NoBrush);
        mainViewRectItem->setZValue(10);
    }

    updateMiniMapView(); // 새 맵 만들고 바로 맞춤

}

void MainWindow::setupMiniMap()
{
    // 전체 맵이 보이도록 한 번 맞춤
    ui->graphicsView_3->setScene(scene);
    ui->graphicsView_3->setRenderHint(QPainter::Antialiasing);
    ui->graphicsView_3->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->graphicsView_3->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->graphicsView_3->setInteractive(false);
}

bool MainWindow::collidesBlockingPlayer(const QGraphicsEllipseItem* who) const
{
    // 벽/물과 충돌하면 true
    for (auto* r: walls)  if (who->collidesWithItem(r)) return true;
    for (auto* e: waters) if (who->collidesWithItem(e)) return true;
    return false;
}

bool MainWindow::bulletHitObstacle(QGraphicsEllipseItem* bulletItem) const
{
    // 벽 or 덤불에 닿으면 총알 제거
    for (auto* r: walls)  if (bulletItem->collidesWithItem(r)) return true;
    for (auto* e: bushes) if (bulletItem->collidesWithItem(e)) return true;
    return false;
}

void MainWindow::updateHideOpacity()
{
    // 내가 덤불과 겹치면 살짝 투명하게 (숨는 효과)
    bool inBush = false;
    for (auto* e: bushes) if (playerItem->collidesWithItem(e)) { inBush=true; break; }
    playerItem->setOpacity(inBush ? 0.5 : 1.0);

    // 상대도 동일 처리(선택)
    bool peerInBush = false;
    for (auto* e: bushes) if (peerItem->collidesWithItem(e)) { peerInBush=true; break; }
    peerItem->setOpacity(peerInBush ? 0.0 : 1.0);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // 채팅 입력창: Enter로 전송 (Shift+Enter는 줄바꿈)
    if (obj == ui->textBrowser_3 && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && !(ke->modifiers() & Qt::ShiftModifier)) {
            sendChat();
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            ui->mainView->setFocus(Qt::OtherFocusReason);
            return true;
        }
        return false;        
    }

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
                if(!currentWeaponPtr || !mapReady) return true;

                // 무기 발사(로컬 이펙트/탄약은 Weapon::fire 내부)
                if(!currentWeaponPtr->fire(scene, playerPos, mousePos, this)) {
                    ui->textBrowser->setText("발사 실패: 탄 없음/장전중 (R)");
                } else {
                    updateWeaponUi();

                    // 내 탄도 스폰(fromPeer=false)
                    const QString wname = currentWeaponPtr->name();
                    spawnBullet(wname, playerPos, mousePos, /*fromPeer=*/false);

                    // 네트워크로 발사 이벤트 송신 (kind 포함)
                    if(udp){
                        const QString kind  = kindFromName(wname);
                        QJsonObject o{{"type","fire"},
                                      {"x", playerPos.x()},
                                      {"y", playerPos.y()},
                                      {"tx", mousePos.x()},
                                      {"ty", mousePos.y()},
                                      {"weapon", wname},
                                      {"kind", kind}};
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

    // T → 채팅 입력창으로 포커스
    if (event->key() == Qt::Key_T) {
        if (QApplication::focusWidget() != ui->textBrowser_3) {
            ui->textBrowser_3->setFocus(Qt::ShortcutFocusReason);
            // 커서를 맨 끝으로
            auto c = ui->textBrowser_3->textCursor();
            c.movePosition(QTextCursor::End);
            ui->textBrowser_3->setTextCursor(c);
            return; // T는 더 이상 처리하지 않음
        }
        // 이미 채팅창이 포커스면 't' 타이핑 되도록 그냥 통과
    }


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
        if (!mapReady) break;
        // 충돌하는 픽업 찾기
        WeaponPickup* hit=nullptr;
        for (auto* pk : droppedWeapons) {
            if (pk && pk->item && playerItem->collidesWithItem(pk->item)) { hit = pk; break; }
        }
        if (hit) {
            // 로컬 장착
            if(hit->kind==WeaponPickup::Kind::MULTI)
                itemWeapon=std::make_unique<Weapon>(Weapon::MULTI,"따발총",9.0,20,1400,8,900);
            else if(hit->kind==WeaponPickup::Kind::저격총)
                itemWeapon=std::make_unique<Weapon>(Weapon::저격총,"저격총",20.0,5,2500,40,2000);
            else
                itemWeapon=std::make_unique<Weapon>(Weapon::SHOTGUN,"샷건",20.0,8,1800,7,200);

            itemWeapon->onPicked([this](const QString&s){ui->textBrowser->setText(s);});
            currentWeaponPtr=itemWeapon.get();
            updateWeaponUi();

            // 로컬에서 제거
            removePickupById(hit->id);

            // 네트워크로 습득 알림(양쪽 제거)
            if(udp){
                QJsonObject o{{"type","pickup_weapon"},{"id",(double)hit->id}};
                udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
            }
        }
        break;
    }

    case Qt::Key_Q: {
        if (!mapReady) break;
        // 아이템 무기를 버리기 → 전역 ID 생성 후 스폰 브로드캐스트
        if(currentWeaponPtr!=basicWeapon.get() && itemWeapon) {
            auto k = itemWeapon->kind();
            WeaponPickup::Kind pk = WeaponPickup::Kind::MULTI;
            if(k==Weapon::저격총) pk = WeaponPickup::Kind::저격총;
            else if(k==Weapon::SHOTGUN) pk = WeaponPickup::Kind::SHOTGUN;

            const quint64 id = nextLocalDropId();
            spawnPickupLocal(id, pk, playerPos); // 로컬 생성

            // 브로드캐스트
            if(udp){
                QJsonObject o{{"type","spawn_weapon"},
                              {"id", (double)id},
                              {"kind", kindKeyFromPickupKind(pk)},
                              {"x", playerPos.x()},
                              {"y", playerPos.y()}};
                udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
            }

            itemWeapon.reset();
            currentWeaponPtr=basicWeapon.get();
            updateWeaponUi();
        }
        break;
    }
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
    if(!mapReady) { // 맵 만들어지기 전엔 이동만 막음
        ui->mainView->centerOn(playerItem);
        updateMiniViewRect();
        return;
    }

    qreal dx=0,dy=0, speed=5;
    if(movingUp) dy-=speed;
    if(movingDown) dy+=speed;
    if(movingLeft) dx-=speed;
    if(movingRight) dx+=speed;

    if(dx||dy) {
        // 후보 위치 적용
        QPointF newPos = playerPos + QPointF(dx,dy);
        playerItem->setRect(newPos.x()-playerRadius,newPos.y()-playerRadius,playerRadius*2,playerRadius*2);

        // 벽/물 충돌 체크 → 충돌이면 이동 취소
        if (collidesBlockingPlayer(playerItem)) {
            // 원위치로 되돌림
            playerItem->setRect(playerPos.x()-playerRadius,playerPos.y()-playerRadius,playerRadius*2,playerRadius*2);
        } else {
            // 정상 이동
            playerPos = newPos;

            // 경계 보정
            QRectF bounds=scene->sceneRect();
            if(playerPos.x()-playerRadius<bounds.left()) playerPos.setX(bounds.left()+playerRadius);
            if(playerPos.x()+playerRadius>bounds.right()) playerPos.setX(bounds.right()-playerRadius);
            if(playerPos.y()-playerRadius<bounds.top()) playerPos.setY(bounds.top()+playerRadius);
            if(playerPos.y()+playerRadius>bounds.bottom()) playerPos.setY(bounds.bottom()-playerRadius);

            // 확정 좌표 재설정
            playerItem->setRect(playerPos.x()-playerRadius,playerPos.y()-playerRadius,playerRadius*2,playerRadius*2);

            // 위치 송신
            if(udp){
                QJsonObject o{{"type","pos"},{"x",playerPos.x()},{"y",playerPos.y()}};
                udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
            }
        }
    }

    // 조준선 갱신
    if(aimVisible){ QLineF line(playerPos,mousePos); line.setLength(1000); aimLineItem->setLine(line); }

    // 숨기 효과(덤불)
    updateHideOpacity();

    // 카메라: 플레이어를 따라다니기
    ui->mainView->centerOn(playerItem);
}

void MainWindow::updateWeaponUi() {
    if(!currentWeaponPtr){ ui->textBrowser->setText("무기 없음"); return; }
    ui->textBrowser->setText(
        QString("무기:%1 | 탄:%2/%3 | 데미지:%4 | 속도:%5 | 사거리:%6 | 내HP:%7 / 상대HP:%8")
            .arg(currentWeaponPtr->name())
            .arg(currentWeaponPtr->ammo()).arg(currentWeaponPtr->magSize())
            .arg(currentWeaponPtr->damage())
            .arg(currentWeaponPtr->bulletSpeed())
            .arg(currentWeaponPtr->rangePx())
            .arg(myHP).arg(peerHP)
        );
}

void MainWindow::updateHpUi() {
    ui->statusbar->showMessage(QString("내 HP: %1   |   상대 HP: %2").arg(myHP).arg(peerHP), 1500);
    updateWeaponUi();
}

// --- 총알 스폰 ---
void MainWindow::spawnBullet(const QString& kindOrName, const QPointF& start, const QPointF& target, bool fromPeer)
{
    QString kind = kindOrName;
    if (kind != "multi" && kind != "sniper" && kind != "shotgun" && kind != "basic")
        kind = kindFromName(kindOrName);

    qreal speed; int damage; qreal range;
    paramsForKind(kind, speed, damage, range);

    QPointF dir0 = unitVec(target - start);

    auto spawnOne = [&](qreal jitterDeg){
        QPointF d   = (jitterDeg != 0.0 ? rotateVec(dir0, jitterDeg) : dir0);
        QPointF vel = d * speed;

        auto* item = scene->addEllipse(-3,-3,6,6, QPen(Qt::NoPen), QBrush(Qt::darkRed));
        item->setPos(start);

        bullets.push_back(new Bullet{ item, vel, range, damage, fromPeer });
    };

    if (kind == "multi") {
        const qreal spreadDeg = 6.0;
        spawnOne(-spreadDeg);
        spawnOne(+spreadDeg);
    } else if (kind == "shotgun") {
        const int   count     = 6;
        const qreal spreadDeg = 12.0;
        for (int i=0; i<count; ++i)
            spawnOne(randRange(-spreadDeg, +spreadDeg));
    } else {
        spawnOne(0.0);
    }
}

void MainWindow::updateBullets()
{
    if(bullets.empty()) return;

    for(size_t i=0; i<bullets.size(); ){
        Bullet* b = bullets[i];

        // 이동
        b->item->moveBy(b->vel.x(), b->vel.y());
        const qreal step = std::hypot(b->vel.x(), b->vel.y());
        b->remaining -= step;

        bool remove = false;

        if(b->remaining <= 0){
            remove = true;
        } else {
            // 장애물(벽/덤불) 충돌 → 제거
            if (bulletHitObstacle(b->item)) {
                remove = true;
            } else {
                // 충돌 대상: fromPeer ? 내가 맞음 : 상대가 맞음
                QGraphicsEllipseItem* target = b->fromPeer ? playerItem : peerItem;

                if(target && target->collidesWithItem(b->item)){
                    if (b->fromPeer) {
                        // 내가 맞음 → 내 HP 확정, 'hp'(내 HP) 전송
                        myHP -= b->damage;
                        if (myHP < 0) myHP = 0;
                        updateHpUi();

                        if(udp){
                            QJsonObject o{{"type","hp"},{"hp", myHP}};
                            udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
                        }
                    } else {
                        // 내가 맞췄다고 판단 → 로컬로 상대 HP 반영, 'hit' 통지
                        peerHP -= b->damage;
                        if (peerHP < 0) peerHP = 0;
                        updateHpUi();

                        if(udp){
                            QJsonObject o{{"type","hit"},{"dmg", b->damage}};
                            udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
                        }
                    }
                    remove = true;
                }
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

// --- 드랍 동기화 보조 ---
QString MainWindow::kindKeyFromPickupKind(WeaponPickup::Kind k){
    switch(k){
    case WeaponPickup::Kind::MULTI:   return "multi";
    case WeaponPickup::Kind::저격총:  return "sniper";
    case WeaponPickup::Kind::SHOTGUN: return "shotgun";
    }
    return "multi";
}

MainWindow::WeaponPickup::Kind MainWindow::pickupKindFromKey(const QString& key){
    if(key=="sniper")  return WeaponPickup::Kind::저격총;
    if(key=="shotgun") return WeaponPickup::Kind::SHOTGUN;
    return WeaponPickup::Kind::MULTI; // default multi
}

void MainWindow::spawnPickupLocal(quint64 id, WeaponPickup::Kind kind, const QPointF& pos)
{
    QColor color=Qt::red;
    if(kind==WeaponPickup::Kind::저격총) color=Qt::blue;
    else if(kind==WeaponPickup::Kind::SHOTGUN) color=QColor("#ff9500");

    auto* e=scene->addEllipse(-8,-8,16,16,QPen(),QBrush(color));
    e->setPos(pos);

    auto* pk = new WeaponPickup{e, kind, id};
    droppedWeapons.push_back(pk);
    pickupById.insert(id, pk);
}

void MainWindow::removePickupById(quint64 id)
{
    auto it = pickupById.find(id);
    if (it == pickupById.end()) return;
    WeaponPickup* pk = it.value();

    if (pk && pk->item) {
        scene->removeItem(pk->item);
        delete pk->item;
    }
    // droppedWeapons 벡터에서도 제거
    for (auto vit = droppedWeapons.begin(); vit != droppedWeapons.end(); ++vit) {
        if (*vit == pk) { droppedWeapons.erase(vit); break; }
    }
    delete pk;
    pickupById.remove(id);
}

quint64 MainWindow::nextLocalDropId()
{
    return (quint64(localPort) << 32) | quint64(localDropCounter++);
}

// --- 5초마다 스폰(호스트만 호출) ---
void MainWindow::timedSpawnTick()
{
    if (!isHost || !mapReady) return;

    // 1~3 중 하나: MULTI / SNIPER / SHOTGUN
    int t = QRandomGenerator::global()->bounded(1,4);
    WeaponPickup::Kind kind = WeaponPickup::Kind::MULTI;
    if (t==2) kind = WeaponPickup::Kind::저격총;
    else if (t==3) kind = WeaponPickup::Kind::SHOTGUN;

    // 맵 안쪽 랜덤 위치 (벽/물 피해서 배치)
    QRectF R = scene->sceneRect().adjusted(40,40,-40,-40);
    QPointF pos;
    for (int tries=0; tries<100; ++tries) {
        QPointF p(
            QRandomGenerator::global()->bounded(int(R.left()),  int(R.right())),
            QRandomGenerator::global()->bounded(int(R.top()),   int(R.bottom()))
            );
        // 간단한 충돌 체크: 드랍 아이콘(지름 16)과 벽/물 충돌 안 나야 함
        auto* ghost = new QGraphicsEllipseItem(-8,-8,16,16);
        ghost->setPos(p);
        bool bad=false;
        for (auto* r: walls)  if (ghost->collidesWithItem(r)) { bad=true; break; }
        for (auto* e: waters) if (ghost->collidesWithItem(e)) { bad=true; break; }
        delete ghost;
        if (!bad) { pos=p; break; }
    }
    if (pos.isNull()) pos = QPointF(R.center());

    const quint64 id = nextLocalDropId();

    // 로컬 생성
    spawnPickupLocal(id, kind, pos);

    // 브로드캐스트(상대 주소를 알고 있을 때만)
    if (udp && !peerAddr.isNull() && peerPort) {
        QJsonObject o{{"type","spawn_weapon"},
                      {"id",(double)id},
                      {"kind", kindKeyFromPickupKind(kind)},
                      {"x", pos.x()},
                      {"y", pos.y()}};
        udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
    } else {
        ui->statusbar->showMessage("상대 주소 미설정: 스폰 브로드캐스트 보류", 1000);
    }
}

// --- 채팅 ---
void MainWindow::sendChat()
{
    if (!udp) return;
    const QString msg = ui->textBrowser_3->toPlainText().trimmed();
    if (msg.isEmpty()) return;

    appendChatMessage("나", msg);

    if (udp && !peerAddr.isNull() && peerPort) {
        QJsonObject o{{"type","chat"},{"msg", msg}};
        udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
    } else {
        ui->statusbar->showMessage("상대 주소 미설정: 채팅 전송 보류", 1000);
    }

    ui->textBrowser_3->clear();

    // 전송 후 다시 게임 조작 가능하게 메인뷰로 포커스
    ui->mainView->setFocus(Qt::OtherFocusReason);
}

void MainWindow::appendChatMessage(const QString& sender, const QString& text)
{
    const QString ts = QTime::currentTime().toString("HH:mm:ss");
    ui->textBrowser_2->append(QString("[%1] %2: %3").arg(ts, sender, text));
}

// --- UDP 수신 ---
void MainWindow::udpDataReceived() {
    while (udp && udp->hasPendingDatagrams()) {
        QByteArray buf; buf.resize(int(udp->pendingDatagramSize()));
        QHostAddress from; quint16 port;
        udp->readDatagram(buf.data(), buf.size(), &from, &port);

        // 상대 주소 자동 학습
        if (peerAddr.isNull() || peerPort == 0) {
            peerAddr = from;
            peerPort = port;
            ui->statusbar->showMessage(QString("상대 주소 학습: %1:%2")
                                           .arg(peerAddr.toString()).arg(peerPort), 1500);
            // 호스트면 맵 즉시 전송
            if (isHost && mapReady && udp) {
                QJsonObject o{{"type","map_seed"},{"seed",(double)mapSeed},{"w",mapW},{"h",mapH}};
                udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
            }
        }

        const auto doc = QJsonDocument::fromJson(buf);
        if (!doc.isObject()) continue;
        const auto o = doc.object();
        const auto type = o.value("type").toString();

        if (type=="map_seed") {
            // 호스트가 보낸 맵 시드/크기 수신 → 맵 생성
            mapSeed = (quint32)o.value("seed").toDouble();
            mapW = o.value("w").toDouble(mapW);
            mapH = o.value("h").toDouble(mapH);
            buildRandomMap(mapSeed, mapW, mapH);
            setupMiniMap();
            appendChatMessage("시스템","맵 동기화 완료");

            peerItem->setVisible(false);    // 새 맵 받았으니 다시 숨김

            // 맵 받은 직후 안전 스폰
            QPointF safe = findSafeSpawn(playerRadius);
            teleportPlayerTo(safe, /*broadcast=*/true);

        } else if (type=="pos") {
            peerPos.setX(o.value("x").toDouble(peerPos.x()));
            peerPos.setY(o.value("y").toDouble(peerPos.y()));
            peerItem->setRect(peerPos.x()-peerRadius, peerPos.y()-peerRadius, peerRadius*2, peerRadius*2);

            peerItem->setVisible(true);    // 좌표 받는 순간 표시

        } else if (type=="fire") {
            // 상대 발사 → 빨간 탄 스폰(fromPeer=true)
            const QPointF start(o.value("x").toDouble(), o.value("y").toDouble());
            const QPointF target(o.value("tx").toDouble(), o.value("ty").toDouble());
            const QString kind = o.value("kind").toString();
            const QString name = o.value("weapon").toString();
            if (mapReady) spawnBullet(!kind.isEmpty()? kind : name, start, target, /*fromPeer=*/true);
            ui->statusbar->showMessage("상대 발사", 300);

        } else if (type=="hit") {
            // 내가 맞았다고 통지 → 내 HP 확정, 'hp' 회신
            const int dmg = o.value("dmg").toInt(0);
            if (dmg > 0) {
                myHP -= dmg;
                if (myHP < 0) myHP = 0;
                updateHpUi();
                if (udp && !peerAddr.isNull() && peerPort) {
                    QJsonObject reply{{"type","hp"},{"hp", myHP}};
                    udp->writeDatagram(QJsonDocument(reply).toJson(QJsonDocument::Compact), peerAddr, peerPort);
                }
            }

        } else if (type=="hp") {
            // 상대가 보낸 '자기 HP' → 우리쪽 peerHP 동기화
            peerHP = o.value("hp").toInt(peerHP);
            updateHpUi();

        } else if (type=="chat") {
            const QString msg = o.value("msg").toString();
            if (!msg.isEmpty()) appendChatMessage("상대", msg);

        } else if (type=="spawn_weapon") {
            // 새 드랍 동기화
            const quint64 id = (quint64) o.value("id").toDouble();
            const QString k  = o.value("kind").toString();
            const QPointF pos(o.value("x").toDouble(), o.value("y").toDouble());
            spawnPickupLocal(id, pickupKindFromKey(k), pos);

        } else if (type=="pickup_weapon") {
            // 드랍 제거 동기화
            const quint64 id = (quint64) o.value("id").toDouble();
            removePickupById(id);

        } else if (type=="hello") {
            ui->statusbar->showMessage(
                QString("상대 접속 확인 (상대 host=%1)").arg(o.value("host").toBool(false)), 800);
            // 호스트인데 아직 맵 못 보냈으면 한 번 더 시도
            if (isHost && mapReady && udp && !peerAddr.isNull() && peerPort) {
                QJsonObject seed{{"type","map_seed"},{"seed",(double)mapSeed},{"w",mapW},{"h",mapH}};
                udp->writeDatagram(QJsonDocument(seed).toJson(QJsonDocument::Compact), peerAddr, peerPort);
            }
        }
    }
}

void MainWindow::updateMiniMapView()
{
    if (!scene) return;
    ui->graphicsView_3->resetTransform();
    ui->graphicsView_3->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
    updateMiniViewRect(); // (옵션) 아래 4)번 같이 쓸 경우
}

void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    // 레이아웃이 끝난 직후 한 번 더 맞추기
    QTimer::singleShot(0, this, [this]{ updateMiniMapView(); });
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    updateMiniMapView(); // 창/패널 크기 바뀔 때마다 다시 맞춤
}

void MainWindow::updateMiniViewRect()
{
    if (!scene || !ui || !ui->mainView) return;

    // 처음 한 번 생성
    if (!mainViewRectItem) {
        mainViewRectItem = scene->addRect(
            QRectF(),
            QPen(QColor(255,255,255,220), 2, Qt::DashLine),
            Qt::NoBrush
            );
        mainViewRectItem->setZValue(10);
    }

    // mainView가 실제로 보여주는 씬 좌표 영역 계산
    const QRect viewPx = ui->mainView->viewport()->rect();
    const QPolygonF poly = ui->mainView->mapToScene(viewPx);
    const QRectF vis = poly.boundingRect();

    mainViewRectItem->setRect(vis.normalized());
}

QPointF MainWindow::findSafeSpawn(qreal radius, int tries) const
{
    if (!scene) return QPointF(0,0);

    // 외곽벽과 충분한 마진(40px) 확보
    const qreal margin = 40.0 + radius;
    QRectF R = scene->sceneRect().adjusted(margin, margin, -margin, -margin);

    // 랜덤 샘플링
    for (int i = 0; i < tries; ++i) {
        QPointF p(
            QRandomGenerator::global()->bounded(int(R.left()),  int(R.right())),
            QRandomGenerator::global()->bounded(int(R.top()),   int(R.bottom()))
            );

        // 반지름만큼의 '유령 원'을 만들어서 벽/물과 충돌하는지 검사
        auto* ghost = new QGraphicsEllipseItem(-radius, -radius, 2*radius, 2*radius);
        ghost->setPos(p);

        bool bad = false;
        for (auto* r: walls)  { if (ghost->collidesWithItem(r)) { bad = true; break; } }
        if (!bad) {
            for (auto* e: waters) { if (ghost->collidesWithItem(e)) { bad = true; break; } }
        }
        // (덤불은 통과 가능하므로 제외. 덤불도 피하고 싶으면 bushes도 검사)

        delete ghost;
        if (!bad) return p;
    }

    // 실패하면 중심 근처 그리드 탐색(좁은 맵/벽 과다시 대비)
    const int    grid = 20;
    const qreal  stepX = R.width()  / (grid + 1);
    const qreal  stepY = R.height() / (grid + 1);
    for (int gy = 1; gy <= grid; ++gy) {
        for (int gx = 1; gx <= grid; ++gx) {
            QPointF p(R.left() + gx * stepX, R.top() + gy * stepY);

            auto* ghost = new QGraphicsEllipseItem(-radius, -radius, 2*radius, 2*radius);
            ghost->setPos(p);

            bool bad = false;
            for (auto* r: walls)  { if (ghost->collidesWithItem(r)) { bad = true; break; } }
            if (!bad) {
                for (auto* e: waters) { if (ghost->collidesWithItem(e)) { bad = true; break; } }
            }

            delete ghost;
            if (!bad) return p;
        }
    }

    // 정말 안 되면 중앙 리턴(어차피 updateMovement에서 충돌 시 이동 취소되니 큰 문제는 없음)
    return R.center();
}

void MainWindow::teleportPlayerTo(const QPointF& p, bool broadcast)
{
    playerPos = p;
    playerItem->setRect(p.x()-playerRadius, p.y()-playerRadius, playerRadius*2, playerRadius*2);
    ui->mainView->centerOn(playerItem);

    if (broadcast && udp && !peerAddr.isNull() && peerPort) {
        QJsonObject o{{"type","pos"},{"x",playerPos.x()},{"y",playerPos.y()}};
        udp->writeDatagram(QJsonDocument(o).toJson(QJsonDocument::Compact), peerAddr, peerPort);
    }
}
