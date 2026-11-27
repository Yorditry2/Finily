#include "ui/MainWindow.h"

#include "core/Utils.h"
#include "engine/DocumentSerializer.h"
#include "engine/ImageVectorizer.h"
#include "engine/LibraryStore.h"
#include "ui/CanvasWidget.h"
#include "ui/FloatingToolbar.h"
#include "ui/HomeScreen.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QProcess>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include <algorithm>

#ifdef Q_OS_LINUX
#include <QSocketNotifier>

#include <cerrno>
#include <fcntl.h>
#include <linux/joystick.h>
#include <unistd.h>
#endif

namespace finily {
namespace {

QString normalizedFilePath(const QString& path) {
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    const QString canon = info.canonicalFilePath();
    return canon.isEmpty() ? info.absoluteFilePath() : canon;
}

} // namespace

MainWindow::MainWindow() {
    setWindowTitle(tr("Finily"));
    resize(1360, 840);
    setMinimumSize(960, 640);

    setupEditorPage();
    setupHomeConnections();
    setupToolbarConnections();
    installShortcuts();
    reloadPadActions();
#ifdef Q_OS_LINUX
    setupWacomPadListener();
#endif
    setupVisionCallbacks();
    setupUpdateCallbacks();

    // La sesión anterior ya la restauró el constructor de CanvasWidget.
    m_toolbar->refreshFromCanvas();
    syncActiveTabFromCanvas();
    applyStartupPreference();
}

void MainWindow::setupEditorPage() {
    // Dos páginas: 0 = biblioteca (HomeScreen), 1 = editor. El editor vive en
    // un contenedor plano donde el lienzo y la barra son HERMANOS (la barra NO
    // es hija del lienzo). Así, los QTabletEvent ignorados por la barra se
    // propagan al contenedor sin ser aceptados por CanvasWidget::tabletEvent()
    // y Qt genera los clics de mouse sintéticos que hacen funcionales los
    // botones con lápiz.
    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    m_homeScreen = new HomeScreen(m_stack);
    m_stack->addWidget(m_homeScreen); // página 0

    m_container = new QWidget(m_stack);
    m_stack->addWidget(m_container); // página 1

    m_canvas = new CanvasWidget(m_container);
    m_toolbar = new FloatingToolbar(m_canvas, m_container);
    m_container->installEventFilter(this);
    layoutChildren();
    m_toolbar->raise();
    m_toolbar->refreshFromCanvas();

    // Título de ventana y barra de pestañas sincronizados con el cuaderno actual.
    connect(m_canvas, &CanvasWidget::stateChanged, this, [this] {
        materializeEphemeralTabIfNeeded();
        updateWindowTitle();
        syncTabBarUi();
    });
    updateWindowTitle();
}

void MainWindow::setupHomeConnections() {
    if (!m_homeScreen)
        return;

    connect(m_homeScreen, &HomeScreen::createDocumentRequested, this, &MainWindow::launchNewDocument);

    connect(m_homeScreen, &HomeScreen::openNotebookRequested, this, [this](const QString& path) {
        if (!openNotebookInTab(path, false) && m_homeScreen)
            m_homeScreen->showErrorToast(tr("No se pudo abrir el cuaderno."));
    });

    connect(m_homeScreen, &HomeScreen::openNotebookInNewTabRequested, this, [this](const QString& path) {
        if (!openNotebookInTab(path, true) && m_homeScreen)
            m_homeScreen->showErrorToast(tr("No se pudo abrir el cuaderno en una nueva pestaña."));
    });

    connect(m_homeScreen, &HomeScreen::openNotebooksInTabsRequested, this, [this](const QStringList& paths) {
        int failed = 0;
        for (const QString& p : paths) {
            if (!openNotebookInTab(p, true))
                ++failed;
        }
        if (failed > 0 && m_homeScreen)
            m_homeScreen->showErrorToast(tr("Algunos cuadernos no se pudieron abrir."));
    });

    connect(m_homeScreen, &HomeScreen::importPdfFromHomeRequested, this,
            [this](const QString& folderRelativePath) {
                if (!m_homeScreen)
                    return;
                const QString pdfPath = QFileDialog::getOpenFileName(
                    this, QStringLiteral("Importar PDF"), QString(),
                    QStringLiteral("Archivos PDF (*.pdf)"));
                if (pdfPath.isEmpty())
                    return;
                m_homeScreen->startPdfImport(pdfPath, folderRelativePath);
            });

    connect(m_homeScreen, &HomeScreen::openExternalFileRequested, this, [this] {
        if (!m_canvas)
            return;
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Abrir cuaderno"), QString(), QStringLiteral("Cuaderno Finily (*.finily)"));
        if (path.isEmpty())
            return;
        if (!openNotebookInTab(path, false)) {
            if (m_stack && m_stack->currentWidget() == m_homeScreen && m_homeScreen)
                m_homeScreen->showErrorToast(tr("No se pudo abrir el cuaderno."));
            else if (m_toolbar)
                m_toolbar->showToast(tr("No se pudo abrir el cuaderno."));
        }
    });

    connect(m_homeScreen, &HomeScreen::boardStyleChanged, this, [this](const QString& path) {
        const QString norm = normalizedFilePath(path);
        if (norm.isEmpty() || !m_canvas)
            return;
        for (int i = 0; i < m_tabs.size(); ++i) {
            const QString tabPath = (i == m_activeTab) ? m_canvas->currentFilePath()
                                                       : m_tabs.at(i).filePath;
            if (tabPath.isEmpty() || normalizedFilePath(tabPath) != norm)
                continue;
            if (i == m_activeTab) {
                if (!m_canvas->hasUnsavedChanges())
                    m_canvas->loadFromFile(path);
                if (m_toolbar)
                    m_toolbar->refreshFromCanvas();
            } else {
                const NotebookFileMeta meta = readNotebookMetadata(path);
                if (meta.found) {
                    m_tabs[i].themeId = meta.themeId;
                    m_tabs[i].tpl = meta.tpl;
                    m_tabs[i].pageFormat = meta.pageFormat;
                    m_tabs[i].templateScale = meta.templateScale;
                    m_tabs[i].customBackground = meta.customBackground;
                }
            }
            syncTabBarUi();
            break;
        }
    });

    connect(m_homeScreen, &HomeScreen::palmRejectionChanged, this, [this](bool enabled) {
        if (m_canvas)
            m_canvas->setPalmRejectionEnabled(enabled);
        else
            writeAppSetting(QStringLiteral("tablet/palmRejection"), enabled);
    });

    connect(m_homeScreen, &HomeScreen::continueDraftRequested, this, [this] {
        if (!m_canvas)
            return;
        if (!m_tabs.isEmpty()) {
            m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
            m_tabs[m_activeTab].ephemeralIfEmpty = false;
            m_tabs[m_activeTab].pendingTitle.clear();
        }
        m_canvas->loadAutosave();
        syncActiveTabFromCanvas();
        showEditorPage();
        if (m_toolbar)
            m_toolbar->refreshFromCanvas();
        m_canvas->setFocus();
    });

    auto onSingleFileRelocated = [this](const QString& oldPath, const QString& newPath) {
        const QString oldNorm = normalizedFilePath(oldPath);
        if (oldNorm.isEmpty())
            return;
        updateOpenTabsForPathMapping([&oldNorm, &newPath](const QString& tabPath) -> QString {
            return (normalizedFilePath(tabPath) == oldNorm) ? newPath : QString();
        });
    };
    connect(m_homeScreen, &HomeScreen::notebookRenamed, this, onSingleFileRelocated);
    connect(m_homeScreen, &HomeScreen::notebookMoved, this, onSingleFileRelocated);

    connect(m_homeScreen, &HomeScreen::folderRenamed, this,
            [this](const QString& oldAbsDir, const QString& newAbsDir) {
                const QString oldClean = QDir::cleanPath(oldAbsDir);
                const QString newClean = QDir::cleanPath(newAbsDir);
                if (oldClean.isEmpty() || newClean.isEmpty())
                    return;
                const QDir oldDir(oldClean);
                const QDir newDir(newClean);
                const QString prefix = oldClean + QLatin1Char('/');
                updateOpenTabsForPathMapping([&](const QString& tabPath) -> QString {
                    const QString cleanTab = QDir::cleanPath(tabPath);
                    if (!cleanTab.startsWith(prefix))
                        return {};
                    return newDir.filePath(oldDir.relativeFilePath(cleanTab));
                });
            });

    connect(m_homeScreen, &HomeScreen::notebookDeleted, this, [this](const QString& filePath) {
        const QString targetNorm = normalizedFilePath(filePath);
        if (targetNorm.isEmpty())
            return;
        closeOpenTabsMatching([&targetNorm](const QString& tabPath) {
            return normalizedFilePath(tabPath) == targetNorm;
        });
    });

    connect(m_homeScreen, &HomeScreen::folderDeleted, this, [this](const QString& absDir) {
        const QString clean = QDir::cleanPath(absDir);
        if (clean.isEmpty())
            return;
        const QString prefix = clean + QLatin1Char('/');
        closeOpenTabsMatching([&prefix](const QString& tabPath) {
            return QDir::cleanPath(tabPath).startsWith(prefix);
        });
    });
}

void MainWindow::setupToolbarConnections() {
    if (!m_toolbar)
        return;

    connect(m_toolbar, &FloatingToolbar::newTabRequested, this, &MainWindow::openNewBlankTab);
    connect(m_toolbar, &FloatingToolbar::createDocumentRequested, this, &MainWindow::launchNewDocument);
    connect(m_toolbar, &FloatingToolbar::openNotebookInNewTabRequested, this, [this](const QString& path) {
        if (!openNotebookInTab(path, true) && m_toolbar)
            m_toolbar->showToast(tr("No se pudo abrir el cuaderno"));
    });
    connect(m_toolbar, &FloatingToolbar::importPdfInNewTabRequested, this, [this](const QString& path) {
        if (!m_canvas)
            return;
        if (openInReusableOrNewTab([this, &path] { return m_canvas->importPdfFile(path, true); })) {
            showEditorPage();
            m_canvas->resetViewport();
            if (m_toolbar)
                m_toolbar->refreshFromCanvas();
            m_canvas->setFocus();
        } else if (m_toolbar) {
            m_toolbar->showToast(tr("No se pudo importar el PDF"));
        }
    });
    connect(m_toolbar, &FloatingToolbar::switchTabRequested, this, &MainWindow::switchToTab);
    connect(m_toolbar, &FloatingToolbar::closeTabRequested, this, &MainWindow::closeTab);
    connect(m_toolbar, &FloatingToolbar::duplicateTabRequested, this, &MainWindow::duplicateTab);
    connect(m_toolbar, &FloatingToolbar::revealInLibraryRequested, this, &MainWindow::revealTabInLibrary);
    connect(m_toolbar, &FloatingToolbar::exportPageImageRequested, this, &MainWindow::exportCurrentPageImage);
    connect(m_toolbar, &FloatingToolbar::fullscreenToggled, this, [this] {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });
    connect(m_toolbar, &FloatingToolbar::padActionsChanged, this, &MainWindow::reloadPadActions);
    if (m_canvas) {
        connect(m_canvas, &CanvasWidget::tabletActionTriggered, this, &MainWindow::executeTabletAction);
    }

    // Botón 'Inicio' de la pill: guardar y volver a la biblioteca.
    connect(m_toolbar, &FloatingToolbar::homeRequested, this, [this] {
        if (!m_canvas)
            return;
        materializeEphemeralTabIfNeeded();
        if (isActiveTabEmptyEphemeral()) {
            discardActiveEphemeralTab();
        } else if (!m_canvas->currentFilePath().isEmpty()) {
            if (!m_canvas->saveToFile(m_canvas->currentFilePath()))
                return;
        } else {
            m_canvas->saveAutosave();  // programa el autoguardado
            m_canvas->flushAutosave(); // y lo escribe ya antes de salir
        }
        if (m_homeScreen)
            m_homeScreen->refreshLibrary();
        showHomePage();
    });

    connect(m_toolbar, &FloatingToolbar::renameRequested, this, [this](const QString& rawName) {
        if (!m_canvas || !m_toolbar)
            return;
        const QString clean = LibraryStore::sanitizeBaseName(rawName);
        if (clean.isEmpty())
            return;
        const QString oldPath = m_canvas->currentFilePath();
        const QString oldKey = normalizedFilePath(oldPath);
        const QStringList directories = oldPath.isEmpty()
            ? LibraryStore::libraryDirectories()
            : QStringList{QFileInfo(oldPath).absolutePath()};
        for (const QString& directory : directories) {
            const QString target = QDir(directory).filePath(clean + QStringLiteral(".finily"));
            const QString targetKey = normalizedFilePath(target);
            if (!oldPath.isEmpty() && (target == oldPath || targetKey == oldKey)) {
                updateWindowTitle();
                return;
            }
            for (int i = 0; i < m_tabs.size(); ++i) {
                if (i == m_activeTab)
                    continue;
                if (!m_tabs.at(i).filePath.isEmpty()
                    && normalizedFilePath(m_tabs.at(i).filePath) == targetKey) {
                    m_toolbar->showToast(tr("Ya hay otra pestaña abierta con ese nombre"));
                    updateWindowTitle();
                    return;
                }
            }
            if (QFile::exists(target)) {
                m_toolbar->showToast(tr("Ya existe un cuaderno con ese nombre"));
                updateWindowTitle();
                return;
            }
            if (oldPath.isEmpty()) {
                if (!QDir().mkpath(directory) || !m_canvas->saveToFile(target, true))
                    continue;
                if (!m_tabs.isEmpty()) {
                    const int idx = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
                    m_tabs[idx].ephemeralIfEmpty = false;
                    m_tabs[idx].pendingTitle.clear();
                }
            } else {
                if (!m_canvas->saveToFile(oldPath))
                    break;
                m_canvas->flushAutosave();
                if (!QFile::rename(oldPath, target))
                    break;
                moveCoverThumbnail(oldPath, target);
                m_canvas->setCurrentFilePath(target);
                m_canvas->saveAutosave();
            }
            m_lastTabBarSig.clear();
            syncTabBarUi();
            updateWindowTitle();
            m_toolbar->showToast(tr("Cuaderno %1").arg(clean));
            return;
        }
        m_toolbar->showToast(oldPath.isEmpty() ? tr("No se pudo guardar el cuaderno")
                                               : tr("No se pudo renombrar el cuaderno"));
        updateWindowTitle();
    });
}

void MainWindow::setupVisionCallbacks() {
    m_visionExtractor = new VisionExtractor(this);

    if (m_toolbar) {
        connect(m_toolbar, &FloatingToolbar::vectorizeSelectedImageRequested, this,
                &MainWindow::vectorizeSelectedImage);
        connect(m_toolbar, &FloatingToolbar::configureAiApiRequested, this, [this] {
            if (m_toolbar)
                m_toolbar->openSettingsModal(QStringLiteral("ia"));
        });
        connect(m_toolbar, &FloatingToolbar::extractSelectedImageRequested, this,
                &MainWindow::startImageExtraction);
    }

    connect(m_visionExtractor, &VisionExtractor::extractionFinished, this,
            [this](const ExtractedContent& content) {
                const QString targetTabId = m_visionTargetTabId;
                const int targetPageIndex = m_visionTargetPageIndex;
                m_visionTargetTabId.clear();
                m_visionTargetPageIndex = -1;
                if (!m_canvas)
                    return;

                int targetTab = -1;
                if (!targetTabId.isEmpty()) {
                    for (int i = 0; i < m_tabs.size(); ++i) {
                        if (m_tabs.at(i).id == targetTabId) {
                            targetTab = i;
                            break;
                        }
                    }
                }
                if (targetTab < 0) {
                    if (m_toolbar)
                        m_toolbar->showToast(tr("La pestaña de origen fue cerrada"));
                    return;
                }
                if (targetTab != m_activeTab)
                    switchToTab(targetTab);
                if (targetPageIndex >= 0 && targetPageIndex < m_canvas->pageCount()
                    && m_canvas->currentPageIndex() != targetPageIndex) {
                    m_canvas->goToPage(targetPageIndex);
                }
                m_canvas->insertExtractedContent(content.shapes, content.texts);
                if (m_toolbar) {
                    m_toolbar->refreshFromCanvas();
                    m_toolbar->showToast(tr("Extraído: %1 figuras, %2 textos ✓")
                                             .arg(content.shapes.size())
                                             .arg(content.texts.size()));
                }
            });

    connect(m_visionExtractor, &VisionExtractor::extractionFailed, this, [this](const QString& error) {
        m_visionTargetTabId.clear();
        m_visionTargetPageIndex = -1;
        if (m_toolbar)
            m_toolbar->showToast(tr("IA: %1").arg(error));
    });
}

void MainWindow::setupUpdateCallbacks() {
    m_updater = new UpdateManager(this);
#ifdef Q_OS_WIN
    constexpr bool kCanInstallInApp = true;
#else
    constexpr bool kCanInstallInApp = false;
#endif

    if (m_toolbar) {
        connect(m_toolbar, &FloatingToolbar::checkForUpdatesRequested, this, [this] {
            if (!m_updater)
                return;
            if (m_toolbar)
                m_toolbar->showToast(tr("Buscando actualizaciones…"));
            m_updater->checkForUpdates(true);
        });
    }

    connect(m_updater, &UpdateManager::updateAvailable, this,
            [this](const UpdateInfo& info, bool manual) {
                m_availableUpdate = info;
                m_pendingInstallerPath.clear();
                const bool canDirect =
                    kCanInstallInApp && !info.installerUrl.isEmpty() && !info.installerSha256.isEmpty();
                const bool onHome = (m_stack && m_stack->currentWidget() == m_homeScreen);
                if (m_toolbar)
                    m_toolbar->setUpdateAvailable(info.version);
                if (m_homeScreen)
                    m_homeScreen->showUpdateBanner(info.version, canDirect);
                if (manual && onHome && m_homeScreen) {
                    m_homeScreen->showInfoToast(
                        tr("Nueva versión Finily v%1 disponible").arg(info.version),
                        QStringLiteral("info"));
                } else if (manual && !onHome && m_toolbar) {
                    m_toolbar->showToast(
                        tr("Nueva versión Finily v%1 disponible").arg(info.version));
                }
            });

    connect(m_updater, &UpdateManager::upToDate, this, [this](bool manual) {
        if (!manual)
            return;
        const QString msg = tr("Finily v%1 está actualizado ✓").arg(QApplication::applicationVersion());
        const bool onHome = (m_stack && m_stack->currentWidget() == m_homeScreen);
        if (onHome && m_homeScreen)
            m_homeScreen->showInfoToast(msg, QStringLiteral("check"));
        else if (m_toolbar)
            m_toolbar->showToast(msg);
    });

    connect(m_updater, &UpdateManager::checkFailed, this, [this](const QString& error, bool manual) {
        if (!manual)
            return;
        const bool onHome = (m_stack && m_stack->currentWidget() == m_homeScreen);
        if (onHome && m_homeScreen)
            m_homeScreen->showErrorToast(tr("No se pudo comprobar actualizaciones"));
        else if (m_toolbar)
            m_toolbar->showToast(tr("No se pudo comprobar actualizaciones: %1").arg(error));
    });

    if (m_homeScreen) {
        connect(m_homeScreen, &HomeScreen::checkUpdatesRequested, this, [this] {
            if (m_updater)
                m_updater->checkForUpdates(true);
        });
        connect(m_homeScreen, &HomeScreen::pressureSensitivityChanged, this, [this](double v) {
            if (m_canvas)
                m_canvas->setPressureSensitivity(v);
        });
        connect(m_homeScreen, &HomeScreen::updateInstallRequested, this,
                &MainWindow::startUpdateInstall);
        connect(m_homeScreen, &HomeScreen::updateCancelRequested, this, [this] {
            if (m_updater)
                m_updater->cancelDownload();
        });
        connect(m_homeScreen, &HomeScreen::updateReleaseNotesRequested, this, [this] {
            if (!m_availableUpdate.releaseUrl.isEmpty())
                QDesktopServices::openUrl(QUrl(m_availableUpdate.releaseUrl));
        });
        connect(m_homeScreen, &HomeScreen::updateDismissed, this, [this] {
            if (m_updater && m_availableUpdate.valid)
                m_updater->skipVersion(m_availableUpdate.version);
            if (m_homeScreen)
                m_homeScreen->hideUpdateBanner();
        });
        connect(m_homeScreen, &HomeScreen::updateRestartRequested, this, [this] {
            if (!m_pendingInstallerPath.isEmpty()) {
                saveAllDirtyTabsBeforeExit();
                if (QProcess::startDetached(m_pendingInstallerPath, {})) {
                    QApplication::quit();
                    return;
                }
                if (m_homeScreen) {
                    m_homeScreen->showUpdateError(
                        tr("No se pudo iniciar el instalador descargado"));
                }
                return;
            }
            startUpdateInstall();
        });
    }

    connect(m_updater, &UpdateManager::downloadProgress, this, [this](qint64 received, qint64 total) {
        const int percent = total > 0 ? int((received * 100) / total) : 0;
        if (m_homeScreen)
            m_homeScreen->setUpdateDownloadProgress(percent);
    });

    connect(m_updater, &UpdateManager::downloadCancelled, this, [this] {
        m_pendingInstallerPath.clear();
        if (m_homeScreen)
            m_homeScreen->resetUpdateBannerState();
    });

    connect(m_updater, &UpdateManager::downloadFailed, this, [this](const QString& error) {
        m_pendingInstallerPath.clear();
        const bool onHome = (m_stack && m_stack->currentWidget() == m_homeScreen);
        if (onHome && m_homeScreen) {
            m_homeScreen->showUpdateError(error);
        } else {
            if (m_homeScreen)
                m_homeScreen->resetUpdateBannerState();
            if (m_toolbar)
                m_toolbar->showToast(tr("Error al descargar actualización: %1").arg(error));
        }
    });

    connect(m_updater, &UpdateManager::installerReady, this,
            [this](const QString& installerPath, const UpdateInfo& info) {
                Q_UNUSED(info);
                const bool onHome = (m_stack && m_stack->currentWidget() == m_homeScreen);
                if (onHome && m_homeScreen) {
                    m_pendingInstallerPath = installerPath;
                    m_homeScreen->showUpdateReadyToInstall(info.version);
                    return;
                }
                if (m_homeScreen)
                    m_homeScreen->resetUpdateBannerState();
                saveAllDirtyTabsBeforeExit();
                if (QProcess::startDetached(installerPath, {})) {
                    QApplication::quit();
                } else if (m_toolbar) {
                    m_toolbar->showToast(tr("Instalador guardado en: %1").arg(installerPath));
                }
            });

    QTimer::singleShot(3000, this, [this] {
        if (m_updater)
            m_updater->checkForUpdates(false);
    });
}

void MainWindow::saveAllDirtyTabsBeforeExit() {
    materializeEphemeralTabIfNeeded();
    const bool skipFlush = isActiveTabEmptyEphemeral();
    syncActiveTabFromCanvas();
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (i == m_activeTab)
            continue;
        NotebookSession& tab = m_tabs[i];
        if (!tab.filePath.isEmpty() && tab.dirty && !tab.pages.isEmpty()) {
            const int cur = std::clamp(tab.currentPageIndex, 0, int(tab.pages.size()) - 1);
            const Viewport vp = tab.pages.at(cur).viewport;
            const auto settings = documentSettings(ShapeKind::Triangle, true, true, false,
                                                   PressureProfile::Light, tab.themeId, tab.tpl,
                                                   vp, tab.pageFormat, tab.templateScale,
                                                   tab.cover.colorIndex, tab.cover.iconIndex,
                                                   tab.customBackground);
            if (serializeDocumentToFile(tab.filePath, tab.pages, cur, settings)) {
                tab.dirty = false;
                updateCoverThumbnailForPages(tab.filePath, tab.pages, tab.themeId, tab.tpl,
                                             tab.pageFormat, tab.templateScale,
                                             tab.customBackground);
            }
        }
    }
    if (m_canvas) {
        if (!m_canvas->currentFilePath().isEmpty() && m_canvas->hasUnsavedChanges())
            m_canvas->saveToFile(m_canvas->currentFilePath());
        if (skipFlush)
            m_canvas->cancelPendingAutosave();
        else
            m_canvas->flushAutosave();
    }
    m_lastTabBarSig.clear();
    syncTabBarUi();
}

bool MainWindow::openInReusableOrNewTab(const std::function<bool()>& loader) {
    if (!m_canvas || !loader)
        return false;
    const bool reuseCurrent = !m_tabs.isEmpty()
        && m_canvas->currentFilePath().isEmpty()
        && !m_canvas->currentPageHasContent()
        && !m_canvas->hasAnyContent()
        && m_canvas->pageCount() == 1
        && !m_canvas->hasUnsavedChanges();
    const int prevActive = m_activeTab;
    if (!reuseCurrent) {
        if (isActiveTabEmptyEphemeral())
            m_canvas->cancelPendingAutosave();
        else
            m_canvas->flushAutosave();
        syncActiveTabFromCanvas();
        NotebookSession slot;
        slot.id = freshId();
        m_tabs.append(slot);
        m_activeTab = int(m_tabs.size()) - 1;
    }
    if (!loader()) {
        if (!reuseCurrent && !m_tabs.isEmpty()) {
            m_tabs.removeLast();
            m_activeTab = std::clamp(prevActive, 0, std::max(0, int(m_tabs.size()) - 1));
            if (!m_tabs.isEmpty())
                m_canvas->importSession(m_tabs.at(m_activeTab));
            m_lastTabBarSig.clear();
            syncTabBarUi();
        }
        return false;
    }
    if (!m_tabs.isEmpty()) {
        m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
        m_tabs[m_activeTab].ephemeralIfEmpty = false;
        m_tabs[m_activeTab].pendingTitle.clear();
    }
    m_lastTabBarSig.clear();
    syncActiveTabFromCanvas();
    return true;
}

void MainWindow::launchNewDocument(const NewDocumentSpec& spec) {
    if (!m_canvas)
        return;
    if (spec.kind == DocumentKind::Board || spec.kind == DocumentKind::Notebook)
        LibraryStore::saveLastUsedSpec(spec);

    if (spec.ephemeralIfEmpty) {
        auto loader = [this, &spec] {
            m_canvas->startEphemeralDocument(spec.theme, spec.tpl, spec.format,
                                             spec.templateScale, spec.cover,
                                             spec.customBackground);
            return true;
        };
        if (openInReusableOrNewTab(loader)) {
            if (!m_tabs.isEmpty()) {
                m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
                m_tabs[m_activeTab].ephemeralIfEmpty = true;
                m_tabs[m_activeTab].pendingTitle = spec.title;
            }
            m_lastTabBarSig.clear();
            showEditorPage();
            if (spec.format != PageFormat::Infinite)
                m_canvas->resetViewport();
            if (m_toolbar)
                m_toolbar->refreshFromCanvas();
            syncTabBarUi();
            updateWindowTitle();
            m_canvas->setFocus();
        }
        return;
    }

    const QString base = LibraryStore::sanitizeBaseName(spec.title);
    const QString safeName = base.isEmpty() ? tr("Sin título") : base;
    QStringList targetDirs;
    if (!spec.folderRelativePath.isEmpty()) {
        const QString resolved = LibraryStore::resolveDirectory(spec.folderRelativePath);
        if (!resolved.isEmpty())
            targetDirs.append(resolved);
    }
    targetDirs.append(LibraryStore::libraryDirectories());

    for (const QString& directory : targetDirs) {
        if (!QDir().mkpath(directory))
            continue;
        const QString path = LibraryStore::uniqueNotebookPath(directory, safeName);
        auto loader = [this, &path, &spec] {
            return m_canvas->createNotebook(path, spec.theme, spec.tpl, spec.format,
                                            spec.templateScale, spec.cover,
                                            spec.customBackground);
        };
        if (openInReusableOrNewTab(loader)) {
            for (const QString& tagId : spec.tagIds)
                LibraryStore::setDocumentTag(path, tagId, true);
            showEditorPage();
            m_canvas->resetViewport();
            if (m_toolbar)
                m_toolbar->refreshFromCanvas();
            m_canvas->setFocus();
            return;
        }
    }
    const QString err = tr("No se pudo guardar el documento nuevo. Revisa el nombre, "
                           "el espacio libre y los permisos de la carpeta.");
    if (m_stack && m_stack->currentWidget() == m_homeScreen && m_homeScreen)
        m_homeScreen->showErrorToast(err);
    else if (m_toolbar)
        m_toolbar->showToast(err);
}

void MainWindow::updateOpenTabsForPathMapping(
    const std::function<QString(const QString&)>& mapper) {
    bool matched = false;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const QString tabPath = (i == m_activeTab && m_canvas)
            ? m_canvas->currentFilePath()
            : m_tabs.at(i).filePath;
        if (tabPath.isEmpty())
            continue;
        const QString mapped = mapper(tabPath);
        if (mapped.isEmpty())
            continue;
        m_tabs[i].filePath = mapped;
        if (i == m_activeTab && m_canvas) {
            m_canvas->setCurrentFilePath(mapped);
            m_canvas->saveAutosave();
        }
        matched = true;
    }
    if (!matched)
        return;
    m_lastTabBarSig.clear();
    updateWindowTitle();
    syncTabBarUi();
}

void MainWindow::closeOpenTabsMatching(const std::function<bool(const QString&)>& predicate) {
    QVector<int> matchingIndices;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const QString tabPath = (i == m_activeTab && m_canvas)
            ? m_canvas->currentFilePath()
            : m_tabs.at(i).filePath;
        if (!tabPath.isEmpty() && predicate(tabPath))
            matchingIndices.append(i);
    }
    if (matchingIndices.isEmpty())
        return;

    if (matchingIndices.size() >= m_tabs.size()) {
        m_tabs.clear();
        m_activeTab = 0;
        if (m_canvas) {
            m_canvas->newDocument();
            syncActiveTabFromCanvas();
        }
        if (m_toolbar)
            m_toolbar->refreshFromCanvas();
        m_lastTabBarSig.clear();
        updateWindowTitle();
        syncTabBarUi();
        return;
    }

    bool activeRemoved = false;
    int newActive = m_activeTab;
    for (int k = int(matchingIndices.size()) - 1; k >= 0; --k) {
        const int idx = matchingIndices.at(k);
        if (idx == m_activeTab)
            activeRemoved = true;
        else if (idx < newActive)
            --newActive;
        m_tabs.removeAt(idx);
    }
    m_activeTab = std::clamp(newActive, 0, int(m_tabs.size()) - 1);
    m_lastTabBarSig.clear();
    if (activeRemoved && m_canvas) {
        m_canvas->importSession(m_tabs.at(m_activeTab));
        if (m_toolbar)
            m_toolbar->refreshFromCanvas();
    }
    updateWindowTitle();
    syncTabBarUi();
}

bool MainWindow::isActiveTabEmptyEphemeral() const {
    if (!m_canvas || m_tabs.isEmpty())
        return false;
    const int idx = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
    return m_tabs.at(idx).ephemeralIfEmpty
        && m_canvas->currentFilePath().isEmpty()
        && !m_canvas->hasAnyContent();
}

void MainWindow::materializeEphemeralTabIfNeeded() {
    if (m_materializingEphemeral || !m_canvas || m_tabs.isEmpty())
        return;
    m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
    NotebookSession& tab = m_tabs[m_activeTab];
    if (!tab.ephemeralIfEmpty || !m_canvas->currentFilePath().isEmpty() || !m_canvas->hasAnyContent())
        return;

    m_materializingEphemeral = true;
    const QString rawTitle =
        tab.pendingTitle.isEmpty() ? LibraryStore::defaultQuickNoteTitle() : tab.pendingTitle;
    const QString safeBase = LibraryStore::sanitizeBaseName(rawTitle);
    const QString base = safeBase.isEmpty() ? QStringLiteral("Nota rápida") : safeBase;
    for (const QString& directory : LibraryStore::libraryDirectories()) {
        if (!QDir().mkpath(directory))
            continue;
        const QString path = LibraryStore::uniqueNotebookPath(directory, base);
        if (m_canvas->saveToFile(path, /*newFile=*/true, /*silent=*/true)) {
            tab.ephemeralIfEmpty = false;
            tab.pendingTitle.clear();
            tab.filePath = path;
            m_lastTabBarSig.clear();
            break;
        }
    }
    m_materializingEphemeral = false;
}

void MainWindow::discardActiveEphemeralTab() {
    if (!m_canvas)
        return;
    m_canvas->cancelPendingAutosave();
    if (m_tabs.size() > 1) {
        m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
        m_tabs.removeAt(m_activeTab);
        m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
        m_lastTabBarSig.clear();
        m_canvas->importSession(m_tabs.at(m_activeTab));
    } else {
        if (!m_tabs.isEmpty()) {
            m_tabs[0].ephemeralIfEmpty = false;
            m_tabs[0].pendingTitle.clear();
        }
        m_canvas->newDocument();
        m_canvas->cancelPendingAutosave();
        syncActiveTabFromCanvas();
    }
    if (m_toolbar)
        m_toolbar->refreshFromCanvas();
    updateWindowTitle();
    syncTabBarUi();
}

void MainWindow::startUpdateInstall() {
    if (!m_availableUpdate.valid)
        return;
#ifdef Q_OS_WIN
    if (m_updater && !m_availableUpdate.installerUrl.isEmpty()
        && !m_availableUpdate.installerSha256.isEmpty()) {
        if (m_updater->isDownloading())
            return;
        const bool onHome = (m_stack && m_stack->currentWidget() == m_homeScreen);
        if (onHome && m_homeScreen) {
            m_homeScreen->setUpdateDownloadProgress(0);
        } else if (m_toolbar) {
            m_toolbar->showToast(tr("Descargando Finily v%1…").arg(m_availableUpdate.version));
        }
        m_updater->downloadUpdate(m_availableUpdate);
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl(m_availableUpdate.releaseUrl));
}

MainWindow::~MainWindow() {
#ifdef Q_OS_LINUX
    if (m_padReconnectTimer) {
        m_padReconnectTimer->stop();
        delete m_padReconnectTimer;
        m_padReconnectTimer = nullptr;
    }
    if (m_padNotifier) {
        m_padNotifier->setEnabled(false);
        delete m_padNotifier;
        m_padNotifier = nullptr;
    }
    if (m_padFd >= 0) {
        ::close(m_padFd);
        m_padFd = -1;
    }
#endif
}

void MainWindow::applyStartupPreference() {
#ifdef FINILY_DEV_SNAPSHOTS
    const QString edScene = qEnvironmentVariable("FINILY_SNAPSHOT_EDITOR");
    if (!edScene.isEmpty()) {
        const ThemeId th = qEnvironmentVariableIntValue("FINILY_SNAPSHOT_DARK") == 1
            ? ThemeId::DarkBoard
            : ThemeId::PaperLight;
        const bool isPageDoc = (edScene == QLatin1String("paper")
                                || edScene == QLatin1String("pages")
                                || edScene == QLatin1String("elements"));
        NewDocumentSpec spec;
        spec.kind = isPageDoc ? DocumentKind::Notebook : DocumentKind::Board;
        spec.title = QStringLiteral("Clase de Física");
        spec.theme = th;
        spec.tpl = BackgroundTemplate::Grid;
        spec.format = isPageDoc ? PageFormat::A4 : PageFormat::Infinite;
        spec.ephemeralIfEmpty = true;
        launchNewDocument(spec);
        if (edScene == QLatin1String("top") || edScene == QLatin1String("left")
            || edScene == QLatin1String("float")) {
            NotebookSession tab2;
            tab2.id = freshId();
            tab2.pages = {PageData{}};
            tab2.themeId = th;
            tab2.tpl = BackgroundTemplate::Blank;
            tab2.pageFormat = PageFormat::A4;
            tab2.ephemeralIfEmpty = true;
            tab2.pendingTitle = QStringLiteral("Guía de Problemas");
            m_tabs.append(std::move(tab2));
            m_lastTabBarSig.clear();
            syncTabBarUi();
        } else if (edScene == QLatin1String("capsule") && m_canvas) {
            m_canvas->createTableShape(3, 3);
            m_canvas->setTool(ToolType::Select);
        }
        showEditorPage();
        QTimer::singleShot(150, this, [this, edScene] {
            if (m_toolbar) {
                m_toolbar->refreshFromCanvas();
                m_toolbar->applySnapshotScene(edScene);
            }
        });
        return;
    }
    if (!qEnvironmentVariableIsEmpty("FINILY_SNAPSHOT")
        || !qEnvironmentVariableIsEmpty("FINILY_SNAPSHOT_SHEET")
        || !qEnvironmentVariableIsEmpty("FINILY_SNAPSHOT_FOLDER")) {
        return;
    }
#endif
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && QFileInfo::exists(args.value(1)))
        return;

    const int behavior = std::clamp(readAppSetting(QStringLiteral("home/startupBehavior"), 0).toInt(), 0, 2);
    if (behavior == 1) {
        const QVector<NotebookMeta> docs = LibraryStore::listAllNotebooksRecursive();
        if (!docs.isEmpty()) {
            const auto newest = std::max_element(docs.cbegin(), docs.cend(),
                                                 [](const NotebookMeta& a, const NotebookMeta& b) {
                                                     return a.modified < b.modified;
                                                 });
            openNotebookInTab(newest->path, false);
        }
    } else if (behavior == 2) {
        launchNewDocument(LibraryStore::lastUsedQuickNoteSpec());
    }
}

void MainWindow::openInitialFile(const QString& path) {
    if (!m_canvas)
        return;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return; // sin archivo válido: arranca limpio en la pantalla de inicio
    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("pdf")) {
        if (!m_canvas->importPdfFile(info.absoluteFilePath(), true)) {
            if (m_homeScreen)
                m_homeScreen->showErrorToast(tr("No se pudo importar el PDF."));
            return;
        }
        syncActiveTabFromCanvas();
        updateWindowTitle();
        showEditorPage();
        m_canvas->resetViewport();
        m_canvas->setFocus();
    } else if (suffix == QLatin1String("finily")) {
        if (!openNotebookInTab(info.absoluteFilePath())) {
            if (m_homeScreen)
                m_homeScreen->showErrorToast(tr("No se pudo abrir el cuaderno."));
            return;
        }
    }
}

void MainWindow::showEditorPage() {
    if (m_stack)
        m_stack->setCurrentWidget(m_container);
    layoutChildren();
    if (m_toolbar) {
        m_toolbar->raise();
        m_toolbar->refreshFromCanvas();
    }
    updateWindowTitle();
}

void MainWindow::showHomePage() {
    if (m_stack)
        m_stack->setCurrentWidget(m_homeScreen);
    setWindowTitle(tr("Finily — Biblioteca"));
}

#ifdef Q_OS_LINUX
void MainWindow::setupWacomPadListener() {
    if (!m_padReconnectTimer) {
        m_padReconnectTimer = new QTimer(this);
        m_padReconnectTimer->setInterval(3000);
        connect(m_padReconnectTimer, &QTimer::timeout, this, &MainWindow::setupWacomPadListener);
    }
    if (m_padFd >= 0) {
        m_padReconnectTimer->stop();
        return;
    }

    // Auto-detecta el dispositivo /dev/input/js* de "Wacom ... Pad" (plug-and-play sin root).
    QDir sysInput("/sys/class/input");
    const QStringList entries = sysInput.entryList({"js*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    QString padDevicePath;
    for (const QString& entry : entries) {
        QFile nameFile(QString("/sys/class/input/%1/device/name").arg(entry));
        if (nameFile.open(QIODevice::ReadOnly)) {
            const QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
            if (name.contains("Wacom", Qt::CaseInsensitive) && name.contains("Pad", Qt::CaseInsensitive)) {
                padDevicePath = QString("/dev/input/%1").arg(entry);
                break;
            }
        }
    }
    if (padDevicePath.isEmpty()) {
        if (!m_padReconnectTimer->isActive())
            m_padReconnectTimer->start();
        return;
    }

    m_padFd = ::open(padDevicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_padFd < 0) {
        if (!m_padReconnectTimer->isActive())
            m_padReconnectTimer->start();
        return;
    }

    if (m_padNotifier) {
        m_padNotifier->setEnabled(false);
        m_padNotifier->deleteLater();
        m_padNotifier = nullptr;
    }
    m_padNotifier = new QSocketNotifier(m_padFd, QSocketNotifier::Read, this);
    connect(m_padNotifier, &QSocketNotifier::activated, this, &MainWindow::onWacomPadReadable);
    m_padReconnectTimer->stop();
}

void MainWindow::onWacomPadReadable() {
    if (m_padFd < 0)
        return;

    struct js_event ev {};
    while (true) {
        const ssize_t n = ::read(m_padFd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
        }
        if (n <= 0) {
            if (m_padNotifier) {
                m_padNotifier->setEnabled(false);
                m_padNotifier->deleteLater();
                m_padNotifier = nullptr;
            }
            if (m_padFd >= 0) {
                ::close(m_padFd);
                m_padFd = -1;
            }
            if (m_padReconnectTimer && !m_padReconnectTimer->isActive())
                m_padReconnectTimer->start();
            return;
        }
        if (n != static_cast<ssize_t>(sizeof(ev)))
            break;
        // Ignorar eventos sintéticos iniciales (JS_EVENT_INIT) y soltar botón (value == 0)
        if ((ev.type & JS_EVENT_INIT) != 0)
            continue;
        if (ev.type != JS_EVENT_BUTTON || ev.value != 1)
            continue;
        // Solo actúa en la página del editor (la biblioteca ignora el pad).
        if (!m_stack || m_stack->currentWidget() != m_container)
            continue;
        // Solo actuar cuando la ventana de Finily está enfocada y no hay ningún modal abierto
        if ((!isActiveWindow() && !(m_canvas && m_canvas->hasFocus()))
            || !m_canvas || !m_toolbar || m_toolbar->hasModalOpen())
            continue;

        if (ev.number < m_padActions.size())
            executeTabletAction(m_padActions[ev.number]);
    }
}
#endif

void MainWindow::reloadPadActions() {
    constexpr TabletAction defaults[4] = {
        TabletAction::Undo,
        TabletAction::Redo,
        TabletAction::Lasso,
        TabletAction::RadialMenu,
    };
    for (int i = 0; i < 4; ++i) {
        const int raw = readAppSetting(QStringLiteral("tablet/padButton%1").arg(i), int(defaults[i])).toInt();
        m_padActions[i] = TabletAction(raw);
    }
}

void MainWindow::executeTabletAction(TabletAction action, const QPointF& screenPos) {
    if (!m_canvas || !m_toolbar)
        return;
    switch (action) {
    case TabletAction::None:
        return;
    case TabletAction::Undo:
        m_canvas->undo();
        m_toolbar->refreshFromCanvas();
        return;
    case TabletAction::Redo:
        m_canvas->redo();
        m_toolbar->refreshFromCanvas();
        return;
    case TabletAction::Pen:
        m_toolbar->selectTool(ToolType::Pen);
        return;
    case TabletAction::Highlighter:
        m_toolbar->selectTool(ToolType::Highlighter);
        return;
    case TabletAction::Eraser:
    case TabletAction::HoldEraser:
        m_toolbar->selectTool(ToolType::Eraser);
        return;
    case TabletAction::LastWriteTool:
        m_toolbar->selectTool(m_canvas->lastWriteTool() == ToolType::Ballpoint
                                  ? ToolType::Ballpoint
                                  : ToolType::Pen);
        return;
    case TabletAction::RightClickMenu:
        if (m_canvas->hasSelection()) {
            const QPointF target = screenPos.isNull() ? m_canvas->lastHoverScreenPos() : screenPos;
            emit m_canvas->selectionMenuRequested(m_canvas->mapToGlobal(target.toPoint()));
        } else if (m_canvas->isRadialMenuOpen()) {
            m_canvas->closeRadialMenu();
        } else if (m_canvas->radialMenuEnabled()) {
            const QPointF target = screenPos.isNull() ? m_canvas->lastHoverScreenPos() : screenPos;
            m_canvas->openRadialMenu(target);
        }
        return;
    case TabletAction::RadialMenu:
        if (m_canvas->isRadialMenuOpen()) {
            m_canvas->closeRadialMenu();
        } else if (m_canvas->radialMenuEnabled()) {
            const QPointF target = screenPos.isNull() ? m_canvas->lastHoverScreenPos() : screenPos;
            m_canvas->openRadialMenu(target);
        }
        return;
    case TabletAction::Lasso:
        m_toolbar->selectTool(ToolType::Lasso);
        return;
    case TabletAction::Select:
        m_toolbar->selectTool(ToolType::Select);
        return;
    case TabletAction::Shapes:
        m_toolbar->selectTool(ToolType::Shape);
        return;
    case TabletAction::Polyline:
        m_toolbar->selectTool(ToolType::Polyline);
        return;
    case TabletAction::Hand:
        m_toolbar->selectTool(ToolType::Hand);
        return;
    case TabletAction::Laser:
        m_toolbar->selectTool(ToolType::Laser);
        return;
    case TabletAction::ToggleCleanUi:
        m_toolbar->toggleCleanUi();
        return;
    case TabletAction::NextPage:
        m_canvas->nextPage();
        m_toolbar->refreshFromCanvas();
        return;
    case TabletAction::PrevPage:
        m_canvas->prevPage();
        m_toolbar->refreshFromCanvas();
        return;
    }
}

void MainWindow::layoutChildren() {
    if (!m_container)
        return;
    const QRect area = m_container->rect();
    if (m_canvas)
        m_canvas->setGeometry(area);
    if (m_toolbar)
        m_toolbar->setGeometry(area);
}

void MainWindow::updateWindowTitle() {
    const QString path = m_canvas ? m_canvas->currentFilePath() : QString();
    QString pending;
    if (path.isEmpty() && !m_tabs.isEmpty()) {
        const int idx = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
        pending = m_tabs.at(idx).pendingTitle;
    }
    const QString name = !path.isEmpty() ? QFileInfo(path).fileName()
        : (!pending.isEmpty() ? pending : tr("Sin título"));
    const QString shortName = !path.isEmpty() ? QFileInfo(path).completeBaseName() : name;
    const bool unsaved = m_canvas && m_canvas->hasUnsavedChanges();
    // "•" marca cambios que aún no están en ningún .finily (Ctrl+S).
    setWindowTitle(tr("%1%2 — Finily").arg(unsaved ? QStringLiteral("• ") : QString(), name));
    if (m_toolbar)
        m_toolbar->setDocumentTitle(shortName, unsaved);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Cerrar es seguro: guardamos cualquier pestaña con archivo .finily modificado y el autoguardado activo.
    saveAllDirtyTabsBeforeExit();
    QMainWindow::closeEvent(event);
    event->accept();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_container && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        layoutChildren();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::installShortcuts() {
    // Todos los atajos del editor viven en el contenedor del editor con
    // WidgetWithChildrenShortcut: así escribir en la búsqueda de la biblioteca
    // o en el diálogo de nuevo cuaderno NUNCA dispara atajos del lienzo.
    auto addShortcut = [this](const QKeySequence& sequence, const std::function<void()>& action) {
        auto* shortcut = new QShortcut(sequence, m_container);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, action] {
            if (m_toolbar && m_toolbar->hasModalOpen())
                return;
            action();
        });
    };

    addShortcut(QKeySequence("Ctrl+Z"), [this] {
        m_canvas->undo();
        m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("Ctrl+Shift+Z"), [this] {
        m_canvas->redo();
        m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("Ctrl+Y"), [this] {
        m_canvas->redo();
        m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("Ctrl+0"), [this] {
        m_canvas->resetViewport();
        m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("Tab"), [this] { m_toolbar->toggleCleanUi(); });
    auto* fullscreenSc = new QShortcut(QKeySequence("F11"), m_container);
    fullscreenSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(fullscreenSc, &QShortcut::activated, this, [this] {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });

    m_presetShortcuts.clear();
    for (int i = 0; i < 5; ++i) {
        auto* sc = new QShortcut(QKeySequence(m_toolbar->presetShortcut(i)), m_container);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, [this, i] {
            if (m_toolbar && m_toolbar->hasModalOpen())
                return;
            m_toolbar->selectPreset(i);
        });
        m_presetShortcuts.append(sc);
    }
    connect(m_toolbar, &FloatingToolbar::presetsChanged, this, [this] {
        for (int i = 0; i < m_presetShortcuts.size(); ++i)
            m_presetShortcuts[i]->setKey(QKeySequence(m_toolbar->presetShortcut(i)));
    });

    // 'P' activa Pluma/Lápiz al primer toque y abre la rueda radial al doble toque rápido.
    auto* penSc = new QShortcut(QKeySequence(QStringLiteral("P")), m_container);
    penSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(penSc, &QShortcut::activated, this, [this] {
        if (!m_canvas || (m_toolbar && m_toolbar->hasModalOpen()))
            return;
        if (m_canvas->isInlineTextEditing() || qobject_cast<QLineEdit*>(QApplication::focusWidget()))
            return;
        m_canvas->triggerPenOrRadialShortcut();
        if (m_toolbar)
            m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("B"), [this] { m_toolbar->selectTool(ToolType::Ballpoint); });
    addShortcut(QKeySequence("H"), [this] { m_toolbar->selectTool(ToolType::Highlighter); });
    addShortcut(QKeySequence("T"), [this] { m_toolbar->selectTool(ToolType::Text); });
    addShortcut(QKeySequence("K"), [this] { m_toolbar->selectTool(ToolType::Laser); });
    addShortcut(QKeySequence("E"), [this] { m_toolbar->selectTool(ToolType::Eraser); });
    addShortcut(QKeySequence("F"), [this] { m_toolbar->selectTool(ToolType::Fill); });
    addShortcut(QKeySequence("G"), [this] { m_toolbar->selectTool(ToolType::Fill); });
    addShortcut(QKeySequence("U"), [this] { m_toolbar->selectTool(ToolType::Select); });
    addShortcut(QKeySequence("V"), [this] { m_toolbar->selectTool(ToolType::Select); });

    // Figuras, polilínea y renombrar pestaña
    addShortcut(QKeySequence("S"), [this] { m_toolbar->selectTool(ToolType::Shape); });
    addShortcut(QKeySequence("L"), [this] { m_toolbar->selectTool(ToolType::Lasso); });
    addShortcut(QKeySequence("Shift+L"), [this] { m_toolbar->selectTool(ToolType::Polyline); });
    addShortcut(QKeySequence("F2"), [this] {
        if (m_toolbar)
            m_toolbar->startInlineRenameActiveTab();
    });
    addShortcut(QKeySequence("R"), [this] {
        m_canvas->setShapeKind(ShapeKind::Radical);
        m_toolbar->selectTool(ToolType::Shape);
    });
    auto finishPolyline = [this] {
        if (m_canvas->isDrawingPolyline()) {
            m_canvas->finishActivePolyline(false);
            m_toolbar->refreshFromCanvas();
        }
    };
    addShortcut(QKeySequence("Return"), finishPolyline);
    addShortcut(QKeySequence("Enter"), finishPolyline);
    auto* escSc = new QShortcut(QKeySequence("Escape"), m_container);
    escSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escSc, &QShortcut::activated, this, [this] {
        if (m_toolbar && m_toolbar->handleEscape())
            return;
        m_canvas->cancelInteraction();
    });
    // Lazo, portapapeles, línea auxiliar y PDF
    addShortcut(QKeySequence("A"), [this] { m_toolbar->selectTool(ToolType::Lasso); });
    addShortcut(QKeySequence("M"), [this] {
        m_canvas->setMathBeautifyEnabled(!m_canvas->mathBeautifyEnabled());
        m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("D"), [this] {
        if (m_canvas->linePattern() == LinePattern::Dimension)
            return;
        const LinePattern next =
            (m_canvas->linePattern() == LinePattern::Solid) ? LinePattern::Dashed : LinePattern::Solid;
        m_canvas->setLinePattern(next);
        m_toolbar->refreshFromCanvas();
        m_toolbar->showToast(next == LinePattern::Dashed ? QStringLiteral("Línea discontinua")
                                                         : QStringLiteral("Línea sólida"));
    });
    addShortcut(QKeySequence("Ctrl+V"), [this] {
        m_canvas->pasteFromClipboard();
        m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("Ctrl+D"), [this] {
        m_canvas->duplicateSelection();
        m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("Ctrl+G"), [this] { m_canvas->groupSelection(); });
    addShortcut(QKeySequence("Ctrl+Shift+G"), [this] { m_canvas->ungroupSelection(); });
    addShortcut(QKeySequence("Ctrl+O"), [this] { m_toolbar->importPdfDialog(); });
    addShortcut(QKeySequence("Ctrl+E"), [this] { m_toolbar->exportPdfDialog(); });
    addShortcut(QKeySequence("Ctrl+K"), [this] { m_toolbar->openStickersPopover(); });

    // Cuaderno .finily y pestañas (Ctrl+T / Ctrl+W / Ctrl+Tab)
    addShortcut(QKeySequence("Ctrl+N"), [this] { m_toolbar->newDocumentAction(); });
    addShortcut(QKeySequence("Ctrl+T"), [this] { openNewBlankTab(); });
    addShortcut(QKeySequence("Ctrl+W"), [this] {
        if (m_tabs.size() > 1)
            closeTab(m_activeTab);
    });
    addShortcut(QKeySequence("Ctrl+Tab"), [this] {
        if (m_tabs.size() > 1)
            switchToTab((m_activeTab + 1) % int(m_tabs.size()));
    });
    addShortcut(QKeySequence("Ctrl+Shift+Tab"), [this] {
        if (m_tabs.size() > 1)
            switchToTab((m_activeTab - 1 + int(m_tabs.size())) % int(m_tabs.size()));
    });
    addShortcut(QKeySequence("Ctrl+Shift+O"), [this] { m_toolbar->openNotebookDialog(); });
    addShortcut(QKeySequence("Ctrl+S"), [this] { m_toolbar->saveNotebookAction(); });
    addShortcut(QKeySequence("Ctrl+Shift+S"), [this] { m_toolbar->saveNotebookAsDialog(); });

    // Páginas del cuaderno
    addShortcut(QKeySequence("PgUp"), [this] {
        m_canvas->prevPage();
        m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("PgDown"), [this] {
        m_canvas->nextPage();
        m_toolbar->refreshFromCanvas();
    });
    auto syncPageNavShortcuts = [this](FloatingToolbar::PageNavDirection dir) {
        const bool vert = (dir == FloatingToolbar::PageNavDirection::Vertical);
        if (m_pagePrevAltShortcut) {
            m_pagePrevAltShortcut->setKey(
                QKeySequence(vert ? QStringLiteral("Alt+Up") : QStringLiteral("Alt+Left")));
        }
        if (m_pageNextAltShortcut) {
            m_pageNextAltShortcut->setKey(
                QKeySequence(vert ? QStringLiteral("Alt+Down") : QStringLiteral("Alt+Right")));
        }
    };
    m_pagePrevAltShortcut = new QShortcut(m_container);
    m_pagePrevAltShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(m_pagePrevAltShortcut, &QShortcut::activated, this, [this] {
        if (m_toolbar && m_toolbar->hasModalOpen())
            return;
        m_canvas->prevPage();
        m_toolbar->refreshFromCanvas();
    });
    m_pageNextAltShortcut = new QShortcut(m_container);
    m_pageNextAltShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(m_pageNextAltShortcut, &QShortcut::activated, this, [this] {
        if (m_toolbar && m_toolbar->hasModalOpen())
            return;
        m_canvas->nextPage();
        m_toolbar->refreshFromCanvas();
    });
    syncPageNavShortcuts(m_toolbar->pageNavDirection());
    connect(m_toolbar, &FloatingToolbar::pageNavDirectionChanged, this, syncPageNavShortcuts);
    addShortcut(QKeySequence("Ctrl+Shift+N"), [this] {
        m_canvas->addPage();
        m_toolbar->refreshFromCanvas();
    });

    auto deleteSelection = [this] {
        if (m_canvas->hasLassoSelection() || m_canvas->hasSelectedShape() ||
            m_canvas->hasSelectedImage() || m_canvas->hasSelectedText()) {
            m_canvas->deleteSelection();
            m_toolbar->refreshFromCanvas();
        }
    };
    addShortcut(QKeySequence("Delete"), deleteSelection);
    addShortcut(QKeySequence("Backspace"), deleteSelection);
}

void MainWindow::vectorizeSelectedImage() {
    if (!m_canvas)
        return;
    const auto img = m_canvas->selectedImage();
    if (!img) {
        if (m_toolbar)
            m_toolbar->showToast(tr("Selecciona primero una imagen en el lienzo"));
        return;
    }
    const QRectF targetRect = img->rect.translated(img->rect.width() + 28.0, 0.0);
    VectorizeOptions opts;
    opts.inkColor = m_canvas->inkColor();
    opts.strokeSize = m_canvas->strokeSize();
    const VectorizeResult res = vectorizeGeometryFromImage(img->image, targetRect, opts);
    if (res.shapes.isEmpty()) {
        if (m_toolbar)
            m_toolbar->showToast(tr("No se detectaron figuras geométricas claras en la imagen"));
        return;
    }
    m_canvas->insertExtractedContent(res.shapes, {}, true);
    if (m_toolbar) {
        m_toolbar->refreshFromCanvas();
        m_toolbar->showToast(tr("Vectorizado: %1 figuras (%2 ms) ✓")
                                 .arg(res.shapes.size())
                                 .arg(res.elapsedMs));
    }
}

void MainWindow::startImageExtraction() {
    if (!m_canvas || !m_visionExtractor)
        return;
    const auto img = m_canvas->selectedImage();
    if (!img) {
        if (m_toolbar)
            m_toolbar->showToast(tr("Selecciona primero una imagen en el lienzo"));
        return;
    }
    if (VisionExtractor::apiKey().isEmpty()) {
        if (m_toolbar) {
            m_toolbar->openSettingsModal(QStringLiteral("ia"));
            m_toolbar->showToast(tr("Configura tu clave API en Ajustes › IA"));
        }
        return;
    }
    if (m_tabs.isEmpty())
        syncActiveTabFromCanvas();
    m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
    m_visionTargetTabId = m_tabs.at(m_activeTab).id;
    m_visionTargetPageIndex = m_canvas->currentPageIndex();

    const QRectF targetRect = img->rect.translated(img->rect.width() + 28.0, 0.0);
    if (m_toolbar)
        m_toolbar->showToast(tr("Analizando imagen con %1…").arg(VisionExtractor::model()));
    m_visionExtractor->extractFromImage(img->image, targetRect, m_canvas->inkColor(),
                                        m_canvas->strokeSize());
}

void MainWindow::syncActiveTabFromCanvas() {
    if (!m_canvas)
        return;
    NotebookSession exported = m_canvas->exportSession();
    if (m_tabs.isEmpty()) {
        exported.id = freshId();
        m_tabs.append(std::move(exported));
        m_activeTab = 0;
    } else {
        m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
        const QString existingId =
            m_tabs[m_activeTab].id.isEmpty() ? freshId() : m_tabs[m_activeTab].id;
        const bool wasEphemeral = m_tabs[m_activeTab].ephemeralIfEmpty;
        const QString prevPending = m_tabs[m_activeTab].pendingTitle;
        exported.id = existingId;
        if (exported.filePath.isEmpty()) {
            exported.ephemeralIfEmpty = wasEphemeral;
            exported.pendingTitle = prevPending;
        }
        m_tabs[m_activeTab] = std::move(exported);
    }
    syncTabBarUi();
}

void MainWindow::syncTabBarUi() {
    if (!m_toolbar || !m_canvas)
        return;
    if (m_tabs.isEmpty()) {
        syncActiveTabFromCanvas();
        return;
    }
    m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
    m_tabs[m_activeTab].filePath = m_canvas->currentFilePath();
    m_tabs[m_activeTab].dirty = m_canvas->hasUnsavedChanges();
    m_tabs[m_activeTab].themeId = m_canvas->themeId();
    m_tabs[m_activeTab].customBackground = m_canvas->customBackground();
    m_tabs[m_activeTab].tpl = m_canvas->defaultBackgroundTemplate();
    m_tabs[m_activeTab].pageFormat = m_canvas->pageFormat();
    m_tabs[m_activeTab].templateScale = m_canvas->templateScale();
    m_tabs[m_activeTab].cover = m_canvas->coverStyle();

    QVector<FloatingToolbar::TabEntry> entries;
    entries.reserve(m_tabs.size());
    QString sig = QString::number(m_activeTab);
    for (int i = 0; i < m_tabs.size(); ++i) {
        const NotebookSession& s = m_tabs.at(i);
        const QString title = !s.filePath.isEmpty()
            ? QFileInfo(s.filePath).completeBaseName()
            : (!s.pendingTitle.isEmpty() ? s.pendingTitle : tr("Sin título"));
        const bool dirty = (i == m_activeTab) ? m_canvas->hasUnsavedChanges() : s.dirty;
        const PageFormat fmt = (i == m_activeTab) ? m_canvas->pageFormat() : s.pageFormat;
        const bool isBoard = (fmt == PageFormat::Infinite);
        const bool isPdf = (i == m_activeTab)
            ? (m_canvas->documentPdfPageCount() > 0)
            : std::any_of(s.pages.cbegin(), s.pages.cend(), [](const PageData& page) {
                  return std::any_of(page.images.cbegin(), page.images.cend(),
                                     [](const ImageItem& img) { return img.isPdfPage; });
              });
        const QString tabPath = (i == m_activeTab) ? m_canvas->currentFilePath() : s.filePath;
        entries.append({title, dirty, i == m_activeTab, isBoard, isPdf, tabPath});
        sig += QStringLiteral("|%1:%2:%3:%4:%5")
                   .arg(title)
                   .arg(dirty ? 1 : 0)
                   .arg(isBoard ? 1 : 0)
                   .arg(isPdf ? 1 : 0)
                   .arg(tabPath);
    }
    if (sig != m_lastTabBarSig) {
        m_lastTabBarSig = sig;
        m_toolbar->setNotebookTabs(entries);
    }
}

void MainWindow::duplicateTab(int index) {
    if (!m_canvas || index < 0 || index >= m_tabs.size())
        return;
    materializeEphemeralTabIfNeeded();
    syncActiveTabFromCanvas();
    const NotebookSession src = m_tabs.at(index);
    if (!src.filePath.isEmpty() && QFileInfo::exists(src.filePath)) {
        if (index == m_activeTab && m_canvas->hasUnsavedChanges())
            m_canvas->saveToFile(src.filePath);
        QString dupPath;
        QString err;
        if (LibraryStore::duplicateNotebook(src.filePath, &dupPath, &err) && !dupPath.isEmpty()) {
            openNotebookInTab(dupPath, true);
            if (m_toolbar)
                m_toolbar->showToast(tr("Pestaña duplicada ✓"));
            return;
        }
        if (m_toolbar && !err.isEmpty())
            m_toolbar->showToast(err);
        return;
    }
    NotebookSession copy = src;
    copy.id = freshId();
    copy.filePath.clear();
    const QString baseTitle = !src.pendingTitle.isEmpty() ? src.pendingTitle : tr("Sin título");
    copy.pendingTitle = baseTitle + tr(" (copia)");
    copy.dirty = true;
    copy.ephemeralIfEmpty = false;
    m_tabs.append(std::move(copy));
    switchToTab(int(m_tabs.size()) - 1);
    if (m_toolbar)
        m_toolbar->showToast(tr("Pestaña duplicada ✓"));
}

void MainWindow::revealTabInLibrary(int index) {
    if (!m_canvas || !m_homeScreen || index < 0 || index >= m_tabs.size())
        return;
    materializeEphemeralTabIfNeeded();
    if (index == m_activeTab && !m_canvas->currentFilePath().isEmpty() && m_canvas->hasUnsavedChanges())
        m_canvas->saveToFile(m_canvas->currentFilePath());
    const QString path = (index == m_activeTab) ? m_canvas->currentFilePath() : m_tabs.at(index).filePath;
    if (path.isEmpty()) {
        if (m_toolbar)
            m_toolbar->showToast(tr("Guarda primero el documento para verlo en la biblioteca"));
        return;
    }
    m_homeScreen->revealNotebookPath(path);
    showHomePage();
}

void MainWindow::exportCurrentPageImage() {
    if (!m_canvas || !m_toolbar)
        return;
    QString baseName = QFileInfo(m_canvas->currentFilePath()).completeBaseName();
    if (baseName.isEmpty() && !m_tabs.isEmpty())
        baseName = m_tabs.at(std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1)).pendingTitle;
    if (baseName.isEmpty())
        baseName = tr("Finily");
    const QString docsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = QDir(docsDir.isEmpty() ? QDir::homePath() : docsDir)
                                  .filePath(QStringLiteral("%1-pag-%2.png")
                                                .arg(baseName)
                                                .arg(m_canvas->currentPageIndex() + 1));
    const QString outPath = QFileDialog::getSaveFileName(
        this, tr("Exportar página como PNG"), suggested, tr("Imagen PNG (*.png)"));
    if (outPath.isEmpty())
        return;
    if (m_canvas->exportCurrentPageToPng(outPath))
        m_toolbar->showToast(tr("Página exportada como PNG ✓"));
    else
        m_toolbar->showToast(tr("No se pudo exportar la imagen PNG"));
}

void MainWindow::switchToTab(int index) {
    if (!m_canvas || index < 0 || index >= m_tabs.size() || index == m_activeTab)
        return;
    materializeEphemeralTabIfNeeded();
    if (isActiveTabEmptyEphemeral())
        m_canvas->cancelPendingAutosave();
    else
        m_canvas->flushAutosave();
    syncActiveTabFromCanvas();
    m_activeTab = index;
    m_lastTabBarSig.clear();
    m_canvas->importSession(m_tabs.at(m_activeTab));
    if (m_toolbar)
        m_toolbar->refreshFromCanvas();
    updateWindowTitle();
    syncTabBarUi();
}

void MainWindow::openNewBlankTab() {
    if (!m_canvas)
        return;
    materializeEphemeralTabIfNeeded();
    if (isActiveTabEmptyEphemeral())
        m_canvas->cancelPendingAutosave();
    else
        m_canvas->flushAutosave();
    syncActiveTabFromCanvas();
    NotebookSession fresh;
    fresh.id = freshId();
    fresh.pages = {PageData{}};
    fresh.themeId = m_canvas->themeId();
    fresh.customBackground = m_canvas->customBackground();
    fresh.tpl = m_canvas->defaultBackgroundTemplate();
    fresh.pageFormat = m_canvas->pageFormat();
    fresh.templateScale = m_canvas->templateScale();
    fresh.cover = m_canvas->coverStyle();
    m_tabs.append(std::move(fresh));
    m_activeTab = int(m_tabs.size()) - 1;
    m_lastTabBarSig.clear();
    m_canvas->newDocument();
    syncActiveTabFromCanvas();
    if (m_toolbar) {
        m_toolbar->refreshFromCanvas();
        m_toolbar->showToast(tr("Nueva pestaña ✓"));
    }
    updateWindowTitle();
}

void MainWindow::closeTab(int index) {
    if (!m_canvas || !m_toolbar || m_tabs.size() <= 1 || index < 0 || index >= m_tabs.size())
        return;
    if (index != m_activeTab)
        switchToTab(index);
    materializeEphemeralTabIfNeeded();
    if (isActiveTabEmptyEphemeral()) {
        m_canvas->cancelPendingAutosave();
    } else if (!m_canvas->currentFilePath().isEmpty() && m_canvas->hasUnsavedChanges()) {
        if (!m_canvas->saveToFile(m_canvas->currentFilePath()))
            return;
    } else if (!m_toolbar->confirmDiscardChanges(tr("cerrar esta pestaña"))) {
        return;
    }
    m_tabs.removeAt(m_activeTab);
    if (m_tabs.isEmpty())
        return;
    m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
    m_lastTabBarSig.clear();
    m_canvas->importSession(m_tabs.at(m_activeTab));
    m_toolbar->refreshFromCanvas();
    updateWindowTitle();
    syncTabBarUi();
}

bool MainWindow::openNotebookInTab(const QString& path, bool forceNewTab) {
    if (!m_canvas || path.isEmpty())
        return false;
    const QString targetKey = normalizedFilePath(path);
    if (!targetKey.isEmpty()) {
        for (int i = 0; i < m_tabs.size(); ++i) {
            const QString tabPath = (i == m_activeTab) ? m_canvas->currentFilePath()
                                                       : m_tabs.at(i).filePath;
            if (!tabPath.isEmpty() && normalizedFilePath(tabPath) == targetKey) {
                if (i != m_activeTab)
                    switchToTab(i);
                showEditorPage();
                if (m_toolbar)
                    m_toolbar->refreshFromCanvas();
                m_canvas->setFocus();
                return true;
            }
        }
    }

    const bool openNew = forceNewTab || readAppSetting(QStringLiteral("home/openInNewTab"), true).toBool();
    const bool currentTabDirtyUntitled = m_canvas->currentFilePath().isEmpty()
        && !isActiveTabEmptyEphemeral() && m_canvas->hasAnyContent();
    if (!openNew && !m_tabs.isEmpty() && !currentTabDirtyUntitled) {
        if (!m_canvas->currentFilePath().isEmpty() && m_canvas->hasUnsavedChanges())
            m_canvas->saveToFile(m_canvas->currentFilePath());
        if (isActiveTabEmptyEphemeral())
            m_canvas->cancelPendingAutosave();
        else
            m_canvas->flushAutosave();
        if (!m_canvas->loadFromFile(path))
            return false;
        m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
        m_tabs[m_activeTab].ephemeralIfEmpty = false;
        m_tabs[m_activeTab].pendingTitle.clear();
        m_lastTabBarSig.clear();
        syncActiveTabFromCanvas();
    } else {
        if (!openInReusableOrNewTab([this, &path] { return m_canvas->loadFromFile(path); }))
            return false;
    }
    showEditorPage();
    if (m_toolbar)
        m_toolbar->refreshFromCanvas();
    m_canvas->setFocus();
    return true;
}

} // namespace finily
