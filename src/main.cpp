#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("CrossTerm");
    QApplication::setOrganizationName("CrossTerm");
    QApplication::setApplicationVersion("0.1.0");
    QGuiApplication::setDesktopFileName(QStringLiteral("crossterm"));

    QIcon appIcon;
    const QPixmap baseIcon(QStringLiteral(":/icons/app_icon.png"));
    if (!baseIcon.isNull()) {
        const QList<int> sizes = {16, 24, 32, 48, 64, 96, 128, 256, 512};
        for (int size : sizes) {
            appIcon.addPixmap(baseIcon.scaled(size,
                                              size,
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
        }
    }

    app.setWindowIcon(appIcon);
    QApplication::setWindowIcon(appIcon);

    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    return app.exec();
}
