#include "ui/HomeScreen.h"

#include "core/Utils.h"
#include "ui/DesignTokens.h"
#include "ui/home/CreateMenu.h"
#include "ui/home/FolderCards.h"
#include "ui/home/HomeHeader.h"
#include "ui/home/HomeMenu.h"
#include "ui/home/HomeMotion.h"
#include "ui/home/HomeSidebar.h"
#include "ui/home/HomeTheme.h"
#include "ui/home/LibraryViews.h"
#include "ui/home/MoveToSheet.h"
#include "ui/home/NewBoardSheet.h"
#include "ui/home/NewFolderSheet.h"
#include "ui/home/NewNotebookSheet.h"
#include "ui/home/NotebookCards.h"
#include "ui/home/SettingsView.h"
#include "ui/home/TrashCards.h"
#include "ui/home/UpdateBanner.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QParallelAnimationGroup>
#include <QPauseAnimation>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSequentialAnimationGroup>
#include <QShortcut>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace finily {

// Recuadro translúcido para seleccionar varios elementos arrastrando en espacio vacío.
class MarqueeSelectionOverlay : public QWidget {
public:
    explicit MarqueeSelectionOverlay(QWidget* parent)
        : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_StyledBackground, false);
        hide();
    }
    void setSelectionRect(const QRect& r, bool dark) {
        m_rect = r.normalized();
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (m_rect.width() < 2 || m_rect.height() < 2)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(m_dark);
        QColor fill = tk.accent;
        fill.setAlpha(24);
        QColor border = tk.accent;
        border.setAlpha(150);
        p.setPen(QPen(border, 1.2));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(m_rect).adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);
    }

private:
    QRect m_rect;
    bool m_dark = false;
};

namespace {

bool isSupportedDropFile(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("pdf") || ext == QLatin1String("finily")
        || ext == QLatin1String("png") || ext == QLatin1String("jpg")
        || ext == QLatin1String("jpeg") || ext == QLatin1String("webp");
}

QStringList droppedLocalFiles(const QMimeData* mime) {
    QStringList out;
    if (!mime || !mime->hasUrls())
        return out;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString p = url.toLocalFile();
        if (QFileInfo(p).isFile() && isSupportedDropFile(p))
            out.append(p);
    }
    return out;
}

bool systemPrefersDark() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    const QPalette pal = QGuiApplication::palette();
    return pal.color(QPalette::Window).lightness() < pal.color(QPalette::WindowText).lightness();
#endif
}

} // namespace

HomeScreen::HomeScreen(QWidget* parent)
    : QWidget(parent)
    , m_pdfCancelFlag(std::make_shared<std::atomic_bool>(false)) {
    setObjectName(QStringLiteral("finilyHome"));
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setAcceptDrops(true);

    m_filter.sort = LibraryFilter::loadSavedSort();

    {
        const int mode = SettingsView::themeMode();
        m_dark = mode == SettingsView::System
                     ? systemPrefersDark()
                     : mode == SettingsView::Dark;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (SettingsView::themeMode() == SettingsView::System)
            applyThemeMode(SettingsView::System);
    });
#endif
    // La papelera conserva N días configurados en Ajustes; lo vencido se borra al abrir.
    LibraryStore::purgeExpiredTrash();

    buildLayout();
    buildOverlays();
    installShortcuts();
    applyTheme();
    refreshLibrary();

#ifdef FINILY_DEV_SNAPSHOTS
    // Solo desarrollo: con FINILY_SNAPSHOT, FINILY_SNAPSHOT_FOLDER abre esa carpeta.
    const QString snapFolder = qEnvironmentVariable("FINILY_SNAPSHOT_FOLDER");
    if (!snapFolder.isEmpty())
        QTimer::singleShot(200, this, [this, snapFolder] { openFolder(snapFolder, false); });

    // Solo desarrollo: FINILY_SNAPSHOT_SHEET abre la vista/superficie solicitada.
    const QString snapSheet = qEnvironmentVariable("FINILY_SNAPSHOT_SHEET");
    if (!snapSheet.isEmpty()) {
        QTimer::singleShot(380, this, [this, snapSheet] {
            const HomePalette pal = homePaletteFor(m_dark);
            if (snapSheet == QLatin1String("menu")) openCreateMenu();
            else if (snapSheet == QLatin1String("board")) m_boardSheet->openCreate({}, pal, m_dark);
            else if (snapSheet == QLatin1String("board-color")) {
                m_boardSheet->openCreate({}, pal, m_dark);
                QTimer::singleShot(260, this, [this] {
                    if (m_boardSheet && m_boardSheet->isVisible())
                        m_boardSheet->openColorPopover();
                });
            } else if (snapSheet == QLatin1String("notebook")) m_notebookSheet->openCreate({}, pal, m_dark);
            else if (snapSheet == QLatin1String("notebook-land")) {
                m_notebookSheet->openCreate({}, pal, m_dark);
                QMetaObject::invokeMethod(m_notebookSheet, "setLandscapeOrientation", Q_ARG(bool, true));
            }
            else if (snapSheet == QLatin1String("folder")) m_folderSheet->openCreate({}, pal, m_dark);
            else if (snapSheet == QLatin1String("move") && !m_cards.isEmpty()) {
                openMoveSheetFor({m_cards.first()->filePath()}, {}, m_cards.first()->title(),
                                 m_cards.first()->meta().folderRelativePath);
            } else if (snapSheet == QLatin1String("ctxdoc") && !m_cards.isEmpty())
                showNotebookMenu(m_cards.first(), m_cards.first()->mapToGlobal(QPoint(120, 60)));
            else if (snapSheet == QLatin1String("ctxfolder")) {
                if (m_folderCards.isEmpty())
                    selectFilter(HomeSidebar::NavFolders);
                if (!m_folderCards.isEmpty()) {
                    showFolderMenu(m_folderCards.first()->meta(),
                                   m_folderCards.first()->mapToGlobal(QPoint(120, 60)), m_folderCards.first());
                }
            }
            else if (snapSheet == QLatin1String("ctxempty"))
                showEmptySpaceMenu(mapToGlobal(QPoint(320, 520)));
            else if (snapSheet == QLatin1String("multi") && m_cards.size() >= 2) {
                m_selectedKeys.insert(keyForDoc(m_cards[0]->filePath()));
                m_selectedKeys.insert(keyForDoc(m_cards[1]->filePath()));
                syncCardSelectionVisuals();
                showMultiSelectMenu(m_cards[1]->mapToGlobal(QPoint(120, 80)));
            } else if (snapSheet == QLatin1String("rename") && !m_cards.isEmpty()) m_cards.first()->beginRename();
            else if (snapSheet == QLatin1String("toast"))
                m_toast->showMessage(QStringLiteral("«Física» se movió a la papelera"),
                                     QStringLiteral("Deshacer"), [] {}, 6000,
                                     QStringLiteral("trash"), QStringLiteral("Ctrl Z"));
            else if (snapSheet == QLatin1String("trash")) selectFilter(HomeSidebar::NavTrash);
            else if (snapSheet == QLatin1String("favs")) selectFilter(HomeSidebar::NavFavs);
            else if (snapSheet == QLatin1String("folders")) selectFilter(HomeSidebar::NavFolders);
            else if (snapSheet == QLatin1String("tag") && !m_allTags.isEmpty()) openTag(m_allTags.first().id);
            else if (snapSheet == QLatin1String("collapsed")) setSidebarCollapsed(true, false);
            else if (snapSheet == QLatin1String("update-card"))
                showUpdateBanner(QStringLiteral("0.3.0"), false, QStringLiteral("Mejoras en el borrador y la tinta."));
            else if (snapSheet == QLatin1String("settings")) openSettings();
            else if (snapSheet == QLatin1String("settings-inicio")) { openSettings(); QTimer::singleShot(40, this, [this] { m_settings->showSection(1); }); }
            else if (snapSheet == QLatin1String("settings-lapiz")) { openSettings(); QTimer::singleShot(40, this, [this] { m_settings->showSection(2); }); }
            else if (snapSheet == QLatin1String("settings-storage")) { openSettings(); QTimer::singleShot(40, this, [this] { m_settings->showSection(3); }); }
            else if (snapSheet.startsWith(QLatin1String("search:"))) {
                m_header->openSearch();
                if (auto* le = m_header->findChild<QLineEdit*>())
                    le->setText(snapSheet.mid(7));
            } else if (snapSheet == QLatin1String("filter")) openFilterPanel();
            else if (snapSheet == QLatin1String("filtered")) {
                LibraryFilter f; f.kind = LibraryFilter::Notebooks; f.date = LibraryFilter::LastWeek;
                setLibraryFilter(f);
            } else if (snapSheet == QLatin1String("confirm")) {
                selectFilter(HomeSidebar::NavTrash);
                QTimer::singleShot(260, this, [this] { askEmptyTrash(); });
            } else if (snapSheet == QLatin1String("dropzone") && m_dropZone) {
                m_dropZone->setTargetName(QStringLiteral("Documentos"));
                m_dropZone->setDark(m_dark);
                m_dropZone->setGeometry(QRect(m_scroll->mapTo(this, QPoint(0, 0)), m_scroll->size()));
                m_dropZone->show();
                m_dropZone->raise();
            }
        });
    }
#endif
}

HomeScreen::~HomeScreen() {
    if (m_pdfCancelFlag)
        m_pdfCancelFlag->store(true);
}

void HomeScreen::buildLayout() {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_sidebar = new HomeSidebar(this);
    m_updateBanner = m_sidebar->updateBanner();
    connect(m_sidebar, &HomeSidebar::filterSelected, this, &HomeScreen::selectFilter);
    connect(m_sidebar, &HomeSidebar::folderSelected, this,
            [this](const QString& rel) { openFolder(rel, true); });
    connect(m_sidebar, &HomeSidebar::folderMenuRequested, this,
            [this](const FolderMeta& meta, const QPoint& pos) { showFolderMenu(meta, pos, nullptr); });
    connect(m_sidebar, &HomeSidebar::tagSelected, this, &HomeScreen::openTag);
    connect(m_sidebar, &HomeSidebar::tagMenuRequested, this, &HomeScreen::showTagMenu);
    connect(m_sidebar, &HomeSidebar::collapseRequested, this,
            [this] { setSidebarCollapsed(true, true); });
    connect(m_sidebar, &HomeSidebar::continueDraftRequested, this,
            &HomeScreen::continueDraftRequested);
    connect(m_sidebar, &HomeSidebar::darkModeToggled, this, [this](bool dark) {
        // La luna fija el tema a mano (deja de seguir al sistema).
        SettingsView::setThemeMode(dark ? SettingsView::Dark : SettingsView::Light);
        setDarkMode(dark, true);
        if (m_settings && m_settingsOpen)
            m_settings->reload();
    });
    connect(m_sidebar, &HomeSidebar::settingsRequested, this, &HomeScreen::openSettings);
    if (m_updateBanner) {
        connect(m_updateBanner, &UpdateBanner::installRequested, this, &HomeScreen::updateInstallRequested);
        connect(m_updateBanner, &UpdateBanner::cancelRequested, this, &HomeScreen::updateCancelRequested);
        connect(m_updateBanner, &UpdateBanner::releaseNotesRequested, this, &HomeScreen::updateReleaseNotesRequested);
        connect(m_updateBanner, &UpdateBanner::dismissed, this, &HomeScreen::updateDismissed);
        connect(m_updateBanner, &UpdateBanner::restartNowRequested, this, &HomeScreen::updateRestartRequested);
        connect(m_updateBanner, &UpdateBanner::laterRequested, this, &HomeScreen::hideUpdateBanner);
    }
    root->addWidget(m_sidebar);

    auto* mainPane = new QWidget(this);
    mainPane->setAttribute(Qt::WA_StyledBackground, false);
    auto* mainCol = new QVBoxLayout(mainPane);
    mainCol->setContentsMargins(0, 0, 0, 0);
    mainCol->setSpacing(0);

    m_header = new HomeHeader(mainPane);
    connect(m_header, &HomeHeader::searchTextChanged, this, &HomeScreen::onSearchTextChanged);
    connect(m_header, &HomeHeader::filterRequested, this, &HomeScreen::openFilterPanel);
    connect(m_header, &HomeHeader::searchSubmitted, this, &HomeScreen::openFirstResult);
    connect(m_header, &HomeHeader::searchScopeCleared, this, [this] {
        m_searchWhole = true;
        refreshLibrary();
    });
    connect(m_header, &HomeHeader::breadcrumbClicked, this, [this](const QString& rel) {
        if (rel.isEmpty())
            selectFilter(HomeSidebar::NavFolders);
        else
            openFolder(rel, true);
    });
    connect(m_header, &HomeHeader::newRequested, this, &HomeScreen::openCreateMenu);
    connect(m_header, &HomeHeader::reopenSidebarRequested, this,
            [this] { setSidebarCollapsed(false, true); });
    connect(m_header, &HomeHeader::emptyTrashRequested, this, &HomeScreen::askEmptyTrash);
    connect(m_header, &HomeHeader::settingsCloseRequested, this, [this] {
        if (m_sidebar)
            m_sidebar->setSelectedFilter(HomeSidebar::NavDocs);
        selectFilter(HomeSidebar::NavDocs);
    });
    mainCol->addWidget(m_header);

    auto* body = new QVBoxLayout();
    body->setContentsMargins(32, 0, 18, 0);
    body->setSpacing(0);
    mainCol->addLayout(body, 1);

    m_chipsBar = new FilterChipsBar(mainPane);
    connect(m_chipsBar, &FilterChipsBar::filterChanged, this, &HomeScreen::setLibraryFilter);
    body->addSpacing(8);
    body->addWidget(m_chipsBar);
    m_chipsBar->hide();

    m_scroll = new QScrollArea(mainPane);
    m_scroll->setObjectName(QStringLiteral("finilyHomeScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->viewport()->setAttribute(Qt::WA_StyledBackground, false);

    m_gridContent = new QWidget;
    m_gridContent->setObjectName(QStringLiteral("finilyHomeGrid"));
    m_gridContent->setAutoFillBackground(false);
    m_gridContent->setAttribute(Qt::WA_StyledBackground, false);
    m_gridContent->installEventFilter(this);
    m_scroll->setWidget(m_gridContent);
    body->addWidget(m_scroll, 1);

    m_settings = new SettingsView(mainPane);
    m_settings->hide();
    connect(m_settings, &SettingsView::themeModeChanged, this, &HomeScreen::applyThemeMode);
    connect(m_settings, &SettingsView::appearanceChanged, this, &HomeScreen::applyAppearancePreferences);
    connect(m_settings, &SettingsView::homePrefsChanged, this, [this] { refreshLibrary(); });
    connect(m_settings, &SettingsView::pressureSensitivityChanged, this,
            &HomeScreen::pressureSensitivityChanged);
    connect(m_settings, &SettingsView::palmRejectionChanged, this, &HomeScreen::palmRejectionChanged);
    connect(m_settings, &SettingsView::libraryDirChanged, this, [this] { refreshLibrary(); });
    connect(m_settings, &SettingsView::checkUpdatesRequested, this, [this] {
        if (m_toast)
            m_toast->showMessage(QStringLiteral("Buscando actualizaciones…"));
        emit checkUpdatesRequested();
    });
    connect(m_settings, &SettingsView::closeRequested, this, [this] {
        if (m_sidebar)
            m_sidebar->setSelectedFilter(HomeSidebar::NavDocs);
        selectFilter(HomeSidebar::NavDocs);
    });
    mainCol->addWidget(m_settings, 1);

    m_emptyState = new EmptyStateView(m_gridContent);
    m_emptyState->hide();
    m_firstRunHint = new FirstRunHint(this);
    m_firstRunHint->hide();
    m_filterPanel = new FilterPanel(this);
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &HomeScreen::setLibraryFilter);

    m_trashBar = new TrashInfoBar(m_gridContent);
    connect(m_trashBar, &TrashInfoBar::emptyRequested, this, &HomeScreen::askEmptyTrash);
    m_trashBar->hide();

    m_marquee = new MarqueeSelectionOverlay(m_gridContent);

    root->addWidget(mainPane, 1);
    setSidebarCollapsed(readAppSetting(QStringLiteral("home/sidebarCollapsed"), false).toBool(), false);
}

void HomeScreen::buildOverlays() {
    m_createMenu = new CreateMenu(this);
    connect(m_createMenu, &CreateMenu::quickNoteChosen, this, &HomeScreen::launchQuickNote);
    connect(m_createMenu, &CreateMenu::boardChosen, this, [this] {
        const QString folder = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
        m_boardSheet->setFolders(m_allFolders);
        m_boardSheet->setTags(m_allTags);
        m_boardSheet->openCreate(folder, homePaletteFor(m_dark), m_dark);
    });
    connect(m_createMenu, &CreateMenu::notebookChosen, this, [this] {
        const QString folder = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
        m_notebookSheet->setFolders(m_allFolders);
        m_notebookSheet->setTags(m_allTags);
        m_notebookSheet->openCreate(folder, homePaletteFor(m_dark), m_dark);
    });
    connect(m_createMenu, &CreateMenu::folderChosen, this, [this] {
        const QString parent = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
        m_folderSheet->setFolders(m_allFolders);
        m_folderSheet->openCreate(parent, homePaletteFor(m_dark), m_dark);
    });
    connect(m_createMenu, &CreateMenu::importPdfChosen, this, [this] {
        const QString folder = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
        emit importPdfFromHomeRequested(folder);
    });

    m_folderSheet = new NewFolderSheet(this);
    connect(m_folderSheet, &NewFolderSheet::createSubmitted, this,
            [this](const QString& parentRel, const QString& name, int colorIdx, int iconIdx) {
                QString newRel, err;
                if (!LibraryStore::createFolder(parentRel, name, colorIdx, iconIdx, &newRel, &err)) {
                    showError(err.isEmpty() ? QStringLiteral("No se pudo crear la carpeta.") : err);
                    return;
                }
                m_justCreatedFolderRel = newRel;
                if (m_viewMode != ViewMode::Folder && m_viewMode != ViewMode::FoldersRoot) {
                    if (parentRel.isEmpty()) {
                        m_viewMode = ViewMode::FoldersRoot;
                        m_currentFolder.clear();
                    } else {
                        m_viewMode = ViewMode::Folder;
                        m_currentFolder = parentRel;
                    }
                }
                refreshLibrary();
            });
    connect(m_folderSheet, &NewFolderSheet::editSubmitted, this,
            [this](const QString& relPath, const QString& newName, int colorIdx, int iconIdx) {
                LibraryStore::updateFolderStyle(relPath, colorIdx, iconIdx);
                QString newRel, oldAbs, newAbs, err;
                if (LibraryStore::renameFolder(relPath, newName, &newRel, &oldAbs, &newAbs, &err)
                    && newRel != relPath) {
                    if (m_currentFolder == relPath)
                        m_currentFolder = newRel;
                    else if (m_currentFolder.startsWith(relPath + QLatin1Char('/')))
                        m_currentFolder = newRel + m_currentFolder.mid(relPath.size());
                    emit folderRenamed(oldAbs, newAbs);
                }
                refreshLibrary();
            });

    m_boardSheet = new NewBoardSheet(this);
    connect(m_boardSheet, &NewBoardSheet::boardSubmitted, this, &HomeScreen::createDocumentRequested);
    connect(m_boardSheet, &NewBoardSheet::styleEditSubmitted, this,
            [this](const QString& path, ThemeId theme, const QColor& customBg,
                   BackgroundTemplate tpl, double scale) {
                if (!LibraryStore::setBoardStyle(path, theme, customBg, tpl, scale)) {
                    showError(QStringLiteral("No se pudo actualizar el estilo de la pizarra."));
                    return;
                }
                emit boardStyleChanged(path);
                refreshLibrary();
            });
    connect(m_boardSheet, &NewBoardSheet::tagCreateRequested, this, [this](const QString& name) {
        const TagInfo created = LibraryStore::createTag(name);
        if (!created.id.isEmpty()) {
            refreshLibrary();
            m_boardSheet->setTags(m_allTags);
        }
    });

    m_notebookSheet = new NewNotebookSheet(this);
    connect(m_notebookSheet, &NewNotebookSheet::notebookSubmitted, this, &HomeScreen::createDocumentRequested);
    connect(m_notebookSheet, &NewNotebookSheet::coverEditSubmitted, this,
            [this](const QString& filePath, CoverStyle style) {
                LibraryStore::setCoverStyle(filePath, style);
                refreshLibrary();
            });
    connect(m_notebookSheet, &NewNotebookSheet::tagCreateRequested, this, [this](const QString& name) {
        const TagInfo created = LibraryStore::createTag(name);
        if (!created.id.isEmpty()) {
            refreshLibrary();
            m_notebookSheet->setTags(m_allTags);
        }
    });

    m_moveSheet = new MoveToSheet(this);
    connect(m_moveSheet, &MoveToSheet::folderCreated, this, [this](const QString&) {
        refreshLibrary();
    });
    connect(m_moveSheet, &MoveToSheet::moveSubmitted, this, [this](const QString& targetRel) {
        executeMoveTarget(m_pendingMove, targetRel, true);
        m_pendingMove = PendingMoveTarget{};
    });

    m_menu = new HomeMenu(this);
    m_toast = new HomeToast(this);
    m_confirm = new ConfirmDialog(this);
    m_dropZone = new DropZoneOverlay(this);
    m_dragGhost = new DragGhostOverlay(this);
}

void HomeScreen::installShortcuts() {
    auto addShortcut = [this](const QKeySequence& seq, const std::function<void()>& fn) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };

    addShortcut(QKeySequence::Find, [this] {
        if (!m_settingsOpen && m_header)
            m_header->openSearch();
    });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+N")), [this] {
        if (!m_settingsOpen)
            launchQuickNote();
    });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+A")), [this] {
        if (m_settingsOpen || (m_header && !m_header->searchText().isEmpty()))
            return;
        if (QWidget* fw = QApplication::focusWidget()) {
            if (qobject_cast<QLineEdit*>(fw))
                return;
        }
        selectAllVisible();
    });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+B")), [this] {
        if (m_settingsOpen || m_viewMode == ViewMode::Trash)
            return;
        if (!m_selectedKeys.isEmpty())
            toggleFavoriteForSelection();
        else if (ObjectCard* card = hoveredCard())
            toggleFavoriteForCard(card);
    });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+D")), [this] {
        if (m_settingsOpen || m_viewMode == ViewMode::Trash)
            return;
        QStringList paths;
        for (NotebookCard* nc : m_cards) {
            if (nc && m_selectedKeys.contains(keyForDoc(nc->filePath())))
                paths.append(nc->filePath());
        }
        if (paths.isEmpty()) {
            if (auto* nc = qobject_cast<NotebookCard*>(hoveredCard()))
                paths.append(nc->filePath());
        }
        if (!paths.isEmpty())
            duplicateNotebooks(paths);
    });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+Z")), [this] {
        if (m_toast && m_toast->isVisible() && m_toast->hasAction())
            m_toast->triggerAction();
    });
    addShortcut(QKeySequence(Qt::Key_F2), [this] {
        if (m_settingsOpen || m_viewMode == ViewMode::Trash)
            return;
        if (m_selectedKeys.size() == 1) {
            for (ObjectCard* c : orderedCards()) {
                if (m_selectedKeys.contains(keyForCard(c))) {
                    c->beginRename();
                    return;
                }
            }
        }
        if (ObjectCard* card = hoveredCard())
            card->beginRename();
    });
    addShortcut(QKeySequence(QKeySequence::Delete), [this] {
        if (m_settingsOpen)
            return;
        if (ObjectCard* h = hoveredCard()) {
            if (h->isRenaming())
                return;
        }
        if (m_viewMode == ViewMode::Trash)
            return;
        if (!m_selectedKeys.isEmpty()) {
            moveSelectionToTrash();
            return;
        }
        ObjectCard* card = hoveredCard();
        if (!card || card->isRenaming())
            return;
        if (auto* fc = qobject_cast<FolderCard*>(card))
            moveToTrash(LibraryStore::resolveDirectory(fc->meta().relativePath), fc->meta().name, true, fc);
        else if (auto* nc = qobject_cast<NotebookCard*>(card))
            moveToTrash(nc->filePath(), nc->title(), false, nc);
    });
    addShortcut(QKeySequence(Qt::Key_Escape), [this] {
        if (m_settingsOpen) {
            if (m_sidebar)
                m_sidebar->setSelectedFilter(HomeSidebar::NavDocs);
            selectFilter(HomeSidebar::NavDocs);
            return;
        }
        if (m_internalDragging) {
            finishInternalDrag(QCursor::pos(), true);
            return;
        }
        if (!m_selectedKeys.isEmpty()) {
            clearSelection();
        }
    });
}

void HomeScreen::setSidebarCollapsed(bool collapsed, bool /*animated*/) {
    m_sidebarCollapsed = collapsed;
    writeAppSetting(QStringLiteral("home/sidebarCollapsed"), collapsed);
    if (!m_sidebar)
        return;
    m_sidebar->setPanelWidth(collapsed ? 0 : tokens::space::SidebarWidth);
    m_sidebar->setVisible(!collapsed);
    if (m_header)
        m_header->setSidebarCollapsed(collapsed, false);
    syncToastArea();
    if (isVisible())
        relayoutGrid();
    else
        QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

void HomeScreen::applyTheme() {
    const HomePalette pal = homePaletteFor(m_dark);
    setStyleSheet(homeScreenStylesheet(pal, m_dark));
    if (m_scroll)
        m_scroll->viewport()->setAutoFillBackground(false);
    if (m_sidebar)
        m_sidebar->applyTheme(pal, m_dark);
    if (m_header)
        m_header->applyTheme(pal, m_dark);
    if (m_createMenu)
        m_createMenu->applyTheme(pal, m_dark);
    if (m_menu)
        m_menu->applyTheme(m_dark);
    if (m_confirm)
        m_confirm->applyTheme(m_dark);
    if (m_trashBar)
        m_trashBar->setPalette(pal, m_dark);
    if (m_emptyState)
        m_emptyState->applyTheme(pal, m_dark);
    if (m_firstRunHint)
        m_firstRunHint->applyTheme(pal);
    if (m_chipsBar)
        m_chipsBar->applyTheme(pal, m_dark);
    if (m_filterPanel)
        m_filterPanel->applyTheme(pal, m_dark);
    if (m_dropZone)
        m_dropZone->setDark(m_dark);
    if (m_settings)
        m_settings->applyTheme(pal, m_dark);
    for (ResultGroupLabel* gl : m_groupLabels) {
        if (gl)
            gl->applyTheme(pal);
    }
    for (TrashCard* tc : m_trashCards) {
        if (tc)
            tc->setPalette(pal, m_dark);
    }
    for (FolderCard* fc : m_folderCards) {
        if (fc)
            fc->setPalette(pal, m_dark);
    }
    for (NotebookCard* card : m_cards) {
        if (card)
            card->setPalette(pal, m_dark);
    }
    for (ImportingCard* ic : m_importingCards) {
        if (ic)
            ic->setDark(m_dark);
    }
    update();
}

void HomeScreen::setDarkMode(bool dark, bool animated) {
    if (m_dark == dark)
        return;
    QPixmap beforeSnap;
    QPoint origin;
    const bool doReveal = animated && isVisible() && m_sidebar;
    if (doReveal) {
        beforeSnap = grab();
        origin = m_sidebar->isVisible() ? m_sidebar->themeButtonCenterIn(this) : rect().center();
    }
    m_dark = dark;
    writeAppSetting(QStringLiteral("home/darkMode"), dark);
    applyTheme();
    if (doReveal) {
        if (m_sidebar->isVisible())
            m_sidebar->pulseThemeButton();
        ThemeRevealOverlay::play(this, beforeSnap, origin);
    }
}

void HomeScreen::applyThemeMode(int mode) {
    const bool dark = mode == SettingsView::System
                          ? systemPrefersDark()
                          : mode == SettingsView::Dark;
    setDarkMode(dark, true);
}

void HomeScreen::applyAppearancePreferences() {
    applyTheme();
}

void HomeScreen::openSettings() {
    if (!m_settings)
        return;
    m_settingsOpen = true;
    if (m_filterPanel)
        m_filterPanel->closePanel();
    if (m_sidebar)
        m_sidebar->setSelectedSettings();
    m_header->setSettingsMode(true);
    m_header->setSectionTitle(QStringLiteral("Ajustes"));
    for (QWidget* w : {static_cast<QWidget*>(m_scroll), static_cast<QWidget*>(m_chipsBar),
                       static_cast<QWidget*>(m_firstRunHint)})
        w->hide();
    m_settings->show();
    m_settings->setFocus();
    QTimer::singleShot(0, m_settings, [this] { m_settings->reload(); });
}

void HomeScreen::leaveSettings() {
    if (!m_settingsOpen)
        return;
    m_settingsOpen = false;
    m_settings->hide();
    m_scroll->show();
    m_header->setSettingsMode(false);
}

void HomeScreen::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), homePaletteFor(m_dark).bg);
}

void HomeScreen::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_createMenu && m_createMenu->isVisible())
        m_createMenu->reposition(createMenuAnchor());
    for (auto* sheet : {static_cast<GlassSheet*>(m_folderSheet),
                        static_cast<GlassSheet*>(m_boardSheet),
                        static_cast<GlassSheet*>(m_notebookSheet),
                        static_cast<GlassSheet*>(m_moveSheet)}) {
        if (sheet && sheet->isVisible())
            sheet->repositionOnResize();
    }
    if (m_dropZone && m_dropZone->isVisible() && m_scroll)
        m_dropZone->setGeometry(QRect(m_scroll->mapTo(this, QPoint(0, 0)), m_scroll->size()));
    syncToastArea();
    QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

void HomeScreen::syncToastArea() {
    if (!m_toast || !m_scroll)
        return;
    const QPoint tl = m_scroll->mapTo(this, QPoint(0, 0));
    m_toast->setArea(QRect(tl.x(), 0, std::max(260, m_scroll->width()), height()));
}

void HomeScreen::refreshLibrary() {
    for (FolderCard* fc : m_folderCards) {
        if (fc) { fc->hide(); fc->deleteLater(); }
    }
    m_folderCards.clear();
    for (NotebookCard* card : m_cards) {
        if (card) { card->hide(); card->deleteLater(); }
    }
    m_cards.clear();
    for (TrashCard* tc : m_trashCards) {
        if (tc) { tc->hide(); tc->deleteLater(); }
    }
    m_trashCards.clear();

    m_allFolders = LibraryStore::listAllFoldersTree();
    m_allTags = LibraryStore::listTags();
    const QVector<NotebookMeta> allDocs = LibraryStore::listAllNotebooksRecursive();

    if (m_viewMode == ViewMode::Folder) {
        const bool exists = std::any_of(m_allFolders.cbegin(), m_allFolders.cend(),
                                        [this](const FolderMeta& f) { return f.relativePath == m_currentFolder; });
        if (!exists) {
            m_viewMode = ViewMode::Todos;
            m_currentFolder.clear();
        }
    }
    if (m_viewMode == ViewMode::Tag) {
        const bool tagExists = std::any_of(m_allTags.cbegin(), m_allTags.cend(),
                                           [this](const TagInfo& t) { return t.id == m_currentTagId; });
        if (!tagExists) {
            m_viewMode = ViewMode::Todos;
            m_currentTagId.clear();
        }
    }

    if (m_sidebar) {
        m_sidebar->setFolders(m_allFolders);
        m_sidebar->setTags(m_allTags);
        if (m_folderSheet) m_folderSheet->setFolders(m_allFolders);
        if (m_boardSheet) {
            m_boardSheet->setFolders(m_allFolders);
            m_boardSheet->setTags(m_allTags);
        }
        if (m_notebookSheet) {
            m_notebookSheet->setFolders(m_allFolders);
            m_notebookSheet->setTags(m_allTags);
        }
        if (m_settingsOpen) {
            m_sidebar->setSelectedSettings();
        } else if (m_viewMode == ViewMode::Folder) {
            m_sidebar->setSelectedFolder(m_currentFolder);
        } else if (m_viewMode == ViewMode::Tag) {
            m_sidebar->setSelectedTag(m_currentTagId);
        } else {
            const int navIdx = (m_viewMode == ViewMode::Favoritos) ? HomeSidebar::NavFavs
                             : (m_viewMode == ViewMode::FoldersRoot) ? HomeSidebar::NavFolders
                             : (m_viewMode == ViewMode::Trash) ? HomeSidebar::NavTrash
                             : HomeSidebar::NavDocs;
            m_sidebar->setSelectedFilter(navIdx);
        }
        m_sidebar->setDraftVisible(LibraryStore::hasRecoverableDraft());
    }
    if (m_filterPanel)
        m_filterPanel->setTags(m_allTags);
    if (m_chipsBar)
        m_chipsBar->setTags(m_allTags);

    const HomePalette pal = homePaletteFor(m_dark);
    const bool showDates = readAppSetting(QStringLiteral("home/showDates"), true).toBool();
    const QVector<TrashEntry> trash = LibraryStore::listTrash();
    if (m_sidebar)
        m_sidebar->setTrashCount(int(trash.size()));
    if (m_trashBar) {
        m_trashBar->setCount(int(trash.size()));
        m_trashBar->setRetentionDays(LibraryStore::trashRetentionDays());
        m_trashBar->setVisible(m_viewMode == ViewMode::Trash && !trash.isEmpty());
    }

    clearGroupLabels();

    if (m_viewMode == ViewMode::Trash) {
        QVector<TrashEntry> sortedTrash = trash;
        if (m_filter.sort == LibraryFilter::Name) {
            std::stable_sort(sortedTrash.begin(), sortedTrash.end(), [](const TrashEntry& a, const TrashEntry& b) {
                return QString::localeAwareCompare(a.name, b.name) < 0;
            });
        }
        for (const TrashEntry& entry : sortedTrash) {
            auto* tc = new TrashCard(entry, m_gridContent);
            tc->setPalette(pal, m_dark);
            tc->setShowDetail(showDates);
            const QPointer<TrashCard> guard(tc);
            connect(tc, &ObjectCard::menuRequested, this, [this, guard](const QPoint& pos) {
                if (guard) showTrashMenu(guard.data(), pos);
            });
            connect(tc, &TrashCard::restoreRequested, this, &HomeScreen::restoreFromTrash);
            connect(tc, &TrashCard::restorePromptRequested, this, &HomeScreen::promptRestoreFromTrash);
            connect(tc, &TrashCard::deleteForeverRequested, this, &HomeScreen::deleteForever);
            m_trashCards.append(tc);
        }
        m_selectedKeys.clear();
        applySearchFilter(m_header ? m_header->searchText() : QString());
        return;
    }

    const bool searching = isSearching();
    const QString needle = searching ? m_header->searchText().trimmed() : QString();
    const bool scoped = searchIsScoped();
    const QString scopePrefix = m_currentFolder + QLatin1Char('/');
    auto inScope = [&](const QString& rel) {
        return !scoped || rel == m_currentFolder || rel.startsWith(scopePrefix);
    };
    auto pathLabel = [](const QString& rel) {
        return rel.isEmpty() ? QStringLiteral("Documentos")
                             : QString(rel).replace(QLatin1Char('/'), QStringLiteral(" › "));
    };
    m_libraryEmpty = allDocs.isEmpty() && m_allFolders.isEmpty();

    QVector<FolderMeta> folders;
    QVector<NotebookMeta> visibleDocs;

    if (searching) {
        if (m_filter.acceptsFolders()) {
            for (const FolderMeta& fm : m_allFolders) {
                if (fm.relativePath != m_currentFolder && inScope(fm.relativePath) &&
                    fm.name.contains(needle, Qt::CaseInsensitive))
                    folders.append(fm);
            }
        }
        for (const NotebookMeta& meta : allDocs) {
            if (inScope(meta.folderRelativePath) && meta.title.contains(needle, Qt::CaseInsensitive))
                visibleDocs.append(meta);
        }
    } else if (m_viewMode == ViewMode::Favoritos) {
        const QStringList favKeys = LibraryStore::favoriteKeys();
        QHash<QString, FolderMeta> folderByRel;
        for (const FolderMeta& fm : m_allFolders)
            folderByRel.insert(fm.relativePath, fm);
        QHash<QString, NotebookMeta> docByKey;
        for (const NotebookMeta& nm : allDocs)
            docByKey.insert(keyForDoc(nm.path), nm);

        for (const QString& k : favKeys) {
            if (k.startsWith(QLatin1String("folder:"))) {
                const QString rel = k.mid(7);
                if (folderByRel.contains(rel) && m_filter.acceptsFolders())
                    folders.append(folderByRel.value(rel));
            } else if (k.startsWith(QLatin1String("doc:"))) {
                const QString relDoc = k.mid(4);
                for (const NotebookMeta& nm : allDocs) {
                    const QString expectedRel = nm.folderRelativePath.isEmpty()
                        ? QFileInfo(nm.path).fileName()
                        : (nm.folderRelativePath + QLatin1Char('/') + QFileInfo(nm.path).fileName());
                    if (expectedRel == relDoc) {
                        visibleDocs.append(nm);
                        break;
                    }
                }
            }
        }
    } else if (m_viewMode == ViewMode::FoldersRoot) {
        if (m_filter.acceptsFolders())
            folders = LibraryStore::listFolders(QString());
    } else if (m_viewMode == ViewMode::Folder) {
        if (m_filter.acceptsFolders())
            folders = LibraryStore::listFolders(m_currentFolder);
        visibleDocs = LibraryStore::listNotebooks(m_currentFolder);
    } else if (m_viewMode == ViewMode::Tag) {
        for (const NotebookMeta& nm : allDocs) {
            if (nm.tagIds.contains(m_currentTagId))
                visibleDocs.append(nm);
        }
    } else {
        // ViewMode::Todos (Propuesta 1 aprobada: Documentos muestra archivos;
        // si el usuario activa filtro "Solo carpetas", muestra todas las carpetas).
        if (m_filter.kind == LibraryFilter::Folders && m_filter.acceptsFolders())
            folders = m_allFolders;
        visibleDocs = allDocs;
    }

    if (m_viewMode != ViewMode::Favoritos && m_filter.sort == LibraryFilter::Name) {
        std::stable_sort(folders.begin(), folders.end(), [](const FolderMeta& a, const FolderMeta& b) {
            return QString::localeAwareCompare(a.name, b.name) < 0;
        });
    }

    visibleDocs.erase(std::remove_if(visibleDocs.begin(), visibleDocs.end(),
                                     [this](const NotebookMeta& m) { return !m_filter.acceptsDocument(m); }),
                      visibleDocs.end());
    if (m_viewMode != ViewMode::Favoritos || m_filter.sort != LibraryFilter::Recent)
        sortDocuments(visibleDocs, m_filter.sort);

    QSet<QString> validKeys;
    for (const FolderMeta& fm : folders) {
        auto* fc = new FolderCard(fm, m_gridContent);
        fc->setPalette(pal, m_dark);
        fc->setShowDetail(showDates);
        fc->setFavoriteBadge(m_viewMode == ViewMode::Favoritos || fm.isFavorite);
        fc->setHidden(false);
        const QString fkey = keyForFolder(fm.relativePath);
        validKeys.insert(fkey);
        if (m_selectedKeys.contains(fkey))
            fc->setSelected(true);
        if (searching) {
            fc->setHighlight(needle);
            const QString parentRel = QFileInfo(fm.relativePath).path();
            fc->setDetailOverride(pathLabel(parentRel == QLatin1String(".") ? QString() : parentRel));
        } else if (m_viewMode == ViewMode::Favoritos) {
            const QString parentRel = QFileInfo(fm.relativePath).path();
            fc->setDetailOverride(pathLabel(parentRel == QLatin1String(".") ? QString() : parentRel));
        }
        connect(fc, &FolderCard::openRequested, this, [this](const QString& rel) {
            if (m_header)
                m_header->closeSearch();
            openFolder(rel, true);
        });
        const QPointer<FolderCard> guard(fc);
        connect(fc, &ObjectCard::menuRequested, this, [this, guard](const QPoint& pos) {
            if (guard) showFolderMenu(guard->meta(), pos, guard.data());
        });
        connect(fc, &ObjectCard::selectionClicked, this, &HomeScreen::handleCardSelectionClick);
        connect(fc, &ObjectCard::dragStarted, this, &HomeScreen::startInternalDrag);
        connect(fc, &ObjectCard::renameCommitted, this, [this, guard, rel = fm.relativePath](const QString& name) {
            QTimer::singleShot(0, this, [this, guard, rel, name] { renameFolderTo(rel, name, guard.data()); });
        });
        m_folderCards.append(fc);
        if (!m_justCreatedFolderRel.isEmpty() && fm.relativePath == m_justCreatedFolderRel) {
            m_justCreatedFolderRel.clear();
            QTimer::singleShot(0, fc, [guard] {
                if (guard)
                    guard->playPopIn();
            });
        }
    }

    for (const NotebookMeta& meta : visibleDocs) {
        auto* card = new NotebookCard(meta, m_gridContent);
        card->setPalette(pal, m_dark);
        card->setShowDetail(showDates);
        card->setFavoriteBadge(m_viewMode == ViewMode::Favoritos || meta.isFavorite);
        const QString dkey = keyForDoc(meta.path);
        validKeys.insert(dkey);
        if (m_selectedKeys.contains(dkey))
            card->setSelected(true);
        if (searching || m_viewMode == ViewMode::Favoritos || m_viewMode == ViewMode::Tag)
            card->setDetailOverride(pathLabel(meta.folderRelativePath));
        if (searching)
            card->setHighlight(needle);
        connect(card, &NotebookCard::openRequested, this, [this](const QString& path) {
            emit openNotebookRequested(path);
        });
        const QPointer<NotebookCard> guard(card);
        connect(card, &ObjectCard::menuRequested, this, [this, guard](const QPoint& pos) {
            if (guard) showNotebookMenu(guard.data(), pos);
        });
        connect(card, &ObjectCard::selectionClicked, this, &HomeScreen::handleCardSelectionClick);
        connect(card, &ObjectCard::dragStarted, this, &HomeScreen::startInternalDrag);
        connect(card, &ObjectCard::renameCommitted, this, [this, guard, path = meta.path](const QString& name) {
            QTimer::singleShot(0, this, [this, guard, path, name] { renameNotebookTo(path, name, guard.data()); });
        });
        m_cards.append(card);
        if (!m_pendingFlyRestorePath.isEmpty() && meta.path == m_pendingFlyRestorePath) {
            m_pendingFlyRestorePath.clear();
            card->setProperty("finilyHideUntilLanded", true);
        }
    }
    m_selectedKeys.intersect(validKeys);

    if (searching) {
        if (!m_folderCards.isEmpty())
            m_groupLabels.append(new ResultGroupLabel(QStringLiteral("Carpetas"), int(m_folderCards.size()), m_gridContent));
        if (!m_cards.isEmpty())
            m_groupLabels.append(new ResultGroupLabel(QStringLiteral("Documentos"), int(m_cards.size()), m_gridContent));
    } else if (m_viewMode == ViewMode::Folder && !m_folderCards.isEmpty() && !m_cards.isEmpty()) {
        m_groupLabels.append(new ResultGroupLabel(QStringLiteral("Carpetas"), int(m_folderCards.size()), m_gridContent));
        m_groupLabels.append(new ResultGroupLabel(QStringLiteral("Documentos"), int(m_cards.size()), m_gridContent));
    }
    for (ResultGroupLabel* gl : m_groupLabels) {
        gl->applyTheme(pal);
        gl->show();
    }

    applySearchFilter(m_header ? m_header->searchText() : QString());
}

void HomeScreen::selectFilter(int index) {
    leaveSettings();
    m_selectedKeys.clear();
    m_currentFolder.clear();
    m_currentTagId.clear();
    m_searchWhole = false;
    if (index == HomeSidebar::NavTrash)
        m_viewMode = ViewMode::Trash;
    else if (index == HomeSidebar::NavFavs)
        m_viewMode = ViewMode::Favoritos;
    else if (index == HomeSidebar::NavFolders)
        m_viewMode = ViewMode::FoldersRoot;
    else
        m_viewMode = ViewMode::Todos;
    refreshLibrary();
}

void HomeScreen::revealNotebookPath(const QString& filePath) {
    leaveSettings();
    const QFileInfo fi(filePath);
    if (!filePath.isEmpty()) {
        const QString absDir = QDir::cleanPath(fi.absolutePath());
        for (const QString& root : LibraryStore::libraryDirectories()) {
            const QDir rootDir(QDir::cleanPath(root));
            const QString rel = rootDir.relativeFilePath(absDir);
            if (rel == QLatin1String(".")) {
                selectFilter(HomeSidebar::NavDocs);
                return;
            }
            if (!rel.isEmpty() && !rel.startsWith(QLatin1String(".."))) {
                openFolder(rel, false);
                return;
            }
        }
    }
    selectFilter(HomeSidebar::NavDocs);
}

void HomeScreen::runAnimatedFolderTransition(const QPointF& pivotInViewport, bool entering,
                                             const std::function<void()>& applyState) {
    if (!m_scroll || !m_scroll->viewport()->isVisible() || m_settingsOpen) {
        applyState();
        return;
    }
    QWidget* vp = m_scroll->viewport();
    const QPixmap oldGrid = vp->grab();
    m_skipNextFlip = true;
    applyState();
    m_gridContent->resize(vp->width(), std::max(vp->height(), m_gridContent->minimumHeight()));
    relayoutGrid();
    const QPixmap newGrid = vp->grab();
    m_skipNextFlip = false;

    const QRect gridRect(vp->mapTo(this, QPoint(0, 0)), vp->size());
    FolderZoomOverlay::play(this, oldGrid, newGrid, gridRect, pivotInViewport,
                            homePaletteFor(m_dark).bg, entering);
}

void HomeScreen::openFolder(const QString& relativePath, bool animated) {
    leaveSettings();
    m_selectedKeys.clear();
    const QString norm = LibraryStore::normalizeRelativeFolder(relativePath);
    if (norm.isEmpty()) {
        if (animated && m_viewMode == ViewMode::Folder) {
            const QPointF pivot(m_scroll->viewport()->width() / 2.0,
                                m_scroll->viewport()->height() / 3.0);
            runAnimatedFolderTransition(pivot, false, [this] {
                m_viewMode = ViewMode::FoldersRoot;
                m_currentFolder.clear();
                m_currentTagId.clear();
                m_searchWhole = false;
                refreshLibrary();
            });
            return;
        }
        selectFilter(HomeSidebar::NavDocs);
        return;
    }

    const bool entering = m_viewMode != ViewMode::Folder || norm.size() >= m_currentFolder.size();
    QPointF pivot(m_scroll ? m_scroll->viewport()->width() / 2.0 : 200.0,
                  m_scroll ? m_scroll->viewport()->height() / 3.0 : 180.0);
    if (FolderCard* fc = folderCardFor(norm)) {
        if (m_scroll && m_scroll->viewport())
            pivot = fc->mapTo(m_scroll->viewport(), QPointF(fc->width() / 2.0, fc->height() / 2.0));
    }

    auto apply = [this, norm] {
        m_viewMode = ViewMode::Folder;
        m_currentFolder = norm;
        m_currentTagId.clear();
        m_searchWhole = false;
        refreshLibrary();
    };
    if (animated && isVisible())
        runAnimatedFolderTransition(pivot, entering, apply);
    else
        apply();
}

void HomeScreen::openTag(const QString& tagId) {
    leaveSettings();
    m_selectedKeys.clear();
    m_viewMode = ViewMode::Tag;
    m_currentTagId = tagId;
    m_currentFolder.clear();
    m_searchWhole = false;
    refreshLibrary();
}

bool HomeScreen::isSearching() const {
    return m_header && m_viewMode != ViewMode::Trash && !m_header->searchText().trimmed().isEmpty();
}

bool HomeScreen::searchIsScoped() const {
    return m_viewMode == ViewMode::Folder && !m_searchWhole;
}

void HomeScreen::onSearchTextChanged(const QString& text) {
    if (m_viewMode == ViewMode::Trash) {
        applySearchFilter(text);
        return;
    }
    if (text.trimmed().isEmpty())
        m_searchWhole = false;
    const auto before = captureCardPositions();
    refreshLibrary();
    if (!m_skipNextFlip)
        animateGridFlip(before, currentCardWidgets(), 0);
}

void HomeScreen::setLibraryFilter(const LibraryFilter& filter) {
    if (filter.sort != m_filter.sort)
        LibraryFilter::saveSort(filter.sort);
    const auto before = captureCardPositions();
    m_filter = filter;
    refreshLibrary();
    if (!m_skipNextFlip)
        animateGridFlip(before, currentCardWidgets(), 15);
}

void HomeScreen::openFilterPanel() {
    if (!m_filterPanel || !m_header)
        return;
    if (m_filterPanel->isVisible()) {
        m_filterPanel->closePanel();
        return;
    }
    m_filterPanel->setTags(m_allTags);
    const QRect r = m_header->filterButtonGeometry();
    m_filterPanel->popup(QRect(m_header->mapTo(this, r.topLeft()), r.size()),
                         m_filter, m_viewMode == ViewMode::Trash);
}

void HomeScreen::openFirstResult() {
    if (!m_folderCards.isEmpty())
        m_folderCards.first()->open();
    else if (!m_cards.isEmpty())
        m_cards.first()->open();
}

void HomeScreen::clearGroupLabels() {
    for (ResultGroupLabel* gl : m_groupLabels) {
        if (gl) { gl->hide(); gl->deleteLater(); }
    }
    m_groupLabels.clear();
}

void HomeScreen::applySearchFilter(const QString& text) {
    const QString needle = m_viewMode == ViewMode::Trash ? text.trimmed() : QString();
    for (TrashCard* tc : m_trashCards) {
        if (tc)
            tc->setHidden(!tc->matchesSearch(needle));
    }
    for (NotebookCard* card : m_cards) {
        if (card)
            card->setHidden(false);
    }
    updateHeaderAndEmptyState();
    relayoutGrid();
}

void HomeScreen::updateHeaderAndEmptyState() {
    int visibleFolders = 0;
    for (const FolderCard* fc : m_folderCards) {
        if (fc && !fc->isHidden()) ++visibleFolders;
    }
    int visibleDocs = 0;
    for (const NotebookCard* card : m_cards) {
        if (card && !card->isHidden()) ++visibleDocs;
    }
    for (const TrashCard* tc : m_trashCards) {
        if (tc && !tc->isHidden()) ++visibleDocs;
    }
    visibleDocs += int(m_importingCards.size());
    const int totalVisible = visibleFolders + visibleDocs;
    if (m_header) {
        const bool animTitle = !m_settingsOpen && isVisible();
        if (m_viewMode == ViewMode::Folder)
            m_header->setFolderBreadcrumb(m_currentFolder, animTitle);
        else if (m_viewMode == ViewMode::Tag) {
            QString tname = QStringLiteral("Etiqueta");
            for (const TagInfo& t : m_allTags) {
                if (t.id == m_currentTagId) {
                    tname = t.name;
                    break;
                }
            }
            m_header->setTagBreadcrumb(tname, animTitle);
        } else if (m_viewMode == ViewMode::Favoritos)
            m_header->setSectionTitle(QStringLiteral("Favoritos"), animTitle);
        else if (m_viewMode == ViewMode::FoldersRoot)
            m_header->setSectionTitle(QStringLiteral("Carpetas"), animTitle);
        else if (m_viewMode == ViewMode::Trash)
            m_header->setSectionTitle(QStringLiteral("Papelera"), animTitle);
        else
            m_header->setSectionTitle(QStringLiteral("Documentos"), animTitle);

        const bool trash = m_viewMode == ViewMode::Trash;
        m_header->setTrashEmptyButtonVisible(trash && !m_trashCards.isEmpty());
        m_header->setFilterVisible(true);
        m_header->setFilterActive(m_filter.activeCount() > 0);
        m_header->setSearchScope(isSearching() && searchIsScoped() ? QFileInfo(m_currentFolder).fileName() : QString());
    }
    if (m_chipsBar) {
        m_chipsBar->setFilter(m_filter);
        m_chipsBar->setVisible(!m_settingsOpen && m_viewMode != ViewMode::Trash && m_chipsBar->hasChips());
    }
    configureEmptyState(totalVisible);
    if (m_settingsOpen && m_header) {
        m_header->setSectionTitle(QStringLiteral("Ajustes"));
        m_header->setSettingsMode(true);
        if (m_firstRunHint)
            m_firstRunHint->hide();
    }
}

void HomeScreen::configureEmptyState(int totalVisible) {
    if (!m_emptyState)
        return;
    const bool firstRun = totalVisible == 0 && m_libraryEmpty && m_viewMode == ViewMode::Todos && !isSearching();
    if (m_firstRunHint)
        m_firstRunHint->setVisible(firstRun && !m_settingsOpen);
    m_emptyState->setHidden(totalVisible != 0);
    if (totalVisible != 0)
        return;

    const bool filtered = m_filter.activeCount() > 0;
    auto clearFilter = [this] {
        LibraryFilter clean;
        clean.sort = m_filter.sort;
        setLibraryFilter(clean);
    };
    const QString folderName = QFileInfo(m_currentFolder).fileName().toHtmlEscaped();
    if (isSearching()) {
        const QString q = m_header->searchText().trimmed();
        QStringList limits;
        if (searchIsScoped())
            limits << QStringLiteral("solo en <b>%1</b>").arg(folderName);
        if (m_filter.kind != LibraryFilter::AllKinds)
            limits << QStringLiteral("solo %1").arg(LibraryFilter::kindLabel(m_filter.kind).toLower());
        if (m_filter.date != LibraryFilter::AnyDate)
            limits << LibraryFilter::dateLabel(m_filter.date).toLower();
        QVector<EmptyStateView::Action> acts;
        if (searchIsScoped())
            acts.append({QStringLiteral("Buscar en toda la biblioteca"), [this] {
                             m_searchWhole = true;
                             refreshLibrary();
                         }});
        if (filtered)
            acts.append({QStringLiteral("Quitar filtro"), clearFilter});
        m_emptyState->setContent(
            QStringLiteral("search"), QStringLiteral("Sin resultados para «%1»").arg(q),
            limits.isEmpty() ? QStringLiteral("Prueba con otra palabra o revisa cómo está escrita.")
                             : QStringLiteral("Estás buscando %1.").arg(limits.join(QStringLiteral(" y "))),
            acts);
    } else if (m_viewMode == ViewMode::Trash) {
        const int days = LibraryStore::trashRetentionDays();
        const QString msg = days > 0
            ? QStringLiteral("Lo que borres se guarda aquí <b>%1 días</b> por si cambias de opinión.").arg(days)
            : QStringLiteral("La limpieza automática está desactivada.");
        m_emptyState->setContent(QStringLiteral("trash"), QStringLiteral("La papelera está vacía"), msg);
    } else if (m_viewMode == ViewMode::Favoritos) {
        m_emptyState->setContent(
            QStringLiteral("star"), QStringLiteral("Aún no tienes favoritos"),
            QStringLiteral("Clic derecho › <b>Añadir a favoritos</b> o <kbd>Ctrl B</kbd> sobre cualquier "
                           "documento o carpeta para tenerlo a mano aquí."));
    } else if (m_viewMode == ViewMode::Tag) {
        TagInfo current;
        for (const TagInfo& t : m_allTags) {
            if (t.id == m_currentTagId) {
                current = t;
                break;
            }
        }
        m_emptyState->setContent(
            QStringLiteral("tag"),
            QStringLiteral("Nada con «%1» todavía").arg(current.name.toHtmlEscaped()),
            QStringLiteral("Añade esta etiqueta desde el clic derecho de un documento, o bórrala si ya no la usas."),
            {{QStringLiteral("Borrar etiqueta"), [this, current] { deleteTagWithUndo(current); },
              QStringLiteral("trash"), true}});
    } else if (filtered) {
        m_emptyState->setContent(QStringLiteral("search"), QStringLiteral("Nada coincide con el filtro"),
                                 QStringLiteral("Ningún elemento de esta vista cumple lo que elegiste."),
                                 {{QStringLiteral("Quitar filtro"), clearFilter}});
    } else if (m_viewMode == ViewMode::Folder) {
        m_emptyState->setContent(
            QStringLiteral("folder"), QStringLiteral("%1 está vacía").arg(QFileInfo(m_currentFolder).fileName()),
            QStringLiteral("Arrastra documentos hasta aquí o usa <b>+&nbsp;Nuevo</b>: lo que crees se guardará en esta carpeta."));
    } else if (m_viewMode == ViewMode::FoldersRoot) {
        m_emptyState->setContent(
            QStringLiteral("folder"), QStringLiteral("Aún no tienes carpetas"),
            QStringLiteral("Usa <b>+&nbsp;Nuevo</b> › <b>Carpeta</b> para organizar tus pizarras y cuadernos."));
    } else if (firstRun) {
        m_emptyState->setContent(QStringLiteral("library"), QStringLiteral("Tu biblioteca está lista"),
                                 QStringLiteral("Crea tu primera pizarra o cuaderno con <b>+&nbsp;Nuevo</b>, abre una nota "
                                                "rápida con <kbd>Ctrl&nbsp;N</kbd> o suelta aquí un PDF."));
    } else {
        m_emptyState->setContent(QStringLiteral("library"), QStringLiteral("No hay documentos aquí"),
                                 QStringLiteral("Lo que abras o crees con <b>+&nbsp;Nuevo</b> aparecerá en esta vista."));
    }
}

void HomeScreen::relayoutGrid() {
    if (!m_gridContent || !m_scroll)
        return;
    constexpr int cardW = ObjectCard::kCardWidth;
    constexpr int cardH = ObjectCard::kCardHeight;
    constexpr int minGap = 18;
    constexpr int rowGap = 24;
    const bool trashView = m_viewMode == ViewMode::Trash && m_trashBar && !m_trashCards.isEmpty();
    const int top = trashView ? 14 + m_trashBar->height() + 14 : 22;

    const int contentWidth = m_scroll->viewport()->width() - 14;
    const int columns = std::max(1, (contentWidth + minGap) / (cardW + minGap));
    const qreal colWidth = qreal(std::max(contentWidth, cardW)) / columns;

    struct Section {
        ResultGroupLabel* label = nullptr;
        QList<QWidget*> items;
    };
    QVector<Section> sections;
    Section folders, docs;
    for (ImportingCard* ic : m_importingCards) {
        if (ic)
            docs.items.append(ic);
    }
    for (FolderCard* fc : m_folderCards) {
        if (fc && !fc->isHidden())
            folders.items.append(fc);
    }
    for (NotebookCard* card : m_cards) {
        if (card && !card->isHidden())
            docs.items.append(card);
    }
    for (TrashCard* tc : m_trashCards) {
        if (tc && !tc->isHidden())
            docs.items.append(tc);
    }

    const bool foldersFirst = readAppSetting(QStringLiteral("home/foldersFirst"), true).toBool();
    if (!m_groupLabels.isEmpty()) {
        int gi = 0;
        if (!folders.items.isEmpty()) folders.label = m_groupLabels.value(gi++);
        if (!docs.items.isEmpty()) docs.label = m_groupLabels.value(gi++);
        if (foldersFirst || isSearching())
            sections << folders << docs;
        else
            sections << docs << folders;
    } else {
        Section unified;
        if (foldersFirst) {
            unified.items = folders.items;
            unified.items.append(docs.items);
        } else {
            unified.items = docs.items;
            unified.items.append(folders.items);
        }
        sections << unified;
    }
    if (trashView)
        m_trashBar->setGeometry(0, 12, std::max(0, contentWidth), m_trashBar->height());

    int y = top;
    for (const Section& sec : sections) {
        if (sec.items.isEmpty())
            continue;
        if (sec.label) {
            sec.label->setGeometry(int((colWidth - cardW) / 2.0) + 8, y, 300, sec.label->height());
            y += sec.label->height() + 6;
        }
        int index = 0;
        for (QWidget* w : sec.items) {
            const int row = index / columns;
            const int col = index % columns;
            w->setGeometry(int(col * colWidth + (colWidth - cardW) / 2.0), y + row * (cardH + rowGap), cardW, cardH);
            if (!w->property("finilyHideUntilLanded").toBool())
                w->show();
            ++index;
        }
        const int rows = (index + columns - 1) / columns;
        y += rows * (cardH + rowGap) + 6;
    }

    m_gridContent->setMinimumHeight(y + 24);
    if (m_firstRunHint && m_firstRunHint->isVisible()) {
        const QRect a = createMenuAnchor();
        m_firstRunHint->move(a.center().x() - m_firstRunHint->width() + 4, a.bottom() + 6);
        m_firstRunHint->raise();
    }
    if (m_emptyState && !m_emptyState->isHidden()) {
        const int h = m_emptyState->preferredHeight();
        const int avail = m_scroll->viewport()->height();
        m_emptyState->setGeometry(0, std::max(40, (avail - h) / 2 - 30), std::max(0, contentWidth), h);
    }
}

// ------------------------------------------------------------ selección y arrastre

QString HomeScreen::keyForDoc(const QString& absPath) {
    return QStringLiteral("doc:") + absPath;
}

QString HomeScreen::keyForFolder(const QString& relPath) {
    return QStringLiteral("folder:") + relPath;
}

QString HomeScreen::keyForCard(const ObjectCard* card) const {
    if (const auto* fc = qobject_cast<const FolderCard*>(card))
        return keyForFolder(fc->meta().relativePath);
    if (const auto* nc = qobject_cast<const NotebookCard*>(card))
        return keyForDoc(nc->filePath());
    return {};
}

QVector<ObjectCard*> HomeScreen::orderedCards() const {
    QVector<ObjectCard*> all;
    for (FolderCard* fc : m_folderCards) {
        if (fc && !fc->isHidden())
            all.append(fc);
    }
    for (NotebookCard* nc : m_cards) {
        if (nc && !nc->isHidden())
            all.append(nc);
    }
    std::sort(all.begin(), all.end(), [](const ObjectCard* a, const ObjectCard* b) {
        if (a->y() != b->y())
            return a->y() < b->y();
        return a->x() < b->x();
    });
    return all;
}

void HomeScreen::handleCardSelectionClick(ObjectCard* card, Qt::KeyboardModifiers mods) {
    const QString key = keyForCard(card);
    if (key.isEmpty())
        return;
    if (mods & Qt::ShiftModifier) {
        const auto all = orderedCards();
        int fromIdx = -1, toIdx = -1;
        for (int i = 0; i < all.size(); ++i) {
            const QString k = keyForCard(all.at(i));
            if (k == m_lastClickedKey) fromIdx = i;
            if (k == key) toIdx = i;
        }
        if (fromIdx >= 0 && toIdx >= 0) {
            if (fromIdx > toIdx)
                std::swap(fromIdx, toIdx);
            if (!(mods & Qt::ControlModifier))
                m_selectedKeys.clear();
            for (int i = fromIdx; i <= toIdx; ++i)
                m_selectedKeys.insert(keyForCard(all.at(i)));
        } else {
            m_selectedKeys.insert(key);
        }
    } else if (mods & Qt::ControlModifier) {
        if (m_selectedKeys.contains(key))
            m_selectedKeys.remove(key);
        else
            m_selectedKeys.insert(key);
        m_lastClickedKey = key;
    } else {
        m_selectedKeys.clear();
        m_selectedKeys.insert(key);
        m_lastClickedKey = key;
    }
    syncCardSelectionVisuals();
}

void HomeScreen::selectAllVisible() {
    m_selectedKeys.clear();
    for (ObjectCard* card : orderedCards()) {
        const QString k = keyForCard(card);
        if (!k.isEmpty())
            m_selectedKeys.insert(k);
    }
    syncCardSelectionVisuals();
}

void HomeScreen::clearSelection() {
    if (m_selectedKeys.isEmpty())
        return;
    m_selectedKeys.clear();
    syncCardSelectionVisuals();
}

void HomeScreen::syncCardSelectionVisuals() {
    for (FolderCard* fc : m_folderCards) {
        if (fc)
            fc->setSelected(m_selectedKeys.contains(keyForFolder(fc->meta().relativePath)));
    }
    for (NotebookCard* nc : m_cards) {
        if (nc)
            nc->setSelected(m_selectedKeys.contains(keyForDoc(nc->filePath())));
    }
}

void HomeScreen::startInternalDrag(ObjectCard* sourceCard, const QPoint& globalPos) {
    if (!sourceCard || m_settingsOpen || m_viewMode == ViewMode::Trash)
        return;
    const QString sourceKey = keyForCard(sourceCard);
    if (sourceKey.isEmpty())
        return;

    m_dragDocPaths.clear();
    m_dragFolderRels.clear();
    if (m_selectedKeys.contains(sourceKey)) {
        for (FolderCard* fc : m_folderCards) {
            if (fc && m_selectedKeys.contains(keyForFolder(fc->meta().relativePath)))
                m_dragFolderRels.append(fc->meta().relativePath);
        }
        for (NotebookCard* nc : m_cards) {
            if (nc && m_selectedKeys.contains(keyForDoc(nc->filePath())))
                m_dragDocPaths.append(nc->filePath());
        }
    } else {
        m_selectedKeys.clear();
        m_selectedKeys.insert(sourceKey);
        syncCardSelectionVisuals();
        if (auto* fc = qobject_cast<FolderCard*>(sourceCard))
            m_dragFolderRels.append(fc->meta().relativePath);
        else if (auto* nc = qobject_cast<NotebookCard*>(sourceCard))
            m_dragDocPaths.append(nc->filePath());
    }

    if (m_dragDocPaths.isEmpty() && m_dragFolderRels.isEmpty())
        return;

    m_internalDragging = true;
    m_dragSourceCard = sourceCard;
    m_dragSnap = sourceCard->grabVisualPixmap();
    sourceCard->installEventFilter(this);

    for (FolderCard* fc : m_folderCards) {
        if (fc && m_dragFolderRels.contains(fc->meta().relativePath))
            fc->setDimmed(true);
    }
    for (NotebookCard* nc : m_cards) {
        if (nc && m_dragDocPaths.contains(nc->filePath()))
            nc->setDimmed(true);
    }

    updateInternalDrag(globalPos);
}

void HomeScreen::updateDropTargetHighlight(const QPoint& globalPos, QString* outFolderRel,
                                           QString* outLabel) {
    QString targetRel;
    QString targetLabel;

    if (m_sidebar && m_sidebar->isVisible()) {
        const QString sbRel = m_sidebar->folderAtGlobalPos(globalPos);
        if (!sbRel.isEmpty()) {
            targetRel = sbRel;
            targetLabel = QFileInfo(sbRel).fileName();
        }
    }
    if (targetRel.isEmpty()) {
        for (FolderCard* fc : m_folderCards) {
            if (!fc || !fc->isVisible())
                continue;
            const QPoint lp = fc->mapFromGlobal(globalPos);
            if (fc->rect().contains(lp) && !m_dragFolderRels.contains(fc->meta().relativePath)) {
                targetRel = fc->meta().relativePath;
                targetLabel = fc->meta().name;
                break;
            }
        }
    }

    // Evitar mover una carpeta dentro de sí misma o un descendiente.
    if (!targetRel.isEmpty()) {
        for (const QString& draggedFolder : m_dragFolderRels) {
            if (targetRel == draggedFolder || targetRel.startsWith(draggedFolder + QLatin1Char('/'))) {
                targetRel.clear();
                targetLabel.clear();
                break;
            }
        }
    }

    for (FolderCard* fc : m_folderCards) {
        if (fc)
            fc->setDropTarget(!targetRel.isEmpty() && fc->meta().relativePath == targetRel);
    }
    if (m_sidebar)
        m_sidebar->setDropHighlightFolder(targetRel);
    if (outFolderRel)
        *outFolderRel = targetRel;
    if (outLabel)
        *outLabel = targetLabel;
}

void HomeScreen::updateInternalDrag(const QPoint& globalPos) {
    if (!m_internalDragging || !m_dragGhost)
        return;
    QString targetRel, targetLabel;
    updateDropTargetHighlight(globalPos, &targetRel, &targetLabel);
    const int count = int(m_dragDocPaths.size() + m_dragFolderRels.size());
    m_dragGhost->updateDrag(m_dragSnap, mapFromGlobal(globalPos), targetLabel, count, m_dark);
}

void HomeScreen::finishInternalDrag(const QPoint& globalPos, bool cancelled) {
    if (!m_internalDragging)
        return;
    m_internalDragging = false;
    if (m_dragSourceCard) {
        m_dragSourceCard->removeEventFilter(this);
        m_dragSourceCard = nullptr;
    }

    QString targetRel, targetLabel;
    if (!cancelled)
        updateDropTargetHighlight(globalPos, &targetRel, &targetLabel);

    for (FolderCard* fc : m_folderCards) {
        if (fc) {
            fc->setDimmed(false);
            fc->setDropTarget(false);
        }
    }
    for (NotebookCard* nc : m_cards) {
        if (nc)
            nc->setDimmed(false);
    }
    if (m_sidebar)
        m_sidebar->setDropHighlightFolder(QString());
    if (m_dragGhost)
        m_dragGhost->clear();

    const QStringList docs = m_dragDocPaths;
    const QStringList folders = m_dragFolderRels;
    m_dragDocPaths.clear();
    m_dragFolderRels.clear();
    m_dragSnap = QPixmap();

    if (!cancelled && !targetRel.isEmpty() && (!docs.isEmpty() || !folders.isEmpty())) {
        PendingMoveTarget t;
        t.docPaths = docs;
        t.folderRels = folders;
        t.label = (docs.size() + folders.size() == 1)
            ? (docs.size() == 1 ? QFileInfo(docs.first()).completeBaseName() : QFileInfo(folders.first()).fileName())
            : QStringLiteral("%1 elementos").arg(docs.size() + folders.size());
        executeMoveTarget(t, targetRel, true);
    }
}

void HomeScreen::dragEnterEvent(QDragEnterEvent* event) {
    if (m_settingsOpen || m_viewMode == ViewMode::Trash)
        return;
    const QStringList files = droppedLocalFiles(event->mimeData());
    if (files.isEmpty())
        return;
    event->acceptProposedAction();
    if (m_dropZone && m_scroll) {
        const QString target = (m_viewMode == ViewMode::Folder)
            ? QFileInfo(m_currentFolder).fileName()
            : QStringLiteral("Documentos");
        m_dropZone->setTargetName(target);
        m_dropZone->setDark(m_dark);
        m_dropZone->setGeometry(QRect(m_scroll->mapTo(this, QPoint(0, 0)), m_scroll->size()));
        m_dropZone->show();
        m_dropZone->raise();
    }
}

void HomeScreen::dragMoveEvent(QDragMoveEvent* event) {
    if (!droppedLocalFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void HomeScreen::dragLeaveEvent(QDragLeaveEvent*) {
    if (m_dropZone)
        m_dropZone->hide();
}

void HomeScreen::dropEvent(QDropEvent* event) {
    if (m_dropZone)
        m_dropZone->hide();
    const QStringList files = droppedLocalFiles(event->mimeData());
    if (files.isEmpty())
        return;
    event->acceptProposedAction();
    const QString folder = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
    const QString targetDir = LibraryStore::resolveDirectory(folder);
    for (const QString& file : files) {
        const QString ext = QFileInfo(file).suffix().toLower();
        if (ext == QLatin1String("pdf")) {
            startPdfImport(file, folder);
        } else if (ext == QLatin1String("finily")) {
            if (!targetDir.isEmpty()) {
                QDir().mkpath(targetDir);
                const QString dst = LibraryStore::uniqueNotebookPath(
                    targetDir, LibraryStore::sanitizeBaseName(QFileInfo(file).completeBaseName()));
                if (QFile::copy(file, dst)) {
                    refreshLibrary();
                    if (m_toast)
                        m_toast->showMessage(QStringLiteral("«%1» importado").arg(QFileInfo(dst).completeBaseName()),
                                             QStringLiteral("Abrir"), [this, dst] { emit openNotebookRequested(dst); });
                }
            }
        } else {
            importExternalImageFile(file, folder);
        }
    }
}

bool HomeScreen::eventFilter(QObject* watched, QEvent* event) {
    if (m_internalDragging && watched == m_dragSourceCard) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            updateInternalDrag(me->globalPosition().toPoint());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            finishInternalDrag(me->globalPosition().toPoint(), false);
            return true;
        }
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            finishInternalDrag(QCursor::pos(), true);
            return true;
        }
    }

    if (watched == m_gridContent && !m_settingsOpen && !m_internalDragging) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            const QPoint pos = me->pos();
            QWidget* hit = m_gridContent->childAt(pos);
            if (!hit || hit == m_gridContent || hit == m_emptyState) {
                if (me->button() == Qt::LeftButton) {
                    if (!(me->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
                        clearSelection();
                    if (m_viewMode != ViewMode::Trash) {
                        m_marqueeActive = true;
                        m_marqueeOrigin = pos;
                        if (m_marquee) {
                            m_marquee->setGeometry(m_gridContent->rect());
                            m_marquee->setSelectionRect(QRect(pos, pos), m_dark);
                            m_marquee->show();
                            m_marquee->raise();
                        }
                    }
                } else if (me->button() == Qt::RightButton && m_viewMode != ViewMode::Trash) {
                    showEmptySpaceMenu(me->globalPosition().toPoint());
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove && m_marqueeActive) {
            auto* me = static_cast<QMouseEvent*>(event);
            const QRect r = QRect(m_marqueeOrigin, me->pos()).normalized();
            if (m_marquee)
                m_marquee->setSelectionRect(r, m_dark);
            m_selectedKeys.clear();
            for (ObjectCard* card : orderedCards()) {
                if (card->geometry().intersects(r)) {
                    const QString k = keyForCard(card);
                    if (!k.isEmpty())
                        m_selectedKeys.insert(k);
                }
            }
            syncCardSelectionVisuals();
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_marqueeActive) {
            m_marqueeActive = false;
            if (m_marquee)
                m_marquee->hide();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QHash<QString, QPoint> HomeScreen::captureCardPositions() const {
    QHash<QString, QPoint> map;
    for (FolderCard* fc : m_folderCards) {
        if (fc && !fc->isHidden())
            map.insert(keyForFolder(fc->meta().relativePath), fc->pos());
    }
    for (NotebookCard* nc : m_cards) {
        if (nc && !nc->isHidden())
            map.insert(keyForDoc(nc->filePath()), nc->pos());
    }
    return map;
}

QVector<QPair<QString, QWidget*>> HomeScreen::currentCardWidgets() const {
    QVector<QPair<QString, QWidget*>> out;
    for (FolderCard* fc : m_folderCards) {
        if (fc && !fc->isHidden())
            out.append({keyForFolder(fc->meta().relativePath), fc});
    }
    for (NotebookCard* nc : m_cards) {
        if (nc && !nc->isHidden())
            out.append({keyForDoc(nc->filePath()), nc});
    }
    return out;
}

void HomeScreen::refreshLibraryWithFlip(const QHash<QString, QPoint>& beforePos, int staggerMs) {
    refreshLibrary();
    if (!m_skipNextFlip)
        animateGridFlip(beforePos, currentCardWidgets(), staggerMs);
}

void HomeScreen::openCreateMenu() {
    if (!m_createMenu)
        return;
    if (m_createMenu->isVisible()) {
        m_createMenu->closeMenu();
        return;
    }
    const QString folderName = (m_viewMode == ViewMode::Folder)
        ? QFileInfo(m_currentFolder).fileName()
        : QString();
    m_createMenu->openAnchoredTo(createMenuAnchor(), folderName);
}

QRect HomeScreen::createMenuAnchor() const {
    if (!m_header)
        return {};
    const QRect r = m_header->newButtonGeometry();
    return QRect(m_header->mapTo(this, r.topLeft()), r.size());
}

void HomeScreen::launchQuickNote() {
    emit createDocumentRequested(LibraryStore::lastUsedQuickNoteSpec());
}

void HomeScreen::showUpdateBanner(const QString& version, bool canInstallInApp, const QString& summary) {
    if (m_updateDismissed)
        return;
    if (m_updateBanner)
        m_updateBanner->showUpdate(version, canInstallInApp, summary);
    QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

void HomeScreen::setUpdateDownloadProgress(int percent) {
    if (m_updateBanner)
        m_updateBanner->setDownloadProgress(percent);
}

void HomeScreen::showUpdateReadyToInstall(const QString& version) {
    if (m_updateBanner)
        m_updateBanner->showReadyToInstall(version);
}

void HomeScreen::showUpdateError(const QString& message) {
    if (m_updateBanner)
        m_updateBanner->showError(message);
}

void HomeScreen::resetUpdateBannerState() {
    if (m_updateBanner)
        m_updateBanner->resetState();
}

void HomeScreen::hideUpdateBanner() {
    m_updateDismissed = true;
    if (m_updateBanner)
        m_updateBanner->hideBanner();
    if (m_sidebar)
        m_sidebar->setUpdateDotVisible(true);
    if (m_header)
        m_header->setUpdateDotVisible(true);
    QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

void HomeScreen::showErrorToast(const QString& text) {
    showError(text);
}

void HomeScreen::showInfoToast(const QString& text, const QString& icon) {
    if (m_toast)
        m_toast->showMessage(text, {}, {}, 3500, icon);
}

} // namespace finily
