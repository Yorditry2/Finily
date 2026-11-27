#include "CanvasWidget.h"
#include "CanvasWidgetInternal.h"
#include "core/ShapeProtocols.h"
#include "core/Utils.h"
#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"
#include "engine/LassoEngine.h"
#include "engine/LiveStroke.h"

#include <QFontMetricsF>
#include <QLineEdit>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace finily {
namespace {

StrokePoint inputPoint(QPointF p, double pressure, double tiltX = 0.0, double tiltY = 0.0) {
    return {
        p.x(),
        p.y(),
        std::clamp(pressure, 0.0, 1.0),
        nowMs(),
        std::clamp(tiltX, -60.0, 60.0),
        std::clamp(tiltY, -60.0, 60.0),
    };
}

StrokePoint snappedPoint(const StrokePoint& origin, StrokePoint point) {
    const double dx = point.x - origin.x;
    const double dy = point.y - origin.y;
    const double step = std::numbers::pi / 12.0;
    const double angle = std::round(std::atan2(dy, dx) / step) * step;
    const double dist = std::hypot(dx, dy);
    point.x = origin.x + dist * std::cos(angle);
    point.y = origin.y + dist * std::sin(angle);
    return point;
}

// Polígono regular inscrito: centro en `center`, radio hasta `end`; el primer vértice apunta al arrastre.
QVector<QPointF> regularPolygon(QPointF center, QPointF end, int sides) {
    const QPointF d = end - center;
    const double radius = std::hypot(d.x(), d.y());
    const double start = radius < 1e-6 ? -std::numbers::pi / 2.0 : std::atan2(d.y(), d.x());
    QVector<QPointF> out;
    out.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        const double a = start + 2.0 * std::numbers::pi * i / sides;
        out.append(center + QPointF(std::cos(a), std::sin(a)) * radius);
    }
    return out;
}

void createShapeVertices(ShapeItem& shape,
                         QPointF origin,
                         QPointF end,
                         int polygonSides = 6,
                         int quadVariant = 0) {
    const QRectF r = QRectF(origin, end).normalized();
    switch (shape.kind) {
    case ShapeKind::Polyline:
        if (quadVariant == 1) {
            shape.vertices = {
                {r.left() + r.width() * 0.22, r.top()},
                r.topRight(),
                {r.right() - r.width() * 0.22, r.bottom()},
                r.bottomLeft(),
            };
        } else if (quadVariant == 2) {
            shape.vertices = {
                {r.left() + r.width() * 0.20, r.top()},
                {r.right() - r.width() * 0.20, r.top()},
                r.bottomRight(),
                r.bottomLeft(),
            };
        } else if (quadVariant == 3) {
            shape.vertices = {
                r.topLeft(),
                {r.right() - r.width() * 0.28, r.top()},
                r.bottomRight(),
                r.bottomLeft(),
            };
        } else if (quadVariant == 4) {
            shape.vertices = {
                {r.left() + r.width() * 0.18, r.top()},
                {r.right() - r.width() * 0.08, r.top() + r.height() * 0.22},
                r.bottomRight(),
                {r.left(), r.bottom() - r.height() * 0.08},
            };
        } else {
            shape.vertices = regularPolygon(origin, end, polygonSides);
        }
        break;
    case ShapeKind::Line:
    case ShapeKind::Circle:
    case ShapeKind::Brace:
        shape.vertices = {origin, end};
        break;
    case ShapeKind::Ellipse:
    case ShapeKind::Rectangle:
    case ShapeKind::Axes:
    case ShapeKind::Solid3D:
        shape.vertices = {r.topLeft(), r.bottomRight()};
        break;
    case ShapeKind::Triangle:
        shape.vertices = {{r.center().x(), r.top()}, r.bottomRight(), r.bottomLeft()};
        break;
    case ShapeKind::Radical:
        shape.vertices = {
            {r.left() + r.width() * 0.22, r.bottom()},
            {r.left() + r.width() * 0.42, r.top()},
            r.topRight(),
        };
        break;
    case ShapeKind::RightAngle:
        shape.vertices = {{end.x(), origin.y()}, origin, {origin.x(), end.y()}};
        break;
    case ShapeKind::Curve: {
        const double lift = std::max(24.0, pointDistance(origin, end) * 0.25);
        shape.vertices = {origin, (origin + end) * 0.5 + QPointF(0.0, -lift), end};
        break;
    }
    case ShapeKind::Arc:
    case ShapeKind::Sector:
    case ShapeKind::AngleMarker: {
        const QPointF ray = end - origin;
        const double angle =
            (shape.kind == ShapeKind::AngleMarker) ? (std::numbers::pi / 4.0) : (std::numbers::pi / 2.0);
        const QPointF second(ray.x() * std::cos(angle) + ray.y() * std::sin(angle),
                             ray.y() * std::cos(angle) - ray.x() * std::sin(angle));
        shape.vertices = {origin, end, origin + second};
        break;
    }
    case ShapeKind::ShadedRegion:
        break;
    }
    shape.closed = shape.kind == ShapeKind::Circle || shape.kind == ShapeKind::Ellipse
        || shape.kind == ShapeKind::Rectangle || shape.kind == ShapeKind::Triangle
        || shape.kind == ShapeKind::Polyline || shape.kind == ShapeKind::Sector;
    shape.bounds = computeShapeBounds(shape);
}

bool drawsFromEndpoints(ShapeKind kind) {
    return kind == ShapeKind::Line || kind == ShapeKind::Curve || kind == ShapeKind::Brace;
}

std::optional<QPointF> handleAnchor(const ShapeItem& shape, int index) {
    const auto handles = getShapeHandles(shape);
    if (handles.size() == 4 && index >= 0 && index < 4
        && (shape.kind == ShapeKind::Rectangle || shape.kind == ShapeKind::Ellipse
            || shape.kind == ShapeKind::Axes || shape.kind == ShapeKind::Solid3D)) {
        return handles[(index + 2) % 4];
    }
    if (shape.kind == ShapeKind::Circle && index == 1 && !shape.vertices.isEmpty()) {
        return shape.vertices[0];
    }
    if (shape.kind == ShapeKind::Line && index >= 0 && index < 2 && shape.vertices.size() >= 2) {
        return shape.vertices[1 - index];
    }
    if (shape.kind == ShapeKind::Polyline && index > 0 && index < shape.vertices.size()) {
        return shape.vertices[index - 1];
    }
    if (shape.kind == ShapeKind::Radical && index == 2 && shape.vertices.size() >= 2) {
        return shape.vertices[1];
    }
    return std::nullopt;
}

bool sameHighlightGeometry(const ShapeItem& a, const ShapeItem& b) {
    if (a.kind != b.kind || a.vertices.size() != b.vertices.size()) {
        return false;
    }
    auto closePts = [](const QPointF& p, const QPointF& q) {
        return pointDistance(p, q) <= 0.5;
    };
    bool forwardMatch = true;
    for (qsizetype i = 0; i < a.vertices.size(); ++i) {
        if (!closePts(a.vertices.at(i), b.vertices.at(i))) {
            forwardMatch = false;
            break;
        }
    }
    if (forwardMatch) {
        return true;
    }
    if (a.kind == ShapeKind::Line && a.vertices.size() == 2) {
        return closePts(a.vertices.at(0), b.vertices.at(1))
            && closePts(a.vertices.at(1), b.vertices.at(0));
    }
    return false;
}

QString recognizedShapeLabel(ShapeKind kind) {
    switch (kind) {
    case ShapeKind::Line:
        return QStringLiteral("Línea");
    case ShapeKind::Circle:
        return QStringLiteral("Círculo");
    case ShapeKind::Ellipse:
        return QStringLiteral("Elipse");
    case ShapeKind::Triangle:
        return QStringLiteral("Triángulo");
    case ShapeKind::Rectangle:
        return QStringLiteral("Rectángulo");
    case ShapeKind::Radical:
        return QStringLiteral("Raíz √");
    case ShapeKind::Polyline:
        return QStringLiteral("Polígono");
    case ShapeKind::Curve:
        return QStringLiteral("Curva");
    case ShapeKind::RightAngle:
        return QStringLiteral("Ángulo recto");
    case ShapeKind::Brace:
        return QStringLiteral("Llave");
    case ShapeKind::Axes:
        return QStringLiteral("Ejes");
    case ShapeKind::Solid3D:
        return QStringLiteral("Sólido");
    case ShapeKind::Arc:
        return QStringLiteral("Arco");
    case ShapeKind::Sector:
        return QStringLiteral("Sector");
    case ShapeKind::AngleMarker:
        return QStringLiteral("Ángulo");
    case ShapeKind::ShadedRegion:
        return {};
    }
    return {};
}

} // namespace

ShapeItem* CanvasWidget::findShapeById(const QString& id) {
    if (id.isEmpty()) {
        return nullptr;
    }
    if (m_hasActiveShape && m_activeShape.id == id) {
        return &m_activeShape;
    }
    for (auto& shape : m_doc.shapes()) {
        if (shape.id == id) {
            return &shape;
        }
    }
    return nullptr;
}

const ShapeItem* CanvasWidget::findShapeById(const QString& id) const {
    if (id.isEmpty()) {
        return nullptr;
    }
    if (m_hasActiveShape && m_activeShape.id == id) {
        return &m_activeShape;
    }
    for (const auto& shape : m_doc.shapes()) {
        if (shape.id == id) {
            return &shape;
        }
    }
    return nullptr;
}

void CanvasWidget::cancelInteraction() {
    if (m_polylineActive) {
        cancelActivePolyline();
        return;
    }
    finishTextEditor(false);
    const DocumentSnapshot* snapshot = nullptr;
    switch (m_mode) {
    case InteractionMode::Erase:
        snapshot = &m_snapshotBeforeErase;
        break;
    case InteractionMode::ShapeMove:
    case InteractionMode::ShapeDragHandle:
    case InteractionMode::ShapeScale:
        snapshot = &m_snapshotBeforeShapeEdit;
        break;
    case InteractionMode::LassoMove:
    case InteractionMode::LassoScale:
    case InteractionMode::LassoRotate:
        snapshot = &m_snapshotBeforeLassoTransform;
        break;
    case InteractionMode::TextMove:
        snapshot = &m_snapshotBeforeTextEdit;
        break;
    case InteractionMode::ImageMove:
    case InteractionMode::ImageResize:
        snapshot = &m_snapshotBeforeImageEdit;
        break;
    default:
        break;
    }
    if (snapshot) {
        m_doc.restoreSnapshot(*snapshot);
    }
    const bool wasRadial = m_radialOpen;
    m_holdTimer->stop();
    m_radialOpen = false;
    m_holdEraseUpper = false;
    m_holdEraseLower = false;
    clearEraserCursor();
    m_liveInk.reset(m_liveInkLayer);
    m_lastLiveDirtyRect = {};
    m_hasActiveStroke = false;
    m_hasActiveShape = false;
    m_holdSnappedToShape = false;
    m_lassoPreviewDelta = QPointF();
    m_mode = InteractionMode::None;
    m_erasedAnything = false;
    m_activeHandleIndex = -1;
    m_rightClickOnSelection = false;
    m_activeGuides.clear();
    clearSelection();
    updateCursorShape();
    if (wasRadial)
        update(radialMenuRect());
}

void CanvasWidget::cancelActivePolyline() {
    m_polylineActive = false;
    m_lastPolylineTapMs = -1;
    m_polylineVertices.clear();
    m_polylineSnapClose = false;
    m_hasActiveShape = false;
    m_activeGuides.clear();
    m_selectedShapeId.clear();
    invalidateStaticCache();
    emit stateChanged();
}

void CanvasWidget::finishActivePolyline(bool closeShape) {
    if (!m_polylineActive) {
        return;
    }
    if (m_polylineVertices.size() < (closeShape ? 3 : 2)) {
        cancelActivePolyline();
        return;
    }
    ShapeItem shape = m_activeShape;
    shape.vertices = m_polylineVertices;
    shape.closed = closeShape;
    if (shape.vertices.size() > 2
        && pointDistance(shape.vertices.front(), shape.vertices.back()) < 0.001) {
        shape.vertices.removeLast();
    }
    shape.bounds = computeShapeBounds(shape);
    pushUndoSnapshot(currentSnapshot());
    m_doc.shapes().append(shape);
    attachNewShape(m_doc.shapes());
    m_polylineActive = false;
    m_lastPolylineTapMs = -1;
    m_polylineVertices.clear();
    m_polylineSnapClose = false;
    m_hasActiveShape = false;
    m_activeGuides.clear();
    m_selectedShapeId = shape.id;
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::updateLinkedPreview(const QVector<ShapeItem>& original, const QSet<QString>& edited) {
    const bool linked = updateShapeLinks(m_doc.shapes(), original, edited);
    if (linked || m_linkedPreview) {
        invalidateStaticCache();
    }
    m_linkedPreview = linked;
}

void CanvasWidget::openInlineTextEditor(const TextItem& draft,
                                        const QPointF& anchorScreen,
                                        const QString& existingTextId,
                                        const QSet<QString>& strokesToReplace) {
    if (m_textEditor) {
        finishTextEditor(true);
    }
    const bool isOcr = !strokesToReplace.isEmpty();
    auto* editor = new InlineTextEditor(this);
    m_textEditor = editor;
    editor->draft = draft;
    editor->origText = draft.text;
    editor->origFontSize = draft.fontSize;
    editor->origFamilyIndex = draft.fontFamilyIndex;
    editor->origBold = draft.bold;
    editor->origItalic = draft.italic;
    editor->setText(draft.text);
    if (isOcr) {
        editor->setPlaceholderText(QStringLiteral("Escribe o corrige el texto…"));
    }
    const double zoom = m_doc.viewport().zoom;
    editor->setFont(textFont(draft.fontSize * zoom, draft.fontFamilyIndex, draft.bold, draft.italic));
    const ThemeConfig theme = effectiveThemeConfig(m_doc.themeId(), m_doc.customBackground());
    const QString bgCss = theme.isDark
        ? QStringLiteral("rgba(12,18,32,0.88)")
        : QStringLiteral("rgba(255,255,255,0.92)");
    const QString style = isOcr
        ? QStringLiteral("QLineEdit { background: %1; color: %2; border: 1.5px solid %3; "
                         "border-radius: 6px; padding: 4px 6px; }")
        : QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                         "border-radius: 5px; padding: 3px; }");
    editor->setStyleSheet(style.arg(bgCss, draft.color.name(), theme.accent.name()));

    const int minW = isOcr ? 220 : 180;
    const int padW = isOcr ? 36 : 24;
    const int padH = isOcr ? 14 : 12;
    const QFontMetricsF metrics(editor->font());
    editor->move(anchorScreen.toPoint());
    editor->resize(std::max(minW, qRound(metrics.horizontalAdvance(draft.text) + padW)),
                   qRound(metrics.height() + padH));
    connect(editor, &QLineEdit::textChanged, editor, [this, editor, minW, padW](const QString& text) {
        const QFontMetricsF fm(editor->font());
        editor->resize(std::max(minW, qRound(fm.horizontalAdvance(text) + padW)), editor->height());
        emit inlineTextEditorChanged(true, editor->geometry());
    });

    editor->commit = [this, editor, existingTextId, strokesToReplace](const QString& text) {
        const TextItem draft = editor->draft;
        if (!strokesToReplace.isEmpty()) {
            const QString cleaned = text.trimmed();
            if (cleaned.isEmpty()) {
                return;
            }
            pushUndoSnapshot(currentSnapshot());
            m_doc.strokes().removeIf([&](const Stroke& s) {
                return strokesToReplace.contains(s.id);
            });
            TextItem updated = draft;
            updated.text = cleaned;
            updated.bounds = textBounds(updated);
            m_doc.texts().append(updated);
            clearSelection();
            m_selectedTextId = updated.id;
            invalidateStaticCache();
            saveAutosave();
            emit stateChanged();
            emit toastRequested(QStringLiteral("Texto: «%1» ✓").arg(cleaned.left(24)));
            return;
        }
        if (existingTextId.isEmpty()) {
            if (text.trimmed().isEmpty()) {
                return;
            }
            pushUndoSnapshot(currentSnapshot());
            TextItem updated = draft;
            updated.text = text;
            updated.bounds = textBounds(updated);
            m_doc.texts().append(updated);
            m_selectedTextId = updated.id;
            invalidateStaticCache();
            saveAutosave();
            emit stateChanged();
            return;
        }
        const bool unchanged = (text == editor->origText
            && draft.fontFamilyIndex == editor->origFamilyIndex
            && std::abs(draft.fontSize - editor->origFontSize) < 1e-4
            && draft.bold == editor->origBold
            && draft.italic == editor->origItalic);
        if (text.trimmed().isEmpty() || unchanged) {
            return;
        }
        auto& texts = m_doc.texts();
        qsizetype foundIdx = -1;
        for (qsizetype i = 0; i < texts.size(); ++i) {
            if (texts.at(i).id == existingTextId) {
                foundIdx = i;
                break;
            }
        }
        if (foundIdx < 0) {
            return;
        }
        pushUndoSnapshot(currentSnapshot());
        TextItem updated = draft;
        updated.id = existingTextId;
        updated.text = text;
        updated.bounds = textBounds(updated);
        m_doc.texts()[foundIdx] = updated;
        m_selectedTextId = existingTextId;
        invalidateStaticCache();
        saveAutosave();
        emit stateChanged();
    };
    editor->onClosed = [this] {
        emit inlineTextEditorChanged(false, QRect());
    };

    editor->show();
    editor->setFocus(Qt::MouseFocusReason);
    editor->selectAll();
    emit inlineTextEditorChanged(true, editor->geometry());
    invalidateStaticCache();
}

void CanvasWidget::beginTextEditAt(const QPointF& screen, const QPointF& world) {
    Q_UNUSED(screen);
    finishTextEditor(true);
    const auto& texts = m_doc.texts();
    qsizetype hitIdx = -1;
    for (qsizetype i = texts.size(); i-- > 0;) {
        if (texts.at(i).bounds.adjusted(-4.0, -4.0, 4.0, 4.0).contains(world)) {
            hitIdx = i;
            break;
        }
    }
    if (hitIdx < 0) {
        return;
    }
    const TextItem existing = texts.at(hitIdx);
    clearSelection();
    m_selectedTextId = existing.id;
    if (existing.locked) {
        invalidateStaticCache();
        emit stateChanged();
        return;
    }
    const Viewport& vp = m_doc.viewport();
    const QPointF anchorScreen = existing.pos * vp.zoom + QPointF(vp.x, vp.y);
    openInlineTextEditor(existing, anchorScreen, existing.id, {});
}

void CanvasWidget::startShapeEdit(const ShapeItem& shape, InteractionMode mode, int handle) {
    m_selectedShapeId = shape.id;
    if (shape.locked) {
        m_mode = InteractionMode::None;
        invalidateStaticCache();
        emit stateChanged();
        return;
    }
    m_snapshotBeforeShapeEdit = currentSnapshot();
    m_activeHandleIndex = handle;
    m_shapeDragOriginalVertices = shape.vertices;
    m_shapeDragOriginalKind = shape.kind;
    m_mode = mode;
    invalidateStaticCache();
    emit stateChanged();
}

void CanvasWidget::startImageEdit(const ImageItem& image, int handle) {
    m_selectedImageId = image.id;
    if (image.locked) {
        m_mode = InteractionMode::None;
        invalidateStaticCache();
        emit stateChanged();
        return;
    }
    m_mode = (handle >= 0) ? InteractionMode::ImageResize : InteractionMode::ImageMove;
    m_imageHandleIndex = handle;
    m_imageOriginalRect = image.rect;
    m_snapshotBeforeImageEdit = currentSnapshot();
    invalidateStaticCache();
    emit stateChanged();
}

void CanvasWidget::startSelectionTransform(int handle, const QPointF& world) {
    m_lassoHandleIndex = handle;
    m_lassoPreviewDelta = QPointF();
    m_snapshotBeforeLassoTransform = currentSnapshot();
    m_lassoDragStartWorld = world;
    m_mode = (handle == 4)  ? InteractionMode::LassoRotate
           : (handle >= 0)  ? InteractionMode::LassoScale
                            : InteractionMode::LassoMove;
    const QVector<QPointF> corners{
        m_lassoBounds.topLeft(),
        m_lassoBounds.topRight(),
        m_lassoBounds.bottomRight(),
        m_lassoBounds.bottomLeft(),
    };
    m_lassoTransformCenter =
        (handle >= 0 && handle < 4) ? corners.at((handle + 2) % 4) : m_lassoBounds.center();
    m_lassoStartAngle =
        std::atan2(world.y() - m_lassoTransformCenter.y(), world.x() - m_lassoTransformCenter.x());
}

void CanvasWidget::selectTogether(const QString& id, const QPointF& world) {
    if (!hasLassoSelection() || !selectedItemIds().contains(id)) {
        clearSelection();
        expandLassoSelection(id);
        invalidateStaticCache();
        emit stateChanged();
    }
    startSelectionTransform(-1, world);
}

void CanvasWidget::beginFillAt(const QPointF& world) {
    auto region = shadedRegionAt(world, m_doc.shapes(), m_color, m_shadePattern);
    if (!region) {
        emit toastRequested(tr("No hay una región cerrada en ese punto"));
        return;
    }
    auto& shapes = m_doc.shapes();
    int sameRegionCount = 0;
    qsizetype lastSameRegionIdx = -1;
    qsizetype exactMatchIdx = -1;
    for (qsizetype i = 0; i < shapes.size(); ++i) {
        const auto& shape = shapes.at(i);
        if (shape.kind == ShapeKind::ShadedRegion && shape.vertices == region->vertices
            && shape.shadeContourStarts == region->shadeContourStarts) {
            ++sameRegionCount;
            lastSameRegionIdx = i;
            if (shape.color == region->color && shape.shadePattern == m_shadePattern) {
                exactMatchIdx = i;
            }
        }
    }
    pushUndoSnapshot(currentSnapshot());
    if (exactMatchIdx >= 0) {
        // Tocar de nuevo con el mismo color y trama quita esa capa (toggle) y renumera las restantes.
        shapes.removeAt(exactMatchIdx);
        int nextLayer = 0;
        for (auto& shape : shapes) {
            if (shape.kind == ShapeKind::ShadedRegion && shape.vertices == region->vertices
                && shape.shadeContourStarts == region->shadeContourStarts) {
                shape.shadeLayer = nextLayer++;
            }
        }
    } else if (sameRegionCount >= 3 && lastSameRegionIdx >= 0) {
        // Máximo 3 capas superpuestas en una misma región.
        shapes[lastSameRegionIdx].color = region->color;
        shapes[lastSameRegionIdx].shadePattern = m_shadePattern;
    } else {
        // Añadir nueva capa encima desplazada/alternada para distinguir ambos colores.
        region->id = freshId();
        region->shadeLayer = sameRegionCount;
        shapes.append(*region);
    }
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
}

bool CanvasWidget::tryBeginSelectTool(const QPointF& world) {
    if (m_tool != ToolType::Select) {
        return false;
    }
    const double zoom = m_doc.viewport().zoom;
    if (const auto* selected = std::as_const(*this).findShapeById(m_selectedShapeId)) {
        const int vertex = hitTestHandle(*selected, world, 8.0 / zoom);
        if (vertex >= 0) {
            m_shapeDragStartWorld = world;
            startShapeEdit(*selected, InteractionMode::ShapeDragHandle, vertex);
            return true;
        }
        const int side = hitTestResizeHandle(*selected, world, zoom);
        if (side >= 0) {
            m_shapeDragStartWorld = world;
            startShapeEdit(*selected, InteractionMode::ShapeScale, side);
            return true;
        }
    }
    const auto& images = m_doc.images();
    for (const auto& image : images) {
        if (image.id != m_selectedImageId || image.isPdfPage) {
            continue;
        }
        const int handle = hitTestLassoHandle(image.rect, world, zoom);
        if (handle >= 0 && handle < 4) {
            m_imageDragStartWorld = world;
            startImageEdit(image, handle);
            return true;
        }
    }
    if (hasLassoSelection()) {
        const int handle = hitTestLassoHandle(m_lassoBounds, world, zoom);
        if (handle >= 0) {
            startSelectionTransform(handle, world);
            return true;
        }
    }

    // Textos por encima de formas, trazos e imágenes (salvo los tiradores ya activos).
    const auto& texts = m_doc.texts();
    for (qsizetype i = texts.size(); i-- > 0;) {
        if (texts.at(i).bounds.adjusted(-4.0, -4.0, 4.0, 4.0).contains(world)) {
            return false;
        }
    }

    const double tolerance = 8.0 / zoom;
    const auto& shapes = m_doc.shapes();
    const auto& strokes = m_doc.strokes();
    auto selectShape = [&](const ShapeItem& shape) {
        if (!shape.groupId.isEmpty() || !shape.shadeBoundary.isEmpty()) {
            selectTogether(shape.id, world);
            return;
        }
        if (m_selectedShapeId != shape.id) {
            clearSelection();
        }
        m_shapeDragStartWorld = world;
        startShapeEdit(shape, InteractionMode::ShapeMove);
    };

    for (auto it = shapes.crbegin(); it != shapes.crend(); ++it) {
        if (it->kind != ShapeKind::ShadedRegion && hitTestShape(*it, world, tolerance)) {
            selectShape(*it);
            return true;
        }
    }
    for (auto it = strokes.crbegin(); it != strokes.crend(); ++it) {
        if (!hitTestStroke(*it, world, tolerance)) {
            continue;
        }
        if (!it->groupId.isEmpty()) {
            selectTogether(it->id, world);
            return true;
        }
        if (!m_selectedStrokeIds.contains(it->id)) {
            clearSelection();
            m_selectedStrokeIds.insert(it->id);
            m_lassoBounds = computeSelectionBounds(strokes, shapes, m_selectedStrokeIds, {});
            invalidateStaticCache();
            emit stateChanged();
        }
        startSelectionTransform(-1, world);
        return true;
    }
    // Interior taps pick the topmost outline before shaded underlays.
    for (const bool shading : {false, true}) {
        for (auto it = shapes.crbegin(); it != shapes.crend(); ++it) {
            if ((it->kind == ShapeKind::ShadedRegion) == shading && containsShapeInterior(*it, world)) {
                selectShape(*it);
                return true;
            }
        }
    }
    for (auto it = images.crbegin(); it != images.crend(); ++it) {
        if (!it->isPdfPage && it->rect.contains(world)) {
            if (!it->groupId.isEmpty()) {
                selectTogether(it->id, world);
                return true;
            }
            clearSelection();
            m_imageDragStartWorld = world;
            startImageEdit(*it);
            return true;
        }
    }
    if (hasLassoSelection() && m_lassoBounds.contains(world)) {
        startSelectionTransform(-1, world);
        return true;
    }

    clearSelection();
    m_mode = InteractionMode::LassoDraw;
    m_lassoRectOrigin = world;
    m_lassoPolygon.append(world);
    update();
    return true;
}

bool CanvasWidget::tryBeginLassoTool(const QPointF& world) {
    if (m_tool != ToolType::Lasso) {
        return false;
    }
    const double zoom = m_doc.viewport().zoom;
    if (hasLassoSelection()) {
        const int handle = hitTestLassoHandle(m_lassoBounds, world, zoom);
        if (handle >= 0) {
            startSelectionTransform(handle, world);
            return true;
        }
    }
    if (m_lassoFilter.media) {
        const auto& texts = m_doc.texts();
        for (qsizetype i = texts.size(); i-- > 0;) {
            if (texts.at(i).bounds.adjusted(-4.0, -4.0, 4.0, 4.0).contains(world)) {
                return false;
            }
        }
        for (const auto& image : m_doc.images()) {
            if (image.id == m_selectedImageId && !image.isPdfPage) {
                const int handle = hitTestLassoHandle(image.rect, world, zoom);
                if ((handle >= 0 && handle < 4) || image.rect.contains(world)) {
                    m_imageDragStartWorld = world;
                    startImageEdit(image, (handle >= 0 && handle < 4) ? handle : -1);
                    return true;
                }
            }
        }
    }
    if (hasLassoSelection() && m_lassoBounds.contains(world)) {
        startSelectionTransform(-1, world);
        return true;
    }
    clearSelection();
    m_mode = InteractionMode::LassoDraw;
    m_lassoRectOrigin = world;
    m_lassoPolygon.append(world);
    update();
    return true;
}

bool CanvasWidget::tryBeginTextInput(const QPointF& screen, const QPointF& world, bool forceEdit) {
    if (m_tool != ToolType::Text && m_tool != ToolType::Lasso && m_tool != ToolType::Select) {
        return false;
    }
    const auto& texts = m_doc.texts();
    const TextItem* hitItem = nullptr;
    for (qsizetype i = texts.size(); i-- > 0;) {
        if (texts.at(i).bounds.adjusted(-4.0, -4.0, 4.0, 4.0).contains(world)) {
            hitItem = &texts.at(i);
            break;
        }
    }
    if (hitItem && !forceEdit) {
        if ((m_tool == ToolType::Select || m_tool == ToolType::Lasso) && !hitItem->groupId.isEmpty()) {
            selectTogether(hitItem->id, world);
            return true;
        }
        const QString hitId = hitItem->id;
        const bool locked = hitItem->locked;
        const QPointF origPos = hitItem->pos;
        clearSelection();
        m_selectedTextId = hitId;
        if (locked) {
            invalidateStaticCache();
            emit stateChanged();
            return true;
        }
        m_snapshotBeforeTextEdit = currentSnapshot();
        m_textOriginalPos = origPos;
        m_textDragStartWorld = world;
        m_mode = InteractionMode::TextMove;
        invalidateStaticCache();
        emit stateChanged();
        return true;
    }
    if (m_tool == ToolType::Text || (hitItem && forceEdit)) {
        if (hitItem) {
            beginTextEditAt(screen, world);
            return true;
        }
        clearSelection();
        TextItem draft;
        draft.id = freshId();
        draft.pos = world;
        draft.fontSize = std::clamp(m_size * 7.5, 16.0, 56.0);
        draft.color = m_color;
        openInlineTextEditor(draft, screen, {}, {});
        return true;
    }
    return false;
}

void CanvasWidget::beginPolylineTap(const QPointF& screen, const QPointF& world) {
    const double zoom = m_doc.viewport().zoom;
    const qint64 tapMs = nowMs();
    if (m_polylineActive && m_polylineVertices.size() >= 3
        && pointDistance(world, m_polylineVertices.front()) * zoom <= 14.0) {
        finishActivePolyline(true);
        return;
    }
    if (m_polylineActive && m_polylineVertices.size() >= 2 && m_lastPolylineTapMs >= 0
        && tapMs - m_lastPolylineTapMs <= 340
        && pointDistance(screen, m_lastPolylineTapScreen) <= 12.0
        && pointDistance(world, m_polylineVertices.back()) * zoom <= 12.0) {
        // Native accepted tablet events never produce mouseDoubleClickEvent.
        finishActivePolyline(false);
        return;
    }
    const auto anchor = m_polylineVertices.isEmpty()
        ? std::nullopt
        : std::optional<QPointF>(m_polylineVertices.back());
    const auto snap = snapPointWithGuides(world, anchor, m_doc.shapes(), {}, zoom, m_smartGuideOptions);
    if (!m_polylineActive) {
        clearSelection();
        m_polylineActive = true;
        m_polylineVertices.clear();
        m_activeShape = {};
        m_activeShape.id = freshId();
        m_activeShape.kind = ShapeKind::Polyline;
        m_activeShape.color = m_color;
        m_activeShape.size = m_size;
        m_activeShape.linePattern = m_linePattern;
        m_activeShape.lineEnding = m_lineEnding;
    }
    if (m_polylineVertices.isEmpty() || pointDistance(snap.point, m_polylineVertices.back()) > 0.1 / zoom) {
        m_polylineVertices.append(snap.point);
    }
    m_lastPolylineTapMs = tapMs;
    m_lastPolylineTapScreen = screen;
    m_polylineHoverWorld = snap.point;
    m_activeGuides = snap.guides;
    m_polylineSnapClose = false;
    m_activeShape.vertices = m_polylineVertices;
    m_activeShape.closed = false;
    m_activeShape.bounds = computeShapeBounds(m_activeShape);
    m_hasActiveShape = true;
    update();
    emit stateChanged();
}

void CanvasWidget::beginShapeInput(const QPointF& world) {
    const bool spring = isPhysicsSpringSpec(m_congruenceMarkSpec);
    if (!m_congruenceMarkSpec.isEmpty() && !spring) {
        auto candidate = m_doc.shapes();
        QString markId;
        if (stampOrCycleCongruenceMark(candidate, world, m_congruenceMarkSpec, m_color, m_size,
                                       m_doc.viewport().zoom, &markId)) {
            pushUndoSnapshot(currentSnapshot());
            m_doc.shapes() = std::move(candidate);
            clearSelection();
            if (!markId.isEmpty()) {
                if (const auto* placed = findShapeById(markId)) {
                    m_shapePulseAnimation->stop();
                    m_shapePulseShape = *placed;
                    m_shapePulseAlpha = 1.0;
                    m_shapePulseAnimation->start();
                }
            }
            invalidateStaticCache();
            saveAutosave();
            emit stateChanged();
            update();
        } else {
            emit toastRequested(QStringLiteral("Toca sobre el lado de una figura para imantar la marca"));
        }
        return;
    }

    clearSelection();
    m_snapshotBeforeShapeEdit = currentSnapshot();
    m_mode = InteractionMode::ShapeCreate;
    m_holdSnappedToShape = false;
    const ShapeKind activeKind = spring ? ShapeKind::Line : m_shapeKind;
    // Solo las líneas imantan su punto inicial a vértices cercanos; las figuras cerradas empiezan en el clic.
    const bool snapOrigin = (activeKind == ShapeKind::Line);
    const auto snap = snapOrigin
        ? snapPointWithGuides(world, std::nullopt, m_doc.shapes(), {}, m_doc.viewport().zoom,
                              m_smartGuideOptions)
        : SnapResult{world, {}, false};
    m_shapeDragStartWorld = world;
    m_shapeCreateOrigin = snap.point;
    m_activeGuides = snap.guides;
    m_activeShape = {};
    m_activeShape.id = freshId();
    m_activeShape.kind = activeKind;
    m_activeShape.color = m_color;
    m_activeShape.size = m_size;
    m_activeShape.linePattern = spring ? LinePattern::Solid : m_linePattern;
    m_activeShape.lineEnding = spring ? LineEnding::None : m_lineEnding;
    m_activeShape.solidKind = m_solidKind;
    if (spring) {
        m_activeShape.label = physicsSpringLabel();
    }
    createShapeVertices(m_activeShape, snap.point, snap.point, m_polygonSides, m_quadVariant);
    m_hasActiveShape = true;
    m_selectedShapeId = m_activeShape.id;
    update();
}

void CanvasWidget::beginFreehandStroke(const QPointF& world,
                                       double pressure,
                                       double tiltX,
                                       double tiltY,
                                       bool hardware,
                                       Qt::KeyboardModifiers mods) {
    Q_UNUSED(mods);
    if (m_beautifyTimer) {
        m_beautifyTimer->stop();
    }
    if (m_mathBeautifyEnabled && (m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint)
        && !m_pendingBeautifyIds.isEmpty() && !m_pendingBeautifyBox.isEmpty()) {
        const double refH = std::clamp(m_pendingBeautifyBox.height(), 18.0, 80.0);
        const bool jumpedRight = (world.x() - m_pendingBeautifyBox.right()) > std::max(14.0, refH * 0.42);
        const bool jumpedLeft = (m_pendingBeautifyBox.left() - world.x()) > std::max(18.0, refH * 0.60);
        const bool jumpedLine = std::abs(world.y() - m_pendingBeautifyBox.center().y()) > refH * 1.15;
        if (jumpedRight || jumpedLeft || jumpedLine) {
            flushPendingBeautify();
        }
    }
    clearSelection();
    m_mode = InteractionMode::Draw;
    m_holdSnappedToShape = false;
    m_stabilizerFilter->reset();
    m_stabilizerFilter->setStrength(0.0);
    m_liveInk.reset(m_liveInkLayer);
    m_lastLiveDirtyRect = {};
    const double zoom = m_doc.viewport().zoom;
    m_inkSmoother.reset(m_stabilizer > 0.0 ? (1.5 + 10.5 * m_stabilizer) / zoom : 0.0,
                        3 + qRound(5.0 * m_stabilizer));
    m_activeStroke = {};
    m_activeStroke.id = freshId();
    m_activeStroke.tool = m_tool;
    m_activeStroke.color = m_color;
    m_activeStroke.size = m_size;
    m_activeStroke.linePattern = LinePattern::Solid;
    m_activeStroke.pressureProfile = m_pressureProfile;
    m_activeStroke.pressureSensitivity = m_pressureSensitivity;
    m_activeStroke.hasHardwarePressure = hardware;
    m_activeStroke.tipStyle = (m_tool == ToolType::Highlighter)
        ? (m_hlRoundTip ? 1 : m_highlighterTip)
        : m_penTip;
    if (m_tool == ToolType::Highlighter) {
        m_activeStroke.opacity = m_highlighterOpacity;
    }
    m_rawStartPoint = inputPoint(world, pressure, tiltX, tiltY);
    m_activeStroke.points.append(m_stabilizerFilter->filter(m_rawStartPoint));
    m_inkSmoother.push(m_activeStroke);
    m_liveStrokeBounds =
        QRectF(m_activeStroke.points.front().x, m_activeStroke.points.front().y, 0.0, 0.0);
    m_liveInkPending = false;
    m_hasActiveStroke = true;
    m_showEraserCursor = false;
    m_holdAnchorWorld = world;
    m_holdLastWorld = world;
    if (m_holdToShapeEnabled
        && (m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint
            || (m_tool == ToolType::Highlighter && m_highlighterTip != 2 && !m_hlStraightenOnRelease))) {
        m_holdTimer->start();
    }
    if (m_activeStroke.tool == ToolType::Highlighter) {
        invalidateStaticCache();
    } else {
        update();
    }
}

void CanvasWidget::beginInput(const QPointF& screenPos,
                              double pressure,
                              double tiltX,
                              double tiltY,
                              bool isHardwarePen,
                              bool isEraserTip,
                              Qt::MouseButton button,
                              Qt::KeyboardModifiers mods) {
    if (m_radialOpen) {
        updateRadialHover(screenPos);
        commitRadialSelection(false);
        return;
    }
    if (m_mode != InteractionMode::None) {
        return;
    }
    if (m_zoomAnimation->state() == QAbstractAnimation::Running) {
        m_zoomAnimation->stop();
        invalidateStaticCache();
    }
    clearPenCursor();
    clearEraserCursor();
    setFocus(Qt::MouseFocusReason);
    finishTextEditor(true);
    m_rightClickOnSelection = false;
    if (m_polylineActive && button == Qt::RightButton) {
        finishActivePolyline();
        return;
    }
    const QPointF world = screenToWorld(screenPos);
    if ((isEraserTip || m_tool == ToolType::Eraser) && button == Qt::LeftButton) {
        if (m_polylineActive) {
            cancelActivePolyline();
        }
        m_activeGuides.clear();
        m_mode = InteractionMode::Erase;
        m_snapshotBeforeErase = currentSnapshot();
        m_erasedAnything = false;
        m_eraserWorldPos = world;
        m_showEraserCursor = true;
        applyEraseAt(world, mods.testFlag(Qt::ShiftModifier));
        updateCursorShape();
        return;
    }
    if (button == Qt::RightButton && !m_spaceHeld && hasSelection()) {
        m_rightClickOnSelection =
            selectionScreenRect().adjusted(-8.0, -8.0, 8.0, 8.0).contains(screenPos);
    }
    if (button == Qt::MiddleButton || button == Qt::RightButton || m_spaceHeld
        || m_tool == ToolType::Hand) {
        m_mode = InteractionMode::Pan;
        m_panStartScreen = screenPos;
        m_panStartViewport = {m_doc.viewport().x, m_doc.viewport().y};
        updateCursorShape();
        return;
    }
    if (button != Qt::LeftButton) {
        return;
    }

    m_activeGuides.clear();
    if (m_tool == ToolType::Laser) {
        m_mode = InteractionMode::LaserTrail;
        m_laserDotFading = false;
        if (m_laserMode == LaserMode::Dot) {
            m_laserTrail = {{world, nowMs(), true}};
        } else {
            m_laserTrail.append({world, nowMs(), true});
        }
        m_laserTimer->start();
        updateLaserDirtyRect();
        return;
    }
    if (m_tool == ToolType::Fill) {
        if (m_fillSegmentMode)
            highlightSegmentAt(world, m_color, 0);
        else
            beginFillAt(world);
        return;
    }

    if (m_tool == ToolType::Select && tryBeginSelectTool(world)) {
        return;
    }
    if (m_tool == ToolType::Lasso && tryBeginLassoTool(world)) {
        return;
    }
    if ((m_tool == ToolType::Text || m_tool == ToolType::Select
         || (m_tool == ToolType::Lasso && m_lassoFilter.media))
        && tryBeginTextInput(screenPos, world, false)) {
        return;
    }
    if (m_tool == ToolType::Select || m_tool == ToolType::Lasso || m_tool == ToolType::Text) {
        return;
    }

    const double zoom = m_doc.viewport().zoom;
    const bool drawingGeometry = (m_tool == ToolType::Polyline || m_tool == ToolType::Shape);
    const bool drawingLine =
        (m_tool == ToolType::Polyline || (m_tool == ToolType::Shape && m_shapeKind == ShapeKind::Line));
    const bool writing = (m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint);
    const bool editModifier = mods.testFlag(Qt::ShiftModifier);
    if (!m_polylineActive && !drawingLine && (writing || m_tool == ToolType::Shape || editModifier)) {
        if (auto* selected = findShapeById(m_selectedShapeId)) {
            const int handle = hitTestHandle(*selected, world, (writing ? 12.0 : 10.0) / zoom);
            if (handle >= 0) {
                m_shapeDragStartWorld = world;
                startShapeEdit(*selected, InteractionMode::ShapeDragHandle, handle);
                return;
            }
            if ((writing || m_tool == ToolType::Shape)
                && !selected->bounds
                        .adjusted(-12.0 / zoom, -12.0 / zoom, 12.0 / zoom, 12.0 / zoom)
                        .contains(world)) {
                clearSelection();
                if (writing) {
                    return;
                }
            }
        }
        if (drawingGeometry && editModifier) {
            auto& shapes = m_doc.shapes();
            for (qsizetype i = shapes.size(); i-- > 0;) {
                if (shapes.at(i).kind == ShapeKind::ShadedRegion) {
                    continue;
                }
                const int handle = hitTestHandle(shapes.at(i), world, 9.0 / zoom);
                if (handle >= 0) {
                    m_shapeDragStartWorld = world;
                    startShapeEdit(shapes.at(i), InteractionMode::ShapeDragHandle, handle);
                    return;
                }
                if (hitTestShape(shapes.at(i), world, 6.0 / zoom)) {
                    m_shapeDragStartWorld = world;
                    startShapeEdit(shapes.at(i), InteractionMode::ShapeMove);
                    return;
                }
            }
        }
    }

    if (m_tool == ToolType::Polyline) {
        beginPolylineTap(screenPos, world);
        return;
    }
    if (m_tool == ToolType::Shape) {
        beginShapeInput(world);
        return;
    }
    beginFreehandStroke(world, pressure, tiltX, tiltY, isHardwarePen, mods);
}

void CanvasWidget::moveText(const QPointF& world) {
    for (auto& item : m_doc.texts()) {
        if (item.id == m_selectedTextId) {
            item.pos = m_textOriginalPos + world - m_textDragStartWorld;
            item.bounds = textBounds(item);
        }
    }
    update();
}

void CanvasWidget::movePan(const QPointF& screen) {
    const QPointF delta = screen - m_panStartScreen;
    if (m_rightClickOnSelection) {
        if (std::hypot(delta.x(), delta.y()) < 4.0) {
            return;
        }
        m_rightClickOnSelection = false;
    }
    Viewport& vp = m_doc.viewport();
    vp.x = m_panStartViewport.x() + delta.x();
    vp.y = m_panStartViewport.y() + delta.y();
    clampFiniteViewport();
    invalidateStaticCache();
}

void CanvasWidget::moveErase(const QPointF& world, Qt::KeyboardModifiers mods) {
    const QPointF previous = m_eraserWorldPos;
    const int steps = std::max(
        1, int(std::ceil(pointDistance(world, previous) / std::max(0.5, m_eraserSize / 4.0))));
    const bool trimIntersections = mods.testFlag(Qt::ShiftModifier);
    for (int i = 1; i <= steps; ++i) {
        applyEraseAt(previous + (world - previous) * (double(i) / steps), trimIntersections);
    }
    updateEraserCursor(world, true);
}

void CanvasWidget::moveLassoDraw(const QPointF& world) {
    if (m_lassoRectMode || m_tool == ToolType::Select) {
        const QRectF r(m_lassoRectOrigin, world);
        m_lassoPolygon = QPolygonF{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    } else if (m_lassoPolygon.isEmpty()
               || pointDistance(world, m_lassoPolygon.back()) * m_doc.viewport().zoom > 1.0) {
        m_lassoPolygon.append(world);
    }
    update();
}

void CanvasWidget::moveLassoTransform(const QPointF& world) {
    auto& strokes = m_doc.strokes();
    auto& shapes = m_doc.shapes();
    auto& texts = m_doc.texts();
    auto& images = m_doc.images();
    strokes = m_snapshotBeforeLassoTransform.strokes;
    shapes = m_snapshotBeforeLassoTransform.shapes;

    if (m_mode == InteractionMode::LassoRotate) {
        const double angle = std::atan2(world.y() - m_lassoTransformCenter.y(),
                                        world.x() - m_lassoTransformCenter.x())
            - m_lassoStartAngle;
        rotateSelection(strokes, shapes, m_selectedStrokeIds, m_selectedShapeIds,
                        m_lassoTransformCenter, angle);
        if (!m_selectedTextIds.isEmpty() || !m_selectedImageIds.isEmpty()) {
            texts = m_snapshotBeforeLassoTransform.texts;
            images = m_snapshotBeforeLassoTransform.images;
            const double c = std::cos(angle);
            const double s = std::sin(angle);
            auto rotatePt = [&](const QPointF& pt) {
                const QPointF d = pt - m_lassoTransformCenter;
                return m_lassoTransformCenter + QPointF(d.x() * c - d.y() * s, d.x() * s + d.y() * c);
            };
            for (auto& text : texts) {
                if (!text.locked && m_selectedTextIds.contains(text.id)) {
                    const QPointF oldCenter = text.bounds.center();
                    const QPointF delta = rotatePt(oldCenter) - oldCenter;
                    text.pos += delta;
                    text.bounds.translate(delta);
                }
            }
            for (auto& image : images) {
                if (!image.locked && m_selectedImageIds.contains(image.id)) {
                    const QPointF oldCenter = image.rect.center();
                    image.rect.translate(rotatePt(oldCenter) - oldCenter);
                }
            }
        }
    } else if (m_lassoHandleIndex >= 0 && m_lassoHandleIndex < 4) {
        QRectF bounds = computeSelectionBounds(m_snapshotBeforeLassoTransform.strokes,
                                               m_snapshotBeforeLassoTransform.shapes,
                                               m_selectedStrokeIds,
                                               m_selectedShapeIds);
        auto addRect = [&](const QRectF& r) {
            bounds = bounds.isNull() ? r : bounds.united(r);
        };
        for (const auto& text : m_snapshotBeforeLassoTransform.texts) {
            if (m_selectedTextIds.contains(text.id)) {
                addRect(text.bounds);
            }
        }
        for (const auto& image : m_snapshotBeforeLassoTransform.images) {
            if (m_selectedImageIds.contains(image.id)) {
                addRect(image.rect);
            }
        }
        const QVector<QPointF> corners{
            bounds.topLeft(),
            bounds.topRight(),
            bounds.bottomRight(),
            bounds.bottomLeft(),
        };
        const QPointF diagonal = corners.at(m_lassoHandleIndex) - m_lassoTransformCenter;
        const QPointF target = diagonal + world - m_lassoDragStartWorld;
        const double denominator = diagonal.x() * diagonal.x() + diagonal.y() * diagonal.y();
        const double scale = std::clamp(
            denominator > 1e-10
                ? (diagonal.x() * target.x() + diagonal.y() * target.y()) / denominator
                : 1.0,
            0.15,
            12.0);
        scaleSelection(strokes, shapes, m_selectedStrokeIds, m_selectedShapeIds,
                       m_lassoTransformCenter, scale, scale);
        if (!m_selectedTextIds.isEmpty() || !m_selectedImageIds.isEmpty()) {
            texts = m_snapshotBeforeLassoTransform.texts;
            images = m_snapshotBeforeLassoTransform.images;
            for (auto& text : texts) {
                if (!text.locked && m_selectedTextIds.contains(text.id)) {
                    const QPointF oldCenter = text.bounds.center();
                    const QPointF newCenter =
                        m_lassoTransformCenter + (oldCenter - m_lassoTransformCenter) * scale;
                    text.fontSize = std::clamp(text.fontSize * scale, 6.0, 240.0);
                    text.pos = m_lassoTransformCenter + (text.pos - m_lassoTransformCenter) * scale;
                    text.bounds = textBounds(text);
                    const QPointF drift = newCenter - text.bounds.center();
                    text.pos += drift;
                    text.bounds.translate(drift);
                }
            }
            for (auto& image : images) {
                if (!image.locked && m_selectedImageIds.contains(image.id)) {
                    const QPointF tl =
                        m_lassoTransformCenter + (image.rect.topLeft() - m_lassoTransformCenter) * scale;
                    const QPointF br =
                        m_lassoTransformCenter + (image.rect.bottomRight() - m_lassoTransformCenter) * scale;
                    image.rect = QRectF(tl, br).normalized();
                }
            }
        }
    }

    if (!m_selectedShapeIds.isEmpty()) {
        updateLinkedPreview(m_snapshotBeforeLassoTransform.shapes, m_selectedShapeIds);
    }
    m_lassoBounds = lassoSelectionBounds();
    update();
}

void CanvasWidget::moveImageTransform(const QPointF& world) {
    auto& images = m_doc.images();
    images = m_snapshotBeforeImageEdit.images;
    for (auto& image : images) {
        if (image.id != m_selectedImageId || image.isPdfPage) {
            continue;
        }
        if (m_mode == InteractionMode::ImageMove) {
            image.rect = m_imageOriginalRect.translated(world - m_imageDragStartWorld);
        } else if (m_imageHandleIndex >= 0 && m_imageHandleIndex < 4) {
            const QVector<QPointF> corners{
                m_imageOriginalRect.topLeft(),
                m_imageOriginalRect.topRight(),
                m_imageOriginalRect.bottomRight(),
                m_imageOriginalRect.bottomLeft(),
            };
            const QPointF anchor = corners.at((m_imageHandleIndex + 2) % 4);
            const QPointF diagonal = corners.at(m_imageHandleIndex) - anchor;
            const QPointF target = diagonal + world - m_imageDragStartWorld;
            const double denominator = diagonal.x() * diagonal.x() + diagonal.y() * diagonal.y();
            const double scale = std::clamp(
                denominator > 1e-10
                    ? (diagonal.x() * target.x() + diagonal.y() * target.y()) / denominator
                    : 1.0,
                0.05,
                20.0);
            image.rect = QRectF(anchor, anchor + diagonal * scale).normalized();
        }
    }
    update();
}

void CanvasWidget::moveShapeEdit(const QPointF& world) {
    const double zoom = m_doc.viewport().zoom;
    auto& shapes = m_doc.shapes();
    shapes = m_snapshotBeforeShapeEdit.shapes;
    if (auto* shape = findShapeById(m_selectedShapeId)) {
        shape->vertices = m_shapeDragOriginalVertices;
        shape->kind = m_shapeDragOriginalKind;
        const QPointF delta = world - m_shapeDragStartWorld;
        if (m_tool == ToolType::Select && std::hypot(delta.x(), delta.y()) * zoom <= 0.5) {
            m_activeGuides.clear();
            shape->bounds = computeShapeBounds(*shape);
        } else if (m_mode == InteractionMode::ShapeScale) {
            const QRectF box = shapeGeometryBounds(*shape);
            const bool horizontal = m_activeHandleIndex < 2;
            const bool towardStart = (m_activeHandleIndex % 2) == 0;
            const QPointF anchor = horizontal
                ? QPointF(towardStart ? box.right() : box.left(), box.center().y())
                : QPointF(box.center().x(), towardStart ? box.bottom() : box.top());
            const double oldSize = horizontal ? box.width() : box.height();
            if (oldSize < 1e-6) {
                return;
            }
            const double change = (towardStart ? -1.0 : 1.0) * (horizontal ? delta.x() : delta.y());
            const double scale = std::clamp(1.0 + change / oldSize, 0.05, 20.0);
            scaleShape(*shape, anchor, horizontal ? scale : 1.0, horizontal ? 1.0 : scale);
            const QRectF next = shapeGeometryBounds(*shape);
            const double guideMargin = 12.0 / zoom;
            if (horizontal) {
                const double x = towardStart ? next.left() : next.right();
                m_activeGuides = {{
                    QPointF(x, next.top() - guideMargin),
                    QPointF(x, next.bottom() + guideMargin),
                    {},
                    false,
                }};
            } else {
                const double y = towardStart ? next.top() : next.bottom();
                m_activeGuides = {{
                    QPointF(next.left() - guideMargin, y),
                    QPointF(next.right() + guideMargin, y),
                    {},
                    false,
                }};
            }
        } else if (m_mode == InteractionMode::ShapeDragHandle) {
            const auto handles = getShapeHandles(*shape);
            const QPointF raw = (m_tool == ToolType::Select && m_activeHandleIndex >= 0
                                 && m_activeHandleIndex < handles.size())
                ? (handles.at(m_activeHandleIndex) + delta)
                : world;
            if (isCongruenceMarkShape(*shape)
                && constrainCongruenceMarkToEdge(*shape, shapes, m_activeHandleIndex, raw, delta)) {
                m_activeGuides.clear();
            } else {
                const auto anchor = handleAnchor(*shape, m_activeHandleIndex);
                const auto external =
                    snapPointWithGuides(raw, anchor, shapes, shape->id, zoom, m_smartGuideOptions);
                const auto result = snapShapeVertex(*shape, m_activeHandleIndex, external, raw, zoom,
                                                    m_smartGuideOptions);
                m_activeGuides = result.guides;
                applyHandleDrag(*shape, m_activeHandleIndex, result.point);
            }
        } else {
            if (isCongruenceMarkShape(*shape)
                && constrainCongruenceMarkToEdge(*shape, shapes, -1, world, delta)) {
                m_activeGuides.clear();
            } else if (!m_shapeDragOriginalVertices.isEmpty()) {
                const QPointF origin = m_shapeDragOriginalVertices.front();
                const auto snapped =
                    snapPointWithGuides(origin + delta, std::nullopt, shapes, shape->id, zoom,
                                        m_smartGuideOptions);
                m_activeGuides = snapped.guides;
                translateShape(*shape, snapped.point - origin);
            }
        }
    }
    updateLinkedPreview(m_snapshotBeforeShapeEdit.shapes, {m_selectedShapeId});
    update();
}

void CanvasWidget::moveShapeCreate(const QPointF& world, Qt::KeyboardModifiers mods) {
    if (!m_hasActiveShape) {
        return;
    }
    const double zoom = m_doc.viewport().zoom;
    const auto& shapes = m_doc.shapes();
    if (m_holdSnappedToShape) {
        if (pointDistance(world, m_shapeDragStartWorld) * zoom <= 1.0) {
            m_activeShape.vertices = m_shapeDragOriginalVertices;
            m_activeShape.bounds = computeShapeBounds(m_activeShape);
            update();
            return;
        }
        m_activeShape.vertices = m_shapeDragOriginalVertices;
        const auto anchor = handleAnchor(m_activeShape, m_activeHandleIndex);
        const auto external =
            snapPointWithGuides(world, anchor, shapes, m_activeShape.id, zoom, m_smartGuideOptions);
        const auto result = m_smartGuideOptions.enabled
            ? snapShapeVertex(m_activeShape, m_activeHandleIndex, external, world, zoom,
                              m_smartGuideOptions)
            : external;
        m_activeGuides = result.guides;
        applyHandleDrag(m_activeShape, m_activeHandleIndex, result.point);
        update();
        return;
    }

    // Only snap angles for Line shapes (not Rectangles, Circles, Ellipses, Triangles!).
    const bool allowAngleSnap = (m_activeShape.kind == ShapeKind::Line);
    const auto anchor = allowAngleSnap ? std::optional<QPointF>(m_shapeCreateOrigin) : std::nullopt;
    const auto snapResult =
        snapPointWithGuides(world, anchor, shapes, m_activeShape.id, zoom, m_smartGuideOptions);
    m_activeGuides = snapResult.guides;
    const QPointF end = snapResult.point;
    QPointF start = m_shapeCreateOrigin;
    if (drawsFromEndpoints(m_activeShape.kind)
        && pointDistance(world, m_shapeDragStartWorld) * zoom > 3.0) {
        const auto reverse =
            snapPointWithGuides(m_shapeDragStartWorld, end, shapes, m_activeShape.id, zoom, false,
                                m_smartGuideOptions);
        if (reverse.contact) {
            start = reverse.point;
        }
    }
    QPointF finalEnd = end;
    // Mayús: óvalo -> círculo perfecto, rectángulo -> cuadrado perfecto.
    if (mods.testFlag(Qt::ShiftModifier)
        && (m_activeShape.kind == ShapeKind::Ellipse || m_activeShape.kind == ShapeKind::Rectangle)) {
        const QPointF d = end - start;
        const double side = std::max(std::abs(d.x()), std::abs(d.y()));
        finalEnd = start + QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
    }
    createShapeVertices(m_activeShape, start, finalEnd, m_polygonSides, m_quadVariant);
    if (m_activeShape.kind == ShapeKind::Triangle) {
        const QRectF r = QRectF(start, finalEnd).normalized();
        const int variant =
            (mods.testFlag(Qt::ShiftModifier) && m_triangleVariant == 0) ? 1 : m_triangleVariant;
        if (variant == 1) {
            m_activeShape.vertices = {r.topLeft(), r.bottomRight(), r.bottomLeft()};
            m_activeShape.bounds = computeShapeBounds(m_activeShape);
        } else if (variant == 3) {
            const double h = r.width() * std::sqrt(3.0) / 2.0;
            m_activeShape.vertices = {{r.center().x(), r.bottom() - h}, r.bottomRight(), r.bottomLeft()};
            m_activeShape.bounds = computeShapeBounds(m_activeShape);
        }
    }
    update();
}

void CanvasWidget::moveFreehandStroke(const QPointF& world,
                                      double pressure,
                                      double tiltX,
                                      double tiltY,
                                      Qt::KeyboardModifiers mods) {
    if (!m_hasActiveStroke) {
        return;
    }
    const Viewport& vp = m_doc.viewport();
    m_holdLastWorld = world;
    if (m_holdToShapeEnabled && !m_holdSnappedToShape
        && (m_activeStroke.tool == ToolType::Pen || m_activeStroke.tool == ToolType::Ballpoint
            || (m_activeStroke.tool == ToolType::Highlighter && m_activeStroke.tipStyle != 2))
        && pointDistance(world, m_holdAnchorWorld) * vp.zoom > 4.0) {
        m_holdAnchorWorld = world;
        m_holdTimer->start();
    }
    StrokePoint point = m_stabilizerFilter->filter(inputPoint(world, pressure, tiltX, tiltY));
    if (mods.testFlag(Qt::ShiftModifier) || m_holdSnappedToShape) {
        m_liveInk.reset(m_liveInkLayer);
        m_liveInkPending = false;
        point = snappedPoint(m_rawStartPoint, point);
        m_activeStroke.points = {m_rawStartPoint, point};
    } else if (m_activeStroke.tool == ToolType::Highlighter && m_activeStroke.tipStyle == 2) {
        m_activeStroke.points = {m_rawStartPoint, point};
    } else if (!m_activeStroke.points.isEmpty()) {
        const auto& last = m_activeStroke.points.back();
        if (pointDistance({point.x, point.y}, {last.x, last.y}) <= 0.001
            && std::abs(point.pressure - last.pressure) <= 0.001
            && std::abs(point.tiltX - last.tiltX) <= 0.01
            && std::abs(point.tiltY - last.tiltY) <= 0.01) {
            return;
        }
        m_activeStroke.points.append(point);
        m_inkSmoother.push(m_activeStroke);
        // Qt agrupa los update(): prepara la capa de tinta una vez al pintar.
        m_liveInkPending = true;
    }
    // Conservar también la zona del trazo anterior al alternar Shift.
    m_liveStrokeBounds.setCoords(std::min(m_liveStrokeBounds.left(), point.x),
                                std::min(m_liveStrokeBounds.top(), point.y),
                                std::max(m_liveStrokeBounds.right(), point.x),
                                std::max(m_liveStrokeBounds.bottom(), point.y));

    const double dpr = devicePixelRatioF();
    const QSize pixels(int(std::ceil(width() * dpr)), int(std::ceil(height() * dpr)));
    QRectF tailWorld = m_liveStrokeBounds;
    if (m_liveInk.usable(vp, pixels, dpr) && LiveInkRenderer::eligible(m_activeStroke)
        && !mods.testFlag(Qt::ShiftModifier) && !m_holdSnappedToShape) {
        const qsizetype fromPt = std::max<qsizetype>(0, m_liveInk.frozenPointIndex() - 2);
        const auto& pts = m_activeStroke.points;
        double l = pts[fromPt].x;
        double r = l;
        double t = pts[fromPt].y;
        double b = t;
        for (qsizetype i = fromPt + 1; i < pts.size(); ++i) {
            l = std::min(l, pts[i].x);
            r = std::max(r, pts[i].x);
            t = std::min(t, pts[i].y);
            b = std::max(b, pts[i].y);
        }
        tailWorld.setCoords(l, t, r, b);
    }

    const QPointF offset(vp.x, vp.y);
    const QRectF screenBounds(tailWorld.topLeft() * vp.zoom + offset,
                              tailWorld.bottomRight() * vp.zoom + offset);
    // Catmull-Rom + predicción de punta pueden exceder los puntos; unir con el rect previo limpia residuos.
    const double pad = m_activeStroke.size * vp.zoom + 24.0;
    const double padX = screenBounds.width() / 6.0 + pad + 24.0;
    const double padY = screenBounds.height() / 6.0 + pad + 24.0;
    const QRect nextDirty = screenBounds.adjusted(-padX, -padY, padX, padY)
                                .toAlignedRect()
                                .intersected(rect());
    const QRect dirty = m_lastLiveDirtyRect.isEmpty() ? nextDirty : m_lastLiveDirtyRect.united(nextDirty);
    m_lastLiveDirtyRect = nextDirty;
    if (!dirty.isEmpty())
        update(dirty);
}

void CanvasWidget::updateHoverCursors(const QPointF& world, bool eraser) {
    const double zoom = m_doc.viewport().zoom;
    if (m_polylineActive) {
        m_polylineSnapClose = (m_polylineVertices.size() >= 3
                               && pointDistance(world, m_polylineVertices.front()) * zoom <= 14.0);
        if (m_polylineSnapClose) {
            m_polylineHoverWorld = m_polylineVertices.front();
            m_activeGuides.clear();
        } else {
            const auto anchor = m_polylineVertices.isEmpty()
                ? std::nullopt
                : std::optional<QPointF>(m_polylineVertices.back());
            const auto snap = snapPointWithGuides(world, anchor, m_doc.shapes(), m_activeShape.id, zoom,
                                                  m_smartGuideOptions);
            m_polylineHoverWorld = snap.point;
            m_activeGuides = snap.guides;
        }
        m_activeShape.vertices = m_polylineVertices;
        if (m_activeShape.vertices.isEmpty()
            || pointDistance(m_activeShape.vertices.back(), m_polylineHoverWorld) > 0.001) {
            m_activeShape.vertices.append(m_polylineHoverWorld);
        }
        m_activeShape.closed = false;
        m_activeShape.bounds = computeShapeBounds(m_activeShape);
        update();
        return;
    }
    if (m_tool == ToolType::Select && !m_spaceHeld && !eraser) {
        const auto* shape = std::as_const(*this).findShapeById(m_selectedShapeId);
        const int side = shape ? hitTestResizeHandle(*shape, world, zoom) : -1;
        const Qt::CursorShape nextCursor =
            (side < 0) ? Qt::ArrowCursor : (side < 2) ? Qt::SizeHorCursor : Qt::SizeVerCursor;
        if (cursor().shape() != nextCursor) {
            setCursor(nextCursor);
        }
    }
    const bool showPen = !eraser && !m_spaceHeld
        && (m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint
            || m_tool == ToolType::Highlighter || m_tool == ToolType::Laser);
    updatePenCursor(world, showPen);
    updateEraserCursor(world, eraser || (m_tool == ToolType::Eraser));
}

void CanvasWidget::moveInput(const QPointF& screenPos,
                             double pressure,
                             double tiltX,
                             double tiltY,
                             bool isHardwarePen,
                             bool isEraserTip,
                             Qt::KeyboardModifiers mods) {
    Q_UNUSED(isHardwarePen);
    const QPointF world = screenToWorld(screenPos);
    switch (m_mode) {
    case InteractionMode::LaserTrail:
        m_laserDotFading = false;
        if (m_laserMode == LaserMode::Dot) {
            m_laserTrail = {{world, nowMs(), true}};
        } else {
            m_laserTrail.append({world, nowMs(), false});
        }
        if (!m_laserTimer->isActive()) {
            m_laserTimer->start();
        }
        updateLaserDirtyRect();
        return;
    case InteractionMode::TextMove:
        moveText(world);
        return;
    case InteractionMode::Pan:
        movePan(screenPos);
        return;
    case InteractionMode::Erase:
        moveErase(world, mods);
        return;
    case InteractionMode::LassoDraw:
        moveLassoDraw(world);
        return;
    case InteractionMode::LassoMove:
        m_lassoPreviewDelta = world - m_lassoDragStartWorld;
        update();
        return;
    case InteractionMode::LassoScale:
    case InteractionMode::LassoRotate:
        moveLassoTransform(world);
        return;
    case InteractionMode::ImageMove:
    case InteractionMode::ImageResize:
        moveImageTransform(world);
        return;
    case InteractionMode::ShapeMove:
    case InteractionMode::ShapeDragHandle:
    case InteractionMode::ShapeScale:
        moveShapeEdit(world);
        return;
    case InteractionMode::ShapeCreate:
        moveShapeCreate(world, mods);
        return;
    case InteractionMode::Draw:
        moveFreehandStroke(world, pressure, tiltX, tiltY, mods);
        return;
    case InteractionMode::None:
        updateHoverCursors(world, isEraserTip);
        return;
    }
}

void CanvasWidget::finishAutoGeometry(const ShapeItem& pulseShape) {
    m_hasActiveStroke = false;
    m_liveInk.reset(m_liveInkLayer);
    m_lastLiveDirtyRect = {};
    m_holdTimer->stop();
    m_shapePulseAnimation->stop();
    m_shapePulseShape = pulseShape;
    m_shapePulseAlpha = 1.0;
    m_shapePulseAnimation->start();
    invalidateStaticCache();
    m_mode = InteractionMode::None;
    m_linkedPreview = false;
    m_activeGuides.clear();
    m_activeHandleIndex = -1;
    updateCursorShape();
    saveAutosave();
    emit stateChanged();
    update();
}

bool CanvasWidget::tryCommitFreehandGeometry(Qt::KeyboardModifiers mods) {
    if (mods.testFlag(Qt::ShiftModifier) || m_holdSnappedToShape) {
        return false;
    }
    if (m_activeStroke.tool != ToolType::Pen && m_activeStroke.tool != ToolType::Ballpoint) {
        return false;
    }
    const double zoom = m_doc.viewport().zoom;
    auto& shapes = m_doc.shapes();

    if (m_gestureOptions.angleArc || m_gestureOptions.rightAngle) {
        if (auto autoAngle = trySnapAngleArcAtVertex(m_activeStroke, shapes, zoom,
                                                     m_gestureOptions.angleArc,
                                                     m_gestureOptions.rightAngle)) {
            if (m_showAutoMeasurements && isAutoVertexAngle(*autoAngle)) {
                autoAngle->label.clear();
                autoAngle->bounds = computeShapeBounds(*autoAngle);
            }
            pushUndoSnapshot(currentSnapshot());
            shapes.append(*autoAngle);
            attachNewShape(shapes);
            finishAutoGeometry(*autoAngle);
            return true;
        }
    }

    if (m_gestureOptions.dividerDot) {
        auto candidate = shapes;
        QString createdId;
        if (trySnapFreehandDividerOnEdge(m_activeStroke, candidate, zoom, &createdId)) {
            pushUndoSnapshot(currentSnapshot());
            shapes = std::move(candidate);
            ShapeItem pulse;
            if (const auto* placed = findShapeById(createdId)) {
                pulse = *placed;
            }
            emit toastRequested(edgeMarkToast(EdgeMarkKind::Divider));
            finishAutoGeometry(pulse);
            return true;
        }
    }

    if (m_gestureOptions.attachArrowhead) {
        auto candidate = shapes;
        QString updatedId;
        if (tryAttachArrowheadToLine(m_activeStroke, candidate, zoom, &updatedId)) {
            pushUndoSnapshot(currentSnapshot());
            shapes = std::move(candidate);
            ShapeItem pulse;
            if (const auto* updated = findShapeById(updatedId)) {
                pulse = *updated;
            }
            emit toastRequested(QStringLiteral("Flecha ✓"));
            finishAutoGeometry(pulse);
            return true;
        }
    }

    if (m_gestureOptions.freehandArrow) {
        if (const auto arrow = tryRecognizeFreehandArrow(m_activeStroke, zoom)) {
            pushUndoSnapshot(currentSnapshot());
            shapes.append(*arrow);
            attachNewShape(shapes);
            emit toastRequested(QStringLiteral("Flecha ✓"));
            finishAutoGeometry(*arrow);
            return true;
        }
    }

    if (m_gestureOptions.edgeMarks) {
        if (const auto mark = trySnapCongruenceStrokeOnEdge(m_activeStroke, shapes, zoom, false)) {
            pushUndoSnapshot(currentSnapshot());
            shapes.append(*mark);
            emit toastRequested(edgeMarkToast(parseEdgeMark(mark->label)));
            finishAutoGeometry(*mark);
            return true;
        }
    }

    return false;
}

void CanvasWidget::endInput(const QPointF& screenPos, Qt::KeyboardModifiers mods) {
    m_holdTimer->stop();
    if (m_mode == InteractionMode::None) {
        return;
    }
    if (m_mode == InteractionMode::LaserTrail) {
        m_mode = InteractionMode::None;
        if (m_laserMode == LaserMode::Dot && !m_laserTrail.isEmpty()) {
            m_laserDotFading = true;
            m_laserDotReleaseMs = nowMs();
            m_laserTimer->start();
        }
        updateLaserDirtyRect();
        return;
    }

    bool documentChanged = false;
    bool skipFullRepaint = false;
    const double zoom = m_doc.viewport().zoom;
    const QPointF world = screenToWorld(screenPos);

    switch (m_mode) {
    case InteractionMode::None:
    case InteractionMode::LaserTrail:
        break;
    case InteractionMode::Pan:
        break;
    case InteractionMode::TextMove:
        moveText(world);
        if (pointDistance(world, m_textDragStartWorld) * zoom > 0.01) {
            pushUndoSnapshot(m_snapshotBeforeTextEdit);
            documentChanged = true;
        }
        invalidateStaticCache();
        break;
    case InteractionMode::LassoDraw: {
        if (m_lassoPolygon.isEmpty() || pointDistance(world, m_lassoPolygon.back()) > 0.001) {
            m_lassoPolygon.append(world);
        }
        const auto& strokes = m_doc.strokes();
        const auto& shapes = m_doc.shapes();
        const auto& texts = m_doc.texts();
        const auto& images = m_doc.images();
        const LassoTargetFilter filter =
            (m_tool == ToolType::Lasso) ? m_lassoFilter : LassoTargetFilter{};
        selectByLasso(m_lassoPolygon, strokes, shapes, m_selectedStrokeIds, m_selectedShapeIds, filter);
        if (filter.media && m_lassoPolygon.size() >= 3) {
            for (const auto& text : texts) {
                if (m_lassoPolygon.containsPoint(text.bounds.center(), Qt::OddEvenFill)) {
                    m_selectedTextIds.insert(text.id);
                }
            }
            for (const auto& image : images) {
                if (!image.isPdfPage && m_lassoPolygon.containsPoint(image.rect.center(), Qt::OddEvenFill)) {
                    m_selectedImageIds.insert(image.id);
                }
            }
        }
        expandLassoSelection();
        if (!hasLassoSelection()) {
            const double tapTol = 8.0 / zoom;
            if (filter.shapes) {
                for (auto it = shapes.crbegin(); it != shapes.crend(); ++it) {
                    if (it->kind != ShapeKind::ShadedRegion && hitTestShape(*it, world, tapTol)) {
                        m_selectedShapeId = it->id;
                        break;
                    }
                }
            }
            if (filter.media && m_selectedShapeId.isEmpty()) {
                for (qsizetype i = images.size(); i-- > 0;) {
                    if (!images.at(i).isPdfPage && images.at(i).rect.contains(world)) {
                        m_selectedImageId = images.at(i).id;
                        break;
                    }
                }
            }
        }
        m_lassoPolygon.clear();
        invalidateStaticCache();
        break;
    }
    case InteractionMode::LassoMove: {
        const QPointF delta = world - m_lassoDragStartWorld;
        m_lassoPreviewDelta = QPointF();
        if (pointDistance(world, m_lassoDragStartWorld) * zoom > 0.01) {
            auto& strokes = m_doc.strokes();
            auto& shapes = m_doc.shapes();
            auto& texts = m_doc.texts();
            auto& images = m_doc.images();
            translateSelection(strokes, shapes, m_selectedStrokeIds, m_selectedShapeIds, delta);
            for (auto& text : texts) {
                if (!text.locked && m_selectedTextIds.contains(text.id)) {
                    text.pos += delta;
                    text.bounds.translate(delta);
                }
            }
            for (auto& image : images) {
                if (!image.locked && m_selectedImageIds.contains(image.id)) {
                    image.rect.translate(delta);
                }
            }
            if (!m_selectedShapeIds.isEmpty()) {
                updateShapeLinks(shapes, m_snapshotBeforeLassoTransform.shapes, m_selectedShapeIds);
            }
            m_lassoBounds = lassoSelectionBounds();
            pushUndoSnapshot(m_snapshotBeforeLassoTransform);
            documentChanged = true;
        }
        invalidateStaticCache();
        break;
    }
    case InteractionMode::LassoScale:
    case InteractionMode::LassoRotate:
        moveLassoTransform(world);
        if (pointDistance(world, m_lassoDragStartWorld) * zoom > 0.01) {
            pushUndoSnapshot(m_snapshotBeforeLassoTransform);
            documentChanged = true;
        }
        invalidateStaticCache();
        break;
    case InteractionMode::ImageMove:
    case InteractionMode::ImageResize:
        moveImageTransform(world);
        if (pointDistance(world, m_imageDragStartWorld) * zoom > 0.01) {
            pushUndoSnapshot(m_snapshotBeforeImageEdit);
            documentChanged = true;
        }
        invalidateStaticCache();
        break;
    case InteractionMode::ShapeMove:
    case InteractionMode::ShapeDragHandle:
    case InteractionMode::ShapeScale:
        moveShapeEdit(world);
        if (const auto* shape = findShapeById(m_selectedShapeId);
            shape
            && (shape->vertices != m_shapeDragOriginalVertices || shape->kind != m_shapeDragOriginalKind)) {
            pushUndoSnapshot(m_snapshotBeforeShapeEdit);
            documentChanged = true;
        }
        invalidateStaticCache();
        break;
    case InteractionMode::ShapeCreate:
        if (m_hasActiveShape) {
            if (!m_holdSnappedToShape) {
                moveShapeCreate(world, mods);
            }
            const QRectF bounds = computeShapeBounds(m_activeShape);
            const bool clickedFigure =
                !m_holdSnappedToShape && pointDistance(world, m_shapeDragStartWorld) * zoom <= 3.0;
            auto& shapes = m_doc.shapes();
            if (clickedFigure) {
                m_selectedShapeId.clear();
                for (auto it = shapes.crbegin(); it != shapes.crend(); ++it) {
                    if (it->kind != ShapeKind::ShadedRegion
                        && (hitTestHandle(*it, m_shapeDragStartWorld, 9.0 / zoom) >= 0
                            || hitTestShape(*it, m_shapeDragStartWorld, 6.0 / zoom))) {
                        m_selectedShapeId = it->id;
                        break;
                    }
                }
            } else if (std::max(bounds.width() - m_activeShape.size, bounds.height() - m_activeShape.size)
                           * zoom
                       > 3.0) {
                pushUndoSnapshot(m_snapshotBeforeShapeEdit);
                documentChanged = true;
                m_activeShape.bounds = bounds;
                shapes.append(m_activeShape);
                attachNewShape(shapes);
                m_selectedShapeId = m_activeShape.id;
            } else {
                m_selectedShapeId.clear();
            }
            m_hasActiveShape = false;
            m_holdSnappedToShape = false;
            invalidateStaticCache();
        }
        break;
    case InteractionMode::Erase:
        moveErase(world, mods);
        if (m_erasedAnything) {
            pushUndoSnapshot(m_snapshotBeforeErase);
            documentChanged = true;
        }
        m_snapshotBeforeErase = {};
        m_erasedAnything = false;
        clearEraserCursor();
        break;
    case InteractionMode::Draw:
        if (!m_hasActiveStroke) {
            break;
        }
        if (!m_activeStroke.points.isEmpty()) {
            const auto& last = m_activeStroke.points.back();
            const StrokePoint end = inputPoint(world, last.pressure, last.tiltX, last.tiltY);
            if (mods.testFlag(Qt::ShiftModifier) || m_holdSnappedToShape) {
                m_activeStroke.points = {m_rawStartPoint, snappedPoint(m_rawStartPoint, end)};
            } else if (m_activeStroke.tool == ToolType::Highlighter
                       && (m_activeStroke.tipStyle == 2 || m_hlStraightenOnRelease)) {
                m_activeStroke.points = {m_rawStartPoint, end};
            } else if (pointDistance({last.x, last.y}, {end.x, end.y}) > 0.001) {
                m_activeStroke.points.append(end);
                m_inkSmoother.push(m_activeStroke);
            }
        }
        if (!mods.testFlag(Qt::ShiftModifier)
            && (m_activeStroke.tool == ToolType::Pen || m_activeStroke.tool == ToolType::Ballpoint)) {
            trimPenHooks(m_activeStroke, zoom);
        }
        if (m_fountainFlickEnabled && m_activeStroke.tool == ToolType::Pen && m_activeStroke.tipStyle == 1
            && !mods.testFlag(Qt::ShiftModifier)) {
            taperPenFlick(m_activeStroke, zoom);
        }
        if (m_activeStroke.tool == ToolType::Highlighter
            && (m_activeStroke.tipStyle == 2 || m_hlStraightenOnRelease)
            && m_activeStroke.points.size() >= 2) {
            m_activeStroke.points = {m_activeStroke.points.front(), m_activeStroke.points.back()};
        }
        m_liveInk.reset(m_liveInkLayer);
        m_activeStroke.bounds = computeBounds(m_activeStroke.points, m_activeStroke.size * 0.7);
        if (tryCommitFreehandGeometry(mods)) {
            m_lastLiveDirtyRect = {};
            return;
        }
        pushUndoSnapshot(currentSnapshot());
        documentChanged = true;
        m_doc.strokes().append(m_activeStroke);
        m_hasActiveStroke = false;
        if (m_mathBeautifyEnabled && !mods.testFlag(Qt::ShiftModifier)
            && (m_activeStroke.tool == ToolType::Pen || m_activeStroke.tool == ToolType::Ballpoint)) {
            const QRectF nb = m_activeStroke.bounds;
            if (!m_pendingBeautifyIds.isEmpty() && !m_pendingBeautifyBox.isEmpty()) {
                const double refH = std::clamp(m_pendingBeautifyBox.height(), 18.0, 80.0);
                const bool retroAccessory = (nb.right() >= m_pendingBeautifyBox.left() - 6.0)
                    && (nb.left() <= m_pendingBeautifyBox.right() + 6.0)
                    && (std::abs(nb.center().y() - m_pendingBeautifyBox.center().y()) <= refH * 0.95);
                const bool jumpedRight =
                    (nb.left() - m_pendingBeautifyBox.right()) > std::max(14.0, refH * 0.42);
                const bool jumpedLeft = !retroAccessory
                    && (m_pendingBeautifyBox.left() - nb.right()) > std::max(14.0, refH * 0.45);
                const bool jumpedLine =
                    std::abs(m_pendingBeautifyBox.center().y() - nb.center().y()) > refH * 1.15;
                if (jumpedRight || jumpedLeft || jumpedLine) {
                    m_mode = InteractionMode::None;
                    flushPendingBeautify();
                }
            }
            m_pendingBeautifyIds.insert(m_activeStroke.id);
            m_pendingBeautifyBox = m_pendingBeautifyBox.isEmpty() ? nb : m_pendingBeautifyBox.united(nb);
            if (m_beautifyTimer) {
                m_beautifyTimer->start(m_beautifyPauseMs);
            }
        }
        skipFullRepaint = appendStrokeToStaticCache(m_activeStroke);
        m_lastLiveDirtyRect = {};
        if (!skipFullRepaint)
            invalidateStaticCache();
        break;
    }

    m_mode = InteractionMode::None;
    m_linkedPreview = false;
    m_activeGuides.clear();
    m_activeHandleIndex = -1;
    updateCursorShape();
    if (documentChanged) {
        saveAutosave();
    }
    emit stateChanged();
    if (!skipFullRepaint)
        update();
}

void CanvasWidget::flushPendingBeautify() {
    if (m_beautifyTimer) {
        m_beautifyTimer->stop();
    }
    if (!m_mathBeautifyEnabled || m_pendingBeautifyIds.isEmpty()) {
        m_pendingBeautifyBox = QRectF();
        m_pendingBeautifyIds.clear();
        return;
    }
    if (m_mode != InteractionMode::None) {
        if (m_beautifyTimer) {
            m_beautifyTimer->start(350);
        }
        return;
    }
    const QSet<QString> ids = m_pendingBeautifyIds;
    m_pendingBeautifyBox = QRectF();
    m_pendingBeautifyIds.clear();
    QVector<Stroke> candidate = m_doc.strokes();
    if (straightenSelectedStrokes(candidate, ids)) {
        pushUndoSnapshot(currentSnapshot()); // 1er Ctrl+Z vuelve al trazo original.
        m_doc.strokes() = std::move(candidate);
        invalidateStaticCache();
        saveAutosave();
        emit stateChanged();
        update();
    }
}

void CanvasWidget::stampCongruenceMarkAt(const QPointF& worldPos, const QString& markSpec) {
    if (markSpec.trimmed().isEmpty()) {
        return;
    }
    auto candidate = m_doc.shapes();
    QString markId;
    if (!stampOrCycleCongruenceMark(candidate, worldPos, markSpec, m_color, m_size,
                                    m_doc.viewport().zoom, &markId)) {
        return;
    }
    pushUndoSnapshot(currentSnapshot());
    m_doc.shapes() = std::move(candidate);
    clearSelection();
    if (!markId.isEmpty()) {
        if (const auto* placed = findShapeById(markId)) {
            m_shapePulseAnimation->stop();
            m_shapePulseShape = *placed;
            m_shapePulseAlpha = 1.0;
            m_shapePulseAnimation->start();
            emit toastRequested(edgeMarkToast(parseEdgeMark(placed->label)));
        }
    }
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    update();
}

void CanvasWidget::highlightSegmentAt(const QPointF& worldPos, const QColor& color, int style) {
    const QColor resolvedColor = color.isValid() ? color : m_color;
    const auto hl = createHighlightedSegment(m_doc.shapes(), worldPos, resolvedColor, m_size,
                                             m_doc.viewport().zoom, style);
    if (!hl) {
        return;
    }

    auto& shapes = m_doc.shapes();
    qsizetype existingIdx = -1;
    for (qsizetype i = 0; i < shapes.size(); ++i) {
        const auto& existing = shapes.at(i);
        if (isHighlightOverlay(existing) && sameHighlightGeometry(existing, *hl)) {
            existingIdx = i;
            break;
        }
    }

    pushUndoSnapshot(currentSnapshot());
    clearSelection();
    if (existingIdx >= 0) {
        const auto& existing = shapes.at(existingIdx);
        if (existing.color == hl->color && existing.label == hl->label
            && existing.linePattern == hl->linePattern) {
            shapes.removeAt(existingIdx);
            emit toastRequested(QStringLiteral("Resaltado quitado"));
        } else {
            ShapeItem updated = *hl;
            updated.id = existing.id;
            shapes[existingIdx] = updated;
            m_shapePulseAnimation->stop();
            m_shapePulseShape = updated;
            m_shapePulseAlpha = 1.0;
            m_shapePulseAnimation->start();
            emit toastRequested(QStringLiteral("Tramo resaltado ✓"));
        }
    } else {
        shapes.append(*hl);
        m_shapePulseAnimation->stop();
        m_shapePulseShape = *hl;
        m_shapePulseAlpha = 1.0;
        m_shapePulseAnimation->start();
        emit toastRequested(QStringLiteral("Tramo resaltado ✓"));
    }
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    update();
}

void CanvasWidget::triggerHoldToShape() {
    if (!m_holdToShapeEnabled || m_mode != InteractionMode::Draw || !m_hasActiveStroke) {
        return;
    }
    const bool isHighlighter =
        (m_activeStroke.tool == ToolType::Highlighter && m_activeStroke.tipStyle != 2);
    if (!isHighlighter && m_activeStroke.tool != ToolType::Pen
        && m_activeStroke.tool != ToolType::Ballpoint) {
        return;
    }
    double total = 0.0;
    for (qsizetype i = 1; i < m_activeStroke.points.size(); ++i) {
        total += pointDistance({m_activeStroke.points[i - 1].x, m_activeStroke.points[i - 1].y},
                               {m_activeStroke.points[i].x, m_activeStroke.points[i].y});
    }
    const Viewport& vp = m_doc.viewport();

    if (isHighlighter) {
        if (total * vp.zoom < 16.0 || m_activeStroke.points.isEmpty()) {
            return;
        }
        m_holdSnappedToShape = true;
        m_liveInk.reset(m_liveInkLayer);
        m_lastLiveDirtyRect = {};
        m_activeStroke.points = {
            m_rawStartPoint,
            snappedPoint(m_rawStartPoint, m_activeStroke.points.back()),
        };
        m_holdTimer->stop();
        update();
        return;
    }

    // 1. Mantener presionado un punto sobre una arista -> abrir mini-barra rápida de marcas.
    if (total * vp.zoom < 16.0) {
        QPointF edgePt;
        if (m_gestureOptions.holdEdgeMenu
            && isPointNearGeometryEdge(m_holdLastWorld, m_doc.shapes(), vp.zoom, 22.0, &edgePt)) {
            m_hasActiveStroke = false;
            m_liveInk.reset(m_liveInkLayer);
            m_lastLiveDirtyRect = {};
            m_mode = InteractionMode::None;
            m_holdTimer->stop();
            update();
            const QPointF screenPos(edgePt.x() * vp.zoom + vp.x, edgePt.y() * vp.zoom + vp.y);
            emit edgeMarkMenuRequested(screenPos, edgePt);
        }
        return;
    }

    if (m_gestureOptions.angleArc || m_gestureOptions.rightAngle) {
        if (auto autoAngle = trySnapAngleArcAtVertex(m_activeStroke, m_doc.shapes(), vp.zoom,
                                                     m_gestureOptions.angleArc,
                                                     m_gestureOptions.rightAngle)) {
            if (m_showAutoMeasurements && isAutoVertexAngle(*autoAngle)) {
                autoAngle->label.clear();
                autoAngle->bounds = computeShapeBounds(*autoAngle);
            }
            pushUndoSnapshot(currentSnapshot());
            m_doc.shapes().append(*autoAngle);
            attachNewShape(m_doc.shapes());
            emit toastRequested(QStringLiteral("Ángulo ✓"));
            finishAutoGeometry(*autoAngle);
            return;
        }
    }

    if (total * vp.zoom < 25.0) {
        return;
    }

    if (m_gestureOptions.edgeMarks) {
        if (const auto mark = trySnapCongruenceStrokeOnEdge(m_activeStroke, m_doc.shapes(), vp.zoom, true)) {
            pushUndoSnapshot(currentSnapshot());
            m_doc.shapes().append(*mark);
            emit toastRequested(edgeMarkToast(parseEdgeMark(mark->label)));
            finishAutoGeometry(*mark);
            return;
        }
    }

    const auto recognized = recognizeFreehandShape(m_activeStroke, m_autoShapeOptions);
    if (!recognized) {
        return;
    }
    // Nunca convertir trazos cortos sobre una arista en una Curva accidental.
    if (recognized->kind == ShapeKind::Curve) {
        const QRectF sb = computeBounds(m_activeStroke.points, 0.0);
        const double dim = std::max(sb.width(), sb.height());
        if (dim * vp.zoom < 150.0
            && isPointNearGeometryEdge(sb.center(), m_doc.shapes(), vp.zoom, 30.0)) {
            return;
        }
    }

    m_liveInk.reset(m_liveInkLayer);
    m_lastLiveDirtyRect = {};
    m_activeShape = *recognized;
    m_hasActiveShape = true;
    m_hasActiveStroke = false;
    m_holdSnappedToShape = true;
    m_selectedShapeId = m_activeShape.id;
    if (drawsFromEndpoints(m_activeShape.kind) && m_activeShape.vertices.size() >= 2) {
        auto& points = m_activeShape.vertices;
        const QPointF oldStart = points.front();
        const QPointF oldEnd = points.back();
        const QPointF rawStart(m_rawStartPoint.x, m_rawStartPoint.y);
        const auto start =
            snapPointWithGuides(rawStart, std::nullopt, m_doc.shapes(), m_activeShape.id, vp.zoom,
                                false, m_smartGuideOptions);
        const auto end = snapPointWithGuides(m_holdLastWorld, start.point, m_doc.shapes(),
                                             m_activeShape.id, vp.zoom, false, m_smartGuideOptions);
        if (end.contact) {
            points.back() = end.point;
        }
        const auto reverse =
            snapPointWithGuides(rawStart, points.back(), m_doc.shapes(), m_activeShape.id, vp.zoom,
                                false, m_smartGuideOptions);
        if (reverse.contact) {
            points.front() = reverse.point;
        }
        if (m_activeShape.kind == ShapeKind::Curve && points.size() >= 3) {
            points[1] += (points.front() - oldStart + points.back() - oldEnd) * 0.5;
        }
        m_activeShape.bounds = computeShapeBounds(m_activeShape);
    }
    m_snapshotBeforeShapeEdit = currentSnapshot();
    m_shapeDragOriginalVertices = m_activeShape.vertices;
    m_shapeDragStartWorld = m_holdLastWorld;
    m_activeHandleIndex = hitTestHandle(m_activeShape, m_holdLastWorld, 1e12);
    if (m_activeShape.kind == ShapeKind::Circle) {
        m_activeHandleIndex = 1;
    }
    m_mode = InteractionMode::ShapeCreate;
    m_holdTimer->stop();
    m_shapePulseAnimation->stop();
    m_shapePulseShape = m_activeShape;
    m_shapePulseAlpha = 1.0;
    m_shapePulseAnimation->start();
    const QString name = recognizedShapeLabel(m_activeShape.kind);
    if (!name.isEmpty()) {
        emit toastRequested(name + QStringLiteral(" ✓"));
    }
    invalidateStaticCache();
    emit stateChanged();
}

void CanvasWidget::applyEraseAt(const QPointF& worldPos, bool trimToIntersections) {
    QVector<Stroke> nextStrokes;
    QSet<QString> removedStrokeIds;
    bool changed = false;
    const double radius = m_eraserSize / 2.0;

    for (const auto& stroke : m_doc.strokes()) {
        const bool isHighlight = (stroke.tool == ToolType::Highlighter);
        const bool canEraseStroke = isHighlight ? m_eraserTargets.highlighter : m_eraserTargets.ink;
        if (!canEraseStroke) {
            nextStrokes.append(stroke);
            continue;
        }
        if (m_eraserMode == EraserMode::Stroke) {
            if (hitTestStroke(stroke, worldPos, radius)) {
                changed = true;
                removedStrokeIds.insert(stroke.id);
            } else {
                nextStrokes.append(stroke);
            }
        } else {
            if (const auto split = eraseStrokePartially(stroke, worldPos, radius)) {
                changed = true;
                removedStrokeIds.insert(stroke.id);
                nextStrokes += *split;
            } else {
                nextStrokes.append(stroke);
            }
        }
    }

    auto pruneSelections = [&](const QSet<QString>& goneStrokes,
                               const QSet<QString>& goneShapes,
                               const QSet<QString>& goneTexts) {
        const bool hadLasso = hasLassoSelection();
        m_selectedStrokeIds.subtract(goneStrokes);
        m_selectedShapeIds.subtract(goneShapes);
        m_selectedTextIds.subtract(goneTexts);
        if (goneShapes.contains(m_selectedShapeId)) {
            m_selectedShapeId.clear();
        }
        if (goneTexts.contains(m_selectedTextId)) {
            m_selectedTextId.clear();
        }
        if (!goneStrokes.isEmpty()) {
            m_pendingBeautifyIds.subtract(goneStrokes);
            if (m_pendingBeautifyIds.isEmpty()) {
                m_pendingBeautifyBox = QRectF();
                if (m_beautifyTimer) {
                    m_beautifyTimer->stop();
                }
            }
        }
        if (hadLasso) {
            m_lassoBounds = hasLassoSelection() ? lassoSelectionBounds() : QRectF();
        }
    };

    auto shapes = m_doc.shapes();
    QSet<QString> removedShapeIds;
    auto eraseShapes = [&](bool shading) {
        if (!m_eraserTargets.shapes) {
            return;
        }
        if (!shading && (m_eraserMode == EraserMode::Partial || trimToIntersections)) {
            QVector<ShapeItem> nextShapes;
            nextShapes.reserve(shapes.size());
            for (const auto& shape : shapes) {
                if (shape.locked || shape.kind == ShapeKind::ShadedRegion) {
                    nextShapes.append(shape);
                    continue;
                }
                const bool partialSupported = shape.kind == ShapeKind::Line
                    || shape.kind == ShapeKind::Polyline || shape.kind == ShapeKind::Triangle
                    || shape.kind == ShapeKind::Rectangle || shape.kind == ShapeKind::Circle
                    || shape.kind == ShapeKind::Ellipse || shape.kind == ShapeKind::Curve
                    || shape.kind == ShapeKind::Arc;
                if (partialSupported) {
                    if (const auto split =
                            eraseShapePartially(shape, shapes, worldPos, radius, trimToIntersections)) {
                        changed = true;
                        removedShapeIds.insert(shape.id);
                        nextShapes += *split;
                    } else {
                        nextShapes.append(shape);
                    }
                } else if (hitTestShape(shape, worldPos, radius)) {
                    changed = true;
                    removedShapeIds.insert(shape.id);
                } else {
                    nextShapes.append(shape);
                }
            }
            shapes = std::move(nextShapes);
            return;
        }
        QVector<ShapeItem> keptShapes;
        keptShapes.reserve(shapes.size());
        for (const auto& shape : shapes) {
            if (!shape.locked && (shape.kind == ShapeKind::ShadedRegion) == shading
                && hitTestShape(shape, worldPos, radius)) {
                changed = true;
                removedShapeIds.insert(shape.id);
            } else {
                keptShapes.append(shape);
            }
        }
        shapes = std::move(keptShapes);
    };

    eraseShapes(false);

    QSet<QString> removedTextIds;
    if (m_eraserTargets.ink) {
        const QRectF eraserRect(worldPos - QPointF(radius, radius), QSizeF(m_eraserSize, m_eraserSize));
        auto& texts = m_doc.texts();
        const auto removedTexts = texts.removeIf([&](const TextItem& item) {
            if (!item.locked && item.bounds.intersects(eraserRect)) {
                removedTextIds.insert(item.id);
                return true;
            }
            return false;
        });
        if (removedTexts > 0) {
            changed = true;
        }
    }

    if (!changed) {
        eraseShapes(true);
    }

    if (changed) {
        if (shapes.size() != m_doc.shapes().size() || !removedShapeIds.isEmpty()) {
            updateShapeLinks(shapes, m_doc.shapes(), {});
        }
        m_doc.strokes() = std::move(nextStrokes);
        m_doc.shapes() = std::move(shapes);
        pruneSelections(removedStrokeIds, removedShapeIds, removedTextIds);
        m_erasedAnything = true;
        invalidateStaticCache();
    }
}

} // namespace finily
