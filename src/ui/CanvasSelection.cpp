#include "CanvasWidget.h"
#include "CanvasWidgetInternal.h"
#include "core/ShapeProtocols.h"
#include "core/Utils.h"
#include "engine/DocumentSerializer.h"
#include "engine/GeometryEngine.h"
#include "engine/HandwritingEngine.h"
#include "engine/InkEngine.h"
#include "engine/LassoEngine.h"
#include "engine/PdfEngine.h"
#include "engine/StickerLibrary.h"

#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QSizeF>
#include <QUrl>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace finily {
namespace {
// Grosor del arco de un ángulo automático según el grosor de su figura.
double autoAngleStrokeWidth(double shapeSize) {
    return std::clamp(shapeSize * 0.85, 1.4, 24.0);
}


constexpr auto objectMimeType = "application/x-finily-objects";

bool linkedFillOf(const ShapeItem& fill, const QString& id) {
    return fill.kind == ShapeKind::ShadedRegion && fill.shadeBoundary.size() == 1
        && fill.shadeBoundary.contains(id);
}

QString singleSelectedShapeId(const QString& selectedShapeId, const QSet<QString>& selectedShapeIds) {
    if (!selectedShapeId.isEmpty())
        return selectedShapeId;
    if (selectedShapeIds.size() == 1)
        return *selectedShapeIds.cbegin();
    return {};
}

std::optional<QRectF> selectedObjectBounds(const NotebookDocument& doc,
                                           const ShapeItem* selectedShape,
                                           const QString& selectedImageId,
                                           const QString& selectedTextId) {
    if (selectedShape)
        return selectedShape->bounds;
    if (!selectedImageId.isEmpty()) {
        for (const auto& im : doc.images()) {
            if (im.id == selectedImageId)
                return im.rect;
        }
    }
    if (!selectedTextId.isEmpty()) {
        for (const auto& t : doc.texts()) {
            if (t.id == selectedTextId)
                return t.bounds;
        }
    }
    return std::nullopt;
}

bool containsPendingBeautifyStroke(const QVector<Stroke>& strokes,
                                   const QSet<QString>& ids,
                                   const QSet<QString>& pendingIds) {
    if (pendingIds.isEmpty() || ids.isEmpty())
        return false;
    return std::any_of(strokes.cbegin(), strokes.cend(), [&](const Stroke& st) {
        return ids.contains(st.id) && pendingIds.contains(st.id);
    });
}

ImageItem makePlacedImage(const QImage& image,
                          const QPointF& centerWorld,
                          const QSize& viewportSize,
                          double zoom) {
    const double visibleW = std::max(240.0, double(viewportSize.width()) / std::max(0.2, zoom));
    const double rawW = double(image.width()) / std::max(1.0, image.devicePixelRatio());
    const double targetW = std::min(visibleW * 0.48, rawW);
    const QSizeF imgSize(targetW, targetW * double(image.height()) / std::max(1, image.width()));
    ImageItem item;
    item.id = freshId();
    item.image = image;
    item.rect = QRectF(centerWorld - QPointF(imgSize.width() / 2.0, imgSize.height() / 2.0), imgSize);
    return item;
}

} // namespace

bool CanvasWidget::hasSelection() const {
    return hasLassoSelection() || hasSelectedShape() || hasSelectedImage() || hasSelectedText();
}

bool CanvasWidget::hasLockableSelection() const {
    return hasSelectedShape() || hasSelectedImage() || hasSelectedText()
        || !m_selectedShapeIds.isEmpty() || !m_selectedImageIds.isEmpty()
        || !m_selectedTextIds.isEmpty();
}

QRectF CanvasWidget::selectionScreenRect() const {
    QRectF worldRect;
    if (hasLassoSelection() && !m_lassoBounds.isEmpty()) {
        worldRect = m_lassoBounds;
    } else if (const auto b = selectedObjectBounds(m_doc, findShapeById(m_selectedShapeId),
                                                   m_selectedImageId, m_selectedTextId)) {
        worldRect = *b;
    } else if (m_polylineActive && !m_polylineVertices.isEmpty()) {
        worldRect = computeShapeBounds(m_activeShape);
    }
    if (worldRect.isEmpty())
        return {};
    const Viewport& vp = m_doc.viewport();
    const QPointF off(vp.x, vp.y);
    return QRectF(worldRect.topLeft() * vp.zoom + off, worldRect.bottomRight() * vp.zoom + off)
        .normalized();
}

std::optional<ImageItem> CanvasWidget::selectedImage() const {
    QString targetId = m_selectedImageId;
    if (targetId.isEmpty() && m_selectedImageIds.size() == 1 && m_selectedStrokeIds.isEmpty()
        && m_selectedShapeIds.isEmpty() && m_selectedTextIds.isEmpty()) {
        targetId = *m_selectedImageIds.cbegin();
    }
    if (targetId.isEmpty())
        return std::nullopt;
    for (const auto& img : m_doc.images()) {
        if (img.id == targetId && !img.isPdfPage && !img.image.isNull())
            return img;
    }
    return std::nullopt;
}

void CanvasWidget::insertExtractedContent(const QVector<ShapeItem>& shapes,
                                          const QVector<TextItem>& texts,
                                          bool selectAsGroup) {
    if (shapes.isEmpty() && texts.isEmpty())
        return;
    if (m_polylineActive)
        finishActivePolyline();
    pushUndoSnapshot(currentSnapshot());
    const QString groupId = (selectAsGroup && (shapes.size() + texts.size()) > 1) ? freshId() : QString();
    QSet<QString> shapeIds;
    QSet<QString> textIds;
    for (ShapeItem shape : shapes) {
        if (shape.id.isEmpty())
            shape.id = freshId();
        if (shape.bounds.isEmpty())
            shape.bounds = computeShapeBounds(shape);
        if (!groupId.isEmpty() && shape.groupId.isEmpty())
            shape.groupId = groupId;
        m_doc.shapes().append(shape);
        attachNewShape(m_doc.shapes());
        shapeIds.insert(shape.id);
    }
    for (TextItem item : texts) {
        if (item.id.isEmpty())
            item.id = freshId();
        if (item.bounds.isEmpty() && !item.text.isEmpty())
            item.bounds = textBounds(item);
        if (!groupId.isEmpty() && item.groupId.isEmpty())
            item.groupId = groupId;
        m_doc.texts().append(item);
        textIds.insert(item.id);
    }
    clearSelection();
    if (!selectAsGroup && shapeIds.size() == 1 && textIds.isEmpty()) {
        m_selectedShapeId = *shapeIds.cbegin();
    } else if (!selectAsGroup && textIds.size() == 1 && shapeIds.isEmpty()) {
        m_selectedTextId = *textIds.cbegin();
    } else {
        m_selectedShapeIds = shapeIds;
        m_selectedTextIds = textIds;
        m_lassoBounds = lassoSelectionBounds();
    }
    if (m_tool != ToolType::Select)
        m_tool = ToolType::Lasso;
    updateCursorShape();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    update();
}

void CanvasWidget::clearSelection() {
    clearPenCursor();
    m_selectedTextId.clear();
    m_selectedShapeId.clear();
    m_activeHandleIndex = -1;
    m_selectedStrokeIds.clear();
    m_selectedShapeIds.clear();
    m_selectedTextIds.clear();
    m_selectedImageIds.clear();
    m_linkedPreview = false;
    m_selectedImageId.clear();
    m_lassoPolygon.clear();
    m_lassoBounds = {};
    m_lassoPreviewDelta = QPointF();
    m_lassoHandleIndex = -1;
    m_imageHandleIndex = -1;
    invalidateStaticCache();
    emit stateChanged();
}

QSet<QString> CanvasWidget::selectedItemIds() const {
    auto selected = m_selectedStrokeIds | m_selectedShapeIds | m_selectedTextIds | m_selectedImageIds;
    for (const auto& id : {m_selectedShapeId, m_selectedImageId, m_selectedTextId}) {
        if (!id.isEmpty())
            selected.insert(id);
    }
    return selected;
}

QRectF CanvasWidget::lassoSelectionBounds() const {
    auto bounds = computeSelectionBounds(m_doc.strokes(), m_doc.shapes(),
                                         m_selectedStrokeIds, m_selectedShapeIds);
    auto add = [&](const QRectF& rect) {
        bounds = bounds.isNull() ? rect : bounds.united(rect);
    };
    for (const auto& text : m_doc.texts()) {
        if (m_selectedTextIds.contains(text.id))
            add(text.bounds);
    }
    for (const auto& image : m_doc.images()) {
        if (m_selectedImageIds.contains(image.id))
            add(image.rect);
    }
    return bounds;
}

void CanvasWidget::expandLassoSelection(const QString& seed) {
    auto ids = selectedItemIds();
    if (!seed.isEmpty())
        ids.insert(seed);
    qsizetype previous;
    do {
        previous = ids.size();
        QSet<QString> groups;
        auto collect = [&](const auto& items) {
            for (const auto& item : items) {
                if (ids.contains(item.id) && !item.groupId.isEmpty())
                    groups.insert(item.groupId);
            }
        };
        collect(m_doc.strokes());
        collect(m_doc.shapes());
        collect(m_doc.texts());
        collect(m_doc.images());
        auto add = [&](const auto& items, QSet<QString>& selected) {
            for (const auto& item : items) {
                if (ids.contains(item.id) || groups.contains(item.groupId)) {
                    selected.insert(item.id);
                    ids.insert(item.id);
                }
            }
        };
        add(m_doc.strokes(), m_selectedStrokeIds);
        add(m_doc.shapes(), m_selectedShapeIds);
        add(m_doc.texts(), m_selectedTextIds);
        add(m_doc.images(), m_selectedImageIds);
        for (const auto& shape : std::as_const(m_doc.shapes())) {
            if (!m_selectedShapeIds.contains(shape.id))
                continue;
            for (auto it = shape.shadeBoundary.keyBegin(); it != shape.shadeBoundary.keyEnd(); ++it) {
                if (std::as_const(*this).findShapeById(*it))
                    ids.insert(*it);
            }
        }
    } while (ids.size() != previous);
    m_lassoBounds = lassoSelectionBounds();
}

bool CanvasWidget::canUngroupSelection() const {
    const auto ids = selectedItemIds();
    if (ids.isEmpty())
        return false;
    auto grouped = [&](const auto& items) {
        return std::any_of(items.cbegin(), items.cend(), [&](const auto& item) {
            return ids.contains(item.id) && !item.groupId.isEmpty();
        });
    };
    return grouped(m_doc.strokes()) || grouped(m_doc.shapes()) || grouped(m_doc.texts())
        || grouped(m_doc.images());
}

bool CanvasWidget::canGroupSelection() const {
    const auto ids = selectedItemIds();
    if (ids.size() < 2)
        return false;
    QSet<QString> groups;
    auto collect = [&](const auto& items) {
        for (const auto& item : items) {
            if (ids.contains(item.id))
                groups.insert(item.groupId);
        }
    };
    collect(m_doc.strokes());
    collect(m_doc.shapes());
    collect(m_doc.texts());
    collect(m_doc.images());
    return groups.size() > 1 || groups.contains(QString());
}

void CanvasWidget::changeSelectionGroup(bool group) {
    if (m_mode != InteractionMode::None || (group ? !canGroupSelection() : !canUngroupSelection()))
        return;
    pushUndoSnapshot(currentSnapshot());
    const auto ids = selectedItemIds();
    const auto groupId = group ? freshId() : QString();
    auto assign = [&](auto& items) {
        for (auto& item : items) {
            if (ids.contains(item.id))
                item.groupId = groupId;
        }
    };
    assign(m_doc.strokes());
    assign(m_doc.shapes());
    assign(m_doc.texts());
    assign(m_doc.images());
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::groupSelection() {
    changeSelectionGroup(true);
}

void CanvasWidget::ungroupSelection() {
    changeSelectionGroup(false);
}

void CanvasWidget::deleteSelectedShape() {
    if (!m_selectedShapeId.isEmpty())
        deleteSelection();
}

void CanvasWidget::deleteSelection() {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return;
    const auto ids = selectedItemIds();
    if (ids.isEmpty())
        return;
    bool skippedLocked = false;
    for (const auto& s : m_doc.shapes()) {
        if (ids.contains(s.id) && s.locked)
            skippedLocked = true;
    }
    for (const auto& im : m_doc.images()) {
        if (ids.contains(im.id) && im.locked)
            skippedLocked = true;
    }
    for (const auto& t : m_doc.texts()) {
        if (ids.contains(t.id) && t.locked)
            skippedLocked = true;
    }
    const bool removedPendingBeautify =
        containsPendingBeautifyStroke(m_doc.strokes(), ids, m_pendingBeautifyIds);
    const auto original = currentSnapshot();
    const auto strokesBefore = m_doc.strokes().size();
    const auto shapesBefore = m_doc.shapes().size();
    const auto imagesBefore = m_doc.images().size();
    const auto textsBefore = m_doc.texts().size();
    auto selected = [&](const auto& item) {
        return ids.contains(item.id);
    };
    m_doc.strokes().removeIf(selected);

    // Recopilar vértices e IDs de figuras eliminadas para limpiar sus ángulos automáticos huérfanos.
    QVector<QPointF> deletedShapeVertices;
    QSet<QString> deletedShapeIds;
    for (const ShapeItem& s : m_doc.shapes()) {
        if (!s.locked && selected(s) && !isAutoVertexAngle(s)) {
            deletedShapeIds.insert(s.id);
            if (s.kind == ShapeKind::Rectangle && s.vertices.size() == 2) {
                const QRectF r = QRectF(s.vertices[0], s.vertices[1]).normalized();
                deletedShapeVertices << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
            } else {
                deletedShapeVertices.append(s.vertices);
            }
        }
    }
    const double angleTol = 6.0 / std::max(0.25, m_doc.viewport().zoom);
    m_doc.shapes().removeIf([&](const ShapeItem& s) {
        if (!s.locked && selected(s))
            return true;
        if (!s.locked && isAutoVertexAngle(s)) {
            for (const auto& att : s.attachments) {
                if (deletedShapeIds.contains(att.targetId))
                    return true;
            }
            if (!s.vertices.isEmpty()) {
                for (const QPointF& v : deletedShapeVertices) {
                    if (pointDistance(s.vertices.front(), v) <= angleTol)
                        return true;
                }
            }
        }
        return false;
    });
    m_doc.images().removeIf([&](const ImageItem& image) {
        return !image.isPdfPage && !image.locked && selected(image);
    });
    m_doc.texts().removeIf([&](const TextItem& t) { return !t.locked && selected(t); });
    const bool removedAnything = (m_doc.strokes().size() != strokesBefore
                                  || m_doc.shapes().size() != shapesBefore
                                  || m_doc.images().size() != imagesBefore
                                  || m_doc.texts().size() != textsBefore);
    if (removedAnything) {
        if (removedPendingBeautify)
            cancelPendingBeautify();
        pushUndoSnapshot(original);
        updateShapeLinks(m_doc.shapes(), original.shapes, {});
        saveAutosave();
    }
    if (skippedLocked)
        emit toastRequested(QStringLiteral("Objeto bloqueado · desbloquéalo con el candado"));
    clearSelection();
    emit stateChanged();
}

bool CanvasWidget::isSelectionLocked() const {
    if (const auto* s = findShapeById(m_selectedShapeId))
        return s->locked;
    for (const auto& s : m_doc.shapes()) {
        if (m_selectedShapeIds.contains(s.id) && s.locked)
            return true;
    }
    for (const auto& im : m_doc.images()) {
        if ((im.id == m_selectedImageId || m_selectedImageIds.contains(im.id)) && im.locked)
            return true;
    }
    for (const auto& t : m_doc.texts()) {
        if ((t.id == m_selectedTextId || m_selectedTextIds.contains(t.id)) && t.locked)
            return true;
    }
    return false;
}

void CanvasWidget::toggleLockSelection() {
    const auto ids = selectedItemIds();
    if (!hasLockableSelection())
        return;
    const bool nextLocked = !isSelectionLocked();
    pushUndoSnapshot(currentSnapshot());
    for (auto& s : m_doc.shapes()) {
        if (ids.contains(s.id))
            s.locked = nextLocked;
    }
    for (auto& im : m_doc.images()) {
        if (ids.contains(im.id) && !im.isPdfPage)
            im.locked = nextLocked;
    }
    for (auto& t : m_doc.texts()) {
        if (ids.contains(t.id))
            t.locked = nextLocked;
    }
    emit toastRequested(nextLocked ? QStringLiteral("Bloqueado") : QStringLiteral("Desbloqueado"));
    saveAutosave();
    emit stateChanged();
}

SelectionStyle CanvasWidget::selectionStyle() const {
    const auto ids = selectedItemIds();
    SelectionStyle style;
    auto readStroke = [&](const auto& item) {
        if (!style.color)
            style.color = item.color;
        if (!style.width)
            style.width = item.size;
        if (!style.pattern && int(item.linePattern) <= int(LinePattern::Dotted))
            style.pattern = item.linePattern;
    };
    auto readFill = [&](const ShapeItem& fill) {
        style.filled = true;
        if (!style.fillColor)
            style.fillColor = fill.color;
        if (!style.fillPattern)
            style.fillPattern = fill.shadePattern;
    };
    for (const auto& shape : m_doc.shapes()) {
        if (!ids.contains(shape.id) || shape.locked)
            continue;
        if (shape.kind == ShapeKind::ShadedRegion) {
            readFill(shape);
            continue;
        }
        readStroke(shape);
        if (!style.ending && supportsLineEnding(shape))
            style.ending = shape.lineEnding;
        if (!style.label && hasEditableMeasurementLabel(shape))
            style.label = shape.label;
        if (!canFillShape(shape))
            continue;
        if (!style.filled)
            style.filled = false;
        for (const auto& region : m_doc.shapes()) {
            if (linkedFillOf(region, shape.id) && !region.locked && region.shadeVisible)
                readFill(region);
        }
    }
    for (const auto& stroke : m_doc.strokes()) {
        if (ids.contains(stroke.id))
            readStroke(stroke);
    }
    for (const auto& item : m_doc.texts()) {
        if (!style.color && !item.locked && ids.contains(item.id))
            style.color = item.color;
    }
    return style;
}

void CanvasWidget::setSelectionStyle(const SelectionStyle& style) {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return;
    const auto ids = selectedItemIds();
    if (ids.isEmpty())
        return;
    auto updated = currentSnapshot();
    bool changed = false;
    auto assign = [&](auto& current, const auto& next) {
        if (next && current != *next) {
            current = *next;
            changed = true;
        }
    };
    auto setStroke = [&](auto& item) {
        assign(item.color, style.color);
        assign(item.size, style.width);
        if (style.pattern && item.linePattern != LinePattern::Dimension)
            assign(item.linePattern, style.pattern);
    };
    for (auto& stroke : updated.strokes) {
        if (ids.contains(stroke.id)) {
            setStroke(stroke);
            stroke.cachedPath = QPainterPath{};
            stroke.cachedOrigin = QPointF{};
            stroke.cachedShapeHash = 0;
            stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
        }
    }
    for (auto& item : updated.texts) {
        if (!item.locked && ids.contains(item.id))
            assign(item.color, style.color);
    }

    QVector<ShapeItem> newFills;
    QSet<QString> removedFills;
    for (auto& shape : updated.shapes) {
        if (!ids.contains(shape.id) || shape.locked)
            continue;
        if (shape.kind == ShapeKind::ShadedRegion) {
            assign(shape.color, style.fillColor);
            assign(shape.shadePattern, style.fillPattern);
            if (style.filled == false)
                removedFills.insert(shape.id);
            continue;
        }
        setStroke(shape);
        if (supportsLineEnding(shape))
            assign(shape.lineEnding, style.ending);
        if (hasEditableMeasurementLabel(shape))
            assign(shape.label, style.label);
        shape.bounds = computeShapeBounds(shape);
        if (!canFillShape(shape) || !(style.filled || style.fillColor || style.fillPattern))
            continue;

        auto region = std::find_if(updated.shapes.begin(), updated.shapes.end(),
                                   [&](const auto& fill) { return linkedFillOf(fill, shape.id); });
        if (region == updated.shapes.end()) {
            if (style.filled != false) {
                if (auto fill = shadedRegionForShape(shape, style.fillColor.value_or(shape.color),
                                                     style.fillPattern.value_or(ShadePattern::Soft))) {
                    fill->id = freshId();
                    newFills.append(*fill);
                }
            }
        } else if (!region->locked) {
            if (style.filled == false) {
                removedFills.insert(region->id);
            } else {
                assign(region->color, style.fillColor);
                assign(region->shadePattern, style.fillPattern);
            }
        }
    }
    // Los ángulos automáticos siguen el color y grosor de su figura.
    if (style.color || style.width) {
        const double tol = 6.0 / std::max(0.25, m_doc.viewport().zoom);
        QVector<const ShapeItem*> owners;
        for (const auto& shape : updated.shapes) {
            if (ids.contains(shape.id) && !shape.locked && !isAutoVertexAngle(shape)
                && shape.kind != ShapeKind::ShadedRegion)
                owners.append(&shape);
        }
        QVector<std::pair<qsizetype, const ShapeItem*>> targets;
        for (qsizetype m = 0; m < updated.shapes.size(); ++m) {
            const auto& marker = updated.shapes[m];
            if (!isAutoVertexAngle(marker) || marker.locked || marker.vertices.isEmpty())
                continue;
            for (const ShapeItem* owner : owners) {
                QVector<QPointF> poly = owner->vertices;
                if (owner->kind == ShapeKind::Rectangle && poly.size() == 2) {
                    const QRectF r = QRectF(poly[0], poly[1]).normalized();
                    poly = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
                }
                if (poly.size() >= 3 && std::any_of(poly.cbegin(), poly.cend(), [&](const QPointF& v) {
                        return pointDistance(marker.vertices.front(), v) <= tol;
                    })) {
                    targets.append({m, owner});
                    break;
                }
            }
        }
        for (const auto& [m, owner] : targets) {
            auto& marker = updated.shapes[m];
            if (style.color)
                assign(marker.color, std::optional<QColor>(owner->color));
            if (style.width)
                assign(marker.size, std::optional<double>(autoAngleStrokeWidth(owner->size)));
            marker.bounds = computeShapeBounds(marker);
        }
    }
    if (!changed && newFills.isEmpty() && removedFills.isEmpty())
        return;
    updated.shapes.removeIf([&](const auto& shape) { return removedFills.contains(shape.id); });
    updated.shapes.append(newFills);
    pushUndoSnapshot(currentSnapshot());
    m_doc.strokes() = std::move(updated.strokes);
    m_doc.shapes() = std::move(updated.shapes);
    m_doc.texts() = std::move(updated.texts);
    m_selectedShapeIds.subtract(removedFills);
    if (removedFills.contains(m_selectedShapeId))
        m_selectedShapeId.clear();
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::duplicateSelection() {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return;
    const auto selection = selectedItemIds();
    if (selection.isEmpty())
        return;
    cloneObjects(currentSnapshot(), selection, QPointF(24, 24), hasLassoSelection());
}

void CanvasWidget::cloneObjects(const DocumentSnapshot& base,
                                const QSet<QString>& selection,
                                const QPointF& offset,
                                bool multiple) {
    if (selection.isEmpty())
        return;
    pushUndoSnapshot(currentSnapshot());
    QHash<QString, QString> copies;
    QHash<QString, QString> groups;
    for (const auto& id : selection)
        copies.insert(id, freshId());
    for (const auto& shape : base.shapes) {
        if (!shape.shadeBoundary.isEmpty()
            && std::all_of(shape.shadeBoundary.keyBegin(), shape.shadeBoundary.keyEnd(),
                           [&](const auto& id) { return copies.contains(id); })) {
            if (!copies.contains(shape.id))
                copies.insert(shape.id, freshId());
        }
    }
    auto identifyCopy = [&](auto& item) {
        item.id = copies.value(item.id);
        if (!item.groupId.isEmpty()) {
            if (!groups.contains(item.groupId))
                groups.insert(item.groupId, freshId());
            item.groupId = groups.value(item.groupId);
        }
    };
    QSet<QString> newStrokes;
    QSet<QString> newShapes;
    QSet<QString> newImages;
    QSet<QString> newTexts;
    for (Stroke stroke : base.strokes) {
        if (!copies.contains(stroke.id))
            continue;
        identifyCopy(stroke);
        for (auto& point : stroke.points) {
            point.x += offset.x();
            point.y += offset.y();
        }
        stroke.cachedPath = QPainterPath{};
        stroke.cachedOrigin = QPointF{};
        stroke.cachedShapeHash = 0;
        stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
        newStrokes.insert(stroke.id);
        m_doc.strokes().append(stroke);
    }
    for (ShapeItem shape : base.shapes) {
        if (!copies.contains(shape.id))
            continue;
        const bool selected = selection.contains(shape.id);
        identifyCopy(shape);
        translateShape(shape, offset);
        shape.attachments.removeIf([&](const auto& link) { return !copies.contains(link.targetId); });
        for (auto& link : shape.attachments)
            link.targetId = copies.value(link.targetId);
        QHash<QString, int> boundary;
        for (auto it = shape.shadeBoundary.cbegin(); it != shape.shadeBoundary.cend(); ++it) {
            if (copies.contains(it.key()))
                boundary.insert(copies.value(it.key()), it.value());
        }
        if (boundary.size() != shape.shadeBoundary.size())
            boundary.clear();
        shape.shadeBoundary = boundary;
        shape.shadeProbe += offset;
        if (selected)
            newShapes.insert(shape.id);
        m_doc.shapes().append(shape);
    }
    for (ImageItem image : base.images) {
        if (image.isPdfPage || !copies.contains(image.id))
            continue;
        identifyCopy(image);
        image.rect.translate(offset);
        newImages.insert(image.id);
        m_doc.images().append(image);
    }
    for (TextItem item : base.texts) {
        if (!copies.contains(item.id))
            continue;
        identifyCopy(item);
        item.pos += offset;
        item.bounds = textBounds(item);
        newTexts.insert(item.id);
        m_doc.texts().append(item);
    }
    clearSelection();
    m_selectedStrokeIds = newStrokes;
    if (!multiple && newShapes.size() == 1 && m_tool == ToolType::Select)
        m_selectedShapeId = *newShapes.cbegin();
    else
        m_selectedShapeIds = newShapes;
    if (!multiple && newImages.size() == 1)
        m_selectedImageId = *newImages.cbegin();
    else
        m_selectedImageIds = newImages;
    if (!multiple && newTexts.size() == 1)
        m_selectedTextId = *newTexts.cbegin();
    else
        m_selectedTextIds = newTexts;
    if (m_tool != ToolType::Select)
        m_tool = ToolType::Lasso;
    m_lassoBounds = lassoSelectionBounds();
    updateCursorShape();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::straightenSelection() {
    if (m_mode != InteractionMode::None || m_selectedStrokeIds.isEmpty())
        return;
    cancelPendingBeautify();
    auto strokes = m_doc.strokes();
    if (!straightenSelectedStrokes(strokes, m_selectedStrokeIds))
        return;
    pushUndoSnapshot(currentSnapshot());
    m_doc.strokes() = std::move(strokes);
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    emit toastRequested(QStringLiteral("Escritura embellecida y ordenada ✓"));
}

void CanvasWidget::flipSelectionAxis(bool horizontal) {
    if (m_mode != InteractionMode::None || m_polylineActive || !hasSelection())
        return;
    QSet<QString> shapeIds = m_selectedShapeIds;
    if (!m_selectedShapeId.isEmpty())
        shapeIds.insert(m_selectedShapeId);
    const QRectF bounds = !m_lassoBounds.isEmpty()
        ? m_lassoBounds
        : computeSelectionBounds(m_doc.strokes(), m_doc.shapes(), m_selectedStrokeIds, shapeIds);
    if (bounds.isEmpty())
        return;
    const auto originalShapes = m_doc.shapes();
    pushUndoSnapshot(currentSnapshot());
    flipSelection(m_doc.strokes(), m_doc.shapes(), m_selectedStrokeIds, shapeIds, bounds, horizontal);
    if (!shapeIds.isEmpty()) {
        updateShapeLinks(m_doc.shapes(), originalShapes, shapeIds);
        for (const QString& sid : shapeIds) {
            const ShapeItem* oldShape = nullptr;
            for (const auto& s : originalShapes) {
                if (s.id == sid) {
                    oldShape = &s;
                    break;
                }
            }
            if (!oldShape || oldShape->kind == ShapeKind::AngleMarker)
                continue;
            if (const auto* newShape = findShapeById(sid)) {
                syncAutoAnglesAfterShapeChange(*oldShape, *newShape);
            }
        }
    }
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    emit toastRequested(horizontal ? QStringLiteral("Volteado horizontalmente ✓")
                                   : QStringLiteral("Volteado verticalmente ✓"));
}

void CanvasWidget::flipSelectionHorizontal() {
    flipSelectionAxis(true);
}

void CanvasWidget::flipSelectionVertical() {
    flipSelectionAxis(false);
}

void CanvasWidget::bringSelectionToFront() {
    if (m_mode != InteractionMode::None || m_polylineActive || !hasSelection())
        return;
    const auto ids = selectedItemIds();
    if (ids.isEmpty())
        return;
    pushUndoSnapshot(currentSnapshot());
    auto partitionToBack = [&](auto& vec, auto pred) {
        std::stable_partition(vec.begin(), vec.end(), [&](const auto& item) { return !pred(item); });
    };
    partitionToBack(m_doc.strokes(), [&](const Stroke& s) { return ids.contains(s.id); });
    partitionToBack(m_doc.shapes(), [&](const ShapeItem& s) {
        return s.kind != ShapeKind::ShadedRegion && ids.contains(s.id);
    });
    partitionToBack(m_doc.images(), [&](const ImageItem& im) {
        return !im.isPdfPage && ids.contains(im.id);
    });
    partitionToBack(m_doc.texts(), [&](const TextItem& t) { return ids.contains(t.id); });
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    emit toastRequested(QStringLiteral("Traído al frente ✓"));
}

void CanvasWidget::sendSelectionToBack() {
    if (m_mode != InteractionMode::None || m_polylineActive || !hasSelection())
        return;
    const auto ids = selectedItemIds();
    if (ids.isEmpty())
        return;
    pushUndoSnapshot(currentSnapshot());
    auto partitionToFront = [&](auto& vec, auto pred) {
        std::stable_partition(vec.begin(), vec.end(), pred);
    };
    partitionToFront(m_doc.strokes(), [&](const Stroke& s) { return ids.contains(s.id); });
    std::stable_partition(m_doc.shapes().begin(), m_doc.shapes().end(), [&](const ShapeItem& s) {
        return s.kind == ShapeKind::ShadedRegion || ids.contains(s.id);
    });
    std::stable_partition(m_doc.shapes().begin(), m_doc.shapes().end(), [](const ShapeItem& s) {
        return s.kind == ShapeKind::ShadedRegion;
    });
    std::stable_partition(m_doc.images().begin(), m_doc.images().end(), [&](const ImageItem& im) {
        return im.isPdfPage || ids.contains(im.id);
    });
    std::stable_partition(m_doc.images().begin(), m_doc.images().end(), [](const ImageItem& im) {
        return im.isPdfPage;
    });
    partitionToFront(m_doc.texts(), [&](const TextItem& t) { return ids.contains(t.id); });
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    emit toastRequested(QStringLiteral("Enviado al fondo ✓"));
}

void CanvasWidget::insertVerticalSpaceAtSelection(double deltaY) {
    if (m_mode != InteractionMode::None || m_polylineActive || !hasSelection())
        return;
    const QRectF screenRect = selectionScreenRect();
    if (screenRect.isEmpty())
        return;
    const double yThreshold = screenToWorld(screenRect.topLeft()).y() - 4.0;
    auto strokes = m_doc.strokes();
    auto shapes = m_doc.shapes();
    auto images = m_doc.images();
    auto texts = m_doc.texts();
    if (!shiftContentVertically(strokes, shapes, images, texts, yThreshold, deltaY))
        return;
    pushUndoSnapshot(currentSnapshot());
    m_doc.strokes() = std::move(strokes);
    m_doc.shapes() = std::move(shapes);
    m_doc.images() = std::move(images);
    m_doc.texts() = std::move(texts);
    if (!m_lassoBounds.isEmpty())
        m_lassoBounds.translate(0.0, deltaY);
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    emit toastRequested(QStringLiteral("Espacio vertical insertado ✓"));
}

void CanvasWidget::cropSelectionToNewPage() {
    if (m_mode != InteractionMode::None || m_polylineActive || !hasSelection())
        return;
    QRectF worldRect = m_lassoBounds;
    if (worldRect.isEmpty()) {
        if (const auto b = selectedObjectBounds(m_doc, findShapeById(m_selectedShapeId),
                                                m_selectedImageId, m_selectedTextId)) {
            worldRect = *b;
        }
    }
    worldRect = worldRect.normalized().adjusted(-12.0, -12.0, 12.0, 12.0);
    if (worldRect.width() < 8.0 || worldRect.height() < 8.0)
        return;

    constexpr double scale = 2.0;
    const QSize pixelSize(std::clamp(qCeil(worldRect.width() * scale), 16, 4096),
                          std::clamp(qCeil(worldRect.height() * scale), 16, 4096));
    QImage captured(pixelSize, QImage::Format_ARGB32_Premultiplied);
    const auto theme = getThemeConfig(m_doc.themeId());
    const bool hasPdf = std::any_of(m_doc.images().cbegin(), m_doc.images().cend(),
                                    [](const ImageItem& img) {
                                        return img.isPdfPage && !img.rect.isEmpty();
                                    });
    captured.fill(hasPdf ? Qt::white : theme.background);
    {
        QPainter p(&captured);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform
                         | QPainter::TextAntialiasing);
        p.scale(double(pixelSize.width()) / worldRect.width(),
                double(pixelSize.height()) / worldRect.height());
        p.translate(-worldRect.topLeft());
        if (!hasPdf) {
            drawBackgroundTemplate(p, m_doc.currentTemplate(), theme, Viewport{0.0, 0.0, 1.0},
                                   worldRect, templateScale(),
                                   sheetRectForPageFormat(m_doc.pageFormat()));
        }
        for (const bool pdfPass : {true, false}) {
            for (const auto& im : m_doc.images()) {
                if (im.isPdfPage == pdfPass && im.rect.intersects(worldRect) && !im.image.isNull())
                    p.drawImage(im.rect, im.image);
            }
        }
        const bool lightHighlightBg = !theme.isDark || hasPdf;
        for (const auto& s : m_doc.shapes()) {
            if (s.kind == ShapeKind::ShadedRegion && s.bounds.intersects(worldRect))
                drawShape(p, s);
        }
        for (const auto& st : m_doc.strokes()) {
            if (st.tool == ToolType::Highlighter && st.bounds.intersects(worldRect))
                drawStroke(p, st, lightHighlightBg);
        }
        for (const auto& s : m_doc.shapes()) {
            if (s.kind != ShapeKind::ShadedRegion && s.bounds.intersects(worldRect))
                drawShape(p, s);
        }
        for (const auto& st : m_doc.strokes()) {
            if (st.tool != ToolType::Highlighter && st.bounds.intersects(worldRect))
                drawStroke(p, st, lightHighlightBg);
        }
        for (const auto& t : m_doc.texts()) {
            if (t.bounds.intersects(worldRect))
                drawTextItem(p, t);
        }
    }
    captured.setDevicePixelRatio(scale);

    if (!preparePageChange())
        return;
    const PageFormat format = m_doc.pageFormat();
    PageData newPage;
    newPage.viewport = fittedSheetViewport(format, size());
    const QRectF sheet = sheetRectForPageFormat(format);
    const double maxW = sheet.isEmpty() ? 680.0 : sheet.width() * 0.82;
    const double targetW = std::min(worldRect.width(), maxW);
    const double targetH = targetW * (worldRect.height() / std::max(1.0, worldRect.width()));
    const double topY = sheet.isEmpty() ? -260.0 : sheet.top() + 36.0;

    ImageItem snippet;
    snippet.id = freshId();
    snippet.image = captured;
    snippet.rect = QRectF(-targetW / 2.0, topY, targetW, targetH);
    newPage.images.append(snippet);

    insertPages(m_doc.currentPageIndex() + 1, {newPage});
    emit toastRequested(QStringLiteral("Recorte enviado a página nueva ✓"));
}

bool CanvasWidget::hasSelectedAxes() const {
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    const auto* s = findShapeById(id);
    return s && s->kind == ShapeKind::Axes;
}

QString CanvasWidget::selectedAxesFunction() const {
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    if (const auto* s = findShapeById(id); s && s->kind == ShapeKind::Axes)
        return s->label;
    return {};
}

void CanvasWidget::setSelectedAxesFunction(const QString& expr) {
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    auto* s = findShapeById(id);
    if (!s || s->kind != ShapeKind::Axes)
        return;
    const QString cleaned = expr.trimmed();
    if (!cleaned.isEmpty() && !isValidMathFunction(cleaned)) {
        emit toastRequested(QStringLiteral("Fórmula no válida (ej: x^2 - 4, sin(x), 2*x + 1)"));
        return;
    }
    if (s->label == cleaned)
        return;
    pushUndoSnapshot(currentSnapshot());
    if (auto* target = findShapeById(id)) {
        target->label = cleaned;
        target->bounds = computeShapeBounds(*target);
    }
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    emit toastRequested(cleaned.isEmpty() ? QStringLiteral("Función borrada")
                                          : QStringLiteral("Función graficada ✓"));
}

void CanvasWidget::createTableShape(int rows, int cols) {
    if (m_mode != InteractionMode::None)
        return;
    if (m_polylineActive)
        finishActivePolyline();
    rows = std::clamp(rows, 1, 12);
    cols = std::clamp(cols, 1, 12);
    pushUndoSnapshot(currentSnapshot());
    clearSelection();
    const QPointF center = screenToWorld(QPointF(width() / 2.0, height() / 2.0));
    const double w = std::clamp(cols * 76.0, 120.0, 720.0);
    const double h = std::clamp(rows * 44.0, 72.0, 520.0);
    ShapeItem table;
    table.id = freshId();
    table.kind = ShapeKind::Rectangle;
    table.color = m_color;
    table.size = m_size;
    table.linePattern = LinePattern::Solid;
    table.closed = true;
    table.label = formatTableSpec(rows, cols);
    table.vertices = {center - QPointF(w / 2.0, h / 2.0), center + QPointF(w / 2.0, h / 2.0)};
    table.bounds = computeShapeBounds(table);
    m_doc.shapes().append(table);
    m_selectedShapeId = table.id;
    m_tool = ToolType::Select;
    invalidateStaticCache();
    saveAutosave();
    updateCursorShape();
    emit stateChanged();
    emit toastRequested(QStringLiteral("Tabla %1×%2 creada ✓").arg(rows).arg(cols));
}

bool CanvasWidget::insertStickerTemplate(const QString& stickerId, StickerStyle style) {
    if (m_mode != InteractionMode::None)
        return false;
    if (m_polylineActive)
        finishActivePolyline();
    const auto tpl = findStickerTemplate(stickerId);
    if (!tpl)
        return false;
    const QPointF centerWorld = screenToWorld(QPointF(width() * 0.5, height() * 0.5));
    const double scale = std::clamp(1.0 / std::max(0.25, m_doc.viewport().zoom), 0.65, 1.8);
    const InstantiatedSticker inst =
        instantiateSticker(*tpl, centerWorld, getThemeConfig(m_doc.themeId()), style, scale);
    if (inst.shapes.isEmpty() && inst.strokes.isEmpty() && inst.texts.isEmpty())
        return false;

    pushUndoSnapshot(currentSnapshot());
    clearSelection();
    for (const auto& shape : inst.shapes) {
        m_doc.shapes().append(shape);
        m_selectedShapeIds.insert(shape.id);
    }
    for (const auto& stroke : inst.strokes) {
        m_doc.strokes().append(stroke);
        m_selectedStrokeIds.insert(stroke.id);
    }
    for (const auto& text : inst.texts) {
        m_doc.texts().append(text);
        m_selectedTextIds.insert(text.id);
    }
    m_tool = ToolType::Lasso;
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    updateCursorShape();
    emit toastRequested(inst.title + QStringLiteral(" ✓"));
    emit stateChanged();
    update();
    return true;
}

bool CanvasWidget::saveSelectionAsSticker(const QString& title, const QString& category) {
    const QString cleanedTitle = title.trimmed();
    if (cleanedTitle.isEmpty())
        return false;

    QVector<Stroke> selStrokes;
    for (const auto& s : std::as_const(m_doc.strokes())) {
        if (m_selectedStrokeIds.contains(s.id))
            selStrokes.append(s);
    }
    QVector<ShapeItem> selShapes;
    for (const auto& sh : std::as_const(m_doc.shapes())) {
        if (m_selectedShapeIds.contains(sh.id)
            || (!m_selectedShapeId.isEmpty() && sh.id == m_selectedShapeId)) {
            selShapes.append(sh);
        }
    }
    QVector<TextItem> selTexts;
    for (const auto& tx : std::as_const(m_doc.texts())) {
        if (m_selectedTextIds.contains(tx.id)
            || (!m_selectedTextId.isEmpty() && tx.id == m_selectedTextId)) {
            selTexts.append(tx);
        }
    }
    if (selStrokes.isEmpty() && selShapes.isEmpty() && selTexts.isEmpty())
        return false;

    if (!saveCustomSticker(cleanedTitle, category, selStrokes, selShapes, selTexts))
        return false;
    emit toastRequested(QStringLiteral("Sticker guardado: ") + cleanedTitle + QStringLiteral(" ✓"));
    return true;
}

void CanvasWidget::convertSelectionToText() {
    if (m_mode != InteractionMode::None || m_selectedStrokeIds.isEmpty())
        return;
    if (containsPendingBeautifyStroke(m_doc.strokes(), m_selectedStrokeIds, m_pendingBeautifyIds))
        cancelPendingBeautify();
    const QString recognized =
        HandwritingEngine::instance()
            .recognizeStrokesToText(m_doc.strokes(), m_selectedStrokeIds)
            .replace(QLatin1Char('\n'), QLatin1Char(' '))
            .simplified();
    const QRectF bounds =
        computeSelectionBounds(m_doc.strokes(), m_doc.shapes(), m_selectedStrokeIds, {});
    if (bounds.isEmpty())
        return;
    QColor inkColor = m_color;
    for (const auto& s : m_doc.strokes()) {
        if (m_selectedStrokeIds.contains(s.id)) {
            inkColor = s.color;
            break;
        }
    }

    finishTextEditor(true);
    const QSet<QString> strokesToConvert = m_selectedStrokeIds;
    TextItem draft;
    draft.id = freshId();
    draft.text = recognized;
    draft.color = inkColor;
    draft.fontSize = std::clamp(bounds.height() * 0.84, 18.0, 68.0);
    draft.pos = bounds.topLeft();
    draft.bounds = textBounds(draft);
    const Viewport& vp = m_doc.viewport();
    const QPointF anchorScreen = draft.pos * vp.zoom + QPointF(vp.x, vp.y);
    openInlineTextEditor(draft, anchorScreen, QString(), strokesToConvert);
}

bool CanvasWidget::copySelection() {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return false;
    const auto ids = selectedItemIds();
    if (ids.isEmpty())
        return false;
    PageData copy;
    for (const auto& item : m_doc.strokes()) {
        if (ids.contains(item.id))
            copy.strokes.append(item);
    }
    for (const auto& item : m_doc.shapes()) {
        if (ids.contains(item.id)
            || (!item.shadeBoundary.isEmpty()
                && std::all_of(item.shadeBoundary.keyBegin(), item.shadeBoundary.keyEnd(),
                               [&](const auto& id) { return ids.contains(id); }))) {
            copy.shapes.append(item);
        }
    }
    for (const auto& item : m_doc.images()) {
        if (!item.isPdfPage && ids.contains(item.id))
            copy.images.append(item);
    }
    for (const auto& item : m_doc.texts()) {
        if (ids.contains(item.id))
            copy.texts.append(item);
    }
    if (copy.strokes.isEmpty() && copy.shapes.isEmpty() && copy.images.isEmpty()
        && copy.texts.isEmpty()) {
        return false;
    }
    bool valid = true;
    const auto object = serializePage(copy, valid);
    if (!valid)
        return false;
    auto* data = new QMimeData;
    data->setData(objectMimeType, QJsonDocument(object).toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(data);
    emit toastRequested(tr("Selección copiada"));
    return true;
}

void CanvasWidget::cutSelection() {
    if (isSelectionLocked()) {
        emit toastRequested(tr("Desbloquea la selección antes de cortarla"));
        return;
    }
    if (copySelection())
        deleteSelection();
}

bool CanvasWidget::pasteFromClipboard() {
    if (m_mode != InteractionMode::None)
        return false;
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (mime && mime->hasFormat(objectMimeType)) {
        const auto document = QJsonDocument::fromJson(mime->data(objectMimeType));
        if (!document.isObject())
            return false;
        QHash<QString, QVector<ImageItem>> pdfSources;
        const auto items = deserializePage(document.object(), pdfSources);
        const DocumentSnapshot source{items.strokes, items.shapes, items.images, items.texts};
        QSet<QString> ids;
        QRectF bounds;
        auto include = [&](const auto& vec, auto extent) {
            for (const auto& item : vec) {
                ids.insert(item.id);
                bounds = bounds.united(extent(item));
            }
        };
        include(items.strokes, [](const auto& item) { return item.bounds; });
        include(items.shapes, [](const auto& item) { return item.bounds; });
        include(items.texts, [](const auto& item) { return item.bounds; });
        include(items.images, [](const auto& item) { return item.rect; });
        if (ids.isEmpty())
            return false;
        if (m_polylineActive)
            finishActivePolyline();
        cloneObjects(source, ids, screenToWorld(rect().center()) - bounds.center(), ids.size() > 1);
        return true;
    }
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull() && mime && mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            const QString local = url.toLocalFile();
            if (local.isEmpty())
                continue;
            if (local.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
                return importPdfFile(local);
            if (importImageFile(local))
                return true;
        }
    }
    if (image.isNull())
        return false;
    if (m_polylineActive)
        finishActivePolyline();
    pushUndoSnapshot(currentSnapshot());
    clearSelection();
    if (m_tool != ToolType::Select)
        m_tool = ToolType::Lasso;
    const ImageItem item = makePlacedImage(
        image, screenToWorld(QPointF(width() / 2.0, height() / 2.0)), size(), m_doc.viewport().zoom);
    m_doc.images().append(item);
    m_selectedImageId = item.id;
    invalidateStaticCache();
    saveAutosave();
    updateCursorShape();
    emit stateChanged();
    emit toastRequested(QStringLiteral("Imagen pegada ✓"));
    return true;
}

bool CanvasWidget::importImageFile(const QString& path) {
    if (m_mode != InteractionMode::None)
        return false;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.width() > 4096 || source.height() > 4096)
        reader.setScaledSize(source.scaled(4096, 4096, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull()) {
        emit toastRequested(QStringLiteral("No se pudo abrir la imagen"));
        return false;
    }
    if (m_polylineActive)
        finishActivePolyline();
    pushUndoSnapshot(currentSnapshot());
    clearSelection();
    if (m_tool != ToolType::Select)
        m_tool = ToolType::Lasso;
    const ImageItem item = makePlacedImage(
        image, screenToWorld(QPointF(width() / 2.0, height() / 2.0)), size(), m_doc.viewport().zoom);
    m_doc.images().append(item);
    m_selectedImageId = item.id;
    invalidateStaticCache();
    saveAutosave();
    updateCursorShape();
    emit stateChanged();
    emit toastRequested(QStringLiteral("Imagen insertada ✓"));
    return true;
}

bool CanvasWidget::importPdfFile(const QString& path, bool newNotebook) {
    if (m_mode != InteractionMode::None)
        return false;
    const auto imported = importPdfPages(path, 0.0);
    if (imported.isEmpty()) {
        emit toastRequested(tr("No se pudo importar el PDF"));
        return false;
    }
    if (!preparePageChange())
        return false;
    if (newNotebook)
        newDocument();
    m_doc.insertPdfPages(imported, true);
    afterPageLoaded(true);
    emit toastRequested(tr("PDF importado · %1 páginas").arg(imported.size()));
    return true;
}

bool CanvasWidget::exportToPdf(const QString& path) {
    finishTextEditor(true);
    if (m_polylineActive)
        finishActivePolyline();
    if (m_mode != InteractionMode::None)
        return false;
    flushPendingBeautify();
    const bool saved = m_doc.exportToPdf(path);
    if (saved)
        emit toastRequested(QStringLiteral("PDF exportado ✓"));
    return saved;
}

ShapeFamily CanvasWidget::selectedShapeFamily(int* outVariant) const {
    if (outVariant)
        *outVariant = 0;
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    const ShapeItem* shape = id.isEmpty() ? nullptr : findShapeById(id);
    return shape ? classifyShapeFamily(*shape, outVariant) : ShapeFamily::None;
}

bool CanvasWidget::applySelectedShapeVariant(ShapeFamily family, int variant) {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return false;
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    if (id.isEmpty())
        return false;
    auto& shapes = m_doc.shapes();
    qsizetype found = -1;
    for (qsizetype i = 0; i < shapes.size(); ++i) {
        if (shapes.at(i).id == id) {
            found = i;
            break;
        }
    }
    if (found < 0 || shapes.at(found).locked)
        return false;
    const ShapeItem oldShape = shapes.at(found);
    ShapeItem updated = oldShape;
    if (!morphShapeToVariant(updated, family, variant))
        return false;
    const DocumentSnapshot before = currentSnapshot();
    shapes[found] = updated;
    updateShapeLinks(shapes, before.shapes, {id});
    syncAutoAnglesAfterShapeChange(oldShape, updated);
    pushUndoSnapshot(before);
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    return true;
}

void CanvasWidget::syncAutoAnglesAfterShapeChange(const ShapeItem& oldShape, const ShapeItem& newShape) {
    auto polyFor = [](const ShapeItem& sh) {
        QVector<QPointF> p = sh.vertices;
        if (sh.kind == ShapeKind::Rectangle && p.size() == 2) {
            const QRectF r = QRectF(p[0], p[1]).normalized();
            p = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
        }
        return p;
    };
    const QVector<QPointF> oldPoly = polyFor(oldShape);
    if (oldPoly.size() < 3)
        return;

    const double zoom = std::max(0.25, m_doc.viewport().zoom);
    const double tol = 6.0 / zoom;
    double savedArm = 42.0 / zoom;
    QSet<QString> oldAngleIds;
    for (const ShapeItem& s : m_doc.shapes()) {
        if (!isAutoVertexAngle(s) || s.vertices.isEmpty())
            continue;
        bool match = false;
        for (const auto& att : s.attachments) {
            if (att.targetId == oldShape.id) {
                match = true;
                break;
            }
        }
        if (!match) {
            for (const QPointF& v : oldPoly) {
                if (pointDistance(s.vertices.front(), v) <= tol) {
                    match = true;
                    break;
                }
            }
        }
        if (match) {
            oldAngleIds.insert(s.id);
            if (s.vertices.size() >= 2) {
                const double d = pointDistance(s.vertices[0], s.vertices[1]);
                if (d > 1e-3)
                    savedArm = d;
            }
        }
    }
    if (oldAngleIds.isEmpty())
        return;

    m_doc.shapes().removeIf([&](const ShapeItem& s) { return oldAngleIds.contains(s.id); });
    m_selectedShapeIds.subtract(oldAngleIds);

    const QVector<QPointF> newPoly = polyFor(newShape);
    if (newPoly.size() < 3)
        return;

    const qsizetype n = newPoly.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QPointF prev = newPoly[(i - 1 + n) % n];
        const QPointF curr = newPoly[i];
        const QPointF next = newPoly[(i + 1) % n];
        const double l1 = pointDistance(curr, prev);
        const double l2 = pointDistance(curr, next);
        if (l1 < 1e-3 || l2 < 1e-3)
            continue;
        const double arm = std::min({savedArm, l1 * 0.45, l2 * 0.45});
        ShapeItem marker;
        marker.id = freshId();
        marker.kind = ShapeKind::AngleMarker;
        marker.label = autoVertexAngleLabel();
        marker.closed = false;
        marker.color = newShape.color;
        marker.size = std::clamp(newShape.size * 0.85, 1.4, 3.2);
        marker.opacity = newShape.opacity;
        marker.attachments = {ShapeAttachment{0, newShape.id, 0, 0.0}};
        marker.vertices = {
            curr,
            curr + ((prev - curr) / l1) * arm,
            curr + ((next - curr) / l2) * arm
        };
        marker.bounds = computeShapeBounds(marker);
        m_doc.shapes().append(std::move(marker));
    }
}

double CanvasWidget::selectedCircleSweepDegrees() const {
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    const ShapeItem* shape = id.isEmpty() ? nullptr : findShapeById(id);
    if (!shape)
        return 360.0;
    if (shape->kind == ShapeKind::Sector && shape->vertices.size() >= 3) {
        const QPointF c = shape->vertices[0];
        const QPointF r1 = shape->vertices[1] - c;
        const QPointF r2 = shape->vertices[2] - c;
        const double a1 = std::atan2(-r1.y(), r1.x());
        const double a2 = std::atan2(-r2.y(), r2.x());
        double deg = (a2 - a1) * 180.0 / M_PI;
        while (deg <= 0.0)
            deg += 360.0;
        while (deg > 360.0)
            deg -= 360.0;
        return std::clamp(deg, 15.0, 360.0);
    }
    return 360.0;
}

bool CanvasWidget::setSelectedCircleSweepDegrees(double degrees) {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return false;
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    if (id.isEmpty())
        return false;
    auto& shapes = m_doc.shapes();
    qsizetype found = -1;
    for (qsizetype i = 0; i < shapes.size(); ++i) {
        if (shapes.at(i).id == id) {
            found = i;
            break;
        }
    }
    if (found < 0 || shapes.at(found).locked)
        return false;
    ShapeItem updated = shapes.at(found);
    if (updated.kind != ShapeKind::Circle && updated.kind != ShapeKind::Sector
        && updated.kind != ShapeKind::Ellipse) {
        return false;
    }
    QPointF center;
    double rad = 40.0;
    double startAngle = 0.0;
    if ((updated.kind == ShapeKind::Circle || updated.kind == ShapeKind::Sector)
        && updated.vertices.size() >= 2) {
        center = updated.vertices[0];
        const QPointF ray = updated.vertices[1] - center;
        rad = std::max(4.0, std::hypot(ray.x(), ray.y()));
        startAngle = std::atan2(-ray.y(), ray.x());
    } else if (updated.vertices.size() >= 2) {
        const QRectF r = QRectF(updated.vertices[0], updated.vertices[1]).normalized();
        center = r.center();
        rad = std::max(4.0, (r.width() + r.height()) * 0.25);
    } else {
        return false;
    }
    const double clamped = std::clamp(degrees, 15.0, 360.0);
    if (clamped >= 359.0) {
        updated.kind = ShapeKind::Circle;
        updated.closed = true;
        updated.vertices = {center, center + QPointF(rad * std::cos(startAngle), -rad * std::sin(startAngle))};
    } else {
        const double endAngle = startAngle + clamped * M_PI / 180.0;
        updated.kind = ShapeKind::Sector;
        updated.closed = true;
        updated.vertices = {
            center,
            center + QPointF(rad * std::cos(startAngle), -rad * std::sin(startAngle)),
            center + QPointF(rad * std::cos(endAngle), -rad * std::sin(endAngle)),
        };
    }
    updated.bounds = computeShapeBounds(updated);
    const DocumentSnapshot before = currentSnapshot();
    shapes[found] = std::move(updated);
    updateShapeLinks(shapes, before.shapes, {id});
    pushUndoSnapshot(before);
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    return true;
}

bool CanvasWidget::setSelectedShapeFillColor(const QColor& color) {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return false;
    if (selectedItemIds().isEmpty())
        return false;
    SelectionStyle style;
    if (!color.isValid() || color.alpha() == 0) {
        style.filled = false;
    } else {
        style.filled = true;
        style.fillColor = color;
        if (!selectionStyle().fillPattern)
            style.fillPattern = m_shadePattern;
    }
    setSelectionStyle(style);
    return true;
}

bool CanvasWidget::selectedShapeHasAngles() const {
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    const ShapeItem* base = id.isEmpty() ? nullptr : findShapeById(id);
    if (!base)
        return false;
    QVector<QPointF> poly = base->vertices;
    if (base->kind == ShapeKind::Rectangle && poly.size() == 2) {
        const QRectF r = QRectF(poly[0], poly[1]).normalized();
        poly = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    }
    if (poly.size() < 3)
        return false;
    const double tol = 6.0 / std::max(0.25, m_doc.viewport().zoom);
    for (const ShapeItem& s : m_doc.shapes()) {
        if (!isAutoVertexAngle(s) || s.vertices.isEmpty())
            continue;
        for (const QPointF& v : poly) {
            if (pointDistance(s.vertices.front(), v) <= tol)
                return true;
        }
    }
    return false;
}

double CanvasWidget::selectedShapeAnglesRadius() const {
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    const ShapeItem* base = id.isEmpty() ? nullptr : findShapeById(id);
    if (!base)
        return 42.0;
    QVector<QPointF> poly = base->vertices;
    if (base->kind == ShapeKind::Rectangle && poly.size() == 2) {
        const QRectF r = QRectF(poly[0], poly[1]).normalized();
        poly = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    }
    if (poly.size() < 3)
        return 42.0;
    const double zoom = std::max(0.25, m_doc.viewport().zoom);
    const double tol = 6.0 / zoom;
    for (const ShapeItem& s : m_doc.shapes()) {
        if (!isAutoVertexAngle(s) || s.vertices.size() < 3)
            continue;
        for (const QPointF& v : poly) {
            if (pointDistance(s.vertices[0], v) <= tol)
                return std::clamp(pointDistance(s.vertices[0], s.vertices[1]) * zoom, 14.0, 160.0);
        }
    }
    return 42.0;
}

bool CanvasWidget::setSelectedShapeAnglesRadius(double armPx) {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return false;
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    const ShapeItem* base = id.isEmpty() ? nullptr : findShapeById(id);
    if (!base || base->locked)
        return false;
    QVector<QPointF> poly = base->vertices;
    if (base->kind == ShapeKind::Rectangle && poly.size() == 2) {
        const QRectF r = QRectF(poly[0], poly[1]).normalized();
        poly = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    }
    if (poly.size() < 3)
        return false;
    const double zoom = std::max(0.25, m_doc.viewport().zoom);
    const double tol = 6.0 / zoom;
    const double targetArm = std::clamp(armPx, 14.0, 160.0) / zoom;
    bool changed = false;
    for (ShapeItem& s : m_doc.shapes()) {
        if (!isAutoVertexAngle(s) || s.vertices.size() < 3)
            continue;
        for (qsizetype i = 0; i < poly.size(); ++i) {
            if (pointDistance(s.vertices[0], poly[i]) <= tol) {
                const QPointF d1 = s.vertices[1] - s.vertices[0];
                const QPointF d2 = s.vertices[2] - s.vertices[0];
                const double l1 = vecLength(d1);
                const double l2 = vecLength(d2);
                if (l1 > 1e-4 && l2 > 1e-4) {
                    const QPointF prev = poly[(i - 1 + poly.size()) % poly.size()];
                    const QPointF next = poly[(i + 1) % poly.size()];
                    const double maxArm = std::min(pointDistance(poly[i], prev), pointDistance(poly[i], next)) * 0.48;
                    const double arm = std::min(targetArm, std::max(4.0 / zoom, maxArm));
                    s.vertices[1] = s.vertices[0] + (d1 / l1) * arm;
                    s.vertices[2] = s.vertices[0] + (d2 / l2) * arm;
                    s.bounds = computeShapeBounds(s);
                    changed = true;
                }
                break;
            }
        }
    }
    if (!changed)
        return false;
    invalidateStaticCache();
    saveAutosave();
    update();
    return true;
}

bool CanvasWidget::toggleSelectedShapeAngles() {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return false;
    const QString id = singleSelectedShapeId(m_selectedShapeId, m_selectedShapeIds);
    const ShapeItem* base = id.isEmpty() ? nullptr : findShapeById(id);
    if (!base || base->locked)
        return false;

    QVector<QPointF> poly = base->vertices;
    if (base->kind == ShapeKind::Rectangle && poly.size() == 2) {
        const QRectF r = QRectF(poly[0], poly[1]).normalized();
        poly = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    }
    if (poly.size() < 3)
        return false;

    const double tol = 6.0 / std::max(0.25, m_doc.viewport().zoom);
    QSet<QString> existingIds;
    for (const ShapeItem& s : m_doc.shapes()) {
        if (!isAutoVertexAngle(s) || s.vertices.isEmpty())
            continue;
        for (const QPointF& v : poly) {
            if (pointDistance(s.vertices.front(), v) <= tol) {
                existingIds.insert(s.id);
                break;
            }
        }
    }

    if (!existingIds.isEmpty()) {
        pushUndoSnapshot(currentSnapshot());
        m_doc.shapes().removeIf([&](const ShapeItem& s) { return existingIds.contains(s.id); });
        m_selectedShapeIds.subtract(existingIds);
        invalidateStaticCache();
        saveAutosave();
        emit stateChanged();
        return true;
    }

    QVector<ShapeItem> added;
    const qsizetype n = poly.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QPointF prev = poly[(i - 1 + n) % n];
        const QPointF curr = poly[i];
        const QPointF next = poly[(i + 1) % n];
        const double l1 = pointDistance(curr, prev);
        const double l2 = pointDistance(curr, next);
        if (l1 < 1e-3 || l2 < 1e-3)
            continue;
        const double arm = std::min({42.0 / std::max(0.25, m_doc.viewport().zoom), l1 * 0.45, l2 * 0.45});
        ShapeItem marker;
        marker.id = freshId();
        marker.kind = ShapeKind::AngleMarker;
        marker.label = autoVertexAngleLabel();
        marker.closed = false;
        marker.color = base->color;
        marker.size = autoAngleStrokeWidth(base->size);
        marker.opacity = base->opacity;
        marker.attachments = {ShapeAttachment{0, base->id, 0, 0.0}};
        marker.vertices = {
            curr,
            curr + ((prev - curr) / l1) * arm,
            curr + ((next - curr) / l2) * arm
        };
        marker.bounds = computeShapeBounds(marker);
        added.append(std::move(marker));
    }
    if (added.isEmpty())
        return false;
    pushUndoSnapshot(currentSnapshot());
    m_doc.shapes().append(added);
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    return true;
}

double CanvasWidget::selectedImageOpacity() const {
    const QString targetId = !m_selectedImageId.isEmpty()
        ? m_selectedImageId
        : (m_selectedImageIds.size() == 1 ? *m_selectedImageIds.cbegin() : QString());
    if (targetId.isEmpty())
        return 1.0;
    for (const ImageItem& img : m_doc.images()) {
        if (img.id == targetId)
            return std::clamp(img.opacity, 0.1, 1.0);
    }
    return 1.0;
}

bool CanvasWidget::setSelectedImageOpacity(double opacity) {
    const QString targetId = !m_selectedImageId.isEmpty()
        ? m_selectedImageId
        : (m_selectedImageIds.size() == 1 ? *m_selectedImageIds.cbegin() : QString());
    if (targetId.isEmpty())
        return false;
    const double clamped = std::clamp(opacity, 0.1, 1.0);
    for (ImageItem& img : m_doc.images()) {
        if (img.id == targetId && !img.isPdfPage) {
            if (img.locked)
                return false;
            if (std::abs(img.opacity - clamped) < 1e-3)
                return true;
            pushUndoSnapshot(currentSnapshot());
            img.opacity = clamped;
            invalidateStaticCache();
            saveAutosave();
            emit stateChanged();
            return true;
        }
    }
    return false;
}

bool CanvasWidget::cropSelectedImageToRect(const QRectF& normalizedRect) {
    const QString targetId = !m_selectedImageId.isEmpty()
        ? m_selectedImageId
        : (m_selectedImageIds.size() == 1 ? *m_selectedImageIds.cbegin() : QString());
    if (targetId.isEmpty())
        return false;
    const QRectF norm = normalizedRect.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (norm.width() < 0.02 || norm.height() < 0.02)
        return false;
    for (ImageItem& img : m_doc.images()) {
        if (img.id == targetId && !img.isPdfPage) {
            if (img.locked || img.image.isNull())
                return false;
            const int iw = img.image.width();
            const int ih = img.image.height();
            const int x = std::clamp(int(std::floor(norm.left() * iw)), 0, std::max(0, iw - 2));
            const int y = std::clamp(int(std::floor(norm.top() * ih)), 0, std::max(0, ih - 2));
            const int cw = std::clamp(int(std::lround(norm.width() * iw)), 2, iw - x);
            const int ch = std::clamp(int(std::lround(norm.height() * ih)), 2, ih - y);
            const QRect pxRect(x, y, cw, ch);
            QImage cropped = img.image.copy(pxRect);
            if (cropped.isNull())
                return false;
            const QRectF newRect(
                img.rect.left() + (double(x) / std::max(1, iw)) * img.rect.width(),
                img.rect.top() + (double(y) / std::max(1, ih)) * img.rect.height(),
                (double(cw) / std::max(1, iw)) * img.rect.width(),
                (double(ch) / std::max(1, ih)) * img.rect.height());
            pushUndoSnapshot(currentSnapshot());
            img.image = std::move(cropped);
            img.rect = newRect;
            m_lassoBounds = lassoSelectionBounds();
            invalidateStaticCache();
            saveAutosave();
            emit stateChanged();
            return true;
        }
    }
    return false;
}

bool CanvasWidget::cropSelectedImageInteractiveOrRatio(double aspectRatio) {
    const QString targetId = !m_selectedImageId.isEmpty()
        ? m_selectedImageId
        : (m_selectedImageIds.size() == 1 ? *m_selectedImageIds.cbegin() : QString());
    if (targetId.isEmpty())
        return false;
    for (const ImageItem& img : m_doc.images()) {
        if (img.id != targetId || img.isPdfPage || img.locked || img.image.isNull())
            continue;
        if (aspectRatio <= 0.0)
            return cropSelectedImageToRect(QRectF(0.08, 0.08, 0.84, 0.84));
        const double curAspect = double(img.image.width()) / std::max(1, img.image.height());
        if (curAspect > aspectRatio) {
            const double nw = std::clamp(aspectRatio / curAspect, 0.05, 1.0);
            return cropSelectedImageToRect(QRectF((1.0 - nw) * 0.5, 0.0, nw, 1.0));
        }
        const double nh = std::clamp(curAspect / aspectRatio, 0.05, 1.0);
        return cropSelectedImageToRect(QRectF(0.0, (1.0 - nh) * 0.5, 1.0, nh));
    }
    return false;
}

InlineTextState CanvasWidget::inlineTextState() const {
    InlineTextState st;
    if (m_textEditor) {
        st.active = true;
        st.fontFamilyIndex = m_textEditor->draft.fontFamilyIndex;
        st.fontSize = m_textEditor->draft.fontSize;
        st.bold = m_textEditor->draft.bold;
        st.italic = m_textEditor->draft.italic;
        st.screenRect = m_textEditor->geometry();
        return st;
    }
    const QString tid = !m_selectedTextId.isEmpty()
        ? m_selectedTextId
        : (m_selectedTextIds.size() == 1 ? *m_selectedTextIds.cbegin() : QString());
    if (!tid.isEmpty()) {
        for (const TextItem& item : m_doc.texts()) {
            if (item.id == tid) {
                st.active = false;
                st.fontFamilyIndex = item.fontFamilyIndex;
                st.fontSize = item.fontSize;
                st.bold = item.bold;
                st.italic = item.italic;
                return st;
            }
        }
    }
    return st;
}

bool CanvasWidget::setInlineTextStyle(int familyIdx, double fontSize, bool bold, bool italic) {
    const int fam = std::clamp(familyIdx, 0, 2);
    const double sz = std::clamp(fontSize, 9.0, 96.0);
    m_lastTextFontSize = sz;
    writeAppSetting(QStringLiteral("text/fontSize"), sz);
    if (m_textEditor) {
        m_textEditor->draft.fontFamilyIndex = fam;
        m_textEditor->draft.fontSize = sz;
        m_textEditor->draft.bold = bold;
        m_textEditor->draft.italic = italic;
        const ThemeConfig theme = effectiveThemeConfig(m_doc.themeId(), m_doc.customBackground());
        m_textEditor->applyTextStyle(m_doc.viewport().zoom, m_textEditor->draft.color, theme.accent);
        emit inlineTextEditorChanged(true, m_textEditor->geometry());
        return true;
    }
    QSet<QString> ids = m_selectedTextIds;
    if (!m_selectedTextId.isEmpty())
        ids.insert(m_selectedTextId);
    if (ids.isEmpty())
        return false;
    bool touched = false;
    DocumentSnapshot before = currentSnapshot();
    for (TextItem& item : m_doc.texts()) {
        if (!ids.contains(item.id) || item.locked)
            continue;
        item.fontFamilyIndex = fam;
        item.fontSize = sz;
        item.bold = bold;
        item.italic = italic;
        item.bounds = textBounds(item);
        touched = true;
    }
    if (!touched)
        return false;
    pushUndoSnapshot(before);
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    return true;
}

} // namespace finily
