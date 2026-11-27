#pragma once

#include "ui/home/HomeTheme.h"

#include <QString>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QLineEdit;

namespace finily {

class HeaderIconButton;
class HeaderNewButton;
class HeaderSearchField;

// Cabecera de una fila (mockup 00): [reabrir sidebar] título o migas ·
// buscar · + Nuevo. Al buscar, el título se cambia por el campo de búsqueda.
class HomeHeader : public QWidget {
    Q_OBJECT

public:
    explicit HomeHeader(QWidget* parent = nullptr);

    void setSectionTitle(const QString& title);
    void setFolderBreadcrumb(const QString& relativePath);
    void setSidebarCollapsed(bool collapsed);
    QString searchText() const;
    QRect newButtonGeometry() const; // en coordenadas de la cabecera
    QRect filterButtonGeometry() const;
    void setFilterActive(bool active);
    void setFilterVisible(bool visible);
    // Ficha de ámbito dentro del buscador («Apuntes ×»); vacío = toda la biblioteca.
    void setSearchScope(const QString& name);
    void openSearch();
    void closeSearch();
    // Ajustes: oculta buscar, filtro y «+ Nuevo».
    void setLibraryControlsVisible(bool visible);
    void applyTheme(const HomePalette& pal, bool dark);

signals:
    void searchTextChanged(const QString& text);
    void breadcrumbClicked(const QString& relativePath);
    void newRequested();
    void reopenSidebarRequested();
    void filterRequested();
    void searchScopeCleared();
    void searchSubmitted();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void clearCrumbs();
    void setSearchOpen(bool open);

    HomePalette m_pal = homePaletteFor(false);
    HeaderIconButton* m_reopenBtn = nullptr;
    QWidget* m_titleBox = nullptr;
    QHBoxLayout* m_crumbRow = nullptr;
    HeaderSearchField* m_searchField = nullptr;
    QLineEdit* m_search = nullptr;
    HeaderIconButton* m_searchBtn = nullptr;
    HeaderIconButton* m_filterBtn = nullptr;
    HeaderNewButton* m_newBtn = nullptr;
};

} // namespace finily
