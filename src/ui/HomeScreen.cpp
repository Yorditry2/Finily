#include "ui/HomeScreen.h"

#include "core/Utils.h"
#include "ui/home/CreateMenu.h"
#include "ui/home/FolderCards.h"
#include "ui/home/HomeHeader.h"
#include "ui/home/HomeSidebar.h"
#include "ui/home/HomeTheme.h"
#include "ui/home/NewBoardSheet.h"
#include "ui/home/NewFolderSheet.h"
#include "ui/home/NewNotebookSheet.h"
#include "ui/home/NotebookCards.h"
#include "ui/home/UpdateBanner.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
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

    buildLayout();
    buildOverlays();
    applyTheme();
    refreshLibrary();
}

void HomeScreen::buildLayout() {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_sidebar = new HomeSidebar(this);
    connect(m_sidebar, &HomeSidebar::filterSelected, this, &HomeScreen::selectFilter);
    connect(m_sidebar, &HomeSidebar::folderSelected, this, &HomeScreen::openFolder);
    connect(m_sidebar, &HomeSidebar::createRootFolderRequested, this, [this] {
        m_folderSheet->openCreate(QString(), homePaletteFor(m_dark), m_dark);
    });
    connect(m_sidebar, &HomeSidebar::createSubfolderRequested, this, [this](const QString& parentRel) {
        m_folderSheet->openCreate(parentRel, homePaletteFor(m_dark), m_dark);
    });
    connect(m_sidebar, &HomeSidebar::editFolderRequested, this, [this](const FolderMeta& meta) {
        m_folderSheet->openEdit(meta, homePaletteFor(m_dark), m_dark);
    });
    connect(m_sidebar, &HomeSidebar::renameFolderRequested, this, &HomeScreen::renameFolderByPath);
    connect(m_sidebar, &HomeSidebar::deleteFolderRequested, this, &HomeScreen::deleteFolderByPath);
    connect(m_sidebar, &HomeSidebar::openExternalFileRequested, this,
            &HomeScreen::openExternalFileRequested);
    connect(m_sidebar, &HomeSidebar::continueDraftRequested, this,
            &HomeScreen::continueDraftRequested);
    connect(m_sidebar, &HomeSidebar::darkModeToggled, this, &HomeScreen::setDarkMode);
    root->addWidget(m_sidebar);

    auto* mainPane = new QWidget(this);
    mainPane->setAttribute(Qt::WA_StyledBackground, false);
    auto* mainCol = new QVBoxLayout(mainPane);
    mainCol->setContentsMargins(34, 22, 34, 0);
    mainCol->setSpacing(0);

    m_header = new HomeHeader(mainPane);
    connect(m_header, &HomeHeader::searchTextChanged, this, &HomeScreen::applySearchFilter);
    connect(m_header, &HomeHeader::breadcrumbClicked, this, [this](const QString& rel) {
        if (rel.isEmpty())
            selectFilter(0);
        else
            openFolder(rel);
    });
    mainCol->addWidget(m_header);

    m_updateBanner = new UpdateBanner(this);
    connect(m_updateBanner, &UpdateBanner::installRequested, this, &HomeScreen::updateInstallRequested);
    connect(m_updateBanner, &UpdateBanner::cancelRequested, this, &HomeScreen::updateCancelRequested);
    connect(m_updateBanner, &UpdateBanner::releaseNotesRequested, this, &HomeScreen::updateReleaseNotesRequested);
    connect(m_updateBanner, &UpdateBanner::dismissed, this, &HomeScreen::updateDismissed);
    mainCol->addSpacing(12);
    mainCol->addWidget(m_updateBanner);
    mainCol->addSpacing(14);

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
    mainCol->addWidget(m_scroll, 1);

    m_createCard = new CreateTileCard(m_gridContent);
    connect(m_createCard, &CreateTileCard::createClicked, this, &HomeScreen::openCreateMenu);

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

    root->addWidget(mainPane, 1);
}

void HomeScreen::buildOverlays() {
    m_createMenu = new CreateMenu(this);
    connect(m_createMenu, &CreateMenu::quickNoteChosen, this, &HomeScreen::launchQuickNote);
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
                    QMessageBox::warning(this, tr("Nueva carpeta"),
                                         err.isEmpty() ? tr("No se pudo crear la carpeta.") : err);
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
    if (m_createCard)
        m_createCard->setPalette(pal, m_dark);
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
    if (m_createMenu && m_createMenu->isVisible() && m_createCard) {
        const QPoint p = m_createCard->mapTo(this, QPoint(0, 0));
        m_createMenu->reposition(QRect(p, m_createCard->size()));
    }
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

    m_allFolders = LibraryStore::listAllFoldersTree();
    const QVector<NotebookMeta> allDocs = LibraryStore::listAllNotebooksRecursive();
    const QDateTime now = QDateTime::currentDateTime();
    int recentCount = 0;
    for (const NotebookMeta& m : allDocs) {
        if (m.modified.daysTo(now) <= 7)
            ++recentCount;
    }

    if (m_viewMode == ViewMode::Folder) {
        const bool exists = std::any_of(m_allFolders.cbegin(), m_allFolders.cend(),
                                        [this](const FolderMeta& f) { return f.relativePath == m_currentFolder; });
        if (!exists) {
            m_viewMode = ViewMode::Todos;
            m_currentFolder.clear();
        }
    }

    if (m_sidebar) {
        m_sidebar->setCounts(int(allDocs.size()), recentCount);
        m_sidebar->setFolders(m_allFolders);
        if (m_viewMode == ViewMode::Folder)
            m_sidebar->setSelectedFolder(m_currentFolder);
        else
            m_sidebar->setSelectedFilter(int(m_viewMode));
        m_sidebar->setDraftVisible(LibraryStore::hasRecoverableDraft());
    }

    const HomePalette pal = homePaletteFor(m_dark);
    if (m_viewMode == ViewMode::Folder) {
        for (const FolderMeta& fm : LibraryStore::listFolders(m_currentFolder)) {
            auto* fc = new FolderCard(fm, m_gridContent);
            fc->setPalette(pal, m_dark);
            connect(fc, &FolderCard::openRequested, this, &HomeScreen::openFolder);
            connect(fc, &FolderCard::newSubfolderRequested, this, [this](const QString& parentRel) {
                m_folderSheet->openCreate(parentRel, homePaletteFor(m_dark), m_dark);
            });
            connect(fc, &FolderCard::editRequested, this, [this](const FolderMeta& meta) {
                m_folderSheet->openEdit(meta, homePaletteFor(m_dark), m_dark);
            });
            connect(fc, &FolderCard::renameRequested, this, &HomeScreen::renameFolderByPath);
            connect(fc, &FolderCard::deleteRequested, this, &HomeScreen::deleteFolderByPath);
            m_folderCards.append(fc);
        }
    }

    const QVector<NotebookMeta> visibleDocs =
        (m_viewMode == ViewMode::Folder) ? LibraryStore::listNotebooks(m_currentFolder) : allDocs;
    for (const NotebookMeta& meta : visibleDocs) {
        auto* card = new NotebookCard(meta, m_gridContent);
        card->setPalette(pal, m_dark);
        card->setAvailableFolders(m_allFolders);
        connect(card, &NotebookCard::openRequested, this, &HomeScreen::openNotebookRequested);
        const QPointer<NotebookCard> guard(card);
        connect(card, &NotebookCard::renameRequested, this, [this, guard] {
            QTimer::singleShot(0, this, [this, guard] { if (guard) renameNotebook(guard.data()); });
        });
        connect(card, &NotebookCard::deleteRequested, this, [this, guard] {
            QTimer::singleShot(0, this, [this, guard] { if (guard) deleteNotebook(guard.data()); });
        });
        connect(card, &NotebookCard::editCoverRequested, this, [this](const NotebookMeta& m) {
            m_notebookSheet->openEditCover(m, homePaletteFor(m_dark), m_dark);
        });
        connect(card, &NotebookCard::moveToFolderRequested, this, &HomeScreen::moveNotebook);
        m_cards.append(card);
    }

    applySearchFilter(m_header ? m_header->searchText() : QString());
}

void HomeScreen::selectFilter(int index) {
    m_viewMode = (index == 1) ? ViewMode::Recientes : ViewMode::Todos;
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
    const int totalVisible = visibleFolders + visibleDocs;
    const QString subtitle = (totalVisible == 0) ? QStringLiteral("Sin elementos")
        : (totalVisible == 1)                  ? QStringLiteral("1 elemento")
                                               : QStringLiteral("%1 elementos").arg(totalVisible);
    if (m_header) {
        if (m_viewMode == ViewMode::Folder) {
            m_header->setFolderBreadcrumb(m_currentFolder, subtitle);
        } else {
            m_header->setSectionTitle(
                m_viewMode == ViewMode::Recientes ? QStringLiteral("Recientes")
                                                  : QStringLiteral("Todos"),
                subtitle);
        }
    }

    const bool searching = m_header && !m_header->searchText().trimmed().isEmpty();
    if (m_emptyState) {
        const bool showEmpty = (totalVisible == 0) && (searching || m_viewMode == ViewMode::Recientes);
        m_emptyState->setHidden(!showEmpty);
        if (m_emptyLabel) {
            m_emptyLabel->setText(
                searching ? QStringLiteral("No hay elementos que coincidan con la búsqueda")
                          : QStringLiteral("Nada editado en los últimos 7 días"));
        }
    }
}

void HomeScreen::relayoutGrid() {
    if (!m_gridContent || !m_scroll)
        return;
    constexpr int spacingX = 18;
    constexpr int spacingY = 20;
    constexpr int top = 8;

    const int contentWidth = m_scroll->viewport()->width();
    const int columns =
        std::max(1, (contentWidth + spacingX) / (NotebookCard::kCardWidth + spacingX));
    const bool searching = m_header && !m_header->searchText().trimmed().isEmpty();
    const bool showCreate = (m_viewMode != ViewMode::Recientes) && !searching;

    QList<QWidget*> items;
    if (m_createCard) {
        m_createCard->setHidden(!showCreate);
        if (showCreate)
            items.append(m_createCard);
    }
    for (FolderCard* fc : m_folderCards) {
        if (fc && !fc->isHidden())
            items.append(fc);
    }
    for (NotebookCard* card : m_cards) {
        if (card && !card->isHidden())
            items.append(card);
    }

    int index = 0;
    for (QWidget* w : items) {
        const int row = index / columns;
        const int col = index % columns;
        w->setGeometry(col * (NotebookCard::kCardWidth + spacingX),
                       top + row * (NotebookCard::kCardHeight + spacingY),
                       NotebookCard::kCardWidth, NotebookCard::kCardHeight);
        w->show();
        ++index;
    }

    const int rows = std::max(1, (index + columns - 1) / columns);
    m_gridContent->setMinimumHeight(top + rows * (NotebookCard::kCardHeight + spacingY) + 24);
    if (m_emptyState && !m_emptyState->isHidden())
        m_emptyState->setGeometry(0, 72, std::max(0, contentWidth), 110);
}

void HomeScreen::openCreateMenu() {
    if (!m_createMenu || !m_createCard)
        return;
    if (m_createMenu->isVisible()) {
        m_createMenu->closeMenu();
        return;
    }
    const QPoint topLeft = m_createCard->mapTo(this, QPoint(0, 0));
    const QString folderName = (m_viewMode == ViewMode::Folder)
        ? QFileInfo(m_currentFolder).fileName()
        : QString();
    m_createMenu->openAnchoredTo(QRect(topLeft, m_createCard->size()), folderName);
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
