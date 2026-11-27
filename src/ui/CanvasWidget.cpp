#include "CanvasWidget.h"
#include "CanvasWidgetInternal.h"
#include "core/Utils.h"
#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"

#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace finily {

CanvasWidget::CanvasWidget(QWidget* parent)
    : QWidget(parent)
    , m_stabilizerFilter(std::make_unique<VelocityGaussianStabilizer>()) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_StaticContents);
    setAttribute(Qt::WA_TabletTracking);
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    m_stabilizerFilter->setStrength(m_stabilizer);

    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(380);
    connect(m_holdTimer, &QTimer::timeout, this, &CanvasWidget::triggerHoldToShape);

    m_laserTimer = new QTimer(this);
    m_laserTimer->setInterval(25);
    connect(m_laserTimer, &QTimer::timeout, this, [this] {
        const auto now = nowMs();
        m_laserTrail.removeIf([now](const LaserSample& sample) {
            return now - sample.timestampMs >= 1400;
        });
        if (m_laserTrail.isEmpty())
            m_laserTimer->stop();
        updateLaserDirtyRect();
    });

    m_zoomAnimation = new QVariantAnimation(this);
    m_zoomAnimation->setDuration(140);
    m_zoomAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_zoomAnimation->setStartValue(0.0);
    m_zoomAnimation->setEndValue(1.0);
    connect(m_zoomAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const double t = value.toDouble();
        m_doc.viewport() = {
            m_zoomStart.x + (m_zoomTarget.x - m_zoomStart.x) * t,
            m_zoomStart.y + (m_zoomTarget.y - m_zoomStart.y) * t,
            m_zoomStart.zoom + (m_zoomTarget.zoom - m_zoomStart.zoom) * t
        };
        emit viewportChanged(m_doc.viewport().zoom);
        update();
    });
    connect(m_zoomAnimation, &QVariantAnimation::finished, this, &CanvasWidget::invalidateStaticCache);

    m_shapePulseAnimation = new QVariantAnimation(this);
    m_shapePulseAnimation->setDuration(220);
    m_shapePulseAnimation->setStartValue(1.0);
    m_shapePulseAnimation->setEndValue(0.0);
    connect(m_shapePulseAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                m_shapePulseAlpha = value.toDouble();
                update(shapePulseRect());
            });

    // Autoguardado diferido: se agrupan los cambios y se escribe 1.5 s después
    // del último, nunca en mitad de un trazo.
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setSingleShot(true);
    m_autosaveTimer->setInterval(1500);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] {
        if (m_mode != InteractionMode::None) {
            m_autosaveTimer->start(1500);
            return;
        }
        writeAutosaveNow();
    });

    m_beautifyTimer = new QTimer(this);
    m_beautifyTimer->setSingleShot(true);
    m_beautifyTimer->setInterval(650);
    connect(m_beautifyTimer, &QTimer::timeout, this, &CanvasWidget::flushPendingBeautify);

    {
        auto settings = appSettings();
        m_mathBeautifyEnabled = settings->value("ink/mathBeautify", false).toBool();
        m_autoColorOperators = settings->value("ink/autoColorOperators", true).toBool();
        const QColor savedOp(settings->value("ink/operatorColor", "#ef4444").toString());
        if (savedOp.isValid())
            m_operatorColor = savedOp;
        m_cursorStyle = std::clamp(settings->value("ink/cursorStyle", 0).toInt(), 0, 2);
        m_penTip = std::clamp(settings->value("ink/penTip", 1).toInt(), 0, 3);
        m_highlighterTip = std::clamp(settings->value("ink/highlighterTip", 0).toInt(), 0, 2);
        m_eraserPreserveShapes = settings->value("ink/eraserPreserveShapes", false).toBool();
        m_eraserOnlyHighlight = settings->value("ink/eraserOnlyHighlight", false).toBool();
        m_pressureSensitivity =
            std::clamp(settings->value("ink/pressureSensitivity", 0.5).toDouble(), 0.0, 1.0);
        m_highlighterOpacity =
            std::clamp(settings->value("ink/highlighterOpacity", 0.25).toDouble(), 0.10, 0.70);
        const double startupScale =
            clampTemplateScale(settings->value("ink/templateScale", 1.0).toDouble());
        m_doc.configureBlank(ThemeId::DarkBoard, BackgroundTemplate::Dots, PageFormat::Infinite,
                             startupScale, CoverStyle{}, Viewport{});
    }

    loadAutosave();
    updateCursorShape();
}

CanvasWidget::~CanvasWidget() {
    finishTextEditor(false);
    m_autosaveTimer->stop();
    waitForAutosave();
    writeAutosaveNow(false);
}

void CanvasWidget::setTool(ToolType tool) {
    clearPenCursor();
    clearEraserCursor();
    const bool hadRadial = m_radialOpen;
    m_radialOpen = false;
    finishTextEditor(true);
    if (m_tool == ToolType::Select && tool != m_tool && m_mode != InteractionMode::None)
        cancelInteraction();
    if (m_polylineActive && tool != m_tool)
        finishActivePolyline();
    if ((hasLassoSelection() || hasSelectedImage()) && tool != ToolType::Lasso && tool != ToolType::Select)
        clearSelection();
    if (tool == ToolType::Lasso && m_tool != ToolType::Lasso)
        clearSelection();
    if (tool == ToolType::Select && m_tool == ToolType::Lasso && hasLassoSelection())
        clearSelection();
    if (tool == ToolType::Pen || tool == ToolType::Ballpoint || tool == ToolType::Highlighter)
        m_lastWriteTool = tool;
    m_tool = tool;
    if (tool == ToolType::Polyline && m_linePattern == LinePattern::Dimension)
        m_linePattern = LinePattern::Solid;
    if (tool != ToolType::Text && tool != ToolType::Lasso && tool != ToolType::Select
        && tool != ToolType::Shape && tool != ToolType::Polyline
        && tool != ToolType::Pen && tool != ToolType::Ballpoint) {
        clearSelection();
    }
    updateCursorShape();
    if (hadRadial)
        update(radialMenuRect());
    emit stateChanged();
}

void CanvasWidget::setShapeKind(ShapeKind kind) {
    m_shapeKind = kind;
    emit stateChanged();
}

void CanvasWidget::setPolygonSides(int sides) {
    m_polygonSides = std::clamp(sides, 3, 12);
    emit stateChanged();
}

void CanvasWidget::setShadePattern(ShadePattern pattern) {
    m_shadePattern = pattern;
    emit stateChanged();
}

void CanvasWidget::setHoldToShapeEnabled(bool enabled) {
    m_holdToShapeEnabled = enabled;
    if (!enabled)
        m_holdTimer->stop();
    emit stateChanged();
}

void CanvasWidget::setSmartGuidesEnabled(bool enabled) {
    m_smartGuidesEnabled = enabled;
    if (!enabled)
        m_activeGuides.clear();
    update();
    emit stateChanged();
}

void CanvasWidget::setMathBeautifyEnabled(bool enabled) {
    if (m_mathBeautifyEnabled == enabled)
        return;
    if (!enabled)
        flushPendingBeautify();
    m_mathBeautifyEnabled = enabled;
    writeAppSetting(QStringLiteral("ink/mathBeautify"), enabled);
    emit toastRequested(enabled ? QStringLiteral("Auto-embellecer escritura activado (M)")
                                : QStringLiteral("Modo Escritura libre (M)"));
    emit stateChanged();
}

void CanvasWidget::setAutoColorOperators(bool enabled) {
    if (m_autoColorOperators == enabled)
        return;
    m_autoColorOperators = enabled;
    writeAppSetting(QStringLiteral("ink/autoColorOperators"), enabled);
    emit stateChanged();
}

void CanvasWidget::setOperatorColor(const QColor& color) {
    if (!color.isValid() || m_operatorColor == color)
        return;
    m_operatorColor = color;
    writeAppSetting(QStringLiteral("ink/operatorColor"), color.name(QColor::HexRgb));
    emit stateChanged();
}

void CanvasWidget::setCursorStyle(int style) {
    style = std::clamp(style, 0, 2);
    if (m_cursorStyle == style)
        return;
    m_cursorStyle = style;
    writeAppSetting(QStringLiteral("ink/cursorStyle"), style);
    clearPenCursor();
    emit stateChanged();
}

void CanvasWidget::setPenTip(int tip) {
    tip = std::clamp(tip, 0, 3);
    m_penTip = tip;
    if (m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint) {
        m_tool = (tip == 0) ? ToolType::Ballpoint : ToolType::Pen;
        m_lastWriteTool = m_tool;
    }
    writeAppSetting(QStringLiteral("ink/penTip"), tip);
    emit stateChanged();
}

void CanvasWidget::setHighlighterTip(int tip) {
    tip = std::clamp(tip, 0, 2);
    if (m_highlighterTip == tip)
        return;
    m_highlighterTip = tip;
    writeAppSetting(QStringLiteral("ink/highlighterTip"), tip);
    emit stateChanged();
}

void CanvasWidget::setSwapPenButtons(bool swapped) {
    m_swapPenButtons = swapped;
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::setLassoRectMode(bool rectMode) {
    if (m_lassoRectMode == rectMode)
        return;
    m_lassoRectMode = rectMode;
    emit stateChanged();
}

void CanvasWidget::setEraserMode(EraserMode mode) {
    m_eraserMode = mode;
    emit stateChanged();
}

void CanvasWidget::setEraserSize(double size) {
    if (!isFinite(size))
        return;
    m_eraserSize = std::clamp(size, 1.0, 500.0);
    update();
    emit stateChanged();
}

void CanvasWidget::setEraserPreserveShapes(bool preserve) {
    if (m_eraserPreserveShapes == preserve)
        return;
    m_eraserPreserveShapes = preserve;
    writeAppSetting(QStringLiteral("ink/eraserPreserveShapes"), preserve);
    emit stateChanged();
}

void CanvasWidget::setEraserOnlyHighlight(bool onlyHighlight) {
    if (m_eraserOnlyHighlight == onlyHighlight)
        return;
    m_eraserOnlyHighlight = onlyHighlight;
    writeAppSetting(QStringLiteral("ink/eraserOnlyHighlight"), onlyHighlight);
    emit stateChanged();
}

void CanvasWidget::setInkColor(const QColor& color) {
    if (!color.isValid())
        return;
    clearPenCursor();
    m_color = color;
    // Cambio de color en 1 clic si hay objetos seleccionados (estilo Xournal++ / PDF Annotator)
    if (m_mode == InteractionMode::None && !m_polylineActive && hasSelection()) {
        const uint64_t revBefore = m_doc.revision();
        SelectionStyle edits;
        edits.color = color;
        setSelectionStyle(edits);
        if (m_doc.revision() == revBefore)
            emit stateChanged();
        return;
    }
    emit stateChanged();
}

void CanvasWidget::setStrokeSize(double size) {
    if (!isFinite(size))
        return;
    const double width = std::clamp(size, 0.1, 200.0);
    clearPenCursor();
    m_size = width;
    if (m_mode == InteractionMode::None && !m_polylineActive && hasSelection()) {
        const uint64_t revBefore = m_doc.revision();
        SelectionStyle edits;
        edits.width = width;
        setSelectionStyle(edits);
        if (m_doc.revision() == revBefore)
            emit stateChanged();
        return;
    }
    emit stateChanged();
}

void CanvasWidget::setLinePattern(LinePattern pattern) {
    if (m_linePattern == pattern)
        return;
    m_linePattern = pattern;
    emit stateChanged();
}

void CanvasWidget::setLineEnding(LineEnding ending) {
    if (m_lineEnding == ending)
        return;
    m_lineEnding = ending;
    emit stateChanged();
}

void CanvasWidget::setPressureSensitivity(double sensitivity) {
    if (!isFinite(sensitivity))
        return;
    const double clamped = std::clamp(sensitivity, 0.0, 1.0);
    if (std::abs(m_pressureSensitivity - clamped) < 1e-6)
        return;
    m_pressureSensitivity = clamped;
    writeAppSetting(QStringLiteral("ink/pressureSensitivity"), clamped);
    emit stateChanged();
}

void CanvasWidget::setPressureProfile(PressureProfile profile) {
    if (int(profile) < int(PressureProfile::Light) || int(profile) > int(PressureProfile::Firm))
        return;
    if (m_pressureProfile == profile)
        return;
    m_pressureProfile = profile;
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::setStabilizer(double strength) {
    if (!isFinite(strength))
        return;
    m_stabilizer = std::clamp(strength, 0.0, 1.0);
    m_stabilizerFilter->setStrength(m_stabilizer);
    emit stateChanged();
}

void CanvasWidget::setHighlighterOpacity(double opacity) {
    if (!isFinite(opacity))
        return;
    const double clamped = std::clamp(opacity, 0.10, 0.70);
    if (std::abs(m_highlighterOpacity - clamped) < 1e-6)
        return;
    m_highlighterOpacity = clamped;
    writeAppSetting(QStringLiteral("ink/highlighterOpacity"), clamped);
    emit stateChanged();
}

void CanvasWidget::setThemeId(ThemeId id) {
    clearPenCursor();
    const auto old = getThemeConfig(m_doc.themeId());
    m_doc.setThemeId(id);
    if (m_color == old.defaultInk)
        m_color = getThemeConfig(id).defaultInk;
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
}

void CanvasWidget::setBackgroundTemplate(BackgroundTemplate tpl) {
    if (tpl == m_doc.currentTemplate())
        return;
    const bool pageLevel = m_doc.page(m_doc.currentPageIndex()).background.has_value();
    if (pageLevel && !preparePageChange())
        return;
    if (!m_doc.setBackgroundTemplate(tpl))
        return;
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
}

void CanvasWidget::setTemplateScale(double scale) {
    const double clamped = clampTemplateScale(scale);
    if (std::abs(m_doc.templateScale() - clamped) < 1e-4)
        return;
    m_doc.setTemplateScale(clamped);
    writeAppSetting(QStringLiteral("ink/templateScale"), clamped);
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
}

QRectF CanvasWidget::activeFiniteSheetRect() const {
    for (const auto& item : m_doc.images())
        if (item.isPdfPage && !item.rect.isEmpty())
            return item.rect;
    return sheetRectForPageFormat(m_doc.pageFormat());
}

void CanvasWidget::clampFiniteViewport() {
    if (width() <= 0 || height() <= 0)
        return;
    const QRectF sheet = activeFiniteSheetRect();
    if (sheet.isEmpty())
        return;

    Viewport& vp = m_doc.viewport();
    const double availW = std::max(240.0, width() - 156.0);
    const double availH = std::max(200.0, height() - 116.0);
    const double minZoom = std::clamp(std::min(availW / sheet.width(), availH / sheet.height()), 0.2, 5.0);
    vp.zoom = std::clamp(vp.zoom, minZoom, 5.0);

    const double sheetW = sheet.width() * vp.zoom;
    const double sheetH = sheet.height() * vp.zoom;
    constexpr double marginX = 72.0;
    constexpr double marginY = 58.0;

    if (sheetW <= width() - marginX * 2.0) {
        vp.x = width() / 2.0 - sheet.center().x() * vp.zoom;
    } else {
        const double maxX = marginX - sheet.left() * vp.zoom;
        const double minX = (width() - marginX) - sheet.right() * vp.zoom;
        vp.x = std::clamp(vp.x, std::min(minX, maxX), std::max(minX, maxX));
    }

    if (sheetH <= height() - marginY * 2.0) {
        vp.y = height() / 2.0 - sheet.center().y() * vp.zoom;
    } else {
        const double maxY = marginY - sheet.top() * vp.zoom;
        const double minY = (height() - marginY) - sheet.bottom() * vp.zoom;
        vp.y = std::clamp(vp.y, std::min(minY, maxY), std::max(minY, maxY));
    }
}

void CanvasWidget::setPageFormat(PageFormat format) {
    if (!isValidPageFormat(int(format)))
        return;
    if (m_doc.pageFormat() == format)
        return;
    m_doc.setPageFormat(format);
    m_zoomAnimation->stop();
    clearPenCursor();
    if (format != PageFormat::Infinite) {
        m_doc.viewport() = fittedSheetViewport(format, size());
        clampFiniteViewport();
    }
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
    emit viewportChanged(m_doc.viewport().zoom);
}

QPointF CanvasWidget::screenToWorld(const QPointF& p) const {
    const Viewport& vp = m_doc.viewport();
    return {(p.x() - vp.x) / vp.zoom, (p.y() - vp.y) / vp.zoom};
}

void CanvasWidget::zoomByFactor(double factor, const QPointF& anchor) {
    if (!isFinite(factor) || factor <= 0.0)
        return;
    m_zoomAnimation->stop();
    clearPenCursor();
    const auto world = screenToWorld(anchor);
    Viewport& vp = m_doc.viewport();
    vp.zoom = std::clamp(vp.zoom * factor, 0.2, 5.0);
    vp.x = anchor.x() - world.x() * vp.zoom;
    vp.y = anchor.y() - world.y() * vp.zoom;
    clampFiniteViewport();
    invalidateStaticCache();
    emit viewportChanged(vp.zoom);
}

void CanvasWidget::animateViewport(const Viewport& target) {
    clearPenCursor();
    m_zoomAnimation->stop();
    m_zoomStart = m_doc.viewport();
    m_zoomTarget = target;
    m_zoomAnimation->start();
}

void CanvasWidget::animateZoom(double factor) {
    const QPointF anchor = rect().center();
    const Viewport base = m_zoomAnimation->state() == QAbstractAnimation::Running
        ? m_zoomTarget
        : m_doc.viewport();
    const QPointF world((anchor.x() - base.x) / base.zoom, (anchor.y() - base.y) / base.zoom);
    const double zoom = std::clamp(base.zoom * factor, 0.2, 5.0);
    Viewport target{anchor.x() - world.x() * zoom, anchor.y() - world.y() * zoom, zoom};
    if (!activeFiniteSheetRect().isEmpty()) {
        m_doc.viewport() = target;
        clampFiniteViewport();
        target = m_doc.viewport();
        m_doc.viewport() = base;
    }
    animateViewport(target);
}

void CanvasWidget::zoomIn() {
    animateZoom(1.18);
}

void CanvasWidget::zoomOut() {
    animateZoom(1.0 / 1.18);
}

void CanvasWidget::resetViewport() {
    Viewport target;
    for (const auto& image : m_doc.images()) {
        if (image.isPdfPage && !image.rect.isEmpty()) {
            target = fittedPdfViewport(image.rect, size());
            animateViewport(target);
            saveAutosave();
            return;
        }
    }
    if (m_doc.pageFormat() != PageFormat::Infinite)
        target = fittedSheetViewport(m_doc.pageFormat(), size());
    animateViewport(target);
    saveAutosave();
}

void CanvasWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    m_zoomAnimation->stop();
    const auto& imgs = m_doc.images();
    const auto pdf = std::find_if(imgs.cbegin(), imgs.cend(), [](const auto& image) {
        return image.isPdfPage && !image.rect.isEmpty();
    });
    if (pdf != imgs.cend() && !size().isEmpty()) {
        const auto previous = fittedPdfViewport(pdf->rect, event->oldSize());
        const Viewport& vp = m_doc.viewport();
        const bool wasFitted = std::abs(previous.x - vp.x) < 1.0
            && std::abs(previous.y - vp.y) < 1.0
            && std::abs(previous.zoom - vp.zoom) < 0.005;
        if (wasFitted || event->oldSize().isEmpty() || event->oldSize().width() <= 960) {
            m_doc.viewport() = fittedPdfViewport(pdf->rect, size());
            clampFiniteViewport();
            emit viewportChanged(m_doc.viewport().zoom);
        }
    } else if (m_doc.pageFormat() != PageFormat::Infinite && !size().isEmpty()) {
        const auto previous = fittedSheetViewport(m_doc.pageFormat(), event->oldSize());
        const Viewport& vp = m_doc.viewport();
        const bool wasFitted = std::abs(previous.x - vp.x) < 1.0
            && std::abs(previous.y - vp.y) < 1.0
            && std::abs(previous.zoom - vp.zoom) < 0.005;
        if (wasFitted || event->oldSize().isEmpty() || event->oldSize().width() <= 960)
            m_doc.viewport() = fittedSheetViewport(m_doc.pageFormat(), size());
        clampFiniteViewport();
        emit viewportChanged(m_doc.viewport().zoom);
    }
    invalidateStaticCache();
}

void CanvasWidget::openRadialMenu(const QPointF& centerScreen) {
    clearPenCursor();
    m_radialOpen = true;
    m_radialHoveredColor = -1;
    QPointF c = centerScreen;
    c.setX(std::clamp(c.x(), 90.0, std::max(90.0, width() - 90.0)));
    c.setY(std::clamp(c.y(), 90.0, std::max(90.0, height() - 90.0)));
    m_radialCenter = c;
    m_radialCursorPos = c;
    updateCursorShape();
    update(radialMenuRect());
}

void CanvasWidget::tabletEvent(QTabletEvent* event) {
    event->accept();
    const bool eraser = event->pointerType() == QPointingDevice::PointerType::Eraser;
    const Qt::MouseButtons btns = event->buttons();
    const Qt::MouseButton btn = event->button();

    // Wacom Intuos BT S Pen 4K barrel buttons (calibrated to Yordi's grip):
    // - Larger barrel button (RightButton by default)  => switch to Eraser!
    // - Smaller barrel button (MiddleButton by default) => switch back to Writing (last pen)!
    const Qt::MouseButton eraseBtn = m_swapPenButtons ? Qt::MiddleButton : Qt::RightButton;
    const Qt::MouseButton writeBtn = m_swapPenButtons ? Qt::RightButton : Qt::MiddleButton;

    const bool newlyPressedErase = ((btns & eraseBtn) && !(m_lastTabletButtons & eraseBtn));
    const bool newlyPressedWrite = ((btns & writeBtn) && !(m_lastTabletButtons & writeBtn));
    m_lastTabletButtons = btns;

    if (event->type() != QEvent::TabletRelease) {
        if (newlyPressedErase && event->pressure() <= 0.01) {
            if (m_radialOpen) {
                m_radialOpen = false;
                updateCursorShape();
                update(radialMenuRect());
            }
            switchTabletTool(ToolType::Eraser);
            return;
        }
        if (newlyPressedWrite && event->pressure() <= 0.01) {
            const qint64 now = nowMs();
            if (m_radialOpen) {
                m_radialOpen = false;
                updateCursorShape();
                update(radialMenuRect());
                return;
            }
            const qint64 dt = now - m_lastPenTriggerMs;
            if (m_mode == InteractionMode::None && m_lastPenTriggerMs > 0 && dt >= 70 && dt <= 420) {
                m_lastPenTriggerMs = 0;
                openRadialMenu(event->position());
            } else {
                m_lastPenTriggerMs = now;
                switchTabletTool(m_lastWriteTool);
            }
            return;
        }
    }
    if (m_radialOpen && event->pressure() <= 0.01) {
        updateRadialHover(event->position());
        return;
    }

    const bool useEraser = eraser || ((btns & eraseBtn) != 0);

    switch (event->type()) {
    case QEvent::TabletPress:
        if (btn == eraseBtn && event->pressure() <= 0.01) {
            switchTabletTool(ToolType::Eraser);
            return;
        }
        if (btn == writeBtn && event->pressure() <= 0.01) {
            switchTabletTool(m_lastWriteTool);
            return;
        }
        m_tabletActive = true;
        beginInput(event->position(), event->pressure(), event->xTilt(), event->yTilt(),
                   true, useEraser, Qt::LeftButton, event->modifiers());
        break;
    case QEvent::TabletMove:
        moveInput(event->position(), event->pressure(), event->xTilt(), event->yTilt(),
                  true, useEraser, event->modifiers());
        break;
    case QEvent::TabletRelease:
        if (m_tabletActive) {
            moveInput(event->position(), event->pressure(), event->xTilt(), event->yTilt(),
                      true, useEraser, event->modifiers());
            if (m_mode == InteractionMode::Draw && m_hasActiveStroke && !m_activeStroke.points.isEmpty())
                m_activeStroke.points.back().pressure = std::clamp(event->pressure(), 0.0, 1.0);
            endInput(event->position(), event->modifiers());
        }
        m_tabletActive = false;
        break;
    default:
        break;
    }
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    event->accept();
    if (m_tabletActive || event->source() != Qt::MouseEventNotSynthesized)
        return;
    beginInput(event->position(), 0.5, 0.0, 0.0, false, false, event->button(), event->modifiers());
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    event->accept();
    if (m_mode == InteractionMode::None && !m_tabletActive) {
        if (m_radialOpen) {
            updateRadialHover(event->position());
            return;
        }
        const auto world = screenToWorld(event->position());
        const bool showPen = !m_spaceHeld
            && (m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint
                || m_tool == ToolType::Highlighter || m_tool == ToolType::Laser);
        updatePenCursor(world, showPen);
    }
    if (m_tabletActive || event->source() != Qt::MouseEventNotSynthesized)
        return;
    moveInput(event->position(), 0.5, 0.0, 0.0, false, false, event->modifiers());
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    event->accept();
    if (m_tabletActive || event->source() != Qt::MouseEventNotSynthesized)
        return;
    if ((m_tool == ToolType::Text || m_tool == ToolType::Lasso || m_tool == ToolType::Select)
        && event->button() == Qt::LeftButton) {
        const auto world = screenToWorld(event->position());
        if (std::any_of(m_doc.texts().cbegin(), m_doc.texts().cend(), [&](const TextItem& item) {
                return item.bounds.adjusted(-4, -4, 4, 4).contains(world);
            })) {
            m_mode = InteractionMode::None;
            beginTextEditAt(event->position(), world);
            return;
        }
    }
    if (m_tool == ToolType::Polyline && m_polylineActive && event->button() == Qt::LeftButton) {
        const auto world = screenToWorld(event->position());
        if (m_polylineVertices.size() >= 3
            && pointDistance(world, m_polylineVertices.front()) * m_doc.viewport().zoom <= 14.0) {
            finishActivePolyline(true);
        } else {
            if (pointDistance(world, m_polylineVertices.back()) * m_doc.viewport().zoom > 2.0)
                beginInput(event->position(), 0.5, 0.0, 0.0, false, false, Qt::LeftButton,
                           event->modifiers());
            finishActivePolyline();
        }
    }
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    event->accept();
    if (m_tabletActive || event->source() != Qt::MouseEventNotSynthesized)
        return;
    if (m_mode == InteractionMode::Pan
        && (event->buttons() & (Qt::LeftButton | Qt::MiddleButton | Qt::RightButton))) {
        return;
    }
    if (m_mode == InteractionMode::Draw && event->button() != Qt::LeftButton)
        return;
    const auto delta = event->position() - m_panStartScreen;
    const bool showMenu = m_mode == InteractionMode::Pan && m_rightClickOnSelection
        && event->button() == Qt::RightButton && std::hypot(delta.x(), delta.y()) < 4.0;
    m_rightClickOnSelection = false;
    if (showMenu) {
        m_mode = InteractionMode::None;
        updateCursorShape();
        emit selectionMenuRequested(event->globalPosition().toPoint());
        return;
    }
    endInput(event->position(), event->modifiers());
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    event->accept();
    if (m_zoomAnimation->state() == QAbstractAnimation::Running) {
        m_zoomAnimation->stop();
        invalidateStaticCache();
    }
    if (m_mode != InteractionMode::None)
        return;
    const auto mods = event->modifiers();
    if (mods.testFlag(Qt::ControlModifier)) {
        const double delta = event->pixelDelta().isNull()
            ? event->angleDelta().y()
            : event->pixelDelta().y() * 2.0;
        zoomByFactor(std::exp(delta * 0.0015), event->position());
        return;
    }
    clearPenCursor();
    QPointF scrollDelta;
    if (!event->pixelDelta().isNull()) {
        scrollDelta = QPointF(event->pixelDelta());
    } else {
        const QPoint angle = event->angleDelta();
        if (mods.testFlag(Qt::ShiftModifier) && angle.x() == 0)
            scrollDelta = QPointF(angle.y() * 0.65, 0.0);
        else
            scrollDelta = QPointF(angle.x() * 0.65, angle.y() * 0.65);
    }
    if (scrollDelta.isNull())
        return;
    Viewport& vp = m_doc.viewport();
    vp.x += scrollDelta.x();
    vp.y += scrollDelta.y();
    clampFiniteViewport();
    invalidateStaticCache();
    saveAutosave();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_0 && event->modifiers().testFlag(Qt::ControlModifier)) {
        resetViewport();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_P && !event->isAutoRepeat() && event->modifiers() == Qt::NoModifier) {
        if (m_radialOpen) {
            m_radialOpen = false;
            updateCursorShape();
            update(radialMenuRect());
            event->accept();
            return;
        }
        const qint64 now = nowMs();
        const qint64 dt = now - m_lastPenTriggerMs;
        if (m_mode == InteractionMode::None && m_lastPenTriggerMs > 0 && dt >= 70 && dt <= 420) {
            m_lastPenTriggerMs = 0;
            const Viewport& vp = m_doc.viewport();
            QPointF c = m_showPenCursor
                ? (m_hoverWorldPos * vp.zoom + QPointF(vp.x, vp.y))
                : QPointF(mapFromGlobal(QCursor::pos()));
            if (!rect().contains(c.toPoint()))
                c = QPointF(width() / 2.0, height() / 2.0);
            openRadialMenu(c);
        } else {
            m_lastPenTriggerMs = now;
            setTool(m_lastWriteTool);
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space) {
        clearPenCursor();
        m_spaceHeld = true;
        updateCursorShape();
        event->accept();
        return;
    }
    if (m_polylineActive && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        finishActivePolyline();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        cancelInteraction();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        cutSelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        pasteFromClipboard();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_D && event->modifiers().testFlag(Qt::ControlModifier)) {
        duplicateSelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Undo)) {
        undo();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        event->accept();
        return;
    }
    if (pageCount() > 1 && !hasLassoSelection() && !hasSelectedShape()
        && !hasSelectedText() && !hasSelectedImage()) {
        if (event->key() == Qt::Key_Left) {
            prevPage();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            nextPage();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        updateCursorShape();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CanvasWidget::focusOutEvent(QFocusEvent* event) {
    m_spaceHeld = false;
    if (m_radialOpen) {
        m_radialOpen = false;
        update(radialMenuRect());
    }
    updateCursorShape();
    QWidget::focusOutEvent(event);
}

void CanvasWidget::switchTabletTool(ToolType tool) {
    if (m_tool == tool)
        return;
    setTool(tool);
    emit toastRequested(tool == ToolType::Eraser      ? QStringLiteral("Borrador")
                      : tool == ToolType::Highlighter ? QStringLiteral("Resaltador")
                      : tool == ToolType::Ballpoint   ? QStringLiteral("Bolígrafo gel")
                                                      : QStringLiteral("Pluma fuente"));
}

void CanvasWidget::leaveEvent(QEvent* event) {
    clearPenCursor();
    clearEraserCursor();
    QWidget::leaveEvent(event);
}

void CanvasWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() && (event->mimeData()->hasImage() || event->mimeData()->hasUrls()))
        event->acceptProposedAction();
}

void CanvasWidget::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (!mime)
        return;
    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            const QString local = url.toLocalFile();
            if (local.isEmpty())
                continue;
            if (local.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
                importPdfFile(local);
            else
                importImageFile(local);
        }
        event->acceptProposedAction();
    } else if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            QGuiApplication::clipboard()->setImage(img);
            pasteFromClipboard();
            event->acceptProposedAction();
        }
    }
}

void CanvasWidget::updateCursorShape() {
    if (m_radialOpen) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (m_mode == InteractionMode::Pan) {
        setCursor(Qt::ClosedHandCursor);
    } else if (m_spaceHeld || m_tool == ToolType::Hand) {
        setCursor(Qt::OpenHandCursor);
    } else if (m_tool == ToolType::Eraser || m_mode == InteractionMode::Erase
               || m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint
               || m_tool == ToolType::Highlighter || m_tool == ToolType::Laser) {
        setCursor(Qt::BlankCursor);
    } else if (m_tool == ToolType::Select) {
        setCursor(Qt::ArrowCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

} // namespace finily
