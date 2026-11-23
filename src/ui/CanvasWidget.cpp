#include "CanvasWidget.h"
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
    const double pad = (format == PageFormat::Slide16x9) ? 0.0 : 24.0;
    const double zoom=std::clamp(std::min(std::max(1.0,size.width()-pad)/sheet.width(),
        std::max(1.0,size.height()-pad)/sheet.height()),0.2,5.0);
    return {size.width()/2.0-sheet.center().x()*zoom,size.height()/2.0-sheet.center().y()*zoom,zoom};
}
Viewport fittedPdfViewport(const QRectF& bounds, const QSize& size) {
    const double w = std::max(1.0, size.width() - 64.0);
    const double h = std::max(1.0, size.height() - 64.0);
    const double zoom = std::clamp(std::min(w / bounds.width(), h / bounds.height()), 0.2, 5.0);
    return {size.width() / 2.0 - bounds.center().x() * zoom,
            size.height() / 2.0 - bounds.center().y() * zoom, zoom};
}

QVector<int> validPageIndices(QVector<int> indices, int count) {
    indices.removeIf([count](int index) { return index < 0 || index >= count; });
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}
qint64 nowMs() { static const QElapsedTimer timer=[] { QElapsedTimer t;t.start();return t; }();return timer.elapsed(); }
QString autosavePath() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+QStringLiteral("/autosave.json"); }
QJsonObject strokeJson(const Stroke& s) {
    QJsonArray points;
    for (const auto& p:s.points) points.append(QJsonArray{p.x,p.y,p.pressure,double(p.timestampMs),p.tiltX,p.tiltY});
    return {{"id",s.id},{"groupId",s.groupId},{"tool",int(s.tool)},{"color",s.color.name(QColor::HexArgb)},{"size",s.size},
            {"opacity",s.opacity},{"linePattern",int(s.linePattern)},{"pressureProfile",int(s.pressureProfile)},{"hasHardwarePressure",s.hasHardwarePressure},{"tipStyle",s.tipStyle},{"points",points}};
}
constexpr auto objectMimeType = "application/x-finily-objects";
bool linkedFillOf(const ShapeItem& fill, const QString& id) {
    return fill.kind == ShapeKind::ShadedRegion && fill.shadeBoundary.size() == 1
        && fill.shadeBoundary.contains(id);
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
    QJsonObject result{{"id",shape.id},{"groupId",shape.groupId},{"kind",int(shape.kind)},{"color",shape.color.name(QColor::HexArgb)},
                       {"size",shape.size},{"opacity",shape.opacity},{"linePattern",int(shape.linePattern)},{"lineEnding",int(shape.lineEnding)},
                       {"closed",shape.closed},{"locked",shape.locked},{"solidKind",shape.solidKind},{"vertices",vertices}};
    if (!shape.label.isEmpty())
        result.insert("label", shape.label);
    if (shape.kind == ShapeKind::ShadedRegion) {
        result.insert("shadePattern", int(shape.shadePattern));
        if (!shape.shadeBoundary.isEmpty()) {
            QJsonObject boundary;
            for (auto it = shape.shadeBoundary.cbegin(); it != shape.shadeBoundary.cend(); ++it)
                boundary.insert(it.key(), it.value());
            result.insert("shadeBoundary", boundary);
            result.insert("shadeProbe", QJsonArray{shape.shadeProbe.x(), shape.shadeProbe.y()});
            result.insert("shadeVisible", shape.shadeVisible);
        }
        if (!shape.shadeContourStarts.isEmpty()) {
            QJsonArray starts;
            for (int start : shape.shadeContourStarts)
                starts.append(start);
            result.insert("shadeContourStarts", starts);
        }
    }
    if (shape.attachmentsKnown) {
        QJsonArray links;
        for (const auto& attachment : shape.attachments)
            links.append(QJsonArray{attachment.vertex, attachment.targetId, attachment.edge, attachment.position});
        result.insert("attachments", links);
    }
    return result;
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
    bool event(QEvent* event) override {
        if(event->type()==QEvent::ShortcutOverride) {
            const auto* ke=static_cast<QKeyEvent*>(event);
            const auto k=ke->key();
            if(k==Qt::Key_Escape||k==Qt::Key_Return||k==Qt::Key_Enter||k==Qt::Key_Tab
               ||k==Qt::Key_Backspace||k==Qt::Key_Delete
               ||(!ke->modifiers().testFlag(Qt::ControlModifier)&&!ke->modifiers().testFlag(Qt::AltModifier))) {
                event->accept();return true;
            }
        }
        return QLineEdit::event(event);
    }
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
    for(const auto& item:document.texts)texts.append(QJsonObject{{"id",item.id},{"groupId",item.groupId},{"pos",QJsonArray{item.pos.x(),item.pos.y()}},
        {"text",item.text},{"color",item.color.name(QColor::HexArgb)},{"fontSize",item.fontSize},{"locked",item.locked}});
    for(const auto& image:document.images) {
        QJsonObject value{{"id",image.id},{"groupId",image.groupId},{"rect",QJsonArray{image.rect.x(),image.rect.y(),image.rect.width(),image.rect.height()}},
            {"isPdfPage",image.isPdfPage},{"pdfPageIndex",image.pdfPageIndex},{"pdfSourcePath",image.pdfSourcePath},{"locked",image.locked}};
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
    QJsonObject page{{"strokes",strokes},{"shapes",shapes},{"images",images},{"texts",texts},
        {"viewport",QJsonObject{{"x",document.viewport.x},{"y",document.viewport.y},{"zoom",document.viewport.zoom}}}};
    if (document.background)
        page.insert("pageBackground", int(*document.background));
    return page;
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
        if(tool<0||tool>int(ToolType::Highlighter)||pattern<0||pattern>int(LinePattern::Dimension))continue;
        const int profile=o.value("pressureProfile").toInt(int(PressureProfile::Light));
        if(profile<int(PressureProfile::Light)||profile>int(PressureProfile::Firm))continue;
        s.pressureProfile=PressureProfile(profile);
        s.tool=ToolType(tool);s.linePattern=LinePattern(pattern);s.id=o.value("id").toString();
        s.groupId=o.value("groupId").toString();
        s.tipStyle = std::clamp(o.value("tipStyle").toInt(s.tool == ToolType::Pen ? 1 : 0), 0, s.tool == ToolType::Highlighter ? 2 : 3);
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
        if(kind<0||kind>int(ShapeKind::AngleMarker)||pattern<0||pattern>int(LinePattern::Dimension))continue;
        ShapeItem shape;shape.kind=ShapeKind(kind);shape.linePattern=LinePattern(pattern);
        shape.locked=o.value("locked").toBool(false);
        shape.lineEnding = LineEnding(std::clamp(o.value("lineEnding").toInt(), 0, 2));
        if (shape.linePattern == LinePattern::Arrow || shape.linePattern == LinePattern::DoubleArrow) {
            shape.lineEnding = shape.linePattern == LinePattern::Arrow ? LineEnding::Arrow : LineEnding::DoubleArrow;
            shape.linePattern = LinePattern::Solid;
        }
        shape.label = o.value("label").toString().left(80);
        shape.solidKind=std::clamp(o.value("solidKind").toInt(0),0,9);
        shape.id=o.value("id").toString();if(shape.id.isEmpty())shape.id=freshId();
        shape.groupId=o.value("groupId").toString();
        shape.color=QColor(o.value("color").toString());shape.size=o.value("size").toDouble(2.6);shape.opacity=o.value("opacity").toDouble(1);
        if(!shape.color.isValid()||!finite(shape.size)||shape.size<=0||shape.size>200||!finite(shape.opacity))continue;
        shape.opacity=std::clamp(shape.opacity,0.0,1.0);shape.closed=o.value("closed").toBool();bool valid=true;
        for(const auto& vertex:o.value("vertices").toArray()) {
            const auto a=vertex.toArray();if(a.size()!=2||!a[0].isDouble()||!a[1].isDouble()||!finite(a[0].toDouble())||!finite(a[1].toDouble())){valid=false;break;}
            shape.vertices.append({a[0].toDouble(),a[1].toDouble()});
        }
        const bool threeVertices = shape.kind == ShapeKind::Triangle || shape.kind == ShapeKind::Radical
            || shape.kind == ShapeKind::Curve || shape.kind == ShapeKind::RightAngle
            || shape.kind == ShapeKind::Arc || shape.kind == ShapeKind::Sector || shape.kind == ShapeKind::AngleMarker;
        const qsizetype expected = threeVertices ? 3 : 2;
        if(!valid)continue;
        if (shape.kind == ShapeKind::ShadedRegion) {
            const int shade = o.value("shadePattern").toInt(0);
            if (shade < int(ShadePattern::Soft) || shade > int(ShadePattern::Grid)
                || !shape.closed)
                continue;
            int previous = 0;
            for (const auto& offset : o.value("shadeContourStarts").toArray()) {
                const int start = offset.toInt(-1);
                if (start <= previous || start >= shape.vertices.size()) {
                    valid = false;
                    break;
                }
                const int count = start - previous;
                if (count < 7 || (count - 1) % 3 != 0) {
                    valid = false;
                    break;
                }
                shape.shadeContourStarts.append(start);
                previous = start;
            }
            const auto lastCount = shape.vertices.size() - previous;
            if (!valid || lastCount < 7 || (lastCount - 1) % 3 != 0)
                continue;
            shape.shadePattern = ShadePattern(shade);
            const auto boundary = o.value("shadeBoundary").toObject();
            const auto probe = o.value("shadeProbe").toArray();
            if (probe.size() == 2 && probe[0].isDouble() && probe[1].isDouble()
                && finite(probe[0].toDouble()) && finite(probe[1].toDouble())) {
                for (auto it = boundary.constBegin(); it != boundary.constEnd(); ++it) {
                    const int side = it.value().toInt();
                    if (!it.key().isEmpty() && side >= 1 && side <= 3)
                        shape.shadeBoundary.insert(it.key(), side);
                }
                shape.shadeProbe = {probe[0].toDouble(), probe[1].toDouble()};
                shape.shadeVisible = o.value("shadeVisible").toBool(true);
            }
        } else if (shape.vertices.size()<expected||(shape.kind!=ShapeKind::Polyline&&shape.vertices.size()!=expected))continue;
        const auto links = o.value("attachments");
        shape.attachmentsKnown = links.isArray();
        for (const auto& link : links.toArray()) {
            const auto fields = link.toArray();
            if (fields.size() != 4 || !fields[3].isDouble()) continue;
            const int vertex = fields[0].toInt(-1), edge = fields[2].toInt(-4);
            const auto target = fields[1].toString();
            const double position = fields[3].toDouble();
            if ((vertex == 0 || vertex == shape.vertices.size() - 1) && !target.isEmpty() && target != shape.id
                && edge >= -3 && finite(position) && (edge == -2 || (position >= 0 && position <= 1)))
                shape.attachments.append({vertex, target, edge, position});
        }
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
        if(!image.isPdfPage)image.groupId=o.value("groupId").toString();
        image.locked = o.value("locked").toBool();
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
        item.groupId=o.value("groupId").toString();
        item.locked = o.value("locked").toBool();
        item.pos={pos[0].toDouble(),pos[1].toDouble()};item.text=o.value("text").toString();item.color=QColor(o.value("color").toString());item.fontSize=o.value("fontSize").toDouble(22);
        if(item.text.trimmed().isEmpty()||!item.color.isValid()||!finite(item.fontSize)||item.fontSize<1||item.fontSize>1000)continue;
        item.bounds=textBounds(item);loadedTexts.append(item);
    }
    initializeShapeLinks(loadedShapes);
    const auto view=root.value("viewport").toObject();
    const double x=view.value("x").toDouble(),y=view.value("y").toDouble(),zoom=view.value("zoom").toDouble(1);
    Viewport viewport;if(finite(x)&&finite(y)&&finite(zoom))viewport={x,y,std::clamp(zoom,.2,5.0)};
    std::optional<BackgroundTemplate> background;
    const int storedBackground = root.value("pageBackground").toInt(-1);
    if (storedBackground >= int(BackgroundTemplate::Blank) && storedBackground <= int(BackgroundTemplate::Ruled))
        background = BackgroundTemplate(storedBackground);
    return {loaded,loadedShapes,loadedImages,loadedTexts,viewport,background};
}
void syncWorkingPage(QVector<PageData>& pages,int& index,const DocumentSnapshot& document,Viewport viewport) {
    if(pages.isEmpty())pages.append(PageData{});
    index=std::clamp(index,0,int(pages.size())-1);
    auto& page=pages[index];
    page.strokes=document.strokes;page.shapes=document.shapes;page.images=document.images;page.texts=document.texts;page.viewport=viewport;
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
    m_beautifyTimer=new QTimer(this);m_beautifyTimer->setSingleShot(true);m_beautifyTimer->setInterval(420);
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
    }
    m_pages.append(PageData{});loadAutosave();updateCursorShape();
}
CanvasWidget::~CanvasWidget() {
    finishTextEditor(this,false);
    m_autosaveTimer->stop();writeAutosaveNow();
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
    m_selectedImageId.clear();m_lassoPolygon.clear();m_lassoBounds={};m_lassoHandleIndex=-1;m_imageHandleIndex=-1;
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
    m_hasActiveStroke=false;m_hasActiveShape=false;m_holdSnappedToShape=false;
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
    m_lassoBounds=lassoSelectionBounds();invalidateStaticCache();saveAutosave();emit stateChanged();emit toastRequested(QStringLiteral("Letras niveladas ✓"));
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
void CanvasWidget::setInkColor(const QColor& color) {
    if (!color.isValid() || m_color == color)
        return;
    clearPenCursor();
    m_color = color;
    emit stateChanged();
}

void CanvasWidget::setStrokeSize(double size) {
    if (!finite(size))
        return;
    const double width = std::clamp(size, .1, 200.0);
    if (m_size == width)
        return;
    clearPenCursor();
    m_size = width;
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
void CanvasWidget::setPageFormat(PageFormat format) {
    if(format!=PageFormat::Infinite&&format!=PageFormat::A4&&format!=PageFormat::Slide16x9)return;
    if(m_pageFormat==format)return;
    m_pageFormat=format;m_dirty=true;
    m_zoomAnimation->stop();clearPenCursor();
    if(format!=PageFormat::Infinite)m_viewport=fittedSheetViewport(format,size());
    invalidateStaticCache();saveAutosave();emit pageStructureChanged();emit stateChanged();emit viewportChanged(m_viewport.zoom);
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
    const auto pdf = std::find_if(m_images.cbegin(), m_images.cend(),
                                  [](const auto& item) { return item.isPdfPage && !item.rect.isEmpty(); });
    animateViewport(pdf == m_images.cend() ? fittedSheetViewport(m_pageFormat, size())
                                          : fittedPdfViewport(pdf->rect, size()));
    saveAutosave();
}

bool CanvasWidget::preparePageChange() {
    if (m_mode == InteractionMode::LaserTrail)
        m_mode = InteractionMode::None;
    if (m_mode != InteractionMode::None)
        return false;
    finishTextEditor(this, true);
    if (m_polylineActive)
        finishActivePolyline();
    flushPendingBeautify();
    m_zoomAnimation->stop();
    return true;
}

void CanvasWidget::loadWorkingPage(int index, bool structureChanged) {
    m_zoomAnimation->stop();
    stopShapePulse();
    clearPenCursor();
    m_holdTimer->stop();
    m_laserTimer->stop();
    m_laserTrail.clear();
    m_tabletActive = false;
    m_currentPageIndex = index;
    const auto& page = m_pages.at(index);
    m_strokes = page.strokes;
    m_shapes = page.shapes;
    m_images = page.images;
    m_texts = page.texts;
    m_viewport = page.viewport;
    if (m_viewport.x == 0 && m_viewport.y == 0 && m_viewport.zoom == 1) {
        for (const auto& image : m_images) {
            if (image.isPdfPage && !image.rect.isEmpty()) {
                m_viewport = fittedPdfViewport(image.rect, size());
                break;
            }
        }
    }
    clearSelection();
    updateCursorShape();
    invalidateStaticCache();
    saveAutosave();
    if (structureChanged)
        emit pageStructureChanged();
    emit stateChanged();
    emit viewportChanged(m_viewport.zoom);
}

void CanvasWidget::goToPage(int index) {
    if (index < 0 || index >= pageCount() || index == m_currentPageIndex || !preparePageChange())
        return;
    syncWorkingPage(m_pages, m_currentPageIndex, currentSnapshot(), m_viewport);
    loadWorkingPage(index, false);
}

void CanvasWidget::addPage() { insertPage(m_currentPageIndex + 1); }
void CanvasWidget::nextPage() { goToPage(m_currentPageIndex + 1); }
void CanvasWidget::prevPage() { goToPage(m_currentPageIndex - 1); }
void CanvasWidget::deleteCurrentPage() { deletePages({m_currentPageIndex}); }

void CanvasWidget::insertPage(int index, std::optional<BackgroundTemplate> background) {
    PageData page;
    page.viewport = fittedSheetViewport(m_pageFormat, size());
    page.background = background;
    insertPages(index, {page});
}

void CanvasWidget::insertPages(int index, const QVector<PageData>& pages) {
    if (pages.isEmpty() || index < 0 || index > pageCount() || !preparePageChange())
        return;
    pushPageHistory();
    int position = index;
    for (const auto& page : pages)
        m_pages.insert(position++, page);
    loadWorkingPage(index, true);
}

bool CanvasWidget::movePages(QVector<int> indices, int destination) {
    indices = validPageIndices(std::move(indices), pageCount());
    if (indices.isEmpty() || destination < 0 || destination > pageCount())
        return false;
    const int before = int(std::lower_bound(indices.cbegin(), indices.cend(), destination) - indices.cbegin());
    const int target = destination - before;
    if (indices.last() - indices.first() + 1 == indices.size() && target == indices.first())
        return false;
    if (!preparePageChange())
        return false;
    pushPageHistory();
    QVector<PageData> moved;
    for (int index : std::as_const(indices))
        moved.append(m_pages.at(index));
    for (auto it = indices.crbegin(); it != indices.crend(); ++it)
        m_pages.removeAt(*it);
    for (qsizetype i = 0; i < moved.size(); ++i)
        m_pages.insert(target + i, moved.at(i));
    loadWorkingPage(target, true);
    return true;
}

void CanvasWidget::duplicatePages(QVector<int> indices) {
    indices = validPageIndices(std::move(indices), pageCount());
    if (indices.isEmpty() || !preparePageChange())
        return;
    syncWorkingPage(m_pages, m_currentPageIndex, currentSnapshot(), m_viewport);
    QVector<PageData> copies;
    for (int index : std::as_const(indices))
        copies.append(m_pages.at(index));
    insertPages(indices.last() + 1, copies);
}

bool CanvasWidget::copyPages(QVector<int> indices) {
    indices = validPageIndices(std::move(indices), pageCount());
    if (indices.isEmpty() || !preparePageChange())
        return false;
    syncWorkingPage(m_pages, m_currentPageIndex, currentSnapshot(), m_viewport);
    m_copiedPages.clear();
    for (int index : std::as_const(indices)) {
        m_copiedPages.append(m_pages.at(index));
        if (!m_copiedPages.last().background)
            m_copiedPages.last().background = m_template;
    }
    emit toastRequested(tr("Páginas copiadas · %1").arg(indices.size()));
    return true;
}

void CanvasWidget::pastePages(int index) { insertPages(index, m_copiedPages); }

void CanvasWidget::deletePages(QVector<int> indices) {
    indices = validPageIndices(std::move(indices), pageCount());
    if (indices.isEmpty() || !preparePageChange())
        return;
    pushPageHistory();
    const int current = m_currentPageIndex;
    const auto firstAtOrAfter = std::lower_bound(indices.cbegin(), indices.cend(), current);
    const int removedBefore = int(firstAtOrAfter - indices.cbegin());
    for (auto it = indices.crbegin(); it != indices.crend(); ++it)
        m_pages.removeAt(*it);
    if (m_pages.isEmpty()) {
        PageData blank;
        blank.viewport = fittedSheetViewport(m_pageFormat, size());
        m_pages.append(blank);
    }
    loadWorkingPage(std::min(current - removedBefore, int(m_pages.size()) - 1), true);
}
bool CanvasWidget::currentPageHasContent() const { return !m_strokes.isEmpty()||!m_shapes.isEmpty()||!m_images.isEmpty()||!m_texts.isEmpty(); }
bool CanvasWidget::hasUnsavedChanges() const {
    if(!m_dirty)return false;
    if(currentPageHasContent()||m_pages.size()>1||m_pageFormat!=PageFormat::Infinite)return true;
    return !m_currentFilePath.isEmpty(); // un cuaderno guardado que se vació también es un cambio
}
DocumentSnapshot CanvasWidget::currentSnapshot() const { return {m_strokes,m_shapes,m_images,m_texts}; }
void CanvasWidget::pushUndoSnapshot(const DocumentSnapshot& snapshot) {
    m_undoStack.append({m_currentPageIndex, snapshot, {}});
    if (m_undoStack.size() > 100)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    m_dirty = true;
}

void CanvasWidget::pushPageHistory() {
    syncWorkingPage(m_pages, m_currentPageIndex, currentSnapshot(), m_viewport);
    m_undoStack.append({m_currentPageIndex, {}, m_pages});
    if (m_undoStack.size() > 100)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    m_dirty = true;
}

void CanvasWidget::restoreHistoryEntry(QVector<HistoryEntry>& source, QVector<HistoryEntry>& destination) {
    if (source.isEmpty())
        return;
    auto entry = source.takeLast();
    syncWorkingPage(m_pages, m_currentPageIndex, currentSnapshot(), m_viewport);
    const bool structureChanged = !entry.pages.isEmpty();
    if (structureChanged) {
        destination.append({m_currentPageIndex, {}, m_pages});
        m_pages = std::move(entry.pages);
    } else {
        auto& page = m_pages[entry.pageIndex];
        destination.append({entry.pageIndex, {page.strokes, page.shapes, page.images, page.texts}, {}});
        page.strokes = entry.content.strokes;
        page.shapes = entry.content.shapes;
        page.images = entry.content.images;
        page.texts = entry.content.texts;
    }
    m_dirty = true;
    loadWorkingPage(entry.pageIndex, structureChanged);
}

void CanvasWidget::undo() {
    if (m_mode != InteractionMode::None)
        return;
    finishTextEditor(this, true);
    if (m_polylineActive) {
        cancelActivePolyline();
        return;
    }
    if (m_beautifyTimer) m_beautifyTimer->stop();
    m_pendingBeautifyStart = -1; m_pendingBeautifyBox = QRectF();
    restoreHistoryEntry(m_undoStack, m_redoStack);
}

void CanvasWidget::redo() {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return;
    finishTextEditor(this, true);
    if (m_beautifyTimer) m_beautifyTimer->stop();
    m_pendingBeautifyStart = -1; m_pendingBeautifyBox = QRectF();
    restoreHistoryEntry(m_redoStack, m_undoStack);
}

void CanvasWidget::clearBoard() {
    if (m_mode != InteractionMode::None)
        return;
    finishTextEditor(this, true);
    if (m_polylineActive)
        cancelActivePolyline();
    if (m_strokes.isEmpty() && m_shapes.isEmpty() && m_texts.isEmpty()
        && std::none_of(m_images.cbegin(), m_images.cend(), [](const auto& image) { return !image.isPdfPage; }))
        return;
    flushPendingBeautify();
    pushUndoSnapshot(currentSnapshot());
    m_strokes.clear();
    m_shapes.clear();
    m_images.removeIf([](const auto& image) { return !image.isPdfPage; });
    m_texts.clear();
    clearSelection();
    saveAutosave();
    emit stateChanged();
}
void CanvasWidget::newDocument() {
    m_zoomAnimation->stop();stopShapePulse();clearPenCursor();
    finishTextEditor(this,false);m_holdTimer->stop();m_laserTimer->stop();m_laserTrail.clear();
    m_beautifyTimer->stop();m_pendingBeautifyStart=-1;
    m_mode=InteractionMode::None;m_tabletActive=false;m_hasActiveStroke=false;m_hasActiveShape=false;m_holdSnappedToShape=false;
    cancelActivePolyline();m_strokes.clear();m_shapes.clear();m_images.clear();m_texts.clear();m_currentFilePath.clear();
    m_pages={PageData{}};m_currentPageIndex=0;m_dirty=false;
    m_undoStack.clear();m_redoStack.clear();m_showEraserCursor=false;clearSelection();m_viewport=fittedSheetViewport(m_pageFormat,size());emit viewportChanged(m_viewport.zoom);updateCursorShape();saveAutosave();emit pageStructureChanged();emit stateChanged();
}
bool CanvasWidget::saveToFile(const QString& path, bool newFile) {
    if (!preparePageChange() || path.isEmpty())
        return false;
    syncWorkingPage(m_pages, m_currentPageIndex, currentSnapshot(), m_viewport);
    const auto settings = documentSettings(m_shapeKind, m_holdToShapeEnabled, m_smartGuidesEnabled,
        m_swapPenButtons, m_pressureProfile, m_themeId, m_template, m_viewport, m_pageFormat);
    bool saved;
    if (newFile) {
        QTemporaryFile pending(QFileInfo(path).absolutePath() + QStringLiteral("/.finily-XXXXXX"));
        saved = pending.open();
        if (saved) {
            pending.close();
            saved = serializeDocumentToFile(pending.fileName(), m_pages, m_currentPageIndex, settings)
                && QFile::rename(pending.fileName(), path);
        }
    } else {
        saved = serializeDocumentToFile(path, m_pages, m_currentPageIndex, settings);
    }
    if (!saved) {
        emit toastRequested(tr("No se pudo guardar el cuaderno"));
        return false;
    }
    m_currentFilePath = path;
    m_dirty = false;
    saveAutosave();
    emit stateChanged();
    emit toastRequested(tr("Guardado ✓"));
    return true;
}
void CanvasWidget::saveAutosave() const { m_autosaveTimer->start(); }
void CanvasWidget::flushAutosave() { if(m_autosaveTimer->isActive()){m_autosaveTimer->stop();writeAutosaveNow();} }
void CanvasWidget::writeAutosaveNow() const {
    DocumentSnapshot document=currentSnapshot();
    if(m_mode==InteractionMode::Erase)document=m_snapshotBeforeErase;
    if(m_mode==InteractionMode::ShapeDragHandle||m_mode==InteractionMode::ShapeMove||m_mode==InteractionMode::ShapeScale)document=m_snapshotBeforeShapeEdit;
    if(m_mode==InteractionMode::LassoMove||m_mode==InteractionMode::LassoScale||m_mode==InteractionMode::LassoRotate)document=m_snapshotBeforeLassoTransform;
    if(m_mode==InteractionMode::ImageMove||m_mode==InteractionMode::ImageResize)document=m_snapshotBeforeImageEdit;
    if(m_mode==InteractionMode::TextMove)document=m_snapshotBeforeTextEdit;
    const auto path=autosavePath();if(!QDir().mkpath(QFileInfo(path).absolutePath()))return;
    auto* self=const_cast<CanvasWidget*>(this);
    syncWorkingPage(self->m_pages,self->m_currentPageIndex,document,m_viewport);
    auto settings=documentSettings(m_shapeKind,m_holdToShapeEnabled,m_smartGuidesEnabled,m_swapPenButtons,m_pressureProfile,m_themeId,m_template,m_viewport,m_pageFormat);
    // Autoguardado real: si el cuaderno ya tiene archivo .finily, también lo actualiza
    // atómicamente en disco (QSaveFile) para jamás perder trabajo!
    if(!m_currentFilePath.isEmpty()&&m_dirty) {
        if(serializeDocumentToFile(m_currentFilePath,m_pages,m_currentPageIndex,settings))
            self->m_dirty=false;
    }
    // Datos de sesión: al reabrir la app se recupera el cuaderno asociado y si
    // tenía cambios sin guardar (los .finily del usuario nunca los llevan).
    settings.insert("sessionFilePath",m_currentFilePath);settings.insert("sessionDirty",self->m_dirty);
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
    m_beautifyTimer->stop();m_pendingBeautifyStart=-1;
    QVector<PageData> pages;QHash<QString,QVector<ImageItem>> pdfSources;
    if(root.value("version").toInt()==5)for(const auto& value:root.value("pages").toArray())pages.append(deserializePage(value.toObject(),pdfSources));
    else pages.append(deserializePage(root,pdfSources));
    if(pages.isEmpty())pages.append(PageData{});
    m_liveInk.reset();m_liveInkLayer=QImage();m_radialOpen=false;
    m_pages=std::move(pages);m_currentPageIndex=std::clamp(root.value("currentPageIndex").toInt(),0,int(m_pages.size())-1);
    const auto page=m_pages[m_currentPageIndex];
    m_strokes=page.strokes;m_shapes=page.shapes;m_images=page.images;m_texts=page.texts;m_viewport=page.viewport;
    m_selectedTextId.clear();m_laserTrail.clear();m_laserTimer->stop();m_hasActiveStroke=false;
    m_undoStack.clear();m_redoStack.clear();
    m_selectedStrokeIds.clear();m_selectedShapeIds.clear();m_selectedTextIds.clear();m_selectedImageIds.clear();m_selectedImageId.clear();m_lassoBounds={};m_lassoPolygon.clear();m_linkedPreview=false;
    m_polylineActive=false;m_lastPolylineTapMs=-1;m_polylineVertices.clear();m_hasActiveShape=false;m_selectedShapeId.clear();m_activeGuides.clear();
    const int shapeKind=root.value("shapeKind").toInt(int(ShapeKind::Triangle));
    if(shapeKind>=0&&shapeKind<=int(ShapeKind::AngleMarker)&&shapeKind!=int(ShapeKind::ShadedRegion))m_shapeKind=ShapeKind(shapeKind);
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
    invalidateStaticCache();saveAutosave();emit pageStructureChanged();emit stateChanged();emit viewportChanged(m_viewport.zoom);return true;
}
void CanvasWidget::invalidateStaticCache() { m_staticCacheDirty=true;update(); }
void CanvasWidget::rebuildStaticCache() {
    const double dpr=devicePixelRatioF();const QSize pixels(qCeil(width()*dpr),qCeil(height()*dpr));
    if(pixels.isEmpty())return;
    if(m_staticCache.size()!=pixels||m_staticCache.devicePixelRatio()!=dpr) {
        m_staticCache=QImage(pixels,QImage::Format_ARGB32_Premultiplied);m_staticCache.setDevicePixelRatio(dpr);
    }
    const auto theme=getThemeConfig(m_themeId);
    const bool hasPdfPage=std::any_of(m_images.cbegin(),m_images.cend(),[](const ImageItem& img){return img.isPdfPage&&!img.rect.isEmpty();});
    const auto sheet=hasPdfPage?QRectF():sheetRectForPageFormat(m_pageFormat);
    // Sin bordes negros: en Hoja/Diapo el exterior usa el mismo color del tema (sin cuadrícula);
    // en PDFs usa un tono armónico suave.
    m_staticCache.fill(hasPdfPage?(theme.isDark?theme.background.darker(118):QColor("#eef0f4")):theme.background);
    QPainter p(&m_staticCache);p.translate(m_viewport.x,m_viewport.y);p.scale(m_viewport.zoom,m_viewport.zoom);
    const QRectF visible(screenToWorld(QPointF(0,0)),screenToWorld(QPointF(width(),height())));
    if(hasPdfPage) {
        // Modo presentación / PDF: fondo de escritorio limpio SIN cuadrícula de pizarra detrás.
    } else if(sheet.isEmpty())drawBackgroundTemplate(p,backgroundTemplate(),theme,m_viewport,visible);
    else {
        p.save();
        p.fillRect(sheet,theme.background);
        p.save();p.setClipRect(sheet,Qt::IntersectClip);
        drawBackgroundTemplate(p,backgroundTemplate(),theme,m_viewport,sheet.intersected(visible));p.restore();
        p.setBrush(Qt::NoBrush);
        QPen border(theme.isDark?QColor(255,255,255,22):QColor(15,23,42,24));
        border.setStyle(Qt::DashLine);
        border.setWidthF(1.0/m_viewport.zoom);p.setPen(border);p.drawRect(sheet);p.restore();
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
    auto drawShapes=[&](QPainter& target,bool shading) {
        for(const auto& shape:m_shapes)
            if((shape.kind==ShapeKind::ShadedRegion)==shading&&shape.id!=m_selectedShapeId
               &&!m_selectedShapeIds.contains(shape.id)&&shape.bounds.intersects(visible))drawShape(target,shape);
    };
    const bool selectedHighlight=std::any_of(m_strokes.cbegin(),m_strokes.cend(),[&](const Stroke& stroke){return m_selectedStrokeIds.contains(stroke.id)&&stroke.tool==ToolType::Highlighter;});
    const bool selectedShade=std::any_of(m_shapes.cbegin(),m_shapes.cend(),[&](const ShapeItem& shape){return (m_selectedShapeIds.contains(shape.id)||shape.id==m_selectedShapeId)&&shape.kind==ShapeKind::ShadedRegion;});
    const bool imageUnderlay=!m_selectedImageId.isEmpty();
    if(!imageUnderlay)drawShapes(p,true);
    if(!imageUnderlay&&!selectedShade)drawPass(p,true);
    if((m_hasActiveStroke&&m_activeStroke.tool==ToolType::Highlighter)||selectedHighlight||selectedShade||imageUnderlay) {
        auto& layers=highlighterLayers(this);
        layers.background=m_staticCache.copy();
        layers.ink=QImage(pixels,QImage::Format_ARGB32_Premultiplied);
        layers.ink.setDevicePixelRatio(dpr);layers.ink.fill(Qt::transparent);
        {
            QPainter inkPainter(&layers.ink);
            inkPainter.translate(m_viewport.x,m_viewport.y);inkPainter.scale(m_viewport.zoom,m_viewport.zoom);
            if(imageUnderlay)drawShapes(inkPainter,true);
            if(imageUnderlay||selectedShade)drawPass(inkPainter,true);
            drawShapes(inkPainter,false);drawPass(inkPainter,false);
            for(const auto& item:m_texts)if(item.id!=m_selectedTextId)drawTextItem(inkPainter,item);
        }
        p.save();p.resetTransform();p.drawImage(QPointF(0,0),layers.ink);p.restore();
    } else {
        drawShapes(p,false);drawPass(p,false);
        for(const auto& item:m_texts)if(item.id!=m_selectedTextId)drawTextItem(p,item);
        // Release temporary full-size buffers as soon as the highlight commits.
        auto* child=findChild<QObject*>(QStringLiteral("_finily_highlighter_layers"),Qt::FindDirectChildrenOnly);
        if(child) { auto* layers=static_cast<HighlighterLayers*>(child);layers->background=QImage();layers->ink=QImage(); }
    }
    m_staticCacheDirty=false;
}
void CanvasWidget::paintEvent(QPaintEvent*) {
    const double dpr = devicePixelRatioF();
    const QSize pixels(qCeil(width() * dpr), qCeil(height() * dpr));
    if (m_staticCacheDirty || m_staticCache.size() != pixels || m_staticCache.devicePixelRatio() != dpr)
        rebuildStaticCache();
    const bool liveHighlight=(m_hasActiveStroke&&m_activeStroke.tool==ToolType::Highlighter)||!m_selectedImageId.isEmpty()
        ||std::any_of(m_strokes.cbegin(),m_strokes.cend(),[&](const Stroke& stroke){return m_selectedStrokeIds.contains(stroke.id)&&stroke.tool==ToolType::Highlighter;})
        ||std::any_of(m_shapes.cbegin(),m_shapes.cend(),[&](const ShapeItem& shape){return (m_selectedShapeIds.contains(shape.id)||shape.id==m_selectedShapeId)&&shape.kind==ShapeKind::ShadedRegion;});
    QPainter p(this);
    p.drawImage(QPointF(0,0),liveHighlight?highlighterLayers(this).background:m_staticCache);
    p.translate(m_viewport.x,m_viewport.y);p.scale(m_viewport.zoom,m_viewport.zoom);
    for(const auto& image:m_images)if(image.id==m_selectedImageId){p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(image.rect,image.image);}
    for(const auto& shape:m_shapes)if(shape.kind==ShapeKind::ShadedRegion&&(m_selectedShapeIds.contains(shape.id)||shape.id==m_selectedShapeId))drawShape(p,shape);
    for(const auto& stroke:m_strokes)if(m_selectedStrokeIds.contains(stroke.id)&&stroke.tool==ToolType::Highlighter)drawStroke(p,stroke);
    if(m_hasActiveStroke) {
        if (m_liveInkPending) {
            m_liveInk.advance(m_activeStroke, m_liveInkLayer, m_viewport, pixels, dpr);
            m_liveInkPending = false;
        }
        const Stroke live=withPredictedTip(m_activeStroke,m_viewport.zoom);
        if (LiveInkRenderer::eligible(live) && m_liveInk.usable(m_viewport, pixels)
            && m_liveInkLayer.devicePixelRatio() == dpr) {
            p.save();p.resetTransform();p.drawImage(QPointF(0,0),m_liveInkLayer);p.restore();
            m_liveInk.drawTail(p,live);
        } else drawStroke(p,live);
    }
    if(liveHighlight) {
        p.save();p.resetTransform();p.drawImage(QPointF(0,0),highlighterLayers(this).ink);p.restore();
    }
    for(const auto& shape:m_shapes)if(shape.kind!=ShapeKind::ShadedRegion&&m_selectedShapeIds.contains(shape.id))drawShape(p,shape);
    for(const auto& stroke:m_strokes)if(m_selectedStrokeIds.contains(stroke.id)&&stroke.tool!=ToolType::Highlighter)drawStroke(p,stroke);
    if(m_mode==InteractionMode::LassoDraw)drawLassoPath(p,m_lassoPolygon,getThemeConfig(m_themeId),m_viewport.zoom);
    if(hasLassoSelection())drawLassoSelectionBox(p,m_lassoBounds,getThemeConfig(m_themeId),m_viewport.zoom,m_selectedTextIds.isEmpty()&&m_selectedImageIds.isEmpty());
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
        if(selected->kind!=ShapeKind::ShadedRegion&&(!m_hasActiveShape||selected->id!=m_activeShape.id))drawShape(p,*selected);
        drawSelectionOverlay(p,*selected,getThemeConfig(m_themeId),m_viewport.zoom,m_tool==ToolType::Select);
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
    // Láser continuo de seda (UN SOLO QPainterPath suavizado con quadTo por trazo,
    // CERO bolitas/puntos superpuestos en los vértices!).
    {
        qsizetype segStart=0;
        while(segStart<m_laserTrail.size()) {
            qsizetype segEnd=segStart+1;
            while(segEnd<m_laserTrail.size()&&!m_laserTrail[segEnd].breakBefore)++segEnd;
            // Recolectar puntos aún vivos de este trazo continuo
            QVector<QPointF> livePts;
            double avgFade=0.0;
            for(qsizetype i=segStart;i<segEnd;++i) {
                const double f=std::clamp(1.0-double(laserNow-m_laserTrail[i].timestampMs)/1200.0,0.0,1.0);
                if(f>0.02){livePts.append(m_laserTrail[i].pos);avgFade=std::max(avgFade,f);}
            }
            if(livePts.size()>=2&&avgFade>0.02) {
                QPainterPath smoothPath;
                smoothPath.moveTo(livePts[0]);
                for(qsizetype i=1;i+1<livePts.size();++i) {
                    const QPointF mid=(livePts[i]+livePts[i+1])*0.5;
                    smoothPath.quadTo(livePts[i],mid);
                }
                smoothPath.lineTo(livePts.back());
                const double ease=avgFade*avgFade;
                QColor glowCol("#ff3b30"); glowCol.setAlphaF(ease*0.22);
                QColor midCol("#ff453a");  midCol.setAlphaF(ease*0.65);
                QColor coreCol("#fff8f6"); coreCol.setAlphaF(ease*0.98);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(glowCol,14.0/m_viewport.zoom,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                p.strokePath(smoothPath,p.pen());
                p.setPen(QPen(midCol,6.2/m_viewport.zoom,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                p.strokePath(smoothPath,p.pen());
                p.setPen(QPen(coreCol,2.2/m_viewport.zoom,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                p.strokePath(smoothPath,p.pen());
            }
            segStart=segEnd;
        }
    }
    if(!m_laserTrail.isEmpty()) {
        const auto& tip=m_laserTrail.back();const double fade=std::clamp(1.0-double(laserNow-tip.timestampMs)/1200.0,0.0,1.0);
        if(fade>0.02) {
            p.setPen(Qt::NoPen);
            for(int layer=3;layer>=0;--layer){
                QColor color=layer==0?QColor("#ffffff"):QColor("#ff3b30");
                color.setAlphaF(fade*(layer==0?1.0:layer==1?0.55:layer==2?0.24:0.10));
                const double r=(2.6+layer*3.4)/m_viewport.zoom;
                p.drawEllipse(tip.pos,r,r);
            }
        }
    }
    drawSmartGuides(p,m_activeGuides,getThemeConfig(m_themeId),m_viewport.zoom);
    if(m_showPenCursor) {
        p.save();p.resetTransform();
        const QPointF c=m_hoverWorldPos*m_viewport.zoom+QPointF(m_viewport.x,m_viewport.y);
        if(m_tool==ToolType::Laser) {
            p.setPen(Qt::NoPen);
            QColor glow("#ff3b30"); glow.setAlpha(75);
            p.setBrush(glow); p.drawEllipse(c,10.0,10.0);
            p.setBrush(QColor("#ff3b30")); p.drawEllipse(c,4.5,4.5);
            p.setBrush(QColor("#ffffff")); p.drawEllipse(c,1.9,1.9);
        } else if(m_cursorStyle==0) {
            // Plantilla 0: Lapicito estilizado cuya punta exacta está en c!
            const bool darkBg=getThemeConfig(m_themeId).isDark;
            const QColor outline=darkBg?QColor("#0f172a"):QColor("#1e293b");
            const QColor bodyCol=darkBg?QColor("#f8fafc"):QColor("#ffffff");
            QColor ink=inkColor();
            if(m_tool==ToolType::Highlighter)ink.setAlpha(200);
            // Pequeño punto de contacto en la punta exacta
            p.setPen(Qt::NoPen);p.setBrush(ink);p.drawEllipse(c,1.8,1.8);
            // Cuerpo del lapicito apuntando a c desde arriba-derecha (45°)
            p.save();
            p.translate(c);
            p.rotate(-45.0);
            // Cono de la punta (x: 0..6)
            QPainterPath tip;
            tip.moveTo(0.0,0.0); tip.lineTo(6.5,-2.8); tip.lineTo(6.5,2.8); tip.closeSubpath();
            p.setPen(QPen(outline,1.0,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            p.setBrush(QColor("#e2e8f0"));
            p.drawPath(tip);
            // Punta coloreada con la tinta activa
            QPainterPath nib;
            nib.moveTo(0.0,0.0); nib.lineTo(2.8,-1.2); nib.lineTo(2.8,1.2); nib.closeSubpath();
            p.setPen(Qt::NoPen);p.setBrush(ink);p.drawPath(nib);
            // Banda de color de tinta (x: 6.5..10.5)
            p.setPen(QPen(outline,1.0));p.setBrush(ink);
            p.drawRect(QRectF(6.5,-2.8,4.0,5.6));
            // Barril limpio (x: 10.5..21.5)
            p.setBrush(bodyCol);
            p.drawRoundedRect(QRectF(10.5,-2.8,11.0,5.6),1.6,1.6);
            p.restore();
        } else if(m_cursorStyle==1) {
            // Plantilla 1: Aro de precisión + punto de tinta real
            const double rawR=strokeSize()*m_viewport.zoom/2.0;
            const double ringR=std::max(5.0,rawR+1.5);
            const bool darkBg=getThemeConfig(m_themeId).isDark;
            const QColor outer=darkBg?QColor(255,255,255,215):QColor(15,23,42,195);
            const QColor halo=darkBg?QColor(0,0,0,155):QColor(255,255,255,210);
            p.setPen(QPen(halo,2.4));p.setBrush(Qt::NoBrush);p.drawEllipse(c,ringR,ringR);
            p.setPen(QPen(outer,1.3));p.drawEllipse(c,ringR,ringR);
            p.setPen(Qt::NoPen);p.setBrush(inkColor());p.drawEllipse(c,std::max(2.0,rawR),std::max(2.0,rawR));
        } else {
            // Plantilla 2: Cruz fina técnica + punto central de tinta
            const bool darkBg=getThemeConfig(m_themeId).isDark;
            const QColor lineCol=darkBg?QColor(255,255,255,210):QColor(15,23,42,195);
            p.setPen(QPen(lineCol,1.3,Qt::SolidLine,Qt::RoundCap));
            p.drawLine(c+QPointF(-8,0),c+QPointF(-3,0));
            p.drawLine(c+QPointF(3,0),c+QPointF(8,0));
            p.drawLine(c+QPointF(0,-8),c+QPointF(0,-3));
            p.drawLine(c+QPointF(0,3),c+QPointF(0,8));
            p.setPen(Qt::NoPen);p.setBrush(inkColor());p.drawEllipse(c,2.0,2.0);
        }
        p.restore();
    }
    if(m_showEraserCursor) {
        p.setRenderHint(QPainter::Antialiasing);QPen pen(getThemeConfig(m_themeId).accent);pen.setCosmetic(true);pen.setWidthF(1.3);
        p.setPen(pen);p.setBrush(Qt::NoBrush);p.drawEllipse(m_eraserWorldPos,m_eraserSize/2,m_eraserSize/2);
    }
    if(m_radialOpen) {
        p.save();
        p.resetTransform();
        p.setRenderHint(QPainter::Antialiasing,true);
        const ThemeConfig th=getThemeConfig(m_themeId);
        const QPointF c=m_radialCenter;
        // Anillo limpio de cristal con 8 colores (sin texto cortado)
        const QColor bg=th.isDark?QColor(18,28,24,226):QColor(255,255,255,236);
        const QColor ringCol=th.isDark?QColor(255,255,255,34):QColor(15,23,42,26);
        // Sombra suave exterior
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0,0,0,th.isDark?75:32));
        p.drawEllipse(c+QPointF(0,4),78.0,78.0);
        // Anillo principal (dona limpia)
        QPainterPath ring;
        ring.setFillRule(Qt::OddEvenFill);
        ring.addEllipse(c,76.0,76.0);
        ring.addEllipse(c,24.0,24.0);
        p.setPen(QPen(ringCol,1.2));
        p.setBrush(bg);
        p.drawPath(ring);

        const QVector<QColor> rColors = th.isDark
            ? QVector<QColor>{QColor("#f8fafc"),QColor("#fde047"),QColor("#fb923c"),QColor("#f87171"),
                              QColor("#f472b6"),QColor("#c084fc"),QColor("#38bdf8"),QColor("#4ade80")}
            : QVector<QColor>{QColor("#0f172a"),QColor("#ca8a04"),QColor("#ea580c"),QColor("#dc2626"),
                              QColor("#db2777"),QColor("#9333ea"),QColor("#0284c7"),QColor("#16a34a")};
        constexpr double kPi=3.14159265358979323846;
        for(int i=0;i<8;++i) {
            const double ang=-kPi/2.0+i*(kPi/4.0);
            const QPointF pt=c+QPointF(std::cos(ang)*50.0,std::sin(ang)*50.0);
            const bool hov=(m_radialHoveredColor==i);
            if(hov) {
                p.setPen(QPen(th.accent,2.2));
                p.setBrush(QColor(th.accent.red(),th.accent.green(),th.accent.blue(),55));
                p.drawEllipse(pt,16.5,16.5);
            }
            p.setPen(QPen(th.isDark?QColor(255,255,255,150):QColor(15,23,42,110),hov?1.8:1.0));
            p.setBrush(rColors[i]);
            p.drawEllipse(pt,hov?12.0:9.5,hov?12.0:9.5);
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
        p.setPen(QPen(ringCol,1.0));
        p.setBrush(m_radialHoveredColor>=0?rColors[m_radialHoveredColor]:m_color);
        p.drawEllipse(c,8.0,8.0);
        p.restore();
    }
}
void CanvasWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const auto pdf = std::find_if(m_images.cbegin(), m_images.cend(),
                                  [](const auto& item) { return item.isPdfPage && !item.rect.isEmpty(); });
    if (pdf != m_images.cend()) {
        const auto previous = fittedPdfViewport(pdf->rect, event->oldSize());
        const bool wasFitted = std::abs(previous.x - m_viewport.x) < .01
            && std::abs(previous.y - m_viewport.y) < .01 && std::abs(previous.zoom - m_viewport.zoom) < .0001;
        if (wasFitted || event->oldSize().isEmpty()) {
            m_viewport = fittedPdfViewport(pdf->rect, size());
            emit viewportChanged(m_viewport.zoom);
        }
    } else if (m_pageFormat == PageFormat::Slide16x9 && !size().isEmpty()) {
        m_viewport = fittedSheetViewport(m_pageFormat, size());
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
        m_eraserWorldPos=world;m_showEraserCursor=true;applyEraseAt(world);updateCursorShape();return;
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
        const auto existing = std::find_if(m_shapes.cbegin(), m_shapes.cend(), [&region](const ShapeItem& shape) {
            return shape.kind == ShapeKind::ShadedRegion && shape.vertices == region->vertices
                && shape.shadeContourStarts == region->shadeContourStarts;
        });
        if (existing == m_shapes.cend()) {
            pushUndoSnapshot(currentSnapshot());
            region->id = freshId();
            m_shapes.append(*region);
        } else if (existing->color != region->color || existing->shadePattern != m_shadePattern) {
            const auto index = existing - m_shapes.cbegin();
            pushUndoSnapshot(currentSnapshot());
            m_shapes[index].color = region->color;
            m_shapes[index].shadePattern = m_shadePattern;
        } else return;
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
        if (hasLassoSelection()&&m_selectedTextIds.isEmpty()&&m_selectedImageIds.isEmpty()) {
            const int handle = hitTestLassoHandle(m_lassoBounds, world, m_viewport.zoom);
            if (handle >= 0) { startSelectionTransform(handle); return; }
        }
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
            const int handle=m_selectedTextIds.isEmpty()&&m_selectedImageIds.isEmpty()
                ? hitTestLassoHandle(m_lassoBounds,world,m_viewport.zoom) : -1;
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
    clearSelection();m_mode=InteractionMode::Draw;m_holdSnappedToShape=false;m_stabilizerFilter->reset();m_stabilizerFilter->setStrength(0.0);m_liveInk.reset();m_liveInkLayer=QImage();m_inkSmoother.reset(m_stabilizer>0?(1.5+7.0*m_stabilizer)/m_viewport.zoom:0.0);m_activeStroke={};
    m_activeStroke.id=freshId();m_activeStroke.tool=m_tool;m_activeStroke.color=m_color;m_activeStroke.size=m_size;
    m_activeStroke.linePattern=LinePattern::Solid;m_activeStroke.pressureProfile=m_pressureProfile;m_activeStroke.hasHardwarePressure=hardware;
    m_activeStroke.tipStyle=(m_tool==ToolType::Highlighter)?m_highlighterTip:m_penTip;
    m_rawStartPoint=inputPoint(world,pressure,tiltX,tiltY);m_activeStroke.points.append(m_stabilizerFilter->filter(m_rawStartPoint));m_inkSmoother.push(m_activeStroke);
    m_liveStrokeBounds = QRectF(m_activeStroke.points.front().x, m_activeStroke.points.front().y, 0, 0);
    m_liveInkPending = false;
    m_hasActiveStroke=true;m_showEraserCursor=false;
    m_holdAnchorWorld=world;m_holdLastWorld=world;
    if(m_holdToShapeEnabled&&(m_tool==ToolType::Pen||m_tool==ToolType::Ballpoint))m_holdTimer->start();
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
        }m_viewport.x=m_panStartViewport.x()+delta.x();m_viewport.y=m_panStartViewport.y()+delta.y();invalidateStaticCache();return;
    }
    if(m_mode==InteractionMode::Erase) {
        const auto previous=m_eraserWorldPos;const int steps=std::max(1,int(std::ceil(distance(world,previous)/std::max(.5,m_eraserSize/4))));
        for(int i=1;i<=steps;++i)applyEraseAt(previous+(world-previous)*(double(i)/steps));
        m_eraserWorldPos=world;update();return;
    }
    if(m_mode==InteractionMode::LassoDraw) {
        if(m_lassoRectMode) {
            const QRectF r(m_lassoRectOrigin, world);
            m_lassoPolygon = QPolygonF{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
        } else if(m_lassoPolygon.isEmpty()||distance(world,m_lassoPolygon.back())*m_viewport.zoom>1) {
            m_lassoPolygon.append(world);
        }
        update();return;
    }
    if(m_mode==InteractionMode::LassoMove||m_mode==InteractionMode::LassoScale||m_mode==InteractionMode::LassoRotate) {
        m_strokes=m_snapshotBeforeLassoTransform.strokes;m_shapes=m_snapshotBeforeLassoTransform.shapes;
        if(m_mode==InteractionMode::LassoMove){
            const auto delta=world-m_lassoDragStartWorld;
            translateSelection(m_strokes,m_shapes,m_selectedStrokeIds,m_selectedShapeIds,delta);
            if(!m_selectedTextIds.isEmpty()||!m_selectedImageIds.isEmpty()) {
                m_texts=m_snapshotBeforeLassoTransform.texts;m_images=m_snapshotBeforeLassoTransform.images;
                for(auto& text:m_texts)if(!text.locked&&m_selectedTextIds.contains(text.id)){text.pos+=delta;text.bounds.translate(delta);}
                for(auto& image:m_images)if(!image.locked&&m_selectedImageIds.contains(image.id))image.rect.translate(delta);
                invalidateStaticCache();
            }
        }
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
                const auto anchor=handleAnchor(*shape,m_activeHandleIndex);
                const auto raw=(m_tool==ToolType::Select)
                    ? (getShapeHandles(*shape).at(m_activeHandleIndex)+delta)
                    : world;
                const auto external=snapPointWithGuides(raw,anchor,m_shapes,shape->id,m_viewport.zoom,m_smartGuidesEnabled);
                const auto result=snapShapeVertex(*shape,m_activeHandleIndex,external,raw,m_viewport.zoom);
                m_activeGuides=result.guides;
                applyHandleDrag(*shape,m_activeHandleIndex,result.point);
            } else {
                const auto origin=m_shapeDragOriginalVertices.front();
                const auto target=snapped(origin+delta,std::nullopt,shape->id);
                translateShape(*shape,target-origin);
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
            applyHandleDrag(m_activeShape,m_activeHandleIndex,snapped(world,handleAnchor(m_activeShape,m_activeHandleIndex),
                            m_activeShape.id));
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
        if (m_holdToShapeEnabled && (m_activeStroke.tool == ToolType::Pen || m_activeStroke.tool == ToolType::Ballpoint)
            && distance(world, m_holdAnchorWorld) * m_viewport.zoom > 4) {
            m_holdAnchorWorld = world;
            m_holdTimer->start();
        }
        auto point = m_stabilizerFilter->filter(inputPoint(world, pressure, tiltX, tiltY));
        if (mods.testFlag(Qt::ShiftModifier)) {
            m_liveInk.reset();
            m_liveInkPending = false;
            point = snappedPoint(m_rawStartPoint, point);
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
void CanvasWidget::triggerHoldToShape() {
    if(!m_holdToShapeEnabled||m_mode!=InteractionMode::Draw||!m_hasActiveStroke||(m_activeStroke.tool!=ToolType::Pen&&m_activeStroke.tool!=ToolType::Ballpoint))return;
    double total=0;for(qsizetype i=1;i<m_activeStroke.points.size();++i)total+=distance({m_activeStroke.points[i-1].x,m_activeStroke.points[i-1].y},{m_activeStroke.points[i].x,m_activeStroke.points[i].y});
    if(total*m_viewport.zoom<25)return;
    const auto recognized=recognizeFreehandShape(m_activeStroke);if(!recognized)return;
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
        else if(distance({last.x,last.y},{end.x,end.y})>.001){m_activeStroke.points.append(end);m_inkSmoother.push(m_activeStroke);}
        if(!mods.testFlag(Qt::ShiftModifier)&&(m_activeStroke.tool==ToolType::Pen||m_activeStroke.tool==ToolType::Ballpoint))
            trimPenHooks(m_activeStroke,m_viewport.zoom);
        if(m_activeStroke.tool==ToolType::Pen&&m_activeStroke.tipStyle==1&&!(mods&Qt::ShiftModifier))
            taperPenFlick(m_activeStroke,m_viewport.zoom);
        if(m_activeStroke.tool==ToolType::Highlighter&&m_activeStroke.tipStyle==2&&m_activeStroke.points.size()>=2)
            m_activeStroke.points={m_activeStroke.points.front(),m_activeStroke.points.back()};
        m_liveInk.reset();m_liveInkLayer=QImage();
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
    auto shapes=m_shapes;
    auto eraseShapes=[&](bool shading) {
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
    event->accept();m_zoomAnimation->stop();if(m_mode!=InteractionMode::None)return;
    const double delta=event->pixelDelta().isNull()?event->angleDelta().y():event->pixelDelta().y()*2.0;
    zoomByFactor(std::exp(delta*0.0015),event->position());
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
void CanvasWidget::updateRadialHover(const QPointF& screenPos) {
    m_radialCursorPos=screenPos;
    const QPointF d=screenPos-m_radialCenter;
    const double r=std::hypot(d.x(),d.y());
    constexpr double kPi=3.14159265358979323846;
    int newCol=-1;
    if(r>=20.0&&r<=110.0) {
        double ang=std::atan2(d.y(),d.x())+kPi/2.0+kPi/8.0;
        while(ang<0) ang+=2.0*kPi;
        while(ang>=2.0*kPi) ang-=2.0*kPi;
        newCol=std::clamp(int(ang/(kPi/4.0)),0,7);
    }
    m_radialHoveredColor=newCol;
    update();
}
void CanvasWidget::commitRadialSelection(bool quickTap) {
    const int colIdx=m_radialHoveredColor;
    m_radialOpen=false;
    m_radialHoveredColor=-1;
    m_radialHoveredTool=-1;
    if(colIdx>=0) {
        const ThemeConfig th=getThemeConfig(m_themeId);
        const QVector<QColor> rColors = th.isDark
            ? QVector<QColor>{QColor("#f8fafc"),QColor("#fde047"),QColor("#fb923c"),QColor("#f87171"),
                              QColor("#f472b6"),QColor("#c084fc"),QColor("#38bdf8"),QColor("#4ade80")}
            : QVector<QColor>{QColor("#0f172a"),QColor("#ca8a04"),QColor("#ea580c"),QColor("#dc2626"),
                              QColor("#db2777"),QColor("#9333ea"),QColor("#0284c7"),QColor("#16a34a")};
        // Primero volvemos al lápiz (sin recolorear una figura seleccionada por accidente) y luego cambiamos color!
        if(m_tool!=ToolType::Pen&&m_tool!=ToolType::Ballpoint&&m_tool!=ToolType::Highlighter)
            setTool(m_lastWriteTool);
        setInkColor(rColors[colIdx]);
    } else if(quickTap) {
        setTool(m_lastWriteTool);
    }
    updateCursorShape();
    update();
}
void CanvasWidget::clearPenCursor() {
    if(!m_showPenCursor)return;
    m_showPenCursor=false;update(m_penCursorRect);m_penCursorRect={};
}
void CanvasWidget::updatePenCursor(const QPointF& world,bool visible) {
    if(!visible){clearPenCursor();return;}
    const QPointF center=world*m_viewport.zoom+QPointF(m_viewport.x,m_viewport.y);
    const double radius=std::max(26.0,strokeSize()*m_viewport.zoom/2.0+6.0);
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

// Miniatura simplificada de una página (fondo del tema + trazos y figuras como polilíneas).
QImage CanvasWidget::pageThumbnail(int index, const QSize& size) const {
    QImage image(size * 2, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0);
    const auto theme = getThemeConfig(m_themeId);
    image.fill(theme.background);
    if (index < 0 || index >= m_pages.size())
        return image;

    const auto& page = m_pages.at(index);
    const bool current = index == m_currentPageIndex;
    const auto& strokes = current ? m_strokes : page.strokes;
    const auto& shapes = current ? m_shapes : page.shapes;
    const auto& images = current ? m_images : page.images;
    const auto& texts = current ? m_texts : page.texts;
    QRectF bounds = sheetRectForPageFormat(m_pageFormat);
    for (const auto& item : images) {
        if (item.isPdfPage) {
            bounds = item.rect;
            break;
        }
    }
    for (const auto& item : images)
        bounds = bounds.united(item.rect);
    for (const auto& item : strokes)
        bounds = bounds.united(item.bounds);
    for (const auto& item : shapes)
        bounds = bounds.united(item.bounds);
    for (const auto& item : texts)
        bounds = bounds.united(item.bounds);
    if (bounds.isEmpty())
        bounds = QRectF(0, 0, 1000, 700);
    const double zoom = std::min((size.width() - 8.0) / bounds.width(),
                                 (size.height() - 8.0) / bounds.height());
    if (zoom <= 0)
        return image;

    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.translate(size.width() / 2.0, size.height() / 2.0);
    painter.scale(zoom, zoom);
    painter.translate(-bounds.center());
    drawBackgroundTemplate(painter, page.background.value_or(m_template), theme, Viewport{0, 0, zoom}, bounds);
    for (bool pdf : {true, false})
        for (const auto& item : images)
            if (item.isPdfPage == pdf)
                painter.drawImage(item.rect, item.image);
    for (const auto& shape : shapes)
        if (shape.kind == ShapeKind::ShadedRegion)
            drawShape(painter, shape);
    for (const auto& stroke : strokes)
        if (stroke.tool == ToolType::Highlighter)
            drawStroke(painter, stroke);
    for (const auto& shape : shapes)
        if (shape.kind != ShapeKind::ShadedRegion)
            drawShape(painter, shape);
    for (const auto& stroke : strokes)
        if (stroke.tool != ToolType::Highlighter)
            drawStroke(painter, stroke);
    for (const auto& item : texts)
        drawTextItem(painter, item);
    return image;
}

int CanvasWidget::documentPdfPageCount() const {
    int total = 0;
    for (int i = 0; i < m_pages.size(); ++i) {
        const auto& images = i == m_currentPageIndex ? m_images : m_pages.at(i).images;
        total += int(std::count_if(images.cbegin(), images.cend(), [](const auto& item) { return item.isPdfPage; }));
    }
    return total;
}
} // namespace finily
