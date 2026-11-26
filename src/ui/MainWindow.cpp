#include "ui/MainWindow.h"

#include "engine/DocumentSerializer.h"
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
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include <functional>

#ifdef Q_OS_LINUX
#include <QSocketNotifier>

#include <cerrno>
#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace finily {

namespace {

bool isFlatpakSandbox() {
#ifdef Q_OS_LINUX
    return QFileInfo::exists(QStringLiteral("/.flatpak-info"))
        || qEnvironmentVariableIsSet("FLATPAK_ID");
#else
    return false;
#endif
}

#ifdef Q_OS_LINUX
bool isWacomPadName(const QString& name) {
    return name.contains(QLatin1String("Wacom"), Qt::CaseInsensitive)
        && name.contains(QLatin1String("Pad"), Qt::CaseInsensitive);
}
#endif

} // namespace

MainWindow::MainWindow() {
    setWindowTitle(tr("Finily"));
    resize(1360, 840);
    setMinimumSize(960, 640);

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

    // Título de ventana sincronizado con el cuaderno (.finily) actual.
    connect(m_canvas, &CanvasWidget::stateChanged, this, &MainWindow::updateWindowTitle);
    updateWindowTitle();

    // --- Conexiones HomeScreen -> editor / biblioteca ---
    connect(m_homeScreen, &HomeScreen::createNotebookRequested, this,
            [this](const QString& name, ThemeId theme, BackgroundTemplate tpl, PageFormat format) {
                if (!m_canvas)
                    return;
                for (const QString& directory : HomeScreen::libraryDirectories()) {
                    if (!QDir().mkpath(directory))
                        continue;
                    const QString path = HomeScreen::uniqueNotebookPath(directory, name);
                    if (m_canvas->createNotebook(path, theme, tpl, format)) {
                        showEditorPage();
                        m_canvas->resetViewport();
                        m_toolbar->refreshFromCanvas();
                        m_canvas->setFocus();
                        return;
                    }
                }
                QMessageBox::warning(this, tr("Crear cuaderno"),
                                     tr("No se pudo guardar el cuaderno nuevo. Revisa el nombre, "
                                        "el espacio libre y los permisos de la carpeta."));
            });
    connect(m_homeScreen, &HomeScreen::openNotebookRequested, this, [this](const QString& path) {
        if (!m_canvas)
            return;
        showEditorPage();
        if (!m_canvas->loadFromFile(path)) {
            showHomePage();
            QMessageBox::warning(this, tr("Abrir cuaderno"), tr("No se pudo abrir el cuaderno."));
            return;
        }
        m_toolbar->refreshFromCanvas();
        m_canvas->setFocus();
    });
    connect(m_homeScreen, &HomeScreen::importPdfFromHomeRequested, this, [this] {
        if (!m_canvas)
            return;
        const QString pdfPath = QFileDialog::getOpenFileName(this, QStringLiteral("Importar PDF"),
                                                             QString(),
                                                             QStringLiteral("Archivos PDF (*.pdf)"));
        if (pdfPath.isEmpty())
            return;
        if (!m_canvas->importPdfFile(pdfPath, true)) {
            QMessageBox::warning(this, tr("Importar PDF"), tr("No se pudo importar el PDF."));
            return;
        }
        showEditorPage();
        m_canvas->resetViewport();
        m_canvas->setFocus();
    });
    connect(m_homeScreen, &HomeScreen::openExternalFileRequested, this, [this] {
        if (!m_canvas)
            return;
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("Abrir cuaderno"), QString(),
                                         QStringLiteral("Cuaderno Finily (*.finily)"));
        if (path.isEmpty())
            return;
        if (!m_canvas->loadFromFile(path)) {
            QMessageBox::warning(this, tr("Abrir cuaderno"), tr("No se pudo abrir el cuaderno."));
            return;
        }
        showEditorPage();
        m_toolbar->refreshFromCanvas();
        m_canvas->setFocus();
    });
    connect(m_homeScreen, &HomeScreen::continueDraftRequested, this, [this] {
        if (!m_canvas)
            return;
        m_canvas->loadAutosave();
        showEditorPage();
        m_toolbar->refreshFromCanvas();
        m_canvas->setFocus();
    });

    // Botón 'Inicio' de la pill: guardar y volver a la biblioteca.
    connect(m_toolbar, &FloatingToolbar::homeRequested, this, [this] {
        if (!m_canvas)
            return;
        if (!m_canvas->currentFilePath().isEmpty()) {
            if (!m_canvas->saveToFile(m_canvas->currentFilePath()))
                return;
        } else {
            m_canvas->saveAutosave();   // programa el autoguardado
            m_canvas->flushAutosave();  // y lo escribe ya antes de salir
        }
        m_homeScreen->refreshLibrary();
        showHomePage();
    });
    connect(m_toolbar, &FloatingToolbar::renameRequested, this, [this](const QString& rawName) {
        if (!m_canvas)
            return;
        const QString clean = HomeScreen::notebookBaseName(rawName);
        if (clean.isEmpty())
            return;
        const QString oldPath = m_canvas->currentFilePath();
        const QStringList directories = oldPath.isEmpty() ? HomeScreen::libraryDirectories()
            : QStringList{QFileInfo(oldPath).absolutePath()};
        for (const QString& directory : directories) {
            const QString target = QDir(directory).filePath(clean + QStringLiteral(".finily"));
            if (target == oldPath)
                return;
            if (QFile::exists(target)) {
                m_toolbar->showToast(tr("Ya existe un cuaderno con ese nombre"));
                updateWindowTitle();
                return;
            }
            if (oldPath.isEmpty()) {
                if (!QDir().mkpath(directory) || !m_canvas->saveToFile(target, true))
                    continue;
            } else {
                if (!m_canvas->saveToFile(oldPath) || !QFile::rename(oldPath, target))
                    break;
                moveCoverThumbnail(oldPath, target);
                m_canvas->setCurrentFilePath(target);
                m_canvas->saveAutosave();
            }
            updateWindowTitle();
            m_toolbar->showToast(tr("Cuaderno %1").arg(clean));
            return;
        }
        m_toolbar->showToast(oldPath.isEmpty() ? tr("No se pudo guardar el cuaderno")
                                              : tr("No se pudo renombrar el cuaderno"));
        updateWindowTitle();
    });

    installShortcuts();
#ifdef Q_OS_LINUX
    setupWacomPadListener();
#endif

    // --- Comprobador / actualizador integrado ---
    m_updater = new UpdateManager(this);
#ifdef Q_OS_WIN
    constexpr bool kCanInstallInApp = true;
#else
    constexpr bool kCanInstallInApp = false;
#endif

    connect(m_toolbar, &FloatingToolbar::checkForUpdatesRequested, this, [this] {
        if (!m_updater)
            return;
        if (m_toolbar)
            m_toolbar->showToast(tr("Buscando actualizaciones…"));
        m_updater->checkForUpdates(true);
    });
    connect(m_updater, &UpdateManager::updateAvailable, this,
            [this, kCanInstallInApp](const UpdateInfo& info, bool manual) {
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
                    if (canDirect) {
                        box.setText(tr("Hay una nueva versión disponible: Finily v%1 (tienes v%2).")
                                        .arg(info.version, QApplication::applicationVersion()));
                    } else if (isFlatpakSandbox()) {
                        box.setText(tr("Hay una nueva versión disponible: Finily v%1 (tienes v%2).\n\n"
                                       "En Flatpak las actualizaciones se realizan descargando el nuevo paquete "
                                       ".flatpak desde Releases o desde tu gestor Flatpak.")
                                        .arg(info.version, QApplication::applicationVersion()));
                    } else {
                        box.setText(tr("Hay una nueva versión disponible: Finily v%1 (tienes v%2).")
                                        .arg(info.version, QApplication::applicationVersion()));
                    }
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
    connect(m_homeScreen, &HomeScreen::updateInstallRequested, this, &MainWindow::startUpdateInstall);
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
        if (m_updateProgress) {
            m_updateProgress->close();
            m_updateProgress->deleteLater();
            m_updateProgress = nullptr;
        }
        if (m_homeScreen)
            m_homeScreen->resetUpdateBannerState();
    });
    connect(m_updater, &UpdateManager::downloadFailed, this, [this](const QString& error) {
        if (m_updateProgress) {
            m_updateProgress->close();
            m_updateProgress->deleteLater();
            m_updateProgress = nullptr;
        }
        if (m_homeScreen)
            m_homeScreen->resetUpdateBannerState();
        QMessageBox::warning(this, tr("Error al descargar actualización"), error);
    });
    connect(m_updater, &UpdateManager::installerReady, this,
            [this](const QString& installerPath, const UpdateInfo& info) {
                if (m_updateProgress) {
                    m_updateProgress->close();
                    m_updateProgress->deleteLater();
                    m_updateProgress = nullptr;
                }
                if (m_homeScreen)
                    m_homeScreen->resetUpdateBannerState();
                if (m_canvas) {
                    if (!m_canvas->currentFilePath().isEmpty() && m_canvas->hasUnsavedChanges())
                        m_canvas->saveToFile(m_canvas->currentFilePath());
                    m_canvas->flushAutosave();
                }
                if (QProcess::startDetached(installerPath, {})) {
                    QApplication::quit();
                } else {
                    QMessageBox::warning(this, tr("Actualizar Finily"),
                                         tr("No se pudo iniciar el instalador descargado en:\n%1\n"
                                            "Puedes abrirlo manualmente o visitar:\n%2")
                                             .arg(installerPath, info.releaseUrl));
                }
            });

    QTimer::singleShot(3000, this, [this] {
        if (m_updater)
            m_updater->checkForUpdates(false);
    });

    // La sesión anterior ya la restauró el constructor de CanvasWidget.
    m_toolbar->refreshFromCanvas();
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
            if (m_updateProgress)
                m_updateProgress->deleteLater();
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
        showEditorPage();
        m_canvas->resetViewport();
        m_canvas->setFocus();
    } else if (suffix == QLatin1String("finily")) {
        if (!m_canvas->loadFromFile(info.absoluteFilePath())) {
            QMessageBox::warning(this, tr("Abrir cuaderno"), tr("No se pudo abrir el cuaderno."));
            return;
        }
        showEditorPage();
        m_toolbar->refreshFromCanvas();
        m_canvas->setFocus();
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
    // Auto-detecta el dispositivo /dev/input/js* de "Wacom ... Pad" (plug-and-play sin root).
    QDir sysInput(QStringLiteral("/sys/class/input"));
    const QStringList entries = sysInput.entryList({QStringLiteral("js*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    QString padDevicePath;
    for (const QString& entry : entries) {
        QFile nameFile(QStringLiteral("/sys/class/input/%1/device/name").arg(entry));
        if (nameFile.open(QIODevice::ReadOnly)) {
            const QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
            if (isWacomPadName(name)) {
                padDevicePath = QStringLiteral("/dev/input/%1").arg(entry);
                break;
            }
        }
    }
    if (!padDevicePath.isEmpty()) {
        m_padFd = ::open(padDevicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    } else {
        // Respaldo dentro de sandbox (p. ej. Flatpak con --device=input) si /sys/class/input no está poblado.
        QDir devInput(QStringLiteral("/dev/input"));
        const QStringList jsNodes = devInput.entryList({QStringLiteral("js*")}, QDir::System | QDir::Readable);
        for (const QString& node : jsNodes) {
            const QString candidate = devInput.filePath(node);
            const int fd = ::open(candidate.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0)
                continue;
            char nameBuf[256] = {};
            if (::ioctl(fd, JSIOCGNAME(sizeof(nameBuf)), nameBuf) >= 0) {
                const QString name = QString::fromUtf8(nameBuf).trimmed();
                if (isWacomPadName(name)) {
                    m_padFd = fd;
                    break;
                }
            }
            ::close(fd);
        }
    }
    if (m_padFd < 0)
        return;

    m_padNotifier = new QSocketNotifier(m_padFd, QSocketNotifier::Read, this);
    connect(m_padNotifier, &QSocketNotifier::activated, this, &MainWindow::onWacomPadReadable);
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
            if (m_padNotifier) {
                m_padNotifier->setEnabled(false);
                m_padNotifier->deleteLater();
                m_padNotifier = nullptr;
            }
            ::close(m_padFd);
            m_padFd = -1;
            return;
        }
        if (n == 0) {
            if (m_padNotifier) {
                m_padNotifier->setEnabled(false);
                m_padNotifier->deleteLater();
                m_padNotifier = nullptr;
            }
            ::close(m_padFd);
            m_padFd = -1;
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
    const QString name = path.isEmpty() ? tr("Sin título") : QFileInfo(path).fileName();
    const bool unsaved = m_canvas && m_canvas->hasUnsavedChanges();
    // "•" marca cambios que aún no están en ningún .finily (Ctrl+S).
    setWindowTitle(tr("%1%2 — Finily").arg(unsaved ? QStringLiteral("• ") : QString(), name));
    if (m_toolbar)
        m_toolbar->setDocumentTitle(path.isEmpty() ? name : QFileInfo(path).completeBaseName(), unsaved);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Cerrar es seguro: la sesión entera se restaura al volver a abrir.
    if (m_canvas)
        m_canvas->flushAutosave();
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

    addShortcut(QKeySequence("1"), [this] { m_toolbar->selectPreset(0); });
    addShortcut(QKeySequence("2"), [this] { m_toolbar->selectPreset(1); });
    addShortcut(QKeySequence("3"), [this] { m_toolbar->selectPreset(2); });
    addShortcut(QKeySequence("4"), [this] { m_toolbar->selectPreset(3); });
    addShortcut(QKeySequence("5"), [this] { m_toolbar->selectPreset(4); });

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

    // Cuaderno .finily
    addShortcut(QKeySequence("Ctrl+N"), [this] { m_toolbar->newDocumentAction(); });
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

} // namespace finily
