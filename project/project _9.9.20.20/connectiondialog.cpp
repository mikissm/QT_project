#include "connectiondialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QNetworkInterface>
#include <QIntValidator>

ConnectionDialog::ConnectionDialog(QWidget* parent): QDialog(parent) {
    setWindowTitle("네트워크 설정 (UDP)");
    auto* lay = new QVBoxLayout(this);

    auto row = [&](const QString& label, QWidget* editor){
        auto* h = new QHBoxLayout;
        h->addWidget(new QLabel(label));
        h->addWidget(editor);
        lay->addLayout(h);
    };

    editLocalIp   = new QLineEdit(detectLocalIPv4(), this);
    editLocalIp->setReadOnly(true);

    editLocalPort = new QLineEdit(this);
    editLocalPort->setValidator(new QIntValidator(1, 65535, this));
    editLocalPort->setPlaceholderText("예: 50000");

    editPeerIp    = new QLineEdit(this);
    editPeerIp->setPlaceholderText("상대 IPv4 (예: 192.168.x.y)");

    editPeerPort  = new QLineEdit(this);
    editPeerPort->setValidator(new QIntValidator(1, 65535, this));
    editPeerPort->setPlaceholderText("예: 50001");

    row("내 IP", editLocalIp);
    row("내 포트", editLocalPort);
    row("상대 IP", editPeerIp);
    row("상대 포트", editPeerPort);

    auto* btns = new QHBoxLayout;
    okBtn = new QPushButton("확인");
    cancelBtn = new QPushButton("취소");
    btns->addStretch(); btns->addWidget(okBtn); btns->addWidget(cancelBtn);
    lay->addLayout(btns);

    connect(okBtn, &QPushButton::clicked, this, [this]{
        if(editLocalPort->text().isEmpty() || editPeerIp->text().isEmpty() || editPeerPort->text().isEmpty()){
            // 간단 검증
            return;
        }
        accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

QString ConnectionDialog::localIp() const { return editLocalIp->text(); }
quint16 ConnectionDialog::localPort() const { return editLocalPort->text().toUShort(); }
QString ConnectionDialog::peerIp() const { return editPeerIp->text(); }
quint16 ConnectionDialog::peerPort() const { return editPeerPort->text().toUShort(); }

QString ConnectionDialog::detectLocalIPv4() const {
    const auto addrs = QNetworkInterface::allAddresses();
    for (const auto& ip : addrs) {
        if (ip.protocol()==QAbstractSocket::IPv4Protocol && ip!=QHostAddress::LocalHost)
            return ip.toString();
    }
    return QStringLiteral("127.0.0.1");
}
