#pragma once

#include <QWidget>

class QLineEdit;

namespace frontend::pages {

class LoginPage final : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPage(QWidget *parent = nullptr);

signals:
    void loginRequested(const QString &username, const QString &password);
    void registerRequested(const QString &username, const QString &password, const QString &email);

private:
    QLineEdit *usernameEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QLineEdit *emailEdit_ = nullptr;
};

}  // namespace frontend::pages
