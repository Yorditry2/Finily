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
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
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

void closeProgressDialog(QPointer<QProgressDialog>& dialog) {
    if (QProgressDialog* dlg = dialog) {
        dialog.clear();
        dlg->disconnect();
        dlg->close();
        dlg->deleteLater();
    }
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
#ifdef Q_OS_LINUX
    setupWacomPadListener();
#endif
    setupVisionCallbacks();
    setupUpdateCallbacks();

    // La sesión anterior ya la restauró el constructor de CanvasWidget.
    m_toolbar->refreshFromCanvas();
    syncActiveTabFromCanvas();
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
        if (!openNotebookInTab(path))
            QMessageBox::warning(this, tr("Abrir cuaderno"), tr("No se pudo abrir el cuaderno."));
    });

    connect(m_homeScreen, &HomeScreen::importPdfFromHomeRequested, this,
            [this](const QString& folderRelativePath) {
                if (!m_canvas)
                    return;
                const QString pdfPath = QFileDialog::getOpenFileName(
                    this, QStringLiteral("Importar PDF"), QString(),
                    QStringLiteral("Archivos PDF (*.pdf)"));
                if (pdfPath.isEmpty())
                    return;
                if (!openInReusableOrNewTab(
                        [this, &pdfPath] { return m_canvas->importPdfFile(pdfPath, true); })) {
                    QMessageBox::warning(this, tr("Importar PDF"), tr("No se pudo importar el PDF."));
                    return;
                }
                const QString base = LibraryStore::sanitizeBaseName(QFileInfo(pdfPath).completeBaseName());
                const QString safeBase = base.isEmpty() ? tr("PDF importado") : base;
                QStringList targetDirs;
                if (!folderRelativePath.isEmpty()) {
                    const QString resolved = LibraryStore::resolveDirectory(folderRelativePath);
                    if (!resolved.isEmpty())
                        targetDirs.append(resolved);
                }
                targetDirs.append(LibraryStore::libraryDirectories());
                for (const QString& directory : targetDirs) {
                    if (!QDir().mkpath(directory))
                        continue;
                    const QString nbPath = LibraryStore::uniqueNotebookPath(directory, safeBase);
                    if (m_canvas->saveToFile(nbPath, /*newFile=*/true, /*silent=*/true)) {
                        syncActiveTabFromCanvas();
                        break;
                    }
                }
                showEditorPage();
                m_canvas->resetViewport();
                if (m_toolbar)
                    m_toolbar->refreshFromCanvas();
                m_canvas->setFocus();
            });

    connect(m_homeScreen, &HomeScreen::openExternalFileRequested, this, [this] {
        if (!m_canvas)
            return;
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Abrir cuaderno"), QString(), QStringLiteral("Cuaderno Finily (*.finily)"));
        if (path.isEmpty())
            return;
        if (!openNotebookInTab(path))
            QMessageBox::warning(this, tr("Abrir cuaderno"), tr("No se pudo abrir el cuaderno."));
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
    connect(m_toolbar, &FloatingToolbar::switchTabRequested, this, &MainWindow::switchToTab);
    connect(m_toolbar, &FloatingToolbar::closeTabRequested, this, &MainWindow::closeTab);

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
        connect(m_toolbar, &FloatingToolbar::configureAiApiRequested, this,
                &MainWindow::configureAiApiDialog);
        connect(m_toolbar, &FloatingToolbar::extractSelectedImageRequested, this,
                &MainWindow::startImageExtraction);
    }

    connect(m_visionExtractor, &VisionExtractor::extractionFinished, this,
            [this](const ExtractedContent& content) {
                closeProgressDialog(m_visionProgress);
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
        closeProgressDialog(m_visionProgress);
        m_visionTargetTabId.clear();
        m_visionTargetPageIndex = -1;
        QMessageBox::warning(this, tr("Extraer figuras y texto (IA)"), error);
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
                const bool canDirect =
                    kCanInstallInApp && !info.installerUrl.isEmpty() && !info.installerSha256.isEmpty();
                if (m_toolbar)
                    m_toolbar->setUpdateAvailable(info.version);
                if (m_homeScreen)
                    m_homeScreen->showUpdateBanner(info.version, canDirect);
                if (manual) {
                    QMessageBox box(this);
                    box.setWindowTitle(tr("Actualización disponible"));
                    box.setIcon(QMessageBox::Information);
                    box.setText(tr("Hay una nueva versión disponible: Finily v%1 (tienes v%2).")
                                    .arg(info.version, QApplication::applicationVersion()));
                    QPushButton* primaryBtn = box.addButton(
                        canDirect ? tr("Actualizar ahora") : tr("Abrir página de descargas"),
                        QMessageBox::AcceptRole);
                    QPushButton* notesBtn = box.addButton(tr("Ver cambios"), QMessageBox::ActionRole);
                    box.addButton(tr("Más tarde"), QMessageBox::RejectRole);
                    box.exec();
                    if (box.clickedButton() == primaryBtn)
                        startUpdateInstall();
                    else if (box.clickedButton() == notesBtn)
                        QDesktopServices::openUrl(QUrl(info.releaseUrl));
                }
            });

    connect(m_updater, &UpdateManager::upToDate, this, [this](bool manual) {
        if (!manual)
            return;
        const QString msg = tr("Finily v%1 está actualizado ✓").arg(QApplication::applicationVersion());
        if (m_stack && m_stack->currentWidget() == m_container && m_toolbar)
            m_toolbar->showToast(msg);
        else
            QMessageBox::information(this, tr("Buscar actualizaciones"), msg);
    });

    connect(m_updater, &UpdateManager::checkFailed, this, [this](const QString& error, bool manual) {
        if (!manual)
            return;
        const QString msg = tr("No se pudo comprobar actualizaciones: %1").arg(error);
        if (m_stack && m_stack->currentWidget() == m_container && m_toolbar)
            m_toolbar->showToast(tr("No se pudo comprobar actualizaciones"));
        else
            QMessageBox::warning(this, tr("Buscar actualizaciones"), msg);
    });

    if (m_homeScreen) {
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
    }

    connect(m_updater, &UpdateManager::downloadProgress, this, [this](qint64 received, qint64 total) {
        const int percent = total > 0 ? int((received * 100) / total) : 0;
        if (m_homeScreen)
            m_homeScreen->setUpdateDownloadProgress(percent);
        if (m_updateProgress) {
            if (total > 0) {
                m_updateProgress->setRange(0, 100);
                m_updateProgress->setValue(std::clamp(percent, 0, 100));
            } else {
                m_updateProgress->setRange(0, 0);
            }
        }
    });

    connect(m_updater, &UpdateManager::downloadCancelled, this, [this] {
        closeProgressDialog(m_updateProgress);
        if (m_homeScreen)
            m_homeScreen->resetUpdateBannerState();
    });

    connect(m_updater, &UpdateManager::downloadFailed, this, [this](const QString& error) {
        closeProgressDialog(m_updateProgress);
        if (m_homeScreen)
            m_homeScreen->resetUpdateBannerState();
        QMessageBox::warning(this, tr("Error al descargar actualización"), error);
    });

    connect(m_updater, &UpdateManager::installerReady, this,
            [this](const QString& installerPath, const UpdateInfo& info) {
                closeProgressDialog(m_updateProgress);
                if (m_homeScreen)
                    m_homeScreen->resetUpdateBannerState();
                saveAllDirtyTabsBeforeExit();
                if (QProcess::startDetached(installerPath, {})) {
                    QApplication::quit();
                } else {
                    QMessageBox::warning(
                        this, tr("Actualizar Finily"),
                        tr("No se pudo iniciar el instalador descargado en:\n%1\n"
                           "Puedes abrirlo manualmente o visitar:\n%2")
                            .arg(installerPath, info.releaseUrl));
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
                                                   tab.cover.colorIndex);
            if (serializeDocumentToFile(tab.filePath, tab.pages, cur, settings)) {
                tab.dirty = false;
                updateCoverThumbnailForPages(tab.filePath, tab.pages, tab.themeId, tab.tpl,
                                             tab.pageFormat, tab.templateScale);
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
                                             spec.templateScale, spec.cover);
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
                                            spec.templateScale, spec.cover);
        };
        if (openInReusableOrNewTab(loader)) {
            showEditorPage();
            m_canvas->resetViewport();
            if (m_toolbar)
                m_toolbar->refreshFromCanvas();
            m_canvas->setFocus();
            return;
        }
    }
    QMessageBox::warning(this, tr("Crear documento"),
                         tr("No se pudo guardar el documento nuevo. Revisa el nombre, "
                            "el espacio libre y los permisos de la carpeta."));
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
        } else {
            closeProgressDialog(m_updateProgress);
            m_updateProgress = new QProgressDialog(
                tr("Descargando Finily v%1…").arg(m_availableUpdate.version),
                tr("Cancelar"), 0, 100, this);
            m_updateProgress->setWindowTitle(tr("Actualizar Finily"));
            m_updateProgress->setWindowModality(Qt::WindowModal);
            m_updateProgress->setAutoClose(false);
            m_updateProgress->setAutoReset(false);
            m_updateProgress->setValue(0);
            connect(m_updateProgress, &QProgressDialog::canceled, this, [this] {
                if (m_updater)
                    m_updater->cancelDownload();
            });
            m_updateProgress->show();
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

void MainWindow::openInitialFile(const QString& path) {
    if (!m_canvas)
        return;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return; // sin archivo válido: arranca limpio en la pantalla de inicio
    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("pdf")) {
        if (!m_canvas->importPdfFile(info.absoluteFilePath(), true)) {
            QMessageBox::warning(this, tr("Importar PDF"), tr("No se pudo importar el PDF."));
            return;
        }
        syncActiveTabFromCanvas();
        updateWindowTitle();
        showEditorPage();
        m_canvas->resetViewport();
        m_canvas->setFocus();
    } else if (suffix == QLatin1String("finily")) {
        if (!openNotebookInTab(info.absoluteFilePath())) {
            QMessageBox::warning(this, tr("Abrir cuaderno"), tr("No se pudo abrir el cuaderno."));
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
        // Solo actuar cuando la ventana de Finily está enfocada
        if ((!isActiveWindow() && !(m_canvas && m_canvas->hasFocus())) || !m_canvas || !m_toolbar)
            continue;

        // 4 botones físicos del Pad Wacom Intuos BT S (izquierda -> derecha):
        // 0: Deshacer | 1: Rehacer | 2: Lazo (Seleccionar) | 3: Polilínea (Figuras continuas)
        switch (ev.number) {
            case 0:
                m_canvas->undo();
                m_toolbar->refreshFromCanvas();
                break;
            case 1:
                m_canvas->redo();
                m_toolbar->refreshFromCanvas();
                break;
            case 2:
                m_toolbar->selectTool(ToolType::Lasso);
                break;
            case 3:
                m_toolbar->selectTool(ToolType::Polyline);
                break;
            default:
                break;
        }
    }
}
#endif

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
        connect(shortcut, &QShortcut::activated, this, action);
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
    addShortcut(QKeySequence("F11"), [this] {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });

    m_presetShortcuts.clear();
    for (int i = 0; i < 5; ++i) {
        auto* sc = new QShortcut(QKeySequence(m_toolbar->presetShortcut(i)), m_container);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, [this, i] { m_toolbar->selectPreset(i); });
        m_presetShortcuts.append(sc);
    }
    connect(m_toolbar, &FloatingToolbar::presetsChanged, this, [this] {
        for (int i = 0; i < m_presetShortcuts.size(); ++i)
            m_presetShortcuts[i]->setKey(QKeySequence(m_toolbar->presetShortcut(i)));
    });

    // 'P' lo maneja CanvasWidget directamente (toque rápido = Lápiz, mantener = Menú radial).
    addShortcut(QKeySequence("B"), [this] { m_toolbar->selectTool(ToolType::Ballpoint); });
    addShortcut(QKeySequence("H"), [this] { m_toolbar->selectTool(ToolType::Highlighter); });
    addShortcut(QKeySequence("T"), [this] { m_toolbar->selectTool(ToolType::Text); });
    addShortcut(QKeySequence("K"), [this] { m_toolbar->selectTool(ToolType::Laser); });
    addShortcut(QKeySequence("E"), [this] { m_toolbar->selectTool(ToolType::Eraser); });
    addShortcut(QKeySequence("F"), [this] { m_toolbar->selectTool(ToolType::Fill); });
    addShortcut(QKeySequence("U"), [this] { m_toolbar->selectTool(ToolType::Select); });

    // Figuras y polilínea
    addShortcut(QKeySequence("S"), [this] { m_toolbar->selectTool(ToolType::Shape); });
    addShortcut(QKeySequence("L"), [this] { m_toolbar->selectTool(ToolType::Polyline); });
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
    addShortcut(QKeySequence("Escape"), [this] { m_canvas->cancelInteraction(); });
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
        m_canvas->prevPage();
        m_toolbar->refreshFromCanvas();
    });
    m_pageNextAltShortcut = new QShortcut(m_container);
    m_pageNextAltShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(m_pageNextAltShortcut, &QShortcut::activated, this, [this] {
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

bool MainWindow::configureAiApiDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Configurar IA de extracción (Gemini)"));
    dlg.setMinimumWidth(440);
    auto* form = new QFormLayout(&dlg);

    auto* info = new QLabel(
        tr("Introduce tu clave API personal de Google Gemini (gratis en "
           "<a href=\"https://aistudio.google.com/apikey\">aistudio.google.com/apikey</a>).<br>"
           "Tu clave se guarda únicamente de forma local en este equipo."),
        &dlg);
    info->setWordWrap(true);
    info->setOpenExternalLinks(true);
    form->addRow(info);

    auto* keyEdit = new QLineEdit(VisionExtractor::apiKey(), &dlg);
    keyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    keyEdit->setPlaceholderText(QStringLiteral("AIza..."));
    form->addRow(tr("Clave API:"), keyEdit);

    auto* modelBox = new QComboBox(&dlg);
    modelBox->setEditable(true);
    modelBox->addItems(VisionExtractor::supportedModels());
    modelBox->setCurrentText(VisionExtractor::model());
    form->addRow(tr("Modelo:"), modelBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Guardar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    const QString key = keyEdit->text().trimmed();
    VisionExtractor::setApiKey(key);
    VisionExtractor::setModel(modelBox->currentText().trimmed());
    if (m_toolbar)
        m_toolbar->showToast(key.isEmpty() ? tr("Clave API eliminada") : tr("IA configurada ✓"));
    return !key.isEmpty();
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
        if (!configureAiApiDialog())
            return;
    }
    if (m_tabs.isEmpty())
        syncActiveTabFromCanvas();
    m_activeTab = std::clamp(m_activeTab, 0, int(m_tabs.size()) - 1);
    m_visionTargetTabId = m_tabs.at(m_activeTab).id;
    m_visionTargetPageIndex = m_canvas->currentPageIndex();

    const QRectF targetRect = img->rect.translated(img->rect.width() + 28.0, 0.0);
    closeProgressDialog(m_visionProgress);
    m_visionProgress = new QProgressDialog(
        tr("Analizando imagen con %1…").arg(VisionExtractor::model()), tr("Cancelar"), 0, 0, this);
    m_visionProgress->setWindowTitle(tr("Extraer figuras y texto (IA)"));
    m_visionProgress->setWindowModality(Qt::NonModal);
    m_visionProgress->setMinimumDuration(0);
    connect(m_visionProgress, &QProgressDialog::canceled, this, [this] {
        closeProgressDialog(m_visionProgress);
        m_visionTargetTabId.clear();
        m_visionTargetPageIndex = -1;
        if (m_visionExtractor)
            m_visionExtractor->cancel();
    });
    m_visionProgress->show();
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
        entries.append({title, dirty, i == m_activeTab});
        sig += QStringLiteral("|%1:%2").arg(title).arg(dirty ? 1 : 0);
    }
    if (sig != m_lastTabBarSig) {
        m_lastTabBarSig = sig;
        m_toolbar->setNotebookTabs(entries);
    }
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

bool MainWindow::openNotebookInTab(const QString& path) {
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

    if (!openInReusableOrNewTab([this, &path] { return m_canvas->loadFromFile(path); }))
        return false;
    showEditorPage();
    if (m_toolbar)
        m_toolbar->refreshFromCanvas();
    m_canvas->setFocus();
    return true;
}

} // namespace finily
