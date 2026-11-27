#include "ui/editor/EditorColorPopover.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace finily {

EditorColorPopover::EditorColorPopover(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedSize(264, 274);
    hide();

    m_presets = {
        QColor("#0F172A"), QColor("#0284C7"), QColor("#0D9488"),
        QColor("#65A30D"), QColor("#EA580C"), QColor("#E11D48"), QColor("#9333EA")
    };

    m_hexEdit = new EditorLineEdit(this);
    m_hexEdit->setGeometry(176, 12, 74, 26);
    m_hexEdit->setMaxLength(7);
    m_hexEdit->setAlignment(Qt::AlignCenter);
    connect(m_hexEdit, &QLineEdit::returnPressed, this, [this] {
        const QColor c(m_hexEdit->text().trimmed());
        if (c.isValid())
            commitColor(c);
    });

    setColor(m_color);
}

void EditorColorPopover::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    if (m_hexEdit)
        m_hexEdit->applyEditorTheme(dark);
}

void EditorColorPopover::setColor(const QColor& color) {
    if (!color.isValid())
        return;
    m_color = color;
    float h = 0.0f, s = 0.0f, v = 0.0f;
    m_color.getHsvF(&h, &s, &v);
    if (h >= 0.0f)
        m_hue = double(h);
    m_sat = double(s);
    m_val = double(v);
    if (m_hexEdit)
        m_hexEdit->setText(m_color.name(QColor::HexRgb).toUpper());
    update();
}

void EditorColorPopover::commitColor(const QColor& c) {
    setColor(c);
    emit colorPicked(m_color);
}

QRectF EditorColorPopover::svRect() const {
    return QRectF(14.0, 48.0, width() - 28.0, 112.0);
}

QRectF EditorColorPopover::hueRect() const {
    return QRectF(14.0, 170.0, width() - 28.0, 12.0);
}

QRectF EditorColorPopover::swatchRect(int index) const {
    const qreal x = 14.0 + index * 32.0;
    return QRectF(x, 214.0, 22.0, 22.0);
}

void EditorColorPopover::rebuildSvCache() {
    const QRectF sv = svRect();
    const int w = std::max(1, int(sv.width()));
    const int h = std::max(1, int(sv.height()));
    if (!m_svCache.isNull() && m_svCache.width() == w && m_svCache.height() == h &&
        std::abs(m_cachedHue - m_hue) < 1e-4) {
        return;
    }
    m_svCache = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        const qreal val = 1.0 - qreal(y) / std::max(1, h - 1);
        auto* row = reinterpret_cast<QRgb*>(m_svCache.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const qreal sat = qreal(x) / std::max(1, w - 1);
            row[x] = QColor::fromHsvF(float(m_hue), float(sat), float(val)).rgba();
        }
    }
    m_cachedHue = m_hue;
}

void EditorColorPopover::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14.0, 12.0, 156.0, 26.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Color personalizado"));

    rebuildSvCache();
    const QRectF sv = svRect();
    QPainterPath svClip;
    svClip.addRoundedRect(sv, 10.0, 10.0);
    p.save();
    p.setClipPath(svClip);
    p.drawImage(sv.topLeft(), m_svCache);
    p.restore();
    p.setPen(QPen(m_pal.border, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(sv, 10.0, 10.0);

    const QPointF thumb(sv.left() + m_sat * sv.width(), sv.top() + (1.0 - m_val) * sv.height());
    p.setPen(QPen(Qt::white, 2.2));
    p.setBrush(m_color);
    p.drawEllipse(thumb, 6.5, 6.5);

    const QRectF hue = hueRect();
    QLinearGradient hGrad(hue.topLeft(), hue.topRight());
    for (int i = 0; i <= 6; ++i)
        hGrad.setColorAt(i / 6.0, QColor::fromHsvF(i / 6.0f, 1.0f, 1.0f));
    p.setPen(QPen(m_pal.border, 1.0));
    p.setBrush(hGrad);
    p.drawRoundedRect(hue, 6.0, 6.0);

    const qreal hx = hue.left() + m_hue * hue.width();
    p.setPen(QPen(m_pal.text, 1.2));
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(hx - 3.5, hue.top() - 2.0, 7.0, hue.height() + 4.0), 3.5, 3.5);

    p.setFont(editorFont(10.0, QFont::Bold));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(14.0, 193.0, width() - 28.0, 15.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("TONOS RÁPIDOS"));

    for (int i = 0; i < m_presets.size(); ++i) {
        const bool active = (m_presets.at(i).name() == m_color.name());
        paintEditorSwatch(p, swatchRect(i), m_presets.at(i), active, m_pal);
    }

    p.setFont(editorFont(10.5, QFont::Normal));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(14.0, 245.0, width() - 28.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Sustituye el círculo seleccionado en la barra"));
}

void EditorColorPopover::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF pos = event->position();
    if (svRect().adjusted(-4, -4, 4, 4).contains(pos)) {
        m_drag = DragMode::SvSquare;
        updateFromSv(pos);
        event->accept();
        return;
    }
    if (hueRect().adjusted(-4, -4, 4, 4).contains(pos)) {
        m_drag = DragMode::HueBar;
        updateFromHue(pos);
        event->accept();
        return;
    }
    for (int i = 0; i < m_presets.size(); ++i) {
        if (swatchRect(i).adjusted(-3, -3, 3, 3).contains(pos)) {
            commitColor(m_presets.at(i));
            event->accept();
            return;
        }
    }
    EditorGlassCard::mousePressEvent(event);
}

void EditorColorPopover::mouseMoveEvent(QMouseEvent* event) {
    if (m_drag == DragMode::SvSquare)
        updateFromSv(event->position());
    else if (m_drag == DragMode::HueBar)
        updateFromHue(event->position());
}

void EditorColorPopover::mouseReleaseEvent(QMouseEvent*) {
    m_drag = DragMode::None;
}

void EditorColorPopover::updateFromSv(const QPointF& pos) {
    const QRectF sv = svRect();
    m_sat = std::clamp((pos.x() - sv.left()) / sv.width(), 0.0, 1.0);
    m_val = std::clamp(1.0 - (pos.y() - sv.top()) / sv.height(), 0.0, 1.0);
    commitColor(QColor::fromHsvF(float(m_hue), float(m_sat), float(m_val)));
}

void EditorColorPopover::updateFromHue(const QPointF& pos) {
    const QRectF hue = hueRect();
    m_hue = std::clamp((pos.x() - hue.left()) / hue.width(), 0.0, 0.999);
    commitColor(QColor::fromHsvF(float(m_hue), float(m_sat), float(m_val)));
}

// ---------------- EditorFineWidthPopover ----------------

EditorFineWidthPopover::EditorFineWidthPopover(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedSize(220, 76);
    hide();

    m_slider = new EditorSlider(this);
    m_slider->setGeometry(14, 44, 192, 20);
    connect(m_slider, &EditorSlider::valueChanged, this, [this](double val) {
        m_widthMm = val;
        update();
        emit widthChanged(m_widthMm);
    });
}

void EditorFineWidthPopover::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    if (m_slider)
        m_slider->applyEditorTheme(dark);
}

void EditorFineWidthPopover::configure(ToolType tool, double mm) {
    m_tool = tool;
    if (tool == ToolType::Highlighter) {
        m_minMm = 4.0;
        m_maxMm = 24.0;
        m_stepMm = 1.0;
    } else if (tool == ToolType::Eraser) {
        m_minMm = 4.0;
        m_maxMm = 40.0;
        m_stepMm = 1.0;
    } else {
        m_minMm = 0.5;
        m_maxMm = 8.0;
        m_stepMm = 0.2;
    }
    m_widthMm = std::clamp(mm, m_minMm, m_maxMm);
    m_slider->setRange(m_minMm, m_maxMm, m_stepMm);
    m_slider->setValue(m_widthMm);
    update();
}

void EditorFineWidthPopover::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(12.5, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14.0, 12.0, 130.0, 22.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Grosor exacto"));

    p.setFont(editorFont(11.5, QFont::DemiBold));
    p.setPen(m_pal.muted);
    const QString lbl = QString::number(m_widthMm, 'f', m_stepMm < 1.0 ? 1 : 0) + QStringLiteral(" mm");
    p.drawText(QRectF(138.0, 12.0, 68.0, 22.0), Qt::AlignRight | Qt::AlignVCenter, lbl);
}

} // namespace finily
