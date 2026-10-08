#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QScreen>
#include <QSplashScreen>
#include <QThread>

#include "Theme.h"
#include "Windows/MainWindow.h"

namespace {

// Long enough to read, so a fast start does not just flash the splash
constexpr int MinimumSplashMs = 800;

// Placeholder art until the app has a real logo
QPixmap splashPixmap()
{
    const QSize size(480, 270);
    const qreal ratio = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;

    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QLinearGradient background(0, 0, size.width(), size.height());
    background.setColorAt(0, QColor(28, 36, 52));
    background.setColorAt(1, QColor(12, 16, 26));
    painter.fillRect(QRect(QPoint(0, 0), size), background);

    // A stack of three "archive" slabs
    const QColor accent(86, 156, 214);
    for (int i = 0; i < 3; ++i) {
        const QRectF slab(48, 78 + i * 26, 92, 18);
        painter.setPen(Qt::NoPen);
        painter.setBrush(i == 1 ? accent : accent.darker(160 - i * 20));
        painter.drawRoundedRect(slab, 4, 4);
    }

    QFont title = painter.font();
    title.setPixelSize(56);
    title.setBold(true);
    painter.setFont(title);
    painter.setPen(Qt::white);
    painter.drawText(QRect(164, 70, 300, 70), Qt::AlignLeft | Qt::AlignVCenter, "QtVP");

    QFont subtitle = painter.font();
    subtitle.setPixelSize(15);
    subtitle.setBold(false);
    painter.setFont(subtitle);
    painter.setPen(QColor(170, 182, 200));
    painter.drawText(QRect(166, 136, 300, 24), Qt::AlignLeft | Qt::AlignVCenter,
        QCoreApplication::translate("main", "VP archive viewer for FreeSpace Open"));

    return pixmap;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtVP");
    QCoreApplication::setApplicationName("QtVP");
    Theme::apply(Theme::saved());

    QElapsedTimer shown;
    shown.start();
    QSplashScreen splash(splashPixmap());
    splash.show();
    const auto status = [&](const QString& text) {
        splash.showMessage(text, Qt::AlignLeft | Qt::AlignBottom, QColor(170, 182, 200));
        app.processEvents();
    };

    status(QCoreApplication::translate("main", "Starting..."));
    MainWindow window;

    // The slow parts that would otherwise stall the window the first time they are used
    status(QCoreApplication::translate("main", "Starting audio..."));
    window.warmUp();

    const qint64 remaining = MinimumSplashMs - shown.elapsed();
    if (remaining > 0)
        QThread::msleep(quint64(remaining));

    window.show();
    splash.finish(&window);

    // Launching with a .vp path (file association, drag onto the exe) opens it
    const QStringList args = QCoreApplication::arguments();
    if (args.size() >= 2 && QFileInfo::exists(args.at(1))) {
        window.openVp(args.at(1));
    }

    return app.exec();
}
