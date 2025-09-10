#pragma once
#include <QMainWindow>
#include <QGraphicsScene>
#include <QUdpSocket>
#include <QHostAddress>
#include <QHash>
#include <QRectF>
#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QWidget;
class QGraphicsEllipseItem;
class QGraphicsLineItem;
class QGraphicsRectItem;
class QTimer;
class Weapon;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent* e) override;

private slots:
    void updateMovement();
    void updateBullets();
    void udpDataReceived();
    void sendChat();
    void timedSpawnTick(); // 5초마다(호스트만) 무기 스폰

private:
    // --- UI/Scene ---
    void updateWeaponUi();
    void updateHpUi();

    // --- 맵 ---
    void buildRandomMap(quint32 seed, qreal W, qreal H);   // 랜덤 맵 생성(동기화된 seed)
    void setupMiniMap();                                   // 미니맵(우상단) 설정
    bool collidesBlockingPlayer(const QGraphicsEllipseItem* who) const; // 벽/물과 충돌?
    bool bulletHitObstacle(QGraphicsEllipseItem* bulletItem) const;     // 벽/덤불과 충돌?
    void updateHideOpacity();                               // 덤불 안에 있으면 투명도 조정
    void updateMiniMapView();                               // 미니맵 fitInView 등
    void updateMiniViewRect();                              // 메인뷰 시야 계산 → 미니맵 오버레이 갱신

    // --- 총알 ---
    void spawnBullet(const QString& kindOrName, const QPointF& start, const QPointF& target, bool fromPeer);

    // --- 채팅 ---
    void appendChatMessage(const QString& sender, const QString& text);

    // --- 드랍 동기화 ---
    struct WeaponPickup {
        QGraphicsEllipseItem* item;
        enum class Kind { MULTI, 저격총, SHOTGUN } kind;
        quint64 id; // 전역 유니크 ID
    };

    void spawnPickupLocal(quint64 id, WeaponPickup::Kind kind, const QPointF& pos); // 로컬에 생성
    void removePickupById(quint64 id);                                              // 로컬에서 제거
    static QString kindKeyFromPickupKind(WeaponPickup::Kind k);                     // "multi"/"sniper"/"shotgun"
    static WeaponPickup::Kind pickupKindFromKey(const QString& key);

    // ID 생성: (localPort << 32) | counter
    quint64 nextLocalDropId();

    // 네트워킹
    QUdpSocket* udp = nullptr;
    QHostAddress peerAddr;
    quint16 localPort = 0;
    quint16 peerPort  = 0;
    bool isHost       = false;

    // UI / Scene
    Ui::MainWindow *ui = nullptr;
    QGraphicsScene* scene = nullptr;

    // Player (나)
    QPointF playerPos;
    QGraphicsEllipseItem* playerItem = nullptr;
    qreal playerRadius = 12;

    // Peer (상대)
    QPointF peerPos;
    QGraphicsEllipseItem* peerItem = nullptr;
    qreal peerRadius = 12;

    // Aim / 이동
    QPointF mousePos;
    QGraphicsLineItem* aimLineItem = nullptr;
    bool aimVisible = false;
    bool movingUp=false, movingDown=false, movingLeft=false, movingRight=false;
    QTimer* moveTimer = nullptr;

    // 랜덤 맵 요소
    bool mapReady = false;
    quint32 mapSeed = 0;
    qreal mapW = 0, mapH = 0;
    std::vector<QGraphicsRectItem*> walls;     // 벽: 사람/총알 둘 다 막음
    std::vector<QGraphicsEllipseItem*> waters; // 물: 사람만 막음(총알 통과)
    std::vector<QGraphicsEllipseItem*> bushes; // 덤불: 사람 통과, 총알 막음(숨기)
    QPointF findSafeSpawn(qreal radius, int tries = 300) const;
    void    teleportPlayerTo(const QPointF& p, bool broadcast = true);

    // 무기
    QStringList weaponNames;
    Weapon* currentWeaponPtr = nullptr;
    std::unique_ptr<Weapon> basicWeapon;
    std::unique_ptr<Weapon> itemWeapon;

    // 드랍된 무기들
    std::vector<WeaponPickup*> droppedWeapons;
    QHash<quint64, WeaponPickup*> pickupById;
    quint32 localDropCounter = 1;
    QTimer* spawnTimer = nullptr; // 5초 타이머(호스트만 동작)

    // 총알 & HP
    struct Bullet {
        QGraphicsEllipseItem* item;
        QPointF vel;          // 프레임당 이동(px/frame)
        qreal remaining;      // 남은 사거리(px)
        int damage;           // 데미지
        bool fromPeer;        // true: 상대 탄(내가 맞음), false: 내 탄(상대가 맞음)
    };
    std::vector<Bullet*> bullets;
    QTimer* bulletTimer = nullptr;

    int myHP  = 100;
    int peerHP = 100;

    // 무적
    qint64 myInvulnUntilMs = 0;
    QTimer* invulnBlinkTimer = nullptr;
    bool invulnBlinkOn = false;
    QPen playerDefaultPen; // 기본 펜 저장(생성 시 채워줌)

    void startMyInvulnerability(int ms = 2000);

    // ---- 미니맵 전용 오버레이(새 헤더/서브클래스 없이) ----
    QWidget* miniOverlay = nullptr; // 미니맵 viewport 위의 투명 오버레이
    QRectF   visSceneRect;          // 메인뷰가 현재 보여주는 '씬 좌표' 사각형
};
