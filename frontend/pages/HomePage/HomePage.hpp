#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;
class QResizeEvent;
class QScrollArea;

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
    void resizeEvent(QResizeEvent *event) override;

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
