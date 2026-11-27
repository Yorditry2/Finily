#pragma once

#include "ui/home/HomeTheme.h"

#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPropertyAnimation;
class QSequentialAnimationGroup;

namespace finily {

class HeaderIconButton;
class HeaderKbdButton;
class HeaderNewButton;
class HeaderPillButton;
class HeaderSearchField;
class TitleTransitionOverlay;

// Cabecera de una fila (mockups 00, 09, 10, 11): [reabrir sidebar] título o migas ·
// [vaciar papelera] · buscar · filtro · + Nuevo (o chip «Esc» en Ajustes).
class HomeHeader : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal searchProgress READ searchProgress WRITE setSearchProgress)

public:
    explicit HomeHeader(QWidget* parent = nullptr);

    void setSectionTitle(const QString& title, bool animate = false);
    void setFolderBreadcrumb(const QString& relativePath, bool animate = false);
    void setTagBreadcrumb(const QString& tagName, bool animate = false);
    void setSidebarCollapsed(bool collapsed, bool animated = false);
    void setUpdateDotVisible(bool visible);
    void setTrashEmptyButtonVisible(bool visible);
    void setSettingsMode(bool on);

    QString searchText() const;
    QRect newButtonGeometry() const; // en coordenadas de la cabecera
    QRect filterButtonGeometry() const;
    void setFilterActive(bool active);
    void setFilterVisible(bool visible);
    // Ficha de ámbito dentro del buscador («Apuntes ×»); vacío = toda la biblioteca.
    void setSearchScope(const QString& name);
    void openSearch();
    void closeSearch();
    // Ajustes: oculta buscar, filtro, papelera y «+ Nuevo»; muestra «Esc».
    void setLibraryControlsVisible(bool visible);

    qreal searchProgress() const { return m_searchProgress; }
    void setSearchProgress(qreal progress);

    void applyTheme(const HomePalette& pal, bool dark);

signals:
    void searchTextChanged(const QString& text);
    void breadcrumbClicked(const QString& relativePath);
    void newRequested();
    void reopenSidebarRequested();
    void filterRequested();
    void searchScopeCleared();
    void searchSubmitted();
    void emptyTrashRequested();
    void settingsCloseRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void clearCrumbs();
    void setSearchOpen(bool open);
    QRect searchTargetRect() const;
    void runTitleChange(const QString& key, bool animate, const std::function<void()>& rebuild);

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    bool m_settingsMode = false;
    bool m_filterVisible = true;
    bool m_trashEmptyVisible = false;
    bool m_sidebarCollapsed = false;
    bool m_updateDotVisible = false;
    bool m_searchOpen = false;
    qreal m_searchProgress = 0.0;
    QString m_headerKey;

    HeaderIconButton* m_reopenBtn = nullptr;
    QWidget* m_titleBox = nullptr;
    QHBoxLayout* m_crumbRow = nullptr;
    QVector<QPointer<QLabel>> m_sepLabels;
    QVector<QPointer<QLabel>> m_mutedLabels;
    TitleTransitionOverlay* m_titleOverlay = nullptr;

    HeaderSearchField* m_searchField = nullptr;
    QLineEdit* m_search = nullptr;
    HeaderPillButton* m_emptyTrashBtn = nullptr;
    HeaderIconButton* m_searchBtn = nullptr;
    HeaderIconButton* m_filterBtn = nullptr;
    HeaderNewButton* m_newBtn = nullptr;
    HeaderKbdButton* m_escBtn = nullptr;

    QPropertyAnimation* m_searchAnim = nullptr;
    QSequentialAnimationGroup* m_reopenAnim = nullptr;
};

} // namespace finily
