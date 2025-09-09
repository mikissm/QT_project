#pragma once
#include <QMainWindow>
#include <QGraphicsScene>
#include <QUdpSocket>
#include <QHostAddress>

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
    void dropRandomWeapon();
    void udpDataReceived();

private:
    void updateWeaponUi();

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
    class Weapon* currentWeaponPtr = nullptr;
    std::unique_ptr<class Weapon> basicWeapon;
    std::unique_ptr<class Weapon> itemWeapon;

    struct WeaponPickup {
        QGraphicsEllipseItem* item;
        enum class Kind { MULTI, 저격총, SHOTGUN } kind;
    };
    std::vector<WeaponPickup*> droppedWeapons;
};
