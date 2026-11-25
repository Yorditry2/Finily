#pragma once

#include "core/Types.h"

#include <QDateTime>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QScrollArea;
class QToolButton;
class QAction;

namespace finily {

class NotebookCard;
class NewNotebookCard;
class FinilyBadge;
class ThemeSwitch;

// Pantalla de inicio estilo StarNote/GoodNotes: estudio editorial cálido con
// sidebar blanca fija (232 px), galería de tarjetas verticales 3:4 pintadas a
// mano (QPainter, inmune al estilo de sistema Caelestia/KDE), pestañas de
// filtro, búsqueda, tarjeta '+' punteada siempre visible y alternancia
// Claro/Oscuro persistida en QSettings.
class HomeScreen : public QWidget {
    Q_OBJECT

public:
    explicit HomeScreen(QWidget* parent = nullptr);

    static QStringList libraryDirectories();
    static QString notebookBaseName(QString name);
    static QString uniqueNotebookPath(const QString& directory, const QString& baseName);

    // Reescanea la biblioteca (se llama al volver desde el editor).
    void refreshLibrary();

    // Banner discreto de actualización disponible.
    void showUpdateBanner(const QString& version, bool canInstallInApp);
    void setUpdateDownloadProgress(int percent);
    void resetUpdateBannerState();
    void hideUpdateBanner();

signals:
    void createNotebookRequested(const QString& name, finily::ThemeId theme,
                                 finily::BackgroundTemplate tpl, finily::PageFormat format);
    void openNotebookRequested(const QString& filePath);
    void importPdfFromHomeRequested();
    void openExternalFileRequested();
    void continueDraftRequested();
    void updateInstallRequested();
    void updateCancelRequested();
    void updateReleaseNotesRequested();
    void updateDismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class Filter { Todos, Recientes, ConPdf };

    void buildSidebar();
    void buildGallery();
    void openCreateDialog();
    void renameNotebook(NotebookCard* card);
    void deleteNotebook(NotebookCard* card);
    void applySearchFilter(const QString& text);
    void selectFilter(int index);
    void setDarkMode(bool dark);
    void applyTheme();
    void updateSectionHeader();
    void relayoutGrid();
    void updateEmptyState();

    // Sidebar (232 px fijos, blanca en modo cálido)
    QWidget* m_sidebar = nullptr;
    FinilyBadge* m_badge = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QToolButton* m_newBtn = nullptr;
    QVector<QToolButton*> m_filterTabs;
    QToolButton* m_importBtn = nullptr;
    QToolButton* m_openBtn = nullptr;
    QToolButton* m_draftBtn = nullptr;
    ThemeSwitch* m_themeSwitch = nullptr;
    QLabel* m_themeLabel = nullptr;

    // Galería de cuadernos
    QLabel* m_sectionTitle = nullptr;
    QLabel* m_countPill = nullptr;
    QLineEdit* m_search = nullptr;
    QAction* m_searchAction = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_gridContent = nullptr;
    NewNotebookCard* m_newCard = nullptr;
    QWidget* m_emptyState = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QVector<NotebookCard*> m_cards;

    // Banner de actualización
    QWidget* m_updateBanner = nullptr;
    QLabel* m_updateLabel = nullptr;
    QToolButton* m_updateNotesBtn = nullptr;
    QToolButton* m_updatePrimaryBtn = nullptr;
    QToolButton* m_updateCancelBtn = nullptr;
    QToolButton* m_updateDismissBtn = nullptr;
    bool m_updateCanInstall = false;

    Filter m_filter = Filter::Todos;
    bool m_dark = false;
};

} // namespace finily
