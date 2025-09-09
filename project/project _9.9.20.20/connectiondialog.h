#pragma once
#include <QDialog>
#include <QHostAddress>

class QLineEdit;
class QPushButton;
class QLabel;

class ConnectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectionDialog(QWidget* parent=nullptr);
    QString localIp() const;
    quint16 localPort() const;
    QString peerIp() const;
    quint16 peerPort() const;

private:
    QString detectLocalIPv4() const;

    QLineEdit* editLocalIp;
    QLineEdit* editLocalPort;
    QLineEdit* editPeerIp;
    QLineEdit* editPeerPort;
    QPushButton* okBtn;
    QPushButton* cancelBtn;
};
