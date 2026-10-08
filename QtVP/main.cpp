#include <QApplication>
#include <QFileInfo>

#include "Theme.h"
#include "Windows/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtVP");
    QCoreApplication::setApplicationName("QtVP");
    Theme::apply(Theme::saved());

    MainWindow window;
    window.show();

    // Launching with a .vp path (file association, drag onto the exe) opens it
    const QStringList args = QCoreApplication::arguments();
    if (args.size() >= 2 && QFileInfo::exists(args.at(1))) {
        window.openVp(args.at(1));
    }

    return app.exec();
}
