#include "CharacterStage.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979;
constexpr double kDt = 0.033;
constexpr double kOwl = 1.15;  // 부엉이 크기 (다른 캐릭터 대비)
const QColor kInk("#4A3B32");
const QColor kMuted("#8A7A6E");
const int kSlots[4] = {0, 1, 3, 4};  // 가운데(2)는 부엉이 자리

QPolygonF tri(QPointF a, QPointF b, QPointF c) {
    QPolygonF poly;
    poly << a << b << c;
    return poly;
}

void drawShadow(QPainter& p, QPointF foot, double w, double lift) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(74, 59, 50, 30));
    double k = std::max(0.4, 1 - lift * 0.5);
    p.drawEllipse(QPointF(foot.x(), foot.y() + 2), w * 0.42 * k, w * 0.07 * k);
}

QPainterPath ghostPath(const QRectF& body, double t) {
    QPainterPath path;
    double w = body.width();
    path.moveTo(body.left(), body.bottom());
    path.lineTo(body.left(), body.top() + w / 2);
    path.arcTo(QRectF(body.left(), body.top(), w, w), 180, -180);
    path.lineTo(body.right(), body.bottom());
    const int bumps = 4;
    double seg = w / bumps;
    for (int k = 0; k < bumps; ++k) {
        double x0 = body.right() - k * seg;
        double dip = (k % 2 ? -1 : 1) * w * 0.1 + std::sin(t * 4 + k) * w * 0.03;
        path.quadTo(QPointF(x0 - seg / 2, body.bottom() + dip), QPointF(x0 - seg, body.bottom()));
    }
    path.closeSubpath();
    return path;
}
}  // namespace

CharacterStage::CharacterStage(QWidget* parent) : QWidget(parent) {
    actors_[0] = {"새싹이", "생성", QColor("#5BBF7A")};
    actors_[1] = {"뚝딱이", "수정", QColor("#F5A44A")};
    actors_[2] = {"사르르", "삭제", QColor("#EF6F6C")};
    actors_[3] = {"이사짱", "이동", QColor("#5B9BEB")};
    setMinimumHeight(220);
    setMaximumHeight(240);
    setCursor(Qt::PointingHandCursor);
    setToolTip("캐릭터를 누르면 그 종류 이벤트만 볼 수 있어요 (부엉이는 전체)");
    setAccessibleName("캐릭터 무대");
    connect(&timer_, &QTimer::timeout, this, &CharacterStage::tick);
    timer_.start(33);
}

double CharacterStage::unit() const { return std::min(width() / 5.0 * 0.40, height() * 0.27); }

// 가장 큰 부엉이(귀 끝)부터 숫자 알약 아래까지를 무대 세로 가운데에 둔다
double CharacterStage::groundY() const {
    double top = unit() * kOwl * 1.27;  // 부엉이 귀 끝 ~ 땅
    double block = top + 36;            // + 알약
    // 부엉이 머리 위에 말풍선 들어갈 자리(46px)는 남긴다
    return std::max((height() - block) / 2 + top, top + 46);
}

void CharacterStage::setWatching(bool on) {
    if (on && !watching_) owlBounce_ = 1;  // 깨어나며 폴짝
    watching_ = on;
    update();
}

void CharacterStage::react(EventKind kind, const QString& bubble) {
    int i = int(kind);
    if (i < 0 || i > 3) return say(bubble);
    actors_[i].count++;
    actors_[i].bounce = 1;
    speaker_ = i;
    bubble_ = bubble;
    bubbleLife_ = 3.0;
    lookTarget_ = i < 2 ? -1 : 1;  // 부엉이가 그쪽을 쳐다본다
    sinceEvent_ = 0;
    setAccessibleDescription(bubble);
}

void CharacterStage::say(const QString& bubble) {
    speaker_ = -1;
    bubble_ = bubble;
    bubbleLife_ = 3.5;
    owlBounce_ = std::max(owlBounce_, 0.5);
    setAccessibleDescription(bubble);
}

void CharacterStage::celebrate(const QString& bubble) {
    say(bubble);
    owlBounce_ = 1;
    auto* rng = QRandomGenerator::global();
    const QColor colors[] = {QColor("#5BBF7A"), QColor("#F5A44A"), QColor("#EF6F6C"), QColor("#5B9BEB"),
                             QColor("#9B7BE0"), QColor("#FFD166")};
    QPointF from(slotX(2), groundY() - unit() * kOwl * 1.1);
    for (int k = 0; k < 70; ++k) {
        double a = -kPi * (0.1 + 0.8 * rng->generateDouble());
        double speed = 180 + 260 * rng->generateDouble();
        confetti_.push_back({from, QPointF(std::cos(a) * speed, std::sin(a) * speed), colors[k % 6],
                             1.2 + rng->generateDouble() * 0.8, rng->generateDouble() * 360,
                             (rng->generateDouble() - 0.5) * 720});
    }
}

void CharacterStage::resetCounts() {
    for (auto& a : actors_) a.count = 0;
    update();
}

void CharacterStage::tick() {
    t_ += kDt;
    for (auto& a : actors_) a.bounce = std::max(0.0, a.bounce - kDt / 0.8);
    owlBounce_ = std::max(0.0, owlBounce_ - kDt / 0.8);
    bubbleLife_ = std::max(0.0, bubbleLife_ - kDt);
    sinceEvent_ += kDt;
    // 조용하면 두리번거린다
    double target = sinceEvent_ > 2.5 ? (watching_ ? std::sin(t_ * 0.7) * 0.8 : 0) : lookTarget_;
    look_ += (target - look_) * 0.12;
    for (auto& c : confetti_) {
        c.v.ry() += 520 * kDt;
        c.p += c.v * kDt;
        c.life -= kDt;
        c.angle += c.spin * kDt;
    }
    confetti_.erase(std::remove_if(confetti_.begin(), confetti_.end(), [](auto& c) { return c.life <= 0; }),
                    confetti_.end());
    update();
}

void CharacterStage::mousePressEvent(QMouseEvent* e) {
    int slot = std::clamp(int(e->pos().x() * 5 / std::max(1, width())), 0, 4);
    emit characterClicked(slot == 2 ? -1 : (slot < 2 ? slot : slot - 1));
}

void CharacterStage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath frame;
    frame.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
    p.fillPath(frame, QColor("#FBF1E8"));
    p.setClipPath(frame);

    double u = unit();
    for (int i = 0; i < 4; ++i) drawBuddy(p, i, QPointF(slotX(kSlots[i]), groundY()), u);
    drawOwl(p, QPointF(slotX(2), groundY()), u * kOwl);

    for (int i = 0; i < 4; ++i)
        drawLabel(p, slotX(kSlots[i]), QString("%1 %2").arg(actors_[i].label).arg(actors_[i].count), actors_[i].color);
    drawLabel(p, slotX(2), watching_ ? "지켜보는 중" : "쉬는 중", QColor("#B08A6E"));

    for (auto& c : confetti_) {
        p.save();
        p.translate(c.p);
        p.rotate(c.angle);
        p.setOpacity(std::min(1.0, c.life));
        p.fillRect(QRectF(-4, -2, 8, 4), c.c);
        p.restore();
    }

    if (bubbleLife_ > 0 && !bubble_.isEmpty()) {
        if (speaker_ < 0)
            drawBubble(p, QPointF(slotX(2), groundY() - u * kOwl * 1.32), QColor("#9C6B4E"));
        else
            drawBubble(p, QPointF(slotX(kSlots[speaker_]), groundY() - u * 1.25), actors_[speaker_].color);
    }

    p.setClipping(false);
    p.setPen(QPen(QColor("#EADFD3"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(frame);
}

void CharacterStage::drawEye(QPainter& p, QPointF c, double r, int mode) {
    // 점 눈: 0 보통, 1 감음, 2 웃음(^), 3 놀람
    p.setPen(QPen(kInk, 2, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    if (mode == 1) return p.drawLine(c - QPointF(r * 0.5, 0), c + QPointF(r * 0.5, 0));
    if (mode == 2) return p.drawArc(QRectF(c.x() - r * 0.55, c.y() - r * 0.2, r * 1.1, r * 0.9), 20 * 16, 140 * 16);
    p.setPen(Qt::NoPen);
    p.setBrush(kInk);
    if (mode == 3) return p.drawEllipse(c, r * 0.3, r * 0.3);
    p.drawEllipse(c, r * 0.48, r * 0.6);
    p.setBrush(Qt::white);
    p.drawEllipse(c + QPointF(-r * 0.15, -r * 0.22), r * 0.17, r * 0.17);
}

void CharacterStage::drawBuddy(QPainter& p, int i, QPointF foot, double s) {
    const Actor& a = actors_[i];
    bool excited = a.bounce > 0.05;
    double phase = 1 - a.bounce;              // 반응 진행도 0 → 1
    double swell = excited ? std::sin(phase * kPi) : 0;  // 0 → 1 → 0
    // 사르르는 뛰지 않고 둥실 떠오른다
    double hop = i == 2 ? 0 : std::abs(std::sin(a.bounce * kPi * 2)) * a.bounce * s * 0.8;
    // 땅에 닿는 순간 찌그러졌다 펴짐
    double landing = (excited && i != 2) ? (1 - std::abs(std::sin(a.bounce * kPi * 2))) * a.bounce : 0;
    double sway = i == 3 ? std::sin(a.bounce * kPi * 4) * a.bounce * s * 0.35 : 0;  // 이사짱: 옆으로 휙
    double shake = i == 1 ? std::sin(t_ * 40) * a.bounce * s * 0.05 : 0;            // 뚝딱이: 바쁘게 흔들
    double hover = i == 2 ? std::sin(t_ * 2) * s * 0.04 - swell * s * 0.4 : 0;     // 사르르: 둥실 + 떠오름
    double w = s * (1 + 0.16 * landing), h = s * 0.92 * (1 + 0.03 * std::sin(t_ * 2.2 + i)) * (1 - 0.16 * landing);
    QPointF base(foot.x() + sway + shake, foot.y() - hop + hover);
    drawShadow(p, QPointF(foot.x() + sway, foot.y()), w, (hop - hover) / s);
    p.save();
    if (i == 2) p.setOpacity(1 - 0.5 * swell);  // 사르르: 사라지듯 반투명
    QRectF body(base.x() - w / 2, base.y() - h, w, h);
    const QColor& c = a.color;

    if (i == 3) {  // 가방은 몸 뒤에
        if (excited) {  // 발밑 먼지
            p.setPen(Qt::NoPen);
            for (int k = 0; k < 3; ++k) {
                QColor dust(190, 175, 160, int(140 * a.bounce));
                p.setBrush(dust);
                double r = s * (0.05 + 0.09 * phase) * (1 + k * 0.3);
                p.drawEllipse(QPointF(foot.x() + sway - s * (0.45 + 0.22 * k), foot.y() - r * 0.6), r, r * 0.8);
            }
        }
        if (excited) {
            p.setPen(QPen(c.lighter(130), 2, Qt::SolidLine, Qt::RoundCap));
            for (int k = 0; k < 3; ++k) {
                double y = body.top() + h * (0.3 + k * 0.2);
                p.drawLine(QPointF(body.left() - s * 0.22 - k * 4, y), QPointF(body.left() - s * 0.5, y));
            }
        }
        p.setPen(QPen(QColor("#8A5A3B"), 1.5));
        p.setBrush(QColor("#C98B5A"));
        p.drawRoundedRect(QRectF(body.left() - s * 0.12, body.top() + h * 0.35, s * 0.22, s * 0.38), 5, 5);
    }

    // 찹쌀떡 같은 하얀 몸 + 아래쪽을 두르는 담당 색 띠
    QPainterPath shape;
    if (i == 2) shape = ghostPath(body, t_);
    else shape.addRoundedRect(body, w * 0.45, w * 0.45);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#FFFDF8"));
    p.drawPath(shape);
    p.save();
    p.setClipPath(shape);
    p.setPen(QPen(c.darker(115), s * 0.09, Qt::SolidLine, Qt::FlatCap));
    p.setBrush(Qt::NoBrush);
    double band = body.top() + h * 0.74;  // 몸 둘레를 따라 살짝 휘게
    p.drawArc(QRectF(body.left() - w * 0.2, band - h * 0.25, w * 1.4, h * 0.4), 200 * 16, 140 * 16);
    p.restore();
    p.setPen(QPen(QColor("#4A3B32"), 2.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);

    // 얼굴: 점 눈 · 볼터치 · 작은 입
    bool blink = !excited && std::fmod(t_ + i * 1.1, 3.7) < 0.13;
    bool scared = i == 2 && excited;
    int eye = blink ? 1 : scared ? 3 : excited ? 2 : 0;
    double er = s * 0.11, ey = body.top() + h * 0.42;
    drawEye(p, QPointF(base.x() - w * 0.17, ey), er, eye);
    drawEye(p, QPointF(base.x() + w * 0.17, ey), er, eye);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 140, 150, 130));
    p.drawEllipse(QPointF(base.x() - w * 0.29, ey + er * 1.2), er * 0.75, er * 0.45);
    p.drawEllipse(QPointF(base.x() + w * 0.29, ey + er * 1.2), er * 0.75, er * 0.45);
    double my = ey + er * 0.8;
    p.setPen(QPen(kInk, 1.8, Qt::SolidLine, Qt::RoundCap));
    if (scared) {  // 울먹: 물결 입 + 눈물
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(base.x() - s * 0.07, my, s * 0.07, s * 0.06), 0, 180 * 16);
        p.drawArc(QRectF(base.x(), my, s * 0.07, s * 0.06), 0, 180 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#8EC5F2"));
        p.drawEllipse(QPointF(base.x() + w * 0.2, ey + er * (1.2 + std::fmod(t_ * 2, 1.0))), er * 0.22, er * 0.32);
    } else if (excited) {  // 활짝
        p.setBrush(QColor("#E87474"));
        p.drawChord(QRectF(base.x() - s * 0.06, my - s * 0.02, s * 0.12, s * 0.1), 180 * 16, 180 * 16);
    } else {  // ω
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(base.x() - s * 0.06, my, s * 0.06, s * 0.05), 180 * 16, 180 * 16);
        p.drawArc(QRectF(base.x(), my, s * 0.06, s * 0.05), 180 * 16, 180 * 16);
    }

    if (i == 0) {  // 새싹이: 머리 위 떡잎
        double grow = 1 + 0.7 * swell;  // 떡잎이 쑥 자란다
        QPointF top(base.x(), body.top() + 1), tip = top - QPointF(0, s * 0.16 * grow);
        double wig = std::sin(t_ * 12) * a.bounce * 25 + std::sin(t_ * 1.5) * 5;
        p.setPen(QPen(QColor("#3E8E58"), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(top, tip);
        p.setPen(QPen(QColor("#3E8E58"), 1.5));
        p.setBrush(QColor("#7CD992"));
        for (int side : {-1, 1}) {
            p.save();
            p.translate(tip);
            p.rotate(-side * (35 + wig));
            p.drawEllipse(QRectF(side > 0 ? 0 : -s * 0.22 * grow, -s * 0.06 * grow, s * 0.22 * grow, s * 0.12 * grow));
            p.restore();
        }
        if (excited) {  // 반짝이가 퍼진다
            QPointF c0(base.x(), body.top() + h * 0.3);
            for (int k = 0; k < 6; ++k) {
                double ang = (k * 60 + 15) * kPi / 180, rad = s * (0.45 + 0.6 * phase), len = s * 0.07 * (1 - phase * 0.5);
                QPointF c1 = c0 + QPointF(std::cos(ang) * rad, std::sin(ang) * rad * 0.8);
                QColor sc = k % 2 ? QColor("#F7C948") : QColor("#7CD992");
                sc.setAlphaF(a.bounce);
                p.setPen(QPen(sc, 2, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(c1 - QPointF(len, 0), c1 + QPointF(len, 0));
                p.drawLine(c1 - QPointF(0, len), c1 + QPointF(0, len));
            }
        }
    } else if (i == 1) {  // 뚝딱이: 안전모 + 망치
        p.setPen(QPen(QColor("#D9A21B"), 1.5));
        p.setBrush(QColor("#FFD166"));
        p.drawChord(QRectF(body.left() + w * 0.14, body.top() - s * 0.1, w * 0.72, s * 0.36), 0, 180 * 16);
        p.drawRoundedRect(QRectF(body.left() + w * 0.06, body.top() + s * 0.06, w * 0.88, s * 0.06), 3, 3);
        p.save();
        p.translate(body.right() - w * 0.02, body.top() + h * 0.62);
        p.rotate(30 + std::sin(t_ * 20) * 45 * a.bounce);
        p.setPen(QPen(QColor("#8A5A3B"), 1));
        p.setBrush(QColor("#B07A52"));
        p.drawRoundedRect(QRectF(-s * 0.03, -s * 0.32, s * 0.06, s * 0.34), 2, 2);
        p.setPen(QPen(QColor("#5E6670"), 1));
        p.setBrush(QColor("#8C96A3"));
        p.drawRoundedRect(QRectF(-s * 0.11, -s * 0.40, s * 0.22, s * 0.1), 2, 2);
        if (excited && std::sin(t_ * 20) > 0.55) {  // 내리칠 때 불꽃
            p.setPen(QPen(QColor("#FFB020"), 2, Qt::SolidLine, Qt::RoundCap));
            for (int k = -1; k <= 1; ++k)
                p.drawLine(QPointF(-s * 0.13, -s * 0.35 + k * s * 0.06), QPointF(-s * 0.24, -s * 0.35 + k * s * 0.12));
        }
        p.restore();
    } else if (i == 3) {  // 이사짱: 가방끈
        p.setPen(QPen(QColor("#8A5A3B"), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(body.left() + w * 0.1, body.top() + h * 0.28),
                   QPointF(body.left() + w * 0.22, body.bottom() - h * 0.1));
    }
    p.restore();
}

void CharacterStage::drawOwl(QPainter& p, QPointF foot, double s) {
    const QColor brown("#C9966F");
    double hop = std::abs(std::sin(owlBounce_ * kPi * 2)) * owlBounce_ * s * 0.5;
    double w = s * 1.05, h = s * 1.15 * (1 + 0.025 * std::sin(t_ * 1.6));
    QPointF base(foot.x(), foot.y() - hop);
    drawShadow(p, foot, w, hop / s);
    QRectF body(base.x() - w / 2, base.y() - h, w, h);
    p.save();  // 이벤트 난 쪽으로 고개(몸)를 갸웃
    p.translate(foot);
    p.rotate(watching_ ? look_ * 7 : 0);
    p.translate(-foot);

    p.setPen(QPen(brown.darker(130), 2));
    p.setBrush(brown);
    // 귀깃
    p.drawPolygon(tri({body.left() + w * 0.14, body.top() + h * 0.2}, {body.left() + w * 0.06, body.top() - h * 0.1},
                      {body.left() + w * 0.4, body.top() + h * 0.06}));
    p.drawPolygon(tri({body.right() - w * 0.14, body.top() + h * 0.2}, {body.right() - w * 0.06, body.top() - h * 0.1},
                      {body.right() - w * 0.4, body.top() + h * 0.06}));
    // 날개 (커밋하면 파닥파닥)
    double flap = std::sin(t_ * 18) * owlBounce_ * 25;
    for (int side : {-1, 1}) {
        p.save();
        p.translate(base.x() + side * w * 0.46, body.top() + h * 0.58);
        p.rotate(side * (15 + flap));
        p.drawEllipse(QRectF(-w * 0.12, -h * 0.22, w * 0.24, h * 0.44));
        p.restore();
    }
    p.drawEllipse(body);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#F3DCC4"));
    p.drawEllipse(QRectF(base.x() - w * 0.3, body.top() + h * 0.44, w * 0.6, h * 0.5));

    // 눈: 감시 중이면 뜨고 두리번, 아니면 감고 zzz
    double er = s * 0.2;
    bool blink = std::fmod(t_, 5.1) < 0.12;
    for (int side : {-1, 1}) {
        QPointF c(base.x() + side * w * 0.22, body.top() + h * 0.34);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#F6B756"));
        p.drawEllipse(c, er * 1.18, er * 1.18);
        p.setBrush(Qt::white);
        p.drawEllipse(c, er, er);
        if (watching_ && !blink) {
            QPointF pc = c + QPointF(look_ * er * 0.4, 0);
            p.setBrush(kInk);
            p.drawEllipse(pc, er * 0.5, er * 0.5);
            p.setBrush(Qt::white);
            p.drawEllipse(pc + QPointF(-er * 0.15, -er * 0.18), er * 0.14, er * 0.14);
        } else {
            p.setPen(QPen(kInk, 2.5, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(c.x() - er * 0.6, c.y() - er * 0.4, er * 1.2, er * 0.8), 200 * 16, 140 * 16);
        }
    }
    p.setPen(QPen(QColor("#D9822B"), 1));
    p.setBrush(QColor("#F4A13C"));
    p.drawPolygon(tri({base.x() - s * 0.07, body.top() + h * 0.47}, {base.x() + s * 0.07, body.top() + h * 0.47},
                      {base.x(), body.top() + h * 0.6}));

    if (!watching_) {
        QFont f = font();
        f.setBold(true);
        for (int k = 0; k < 3; ++k) {
            double ph = std::fmod(t_ * 0.5 + k / 3.0, 1.0);
            f.setPixelSize(int(10 + ph * 10));
            p.setFont(f);
            QColor zc = kMuted;
            zc.setAlphaF(1 - ph);
            p.setPen(zc);
            p.drawText(QPointF(body.right() - w * 0.05 + ph * s * 0.4, body.top() + h * 0.1 - ph * s * 0.5), "z");
        }
    }
    p.restore();
}

void CharacterStage::drawLabel(QPainter& p, double x, const QString& sub, const QColor& c) {
    QFont f = font();
    f.setBold(true);
    f.setPixelSize(14);
    p.setFont(f);
    double pw = QFontMetricsF(f).horizontalAdvance(sub) + 36;
    QRectF pill(x - pw / 2, groundY() + 10, pw, 26);
    QColor bg = c;
    bg.setAlpha(55);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(pill, 13, 13);
    p.setPen(c.darker(150));
    p.drawText(pill, Qt::AlignCenter, sub);
}

void CharacterStage::drawBubble(QPainter& p, QPointF anchor, const QColor& c) {
    QFont f = font();
    f.setPixelSize(14);
    f.setBold(true);
    p.setFont(f);
    QFontMetricsF fm(f);
    QString text = fm.elidedText(bubble_, Qt::ElideMiddle, std::min(320.0, width() - 40.0));
    double tw = fm.horizontalAdvance(text) + 26, th = fm.height() + 14;
    QRectF r(anchor.x() - tw / 2, anchor.y() - th - 10, tw, th);
    if (r.left() < 6) r.moveLeft(6);
    if (r.right() > width() - 6) r.moveRight(width() - 6);
    if (r.top() < 4) r.moveTop(4);
    double tx = std::clamp(anchor.x(), r.left() + 16, r.right() - 16);

    p.setOpacity(std::min(1.0, bubbleLife_ / 0.4));
    QPainterPath path;
    path.addRoundedRect(r, 12, 12);
    path.addPolygon(tri({tx - 7, r.bottom() - 1}, {tx + 7, r.bottom() - 1}, {tx, r.bottom() + 9}));
    path = path.simplified();
    p.setPen(QPen(c.lighter(140), 1.5));
    p.setBrush(Qt::white);
    p.drawPath(path);
    p.setPen(c.darker(140));
    p.drawText(r, Qt::AlignCenter, text);
    p.setOpacity(1);
}
