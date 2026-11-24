#pragma once

#include "core/Types.h"

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QVector>

namespace finily {

QString autosavePath();

QJsonObject documentSettings(ShapeKind kind, bool hold, bool guides, bool swap,
                             PressureProfile profile, ThemeId theme,
                             BackgroundTemplate tpl, Viewport viewport,
                             PageFormat pageFormat);

QJsonObject serializePage(const PageData& document, bool& valid);

bool serializeDocumentToFile(const QString& path, const QVector<PageData>& pages,
                             int currentPage, QJsonObject root, bool newFile = false);

QJsonObject loadDocumentFromFile(const QString& path);

PageData deserializePage(const QJsonObject& root,
                         QHash<QString, QVector<ImageItem>>& pdfSources);

// Renderiza una miniatura real de cualquier PageData (2x HiDPI).
QImage renderPageThumbnail(const PageData& page, ThemeId themeId,
                           BackgroundTemplate fallbackTpl, PageFormat pageFormat,
                           const QSize& size);

// Caché en disco de miniaturas de portada para la Biblioteca (HomeScreen).
QString coverThumbnailPath(const QString& notebookPath);
void saveCoverThumbnail(const QString& notebookPath, const QImage& thumbnail);
void updateCoverThumbnailForPages(const QString& notebookPath, const QVector<PageData>& pages,
                                  ThemeId themeId, BackgroundTemplate fallbackTpl,
                                  PageFormat pageFormat);
void moveCoverThumbnail(const QString& oldNotebookPath, const QString& newNotebookPath);
void removeCoverThumbnail(const QString& notebookPath);
QImage loadOrGenerateCoverThumbnail(const QString& notebookPath, const QDateTime& notebookModified);

} // namespace finily
