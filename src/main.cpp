#include "ui/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>

int main(int argc, char* argv[]) {
    // Qt 6 activa el escalado High-DPI por defecto; fijamos la política de redondeo.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Wacom en Wayland/Hyprland: sin esto, los eventos de tableta que CanvasWidget
    // acepta (al propagar desde la barra) suprimen los clics sintéticos del lápiz.
    QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents, true);

    QApplication app(argc, argv);
    QApplication::setApplicationName("Finily");
    QApplication::setApplicationDisplayName("Finily");
    QApplication::setOrganizationName("Finily");
    QApplication::setOrganizationDomain("finily.app");
    QApplication::setApplicationVersion("0.1.0");

    finily::MainWindow window;
    window.show();

    return QApplication::exec();
}
