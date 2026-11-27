#include "ui/MainWindow.h"

#include <QApplication>
#include <QFontDatabase>
#ifdef FINILY_DEV_SNAPSHOTS
#include <QTimer>
#endif
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
    QApplication::setApplicationVersion(QStringLiteral(FINILY_VERSION));

    // Inter va embebida (resources/fonts, licencia OFL) para que se vea igual
    // en Linux y Windows; si no cargara, se usa la del sistema.
    QString interFamily = QStringLiteral("Inter");
    const int interId = QFontDatabase::addApplicationFont(QStringLiteral(":/resources/fonts/InterVariable.ttf"));
    if (interId >= 0 && !QFontDatabase::applicationFontFamilies(interId).isEmpty())
        interFamily = QFontDatabase::applicationFontFamilies(interId).constFirst();
    QFont uiFont = QApplication::font();
#ifdef Q_OS_WIN
    uiFont.setFamilies({interFamily, QStringLiteral("Segoe UI"), uiFont.family()});
#else
    uiFont.setFamilies({interFamily, QStringLiteral("SF Pro Text"),
                        QStringLiteral("Noto Sans"), QStringLiteral("sans-serif")});
    uiFont.setHintingPreference(QFont::PreferNoHinting);
#endif
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

#ifdef FINILY_DEV_SNAPSHOTS
    // Solo desarrollo: FINILY_SNAPSHOT=archivo.png guarda una captura de la
    // ventana (sirve también con QT_QPA_PLATFORM=offscreen) y sale.
    const QString snapshotPath = qEnvironmentVariable("FINILY_SNAPSHOT");
    if (!snapshotPath.isEmpty()) {
        window.resize(1280, 800);
        QTimer::singleShot(1200, &window, [&window, snapshotPath] {
            window.grab().save(snapshotPath);
            QApplication::quit();
        });
    }
#endif

    return QApplication::exec();
}
