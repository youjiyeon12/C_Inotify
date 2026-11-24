#pragma once
// 캐릭터 무대: 이벤트마다 담당 캐릭터가 통통 튀고 말풍선으로 알려 준다. 그림 파일 없이 QPainter로 그린다.
#include <QColor>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <vector>

#include "Watcher.h"

class CharacterStage : public QWidget {
    Q_OBJECT
public:
    explicit CharacterStage(QWidget* parent = nullptr);
    void setWatching(bool on);
    void react(EventKind kind, const QString& bubble);  // 담당 캐릭터가 반응 + 숫자 +1
    void say(const QString& bubble);                    // 부엉이가 말함
    void celebrate(const QString& bubble);              // 커밋 축하 (색종이)
    void resetCounts();
    QSize sizeHint() const override { return {900, 260}; }

signals:
    void characterClicked(int kind);  // 0~3: 이벤트 종류, -1: 부엉이(전체)

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    struct Actor {
        QString name, label;
        QColor color;
        int count = 0;
        double bounce = 0;  // 1 → 0 으로 줄어들며 튄다
    };
    struct Confetti {
        QPointF p, v;
        QColor c;
        double life, angle, spin;
    };
    void tick();
    double slotX(int slot) const { return width() * (slot + 0.5) / 5.0; }
    double groundY() const;
    double unit() const;
    void drawBuddy(QPainter& p, int i, QPointF foot, double s);
    void drawOwl(QPainter& p, QPointF foot, double s);
    void drawEye(QPainter& p, QPointF c, double r, int mode);
    void drawLabel(QPainter& p, double x, const QString& sub, const QColor& c);
    void drawBubble(QPainter& p, QPointF anchor, const QColor& c);

    Actor actors_[4];
    bool watching_ = false;
    double owlBounce_ = 0, look_ = 0, lookTarget_ = 0, sinceEvent_ = 99, t_ = 0;
    int speaker_ = -1;  // -1 부엉이, 0~3 캐릭터
    QString bubble_;
    double bubbleLife_ = 0;
    std::vector<Confetti> confetti_;
    QTimer timer_;
};
