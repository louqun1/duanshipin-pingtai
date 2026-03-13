#include "pages/AccountPage/AccountPage.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace frontend::pages {

AccountPage::AccountPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 36, 40, 36);
    layout->setSpacing(18);

    auto *title = new QLabel("Account center", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(title);

    auto *summary = new QLabel(
        "Sign in to view your account details. After a successful login, this page switches to the profile view automatically.",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    stateStack_ = new QStackedWidget(this);
    stateStack_->addWidget(buildGuestPanel());
    stateStack_->addWidget(buildProfilePanel());
    layout->addWidget(stateStack_, 1);

    showLoggedOutState("Please sign in to view your account.");
}

QWidget *AccountPage::buildGuestPanel()
{
    auto *panel = new QWidget(this);
    panel->setStyleSheet("background: white; border-radius: 18px; border: 1px solid #dbe3ef;");

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(16);

    auto *headline = new QLabel("Not signed in", panel);
    headline->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
    layout->addWidget(headline);

    auto *description = new QLabel(
        "使用同一个账户页面进行登录、注册，然后继续进入您的基本个人资料页面。",
        panel);
    description->setWordWrap(true);
    description->setStyleSheet("font-size: 14px; color: #64748b;");
    layout->addWidget(description);

    guestStatusLabel_ = new QLabel(panel);
    guestStatusLabel_->setWordWrap(true);
    guestStatusLabel_->setStyleSheet("font-size: 13px;");
    layout->addWidget(guestStatusLabel_);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);

    usernameEdit_ = new QLineEdit(panel);
    usernameEdit_->setPlaceholderText("Username");
    form->addRow("Username", usernameEdit_);

    passwordEdit_ = new QLineEdit(panel);
    passwordEdit_->setPlaceholderText("Password");
    passwordEdit_->setEchoMode(QLineEdit::Password);
    form->addRow("Password", passwordEdit_);

    emailEdit_ = new QLineEdit(panel);
    emailEdit_->setPlaceholderText("Email for registration");
    form->addRow("Email", emailEdit_);

    layout->addLayout(form);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(12);

    auto *loginButton = new QPushButton("Log in", panel);
    actions->addWidget(loginButton);

    auto *registerButton = new QPushButton("Register", panel);
    actions->addWidget(registerButton);

    actions->addStretch();
    layout->addLayout(actions);
    layout->addStretch();

    connect(loginButton, &QPushButton::clicked, this, [this]() {
        emit loginRequested(usernameEdit_->text().trimmed(), passwordEdit_->text());
    });

    connect(registerButton, &QPushButton::clicked, this, [this]() {
        emit registerRequested(
            usernameEdit_->text().trimmed(),
            passwordEdit_->text(),
            emailEdit_->text().trimmed());
    });

    return panel;
}

QWidget *AccountPage::buildProfilePanel()
{
    auto *panel = new QWidget(this);
    panel->setStyleSheet("background: white; border-radius: 18px; border: 1px solid #dbe3ef;");

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(16);

    auto *headline = new QLabel("Profile overview", panel);
    headline->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
    layout->addWidget(headline);

    auto *description = new QLabel(
        "这是第二阶段的基本个人页面。之后还会添加更多的个人资料编辑功能和创作者工具。",
        panel);
    description->setWordWrap(true);
    description->setStyleSheet("font-size: 14px; color: #64748b;");
    layout->addWidget(description);

    auto *infoLayout = new QFormLayout();
    infoLayout->setHorizontalSpacing(16);
    infoLayout->setVerticalSpacing(12);

    profileUsernameValue_ = new QLabel(panel);
    infoLayout->addRow("Username", profileUsernameValue_);

    profileEmailValue_ = new QLabel(panel);
    infoLayout->addRow("Email", profileEmailValue_);

    layout->addLayout(infoLayout);

    profileStatusLabel_ = new QLabel(panel);
    profileStatusLabel_->setWordWrap(true);
    profileStatusLabel_->setStyleSheet("font-size: 13px; color: #2563eb;");
    layout->addWidget(profileStatusLabel_);

    auto *logoutButton = new QPushButton("Log out", panel);
    logoutButton->setFixedWidth(120);
    layout->addWidget(logoutButton, 0, Qt::AlignLeft);
    layout->addStretch();

    connect(logoutButton, &QPushButton::clicked, this, &AccountPage::logoutRequested);

    return panel;
}

void AccountPage::showLoggedOutState(const QString &message)
{
    if (stateStack_) {
        stateStack_->setCurrentIndex(0);
    }

    if (usernameEdit_) {
        usernameEdit_->clear();
    }
    if (passwordEdit_) {
        passwordEdit_->clear();
    }
    if (emailEdit_) {
        emailEdit_->clear();
    }

    updateGuestStatus(message.isEmpty() ? "Please sign in to continue." : message, true);
    updateProfileStatus(QString());
}

void AccountPage::showAuthenticatedState(const QString &username, const QString &email, const QString &message)
{
    if (profileUsernameValue_) {
        profileUsernameValue_->setText(username);
    }
    if (profileEmailValue_) {
        profileEmailValue_->setText(email.isEmpty() ? "Not set" : email);
    }

    updateGuestStatus(QString(), false);
    updateProfileStatus(message.isEmpty() ? QString("Signed in as %1").arg(username) : message);

    if (stateStack_) {
        stateStack_->setCurrentIndex(1);
    }
}

void AccountPage::updateGuestStatus(const QString &message, bool isError)
{
    if (!guestStatusLabel_) {
        return;
    }

    guestStatusLabel_->setText(message);
    guestStatusLabel_->setStyleSheet(
        QString("font-size: 13px; color: %1;").arg(isError ? "#b91c1c" : "#2563eb"));
}

void AccountPage::updateProfileStatus(const QString &message)
{
    if (!profileStatusLabel_) {
        return;
    }

    profileStatusLabel_->setText(message);
}

}  // namespace frontend::pages
