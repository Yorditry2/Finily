#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/HomeTheme.h"

#include <QPoint>
#include <QString>
#include <QVector>
#include <QWidget>

class QScrollArea;
class QVBoxLayout;

namespace finily {

class SidebarDivider;
class SidebarIconButton;
class SidebarNavRow;
class SidebarTreeBox;
class UpdateBanner;

// Barra lateral del Inicio (mockups 00, 09, 11): marca + colapsar, Documentos,
// Favoritos, Carpetas con árbol desplegable, Etiquetas progresivas y pie.
class HomeSidebar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth)

public:
    enum NavSection { NavDocs = 0, NavFavs = 1, NavFolders = 2, NavTrash = 3 };
    static constexpr int kTrashFilter = NavTrash; // compatibilidad con la vista Papelera

    explicit HomeSidebar(QWidget* parent = nullptr);

    UpdateBanner* updateBanner() const { return m_updateBanner; }
    void setUpdateDotVisible(bool visible);

    void setSelectedFilter(int filterIndex);
    void setSelectedFolder(const QString& relativePath);
    void setSelectedTag(const QString& tagId);
    void setSelectedSettings();
    void setFolders(const QVector<FolderMeta>& folders);
    void setTags(const QVector<TagInfo>& tags);
    void setDraftVisible(bool visible);
    void setTrashCount(int count);
    void setDropHighlightFolder(const QString& relativePath);
    QString folderAtGlobalPos(const QPoint& globalPos) const;

    int panelWidth() const { return m_panelWidth; }
    void setPanelWidth(int width);

    QPoint themeButtonCenterIn(QWidget* ancestor) const;
    void pulseThemeButton();
    QPoint trashIconCenterIn(QWidget* ancestor) const;
    void bounceTrashIcon();

    void applyTheme(const HomePalette& pal, bool dark);

signals:
    void filterSelected(int filterIndex);
    void folderSelected(const QString& relativePath);
    void folderMenuRequested(const finily::FolderMeta& meta, const QPoint& globalPos);
    void tagSelected(const QString& tagId);
    void tagMenuRequested(const finily::TagInfo& tag, const QPoint& globalPos);
    void continueDraftRequested();
    void darkModeToggled(bool dark);
    void settingsRequested();
    void collapseRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setTreeOpen(bool open);
    void setTagsOpen(bool open);
    void refreshActive();
    void updateScrollBounds(QScrollArea* scroll, QWidget* box);

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    bool m_treeOpen = false;
    bool m_tagsOpen = true;
    int m_selectedFilter = NavDocs;
    QString m_selectedFolder;
    QString m_selectedTag;
    QString m_dropHighlightFolder;
    bool m_settingsSelected = false;
    bool m_updateDotVisible = false;
    int m_panelWidth = tokens::space::SidebarWidth;

    SidebarIconButton* m_collapseBtn = nullptr;
    SidebarNavRow* m_docsRow = nullptr;
    SidebarNavRow* m_favsRow = nullptr;
    SidebarNavRow* m_foldersRow = nullptr;
    QScrollArea* m_treeScroll = nullptr;
    SidebarTreeBox* m_treeBox = nullptr;
    QVBoxLayout* m_treeLayout = nullptr;
    QVector<SidebarNavRow*> m_folderRows;

    SidebarNavRow* m_tagsHeaderRow = nullptr;
    QScrollArea* m_tagsScroll = nullptr;
    SidebarTreeBox* m_tagsBox = nullptr;
    QVBoxLayout* m_tagsLayout = nullptr;
    QVector<SidebarNavRow*> m_tagRows;
    QVector<TagInfo> m_tags;

    UpdateBanner* m_updateBanner = nullptr;
    SidebarNavRow* m_draftRow = nullptr;
    SidebarNavRow* m_trashRow = nullptr;
    SidebarDivider* m_footLine = nullptr;
    SidebarIconButton* m_themeBtn = nullptr;
    SidebarIconButton* m_settingsBtn = nullptr;
};

} // namespace finily
