#include "CanvasWidget.h"
#include "engine/InkEngine.h"
#include "engine/GeometryEngine.h"
#include "engine/HandwritingEngine.h"
#include "engine/LassoEngine.h"
#include "engine/PdfEngine.h"
#include <QSettings>
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImageReader>
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
#include <QPainter>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTabletEvent>
#include <QTimer>
#include <QUuid>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace finily {
namespace {
// Transient highlighter layers live with the widget without expanding its public
// interface. Keeping ink separate lets a live highlight move beneath cached ink.
class HighlighterLayers final : public QObject {
public:
    explicit HighlighterLayers(CanvasWidget* owner):QObject(owner) { setObjectName(QStringLiteral("_finily_highlighter_layers")); }
    QImage background;
    QImage ink;
};
HighlighterLayers& highlighterLayers(CanvasWidget* widget) {
    auto* child=widget->findChild<QObject*>(QStringLiteral("_finily_highlighter_layers"),Qt::FindDirectChildrenOnly);
    return child?*static_cast<HighlighterLayers*>(child):*new HighlighterLayers(widget);
}
Viewport fittedSheetViewport(PageFormat format,const QSize& size) {
    const auto sheet=sheetRectForPageFormat(format);
    if(sheet.isEmpty())return {};
    const double zoom=std::clamp(std::min(std::max(1.0,size.width()-96.0)/sheet.width(),
        std::max(1.0,size.height()-96.0)/sheet.height()),0.2,5.0);
    return {size.width()/2.0-sheet.center().x()*zoom,size.height()/2.0-sheet.center().y()*zoom,zoom};
}
qint64 nowMs() { static const QElapsedTimer timer=[] { QElapsedTimer t;t.start();return t; }();return timer.elapsed(); }
QString autosavePath() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+QStringLiteral("/autosave.json"); }
QJsonObject strokeJson(const Stroke& s) {
    QJsonArray points;
    for (const auto& p:s.points) points.append(QJsonArray{p.x,p.y,p.pressure,double(p.timestampMs),p.tiltX,p.tiltY});
    return {{"id",s.id},{"tool",int(s.tool)},{"color",s.color.name(QColor::HexArgb)},{"size",s.size},
            {"opacity",s.opacity},{"linePattern",int(s.linePattern)},{"pressureProfile",int(s.pressureProfile)},{"hasHardwarePressure",s.hasHardwarePressure},{"points",points}};
}
bool finite(double v) { return std::isfinite(v); }
StrokePoint inputPoint(QPointF p,double pressure,double tiltX=0.0,double tiltY=0.0) { return {p.x(),p.y(),std::clamp(pressure,0.0,1.0),nowMs(),std::clamp(tiltX,-60.0,60.0),std::clamp(tiltY,-60.0,60.0)}; }
StrokePoint snappedPoint(const StrokePoint& origin,StrokePoint point) {
    const double dx=point.x-origin.x,dy=point.y-origin.y;
    const double angle=std::round(std::atan2(dy,dx)/(std::numbers::pi/12))*(std::numbers::pi/12);
    const double dist=std::hypot(dx,dy);
    point.x=origin.x+dist*std::cos(angle);point.y=origin.y+dist*std::sin(angle);return point;
}
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
double distance(QPointF a,QPointF b) { return std::hypot(a.x()-b.x(),a.y()-b.y()); }
QJsonObject shapeJson(const ShapeItem& shape) {
    QJsonArray vertices;for(auto p:shape.vertices)vertices.append(QJsonArray{p.x(),p.y()});
    return {{"id",shape.id},{"kind",int(shape.kind)},{"color",shape.color.name(QColor::HexArgb)},
            {"size",shape.size},{"opacity",shape.opacity},{"linePattern",int(shape.linePattern)},
            {"closed",shape.closed},{"vertices",vertices}};
}
QFont textFont(double size) {
    QFont font(QStringLiteral("Sans Serif"));font.setPixelSize(qRound(size));return font;
}
QRectF textBounds(const TextItem& item) {
    const QFontMetricsF metrics(textFont(item.fontSize));
    const QRectF ink=metrics.boundingRect(item.text).translated(item.pos+QPointF(0,metrics.ascent()));
    return ink.united(QRectF(item.pos,QSizeF(metrics.horizontalAdvance(item.text),metrics.height())));
}
void drawTextItem(QPainter& painter,const TextItem& item) {
    painter.save();painter.setRenderHint(QPainter::TextAntialiasing);painter.setFont(textFont(item.fontSize));painter.setPen(item.color);
    painter.drawText(item.pos+QPointF(0,QFontMetricsF(painter.font()).ascent()),item.text);painter.restore();
}
class InlineTextEditor final : public QLineEdit {
public:
    explicit InlineTextEditor(QWidget* parent):QLineEdit(parent) { setObjectName(QStringLiteral("_finily_text_editor")); }
    std::function<void(const QString&)> commit;
    void finish(bool accept) {
        if(done)return;
        done=true;const auto value=text();hide();if(accept&&commit)commit(value);deleteLater();
    }
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Escape){finish(false);event->accept();return;}
        if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter){finish(true);event->accept();return;}
        QLineEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override { QLineEdit::focusOutEvent(event);finish(true); }
private:
    bool done=false;
};
void finishTextEditor(CanvasWidget* widget,bool accept) {
    for(auto* child:widget->findChildren<QLineEdit*>(QStringLiteral("_finily_text_editor"),Qt::FindDirectChildrenOnly))
        static_cast<InlineTextEditor*>(child)->finish(accept);
}
QJsonObject documentSettings(ShapeKind kind,bool hold,bool guides,bool swap,PressureProfile profile,ThemeId theme,BackgroundTemplate tpl,Viewport viewport,PageFormat pageFormat) {
    return {{"shapeKind",int(kind)},{"holdToShapeEnabled",hold},{"smartGuidesEnabled",guides},{"swapPenButtons",swap},
        {"pressureProfile",int(profile)},{"theme",int(theme)},{"template",int(tpl)},{"pageFormat",int(pageFormat)},
        {"viewport",QJsonObject{{"x",viewport.x},{"y",viewport.y},{"zoom",viewport.zoom}}}};
}
struct PngCache { QHash<qint64,QString> previous,next; };
PngCache& pngCache() { static PngCache cache;return cache; }
QJsonObject serializePage(const PageData& document,bool& valid) {
    QJsonArray strokes,shapes,images,texts;
    for(const auto& stroke:document.strokes)strokes.append(strokeJson(stroke));
    for(const auto& shape:document.shapes)shapes.append(shapeJson(shape));
    for(const auto& item:document.texts)texts.append(QJsonObject{{"id",item.id},{"pos",QJsonArray{item.pos.x(),item.pos.y()}},
        {"text",item.text},{"color",item.color.name(QColor::HexArgb)},{"fontSize",item.fontSize}});
    for(const auto& image:document.images) {
        QJsonObject value{{"id",image.id},{"rect",QJsonArray{image.rect.x(),image.rect.y(),image.rect.width(),image.rect.height()}},
            {"isPdfPage",image.isPdfPage},{"pdfPageIndex",image.pdfPageIndex},{"pdfSourcePath",image.pdfSourcePath}};
        // Codificar a PNG es lo más caro del guardado: se reutiliza el resultado
        // mientras los píxeles de la imagen no cambien (QImage::cacheKey).
        const qint64 key=image.image.cacheKey();QString encoded=pngCache().previous.value(key);
        if(encoded.isEmpty()) {
            QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
            if(!image.image.save(&buffer,"PNG")){valid=false;return {};}
            encoded=QString::fromLatin1(png.toBase64());
        }
        pngCache().next.insert(key,encoded);
        value.insert("png",encoded);images.append(value);
    }
    return {{"strokes",strokes},{"shapes",shapes},{"images",images},{"texts",texts},
        {"viewport",QJsonObject{{"x",document.viewport.x},{"y",document.viewport.y},{"zoom",document.viewport.zoom}}}};
}
bool serializeDocumentToFile(const QString& path,const QVector<PageData>& pages,int currentPage,QJsonObject root) {
    QJsonArray serialized;bool valid=true;pngCache().next.clear();
    for(const auto& page:pages){serialized.append(serializePage(page,valid));if(!valid)return false;}
    if(serialized.isEmpty())serialized.append(serializePage(PageData{},valid));
    // Conserva solo las imágenes que siguen en el documento.
    pngCache().previous=std::move(pngCache().next);pngCache().next.clear();
    root.remove("viewport");root.insert("version",5);root.insert("pages",serialized);root.insert("currentPageIndex",currentPage);
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    const auto data=QJsonDocument(root).toJson(QJsonDocument::Compact);
    if(file.write(data)!=data.size()){file.cancelWriting();return false;}
    return file.commit();
}
QJsonObject loadDocumentFromFile(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!doc.isObject())return {};
    const auto root=doc.object();
    const int version=root.value("version").toInt();if(version<1||version>5)return {};
    if(version<5&&!root.value("strokes").isArray())return {};
    if(version==5) {
        if(!root.value("pages").isArray()||root.value("pages").toArray().isEmpty())return {};
        for(const auto& value:root.value("pages").toArray())if(!value.isObject()||!value.toObject().value("strokes").isArray())return {};
    }
    return root;
}
PageData deserializePage(const QJsonObject& root,QHash<QString,QVector<ImageItem>>& pdfSources) {
    QVector<Stroke> loaded;
    for(const auto& value:root.value("strokes").toArray()) {
        if(!value.isObject())continue;
        const auto o=value.toObject();Stroke s;
        const int tool=o.value("tool").toInt(-1),pattern=o.value("linePattern").toInt(-1);
        if(tool<0||tool>int(ToolType::Highlighter)||pattern<0||pattern>int(LinePattern::DoubleArrow))continue;
        const int profile=o.value("pressureProfile").toInt(int(PressureProfile::Light));
        if(profile<int(PressureProfile::Light)||profile>int(PressureProfile::Firm))continue;
        s.pressureProfile=PressureProfile(profile);
        s.tool=ToolType(tool);s.linePattern=LinePattern(pattern);s.id=o.value("id").toString();
        if(s.id.isEmpty())s.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.color=QColor(o.value("color").toString());s.size=o.value("size").toDouble(2.6);s.opacity=o.value("opacity").toDouble(1.0);
        if(!s.color.isValid()||!finite(s.size)||s.size<=0||s.size>200||!finite(s.opacity))continue;
        s.opacity=std::clamp(s.opacity,0.0,1.0);s.hasHardwarePressure=o.value("hasHardwarePressure").toBool();
        bool valid=true;
        for(const auto& pv:o.value("points").toArray()) {
            const auto p=pv.toArray();if(p.size()!=4&&p.size()!=6){valid=false;break;}
            for(const auto& n:p)if(!n.isDouble()||!finite(n.toDouble()))valid=false;
            if(!valid)break;
            const double stamp=p[3].toDouble();if(std::abs(stamp)>9e15){valid=false;break;}
            s.points.append({p[0].toDouble(),p[1].toDouble(),std::clamp(p[2].toDouble(),0.0,1.0),qint64(stamp),p.size()==6?std::clamp(p[4].toDouble(),-60.0,60.0):0.0,p.size()==6?std::clamp(p[5].toDouble(),-60.0,60.0):0.0});
        }
        if(valid&&!s.points.isEmpty()){s.bounds=computeBounds(s.points,s.size*0.7);loaded.append(s);}
    }
    QVector<ShapeItem> loadedShapes;
    for(const auto& value:root.value("shapes").toArray()) {
        if(!value.isObject())continue;
        const auto o=value.toObject();const int kind=o.value("kind").toInt(-1),pattern=o.value("linePattern").toInt(0);
        if(kind<0||kind>int(ShapeKind::Axes)||pattern<0||pattern>int(LinePattern::DoubleArrow))continue;
        ShapeItem shape;shape.kind=ShapeKind(kind);shape.linePattern=LinePattern(pattern);
        shape.id=o.value("id").toString();if(shape.id.isEmpty())shape.id=freshId();
        shape.color=QColor(o.value("color").toString());shape.size=o.value("size").toDouble(2.6);shape.opacity=o.value("opacity").toDouble(1);
        if(!shape.color.isValid()||!finite(shape.size)||shape.size<=0||shape.size>200||!finite(shape.opacity))continue;
        shape.opacity=std::clamp(shape.opacity,0.0,1.0);shape.closed=o.value("closed").toBool();bool valid=true;
        for(const auto& vertex:o.value("vertices").toArray()) {
            const auto a=vertex.toArray();if(a.size()!=2||!a[0].isDouble()||!a[1].isDouble()||!finite(a[0].toDouble())||!finite(a[1].toDouble())){valid=false;break;}
            shape.vertices.append({a[0].toDouble(),a[1].toDouble()});
        }
        const qsizetype expected=(shape.kind==ShapeKind::Triangle||shape.kind==ShapeKind::Radical||shape.kind==ShapeKind::Curve||shape.kind==ShapeKind::RightAngle)?3:2;
        if(!valid||shape.vertices.size()<expected||(shape.kind!=ShapeKind::Polyline&&shape.vertices.size()!=expected))continue;
        if(shape.kind==ShapeKind::Radical)shape.vertices[2].setY(shape.vertices[1].y());
        shape.bounds=computeShapeBounds(shape);loadedShapes.append(shape);
    }
    QVector<ImageItem> loadedImages;
    for(const auto& value:root.value("images").toArray()) {
        const auto o=value.toObject();const auto r=o.value("rect").toArray();if(r.size()!=4)continue;
        bool valid=true;for(const auto& number:r)if(!number.isDouble()||!finite(number.toDouble()))valid=false;
        if(!valid||r[2].toDouble()<=0||r[3].toDouble()<=0)continue;
        ImageItem image;image.id=o.value("id").toString();if(image.id.isEmpty())image.id=freshId();
        image.rect=QRectF(r[0].toDouble(),r[1].toDouble(),r[2].toDouble(),r[3].toDouble());image.isPdfPage=o.value("isPdfPage").toBool();
        image.pdfSourcePath=o.value("pdfSourcePath").toString();image.pdfPageIndex=o.value("pdfPageIndex").toInt(-1);
        image.image=QImage::fromData(QByteArray::fromBase64(o.value("png").toString().toLatin1()),"PNG");
        if(image.isPdfPage&&image.image.isNull()) {
            if(!QFileInfo::exists(image.pdfSourcePath))continue;
            if(!pdfSources.contains(image.pdfSourcePath))pdfSources.insert(image.pdfSourcePath,importPdfPages(image.pdfSourcePath));
            for(const auto& page:pdfSources[image.pdfSourcePath])if(page.pdfPageIndex==image.pdfPageIndex){image.image=page.image;break;}
        }
        if(!image.image.isNull())loadedImages.append(image);
    }
    QVector<TextItem> loadedTexts;
    for(const auto& value:root.value("texts").toArray()) {
        const auto o=value.toObject();const auto pos=o.value("pos").toArray();
        if(pos.size()!=2||!pos[0].isDouble()||!pos[1].isDouble()||!finite(pos[0].toDouble())||!finite(pos[1].toDouble()))continue;
        TextItem item;item.id=o.value("id").toString();if(item.id.isEmpty())item.id=freshId();
        item.pos={pos[0].toDouble(),pos[1].toDouble()};item.text=o.value("text").toString();item.color=QColor(o.value("color").toString());item.fontSize=o.value("fontSize").toDouble(22);
        if(item.text.trimmed().isEmpty()||!item.color.isValid()||!finite(item.fontSize)||item.fontSize<1||item.fontSize>1000)continue;
        item.bounds=textBounds(item);loadedTexts.append(item);
    }
    const auto view=root.value("viewport").toObject();
    const double x=view.value("x").toDouble(),y=view.value("y").toDouble(),zoom=view.value("zoom").toDouble(1);
    Viewport viewport;if(finite(x)&&finite(y)&&finite(zoom))viewport={x,y,std::clamp(zoom,.2,5.0)};
    return {loaded,loadedShapes,loadedImages,loadedTexts,viewport,{},{}};
}
void syncWorkingPage(QVector<PageData>& pages,int& index,const DocumentSnapshot& document,Viewport viewport) {
    if(pages.isEmpty())pages.append(PageData{});
    index=std::clamp(index,0,int(pages.size())-1);
    auto& page=pages[index];
    page.strokes=document.strokes;page.shapes=document.shapes;page.images=document.images;page.texts=document.texts;page.viewport=viewport;
}
void createShapeVertices(ShapeItem& shape,QPointF origin,QPointF end) {
    const QRectF r=QRectF(origin,end).normalized();
    switch(shape.kind) {
    case ShapeKind::Line:case ShapeKind::Circle:case ShapeKind::Polyline:case ShapeKind::Brace:shape.vertices={origin,end};break;
    case ShapeKind::Ellipse:case ShapeKind::Rectangle:case ShapeKind::Axes:shape.vertices={r.topLeft(),r.bottomRight()};break;
    case ShapeKind::Triangle:shape.vertices={{r.center().x(),r.top()},r.bottomRight(),r.bottomLeft()};break;
    case ShapeKind::Radical:shape.vertices={{r.left()+r.width()*.22,r.bottom()},{r.left()+r.width()*.42,r.top()},r.topRight()};break;
    case ShapeKind::RightAngle:shape.vertices={{end.x(),origin.y()},origin,{origin.x(),end.y()}};break;
    case ShapeKind::Curve:shape.vertices={origin, (origin+end)*0.5 + QPointF(0, -std::max(24.0, distance(origin,end)*0.25)), end};break;
    }
    shape.closed=shape.kind==ShapeKind::Circle||shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Rectangle||shape.kind==ShapeKind::Triangle;
    shape.bounds=computeShapeBounds(shape);
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
    setAttribute(Qt::WA_TabletTracking);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);
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
    m_beautifyTimer=new QTimer(this);m_beautifyTimer->setSingleShot(true);m_beautifyTimer->setInterval(420);
    connect(m_beautifyTimer,&QTimer::timeout,this,&CanvasWidget::flushPendingBeautify);
    {
        QSettings settings("Finily","Finily");
        m_mathBeautifyEnabled=settings.value("ink/mathBeautify",false).toBool();
        m_autoColorOperators=settings.value("ink/autoColorOperators",true).toBool();
        const QColor savedOp(settings.value("ink/operatorColor","#ef4444").toString());
        if(savedOp.isValid())m_operatorColor=savedOp;
    }
    m_pages.append(PageData{});loadAutosave();updateCursorShape();
}
CanvasWidget::~CanvasWidget() {
    finishTextEditor(this,false);
    m_autosaveTimer->stop();writeAutosaveNow();
}
void CanvasWidget::setTool(ToolType tool) {
    clearPenCursor();
    finishTextEditor(this,true);
    if(m_polylineActive&&tool!=m_tool)finishActivePolyline();
    if((hasLassoSelection()||hasSelectedImage())&&tool!=ToolType::Lasso)clearSelection();
    if(tool==ToolType::Lasso&&m_tool!=ToolType::Lasso)clearSelection();
    if(tool==ToolType::Pen||tool==ToolType::Ballpoint||tool==ToolType::Highlighter)m_lastWriteTool=tool;
    m_tool=tool;m_showEraserCursor=false;
    if(tool!=ToolType::Text&&tool!=ToolType::Lasso&&tool!=ToolType::Shape&&tool!=ToolType::Polyline&&tool!=ToolType::Pen&&tool!=ToolType::Ballpoint)clearSelection();
    updateCursorShape();update();emit stateChanged();
}
void CanvasWidget::setShapeKind(ShapeKind kind) { m_shapeKind=kind;emit stateChanged(); }
void CanvasWidget::setHoldToShapeEnabled(bool enabled) { m_holdToShapeEnabled=enabled;if(!enabled)m_holdTimer->stop();emit stateChanged(); }
void CanvasWidget::setSmartGuidesEnabled(bool enabled) { m_smartGuidesEnabled=enabled;if(!enabled)m_activeGuides.clear();update();emit stateChanged(); }
void CanvasWidget::setMathBeautifyEnabled(bool enabled) {
    if(m_mathBeautifyEnabled==enabled)return;
    m_mathBeautifyEnabled=enabled;
    QSettings("Finily","Finily").setValue("ink/mathBeautify",enabled);
    emit toastRequested(enabled ? QStringLiteral("Modo Ecuación ∑ activado (M)")
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
    m_selectedImageId.clear();m_lassoPolygon.clear();m_lassoBounds={};m_lassoHandleIndex=-1;m_imageHandleIndex=-1;
    invalidateStaticCache();emit stateChanged();
}
int CanvasWidget::pdfPageCount() const { return int(std::count_if(m_images.cbegin(),m_images.cend(),[](const ImageItem& image){return image.isPdfPage;})); }
void CanvasWidget::deleteSelection() {
    if(m_mode!=InteractionMode::None||m_polylineActive)return;
    if(!hasLassoSelection()&&!hasSelectedImage()&&!hasSelectedText()){deleteSelectedShape();return;}
    pushUndoSnapshot(currentSnapshot());
    m_strokes.removeIf([&](const Stroke& stroke){return m_selectedStrokeIds.contains(stroke.id);});
    m_shapes.removeIf([&](const ShapeItem& shape){return m_selectedShapeIds.contains(shape.id);});
    m_images.removeIf([&](const ImageItem& image){return !image.isPdfPage&&image.id==m_selectedImageId;});
    m_texts.removeIf([&](const TextItem& item){return item.id==m_selectedTextId;});
    clearSelection();saveAutosave();emit stateChanged();
}
void CanvasWidget::duplicateSelection() {
    if(m_mode!=InteractionMode::None||m_polylineActive)return;
    QSet<QString> strokeIds=m_selectedStrokeIds,shapeIds=m_selectedShapeIds;
    if(!m_selectedShapeId.isEmpty())shapeIds.insert(m_selectedShapeId);
    if(strokeIds.isEmpty()&&shapeIds.isEmpty()&&m_selectedImageId.isEmpty()&&m_selectedTextId.isEmpty())return;
    const auto base=currentSnapshot();pushUndoSnapshot(base);const QPointF offset(24,24);
    QSet<QString> newStrokes,newShapes;QString newImage,newText;
    for(auto stroke:base.strokes)if(strokeIds.contains(stroke.id)) {
        stroke.id=freshId();for(auto& point:stroke.points){point.x+=offset.x();point.y+=offset.y();}
        stroke.bounds=computeBounds(stroke.points,stroke.size*.7);newStrokes.insert(stroke.id);m_strokes.append(stroke);
    }
    for(auto shape:base.shapes)if(shapeIds.contains(shape.id)){shape.id=freshId();translateShape(shape,offset);newShapes.insert(shape.id);m_shapes.append(shape);}
    for(auto image:base.images)if(!image.isPdfPage&&image.id==m_selectedImageId){image.id=freshId();image.rect.translate(offset);newImage=image.id;m_images.append(image);}
    for(auto item:base.texts)if(item.id==m_selectedTextId){item.id=freshId();item.pos+=offset;item.bounds=textBounds(item);newText=item.id;m_texts.append(item);}
    clearSelection();m_selectedTextId=newText;m_tool=ToolType::Lasso;m_selectedStrokeIds=newStrokes;m_selectedShapeIds=newShapes;m_selectedImageId=newImage;
    m_lassoBounds=computeSelectionBounds(m_strokes,m_shapes,newStrokes,newShapes);invalidateStaticCache();saveAutosave();emit stateChanged();
}
void CanvasWidget::straightenSelection() {
    if(m_mode!=InteractionMode::None||m_selectedStrokeIds.isEmpty())return;
    auto strokes=m_strokes;if(!straightenSelectedStrokes(strokes,m_selectedStrokeIds))return;
    pushUndoSnapshot(currentSnapshot());m_strokes=std::move(strokes);
    m_lassoBounds=computeSelectionBounds(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds);invalidateStaticCache();saveAutosave();emit stateChanged();emit toastRequested(QStringLiteral("Letras niveladas ✓"));
}
void CanvasWidget::convertSelectionToText() {
    if(m_mode!=InteractionMode::None||m_selectedStrokeIds.isEmpty())return;
    const QString recognized=HandwritingEngine::instance().recognizeStrokesToText(m_strokes,m_selectedStrokeIds);
    if(recognized.trimmed().isEmpty()){emit toastRequested(QStringLiteral("No se reconoció texto en la selección"));return;}
    const QRectF bounds=computeSelectionBounds(m_strokes,m_shapes,m_selectedStrokeIds,{});
    QColor inkColor=m_color;
    for(const auto& s:m_strokes)if(m_selectedStrokeIds.contains(s.id)){inkColor=s.color;break;}
    pushUndoSnapshot(currentSnapshot());
    m_strokes.removeIf([&](const Stroke& s){return m_selectedStrokeIds.contains(s.id);});
    TextItem item;
    item.id=freshId();
    item.text=recognized;
    item.color=inkColor;
    const int lineCount=std::max(1,int(recognized.count(QLatin1Char('\n'))+1));
    item.fontSize=std::clamp((bounds.height()/double(lineCount))*0.82,16.0,72.0);
    item.pos=bounds.topLeft();
    item.bounds=textBounds(item);
    m_texts.append(item);
    clearSelection();
    m_selectedTextId=item.id;
    invalidateStaticCache();saveAutosave();emit stateChanged();
    emit toastRequested(QStringLiteral("Texto: «%1» ✓").arg(recognized.left(24)));
}
bool CanvasWidget::saveSelectionAsTemplate(const QString& label) {
    if(m_mode!=InteractionMode::None||m_selectedStrokeIds.isEmpty()||label.trimmed().isEmpty())return false;
    QVector<QVector<QPointF>> raw;
    for(const auto& s:m_strokes) {
        if(!m_selectedStrokeIds.contains(s.id)||s.points.size()<2)continue;
        QVector<QPointF> pts;pts.reserve(s.points.size());
        for(const auto& p:s.points)pts.append(QPointF(p.x,p.y));
        raw.append(std::move(pts));
    }
    if(raw.isEmpty())return false;
    if(!HandwritingEngine::instance().saveCustomTemplate(label,raw))return false;
    emit toastRequested(QStringLiteral("Plantilla «%1» guardada ✓").arg(label.trimmed()));
    return true;
}
bool CanvasWidget::pasteFromClipboard() {
    if(m_mode!=InteractionMode::None)return false;
    const QImage image=QGuiApplication::clipboard()->image();if(image.isNull())return false;
    if(m_polylineActive)finishActivePolyline();
    pushUndoSnapshot(currentSnapshot());clearSelection();m_tool=ToolType::Lasso;
    const double width=std::min(520.0,image.width()/image.devicePixelRatio());
    const QSizeF size(width,width*double(image.height())/image.width());
    const QPointF center=screenToWorld(QPointF(this->width()/2.0,height()/2.0));
    ImageItem item;item.id=freshId();item.image=image;item.rect=QRectF(center-QPointF(size.width()/2,size.height()/2),size);
    m_images.append(item);m_selectedImageId=item.id;invalidateStaticCache();saveAutosave();updateCursorShape();emit stateChanged();emit toastRequested(QStringLiteral("Imagen insertada ✓"));return true;
}
bool CanvasWidget::importImageFile(const QString& path) {
    if(m_mode!=InteractionMode::None)return false;
    QImageReader reader(path);reader.setAutoTransform(true);const QSize source=reader.size();
    if(source.width()>4096||source.height()>4096)reader.setScaledSize(source.scaled(4096,4096,Qt::KeepAspectRatio));
    const QImage image=reader.read();if(image.isNull())return false;
    if(m_polylineActive)finishActivePolyline();
    pushUndoSnapshot(currentSnapshot());clearSelection();m_tool=ToolType::Lasso;
    const double width=std::min(520.0,double(image.width()));const QSizeF size(width,width*double(image.height())/image.width());
    const auto center=screenToWorld(QPointF(this->width()/2.0,height()/2.0));
    ImageItem item;item.id=freshId();item.image=image;item.rect=QRectF(center-QPointF(size.width()/2,size.height()/2),size);
    m_images.append(item);m_selectedImageId=item.id;invalidateStaticCache();saveAutosave();updateCursorShape();emit stateChanged();emit toastRequested(QStringLiteral("Imagen insertada ✓"));return true;
}
bool CanvasWidget::importPdfFile(const QString& path) {
    if(m_mode!=InteractionMode::None)return false;
    double y=40;for(const auto& image:m_images)if(image.isPdfPage)y=std::max(y,image.rect.bottom()+40);
    const auto pages=importPdfPages(path,y);if(pages.isEmpty())return false;
    if(m_polylineActive)finishActivePolyline();
    pushUndoSnapshot(currentSnapshot());clearSelection();m_images+=pages;
    m_zoomAnimation->stop();clearPenCursor();
    const auto rect=pages.front().rect;m_viewport.zoom=std::clamp((width()-60.0)/rect.width(),.2,5.0);
    m_viewport.x=width()/2.0-rect.center().x()*m_viewport.zoom;m_viewport.y=30-rect.top()*m_viewport.zoom;
    invalidateStaticCache();saveAutosave();emit viewportChanged(m_viewport.zoom);emit stateChanged();return true;
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
    if(m_mode!=InteractionMode::None||m_polylineActive||m_selectedShapeId.isEmpty())return;
    for(qsizetype i=0;i<m_shapes.size();++i)if(m_shapes[i].id==m_selectedShapeId){pushUndoSnapshot(currentSnapshot());m_shapes.removeAt(i);clearSelection();saveAutosave();return;}
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
    shape.bounds=computeShapeBounds(shape);pushUndoSnapshot(currentSnapshot());m_shapes.append(shape);
    m_polylineActive=false;m_lastPolylineTapMs=-1;m_polylineVertices.clear();m_polylineSnapClose=false;m_hasActiveShape=false;m_activeGuides.clear();
    m_selectedShapeId=shape.id;invalidateStaticCache();saveAutosave();emit stateChanged();
}
void CanvasWidget::setEraserMode(EraserMode mode) { m_eraserMode=mode;emit stateChanged(); }
void CanvasWidget::setEraserSize(double size) { if(!finite(size))return;m_eraserSize=std::clamp(size,1.0,500.0);update();emit stateChanged(); }
void CanvasWidget::setInkColor(const QColor& color) {
    if(!color.isValid())return;
    clearPenCursor();m_color=color;
    if(hasSelectedText()) {
        const auto id=m_selectedTextId;
        const bool changed=std::any_of(m_texts.cbegin(),m_texts.cend(),[&](const TextItem& item){return item.id==id&&item.color!=color;});
        if(changed){pushUndoSnapshot(currentSnapshot());for(auto& item:m_texts)if(item.id==id)item.color=color;invalidateStaticCache();saveAutosave();}
        emit stateChanged();return;
    }
    if(hasLassoSelection()) {
        const bool changed=std::any_of(m_strokes.cbegin(),m_strokes.cend(),[&](const Stroke& item){return m_selectedStrokeIds.contains(item.id)&&item.color!=color;})
            ||std::any_of(m_shapes.cbegin(),m_shapes.cend(),[&](const ShapeItem& item){return m_selectedShapeIds.contains(item.id)&&item.color!=color;});
        if(changed) {
            pushUndoSnapshot(currentSnapshot());
            for(auto& item:m_strokes)if(m_selectedStrokeIds.contains(item.id)){item.color=color;item.bounds=computeBounds(item.points,item.size*.7);}
            for(auto& item:m_shapes)if(m_selectedShapeIds.contains(item.id)){item.color=color;item.bounds=computeShapeBounds(item);}
            m_lassoBounds=computeSelectionBounds(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds);invalidateStaticCache();saveAutosave();
        }
        emit stateChanged();return;
    }
    if(auto* shape=findShapeById(m_selectedShapeId);shape&&shape->color!=color) {
        // Reacquire after the snapshot so QVector detaches before mutation.
        pushUndoSnapshot(currentSnapshot());shape=findShapeById(m_selectedShapeId);shape->color=color;shape->bounds=computeShapeBounds(*shape);
        invalidateStaticCache();saveAutosave();
    }
    emit stateChanged();
}
void CanvasWidget::setStrokeSize(double size) {
    if(!finite(size))return;
    clearPenCursor();m_size=std::clamp(size,0.1,200.0);
    if(hasLassoSelection()) {
        const bool changed=std::any_of(m_strokes.cbegin(),m_strokes.cend(),[&](const Stroke& item){return m_selectedStrokeIds.contains(item.id)&&item.size!=m_size;})
            ||std::any_of(m_shapes.cbegin(),m_shapes.cend(),[&](const ShapeItem& item){return m_selectedShapeIds.contains(item.id)&&item.size!=m_size;});
        if(changed) {
            pushUndoSnapshot(currentSnapshot());
            for(auto& item:m_strokes)if(m_selectedStrokeIds.contains(item.id)){item.size=m_size;item.bounds=computeBounds(item.points,item.size*.7);}
            for(auto& item:m_shapes)if(m_selectedShapeIds.contains(item.id)){item.size=m_size;item.bounds=computeShapeBounds(item);}
            m_lassoBounds=computeSelectionBounds(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds);invalidateStaticCache();saveAutosave();
        }
        emit stateChanged();return;
    }
    if(auto* shape=findShapeById(m_selectedShapeId);shape&&shape->size!=m_size) {
        pushUndoSnapshot(currentSnapshot());shape=findShapeById(m_selectedShapeId);shape->size=m_size;shape->bounds=computeShapeBounds(*shape);
        invalidateStaticCache();saveAutosave();
    }
    emit stateChanged();
}
void CanvasWidget::setLinePattern(LinePattern pattern) {
    m_linePattern=pattern;
    if(hasLassoSelection()) {
        const bool changed=std::any_of(m_strokes.cbegin(),m_strokes.cend(),[&](const Stroke& item){return m_selectedStrokeIds.contains(item.id)&&item.linePattern!=pattern;})
            ||std::any_of(m_shapes.cbegin(),m_shapes.cend(),[&](const ShapeItem& item){return m_selectedShapeIds.contains(item.id)&&item.linePattern!=pattern;});
        if(changed) {
            pushUndoSnapshot(currentSnapshot());
            for(auto& item:m_strokes)if(m_selectedStrokeIds.contains(item.id)){item.linePattern=pattern;item.bounds=computeBounds(item.points,item.size*.7);}
            for(auto& item:m_shapes)if(m_selectedShapeIds.contains(item.id)){item.linePattern=pattern;item.bounds=computeShapeBounds(item);}
            m_lassoBounds=computeSelectionBounds(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds);invalidateStaticCache();saveAutosave();
        }
        emit stateChanged();return;
    }
    if(auto* shape=findShapeById(m_selectedShapeId);shape&&shape->linePattern!=pattern) {
        pushUndoSnapshot(currentSnapshot());shape=findShapeById(m_selectedShapeId);shape->linePattern=pattern;shape->bounds=computeShapeBounds(*shape);
        invalidateStaticCache();saveAutosave();
    }
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
void CanvasWidget::setThemeId(ThemeId id) {
    clearPenCursor();const auto old=getThemeConfig(m_themeId);m_themeId=id;
    if(m_color==old.defaultInk)m_color=getThemeConfig(id).defaultInk;
    invalidateStaticCache();saveAutosave();emit stateChanged();
}
void CanvasWidget::setBackgroundTemplate(BackgroundTemplate tpl) { m_template=tpl;invalidateStaticCache();saveAutosave();emit stateChanged(); }
void CanvasWidget::setPageFormat(PageFormat format) {
    if(format!=PageFormat::Infinite&&format!=PageFormat::A4&&format!=PageFormat::Slide16x9)return;
    if(m_pageFormat==format)return;
    m_pageFormat=format;m_dirty=true;
    m_zoomAnimation->stop();clearPenCursor();
    if(format!=PageFormat::Infinite)m_viewport=fittedSheetViewport(format,size());
    invalidateStaticCache();saveAutosave();emit stateChanged();emit viewportChanged(m_viewport.zoom);
}
QPointF CanvasWidget::screenToWorld(const QPointF& p) const { return {(p.x()-m_viewport.x)/m_viewport.zoom,(p.y()-m_viewport.y)/m_viewport.zoom}; }
void CanvasWidget::zoomByFactor(double factor,const QPointF& anchor) {
    if(!finite(factor)||factor<=0)return;
    m_zoomAnimation->stop();clearPenCursor();
    const auto world=screenToWorld(anchor);m_viewport.zoom=std::clamp(m_viewport.zoom*factor,0.2,5.0);
    m_viewport.x=anchor.x()-world.x()*m_viewport.zoom;m_viewport.y=anchor.y()-world.y()*m_viewport.zoom;
    invalidateStaticCache();emit viewportChanged(m_viewport.zoom);
}
void CanvasWidget::animateViewport(const Viewport& target) {
    m_zoomAnimation->stop();clearPenCursor();m_zoomStart=m_viewport;m_zoomTarget=target;
    if(m_zoomStart.x==target.x&&m_zoomStart.y==target.y&&m_zoomStart.zoom==target.zoom)return;
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
    animateViewport(fittedSheetViewport(m_pageFormat,size()));
    saveAutosave();
}
void CanvasWidget::goToPage(int index) {
    finishTextEditor(this,true);if(m_polylineActive)finishActivePolyline();
    if(m_mode==InteractionMode::LaserTrail)m_mode=InteractionMode::None;
    if(m_mode!=InteractionMode::None)return;
    m_zoomAnimation->stop();stopShapePulse();clearPenCursor();
    syncWorkingPage(m_pages,m_currentPageIndex,currentSnapshot(),m_viewport);
    index=std::clamp(index,0,int(m_pages.size())-1);if(index==m_currentPageIndex)return;
    m_holdTimer->stop();m_laserTimer->stop();m_laserTrail.clear();m_tabletActive=false;
    m_pages[m_currentPageIndex].undoStack=std::move(m_undoStack);m_pages[m_currentPageIndex].redoStack=std::move(m_redoStack);
    m_currentPageIndex=index;const auto page=m_pages[index];
    m_strokes=page.strokes;m_shapes=page.shapes;m_images=page.images;m_texts=page.texts;m_viewport=page.viewport;
    m_hasActiveStroke=false;m_hasActiveShape=false;m_holdSnappedToShape=false;m_activeGuides.clear();m_showEraserCursor=false;
    m_undoStack=page.undoStack;m_redoStack=page.redoStack;clearSelection();updateCursorShape();invalidateStaticCache();saveAutosave();
    emit stateChanged();emit viewportChanged(m_viewport.zoom);
}
void CanvasWidget::addPage() {
    finishTextEditor(this,true);if(m_polylineActive)finishActivePolyline();
    if(m_mode==InteractionMode::LaserTrail)m_mode=InteractionMode::None;
    if(m_mode!=InteractionMode::None)return;
    syncWorkingPage(m_pages,m_currentPageIndex,currentSnapshot(),m_viewport);
    const int next=m_currentPageIndex+1;PageData page;page.viewport=fittedSheetViewport(m_pageFormat,size());m_pages.insert(next,page);m_dirty=true;goToPage(next);emit stateChanged();
    emit toastRequested(QStringLiteral("Página %1 / %2").arg(m_currentPageIndex+1).arg(pageCount()));
}
void CanvasWidget::nextPage() { goToPage(m_currentPageIndex+1); }
void CanvasWidget::prevPage() { goToPage(m_currentPageIndex-1); }
void CanvasWidget::deleteCurrentPage() {
    finishTextEditor(this,true);if(m_polylineActive)finishActivePolyline();
    if(m_mode==InteractionMode::LaserTrail)m_mode=InteractionMode::None;
    if(m_mode!=InteractionMode::None)return;
    if(pageCount()==1){clearBoard();return;}
    const int old=m_currentPageIndex;
    // Load the surviving neighbor before removal, so goToPage cannot sync the
    // deleted working vectors over that neighbor after indices shift.
    goToPage(old+1<int(m_pages.size())?old+1:old-1);m_pages.removeAt(old);m_dirty=true;
    if(m_currentPageIndex>old)--m_currentPageIndex;
    saveAutosave();emit stateChanged();
}
bool CanvasWidget::currentPageHasContent() const { return !m_strokes.isEmpty()||!m_shapes.isEmpty()||!m_images.isEmpty()||!m_texts.isEmpty(); }
bool CanvasWidget::hasUnsavedChanges() const {
    if(!m_dirty)return false;
    if(currentPageHasContent()||m_pages.size()>1||m_pageFormat!=PageFormat::Infinite)return true;
    return !m_currentFilePath.isEmpty(); // un cuaderno guardado que se vació también es un cambio
}
DocumentSnapshot CanvasWidget::currentSnapshot() const { return {m_strokes,m_shapes,m_images,m_texts}; }
void CanvasWidget::pushUndoSnapshot(const DocumentSnapshot& snapshot) {
    m_undoStack.append(snapshot);while(m_undoStack.size()>100)m_undoStack.removeFirst();m_redoStack.clear();m_dirty=true;
}
void CanvasWidget::undo() {
    if(m_mode!=InteractionMode::None)return;
    if(m_polylineActive){cancelActivePolyline();return;}
    if(m_undoStack.isEmpty())return;
    m_dirty=true;m_redoStack.append(currentSnapshot());const auto snapshot=m_undoStack.takeLast();m_strokes=snapshot.strokes;m_shapes=snapshot.shapes;m_images=snapshot.images;m_texts=snapshot.texts;
    clearSelection();saveAutosave();emit stateChanged();
}
void CanvasWidget::redo() {
    if(m_mode!=InteractionMode::None||m_polylineActive||m_redoStack.isEmpty())return;
    m_dirty=true;m_undoStack.append(currentSnapshot());while(m_undoStack.size()>100)m_undoStack.removeFirst();
    const auto snapshot=m_redoStack.takeLast();m_strokes=snapshot.strokes;m_shapes=snapshot.shapes;m_images=snapshot.images;m_texts=snapshot.texts;
    clearSelection();saveAutosave();emit stateChanged();
}
void CanvasWidget::clearBoard() {
    if(m_mode!=InteractionMode::None)return;
    if(m_polylineActive)cancelActivePolyline();
    if(m_strokes.isEmpty()&&m_shapes.isEmpty()&&m_images.isEmpty()&&m_texts.isEmpty())return;
    pushUndoSnapshot(currentSnapshot());m_strokes.clear();m_shapes.clear();m_images.clear();m_texts.clear();clearSelection();saveAutosave();emit stateChanged();
}
void CanvasWidget::newDocument() {
    m_zoomAnimation->stop();stopShapePulse();clearPenCursor();
    finishTextEditor(this,false);m_holdTimer->stop();m_laserTimer->stop();m_laserTrail.clear();
    m_mode=InteractionMode::None;m_tabletActive=false;m_hasActiveStroke=false;m_hasActiveShape=false;m_holdSnappedToShape=false;
    cancelActivePolyline();m_strokes.clear();m_shapes.clear();m_images.clear();m_texts.clear();m_currentFilePath.clear();
    m_pages={PageData{}};m_currentPageIndex=0;m_dirty=false;
    m_undoStack.clear();m_redoStack.clear();m_showEraserCursor=false;clearSelection();m_viewport=fittedSheetViewport(m_pageFormat,size());emit viewportChanged(m_viewport.zoom);updateCursorShape();saveAutosave();emit stateChanged();
}
bool CanvasWidget::saveToFile(const QString& path) {
    finishTextEditor(this,true);if(m_polylineActive)finishActivePolyline();
    if(path.isEmpty()||m_mode!=InteractionMode::None)return false;
    syncWorkingPage(m_pages,m_currentPageIndex,currentSnapshot(),m_viewport);
    if(!serializeDocumentToFile(path,m_pages,m_currentPageIndex,documentSettings(m_shapeKind,m_holdToShapeEnabled,m_smartGuidesEnabled,m_swapPenButtons,m_pressureProfile,m_themeId,m_template,m_viewport,m_pageFormat)))return false;
    m_currentFilePath=path;m_dirty=false;saveAutosave();emit stateChanged();emit toastRequested(QStringLiteral("Guardado ✓"));return true;
}
void CanvasWidget::saveAutosave() const { m_autosaveTimer->start(); }
void CanvasWidget::flushAutosave() { if(m_autosaveTimer->isActive()){m_autosaveTimer->stop();writeAutosaveNow();} }
void CanvasWidget::writeAutosaveNow() const {
    DocumentSnapshot document=currentSnapshot();
    if(m_mode==InteractionMode::Erase)document=m_snapshotBeforeErase;
    if(m_mode==InteractionMode::ShapeDragHandle||m_mode==InteractionMode::ShapeMove)document=m_snapshotBeforeShapeEdit;
    if(m_mode==InteractionMode::LassoMove||m_mode==InteractionMode::LassoScale||m_mode==InteractionMode::LassoRotate)document=m_snapshotBeforeLassoTransform;
    if(m_mode==InteractionMode::ImageMove||m_mode==InteractionMode::ImageResize)document=m_snapshotBeforeImageEdit;
    if(m_mode==InteractionMode::TextMove)document=m_snapshotBeforeTextEdit;
    const auto path=autosavePath();if(!QDir().mkpath(QFileInfo(path).absolutePath()))return;
    auto* self=const_cast<CanvasWidget*>(this);
    syncWorkingPage(self->m_pages,self->m_currentPageIndex,document,m_viewport);
    auto settings=documentSettings(m_shapeKind,m_holdToShapeEnabled,m_smartGuidesEnabled,m_swapPenButtons,m_pressureProfile,m_themeId,m_template,m_viewport,m_pageFormat);
    // Datos de sesión: al reabrir la app se recupera el cuaderno asociado y si
    // tenía cambios sin guardar (los .finily del usuario nunca los llevan).
    settings.insert("sessionFilePath",m_currentFilePath);settings.insert("sessionDirty",m_dirty);
    serializeDocumentToFile(path,m_pages,m_currentPageIndex,settings);
}
void CanvasWidget::loadAutosave() {
    const auto previousPath=m_currentFilePath;const bool previousDirty=m_dirty;
    if(!loadFromFile(autosavePath())){m_currentFilePath=previousPath;m_dirty=previousDirty;return;}
    // loadFromFile ya restauró la sesión; un autoguardado antiguo (sin datos de
    // sesión) deja la ruta apuntando al propio autosave.json.
    if(m_currentFilePath==autosavePath()){m_currentFilePath.clear();m_dirty=true;}
    if(!m_currentFilePath.isEmpty()&&!QFileInfo::exists(m_currentFilePath)){m_currentFilePath.clear();m_dirty=true;}
    m_autosaveTimer->stop();emit stateChanged();
}
bool CanvasWidget::loadFromFile(const QString& path) {
    if(m_mode!=InteractionMode::None)return false;
    const auto root=loadDocumentFromFile(path);if(root.isEmpty())return false;
    m_zoomAnimation->stop();stopShapePulse();clearPenCursor();
    finishTextEditor(this,false);
    QVector<PageData> pages;QHash<QString,QVector<ImageItem>> pdfSources;
    if(root.value("version").toInt()==5)for(const auto& value:root.value("pages").toArray())pages.append(deserializePage(value.toObject(),pdfSources));
    else pages.append(deserializePage(root,pdfSources));
    m_pages=std::move(pages);m_currentPageIndex=std::clamp(root.value("currentPageIndex").toInt(),0,int(m_pages.size())-1);
    const auto page=m_pages[m_currentPageIndex];
    m_strokes=page.strokes;m_shapes=page.shapes;m_images=page.images;m_texts=page.texts;m_viewport=page.viewport;
    m_selectedTextId.clear();m_laserTrail.clear();m_laserTimer->stop();m_hasActiveStroke=false;
    m_undoStack.clear();m_redoStack.clear();
    m_selectedStrokeIds.clear();m_selectedShapeIds.clear();m_selectedImageId.clear();m_lassoBounds={};m_lassoPolygon.clear();
    m_polylineActive=false;m_lastPolylineTapMs=-1;m_polylineVertices.clear();m_hasActiveShape=false;m_selectedShapeId.clear();m_activeGuides.clear();
    const int shapeKind=root.value("shapeKind").toInt(int(ShapeKind::Triangle));
    if(shapeKind>=0&&shapeKind<=int(ShapeKind::Axes))m_shapeKind=ShapeKind(shapeKind);
    m_holdToShapeEnabled=root.value("holdToShapeEnabled").toBool(true);m_smartGuidesEnabled=root.value("smartGuidesEnabled").toBool(true);
    m_swapPenButtons=root.value("swapPenButtons").toBool(false);
    const int profile=root.value("pressureProfile").toInt(int(PressureProfile::Light));
    if(profile>=int(PressureProfile::Light)&&profile<=int(PressureProfile::Firm))m_pressureProfile=PressureProfile(profile);
    const int format=root.value("pageFormat").toInt(int(PageFormat::Infinite));
    m_pageFormat=format>=int(PageFormat::Infinite)&&format<=int(PageFormat::Slide16x9)?PageFormat(format):PageFormat::Infinite;
    const int theme=root.value("theme").toInt(0),tpl=root.value("template").toInt(2);
    if(theme>=0&&theme<=int(ThemeId::PaperCream))m_themeId=ThemeId(theme);
    if(tpl>=0&&tpl<=int(BackgroundTemplate::Ruled))m_template=BackgroundTemplate(tpl);
    m_color=getThemeConfig(m_themeId).defaultInk;
    m_currentFilePath=path;m_dirty=false;
    if(root.contains("sessionFilePath")){m_currentFilePath=root.value("sessionFilePath").toString();m_dirty=root.value("sessionDirty").toBool(false);}
    invalidateStaticCache();saveAutosave();emit stateChanged();emit viewportChanged(m_viewport.zoom);return true;
}
void CanvasWidget::invalidateStaticCache() { m_staticCacheDirty=true;update(); }
void CanvasWidget::rebuildStaticCache() {
    const double dpr=devicePixelRatioF();const QSize pixels(qCeil(width()*dpr),qCeil(height()*dpr));
    if(pixels.isEmpty())return;
    if(m_staticCache.size()!=pixels||m_staticCache.devicePixelRatio()!=dpr) {
        m_staticCache=QImage(pixels,QImage::Format_ARGB32_Premultiplied);m_staticCache.setDevicePixelRatio(dpr);
    }
    const auto theme=getThemeConfig(m_themeId);
    const auto sheet=sheetRectForPageFormat(m_pageFormat);
    m_staticCache.fill(sheet.isEmpty()?theme.background:(theme.isDark?QColor("#0c0e13"):QColor("#e6e7ec")));
    QPainter p(&m_staticCache);p.translate(m_viewport.x,m_viewport.y);p.scale(m_viewport.zoom,m_viewport.zoom);
    const QRectF visible(screenToWorld(QPointF(0,0)),screenToWorld(QPointF(width(),height())));
    if(sheet.isEmpty())drawBackgroundTemplate(p,m_template,theme,m_viewport,visible);
    else {
        p.save();p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);
        const QRectF shadow=sheet.translated(0,6/m_viewport.zoom);
        for(int layer=12;layer>=1;--layer) {
            const double spread=layer/m_viewport.zoom;
            p.setBrush(QColor(0,0,0,theme.isDark?5:3));
            p.drawRoundedRect(shadow.adjusted(-spread,-spread,spread,spread),spread,spread);
        }
        p.fillRect(sheet,theme.background);
        p.save();p.setClipRect(sheet,Qt::IntersectClip);
        drawBackgroundTemplate(p,m_template,theme,m_viewport,sheet.intersected(visible));p.restore();
        p.setBrush(Qt::NoBrush);QPen border(theme.isDark?QColor(255,255,255,24):QColor(15,23,42,26));
        border.setWidthF(1/m_viewport.zoom);p.setPen(border);p.drawRect(sheet);p.restore();
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for(int pass=0;pass<2;++pass)for(const auto& image:m_images) {
        if(image.isPdfPage!=(pass==0)||image.id==m_selectedImageId||!image.rect.intersects(visible))continue;
        if(image.isPdfPage){p.fillRect(image.rect.translated(4/m_viewport.zoom,5/m_viewport.zoom),QColor(0,0,0,38));p.fillRect(image.rect,Qt::white);}
        p.drawImage(image.rect,image.image);
    }
    auto drawPass=[&](QPainter& target,bool highlights) {
        for(const auto& stroke:m_strokes) {
            if((stroke.tool==ToolType::Highlighter)!=highlights||m_selectedStrokeIds.contains(stroke.id))continue;
            const double margin=std::max(16.0,stroke.size*5.0);
            if(stroke.bounds.adjusted(-margin,-margin,margin,margin).intersects(visible))drawStroke(target,stroke);
        }
    };
    auto drawShapes=[&](QPainter& target) {
        for(const auto& shape:m_shapes)if(shape.id!=m_selectedShapeId&&!m_selectedShapeIds.contains(shape.id)&&shape.bounds.intersects(visible))drawShape(target,shape);
    };
    const bool selectedHighlight=std::any_of(m_strokes.cbegin(),m_strokes.cend(),[&](const Stroke& stroke){return m_selectedStrokeIds.contains(stroke.id)&&stroke.tool==ToolType::Highlighter;});
    const bool imageUnderlay=!m_selectedImageId.isEmpty();
    if(!imageUnderlay)drawPass(p,true);
    if((m_hasActiveStroke&&m_activeStroke.tool==ToolType::Highlighter)||selectedHighlight||imageUnderlay) {
        auto& layers=highlighterLayers(this);
        layers.background=m_staticCache.copy();
        layers.ink=QImage(pixels,QImage::Format_ARGB32_Premultiplied);
        layers.ink.setDevicePixelRatio(dpr);layers.ink.fill(Qt::transparent);
        {
            QPainter inkPainter(&layers.ink);
            inkPainter.translate(m_viewport.x,m_viewport.y);inkPainter.scale(m_viewport.zoom,m_viewport.zoom);
            if(imageUnderlay)drawPass(inkPainter,true);
            drawShapes(inkPainter);drawPass(inkPainter,false);
            for(const auto& item:m_texts)if(item.id!=m_selectedTextId)drawTextItem(inkPainter,item);
        }
        p.save();p.resetTransform();p.drawImage(QPointF(0,0),layers.ink);p.restore();
    } else {
        drawShapes(p);drawPass(p,false);
        for(const auto& item:m_texts)if(item.id!=m_selectedTextId)drawTextItem(p,item);
        // Release temporary full-size buffers as soon as the highlight commits.
        auto* child=findChild<QObject*>(QStringLiteral("_finily_highlighter_layers"),Qt::FindDirectChildrenOnly);
        if(child) { auto* layers=static_cast<HighlighterLayers*>(child);layers->background=QImage();layers->ink=QImage(); }
    }
    m_staticCacheDirty=false;
}
void CanvasWidget::paintEvent(QPaintEvent*) {
    if(m_staticCacheDirty||m_staticCache.size()!=QSize(qCeil(width()*devicePixelRatioF()),qCeil(height()*devicePixelRatioF()))||m_staticCache.devicePixelRatio()!=devicePixelRatioF())rebuildStaticCache();
    const bool liveHighlight=(m_hasActiveStroke&&m_activeStroke.tool==ToolType::Highlighter)||!m_selectedImageId.isEmpty()
        ||std::any_of(m_strokes.cbegin(),m_strokes.cend(),[&](const Stroke& stroke){return m_selectedStrokeIds.contains(stroke.id)&&stroke.tool==ToolType::Highlighter;});
    QPainter p(this);
    p.drawImage(QPointF(0,0),liveHighlight?highlighterLayers(this).background:m_staticCache);
    p.translate(m_viewport.x,m_viewport.y);p.scale(m_viewport.zoom,m_viewport.zoom);
    for(const auto& image:m_images)if(image.id==m_selectedImageId){p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(image.rect,image.image);}
    for(const auto& stroke:m_strokes)if(m_selectedStrokeIds.contains(stroke.id)&&stroke.tool==ToolType::Highlighter)drawStroke(p,stroke);
    if(m_hasActiveStroke)drawStroke(p,m_activeStroke);
    if(liveHighlight) {
        p.save();p.resetTransform();p.drawImage(QPointF(0,0),highlighterLayers(this).ink);p.restore();
    }
    for(const auto& shape:m_shapes)if(m_selectedShapeIds.contains(shape.id))drawShape(p,shape);
    for(const auto& stroke:m_strokes)if(m_selectedStrokeIds.contains(stroke.id)&&stroke.tool!=ToolType::Highlighter)drawStroke(p,stroke);
    if(m_mode==InteractionMode::LassoDraw)drawLassoPath(p,m_lassoPolygon,getThemeConfig(m_themeId),m_viewport.zoom);
    if(hasLassoSelection())drawLassoSelectionBox(p,m_lassoBounds,getThemeConfig(m_themeId),m_viewport.zoom);
    for(const auto& image:m_images)if(image.id==m_selectedImageId) {
        const auto theme=getThemeConfig(m_themeId);p.setPen(QPen(theme.accent,1.4/m_viewport.zoom,Qt::DashLine));p.setBrush(Qt::NoBrush);p.drawRect(image.rect);
        p.setPen(QPen(theme.accent,1.5/m_viewport.zoom));p.setBrush(theme.isDark?theme.background:QColor(Qt::white));
        for(auto point:{image.rect.topLeft(),image.rect.topRight(),image.rect.bottomRight(),image.rect.bottomLeft()})p.drawEllipse(point,6/m_viewport.zoom,6/m_viewport.zoom);
    }
    if(m_shapePulseAlpha>0.0) {
        ShapeItem halo=m_shapePulseShape;halo.linePattern=LinePattern::Solid;
        const double expansion=(1.0-m_shapePulseAlpha)*12.0/m_viewport.zoom;
        for(int layer=3;layer>=1;--layer) {
            halo.color=getThemeConfig(m_themeId).accent;
            halo.opacity=m_shapePulseAlpha*.16;
            halo.size=m_shapePulseShape.size+expansion+layer*4.0/m_viewport.zoom;
            drawShape(p,halo);
        }
    }
    if(m_hasActiveShape)drawShape(p,m_activeShape);
    if(const auto* selected=findShapeById(m_selectedShapeId)) {
        if(!m_hasActiveShape||selected->id!=m_activeShape.id)drawShape(p,*selected);
        drawSelectionOverlay(p,*selected,getThemeConfig(m_themeId),m_viewport.zoom);
    }
    if(m_polylineActive&&m_polylineSnapClose&&!m_polylineVertices.isEmpty()) {
        p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(getThemeConfig(m_themeId).accent,2/m_viewport.zoom));p.setBrush(Qt::NoBrush);
        p.drawEllipse(m_polylineVertices.front(),10/m_viewport.zoom,10/m_viewport.zoom);
    }
    for(const auto& item:m_texts)if(item.id==m_selectedTextId) {
        drawTextItem(p,item);p.setPen(QPen(getThemeConfig(m_themeId).accent,1/m_viewport.zoom,Qt::DashLine));p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(item.bounds.adjusted(-4/m_viewport.zoom,-3/m_viewport.zoom,4/m_viewport.zoom,3/m_viewport.zoom),2,2);
    }
    p.setRenderHint(QPainter::Antialiasing);
    const auto laserNow=nowMs();
    for(qsizetype i=0;i<m_laserTrail.size();++i) {
        const auto& sample=m_laserTrail[i];const double fade=std::clamp(1.0-double(laserNow-sample.timestampMs)/1400.0,0.0,1.0);
        if(fade<=0)continue;
        const auto from=i>0&&!sample.breakBefore?m_laserTrail[i-1].pos:sample.pos;
        for(int layer=3;layer>=0;--layer) {
            QColor color=layer==0?QColor("#fff5ee"):QColor("#ff493f");color.setAlphaF(fade*(layer==0?.95:layer==1?.6:.12));
            p.setPen(QPen(color,(layer==0?1.8:layer==1?4.0:layer==2?9.0:17.0)/m_viewport.zoom,Qt::SolidLine,Qt::RoundCap));p.drawLine(from,sample.pos);
        }
    }
    if(!m_laserTrail.isEmpty()) {
        const auto& tip=m_laserTrail.back();const double fade=std::clamp(1.0-double(laserNow-tip.timestampMs)/1400.0,0.0,1.0);
        p.setPen(Qt::NoPen);
        for(int layer=3;layer>=0;--layer){QColor color=layer==0?QColor("#ffffff"):QColor("#ff5547");color.setAlphaF(fade*(layer==0?1.0:.17));p.setBrush(color);const double r=(2.5+layer*3.0)/m_viewport.zoom;p.drawEllipse(tip.pos,r,r);}
    }
    drawSmartGuides(p,m_activeGuides,getThemeConfig(m_themeId),m_viewport.zoom);
    if(m_showPenCursor) {
        p.save();p.resetTransform();
        const QPointF center=m_hoverWorldPos*m_viewport.zoom+QPointF(m_viewport.x,m_viewport.y);
        const double radius=strokeSize()*m_viewport.zoom/2.0;
        const QColor halo=inkColor().lightnessF()>.5?QColor(0,0,0,150):QColor(255,255,255,175);
        p.setPen(QPen(halo,1.0));p.setBrush(inkColor());p.drawEllipse(center,radius,radius);p.restore();
    }
    if(m_showEraserCursor) {
        p.setRenderHint(QPainter::Antialiasing);QPen pen(getThemeConfig(m_themeId).accent);pen.setCosmetic(true);pen.setWidthF(1.3);
        p.setPen(pen);p.setBrush(Qt::NoBrush);p.drawEllipse(m_eraserWorldPos,m_eraserSize/2,m_eraserSize/2);
    }
}
void CanvasWidget::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event);invalidateStaticCache(); }
void CanvasWidget::beginInput(const QPointF& screen,double pressure,double tiltX,double tiltY,bool hardware,bool eraser,Qt::MouseButton button,Qt::KeyboardModifiers mods) {
    if(m_mode!=InteractionMode::None)return;
    m_zoomAnimation->stop();clearPenCursor();
    setFocus(Qt::MouseFocusReason);
    if(m_polylineActive&&button==Qt::RightButton){finishActivePolyline();return;}
    if(button==Qt::MiddleButton||button==Qt::RightButton||m_spaceHeld||m_tool==ToolType::Hand) {
        m_mode=InteractionMode::Pan;m_panStartScreen=screen;m_panStartViewport={m_viewport.x,m_viewport.y};updateCursorShape();return;
    }
    if(button!=Qt::LeftButton)return;
    const auto world=screenToWorld(screen);m_activeGuides.clear();
    if(eraser||m_tool==ToolType::Eraser) {
        if(m_polylineActive)cancelActivePolyline();
        m_mode=InteractionMode::Erase;m_snapshotBeforeErase=currentSnapshot();m_erasedAnything=false;
        m_eraserWorldPos=world;m_showEraserCursor=true;applyEraseAt(world);updateCursorShape();return;
    }
    if(m_tool==ToolType::Laser) {
        m_mode=InteractionMode::LaserTrail;m_laserTrail.append({world,nowMs(),true});m_laserTimer->start();update();return;
    }
    if(m_tool==ToolType::Text||m_tool==ToolType::Lasso) {
        QString hit;
        for(qsizetype i=m_texts.size();i-->0;)if(m_texts[i].bounds.adjusted(-4,-4,4,4).contains(world)){hit=m_texts[i].id;break;}
        const bool edit=property("_finily_edit_text").toBool();setProperty("_finily_edit_text",false);
        if(!hit.isEmpty()&&!edit) {
            clearSelection();m_selectedTextId=hit;m_snapshotBeforeTextEdit=currentSnapshot();
            for(const auto& item:std::as_const(m_texts))if(item.id==hit)m_textOriginalPos=item.pos;
            m_textDragStartWorld=world;m_mode=InteractionMode::TextMove;invalidateStaticCache();emit stateChanged();return;
        }
        if(m_tool==ToolType::Text||(!hit.isEmpty()&&edit)) {
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
    if(m_tool==ToolType::Lasso) {
        for(const auto& image:std::as_const(m_images))if(image.id==m_selectedImageId&&!image.isPdfPage) {
            const int handle=hitTestLassoHandle(image.rect,world,m_viewport.zoom);
            if((handle>=0&&handle<4)||image.rect.contains(world)) {
                m_mode=handle>=0&&handle<4?InteractionMode::ImageResize:InteractionMode::ImageMove;
                m_imageHandleIndex=handle;m_imageOriginalRect=image.rect;m_imageDragStartWorld=world;m_snapshotBeforeImageEdit=currentSnapshot();return;
            }
        }
        if(hasLassoSelection()) {
            m_lassoHandleIndex=hitTestLassoHandle(m_lassoBounds,world,m_viewport.zoom);
            if(m_lassoHandleIndex>=0||m_lassoBounds.contains(world)) {
                m_snapshotBeforeLassoTransform=currentSnapshot();m_lassoDragStartWorld=world;
                m_mode=m_lassoHandleIndex==4?InteractionMode::LassoRotate:m_lassoHandleIndex>=0?InteractionMode::LassoScale:InteractionMode::LassoMove;
                const QVector<QPointF> corners{m_lassoBounds.topLeft(),m_lassoBounds.topRight(),m_lassoBounds.bottomRight(),m_lassoBounds.bottomLeft()};
                m_lassoTransformCenter=m_lassoHandleIndex>=0&&m_lassoHandleIndex<4?corners[(m_lassoHandleIndex+2)%4]:m_lassoBounds.center();
                m_lassoStartAngle=std::atan2(world.y()-m_lassoTransformCenter.y(),world.x()-m_lassoTransformCenter.x());return;
            }
        }
        clearSelection();m_mode=InteractionMode::LassoDraw;m_lassoPolygon.append(world);update();return;
    }
    auto startEdit=[&](ShapeItem& shape,int handle) {
        m_snapshotBeforeShapeEdit=currentSnapshot();m_selectedShapeId=shape.id;m_activeHandleIndex=handle;
        m_shapeDragStartWorld=world;m_shapeDragOriginalVertices=shape.vertices;
        m_mode=handle>=0?InteractionMode::ShapeDragHandle:InteractionMode::ShapeMove;
        invalidateStaticCache();emit stateChanged();
    };
    if(!m_polylineActive&&m_tool!=ToolType::Highlighter) {
        if(auto* selected=findShapeById(m_selectedShapeId)) {
            const bool inkTool=m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint;
            const int handle=hitTestHandle(*selected,world,(inkTool?12.0:10.0)/m_viewport.zoom);
            if(handle>=0){startEdit(*selected,handle);return;}
        }
        if(m_tool==ToolType::Shape||m_tool==ToolType::Polyline) {
            for(qsizetype i=m_shapes.size();i-->0;) {
                const int handle=hitTestHandle(m_shapes[i],world,9/m_viewport.zoom);
                if(handle>=0){startEdit(m_shapes[i],handle);return;}
                if(hitTestShape(m_shapes[i],world,6/m_viewport.zoom)){startEdit(m_shapes[i],-1);return;}
            }
        }
    }
    if(m_tool==ToolType::Polyline) {
        const qint64 tapMs=nowMs();
        if(m_polylineActive&&m_polylineVertices.size()>=2&&m_lastPolylineTapMs>=0
           &&tapMs-m_lastPolylineTapMs<=340&&distance(screen,m_lastPolylineTapScreen)<=12
           &&distance(world,m_polylineVertices.back())*m_viewport.zoom<=12) {
            // Native accepted tablet events never produce mouseDoubleClickEvent.
            finishActivePolyline(false);return;
        }
        if(m_polylineActive&&m_polylineVertices.size()>=3&&distance(world,m_polylineVertices.front())*m_viewport.zoom<=14){finishActivePolyline(true);return;}
        const auto snap=m_smartGuidesEnabled?snapPointWithGuides(world,m_polylineVertices.isEmpty()?std::nullopt:std::optional<QPointF>(m_polylineVertices.back()),m_shapes,{},m_viewport.zoom):SnapResult{world,{}};
        if(!m_polylineActive) {
            clearSelection();m_polylineActive=true;m_polylineVertices.clear();m_activeShape={};m_activeShape.id=freshId();
            m_activeShape.kind=ShapeKind::Polyline;m_activeShape.color=m_color;m_activeShape.size=m_size;m_activeShape.linePattern=m_linePattern;
        }
        if(m_polylineVertices.isEmpty()||distance(snap.point,m_polylineVertices.back())>.1/m_viewport.zoom)m_polylineVertices.append(snap.point);
        m_lastPolylineTapMs=tapMs;m_lastPolylineTapScreen=screen;
        m_polylineHoverWorld=snap.point;m_activeGuides=snap.guides;m_polylineSnapClose=false;
        m_activeShape.vertices=m_polylineVertices;m_activeShape.closed=false;m_activeShape.bounds=computeShapeBounds(m_activeShape);m_hasActiveShape=true;update();emit stateChanged();return;
    }
    if(m_tool==ToolType::Shape) {
        clearSelection();m_snapshotBeforeShapeEdit=currentSnapshot();m_mode=InteractionMode::ShapeCreate;m_holdSnappedToShape=false;
        const auto snap=m_smartGuidesEnabled?snapPointWithGuides(world,std::nullopt,m_shapes,{},m_viewport.zoom):SnapResult{world,{}};
        m_shapeCreateOrigin=snap.point;m_activeGuides=snap.guides;m_activeShape={};m_activeShape.id=freshId();
        m_activeShape.kind=m_shapeKind;m_activeShape.color=m_color;m_activeShape.size=m_size;m_activeShape.linePattern=m_linePattern;
        createShapeVertices(m_activeShape,snap.point,snap.point);m_hasActiveShape=true;m_selectedShapeId=m_activeShape.id;update();return;
    }
    if(m_beautifyTimer)m_beautifyTimer->stop();
    clearSelection();m_mode=InteractionMode::Draw;m_holdSnappedToShape=false;m_stabilizerFilter->reset();m_activeStroke={};
    m_activeStroke.id=freshId();m_activeStroke.tool=m_tool;m_activeStroke.color=m_color;m_activeStroke.size=m_size;
    m_activeStroke.linePattern=m_linePattern;m_activeStroke.pressureProfile=m_pressureProfile;m_activeStroke.hasHardwarePressure=hardware;
    m_rawStartPoint=inputPoint(world,pressure,tiltX,tiltY);m_activeStroke.points.append(m_stabilizerFilter->filter(m_rawStartPoint));
    m_activeStroke.bounds=computeBounds(m_activeStroke.points,m_size*.7);m_hasActiveStroke=true;m_showEraserCursor=false;
    m_holdAnchorWorld=world;
    if(m_holdToShapeEnabled&&(m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint))m_holdTimer->start();
    Q_UNUSED(mods);if(m_activeStroke.tool==ToolType::Highlighter)invalidateStaticCache();else update();
}
void CanvasWidget::moveInput(const QPointF& screen,double pressure,double tiltX,double tiltY,bool hardware,bool eraser,Qt::KeyboardModifiers mods) {
    Q_UNUSED(hardware);const auto world=screenToWorld(screen);
    auto snapped=[&](QPointF raw,std::optional<QPointF> anchor,QString excluded) {
        const auto result=m_smartGuidesEnabled?snapPointWithGuides(raw,anchor,m_shapes,excluded,m_viewport.zoom):SnapResult{raw,{}};
        m_activeGuides=result.guides;return result.point;
    };
    if(m_mode==InteractionMode::LaserTrail) {m_laserTrail.append({world,nowMs(),false});if(!m_laserTimer->isActive())m_laserTimer->start();update();return;}
    if(m_mode==InteractionMode::TextMove) {
        for(auto& item:m_texts)if(item.id==m_selectedTextId){item.pos=m_textOriginalPos+world-m_textDragStartWorld;item.bounds=textBounds(item);}
        update();return;
    }
    if(m_mode==InteractionMode::Pan) {
        const auto delta=screen-m_panStartScreen;m_viewport.x=m_panStartViewport.x()+delta.x();m_viewport.y=m_panStartViewport.y()+delta.y();invalidateStaticCache();return;
    }
    if(m_mode==InteractionMode::Erase) {
        const auto previous=m_eraserWorldPos;const int steps=std::max(1,int(std::ceil(distance(world,previous)/std::max(.5,m_eraserSize/4))));
        for(int i=1;i<=steps;++i)applyEraseAt(previous+(world-previous)*(double(i)/steps));
        m_eraserWorldPos=world;update();return;
    }
    if(m_mode==InteractionMode::LassoDraw) {
        if(m_lassoPolygon.isEmpty()||distance(world,m_lassoPolygon.back())*m_viewport.zoom>1)m_lassoPolygon.append(world);
        update();return;
    }
    if(m_mode==InteractionMode::LassoMove||m_mode==InteractionMode::LassoScale||m_mode==InteractionMode::LassoRotate) {
        m_strokes=m_snapshotBeforeLassoTransform.strokes;m_shapes=m_snapshotBeforeLassoTransform.shapes;
        if(m_mode==InteractionMode::LassoMove)translateSelection(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds,world-m_lassoDragStartWorld);
        else if(m_mode==InteractionMode::LassoRotate) {
            const double angle=std::atan2(world.y()-m_lassoTransformCenter.y(),world.x()-m_lassoTransformCenter.x())-m_lassoStartAngle;
            rotateSelection(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds,m_lassoTransformCenter,angle);
        } else {
            const auto bounds=computeSelectionBounds(m_snapshotBeforeLassoTransform.strokes,m_snapshotBeforeLassoTransform.shapes,m_selectedStrokeIds,m_selectedShapeIds);
            const QVector<QPointF> corners{bounds.topLeft(),bounds.topRight(),bounds.bottomRight(),bounds.bottomLeft()};
            const auto diagonal=corners[m_lassoHandleIndex]-m_lassoTransformCenter,target=diagonal+world-m_lassoDragStartWorld;
            const double denominator=diagonal.x()*diagonal.x()+diagonal.y()*diagonal.y();
            const double scale=std::clamp(denominator>1e-10?(diagonal.x()*target.x()+diagonal.y()*target.y())/denominator:1.0,.05,20.0);
            scaleSelection(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds,m_lassoTransformCenter,scale,scale);
        }
        m_lassoBounds=computeSelectionBounds(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds);update();return;
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
    if(m_mode==InteractionMode::ShapeMove||m_mode==InteractionMode::ShapeDragHandle) {
        if(auto* shape=findShapeById(m_selectedShapeId)) {
            shape->vertices=m_shapeDragOriginalVertices;
            if(m_mode==InteractionMode::ShapeDragHandle)applyHandleDrag(*shape,m_activeHandleIndex,snapped(world,handleAnchor(*shape,m_activeHandleIndex),shape->id));
            else {
                const auto origin=m_shapeDragOriginalVertices.front();
                const auto target=snapped(origin+world-m_shapeDragStartWorld,std::nullopt,shape->id);
                translateShape(*shape,target-origin);
            }
        }
        update();return;
    }
    if(m_mode==InteractionMode::ShapeCreate&&m_hasActiveShape) {
        if(m_holdSnappedToShape) {
            if(distance(world,m_shapeDragStartWorld)*m_viewport.zoom<=1) {
                m_activeShape.vertices=m_shapeDragOriginalVertices;m_activeShape.bounds=computeShapeBounds(m_activeShape);update();return;
            }
            m_activeShape.vertices=m_shapeDragOriginalVertices;
            applyHandleDrag(m_activeShape,m_activeHandleIndex,snapped(world,handleAnchor(m_activeShape,m_activeHandleIndex),m_activeShape.id));
        } else {
            // Only snap angles for Line shapes (not Rectangles, Circles, Ellipses, Triangles!)
            const bool allowAngleSnap = (m_activeShape.kind == ShapeKind::Line);
            const auto anchor = allowAngleSnap ? std::optional<QPointF>(m_shapeCreateOrigin) : std::nullopt;
            createShapeVertices(m_activeShape, m_shapeCreateOrigin, snapped(world, anchor, m_activeShape.id));
        }
        update();return;
    }
    if(m_mode==InteractionMode::Draw&&m_hasActiveStroke) {
        auto point=m_stabilizerFilter->filter(inputPoint(world,pressure,tiltX,tiltY));
        if(mods.testFlag(Qt::ShiftModifier)){point=snappedPoint(m_rawStartPoint,point);m_activeStroke.points={m_rawStartPoint,point};}
        else {
            const auto& last=m_activeStroke.points.back();
            if(distance(QPointF(point.x,point.y),QPointF(last.x,last.y))>.001||std::abs(point.pressure-last.pressure)>.001||std::abs(point.tiltX-last.tiltX)>.01||std::abs(point.tiltY-last.tiltY)>.01)m_activeStroke.points.append(point);
        }
        m_activeStroke.bounds=computeBounds(m_activeStroke.points,m_activeStroke.size*.7);
        if(m_holdToShapeEnabled&&(m_activeStroke.tool==ToolType::Pen||m_activeStroke.tool==ToolType::Ballpoint)&&distance(world,m_holdAnchorWorld)*m_viewport.zoom>4){m_holdAnchorWorld=world;m_holdTimer->start();}
        update();return;
    }
    if(m_polylineActive) {
        m_polylineSnapClose=m_polylineVertices.size()>=3&&distance(world,m_polylineVertices.front())*m_viewport.zoom<=14;
        m_polylineHoverWorld=m_polylineSnapClose?m_polylineVertices.front():snapped(world,m_polylineVertices.back(),m_activeShape.id);
        if(m_polylineSnapClose)m_activeGuides.clear();
        m_activeShape.vertices=m_polylineVertices;
        if(distance(m_activeShape.vertices.back(),m_polylineHoverWorld)>.001)m_activeShape.vertices.append(m_polylineHoverWorld);
        m_activeShape.closed=false;m_activeShape.bounds=computeShapeBounds(m_activeShape);update();return;
    }
    updatePenCursor(world,m_mode==InteractionMode::None&&!eraser&&!m_spaceHeld&&
        (m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint||m_tool==ToolType::Highlighter));
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
void CanvasWidget::triggerHoldToShape() {
    if(!m_holdToShapeEnabled||m_mode!=InteractionMode::Draw||!m_hasActiveStroke||(m_activeStroke.tool!=ToolType::Pen&&m_activeStroke.tool!=ToolType::Ballpoint))return;
    double total=0;for(qsizetype i=1;i<m_activeStroke.points.size();++i)total+=distance({m_activeStroke.points[i-1].x,m_activeStroke.points[i-1].y},{m_activeStroke.points[i].x,m_activeStroke.points[i].y});
    if(total*m_viewport.zoom<25)return;
    const auto recognized=recognizeFreehandShape(m_activeStroke);if(!recognized)return;
    m_activeShape=*recognized;m_hasActiveShape=true;m_hasActiveStroke=false;m_holdSnappedToShape=true;m_selectedShapeId=m_activeShape.id;
    m_snapshotBeforeShapeEdit=currentSnapshot();m_shapeDragOriginalVertices=m_activeShape.vertices;
    const auto last=m_activeStroke.points.back();m_shapeDragStartWorld={last.x,last.y};m_activeHandleIndex=hitTestHandle(m_activeShape,{last.x,last.y},1e12);
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
    }
    emit toastRequested(name+QStringLiteral(" ✓"));invalidateStaticCache();emit stateChanged();
}
void CanvasWidget::endInput(const QPointF& screen,Qt::KeyboardModifiers mods) {
    m_holdTimer->stop();
    if(m_mode==InteractionMode::LaserTrail) {m_mode=InteractionMode::None;update();return;}
    if(m_mode==InteractionMode::TextMove) {
        moveInput(screen,0,0,0,false,false,mods);
        if(distance(screenToWorld(screen),m_textDragStartWorld)*m_viewport.zoom>.01)pushUndoSnapshot(m_snapshotBeforeTextEdit);
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::LassoDraw) {
        const auto world=screenToWorld(screen);if(m_lassoPolygon.isEmpty()||distance(world,m_lassoPolygon.back())>.001)m_lassoPolygon.append(world);
        selectByLasso(m_lassoPolygon,m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds);
        m_lassoBounds=computeSelectionBounds(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds);
        if(!hasLassoSelection())for(qsizetype i=m_images.size();i-->0;)if(!m_images[i].isPdfPage&&m_images[i].rect.contains(world)){m_selectedImageId=m_images[i].id;break;}
        m_lassoPolygon.clear();invalidateStaticCache();
    } else if(m_mode==InteractionMode::LassoMove||m_mode==InteractionMode::LassoScale||m_mode==InteractionMode::LassoRotate) {
        moveInput(screen,0,0,0,false,false,mods);
        if(distance(screenToWorld(screen),m_lassoDragStartWorld)*m_viewport.zoom>.01)pushUndoSnapshot(m_snapshotBeforeLassoTransform);
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::ImageMove||m_mode==InteractionMode::ImageResize) {
        moveInput(screen,0,0,0,false,false,mods);
        if(distance(screenToWorld(screen),m_imageDragStartWorld)*m_viewport.zoom>.01)pushUndoSnapshot(m_snapshotBeforeImageEdit);
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::Draw&&m_hasActiveStroke) {
        const auto& last=m_activeStroke.points.back();auto end=inputPoint(screenToWorld(screen),last.pressure,last.tiltX,last.tiltY);
        if(mods.testFlag(Qt::ShiftModifier))m_activeStroke.points={m_rawStartPoint,snappedPoint(m_rawStartPoint,end)};
        else if(distance({last.x,last.y},{end.x,end.y})>.001)m_activeStroke.points.append(end);
        pushUndoSnapshot(currentSnapshot());m_activeStroke.bounds=computeBounds(m_activeStroke.points,m_activeStroke.size*.7);
        m_strokes.append(m_activeStroke);m_hasActiveStroke=false;
        if(m_mathBeautifyEnabled&&!mods.testFlag(Qt::ShiftModifier)
           &&(m_activeStroke.tool==ToolType::Pen||m_activeStroke.tool==ToolType::Ballpoint)) {
            int newIdx=int(m_strokes.size())-1;
            const QRectF nb=m_activeStroke.bounds;
            bool sameChar=false;
            if(m_pendingBeautifyStart>=0&&m_pendingBeautifyStart<newIdx&&(newIdx-m_pendingBeautifyStart)<3) {
                const QRectF& pb=m_pendingBeautifyBox;
                const double overlapX=std::min(pb.right(),nb.right())-std::max(pb.left(),nb.left());
                const double minW=std::max(6.0,std::min(pb.width(),nb.width()));
                const double refH=std::max({pb.height(),nb.height(),24.0});
                const double dy=std::abs(pb.center().y()-nb.center().y());
                // Un circulito pequeño arriba-derecha (signo de grado °) NO se fusiona con el número previo!
                const bool isDegreePos=(std::max(nb.width(),nb.height())<=pb.height()*0.55)
                                       &&(nb.center().y()<=pb.top()+pb.height()*0.38)
                                       &&(nb.center().x()>=pb.center().x());
                // SOLO fusionar trazos que realmente se solapan en X (>= 28% del ancho menor),
                // jamás dígitos vecinos como '1' y '0' o '2' y '0'!
                sameChar=(!isDegreePos&&dy<=refH*1.10&&overlapX>=minW*0.28);
            }
            if(sameChar) {
                m_pendingBeautifyBox=m_pendingBeautifyBox.united(nb);
            } else {
                if(m_pendingBeautifyStart>=0&&m_pendingBeautifyStart<newIdx) {
                    QVector<Stroke> candidate=m_strokes;QString label;int delta=0;
                    const QColor opCol=m_autoColorOperators?m_operatorColor:QColor();
                    if(HandwritingEngine::instance().beautifyCluster(candidate,m_pendingBeautifyStart,newIdx,opCol,&label,&delta,m_shapes)) {
                        pushUndoSnapshot(currentSnapshot());
                        m_strokes=std::move(candidate);
                        newIdx+=delta;
                    }
                }
                m_pendingBeautifyStart=newIdx;
                m_pendingBeautifyBox=nb;
            }
            m_beautifyTimer->start(380);
        }
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::ShapeCreate&&m_hasActiveShape) {
        // No artificial resize on release after recognition: a held pen can lift
        // at the same physical position without changing its recognized geometry.
        if(!m_holdSnappedToShape)moveInput(screen,0,0,0,false,false,mods);
        const auto bounds=computeShapeBounds(m_activeShape);
        if(std::max(bounds.width()-m_activeShape.size,bounds.height()-m_activeShape.size)*m_viewport.zoom>3) {
            pushUndoSnapshot(m_snapshotBeforeShapeEdit);m_activeShape.bounds=bounds;m_shapes.append(m_activeShape);m_selectedShapeId=m_activeShape.id;
        } else m_selectedShapeId.clear();
        m_hasActiveShape=false;m_holdSnappedToShape=false;invalidateStaticCache();
    } else if(m_mode==InteractionMode::ShapeMove||m_mode==InteractionMode::ShapeDragHandle) {
        moveInput(screen,0,0,0,false,false,mods);
        if(const auto* shape=findShapeById(m_selectedShapeId);shape&&shape->vertices!=m_shapeDragOriginalVertices)pushUndoSnapshot(m_snapshotBeforeShapeEdit);
        invalidateStaticCache();
    } else if(m_mode==InteractionMode::Erase) {
        moveInput(screen,0,0,0,false,true,mods);if(m_erasedAnything)pushUndoSnapshot(m_snapshotBeforeErase);
        m_snapshotBeforeErase={};m_erasedAnything=false;
    }
    m_mode=InteractionMode::None;m_activeGuides.clear();m_activeHandleIndex=-1;updateCursorShape();saveAutosave();emit stateChanged();update();
}
void CanvasWidget::flushPendingBeautify() {
    if(m_beautifyTimer)m_beautifyTimer->stop();
    const int start=m_pendingBeautifyStart;
    m_pendingBeautifyStart=-1;
    m_pendingBeautifyBox=QRectF();
    if(!m_mathBeautifyEnabled||m_mode!=InteractionMode::None||start<0||start>=m_strokes.size())
        return;
    QVector<Stroke> candidate=m_strokes;
    QString label;
    const QColor opCol=m_autoColorOperators?m_operatorColor:QColor();
    if(HandwritingEngine::instance().beautifyCluster(candidate,start,-1,opCol,&label,nullptr,m_shapes)) {
        pushUndoSnapshot(currentSnapshot()); // 1er Ctrl+Z vuelve al trazo original!
        m_strokes=std::move(candidate);
        invalidateStaticCache();
        saveAutosave();
        emit stateChanged();
        update();
    }
}
void CanvasWidget::applyEraseAt(const QPointF& world) {
    QVector<Stroke> next;bool changed=false;
    for(const auto& stroke:m_strokes) {
        if(m_eraserMode==EraserMode::Stroke){if(hitTestStroke(stroke,world,m_eraserSize/2))changed=true;else next.append(stroke);}
        else {const auto split=eraseStrokePartially(stroke,world,m_eraserSize/2);if(split){changed=true;next+=*split;}else next.append(stroke);}
    }
    QVector<ShapeItem> shapes;
    for(const auto& shape:m_shapes) {
        if(hitTestShape(shape,world,m_eraserSize/2)){changed=true;if(shape.id==m_selectedShapeId)m_selectedShapeId.clear();}
        else shapes.append(shape);
    }
    const QRectF eraserRect(world-QPointF(m_eraserSize/2,m_eraserSize/2),QSizeF(m_eraserSize,m_eraserSize));
    const auto removed=m_texts.removeIf([&](const TextItem& item){return item.bounds.intersects(eraserRect);});
    if(removed){changed=true;if(std::none_of(m_texts.cbegin(),m_texts.cend(),[&](const TextItem& item){return item.id==m_selectedTextId;}))m_selectedTextId.clear();}
    if(changed){m_strokes=std::move(next);m_shapes=std::move(shapes);m_erasedAnything=true;invalidateStaticCache();}
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

    const bool newlyPressedErase = (btn == eraseBtn) || ((btns & eraseBtn) && !(m_lastTabletButtons & eraseBtn));
    const bool newlyPressedWrite = (btn == writeBtn) || ((btns & writeBtn) && !(m_lastTabletButtons & writeBtn));
    m_lastTabletButtons = btns;

    if (newlyPressedErase && event->pressure() <= 0.01) {
        switchTabletTool(ToolType::Eraser);
        return;
    }
    if (newlyPressedWrite && event->pressure() <= 0.01) {
        switchTabletTool(m_lastWriteTool);
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
    event->accept();if(m_tabletActive||event->source()!=Qt::MouseEventNotSynthesized)return;
    moveInput(event->position(),0.5,0,0,false,false,event->modifiers());
}
void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    event->accept();if(m_tabletActive||event->source()!=Qt::MouseEventNotSynthesized)return;
    if((m_tool==ToolType::Text||m_tool==ToolType::Lasso)&&event->button()==Qt::LeftButton) {
        const auto world=screenToWorld(event->position());
        if(std::any_of(m_texts.cbegin(),m_texts.cend(),[&](const TextItem& item){return item.bounds.adjusted(-4,-4,4,4).contains(world);})) {
            m_mode=InteractionMode::None;setProperty("_finily_edit_text",true);
            beginInput(event->position(),.5,0,0,false,false,Qt::LeftButton,event->modifiers());return;
        }
    }
    if(m_tool==ToolType::Polyline&&m_polylineActive&&event->button()==Qt::LeftButton) {
        const auto world=screenToWorld(event->position());
        if(distance(world,m_polylineVertices.back())*m_viewport.zoom>2)beginInput(event->position(),.5,0,0,false,false,Qt::LeftButton,event->modifiers());
        finishActivePolyline();
    }
}
void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    event->accept();if(m_tabletActive||event->source()!=Qt::MouseEventNotSynthesized)return;
    // Another mouse button released during a pan must not end the drag.
    if(m_mode==InteractionMode::Pan && (event->buttons()&(Qt::LeftButton|Qt::MiddleButton|Qt::RightButton)))return;
    if(m_mode==InteractionMode::Draw&&event->button()!=Qt::LeftButton)return;
    endInput(event->position(),event->modifiers());
}
void CanvasWidget::wheelEvent(QWheelEvent* event) {
    event->accept();m_zoomAnimation->stop();if(m_mode!=InteractionMode::None)return;
    const double delta=event->pixelDelta().isNull()?event->angleDelta().y():event->pixelDelta().y()*2.0;
    zoomByFactor(std::exp(delta*0.0015),event->position());
}
void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if(event->key()==Qt::Key_0&&event->modifiers().testFlag(Qt::ControlModifier)){resetViewport();event->accept();return;}
    if(event->key()==Qt::Key_Space){clearPenCursor();m_spaceHeld=true;updateCursorShape();event->accept();return;}
    if(m_polylineActive&&(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)){finishActivePolyline();event->accept();return;}
    if(event->key()==Qt::Key_Escape) {
        if(m_polylineActive)cancelActivePolyline();
        else if(m_mode!=InteractionMode::None) {
            m_holdTimer->stop();
            if(m_mode==InteractionMode::Erase){m_strokes=m_snapshotBeforeErase.strokes;m_shapes=m_snapshotBeforeErase.shapes;m_images=m_snapshotBeforeErase.images;m_texts=m_snapshotBeforeErase.texts;}
            if(m_mode==InteractionMode::ShapeMove||m_mode==InteractionMode::ShapeDragHandle){m_strokes=m_snapshotBeforeShapeEdit.strokes;m_shapes=m_snapshotBeforeShapeEdit.shapes;m_images=m_snapshotBeforeShapeEdit.images;}
            if(m_mode==InteractionMode::LassoMove||m_mode==InteractionMode::LassoScale||m_mode==InteractionMode::LassoRotate){m_strokes=m_snapshotBeforeLassoTransform.strokes;m_shapes=m_snapshotBeforeLassoTransform.shapes;m_images=m_snapshotBeforeLassoTransform.images;}
            if(m_mode==InteractionMode::TextMove)m_texts=m_snapshotBeforeTextEdit.texts;
            if(m_mode==InteractionMode::ImageMove||m_mode==InteractionMode::ImageResize)m_images=m_snapshotBeforeImageEdit.images;
            m_hasActiveStroke=false;m_hasActiveShape=false;m_holdSnappedToShape=false;m_mode=InteractionMode::None;m_erasedAnything=false;m_activeGuides.clear();clearSelection();updateCursorShape();
        } else clearSelection();
        event->accept();return;
    }
    if(event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace){deleteSelection();event->accept();return;}
    if(event->matches(QKeySequence::Paste)){pasteFromClipboard();event->accept();return;}
    if(event->key()==Qt::Key_D&&event->modifiers().testFlag(Qt::ControlModifier)){duplicateSelection();event->accept();return;}
    if(event->matches(QKeySequence::Undo)){undo();event->accept();return;}
    if(event->matches(QKeySequence::Redo)){redo();event->accept();return;}
    QWidget::keyPressEvent(event);
}
void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if(event->key()==Qt::Key_Space&&!event->isAutoRepeat()){m_spaceHeld=false;updateCursorShape();event->accept();return;}
    QWidget::keyReleaseEvent(event);
}
void CanvasWidget::clearPenCursor() {
    if(!m_showPenCursor)return;
    m_showPenCursor=false;update(m_penCursorRect);m_penCursorRect={};
}
void CanvasWidget::updatePenCursor(const QPointF& world,bool visible) {
    if(!visible){clearPenCursor();return;}
    const QPointF center=world*m_viewport.zoom+QPointF(m_viewport.x,m_viewport.y);
    const double radius=strokeSize()*m_viewport.zoom/2.0+2.0;
    const QRect dirty=QRectF(center-QPointF(radius,radius),QSizeF(radius*2,radius*2)).toAlignedRect();
    // Coalesce subpixel hardware jitter without timers or full-canvas repaints.
    if(m_showPenCursor&&dirty==m_penCursorRect&&distance(world,m_hoverWorldPos)*m_viewport.zoom<.5)return;
    if(m_showPenCursor)update(m_penCursorRect);
    m_hoverWorldPos=world;m_showPenCursor=true;m_penCursorRect=dirty;update(dirty);
}
QRect CanvasWidget::shapePulseRect() const {
    const auto bounds=computeShapeBounds(m_shapePulseShape);
    const QPointF offset(m_viewport.x,m_viewport.y);
    return QRectF(bounds.topLeft()*m_viewport.zoom+offset,bounds.bottomRight()*m_viewport.zoom+offset)
        .adjusted(-28,-28,28,28).toAlignedRect();
}
void CanvasWidget::stopShapePulse() {
    if(!m_shapePulseAnimation)return;
    m_shapePulseAnimation->stop();
    if(m_shapePulseAlpha>0.0){m_shapePulseAlpha=0.0;update(shapePulseRect());}
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
void CanvasWidget::updateCursorShape() {
    if(m_mode==InteractionMode::Pan)setCursor(Qt::ClosedHandCursor);
    else if(m_spaceHeld||m_tool==ToolType::Hand)setCursor(Qt::OpenHandCursor);
    else if(m_tool==ToolType::Eraser||m_mode==InteractionMode::Erase)setCursor(Qt::BlankCursor);
    else setCursor(Qt::CrossCursor);
}

// Miniatura simplificada de una página (fondo del tema + trazos y figuras como polilíneas).
QImage CanvasWidget::pageThumbnail(int index, const QSize& size) const {
    QImage img(size * 2, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(2.0);
    const ThemeConfig theme = getThemeConfig(m_themeId);
    img.fill(theme.background);
    if (index < 0 || index >= m_pages.size())
        return img;
    const bool cur = index == m_currentPageIndex;
    const QVector<Stroke>& strokes = cur ? m_strokes : m_pages[index].strokes;
    const QVector<ShapeItem>& shapes = cur ? m_shapes : m_pages[index].shapes;
    QRectF box;
    for (const auto& st : strokes)
        box = box.isNull() ? st.bounds : box.united(st.bounds);
    for (const auto& sh : shapes)
        box = box.isNull() ? sh.bounds : box.united(sh.bounds);
    if (box.isNull() || box.width() <= 0 || box.height() <= 0)
        return img;
    const double margin = 8;
    const double k = std::min((size.width() - 2 * margin) / std::max(box.width(), 1.0),
                              (size.height() - 2 * margin) / std::max(box.height(), 1.0));
    const double scale = std::min(k, 0.6);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(size.width() / 2.0 - box.center().x() * scale, size.height() / 2.0 - box.center().y() * scale);
    p.scale(scale, scale);
    auto pen = [&](const QColor& c, double w, double op) {
        QColor col = c;
        col.setAlphaF(op);
        QPen q(col, std::max(w, 1.4 / scale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(q);
    };
    for (const auto& st : strokes) {
        pen(st.color, st.size, st.tool == ToolType::Highlighter ? 0.35 : st.opacity);
        QPolygonF poly;
        for (const auto& pt : st.points)
            poly << QPointF(pt.x, pt.y);
        if (poly.size() > 1)
            p.drawPolyline(poly);
        else if (poly.size() == 1)
            p.drawPoint(poly[0]);
    }
    for (const auto& sh : shapes) {
        pen(sh.color, sh.size, sh.opacity);
        QPolygonF poly(sh.vertices);
        if (sh.kind == ShapeKind::Circle && sh.vertices.size() >= 2) {
            const double r = QLineF(sh.vertices[0], sh.vertices[1]).length();
            p.drawEllipse(sh.vertices[0], r, r);
        } else if ((sh.kind == ShapeKind::Ellipse || sh.kind == ShapeKind::Rectangle) && sh.vertices.size() >= 2) {
            const QRectF r(sh.vertices[0], sh.vertices[1]);
            sh.kind == ShapeKind::Ellipse ? p.drawEllipse(r.normalized()) : p.drawRect(r.normalized());
        } else if (poly.size() > 1) {
            sh.closed ? p.drawPolygon(poly) : p.drawPolyline(poly);
        }
    }
    return img;
}
} // namespace finily
