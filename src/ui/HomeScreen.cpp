#include "ui/HomeScreen.h"

#include "core/Utils.h"
#include "ui/home/CreateMenu.h"
#include "ui/home/FolderCards.h"
#include "ui/home/HomeHeader.h"
#include "ui/home/HomeMenu.h"
#include "ui/home/HomeSidebar.h"
#include "ui/home/HomeTheme.h"
#include "ui/home/LibraryViews.h"
#include "ui/home/NewBoardSheet.h"
#include "ui/home/NewFolderSheet.h"
#include "ui/home/NewNotebookSheet.h"
#include "ui/home/NotebookCards.h"
#include "ui/home/SettingsView.h"
#include "ui/home/TrashCards.h"
#include "ui/home/UpdateBanner.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QStyleHints>
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

    {
        const int mode = SettingsView::themeMode();
        m_dark = mode == SettingsView::System
                     ? QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
                     : mode == SettingsView::Dark;
    }
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (SettingsView::themeMode() == SettingsView::System)
            applyThemeMode(SettingsView::System);
    });
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
            else if (snapSheet == QLatin1String("settings")) openSettings();
            else if (snapSheet == QLatin1String("settings-storage")) { openSettings(); QTimer::singleShot(50, this, [this] { m_settings->showSection(2); }); }
            else if (snapSheet.startsWith(QLatin1String("search:"))) {
                m_header->openSearch();
                findChild<QLineEdit*>()->setText(snapSheet.mid(7));
            }
            else if (snapSheet == QLatin1String("filter")) openFilterPanel();
            else if (snapSheet == QLatin1String("filtered")) {
                LibraryFilter f; f.kind = LibraryFilter::Notebooks; f.date = LibraryFilter::LastWeek;
                setLibraryFilter(f);
            }
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
    connect(m_sidebar, &HomeSidebar::darkModeToggled, this, [this](bool dark) {
        // La luna fija el tema a mano (deja de seguir al sistema).
        SettingsView::setThemeMode(dark ? SettingsView::Dark : SettingsView::Light);
        setDarkMode(dark);
        if (m_settings && m_settingsOpen)
            m_settings->reload();
    });
    connect(m_sidebar, &HomeSidebar::settingsRequested, this, &HomeScreen::openSettings);
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

    m_chipsBar = new FilterChipsBar(mainPane);
    connect(m_chipsBar, &FilterChipsBar::filterChanged, this, &HomeScreen::setLibraryFilter);
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
    m_scroll->setWidget(m_gridContent);
    body->addWidget(m_scroll, 1);

    m_settings = new SettingsView(mainPane);
    m_settings->hide();
    connect(m_settings, &SettingsView::themeModeChanged, this, &HomeScreen::applyThemeMode);
    connect(m_settings, &SettingsView::pressureSensitivityChanged, this, &HomeScreen::pressureSensitivityChanged);
    connect(m_settings, &SettingsView::checkUpdatesRequested, this, [this] {
        if (m_toast)
            m_toast->showMessage(QStringLiteral("Buscando actualizaciones…"));
        emit checkUpdatesRequested();
    });
    connect(m_settings, &SettingsView::closeRequested, this, [this] {
        if (m_sidebar)
            m_sidebar->setSelectedFilter(0);
        selectFilter(0);
    });
    mainCol->addWidget(m_settings, 1);

    m_emptyState = new EmptyStateView(m_gridContent);
    m_emptyState->hide();
    m_firstRunHint = new FirstRunHint(this);
    m_firstRunHint->hide();
    m_filterPanel = new FilterPanel(this);
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &HomeScreen::setLibraryFilter);

    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this] { m_header->openSearch(); });

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
    if (m_emptyState)
        m_emptyState->applyTheme(pal, m_dark);
    if (m_firstRunHint)
        m_firstRunHint->applyTheme(pal);
    if (m_chipsBar)
        m_chipsBar->applyTheme(pal, m_dark);
    if (m_filterPanel)
        m_filterPanel->applyTheme(pal, m_dark);
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
    update();
}

void HomeScreen::setDarkMode(bool dark) {
    if (m_dark == dark)
        return;
    m_dark = dark;
    writeAppSetting(QStringLiteral("home/darkMode"), dark);
    applyTheme();
}

void HomeScreen::applyThemeMode(int mode) {
    const bool dark = mode == SettingsView::System
                          ? QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
                          : mode == SettingsView::Dark;
    setDarkMode(dark);
}

void HomeScreen::openSettings() {
    if (!m_settings)
        return;
    m_settingsOpen = true;
    if (m_filterPanel)
        m_filterPanel->closePanel();
    if (m_sidebar)
        m_sidebar->setSelectedFolder(QString());
    m_header->setLibraryControlsVisible(false);
    m_header->setSectionTitle(QStringLiteral("Ajustes"));
    m_updateBanner->setProperty("finilyWasVisible", m_updateBanner->isVisible());
    for (QWidget* w : {static_cast<QWidget*>(m_scroll), static_cast<QWidget*>(m_chipsBar),
                       static_cast<QWidget*>(m_updateBanner), static_cast<QWidget*>(m_firstRunHint)})
        w->hide();
    m_settings->show();
    m_settings->setFocus();
    // Tras mostrarla, para que el índice lea posiciones ya maquetadas.
    QTimer::singleShot(0, m_settings, [this] { m_settings->reload(); });
}

void HomeScreen::leaveSettings() {
    if (!m_settingsOpen)
        return;
    m_settingsOpen = false;
    m_settings->hide();
    m_scroll->show();
    if (m_updateBanner->property("finilyWasVisible").toBool())
        m_updateBanner->show();
    m_header->setLibraryControlsVisible(true);
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
    // Qué carpetas y documentos entran: vista normal, o resultados de búsqueda
    // (toda la biblioteca, o solo el subárbol de la carpeta abierta).
    const bool searching = isSearching();
    const QString needle = searching ? m_header->searchText().trimmed() : QString();
    const bool scoped = searchIsScoped();
    const QString scopePrefix = m_currentFolder + QLatin1Char('/');
    auto inScope = [&](const QString& rel) {
        return !scoped || rel == m_currentFolder || rel.startsWith(scopePrefix);
    };
    auto pathLabel = [](const QString& rel) {
        return rel.isEmpty() ? QStringLiteral("Documentos") : QString(rel).replace(QLatin1Char('/'), QStringLiteral(" › "));
    };
    m_libraryEmpty = allDocs.isEmpty() && m_allFolders.isEmpty();

    QVector<FolderMeta> folders;
    if (m_filter.acceptsFolders()) {
        if (searching) {
            for (const FolderMeta& fm : m_allFolders) {
                if (fm.relativePath != m_currentFolder && inScope(fm.relativePath) &&
                    fm.name.contains(needle, Qt::CaseInsensitive))
                    folders.append(fm);
            }
        } else if (m_viewMode == ViewMode::Folder) {
            folders = LibraryStore::listFolders(m_currentFolder);
        } else if (m_filter.kind == LibraryFilter::Folders) {
            folders = m_allFolders;
        }
        if (m_filter.sort == LibraryFilter::Name) {
            std::stable_sort(folders.begin(), folders.end(), [](const FolderMeta& a, const FolderMeta& b) {
                return QString::localeAwareCompare(a.name, b.name) < 0;
            });
        }
    }
    for (const FolderMeta& fm : folders) {
        auto* fc = new FolderCard(fm, m_gridContent);
        fc->setPalette(pal, m_dark);
        fc->setHidden(false);
        if (searching) {
            fc->setHighlight(needle);
            fc->setDetailOverride(pathLabel(QFileInfo(fm.relativePath).path() == QLatin1String(".")
                                                ? QString()
                                                : QFileInfo(fm.relativePath).path()));
        }
        connect(fc, &FolderCard::openRequested, this, [this](const QString& rel) {
            if (m_header)
                m_header->closeSearch(); // la búsqueda se cierra al navegar
            openFolder(rel);
        });
        const QPointer<FolderCard> guard(fc);
        connect(fc, &ObjectCard::menuRequested, this, [this, guard](const QPoint& pos) {
            if (guard) showFolderMenu(guard->meta(), pos, guard.data());
        });
        connect(fc, &ObjectCard::renameCommitted, this, [this, rel = fm.relativePath](const QString& name) {
            QTimer::singleShot(0, this, [this, rel, name] { renameFolderTo(rel, name); });
        });
        m_folderCards.append(fc);
    }

    QVector<NotebookMeta> visibleDocs;
    if (searching) {
        for (const NotebookMeta& meta : allDocs) {
            if (inScope(meta.folderRelativePath) && meta.title.contains(needle, Qt::CaseInsensitive))
                visibleDocs.append(meta);
        }
    } else {
        visibleDocs = (m_viewMode == ViewMode::Folder) ? LibraryStore::listNotebooks(m_currentFolder) : allDocs;
    }
    visibleDocs.erase(std::remove_if(visibleDocs.begin(), visibleDocs.end(),
                                     [this](const NotebookMeta& m) { return !m_filter.acceptsDocument(m); }),
                      visibleDocs.end());
    sortDocuments(visibleDocs, m_filter.sort);
    for (const NotebookMeta& meta : visibleDocs) {
        auto* card = new NotebookCard(meta, m_gridContent);
        card->setPalette(pal, m_dark);
        if (searching) {
            card->setHighlight(needle);
            card->setDetailOverride(pathLabel(meta.folderRelativePath));
        }
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

    clearGroupLabels();
    if (searching) {
        if (!m_folderCards.isEmpty())
            m_groupLabels.append(new ResultGroupLabel(QStringLiteral("Carpetas"), int(m_folderCards.size()), m_gridContent));
        if (!m_cards.isEmpty())
            m_groupLabels.append(new ResultGroupLabel(QStringLiteral("Documentos"), int(m_cards.size()), m_gridContent));
        for (ResultGroupLabel* gl : m_groupLabels) {
            gl->applyTheme(pal);
            gl->show();
        }
    }

    applySearchFilter(m_header ? m_header->searchText() : QString());
}

void HomeScreen::selectFilter(int index) {
    leaveSettings();
    m_viewMode = index == 3 ? ViewMode::Trash : index == 1 ? ViewMode::Recientes : ViewMode::Todos;
    m_currentFolder.clear();
    m_searchWhole = false;
    refreshLibrary();
}

void HomeScreen::openFolder(const QString& relativePath) {
    leaveSettings();
    const QString norm = LibraryStore::normalizeRelativeFolder(relativePath);
    if (norm.isEmpty()) {
        selectFilter(0);
        return;
    }
    m_viewMode = ViewMode::Folder;
    m_currentFolder = norm;
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
    refreshLibrary();
}

void HomeScreen::setLibraryFilter(const LibraryFilter& filter) {
    m_filter = filter;
    refreshLibrary();
}

void HomeScreen::openFilterPanel() {
    if (!m_filterPanel || !m_header)
        return;
    if (m_filterPanel->isVisible()) {
        m_filterPanel->closePanel();
        return;
    }
    const QRect r = m_header->filterButtonGeometry();
    m_filterPanel->popup(QRect(m_header->mapTo(this, r.topLeft()), r.size()), m_filter, false);
}

void HomeScreen::openFirstResult() {
    if (!m_folderCards.isEmpty())
        m_folderCards.first()->open();
    else if (!m_cards.isEmpty())
        m_cards.first()->open();
}

void HomeScreen::clearGroupLabels() {
    for (ResultGroupLabel* gl : m_groupLabels) {
        if (gl) gl->deleteLater();
    }
    m_groupLabels.clear();
}

void HomeScreen::applySearchFilter(const QString& text) {
    const QString needle = m_viewMode == ViewMode::Trash ? text.trimmed() : QString();
    for (TrashCard* tc : m_trashCards) {
        if (tc)
            tc->setHidden(!tc->matchesSearch(needle));
    }
    const int docFilter = (m_viewMode == ViewMode::Recientes) ? 1 : 0;
    for (NotebookCard* card : m_cards) {
        if (card)
            card->setHidden(!card->matchesFilter(docFilter));
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

    if (m_header) {
        const bool trash = m_viewMode == ViewMode::Trash;
        m_header->setFilterVisible(!trash);
        m_header->setFilterActive(m_filter.activeCount() > 0);
        m_header->setSearchScope(isSearching() && searchIsScoped() ? QFileInfo(m_currentFolder).fileName() : QString());
    }
    if (m_chipsBar) {
        m_chipsBar->setFilter(m_filter);
        m_chipsBar->setVisible(!m_settingsOpen && m_viewMode != ViewMode::Trash && m_chipsBar->hasChips());
    }
    configureEmptyState(totalVisible);
    if (m_settingsOpen && m_header) {
        // Un refresco de la biblioteca no debe tapar la vista de Ajustes.
        m_header->setSectionTitle(QStringLiteral("Ajustes"));
        m_header->setLibraryControlsVisible(false);
        if (m_firstRunHint)
            m_firstRunHint->hide();
    }
}

void HomeScreen::configureEmptyState(int totalVisible) {
    if (!m_emptyState)
        return;
    const bool firstRun = totalVisible == 0 && m_libraryEmpty && m_viewMode == ViewMode::Todos && !isSearching();
    if (m_firstRunHint) {
        m_firstRunHint->setVisible(firstRun && !m_settingsOpen);
    }
    m_emptyState->setHidden(totalVisible != 0);
    if (totalVisible != 0)
        return;

    const bool filtered = m_filter.activeCount() > 0;
    auto clearFilter = [this] { setLibraryFilter(LibraryFilter{}); };
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
        m_emptyState->setContent(QStringLiteral("trash"), QStringLiteral("La papelera está vacía"),
                                 QStringLiteral("Lo que borres se guarda aquí durante <b>30 días</b> por si cambias de idea."));
    } else if (filtered) {
        m_emptyState->setContent(QStringLiteral("search"), QStringLiteral("Nada coincide con el filtro"),
                                 QStringLiteral("Ningún elemento de esta vista cumple lo que elegiste."),
                                 {{QStringLiteral("Quitar filtro"), clearFilter}});
    } else if (m_viewMode == ViewMode::Folder) {
        m_emptyState->setContent(
            QStringLiteral("folder"), QStringLiteral("%1 está vacía").arg(QFileInfo(m_currentFolder).fileName()),
            QStringLiteral("Arrastra documentos hasta aquí o usa <b>+&nbsp;Nuevo</b>: lo que crees se guardará en esta carpeta."));
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

    // Secciones: en búsqueda, «CARPETAS» y «DOCUMENTOS» con su rótulo; si no, una sola rejilla.
    struct Section {
        ResultGroupLabel* label = nullptr;
        QList<QWidget*> items;
    };
    QVector<Section> sections;
    Section folders, docs;
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
    if (!m_groupLabels.isEmpty()) {
        int gi = 0;
        if (!folders.items.isEmpty()) folders.label = m_groupLabels.value(gi++);
        if (!docs.items.isEmpty()) docs.label = m_groupLabels.value(gi++);
        sections << folders << docs;
    } else {
        folders.items.append(docs.items);
        sections << folders;
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
