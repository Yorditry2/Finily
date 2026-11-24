#include "CanvasWidget.h"
#include "CanvasWidgetInternal.h"
#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"
#include "engine/LassoEngine.h"
#include "engine/LiveStroke.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace finily {

void CanvasWidget::invalidateStaticCache() {
    m_staticCacheDirty = true;
    update();
}

void CanvasWidget::rebuildStaticCache() {
    const double dpr = devicePixelRatioF();
    const QSize pixels(qCeil(width() * dpr), qCeil(height() * dpr));
    if (pixels.isEmpty())
        return;
    if (m_staticCache.size() != pixels || m_staticCache.devicePixelRatio() != dpr) {
        m_staticCache = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        m_staticCache.setDevicePixelRatio(dpr);
    }
    const auto theme = getThemeConfig(m_themeId);
    const bool hasPdfPage = std::any_of(m_images.cbegin(), m_images.cend(),
                                        [](const ImageItem& img) { return img.isPdfPage && !img.rect.isEmpty(); });
    const auto sheet = hasPdfPage ? QRectF() : sheetRectForPageFormat(m_pageFormat);
    const bool finiteSheet = hasPdfPage || !sheet.isEmpty();
    // En Hoja A4, Diapo 16:9 y PDFs, el exterior ("la mesa") tiene un tono de escritorio
    // diferenciado para que la hoja centrada destaque con claridad.
    const QColor deskColor = theme.isDark ? theme.background.darker(135) : QColor("#e4e7ee");
    m_staticCache.fill(finiteSheet ? deskColor : theme.background);
    QPainter p(&m_staticCache);
    p.translate(m_viewport.x, m_viewport.y);
    p.scale(m_viewport.zoom, m_viewport.zoom);
    const QRectF visible(screenToWorld(QPointF(0, 0)), screenToWorld(QPointF(width(), height())));
    if (hasPdfPage) {
        // Modo presentación / PDF: fondo de escritorio limpio SIN cuadrícula de pizarra detrás.
    } else if (sheet.isEmpty()) {
        drawBackgroundTemplate(p, backgroundTemplate(), theme, m_viewport, visible);
    } else {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(sheet.translated(0, 4.0 / m_viewport.zoom), QColor(0, 0, 0, theme.isDark ? 65 : 28));
        p.fillRect(sheet, theme.background);
        p.save();
        p.setClipRect(sheet, Qt::IntersectClip);
        drawBackgroundTemplate(p, backgroundTemplate(), theme, m_viewport, sheet.intersected(visible));
        p.restore();
        p.setBrush(Qt::NoBrush);
        QPen border(theme.isDark ? QColor(255, 255, 255, 38) : QColor(15, 23, 42, 36));
        border.setStyle(Qt::SolidLine);
        border.setWidthF(1.2 / m_viewport.zoom);
        p.setPen(border);
        p.drawRect(sheet);
        p.restore();
        p.setClipRect(sheet, Qt::IntersectClip);
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& image : m_images) {
            if (image.isPdfPage != (pass == 0) || image.id == m_selectedImageId
                || m_selectedImageIds.contains(image.id) || !image.rect.intersects(visible))
                continue;
            if (image.isPdfPage) {
                p.fillRect(image.rect.translated(4 / m_viewport.zoom, 5 / m_viewport.zoom), QColor(0, 0, 0, 38));
                p.fillRect(image.rect, Qt::white);
            }
            p.drawImage(image.rect, image.image);
        }
    }
    const bool lightHlBg = !getThemeConfig(m_themeId).isDark || hasPdfPage;
    auto drawPass = [&](QPainter& target, bool highlights) {
        for (const auto& stroke : m_strokes) {
            if ((stroke.tool == ToolType::Highlighter) != highlights || m_selectedStrokeIds.contains(stroke.id))
                continue;
            const double margin = std::max(16.0, stroke.size * 5.0);
            if (stroke.bounds.adjusted(-margin, -margin, margin, margin).intersects(visible))
                drawStroke(target, stroke, lightHlBg);
        }
    };
    auto drawShapes = [&](QPainter& target, bool shading) {
        for (const auto& shape : m_shapes)
            if ((shape.kind == ShapeKind::ShadedRegion) == shading && shape.id != m_selectedShapeId
                && !m_selectedShapeIds.contains(shape.id) && shape.bounds.intersects(visible))
                drawShape(target, shape);
    };
    const bool selectedHighlight = std::any_of(m_strokes.cbegin(), m_strokes.cend(),
                                               [&](const Stroke& stroke) {
                                                   return m_selectedStrokeIds.contains(stroke.id)
                                                       && stroke.tool == ToolType::Highlighter;
                                               });
    const bool selectedShade = std::any_of(m_shapes.cbegin(), m_shapes.cend(),
                                           [&](const ShapeItem& shape) {
                                               return (m_selectedShapeIds.contains(shape.id) || shape.id == m_selectedShapeId)
                                                   && shape.kind == ShapeKind::ShadedRegion;
                                           });
    const bool imageUnderlay = !m_selectedImageId.isEmpty() || !m_selectedImageIds.isEmpty();
    if (!imageUnderlay)
        drawShapes(p, true);
    if (!imageUnderlay && !selectedShade)
        drawPass(p, true);
    if ((m_hasActiveStroke && m_activeStroke.tool == ToolType::Highlighter) || selectedHighlight
        || selectedShade || imageUnderlay) {
        auto& layers = highlighterLayers(this);
        layers.background = m_staticCache.copy();
        layers.ink = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        layers.ink.setDevicePixelRatio(dpr);
        layers.ink.fill(Qt::transparent);
        {
            QPainter inkPainter(&layers.ink);
            inkPainter.translate(m_viewport.x, m_viewport.y);
            inkPainter.scale(m_viewport.zoom, m_viewport.zoom);
            if (!sheet.isEmpty())
                inkPainter.setClipRect(sheet, Qt::IntersectClip);
            if (imageUnderlay)
                drawShapes(inkPainter, true);
            if (imageUnderlay || selectedShade)
                drawPass(inkPainter, true);
            drawShapes(inkPainter, false);
            drawPass(inkPainter, false);
            for (const auto& item : m_texts)
                if (item.id != m_selectedTextId && !m_selectedTextIds.contains(item.id))
                    drawTextItem(inkPainter, item);
        }
        p.save();
        p.resetTransform();
        p.drawImage(QPointF(0, 0), layers.ink);
        p.restore();
    } else {
        drawShapes(p, false);
        drawPass(p, false);
        for (const auto& item : m_texts)
            if (item.id != m_selectedTextId && !m_selectedTextIds.contains(item.id))
                drawTextItem(p, item);
        // Release temporary full-size buffers as soon as the highlight commits.
        auto* child = findChild<QObject*>(QStringLiteral("_finily_highlighter_layers"), Qt::FindDirectChildrenOnly);
        if (child) {
            auto* layers = static_cast<HighlighterLayers*>(child);
            layers->background = QImage();
            layers->ink = QImage();
        }
    }
    m_staticCacheDirty = false;
}

void CanvasWidget::paintEvent(QPaintEvent*) {
    const double dpr = devicePixelRatioF();
    const QSize pixels(qCeil(width() * dpr), qCeil(height() * dpr));
    if (m_staticCacheDirty || m_staticCache.size() != pixels || m_staticCache.devicePixelRatio() != dpr)
        rebuildStaticCache();
    const bool liveHighlight = (m_hasActiveStroke && m_activeStroke.tool == ToolType::Highlighter)
        || !m_selectedImageId.isEmpty() || !m_selectedImageIds.isEmpty()
        || std::any_of(m_strokes.cbegin(), m_strokes.cend(), [&](const Stroke& stroke) {
               return m_selectedStrokeIds.contains(stroke.id) && stroke.tool == ToolType::Highlighter;
           })
        || std::any_of(m_shapes.cbegin(), m_shapes.cend(), [&](const ShapeItem& shape) {
               return (m_selectedShapeIds.contains(shape.id) || shape.id == m_selectedShapeId)
                   && shape.kind == ShapeKind::ShadedRegion;
           });
    QPainter p(this);
    p.drawImage(QPointF(0, 0), liveHighlight ? highlighterLayers(this).background : m_staticCache);
    p.translate(m_viewport.x, m_viewport.y);
    p.scale(m_viewport.zoom, m_viewport.zoom);
    const bool hasPdfPage = std::any_of(m_images.cbegin(), m_images.cend(),
                                        [](const ImageItem& img) { return img.isPdfPage && !img.rect.isEmpty(); });
    const QRectF sheet = hasPdfPage ? QRectF() : sheetRectForPageFormat(m_pageFormat);
    if (!sheet.isEmpty())
        p.setClipRect(sheet, Qt::IntersectClip);
    const bool lightHlBg = !getThemeConfig(m_themeId).isDark || hasPdfPage;
    const QPointF moveDelta = (m_mode == InteractionMode::LassoMove) ? m_lassoPreviewDelta : QPointF();
    auto drawWithDelta = [&](bool locked, auto&& fn) {
        if (!locked && !moveDelta.isNull()) {
            p.save();
            p.translate(moveDelta);
            fn();
            p.restore();
        } else {
            fn();
        }
    };
    for (const auto& image : m_images) {
        if (image.id == m_selectedImageId || m_selectedImageIds.contains(image.id)) {
            drawWithDelta(image.locked && image.id != m_selectedImageId, [&] {
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                p.drawImage(image.rect, image.image);
            });
        }
    }
    for (const auto& shape : m_shapes)
        if (shape.kind == ShapeKind::ShadedRegion
            && (m_selectedShapeIds.contains(shape.id) || shape.id == m_selectedShapeId))
            drawWithDelta(shape.locked, [&] { drawShape(p, shape); });
    for (const auto& stroke : m_strokes)
        if (m_selectedStrokeIds.contains(stroke.id) && stroke.tool == ToolType::Highlighter)
            drawWithDelta(false, [&] { drawStroke(p, stroke, lightHlBg); });
    if (m_hasActiveStroke) {
        if (m_liveInkPending) {
            m_liveInk.advance(m_activeStroke, m_liveInkLayer, m_viewport, pixels, dpr);
            m_liveInkPending = false;
        }
        const Stroke live = withPredictedTip(m_activeStroke, m_viewport.zoom);
        if (LiveInkRenderer::eligible(live) && m_liveInk.usable(m_viewport, pixels)
            && m_liveInkLayer.devicePixelRatio() == dpr) {
            p.save();
            p.resetTransform();
            p.drawImage(QPointF(0, 0), m_liveInkLayer);
            p.restore();
            m_liveInk.drawTail(p, live);
        } else {
            drawStroke(p, live, lightHlBg);
        }
    }
    if (liveHighlight) {
        p.save();
        p.resetTransform();
        p.drawImage(QPointF(0, 0), highlighterLayers(this).ink);
        p.restore();
    }
    for (const auto& shape : m_shapes)
        if (shape.kind != ShapeKind::ShadedRegion && m_selectedShapeIds.contains(shape.id))
            drawWithDelta(shape.locked, [&] { drawShape(p, shape); });
    for (const auto& stroke : m_strokes)
        if (m_selectedStrokeIds.contains(stroke.id) && stroke.tool != ToolType::Highlighter)
            drawWithDelta(false, [&] { drawStroke(p, stroke); });
    for (const auto& item : m_texts)
        if (m_selectedTextIds.contains(item.id) && item.id != m_selectedTextId)
            drawWithDelta(item.locked, [&] { drawTextItem(p, item); });
    if (m_mode == InteractionMode::LassoDraw)
        drawLassoPath(p, m_lassoPolygon, getThemeConfig(m_themeId), m_viewport.zoom);
    if (hasLassoSelection())
        drawLassoSelectionBox(p, m_lassoBounds.translated(moveDelta), getThemeConfig(m_themeId), m_viewport.zoom, true);
    for (const auto& image : m_images) {
        if (image.id == m_selectedImageId) {
            const auto theme = getThemeConfig(m_themeId);
            p.setPen(QPen(theme.accent, 1.4 / m_viewport.zoom, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(image.rect);
            p.setPen(QPen(theme.accent, 1.5 / m_viewport.zoom));
            p.setBrush(theme.isDark ? theme.background : QColor(Qt::white));
            for (auto point : {image.rect.topLeft(), image.rect.topRight(), image.rect.bottomRight(), image.rect.bottomLeft()})
                p.drawEllipse(point, 6 / m_viewport.zoom, 6 / m_viewport.zoom);
        }
    }
    if (m_shapePulseAlpha > 0.0) {
        ShapeItem halo = m_shapePulseShape;
        halo.linePattern = LinePattern::Solid;
        const double expansion = (1.0 - m_shapePulseAlpha) * 12.0 / m_viewport.zoom;
        for (int layer = 3; layer >= 1; --layer) {
            halo.color = getThemeConfig(m_themeId).accent;
            halo.opacity = m_shapePulseAlpha * .16;
            halo.size = m_shapePulseShape.size + expansion + layer * 4.0 / m_viewport.zoom;
            drawShape(p, halo);
        }
    }
    if (m_hasActiveShape)
        drawShape(p, m_activeShape);
    if (const auto* selected = findShapeById(m_selectedShapeId)) {
        if (selected->kind != ShapeKind::ShadedRegion && (!m_hasActiveShape || selected->id != m_activeShape.id))
            drawShape(p, *selected);
        drawSelectionOverlay(p, *selected, getThemeConfig(m_themeId), m_viewport.zoom, m_tool == ToolType::Select);
    }
    if (m_polylineActive && m_polylineSnapClose && !m_polylineVertices.isEmpty()) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(getThemeConfig(m_themeId).accent, 2 / m_viewport.zoom));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(m_polylineVertices.front(), 10 / m_viewport.zoom, 10 / m_viewport.zoom);
    }
    for (const auto& item : m_texts) {
        if (item.id == m_selectedTextId) {
            drawTextItem(p, item);
            p.setPen(QPen(getThemeConfig(m_themeId).accent, 1 / m_viewport.zoom, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(item.bounds.adjusted(-4 / m_viewport.zoom, -3 / m_viewport.zoom,
                                                  4 / m_viewport.zoom, 3 / m_viewport.zoom),
                              2, 2);
        }
    }
    p.setRenderHint(QPainter::Antialiasing);
    const auto laserNow = nowMs();
    // Láser continuo de seda (UN SOLO QPainterPath suavizado con quadTo por trazo,
    // CERO bolitas/puntos superpuestos en los vértices!).
    {
        qsizetype segStart = 0;
        while (segStart < m_laserTrail.size()) {
            qsizetype segEnd = segStart + 1;
            while (segEnd < m_laserTrail.size() && !m_laserTrail[segEnd].breakBefore)
                ++segEnd;
            // Recolectar puntos aún vivos de este trazo continuo
            QVector<QPointF> livePts;
            double avgFade = 0.0;
            for (qsizetype i = segStart; i < segEnd; ++i) {
                const double f = std::clamp(1.0 - double(laserNow - m_laserTrail[i].timestampMs) / 1200.0, 0.0, 1.0);
                if (f > 0.02) {
                    livePts.append(m_laserTrail[i].pos);
                    avgFade = std::max(avgFade, f);
                }
            }
            if (livePts.size() >= 2 && avgFade > 0.02) {
                QPainterPath smoothPath;
                smoothPath.moveTo(livePts[0]);
                for (qsizetype i = 1; i + 1 < livePts.size(); ++i) {
                    const QPointF mid = (livePts[i] + livePts[i + 1]) * 0.5;
                    smoothPath.quadTo(livePts[i], mid);
                }
                smoothPath.lineTo(livePts.back());
                const double ease = avgFade * avgFade;
                QColor glowCol("#ff3b30");
                glowCol.setAlphaF(ease * 0.22);
                QColor midCol("#ff453a");
                midCol.setAlphaF(ease * 0.65);
                QColor coreCol("#fff8f6");
                coreCol.setAlphaF(ease * 0.98);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(glowCol, 14.0 / m_viewport.zoom, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.strokePath(smoothPath, p.pen());
                p.setPen(QPen(midCol, 6.2 / m_viewport.zoom, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.strokePath(smoothPath, p.pen());
                p.setPen(QPen(coreCol, 2.2 / m_viewport.zoom, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.strokePath(smoothPath, p.pen());
            }
            segStart = segEnd;
        }
    }
    if (!m_laserTrail.isEmpty()) {
        const auto& tip = m_laserTrail.back();
        const double fade = std::clamp(1.0 - double(laserNow - tip.timestampMs) / 1200.0, 0.0, 1.0);
        if (fade > 0.02) {
            p.setPen(Qt::NoPen);
            for (int layer = 3; layer >= 0; --layer) {
                QColor color = layer == 0 ? QColor("#ffffff") : QColor("#ff3b30");
                color.setAlphaF(fade * (layer == 0 ? 1.0 : layer == 1 ? 0.55 : layer == 2 ? 0.24 : 0.10));
                const double r = (2.6 + layer * 3.4) / m_viewport.zoom;
                p.drawEllipse(tip.pos, r, r);
            }
        }
    }
    drawSmartGuides(p, m_activeGuides, getThemeConfig(m_themeId), m_viewport.zoom);
    if (m_showPenCursor) {
        p.save();
        p.resetTransform();
        const QPointF c = m_hoverWorldPos * m_viewport.zoom + QPointF(m_viewport.x, m_viewport.y);
        if (m_tool == ToolType::Laser) {
            p.setPen(Qt::NoPen);
            QColor glow("#ff3b30");
            glow.setAlpha(75);
            p.setBrush(glow);
            p.drawEllipse(c, 10.0, 10.0);
            p.setBrush(QColor("#ff3b30"));
            p.drawEllipse(c, 4.5, 4.5);
            p.setBrush(QColor("#ffffff"));
            p.drawEllipse(c, 1.9, 1.9);
        } else if (m_cursorStyle == 0) {
            // Plantilla 0: Lapicito estilizado cuya punta exacta está en c!
            const bool darkBg = getThemeConfig(m_themeId).isDark;
            const QColor outline = darkBg ? QColor("#0f172a") : QColor("#1e293b");
            const QColor bodyCol = darkBg ? QColor("#f8fafc") : QColor("#ffffff");
            QColor ink = inkColor();
            if (m_tool == ToolType::Highlighter)
                ink.setAlpha(200);
            // Pequeño punto de contacto en la punta exacta
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawEllipse(c, 1.8, 1.8);
            // Cuerpo del lapicito apuntando a c desde arriba-derecha (45°)
            p.save();
            p.translate(c);
            p.rotate(-45.0);
            // Cono de la punta (x: 0..6)
            QPainterPath tip;
            tip.moveTo(0.0, 0.0);
            tip.lineTo(6.5, -2.8);
            tip.lineTo(6.5, 2.8);
            tip.closeSubpath();
            p.setPen(QPen(outline, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(QColor("#e2e8f0"));
            p.drawPath(tip);
            // Punta coloreada con la tinta activa
            QPainterPath nib;
            nib.moveTo(0.0, 0.0);
            nib.lineTo(2.8, -1.2);
            nib.lineTo(2.8, 1.2);
            nib.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawPath(nib);
            // Banda de color de tinta (x: 6.5..10.5)
            p.setPen(QPen(outline, 1.0));
            p.setBrush(ink);
            p.drawRect(QRectF(6.5, -2.8, 4.0, 5.6));
            // Barril limpio (x: 10.5..21.5)
            p.setBrush(bodyCol);
            p.drawRoundedRect(QRectF(10.5, -2.8, 11.0, 5.6), 1.6, 1.6);
            p.restore();
        } else if (m_cursorStyle == 1) {
            // Plantilla 1: Aro de precisión + punto de tinta real
            const double rawR = strokeSize() * m_viewport.zoom / 2.0;
            const double ringR = std::max(5.0, rawR + 1.5);
            const bool darkBg = getThemeConfig(m_themeId).isDark;
            const QColor outer = darkBg ? QColor(255, 255, 255, 215) : QColor(15, 23, 42, 195);
            const QColor halo = darkBg ? QColor(0, 0, 0, 155) : QColor(255, 255, 255, 210);
            p.setPen(QPen(halo, 2.4));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, ringR, ringR);
            p.setPen(QPen(outer, 1.3));
            p.drawEllipse(c, ringR, ringR);
            p.setPen(Qt::NoPen);
            p.setBrush(inkColor());
            p.drawEllipse(c, std::max(2.0, rawR), std::max(2.0, rawR));
        } else {
            // Plantilla 2: Cruz fina técnica + punto central de tinta
            const bool darkBg = getThemeConfig(m_themeId).isDark;
            const QColor lineCol = darkBg ? QColor(255, 255, 255, 210) : QColor(15, 23, 42, 195);
            p.setPen(QPen(lineCol, 1.3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c + QPointF(-8, 0), c + QPointF(-3, 0));
            p.drawLine(c + QPointF(3, 0), c + QPointF(8, 0));
            p.drawLine(c + QPointF(0, -8), c + QPointF(0, -3));
            p.drawLine(c + QPointF(0, 3), c + QPointF(0, 8));
            p.setPen(Qt::NoPen);
            p.setBrush(inkColor());
            p.drawEllipse(c, 2.0, 2.0);
        }
        p.restore();
    }
    if (m_showEraserCursor) {
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(getThemeConfig(m_themeId).accent);
        pen.setCosmetic(true);
        pen.setWidthF(1.3);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(m_eraserWorldPos, m_eraserSize / 2, m_eraserSize / 2);
    }
    if (m_radialOpen) {
        p.save();
        p.resetTransform();
        p.setRenderHint(QPainter::Antialiasing, true);
        const ThemeConfig th = getThemeConfig(m_themeId);
        const QPointF c = m_radialCenter;
        // Anillo limpio de cristal con 8 colores (sin texto cortado)
        const QColor bg = th.isDark ? QColor(18, 28, 24, 226) : QColor(255, 255, 255, 236);
        const QColor ringCol = th.isDark ? QColor(255, 255, 255, 34) : QColor(15, 23, 42, 26);
        // Sombra suave exterior
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, th.isDark ? 75 : 32));
        p.drawEllipse(c + QPointF(0, 4), 78.0, 78.0);
        // Anillo principal (dona limpia)
        QPainterPath ring;
        ring.setFillRule(Qt::OddEvenFill);
        ring.addEllipse(c, 76.0, 76.0);
        ring.addEllipse(c, 24.0, 24.0);
        p.setPen(QPen(ringCol, 1.2));
        p.setBrush(bg);
        p.drawPath(ring);

        const QVector<QColor> rColors = th.isDark
            ? QVector<QColor>{QColor("#f8fafc"), QColor("#fde047"), QColor("#fb923c"), QColor("#f87171"),
                              QColor("#f472b6"), QColor("#c084fc"), QColor("#38bdf8"), QColor("#4ade80")}
            : QVector<QColor>{QColor("#0f172a"), QColor("#ca8a04"), QColor("#ea580c"), QColor("#dc2626"),
                              QColor("#db2777"), QColor("#9333ea"), QColor("#0284c7"), QColor("#16a34a")};
        constexpr double kPi = 3.14159265358979323846;
        for (int i = 0; i < 8; ++i) {
            const double ang = -kPi / 2.0 + i * (kPi / 4.0);
            const QPointF pt = c + QPointF(std::cos(ang) * 50.0, std::sin(ang) * 50.0);
            const bool hov = (m_radialHoveredColor == i);
            if (hov) {
                p.setPen(QPen(th.accent, 2.2));
                p.setBrush(QColor(th.accent.red(), th.accent.green(), th.accent.blue(), 55));
                p.drawEllipse(pt, 16.5, 16.5);
            }
            p.setPen(QPen(th.isDark ? QColor(255, 255, 255, 150) : QColor(15, 23, 42, 110), hov ? 1.8 : 1.0));
            p.setBrush(rColors[i]);
            p.drawEllipse(pt, hov ? 12.0 : 9.5, hov ? 12.0 : 9.5);
        }
        // Puntero visible desde el centro hacia el cursor para seleccionar con precisión
        const QPointF dir = m_radialCursorPos - c;
        const double dist = std::hypot(dir.x(), dir.y());
        if (dist > 4.0) {
            const double clamped = std::min(dist, 64.0);
            const QPointF tip = c + (dir / dist) * clamped;
            p.setPen(QPen(th.accent, 2.0, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c, tip);
            p.setPen(Qt::NoPen);
            p.setBrush(th.accent);
            p.drawEllipse(tip, 3.6, 3.6);
        }
        // Punto central con color activo (o el color resaltado)
        p.setPen(QPen(ringCol, 1.0));
        p.setBrush(m_radialHoveredColor >= 0 ? rColors[m_radialHoveredColor] : m_color);
        p.drawEllipse(c, 8.0, 8.0);
        p.restore();
    }
}

void CanvasWidget::updateRadialHover(const QPointF& screenPos) {
    m_radialCursorPos = screenPos;
    const QPointF d = screenPos - m_radialCenter;
    const double r = std::hypot(d.x(), d.y());
    constexpr double kPi = 3.14159265358979323846;
    int newCol = -1;
    if (r >= 20.0 && r <= 110.0) {
        double ang = std::atan2(d.y(), d.x()) + kPi / 2.0 + kPi / 8.0;
        while (ang < 0)
            ang += 2.0 * kPi;
        while (ang >= 2.0 * kPi)
            ang -= 2.0 * kPi;
        newCol = std::clamp(int(ang / (kPi / 4.0)), 0, 7);
    }
    m_radialHoveredColor = newCol;
    update();
}

void CanvasWidget::commitRadialSelection(bool quickTap) {
    const int colIdx = m_radialHoveredColor;
    m_radialOpen = false;
    m_radialHoveredColor = -1;
    m_radialHoveredTool = -1;
    if (colIdx >= 0) {
        const ThemeConfig th = getThemeConfig(m_themeId);
        const QVector<QColor> rColors = th.isDark
            ? QVector<QColor>{QColor("#f8fafc"), QColor("#fde047"), QColor("#fb923c"), QColor("#f87171"),
                              QColor("#f472b6"), QColor("#c084fc"), QColor("#38bdf8"), QColor("#4ade80")}
            : QVector<QColor>{QColor("#0f172a"), QColor("#ca8a04"), QColor("#ea580c"), QColor("#dc2626"),
                              QColor("#db2777"), QColor("#9333ea"), QColor("#0284c7"), QColor("#16a34a")};
        // Primero volvemos al lápiz (sin recolorear una figura seleccionada por accidente) y luego cambiamos color!
        if (m_tool != ToolType::Pen && m_tool != ToolType::Ballpoint && m_tool != ToolType::Highlighter)
            setTool(m_lastWriteTool);
        setInkColor(rColors[colIdx]);
    } else if (quickTap) {
        setTool(m_lastWriteTool);
    }
    updateCursorShape();
    update();
}

void CanvasWidget::clearPenCursor() {
    if (!m_showPenCursor)
        return;
    m_showPenCursor = false;
    update(m_penCursorRect);
    m_penCursorRect = {};
}

void CanvasWidget::updatePenCursor(const QPointF& world, bool visible) {
    if (!visible) {
        clearPenCursor();
        return;
    }
    const QPointF center = world * m_viewport.zoom + QPointF(m_viewport.x, m_viewport.y);
    const double radius = std::max(26.0, strokeSize() * m_viewport.zoom / 2.0 + 6.0);
    const QRect dirty = QRectF(center - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)).toAlignedRect();
    // Coalesce subpixel hardware jitter without timers or full-canvas repaints.
    if (m_showPenCursor && dirty == m_penCursorRect
        && distance(world, m_hoverWorldPos) * m_viewport.zoom < .5)
        return;
    if (m_showPenCursor)
        update(m_penCursorRect);
    m_hoverWorldPos = world;
    m_showPenCursor = true;
    m_penCursorRect = dirty;
    update(dirty);
}

QRect CanvasWidget::shapePulseRect() const {
    const auto bounds = computeShapeBounds(m_shapePulseShape);
    const QPointF offset(m_viewport.x, m_viewport.y);
    return QRectF(bounds.topLeft() * m_viewport.zoom + offset,
                  bounds.bottomRight() * m_viewport.zoom + offset)
        .adjusted(-28, -28, 28, 28)
        .toAlignedRect();
}

void CanvasWidget::stopShapePulse() {
    if (!m_shapePulseAnimation)
        return;
    m_shapePulseAnimation->stop();
    if (m_shapePulseAlpha > 0.0) {
        m_shapePulseAlpha = 0.0;
        update(shapePulseRect());
    }
}

} // namespace finily
