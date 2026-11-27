#pragma once

#include "ui/DesignTokens.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QLineEdit>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QPainter;
class QTabletEvent;

namespace finily {

// Paleta canónica del Editor (mockups/redesign/22-editor-arreglos.html).
struct EditorPalette {
    QColor bg;           // --ed-bg
    QColor deskNb;       // --ed-desk-nb
    QColor sheet;        // --ed-sheet
    QColor grid;         // --ed-grid
    QColor surfaceGlass; // --ed-surface (translúcida)
    QColor surfaceSolid; // --ed-surface-solid
    QColor subtle;       // --ed-subtle
    QColor tabsBg;       // --ed-tabs-bg
    QColor border;       // --ed-border
    QColor borderSoft;   // --ed-border-soft
    QColor checkOff;     // borde de casilla desmarcada
    QColor text;         // --ed-text
    QColor muted;        // --ed-muted
    QColor icon;         // --ed-icon
    QColor hover;        // --ed-hover
    QColor accent;       // --ed-accent
    QColor accentFg;     // --ed-accent-fg
    QColor danger;       // --ed-danger
    QColor glassHi;      // brillo 1px superior
    QColor unsavedAmber; // #F59E0B
    QColor selectionBlue;// #2563EB
    QColor veil;         // velo de modal rgba(15,23,42,0.28)
};

EditorPalette editorPaletteFor(bool dark);
QFont editorFont(qreal px, int weight = QFont::Normal);
QColor resolveAdaptableInkColor(const QColor& stored, bool sheetDark);

struct EditorDropdownItem {
    int id = 0;
    QString label;
    QString shortcut;
    QString iconName;
    QString iconPack = QStringLiteral("ui");
    bool separatorBefore = false;
    bool danger = false;
    bool checked = false;
    bool enabled = true;
};

using DropdownRequestFn = std::function<void(
    QWidget* anchor,
    const QVector<EditorDropdownItem>& items,
    std::function<void(int)> onPicked)>;

// Utilidades compartidas de pintado QPainter (inmunes al tema del sistema).
void paintEditorSwatch(QPainter& p, const QRectF& circleRect, const QColor& color,
                       bool active, const EditorPalette& pal);
void paintEditorRainbowSwatch(QPainter& p, const QRectF& circleRect, bool active,
                              const EditorPalette& pal);
void paintEditorWidthSlot(QPainter& p, const QRectF& rect, qreal pillThickness,
                          bool active, bool hovered, const QColor& inkColor,
                          const EditorPalette& pal);
void paintEditorSubChevron(QPainter& p, const QRectF& btnRect, const QColor& color);
void paintEditorFamilyCorner(QPainter& p, const QRectF& btnRect, const QColor& color);
void paintEditorGripDots(QPainter& p, const QRectF& rect, const QColor& color, qreal opacity = 0.55);
QString editorScrollAreaStyle(bool dark);

// Superficie flotante / panel / cápsula pintada con QPainter (Wayland + Wacom safe).
class EditorGlassCard : public QWidget {
    Q_OBJECT

public:
    enum class Role {
        BarGlass,
        PopoverSolid,
        SidePanel,
        ContextCapsule,
        ModalCard
    };

    explicit EditorGlassCard(QWidget* parent = nullptr);

    void setRole(Role role);
    Role role() const { return m_role; }

    void setCornerRadius(qreal radius);
    qreal cornerRadius() const { return m_radius; }

    void setDark(bool dark);
    bool isDark() const { return m_dark; }
    const EditorPalette& palette() const { return m_pal; }

    // Captura fondo 1/4 blur una sola vez al abrir/mover (nunca en paintEvent).
    void refreshBackdrop(QWidget* canvasSource);
    void clearBackdrop();

    virtual void applyEditorTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;

    EditorPalette m_pal;
    bool m_dark = false;
    Role m_role = Role::PopoverSolid;
    qreal m_radius = 16.0;
    QImage m_blurredBackdrop;
    bool m_grabbingBackdrop = false;
};

// QLineEdit que consume atajos de una sola tecla mientras tiene el foco.
class EditorLineEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit EditorLineEdit(QWidget* parent = nullptr);
    void applyEditorTheme(bool dark);

protected:
    bool event(QEvent* e) override;
};

// Botón de icono estilizado con QPainter (34x34 por defecto).
class EditorIconButton : public QWidget {
    Q_OBJECT

public:
    explicit EditorIconButton(const QString& iconName = {},
                              const QString& iconPack = QStringLiteral("ui"),
                              QWidget* parent = nullptr);

    void setIcon(const QString& name, const QString& pack = QStringLiteral("ui"));
    void setDynamicPenMode(bool enabled, const QColor& nibColor);
    void setDynamicHighlighterMode(bool enabled, const QColor& barColor);
    void setActive(bool active);
    void setShowSubChevron(bool show);
    void setShowFamilyCorner(bool show);
    void setDanger(bool danger);
    void setEnabledState(bool enabled);
    void setIconSizePx(qreal px);
    void applyEditorTheme(bool dark);

    bool isActive() const { return m_active; }

signals:
    void clicked();
    void rightClicked(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    QString m_iconName;
    QString m_iconPack = QStringLiteral("ui");
    bool m_dynamicPen = false;
    bool m_dynamicHl = false;
    QColor m_dynamicColor;
    bool m_active = false;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_showSubChev = false;
    bool m_showFamilyCorner = false;
    bool m_danger = false;
    bool m_enabled = true;
    qreal m_iconPx = 17.0;
    bool m_dark = false;
    EditorPalette m_pal;
};

// Segmentado (.seg) de N opciones con botón activo elevado.
class EditorSegControl : public QWidget {
    Q_OBJECT

public:
    explicit EditorSegControl(QWidget* parent = nullptr);

    void setOptions(const QStringList& labels);
    void setCurrentIndex(int index);
    int currentIndex() const { return m_current; }
    void applyEditorTheme(bool dark);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void currentIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    int indexAt(const QPointF& pos) const;

    QStringList m_labels;
    int m_current = 0;
    int m_hovered = -1;
    bool m_dark = false;
    EditorPalette m_pal;
};

// Chip redondeado (.chip) opcionalmente con punto de color o ícono.
class EditorChipButton : public QWidget {
    Q_OBJECT

public:
    explicit EditorChipButton(const QString& text = {}, QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const { return m_text; }
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    void setCheckable(bool checkable) { m_checkable = checkable; }
    void setIcon(const QString& name, const QString& pack = QStringLiteral("ui"));
    void setDotColor(const QColor& color);
    void setDanger(bool danger);
    void applyEditorTheme(bool dark);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void clicked();
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    QString m_text;
    QString m_iconName;
    QString m_iconPack = QStringLiteral("ui");
    QColor m_dotColor;
    bool m_checked = false;
    bool m_checkable = false;
    bool m_hovered = false;
    bool m_danger = false;
    bool m_dark = false;
    EditorPalette m_pal;
};

// Interruptor (.sw-toggle) 32x18 px con etiqueta opcional.
class EditorSwitch : public QWidget {
    Q_OBJECT

public:
    explicit EditorSwitch(QWidget* parent = nullptr);

    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    void applyEditorTheme(bool dark);

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    bool m_checked = false;
    bool m_dark = false;
    EditorPalette m_pal;
};

// Casilla de verificación (.ck) 16x16 + título + subtítulo opcional.
class EditorCheckBox : public QWidget {
    Q_OBJECT

public:
    explicit EditorCheckBox(const QString& title = {}, const QString& subtitle = {},
                            QWidget* parent = nullptr);

    void setText(const QString& title, const QString& subtitle = {});
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    void applyEditorTheme(bool dark);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    QString m_title;
    QString m_subtitle;
    bool m_checked = false;
    bool m_hovered = false;
    bool m_dark = false;
    EditorPalette m_pal;
};

// Deslizador limpio (.ed-range) pintado con QPainter.
class EditorSlider : public QWidget {
    Q_OBJECT

public:
    explicit EditorSlider(QWidget* parent = nullptr);

    void setRange(double minVal, double maxVal, double step = 1.0);
    void setValue(double value);
    double value() const { return m_value; }
    void applyEditorTheme(bool dark);

signals:
    void valueChanged(double value);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    void updateFromX(qreal x);

    double m_min = 0.0;
    double m_max = 100.0;
    double m_step = 1.0;
    double m_value = 50.0;
    bool m_dragging = false;
    bool m_dark = false;
    EditorPalette m_pal;
};

} // namespace finily
