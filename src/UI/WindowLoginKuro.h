#pragma once

#include <QPointer>
#include <QWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <string>
#include "KuroApi.hpp"

class QCloseEvent;
class QDialog;
class QLabel;
class QLineEdit;
class QPushButton;

class WindowLoginKuro : public QWidget
{
    Q_OBJECT
public:
    explicit WindowLoginKuro(QWidget* parent = nullptr, KuroApi::Transport transport = {});
    ~WindowLoginKuro() override;
    Q_DISABLE_COPY(WindowLoginKuro)
    void setReauthentication(const QString& uid, const QString& name);

signals:
    void AddUserInfo(const std::string name, const std::string token, const std::string uid,
                     const std::string mid, const std::string type);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void InitTabSms();
    void startSmsVerification();
    void sendSms(QString phone, QString proof, quint64 generation);
    void verifyAccount(QString phone, QString code);
    void setBusy(bool busy);
    void updateSendButton();

    QWidget* tabSms{};
    QLineEdit* phoneEdit{};
    QLineEdit* codeEdit{};
    QPushButton* btnSendCode{};
    QPushButton* btnSmsConfirm{};
    QLineEdit* nameEdit{};
    QLabel* statusLabel{};
    QPointer<QDialog> captchaDialog_;
    QTimer cooldownTimer_;
    QElapsedTimer cooldown_;
    QString smsPhone_;
    QString expectedUid_;
    std::string deviceCode_;
    KuroApi::Transport transport_;
    quint64 generation_{};
    bool busy_{};
    bool closing_{};
};
