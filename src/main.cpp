#include "ui/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QStringList>

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

    // Tipografía de interfaz: Inter si está instalada (aspecto limpio y
    // premium); si no, la sans del sistema.
    QFont uiFont = QApplication::font();
    uiFont.setFamilies({QStringLiteral("Inter"), QStringLiteral("SF Pro Text"),
                        QStringLiteral("Noto Sans"), QStringLiteral("sans-serif")});
    uiFont.setHintingPreference(QFont::PreferNoHinting);
    QApplication::setFont(uiFont);

    finily::MainWindow window;
    window.show();

    // Doble clic en un .finily / .pdf (o 'finily archivo.finily'): abrir directo
    // en el editor; sin argumento, arranca limpio en la biblioteca.
    const QStringList arguments = QApplication::arguments();
    if (arguments.size() > 1) {
        const QString path = arguments.value(1);
        if (!path.isEmpty() && QFileInfo::exists(path))
            window.openInitialFile(path);
    }

    return QApplication::exec();
}
