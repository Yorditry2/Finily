#pragma once

#include "ui/home/HomeTheme.h"

#include <QImage>
#include <QRect>
#include <QVector>
#include <QWidget>

#include <functional>

class QPropertyAnimation;

namespace finily {

// Fila de un menú contextual del Inicio (mockup 07).
struct HomeMenuItem {
    enum class Kind { Action, Separator, Header, Swatches };
    Kind kind = Kind::Action;
    QString icon;
    QString label;
    QString shortcut;
    bool danger = false;
    bool enabled = true;
    std::function<void()> action;
    QVector<HomeMenuItem> children; // submenú (se abre al pasar el cursor)
    int swatchCurrent = -1;
    std::function<void(int)> swatchPicked;

    static HomeMenuItem act(const QString& icon, const QString& label, std::function<void()> fn,
                            const QString& shortcut = {}) {
        HomeMenuItem i;
        i.icon = icon;
        i.label = label;
        i.action = std::move(fn);
        i.shortcut = shortcut;
        return i;
    }
    static HomeMenuItem sep() {
        HomeMenuItem i;
        i.kind = Kind::Separator;
        return i;
    }
    static HomeMenuItem header(const QString& label) {
        HomeMenuItem i;
        i.kind = Kind::Header;
        i.label = label;
        return i;
    }
};

// Menú contextual pintado, hijo directo de HomeScreen (nunca Qt::Popup ni QMenu):
// cubre el padre para cerrarse con un clic fuera o Esc.
class HomeMenu : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit HomeMenu(QWidget* parent = nullptr);

    qreal progress() const { return m_progress; }
    void setProgress(qreal p);
    void applyTheme(bool dark);
    void popup(const QPoint& posInParent, const QVector<HomeMenuItem>& items);
    void closeMenu();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Panel {
        QVector<HomeMenuItem> items;
        QRect rect;
        QImage backdrop;
        int hovered = -1;
        int hoveredSwatch = -1;
    };
    static int itemHeight(const HomeMenuItem& it);
    QRect itemRect(const Panel& p, int index) const;
    QRect swatchRect(const QRect& row, int i) const;
    void layoutPanel(Panel& p, const QPoint& at, bool preferLeftOf, int leftEdge);
    bool hitTest(const QPoint& pos, int* panel, int* item, int* swatch) const;
    void paintPanel(QPainter& painter, const Panel& p);
    void trigger(int panel, int item, int swatch);
    void openSubmenu(int item);

    QVector<Panel> m_panels; // 0 = principal, 1 = submenú
    int m_submenuOf = -1;
    bool m_dark = false;
    qreal m_progress = 1.0;
    QPropertyAnimation* m_anim = nullptr;
};

// Aviso inferior oscuro con acción opcional «Deshacer» (6 s).
class HomeToast : public QWidget {
    Q_OBJECT
public:
    explicit HomeToast(QWidget* parent = nullptr);
    void showMessage(const QString& text, const QString& actionText = {},
                     std::function<void()> action = {}, int msec = 6000);
    // Centra el aviso en el área de contenido (sin la barra lateral).
    void setArea(const QRect& area);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect actionRect() const;
    QRect closeRect() const;
    void dismiss();

    QString m_text;
    QString m_actionText;
    std::function<void()> m_action;
    QRect m_area;
    class QTimer* m_timer = nullptr;
    int m_hover = 0; // 1 = acción, 2 = cerrar
};

// Ventana de confirmación propia, solo para acciones irreversibles.
class ConfirmDialog : public QWidget {
    Q_OBJECT
public:
    explicit ConfirmDialog(QWidget* parent = nullptr);
    void applyTheme(bool dark);
    void ask(const QString& icon, const QString& title, const QString& body,
             const QString& confirmText, std::function<void()> onConfirm);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRect cardRect() const;
    QRect cancelRect() const;
    QRect confirmRect() const;
    int bodyHeight() const;
    void finish(bool accepted);

    QString m_icon, m_title, m_body, m_confirm;
    std::function<void()> m_onConfirm;
    bool m_dark = false;
    int m_hover = 0; // 1 = cancelar, 2 = confirmar
};

} // namespace finily
