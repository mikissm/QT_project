#pragma once
#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QTimer>
#include <memory>
#include "weapon.h"

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
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    Ui::MainWindow *ui;
    QGraphicsScene* scene;
    QGraphicsEllipseItem* playerItem;
    QGraphicsLineItem* aimLineItem;

    QPointF playerPos;
    qreal playerRadius = 20;
    QPointF mousePos;
    bool aimVisible = false;

    bool movingUp=false, movingDown=false, movingLeft=false, movingRight=false;
    QTimer* moveTimer;

    // 무기 관련
    std::unique_ptr<Weapon> basicWeapon;
    std::unique_ptr<Weapon> itemWeapon;
    Weapon* currentWeaponPtr = nullptr;

    struct WeaponPickup { QGraphicsEllipseItem* item; Weapon::Kind kind; };
    QVector<WeaponPickup*> droppedWeapons;

    QStringList weaponNames;

    void updateMovement();
    void dropRandomWeapon();
    void updateWeaponUi();
};
