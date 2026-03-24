#pragma once

#include <QEvent>
#include <QString>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;
class QResizeEvent;
class QScrollArea;
class QShowEvent;

namespace frontend::components {
class VideoCard;
}

namespace frontend::pages {

class HomePage final : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);

signals:
    void playRequested(
        const QString &videoId,
        const QString &title,
        const QString &creator,
        const QString &duration);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void populateFeed();
    void relayoutCards();

    QScrollArea *feedScrollArea_ = nullptr;
    QWidget *feedContainer_ = nullptr;
    QGridLayout *feedGrid_ = nullptr;
    QLabel *feedStatsLabel_ = nullptr;
    QVector<frontend::components::VideoCard *> cards_;
};

}  // namespace frontend::pages
