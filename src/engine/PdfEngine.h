#pragma once
#include "core/Types.h"
namespace finily {
// An empty rectangle denotes the unbounded canvas.
inline QRectF sheetRectForPageFormat(PageFormat format) {
    switch(format) {
    case PageFormat::A4: return QRectF(-397.0,-561.5,794.0,1123.0);
    case PageFormat::Slide16x9: return QRectF(-640.0,-360.0,1280.0,720.0);
    case PageFormat::Infinite: return {};
    }
    return {};
}
QVector<ImageItem> importPdfPages(const QString& filePath,double startY=40.0);
bool exportDocumentToPdf(const QString& filePath,const QVector<Stroke>& strokes,const QVector<ShapeItem>& shapes,const QVector<ImageItem>& images,const ThemeConfig& theme,BackgroundTemplate tpl,const QVector<TextItem>& texts = {});
// Every notebook page produces one sheet, or one sheet per imported PDF slide.
bool exportPagesToPdf(const QString& filePath,const QVector<PageData>& pages,const ThemeConfig& theme,BackgroundTemplate tpl,PageFormat pageFormat = PageFormat::Infinite);
} // namespace finily
