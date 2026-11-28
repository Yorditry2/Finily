#include "ui/editor/EditorToolPanels.h"

#include "core/ShapeProtocols.h"
#include "core/Utils.h"
#include "ui/CanvasWidget.h"
#include "ui/UiIcons.h"

#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace finily {

namespace {

void paintPenWaveSample(QPainter& p, const QRectF& box, int tipId, qreal mm, const QColor& col) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal x0 = box.left();
    const qreal x1 = box.right();
    const qreal cy = box.center().y();
    const qreal amp = box.height() * 0.28;
    const qreal base = std::max<qreal>(1.1, mm * 1.4);

    auto makeWave = [&]() {
        QPainterPath path;
        const int steps = 40;
        for (int i = 0; i <= steps; ++i) {
            const qreal t = qreal(i) / steps;
            const qreal x = x0 + (x1 - x0) * t;
            const qreal y = cy + std::sin(t * 6.28318530718) * amp;
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        return path;
    };

    if (tipId == 3) {
        // Tiza: 3 capas con textura punteada
        const QPainterPath wave = makeWave();
        for (int i = 0; i < 3; ++i) {
            QColor c = col;
            c.setAlphaF(std::clamp(0.35 + i * 0.20, 0.0, 1.0));
            QPen pen(c, base * (1.0 - i * 0.25), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            if (i > 0)
                pen.setDashPattern({1.0, 2.0});
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(wave);
        }
    } else if (tipId == 2) {
        // Grafito: grano fino
        const QPainterPath wave = makeWave();
        QColor c = col;
        c.setAlphaF(0.78);
        QPen pen(c, base * 0.80, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pen.setDashPattern({0.6, 0.9});
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(wave);
    } else if (tipId == 1) {
        // Pluma: grosor variable modulando con coseno
        constexpr int N = 28;
        auto yAt = [&](qreal x) {
            return cy + std::sin(((x - x0) / std::max<qreal>(1.0, x1 - x0)) * 6.28318530718) * amp;
        };
        for (int i = 0; i < N; ++i) {
            const qreal xa = x0 + (x1 - x0) * qreal(i) / N;
            const qreal xb = x0 + (x1 - x0) * qreal(i + 1) / N;
            const qreal k = 0.45 + 0.55 * std::abs(std::cos(qreal(i) / N * 6.28318530718));
            p.setPen(QPen(col, base * k, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(xa, yAt(xa)), QPointF(xb, yAt(xb)));
        }
    } else {
        // Bolígrafo: grosor uniforme
        const QPainterPath wave = makeWave();
        p.setPen(QPen(col, base * 0.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(wave);
    }
    p.restore();
}

void paintPenTipIllustration(QPainter& p, const QRectF& box, int tipId, const QColor& col) {
    // Recorte de la punta (viewBox "-2 0 46 30") inclinada -38°, exactamente como en 04c D1.
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(box, 12.0, 12.0);
    p.setClipPath(clip);

    p.translate(box.center());
    p.rotate(-38.0);
    p.translate(8.0, 2.0);
    // Mapear viewBox (-2, 0, 46, 30) en 58x40 centrado
    const qreal sx = 58.0 / 46.0;
    const qreal sy = 40.0 / 30.0;
    p.scale(sx, sy);
    p.translate(-21.0, -15.0);

    auto cylGrad = [&](qreal y0, qreal y1) {
        QLinearGradient g(0, y0, 0, y1);
        g.setColorAt(0.0, QColor(255, 255, 255, 97));
        g.setColorAt(0.35, QColor(255, 255, 255, 15));
        g.setColorAt(0.7, QColor(0, 0, 0, 20));
        g.setColorAt(1.0, QColor(0, 0, 0, 97));
        return g;
    };

    if (tipId == 1) {
        // Pluma fuente: plumín dorado + ranura/agujero + cuello negro + punto de tinta
        QLinearGradient gold(0, 7.5, 0, 22.5);
        gold.setColorAt(0.0, QColor("#f7dc9a"));
        gold.setColorAt(0.5, QColor("#d8ae5e"));
        gold.setColorAt(1.0, QColor("#9c7533"));
        QPainterPath nib;
        nib.moveTo(1, 15); nib.lineTo(27, 7.5); nib.lineTo(27, 22.5); nib.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(gold);
        p.drawPath(nib);

        p.setPen(QPen(QColor("#6b4f1f"), 1.0));
        p.drawLine(QPointF(3, 15), QPointF(21, 15));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#6b4f1f"));
        p.drawEllipse(QPointF(21, 15), 1.8, 1.8);

        QPainterPath collar;
        collar.moveTo(27, 8); collar.lineTo(45, 6.5); collar.lineTo(45, 23.5); collar.lineTo(27, 22); collar.closeSubpath();
        p.setBrush(QColor("#15171d"));
        p.drawPath(collar);
        p.setBrush(cylGrad(6.5, 23.5));
        p.drawPath(collar);

        p.setBrush(gold);
        p.drawRect(QRectF(45, 5.5, 4, 19));

        p.setBrush(col);
        p.drawEllipse(QPointF(2, 15), 2.0, 2.0);
    } else if (tipId == 0) {
        // Bolígrafo: punta acero + bolita de tinta + cono translúcido + grip con estrías
        QLinearGradient steel(0, 12.5, 0, 17.5);
        steel.setColorAt(0.0, QColor("#f4f5f7"));
        steel.setColorAt(0.5, QColor("#b9bcc4"));
        steel.setColorAt(1.0, QColor("#7d818b"));
        QPainterPath tip;
        tip.moveTo(1.5, 15); tip.lineTo(9, 12.5); tip.lineTo(9, 17.5); tip.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(steel);
        p.drawPath(tip);

        p.setBrush(col);
        p.drawEllipse(QPointF(2, 15), 1.6, 1.6);

        QPainterPath cone;
        cone.moveTo(9, 12.5); cone.lineTo(28, 8.5); cone.lineTo(28, 21.5); cone.lineTo(9, 17.5); cone.closeSubpath();
        p.setBrush(QColor(233, 235, 239, 235));
        p.drawPath(cone);
        p.setBrush(cylGrad(8.5, 21.5));
        p.drawPath(cone);

        p.setBrush(QColor("#30333b"));
        p.drawRoundedRect(QRectF(28, 7.5, 30, 15), 3, 3);
        p.setBrush(cylGrad(7.5, 22.5));
        p.drawRoundedRect(QRectF(28, 7.5, 30, 15), 3, 3);

        p.setPen(QPen(QColor("#1d1f25"), 1.2));
        for (qreal gx : {33.0, 38.0, 43.0})
            p.drawLine(QPointF(gx, 8), QPointF(gx, 22));
    } else if (tipId == 2) {
        // Grafito: mina teñida + madera + cuerpo hexagonal de 3 caras teñido
        QPainterPath lead;
        lead.moveTo(0.5, 15); lead.lineTo(9, 12); lead.lineTo(9, 18); lead.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawPath(lead);
        p.setBrush(cylGrad(12, 18));
        p.drawPath(lead);

        QLinearGradient wood(0, 5.5, 0, 24.5);
        wood.setColorAt(0.0, QColor("#f6dfb4"));
        wood.setColorAt(1.0, QColor("#d2a76a"));
        QPainterPath cone;
        cone.moveTo(9, 12); cone.lineTo(30, 5.5); cone.lineTo(30, 24.5); cone.lineTo(9, 18); cone.closeSubpath();
        p.setBrush(wood);
        p.drawPath(cone);

        p.setBrush(col);
        p.drawRect(QRectF(30, 5.5, 24, 19.0));
        p.setBrush(QColor(255, 255, 255, 72));
        p.drawRect(QRectF(30, 5.5, 24, 6.4));
        p.setBrush(QColor(0, 0, 0, 56));
        p.drawRect(QRectF(30, 18.1, 24, 6.4));

        QPainterPath scallop;
        scallop.moveTo(30, 5.5);
        scallop.quadTo(27, 8.6, 30, 11.8);
        scallop.quadTo(27, 15.0, 30, 18.2);
        scallop.quadTo(27, 21.4, 30, 24.5);
        p.setPen(QPen(QColor("#c99a58"), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(scallop);
    } else {
        // Tiza: barra gastada clara con textura porosa
        QPainterPath chalk;
        chalk.moveTo(6, 9);
        chalk.quadTo(2, 12, 3, 15.5);
        chalk.quadTo(2.5, 19, 6.5, 21.5);
        chalk.lineTo(9, 23);
        chalk.lineTo(54, 23);
        chalk.lineTo(54, 7);
        chalk.lineTo(9, 7);
        chalk.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawPath(chalk);
        p.setBrush(QColor(255, 255, 255, 128));
        p.drawPath(chalk);
        p.setBrush(cylGrad(7, 23));
        p.drawPath(chalk);

        QPainterPath edge;
        edge.moveTo(5, 10);
        edge.quadTo(2.5, 13, 3.2, 15.5);
        edge.quadTo(2.7, 18.5, 6, 21);
        QColor ec = col; ec.setAlpha(204);
        p.setPen(QPen(ec, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(edge);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 140));
        p.drawEllipse(QPointF(24, 11), 1.0, 1.0);
        p.drawEllipse(QPointF(41, 18), 1.3, 1.3);
        p.drawEllipse(QPointF(17, 18), 0.9, 0.9);
        p.drawEllipse(QPointF(33, 13), 1.1, 1.1);
    }

    p.restore();
}

} // namespace

// ---------------- EditorWidthSection ----------------

EditorWidthSection::EditorWidthSection(ToolType tool, double minMm, double maxMm, double stepMm,
                                       std::array<double, 5> presets, QWidget* parent)
    : QWidget(parent),
      m_tool(tool),
      m_minMm(minMm),
      m_maxMm(maxMm),
      m_stepMm(stepMm),
      m_presets(presets) {
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(36);
    m_pal = editorPaletteFor(false);

    m_slider = new EditorSlider(this);
    m_slider->setRange(minMm, maxMm, stepMm);
    m_slider->hide();
    connect(m_slider, &EditorSlider::valueChanged, this, [this](double mm) {
        m_currentMm = mm;
        if (m_canvas)
            m_canvas->setToolFineWidth(m_tool, mm);
        emit widthChanged(mm);
        update();
    });
}

void EditorWidthSection::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    if (m_slider)
        m_slider->applyEditorTheme(dark);
    update();
}

void EditorWidthSection::syncFromCanvas(CanvasWidget* canvas) {
    m_canvas = canvas;
    if (!canvas)
        return;
    const auto widths = canvas->toolWidths(m_tool);
    const int slot = std::clamp(canvas->activeWidthSlot(m_tool), 0, 2);
    m_currentMm = widths[size_t(slot)];
    if (m_tool == ToolType::Eraser) {
        m_inkColor = m_pal.text;
    } else {
        const auto pal = canvas->toolPalette(m_tool);
        if (!pal.isEmpty()) {
            const int idx = std::clamp(canvas->activeColorIndex(m_tool), 0, int(pal.size()) - 1);
            m_inkColor = (m_tool == ToolType::Highlighter)
                ? pal.at(idx)
                : resolveAdaptableInkColor(pal.at(idx), canvas->isCurrentSheetDark());
        } else {
            m_inkColor = m_pal.text;
        }
    }
    if (m_slider)
        m_slider->setValue(m_currentMm);
    update();
}

QRectF EditorWidthSection::valueRect() const {
    return QRectF(width() - 52.0, 0.0, 52.0, 36.0);
}

QRectF EditorWidthSection::dotButtonRect(int i) const {
    const qreal avail = std::max<qreal>(180.0, width() - 58.0);
    const qreal btnSz = 36.0;
    const qreal gap = std::max<qreal>(2.0, (avail - 5.0 * btnSz) / 4.0);
    return QRectF(i * (btnSz + gap), 0.0, btnSz, btnSz);
}

void EditorWidthSection::toggleFineSlider() {
    m_expanded = !m_expanded;
    if (m_expanded) {
        m_slider->setGeometry(0, 44, width(), 20);
        m_slider->show();
        setFixedHeight(66);
    } else {
        m_slider->hide();
        setFixedHeight(36);
    }
    emit layoutHeightChanged();
    update();
}

void EditorWidthSection::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < 5; ++i) {
        const QRectF sr = dotButtonRect(i);
        const bool active = (std::abs(m_presets[size_t(i)] - m_currentMm) < (m_stepMm < 1.0 ? 0.08 : 0.6));
        const bool hovered = (i == m_hoverSlot);
        p.setPen(Qt::NoPen);
        if (active)
            p.setBrush(m_dark ? QColor(255, 255, 255, 23) : m_pal.subtle);
        else if (hovered)
            p.setBrush(m_pal.hover);
        else
            p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(sr, 10.0, 10.0);

        qreal d = 4.0;
        if (m_tool == ToolType::Highlighter)
            d = std::clamp(m_presets[size_t(i)] * 0.72, 4.0, 18.0);
        else if (m_tool == ToolType::Eraser)
            d = std::clamp(m_presets[size_t(i)] * 0.44, 4.0, 18.0);
        else
            d = std::clamp(m_presets[size_t(i)] * 2.6, 3.0, 18.0);

        p.setBrush(m_inkColor);
        p.drawEllipse(sr.center(), d * 0.5, d * 0.5);
    }

    const QRectF vr = valueRect();
    if (m_hoverValue || m_expanded) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_pal.hover);
        p.drawRoundedRect(vr.adjusted(0, 5, 0, -5), 6.0, 6.0);
    }
    p.setFont(editorTabularFont(12.0, QFont::Medium));
    p.setPen(m_expanded ? m_pal.text : m_pal.muted);
    const QString mmTxt = QString::number(m_currentMm, 'f', m_stepMm < 1.0 ? 1 : 0) + QStringLiteral(" mm");
    p.drawText(vr, Qt::AlignRight | Qt::AlignVCenter, mmTxt);
}

void EditorWidthSection::mouseMoveEvent(QMouseEvent* e) {
    int hs = -1;
    for (int i = 0; i < 5; ++i) {
        if (dotButtonRect(i).contains(e->position())) {
            hs = i;
            break;
        }
    }
    const bool hv = valueRect().contains(e->position());
    if (hs != m_hoverSlot || hv != m_hoverValue) {
        m_hoverSlot = hs;
        m_hoverValue = hv;
        setCursor((hs >= 0 || hv) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorWidthSection::leaveEvent(QEvent*) {
    if (m_hoverSlot != -1 || m_hoverValue) {
        m_hoverSlot = -1;
        m_hoverValue = false;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorWidthSection::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < 5; ++i) {
        if (dotButtonRect(i).contains(e->position())) {
            m_currentMm = m_presets[size_t(i)];
            if (m_canvas)
                m_canvas->setToolFineWidth(m_tool, m_currentMm);
            if (m_slider)
                m_slider->setValue(m_currentMm);
            emit widthChanged(m_currentMm);
            update();
            return;
        }
    }
}

void EditorWidthSection::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && valueRect().contains(e->position())) {
        toggleFineSlider();
    }
}

void EditorWidthSection::tabletEvent(QTabletEvent* e) {
    e->ignore();
}

// ---------------- EditorColorRow ----------------

EditorColorRow::EditorColorRow(ToolType tool, QWidget* parent)
    : QWidget(parent),
      m_tool(tool) {
    setFixedHeight(34);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    m_pal = editorPaletteFor(false);
}

void EditorColorRow::applyEditorTheme(bool dark) {
    m_pal = editorPaletteFor(dark);
    update();
}

void EditorColorRow::syncFromCanvas(CanvasWidget* canvas) {
    m_canvas = canvas;
    if (!canvas)
        return;
    m_colors = canvas->toolPalette(m_tool);
    m_activeIdx = canvas->activeColorIndex(m_tool);
    m_darkSheet = canvas->isCurrentSheetDark();
    update();
}

QRectF EditorColorRow::swatchRect(int idx) const {
    const int count = int(m_colors.size()) + 1;
    constexpr qreal sz = 26.0;
    const qreal margin = 3.0;
    const qreal avail = std::max<qreal>(sz, width() - 2.0 * margin - sz);
    const qreal step = (count > 1) ? (avail / qreal(count - 1)) : 0.0;
    return QRectF(margin + idx * step, 4.0, sz, sz);
}

void EditorColorRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool adaptInk = (m_tool != ToolType::Highlighter && m_tool != ToolType::Laser);
    for (int i = 0; i < m_colors.size(); ++i) {
        const QColor c = adaptInk ? resolveAdaptableInkColor(m_colors.at(i), m_darkSheet) : m_colors.at(i);
        paintEditorSwatch(p, swatchRect(i), c, i == m_activeIdx, m_pal);
    }
    paintEditorRainbowSwatch(p, swatchRect(m_colors.size()),
                             m_activeIdx < 0 || m_activeIdx >= m_colors.size(), m_pal);
}

void EditorColorRow::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_canvas)
        return;
    for (int i = 0; i < m_colors.size(); ++i) {
        if (swatchRect(i).adjusted(-3, -3, 3, 3).contains(e->position())) {
            m_canvas->setToolColorIndex(m_tool, i);
            syncFromCanvas(m_canvas);
            emit colorChanged();
            return;
        }
    }
    const QRectF wr = swatchRect(m_colors.size());
    if (wr.adjusted(-3, -3, 3, 3).contains(e->position())) {
        const QWidget* topHost = parentWidget() ? parentWidget()->parentWidget() : nullptr;
        const QPoint p = topHost ? mapTo(const_cast<QWidget*>(topHost), wr.topLeft().toPoint()) : wr.topLeft().toPoint();
        emit customColorRequested(m_tool, QRect(p, wr.size().toSize()));
    }
}

void EditorColorRow::tabletEvent(QTabletEvent* e) {
    e->ignore();
}

// ---------------- 1. EditorPenPanel (D1) ----------------

EditorPenPanel::EditorPenPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(18.0);
    setFixedSize(360, 254); // 8 + 4*58 + 3*2 + 8
    setMouseTracking(true);
    hide();

    m_drawAnim = new QVariantAnimation(this);
    m_drawAnim->setDuration(320);
    m_drawAnim->setEasingCurve(editorEasingOut());
    connect(m_drawAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_drawProgress = v.toReal();
        update();
    });

    m_openAnim = new QVariantAnimation(this);
    m_openAnim->setDuration(254); // 200 ms + 3*18 ms stagger
    m_openAnim->setEasingCurve(QEasingCurve::Linear);
    connect(m_openAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_openProgress = v.toReal();
        const qreal panelT = std::clamp(m_openProgress * 254.0 / 200.0, 0.0, 1.0);
        setRevealProgress(editorEasingOut().valueForProgress(panelT));
        update();
    });

    syncFromCanvas();
}

void EditorPenPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
}

void EditorPenPanel::playOpenAnimation() {
    if (tokens::reducedMotion()) {
        m_openProgress = 1.0;
        setRevealProgress(1.0);
        update();
        return;
    }
    m_openAnim->stop();
    m_openProgress = 0.0;
    setRevealProgress(0.0);
    m_openAnim->setStartValue(0.0);
    m_openAnim->setEndValue(1.0);
    m_openAnim->start();
}

void EditorPenPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_activeTip = (m_canvas->tool() == ToolType::Ballpoint) ? 0 : m_canvas->penTip();
    for (int t = 0; t < 4; ++t)
        m_tipWidths[size_t(t)] = m_canvas->penTipWidth(t);
    const auto pal = m_canvas->toolPalette(ToolType::Pen);
    const int idx = std::clamp(m_canvas->activeColorIndex(ToolType::Pen), 0, std::max(0, int(pal.size()) - 1));
    m_activeColor = pal.isEmpty()
        ? resolveAdaptableInkColor(m_canvas->inkColor(), m_canvas->isCurrentSheetDark())
        : resolveAdaptableInkColor(pal.at(idx), m_canvas->isCurrentSheetDark());
    update();
}

QRectF EditorPenPanel::rowRect(int idx) const {
    return QRectF(8.0, 8.0 + idx * 60.0, width() - 16.0, 58.0);
}

void EditorPenPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    struct TipInfo { int id; const char* name; };
    const TipInfo tips[4] = {
        {1, "Pluma"},
        {0, "Bolígrafo"},
        {2, "Grafito"},
        {3, "Tiza"},
    };
    const QEasingCurve outCurve = editorEasingOut();

    for (int i = 0; i < 4; ++i) {
        const qreal elapsed = m_openProgress * 254.0;
        const qreal rowRaw = std::clamp((elapsed - i * 18.0) / 200.0, 0.0, 1.0);
        const qreal rowT = outCurve.valueForProgress(rowRaw);
        if (rowT <= 0.001)
            continue;

        p.save();
        p.setOpacity(rowT);
        p.translate(0.0, (1.0 - rowT) * 6.0);

        const QRectF r = rowRect(i);
        const bool active = (m_activeTip == tips[i].id);
        const bool hovered = (i == m_hoverRow);

        // Fondo de fila (hover 4%, activa acento 11%)
        if (active) {
            QColor bg = m_pal.accent;
            bg.setAlphaF(0.11);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, 12.0, 12.0);

            // Barrita de acento 3 px a la izquierda (top:18, bottom:18)
            p.setBrush(m_pal.accent);
            p.drawRoundedRect(QRectF(r.left(), r.top() + 18.0, 3.0, r.height() - 36.0), 1.5, 1.5);
        } else if (hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 10) : m_pal.hover);
            p.drawRoundedRect(r, 12.0, 12.0);
        }

        // 1. Cuadro de punta 46x46 (radio 12, fondo 4%, activa acento 16%)
        const QRectF tipBox(r.left() + 12.0, r.top() + 6.0, 46.0, 46.0);
        QColor tipBg = m_dark ? QColor(255, 255, 255, 10) : m_pal.subtle;
        if (active) {
            tipBg = m_pal.accent;
            tipBg.setAlphaF(0.16);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(tipBg);
        p.drawRoundedRect(tipBox, 12.0, 12.0);
        paintPenTipIllustration(p, tipBox, tips[i].id, m_activeColor);

        // 2. Nombre 13 px (ancho 84 px)
        p.setFont(editorFont(13.0, active ? QFont::DemiBold : QFont::Medium));
        p.setPen(active ? m_pal.text : (m_dark ? QColor("#A1A1AA") : m_pal.muted));
        p.drawText(QRectF(tipBox.right() + 14.0, r.top(), 84.0, r.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(tips[i].name));

        // 3. Trazo de muestra con grosor recordado de esa punta
        const QRectF smpBox(tipBox.right() + 14.0 + 84.0 + 14.0, r.top() + 12.0,
                            r.right() - 12.0 - (tipBox.right() + 14.0 + 84.0 + 14.0), 34.0);
        if (smpBox.width() > 20.0) {
            p.save();
            if (i == m_animRow && m_drawProgress < 0.999) {
                p.setClipRect(QRectF(smpBox.left(), smpBox.top() - 8.0,
                                     smpBox.width() * m_drawProgress, smpBox.height() + 16.0));
            }
            const qreal mm = m_tipWidths[size_t(tips[i].id)];
            paintPenWaveSample(p, smpBox, tips[i].id, mm, m_activeColor);
            p.restore();
        }

        p.restore();
    }
}

void EditorPenPanel::mouseMoveEvent(QMouseEvent* e) {
    int hr = -1;
    for (int i = 0; i < 4; ++i) {
        if (rowRect(i).contains(e->position())) {
            hr = i;
            break;
        }
    }
    if (hr != m_hoverRow) {
        m_hoverRow = hr;
        setCursor(hr >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorPenPanel::leaveEvent(QEvent*) {
    if (m_hoverRow != -1) {
        m_hoverRow = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorPenPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    const int tipIds[4] = {1, 0, 2, 3};
    for (int i = 0; i < 4; ++i) {
        if (rowRect(i).contains(e->position())) {
            const bool changed = (m_activeTip != tipIds[i]);
            if (m_canvas) {
                m_canvas->setTool(ToolType::Pen);
                m_canvas->setPenTip(tipIds[i]);
            }
            syncFromCanvas();
            if (changed && !tokens::reducedMotion()) {
                m_animRow = i;
                m_drawAnim->stop();
                m_drawProgress = 0.0;
                m_drawAnim->setStartValue(0.0);
                m_drawAnim->setEndValue(1.0);
                m_drawAnim->start();
            }
            return;
        }
    }
}

// ---------------- 2. EditorHighlighterPanel (R1) ----------------

EditorHighlighterPanel::EditorHighlighterPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(18.0);
    setFixedSize(300, 134);
    hide();

    m_opacitySlider = new EditorOpacitySlider(this);
    m_opacitySlider->setRange(0.15, 0.90, 0.01);
    m_opacitySlider->setGeometry(86, 96, 198, 28);
    connect(m_opacitySlider, &EditorOpacitySlider::valueChanged, this, [this](double op) {
        m_opacity = op;
        if (m_canvas)
            m_canvas->setHighlighterOpacity(op);
        update();
    });

    m_sweepAnim = new QVariantAnimation(this);
    m_sweepAnim->setDuration(320);
    m_sweepAnim->setEasingCurve(editorEasingOut());
    connect(m_sweepAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_sweepProgress = v.toReal();
        update();
    });

    m_openAnim = new QVariantAnimation(this);
    m_openAnim->setDuration(200);
    m_openAnim->setEasingCurve(editorEasingOut());
    connect(m_openAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        setRevealProgress(v.toReal());
    });

    syncFromCanvas();
}

void EditorHighlighterPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    if (m_opacitySlider)
        m_opacitySlider->applyEditorTheme(dark);
}

void EditorHighlighterPanel::playOpenAnimation() {
    if (tokens::reducedMotion()) {
        setRevealProgress(1.0);
        m_sweepProgress = 1.0;
        update();
        return;
    }
    m_openAnim->stop();
    setRevealProgress(0.0);
    m_openAnim->setStartValue(0.0);
    m_openAnim->setEndValue(1.0);
    m_openAnim->start();

    m_sweepAnim->stop();
    m_sweepProgress = 0.0;
    m_sweepAnim->setStartValue(0.0);
    m_sweepAnim->setEndValue(1.0);
    m_sweepAnim->start();
}

void EditorHighlighterPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    const auto widths = m_canvas->toolWidths(ToolType::Highlighter);
    m_activeMm = widths[size_t(std::clamp(m_canvas->activeWidthSlot(ToolType::Highlighter), 0, 2))];
    const auto pal = m_canvas->toolPalette(ToolType::Highlighter);
    const int idx = std::clamp(m_canvas->activeColorIndex(ToolType::Highlighter), 0, std::max(0, int(pal.size()) - 1));
    m_activeColor = pal.isEmpty() ? QColor("#FACC15") : pal.at(idx);
    m_opacity = std::clamp(m_canvas->highlighterOpacity(), 0.15, 0.90);
    m_opacitySlider->setColor(m_activeColor);
    m_opacitySlider->setValue(m_opacity);
    update();
}

void EditorHighlighterPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (revealProgress() < 0.999)
        p.setOpacity(revealProgress());

    // 1. Muestra manuscrita sobre papel (#f7f0d6) con resaltado en modo multiply
    const QRectF paper(10.0, 10.0, width() - 20.0, 72.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#F7F0D6"));
    p.drawRoundedRect(paper, 12.0, 12.0);

    QFont handFont(QStringLiteral("Segoe Print"));
    handFont.setFamilies({QStringLiteral("Segoe Print"), QStringLiteral("Patrick Hand"),
                          QStringLiteral("Comic Neue"), QStringLiteral("Inter")});
    handFont.setPixelSize(16);
    handFont.setWeight(QFont::Medium);
    p.setFont(handFont);

    const QFontMetricsF fm(handFont);
    const QString pre = QStringLiteral("La ");
    const QString word = QStringLiteral("fotosíntesis");
    const QString post = QStringLiteral(" ocurre");
    const QString line2 = QStringLiteral("en el cloroplasto.");

    const qreal tx = paper.left() + 16.0;
    const qreal y1 = paper.top() + 30.0;
    const qreal y2 = paper.top() + 54.0;
    const qreal preW = fm.horizontalAdvance(pre);
    const qreal wordW = fm.horizontalAdvance(word);

    // Pintar texto
    p.setPen(QColor("#27272A"));
    p.drawText(QPointF(tx, y1), pre + word + post);
    p.drawText(QPointF(tx, y2), line2);

    // Resaltado sobre "fotosíntesis" en modo multiply
    const qreal hlH = std::clamp(m_activeMm * 1.05, 6.0, 24.0);
    const qreal hlCenterY = y1 - fm.ascent() * 0.36;
    const QRectF hlRect(tx + preW - 3.0, hlCenterY - hlH * 0.5, wordW + 6.0, hlH);
    p.save();
    p.setClipRect(QRectF(hlRect.left(), paper.top(), hlRect.width() * m_sweepProgress, paper.height()));
    p.setCompositionMode(QPainter::CompositionMode_Multiply);
    QColor hlCol = m_activeColor;
    hlCol.setAlphaF(std::clamp(m_opacity, 0.15, 0.90));
    p.setPen(Qt::NoPen);
    p.setBrush(hlCol);
    p.drawRoundedRect(hlRect, 2.0, 2.0);
    p.restore();

    // 2. Etiqueta «Opacidad» (sin números ni %)
    p.setFont(editorFont(12.0, QFont::Medium));
    p.setPen(m_dark ? QColor("#A1A1AA") : m_pal.muted);
    p.drawText(QRectF(16.0, 96.0, 58.0, 28.0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Opacidad"));
}

// ---------------- 3. EditorEraserPanel (B3) ----------------

EditorEraserPanel::EditorEraserPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(18.0);
    setFixedSize(316, 248);
    setMouseTracking(true);
    hide();

    m_modeSeg = new EditorSegControl(this);
    m_modeSeg->setOptions({QStringLiteral("Trazo completo"), QStringLiteral("Solo lo que toco")});
    m_modeSeg->setGeometry(10, 10, 296, 36);
    connect(m_modeSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        if (m_canvas)
            m_canvas->setEraserMode(idx == 0 ? EraserMode::Stroke : EraserMode::Partial);
    });

    for (int i = 0; i < 3; ++i) {
        m_swAnims[size_t(i)] = new QVariantAnimation(this);
        m_swAnims[size_t(i)]->setDuration(200);
        m_swAnims[size_t(i)]->setEasingCurve(editorEasingSpring());
        connect(m_swAnims[size_t(i)], &QVariantAnimation::valueChanged, this, [this, i](const QVariant& v) {
            m_swAnimPos[size_t(i)] = v.toReal();
            update();
        });
    }

    m_openAnim = new QVariantAnimation(this);
    m_openAnim->setDuration(200);
    m_openAnim->setEasingCurve(editorEasingOut());
    connect(m_openAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        setRevealProgress(v.toReal());
    });

    syncFromCanvas();
}

void EditorEraserPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    if (m_modeSeg)
        m_modeSeg->applyEditorTheme(dark);
}

void EditorEraserPanel::playOpenAnimation() {
    if (tokens::reducedMotion()) {
        setRevealProgress(1.0);
        return;
    }
    m_openAnim->stop();
    setRevealProgress(0.0);
    m_openAnim->setStartValue(0.0);
    m_openAnim->setEndValue(1.0);
    m_openAnim->start();
}

void EditorEraserPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_modeSeg->setCurrentIndex(m_canvas->eraserMode() == EraserMode::Stroke ? 0 : 1);
    const auto t = m_canvas->eraserTargets();
    const std::array<bool, 3> next{t.ink, t.highlighter, t.shapes};
    for (int i = 0; i < 3; ++i) {
        m_targets[size_t(i)] = next[size_t(i)];
        m_swAnims[size_t(i)]->stop();
        m_swAnimPos[size_t(i)] = next[size_t(i)] ? 1.0 : 0.0;
    }
    update();
}

QRectF EditorEraserPanel::filterRowRect(int idx) const {
    return QRectF(10.0, 78.0 + idx * 36.0, width() - 20.0, 36.0);
}

QRectF EditorEraserPanel::clearLinkRect() const {
    return QRectF(10.0, 206.0, width() - 20.0, 32.0);
}

void EditorEraserPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (revealProgress() < 0.999)
        p.setOpacity(revealProgress());

    // Kicker «QUÉ BORRA»
    QFont capFont = editorFont(11.0, QFont::DemiBold);
    capFont.setLetterSpacing(QFont::PercentageSpacing, 105.0);
    p.setFont(capFont);
    p.setPen(m_dark ? QColor("#71717A") : m_pal.muted);
    p.drawText(QRectF(16.0, 56.0, width() - 32.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("QUÉ BORRA"));

    // Lista de 3 interruptores (Tinta, Resaltador, Figuras)
    struct RowSpec { const char* icon; const char* label; };
    const RowSpec rows[3] = {
        {"pen", "Tinta"},
        {"hl", "Resaltador"},
        {"shapes", "Figuras"},
    };
    for (int i = 0; i < 3; ++i) {
        const QRectF r = filterRowRect(i);
        const bool on = m_targets[size_t(i)];
        if (i == m_hoverRow) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 10) : m_pal.hover);
            p.drawRoundedRect(r, 10.0, 10.0);
        }

        const QRectF icR(r.left() + 6.0, r.center().y() - 9.0, 18.0, 18.0);
        paintUiIcon(&p, icR, QString::fromLatin1(rows[i].icon),
                    m_dark ? QColor("#A1A1AA") : m_pal.muted, QStringLiteral("tools"));

        p.setFont(editorFont(13.0, QFont::Medium));
        p.setPen(m_dark ? QColor("#D4D4D8") : m_pal.text);
        p.drawText(QRectF(icR.right() + 10.0, r.top(), r.width() - 78.0, r.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(rows[i].label));

        // Interruptor 30x18 px (gris apagado, acento prendido)
        const QRectF sw(r.right() - 36.0, r.center().y() - 9.0, 30.0, 18.0);
        const qreal pos = std::clamp(m_swAnimPos[size_t(i)], 0.0, 1.0);
        const QColor offBg = m_dark ? QColor("#3A3D48") : QColor("#D4D4D8");
        const QColor onBg = m_pal.accent;
        const QColor trackCol(
            int(std::round(offBg.red() + (onBg.red() - offBg.red()) * pos)),
            int(std::round(offBg.green() + (onBg.green() - offBg.green()) * pos)),
            int(std::round(offBg.blue() + (onBg.blue() - offBg.blue()) * pos)));
        p.setPen(Qt::NoPen);
        p.setBrush(trackCol);
        p.drawRoundedRect(sw, 9.0, 9.0);

        const qreal kx = sw.left() + 2.0 + pos * 12.0;
        p.setBrush(QColor("#FFFFFF"));
        p.drawEllipse(QRectF(kx, sw.top() + 2.0, 14.0, 14.0));
        Q_UNUSED(on);
    }

    // Separador hairline
    p.setPen(QPen(m_dark ? QColor(255, 255, 255, 15) : m_pal.borderSoft, 1.0));
    p.drawLine(QPointF(12.0, 196.5), QPointF(width() - 12.0, 196.5));

    // Enlace discreto «Borrar toda la página» (gris -> rojo al hover)
    const QRectF cl = clearLinkRect();
    const QColor linkCol = m_hoverClear ? m_pal.danger : (m_dark ? QColor("#A1A1AA") : m_pal.muted);
    const QRectF trIc(cl.left() + 6.0, cl.center().y() - 8.0, 16.0, 16.0);
    paintUiIcon(&p, trIc, QStringLiteral("trash"), linkCol);
    p.setFont(editorFont(12.5, QFont::Medium));
    p.setPen(linkCol);
    p.drawText(QRectF(trIc.right() + 8.0, cl.top(), cl.width() - 36.0, cl.height()),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Borrar toda la página"));
}

void EditorEraserPanel::mouseMoveEvent(QMouseEvent* e) {
    int hr = -1;
    for (int i = 0; i < 3; ++i) {
        if (filterRowRect(i).contains(e->position())) {
            hr = i;
            break;
        }
    }
    const bool hc = clearLinkRect().contains(e->position());
    if (hr != m_hoverRow || hc != m_hoverClear) {
        m_hoverRow = hr;
        m_hoverClear = hc;
        setCursor((hr >= 0 || hc) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorEraserPanel::leaveEvent(QEvent*) {
    if (m_hoverRow != -1 || m_hoverClear) {
        m_hoverRow = -1;
        m_hoverClear = false;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorEraserPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < 3; ++i) {
        if (filterRowRect(i).contains(e->position())) {
            m_targets[size_t(i)] = !m_targets[size_t(i)];
            const qreal targetPos = m_targets[size_t(i)] ? 1.0 : 0.0;
            if (!tokens::reducedMotion()) {
                m_swAnims[size_t(i)]->stop();
                m_swAnims[size_t(i)]->setStartValue(m_swAnimPos[size_t(i)]);
                m_swAnims[size_t(i)]->setEndValue(targetPos);
                m_swAnims[size_t(i)]->start();
            } else {
                m_swAnimPos[size_t(i)] = targetPos;
                update();
            }
            if (m_canvas) {
                EraserTargets t;
                t.ink = m_targets[0];
                t.highlighter = m_targets[1];
                t.shapes = m_targets[2];
                m_canvas->setEraserTargets(t);
            }
            return;
        }
    }
    if (clearLinkRect().contains(e->position())) {
        emit clearPageRequested();
    }
}

// ---------------- 4. EditorFillPanel (F3) ----------------

EditorFillPanel::EditorFillPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(18.0);
    setFixedSize(300, 122);
    setMouseTracking(true);
    hide();

    m_opacitySlider = new EditorOpacitySlider(this);
    m_opacitySlider->setRange(0.10, 1.0, 0.01);
    m_opacitySlider->setValue(0.60);
    m_opacitySlider->setGeometry(88, 84, 198, 28);
    connect(m_opacitySlider, &EditorOpacitySlider::valueChanged, this, [this](double val) {
        m_opacity = val;
        if (m_canvas)
            m_canvas->setShadeOpacity(val);
        update();
    });

    for (int i = 0; i < 5; ++i) {
        m_scaleAnims[size_t(i)] = new QVariantAnimation(this);
        m_scaleAnims[size_t(i)]->setDuration(200);
        m_scaleAnims[size_t(i)]->setEasingCurve(editorEasingSpring());
        connect(m_scaleAnims[size_t(i)], &QVariantAnimation::valueChanged, this, [this, i](const QVariant& v) {
            m_chipScale[size_t(i)] = v.toReal();
            update();
        });
    }

    m_openAnim = new QVariantAnimation(this);
    m_openAnim->setDuration(200);
    m_openAnim->setEasingCurve(editorEasingOut());
    connect(m_openAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        setRevealProgress(v.toReal());
    });

    syncFromCanvas();
}

void EditorFillPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    if (m_opacitySlider)
        m_opacitySlider->applyEditorTheme(dark);
}

void EditorFillPanel::playOpenAnimation() {
    if (tokens::reducedMotion()) {
        setRevealProgress(1.0);
        return;
    }
    m_openAnim->stop();
    setRevealProgress(0.0);
    m_openAnim->setStartValue(0.0);
    m_openAnim->setEndValue(1.0);
    m_openAnim->start();
}

QRectF EditorFillPanel::patternCardRect(int idx) const {
    const qreal gap = 6.0;
    const qreal sz = (width() - 20.0 - 4.0 * gap) / 5.0; // ~51.2 px
    return QRectF(10.0 + idx * (sz + gap), 10.0, sz, sz);
}

void EditorFillPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_pattern = m_canvas->shadePattern();
    if (m_pattern == ShadePattern::Soft)
        m_pattern = ShadePattern::Solid;
    m_opacity = std::clamp(m_canvas->shadeOpacity(), 0.10, 1.0);
    m_activeColor = resolveAdaptableInkColor(m_canvas->inkColor(), m_canvas->isCurrentSheetDark());
    if (m_opacitySlider) {
        m_opacitySlider->setColor(m_activeColor);
        m_opacitySlider->setValue(m_opacity);
    }
    const ShadePattern pats[5] = {
        ShadePattern::Solid, ShadePattern::Diagonal, ShadePattern::Dotted,
        ShadePattern::Crosshatch, ShadePattern::Grid
    };
    for (int i = 0; i < 5; ++i) {
        m_scaleAnims[size_t(i)]->stop();
        m_chipScale[size_t(i)] = (m_pattern == pats[i]) ? 1.04 : 1.0;
    }
    update();
}

void EditorFillPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (revealProgress() < 0.999)
        p.setOpacity(revealProgress());

    const ShadePattern pats[5] = {
        ShadePattern::Solid, ShadePattern::Diagonal, ShadePattern::Dotted,
        ShadePattern::Crosshatch, ShadePattern::Grid
    };

    for (int i = 0; i < 5; ++i) {
        const QRectF baseR = patternCardRect(i);
        const bool active = (m_pattern == pats[i]);
        const qreal sc = m_chipScale[size_t(i)];

        p.save();
        p.translate(baseR.center());
        p.scale(sc, sc);
        const QRectF cr(-baseR.width() * 0.5, -baseR.height() * 0.5, baseR.width(), baseR.height());

        // Marco exterior del parche (borde acento 2 px si activo)
        p.setPen(active ? QPen(m_pal.accent, 2.0)
                        : QPen(m_dark ? QColor(255, 255, 255, 16) : m_pal.borderSoft, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(cr.adjusted(1.0, 1.0, -1.0, -1.0), 12.0, 12.0);

        // Parche interior sobre fondo papel (#f7f0d6) con trama en color e intensidad actuales
        const QRectF inner = cr.adjusted(4.0, 4.0, -4.0, -4.0);
        QPainterPath clip;
        clip.addRoundedRect(inner, 8.0, 8.0);
        p.save();
        p.setClipPath(clip);
        p.fillRect(inner, QColor("#F7F0D6"));

        QColor col = m_activeColor;
        col.setAlphaF(std::clamp(m_opacity, 0.10, 1.0));

        if (pats[i] == ShadePattern::Solid) {
            p.fillRect(inner, col);
        } else if (pats[i] == ShadePattern::Diagonal || pats[i] == ShadePattern::Crosshatch) {
            p.setPen(QPen(col, 1.9, Qt::SolidLine, Qt::SquareCap));
            const qreal span = inner.width() + inner.height();
            for (qreal d = -inner.height(); d < span; d += 8.0)
                p.drawLine(QPointF(inner.left() + d, inner.bottom()),
                           QPointF(inner.left() + d + inner.height(), inner.top()));
            if (pats[i] == ShadePattern::Crosshatch) {
                for (qreal d = -inner.height(); d < span; d += 8.0)
                    p.drawLine(QPointF(inner.left() + d, inner.top()),
                               QPointF(inner.left() + d + inner.height(), inner.bottom()));
            }
        } else if (pats[i] == ShadePattern::Grid) {
            p.setPen(QPen(col, 1.8, Qt::SolidLine, Qt::SquareCap));
            for (qreal x = inner.left() + 6.0; x < inner.right(); x += 9.0)
                p.drawLine(QPointF(x, inner.top()), QPointF(x, inner.bottom()));
            for (qreal y = inner.top() + 6.0; y < inner.bottom(); y += 9.0)
                p.drawLine(QPointF(inner.left(), y), QPointF(inner.right(), y));
        } else if (pats[i] == ShadePattern::Dotted) {
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            for (qreal x = inner.left() + 5.0; x < inner.right(); x += 8.0)
                for (qreal y = inner.top() + 5.0; y < inner.bottom(); y += 8.0)
                    p.drawEllipse(QPointF(x, y), 1.8, 1.8);
        }
        p.restore();
        p.restore();
    }

    // Separador hairline
    p.setPen(QPen(m_dark ? QColor(255, 255, 255, 15) : m_pal.borderSoft, 1.0));
    p.drawLine(QPointF(12.0, 72.5), QPointF(width() - 12.0, 72.5));

    // Etiqueta «Intensidad» (sin números)
    p.setFont(editorFont(12.0, QFont::Medium));
    p.setPen(m_dark ? QColor("#A1A1AA") : m_pal.muted);
    p.drawText(QRectF(16.0, 84.0, 66.0, 28.0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Intensidad"));
}

void EditorFillPanel::mouseMoveEvent(QMouseEvent* e) {
    static const char* kNames[5] = {"Sólido", "Diagonal", "Puntos", "Cruzado", "Cuadrícula"};
    int hc = -1;
    for (int i = 0; i < 5; ++i)
        if (patternCardRect(i).contains(e->position())) { hc = i; break; }
    if (hc != m_hoverCard) {
        m_hoverCard = hc;
        setCursor(hc >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(hc >= 0 ? QString::fromUtf8(kNames[hc]) : QString());
        update();
    }
}

void EditorFillPanel::leaveEvent(QEvent*) {
    if (m_hoverCard != -1) {
        m_hoverCard = -1;
        setCursor(Qt::ArrowCursor);
        setToolTip(QString());
        update();
    }
}

void EditorFillPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    const ShadePattern pats[5] = {
        ShadePattern::Solid, ShadePattern::Diagonal, ShadePattern::Dotted,
        ShadePattern::Crosshatch, ShadePattern::Grid
    };
    for (int i = 0; i < 5; ++i) {
        if (patternCardRect(i).contains(e->position())) {
            m_pattern = pats[i];
            if (m_canvas)
                m_canvas->setShadePattern(m_pattern);
            for (int j = 0; j < 5; ++j) {
                const qreal target = (j == i) ? 1.04 : 1.0;
                if (!tokens::reducedMotion()) {
                    m_scaleAnims[size_t(j)]->stop();
                    m_scaleAnims[size_t(j)]->setStartValue(m_chipScale[size_t(j)]);
                    m_scaleAnims[size_t(j)]->setEndValue(target);
                    m_scaleAnims[size_t(j)]->start();
                } else {
                    m_chipScale[size_t(j)] = target;
                }
            }
            update();
            return;
        }
    }
}

// ---------------- 5. EditorLassoPanel (L1) ----------------

EditorLassoPanel::EditorLassoPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(18.0);
    setFixedSize(290, 150);
    setMouseTracking(true);
    hide();

    for (int i = 0; i < 3; ++i) {
        m_swAnims[size_t(i)] = new QVariantAnimation(this);
        m_swAnims[size_t(i)]->setDuration(200);
        m_swAnims[size_t(i)]->setEasingCurve(editorEasingSpring());
        connect(m_swAnims[size_t(i)], &QVariantAnimation::valueChanged, this, [this, i](const QVariant& v) {
            m_swAnimPos[size_t(i)] = v.toReal();
            update();
        });
    }

    m_shakeAnim = new QVariantAnimation(this);
    m_shakeAnim->setDuration(240);
    m_shakeAnim->setEasingCurve(QEasingCurve::Linear);
    connect(m_shakeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal t = v.toReal();
        m_shakeOffset = std::sin(t * 4.0 * 3.14159265) * 5.0 * (1.0 - t);
        update();
    });
    connect(m_shakeAnim, &QVariantAnimation::finished, this, [this] {
        m_shakeRow = -1;
        m_shakeOffset = 0.0;
        update();
    });

    m_openAnim = new QVariantAnimation(this);
    m_openAnim->setDuration(200);
    m_openAnim->setEasingCurve(editorEasingOut());
    connect(m_openAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        setRevealProgress(v.toReal());
    });

    syncFromCanvas();
}

void EditorLassoPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
}

void EditorLassoPanel::playOpenAnimation() {
    if (tokens::reducedMotion()) {
        setRevealProgress(1.0);
        return;
    }
    m_openAnim->stop();
    setRevealProgress(0.0);
    m_openAnim->setStartValue(0.0);
    m_openAnim->setEndValue(1.0);
    m_openAnim->start();
}

void EditorLassoPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    const auto f = m_canvas->lassoTargetFilter();
    const std::array<bool, 3> next{f.ink, f.shapes, f.media};
    for (int i = 0; i < 3; ++i) {
        m_filters[size_t(i)] = next[size_t(i)];
        m_swAnims[size_t(i)]->stop();
        m_swAnimPos[size_t(i)] = next[size_t(i)] ? 1.0 : 0.0;
    }
    update();
}

QRectF EditorLassoPanel::filterRowRect(int idx) const {
    return QRectF(10.0, 32.0 + idx * 36.0, width() - 20.0, 36.0);
}

void EditorLassoPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (revealProgress() < 0.999)
        p.setOpacity(revealProgress());

    QFont capFont = editorFont(11.0, QFont::DemiBold);
    capFont.setLetterSpacing(QFont::PercentageSpacing, 105.0);
    p.setFont(capFont);
    p.setPen(m_dark ? QColor("#71717A") : m_pal.muted);
    p.drawText(QRectF(16.0, 12.0, width() - 32.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("QUÉ SELECCIONA"));

    struct RowSpec { const char* icon; const char* label; };
    const RowSpec rows[3] = {
        {"pen", "Escritura"},
        {"shapes", "Figuras"},
        {"text", "Texto e imágenes"},
    };
    for (int i = 0; i < 3; ++i) {
        QRectF r = filterRowRect(i);
        if (i == m_shakeRow)
            r.translate(m_shakeOffset, 0.0);

        if (i == m_hoverRow) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 10) : m_pal.hover);
            p.drawRoundedRect(r, 10.0, 10.0);
        }

        const QRectF icR(r.left() + 6.0, r.center().y() - 9.0, 18.0, 18.0);
        paintUiIcon(&p, icR, QString::fromLatin1(rows[i].icon),
                    m_dark ? QColor("#A1A1AA") : m_pal.muted, QStringLiteral("tools"));

        p.setFont(editorFont(13.0, QFont::Medium));
        p.setPen(m_dark ? QColor("#D4D4D8") : m_pal.text);
        p.drawText(QRectF(icR.right() + 10.0, r.top(), r.width() - 78.0, r.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(rows[i].label));

        const QRectF sw(r.right() - 36.0, r.center().y() - 9.0, 30.0, 18.0);
        const qreal pos = std::clamp(m_swAnimPos[size_t(i)], 0.0, 1.0);
        const QColor offBg = m_dark ? QColor("#3A3D48") : QColor("#D4D4D8");
        const QColor onBg = m_pal.accent;
        const QColor trackCol(
            int(std::round(offBg.red() + (onBg.red() - offBg.red()) * pos)),
            int(std::round(offBg.green() + (onBg.green() - offBg.green()) * pos)),
            int(std::round(offBg.blue() + (onBg.blue() - offBg.blue()) * pos)));
        p.setPen(Qt::NoPen);
        p.setBrush(trackCol);
        p.drawRoundedRect(sw, 9.0, 9.0);

        const qreal kx = sw.left() + 2.0 + pos * 12.0;
        p.setBrush(QColor("#FFFFFF"));
        p.drawEllipse(QRectF(kx, sw.top() + 2.0, 14.0, 14.0));
    }
}

void EditorLassoPanel::mouseMoveEvent(QMouseEvent* e) {
    int hr = -1;
    for (int i = 0; i < 3; ++i) {
        if (filterRowRect(i).contains(e->position())) {
            hr = i;
            break;
        }
    }
    if (hr != m_hoverRow) {
        m_hoverRow = hr;
        setCursor(hr >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorLassoPanel::leaveEvent(QEvent*) {
    if (m_hoverRow != -1) {
        m_hoverRow = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorLassoPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < 3; ++i) {
        if (filterRowRect(i).contains(e->position())) {
            const int activeCount = (m_filters[0] ? 1 : 0) + (m_filters[1] ? 1 : 0) + (m_filters[2] ? 1 : 0);
            if (m_filters[size_t(i)] && activeCount <= 1) {
                // El último interruptor encendido no se puede apagar: la fila tiembla 240 ms
                if (!tokens::reducedMotion()) {
                    m_shakeRow = i;
                    m_shakeAnim->stop();
                    m_shakeAnim->setStartValue(0.0);
                    m_shakeAnim->setEndValue(1.0);
                    m_shakeAnim->start();
                }
                return;
            }
            m_filters[size_t(i)] = !m_filters[size_t(i)];
            const qreal targetPos = m_filters[size_t(i)] ? 1.0 : 0.0;
            if (!tokens::reducedMotion()) {
                m_swAnims[size_t(i)]->stop();
                m_swAnims[size_t(i)]->setStartValue(m_swAnimPos[size_t(i)]);
                m_swAnims[size_t(i)]->setEndValue(targetPos);
                m_swAnims[size_t(i)]->start();
            } else {
                m_swAnimPos[size_t(i)] = targetPos;
                update();
            }
            if (m_canvas) {
                LassoTargetFilter f;
                f.ink = m_filters[0];
                f.shapes = m_filters[1];
                f.media = m_filters[2];
                m_canvas->setLassoTargetFilter(f);
            }
            return;
        }
    }
}

// ---------------- 6. EditorShapesBubble + EditorShapesPanel (M1) ----------------

namespace {

struct ShapeVarEntry {
    int val;
    const char* name;
    const char* solidIcon; // solo para familia 5 (Sólidos 3D)
};

const QVector<ShapeVarEntry>& shapeFamilyVariants(int famIdx) {
    static const QVector<ShapeVarEntry> kLine = {
        {0, "Sin punta", nullptr},
        {1, "Flecha", nullptr},
        {2, "Doble flecha", nullptr},
        {3, "Cota", nullptr},
    };
    static const QVector<ShapeVarEntry> kCircle = {
        {0, "Círculo", nullptr},
        {3, "Elipse", nullptr},
    };
    static const QVector<ShapeVarEntry> kTriangle = {
        {0, "Libre", nullptr},
        {1, "Isósceles", nullptr},
        {2, "Rectángulo", nullptr},
        {3, "Equilátero", nullptr},
    };
    static const QVector<ShapeVarEntry> kQuad = {
        {0, "Rectángulo", nullptr},
        {1, "Cuadrado", nullptr},
        {2, "Rombo", nullptr},
        {3, "Paralelogramo", nullptr},
        {4, "Trapecio", nullptr},
        {6, "Trapezoide", nullptr},
    };
    static const QVector<ShapeVarEntry> kPolygon = {
        {5, "5 lados", nullptr},
        {6, "6 lados", nullptr},
        {7, "7 lados", nullptr},
        {8, "8 lados", nullptr},
    };
    static const QVector<ShapeVarEntry> kSolid = {
        {0, "Cubo", "shp-cube"},
        {1, "Prisma", "shp-prism"},
        {5, "Pirámide", "shp-pyramid"},
        {6, "Tetraedro", "shp-tetrahedron"},
        {7, "Octaedro", "shp-octahedron"},
        {2, "Cilindro", "shp-cylinder"},
        {3, "Cono", "shp-cone"},
        {4, "Esfera", "shp-sphere"},
        {8, "Tronco de cono", "shp-frustum-cone"},
        {9, "Tronco de pirámide", "shp-frustum-pyramid"},
    };
    switch (famIdx) {
    case 0: return kLine;
    case 1: return kCircle;
    case 2: return kTriangle;
    case 3: return kQuad;
    case 4: return kPolygon;
    default: return kSolid;
    }
}

const char* shapeFamilyName(int famIdx) {
    static const char* kNames[6] = {"Línea", "Círculo", "Triángulo", "Cuadrilátero", "Polígono", "Sólidos 3D"};
    return kNames[std::clamp(famIdx, 0, 5)];
}

QString shapeVariantLabel(int famIdx, int varVal) {
    const auto& vars = shapeFamilyVariants(famIdx);
    for (const auto& v : vars)
        if (v.val == varVal)
            return QString::fromUtf8(v.name);
    return vars.isEmpty() ? QString() : QString::fromUtf8(vars.first().name);
}

void paintFamilyGlyph(QPainter& p, const QRectF& box, int famIdx, int varVal, const QColor& col) {
    if (famIdx == 0) {
        paintLineEndingGlyph(p, box, varVal, col);
    } else if (famIdx == 1) {
        paintShapeVariantGlyph(p, box, ShapeFamily::Circle, varVal, col);
    } else if (famIdx == 2) {
        paintShapeVariantGlyph(p, box, ShapeFamily::Triangle, varVal, col);
    } else if (famIdx == 3) {
        paintShapeVariantGlyph(p, box, ShapeFamily::Quad, varVal, col);
    } else if (famIdx == 4) {
        paintShapeVariantGlyph(p, box, ShapeFamily::Polygon, varVal, col);
    } else {
        const auto& vars = shapeFamilyVariants(5);
        const char* ic = "shp-cube";
        for (const auto& v : vars) {
            if (v.val == varVal && v.solidIcon) {
                ic = v.solidIcon;
                break;
            }
        }
        paintUiIcon(&p, box, QString::fromLatin1(ic), col, QStringLiteral("editor"));
    }
}

} // namespace

EditorShapesBubble::EditorShapesBubble(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::PopoverSolid);
    setCornerRadius(14.0);
    setMouseTracking(true);
    hide();

    m_openAnim = new QVariantAnimation(this);
    m_openAnim->setDuration(200);
    m_openAnim->setEasingCurve(editorEasingSpring());
    connect(m_openAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_openT = v.toReal();
        setRevealProgress(std::clamp(m_openT, 0.0, 1.0));
        update();
    });

    m_slideAnim = new QVariantAnimation(this);
    m_slideAnim->setDuration(160);
    m_slideAnim->setEasingCurve(editorEasingOut());
    connect(m_slideAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        move(v.toPoint());
        emit visibilityChanged();
    });
}

void EditorShapesBubble::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
}

int EditorShapesBubble::itemCount() const {
    if (m_famIdx < 0 || m_famIdx > 5)
        return 0;
    return int(shapeFamilyVariants(m_famIdx).size());
}

QRectF EditorShapesBubble::itemRect(int idx) const {
    const bool isGrid = (m_famIdx == 5);
    const int col = isGrid ? (idx % 5) : idx;
    const int row = isGrid ? (idx / 5) : 0;
    constexpr qreal kTopPad = 10.0; // espacio para el pico triangular
    return QRectF(5.0 + col * 46.0, kTopPad + 5.0 + row * 46.0, 42.0, 42.0);
}

void EditorShapesBubble::openForFamily(int famIdx, int selectedVar, const QPoint& anchorCenterInParent,
                                       int panelBottomY, int hostWidth, bool animateOpen) {
    const bool wasVisible = isVisible();
    m_famIdx = std::clamp(famIdx, 0, 5);
    m_selectedVar = selectedVar;
    m_hoverIdx = -1;

    const int n = itemCount();
    const int cols = (m_famIdx == 5) ? 5 : n;
    const int rows = (m_famIdx == 5) ? 2 : 1;
    const int w = 10 + cols * 42 + std::max(0, cols - 1) * 4;
    const int h = 10 + 10 + rows * 42 + std::max(0, rows - 1) * 4;
    setFixedSize(w, h);

    const int targetX = std::clamp(anchorCenterInParent.x() - w / 2, 12, std::max(12, hostWidth - w - 12));
    const int targetY = panelBottomY; // el pico ocupa los 10 px superiores de la burbuja
    m_arrowX = std::clamp(qreal(anchorCenterInParent.x() - targetX), 18.0, qreal(w - 18));

    if (wasVisible && !animateOpen && !tokens::reducedMotion()) {
        m_slideAnim->stop();
        m_slideAnim->setStartValue(pos());
        m_slideAnim->setEndValue(QPoint(targetX, targetY));
        m_slideAnim->start();
    } else {
        m_slideAnim->stop();
        move(targetX, targetY);
    }

    show();
    raise();

    if (animateOpen && !tokens::reducedMotion()) {
        m_openAnim->stop();
        m_openT = 0.0;
        setRevealProgress(0.0);
        m_openAnim->setStartValue(0.0);
        m_openAnim->setEndValue(1.0);
        m_openAnim->start();
    } else {
        m_openAnim->stop();
        m_openT = 1.0;
        setRevealProgress(1.0);
    }

    emit visibilityChanged();
    update();
}

void EditorShapesBubble::closeBubble() {
    if (!isVisible())
        return;
    m_openAnim->stop();
    m_slideAnim->stop();
    m_famIdx = -1;
    m_hoverIdx = -1;
    hide();
    emit hoverVariantChanged(QString());
    emit visibilityChanged();
}

void EditorShapesBubble::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal op = std::clamp(m_openT, 0.0, 1.0);
    if (op < 0.999)
        p.setOpacity(op);

    // Escala desde el pico (0.9 -> 1.0)
    const qreal sc = 0.9 + 0.1 * m_openT;
    if (std::abs(sc - 1.0) > 0.001) {
        p.translate(m_arrowX, 4.0);
        p.scale(sc, sc);
        p.translate(-m_arrowX, -4.0);
    }

    const QRectF body(0.5, 10.5, width() - 1.0, height() - 11.0);
    const QColor bgCol = m_dark ? QColor(32, 35, 45, 252) : m_pal.surfaceSolid;
    const QColor borderCol = m_dark ? QColor(255, 255, 255, 23) : m_pal.border;

    // Forma unificada cuerpo + pico triangular de 12 px apuntando al botón
    QPainterPath bubble;
    bubble.addRoundedRect(body, 14.0, 14.0);
    QPainterPath peak;
    peak.moveTo(m_arrowX, 4.0);
    peak.lineTo(m_arrowX + 6.0, 11.0);
    peak.lineTo(m_arrowX - 6.0, 11.0);
    peak.closeSubpath();

    p.setPen(QPen(borderCol, 1.0));
    p.setBrush(bgCol);
    p.drawPath(bubble.united(peak));

    const auto& vars = shapeFamilyVariants(m_famIdx);
    for (int i = 0; i < vars.size(); ++i) {
        const QRectF r = itemRect(i);
        const bool selected = (vars[i].val == m_selectedVar);
        const bool hovered = (i == m_hoverIdx);

        if (selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor("#2E323D") : m_pal.subtle);
            p.drawRoundedRect(r, 10.0, 10.0);
        } else if (hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 15) : m_pal.hover);
            p.drawRoundedRect(r, 10.0, 10.0);
        }

        const QColor fg = (selected || hovered) ? m_pal.text : (m_dark ? QColor("#A1A1AA") : m_pal.muted);
        const QRectF glyphR(r.center().x() - 12.0, r.center().y() - 12.0, 24.0, 24.0);
        paintFamilyGlyph(p, glyphR, m_famIdx, vars[i].val, fg);
    }
}

void EditorShapesBubble::mouseMoveEvent(QMouseEvent* e) {
    const auto& vars = shapeFamilyVariants(m_famIdx);
    int hit = -1;
    for (int i = 0; i < vars.size(); ++i) {
        if (itemRect(i).contains(e->position())) {
            hit = i;
            break;
        }
    }
    if (hit != m_hoverIdx) {
        m_hoverIdx = hit;
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        emit hoverVariantChanged(hit >= 0 ? QString::fromUtf8(vars[hit].name) : QString());
        update();
    }
}

void EditorShapesBubble::leaveEvent(QEvent*) {
    if (m_hoverIdx != -1) {
        m_hoverIdx = -1;
        setCursor(Qt::ArrowCursor);
        emit hoverVariantChanged(QString());
        update();
    }
}

void EditorShapesBubble::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    const auto& vars = shapeFamilyVariants(m_famIdx);
    for (int i = 0; i < vars.size(); ++i) {
        if (itemRect(i).contains(e->position())) {
            const int fam = m_famIdx;
            const int val = vars[i].val;
            closeBubble();
            emit variantChosen(fam, val);
            return;
        }
    }
}

EditorShapesPanel::EditorShapesPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedSize(308, 82); // 6 + 6*46 + 5*4 + 6 = 308; 6 + 46 + 24 + 6 = 82
    setMouseTracking(true);
    hide();

    loadSavedVariants();

    m_bubble = new EditorShapesBubble(parent);
    connect(m_bubble, &EditorShapesBubble::variantChosen, this, [this](int famIdx, int varVal) {
        m_activeFam = std::clamp(famIdx, 0, 5);
        m_lastVar[size_t(m_activeFam)] = varVal;
        saveVariants();
        applyFamilyVariantToCanvas(m_activeFam, varVal);
        update();
    });
    connect(m_bubble, &EditorShapesBubble::hoverVariantChanged, this, [this](const QString& lbl) {
        m_hoverBubbleLabel = lbl;
        update();
    });
    connect(m_bubble, &EditorShapesBubble::visibilityChanged, this, &EditorShapesPanel::bubbleVisibilityChanged);

    m_openAnim = new QVariantAnimation(this);
    m_openAnim->setDuration(200);
    m_openAnim->setEasingCurve(editorEasingOut());
    connect(m_openAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        setRevealProgress(v.toReal());
    });

    syncFromCanvas();
}

void EditorShapesPanel::loadSavedVariants() {
    m_activeFam = std::clamp(readAppSetting(QStringLiteral("shapes/lastFamily"), 3).toInt(), 0, 5);
    const std::array<int, 6> defs{1, 0, 1, 0, 6, 0};
    for (int i = 0; i < 6; ++i) {
        m_lastVar[size_t(i)] = readAppSetting(QStringLiteral("shapes/lastVar_%1").arg(i), defs[size_t(i)]).toInt();
    }
}

void EditorShapesPanel::saveVariants() const {
    writeAppSetting(QStringLiteral("shapes/lastFamily"), m_activeFam);
    for (int i = 0; i < 6; ++i)
        writeAppSetting(QStringLiteral("shapes/lastVar_%1").arg(i), m_lastVar[size_t(i)]);
}

void EditorShapesPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    if (m_bubble)
        m_bubble->applyEditorTheme(dark);
}

void EditorShapesPanel::playOpenAnimation() {
    if (tokens::reducedMotion()) {
        setRevealProgress(1.0);
        return;
    }
    m_openAnim->stop();
    setRevealProgress(0.0);
    m_openAnim->setStartValue(0.0);
    m_openAnim->setEndValue(1.0);
    m_openAnim->start();
}

void EditorShapesPanel::closeBubble() {
    if (m_bubble)
        m_bubble->closeBubble();
}

void EditorShapesPanel::hideEvent(QHideEvent* event) {
    EditorGlassCard::hideEvent(event);
    closeBubble();
}

QRectF EditorShapesPanel::familyBtnRect(int idx) const {
    return QRectF(6.0 + idx * 50.0, 6.0, 46.0, 46.0);
}

void EditorShapesPanel::applyFamilyVariantToCanvas(int famIdx, int varVal) {
    if (!m_canvas)
        return;
    m_canvas->setTool(ToolType::Shape);
    m_canvas->setCongruenceMarkSpec(QString());
    if (famIdx == 0) {
        // Línea: 0 Sin punta, 1 Flecha, 2 Doble flecha, 3 Cota
        m_canvas->setShapeKind(ShapeKind::Line);
        if (varVal == 0) {
            m_canvas->setLineEnding(LineEnding::None);
            if (m_canvas->linePattern() == LinePattern::Dimension)
                m_canvas->setLinePattern(LinePattern::Solid);
        } else if (varVal == 1) {
            m_canvas->setLineEnding(LineEnding::Arrow);
            if (m_canvas->linePattern() == LinePattern::Dimension)
                m_canvas->setLinePattern(LinePattern::Solid);
        } else if (varVal == 2) {
            m_canvas->setLineEnding(LineEnding::DoubleArrow);
            if (m_canvas->linePattern() == LinePattern::Dimension)
                m_canvas->setLinePattern(LinePattern::Solid);
        } else {
            m_canvas->setLineEnding(LineEnding::DoubleArrow);
            m_canvas->setLinePattern(LinePattern::Dimension);
        }
    } else if (famIdx == 1) {
        // Círculo / Elipse
        m_canvas->setShapeKind(varVal == 3 ? ShapeKind::Ellipse : ShapeKind::Circle);
    } else if (famIdx == 2) {
        // Triángulo: 0 Libre, 1 Isósceles, 2 Rectángulo, 3 Equilátero
        m_canvas->setShapeKind(ShapeKind::Triangle);
        m_canvas->setTriangleVariant(varVal);
    } else if (famIdx == 3) {
        // Cuadrilátero
        if (varVal == 0) {
            m_canvas->setShapeKind(ShapeKind::Rectangle);
            m_canvas->setQuadVariant(0);
        } else {
            m_canvas->setShapeKind(ShapeKind::Polyline);
            m_canvas->setQuadVariant(varVal);
        }
    } else if (famIdx == 4) {
        // Polígono 5..8 lados
        m_canvas->setQuadVariant(0);
        m_canvas->setShapeKind(ShapeKind::Polyline);
        m_canvas->setPolygonSides(std::clamp(varVal, 5, 8));
    } else {
        // Sólidos 3D
        m_canvas->setShapeKind(ShapeKind::Solid3D);
        m_canvas->setSolidKind(std::clamp(varVal, 0, 9));
    }
}

void EditorShapesPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    const ShapeKind sk = m_canvas->shapeKind();
    if (sk == ShapeKind::Line) {
        m_activeFam = 0;
        if (m_canvas->linePattern() == LinePattern::Dimension) m_lastVar[0] = 3;
        else if (m_canvas->lineEnding() == LineEnding::DoubleArrow) m_lastVar[0] = 2;
        else if (m_canvas->lineEnding() == LineEnding::Arrow) m_lastVar[0] = 1;
        else m_lastVar[0] = 0;
    } else if (sk == ShapeKind::Circle || sk == ShapeKind::Arc || sk == ShapeKind::Sector) {
        m_activeFam = 1;
        m_lastVar[1] = 0;
    } else if (sk == ShapeKind::Ellipse) {
        m_activeFam = 1;
        m_lastVar[1] = 3;
    } else if (sk == ShapeKind::Triangle) {
        m_activeFam = 2;
        m_lastVar[2] = std::clamp(m_canvas->triangleVariant(), 0, 3);
    } else if (sk == ShapeKind::Rectangle) {
        m_activeFam = 3;
        m_lastVar[3] = 0;
    } else if (sk == ShapeKind::Polyline) {
        if (m_canvas->quadVariant() > 0) {
            m_activeFam = 3;
            m_lastVar[3] = m_canvas->quadVariant();
        } else {
            m_activeFam = 4;
            m_lastVar[4] = std::clamp(m_canvas->polygonSides(), 5, 8);
        }
    } else if (sk == ShapeKind::Solid3D) {
        m_activeFam = 5;
        m_lastVar[5] = std::clamp(m_canvas->solidKind(), 0, 9);
    }
    update();
}

void EditorShapesPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (revealProgress() < 0.999)
        p.setOpacity(revealProgress());

    for (int i = 0; i < 6; ++i) {
        const QRectF r = familyBtnRect(i);
        const bool active = (i == m_activeFam);
        const bool hovered = (i == m_hoverFam);

        if (active) {
            QColor bg = m_pal.accent;
            bg.setAlphaF(0.16);
            p.setPen(QPen(m_pal.accent, 1.5));
            p.setBrush(bg);
            p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), 12.0, 12.0);
        } else if (hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 15) : m_pal.hover);
            p.drawRoundedRect(r, 12.0, 12.0);
        }

        const QColor fg = (active || hovered) ? m_pal.text : (m_dark ? QColor("#A1A1AA") : m_pal.muted);
        const QRectF glyphR(r.center().x() - 13.0, r.center().y() - 13.0, 26.0, 26.0);
        paintFamilyGlyph(p, glyphR, i, m_lastVar[size_t(i)], fg);
        paintEditorFamilyCorner(p, r, fg);
    }

    // Línea inferior con el nombre: «Familia · Variante» (en negrita la variante)
    const QRectF nameR(6.0, 56.0, width() - 12.0, 20.0);
    if (!m_hoverBubbleLabel.isEmpty()) {
        p.setFont(editorFont(12.0, QFont::DemiBold));
        p.setPen(m_pal.text);
        p.drawText(nameR, Qt::AlignCenter, m_hoverBubbleLabel);
    } else if (m_hoverFam >= 0) {
        p.setFont(editorFont(12.0, QFont::DemiBold));
        p.setPen(m_pal.text);
        p.drawText(nameR, Qt::AlignCenter, QString::fromUtf8(shapeFamilyName(m_hoverFam)));
    } else {
        const QString famStr = QString::fromUtf8(shapeFamilyName(m_activeFam));
        const QString varStr = shapeVariantLabel(m_activeFam, m_lastVar[size_t(m_activeFam)]);
        if (varStr == famStr || varStr.isEmpty()) {
            p.setFont(editorFont(12.0, QFont::Medium));
            p.setPen(m_dark ? QColor("#8B8D98") : m_pal.muted);
            p.drawText(nameR, Qt::AlignCenter, famStr);
        } else {
            const QString prefix = famStr + QStringLiteral(" · ");
            const QFont fReg = editorFont(12.0, QFont::Medium);
            const QFont fBold = editorFont(12.0, QFont::DemiBold);
            const qreal w1 = QFontMetricsF(fReg).horizontalAdvance(prefix);
            const qreal w2 = QFontMetricsF(fBold).horizontalAdvance(varStr);
            const qreal sx = nameR.center().x() - (w1 + w2) * 0.5;
            p.setFont(fReg);
            p.setPen(m_dark ? QColor("#8B8D98") : m_pal.muted);
            p.drawText(QRectF(sx, nameR.top(), w1 + 2.0, nameR.height()), Qt::AlignLeft | Qt::AlignVCenter, prefix);
            p.setFont(fBold);
            p.setPen(m_pal.text);
            p.drawText(QRectF(sx + w1, nameR.top(), w2 + 2.0, nameR.height()), Qt::AlignLeft | Qt::AlignVCenter, varStr);
        }
    }
}

void EditorShapesPanel::mouseMoveEvent(QMouseEvent* e) {
    int hf = -1;
    for (int i = 0; i < 6; ++i) {
        if (familyBtnRect(i).contains(e->position())) {
            hf = i;
            break;
        }
    }
    if (hf != m_hoverFam) {
        m_hoverFam = hf;
        setCursor(hf >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorShapesPanel::leaveEvent(QEvent*) {
    if (m_hoverFam != -1) {
        m_hoverFam = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorShapesPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < 6; ++i) {
        if (familyBtnRect(i).contains(e->position())) {
            const bool sameOpen = (m_bubble && m_bubble->isVisible() && m_bubble->familyIndex() == i);
            m_activeFam = i;
            saveVariants();
            applyFamilyVariantToCanvas(i, m_lastVar[size_t(i)]);
            if (sameOpen) {
                closeBubble();
            } else if (m_bubble && parentWidget()) {
                const bool wasClosed = !m_bubble->isVisible();
                const QPoint btnCenter = mapTo(parentWidget(), familyBtnRect(i).center().toPoint());
                const int panelBottom = geometry().bottom() + 1;
                m_bubble->openForFamily(i, m_lastVar[size_t(i)], btnCenter, panelBottom,
                                        parentWidget()->width(), wasClosed);
            }
            update();
            return;
        }
    }
}

// ---------------- 8. EditorPaperPanel ----------------

EditorPaperPanel::EditorPaperPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedWidth(312);
    setMouseTracking(true);
    hide();

    m_formatSeg = new EditorSegControl(this);
    m_formatSeg->setOptions({QStringLiteral("A4 ↕"), QStringLiteral("A4 ↔"), QStringLiteral("Carta"), QStringLiteral("16:9")});
    connect(m_formatSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        if (!m_canvas)
            return;
        const PageFormat fmts[4] = {PageFormat::A4, PageFormat::A4Landscape, PageFormat::Letter, PageFormat::Slide16x9};
        m_canvas->setPageFormat(fmts[std::clamp(idx, 0, 3)]);
        syncFromCanvas();
    });

    m_scaleSeg = new EditorSegControl(this);
    m_scaleSeg->setOptions({QStringLiteral("4 mm"), QStringLiteral("5 mm"), QStringLiteral("7 mm"), QStringLiteral("10 mm")});
    connect(m_scaleSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        if (!m_canvas)
            return;
        const double scales[4] = {0.8, 1.0, 1.4, 2.0};
        m_canvas->setTemplateScale(scales[std::clamp(idx, 0, 3)]);
    });

    syncFromCanvas();
}

int EditorPaperPanel::templateCount() const {
    return m_infinite ? 6 : 8;
}

EditorPaperPanel::TplDef EditorPaperPanel::templateAt(int i) const {
    static const TplDef kDefs[8] = {
        {BackgroundTemplate::Blank, "Lisa"},
        {BackgroundTemplate::Grid, "Cuadríc."},
        {BackgroundTemplate::Dots, "Puntos"},
        {BackgroundTemplate::Ruled, "Rayado"},
        {BackgroundTemplate::Engineering, "Milim."},
        {BackgroundTemplate::Isometric, "Isomét."},
        {BackgroundTemplate::Cornell, "Cornell"},
        {BackgroundTemplate::School, "Escolar"},
    };
    return kDefs[std::clamp(i, 0, 7)];
}

void EditorPaperPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_formatSeg->applyEditorTheme(dark);
    m_scaleSeg->applyEditorTheme(dark);
    syncFromCanvas();
}

void EditorPaperPanel::relayout() {
    int y = 42;
    if (!m_infinite) {
        m_formatSeg->setGeometry(14, y, width() - 28, 30);
        m_formatSeg->show();
        y += 40;
    } else {
        m_formatSeg->hide();
    }
    // Bloque de color: cabecera (16) + círculos (28) + gap (10)
    y += 54;
    // Bloque de plantillas: cabecera (16) + 2 filas de tarjetas con etiqueta (56 px cada fila + 8 gap)
    const int tplRows = 2;
    const int tplH = tplRows * 56 + (tplRows - 1) * 8;
    y += 18 + tplH;

    if (!m_infinite) {
        y += 28;
        m_scaleSeg->setGeometry(14, y, width() - 28, 30);
        m_scaleSeg->setEnabled(m_tpl != BackgroundTemplate::Blank);
        m_scaleSeg->show();
        y += 30 + 14;
    } else {
        m_scaleSeg->hide();
        y += 14;
    }
    setFixedSize(312, y);
    emit panelResized();
    update();
}

void EditorPaperPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_infinite = (m_canvas->pageFormat() == PageFormat::Infinite);

    int fIdx = -1;
    switch (m_canvas->pageFormat()) {
    case PageFormat::A4: fIdx = 0; break;
    case PageFormat::A4Landscape: fIdx = 1; break;
    case PageFormat::Letter: fIdx = 2; break;
    case PageFormat::Slide16x9: fIdx = 3; break;
    default: fIdx = -1; break;
    }
    m_formatSeg->setCurrentIndex(fIdx);

    m_sheetColors = m_dark
        ? QVector<QColor>{QColor("#1A1B22"), QColor("#13241B"), QColor("#18202C"),
                          QColor("#23201E"), QColor("#FFFFFF"), QColor("#FBF7EE")}
        : QVector<QColor>{QColor("#FFFFFF"), QColor("#FBF7EE"), QColor("#F4F6F8"),
                          QColor("#EEF5F0"), QColor("#1A1B22"), QColor("#152A20")};

    m_currentBg = m_canvas->customBackground().isValid()
        ? m_canvas->customBackground()
        : effectiveThemeConfig(m_canvas->themeId(), {}).background;
    m_tpl = m_canvas->backgroundTemplate();

    const double sc = m_canvas->templateScale();
    int sIdx = 1;
    if (sc < 0.9) sIdx = 0;
    else if (sc > 1.7) sIdx = 3;
    else if (sc > 1.2) sIdx = 2;
    m_scaleSeg->setCurrentIndex(sIdx);

    relayout();
}

QRectF EditorPaperPanel::colorRect(int i) const {
    const int baseY = (m_infinite ? 42 : 82) + 20;
    const qreal step = (width() - 28.0 - 22.0) / 6.0;
    return QRectF(14.0 + i * step, baseY, 22.0, 22.0);
}

QRectF EditorPaperPanel::templateRect(int i) const {
    const int baseY = (m_infinite ? 42 : 82) + 54 + 18;
    const int cols = m_infinite ? 3 : 4;
    const qreal gap = 8.0;
    const qreal w = (width() - 28.0 - gap * (cols - 1)) / cols;
    const int col = i % cols;
    const int row = i / cols;
    return QRectF(14.0 + col * (w + gap), baseY + row * (56.0 + gap), w, 56.0);
}

void EditorPaperPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, 130, 22), Qt::AlignLeft | Qt::AlignVCenter,
               m_infinite ? QStringLiteral("Fondo") : QStringLiteral("Hoja y fondo"));

    QString fmtStr = QStringLiteral("Pizarra ∞");
    if (!m_infinite && m_canvas) {
        switch (m_canvas->pageFormat()) {
        case PageFormat::A4Landscape: fmtStr = QStringLiteral("Cuaderno A4 ↔"); break;
        case PageFormat::Letter: fmtStr = QStringLiteral("Cuaderno Carta"); break;
        case PageFormat::Slide16x9: fmtStr = QStringLiteral("Cuaderno 16:9"); break;
        default: fmtStr = QStringLiteral("Cuaderno A4 ↕"); break;
        }
    }
    p.setFont(editorFont(11.5, QFont::Medium));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(145, 12, width() - 159, 22), Qt::AlignRight | Qt::AlignVCenter, fmtStr);

    const int cy = m_infinite ? 42 : 82;
    p.setFont(editorFont(10.0, QFont::Bold));
    p.drawText(QRectF(14, cy, width() - 28, 16), Qt::AlignLeft | Qt::AlignVCenter,
               m_infinite ? QStringLiteral("COLOR DE FONDO") : QStringLiteral("COLOR DE HOJA"));

    bool matchedPreset = false;
    for (int i = 0; i < m_sheetColors.size(); ++i) {
        const bool active = (m_sheetColors.at(i).name().toLower() == m_currentBg.name().toLower());
        matchedPreset = matchedPreset || active;
        paintEditorSwatch(p, colorRect(i), m_sheetColors.at(i), active, m_pal);
    }
    paintEditorRainbowSwatch(p, colorRect(m_sheetColors.size()), !matchedPreset, m_pal);

    p.setFont(editorFont(10.0, QFont::Bold));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(14, cy + 54, width() - 28, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("PLANTILLA"));

    const ThemeConfig sheetTheme = effectiveThemeConfig(
        m_canvas ? m_canvas->themeId() : ThemeId::PaperLight, m_currentBg);
    const int count = templateCount();
    for (int i = 0; i < count; ++i) {
        const TplDef def = templateAt(i);
        const QRectF card = templateRect(i);
        const QRectF previewRect(card.left(), card.top(), card.width(), card.height() - 16.0);
        const bool active = (m_tpl == def.tpl);
        const bool hover = (m_hoverTpl == i);

        p.save();
        p.setPen(QPen(active ? m_pal.accent : (hover ? m_pal.text : m_pal.border), active ? 1.8 : 1.0));
        p.setBrush(sheetTheme.background);
        p.drawRoundedRect(previewRect, 8.0, 8.0);

        QPainterPath clip;
        clip.addRoundedRect(previewRect.adjusted(1.0, 1.0, -1.0, -1.0), 7.0, 7.0);
        p.setClipPath(clip);

        QColor lineCol = sheetTheme.patternColor;
        lineCol.setAlpha(std::max(lineCol.alpha(), sheetTheme.isDark ? 95 : 85));
        if (def.tpl == BackgroundTemplate::Grid) {
            p.setPen(QPen(lineCol, 0.9));
            for (qreal x = previewRect.left() + 10; x < previewRect.right() - 2; x += 10.0)
                p.drawLine(QPointF(x, previewRect.top() + 2), QPointF(x, previewRect.bottom() - 2));
            for (qreal y = previewRect.top() + 10; y < previewRect.bottom() - 2; y += 10.0)
                p.drawLine(QPointF(previewRect.left() + 2, y), QPointF(previewRect.right() - 2, y));
        } else if (def.tpl == BackgroundTemplate::Dots) {
            p.setPen(Qt::NoPen);
            p.setBrush(lineCol);
            for (qreal x = previewRect.left() + 9; x < previewRect.right() - 3; x += 9.0)
                for (qreal y = previewRect.top() + 9; y < previewRect.bottom() - 3; y += 9.0)
                    p.drawEllipse(QPointF(x, y), 1.1, 1.1);
        } else if (def.tpl == BackgroundTemplate::Ruled) {
            QColor ruleCol = sheetTheme.isDark ? lineCol : QColor(90, 150, 220, 145);
            p.setPen(QPen(ruleCol, 0.9));
            for (qreal y = previewRect.top() + 9; y < previewRect.bottom() - 3; y += 7.5)
                p.drawLine(QPointF(previewRect.left() + 4, y), QPointF(previewRect.right() - 4, y));
        } else if (def.tpl == BackgroundTemplate::Engineering) {
            QColor minorCol = lineCol;
            minorCol.setAlpha(lineCol.alpha() / 2);
            p.setPen(QPen(minorCol, 0.6));
            for (qreal x = previewRect.left() + 5; x < previewRect.right() - 2; x += 5.0)
                p.drawLine(QPointF(x, previewRect.top() + 2), QPointF(x, previewRect.bottom() - 2));
            for (qreal y = previewRect.top() + 5; y < previewRect.bottom() - 2; y += 5.0)
                p.drawLine(QPointF(previewRect.left() + 2, y), QPointF(previewRect.right() - 2, y));
            p.setPen(QPen(lineCol, 1.0));
            for (qreal x = previewRect.left() + 15; x < previewRect.right() - 2; x += 15.0)
                p.drawLine(QPointF(x, previewRect.top() + 2), QPointF(x, previewRect.bottom() - 2));
            for (qreal y = previewRect.top() + 15; y < previewRect.bottom() - 2; y += 15.0)
                p.drawLine(QPointF(previewRect.left() + 2, y), QPointF(previewRect.right() - 2, y));
        } else if (def.tpl == BackgroundTemplate::Isometric) {
            p.setPen(QPen(lineCol, 0.8));
            for (qreal offset = -48.0; offset < previewRect.width() + 48.0; offset += 12.0) {
                p.drawLine(QPointF(previewRect.left() + offset, previewRect.bottom()),
                           QPointF(previewRect.left() + offset + previewRect.height() * 1.4, previewRect.top()));
                p.drawLine(QPointF(previewRect.left() + offset, previewRect.top()),
                           QPointF(previewRect.left() + offset + previewRect.height() * 1.4, previewRect.bottom()));
            }
            for (qreal x = previewRect.left() + 12; x < previewRect.right() - 2; x += 12.0)
                p.drawLine(QPointF(x, previewRect.top()), QPointF(x, previewRect.bottom()));
        } else if (def.tpl == BackgroundTemplate::Cornell) {
            QColor ruleCol = sheetTheme.isDark ? lineCol : QColor(90, 150, 220, 130);
            p.setPen(QPen(ruleCol, 0.8));
            for (qreal y = previewRect.top() + 12; y < previewRect.bottom() - 9; y += 6.5)
                p.drawLine(QPointF(previewRect.left() + 4, y), QPointF(previewRect.right() - 4, y));
            QColor accentRule(220, 90, 100, 165);
            p.setPen(QPen(accentRule, 1.0));
            p.drawLine(QPointF(previewRect.left() + 18, previewRect.top() + 3),
                       QPointF(previewRect.left() + 18, previewRect.bottom() - 9));
            p.drawLine(QPointF(previewRect.left() + 4, previewRect.bottom() - 9),
                       QPointF(previewRect.right() - 4, previewRect.bottom() - 9));
        } else if (def.tpl == BackgroundTemplate::School) {
            QColor ruleCol = sheetTheme.isDark ? lineCol : QColor(90, 150, 220, 130);
            p.setPen(QPen(ruleCol, 0.8));
            for (qreal y = previewRect.top() + 12; y < previewRect.bottom() - 3; y += 6.5)
                p.drawLine(QPointF(previewRect.left() + 4, y), QPointF(previewRect.right() - 4, y));
            QColor accentRule(220, 90, 100, 170);
            p.setPen(QPen(accentRule, 1.0));
            p.drawLine(QPointF(previewRect.left() + 14, previewRect.top() + 3),
                       QPointF(previewRect.left() + 14, previewRect.bottom() - 3));
            p.drawLine(QPointF(previewRect.left() + 4, previewRect.top() + 9),
                       QPointF(previewRect.right() - 4, previewRect.top() + 9));
        }
        p.restore();

        p.setFont(editorFont(10.0, active ? QFont::DemiBold : QFont::Normal));
        p.setPen(active ? m_pal.text : m_pal.muted);
        p.drawText(QRectF(card.left(), previewRect.bottom() + 1, card.width(), 15.0),
                   Qt::AlignCenter, QString::fromUtf8(def.label));
    }

    if (!m_infinite) {
        p.setFont(editorFont(10.0, QFont::Bold));
        p.setPen(m_pal.muted);
        p.drawText(QRectF(14, m_scaleSeg->y() - 18, width() - 28, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("TRAMA"));
    }
}

void EditorPaperPanel::mouseMoveEvent(QMouseEvent* e) {
    int ht = -1;
    const int count = templateCount();
    for (int i = 0; i < count; ++i)
        if (templateRect(i).contains(e->position())) { ht = i; break; }
    bool overColor = false;
    for (int i = 0; !overColor && i <= m_sheetColors.size(); ++i)
        overColor = colorRect(i).adjusted(-3, -3, 3, 3).contains(e->position());
    if (ht != m_hoverTpl) {
        m_hoverTpl = ht;
        update();
    }
    setCursor((ht >= 0 || overColor) ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void EditorPaperPanel::leaveEvent(QEvent*) {
    if (m_hoverTpl != -1) {
        m_hoverTpl = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorPaperPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_canvas)
        return;
    for (int i = 0; i < m_sheetColors.size(); ++i) {
        if (colorRect(i).adjusted(-3, -3, 3, 3).contains(e->position())) {
            m_canvas->setCustomBackground(m_sheetColors.at(i));
            syncFromCanvas();
            return;
        }
    }
    const QRectF rainbowRect = colorRect(m_sheetColors.size());
    if (rainbowRect.adjusted(-4, -4, 4, 4).contains(e->position())) {
        const QPoint topLeft = mapTo(parentWidget(), rainbowRect.topLeft().toPoint());
        emit customSheetColorRequested(QRect(topLeft, rainbowRect.size().toSize()));
        return;
    }
    const int count = templateCount();
    for (int i = 0; i < count; ++i) {
        if (templateRect(i).contains(e->position())) {
            m_canvas->setBackgroundTemplate(templateAt(i).tpl);
            syncFromCanvas();
            return;
        }
    }
}

} // namespace finily
