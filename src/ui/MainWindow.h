#pragma once

#include <QMainWindow>

class QSocketNotifier;
class QStackedWidget;

namespace finily {

class CanvasWidget;
class FloatingToolbar;
class HomeScreen;

// Ventana principal nativa de Finily con dos páginas en un QStackedWidget:
// página 0 = HomeScreen (biblioteca de cuadernos), página 1 = contenedor del
// editor (CanvasWidget + FloatingToolbar como HERMANOS: los QTabletEvent
// ignorados por la barra propagan al contenedor y Qt genera los clics de mouse
// sintéticos que hacen funcionales los botones con lápiz). Coordina los
// atajos (acotados al editor), el Pad Wacom (solo en el editor) y el
// autoguardado.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

    // Abre un archivo recibido por CLI (.finily o .pdf) y va directo al editor;
    // si no hay archivo válido, arranca limpio en la pantalla de inicio.
    void openInitialFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void installShortcuts();
    void setupWacomPadListener();
    void onWacomPadReadable();
    void layoutChildren();
    void updateWindowTitle();
    void showEditorPage();
    void showHomePage();

    QStackedWidget* m_stack = nullptr;
    QWidget* m_container = nullptr; // página 1: contenedor plano del editor
    HomeScreen* m_homeScreen = nullptr;
    CanvasWidget* m_canvas = nullptr;
    FloatingToolbar* m_toolbar = nullptr;
    int m_padFd = -1;
    QSocketNotifier* m_padNotifier = nullptr;
};

} // namespace finily
