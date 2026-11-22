#include "ui/MainWindow.h"

#include "ui/CanvasWidget.h"
#include "ui/FloatingToolbar.h"
#include "ui/HomeScreen.h"

#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QShortcut>
#include <QSocketNotifier>
#include <QStackedWidget>
#include <QWidget>

#include <fcntl.h>
#include <functional>
#include <linux/joystick.h>
#include <unistd.h>

namespace finily {

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
            [this](const QString& path, ThemeId theme, BackgroundTemplate tpl, PageFormat format) {
                if (!m_canvas)
                    return;
                m_canvas->newDocument();
                m_canvas->setThemeId(theme);
                m_canvas->setBackgroundTemplate(tpl);
                m_canvas->setPageFormat(format);
                m_canvas->saveToFile(path);
                showEditorPage();
                m_toolbar->refreshFromCanvas();
                m_canvas->setFocus();
            });
    connect(m_homeScreen, &HomeScreen::openNotebookRequested, this, [this](const QString& path) {
        if (!m_canvas)
            return;
        m_canvas->loadFromFile(path);
        showEditorPage();
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
        m_canvas->newDocument();
        m_canvas->importPdfFile(pdfPath);
        showEditorPage();
        m_toolbar->refreshFromCanvas();
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
        m_canvas->loadFromFile(path);
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
        if (!m_canvas->currentFilePath().isEmpty())
            m_canvas->saveToFile(m_canvas->currentFilePath());
        else {
            m_canvas->saveAutosave();   // programa el autoguardado
            m_canvas->flushAutosave();  // y lo escribe ya antes de salir
        }
        m_homeScreen->refreshLibrary();
        showHomePage();
    });

    installShortcuts();
    setupWacomPadListener();

    // La sesión anterior ya la restauró el constructor de CanvasWidget.
    m_toolbar->refreshFromCanvas();
}

MainWindow::~MainWindow() {
    if (m_padNotifier) {
        m_padNotifier->setEnabled(false);
        delete m_padNotifier;
        m_padNotifier = nullptr;
    }
    if (m_padFd >= 0) {
        ::close(m_padFd);
        m_padFd = -1;
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
        m_canvas->newDocument();
        m_canvas->importPdfFile(info.absoluteFilePath());
        showEditorPage();
        m_toolbar->refreshFromCanvas();
        m_canvas->setFocus();
    } else if (suffix == QLatin1String("finily")) {
        m_canvas->loadFromFile(info.absoluteFilePath());
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

void MainWindow::setupWacomPadListener() {
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
    if (padDevicePath.isEmpty())
        return;

    m_padFd = ::open(padDevicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_padFd < 0)
        return;

    m_padNotifier = new QSocketNotifier(m_padFd, QSocketNotifier::Read, this);
    connect(m_padNotifier, &QSocketNotifier::activated, this, &MainWindow::onWacomPadReadable);
}

void MainWindow::onWacomPadReadable() {
    if (m_padFd < 0)
        return;

    struct js_event ev {};
    while (::read(m_padFd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
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

    addShortcut(QKeySequence("1"), [this] { m_toolbar->selectPreset(0); });
    addShortcut(QKeySequence("2"), [this] { m_toolbar->selectPreset(1); });
    addShortcut(QKeySequence("3"), [this] { m_toolbar->selectPreset(2); });
    addShortcut(QKeySequence("4"), [this] { m_toolbar->selectPreset(3); });
    addShortcut(QKeySequence("5"), [this] { m_toolbar->selectPreset(4); });

    addShortcut(QKeySequence("P"), [this] { m_toolbar->selectTool(ToolType::Pen); });
    addShortcut(QKeySequence("B"), [this] { m_toolbar->selectTool(ToolType::Ballpoint); });
    addShortcut(QKeySequence("H"), [this] { m_toolbar->selectTool(ToolType::Highlighter); });
    addShortcut(QKeySequence("T"), [this] { m_toolbar->selectTool(ToolType::Text); });
    addShortcut(QKeySequence("K"), [this] { m_toolbar->selectTool(ToolType::Laser); });
    addShortcut(QKeySequence("E"), [this] { m_toolbar->selectTool(ToolType::Eraser); });

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
    addShortcut(QKeySequence("Escape"), [this] {
        if (m_canvas->isDrawingPolyline())
            m_canvas->cancelActivePolyline();
        else
            m_canvas->clearSelection();
        m_toolbar->refreshFromCanvas();
    });
    // Lazo, portapapeles, línea auxiliar y PDF
    addShortcut(QKeySequence("A"), [this] { m_toolbar->selectTool(ToolType::Lasso); });
    addShortcut(QKeySequence("M"), [this] {
        m_canvas->setMathBeautifyEnabled(!m_canvas->mathBeautifyEnabled());
        m_toolbar->refreshFromCanvas();
    });
    addShortcut(QKeySequence("D"), [this] {
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
    addShortcut(QKeySequence("Ctrl+O"), [this] { m_toolbar->importPdfDialog(); });
    addShortcut(QKeySequence("Ctrl+E"), [this] { m_toolbar->exportPdfDialog(); });

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
