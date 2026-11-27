#pragma once

#include "ui/home/HomeTheme.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLineEdit;
class QPropertyAnimation;

namespace finily {

// Selector flotante de color de fondo para «Nueva pizarra infinita» (mockup 00, escena 08).
// Hijo directo de HomeScreen (hermano de NewBoardSheet), sin Qt::Popup ni QColorDialog.
class BoardColorPopover : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit BoardColorPopover(QWidget* parent = nullptr);

    qreal progress() const { return m_progress; }
    void setProgress(qreal p);

    void openAnchoredTo(const QRect& cardRectInParent, const QColor& current, bool dark);
    void closePopover();
    void setDark(bool dark);
    bool isClosing() const { return m_closing; }

signals:
    void colorChanged(const QColor& color);
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class DragMode { None, SvSquare, HueBar };

    QRectF closeRect() const;
    QRectF swatchRect(int index) const;
    QRectF svRect() const;
    QRectF hueRect() const;
    QRectF hexFieldRect() const;

    void setColorInternal(const QColor& c, bool syncHex, bool emitChange);
    void updateFromSv(const QPointF& pos);
    void updateFromHue(const QPointF& pos);
    void updateHexEditStyle();

    QRect m_cardRect;
    QImage m_backdrop;
    QColor m_color = QColor("#1A1B22");
    qreal m_hue = 232.0 / 360.0;
    qreal m_sat = 0.235;
    qreal m_val = 0.133;
    DragMode m_drag = DragMode::None;
    bool m_hoverClose = false;
    bool m_dark = false;
    bool m_closing = false;
    bool m_grabbingBackdrop = false;
    qreal m_progress = 1.0;

    QLineEdit* m_hexEdit = nullptr;
    QGraphicsOpacityEffect* m_hexOpacity = nullptr;
    QPropertyAnimation* m_anim = nullptr;
};

} // namespace finily
