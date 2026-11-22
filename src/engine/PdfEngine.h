#pragma once
#include "core/Types.h"
namespace finily {
QVector<ImageItem> importPdfPages(const QString& filePath,double startY=40.0);
bool exportDocumentToPdf(const QString& filePath,const QVector<Stroke>& strokes,const QVector<ShapeItem>& shapes,const QVector<ImageItem>& images,const ThemeConfig& theme,BackgroundTemplate tpl,const QVector<TextItem>& texts = {});
// Every notebook page produces one sheet, or one sheet per imported PDF slide.
bool exportPagesToPdf(const QString& filePath,const QVector<PageData>& pages,const ThemeConfig& theme,BackgroundTemplate tpl);
} // namespace finily
