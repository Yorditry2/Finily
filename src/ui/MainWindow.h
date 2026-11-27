#pragma once

#include "core/Types.h"
#include "engine/UpdateManager.h"
#include "engine/VisionExtractor.h"

#include <QMainWindow>

#include <array>
#include <functional>

class QShortcut;
#ifdef Q_OS_LINUX
class QSocketNotifier;
class QTimer;
#endif
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
    void setupEditorPage();
    void setupHomeConnections();
    void setupToolbarConnections();
    void setupVisionCallbacks();
    void setupUpdateCallbacks();
    void saveAllDirtyTabsBeforeExit();
    bool openInReusableOrNewTab(const std::function<bool()>& loader);
    void launchNewDocument(const NewDocumentSpec& spec);
    void materializeEphemeralTabIfNeeded();
    void discardActiveEphemeralTab();
    bool isActiveTabEmptyEphemeral() const;
    void updateOpenTabsForPathMapping(const std::function<QString(const QString&)>& mapper);
    void closeOpenTabsMatching(const std::function<bool(const QString&)>& predicate);

    void installShortcuts();
    void reloadPadActions();
    void executeTabletAction(TabletAction action, const QPointF& screenPos = {});
#ifdef Q_OS_LINUX
    void setupWacomPadListener();
    void onWacomPadReadable();
#endif
    void layoutChildren();
    void updateWindowTitle();
    void showEditorPage();
    void showHomePage();
    void startUpdateInstall();
    void vectorizeSelectedImage();
    void startImageExtraction();
    void syncActiveTabFromCanvas();
    void syncTabBarUi();
    void switchToTab(int index);
    void openNewBlankTab();
    void duplicateTab(int index);
    void revealTabInLibrary(int index);
    void exportCurrentPageImage();
    void closeTab(int index);
    bool openNotebookInTab(const QString& path, bool forceNewTab = false);
    void applyStartupPreference();

    QStackedWidget* m_stack = nullptr;
    QWidget* m_container = nullptr; // página 1: contenedor plano del editor
    HomeScreen* m_homeScreen = nullptr;
    CanvasWidget* m_canvas = nullptr;
    FloatingToolbar* m_toolbar = nullptr;
    QVector<NotebookSession> m_tabs;
    int m_activeTab = 0;
    QString m_lastTabBarSig;
    bool m_materializingEphemeral = false;
    UpdateManager* m_updater = nullptr;
    VisionExtractor* m_visionExtractor = nullptr;
    QString m_visionTargetTabId;
    int m_visionTargetPageIndex = -1;
    UpdateInfo m_availableUpdate;
    QString m_pendingInstallerPath;
    QVector<QShortcut*> m_presetShortcuts;
    QShortcut* m_pagePrevAltShortcut = nullptr;
    QShortcut* m_pageNextAltShortcut = nullptr;
    std::array<TabletAction, 4> m_padActions{
        TabletAction::Undo, TabletAction::Redo, TabletAction::Lasso, TabletAction::RadialMenu};
#ifdef Q_OS_LINUX
    int m_padFd = -1;
    QSocketNotifier* m_padNotifier = nullptr;
    QTimer* m_padReconnectTimer = nullptr;
#endif
};

} // namespace finily
