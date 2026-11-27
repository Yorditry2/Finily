#include "ui/editor/EditorToolPanels.h"

#include "core/ShapeProtocols.h"
#include "ui/CanvasWidget.h"
#include "ui/UiIcons.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>

#include <algorithm>
#include <cmath>

namespace finily {

// ---------------- EditorWidthSection ----------------

EditorWidthSection::EditorWidthSection(ToolType tool, double minMm, double maxMm, double stepMm, QWidget* parent)
    : QWidget(parent),
      m_tool(tool),
      m_minMm(minMm),
      m_maxMm(maxMm),
      m_stepMm(stepMm) {
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(58);
    m_pal = editorPaletteFor(false);

    m_slider = new EditorSlider(this);
    m_slider->setRange(minMm, maxMm, stepMm);
    m_slider->hide();
    connect(m_slider, &EditorSlider::valueChanged, this, [this](double mm) {
        m_currentMm = mm;
        if (m_canvas)
            m_canvas->setToolFineWidth(m_tool, mm);
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
    m_presets = canvas->toolWidths(m_tool);
    m_activeSlot = canvas->activeWidthSlot(m_tool);
    m_currentMm = m_presets[std::clamp(m_activeSlot, 0, 2)];
    const auto pal = canvas->toolPalette(m_tool);
    if (!pal.isEmpty()) {
        const int idx = std::clamp(canvas->activeColorIndex(m_tool), 0, int(pal.size()) - 1);
        m_inkColor = resolveAdaptableInkColor(pal.at(idx), canvas->isCurrentSheetDark());
    } else {
        m_inkColor = m_pal.text;
    }
    if (m_slider)
        m_slider->setValue(m_currentMm);
    update();
}

QRectF EditorWidthSection::badgeRect() const {
    return QRectF(width() - 70.0, 0.0, 70.0, 18.0);
}

QRectF EditorWidthSection::slotRect(int i) const {
    const qreal gap = 8.0;
    const qreal slotW = (width() - 2.0 * gap) / 3.0;
    return QRectF(i * (slotW + gap), 22.0, slotW, 34.0);
}

void EditorWidthSection::toggleFineSlider() {
    m_expanded = !m_expanded;
    if (m_expanded) {
        m_slider->setGeometry(0, 62, width(), 20);
        m_slider->show();
        setFixedHeight(84);
    } else {
        m_slider->hide();
        setFixedHeight(58);
    }
    emit layoutHeightChanged();
    update();
}

void EditorWidthSection::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(10.0, QFont::Bold));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(0, 0, width() - 74.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("GROSOR (SINCRONIZADO CON BARRA)"));

    const QRectF br = badgeRect();
    p.setPen(Qt::NoPen);
    p.setBrush(m_hoverBadge ? m_pal.hover : m_pal.subtle);
    p.drawRoundedRect(br, 6.0, 6.0);
    p.setFont(editorFont(11.0, QFont::DemiBold));
    p.setPen(m_pal.text);
    const QString mmTxt = QString::number(m_currentMm, 'f', m_stepMm < 1.0 ? 1 : 0) + QStringLiteral(" mm ▾");
    p.drawText(br, Qt::AlignCenter, mmTxt);

    for (int i = 0; i < 3; ++i) {
        const QRectF sr = slotRect(i);
        const bool active = (i == m_activeSlot);
        const bool hovered = (i == m_hoverSlot);
        p.setPen(Qt::NoPen);
        if (active)
            p.setBrush(m_pal.accent);
        else if (hovered)
            p.setBrush(m_pal.hover);
        else
            p.setBrush(m_pal.subtle);
        p.drawRoundedRect(sr, 10.0, 10.0);

        const qreal d = std::clamp(m_presets[i] * (m_tool == ToolType::Pen ? 2.2 : 0.48), 3.0, 14.0);
        p.setBrush(active ? m_pal.accentFg : m_inkColor);
        p.drawEllipse(sr.center(), d * 0.5, d * 0.5);
    }
}

void EditorWidthSection::mouseMoveEvent(QMouseEvent* e) {
    int hs = -1;
    for (int i = 0; i < 3; ++i) {
        if (slotRect(i).contains(e->position())) {
            hs = i;
            break;
        }
    }
    const bool hb = badgeRect().contains(e->position());
    if (hs != m_hoverSlot || hb != m_hoverBadge) {
        m_hoverSlot = hs;
        m_hoverBadge = hb;
        setCursor((hs >= 0 || hb) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorWidthSection::leaveEvent(QEvent*) {
    if (m_hoverSlot != -1 || m_hoverBadge) {
        m_hoverSlot = -1;
        m_hoverBadge = false;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorWidthSection::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        for (int i = 0; i < 3; ++i) {
            if (slotRect(i).contains(e->position())) {
                if (m_canvas)
                    m_canvas->setToolWidthSlot(m_tool, i);
                if (!m_expanded)
                    toggleFineSlider();
                return;
            }
        }
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (badgeRect().contains(e->position())) {
        toggleFineSlider();
        return;
    }
    for (int i = 0; i < 3; ++i) {
        if (slotRect(i).contains(e->position())) {
            if (i == m_activeSlot) {
                toggleFineSlider();
            } else if (m_canvas) {
                m_canvas->setToolWidthSlot(m_tool, i);
                syncFromCanvas(m_canvas);
            }
            return;
        }
    }
}

void EditorWidthSection::tabletEvent(QTabletEvent* e) {
    e->ignore();
}

// ---------------- EditorColorRow ----------------

EditorColorRow::EditorColorRow(ToolType tool, QWidget* parent)
    : QWidget(parent),
      m_tool(tool) {
    setFixedHeight(28);
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
    const qreal step = std::min<qreal>(34.0, (width() - 22.0) / std::max(1, count - 1));
    return QRectF(idx * step + 2.0, 4.0, 20.0, 20.0);
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

// ---------------- 1. EditorPenPanel ----------------

EditorPenPanel::EditorPenPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedWidth(318);
    setMouseTracking(true);
    hide();

    m_widthSection = new EditorWidthSection(ToolType::Pen, 0.5, 8.0, 0.2, this);
    connect(m_widthSection, &EditorWidthSection::layoutHeightChanged, this, &EditorPenPanel::relayout);

    m_colorRow = new EditorColorRow(ToolType::Pen, this);
    connect(m_colorRow, &EditorColorRow::customColorRequested, this, &EditorPenPanel::customColorRequested);

    relayout();
    syncFromCanvas();
}

void EditorPenPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_widthSection->applyEditorTheme(dark);
    m_colorRow->applyEditorTheme(dark);
}

void EditorPenPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_activeTip = (m_canvas->tool() == ToolType::Ballpoint) ? 0 : m_canvas->penTip();
    const auto pal = m_canvas->toolPalette(ToolType::Pen);
    const int idx = std::clamp(m_canvas->activeColorIndex(ToolType::Pen), 0, std::max(0, int(pal.size()) - 1));
    m_activeColor = pal.isEmpty()
        ? resolveAdaptableInkColor(m_canvas->inkColor(), m_canvas->isCurrentSheetDark())
        : resolveAdaptableInkColor(pal.at(idx), m_canvas->isCurrentSheetDark());
    m_widthSection->syncFromCanvas(m_canvas);
    m_colorRow->syncFromCanvas(m_canvas);
    update();
}

QRectF EditorPenPanel::linkRect() const {
    return QRectF(width() - 108.0, 12.0, 94.0, 22.0);
}

QRectF EditorPenPanel::tipRowRect(int idx) const {
    return QRectF(14.0, 44.0 + idx * 40.0, width() - 28.0, 36.0);
}

void EditorPenPanel::relayout() {
    const int wy = 44 + 4 * 40 + 6;
    m_widthSection->setGeometry(14, wy, width() - 28, m_widthSection->height());
    const int cy = wy + m_widthSection->height() + 10;
    m_colorRow->setGeometry(14, cy, width() - 28, 28);
    setFixedSize(318, cy + 28 + 14);
    emit panelResized();
    update();
}

void EditorPenPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, 120, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Pluma"));

    p.setFont(editorFont(11.5, QFont::DemiBold));
    p.setPen(m_hoverLink ? m_pal.text : m_pal.muted);
    p.drawText(linkRect(), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Más puntas ›"));

    struct TipInfo { int id; const char* name; qreal width; qreal alpha; bool rough; };
    const TipInfo tips[4] = {
        {1, "Pluma", 3.0, 1.0, false},
        {0, "Bolígrafo", 1.8, 1.0, false},
        {2, "Grafito", 2.4, 0.80, true},
        {3, "Tiza", 4.6, 0.72, true},
    };

    for (int i = 0; i < 4; ++i) {
        const QRectF rr = tipRowRect(i);
        const bool active = (m_activeTip == tips[i].id);
        p.setPen(Qt::NoPen);
        if (active)
            p.setBrush(m_pal.subtle);
        else if (i == m_hoverTip)
            p.setBrush(m_pal.hover);
        else
            p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rr, 9.0, 9.0);
        if (active) {
            p.setPen(QPen(m_pal.border, 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rr.adjusted(0.5, 0.5, -0.5, -0.5), 9.0, 9.0);
        }

        p.setFont(editorFont(11.5, active ? QFont::Bold : QFont::DemiBold));
        p.setPen(active ? m_pal.text : m_pal.muted);
        p.drawText(QRectF(rr.left() + 10, rr.top(), 68, rr.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(tips[i].name));

        // Trazo ondulado de muestra en el color activo
        const qreal wx0 = rr.left() + 84.0;
        const qreal wx1 = rr.right() - 12.0;
        const qreal wcy = rr.center().y();
        QPainterPath wave;
        for (int s = 0; s <= 28; ++s) {
            const qreal t = s / 28.0;
            const qreal x = wx0 + (wx1 - wx0) * t;
            const qreal y = wcy + std::sin(t * 6.28318 * 2.0) * 6.0;
            if (s == 0) wave.moveTo(x, y);
            else wave.lineTo(x, y);
        }
        QColor strokeCol = m_activeColor;
        strokeCol.setAlphaF(tips[i].alpha);
        QPen wPen(strokeCol, tips[i].width, tips[i].rough ? Qt::SolidLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(wPen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(wave);
    }
}

void EditorPenPanel::mouseMoveEvent(QMouseEvent* e) {
    int ht = -1;
    for (int i = 0; i < 4; ++i) {
        if (tipRowRect(i).contains(e->position())) {
            ht = i;
            break;
        }
    }
    const bool hl = linkRect().contains(e->position());
    if (ht != m_hoverTip || hl != m_hoverLink) {
        m_hoverTip = ht;
        m_hoverLink = hl;
        setCursor((ht >= 0 || hl) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorPenPanel::leaveEvent(QEvent*) {
    if (m_hoverTip != -1 || m_hoverLink) {
        m_hoverTip = -1;
        m_hoverLink = false;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorPenPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    if (linkRect().contains(e->position())) {
        emit libraryRequested(QStringLiteral("escritura"));
        return;
    }
    const int tipIds[4] = {1, 0, 2, 3};
    for (int i = 0; i < 4; ++i) {
        if (tipRowRect(i).contains(e->position())) {
            if (m_canvas) {
                m_canvas->setTool(ToolType::Pen);
                m_canvas->setPenTip(tipIds[i]);
            }
            syncFromCanvas();
            return;
        }
    }
}

// ---------------- 2. EditorHighlighterPanel ----------------

EditorHighlighterPanel::EditorHighlighterPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedWidth(296);
    hide();

    m_opacitySlider = new EditorSlider(this);
    m_opacitySlider->setRange(15.0, 75.0, 5.0);
    connect(m_opacitySlider, &EditorSlider::valueChanged, this, [this](double pct) {
        m_opacityPct = qRound(pct);
        if (m_canvas)
            m_canvas->setHighlighterOpacity(pct / 100.0);
        update();
    });

    m_widthSection = new EditorWidthSection(ToolType::Highlighter, 4.0, 24.0, 1.0, this);
    connect(m_widthSection, &EditorWidthSection::layoutHeightChanged, this, &EditorHighlighterPanel::relayout);

    m_colorRow = new EditorColorRow(ToolType::Highlighter, this);
    connect(m_colorRow, &EditorColorRow::customColorRequested, this, &EditorHighlighterPanel::customColorRequested);

    relayout();
    syncFromCanvas();
}

void EditorHighlighterPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_opacitySlider->applyEditorTheme(dark);
    m_widthSection->applyEditorTheme(dark);
    m_colorRow->applyEditorTheme(dark);
}

void EditorHighlighterPanel::relayout() {
    m_opacitySlider->setGeometry(14, 62, width() - 28, 20);
    m_widthSection->setGeometry(14, 94, width() - 28, m_widthSection->height());
    const int cy = 94 + m_widthSection->height() + 10;
    m_colorRow->setGeometry(14, cy, width() - 28, 28);
    setFixedSize(296, cy + 28 + 14);
    emit panelResized();
    update();
}

void EditorHighlighterPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_opacityPct = std::clamp(qRound(m_canvas->highlighterOpacity() * 100.0), 15, 75);
    m_opacitySlider->setValue(m_opacityPct);
    m_widthSection->syncFromCanvas(m_canvas);
    m_colorRow->syncFromCanvas(m_canvas);
    update();
}

void EditorHighlighterPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, 160, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Resaltador"));

    p.setFont(editorFont(11.5, QFont::DemiBold));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(width() - 74, 12, 60, 22), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 %").arg(m_opacityPct));

    p.setFont(editorFont(10.0, QFont::Bold));
    p.drawText(QRectF(14, 42, width() - 28, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("OPACIDAD"));
}

// ---------------- 3. EditorEraserPanel ----------------

EditorEraserPanel::EditorEraserPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedWidth(308);
    hide();

    m_modeSeg = new EditorSegControl(this);
    m_modeSeg->setOptions({QStringLiteral("Trazo completo"), QStringLiteral("Solo lo que toco")});
    connect(m_modeSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        if (m_canvas)
            m_canvas->setEraserMode(idx == 0 ? EraserMode::Stroke : EraserMode::Partial);
    });

    m_widthSection = new EditorWidthSection(ToolType::Eraser, 4.0, 40.0, 1.0, this);
    connect(m_widthSection, &EditorWidthSection::layoutHeightChanged, this, &EditorEraserPanel::relayout);

    m_inkChip = new EditorChipButton(QStringLiteral("Tinta"), this);
    m_inkChip->setCheckable(true);
    m_hlChip = new EditorChipButton(QStringLiteral("Resaltador"), this);
    m_hlChip->setCheckable(true);
    m_shapeChip = new EditorChipButton(QStringLiteral("Figuras"), this);
    m_shapeChip->setCheckable(true);

    auto pushTargets = [this] {
        if (!m_canvas)
            return;
        EraserTargets t;
        t.ink = m_inkChip->isChecked();
        t.highlighter = m_hlChip->isChecked();
        t.shapes = m_shapeChip->isChecked();
        m_canvas->setEraserTargets(t);
    };
    connect(m_inkChip, &EditorChipButton::toggled, this, [pushTargets](bool) { pushTargets(); });
    connect(m_hlChip, &EditorChipButton::toggled, this, [pushTargets](bool) { pushTargets(); });
    connect(m_shapeChip, &EditorChipButton::toggled, this, [pushTargets](bool) { pushTargets(); });

    m_clearBtn = new EditorChipButton(QStringLiteral("Borrar anotaciones de la página"), this);
    m_clearBtn->setDanger(true);
    connect(m_clearBtn, &EditorChipButton::clicked, this, &EditorEraserPanel::clearPageRequested);

    relayout();
    syncFromCanvas();
}

void EditorEraserPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_modeSeg->applyEditorTheme(dark);
    m_widthSection->applyEditorTheme(dark);
    m_inkChip->applyEditorTheme(dark);
    m_hlChip->applyEditorTheme(dark);
    m_shapeChip->applyEditorTheme(dark);
    m_clearBtn->applyEditorTheme(dark);
}

void EditorEraserPanel::relayout() {
    m_modeSeg->setGeometry(14, 42, width() - 28, 30);
    m_widthSection->setGeometry(14, 84, width() - 28, m_widthSection->height());
    const int ty = 84 + m_widthSection->height() + 28;
    const int chipW = (width() - 28 - 12) / 3;
    m_inkChip->setGeometry(14, ty, chipW, 27);
    m_hlChip->setGeometry(14 + chipW + 6, ty, chipW, 27);
    m_shapeChip->setGeometry(14 + (chipW + 6) * 2, ty, chipW, 27);
    const int cy = ty + 27 + 34;
    m_clearBtn->setGeometry(14, cy, width() - 28, 30);
    setFixedSize(308, cy + 30 + 14);
    emit panelResized();
    update();
}

void EditorEraserPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_modeSeg->setCurrentIndex(m_canvas->eraserMode() == EraserMode::Stroke ? 0 : 1);
    m_widthSection->syncFromCanvas(m_canvas);
    const auto t = m_canvas->eraserTargets();
    m_inkChip->setChecked(t.ink);
    m_hlChip->setChecked(t.highlighter);
    m_shapeChip->setChecked(t.shapes);
    update();
}

void EditorEraserPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, width() - 28, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Borrador"));

    const int ky = 84 + m_widthSection->height() + 10;
    p.setFont(editorFont(10.0, QFont::Bold));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(14, ky, width() - 28, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("QUÉ BORRA"));

    const int hy = ky + 18 + 27 + 6;
    p.setFont(editorFont(10.5, QFont::Normal));
    p.drawText(QRectF(14, hy, width() - 28, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Mantén Mayús para recortar entre cruces"));

    p.setPen(QPen(m_pal.borderSoft, 1.0));
    p.drawLine(QPointF(14, hy + 21), QPointF(width() - 14, hy + 21));
}

// ---------------- 4. EditorFillPanel ----------------

EditorFillPanel::EditorFillPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedSize(332, 266);
    setMouseTracking(true);
    hide();

    m_modeSeg = new EditorSegControl(this);
    m_modeSeg->setOptions({QStringLiteral("Región"), QStringLiteral("Tramo / Arco")});
    m_modeSeg->setGeometry(14, 42, width() - 28, 30);
    connect(m_modeSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        m_segmentMode = (idx == 1);
        update();
        emit fillSubModeChanged(m_segmentMode);
    });

    m_opacitySlider = new EditorSlider(this);
    m_opacitySlider->setRange(10.0, 90.0, 5.0);
    m_opacitySlider->setValue(30.0);
    m_opacitySlider->setGeometry(14, 232, width() - 28, 20);
    connect(m_opacitySlider, &EditorSlider::valueChanged, this, [this](double val) {
        m_opacityPct = qRound(val);
        update();
    });

    syncFromCanvas();
}

void EditorFillPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_modeSeg->applyEditorTheme(dark);
    m_opacitySlider->applyEditorTheme(dark);
}

QRectF EditorFillPanel::patternCardRect(int idx) const {
    const int col = idx % 3;
    const int row = idx / 3;
    const qreal w = (width() - 28.0 - 16.0) / 3.0;
    return QRectF(14.0 + col * (w + 8.0), 84.0 + row * 60.0, w, 52.0);
}

void EditorFillPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_segmentMode = m_canvas->fillSegmentMode();
    if (m_modeSeg)
        m_modeSeg->setCurrentIndex(m_segmentMode ? 1 : 0);
    m_pattern = m_canvas->shadePattern();
    m_activeColor = m_canvas->inkColor();
    update();
}

void EditorFillPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, 120, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Relleno"));

    p.setFont(editorFont(11.5, QFont::Medium));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(140, 12, width() - 154, 22), Qt::AlignRight | Qt::AlignVCenter,
               m_segmentMode ? QStringLiteral("Resaltar lado") : QStringLiteral("Área cerrada"));

    struct PatInfo { const char* name; ShadePattern pat; };
    const PatInfo pats[6] = {
        {"Suave", ShadePattern::Soft},
        {"Diagonal", ShadePattern::Diagonal},
        {"Puntos", ShadePattern::Dotted},
        {"Sólido", ShadePattern::Solid},
        {"Cruzado", ShadePattern::Crosshatch},
        {"Cuadrícula", ShadePattern::Grid},
    };

    for (int i = 0; i < 6; ++i) {
        const QRectF cr = patternCardRect(i);
        const bool active = (m_pattern == pats[i].pat);
        p.setPen(QPen(active ? m_pal.accent : m_pal.borderSoft, active ? 1.5 : 1.0));
        p.setBrush(active ? m_pal.subtle : (i == m_hoverCard ? m_pal.hover : m_pal.surfaceSolid));
        p.drawRoundedRect(cr, 10.0, 10.0);

        const QRectF triBox(cr.center().x() - 22.0, cr.top() + 7.0, 44.0, 24.0);
        QPainterPath tri;
        tri.moveTo(triBox.left(), triBox.bottom());
        tri.lineTo(triBox.right(), triBox.bottom());
        tri.lineTo(triBox.center().x() + 6.0, triBox.top());
        tri.closeSubpath();

        QColor fillC = m_activeColor;
        fillC.setAlphaF(pats[i].pat == ShadePattern::Solid ? 0.65 : 0.25);
        p.save();
        p.setClipPath(tri);
        p.fillRect(triBox, fillC);
        p.setPen(QPen(m_activeColor, 1.0));
        if (pats[i].pat == ShadePattern::Diagonal || pats[i].pat == ShadePattern::Crosshatch) {
            for (qreal dx = -24; dx < 60; dx += 6.0)
                p.drawLine(QPointF(triBox.left() + dx, triBox.bottom()), QPointF(triBox.left() + dx + 22, triBox.top()));
        }
        if (pats[i].pat == ShadePattern::Crosshatch) {
            for (qreal dx = -24; dx < 60; dx += 6.0)
                p.drawLine(QPointF(triBox.left() + dx, triBox.top()), QPointF(triBox.left() + dx + 22, triBox.bottom()));
        }
        if (pats[i].pat == ShadePattern::Grid) {
            for (qreal dx = 4; dx < 44; dx += 7.0)
                p.drawLine(QPointF(triBox.left() + dx, triBox.top()), QPointF(triBox.left() + dx, triBox.bottom()));
            for (qreal dy = 4; dy < 24; dy += 7.0)
                p.drawLine(QPointF(triBox.left(), triBox.top() + dy), QPointF(triBox.right(), triBox.top() + dy));
        }
        if (pats[i].pat == ShadePattern::Dotted) {
            p.setBrush(m_activeColor);
            p.setPen(Qt::NoPen);
            for (qreal dx = 5; dx < 42; dx += 6.0)
                for (qreal dy = 5; dy < 22; dy += 6.0)
                    p.drawEllipse(QPointF(triBox.left() + dx, triBox.top() + dy), 1.1, 1.1);
        }
        p.restore();
        p.setPen(QPen(m_activeColor, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(tri);

        p.setFont(editorFont(10.5, active ? QFont::Bold : QFont::DemiBold));
        p.setPen(active ? m_pal.text : m_pal.muted);
        p.drawText(QRectF(cr.left(), cr.bottom() - 18.0, cr.width(), 16.0),
                   Qt::AlignCenter, QString::fromUtf8(pats[i].name));
    }

    p.setFont(editorFont(10.0, QFont::Bold));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(14, 210, 120, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("OPACIDAD"));
    p.drawText(QRectF(width() - 70, 210, 56, 16), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 %").arg(m_opacityPct));
}

void EditorFillPanel::mouseMoveEvent(QMouseEvent* e) {
    int hc = -1;
    for (int i = 0; i < 6; ++i)
        if (patternCardRect(i).contains(e->position())) { hc = i; break; }
    if (hc != m_hoverCard) {
        m_hoverCard = hc;
        setCursor(hc >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorFillPanel::leaveEvent(QEvent*) {
    if (m_hoverCard != -1) {
        m_hoverCard = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorFillPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    const ShadePattern pats[6] = {
        ShadePattern::Soft, ShadePattern::Diagonal, ShadePattern::Dotted,
        ShadePattern::Solid, ShadePattern::Crosshatch, ShadePattern::Grid
    };
    for (int i = 0; i < 6; ++i) {
        if (patternCardRect(i).contains(e->position())) {
            m_pattern = pats[i];
            if (m_canvas)
                m_canvas->setShadePattern(m_pattern);
            update();
            return;
        }
    }
}

// ---------------- 5. EditorLassoPanel ----------------

EditorLassoPanel::EditorLassoPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedSize(276, 174);
    hide();

    m_modeSeg = new EditorSegControl(this);
    m_modeSeg->setOptions({QStringLiteral("Libre"), QStringLiteral("Rectángulo")});
    m_modeSeg->setGeometry(14, 42, width() - 28, 30);
    connect(m_modeSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        if (m_canvas)
            m_canvas->setLassoRectMode(idx == 1);
    });

    m_inkChip = new EditorChipButton(QStringLiteral("Escritura"), this);
    m_inkChip->setCheckable(true);
    m_shapesChip = new EditorChipButton(QStringLiteral("Figuras"), this);
    m_shapesChip->setCheckable(true);
    m_mediaChip = new EditorChipButton(QStringLiteral("Texto e imágenes"), this);
    m_mediaChip->setCheckable(true);

    m_inkChip->setGeometry(14, 102, 118, 27);
    m_shapesChip->setGeometry(138, 102, 124, 27);
    m_mediaChip->setGeometry(14, 134, 164, 27);

    auto pushFilter = [this] {
        if (!m_canvas)
            return;
        LassoTargetFilter f;
        f.ink = m_inkChip->isChecked();
        f.shapes = m_shapesChip->isChecked();
        f.media = m_mediaChip->isChecked();
        m_canvas->setLassoTargetFilter(f);
    };
    connect(m_inkChip, &EditorChipButton::toggled, this, [pushFilter](bool) { pushFilter(); });
    connect(m_shapesChip, &EditorChipButton::toggled, this, [pushFilter](bool) { pushFilter(); });
    connect(m_mediaChip, &EditorChipButton::toggled, this, [pushFilter](bool) { pushFilter(); });

    syncFromCanvas();
}

void EditorLassoPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_modeSeg->applyEditorTheme(dark);
    m_inkChip->applyEditorTheme(dark);
    m_shapesChip->applyEditorTheme(dark);
    m_mediaChip->applyEditorTheme(dark);
}

void EditorLassoPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_modeSeg->setCurrentIndex(m_canvas->lassoRectMode() ? 1 : 0);
    const auto f = m_canvas->lassoTargetFilter();
    m_inkChip->setChecked(f.ink);
    m_shapesChip->setChecked(f.shapes);
    m_mediaChip->setChecked(f.media);
}

void EditorLassoPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, width() - 28, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Lazo"));

    p.setFont(editorFont(10.0, QFont::Bold));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(14, 82, width() - 28, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("QUÉ SELECCIONA"));
}

// ---------------- 6. EditorShapesPanel ----------------

EditorShapesPanel::EditorShapesPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedWidth(304);
    setMouseTracking(true);
    hide();

    m_specs = {
        // 0: Básicas
        {QStringLiteral("Línea"), QStringLiteral("shp-line"), 0, ShapeKind::Line},
        {QStringLiteral("Flecha"), QStringLiteral("shp-arrow"), 0, ShapeKind::Line, true},
        {QStringLiteral("Círculo"), QStringLiteral("shp-circle"), 0, ShapeKind::Circle, false, false, false, 0, ShapeFamily::Circle},
        {QStringLiteral("Elipse"), QStringLiteral("shp-ellipse"), 0, ShapeKind::Ellipse},
        {QStringLiteral("Triángulo"), QStringLiteral("shp-triangle"), 0, ShapeKind::Triangle, false, false, false, 0, ShapeFamily::Triangle},
        {QStringLiteral("Rectángulo"), QStringLiteral("shp-rect"), 0, ShapeKind::Rectangle, false, false, false, 0, ShapeFamily::Quad},
        // 1: Matemáticas
        {QStringLiteral("Polilínea"), QStringLiteral("shp-polyline"), 1, ShapeKind::Polyline},
        {QStringLiteral("Curva"), QStringLiteral("shp-curve"), 1, ShapeKind::Curve},
        {QStringLiteral("Ejes"), QStringLiteral("shp-axes"), 1, ShapeKind::Axes},
        {QStringLiteral("Radical"), QStringLiteral("shp-radical"), 1, ShapeKind::Radical},
        {QStringLiteral("Llave"), QStringLiteral("shp-brace"), 1, ShapeKind::Brace},
        {QStringLiteral("Polígono"), QStringLiteral("shp-polygon"), 1, ShapeKind::Polyline, false, false, false, 0, ShapeFamily::Polygon},
        // 2: Medidas
        {QStringLiteral("Ángulo recto"), QStringLiteral("shp-right-angle"), 2, ShapeKind::RightAngle},
        {QStringLiteral("Ángulo"), QStringLiteral("shp-angle"), 2, ShapeKind::AngleMarker},
        {QStringLiteral("Cota"), QStringLiteral("shp-dimension"), 2, ShapeKind::Line, false, true},
        {QStringLiteral("Resorte"), QStringLiteral("shp-spring"), 2, ShapeKind::Line, false, false, true},
        // 3: Sólidos 3D
        {QStringLiteral("Cubo"), QStringLiteral("shp-cube"), 3, ShapeKind::Solid3D, false, false, false, int(Solid3DKind::Cube)},
        {QStringLiteral("Cilindro"), QStringLiteral("shp-cylinder"), 3, ShapeKind::Solid3D, false, false, false, int(Solid3DKind::Cylinder)},
        {QStringLiteral("Cono"), QStringLiteral("shp-cone"), 3, ShapeKind::Solid3D, false, false, false, int(Solid3DKind::Cone)},
        {QStringLiteral("Esfera"), QStringLiteral("shp-sphere"), 3, ShapeKind::Solid3D, false, false, false, int(Solid3DKind::Sphere)},
        {QStringLiteral("Pirámide"), QStringLiteral("shp-pyramid"), 3, ShapeKind::Solid3D, false, false, false, int(Solid3DKind::Pyramid)},
    };

    rebuildLayout();
    syncFromCanvas();
}

void EditorShapesPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    for (auto* c : m_variantChips)
        c->applyEditorTheme(dark);
}

void EditorShapesPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    for (const auto& s : m_specs) {
        if (isButtonActive(s)) {
            m_activeLabel = s.label;
            break;
        }
    }
    update();
}

bool EditorShapesPanel::isButtonActive(const ShapeBtnSpec& s) const {
    if (!m_canvas)
        return false;
    const ShapeKind cur = m_canvas->shapeKind();
    if (s.kind == ShapeKind::Solid3D)
        return cur == ShapeKind::Solid3D && m_canvas->solidKind() == s.solidKind;
    if (s.isArrow)
        return cur == ShapeKind::Line && m_canvas->lineEnding() != LineEnding::None;
    if (s.isDimension)
        return cur == ShapeKind::Line && m_canvas->linePattern() == LinePattern::Dimension;
    if (s.isSpring)
        return cur == ShapeKind::Line && isPhysicsSpringSpec(m_canvas->congruenceMarkSpec());
    if (s.kind == ShapeKind::Line && !s.isArrow && !s.isDimension && !s.isSpring)
        return cur == ShapeKind::Line && m_canvas->lineEnding() == LineEnding::None &&
               m_canvas->linePattern() != LinePattern::Dimension && !isPhysicsSpringSpec(m_canvas->congruenceMarkSpec());
    if (s.family == ShapeFamily::Circle)
        return cur == ShapeKind::Circle || cur == ShapeKind::Arc || cur == ShapeKind::Sector;
    if (s.family == ShapeFamily::Quad)
        return cur == ShapeKind::Rectangle || (cur == ShapeKind::Polyline && m_canvas->tool() == ToolType::Shape && m_canvas->quadVariant() > 0);
    if (s.family == ShapeFamily::Polygon)
        return cur == ShapeKind::Polyline && m_canvas->tool() == ToolType::Shape && m_canvas->quadVariant() == 0;
    if (s.kind == ShapeKind::Polyline && s.family == ShapeFamily::None)
        return m_canvas->tool() == ToolType::Polyline;
    return cur == s.kind;
}

void EditorShapesPanel::rebuildLayout() {
    for (auto* c : m_variantChips) {
        c->hide();
        c->deleteLater();
    }
    m_variantChips.clear();
    m_btnRects.resize(m_specs.size());

    const qreal cellW = (width() - 28.0 - 5.0 * 4.0) / 6.0;
    const qreal cellH = 38.0;
    qreal y = 42.0;

    for (int cat = 0; cat < 4; ++cat) {
        y += 18.0; // category kicker
        int col = 0;
        for (int i = 0; i < m_specs.size(); ++i) {
            if (m_specs.at(i).category != cat)
                continue;
            m_btnRects[i] = QRectF(14.0 + col * (cellW + 4.0), y, cellW, cellH);
            ++col;
        }
        y += cellH + 8.0;

        if (m_expandedAfterCat == cat && m_expandedFamily != ShapeFamily::None) {
            struct VarOpt { QString label; int val; };
            QVector<VarOpt> opts;
            int activeVar = -1;
            if (m_expandedFamily == ShapeFamily::Triangle) {
                opts = {{QStringLiteral("Libre"), 0}, {QStringLiteral("Rectángulo"), 1},
                        {QStringLiteral("Isósceles"), 2}, {QStringLiteral("Equilátero"), 3}};
                if (m_canvas) activeVar = m_canvas->triangleVariant();
            } else if (m_expandedFamily == ShapeFamily::Quad) {
                opts = {{QStringLiteral("Rectángulo"), 0}, {QStringLiteral("Paralelogramo"), 1},
                        {QStringLiteral("Trapecio isósceles"), 2}, {QStringLiteral("Trapecio rectángulo"), 3},
                        {QStringLiteral("Trapezoide"), 4}};
                if (m_canvas) activeVar = (m_canvas->shapeKind() == ShapeKind::Rectangle) ? 0 : m_canvas->quadVariant();
            } else if (m_expandedFamily == ShapeFamily::Circle) {
                opts = {{QStringLiteral("Círculo"), 0}, {QStringLiteral("Arco"), 1}, {QStringLiteral("Sector"), 2}};
                if (m_canvas) {
                    activeVar = (m_canvas->shapeKind() == ShapeKind::Arc) ? 1
                              : (m_canvas->shapeKind() == ShapeKind::Sector ? 2 : 0);
                }
            } else if (m_expandedFamily == ShapeFamily::Polygon) {
                opts = {{QStringLiteral("5 lados"), 5}, {QStringLiteral("6 lados"), 6},
                        {QStringLiteral("7 lados"), 7}, {QStringLiteral("8 lados"), 8}};
                if (m_canvas && m_canvas->quadVariant() == 0) activeVar = m_canvas->polygonSides();
            }
            qreal cx = 18.0;
            qreal cy = y + 2.0;
            for (const auto& opt : opts) {
                auto* chip = new EditorChipButton(opt.label, this);
                chip->setChecked(opt.val == activeVar);
                chip->applyEditorTheme(m_dark);
                const QSize sh = chip->sizeHint();
                if (cx + sh.width() > width() - 18) {
                    cx = 18.0;
                    cy += 31.0;
                }
                chip->setGeometry(int(cx), int(cy), sh.width(), 27);
                chip->show();
                connect(chip, &EditorChipButton::clicked, this, [this, opt] {
                    if (!m_canvas)
                        return;
                    m_canvas->setTool(ToolType::Shape);
                    m_canvas->setCongruenceMarkSpec(QString());
                    if (m_expandedFamily == ShapeFamily::Triangle) {
                        m_canvas->setShapeKind(ShapeKind::Triangle);
                        m_canvas->setTriangleVariant(opt.val);
                    } else if (m_expandedFamily == ShapeFamily::Quad) {
                        if (opt.val == 0) {
                            m_canvas->setShapeKind(ShapeKind::Rectangle);
                            m_canvas->setQuadVariant(0);
                        } else {
                            m_canvas->setShapeKind(ShapeKind::Polyline);
                            m_canvas->setQuadVariant(opt.val);
                        }
                    } else if (m_expandedFamily == ShapeFamily::Circle) {
                        m_canvas->setShapeKind(opt.val == 0 ? ShapeKind::Circle
                                             : (opt.val == 1 ? ShapeKind::Arc : ShapeKind::Sector));
                    } else if (m_expandedFamily == ShapeFamily::Polygon) {
                        m_canvas->setQuadVariant(0);
                        m_canvas->setShapeKind(ShapeKind::Polyline);
                        m_canvas->setPolygonSides(opt.val);
                    }
                    rebuildLayout();
                    syncFromCanvas();
                });
                m_variantChips.append(chip);
                cx += sh.width() + 5.0;
            }
            y = cy + 34.0;
        }
    }

    m_moreLinkRect = QRectF(14.0, y + 2.0, width() - 28.0, 22.0);
    setFixedSize(304, int(std::ceil(y + 34.0)));
    emit panelResized();
    update();
}

void EditorShapesPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, 120, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Figuras"));

    p.setFont(editorFont(11.5, QFont::Medium));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(130, 12, width() - 144, 22), Qt::AlignRight | Qt::AlignVCenter, m_activeLabel);

    const char* catNames[4] = {"BÁSICAS", "MATEMÁTICAS", "MEDIDAS", "SÓLIDOS 3D"};
    for (int cat = 0; cat < 4; ++cat) {
        qreal ky = 0.0;
        for (int i = 0; i < m_specs.size(); ++i) {
            if (m_specs.at(i).category == cat) {
                ky = m_btnRects.at(i).top() - 17.0;
                break;
            }
        }
        p.setFont(editorFont(10.0, QFont::Bold));
        p.setPen(m_pal.muted);
        p.drawText(QRectF(14, ky, width() - 28, 15), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8(catNames[cat]));
    }

    for (int i = 0; i < m_specs.size(); ++i) {
        const auto& s = m_specs.at(i);
        const QRectF& br = m_btnRects.at(i);
        const bool active = isButtonActive(s);
        p.setPen(Qt::NoPen);
        if (active)
            p.setBrush(m_pal.accent);
        else if (i == m_hoverBtn)
            p.setBrush(m_pal.hover);
        else
            p.setBrush(m_pal.subtle);
        p.drawRoundedRect(br, 9.0, 9.0);

        const QColor fg = active ? m_pal.accentFg : m_pal.icon;
        paintUiIcon(&p, QRectF(br.center().x() - 10.5, br.center().y() - 10.5, 21.0, 21.0),
                    s.icon, fg, QStringLiteral("editor"));
        if (s.family != ShapeFamily::None)
            paintEditorFamilyCorner(p, br, fg);
    }

    p.setFont(editorFont(11.5, QFont::DemiBold));
    p.setPen(m_hoverMore ? m_pal.text : m_pal.muted);
    p.drawText(m_moreLinkRect, Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("Más figuras (troncos, octaedro…) ›"));
}

void EditorShapesPanel::mouseMoveEvent(QMouseEvent* e) {
    int hb = -1;
    for (int i = 0; i < m_btnRects.size(); ++i)
        if (m_btnRects.at(i).contains(e->position())) { hb = i; break; }
    const bool hm = m_moreLinkRect.contains(e->position());
    if (hb != m_hoverBtn || hm != m_hoverMore) {
        m_hoverBtn = hb;
        m_hoverMore = hm;
        setCursor((hb >= 0 || hm) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorShapesPanel::leaveEvent(QEvent*) {
    if (m_hoverBtn != -1 || m_hoverMore) {
        m_hoverBtn = -1;
        m_hoverMore = false;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorShapesPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    if (m_moreLinkRect.contains(e->position())) {
        emit libraryRequested(QStringLiteral("figuras"));
        return;
    }
    for (int i = 0; i < m_specs.size(); ++i) {
        if (!m_btnRects.at(i).contains(e->position()))
            continue;
        const auto& s = m_specs.at(i);
        const bool wasActive = isButtonActive(s);
        if (m_canvas) {
            if (s.kind == ShapeKind::Polyline && s.family == ShapeFamily::None) {
                m_canvas->setTool(ToolType::Polyline);
                m_canvas->setCongruenceMarkSpec(QString());
            } else {
                m_canvas->setTool(ToolType::Shape);
                m_canvas->setShapeKind(s.kind);
                if (s.family == ShapeFamily::Quad || s.family == ShapeFamily::Polygon)
                    m_canvas->setQuadVariant(0);
                if (s.kind == ShapeKind::Solid3D)
                    m_canvas->setSolidKind(s.solidKind);
                if (s.kind == ShapeKind::Line) {
                    m_canvas->setLineEnding(s.isArrow ? LineEnding::Arrow : LineEnding::None);
                    m_canvas->setLinePattern(s.isDimension ? LinePattern::Dimension : LinePattern::Solid);
                    m_canvas->setCongruenceMarkSpec(s.isSpring ? QStringLiteral("physics:spring") : QString());
                } else {
                    m_canvas->setCongruenceMarkSpec(QString());
                }
            }
        }
        if (wasActive && s.family != ShapeFamily::None) {
            if (m_expandedFamily == s.family) {
                m_expandedFamily = ShapeFamily::None;
                m_expandedAfterCat = -1;
            } else {
                m_expandedFamily = s.family;
                m_expandedAfterCat = s.category;
            }
            rebuildLayout();
        } else {
            m_expandedFamily = ShapeFamily::None;
            m_expandedAfterCat = -1;
            rebuildLayout();
        }
        syncFromCanvas();
        return;
    }
}

// ---------------- 7. EditorLaserPanel ----------------

EditorLaserPanel::EditorLaserPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedSize(224, 108);
    hide();

    m_modeSeg = new EditorSegControl(this);
    m_modeSeg->setOptions({QStringLiteral("Punto"), QStringLiteral("Estela")});
    m_modeSeg->setGeometry(14, 42, width() - 28, 30);
    connect(m_modeSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        if (m_canvas)
            m_canvas->setLaserMode(idx == 0 ? LaserMode::Dot : LaserMode::Trail);
    });

    syncFromCanvas();
}

void EditorLaserPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_modeSeg->applyEditorTheme(dark);
}

void EditorLaserPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_modeSeg->setCurrentIndex(m_canvas->laserMode() == LaserMode::Dot ? 0 : 1);
}

void EditorLaserPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, width() - 28, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Láser"));

    p.setFont(editorFont(10.5, QFont::Normal));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(14, 78, width() - 28, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("La duración se ajusta en Ajustes"));
}

// ---------------- 8. EditorPaperPanel ----------------

EditorPaperPanel::EditorPaperPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(16.0);
    setFixedWidth(296);
    setMouseTracking(true);
    hide();

    m_docModeSeg = new EditorSegControl(this);
    m_docModeSeg->setOptions({QStringLiteral("Pizarra infinita"), QStringLiteral("Cuaderno con hojas")});
    connect(m_docModeSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        if (!m_canvas)
            return;
        m_canvas->setPageFormat(idx == 0 ? PageFormat::Infinite : PageFormat::A4);
        syncFromCanvas();
    });

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

void EditorPaperPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_docModeSeg->applyEditorTheme(dark);
    m_formatSeg->applyEditorTheme(dark);
    m_scaleSeg->applyEditorTheme(dark);
    syncFromCanvas();
}

void EditorPaperPanel::relayout() {
    m_docModeSeg->setGeometry(14, 42, width() - 28, 30);
    int y = 82;
    if (!m_infinite) {
        m_formatSeg->setGeometry(14, y, width() - 28, 30);
        m_formatSeg->show();
        y += 42;
    } else {
        m_formatSeg->hide();
    }
    // Colors (16 + 24), Templates (16 + 44), Trama (16 + 32)
    y += 18 + 28 + 18 + 48 + 20;
    m_scaleSeg->setGeometry(14, y, width() - 28, 30);
    setFixedSize(296, y + 30 + 14);
    emit panelResized();
    update();
}

void EditorPaperPanel::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_infinite = (m_canvas->pageFormat() == PageFormat::Infinite);
    m_docModeSeg->setCurrentIndex(m_infinite ? 0 : 1);

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
    const int baseY = m_infinite ? 100 : 142;
    return QRectF(14.0 + i * 32.0, baseY, 22.0, 22.0);
}

QRectF EditorPaperPanel::templateRect(int i) const {
    const int baseY = m_infinite ? 148 : 190;
    const qreal w = (width() - 28.0 - 24.0) / 4.0;
    return QRectF(14.0 + i * (w + 8.0), baseY, w, 40.0);
}

void EditorPaperPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, 130, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Hoja y fondo"));

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

    const int cy = m_infinite ? 82 : 124;
    p.setFont(editorFont(10.0, QFont::Bold));
    p.drawText(QRectF(14, cy, width() - 28, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("COLOR DE HOJA"));

    for (int i = 0; i < m_sheetColors.size(); ++i) {
        const bool active = (m_sheetColors.at(i).name() == m_currentBg.name());
        paintEditorSwatch(p, colorRect(i), m_sheetColors.at(i), active, m_pal);
    }

    p.setFont(editorFont(10.0, QFont::Bold));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(14, cy + 48, width() - 28, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("PLANTILLA"));

    // Plantillas puramente visuales en tono neutro fijo (sin teñir y sin texto)
    const BackgroundTemplate tpls[4] = {
        BackgroundTemplate::Blank, BackgroundTemplate::Grid,
        BackgroundTemplate::Dots, BackgroundTemplate::Ruled
    };
    for (int i = 0; i < 4; ++i) {
        const QRectF tr = templateRect(i);
        const bool active = (m_tpl == tpls[i]);
        p.setPen(QPen(active ? m_pal.accent : m_pal.border, active ? 1.6 : 1.0));
        p.setBrush(m_dark ? QColor("#242632") : QColor("#F6F6F4"));
        p.drawRoundedRect(tr, 9.0, 9.0);

        const QColor lineCol = m_dark ? QColor(255, 255, 255, 42) : QColor(15, 23, 42, 34);
        if (tpls[i] == BackgroundTemplate::Grid) {
            p.setPen(QPen(lineCol, 1.0));
            for (qreal x = tr.left() + 10; x < tr.right() - 4; x += 10.0)
                p.drawLine(QPointF(x, tr.top() + 5), QPointF(x, tr.bottom() - 5));
            for (qreal y = tr.top() + 10; y < tr.bottom() - 4; y += 10.0)
                p.drawLine(QPointF(tr.left() + 5, y), QPointF(tr.right() - 5, y));
        } else if (tpls[i] == BackgroundTemplate::Dots) {
            p.setPen(Qt::NoPen);
            p.setBrush(lineCol);
            for (qreal x = tr.left() + 9; x < tr.right() - 4; x += 9.0)
                for (qreal y = tr.top() + 9; y < tr.bottom() - 4; y += 9.0)
                    p.drawEllipse(QPointF(x, y), 1.1, 1.1);
        } else if (tpls[i] == BackgroundTemplate::Ruled) {
            p.setPen(QPen(lineCol, 1.0));
            for (qreal y = tr.top() + 10; y < tr.bottom() - 4; y += 8.0)
                p.drawLine(QPointF(tr.left() + 6, y), QPointF(tr.right() - 6, y));
        }
    }

    p.setFont(editorFont(10.0, QFont::Bold));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(14, m_scaleSeg->y() - 18, width() - 28, 16),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("TRAMA"));
}

void EditorPaperPanel::mouseMoveEvent(QMouseEvent* e) {
    int ht = -1;
    for (int i = 0; i < 4; ++i)
        if (templateRect(i).contains(e->position())) { ht = i; break; }
    bool overColor = false;
    for (int i = 0; !overColor && i < m_sheetColors.size(); ++i)
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
    const BackgroundTemplate tpls[4] = {
        BackgroundTemplate::Blank, BackgroundTemplate::Grid,
        BackgroundTemplate::Dots, BackgroundTemplate::Ruled
    };
    for (int i = 0; i < 4; ++i) {
        if (templateRect(i).contains(e->position())) {
            m_canvas->setBackgroundTemplate(tpls[i]);
            syncFromCanvas();
            return;
        }
    }
}

} // namespace finily
