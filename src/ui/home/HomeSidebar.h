#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/HomeTheme.h"

#include <QVector>
#include <QWidget>

class QScrollArea;
class QVBoxLayout;

namespace finily {

class SidebarNavRow;
class SidebarIconButton;

// Barra lateral del Inicio (mockup 00): marca + colapsar, Documentos,
// Carpetas con árbol desplegable y pie con borrador y tema.
class HomeSidebar : public QWidget {
    Q_OBJECT

public:
    explicit HomeSidebar(QWidget* parent = nullptr);

    void setSelectedFilter(int filterIndex);
    void setSelectedFolder(const QString& relativePath);
    void setFolders(const QVector<FolderMeta>& folders);
    void setDraftVisible(bool visible);
    void setTrashCount(int count);

    static constexpr int kTrashFilter = 3; // índice de filtro de la vista Papelera
    void applyTheme(const HomePalette& pal, bool dark);

signals:
    void filterSelected(int filterIndex);
    void folderSelected(const QString& relativePath);
    void folderMenuRequested(const finily::FolderMeta& meta, const QPoint& globalPos);
    void continueDraftRequested();
    void darkModeToggled(bool dark);
    void settingsRequested();
    void collapseRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setTreeOpen(bool open);
    void refreshActive();

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    bool m_treeOpen = true;
    int m_selectedFilter = 0;
    QString m_selectedFolder;

    SidebarIconButton* m_collapseBtn = nullptr;
    SidebarNavRow* m_docsRow = nullptr;
    SidebarNavRow* m_foldersRow = nullptr;
    QScrollArea* m_treeScroll = nullptr;
    QWidget* m_treeBox = nullptr;
    QVBoxLayout* m_treeLayout = nullptr;
    QVector<SidebarNavRow*> m_folderRows;

    SidebarNavRow* m_draftRow = nullptr;
    SidebarNavRow* m_trashRow = nullptr;
    QWidget* m_footLine = nullptr;
    SidebarIconButton* m_themeBtn = nullptr;
    SidebarIconButton* m_settingsBtn = nullptr;
};

} // namespace finily
