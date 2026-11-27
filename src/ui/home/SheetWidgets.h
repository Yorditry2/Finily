#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/HomeTheme.h"

#include <QColor>
#include <QImage>
#include <QLineEdit>
#include <QPointF>
#include <QRect>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QPushButton;

namespace finily {

// Widgets propios de las tarjetas laterales (mockups 04 y 06). Todos se pintan
// con QPainter y leen sus colores de tokens::paletteFor(dark): así ningún tema
// del sistema puede alterarlos.

// Héroe superior: fondo del documento en vivo, chip de tipo, cierre y título editable.
class SheetHero : public QWidget {
    Q_OBJECT
public:
    explicit SheetHero(const QString& chipText, const QString& chipIcon, QWidget* parent = nullptr,
                       int height = 148);
    // Modo carpeta (mockup 06): fondo neutro liso y la carpeta grande al centro.
    void setFolder(int colorIndex, int iconIndex);
    QLineEdit* titleEdit() const { return m_title; }
    void setDark(bool dark);
    void setLook(const ThemeConfig& theme, BackgroundTemplate tpl, double scale);

signals:
    void closeClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    QRectF closeRect() const;
    QLineEdit* m_title = nullptr;
    QString m_chipText, m_chipIcon;
    ThemeConfig m_theme = getThemeConfig(ThemeId::DarkBoard);
    BackgroundTemplate m_tpl = BackgroundTemplate::Grid;
    double m_scale = 1.0;
    bool m_dark = false;
    bool m_folderMode = false;
    int m_folderColor = 0, m_folderIcon = 0;
};

// Fila de muestras circulares de 26 px repartidas a lo ancho (más botón '+' opcional).
class SwatchRow : public QWidget {
    Q_OBJECT
public:
    explicit SwatchRow(QWidget* parent = nullptr);
    void setColors(const QVector<QColor>& colors, const QStringList& names);
    void setCurrent(int index);
    void setDark(bool dark);
    void setShowMoreButton(bool on);
    void setMoreActive(bool on);
    void setMoreColor(const QColor& c);
    QPointF moreCenterInWidget() const;

signals:
    void picked(int index);
    void moreClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF cellRect(int i) const;
    QVector<QColor> m_colors;
    QStringList m_names;
    int m_current = 0;
    bool m_dark = false;
    bool m_showMore = false;
    bool m_moreActive = false;
    QColor m_moreColor;
};

// Cuatro muestras de plantilla (liso, cuadrícula, puntos, rayado) en neutro fijo.
class TemplateRow : public QWidget {
    Q_OBJECT
public:
    explicit TemplateRow(QWidget* parent = nullptr);
    void setCurrent(BackgroundTemplate t);
    void setDark(bool dark);

signals:
    void picked(BackgroundTemplate t);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF cellRect(int i) const;
    BackgroundTemplate m_current = BackgroundTemplate::Grid;
    bool m_dark = false;
};

// Regla de trama en mm con pulgar arrastrable y botones − / +.
class MmRuler : public QWidget {
    Q_OBJECT
public:
    static constexpr double kMin = 3.0, kMax = 12.0, kStep = 0.5;
    explicit MmRuler(QWidget* parent = nullptr);
    double value() const { return m_value; }
    void setValue(double mm);
    void setDark(bool dark);

signals:
    void valueChanged(double mm);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF stepperRect() const;
    QRectF trackRect() const;
    double valueAt(qreal x) const;
    void commit(double mm);
    double m_value = 5.0;
    bool m_dragging = false;
    bool m_dark = false;
};

// Chip informativo (ícono + texto + chevron opcional).
class InfoChip : public QWidget {
    Q_OBJECT
public:
    InfoChip(const QString& icon, const QString& text, bool chevron, bool dashed, QWidget* parent = nullptr);
    void setText(const QString& text);
    void setMaxWidth(int maxWidth);
    void setDashed(bool dashed);
    void setDark(bool dark);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QString m_icon, m_text;
    int m_maxWidth = 180;
    bool m_chevron, m_dashed, m_dark = false;
};

// Etiqueta de sección: 10 px, mayúsculas, espaciada.
QWidget* makeKicker(const QString& text, QWidget* parent, bool dark);

// Botón principal «Crear …» (38 px, acento).
QPushButton* makeCtaButton(const QString& text, QWidget* parent, bool dark);

// Reaplica el tema claro/oscuro a kickers y botones CTA dentro de un contenedor.
void refreshSheetWidgetsTheme(QWidget* root, bool dark);

// Controles compartidos de modales centrados (Pizarra / Cuaderno / Carpeta).
QPushButton* makeModalCloseButton(QWidget* parent);
void styleModalCloseButton(QPushButton* btn, bool dark);
void styleModalTitleEdit(QLineEdit* edit, bool dark, qreal fontSizePx = 15.0);

// Ocho círculos de 28 px de dos tonos (banda superior = solapa), mockup 06.
class DotRow : public QWidget {
    Q_OBJECT
public:
    explicit DotRow(QWidget* parent = nullptr);
    void setCurrent(int index);
    void setDark(bool dark);

signals:
    void picked(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF cellRect(int i) const;
    int hit(const QPointF& pos) const;
    int m_current = 0, m_hover = -1;
    bool m_dark = false;
};

// Rejilla de 14 botones (sin ícono + 13) en 7 columnas.
class IconGrid : public QWidget {
    Q_OBJECT
public:
    explicit IconGrid(QWidget* parent = nullptr);
    void setCurrent(int index);
    void setDark(bool dark);

signals:
    void picked(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF cellRect(int i) const;
    int hit(const QPointF& pos) const;
    int m_current = 0, m_hover = -1;
    bool m_dark = false;
};

struct LocationEntry {
    QString relativePath; // "" = raíz «Carpetas»
    QString name;
    int depth = 0;
    int colorIndex = 0;
};

// Menú flotante del selector «Dentro de»: árbol con sangría y marca de selección.
class LocationPopover : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit LocationPopover(QWidget* parent);
    qreal progress() const { return m_progress; }
    void setProgress(qreal p);
    void setEntries(const QVector<LocationEntry>& entries);
    void setCurrent(const QString& relativePath);
    void setDark(bool dark);
    void popup(const QRect& anchorInParent);
    void closePopover();
    bool isClosing() const { return m_closing; }

signals:
    void picked(const QString& relativePath);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void leaveEvent(QEvent*) override;

private:
    int hit(const QPointF& pos) const;
    void updateCardSize();
    int contentHeight() const;
    int maxScroll() const;

    QVector<LocationEntry> m_entries;
    QString m_current;
    QRect m_cardRect;
    QImage m_backdrop;
    QPropertyAnimation* m_anim = nullptr;
    qreal m_progress = 1.0;
    int m_scrollY = 0;
    int m_hover = -1;
    bool m_dark = false;
    bool m_closing = false;
    bool m_grabbingBackdrop = false;
};

// Popover Liquid Glass para asignar etiquetas o crear una inline desde una hoja.
class TagPickPopover : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit TagPickPopover(QWidget* parent);
    qreal progress() const { return m_progress; }
    void setProgress(qreal p);
    void setTags(const QVector<TagInfo>& tags);
    void setSelectedTags(const QStringList& ids);
    void setDark(bool dark);
    void popup(const QRect& anchorInParent);
    void closePopover();
    bool isClosing() const { return m_closing; }

signals:
    void selectionChanged(const QStringList& selectedIds);
    void newTagRequested(const QString& name);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    int hitRow(const QPointF& pos) const;
    void updateGeometryAndInput();
    void updateInputStyle();
    int listHeight() const;
    int totalListHeight() const;
    int maxScroll() const;

    QVector<TagInfo> m_tags;
    QStringList m_selected;
    QRect m_cardRect;
    QRect m_lastAnchor;
    QImage m_backdrop;
    QLineEdit* m_newEdit = nullptr;
    QGraphicsOpacityEffect* m_editOpacity = nullptr;
    QPropertyAnimation* m_anim = nullptr;
    qreal m_progress = 1.0;
    int m_scrollY = 0;
    int m_hover = -1;
    bool m_dark = false;
    bool m_closing = false;
    bool m_grabbingBackdrop = false;
    bool m_hasAnchor = false;
};

} // namespace finily
