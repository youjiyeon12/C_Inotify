#include "MainWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSocketNotifier>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "CharacterStage.h"

namespace {
// 이벤트 색은 캐릭터 · 배지 · diff에서 똑같이 쓴다
const QColor kKindColor[] = {QColor("#5BBF7A"), QColor("#F5A44A"), QColor("#EF6F6C"), QColor("#5B9BEB")};
const char* const kKindName[] = {"생성", "수정", "삭제", "이동"};
const QColor kInk("#3F322B");
const QColor kMuted("#8A7A6E");
const QColor kLine("#EFE6DD");
enum Roles { PathRole = Qt::UserRole, StatusRole, KindRole, PillRole, TimeRole };

// 앱 전체(드롭다운 · 대화상자 · 툴팁 포함) 공통 스타일. 값은 이 목록만 쓴다:
//   글자 #3F322B / 흐린 글자 #8A7A6E / 테두리 #D9CCBF / 옅은 테두리 #E4D9CE / 배경 #FCF7F2
//   마우스 올림 #FFF4EA / 선택 #F1EAFE / 초점 #9B7BE0 / 모서리 10px / 높이 40px / 글자 14px
const char* kStyle = R"(
QWidget { color: #3F322B; font-size: 14px; }
QMainWindow, QWidget#root, QDialog, QMessageBox { background: #FCF7F2; }
QLabel { background: transparent; }
QLabel#muted { color: #8A7A6E; }

QPushButton { background: white; border: 1px solid #D9CCBF; border-radius: 10px; padding: 0 18px;
              min-height: 40px; max-height: 40px; }
QPushButton:hover { background: #FFF4EA; }
QPushButton:focus { border-color: #9B7BE0; }
QPushButton:disabled { color: #BDB3AA; background: #F6F1EA; }
QPushButton#watch { background: #5BBF7A; color: white; border: none; font-weight: 600; }
QPushButton#watch:hover { background: #4CAE6B; }
QPushButton#watch:checked { background: #EF6F6C; }
QPushButton#watch:checked:hover { background: #E25E5B; }
QPushButton#watch:disabled { background: #E3EFE6; color: #8FAE98; }
QPushButton#segLeft, QPushButton#segRight { border-radius: 0; padding: 0 26px; font-weight: 600; color: #8A7A6E; }
QPushButton#segLeft { border-top-left-radius: 10px; border-bottom-left-radius: 10px; }
QPushButton#segRight { border-top-right-radius: 10px; border-bottom-right-radius: 10px; border-left: none; }
QPushButton#segLeft:checked, QPushButton#segRight:checked { background: #F1EAFE; color: #3F322B; }

QFrame#pathField { background: white; border: 1px solid #E4D9CE; border-radius: 10px; min-height: 40px; max-height: 40px; }
QLabel#path { color: #3F322B; }

QLineEdit { background: white; border: 1px solid #D9CCBF; border-radius: 10px; padding: 0 10px; min-height: 38px; }
QLineEdit:focus { border-color: #9B7BE0; }

QComboBox { background: white; border: 1px solid #D9CCBF; border-radius: 10px; padding: 0 36px 0 12px; min-width: 120px;
            min-height: 40px; max-height: 40px; combobox-popup: 0; }
QComboBox:hover { background: #FFF4EA; }
QComboBox:focus { border-color: #9B7BE0; }
QComboBox::drop-down { border: none; width: 32px; }
QComboBox::down-arrow { image: none; }
QComboBoxPrivateContainer { background: white; border: 1px solid #D9CCBF; }
QComboBox QAbstractItemView { background: white; border: none; padding: 4px; outline: 0; }
QComboBox QAbstractItemView::item { min-height: 36px; padding: 0 10px; border-radius: 6px; }
QComboBox QAbstractItemView::item:hover { background: #FFF4EA; }
QComboBox QAbstractItemView::item:selected { background: #F1EAFE; color: #3F322B; font-weight: 700; }

QListWidget, QTextEdit, QListView, QTreeView, QTableView {
    background: white; border: 1px solid #E4D9CE; border-radius: 10px; padding: 2px; outline: 0;
    selection-background-color: #F1EAFE; selection-color: #3F322B; }
QListWidget:focus, QListView:focus, QTreeView:focus, QTableView:focus { border-color: #9B7BE0; }
QTreeView::item, QListView::item { min-height: 30px; }
QTreeView::item:hover, QListView::item:hover { background: #FFF4EA; }
QHeaderView::section { background: #FCF7F2; color: #8A7A6E; border: none; border-bottom: 1px solid #E4D9CE;
                       padding: 6px 10px; font-weight: 600; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 4px 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px 4px; }
QScrollBar::handle { background: #D9CCBF; border-radius: 4px; min-height: 32px; min-width: 32px; }
QScrollBar::handle:hover { background: #BFAE9E; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QMenu { background: white; border: 1px solid #D9CCBF; padding: 4px; }
QMenu::item { padding: 8px 18px; border-radius: 6px; }
QMenu::item:selected { background: #FFF4EA; }
QMenu::separator { height: 1px; background: #E4D9CE; margin: 4px 8px; }
QToolTip { background: #3F322B; color: white; border: none; padding: 6px 10px; }
QSplitter::handle { background: transparent; width: 10px; }
)";

// 아이콘은 글리프 문자 대신 같은 굵기의 선으로 직접 그린다
enum class Glyph { Folder, Play, Stop };
QIcon glyph(Glyph g, const QColor& c) {
    QPixmap pm(36, 36);  // 2배로 그려 18px로 표시
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (g == Glyph::Folder) {
        QPainterPath path;
        path.moveTo(5, 10);
        path.lineTo(14, 10);
        path.lineTo(17, 13);
        path.lineTo(31, 13);
        path.lineTo(31, 28);
        path.lineTo(5, 28);
        path.closeSubpath();
        p.drawPath(path);
    } else if (g == Glyph::Play) {
        QPolygonF tri;
        tri << QPointF(11, 8) << QPointF(28, 18) << QPointF(11, 28);
        p.setBrush(c);
        p.drawPolygon(tri);
    } else {
        p.setBrush(c);
        p.drawRoundedRect(QRectF(10, 10, 16, 16), 3, 3);
    }
    return QIcon(pm);
}

QIcon dotIcon(const QColor& c) {
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(2, 2, 16, 16));
    return QIcon(pm);
}

// 오른쪽에 ▾를 직접 그리는 콤보박스 (스타일시트를 쓰면 기본 화살표가 사라져서)
class ArrowCombo : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent* e) override {
        QComboBox::paintEvent(e);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(isEnabled() ? kMuted : QColor("#C9BFB6"), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPointF c(width() - 20, height() / 2.0 + 1);
        QPolygonF v;
        v << c + QPointF(-5, -3) << c + QPointF(0, 2) << c + QPointF(5, -3);
        p.drawPolyline(v);
    }
};

// 목록 한 줄: [색 알약] 시간 경로 + 아래 구분선 (참고 시안 모양)
class RowDelegate : public QStyledItemDelegate {
public:
    // pillWidth: 알약 폭을 고정해 뒤 글자 줄이 맞게
    RowDelegate(int pillWidth, QObject* parent) : QStyledItemDelegate(parent), pillWidth_(pillWidth) {}
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        if (idx.data(PillRole).toString().isEmpty()) return QStyledItemDelegate::sizeHint(opt, idx);
        return {opt.rect.width(), 54};
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        QString pill = idx.data(PillRole).toString();
        if (pill.isEmpty()) return QStyledItemDelegate::paint(p, opt, idx);  // 빈 목록 안내
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        QRect r = opt.rect;
        if (opt.state & QStyle::State_Selected) p->fillRect(r, QColor("#F1EAFE"));
        else if (opt.state & QStyle::State_MouseOver) p->fillRect(r, QColor("#FFF4EA"));
        if (idx.row() + 1 < idx.model()->rowCount()) {
            p->setPen(kLine);
            p->drawLine(r.bottomLeft(), r.bottomRight());
        }
        QColor c = kKindColor[idx.data(KindRole).toInt()];
        QFont f = opt.font;
        f.setBold(true);
        p->setFont(f);
        QRectF pr(r.left() + 18, r.center().y() - 14, pillWidth_, 28);
        QColor bg = c;
        bg.setAlpha(50);
        p->setPen(Qt::NoPen);
        p->setBrush(bg);
        p->drawRoundedRect(pr, 14, 14);
        p->setPen(c.darker(150));
        p->drawText(pr, Qt::AlignCenter, pill);

        f.setBold(false);
        p->setFont(f);
        p->setPen(kInk);
        int x = int(pr.right()) + 24;
        QString time = idx.data(TimeRole).toString();
        if (!time.isEmpty()) {
            p->drawText(QRect(x, r.top(), 90, r.height()), Qt::AlignVCenter, time);
            x += 110;
        }
        QString text = QFontMetrics(f).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, r.right() - x - 12);
        p->drawText(QRect(x, r.top(), r.right() - x, r.height()), Qt::AlignVCenter, text);
        p->restore();
    }

private:
    int pillWidth_;
};

QString q(const std::string& s) { return QString::fromStdString(s); }
std::string toStd(const QString& s) { return s.toStdString(); }

int statusKind(char s) { return s == 'A' ? 0 : s == 'M' ? 1 : 2; }
QString statusText(char s) { return s == 'A' ? "새로 생김" : s == 'M' ? "고쳐짐" : "사라짐"; }

QString shortName(const std::string& rel) {
    QString r = q(rel);
    if (r.endsWith('/')) r.chop(1);
    return r.section('/', -1);
}

void warn(QWidget* parent, const QString& title, const QString& text) {
    QMessageBox m(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    m.button(QMessageBox::Ok)->setText("확인");
    m.exec();
}

bool isPlaceholder(const QListWidgetItem* it) { return !it || it->flags() == Qt::NoItemFlags; }

void setPlaceholder(QListWidget* list, const QString& text) {
    list->clear();
    auto* it = new QListWidgetItem(text);
    it->setFlags(Qt::NoItemFlags);
    it->setForeground(kMuted);
    it->setTextAlignment(Qt::AlignCenter);
    it->setSizeHint(QSize(0, 60));
    list->addItem(it);
}

QListWidget* makeList(const QString& name, int pillWidth) {
    auto* l = new QListWidget;
    l->setItemDelegate(new RowDelegate(pillWidth, l));
    l->setMouseTracking(true);
    l->setAccessibleName(name);
    return l;
}

void showNote(QTextEdit* view, const QString& text) {
    view->setHtml("<p style='color:#8A7A6E; margin:18px'>" + text.toHtmlEscaped() + "</p>");
}

// 같은 폴더 안에서 이름만 바뀐 이동인지 (inotify에는 둘 다 MOVED_FROM/TO 한 쌍으로 온다)
bool isRename(const FsEvent& e) {
    auto parent = [](const std::string& p) {
        auto slash = p.rfind('/');
        return slash == std::string::npos ? std::string() : p.substr(0, slash);
    };
    return e.kind == EventKind::Moved && parent(e.fromPath) == parent(e.path);
}

QString bubbleFor(const FsEvent& e) {
    QString name = (e.isDir ? "폴더 " : "") + shortName(e.path);
    switch (e.kind) {
        case EventKind::Created: return name + " 새로 생겼어!";
        case EventKind::Modified: return name + " 고쳐졌어!";
        case EventKind::Deleted: return name + " 사라졌어…";
        case EventKind::Moved:
            return shortName(e.fromPath) + " → " + shortName(e.path) + (isRename(e) ? " 이름 바꿨어!" : " 이사 완료!");
        default: return "";
    }
}
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), watcher_([this](const std::string& rel) { return tracker_.isIgnored(rel); }) {
    setWindowTitle("부엉이 파일 지킴이");
    resize(1180, 860);
    qApp->setStyleSheet(kStyle);  // 드롭다운 · 대화상자 · 툴팁까지 같은 스타일

    auto* root = new QWidget;
    root->setObjectName("root");
    auto* lay = new QVBoxLayout(root);
    lay->setContentsMargins(24, 18, 24, 20);
    lay->setSpacing(16);
    lay->addWidget(buildHeader());
    stage_ = new CharacterStage;
    lay->addWidget(stage_);
    lay->addWidget(buildBar());

    // 페이지 1: 실시간 목록
    events_ = makeList("실시간 이벤트 목록", 84);  // "이름 변경"이 들어가는 폭
    // 페이지 2: 바뀐 파일 + 바뀐 줄
    changes_ = makeList("바뀐 파일 목록", 96);
    connect(changes_, &QListWidget::currentRowChanged, this, &MainWindow::showChangeDiff);
    diffView_ = new QTextEdit;
    diffView_->setReadOnly(true);
    diffView_->setLineWrapMode(QTextEdit::NoWrap);
    diffView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    diffView_->setAccessibleName("바뀐 줄 보기");
    auto* split = new QSplitter;
    split->addWidget(changes_);
    split->addWidget(diffView_);
    split->setStretchFactor(1, 2);
    split->setSizes({420, 640});

    pages_ = new QStackedWidget;
    pages_->addWidget(events_);
    pages_->addWidget(split);
    lay->addWidget(pages_, 1);
    setCentralWidget(root);

    statusTimer_ = new QTimer(this);
    statusTimer_->setSingleShot(true);
    statusTimer_->setInterval(300);  // 이벤트가 몰려도 변경사항 계산은 한 번만
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);

    // 캐릭터를 누르면 그 종류만 보기
    connect(stage_, &CharacterStage::characterClicked, this, [this](int kind) {
        filter_->setCurrentIndex(kind + 1);
        showPage(0);
    });

    pages_->setEnabled(false);
    controls_->setEnabled(false);
    watchBtn_->setEnabled(false);
    setPlaceholder(events_, "폴더를 열어 주세요");
    stage_->say("폴더를 열어 줘");
    updateWatchButton();
}

MainWindow::~MainWindow() { delete notifier_; }

QWidget* MainWindow::buildHeader() {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);

    // 흰 경로 상자 (폴더 아이콘 + 경로)
    auto* field = new QFrame;
    field->setObjectName("pathField");
    auto* fh = new QHBoxLayout(field);
    fh->setContentsMargins(12, 0, 12, 0);
    fh->setSpacing(8);
    auto* folder = new QLabel;
    folder->setPixmap(glyph(Glyph::Folder, kMuted).pixmap(18, 18));
    pathLabel_ = new QLabel("폴더를 열어 주세요");
    pathLabel_->setObjectName("path");
    pathLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fh->addWidget(folder);
    fh->addWidget(pathLabel_, 1);
    h->addWidget(field, 1);

    auto* openBtn = new QPushButton(glyph(Glyph::Folder, kInk), "폴더 열기");
    openBtn->setShortcut(QKeySequence::Open);
    openBtn->setToolTip("감시할 폴더 고르기 (Ctrl+O)");
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    h->addWidget(openBtn);

    watchBtn_ = new QPushButton;
    watchBtn_->setObjectName("watch");
    watchBtn_->setCheckable(true);
    connect(watchBtn_, &QPushButton::clicked, this, &MainWindow::toggleWatch);
    h->addWidget(watchBtn_);
    return w;
}

// [실시간 | 변경사항]              오른쪽: 페이지별 버튼
QWidget* MainWindow::buildBar() {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);

    segLive_ = new QPushButton("실시간");
    segLive_->setObjectName("segLeft");
    segChanges_ = new QPushButton("변경사항");
    segChanges_->setObjectName("segRight");
    auto* group = new QButtonGroup(w);
    int page = 0;
    for (auto* b : {segLive_, segChanges_}) {
        b->setCheckable(true);
        b->setMinimumWidth(140);  // 글자 수가 바뀌어도("변경사항 (12)") 잘리지 않게
        group->addButton(b, page++);
    }
    segLive_->setChecked(true);
    connect(segLive_, &QPushButton::clicked, this, [this] { showPage(0); });
    connect(segChanges_, &QPushButton::clicked, this, [this] { showPage(1); });
    auto* seg = new QHBoxLayout;
    seg->setSpacing(0);
    seg->addWidget(segLive_);
    seg->addWidget(segChanges_);
    h->addLayout(seg);
    h->addStretch(1);

    // 실시간 페이지: 종류 거르기 + 화면 지우기
    auto* live = new QWidget;
    auto* lh = new QHBoxLayout(live);
    lh->setContentsMargins(0, 0, 0, 0);
    lh->setSpacing(10);
    filter_ = new ArrowCombo;
    filter_->setView(new QListView);  // 펼친 목록에도 스타일이 먹게
    filter_->addItem(dotIcon(QColor("#BFAE9E")), "전체");
    for (int k = 0; k < 4; ++k) filter_->addItem(dotIcon(kKindColor[k]), kKindName[k]);
    filter_->setIconSize(QSize(10, 10));
    filter_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    filter_->setToolTip("이 종류의 이벤트만 보기");
    filter_->setAccessibleName("이벤트 종류 거르기");
    connect(filter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::applyFilter);
    auto* clear = new QPushButton("화면 지우기");
    clear->setToolTip("목록과 캐릭터 숫자를 0으로");
    connect(clear, &QPushButton::clicked, this, [this] {
        setPlaceholder(events_, "비웠어요");
        stage_->resetCounts();
    });
    lh->addWidget(filter_);
    lh->addWidget(clear);

    // 변경사항 페이지: 기준 시각 + 지금부터 다시
    auto* chg = new QWidget;
    auto* ch = new QHBoxLayout(chg);
    ch->setContentsMargins(0, 0, 0, 0);
    ch->setSpacing(10);
    baseLabel_ = new QLabel;
    baseLabel_->setObjectName("muted");
    auto* resetBtn = new QPushButton("지금부터 다시");
    resetBtn->setToolTip("목록을 비우고, 지금 이후에 바뀌는 것만 보여 줘요");
    connect(resetBtn, &QPushButton::clicked, this, &MainWindow::resetBaseline);
    ch->addWidget(baseLabel_);
    ch->addWidget(resetBtn);

    controls_ = new QStackedWidget;
    controls_->addWidget(live);
    controls_->addWidget(chg);
    controls_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    h->addWidget(controls_);
    return w;
}

void MainWindow::showPage(int page) {
    pages_->setCurrentIndex(page);
    controls_->setCurrentIndex(page);
    (page == 0 ? segLive_ : segChanges_)->setChecked(true);
}

// ---------------- 폴더 열기 · 감시 ----------------

void MainWindow::chooseFolder() {
    // 앱 스타일을 따르게 Qt 자체 대화상자를 쓰고, 폴더 고르기에 필요 없는 "파일 형식" 줄은 숨긴다
    QFileDialog d(this, "감시할 폴더 고르기", tracker_.isOpen() ? q(tracker_.root()) : QDir::homePath());
    d.setFileMode(QFileDialog::Directory);
    d.setOptions(QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    d.setLabelText(QFileDialog::LookIn, "위치");
    d.setLabelText(QFileDialog::FileName, "폴더");
    d.setLabelText(QFileDialog::Accept, "이 폴더 열기");
    d.setLabelText(QFileDialog::Reject, "취소");
    for (const char* name : {"fileTypeCombo", "fileTypeLabel"})
        if (auto* w = d.findChild<QWidget*>(name)) w->hide();
    d.resize(820, 520);
    if (d.exec() == QDialog::Accepted && !d.selectedFiles().isEmpty()) openFolder(d.selectedFiles().first());
}

void MainWindow::openFolder(const QString& path) {
    if (watching()) stopWatch();
    std::string err;
    deletedSinceBase_.clear();
    recreated_.clear();
    if (!tracker_.open(toStd(path), &err)) {
        warn(this, "폴더를 열 수 없어요", q(err));
        return;
    }
    baseTime_ = QDateTime::currentSecsSinceEpoch();
    pathLabel_->setText(q(tracker_.root()));
    pathLabel_->setToolTip(q(tracker_.root()));
    pages_->setEnabled(true);
    controls_->setEnabled(true);
    watchBtn_->setEnabled(true);
    setPlaceholder(events_, "아직 이벤트 없음");
    stage_->resetCounts();
    refreshStatus();
    startWatch();
}

void MainWindow::toggleWatch() {
    if (watching()) stopWatch();
    else startWatch();
}

void MainWindow::startWatch() {
    if (!tracker_.isOpen()) return;
    if (!watcher_.start(tracker_.root())) {
        warn(this, "감시를 시작할 수 없어요", q(watcher_.takeError()));
        updateWatchButton();
        return;
    }
    notifier_ = new QSocketNotifier(watcher_.fd(), QSocketNotifier::Read, this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(notifier_, &QSocketNotifier::activated, this, &MainWindow::onInotifyReady);
#else  // Qt 5.15엔 activated 오버로드가 둘이라 이름으로 연결
    connect(notifier_, SIGNAL(activated(int)), this, SLOT(onInotifyReady()));
#endif
    std::string warn = watcher_.takeError();
    stage_->setWatching(true);
    stage_->say(warn.empty() ? QString("감시 시작!") : q(warn));
    updateWatchButton();
    refreshStatus();  // 쉬는 동안 바뀐 것도 반영
}

void MainWindow::stopWatch() {
    delete notifier_;  // fd를 닫기 전에 떼어야 한다
    notifier_ = nullptr;
    watcher_.stop();
    stage_->setWatching(false);
    stage_->say("쉬는 중… zzz");
    updateWatchButton();
}

void MainWindow::onInotifyReady() {
    auto events = watcher_.readEvents();
    qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto& e : events) {
        if (e.kind == EventKind::Overflow) {
            stage_->say("너무 많아서 몇 개 놓쳤어");
            continue;
        }
        QString text = e.kind == EventKind::Moved ? q(e.fromPath) + "  →  " + q(e.path) : q(e.path);
        addEventRow(now, int(e.kind), e.isDir, text, isRename(e) ? QString("이름 변경") : QString(kKindName[int(e.kind)]));
        // 기준 이후 지웠다가 같은 이름으로 다시 만든 파일을 기억 (변경사항에 "다시 만듦"으로 표시)
        QString path = q(e.path);
        if (e.kind == EventKind::Deleted || (e.kind == EventKind::Moved && !e.fromPath.empty()))
            deletedSinceBase_.insert(e.kind == EventKind::Moved ? q(e.fromPath) : path);
        if ((e.kind == EventKind::Created || e.kind == EventKind::Moved) && deletedSinceBase_.contains(path))
            recreated_.insert(path);
        stage_->react(e.kind, bubbleFor(e));
        if (e.path == ".trackignore") tracker_.loadIgnore();
    }
    std::string warn = watcher_.takeError();
    if (!warn.empty()) stage_->say(q(warn));
    if (!events.empty()) statusTimer_->start();
    if (watcher_.hasPending()) QTimer::singleShot(2100, this, [this] { if (watching()) onInotifyReady(); });
}

void MainWindow::addEventRow(qint64 when, int kind, bool isDir, const QString& text, const QString& pill) {
    if (events_->count() == 1 && isPlaceholder(events_->item(0))) events_->clear();
    auto* it = new QListWidgetItem((isDir ? "[폴더] " : "") + text);
    it->setData(KindRole, kind);
    it->setData(PillRole, pill);
    it->setData(TimeRole, QDateTime::fromSecsSinceEpoch(when).toString("HH:mm:ss"));
    it->setToolTip(text);
    events_->insertItem(0, it);
    int f = filter_->currentIndex();
    it->setHidden(f != 0 && f - 1 != kind);
    while (events_->count() > 1000) delete events_->takeItem(events_->count() - 1);
}

void MainWindow::applyFilter() {
    int f = filter_->currentIndex();
    for (int i = 0; i < events_->count(); ++i) {
        auto* it = events_->item(i);
        if (!isPlaceholder(it)) it->setHidden(f != 0 && f - 1 != it->data(KindRole).toInt());
    }
}

// ---------------- 변경사항 ----------------

void MainWindow::refreshStatus() {
    if (!tracker_.isOpen()) return;
    QString keep = changes_->currentItem() ? changes_->currentItem()->data(PathRole).toString() : QString();
    auto list = tracker_.status();
    {
        QSignalBlocker block(changes_);
        changes_->clear();
        for (auto& c : list) {
            auto* it = new QListWidgetItem(q(c.path));
            it->setData(PathRole, q(c.path));
            it->setData(StatusRole, int(c.status));
            it->setData(KindRole, statusKind(c.status));
            bool again = c.status == 'M' && recreated_.contains(q(c.path));
            it->setData(PillRole, again ? QString("다시 만듦") : statusText(c.status));
            it->setToolTip(again ? q(c.path) + " — 기준 이후 지웠다가 같은 이름으로 다시 만들었어요" : q(c.path));
            changes_->addItem(it);
            if (q(c.path) == keep) changes_->setCurrentRow(changes_->count() - 1);
        }
        if (list.empty()) setPlaceholder(changes_, "바뀐 파일 없음");
        else if (!changes_->currentItem()) changes_->setCurrentRow(0);
    }
    segChanges_->setText(list.empty() ? "변경사항" : QString("변경사항 (%1)").arg(list.size()));
    baseLabel_->setText(QDateTime::fromSecsSinceEpoch(baseTime_).toString("HH:mm:ss") + " 이후");
    showChangeDiff();
}

void MainWindow::resetBaseline() {
    if (!tracker_.isOpen()) return;
    tracker_.resetBaseline();
    deletedSinceBase_.clear();
    recreated_.clear();
    baseTime_ = QDateTime::currentSecsSinceEpoch();
    stage_->celebrate("여기서부터 다시 볼게!");
    refreshStatus();
}

void MainWindow::showChangeDiff() {
    auto* it = changes_->currentItem();
    if (isPlaceholder(it)) return showNote(diffView_, "파일을 선택하세요");
    std::string rel = toStd(it->data(PathRole).toString());
    renderDiff(rel, char(it->data(StatusRole).toInt()), tracker_.before(rel), tracker_.working(rel));
}

void MainWindow::renderDiff(const std::string& rel, char status, const std::optional<std::string>& before,
                            const std::optional<std::string>& after) {
    QTextEdit* view = diffView_;
    if (!rel.empty() && rel.back() == '/') return showNote(view, status == 'A' ? "새 폴더" : "삭제된 폴더");
    if (status != 'A' && !before) return showNote(view, "1MB가 넘는 파일은 줄 비교를 하지 않아요.");
    if (status != 'D' && !after) {  // 지금 파일이 너무 크거나, 그 사이 지워져서 못 읽음
        QFileInfo fi(QDir(q(tracker_.root())).filePath(q(rel)));
        return showNote(view, fi.exists() && fi.size() > qint64(Tracker::kMaxKeep)
                                  ? QString("1MB가 넘는 파일은 줄 비교를 하지 않아요.")
                                  : QString("파일을 읽을 수 없어요."));
    }
    std::string a = before.value_or(""), b = after.value_or("");
    if (Tracker::isBinary(a) || Tracker::isBinary(b))
        return showNote(view, QString("바이너리 파일 (%1 → %2 바이트)").arg(a.size()).arg(b.size()));
    auto lines = Tracker::diffLines(a, b);
    if (lines.empty()) return showNote(view, "빈 파일");

    int added = 0, removed = 0;
    for (auto& l : lines) added += l.op == '+', removed += l.op == '-';
    view->clear();
    QTextCursor cur(view->document());
    QTextBlockFormat bf;
    QTextCharFormat cf;
    cf.setForeground(kMuted);
    cf.setFontWeight(QFont::Bold);
    cur.setCharFormat(cf);
    cur.insertText(q(rel) + QString("    +%1  −%2").arg(added).arg(removed) +
                   (added + removed == 0 ? "   (내용 같음)" : ""));
    cf.setFontWeight(QFont::Normal);
    // 줄이 아주 많으면 앞부분만 그린다 (한 줄씩 넣는 게 느려서 화면이 멈춘다)
    constexpr size_t kMaxShownLines = 3000;
    if (lines.size() > kMaxShownLines) {
        size_t hidden = lines.size() - kMaxShownLines;
        lines.resize(kMaxShownLines);
        lines.push_back({'.', QString("… 나머지 %1줄은 생략했어요").arg(hidden).toStdString()});
    }
    for (auto& l : lines) {
        if (l.op == '.') {
            bf.setBackground(Qt::white);
            cf.setForeground(kMuted);
            cur.insertBlock(bf, cf);
            cur.insertText(q(l.text));
            continue;
        }
        bool add = l.op == '+', del = l.op == '-';
        bf.setBackground(add ? QColor("#E7F6EC") : del ? QColor("#FDECEC") : QColor(Qt::white));
        cf.setForeground(add ? QColor("#1E7A3C") : del ? QColor("#B03A37") : kInk);
        cur.insertBlock(bf, cf);
        cur.insertText(QString(QChar::fromLatin1(l.op)) + "  " + q(l.text));
    }
    view->moveCursor(QTextCursor::Start);
}

void MainWindow::updateWatchButton() {
    bool on = watching();
    watchBtn_->setChecked(on);
    watchBtn_->setText(on ? "감시 중지" : "감시 시작");
    watchBtn_->setIcon(glyph(on ? Glyph::Stop : Glyph::Play, Qt::white));
    watchBtn_->setToolTip(on ? QString("폴더 %1개 감시 중").arg(watcher_.watchCount()) : QString("감시 시작하기"));
}
