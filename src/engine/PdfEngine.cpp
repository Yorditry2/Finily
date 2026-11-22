#include "PdfEngine.h"
#include "GeometryEngine.h"
#include "InkEngine.h"
#include <QFileInfo>
#include <QFontMetricsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace finily {
QVector<ImageItem> importPdfPages(const QString& path,double startY) {
    QVector<ImageItem> pages;if(!std::isfinite(startY))return pages;
    QPdfDocument document;if(document.load(path)!=QPdfDocument::Error::None)return pages;
    double y=startY;
    for(int i=0;i<document.pageCount();++i) {
        const QSizeF points=document.pagePointSize(i);
        if(points.width()<=0||points.height()<=0||!std::isfinite(points.width())||!std::isfinite(points.height()))continue;
        const double worldScale=1000.0/points.width();const QSizeF world=points*worldScale;
        // Bound both a single raster and the aggregate raster storage of long PDFs.
        const double pixelBudget=256.0*1024*1024/(4.0*std::max(1,document.pageCount()));
        const double rasterScale=std::min({2*worldScale,2200.0/std::max(points.width(),points.height()),std::sqrt(pixelBudget/(points.width()*points.height()))});
        const QSize raster(std::max(1,int(std::ceil(points.width()*rasterScale))),std::max(1,int(std::ceil(points.height()*rasterScale))));
        QImage image=document.render(i,raster);if(image.isNull())continue;
        ImageItem page;page.id=QUuid::createUuid().toString(QUuid::WithoutBraces);page.rect=QRectF(-world.width()/2,y,world.width(),world.height());
        page.image=image;page.isPdfPage=true;page.pdfPageIndex=i;page.pdfSourcePath=QFileInfo(path).absoluteFilePath();pages.append(page);y+=world.height()+40;
    }
    return pages;
}
namespace {
QFont fontFor(double size) {QFont font(QStringLiteral("Sans Serif"));font.setPixelSize(qRound(size));return font;}
QRectF textBounds(const TextItem& item) {
    const QFontMetricsF metrics(fontFor(item.fontSize));
    return metrics.boundingRect(item.text).translated(item.pos+QPointF(0,metrics.ascent())).united(QRectF(item.pos,QSizeF(metrics.horizontalAdvance(item.text),metrics.height())));
}
QRectF strokeBounds(const Stroke& s) {
        auto controls=s.points;
        // Catmull-Rom segments live inside their cubic control hull, including
        // overshoot outside the input-point rectangle on sharp handwriting turns.
        for(qsizetype i=0;i+1<s.points.size();++i) {
            const auto& a=s.points[std::max<qsizetype>(0,i-1)];const auto& b=s.points[i];
            const auto& c=s.points[i+1];const auto& d=s.points[std::min(i+2,s.points.size()-1)];
            controls.append({b.x+(c.x-a.x)/6,b.y+(c.y-a.y)/6});
            controls.append({c.x-(d.x-b.x)/6,c.y-(d.y-b.y)/6});
        }
        QRectF bounds=computeBounds(controls,s.size*.7);
        if(s.linePattern==LinePattern::Arrow||s.linePattern==LinePattern::DoubleArrow) {
            ShapeItem arrows;arrows.kind=ShapeKind::Polyline;arrows.size=s.size;arrows.linePattern=s.linePattern;
            for(const auto& point:s.points)arrows.vertices.append({point.x,point.y});
            bounds=bounds.united(computeShapeBounds(arrows));
        }
        return bounds;
}
QVector<QRectF> pdfPageBounds(const PageData& document,PageFormat pageFormat) {
    const auto& strokes=document.strokes;const auto& shapes=document.shapes;const auto& images=document.images;const auto& texts=document.texts;
    QVector<QRectF> pages;QVector<const ImageItem*> pdfPages;
    for(const auto& image:images)if(image.isPdfPage&&!image.rect.isEmpty())pdfPages.append(&image);
    if(pdfPages.isEmpty()) {
        QRectF bounds;bool first=true;auto add=[&](QRectF r){if(r.isEmpty())return;if(first){bounds=r;first=false;}else bounds=bounds.united(r);};
        for(const auto& s:strokes)if(!s.points.isEmpty())add(strokeBounds(s));
        for(const auto& s:shapes)add(computeShapeBounds(s));
        for(const auto& image:images)add(image.rect);
        for(const auto& item:texts)add(textBounds(item));
        const auto sheet=sheetRectForPageFormat(pageFormat);
        if(!sheet.isEmpty()) {
            QRectF extent=sheet;
            if(!first&&!sheet.contains(bounds))extent=extent.united(bounds.adjusted(-20,-20,20,20));
            pages.append(extent);
        } else {
            if(first)bounds=QRectF(0,0,1000,700);
            pages.append(bounds.adjusted(-40,-40,40,40));
        }
    } else {
        for(auto* page:pdfPages)pages.append(page->rect);
        // Each annotation belongs to its closest slide. Include marginal notes
        // without exploding every slide to the complete infinite-board extent.
        auto assign=[&](QRectF bounds) {
            if(bounds.isEmpty())return;
            qsizetype best=0;double distance=std::numeric_limits<double>::max();
            for(qsizetype i=0;i<pdfPages.size();++i){const auto rect=pdfPages[i]->rect;const auto p=bounds.center();const double dx=std::max({rect.left()-p.x(),0.0,p.x()-rect.right()}),dy=std::max({rect.top()-p.y(),0.0,p.y()-rect.bottom()});const double d=dx*dx+dy*dy;if(d<distance){distance=d;best=i;}}
            pages[best]=pages[best].united(bounds);
        };
        for(const auto& s:strokes)if(!s.points.isEmpty())assign(strokeBounds(s));
        for(const auto& s:shapes)assign(computeShapeBounds(s));
        for(const auto& image:images)if(!image.isPdfPage)assign(image.rect);
        for(const auto& item:texts)assign(textBounds(item));
        for(auto& rect:pages)rect.adjust(-20,-20,20,20);
    }
    return pages;
}
} // namespace
bool exportDocumentToPdf(const QString& path,const QVector<Stroke>& strokes,const QVector<ShapeItem>& shapes,const QVector<ImageItem>& images,const ThemeConfig& theme,BackgroundTemplate tpl,const QVector<TextItem>& texts) {
    return exportPagesToPdf(path,{PageData{strokes,shapes,images,texts,{},{},{}}},theme,tpl);
}
bool exportPagesToPdf(const QString& path,const QVector<PageData>& notebook,const ThemeConfig& theme,BackgroundTemplate tpl,PageFormat pageFormat) {
    // Keep the logical page attached to every PDF sheet: equal world coordinates
    // on different notebook pages must never merge their annotations.
    const QVector<PageData> documents=notebook.isEmpty()?QVector<PageData>{PageData{}}:notebook;
    struct Sheet { qsizetype document;QRectF rect; };
    QVector<Sheet> pages;
    for(qsizetype i=0;i<documents.size();++i)for(const auto& rect:pdfPageBounds(documents[i],pageFormat))pages.append({i,rect});
    QSaveFile output(path);if(!output.open(QIODevice::WriteOnly))return false;
    bool success=true;
    {
        QPdfWriter writer(&output);writer.setResolution(144);writer.setTitle(QStringLiteral("Finily"));writer.setCreator(QStringLiteral("Finily"));
        auto layout=[&](QRectF rect) {
            // One world pixel is 0.75pt; coordinates remain vector paths.
            const QSizeF millimeters=rect.size()*std::min(25.4/96.0,4800.0/std::max(rect.width(),rect.height()));
            return writer.setPageLayout(QPageLayout(QPageSize(millimeters,QPageSize::Millimeter,QString(),QPageSize::ExactMatch),QPageLayout::Portrait,QMarginsF(),QPageLayout::Millimeter));
        };
        if(!layout(pages.front().rect))return false;
        QPainter painter;if(!painter.begin(&writer))return false;
        for(qsizetype page=0;page<pages.size();++page) {
            const auto rect=pages[page].rect;const auto& document=documents[pages[page].document];
            const auto& strokes=document.strokes;const auto& shapes=document.shapes;const auto& images=document.images;const auto& texts=document.texts;
            if(page>0){if(!layout(rect)||!writer.newPage()){success=false;break;}}
            painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.scale(double(writer.width())/rect.width(),double(writer.height())/rect.height());painter.translate(-rect.left(),-rect.top());painter.setClipRect(rect);
            const bool imported=std::any_of(images.cbegin(),images.cend(),[](const ImageItem& image){return image.isPdfPage&&!image.rect.isEmpty();});
            const auto sheet=imported?QRectF():sheetRectForPageFormat(pageFormat);
            if(sheet.isEmpty()) {
                painter.fillRect(rect,theme.background);drawBackgroundTemplate(painter,tpl,theme,Viewport{},rect);
            } else {
                painter.fillRect(rect,theme.isDark?QColor("#0c0e13"):QColor("#e6e7ec"));
                painter.save();painter.setClipRect(sheet,Qt::IntersectClip);
                painter.fillRect(sheet,theme.background);drawBackgroundTemplate(painter,tpl,theme,Viewport{},sheet);painter.restore();
            }
            for(int pass=0;pass<2;++pass)for(const auto& image:images)if(image.isPdfPage==(pass==0)&&image.rect.intersects(rect)&&!image.image.isNull())painter.drawImage(image.rect,image.image);
            for(const auto& s:strokes)if(s.tool==ToolType::Highlighter&&strokeBounds(s).intersects(rect))drawStroke(painter,s);
            for(const auto& s:shapes)if(computeShapeBounds(s).intersects(rect))drawShape(painter,s);
            for(const auto& s:strokes)if(s.tool!=ToolType::Highlighter&&strokeBounds(s).intersects(rect))drawStroke(painter,s);
            painter.setRenderHint(QPainter::TextAntialiasing);
            for(const auto& item:texts)if(textBounds(item).intersects(rect)){painter.setFont(fontFor(item.fontSize));painter.setPen(item.color);painter.drawText(item.pos+QPointF(0,QFontMetricsF(painter.font()).ascent()),item.text);}
            painter.restore();
        }
        if(!painter.end())success=false;
    }
    if(!success){output.cancelWriting();return false;}
    return output.commit();
}
} // namespace finily
