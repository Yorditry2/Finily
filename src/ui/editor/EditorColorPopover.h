#pragma once

#include "core/Types.h"
#include "ui/editor/EditorTheme.h"

#include <QColor>
#include <QVector>

namespace finily {

class EditorColorPopover : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorColorPopover(QWidget* parent = nullptr);

    void setColor(const QColor& color);
    QColor color() const { return m_color; }
    void applyEditorTheme(bool dark) override;

signals:
    void colorPicked(const QColor& color);
    void colorCommitted(const QColor& color);
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class DragMode { None, SvSquare, HueBar };

    QRectF svRect() const;
    QRectF hueRect() const;
    QRectF swatchRect(int index) const;
    void updateFromSv(const QPointF& pos);
    void updateFromHue(const QPointF& pos);
    void commitColor(const QColor& c);
    void rebuildSvCache();

    QColor m_color{0x02, 0x84, 0xC7};
    qreal m_hue = 0.55;
    qreal m_sat = 0.98;
    qreal m_val = 0.78;
    DragMode m_drag = DragMode::None;
    EditorLineEdit* m_hexEdit = nullptr;
    QVector<QColor> m_presets;
    QImage m_svCache;
    qreal m_cachedHue = -1.0;
};

class EditorFineWidthPopover : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorFineWidthPopover(QWidget* parent = nullptr);

    void configure(ToolType tool, double mm);
    double widthMm() const { return m_widthMm; }
    void applyEditorTheme(bool dark) override;

signals:
    void widthChanged(double mm);
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ToolType m_tool = ToolType::Pen;
    double m_widthMm = 2.4;
    double m_minMm = 0.5;
    double m_maxMm = 8.0;
    double m_stepMm = 0.2;
    EditorSlider* m_slider = nullptr;
};

} // namespace finily
