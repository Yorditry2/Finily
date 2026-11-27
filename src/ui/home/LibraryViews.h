#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/HomeTheme.h"

#include <QImage>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QLabel;
class QHBoxLayout;
class QPropertyAnimation;

namespace finily {

// Filtro y orden de la biblioteca (mockup 09, escena 02). El orden se recuerda
// entre sesiones; tipo, fecha y etiquetas se reinician al cerrar (DESIGN.md).
struct LibraryFilter {
    enum Kind { AllKinds = 0, Boards, Notebooks, Pdfs, Folders };
    enum Date { AnyDate = 0, Today, LastWeek, ThisMonth };
    enum Sort { Recent = 0, Name = 1, Created = 2 };

    int kind = AllKinds;
    int date = AnyDate;
    int sort = Recent;
    QStringList tagIds;

    int activeCount() const {
        return int(kind != AllKinds) + int(date != AnyDate) + int(sort != Recent) + int(tagIds.size());
    }
    bool acceptsFolders() const {
        return (kind == AllKinds || kind == Folders) && date == AnyDate && tagIds.isEmpty();
    }
    bool acceptsDocument(const NotebookMeta& meta) const;
    static QString kindLabel(int kind);
    static QString kindIcon(int kind);
    static QString dateLabel(int date);
    static QString sortLabel(int sort);
    static int loadSavedSort();
    static void saveSort(int sort);
};

void sortDocuments(QVector<NotebookMeta>& docs, int sort);

// Estado vacío del mockup 08: dibujo de línea fina, frase y una sola salida.
class EmptyStateView : public QWidget {
    Q_OBJECT

public:
    struct Action {
        QString label;
        std::function<void()> run;
        QString icon = {};
        bool danger = false;
    };

    explicit EmptyStateView(QWidget* parent = nullptr);
    void applyTheme(const HomePalette& pal, bool dark);
    // `art`: nombre del dibujo en resources/icons/empty; `body` admite <b>…</b> y <kbd>…</kbd>.
    void setContent(const QString& art, const QString& title, const QString& body,
                    const QVector<Action>& actions = {});
    int preferredHeight() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void relayout();
    QString bodyHtml() const;

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    QString m_art;
    QSize m_artSize;
    QString m_body;
    QLabel* m_title = nullptr;
    QLabel* m_bodyLabel = nullptr;
    QWidget* m_actionsBox = nullptr;
    QHBoxLayout* m_actionsRow = nullptr;
};

// Flecha punteada suave que apunta a «+ Nuevo» en la primera vez.
class FirstRunHint : public QWidget {
public:
    explicit FirstRunHint(QWidget* parent = nullptr);
    void applyTheme(const HomePalette& pal) { m_pal = pal; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    HomePalette m_pal = homePaletteFor(false);
};

// Rótulo de grupo de resultados: «CARPETAS 1».
class ResultGroupLabel : public QWidget {
public:
    ResultGroupLabel(const QString& title, int count, QWidget* parent = nullptr);
    void applyTheme(const HomePalette& pal) { m_pal = pal; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_title;
    int m_count = 0;
    HomePalette m_pal = homePaletteFor(false);
};

// Fichas de lo aplicado bajo la cabecera, cada una con su ×.
class FilterChipsBar : public QWidget {
    Q_OBJECT

public:
    explicit FilterChipsBar(QWidget* parent = nullptr);
    void applyTheme(const HomePalette& pal, bool dark);
    void setTags(const QVector<TagInfo>& tags);
    void setFilter(const LibraryFilter& filter);
    bool hasChips() const { return !m_chips.isEmpty(); }

signals:
    void filterChanged(const finily::LibraryFilter& filter);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Chip {
        QString icon;
        QString label;
        int field = 0; // 0 tipo, 1 fecha, 2 orden, 3 etiqueta
        QString tagId;
        QRectF rect;
    };
    void rebuild();

    LibraryFilter m_filter;
    QVector<TagInfo> m_tags;
    QVector<Chip> m_chips;
    int m_hover = -1;
    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
};

// Cápsula de filtro y orden (mockup 09 .fpanel), hija directa de HomeScreen.
class FilterPanel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit FilterPanel(QWidget* parent = nullptr);
    void applyTheme(const HomePalette& pal, bool dark);
    void setTags(const QVector<TagInfo>& tags);
    // `anchor`: rectángulo del botón de filtro en coordenadas del padre.
    void popup(const QRect& anchor, const LibraryFilter& filter, bool trashView);
    void closePanel();
    qreal progress() const { return m_progress; }
    void setProgress(qreal p);

signals:
    void filterChanged(const finily::LibraryFilter& filter);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Hit {
        QRectF rect;
        int field = 0; // 0 tipo, 1 fecha, 2 orden, 3 restablecer, 4 etiqueta
        int value = 0;
        QString tagId;
    };
    void layoutPanel();

    LibraryFilter m_filter;
    QVector<TagInfo> m_tags;
    bool m_trashView = false;
    bool m_closing = false;
    QRect m_anchor;
    QRectF m_panel;
    QImage m_backdrop;
    QVector<Hit> m_hits;
    int m_hover = -1;
    qreal m_progress = 1.0;
    QPropertyAnimation* m_anim = nullptr;
    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
};

} // namespace finily
