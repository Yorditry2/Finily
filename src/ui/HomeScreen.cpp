#include "ui/HomeScreen.h"

#include "core/Utils.h"
#include "ui/home/CreateMenu.h"
#include "ui/home/FolderCards.h"
#include "ui/home/HomeHeader.h"
#include "ui/home/HomeMenu.h"
#include "ui/home/HomeSidebar.h"
#include "ui/home/HomeTheme.h"
#include "ui/home/NewBoardSheet.h"
#include "ui/home/NewFolderSheet.h"
#include "ui/home/NewNotebookSheet.h"
#include "ui/home/NotebookCards.h"
#include "ui/home/TrashCards.h"
#include "ui/home/UpdateBanner.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {

HomeScreen::HomeScreen(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finilyHome"));
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    m_dark = appSettings()->value(QStringLiteral("home/darkMode"), false).toBool();
    // La papelera conserva 30 días; lo vencido se borra al abrir el programa.
    LibraryStore::purgeExpiredTrash();

    buildLayout();
    buildOverlays();
    applyTheme();
    refreshLibrary();

    // Solo desarrollo: con FINILY_SNAPSHOT, FINILY_SNAPSHOT_FOLDER abre esa carpeta.
    const QString snapFolder = qEnvironmentVariable("FINILY_SNAPSHOT_FOLDER");
    if (!snapFolder.isEmpty())
        QTimer::singleShot(300, this, [this, snapFolder] { openFolder(snapFolder); });
    // Solo desarrollo: FINILY_SNAPSHOT_SHEET=menu|board|notebook|folder abre esa superficie.
    const QString snapSheet = qEnvironmentVariable("FINILY_SNAPSHOT_SHEET");
    if (!snapSheet.isEmpty()) {
        QTimer::singleShot(500, this, [this, snapSheet] {
            const HomePalette pal = homePaletteFor(m_dark);
            if (snapSheet == QLatin1String("menu")) openCreateMenu();
            else if (snapSheet == QLatin1String("board")) m_boardSheet->openCreate({}, pal, m_dark);
            else if (snapSheet == QLatin1String("notebook")) m_notebookSheet->openCreate({}, pal, m_dark);
            else if (snapSheet == QLatin1String("folder")) m_folderSheet->openCreate({}, pal, m_dark);
            else if (snapSheet == QLatin1String("ctxdoc") && !m_cards.isEmpty())
                showNotebookMenu(m_cards.first(), m_cards.first()->mapToGlobal(QPoint(120, 60)));
            else if (snapSheet == QLatin1String("ctxfolder") && !m_folderCards.isEmpty())
                showFolderMenu(m_folderCards.first()->meta(),
                               m_folderCards.first()->mapToGlobal(QPoint(120, 60)), m_folderCards.first());
            else if (snapSheet == QLatin1String("rename") && !m_cards.isEmpty()) m_cards.first()->beginRename();
            else if (snapSheet == QLatin1String("toast"))
                m_toast->showMessage(QStringLiteral("«Física» se movió a la papelera"), QStringLiteral("Deshacer"), [] {});
            else if (snapSheet == QLatin1String("trash")) selectFilter(3);
            else if (snapSheet == QLatin1String("confirm")) { selectFilter(3); askEmptyTrash(); }
        });
    }
}

void HomeScreen::buildLayout() {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_sidebar = new HomeSidebar(this);
    connect(m_sidebar, &HomeSidebar::filterSelected, this, &HomeScreen::selectFilter);
    connect(m_sidebar, &HomeSidebar::folderSelected, this, &HomeScreen::openFolder);
    connect(m_sidebar, &HomeSidebar::folderMenuRequested, this,
            [this](const FolderMeta& meta, const QPoint& pos) { showFolderMenu(meta, pos, nullptr); });
    connect(m_sidebar, &HomeSidebar::collapseRequested, this, [this] { setSidebarCollapsed(true); });
    connect(m_sidebar, &HomeSidebar::continueDraftRequested, this,
            &HomeScreen::continueDraftRequested);
    connect(m_sidebar, &HomeSidebar::darkModeToggled, this, &HomeScreen::setDarkMode);
    root->addWidget(m_sidebar);

    auto* mainPane = new QWidget(this);
    mainPane->setAttribute(Qt::WA_StyledBackground, false);
    auto* mainCol = new QVBoxLayout(mainPane);
    mainCol->setContentsMargins(0, 0, 0, 0);
    mainCol->setSpacing(0);

    m_header = new HomeHeader(mainPane);
    connect(m_header, &HomeHeader::searchTextChanged, this, &HomeScreen::applySearchFilter);
    connect(m_header, &HomeHeader::breadcrumbClicked, this, [this](const QString& rel) {
        if (rel.isEmpty())
            selectFilter(0);
        else
            openFolder(rel);
    });
    connect(m_header, &HomeHeader::newRequested, this, &HomeScreen::openCreateMenu);
    connect(m_header, &HomeHeader::reopenSidebarRequested, this, [this] { setSidebarCollapsed(false); });
    mainCol->addWidget(m_header);

    // Banner y rejilla van con el margen del contenido (26/28 del mockup).
    auto* body = new QVBoxLayout();
    body->setContentsMargins(28, 0, 14, 0);
    body->setSpacing(0);
    mainCol->addLayout(body, 1);

    m_updateBanner = new UpdateBanner(this);
    connect(m_updateBanner, &UpdateBanner::installRequested, this, &HomeScreen::updateInstallRequested);
    connect(m_updateBanner, &UpdateBanner::cancelRequested, this, &HomeScreen::updateCancelRequested);
    connect(m_updateBanner, &UpdateBanner::releaseNotesRequested, this, &HomeScreen::updateReleaseNotesRequested);
    connect(m_updateBanner, &UpdateBanner::dismissed, this, &HomeScreen::updateDismissed);
    body->addSpacing(12);
    body->addWidget(m_updateBanner);
    body->addSpacing(8);

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
    m_scroll->setWidget(m_gridContent);
    body->addWidget(m_scroll, 1);

    m_emptyState = new QWidget(m_gridContent);
    m_emptyState->setAttribute(Qt::WA_StyledBackground, false);
    auto* emptyBox = new QVBoxLayout(m_emptyState);
    emptyBox->setContentsMargins(24, 16, 24, 16);
    m_emptyLabel = new QLabel(m_emptyState);
    m_emptyLabel->setObjectName(QStringLiteral("finilyEmpty"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    emptyBox->addWidget(m_emptyLabel);
    m_emptyState->hide();

    m_trashBar = new TrashInfoBar(m_gridContent);
    connect(m_trashBar, &TrashInfoBar::emptyRequested, this, &HomeScreen::askEmptyTrash);
    m_trashBar->hide();

    root->addWidget(mainPane, 1);
    setSidebarCollapsed(readAppSetting(QStringLiteral("home/sidebarCollapsed"), false).toBool());
}

void HomeScreen::setSidebarCollapsed(bool collapsed) {
    writeAppSetting(QStringLiteral("home/sidebarCollapsed"), collapsed);
    if (m_sidebar)
        m_sidebar->setVisible(!collapsed);
    if (m_header)
        m_header->setSidebarCollapsed(collapsed);
    if (m_toast && m_scroll) {
        const QPoint tl = m_scroll->mapTo(this, QPoint(0, 0));
        m_toast->setArea(QRect(tl.x(), 0, m_scroll->width(), height()));
    }
    QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

void HomeScreen::buildOverlays() {
    m_createMenu = new CreateMenu(this);
    connect(m_createMenu, &CreateMenu::quickNoteChosen, this, &HomeScreen::launchQuickNote);
    auto* quickNoteShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+N")), this);
    connect(quickNoteShortcut, &QShortcut::activated, this, &HomeScreen::launchQuickNote);
    connect(m_createMenu, &CreateMenu::boardChosen, this, [this] {
        const QString folder = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
        m_boardSheet->openCreate(folder, homePaletteFor(m_dark), m_dark);
    });
    connect(m_createMenu, &CreateMenu::notebookChosen, this, [this] {
        const QString folder = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
        m_notebookSheet->openCreate(folder, homePaletteFor(m_dark), m_dark);
    });
    connect(m_createMenu, &CreateMenu::folderChosen, this, [this] {
        const QString parent = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
        m_folderSheet->openCreate(parent, homePaletteFor(m_dark), m_dark);
    });
    connect(m_createMenu, &CreateMenu::importPdfChosen, this, [this] {
        const QString folder = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
        emit importPdfFromHomeRequested(folder);
    });

    m_folderSheet = new NewFolderSheet(this);
    connect(m_folderSheet, &NewFolderSheet::createSubmitted, this,
            [this](const QString& parentRel, const QString& name, int colorIdx, int iconIdx) {
                QString err;
                if (!LibraryStore::createFolder(parentRel, name, colorIdx, iconIdx, nullptr, &err)) {
                    showError(err.isEmpty() ? QStringLiteral("No se pudo crear la carpeta.") : err);
                    return;
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
    connect(m_boardSheet, &NewBoardSheet::boardSubmitted, this,
            &HomeScreen::createDocumentRequested);

    m_notebookSheet = new NewNotebookSheet(this);
    connect(m_notebookSheet, &NewNotebookSheet::notebookSubmitted, this,
            &HomeScreen::createDocumentRequested);
    connect(m_notebookSheet, &NewNotebookSheet::coverEditSubmitted, this,
            [this](const QString& filePath, CoverStyle style) {
                LibraryStore::setCoverStyle(filePath, style);
                refreshLibrary();
            });

    // Capas propias: menú contextual, aviso con «Deshacer» y confirmación irreversible.
    m_menu = new HomeMenu(this);
    m_toast = new HomeToast(this);
    m_confirm = new ConfirmDialog(this);

    auto* renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    connect(renameShortcut, &QShortcut::activated, this, [this] {
        if (ObjectCard* card = hoveredCard())
            card->beginRename();
    });
    auto* trashShortcut = new QShortcut(QKeySequence(QKeySequence::Delete), this);
    connect(trashShortcut, &QShortcut::activated, this, [this] {
        ObjectCard* card = hoveredCard();
        if (!card || card->isRenaming())
            return;
        if (auto* fc = qobject_cast<FolderCard*>(card))
            moveToTrash(LibraryStore::resolveDirectory(fc->meta().relativePath), fc->meta().name, true);
        else if (auto* nc = qobject_cast<NotebookCard*>(card))
            moveToTrash(nc->filePath(), nc->title(), false);
    });
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
    update();
}

void HomeScreen::setDarkMode(bool dark) {
    if (m_dark == dark)
        return;
    m_dark = dark;
    writeAppSetting(QStringLiteral("home/darkMode"), dark);
    applyTheme();
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
                        static_cast<GlassSheet*>(m_notebookSheet)}) {
        if (sheet && sheet->isVisible())
            sheet->repositionOnResize();
    }
    QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

void HomeScreen::refreshLibrary() {
    for (FolderCard* fc : m_folderCards) {
        if (fc) fc->deleteLater();
    }
    m_folderCards.clear();
    for (NotebookCard* card : m_cards) {
        if (card) card->deleteLater();
    }
    m_cards.clear();
    for (TrashCard* tc : m_trashCards) {
        if (tc) tc->deleteLater();
    }
    m_trashCards.clear();

    m_allFolders = LibraryStore::listAllFoldersTree();
    const QVector<NotebookMeta> allDocs = LibraryStore::listAllNotebooksRecursive();

    if (m_viewMode == ViewMode::Folder) {
        const bool exists = std::any_of(m_allFolders.cbegin(), m_allFolders.cend(),
                                        [this](const FolderMeta& f) { return f.relativePath == m_currentFolder; });
        if (!exists) {
            m_viewMode = ViewMode::Todos;
            m_currentFolder.clear();
        }
    }

    if (m_sidebar) {
        m_sidebar->setFolders(m_allFolders);
        m_folderSheet->setFolders(m_allFolders);
        if (m_viewMode == ViewMode::Folder)
            m_sidebar->setSelectedFolder(m_currentFolder);
        else
            m_sidebar->setSelectedFilter(int(m_viewMode));
        m_sidebar->setDraftVisible(LibraryStore::hasRecoverableDraft());
    }

    const HomePalette pal = homePaletteFor(m_dark);
    const QVector<TrashEntry> trash = LibraryStore::listTrash();
    if (m_sidebar)
        m_sidebar->setTrashCount(int(trash.size()));
    if (m_trashBar) {
        m_trashBar->setCount(int(trash.size()));
        m_trashBar->setVisible(m_viewMode == ViewMode::Trash);
    }
    if (m_viewMode == ViewMode::Trash) {
        for (const TrashEntry& entry : trash) {
            auto* tc = new TrashCard(entry, m_gridContent);
            tc->setPalette(pal, m_dark);
            const QPointer<TrashCard> guard(tc);
            connect(tc, &ObjectCard::menuRequested, this, [this, guard](const QPoint& pos) {
                if (guard) showTrashMenu(guard.data(), pos);
            });
            m_trashCards.append(tc);
        }
        applySearchFilter(m_header ? m_header->searchText() : QString());
        return;
    }
    if (m_viewMode == ViewMode::Folder) {
        for (const FolderMeta& fm : LibraryStore::listFolders(m_currentFolder)) {
            auto* fc = new FolderCard(fm, m_gridContent);
            fc->setPalette(pal, m_dark);
            connect(fc, &FolderCard::openRequested, this, &HomeScreen::openFolder);
            const QPointer<FolderCard> guard(fc);
            connect(fc, &ObjectCard::menuRequested, this, [this, guard](const QPoint& pos) {
                if (guard) showFolderMenu(guard->meta(), pos, guard.data());
            });
            connect(fc, &ObjectCard::renameCommitted, this, [this, rel = fm.relativePath](const QString& name) {
                QTimer::singleShot(0, this, [this, rel, name] { renameFolderTo(rel, name); });
            });
            m_folderCards.append(fc);
        }
    }

    const QVector<NotebookMeta> visibleDocs =
        (m_viewMode == ViewMode::Folder) ? LibraryStore::listNotebooks(m_currentFolder) : allDocs;
    for (const NotebookMeta& meta : visibleDocs) {
        auto* card = new NotebookCard(meta, m_gridContent);
        card->setPalette(pal, m_dark);
        connect(card, &NotebookCard::openRequested, this, &HomeScreen::openNotebookRequested);
        const QPointer<NotebookCard> guard(card);
        connect(card, &ObjectCard::menuRequested, this, [this, guard](const QPoint& pos) {
            if (guard) showNotebookMenu(guard.data(), pos);
        });
        connect(card, &ObjectCard::renameCommitted, this, [this, path = meta.path](const QString& name) {
            QTimer::singleShot(0, this, [this, path, name] { renameNotebookTo(path, name); });
        });
        m_cards.append(card);
    }

    applySearchFilter(m_header ? m_header->searchText() : QString());
}

void HomeScreen::selectFilter(int index) {
    m_viewMode = index == 3 ? ViewMode::Trash : index == 1 ? ViewMode::Recientes : ViewMode::Todos;
    m_currentFolder.clear();
    refreshLibrary();
}

void HomeScreen::openFolder(const QString& relativePath) {
    const QString norm = LibraryStore::normalizeRelativeFolder(relativePath);
    if (norm.isEmpty()) {
        selectFilter(0);
        return;
    }
    m_viewMode = ViewMode::Folder;
    m_currentFolder = norm;
    refreshLibrary();
}

void HomeScreen::applySearchFilter(const QString& text) {
    const QString needle = text.trimmed();
    for (FolderCard* fc : m_folderCards) {
        if (fc)
            fc->setHidden(!fc->matchesSearch(needle));
    }
    for (TrashCard* tc : m_trashCards) {
        if (tc)
            tc->setHidden(!tc->matchesSearch(needle));
    }
    const int docFilter = (m_viewMode == ViewMode::Recientes) ? 1 : 0;
    for (NotebookCard* card : m_cards) {
        if (card)
            card->setHidden(!(card->matchesSearch(needle) && card->matchesFilter(docFilter)));
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
    const int totalVisible = visibleFolders + visibleDocs;
    if (m_header) {
        if (m_viewMode == ViewMode::Folder)
            m_header->setFolderBreadcrumb(m_currentFolder);
        else if (m_viewMode == ViewMode::Trash)
            m_header->setSectionTitle(QStringLiteral("Papelera"));
        else
            m_header->setSectionTitle(QStringLiteral("Documentos"));
    }

    // Estados vacíos provisionales; el diseño completo (mockup 08) llega en la fase 6.
    const bool searching = m_header && !m_header->searchText().trimmed().isEmpty();
    if (m_emptyState) {
        m_emptyState->setHidden(totalVisible != 0);
        if (m_emptyLabel) {
            m_emptyLabel->setText(
                searching ? QStringLiteral("No hay elementos que coincidan con la búsqueda")
                : m_viewMode == ViewMode::Trash ? QStringLiteral("La papelera está vacía.")
                : m_viewMode == ViewMode::Folder
                    ? QStringLiteral("Esta carpeta está vacía. Usa «Nuevo» para crear algo aquí.")
                    : QStringLiteral("Tu biblioteca está vacía. Empieza con «Nuevo»."));
        }
    }
}

void HomeScreen::relayoutGrid() {
    if (!m_gridContent || !m_scroll)
        return;
    // Rejilla del mockup: columnas iguales, objetos centrados en su columna.
    constexpr int cardW = ObjectCard::kCardWidth;
    constexpr int cardH = ObjectCard::kCardHeight;
    constexpr int minGap = 12;
    constexpr int rowGap = 18;
    const bool trashView = m_viewMode == ViewMode::Trash && m_trashBar;
    const int top = trashView ? 18 + m_trashBar->height() + 6 : 18;

    const int contentWidth = m_scroll->viewport()->width() - 14;
    const int columns = std::max(1, (contentWidth + minGap) / (cardW + minGap));
    const qreal colWidth = qreal(std::max(contentWidth, cardW)) / columns;

    QList<QWidget*> items;
    for (FolderCard* fc : m_folderCards) {
        if (fc && !fc->isHidden())
            items.append(fc);
    }
    for (NotebookCard* card : m_cards) {
        if (card && !card->isHidden())
            items.append(card);
    }
    for (TrashCard* tc : m_trashCards) {
        if (tc && !tc->isHidden())
            items.append(tc);
    }
    if (trashView)
        m_trashBar->setGeometry(0, 12, std::max(0, contentWidth), m_trashBar->height());

    int index = 0;
    for (QWidget* w : items) {
        const int row = index / columns;
        const int col = index % columns;
        w->setGeometry(int(col * colWidth + (colWidth - cardW) / 2.0), top + row * (cardH + rowGap), cardW, cardH);
        w->show();
        ++index;
    }

    const int rows = std::max(1, (index + columns - 1) / columns);
    m_gridContent->setMinimumHeight(top + rows * (cardH + rowGap) + 24);
    if (m_emptyState && !m_emptyState->isHidden())
        m_emptyState->setGeometry(0, 72, std::max(0, contentWidth), 110);
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

void HomeScreen::showUpdateBanner(const QString& version, bool canInstallInApp) {
    if (m_updateBanner) m_updateBanner->showUpdate(version, canInstallInApp);
    QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

void HomeScreen::setUpdateDownloadProgress(int percent) {
    if (m_updateBanner) m_updateBanner->setDownloadProgress(percent);
}

void HomeScreen::resetUpdateBannerState() {
    if (m_updateBanner) m_updateBanner->resetState();
}

void HomeScreen::hideUpdateBanner() {
    if (m_updateBanner) m_updateBanner->hide();
    QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

} // namespace finily
