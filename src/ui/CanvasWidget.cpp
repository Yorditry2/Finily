#include "CanvasWidget.h"
#include "CanvasWidgetInternal.h"
#include "engine/DocumentSerializer.h"
#include "engine/InkEngine.h"
#include "engine/GeometryEngine.h"
#include "engine/HandwritingEngine.h"
#include "engine/LassoEngine.h"
#include "engine/PdfEngine.h"
#include <QSettings>
#include <QBuffer>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <QUrl>
#include <QHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QFontMetricsF>
#include <QFocusEvent>
#include <functional>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPainter>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTabletEvent>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace finily {
namespace {
QString congruenceMarkToast(const QString& label) {
    if (label == QStringLiteral("mark:zigzag"))
        return QStringLiteral("Serrucho ✓");
    if (label == QStringLiteral("mark:spring"))
        return QStringLiteral("Resorte ✓");
    if (label == QStringLiteral("mark:dot"))
        return QStringLiteral("Bolita ✓");
    if (label == QStringLiteral("mark:bar"))
        return QStringLiteral("Bultito ✓");
    if (label.startsWith(QStringLiteral("mark:tick")))
        return QStringLiteral("Rayitas ✓");
    if (label.startsWith(QStringLiteral("mark:parallel")))
        return QStringLiteral("Paralelas ✓");
    return QStringLiteral("Marca ✓");
}
constexpr auto objectMimeType = "application/x-finily-objects";
bool linkedFillOf(const ShapeItem& fill, const QString& id) {
    return fill.kind == ShapeKind::ShadedRegion && fill.shadeBoundary.size() == 1
        && fill.shadeBoundary.contains(id);
}
StrokePoint inputPoint(QPointF p,double pressure,double tiltX=0.0,double tiltY=0.0) { return {p.x(),p.y(),std::clamp(pressure,0.0,1.0),nowMs(),std::clamp(tiltX,-60.0,60.0),std::clamp(tiltY,-60.0,60.0)}; }
StrokePoint snappedPoint(const StrokePoint& origin,StrokePoint point) {
    const double dx=point.x-origin.x,dy=point.y-origin.y;
    const double angle=std::round(std::atan2(dy,dx)/(std::numbers::pi/12))*(std::numbers::pi/12);
    const double dist=std::hypot(dx,dy);
    point.x=origin.x+dist*std::cos(angle);point.y=origin.y+dist*std::sin(angle);return point;
}
// Polígono regular inscrito: centro en `origin`, radio hasta `end`; el primer
// vértice apunta hacia donde arrastras (así se puede girar al crearlo).
QVector<QPointF> regularPolygon(QPointF center,QPointF end,int sides) {
    const QPointF d=end-center;const double radius=std::hypot(d.x(),d.y());
    const double start=radius<1e-6?-std::numbers::pi/2:std::atan2(d.y(),d.x());
    QVector<QPointF> out;out.reserve(sides);
    for(int i=0;i<sides;++i){const double a=start+2.0*std::numbers::pi*i/sides;out.append(center+QPointF(std::cos(a),std::sin(a))*radius);}
    return out;
}
void createShapeVertices(ShapeItem& shape,QPointF origin,QPointF end,int polygonSides=6,int quadVariant=0) {
    const QRectF r=QRectF(origin,end).normalized();
    switch(shape.kind) {
    case ShapeKind::Polyline:
        if(quadVariant==1) // Paralelogramo
            shape.vertices={{r.left()+r.width()*.22,r.top()},r.topRight(),{r.right()-r.width()*.22,r.bottom()},r.bottomLeft()};
        else if(quadVariant==2) // Trapecio isósceles
            shape.vertices={{r.left()+r.width()*.20,r.top()},{r.right()-r.width()*.20,r.top()},r.bottomRight(),r.bottomLeft()};
        else if(quadVariant==3) // Trapecio rectángulo
            shape.vertices={r.topLeft(),{r.right()-r.width()*.28,r.top()},r.bottomRight(),r.bottomLeft()};
        else if(quadVariant==4) // Trapezoide asimétrico
            shape.vertices={{r.left()+r.width()*.18,r.top()},{r.right()-r.width()*.08,r.top()+r.height()*.22},r.bottomRight(),{r.left(),r.bottom()-r.height()*.08}};
        else
            shape.vertices=regularPolygon(origin,end,polygonSides);
        break;
    case ShapeKind::Line:case ShapeKind::Circle:case ShapeKind::Brace:shape.vertices={origin,end};break;
    case ShapeKind::Ellipse:case ShapeKind::Rectangle:case ShapeKind::Axes:case ShapeKind::Solid3D:shape.vertices={r.topLeft(),r.bottomRight()};break;
    case ShapeKind::Triangle:shape.vertices={{r.center().x(),r.top()},r.bottomRight(),r.bottomLeft()};break;
    case ShapeKind::Radical:shape.vertices={{r.left()+r.width()*.22,r.bottom()},{r.left()+r.width()*.42,r.top()},r.topRight()};break;
    case ShapeKind::RightAngle:shape.vertices={{end.x(),origin.y()},origin,{origin.x(),end.y()}};break;
    case ShapeKind::Curve:shape.vertices={origin, (origin+end)*0.5 + QPointF(0, -std::max(24.0, distance(origin,end)*0.25)), end};break;
    case ShapeKind::Arc:
    case ShapeKind::Sector:
    case ShapeKind::AngleMarker: {
        const QPointF ray = end - origin;
        const double angle = shape.kind == ShapeKind::AngleMarker ? std::numbers::pi / 4 : std::numbers::pi / 2;
        const QPointF second(ray.x() * std::cos(angle) + ray.y() * std::sin(angle),
                            ray.y() * std::cos(angle) - ray.x() * std::sin(angle));
        shape.vertices = {origin, end, origin + second};
        break;
    }
    case ShapeKind::ShadedRegion:break;
    }
    shape.closed=shape.kind==ShapeKind::Circle||shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Rectangle||shape.kind==ShapeKind::Triangle||shape.kind==ShapeKind::Polyline||shape.kind==ShapeKind::Sector;
    shape.bounds=computeShapeBounds(shape);
}
bool drawsFromEndpoints(ShapeKind kind) {
    return kind == ShapeKind::Line || kind == ShapeKind::Curve || kind == ShapeKind::Brace;
}
std::optional<QPointF> handleAnchor(const ShapeItem& shape,int index) {
    const auto handles=getShapeHandles(shape);
    if(handles.size()==4&&(shape.kind==ShapeKind::Rectangle||shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Axes))return handles[(index+2)%4];
    if(shape.kind==ShapeKind::Circle&&index==1)return shape.vertices[0];
    if(shape.kind==ShapeKind::Line&&index>=0&&index<2)return shape.vertices[1-index];
    if(shape.kind==ShapeKind::Polyline&&index>0&&index<shape.vertices.size())return shape.vertices[index-1];
    if(shape.kind==ShapeKind::Radical&&index==2)return shape.vertices[1];
    return std::nullopt;
}

}
CanvasWidget::CanvasWidget(QWidget* parent):QWidget(parent),m_stabilizerFilter(std::make_unique<VelocityGaussianStabilizer>()) {
    setAttribute(Qt::WA_OpaquePaintEvent);setAttribute(Qt::WA_StaticContents);
    setAttribute(Qt::WA_TabletTracking);setMouseTracking(true);setAcceptDrops(true);setFocusPolicy(Qt::StrongFocus);
    m_stabilizerFilter->setStrength(m_stabilizer);
    m_holdTimer=new QTimer(this);m_holdTimer->setSingleShot(true);m_holdTimer->setInterval(380);
    connect(m_holdTimer,&QTimer::timeout,this,&CanvasWidget::triggerHoldToShape);
    m_laserTimer=new QTimer(this);m_laserTimer->setInterval(16);
    connect(m_laserTimer,&QTimer::timeout,this,[this] {
        const auto now=nowMs();m_laserTrail.removeIf([now](const LaserSample& sample){return now-sample.timestampMs>=1400;});
        if(m_laserTrail.isEmpty())m_laserTimer->stop();
        update();
    });
    m_zoomAnimation=new QVariantAnimation(this);
    m_zoomAnimation->setDuration(140);m_zoomAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_zoomAnimation->setStartValue(0.0);m_zoomAnimation->setEndValue(1.0);
    connect(m_zoomAnimation,&QVariantAnimation::valueChanged,this,[this](const QVariant& value) {
        const double t=value.toDouble();
        m_viewport={m_zoomStart.x+(m_zoomTarget.x-m_zoomStart.x)*t,
            m_zoomStart.y+(m_zoomTarget.y-m_zoomStart.y)*t,
            m_zoomStart.zoom+(m_zoomTarget.zoom-m_zoomStart.zoom)*t};
        invalidateStaticCache();emit viewportChanged(m_viewport.zoom);
    });
    m_shapePulseAnimation=new QVariantAnimation(this);
    m_shapePulseAnimation->setDuration(220);
    m_shapePulseAnimation->setStartValue(1.0);m_shapePulseAnimation->setEndValue(0.0);
    connect(m_shapePulseAnimation,&QVariantAnimation::valueChanged,this,[this](const QVariant& value) {
        m_shapePulseAlpha=value.toDouble();update(shapePulseRect());
    });
    // Autoguardado diferido: se agrupan los cambios y se escribe 1.5 s después
    // del último, nunca en mitad de un trazo.
    m_autosaveTimer=new QTimer(this);m_autosaveTimer->setSingleShot(true);m_autosaveTimer->setInterval(1500);
    connect(m_autosaveTimer,&QTimer::timeout,this,[this] {
        if(m_mode!=InteractionMode::None){m_autosaveTimer->start();return;}
        writeAutosaveNow();
    });
    m_beautifyTimer=new QTimer(this);m_beautifyTimer->setSingleShot(true);m_beautifyTimer->setInterval(650);
    connect(m_beautifyTimer,&QTimer::timeout,this,&CanvasWidget::flushPendingBeautify);
    {
        QSettings settings("Finily","Finily");
        m_mathBeautifyEnabled=settings.value("ink/mathBeautify",false).toBool();
        m_autoColorOperators=settings.value("ink/autoColorOperators",true).toBool();
        const QColor savedOp(settings.value("ink/operatorColor","#ef4444").toString());
        if(savedOp.isValid())m_operatorColor=savedOp;
        m_cursorStyle=std::clamp(settings.value("ink/cursorStyle",0).toInt(),0,2);
        m_penTip=std::clamp(settings.value("ink/penTip",1).toInt(),0,3);
        m_highlighterTip=std::clamp(settings.value("ink/highlighterTip",0).toInt(),0,2);
        m_eraserPreserveShapes=settings.value("ink/eraserPreserveShapes",false).toBool();
        m_eraserOnlyHighlight=settings.value("ink/eraserOnlyHighlight",false).toBool();
        m_pressureSensitivity=std::clamp(settings.value("ink/pressureSensitivity",0.5).toDouble(),0.0,1.0);
        m_highlighterOpacity=std::clamp(settings.value("ink/highlighterOpacity",0.25).toDouble(),0.10,0.70);
    }
    m_pages.append(PageData{});loadAutosave();updateCursorShape();
}
CanvasWidget::~CanvasWidget() {
    finishTextEditor(this,false);
    m_autosaveTimer->stop();
    if(m_autosaveFuture.valid())m_autosaveFuture.wait();
    writeAutosaveNow(false);
}
void CanvasWidget::setTool(ToolType tool) {
    clearPenCursor();
    m_radialOpen=false;
    finishTextEditor(this,true);
    if(m_tool==ToolType::Select&&tool!=m_tool&&m_mode!=InteractionMode::None)cancelInteraction();
    if(m_polylineActive&&tool!=m_tool)finishActivePolyline();
    if((hasLassoSelection()||hasSelectedImage())&&tool!=ToolType::Lasso&&tool!=ToolType::Select)clearSelection();
    if(tool==ToolType::Lasso&&m_tool!=ToolType::Lasso)clearSelection();
    if(tool==ToolType::Select&&m_tool==ToolType::Lasso&&hasLassoSelection())clearSelection();
    if(tool==ToolType::Pen||tool==ToolType::Ballpoint||tool==ToolType::Highlighter)m_lastWriteTool=tool;
    m_tool=tool;m_showEraserCursor=false;
    if (tool == ToolType::Polyline && m_linePattern == LinePattern::Dimension)
        m_linePattern = LinePattern::Solid;
    if(tool!=ToolType::Text&&tool!=ToolType::Lasso&&tool!=ToolType::Select&&tool!=ToolType::Shape&&tool!=ToolType::Polyline&&tool!=ToolType::Pen&&tool!=ToolType::Ballpoint)clearSelection();
    updateCursorShape();update();emit stateChanged();
}
void CanvasWidget::setShapeKind(ShapeKind kind) { m_shapeKind=kind;emit stateChanged(); }
void CanvasWidget::setPolygonSides(int sides) { m_polygonSides=std::clamp(sides,3,12);emit stateChanged(); }
void CanvasWidget::setShadePattern(ShadePattern pattern) {
    m_shadePattern = pattern;
    emit stateChanged();
}
void CanvasWidget::setHoldToShapeEnabled(bool enabled) { m_holdToShapeEnabled=enabled;if(!enabled)m_holdTimer->stop();emit stateChanged(); }
void CanvasWidget::setSmartGuidesEnabled(bool enabled) { m_smartGuidesEnabled=enabled;if(!enabled)m_activeGuides.clear();update();emit stateChanged(); }
void CanvasWidget::setMathBeautifyEnabled(bool enabled) {
    if(m_mathBeautifyEnabled==enabled)return;
    if(!enabled)flushPendingBeautify();
    m_mathBeautifyEnabled=enabled;
    QSettings("Finily","Finily").setValue("ink/mathBeautify",enabled);
    emit toastRequested(enabled ? QStringLiteral("Auto-embellecer escritura activado (M)")
                                : QStringLiteral("Modo Escritura libre (M)"));
    emit stateChanged();
}
void CanvasWidget::setAutoColorOperators(bool enabled) {
    if(m_autoColorOperators==enabled)return;
    m_autoColorOperators=enabled;
    QSettings("Finily","Finily").setValue("ink/autoColorOperators",enabled);
    emit stateChanged();
}
void CanvasWidget::setOperatorColor(const QColor& color) {
    if(!color.isValid()||m_operatorColor==color)return;
    m_operatorColor=color;
    QSettings("Finily","Finily").setValue("ink/operatorColor",color.name(QColor::HexRgb));
    emit stateChanged();
}
void CanvasWidget::setCursorStyle(int style) {
    style=std::clamp(style,0,2);
    if(m_cursorStyle==style)return;
    m_cursorStyle=style;
    QSettings("Finily","Finily").setValue("ink/cursorStyle",style);
    clearPenCursor();
    emit stateChanged();
}
void CanvasWidget::setPenTip(int tip) {
    tip=std::clamp(tip,0,3);
    m_penTip=tip;
    if(m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint) {
        m_tool=(tip==0)?ToolType::Ballpoint:ToolType::Pen;
        m_lastWriteTool=m_tool;
    }
    QSettings("Finily","Finily").setValue("ink/penTip",tip);
    emit stateChanged();
}
void CanvasWidget::setHighlighterTip(int tip) {
    tip=std::clamp(tip,0,2);
    if(m_highlighterTip==tip)return;
    m_highlighterTip=tip;
    QSettings("Finily","Finily").setValue("ink/highlighterTip",tip);
    emit stateChanged();
}
void CanvasWidget::setLassoRectMode(bool rectMode) {
    if(m_lassoRectMode==rectMode)return;
    m_lassoRectMode=rectMode;
    emit stateChanged();
}
bool CanvasWidget::hasSelection() const {
    return hasLassoSelection() || hasSelectedShape() || hasSelectedImage() || hasSelectedText();
}

bool CanvasWidget::hasLockableSelection() const {
    return hasSelectedShape() || hasSelectedImage() || hasSelectedText()
        || !m_selectedShapeIds.isEmpty() || !m_selectedImageIds.isEmpty() || !m_selectedTextIds.isEmpty();
}

QRectF CanvasWidget::selectionScreenRect() const {
    QRectF worldRect;
    if (hasLassoSelection() && !m_lassoBounds.isEmpty()) {
        worldRect = m_lassoBounds;
    } else if (const auto* s = findShapeById(m_selectedShapeId)) {
        worldRect = s->bounds;
    } else if (!m_selectedImageId.isEmpty()) {
        for (const auto& img : m_images)
            if (img.id == m_selectedImageId) { worldRect = img.rect; break; }
    } else if (!m_selectedTextId.isEmpty()) {
        for (const auto& t : m_texts)
            if (t.id == m_selectedTextId) { worldRect = t.bounds; break; }
    } else if (m_polylineActive && !m_polylineVertices.isEmpty()) {
        worldRect = computeShapeBounds(m_activeShape);
    }
    if (worldRect.isEmpty())
        return {};
    const QPointF off(m_viewport.x, m_viewport.y);
    return QRectF(worldRect.topLeft() * m_viewport.zoom + off,
                  worldRect.bottomRight() * m_viewport.zoom + off).normalized();
}
void CanvasWidget::setSwapPenButtons(bool swapped) { m_swapPenButtons=swapped;saveAutosave();emit stateChanged(); }
ShapeItem* CanvasWidget::findShapeById(const QString& id) {
    if(id.isEmpty())return nullptr;
    if(m_hasActiveShape&&m_activeShape.id==id)return &m_activeShape;
    for(auto& shape:m_shapes)if(shape.id==id)return &shape;
    return nullptr;
}
const ShapeItem* CanvasWidget::findShapeById(const QString& id) const {
    if(id.isEmpty())return nullptr;
    if(m_hasActiveShape&&m_activeShape.id==id)return &m_activeShape;
    for(const auto& shape:m_shapes)if(shape.id==id)return &shape;
    return nullptr;
}
void CanvasWidget::clearSelection() {
    clearPenCursor();
    m_selectedTextId.clear();m_selectedShapeId.clear();m_activeHandleIndex=-1;m_selectedStrokeIds.clear();m_selectedShapeIds.clear();
    m_selectedTextIds.clear();m_selectedImageIds.clear();m_linkedPreview=false;
    m_selectedImageId.clear();m_lassoPolygon.clear();m_lassoBounds={};m_lassoPreviewDelta=QPointF();m_lassoHandleIndex=-1;m_imageHandleIndex=-1;
    invalidateStaticCache();emit stateChanged();
}
void CanvasWidget::cancelInteraction() {
    if (m_polylineActive) {
        cancelActivePolyline();
        return;
    }
    finishTextEditor(this,false);
    const DocumentSnapshot* snapshot = nullptr;
    switch (m_mode) {
    case InteractionMode::Erase: snapshot = &m_snapshotBeforeErase; break;
    case InteractionMode::ShapeMove:
    case InteractionMode::ShapeDragHandle:
    case InteractionMode::ShapeScale: snapshot = &m_snapshotBeforeShapeEdit; break;
    case InteractionMode::LassoMove:
    case InteractionMode::LassoScale:
    case InteractionMode::LassoRotate: snapshot = &m_snapshotBeforeLassoTransform; break;
    case InteractionMode::TextMove: snapshot = &m_snapshotBeforeTextEdit; break;
    case InteractionMode::ImageMove:
    case InteractionMode::ImageResize: snapshot = &m_snapshotBeforeImageEdit; break;
    default: break;
    }
    if (snapshot) {
        m_strokes=snapshot->strokes;m_shapes=snapshot->shapes;m_images=snapshot->images;m_texts=snapshot->texts;
    }
    m_holdTimer->stop();
    m_radialOpen=false;
    m_liveInk.reset();m_liveInkLayer=QImage();
    m_hasActiveStroke=false;m_hasActiveShape=false;m_holdSnappedToShape=false;m_lassoPreviewDelta=QPointF();
    m_mode=InteractionMode::None;m_erasedAnything=false;m_activeGuides.clear();
    clearSelection();updateCursorShape();
}
int CanvasWidget::pdfPageCount() const { return int(std::count_if(m_images.cbegin(),m_images.cend(),[](const ImageItem& image){return image.isPdfPage;})); }
QSet<QString> CanvasWidget::selectedItemIds() const {
    auto selected = m_selectedStrokeIds | m_selectedShapeIds | m_selectedTextIds | m_selectedImageIds;
    for (const auto& id : {m_selectedShapeId, m_selectedImageId, m_selectedTextId})
        if (!id.isEmpty()) selected.insert(id);
    return selected;
}

QRectF CanvasWidget::lassoSelectionBounds() const {
    auto bounds = computeSelectionBounds(m_strokes, m_shapes, m_selectedStrokeIds, m_selectedShapeIds);
    auto add = [&](const QRectF& rect) { bounds = bounds.isNull() ? rect : bounds.united(rect); };
    for (const auto& text : m_texts) if (m_selectedTextIds.contains(text.id)) add(text.bounds);
    for (const auto& image : m_images) if (m_selectedImageIds.contains(image.id)) add(image.rect);
    return bounds;
}

void CanvasWidget::expandLassoSelection(const QString& seed) {
    auto ids = selectedItemIds();
    if(!seed.isEmpty())ids.insert(seed);
    qsizetype previous;
    do {
        previous = ids.size();
        QSet<QString> groups;
        auto collect = [&](const auto& items) {
            for (const auto& item : items)
                if (ids.contains(item.id) && !item.groupId.isEmpty()) groups.insert(item.groupId);
        };
        collect(m_strokes);collect(m_shapes);collect(m_texts);collect(m_images);
        auto add = [&](const auto& items, QSet<QString>& selected) {
            for (const auto& item : items) {
                if (ids.contains(item.id) || groups.contains(item.groupId)) {
                    selected.insert(item.id);
                    ids.insert(item.id);
                }
            }
        };
        add(m_strokes,m_selectedStrokeIds);add(m_shapes,m_selectedShapeIds);
        add(m_texts,m_selectedTextIds);add(m_images,m_selectedImageIds);
        for (const auto& shape : std::as_const(m_shapes)) {
            if (!m_selectedShapeIds.contains(shape.id)) continue;
            for (auto it = shape.shadeBoundary.keyBegin(); it != shape.shadeBoundary.keyEnd(); ++it) {
                if (std::as_const(*this).findShapeById(*it)) ids.insert(*it);
            }
        }
    } while (ids.size() != previous);
    m_lassoBounds = lassoSelectionBounds();
}

bool CanvasWidget::canUngroupSelection() const {
    const auto ids = selectedItemIds();
    if (ids.isEmpty()) return false;
    auto grouped = [&](const auto& items) {
        return std::any_of(items.cbegin(), items.cend(), [&](const auto& item) {
            return ids.contains(item.id) && !item.groupId.isEmpty();
        });
    };
    return grouped(m_strokes)||grouped(m_shapes)||grouped(m_texts)||grouped(m_images);
}

bool CanvasWidget::canGroupSelection() const {
    const auto ids = selectedItemIds();
    if (ids.size() < 2) return false;
    QSet<QString> groups;
    auto collect = [&](const auto& items) {
        for (const auto& item : items) if (ids.contains(item.id)) groups.insert(item.groupId);
    };
    collect(m_strokes);collect(m_shapes);collect(m_texts);collect(m_images);
    return groups.size() > 1 || groups.contains(QString());
}

void CanvasWidget::changeSelectionGroup(bool group) {
    if (m_mode != InteractionMode::None || (group ? !canGroupSelection() : !canUngroupSelection())) return;
    pushUndoSnapshot(currentSnapshot());
    const auto ids = selectedItemIds();
    const auto groupId = group ? freshId() : QString();
    auto assign = [&](auto& items) {
        for (auto& item : items)
            if (ids.contains(item.id)) item.groupId = groupId;
    };
    assign(m_strokes);assign(m_shapes);assign(m_texts);assign(m_images);
    saveAutosave();emit stateChanged();
}
void CanvasWidget::groupSelection() { changeSelectionGroup(true); }
void CanvasWidget::ungroupSelection() { changeSelectionGroup(false); }

void CanvasWidget::deleteSelection() {
    if(m_mode!=InteractionMode::None||m_polylineActive)return;
    const auto ids = selectedItemIds();
    if(ids.isEmpty())return;
    bool skippedLocked=false;
    for(const auto& s:m_shapes) if(ids.contains(s.id)&&s.locked) skippedLocked=true;
    for(const auto& im:m_images) if(ids.contains(im.id)&&im.locked) skippedLocked=true;
    for(const auto& t:m_texts) if(ids.contains(t.id)&&t.locked) skippedLocked=true;
    const auto original=currentSnapshot();
    const auto strokesBefore=m_strokes.size(), shapesBefore=m_shapes.size(), imagesBefore=m_images.size(), textsBefore=m_texts.size();
    auto selected = [&](const auto& item) { return ids.contains(item.id); };
    m_strokes.removeIf(selected);
    m_shapes.removeIf([&](const ShapeItem& s){ return !s.locked && selected(s); });
    m_images.removeIf([&](const ImageItem& image){return !image.isPdfPage && !image.locked && selected(image);});
    m_texts.removeIf([&](const TextItem& t){ return !t.locked && selected(t); });
    const bool removedAnything = (m_strokes.size()!=strokesBefore || m_shapes.size()!=shapesBefore
                                  || m_images.size()!=imagesBefore || m_texts.size()!=textsBefore);
    if(removedAnything) {
        pushUndoSnapshot(original);
        updateShapeLinks(m_shapes,original.shapes,{});
        saveAutosave();
    }
    if(skippedLocked) emit toastRequested(QStringLiteral("Objeto bloqueado · desbloquéalo con el candado"));
    clearSelection();emit stateChanged();
}
bool CanvasWidget::isSelectionLocked() const {
    if(const auto* s=findShapeById(m_selectedShapeId)) return s->locked;
    for(const auto& s:m_shapes) if(m_selectedShapeIds.contains(s.id)&&s.locked) return true;
    for(const auto& im:m_images) if((im.id==m_selectedImageId||m_selectedImageIds.contains(im.id))&&im.locked) return true;
    for(const auto& t:m_texts) if((t.id==m_selectedTextId||m_selectedTextIds.contains(t.id))&&t.locked) return true;
    return false;
}
void CanvasWidget::toggleLockSelection() {
    const auto ids=selectedItemIds();
    if(!hasLockableSelection())return;
    const bool nextLocked=!isSelectionLocked();
    pushUndoSnapshot(currentSnapshot());
    for(auto& s:m_shapes) if(ids.contains(s.id)) s.locked=nextLocked;
    for(auto& im:m_images) if(ids.contains(im.id)&&!im.isPdfPage) im.locked=nextLocked;
    for(auto& t:m_texts) if(ids.contains(t.id)) t.locked=nextLocked;
    emit toastRequested(nextLocked?QStringLiteral("Bloqueado"):QStringLiteral("Desbloqueado"));
    saveAutosave();emit stateChanged();
}
SelectionStyle CanvasWidget::selectionStyle() const {
    const auto ids = selectedItemIds();
    SelectionStyle style;
    auto readStroke = [&](const auto& item) {
        if (!style.color) style.color = item.color;
        if (!style.width) style.width = item.size;
        if (!style.pattern && int(item.linePattern) <= int(LinePattern::Dotted))
            style.pattern = item.linePattern;
    };
    auto readFill = [&](const ShapeItem& fill) {
        style.filled = true;
        if (!style.fillColor) style.fillColor = fill.color;
        if (!style.fillPattern) style.fillPattern = fill.shadePattern;
    };
    for (const auto& shape : m_shapes) {
        if (!ids.contains(shape.id) || shape.locked)
            continue;
        if (shape.kind == ShapeKind::ShadedRegion) {
            readFill(shape);
            continue;
        }
        readStroke(shape);
        if (!style.ending && supportsLineEnding(shape))
            style.ending = shape.lineEnding;
        if (!style.label && (shape.linePattern == LinePattern::Dimension || shape.kind == ShapeKind::AngleMarker))
            style.label = shape.label;
        if (!canFillShape(shape))
            continue;
        if (!style.filled)
            style.filled = false;
        for (const auto& region : m_shapes)
            if (linkedFillOf(region, shape.id) && !region.locked && region.shadeVisible)
                readFill(region);
    }
    for (const auto& stroke : m_strokes)
        if (ids.contains(stroke.id))
            readStroke(stroke);
    for (const auto& item : m_texts)
        if (!style.color && !item.locked && ids.contains(item.id))
            style.color = item.color;
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
            stroke.bounds = computeBounds(stroke.points, stroke.size * .7);
        }
    }
    for (auto& item : updated.texts)
        if (!item.locked && ids.contains(item.id))
            assign(item.color, style.color);

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
        if (shape.linePattern == LinePattern::Dimension || shape.kind == ShapeKind::AngleMarker)
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
            if (style.filled == false)
                removedFills.insert(region->id);
            else {
                assign(region->color, style.fillColor);
                assign(region->shadePattern, style.fillPattern);
            }
        }
    }
    if (!changed && newFills.isEmpty() && removedFills.isEmpty())
        return;
    updated.shapes.removeIf([&](const auto& shape) { return removedFills.contains(shape.id); });
    updated.shapes.append(newFills);
    pushUndoSnapshot(currentSnapshot());
    m_strokes = std::move(updated.strokes);
    m_shapes = std::move(updated.shapes);
    m_texts = std::move(updated.texts);
    m_selectedShapeIds.subtract(removedFills);
    if (removedFills.contains(m_selectedShapeId))
        m_selectedShapeId.clear();
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::duplicateSelection() {
    if(m_mode!=InteractionMode::None||m_polylineActive)return;
    const auto selection=selectedItemIds();
    if(selection.isEmpty())return;
    cloneObjects(currentSnapshot(), selection, QPointF(24, 24), hasLassoSelection());
}

void CanvasWidget::cloneObjects(const DocumentSnapshot& base, const QSet<QString>& selection,
                               const QPointF& offset, bool multiple) {
    if (selection.isEmpty())
        return;
    pushUndoSnapshot(currentSnapshot());
    QHash<QString,QString> copies,groups;
    for(const auto& id:selection)copies.insert(id,freshId());
    for(const auto& shape:base.shapes) {
        if(!shape.shadeBoundary.isEmpty()&&std::all_of(shape.shadeBoundary.keyBegin(),shape.shadeBoundary.keyEnd(),
                                                      [&](const auto& id){return copies.contains(id);}))
            if(!copies.contains(shape.id))copies.insert(shape.id,freshId());
    }
    auto identifyCopy=[&](auto& item) {
        item.id=copies.value(item.id);
        if(!item.groupId.isEmpty()) {
            if(!groups.contains(item.groupId))groups.insert(item.groupId,freshId());
            item.groupId=groups.value(item.groupId);
        }
    };
    QSet<QString> newStrokes,newShapes,newImages,newTexts;
    for(auto stroke:base.strokes)if(copies.contains(stroke.id)) {
        identifyCopy(stroke);for(auto& point:stroke.points){point.x+=offset.x();point.y+=offset.y();}
        stroke.bounds=computeBounds(stroke.points,stroke.size*.7);newStrokes.insert(stroke.id);m_strokes.append(stroke);
    }
    for(auto shape:base.shapes)if(copies.contains(shape.id)) {
        const bool selected=selection.contains(shape.id);
        identifyCopy(shape);translateShape(shape,offset);
        shape.attachments.removeIf([&](const auto& link) { return !copies.contains(link.targetId); });
        for(auto& link:shape.attachments)link.targetId=copies.value(link.targetId);
        QHash<QString,int> boundary;
        for(auto it=shape.shadeBoundary.cbegin();it!=shape.shadeBoundary.cend();++it)
            if(copies.contains(it.key()))boundary.insert(copies.value(it.key()),it.value());
        if(boundary.size()!=shape.shadeBoundary.size())boundary.clear();
        shape.shadeBoundary=boundary;
        shape.shadeProbe+=offset;
        if(selected)newShapes.insert(shape.id);
        m_shapes.append(shape);
    }
    for(auto image:base.images)if(!image.isPdfPage&&copies.contains(image.id)){identifyCopy(image);image.rect.translate(offset);newImages.insert(image.id);m_images.append(image);}
    for(auto item:base.texts)if(copies.contains(item.id)){identifyCopy(item);item.pos+=offset;item.bounds=textBounds(item);newTexts.insert(item.id);m_texts.append(item);}
    clearSelection();m_selectedStrokeIds=newStrokes;
    if(!multiple&&newShapes.size()==1&&m_tool==ToolType::Select)m_selectedShapeId=*newShapes.cbegin();
    else m_selectedShapeIds=newShapes;
    if(!multiple&&newImages.size()==1)m_selectedImageId=*newImages.cbegin();
    else m_selectedImageIds=newImages;
    if(!multiple&&newTexts.size()==1)m_selectedTextId=*newTexts.cbegin();
    else m_selectedTextIds=newTexts;
    if(m_tool!=ToolType::Select)m_tool=ToolType::Lasso;
    m_lassoBounds=lassoSelectionBounds();invalidateStaticCache();saveAutosave();emit stateChanged();
}
void CanvasWidget::straightenSelection() {
    if(m_mode!=InteractionMode::None||m_selectedStrokeIds.isEmpty())return;
    auto strokes=m_strokes;if(!straightenSelectedStrokes(strokes,m_selectedStrokeIds))return;
    pushUndoSnapshot(currentSnapshot());m_strokes=std::move(strokes);
    m_lassoBounds=lassoSelectionBounds();invalidateStaticCache();saveAutosave();emit stateChanged();emit toastRequested(QStringLiteral("Escritura embellecida y ordenada ✓"));
}

void CanvasWidget::flipSelectionHorizontal() {
    if (m_mode != InteractionMode::None || m_polylineActive || !hasSelection())
        return;
    QSet<QString> shapeIds = m_selectedShapeIds;
    if (!m_selectedShapeId.isEmpty())
        shapeIds.insert(m_selectedShapeId);
    const QRectF bounds = !m_lassoBounds.isEmpty() ? m_lassoBounds
        : computeSelectionBounds(m_strokes, m_shapes, m_selectedStrokeIds, shapeIds);
    if (bounds.isEmpty())
        return;
    const auto originalShapes = m_shapes;
    pushUndoSnapshot(currentSnapshot());
    flipSelection(m_strokes, m_shapes, m_selectedStrokeIds, shapeIds, bounds, true);
    if (!shapeIds.isEmpty())
        updateShapeLinks(m_shapes, originalShapes, shapeIds);
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    emit toastRequested(QStringLiteral("Volteado horizontalmente ✓"));
}

void CanvasWidget::flipSelectionVertical() {
    if (m_mode != InteractionMode::None || m_polylineActive || !hasSelection())
        return;
    QSet<QString> shapeIds = m_selectedShapeIds;
    if (!m_selectedShapeId.isEmpty())
        shapeIds.insert(m_selectedShapeId);
    const QRectF bounds = !m_lassoBounds.isEmpty() ? m_lassoBounds
        : computeSelectionBounds(m_strokes, m_shapes, m_selectedStrokeIds, shapeIds);
    if (bounds.isEmpty())
        return;
    const auto originalShapes = m_shapes;
    pushUndoSnapshot(currentSnapshot());
    flipSelection(m_strokes, m_shapes, m_selectedStrokeIds, shapeIds, bounds, false);
    if (!shapeIds.isEmpty())
        updateShapeLinks(m_shapes, originalShapes, shapeIds);
    m_lassoBounds = lassoSelectionBounds();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    emit toastRequested(QStringLiteral("Volteado verticalmente ✓"));
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
    partitionToBack(m_strokes, [&](const Stroke& s) { return ids.contains(s.id); });
    partitionToBack(m_shapes, [&](const ShapeItem& s) { return s.kind != ShapeKind::ShadedRegion && ids.contains(s.id); });
    partitionToBack(m_images, [&](const ImageItem& im) { return !im.isPdfPage && ids.contains(im.id); });
    partitionToBack(m_texts, [&](const TextItem& t) { return ids.contains(t.id); });
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
    partitionToFront(m_strokes, [&](const Stroke& s) { return ids.contains(s.id); });
    partitionToFront(m_shapes, [&](const ShapeItem& s) { return s.kind == ShapeKind::ShadedRegion || ids.contains(s.id); });
    std::stable_partition(m_images.begin(), m_images.end(), [&](const ImageItem& im) {
        return im.isPdfPage || ids.contains(im.id);
    });
    std::stable_partition(m_images.begin(), m_images.end(), [](const ImageItem& im) { return im.isPdfPage; });
    partitionToFront(m_texts, [&](const TextItem& t) { return ids.contains(t.id); });
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
    auto strokes = m_strokes;
    auto shapes = m_shapes;
    auto images = m_images;
    auto texts = m_texts;
    if (!shiftContentVertically(strokes, shapes, images, texts, yThreshold, deltaY))
        return;
    pushUndoSnapshot(currentSnapshot());
    m_strokes = std::move(strokes);
    m_shapes = std::move(shapes);
    m_images = std::move(images);
    m_texts = std::move(texts);
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
    if (worldRect.isEmpty() && !m_selectedShapeId.isEmpty())
        if (const auto* s = findShapeById(m_selectedShapeId))
            worldRect = s->bounds;
    if (worldRect.isEmpty() && !m_selectedImageId.isEmpty())
        for (const auto& im : m_images)
            if (im.id == m_selectedImageId)
                worldRect = im.rect;
    if (worldRect.isEmpty() && !m_selectedTextId.isEmpty())
        for (const auto& t : m_texts)
            if (t.id == m_selectedTextId)
                worldRect = t.bounds;
    worldRect = worldRect.normalized().adjusted(-12.0, -12.0, 12.0, 12.0);
    if (worldRect.width() < 8.0 || worldRect.height() < 8.0)
        return;

    const double scale = 2.0;
    const QSize pixelSize(std::clamp(qCeil(worldRect.width() * scale), 16, 4096),
                          std::clamp(qCeil(worldRect.height() * scale), 16, 4096));
    QImage captured(pixelSize, QImage::Format_ARGB32_Premultiplied);
    const auto theme = getThemeConfig(m_themeId);
    const bool hasPdf = std::any_of(m_images.cbegin(), m_images.cend(),
                                    [](const ImageItem& img) { return img.isPdfPage && !img.rect.isEmpty(); });
    captured.fill(hasPdf ? Qt::white : theme.background);
    {
        QPainter p(&captured);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        p.scale(double(pixelSize.width()) / worldRect.width(),
                double(pixelSize.height()) / worldRect.height());
        p.translate(-worldRect.topLeft());
        if (!hasPdf)
            drawBackgroundTemplate(p, backgroundTemplate(), theme, Viewport{0, 0, 1.0}, worldRect);
        for (bool pdfPass : {true, false})
            for (const auto& im : m_images)
                if (im.isPdfPage == pdfPass && im.rect.intersects(worldRect) && !im.image.isNull())
                    p.drawImage(im.rect, im.image);
        for (const auto& s : m_shapes)
            if (s.kind == ShapeKind::ShadedRegion && s.bounds.intersects(worldRect))
                drawShape(p, s);
        for (const auto& st : m_strokes)
            if (st.tool == ToolType::Highlighter && st.bounds.intersects(worldRect))
                drawStroke(p, st);
        for (const auto& s : m_shapes)
            if (s.kind != ShapeKind::ShadedRegion && s.bounds.intersects(worldRect))
                drawShape(p, s);
        for (const auto& st : m_strokes)
            if (st.tool != ToolType::Highlighter && st.bounds.intersects(worldRect))
                drawStroke(p, st);
        for (const auto& t : m_texts)
            if (t.bounds.intersects(worldRect))
                drawTextItem(p, t);
    }
    captured.setDevicePixelRatio(scale);

    if (!preparePageChange())
        return;
    PageData newPage;
    newPage.viewport = fittedSheetViewport(m_pageFormat, size());
    const QRectF sheet = sheetRectForPageFormat(m_pageFormat);
    const double maxW = sheet.isEmpty() ? 680.0 : sheet.width() * 0.82;
    const double targetW = std::min(worldRect.width(), maxW);
    const double targetH = targetW * (worldRect.height() / std::max(1.0, worldRect.width()));
    const double topY = sheet.isEmpty() ? -260.0 : sheet.top() + 36.0;

    ImageItem snippet;
    snippet.id = freshId();
    snippet.image = captured;
    snippet.rect = QRectF(-targetW / 2.0, topY, targetW, targetH);
    newPage.images.append(snippet);

    insertPages(m_currentPageIndex + 1, {newPage});
    emit toastRequested(QStringLiteral("Recorte enviado a página nueva ✓"));
}

bool CanvasWidget::hasSelectedAxes() const {
    if (!m_selectedShapeId.isEmpty()) {
        const auto* s = findShapeById(m_selectedShapeId);
        return s && s->kind == ShapeKind::Axes;
    }
    if (m_selectedShapeIds.size() == 1) {
        const auto* s = findShapeById(*m_selectedShapeIds.cbegin());
        return s && s->kind == ShapeKind::Axes;
    }
    return false;
}

QString CanvasWidget::selectedAxesFunction() const {
    const QString id = !m_selectedShapeId.isEmpty() ? m_selectedShapeId
        : (m_selectedShapeIds.size() == 1 ? *m_selectedShapeIds.cbegin() : QString());
    if (const auto* s = findShapeById(id); s && s->kind == ShapeKind::Axes)
        return s->label;
    return {};
}

void CanvasWidget::setSelectedAxesFunction(const QString& expr) {
    const QString id = !m_selectedShapeId.isEmpty() ? m_selectedShapeId
        : (m_selectedShapeIds.size() == 1 ? *m_selectedShapeIds.cbegin() : QString());
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
    emit toastRequested(cleaned.isEmpty() ? QStringLiteral("Función borrada") : QStringLiteral("Función graficada ✓"));
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
    table.label = QStringLiteral("tabla:%1x%2").arg(rows).arg(cols);
    table.vertices = {center - QPointF(w / 2.0, h / 2.0), center + QPointF(w / 2.0, h / 2.0)};
    table.bounds = computeShapeBounds(table);
    m_shapes.append(table);
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
    const double scale = std::clamp(1.0 / std::max(0.25, m_viewport.zoom), 0.65, 1.8);
    const InstantiatedSticker inst = instantiateSticker(*tpl, centerWorld, getThemeConfig(m_themeId), style, scale);
    if (inst.shapes.isEmpty() && inst.strokes.isEmpty() && inst.texts.isEmpty())
        return false;

    pushUndoSnapshot(currentSnapshot());
    clearSelection();
    for (const auto& shape : inst.shapes) {
        m_shapes.append(shape);
        m_selectedShapeIds.insert(shape.id);
    }
    for (const auto& stroke : inst.strokes) {
        m_strokes.append(stroke);
        m_selectedStrokeIds.insert(stroke.id);
    }
    for (const auto& text : inst.texts) {
        m_texts.append(text);
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
    for (const auto& s : std::as_const(m_strokes)) {
        if (m_selectedStrokeIds.contains(s.id))
            selStrokes.append(s);
    }
    QVector<ShapeItem> selShapes;
    for (const auto& sh : std::as_const(m_shapes)) {
        if (m_selectedShapeIds.contains(sh.id) || (!m_selectedShapeId.isEmpty() && sh.id == m_selectedShapeId))
            selShapes.append(sh);
    }
    QVector<TextItem> selTexts;
    for (const auto& tx : std::as_const(m_texts)) {
        if (m_selectedTextIds.contains(tx.id) || (!m_selectedTextId.isEmpty() && tx.id == m_selectedTextId))
            selTexts.append(tx);
    }
    if (selStrokes.isEmpty() && selShapes.isEmpty() && selTexts.isEmpty())
        return false;

    if (!saveCustomSticker(cleanedTitle, category, selStrokes, selShapes, selTexts))
        return false;
    emit toastRequested(QStringLiteral("Sticker guardado: ") + cleanedTitle + QStringLiteral(" ✓"));
    return true;
}
void CanvasWidget::convertSelectionToText() {
    if(m_mode!=InteractionMode::None||m_selectedStrokeIds.isEmpty())return;
    const QString recognized=HandwritingEngine::instance().recognizeStrokesToText(m_strokes,m_selectedStrokeIds).replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified();
    const QRectF bounds=computeSelectionBounds(m_strokes,m_shapes,m_selectedStrokeIds,{});
    if(bounds.isEmpty())return;
    QColor inkColor=m_color;
    for(const auto& s:m_strokes)if(m_selectedStrokeIds.contains(s.id)){inkColor=s.color;break;}

    finishTextEditor(this, true);
    const QSet<QString> strokesToConvert = m_selectedStrokeIds;
    TextItem draft;
    draft.id = freshId();
    draft.text = recognized;
    draft.color = inkColor;
    draft.fontSize = std::clamp(bounds.height() * 0.84, 18.0, 68.0);
    draft.pos = bounds.topLeft();
    draft.bounds = textBounds(draft);

    auto* editor = new InlineTextEditor(this);
    editor->setText(draft.text);
    editor->setPlaceholderText(QStringLiteral("Escribe o corrige el texto…"));
    editor->setFont(textFont(draft.fontSize * m_viewport.zoom));
    const auto theme = getThemeConfig(m_themeId);
    editor->setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; border: 1.5px solid %3; border-radius: 6px; padding: 4px 6px; }")
                              .arg(theme.uiSurface.name(), draft.color.name(), theme.accent.name()));
    const QPointF anchor = draft.pos * m_viewport.zoom + QPointF(m_viewport.x, m_viewport.y);
    editor->move(anchor.toPoint());
    editor->resize(std::max(220, qRound(QFontMetricsF(editor->font()).horizontalAdvance(draft.text) + 36)),
                   qRound(QFontMetricsF(editor->font()).height() + 14));
    connect(editor, &QLineEdit::textChanged, editor, [editor](const QString& text) {
        editor->resize(std::max(220, qRound(QFontMetricsF(editor->font()).horizontalAdvance(text) + 36)),
                       editor->height());
    });
    editor->commit = [this, draft, strokesToConvert](const QString& text) mutable {
        const QString cleaned = text.trimmed();
        if (cleaned.isEmpty())
            return;
        pushUndoSnapshot(currentSnapshot());
        m_strokes.removeIf([&](const Stroke& s) { return strokesToConvert.contains(s.id); });
        draft.text = cleaned;
        draft.bounds = textBounds(draft);
        m_texts.append(draft);
        clearSelection();
        m_selectedTextId = draft.id;
        invalidateStaticCache();
        saveAutosave();
        emit stateChanged();
        emit toastRequested(QStringLiteral("Texto: «%1» ✓").arg(cleaned.left(24)));
    };
    editor->show();
    editor->setFocus(Qt::MouseFocusReason);
    editor->selectAll();
    update();
}
bool CanvasWidget::copySelection() {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return false;
    const auto ids = selectedItemIds();
    if (ids.isEmpty())
        return false;
    PageData copy;
    for (const auto& item : m_strokes)
        if (ids.contains(item.id)) copy.strokes.append(item);
    for (const auto& item : m_shapes) {
        if (ids.contains(item.id) || (!item.shadeBoundary.isEmpty()
            && std::all_of(item.shadeBoundary.keyBegin(), item.shadeBoundary.keyEnd(),
                           [&](const auto& id) { return ids.contains(id); })))
            copy.shapes.append(item);
    }
    for (const auto& item : m_images)
        if (!item.isPdfPage && ids.contains(item.id)) copy.images.append(item);
    for (const auto& item : m_texts)
        if (ids.contains(item.id)) copy.texts.append(item);
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
    if(m_mode!=InteractionMode::None)return false;
    const QMimeData* mime=QGuiApplication::clipboard()->mimeData();
    if (mime && mime->hasFormat(objectMimeType)) {
        const auto document = QJsonDocument::fromJson(mime->data(objectMimeType));
        if (!document.isObject())
            return false;
        QHash<QString, QVector<ImageItem>> pdfSources;
        const auto items = deserializePage(document.object(), pdfSources);
        const DocumentSnapshot source{items.strokes, items.shapes, items.images, items.texts};
        QSet<QString> ids;
        QRectF bounds;
        auto include = [&](const auto& items, auto extent) {
            for (const auto& item : items) {
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
    QImage image=QGuiApplication::clipboard()->image();
    if(image.isNull()&&mime&&mime->hasUrls()) {
        for(const QUrl& url:mime->urls()) {
            const QString local=url.toLocalFile();
            if(local.isEmpty())continue;
            if(local.endsWith(QStringLiteral(".pdf"),Qt::CaseInsensitive))
                return importPdfFile(local);
            if(importImageFile(local))
                return true;
        }
    }
    if(image.isNull())return false;
    if(m_polylineActive)finishActivePolyline();
    pushUndoSnapshot(currentSnapshot());clearSelection();if(m_tool!=ToolType::Select)m_tool=ToolType::Lasso;
    const double visibleW=std::max(240.0,double(this->width())/std::max(0.2,m_viewport.zoom));
    const double rawW=image.width()/std::max(1.0,image.devicePixelRatio());
    const double width=std::min(visibleW*0.48,rawW);
    const QSizeF size(width,width*double(image.height())/std::max(1,image.width()));
    const QPointF center=screenToWorld(QPointF(this->width()/2.0,height()/2.0));
    ImageItem item;item.id=freshId();item.image=image;item.rect=QRectF(center-QPointF(size.width()/2,size.height()/2),size);
    m_images.append(item);m_selectedImageId=item.id;invalidateStaticCache();saveAutosave();updateCursorShape();emit stateChanged();emit toastRequested(QStringLiteral("Imagen pegada ✓"));return true;
}
bool CanvasWidget::importImageFile(const QString& path) {
    if(m_mode!=InteractionMode::None)return false;
    QImageReader reader(path);reader.setAutoTransform(true);const QSize source=reader.size();
    if(source.width()>4096||source.height()>4096)reader.setScaledSize(source.scaled(4096,4096,Qt::KeepAspectRatio));
    const QImage image=reader.read();
    if(image.isNull()){emit toastRequested(QStringLiteral("No se pudo abrir la imagen"));return false;}
    if(m_polylineActive)finishActivePolyline();
    pushUndoSnapshot(currentSnapshot());clearSelection();if(m_tool!=ToolType::Select)m_tool=ToolType::Lasso;
    const double visibleW=std::max(240.0,double(this->width())/std::max(0.2,m_viewport.zoom));
    const double width=std::min(visibleW*0.48,double(image.width()));
    const QSizeF size(width,width*double(image.height())/std::max(1,image.width()));
    const auto center=screenToWorld(QPointF(this->width()/2.0,height()/2.0));
    ImageItem item;item.id=freshId();item.image=image;item.rect=QRectF(center-QPointF(size.width()/2,size.height()/2),size);
    m_images.append(item);m_selectedImageId=item.id;invalidateStaticCache();saveAutosave();updateCursorShape();emit stateChanged();emit toastRequested(QStringLiteral("Imagen insertada ✓"));return true;
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
    const bool replaceBlank = pageCount() == 1 && !currentPageHasContent() && !m_pages.at(0).background;
    const int first = m_currentPageIndex + (replaceBlank ? 0 : 1);
    pushPageHistory();
    if (replaceBlank)
        m_pages.removeAt(first);
    int index = first;
    for (const auto& image : imported) {
        PageData page;
        page.images.append(image);
        m_pages.insert(index++, std::move(page));
    }
    loadWorkingPage(first, true);
    emit toastRequested(tr("PDF importado · %1 páginas").arg(imported.size()));
    return true;
}
bool CanvasWidget::exportToPdf(const QString& path) const {
    auto* self=const_cast<CanvasWidget*>(this);finishTextEditor(self,true);
    if(m_polylineActive)self->finishActivePolyline();
    if(m_mode!=InteractionMode::None)return false;
    syncWorkingPage(self->m_pages,self->m_currentPageIndex,currentSnapshot(),m_viewport);
    const bool saved=exportPagesToPdf(path,m_pages,getThemeConfig(m_themeId),m_template,m_pageFormat);
    if(saved)emit self->toastRequested(QStringLiteral("PDF exportado ✓"));
    return saved;
}
void CanvasWidget::deleteSelectedShape() {
    if(!m_selectedShapeId.isEmpty())deleteSelection();
}
void CanvasWidget::cancelActivePolyline() {
    m_polylineActive=false;m_lastPolylineTapMs=-1;m_polylineVertices.clear();m_polylineSnapClose=false;m_hasActiveShape=false;m_activeGuides.clear();
    m_selectedShapeId.clear();invalidateStaticCache();emit stateChanged();
}
void CanvasWidget::finishActivePolyline(bool closeShape) {
    if(!m_polylineActive)return;
    if(m_polylineVertices.size()<(closeShape?3:2)){cancelActivePolyline();return;}
    ShapeItem shape=m_activeShape;shape.vertices=m_polylineVertices;shape.closed=closeShape;
    if(shape.vertices.size()>2&&distance(shape.vertices.front(),shape.vertices.back())<.001)shape.vertices.removeLast();
    shape.bounds=computeShapeBounds(shape);pushUndoSnapshot(currentSnapshot());m_shapes.append(shape);attachNewShape(m_shapes);
    m_polylineActive=false;m_lastPolylineTapMs=-1;m_polylineVertices.clear();m_polylineSnapClose=false;m_hasActiveShape=false;m_activeGuides.clear();
    m_selectedShapeId=shape.id;invalidateStaticCache();saveAutosave();emit stateChanged();
}
void CanvasWidget::setEraserMode(EraserMode mode) { m_eraserMode=mode;emit stateChanged(); }
void CanvasWidget::setEraserSize(double size) { if(!finite(size))return;m_eraserSize=std::clamp(size,1.0,500.0);update();emit stateChanged(); }
void CanvasWidget::setEraserPreserveShapes(bool preserve) {
    if (m_eraserPreserveShapes == preserve)
        return;
    m_eraserPreserveShapes = preserve;
    QSettings("Finily", "Finily").setValue("ink/eraserPreserveShapes", preserve);
    emit stateChanged();
}
void CanvasWidget::setEraserOnlyHighlight(bool onlyHighlight) {
    if (m_eraserOnlyHighlight == onlyHighlight)
        return;
    m_eraserOnlyHighlight = onlyHighlight;
    QSettings("Finily", "Finily").setValue("ink/eraserOnlyHighlight", onlyHighlight);
    emit stateChanged();
}
void CanvasWidget::setInkColor(const QColor& color) {
    if (!color.isValid())
        return;
    clearPenCursor();
    m_color = color;
    // Cambio de color en 1 clic si hay objetos seleccionados (estilo Xournal++ / PDF Annotator)
    if (m_mode == InteractionMode::None && !m_polylineActive && hasSelection()) {
        SelectionStyle edits;
        edits.color = color;
        setSelectionStyle(edits);
        return;
    }
    emit stateChanged();
}

void CanvasWidget::setStrokeSize(double size) {
    if (!finite(size))
        return;
    const double width = std::clamp(size, .1, 200.0);
    clearPenCursor();
    m_size = width;
    // Cambio de grosor en 1 clic si hay objetos seleccionados
    if (m_mode == InteractionMode::None && !m_polylineActive && hasSelection()) {
        SelectionStyle edits;
        edits.width = width;
        setSelectionStyle(edits);
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
    if (!finite(sensitivity)) return;
    const double clamped = std::clamp(sensitivity, 0.0, 1.0);
    if (std::abs(m_pressureSensitivity - clamped) < 1e-6) return;
    m_pressureSensitivity = clamped;
    QSettings("Finily", "Finily").setValue("ink/pressureSensitivity", clamped);
    emit stateChanged();
}
void CanvasWidget::setPressureProfile(PressureProfile profile) {
    if(int(profile)<int(PressureProfile::Light)||int(profile)>int(PressureProfile::Firm))return;
    if(m_pressureProfile==profile)return;
    m_pressureProfile=profile;saveAutosave();emit stateChanged();
}
void CanvasWidget::setStabilizer(double strength) {
    if(!finite(strength))return;
    m_stabilizer=std::clamp(strength,0.0,1.0);
    m_stabilizerFilter->setStrength(m_stabilizer);emit stateChanged();
}
void CanvasWidget::setHighlighterOpacity(double opacity) {
    if (!finite(opacity)) return;
    const double clamped = std::clamp(opacity, 0.10, 0.70);
    if (std::abs(m_highlighterOpacity - clamped) < 1e-6) return;
    m_highlighterOpacity = clamped;
    QSettings("Finily", "Finily").setValue("ink/highlighterOpacity", clamped);
    emit stateChanged();
}
void CanvasWidget::setThemeId(ThemeId id) {
    clearPenCursor();const auto old=getThemeConfig(m_themeId);m_themeId=id;
    if(m_color==old.defaultInk)m_color=getThemeConfig(id).defaultInk;
    m_dirty = true;
    invalidateStaticCache();saveAutosave();emit pageStructureChanged();emit stateChanged();
}
BackgroundTemplate CanvasWidget::backgroundTemplate() const {
    return m_pages.at(m_currentPageIndex).background.value_or(m_template);
}

void CanvasWidget::setBackgroundTemplate(BackgroundTemplate tpl) {
    if (tpl == backgroundTemplate())
        return;
    if (m_pages.at(m_currentPageIndex).background) {
        if (!preparePageChange())
            return;
        pushPageHistory();
        m_pages[m_currentPageIndex].background = tpl;
    } else {
        m_template = tpl;
        m_dirty = true;
    }
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
}
QRectF CanvasWidget::activeFiniteSheetRect() const {
    for (const auto& item : m_images)
        if (item.isPdfPage && !item.rect.isEmpty())
            return item.rect;
    return sheetRectForPageFormat(m_pageFormat);
}

void CanvasWidget::clampFiniteViewport() {
    if (width() <= 0 || height() <= 0)
        return;
    const QRectF sheet = activeFiniteSheetRect();
    if (sheet.isEmpty())
        return;

    const double availW = std::max(240.0, width() - 156.0);
    const double availH = std::max(200.0, height() - 116.0);
    const double minZoom = std::clamp(std::min(availW / sheet.width(), availH / sheet.height()), 0.2, 5.0);
    m_viewport.zoom = std::clamp(m_viewport.zoom, minZoom, 5.0);

    const double sheetW = sheet.width() * m_viewport.zoom;
    const double sheetH = sheet.height() * m_viewport.zoom;
    const double marginX = 72.0;
    const double marginY = 58.0;

    if (sheetW <= width() - marginX * 2.0) {
        m_viewport.x = width() / 2.0 - sheet.center().x() * m_viewport.zoom;
    } else {
        const double maxX = marginX - sheet.left() * m_viewport.zoom;
        const double minX = (width() - marginX) - sheet.right() * m_viewport.zoom;
        m_viewport.x = std::clamp(m_viewport.x, std::min(minX, maxX), std::max(minX, maxX));
    }

    if (sheetH <= height() - marginY * 2.0) {
        m_viewport.y = height() / 2.0 - sheet.center().y() * m_viewport.zoom;
    } else {
        const double maxY = marginY - sheet.top() * m_viewport.zoom;
        const double minY = (height() - marginY) - sheet.bottom() * m_viewport.zoom;
        m_viewport.y = std::clamp(m_viewport.y, std::min(minY, maxY), std::max(minY, maxY));
    }
}

void CanvasWidget::setPageFormat(PageFormat format) {
    if(format!=PageFormat::Infinite&&format!=PageFormat::A4&&format!=PageFormat::Slide16x9)return;
    if(m_pageFormat==format)return;
    m_pageFormat=format;m_dirty=true;
    m_zoomAnimation->stop();clearPenCursor();
    if(format!=PageFormat::Infinite) {
        m_viewport=fittedSheetViewport(format,size());
        clampFiniteViewport();
    }
    invalidateStaticCache();saveAutosave();emit pageStructureChanged();emit stateChanged();emit viewportChanged(m_viewport.zoom);
}
QPointF CanvasWidget::screenToWorld(const QPointF& p) const { return {(p.x()-m_viewport.x)/m_viewport.zoom,(p.y()-m_viewport.y)/m_viewport.zoom}; }
void CanvasWidget::zoomByFactor(double factor,const QPointF& anchor) {
    if(!finite(factor)||factor<=0)return;
    m_zoomAnimation->stop();clearPenCursor();
    const auto world=screenToWorld(anchor);m_viewport.zoom=std::clamp(m_viewport.zoom*factor,0.2,5.0);
    m_viewport.x=anchor.x()-world.x()*m_viewport.zoom;m_viewport.y=anchor.y()-world.y()*m_viewport.zoom;
    clampFiniteViewport();
    invalidateStaticCache();emit viewportChanged(m_viewport.zoom);
}
void CanvasWidget::animateViewport(const Viewport& target) {
    m_zoomAnimation->stop();clearPenCursor();m_zoomStart=m_viewport;m_zoomTarget=target;
    if (!activeFiniteSheetRect().isEmpty()) {
        const Viewport saved = m_viewport;
        m_viewport = target;
        clampFiniteViewport();
        m_zoomTarget = m_viewport;
        m_viewport = saved;
    }
    if(m_zoomStart.x==m_zoomTarget.x&&m_zoomStart.y==m_zoomTarget.y&&m_zoomStart.zoom==m_zoomTarget.zoom)return;
    m_zoomAnimation->start();
}
void CanvasWidget::animateZoom(double factor) {
    const auto base=m_zoomAnimation->state()==QAbstractAnimation::Running?m_zoomTarget:m_viewport;
    const QPointF anchor(width()/2.0,height()/2.0);
    const double zoom=std::clamp(base.zoom*factor,.2,5.0);
    animateViewport({anchor.x()-(anchor.x()-base.x)*zoom/base.zoom,
        anchor.y()-(anchor.y()-base.y)*zoom/base.zoom,zoom});
}
void CanvasWidget::zoomIn() { animateZoom(1.2); }
void CanvasWidget::zoomOut() { animateZoom(1/1.2); }
void CanvasWidget::resetViewport() {
    const auto pdf = std::find_if(m_images.cbegin(), m_images.cend(),
                                  [](const auto& item) { return item.isPdfPage && !item.rect.isEmpty(); });
    animateViewport(pdf == m_images.cend() ? fittedSheetViewport(m_pageFormat, size())
                                          : fittedPdfViewport(pdf->rect, size()));
    saveAutosave();
}

void CanvasWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const auto pdf = std::find_if(m_images.cbegin(), m_images.cend(),
                                  [](const auto& item) { return item.isPdfPage && !item.rect.isEmpty(); });
    if (pdf != m_images.cend()) {
        const auto previous = fittedPdfViewport(pdf->rect, event->oldSize());
        const bool wasFitted = std::abs(previous.x - m_viewport.x) < 1.0
            && std::abs(previous.y - m_viewport.y) < 1.0 && std::abs(previous.zoom - m_viewport.zoom) < .005;
        if (wasFitted || event->oldSize().isEmpty() || event->oldSize().width() <= 960) {
            m_viewport = fittedPdfViewport(pdf->rect, size());
        } else {
            clampFiniteViewport();
        }
        emit viewportChanged(m_viewport.zoom);
    } else if (m_pageFormat != PageFormat::Infinite && !size().isEmpty()) {
        const auto previous = fittedSheetViewport(m_pageFormat, event->oldSize());
        const bool wasFitted = std::abs(previous.x - m_viewport.x) < 1.0
            && std::abs(previous.y - m_viewport.y) < 1.0 && std::abs(previous.zoom - m_viewport.zoom) < .005;
        if (wasFitted || event->oldSize().isEmpty() || event->oldSize().width() <= 960) {
            m_viewport = fittedSheetViewport(m_pageFormat, size());
        }
        clampFiniteViewport();
        emit viewportChanged(m_viewport.zoom);
    }
    invalidateStaticCache();
}
void CanvasWidget::beginInput(const QPointF& screen,double pressure,double tiltX,double tiltY,bool hardware,bool eraser,Qt::MouseButton button,Qt::KeyboardModifiers mods) {
    if(m_radialOpen) {
        updateRadialHover(screen);
        commitRadialSelection(false);
        return;
    }
    if(m_mode!=InteractionMode::None)return;
    m_zoomAnimation->stop();clearPenCursor();
    setFocus(Qt::MouseFocusReason);
    m_rightClickOnSelection = false;
    if(m_polylineActive&&button==Qt::RightButton){finishActivePolyline();return;}
    if (button == Qt::RightButton && !m_spaceHeld && hasSelection())
        m_rightClickOnSelection = selectionScreenRect().adjusted(-8, -8, 8, 8).contains(screen);
    if(button==Qt::MiddleButton||button==Qt::RightButton||m_spaceHeld||m_tool==ToolType::Hand) {
        m_mode=InteractionMode::Pan;m_panStartScreen=screen;m_panStartViewport={m_viewport.x,m_viewport.y};updateCursorShape();return;
    }
    if(button!=Qt::LeftButton)return;
    const auto world=screenToWorld(screen);m_activeGuides.clear();
    if(eraser||m_tool==ToolType::Eraser) {
        if(m_polylineActive)cancelActivePolyline();
        m_mode=InteractionMode::Erase;m_snapshotBeforeErase=currentSnapshot();m_erasedAnything=false;
        m_eraserWorldPos=world;m_showEraserCursor=true;applyEraseAt(world, mods.testFlag(Qt::ShiftModifier));updateCursorShape();return;
    }
    if(m_tool==ToolType::Laser) {
        m_mode=InteractionMode::LaserTrail;m_laserTrail.append({world,nowMs(),true});m_laserTimer->start();update();return;
    }
    if (m_tool == ToolType::Fill) {
        auto region = shadedRegionAt(world, m_shapes, m_color, m_shadePattern);
        if (!region) {
            emit toastRequested(tr("No hay una región cerrada en ese punto"));
            return;
        }
        int sameRegionCount = 0;
        qsizetype lastSameRegionIdx = -1;
        qsizetype exactMatchIdx = -1;
        for (qsizetype i = 0; i < m_shapes.size(); ++i) {
            const auto& shape = m_shapes[i];
            if (shape.kind == ShapeKind::ShadedRegion && shape.vertices == region->vertices
                && shape.shadeContourStarts == region->shadeContourStarts) {
                ++sameRegionCount;
                lastSameRegionIdx = i;
                if (shape.color == region->color && shape.shadePattern == m_shadePattern)
                    exactMatchIdx = i;
            }
        }
        pushUndoSnapshot(currentSnapshot());
        if (exactMatchIdx >= 0) {
            // Tocar de nuevo con el mismo color y trama quita esa capa (toggle) y renumera las restantes.
            m_shapes.removeAt(exactMatchIdx);
            int nextLayer = 0;
            for (auto& shape : m_shapes) {
                if (shape.kind == ShapeKind::ShadedRegion && shape.vertices == region->vertices
                    && shape.shadeContourStarts == region->shadeContourStarts) {
                    shape.shadeLayer = nextLayer++;
                }
            }
        } else if (sameRegionCount >= 3 && lastSameRegionIdx >= 0) {
            // Máximo 3 capas superpuestas en una misma región.
            m_shapes[lastSameRegionIdx].color = region->color;
            m_shapes[lastSameRegionIdx].shadePattern = m_shadePattern;
        } else {
            // Añadir nueva capa encima desplazada/alternada para distinguir ambos colores.
            region->id = freshId();
            region->shadeLayer = sameRegionCount;
            m_shapes.append(*region);
        }
        invalidateStaticCache();
        saveAutosave();
        emit stateChanged();
        return;
    }
    auto startShapeEdit = [&](const ShapeItem& shape, InteractionMode mode, int handle = -1) {
        m_selectedShapeId=shape.id;
        if(shape.locked) { m_mode=InteractionMode::None; invalidateStaticCache(); emit stateChanged(); return; }
        m_snapshotBeforeShapeEdit=currentSnapshot();m_activeHandleIndex=handle;
        m_shapeDragStartWorld=world;m_shapeDragOriginalVertices=shape.vertices;m_shapeDragOriginalKind=shape.kind;
        m_mode=mode;
        invalidateStaticCache();emit stateChanged();
    };
    auto startImageEdit = [&](const ImageItem& image, int handle = -1) {
        m_selectedImageId=image.id;
        if(image.locked) { m_mode=InteractionMode::None; invalidateStaticCache(); emit stateChanged(); return; }
        m_mode=handle>=0?InteractionMode::ImageResize:InteractionMode::ImageMove;
        m_imageHandleIndex=handle;m_imageOriginalRect=image.rect;m_imageDragStartWorld=world;m_snapshotBeforeImageEdit=currentSnapshot();
        invalidateStaticCache();emit stateChanged();
    };
    auto startSelectionTransform = [&](int handle) {
        m_lassoHandleIndex=handle;
        m_lassoPreviewDelta=QPointF();
        m_snapshotBeforeLassoTransform=currentSnapshot();m_lassoDragStartWorld=world;
        m_mode=handle==4?InteractionMode::LassoRotate:handle>=0?InteractionMode::LassoScale:InteractionMode::LassoMove;
        const QVector<QPointF> corners{m_lassoBounds.topLeft(),m_lassoBounds.topRight(),m_lassoBounds.bottomRight(),m_lassoBounds.bottomLeft()};
        m_lassoTransformCenter=handle>=0&&handle<4?corners[(handle+2)%4]:m_lassoBounds.center();
        m_lassoStartAngle=std::atan2(world.y()-m_lassoTransformCenter.y(),world.x()-m_lassoTransformCenter.x());
    };
    auto selectTogether = [&](const QString& id) {
        if(!hasLassoSelection() || !selectedItemIds().contains(id)) {
            clearSelection();
            expandLassoSelection(id);
            invalidateStaticCache();emit stateChanged();
        }
        startSelectionTransform(-1);
    };
    if (m_tool == ToolType::Select) {
        if (const auto* selected = std::as_const(*this).findShapeById(m_selectedShapeId)) {
            const int vertex = hitTestHandle(*selected, world, 8 / m_viewport.zoom);
            if (vertex >= 0) { startShapeEdit(*selected, InteractionMode::ShapeDragHandle, vertex); return; }
            const int side = hitTestResizeHandle(*selected, world, m_viewport.zoom);
            if (side >= 0) { startShapeEdit(*selected, InteractionMode::ShapeScale, side); return; }
        }
        for (const auto& image : std::as_const(m_images)) {
            if (image.id != m_selectedImageId || image.isPdfPage)
                continue;
            const int handle = hitTestLassoHandle(image.rect, world, m_viewport.zoom);
            if (handle >= 0 && handle < 4) { startImageEdit(image, handle); return; }
        }
        if (hasLassoSelection()) {
            const int handle = hitTestLassoHandle(m_lassoBounds, world, m_viewport.zoom);
            if (handle >= 0) { startSelectionTransform(handle); return; }
        }
    }
    if (m_tool == ToolType::Lasso && hasLassoSelection()) {
        const int handle = hitTestLassoHandle(m_lassoBounds, world, m_viewport.zoom);
        if (handle >= 0) { startSelectionTransform(handle); return; }
    }
    if(m_tool==ToolType::Text||m_tool==ToolType::Lasso||m_tool==ToolType::Select) {
        QString hit;
        for(qsizetype i=m_texts.size();i-->0;)if(m_texts[i].bounds.adjusted(-4,-4,4,4).contains(world)){hit=m_texts[i].id;break;}
        const bool edit=property("_finily_edit_text").toBool();setProperty("_finily_edit_text",false);
        if(!hit.isEmpty()&&!edit) {
            bool locked=false;
            for(const auto& item:std::as_const(m_texts))if(item.id==hit)locked=item.locked;
            if(m_tool==ToolType::Select||m_tool==ToolType::Lasso) {
                for(const auto& item:std::as_const(m_texts))if(item.id==hit&&!item.groupId.isEmpty()) {
                    selectTogether(hit);return;
                }
            }
            clearSelection();m_selectedTextId=hit;
            if(locked){invalidateStaticCache();emit stateChanged();return;}
            m_snapshotBeforeTextEdit=currentSnapshot();
            for(const auto& item:std::as_const(m_texts))if(item.id==hit)m_textOriginalPos=item.pos;
            m_textDragStartWorld=world;m_mode=InteractionMode::TextMove;invalidateStaticCache();emit stateChanged();return;
        }
        if(m_tool==ToolType::Text||(!hit.isEmpty()&&edit)) {
            if(!hit.isEmpty()) {
                for(const auto& item:std::as_const(m_texts))if(item.id==hit&&item.locked){clearSelection();m_selectedTextId=hit;invalidateStaticCache();emit stateChanged();return;}
            }
            clearSelection();TextItem draft;draft.id=freshId();draft.pos=world;draft.fontSize=std::clamp(m_size*7.5,16.0,56.0);draft.color=m_color;
            for(const auto& item:std::as_const(m_texts))if(item.id==hit)draft=item;
            m_selectedTextId=hit;
            auto* editor=new InlineTextEditor(this);editor->setText(draft.text);editor->setFont(textFont(draft.fontSize*m_viewport.zoom));
            const auto theme=getThemeConfig(m_themeId);
            editor->setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 5px; padding: 3px; }").arg(theme.uiSurface.name(),draft.color.name(),theme.accent.name()));
            const auto anchor=hit.isEmpty()?screen:draft.pos*m_viewport.zoom+QPointF(m_viewport.x,m_viewport.y);
            editor->move(anchor.toPoint());editor->resize(std::max(180,qRound(QFontMetricsF(editor->font()).horizontalAdvance(draft.text)+24)),QFontMetricsF(editor->font()).height()+12);
            connect(editor,&QLineEdit::textChanged,editor,[editor](const QString& text){editor->resize(std::max(180,qRound(QFontMetricsF(editor->font()).horizontalAdvance(text)+24)),editor->height());});
            editor->commit=[this,draft,hit](const QString& text) mutable {
                if(text.trimmed().isEmpty()||text==draft.text)return;
                pushUndoSnapshot(currentSnapshot());draft.text=text;draft.bounds=textBounds(draft);
                if(hit.isEmpty())m_texts.append(draft);
                else for(auto& item:m_texts)if(item.id==hit)item=draft;
                m_selectedTextId=draft.id;invalidateStaticCache();saveAutosave();emit stateChanged();
            };
            editor->show();editor->setFocus(Qt::MouseFocusReason);editor->selectAll();invalidateStaticCache();return;
        }
    }
    if (m_tool == ToolType::Select) {
        const double tolerance = 8 / m_viewport.zoom;
        auto selectShape = [&](const ShapeItem& shape) {
            if(!shape.groupId.isEmpty() || !shape.shadeBoundary.isEmpty()) {
                selectTogether(shape.id);return;
            }
            if (m_selectedShapeId != shape.id)
                clearSelection();
            startShapeEdit(shape, InteractionMode::ShapeMove);
        };
        for (auto it = m_shapes.crbegin(); it != m_shapes.crend(); ++it) {
            if (it->kind != ShapeKind::ShadedRegion && hitTestShape(*it, world, tolerance)) {
                selectShape(*it);
                return;
            }
        }
        for (auto it = m_strokes.crbegin(); it != m_strokes.crend(); ++it) {
            if (!hitTestStroke(*it, world, tolerance))
                continue;
            if(!it->groupId.isEmpty()) { selectTogether(it->id); return; }
            if (!m_selectedStrokeIds.contains(it->id)) {
                clearSelection();
                m_selectedStrokeIds.insert(it->id);
                m_lassoBounds = computeSelectionBounds(m_strokes, m_shapes, m_selectedStrokeIds, {});
                invalidateStaticCache();emit stateChanged();
            }
            startSelectionTransform(-1);
            return;
        }
        // Interior taps pick the topmost outline before shaded underlays.
        for (const bool shading : {false, true}) {
            for (auto it = m_shapes.crbegin(); it != m_shapes.crend(); ++it) {
                if ((it->kind == ShapeKind::ShadedRegion) == shading && containsShapeInterior(*it, world)) {
                    selectShape(*it);
                    return;
                }
            }
        }
        for (auto it = m_images.crbegin(); it != m_images.crend(); ++it) {
            if (!it->isPdfPage && it->rect.contains(world)) {
                if(!it->groupId.isEmpty()) { selectTogether(it->id); return; }
                clearSelection();
                startImageEdit(*it);
                return;
            }
        }
        if(hasLassoSelection()&&m_lassoBounds.contains(world)) { startSelectionTransform(-1); return; }
        clearSelection();
        m_mode = InteractionMode::LassoDraw;
        m_lassoRectOrigin = world;
        m_lassoPolygon.append(world);
        update();
        return;
    }
    if(m_tool==ToolType::Lasso) {
        for(const auto& image:std::as_const(m_images))if(image.id==m_selectedImageId&&!image.isPdfPage) {
            const int handle=hitTestLassoHandle(image.rect,world,m_viewport.zoom);
            if((handle>=0&&handle<4)||image.rect.contains(world)) {
                startImageEdit(image, handle>=0&&handle<4?handle:-1);return;
            }
        }
        if(hasLassoSelection()) {
            const int handle=hitTestLassoHandle(m_lassoBounds,world,m_viewport.zoom);
            if(handle>=0||m_lassoBounds.contains(world)) {
                startSelectionTransform(handle);return;
            }
        }
        clearSelection();m_mode=InteractionMode::LassoDraw;m_lassoRectOrigin=world;m_lassoPolygon.append(world);update();return;
    }
    auto startEdit=[&](ShapeItem& shape,int handle) {
        startShapeEdit(shape,handle>=0?InteractionMode::ShapeDragHandle:InteractionMode::ShapeMove,handle);
        emit stateChanged();
    };
    const bool drawingGeometry = m_tool == ToolType::Polyline || m_tool == ToolType::Shape;
    const bool drawingLine = m_tool == ToolType::Polyline || (m_tool == ToolType::Shape && m_shapeKind == ShapeKind::Line);
    const bool writing = m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint;
    const bool editModifier = mods.testFlag(Qt::ShiftModifier);
    // Regla natural de edición de figuras:
    // - Si hay una figura seleccionada (por Auto-forma o Figuras) y NO estás trazando una Línea:
    //   1) Tocar un vértice → edita el vértice al instante (con guías H/V y 90°).
    //   2) Dar un toque fuera de la figura → SOLO sale del modo edición SIN rayar/ensuciar afuera!
    if(!m_polylineActive && !drawingLine && (m_tool == ToolType::Select || writing || m_tool == ToolType::Shape || editModifier)) {
        if(auto* selected=findShapeById(m_selectedShapeId)) {
            const int handle=hitTestHandle(*selected,world,(writing?12.0:10.0)/m_viewport.zoom);
            if(handle>=0) {
                startEdit(*selected,handle);return;
            }
            if ((writing || m_tool == ToolType::Shape)
                && !selected->bounds.adjusted(-12/m_viewport.zoom, -12/m_viewport.zoom,
                                               12/m_viewport.zoom, 12/m_viewport.zoom).contains(world)) {
                clearSelection();
                // With the pen, a tap dismisses the handles without leaving ink.
                // In the figure tool a drag can immediately start a new shape.
                if (writing)
                    return;
            }
        }
        if(drawingGeometry&&editModifier) {
            for(qsizetype i=m_shapes.size();i-->0;) {
                if(m_shapes[i].kind==ShapeKind::ShadedRegion)continue;
                const int handle=hitTestHandle(m_shapes[i],world,9/m_viewport.zoom);
                if(handle>=0){startEdit(m_shapes[i],handle);return;}
                if(hitTestShape(m_shapes[i],world,6/m_viewport.zoom)){startEdit(m_shapes[i],-1);return;}
            }
        }
    }
    if(m_tool==ToolType::Polyline) {
        const qint64 tapMs=nowMs();
        if(m_polylineActive&&m_polylineVertices.size()>=3
           &&distance(world,m_polylineVertices.front())*m_viewport.zoom<=14) {
            finishActivePolyline(true);return;
        }
        if(m_polylineActive&&m_polylineVertices.size()>=2&&m_lastPolylineTapMs>=0
           &&tapMs-m_lastPolylineTapMs<=340&&distance(screen,m_lastPolylineTapScreen)<=12
           &&distance(world,m_polylineVertices.back())*m_viewport.zoom<=12) {
            // Native accepted tablet events never produce mouseDoubleClickEvent.
            finishActivePolyline(false);return;
        }
        const auto snap=snapPointWithGuides(world,m_polylineVertices.isEmpty()?std::nullopt:std::optional<QPointF>(m_polylineVertices.back()),m_shapes,{},m_viewport.zoom,m_smartGuidesEnabled);
        if(!m_polylineActive) {
            clearSelection();m_polylineActive=true;m_polylineVertices.clear();m_activeShape={};m_activeShape.id=freshId();
            m_activeShape.kind=ShapeKind::Polyline;m_activeShape.color=m_color;m_activeShape.size=m_size;m_activeShape.linePattern=m_linePattern;
            m_activeShape.lineEnding=m_lineEnding;
        }
        if(m_polylineVertices.isEmpty()||distance(snap.point,m_polylineVertices.back())>.1/m_viewport.zoom)m_polylineVertices.append(snap.point);
        m_lastPolylineTapMs=tapMs;m_lastPolylineTapScreen=screen;
        m_polylineHoverWorld=snap.point;m_activeGuides=snap.guides;m_polylineSnapClose=false;
        m_activeShape.vertices=m_polylineVertices;m_activeShape.closed=false;m_activeShape.bounds=computeShapeBounds(m_activeShape);m_hasActiveShape=true;update();emit stateChanged();return;
    }
    if(m_tool==ToolType::Shape) {
        if(!m_congruenceMarkSpec.isEmpty()) {
            auto candidate=m_shapes;
            QString markId;
            if(stampOrCycleCongruenceMark(candidate,world,m_congruenceMarkSpec,m_color,m_size,m_viewport.zoom,&markId)) {
                pushUndoSnapshot(currentSnapshot());
                m_shapes=std::move(candidate);
                clearSelection();
                if(!markId.isEmpty()) {
                    if(const auto* placed=findShapeById(markId)) {
                        m_shapePulseAnimation->stop();
                        m_shapePulseShape=*placed;
                        m_shapePulseAlpha=1.0;
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
        clearSelection();m_snapshotBeforeShapeEdit=currentSnapshot();m_mode=InteractionMode::ShapeCreate;m_holdSnappedToShape=false;
        // Solo las líneas imantan su punto inicial a vértices cercanos; las figuras cerradas
        // (círculo, rectángulo, triángulo, elipse, etc.) comienzan exactamente donde haces clic!
        const bool snapOrigin = (m_shapeKind == ShapeKind::Line);
        const auto snap = snapOrigin
            ? snapPointWithGuides(world,std::nullopt,m_shapes,{},m_viewport.zoom,m_smartGuidesEnabled)
            : SnapResult{world,{},false};
        m_shapeDragStartWorld=world;
        m_shapeCreateOrigin=snap.point;m_activeGuides=snap.guides;m_activeShape={};m_activeShape.id=freshId();
        m_activeShape.kind=m_shapeKind;m_activeShape.color=m_color;m_activeShape.size=m_size;m_activeShape.linePattern=m_linePattern;
        m_activeShape.lineEnding=m_lineEnding;
        m_activeShape.solidKind=m_solidKind;
        createShapeVertices(m_activeShape,snap.point,snap.point,m_polygonSides,m_quadVariant);m_hasActiveShape=true;m_selectedShapeId=m_activeShape.id;update();return;
    }
    if(m_beautifyTimer)m_beautifyTimer->stop();
    if(m_mathBeautifyEnabled&&(m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint)
       &&!m_pendingBeautifyIds.isEmpty()&&!m_pendingBeautifyBox.isEmpty()) {
        const double refH=std::clamp(m_pendingBeautifyBox.height(),18.0,80.0);
        const bool jumpedRight=(world.x()-m_pendingBeautifyBox.right())>std::max(14.0,refH*0.42);
        const bool jumpedLeft=(m_pendingBeautifyBox.left()-world.x())>std::max(18.0,refH*0.60);
        const bool jumpedLine=std::abs(world.y()-m_pendingBeautifyBox.center().y())>refH*1.15;
        if(jumpedRight||jumpedLeft||jumpedLine)
            flushPendingBeautify();
    }
    clearSelection();m_mode=InteractionMode::Draw;m_holdSnappedToShape=false;m_stabilizerFilter->reset();m_stabilizerFilter->setStrength(0.0);m_liveInk.reset();m_liveInkLayer=QImage();m_inkSmoother.reset(m_stabilizer>0?(1.5+10.5*m_stabilizer)/m_viewport.zoom:0.0,3+qRound(5.0*m_stabilizer));m_activeStroke={};
    m_activeStroke.id=freshId();m_activeStroke.tool=m_tool;m_activeStroke.color=m_color;m_activeStroke.size=m_size;
    m_activeStroke.linePattern=LinePattern::Solid;m_activeStroke.pressureProfile=m_pressureProfile;m_activeStroke.pressureSensitivity=m_pressureSensitivity;m_activeStroke.hasHardwarePressure=hardware;
    m_activeStroke.tipStyle=(m_tool==ToolType::Highlighter)?m_highlighterTip:m_penTip;
    if (m_tool == ToolType::Highlighter) m_activeStroke.opacity = m_highlighterOpacity;
    m_rawStartPoint=inputPoint(world,pressure,tiltX,tiltY);m_activeStroke.points.append(m_stabilizerFilter->filter(m_rawStartPoint));m_inkSmoother.push(m_activeStroke);
    m_liveStrokeBounds = QRectF(m_activeStroke.points.front().x, m_activeStroke.points.front().y, 0, 0);
    m_liveInkPending = false;
    m_hasActiveStroke=true;m_showEraserCursor=false;
    m_holdAnchorWorld=world;m_holdLastWorld=world;
    if(m_holdToShapeEnabled&&(m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint||(m_tool==ToolType::Highlighter&&m_highlighterTip!=2)))m_holdTimer->start();
    Q_UNUSED(mods);if(m_activeStroke.tool==ToolType::Highlighter)invalidateStaticCache();else update();
}
void CanvasWidget::updateLinkedPreview(const QVector<ShapeItem>& original, const QSet<QString>& edited) {
    const bool linked = updateShapeLinks(m_shapes, original, edited);
    if(linked||m_linkedPreview)invalidateStaticCache();
    m_linkedPreview=linked;
}

void CanvasWidget::moveInput(const QPointF& screen,double pressure,double tiltX,double tiltY,bool hardware,bool eraser,Qt::KeyboardModifiers mods) {
    Q_UNUSED(hardware);const auto world=screenToWorld(screen);
    auto snapped=[&](QPointF raw,std::optional<QPointF> anchor,const QString& excluded) {
        const auto result=snapPointWithGuides(raw,anchor,m_shapes,excluded,m_viewport.zoom,m_smartGuidesEnabled);
        m_activeGuides=result.guides;return result.point;
    };
    if(m_mode==InteractionMode::LaserTrail) {m_laserTrail.append({world,nowMs(),false});if(!m_laserTimer->isActive())m_laserTimer->start();update();return;}
    if(m_mode==InteractionMode::TextMove) {
        for(auto& item:m_texts)if(item.id==m_selectedTextId){item.pos=m_textOriginalPos+world-m_textDragStartWorld;item.bounds=textBounds(item);}
        update();return;
    }
    if(m_mode==InteractionMode::Pan) {
        const auto delta=screen-m_panStartScreen;
        if (m_rightClickOnSelection) {
            if (std::hypot(delta.x(), delta.y()) < 4.0)
                return;
            m_rightClickOnSelection = false;
        }
        m_viewport.x=m_panStartViewport.x()+delta.x();
        m_viewport.y=m_panStartViewport.y()+delta.y();
        clampFiniteViewport();
        invalidateStaticCache();
        return;
    }
    if(m_mode==InteractionMode::Erase) {
        const auto previous=m_eraserWorldPos;const int steps=std::max(1,int(std::ceil(distance(world,previous)/std::max(.5,m_eraserSize/4))));
        const bool trimIntersections = mods.testFlag(Qt::ShiftModifier);
        for(int i=1;i<=steps;++i)applyEraseAt(previous+(world-previous)*(double(i)/steps), trimIntersections);
        m_eraserWorldPos=world;update();return;
    }
    if(m_mode==InteractionMode::LassoDraw) {
        if(m_lassoRectMode || m_tool == ToolType::Select) {
            const QRectF r(m_lassoRectOrigin, world);
            m_lassoPolygon = QPolygonF{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
        } else if(m_lassoPolygon.isEmpty()||distance(world,m_lassoPolygon.back())*m_viewport.zoom>1) {
            m_lassoPolygon.append(world);
        }
        update();return;
    }
    if (m_mode == InteractionMode::LassoMove) {
        // Durante el arrastre solo desplazamos el delta visual: evita copiar todo el documento y reconstruir el caché a 200 Hz.
        m_lassoPreviewDelta = world - m_lassoDragStartWorld;
        update();
        return;
    }
    if(m_mode==InteractionMode::LassoScale||m_mode==InteractionMode::LassoRotate) {
        m_strokes=m_snapshotBeforeLassoTransform.strokes;m_shapes=m_snapshotBeforeLassoTransform.shapes;
        if(m_mode==InteractionMode::LassoRotate) {
            const double angle=std::atan2(world.y()-m_lassoTransformCenter.y(),world.x()-m_lassoTransformCenter.x())-m_lassoStartAngle;
            rotateSelection(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds,m_lassoTransformCenter,angle);
            if(!m_selectedTextIds.isEmpty()||!m_selectedImageIds.isEmpty()) {
                m_texts=m_snapshotBeforeLassoTransform.texts;m_images=m_snapshotBeforeLassoTransform.images;
                const double c=std::cos(angle),s=std::sin(angle);
                auto rotatePt=[&](const QPointF& pt){
                    const QPointF d=pt-m_lassoTransformCenter;
                    return m_lassoTransformCenter+QPointF(d.x()*c-d.y()*s,d.x()*s+d.y()*c);
                };
                for(auto& text:m_texts)if(!text.locked&&m_selectedTextIds.contains(text.id)){
                    const QPointF oldCenter=text.bounds.center();
                    const QPointF delta=rotatePt(oldCenter)-oldCenter;
                    text.pos+=delta;text.bounds.translate(delta);
                }
                for(auto& image:m_images)if(!image.locked&&m_selectedImageIds.contains(image.id)){
                    const QPointF oldCenter=image.rect.center();
                    image.rect.translate(rotatePt(oldCenter)-oldCenter);
                }
                invalidateStaticCache();
            }
        } else {
            auto bounds=computeSelectionBounds(m_snapshotBeforeLassoTransform.strokes,m_snapshotBeforeLassoTransform.shapes,m_selectedStrokeIds,m_selectedShapeIds);
            auto addRect=[&](const QRectF& r){ bounds=bounds.isNull()?r:bounds.united(r); };
            for(const auto& text:m_snapshotBeforeLassoTransform.texts)if(m_selectedTextIds.contains(text.id))addRect(text.bounds);
            for(const auto& image:m_snapshotBeforeLassoTransform.images)if(m_selectedImageIds.contains(image.id))addRect(image.rect);
            const QVector<QPointF> corners{bounds.topLeft(),bounds.topRight(),bounds.bottomRight(),bounds.bottomLeft()};
            const auto diagonal=corners[m_lassoHandleIndex]-m_lassoTransformCenter,target=diagonal+world-m_lassoDragStartWorld;
            const double denominator=diagonal.x()*diagonal.x()+diagonal.y()*diagonal.y();
            const double scale=std::clamp(denominator>1e-10?(diagonal.x()*target.x()+diagonal.y()*target.y())/denominator:1.0,.15,12.0);
            scaleSelection(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds,m_lassoTransformCenter,scale,scale);
            if(!m_selectedTextIds.isEmpty()||!m_selectedImageIds.isEmpty()) {
                m_texts=m_snapshotBeforeLassoTransform.texts;m_images=m_snapshotBeforeLassoTransform.images;
                for(auto& text:m_texts)if(!text.locked&&m_selectedTextIds.contains(text.id)){
                    const QPointF oldCenter=text.bounds.center();
                    const QPointF newCenter=m_lassoTransformCenter+(oldCenter-m_lassoTransformCenter)*scale;
                    text.fontSize=std::clamp(text.fontSize*scale,6.0,240.0);
                    text.pos=m_lassoTransformCenter+(text.pos-m_lassoTransformCenter)*scale;
                    text.bounds=textBounds(text);
                    const QPointF drift=newCenter-text.bounds.center();
                    text.pos+=drift;
                    text.bounds.translate(drift);
                }
                for(auto& image:m_images)if(!image.locked&&m_selectedImageIds.contains(image.id)){
                    const QPointF tl=m_lassoTransformCenter+(image.rect.topLeft()-m_lassoTransformCenter)*scale;
                    const QPointF br=m_lassoTransformCenter+(image.rect.bottomRight()-m_lassoTransformCenter)*scale;
                    image.rect=QRectF(tl,br).normalized();
                }
                invalidateStaticCache();
            }
        }
        if(!m_selectedShapeIds.isEmpty())updateLinkedPreview(m_snapshotBeforeLassoTransform.shapes,m_selectedShapeIds);
        m_lassoBounds=lassoSelectionBounds();update();return;
    }
    if(m_mode==InteractionMode::ImageMove||m_mode==InteractionMode::ImageResize) {
        m_images=m_snapshotBeforeImageEdit.images;
        for(auto& image:m_images)if(image.id==m_selectedImageId&&!image.isPdfPage) {
            if(m_mode==InteractionMode::ImageMove)image.rect=m_imageOriginalRect.translated(world-m_imageDragStartWorld);
            else {
                const QVector<QPointF> corners{m_imageOriginalRect.topLeft(),m_imageOriginalRect.topRight(),m_imageOriginalRect.bottomRight(),m_imageOriginalRect.bottomLeft()};
                const auto anchor=corners[(m_imageHandleIndex+2)%4],diagonal=corners[m_imageHandleIndex]-anchor,target=diagonal+world-m_imageDragStartWorld;
                const double denominator=diagonal.x()*diagonal.x()+diagonal.y()*diagonal.y();
                const double scale=std::clamp(denominator>1e-10?(diagonal.x()*target.x()+diagonal.y()*target.y())/denominator:1.0,.05,20.0);
                image.rect=QRectF(anchor,anchor+diagonal*scale).normalized();
            }
        }
        update();return;
    }
    if(m_mode==InteractionMode::ShapeMove||m_mode==InteractionMode::ShapeDragHandle||m_mode==InteractionMode::ShapeScale) {
        m_shapes=m_snapshotBeforeShapeEdit.shapes;
        if(auto* shape=findShapeById(m_selectedShapeId)) {
            shape->vertices=m_shapeDragOriginalVertices;
            shape->kind=m_shapeDragOriginalKind;
            const auto delta=world-m_shapeDragStartWorld;
            if(m_tool==ToolType::Select&&std::hypot(delta.x(),delta.y())*m_viewport.zoom<=.5) {
                m_activeGuides.clear();
                shape->bounds=computeShapeBounds(*shape);
            } else if(m_mode==InteractionMode::ShapeScale) {
                const auto box=shapeGeometryBounds(*shape);
                const bool horizontal=m_activeHandleIndex<2;
                const bool towardStart=m_activeHandleIndex%2==0;
                const QPointF anchor=horizontal
                    ? QPointF(towardStart?box.right():box.left(),box.center().y())
                    : QPointF(box.center().x(),towardStart?box.bottom():box.top());
                const double oldSize=horizontal?box.width():box.height();
                if(oldSize<1e-6)return;
                const double change=(towardStart?-1:1)*(horizontal?delta.x():delta.y());
                const double scale=std::clamp(1+change/oldSize,.05,20.0);
                scaleShape(*shape,anchor,horizontal?scale:1,horizontal?1:scale);
                const auto next=shapeGeometryBounds(*shape);
                const double guideMargin=12/m_viewport.zoom;
                if(horizontal) {
                    const double x=towardStart?next.left():next.right();
                    m_activeGuides={{QPointF(x,next.top()-guideMargin),QPointF(x,next.bottom()+guideMargin),{},false}};
                } else {
                    const double y=towardStart?next.top():next.bottom();
                    m_activeGuides={{QPointF(next.left()-guideMargin,y),QPointF(next.right()+guideMargin,y),{},false}};
                }
            } else if(m_mode==InteractionMode::ShapeDragHandle) {
                const auto raw=(m_tool==ToolType::Select)
                    ? (getShapeHandles(*shape).at(m_activeHandleIndex)+delta)
                    : world;
                if (isCongruenceMarkShape(*shape)
                    && constrainCongruenceMarkToEdge(*shape, m_shapes, m_activeHandleIndex, raw, delta)) {
                    m_activeGuides.clear();
                } else {
                    const auto anchor=handleAnchor(*shape,m_activeHandleIndex);
                    const auto external=snapPointWithGuides(raw,anchor,m_shapes,shape->id,m_viewport.zoom,m_smartGuidesEnabled);
                    const auto result=snapShapeVertex(*shape,m_activeHandleIndex,external,raw,m_viewport.zoom);
                    m_activeGuides=result.guides;
                    applyHandleDrag(*shape,m_activeHandleIndex,result.point);
                }
            } else {
                if (isCongruenceMarkShape(*shape)
                    && constrainCongruenceMarkToEdge(*shape, m_shapes, -1, world, delta)) {
                    m_activeGuides.clear();
                } else {
                    const auto origin=m_shapeDragOriginalVertices.front();
                    const auto target=snapped(origin+delta,std::nullopt,shape->id);
                    translateShape(*shape,target-origin);
                }
            }
        }
        updateLinkedPreview(m_snapshotBeforeShapeEdit.shapes,{m_selectedShapeId});
        update();return;
    }
    if(m_mode==InteractionMode::ShapeCreate&&m_hasActiveShape) {
        if(m_holdSnappedToShape) {
            if(distance(world,m_shapeDragStartWorld)*m_viewport.zoom<=1) {
                m_activeShape.vertices=m_shapeDragOriginalVertices;m_activeShape.bounds=computeShapeBounds(m_activeShape);update();return;
            }
            m_activeShape.vertices=m_shapeDragOriginalVertices;
            const auto anchor = handleAnchor(m_activeShape, m_activeHandleIndex);
            const auto external = snapPointWithGuides(world, anchor, m_shapes, m_activeShape.id, m_viewport.zoom, m_smartGuidesEnabled);
            const auto result = m_smartGuidesEnabled
                ? snapShapeVertex(m_activeShape, m_activeHandleIndex, external, world, m_viewport.zoom)
                : external;
            m_activeGuides = result.guides;
            applyHandleDrag(m_activeShape, m_activeHandleIndex, result.point);
        } else {
            // Only snap angles for Line shapes (not Rectangles, Circles, Ellipses, Triangles!)
            const bool allowAngleSnap = (m_activeShape.kind == ShapeKind::Line);
            const auto anchor = allowAngleSnap ? std::optional<QPointF>(m_shapeCreateOrigin) : std::nullopt;
            const auto end = snapped(world, anchor, m_activeShape.id);
            QPointF start = m_shapeCreateOrigin;
            if (drawsFromEndpoints(m_activeShape.kind)
                && distance(world, m_shapeDragStartWorld) * m_viewport.zoom > 3.0) {
                // Resolve contacts in both drawing directions, including rim to center.
                const auto reverse = snapPointWithGuides(m_shapeDragStartWorld, end, m_shapes,
                                                        m_activeShape.id, m_viewport.zoom, false);
                if (reverse.contact)
                    start = reverse.point;
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
                const int variant = mods.testFlag(Qt::ShiftModifier) && m_triangleVariant == 0 ? 1 : m_triangleVariant;
                if (variant == 1) // rectángulo: ángulo recto abajo a la izquierda
                    m_activeShape.vertices = {r.topLeft(), r.bottomRight(), r.bottomLeft()};
                else if (variant == 3) { // equilátero: base abajo, altura = lado·√3/2
                    const double h = r.width() * std::sqrt(3.0) / 2.0;
                    m_activeShape.vertices = {{r.center().x(), r.bottom() - h}, r.bottomRight(), r.bottomLeft()};
                }
            }
        }
        update();return;
    }
    if(m_mode==InteractionMode::Draw&&m_hasActiveStroke) {
        m_holdLastWorld=world;
        if (m_holdToShapeEnabled && !m_holdSnappedToShape
            && (m_activeStroke.tool == ToolType::Pen || m_activeStroke.tool == ToolType::Ballpoint
                || (m_activeStroke.tool == ToolType::Highlighter && m_activeStroke.tipStyle != 2))
            && distance(world, m_holdAnchorWorld) * m_viewport.zoom > 4) {
            m_holdAnchorWorld = world;
            m_holdTimer->start();
        }
        auto point = m_stabilizerFilter->filter(inputPoint(world, pressure, tiltX, tiltY));
        if (mods.testFlag(Qt::ShiftModifier) || m_holdSnappedToShape) {
            m_liveInk.reset();
            m_liveInkPending = false;
            point = snappedPoint(m_rawStartPoint, point);
            m_activeStroke.points = {m_rawStartPoint, point};
        } else if (m_activeStroke.tool == ToolType::Highlighter && m_activeStroke.tipStyle == 2) {
            m_activeStroke.points = {m_rawStartPoint, point};
        } else {
            const auto& last = m_activeStroke.points.back();
            if (distance({point.x, point.y}, {last.x, last.y}) <= .001
                && std::abs(point.pressure - last.pressure) <= .001
                && std::abs(point.tiltX - last.tiltX) <= .01 && std::abs(point.tiltY - last.tiltY) <= .01)
                return;
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
        const double zoom = m_viewport.zoom;
        const QPointF offset(m_viewport.x, m_viewport.y);
        const QRectF screenBounds(m_liveStrokeBounds.topLeft() * zoom + offset,
                                  m_liveStrokeBounds.bottomRight() * zoom + offset);
        // Catmull-Rom puede exceder los puntos hasta 1/6 del rango de cada eje.
        // El margen fijo cubre la punta predicha y el antialiasing.
        const double pad = m_activeStroke.size * zoom + 20.0;
        const double padX = screenBounds.width() / 6.0 + pad;
        const double padY = screenBounds.height() / 6.0 + pad;
        update(screenBounds.adjusted(-padX, -padY, padX, padY).toAlignedRect());
        return;
    }
    if(m_polylineActive) {
        m_polylineSnapClose=m_polylineVertices.size()>=3&&distance(world,m_polylineVertices.front())*m_viewport.zoom<=14;
        m_polylineHoverWorld=m_polylineSnapClose?m_polylineVertices.front():snapped(world,m_polylineVertices.back(),m_activeShape.id);
        if(m_polylineSnapClose)m_activeGuides.clear();
        m_activeShape.vertices=m_polylineVertices;
        if(distance(m_activeShape.vertices.back(),m_polylineHoverWorld)>.001)m_activeShape.vertices.append(m_polylineHoverWorld);
        m_activeShape.closed=false;m_activeShape.bounds=computeShapeBounds(m_activeShape);update();return;
    }
    if(m_tool==ToolType::Select&&!m_spaceHeld&&!eraser) {
        const auto* shape=std::as_const(*this).findShapeById(m_selectedShapeId);
        const int side=shape?hitTestResizeHandle(*shape,world,m_viewport.zoom):-1;
        const auto cursor=side<0?Qt::ArrowCursor:side<2?Qt::SizeHorCursor:Qt::SizeVerCursor;
        if(this->cursor().shape()!=cursor)setCursor(cursor);
    }
    updatePenCursor(world,m_mode==InteractionMode::None&&!eraser&&!m_spaceHeld&&
        (m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint||m_tool==ToolType::Highlighter||m_tool==ToolType::Laser));
    const bool wantsEraserCursor = eraser || m_tool == ToolType::Eraser;
    if (wantsEraserCursor) {
        m_showEraserCursor = true;
        m_eraserWorldPos = world;
        update();
    } else if (m_showEraserCursor) {
        m_showEraserCursor = false;
        update();
    }
}
void CanvasWidget::stampCongruenceMarkAt(const QPointF& worldPos, const QString& markSpec) {
    if (markSpec.trimmed().isEmpty())
        return;
    auto candidate = m_shapes;
    QString markId;
    if (!stampOrCycleCongruenceMark(candidate, worldPos, markSpec, m_color, m_size, m_viewport.zoom, &markId))
        return;
    pushUndoSnapshot(currentSnapshot());
    m_shapes = std::move(candidate);
    clearSelection();
    if (!markId.isEmpty()) {
        if (const auto* placed = findShapeById(markId)) {
            m_shapePulseAnimation->stop();
            m_shapePulseShape = *placed;
            m_shapePulseAlpha = 1.0;
            m_shapePulseAnimation->start();
            emit toastRequested(congruenceMarkToast(placed->label));
        }
    }
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
    update();
}
void CanvasWidget::triggerHoldToShape() {
    if(!m_holdToShapeEnabled||m_mode!=InteractionMode::Draw||!m_hasActiveStroke)return;
    const bool isHighlighter = (m_activeStroke.tool == ToolType::Highlighter && m_activeStroke.tipStyle != 2);
    if(!isHighlighter && m_activeStroke.tool!=ToolType::Pen && m_activeStroke.tool!=ToolType::Ballpoint)return;
    double total=0;for(qsizetype i=1;i<m_activeStroke.points.size();++i)total+=distance({m_activeStroke.points[i-1].x,m_activeStroke.points[i-1].y},{m_activeStroke.points[i].x,m_activeStroke.points[i].y});
    if (isHighlighter) {
        if (total * m_viewport.zoom < 16.0) return;
        m_holdSnappedToShape = true;
        m_activeStroke.points = {m_rawStartPoint, snappedPoint(m_rawStartPoint, m_activeStroke.points.back())};
        m_holdTimer->stop();
        update();
        return;
    }
    // 1. Mantener presionado un punto sobre una arista -> abrir mini-barra rápida de marcas de congruencia
    if (total * m_viewport.zoom < 16.0) {
        QPointF edgePt;
        if (isPointNearGeometryEdge(m_holdLastWorld, m_shapes, m_viewport.zoom, 22.0, &edgePt)) {
            m_hasActiveStroke = false;
            m_liveInk.reset();
            m_liveInkLayer = QImage();
            m_mode = InteractionMode::None;
            m_holdTimer->stop();
            update();
            const QPointF screenPos(edgePt.x() * m_viewport.zoom + m_viewport.x,
                                    edgePt.y() * m_viewport.zoom + m_viewport.y);
            emit edgeMarkMenuRequested(screenPos, edgePt);
        }
        return;
    }
    if(const auto autoAngle=trySnapAngleArcAtVertex(m_activeStroke,m_shapes,m_viewport.zoom)) {
        pushUndoSnapshot(currentSnapshot());
        m_shapes.append(*autoAngle);
        attachNewShape(m_shapes);
        m_hasActiveStroke=false;
        m_mode=InteractionMode::None;
        m_holdTimer->stop();
        m_shapePulseAnimation->stop();
        m_shapePulseShape=*autoAngle;
        m_shapePulseAlpha=1.0;
        m_shapePulseAnimation->start();
        emit toastRequested(QStringLiteral("Ángulo ✓"));
        invalidateStaticCache();
        saveAutosave();
        emit stateChanged();
        update();
        return;
    }
    if(total*m_viewport.zoom<25)return;
    const auto recognized=recognizeFreehandShape(m_activeStroke);if(!recognized)return;
    // Nunca convertir trazos cortos sobre una arista en una Curva accidental
    if (recognized->kind == ShapeKind::Curve) {
        const QRectF sb = computeBounds(m_activeStroke.points, 0.0);
        const double dim = std::max(sb.width(), sb.height());
        if (dim * m_viewport.zoom < 150.0
            && isPointNearGeometryEdge(sb.center(), m_shapes, m_viewport.zoom, 30.0)) {
            return;
        }
    }
    m_activeShape=*recognized;m_hasActiveShape=true;m_hasActiveStroke=false;m_holdSnappedToShape=true;m_selectedShapeId=m_activeShape.id;
    if (drawsFromEndpoints(m_activeShape.kind)) {
        auto& points = m_activeShape.vertices;
        const QPointF oldStart = points.front(), oldEnd = points.back();
        const QPointF rawStart(m_rawStartPoint.x, m_rawStartPoint.y);
        const auto start = snapPointWithGuides(rawStart, std::nullopt, m_shapes, m_activeShape.id, m_viewport.zoom, false);
        const auto end = snapPointWithGuides(m_holdLastWorld, start.point, m_shapes, m_activeShape.id, m_viewport.zoom, false);
        if (end.contact)
            points.back() = end.point;
        const auto reverse = snapPointWithGuides(rawStart, points.back(), m_shapes, m_activeShape.id, m_viewport.zoom, false);
        if (reverse.contact)
            points.front() = reverse.point;
        if (m_activeShape.kind == ShapeKind::Curve)
            points[1] += (points.front() - oldStart + points.back() - oldEnd) * .5;
        m_activeShape.bounds = computeShapeBounds(m_activeShape);
    }
    m_snapshotBeforeShapeEdit=currentSnapshot();m_shapeDragOriginalVertices=m_activeShape.vertices;
    m_shapeDragStartWorld=m_holdLastWorld;m_activeHandleIndex=hitTestHandle(m_activeShape,m_holdLastWorld,1e12);
    if(m_activeShape.kind==ShapeKind::Circle)m_activeHandleIndex=1;
    m_mode=InteractionMode::ShapeCreate;m_holdTimer->stop();
    m_shapePulseAnimation->stop();m_shapePulseShape=m_activeShape;m_shapePulseAlpha=1.0;m_shapePulseAnimation->start();
    QString name;
    switch(m_activeShape.kind) {
    case ShapeKind::Line:name=QStringLiteral("Línea");break;
    case ShapeKind::Circle:name=QStringLiteral("Círculo");break;
    case ShapeKind::Ellipse:name=QStringLiteral("Elipse");break;
    case ShapeKind::Triangle:name=QStringLiteral("Triángulo");break;
    case ShapeKind::Rectangle:name=QStringLiteral("Rectángulo");break;
    case ShapeKind::Radical:name=QStringLiteral("Raíz √");break;
    case ShapeKind::Polyline:name=QStringLiteral("Polígono");break;
    case ShapeKind::Curve:name=QStringLiteral("Curva");break;
    case ShapeKind::RightAngle:name=QStringLiteral("Ángulo recto");break;
    case ShapeKind::Brace:name=QStringLiteral("Llave");break;
    case ShapeKind::Axes:name=QStringLiteral("Ejes");break;
    case ShapeKind::ShadedRegion:break;
    default:break;
    }
    emit toastRequested(name+QStringLiteral(" ✓"));invalidateStaticCache();emit stateChanged();
}
void CanvasWidget::endInput(const QPointF& screen,Qt::KeyboardModifiers mods) {
    m_holdTimer->stop();
    if(m_mode==InteractionMode::None)return;
    if(m_mode==InteractionMode::LaserTrail) {m_mode=InteractionMode::None;update();return;}
    if(m_mode==InteractionMode::TextMove) {
        moveInput(screen,0,0,0,false,false,mods);
        if(distance(screenToWorld(screen),m_textDragStartWorld)*m_viewport.zoom>.01)pushUndoSnapshot(m_snapshotBeforeTextEdit);
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::LassoDraw) {
        const auto world=screenToWorld(screen);if(m_lassoPolygon.isEmpty()||distance(world,m_lassoPolygon.back())>.001)m_lassoPolygon.append(world);
        selectByLasso(m_lassoPolygon,m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds);
        if(m_lassoPolygon.size()>=3) {
            for(const auto& text:std::as_const(m_texts))
                if(m_lassoPolygon.containsPoint(text.bounds.center(),Qt::OddEvenFill))m_selectedTextIds.insert(text.id);
            for(const auto& image:std::as_const(m_images))
                if(!image.isPdfPage&&m_lassoPolygon.containsPoint(image.rect.center(),Qt::OddEvenFill))m_selectedImageIds.insert(image.id);
        }
        expandLassoSelection();
        if(!hasLassoSelection()) {
            const double tapTol = 8.0 / m_viewport.zoom;
            for (auto it = m_shapes.crbegin(); it != m_shapes.crend(); ++it) {
                if (it->kind != ShapeKind::ShadedRegion && hitTestShape(*it, world, tapTol)) {
                    m_selectedShapeId = it->id;
                    break;
                }
            }
            if (m_selectedShapeId.isEmpty()) {
                for (qsizetype i = m_images.size(); i-- > 0;) {
                    if (!m_images[i].isPdfPage && m_images[i].rect.contains(world)) {
                        m_selectedImageId = m_images[i].id;
                        break;
                    }
                }
            }
        }
        m_lassoPolygon.clear();invalidateStaticCache();
    } else if (m_mode == InteractionMode::LassoMove) {
        const QPointF delta = screenToWorld(screen) - m_lassoDragStartWorld;
        m_lassoPreviewDelta = QPointF();
        if (distance(screenToWorld(screen), m_lassoDragStartWorld) * m_viewport.zoom > .01) {
            translateSelection(m_strokes, m_shapes, m_selectedStrokeIds, m_selectedShapeIds, delta);
            for (auto& text : m_texts)
                if (!text.locked && m_selectedTextIds.contains(text.id)) {
                    text.pos += delta;
                    text.bounds.translate(delta);
                }
            for (auto& image : m_images)
                if (!image.locked && m_selectedImageIds.contains(image.id))
                    image.rect.translate(delta);
            if (!m_selectedShapeIds.isEmpty())
                updateShapeLinks(m_shapes, m_snapshotBeforeLassoTransform.shapes, m_selectedShapeIds);
            m_lassoBounds = lassoSelectionBounds();
            pushUndoSnapshot(m_snapshotBeforeLassoTransform);
        }
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::LassoScale||m_mode==InteractionMode::LassoRotate) {
        moveInput(screen,0,0,0,false,false,mods);
        if(distance(screenToWorld(screen),m_lassoDragStartWorld)*m_viewport.zoom>.01)pushUndoSnapshot(m_snapshotBeforeLassoTransform);
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::ImageMove||m_mode==InteractionMode::ImageResize) {
        moveInput(screen,0,0,0,false,false,mods);
        if(distance(screenToWorld(screen),m_imageDragStartWorld)*m_viewport.zoom>.01)pushUndoSnapshot(m_snapshotBeforeImageEdit);
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::Draw&&m_hasActiveStroke) {
        const auto& last=m_activeStroke.points.back();auto end=inputPoint(screenToWorld(screen),last.pressure,last.tiltX,last.tiltY);
        if(mods.testFlag(Qt::ShiftModifier) || m_holdSnappedToShape)m_activeStroke.points={m_rawStartPoint,snappedPoint(m_rawStartPoint,end)};
        else if(m_activeStroke.tool==ToolType::Highlighter&&m_activeStroke.tipStyle==2)m_activeStroke.points={m_rawStartPoint,end};
        else if(distance({last.x,last.y},{end.x,end.y})>.001){m_activeStroke.points.append(end);m_inkSmoother.push(m_activeStroke);}
        if(!mods.testFlag(Qt::ShiftModifier)&&(m_activeStroke.tool==ToolType::Pen||m_activeStroke.tool==ToolType::Ballpoint))
            trimPenHooks(m_activeStroke,m_viewport.zoom);
        if(m_activeStroke.tool==ToolType::Pen&&m_activeStroke.tipStyle==1&&!(mods&Qt::ShiftModifier))
            taperPenFlick(m_activeStroke,m_viewport.zoom);
        if(m_activeStroke.tool==ToolType::Highlighter&&m_activeStroke.tipStyle==2&&m_activeStroke.points.size()>=2)
            m_activeStroke.points={m_activeStroke.points.front(),m_activeStroke.points.back()};
        m_liveInk.reset();m_liveInkLayer=QImage();
        m_activeStroke.bounds=computeBounds(m_activeStroke.points,m_activeStroke.size*.7);
        // Auto-ángulo en vértices o auto-marca de congruencia (serrucho/resorte) sobre lados:
        if(!mods.testFlag(Qt::ShiftModifier)
           &&(m_activeStroke.tool==ToolType::Pen||m_activeStroke.tool==ToolType::Ballpoint)) {
            if(const auto autoAngle=trySnapAngleArcAtVertex(m_activeStroke,m_shapes,m_viewport.zoom)) {
                pushUndoSnapshot(currentSnapshot());
                m_shapes.append(*autoAngle);
                attachNewShape(m_shapes);
                m_hasActiveStroke=false;
                m_shapePulseAnimation->stop();
                m_shapePulseShape=*autoAngle;
                m_shapePulseAlpha=1.0;
                m_shapePulseAnimation->start();
                invalidateStaticCache();
                m_mode=InteractionMode::None;
                m_linkedPreview=false;
                m_activeGuides.clear();
                m_activeHandleIndex=-1;
                updateCursorShape();
                saveAutosave();
                emit stateChanged();
                update();
                return;
            }
        }
        pushUndoSnapshot(currentSnapshot());
        m_strokes.append(m_activeStroke);m_hasActiveStroke=false;
        if(m_mathBeautifyEnabled&&!mods.testFlag(Qt::ShiftModifier)
           &&(m_activeStroke.tool==ToolType::Pen||m_activeStroke.tool==ToolType::Ballpoint)) {
            const QRectF nb=m_activeStroke.bounds;
            if(!m_pendingBeautifyIds.isEmpty()&&!m_pendingBeautifyBox.isEmpty()) {
                const double refH=std::clamp(m_pendingBeautifyBox.height(),18.0,80.0);
                // ¿Es un accesorio retroactivo dentro de la palabra actual (cruce de 't', punto de 'i', tilde)?
                const bool retroAccessory=(nb.right()>=m_pendingBeautifyBox.left()-6.0)
                                        &&(nb.left()<=m_pendingBeautifyBox.right()+6.0)
                                        &&(std::abs(nb.center().y()-m_pendingBeautifyBox.center().y())<=refH*0.95);
                const bool jumpedRight=(nb.left()-m_pendingBeautifyBox.right())>std::max(14.0,refH*0.42);
                const bool jumpedLeft=!retroAccessory&&(m_pendingBeautifyBox.left()-nb.right())>std::max(14.0,refH*0.45);
                const bool jumpedLine=std::abs(m_pendingBeautifyBox.center().y()-nb.center().y())>refH*1.15;
                // Si el trazo nuevo ya pertenece a la siguiente palabra o renglón, embellecer la palabra anterior
                if(jumpedRight||jumpedLeft||jumpedLine) {
                    flushPendingBeautify();
                }
            }
            if(m_pendingBeautifyStart<0)
                m_pendingBeautifyStart=int(m_strokes.size())-1;
            m_pendingBeautifyIds.insert(m_activeStroke.id);
            m_pendingBeautifyBox=m_pendingBeautifyBox.isEmpty()?nb:m_pendingBeautifyBox.united(nb);
            m_beautifyTimer->start(650);
        }
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::ShapeCreate&&m_hasActiveShape) {
        // No artificial resize on release after recognition: a held pen can lift
        // at the same physical position without changing its recognized geometry.
        if(!m_holdSnappedToShape)moveInput(screen,0,0,0,false,false,mods);
        const auto bounds=computeShapeBounds(m_activeShape);
        const bool clickedFigure = !m_holdSnappedToShape
            && distance(screenToWorld(screen), m_shapeDragStartWorld) * m_viewport.zoom <= 3.0;
        if (clickedFigure) {
            m_selectedShapeId.clear();
            for (auto it = m_shapes.crbegin(); it != m_shapes.crend(); ++it) {
                if (it->kind != ShapeKind::ShadedRegion
                    && (hitTestHandle(*it, m_shapeDragStartWorld, 9 / m_viewport.zoom) >= 0
                        || hitTestShape(*it, m_shapeDragStartWorld, 6 / m_viewport.zoom))) {
                    m_selectedShapeId = it->id;
                    break;
                }
            }
        } else if(std::max(bounds.width()-m_activeShape.size,bounds.height()-m_activeShape.size)*m_viewport.zoom>3) {
            pushUndoSnapshot(m_snapshotBeforeShapeEdit);m_activeShape.bounds=bounds;m_shapes.append(m_activeShape);attachNewShape(m_shapes);
            // Figuras cerradas quedan editables en el momento; líneas sueltan selección para encadenar segmentos.
            if (m_activeShape.kind == ShapeKind::Line && m_activeShape.linePattern != LinePattern::Dimension)
                m_selectedShapeId.clear();
            else
                m_selectedShapeId = m_activeShape.id;
        } else m_selectedShapeId.clear();
        m_hasActiveShape=false;m_holdSnappedToShape=false;invalidateStaticCache();
    } else if(m_mode==InteractionMode::ShapeMove||m_mode==InteractionMode::ShapeDragHandle||m_mode==InteractionMode::ShapeScale) {
        moveInput(screen,0,0,0,false,false,mods);
        if(const auto* shape=findShapeById(m_selectedShapeId);shape&&(shape->vertices!=m_shapeDragOriginalVertices||shape->kind!=m_shapeDragOriginalKind))pushUndoSnapshot(m_snapshotBeforeShapeEdit);
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::Erase) {
        moveInput(screen,0,0,0,false,true,mods);if(m_erasedAnything)pushUndoSnapshot(m_snapshotBeforeErase);
        m_snapshotBeforeErase={};m_erasedAnything=false;
    }
    m_mode=InteractionMode::None;m_linkedPreview=false;m_activeGuides.clear();m_activeHandleIndex=-1;updateCursorShape();saveAutosave();emit stateChanged();update();
}
void CanvasWidget::flushPendingBeautify() {
    if(m_beautifyTimer)m_beautifyTimer->stop();
    const QSet<QString> ids=m_pendingBeautifyIds;
    m_pendingBeautifyStart=-1;
    m_pendingBeautifyBox=QRectF();
    m_pendingBeautifyIds.clear();
    if(!m_mathBeautifyEnabled||m_mode!=InteractionMode::None||ids.isEmpty())
        return;
    QVector<Stroke> candidate=m_strokes;
    if(straightenSelectedStrokes(candidate,ids)) {
        pushUndoSnapshot(currentSnapshot()); // 1er Ctrl+Z vuelve al trazo original!
        m_strokes=std::move(candidate);
        invalidateStaticCache();
        saveAutosave();
        emit stateChanged();
        update();
    }
}
void CanvasWidget::applyEraseAt(const QPointF& world, bool trimToIntersections) {
    QVector<Stroke> next;bool changed=false;
    for(const auto& stroke:m_strokes) {
        if (m_eraserOnlyHighlight && stroke.tool != ToolType::Highlighter) {
            next.append(stroke);
            continue;
        }
        if(m_eraserMode==EraserMode::Stroke){if(hitTestStroke(stroke,world,m_eraserSize/2))changed=true;else next.append(stroke);}
        else {const auto split=eraseStrokePartially(stroke,world,m_eraserSize/2);if(split){changed=true;next+=*split;}else next.append(stroke);}
    }
    if (m_eraserOnlyHighlight) {
        if (changed) {
            m_strokes = std::move(next);
            m_erasedAnything = true;
            invalidateStaticCache();
        }
        return;
    }
    auto shapes=m_shapes;
    auto eraseShapes=[&](bool shading) {
        if (m_eraserPreserveShapes && !shading)
            return;
        if (!shading && (m_eraserMode == EraserMode::Partial || trimToIntersections)) {
            QVector<ShapeItem> nextShapes;
            nextShapes.reserve(shapes.size());
            for (const auto& shape : shapes) {
                if (shape.locked || shape.kind == ShapeKind::ShadedRegion) {
                    nextShapes.append(shape);
                    continue;
                }
                const bool partialSupported = (shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Polyline
                    || shape.kind == ShapeKind::Triangle || shape.kind == ShapeKind::Rectangle
                    || shape.kind == ShapeKind::Circle || shape.kind == ShapeKind::Ellipse
                    || shape.kind == ShapeKind::Curve || shape.kind == ShapeKind::Arc);
                if (partialSupported) {
                    if (const auto split = eraseShapePartially(shape, shapes, world, m_eraserSize / 2.0, trimToIntersections)) {
                        changed = true;
                        if (shape.id == m_selectedShapeId) m_selectedShapeId.clear();
                        nextShapes += *split;
                    } else {
                        nextShapes.append(shape);
                    }
                } else if (hitTestShape(shape, world, m_eraserSize / 2.0)) {
                    changed = true;
                    if (shape.id == m_selectedShapeId) m_selectedShapeId.clear();
                } else {
                    nextShapes.append(shape);
                }
            }
            shapes = std::move(nextShapes);
            return;
        }
        const auto count=shapes.removeIf([&](const ShapeItem& shape) {
            if(shape.locked||(shape.kind==ShapeKind::ShadedRegion)!=shading||!hitTestShape(shape,world,m_eraserSize/2))return false;
            if(shape.id==m_selectedShapeId)m_selectedShapeId.clear();
            return true;
        });
        if(count)changed=true;
    };
    eraseShapes(false);
    const QRectF eraserRect(world-QPointF(m_eraserSize/2,m_eraserSize/2),QSizeF(m_eraserSize,m_eraserSize));
    const auto removed=m_texts.removeIf([&](const TextItem& item){return !item.locked&&item.bounds.intersects(eraserRect);});
    if(removed){changed=true;if(std::none_of(m_texts.cbegin(),m_texts.cend(),[&](const TextItem& item){return item.id==m_selectedTextId;}))m_selectedTextId.clear();}
    if(!changed)eraseShapes(true);
    if(changed){
        if(shapes.size()!=m_shapes.size())updateShapeLinks(shapes,m_shapes,{});
        m_strokes=std::move(next);m_shapes=std::move(shapes);m_erasedAnything=true;invalidateStaticCache();
    }
}
void CanvasWidget::tabletEvent(QTabletEvent* event) {
    event->accept();
    const bool eraser=event->pointerType()==QPointingDevice::PointerType::Eraser;
    const Qt::MouseButtons btns = event->buttons();
    const Qt::MouseButton btn = event->button();

    // Wacom Intuos BT S Pen 4K barrel buttons (calibrated to Yordi's grip):
    // - Larger barrel button (RightButton by default)  => switch to Eraser!
    // - Smaller barrel button (MiddleButton by default) => switch back to Writing (last pen)!
    // (Can be flipped anytime via "Invertir botones del lápiz Wacom" in Trazo popover)
    const Qt::MouseButton eraseBtn = m_swapPenButtons ? Qt::MiddleButton : Qt::RightButton;
    const Qt::MouseButton writeBtn = m_swapPenButtons ? Qt::RightButton : Qt::MiddleButton;

    const bool newlyPressedErase = ((btns & eraseBtn) && !(m_lastTabletButtons & eraseBtn));
    const bool newlyPressedWrite = ((btns & writeBtn) && !(m_lastTabletButtons & writeBtn));
    m_lastTabletButtons = btns;

    if (event->type() != QEvent::TabletRelease) {
        if (newlyPressedErase && event->pressure() <= 0.01) {
            if (m_radialOpen) { m_radialOpen = false; updateCursorShape(); update(); }
            switchTabletTool(ToolType::Eraser);
            return;
        }
        if (newlyPressedWrite && event->pressure() <= 0.01) {
            const qint64 now = nowMs();
            if (m_radialOpen) {
                m_radialOpen = false;
                updateCursorShape();
                update();
                return;
            }
            // 1 toque: SIEMPRE vuelve al lápiz inmediatamente (con debounce 70ms para evitar doble evento Wayland).
            // 2 toques seguidos (70..420 ms) en el aire: abre la rueda de colores con puntero visible!
            const qint64 dt = now - m_lastPenTriggerMs;
            if (m_mode == InteractionMode::None && m_lastPenTriggerMs > 0 && dt >= 70 && dt <= 420) {
                m_lastPenTriggerMs = 0;
                clearPenCursor();
                m_radialOpen = true;
                m_radialHoveredColor = -1;
                QPointF c = event->position();
                c.setX(std::clamp(c.x(), 90.0, std::max(90.0, width() - 90.0)));
                c.setY(std::clamp(c.y(), 90.0, std::max(90.0, height() - 90.0)));
                m_radialCenter = c;
                m_radialCursorPos = c;
                updateCursorShape();
                update();
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

    // Also allow holding the erase barrel button while touching the tablet to erase immediately
    const bool useEraser = eraser || ((btns & eraseBtn) != 0);

    switch(event->type()) {
    case QEvent::TabletPress:
        if (btn == eraseBtn && event->pressure() <= 0.01) { switchTabletTool(ToolType::Eraser); return; }
        if (btn == writeBtn && event->pressure() <= 0.01) { switchTabletTool(m_lastWriteTool); return; }
        m_tabletActive=true;beginInput(event->position(),event->pressure(),event->xTilt(),event->yTilt(),true,useEraser,Qt::LeftButton,event->modifiers());break;
    case QEvent::TabletMove:
        moveInput(event->position(),event->pressure(),event->xTilt(),event->yTilt(),true,useEraser,event->modifiers());break;
    case QEvent::TabletRelease:
        if(m_tabletActive) {
            moveInput(event->position(),event->pressure(),event->xTilt(),event->yTilt(),true,useEraser,event->modifiers());
            // Pen-up is a contact boundary, not ADC jitter: retain the true
            // terminal pressure so EMA history cannot blunt the exit flick.
            if(m_mode==InteractionMode::Draw&&m_hasActiveStroke)
                m_activeStroke.points.back().pressure=std::clamp(event->pressure(),0.0,1.0);
            endInput(event->position(),event->modifiers());
        }
        m_tabletActive=false;break;
    default:break;
    }
}
void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    event->accept();if(m_tabletActive||event->source()!=Qt::MouseEventNotSynthesized)return;
    beginInput(event->position(),0.5,0,0,false,false,event->button(),event->modifiers());
}
void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    event->accept();
    // En Wayland, el lápiz Wacom flotando (hover) llega a veces como mouseMoveEvent sintetizado:
    // actualizar SIEMPRE el puntero de lápiz cuando no estamos dibujando!
    if(m_mode==InteractionMode::None&&!m_tabletActive) {
        if(m_radialOpen) {
            updateRadialHover(event->position());
            return;
        }
        const auto world=screenToWorld(event->position());
        updatePenCursor(world,!m_spaceHeld&&
            (m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint||m_tool==ToolType::Highlighter||m_tool==ToolType::Laser));
    }
    if(m_tabletActive||event->source()!=Qt::MouseEventNotSynthesized)return;
    moveInput(event->position(),0.5,0,0,false,false,event->modifiers());
}
void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    event->accept();if(m_tabletActive||event->source()!=Qt::MouseEventNotSynthesized)return;
    if((m_tool==ToolType::Text||m_tool==ToolType::Lasso||m_tool==ToolType::Select)&&event->button()==Qt::LeftButton) {
        const auto world=screenToWorld(event->position());
        if(std::any_of(m_texts.cbegin(),m_texts.cend(),[&](const TextItem& item){return item.bounds.adjusted(-4,-4,4,4).contains(world);})) {
            m_mode=InteractionMode::None;setProperty("_finily_edit_text",true);
            beginInput(event->position(),.5,0,0,false,false,Qt::LeftButton,event->modifiers());return;
        }
    }
    if(m_tool==ToolType::Polyline&&m_polylineActive&&event->button()==Qt::LeftButton) {
        const auto world=screenToWorld(event->position());
        if(m_polylineVertices.size()>=3&&distance(world,m_polylineVertices.front())*m_viewport.zoom<=14)
            finishActivePolyline(true);
        else {
            if(distance(world,m_polylineVertices.back())*m_viewport.zoom>2)
                beginInput(event->position(),.5,0,0,false,false,Qt::LeftButton,event->modifiers());
            finishActivePolyline();
        }
    }
}
void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    event->accept();if(m_tabletActive||event->source()!=Qt::MouseEventNotSynthesized)return;
    // Another mouse button released during a pan must not end the drag.
    if(m_mode==InteractionMode::Pan && (event->buttons()&(Qt::LeftButton|Qt::MiddleButton|Qt::RightButton)))return;
    if(m_mode==InteractionMode::Draw&&event->button()!=Qt::LeftButton)return;
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
    endInput(event->position(),event->modifiers());
}
void CanvasWidget::wheelEvent(QWheelEvent* event) {
    event->accept();
    m_zoomAnimation->stop();
    if (m_mode != InteractionMode::None)
        return;
    const auto mods = event->modifiers();
    if (mods.testFlag(Qt::ControlModifier)) {
        const double delta = event->pixelDelta().isNull() ? event->angleDelta().y() : event->pixelDelta().y() * 2.0;
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
    m_viewport.x += scrollDelta.x();
    m_viewport.y += scrollDelta.y();
    clampFiniteViewport();
    invalidateStaticCache();
    saveAutosave();
}
void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if(event->key()==Qt::Key_0&&event->modifiers().testFlag(Qt::ControlModifier)){resetViewport();event->accept();return;}
    if(event->key()==Qt::Key_P&&!event->isAutoRepeat()&&event->modifiers()==Qt::NoModifier) {
        if(m_radialOpen) {
            m_radialOpen=false;
            updateCursorShape();
            update();
            event->accept();return;
        }
        const qint64 now=nowMs();
        const qint64 dt=now-m_lastPenTriggerMs;
        // 1 toque de P: SIEMPRE vuelve al lápiz inmediatamente.
        // 2 toques seguidos (70..420 ms): abre la rueda de colores con puntero visible!
        if(m_mode==InteractionMode::None&&m_lastPenTriggerMs>0&&dt>=70&&dt<=420) {
            m_lastPenTriggerMs=0;
            m_radialOpen=true;
            m_radialHoveredColor=-1;
            QPointF c=m_showPenCursor
                ? (m_hoverWorldPos*m_viewport.zoom+QPointF(m_viewport.x,m_viewport.y))
                : QPointF(mapFromGlobal(QCursor::pos()));
            if(!rect().contains(c.toPoint())) c=QPointF(width()/2.0,height()/2.0);
            c.setX(std::clamp(c.x(),90.0,std::max(90.0,width()-90.0)));
            c.setY(std::clamp(c.y(),90.0,std::max(90.0,height()-90.0)));
            m_radialCenter=c;
            m_radialCursorPos=c;
            updateCursorShape();
            update();
        } else {
            m_lastPenTriggerMs=now;
            setTool(m_lastWriteTool);
        }
        event->accept();return;
    }
    if(event->key()==Qt::Key_Space){clearPenCursor();m_spaceHeld=true;updateCursorShape();event->accept();return;}
    if(m_polylineActive&&(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)){finishActivePolyline();event->accept();return;}
    if(event->key()==Qt::Key_Escape) {
        cancelInteraction();
        event->accept();return;
    }
    if(event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace){deleteSelection();event->accept();return;}
    if(event->matches(QKeySequence::Copy)){copySelection();event->accept();return;}
    if(event->matches(QKeySequence::Cut)){cutSelection();event->accept();return;}
    if(event->matches(QKeySequence::Paste)){pasteFromClipboard();event->accept();return;}
    if(event->key()==Qt::Key_D&&event->modifiers().testFlag(Qt::ControlModifier)){duplicateSelection();event->accept();return;}
    if(event->matches(QKeySequence::Undo)){undo();event->accept();return;}
    if(event->matches(QKeySequence::Redo)){redo();event->accept();return;}
    if(pageCount()>1&&!hasLassoSelection()&&!hasSelectedShape()&&!hasSelectedText()&&!hasSelectedImage()) {
        if(event->key()==Qt::Key_Left){prevPage();event->accept();return;}
        if(event->key()==Qt::Key_Right){nextPage();event->accept();return;}
    }
    QWidget::keyPressEvent(event);
}
void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if(event->key()==Qt::Key_Space&&!event->isAutoRepeat()){m_spaceHeld=false;updateCursorShape();event->accept();return;}
    QWidget::keyReleaseEvent(event);
}
void CanvasWidget::focusOutEvent(QFocusEvent* event) {
    m_spaceHeld=false;
    if(m_radialOpen){m_radialOpen=false;update();}
    updateCursorShape();
    QWidget::focusOutEvent(event);
}
void CanvasWidget::switchTabletTool(ToolType tool) {
    if(m_tool==tool)return;
    setTool(tool);
    emit toastRequested(tool==ToolType::Eraser?QStringLiteral("Borrador"):
        tool==ToolType::Highlighter?QStringLiteral("Resaltador"):
        tool==ToolType::Ballpoint?QStringLiteral("Bolígrafo gel"):QStringLiteral("Pluma fuente"));
}
void CanvasWidget::leaveEvent(QEvent* event) {
    clearPenCursor();
    if(m_showEraserCursor){m_showEraserCursor=false;update();}
    QWidget::leaveEvent(event);
}
void CanvasWidget::dragEnterEvent(QDragEnterEvent* event) {
    if(event->mimeData()&&(event->mimeData()->hasImage()||event->mimeData()->hasUrls()))
        event->acceptProposedAction();
}
void CanvasWidget::dropEvent(QDropEvent* event) {
    const QMimeData* mime=event->mimeData();
    if(!mime)return;
    if(mime->hasUrls()) {
        for(const QUrl& url:mime->urls()) {
            const QString local=url.toLocalFile();
            if(local.isEmpty())continue;
            if(local.endsWith(QStringLiteral(".pdf"),Qt::CaseInsensitive))
                importPdfFile(local);
            else
                importImageFile(local);
        }
        event->acceptProposedAction();
    } else if(mime->hasImage()) {
        const QImage img=qvariant_cast<QImage>(mime->imageData());
        if(!img.isNull()) {
            QGuiApplication::clipboard()->setImage(img);
            pasteFromClipboard();
            event->acceptProposedAction();
        }
    }
}
void CanvasWidget::updateCursorShape() {
    if(m_radialOpen){setCursor(Qt::ArrowCursor);return;}
    if(m_mode==InteractionMode::Pan)setCursor(Qt::ClosedHandCursor);
    else if(m_spaceHeld||m_tool==ToolType::Hand)setCursor(Qt::OpenHandCursor);
    else if(m_tool==ToolType::Eraser||m_mode==InteractionMode::Erase
            ||m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint
            ||m_tool==ToolType::Highlighter||m_tool==ToolType::Laser)setCursor(Qt::BlankCursor);
    else if(m_tool==ToolType::Select)setCursor(Qt::ArrowCursor);
    else setCursor(Qt::CrossCursor);
}

} // namespace finily
