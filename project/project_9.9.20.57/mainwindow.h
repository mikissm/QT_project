#pragma once
#include <QMainWindow>
#include <QGraphicsScene>
#include <QUdpSocket>
#include <QHostAddress>
#include <QHash>
#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QGraphicsEllipseItem;
class QGraphicsLineItem;
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
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void updateMovement();
    void dropRandomWeapon();
    void udpDataReceived();
    void updateBullets();                 // 총알 이동/충돌 갱신

private:
    void updateWeaponUi();
    void updateHpUi();
    void spawnPeerBullet(const QPointF& start, const QPointF& target, const QString& weaponName);

    // === RPM 관련 ===
    void initDefaultRPMs();               // 무기별 기본 RPM 설정
    int  currentWeaponRPM() const;        // 현재 무기 RPM 조회
    void setCurrentWeaponRPM(int rpm);    // 현재 무기 RPM 설정(클램프)
    bool rpmGateAllowsFire(const QString& wname); // RPM 쿨다운 체크

    // 네트워킹
    QUdpSocket* udp = nullptr;
    QHostAddress peerAddr;
    quint16 localPort = 0;
    quint16 peerPort = 0;

    // UI / Scene
    Ui::MainWindow *ui;
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

    // 무기
    QStringList weaponNames;
    Weapon* currentWeaponPtr = nullptr;
    std::unique_ptr<Weapon> basicWeapon;
    std::unique_ptr<Weapon> itemWeapon;

    // 무기 드랍(표시용)
    struct WeaponPickup {
        QGraphicsEllipseItem* item;
        enum class Kind { MULTI, 저격총, SHOTGUN } kind;
    };
    std::vector<WeaponPickup*> droppedWeapons;

    // 총알 & HP
    struct Bullet {
        QGraphicsEllipseItem* item;
        QPointF vel;          // 프레임당 이동(px/frame)
        qreal remaining;      // 남은 사거리(px)
        int damage;           // 데미지
        bool fromPeer;        // 상대 총알이면 true (나만 맞음 판정)
    };
    std::vector<Bullet*> bullets;
    QTimer* bulletTimer = nullptr;

    int myHP  = 100;
    int peerHP = 100;

    // === RPM 상태(무기명 기준) ===
    QHash<QString,int>    rpmMap;         // 무기명 → RPM
    QHash<QString,qint64> nextFireAtMs;   // 무기명 → 다음 발사 허용 시각(ms)
};
