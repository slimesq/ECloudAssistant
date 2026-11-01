#ifndef LOGINWGT_H
#define LOGINWGT_H
#include <QTcpSocket>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>
#include "defin.h"

class LoginWgt : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWgt(QWidget* parent = nullptr);
signals:
    void sig_logined(std::string const ip, uint16_t port);
protected slots:
    void ReadData();
    void HandleMessage(packet_head const* data);

protected:
    void HandleRegister(RegisterResult* data);
    void HandleLogin(LoginResult* data);
    void HandleError(packet_head const* data);
    void HandleLoadLogin(LoginReply* data);

private:
    QLineEdit* acountEdit_;
    QLineEdit* passwordEdit_;
    QPushButton* loginBtn_;

private:
    QString ip_;
    uint16_t port_;
    bool is_login_ = false;
    bool is_connect_ = false;
    QTcpSocket* socket_ = nullptr;
};

#endif  // LOGINWGT_H
