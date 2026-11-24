#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("부엉이 파일 지킴이");

    // 폴더 고르기 · 알림 창 같은 Qt 기본 창도 한국어로 (qt6-translations-l10n 있으면)
    QTranslator qtKo;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
    QString dir = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
    QLocale::setDefault(QLocale(QLocale::Korean, QLocale::SouthKorea));  // 날짜 · 숫자도 한국어 형식
    if (qtKo.load(QLocale(QLocale::Korean, QLocale::SouthKorea), "qtbase", "_", dir)) app.installTranslator(&qtKo);

    MainWindow w;
    w.show();
    if (argc > 1) w.openFolder(QString::fromLocal8Bit(argv[1]));  // ./inotify_tracker <폴더>
    return app.exec();
}
