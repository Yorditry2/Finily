#pragma once

#include <QMainWindow>

class QSocketNotifier;

namespace finily {

class CanvasWidget;
class FloatingToolbar;

// Ventana principal nativa de Finily: aloja el CanvasWidget a sangre completa
// con la barra flotante superpuesta, coordina los atajos globales de teclado,
// escucha los 4 botones físicos del Pad Wacom Intuos BT S y gestiona el autoguardado.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void installShortcuts();
    void setupWacomPadListener();
    void onWacomPadReadable();
    void layoutChildren();
    void updateWindowTitle();

    QWidget* m_container = nullptr;
    CanvasWidget* m_canvas = nullptr;
    FloatingToolbar* m_toolbar = nullptr;
    int m_padFd = -1;
    QSocketNotifier* m_padNotifier = nullptr;
};

} // namespace finily
