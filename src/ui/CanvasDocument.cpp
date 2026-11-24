#include "CanvasWidget.h"
#include "CanvasWidgetInternal.h"
#include "engine/DocumentSerializer.h"
#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>
#include <chrono>

namespace finily {

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
        if (m_pageFormat != PageFormat::Infinite && m_viewport.x == 0 && m_viewport.y == 0)
            m_viewport = fittedSheetViewport(m_pageFormat, size());
    }
    clampFiniteViewport();
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

bool CanvasWidget::currentPageHasContent() const {
    return !m_strokes.isEmpty() || !m_shapes.isEmpty() || !m_images.isEmpty() || !m_texts.isEmpty();
}

bool CanvasWidget::hasUnsavedChanges() const {
    if (!m_dirty)
        return false;
    if (currentPageHasContent() || m_pages.size() > 1 || m_pageFormat != PageFormat::Infinite)
        return true;
    return !m_currentFilePath.isEmpty(); // un cuaderno guardado que se vació también es un cambio
}

DocumentSnapshot CanvasWidget::currentSnapshot() const {
    return {m_strokes, m_shapes, m_images, m_texts};
}

void CanvasWidget::pushUndoSnapshot(const DocumentSnapshot& snapshot) {
    m_undoStack.append({m_currentPageIndex, snapshot, {}});
    if (m_undoStack.size() > 50)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    m_redoStack.squeeze();
    m_dirty = true;
}

void CanvasWidget::pushPageHistory() {
    syncWorkingPage(m_pages, m_currentPageIndex, currentSnapshot(), m_viewport);
    // Limitar copias de cuaderno completo en el historial para ahorrar memoria RAM
    int fullPageEntries = 0;
    for (qsizetype i = m_undoStack.size() - 1; i >= 0; --i) {
        if (!m_undoStack[i].pages.isEmpty()) {
            ++fullPageEntries;
            if (fullPageEntries >= 12)
                m_undoStack.removeAt(i);
        }
    }
    m_undoStack.append({m_currentPageIndex, {}, m_pages});
    if (m_undoStack.size() > 50)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    m_redoStack.squeeze();
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
    if (m_beautifyTimer)
        m_beautifyTimer->stop();
    m_pendingBeautifyStart = -1;
    m_pendingBeautifyBox = QRectF();
    m_pendingBeautifyIds.clear();
    restoreHistoryEntry(m_undoStack, m_redoStack);
}

void CanvasWidget::redo() {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return;
    finishTextEditor(this, true);
    if (m_beautifyTimer)
        m_beautifyTimer->stop();
    m_pendingBeautifyStart = -1;
    m_pendingBeautifyBox = QRectF();
    m_pendingBeautifyIds.clear();
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
    m_zoomAnimation->stop();
    stopShapePulse();
    clearPenCursor();
    finishTextEditor(this, false);
    m_holdTimer->stop();
    m_laserTimer->stop();
    m_laserTrail.clear();
    m_beautifyTimer->stop();
    m_pendingBeautifyStart = -1;
    m_pendingBeautifyBox = QRectF();
    m_pendingBeautifyIds.clear();
    m_mode = InteractionMode::None;
    m_tabletActive = false;
    m_hasActiveStroke = false;
    m_hasActiveShape = false;
    m_holdSnappedToShape = false;
    cancelActivePolyline();
    m_strokes.clear();
    m_shapes.clear();
    m_images.clear();
    m_texts.clear();
    m_currentFilePath.clear();
    m_pages = {PageData{}};
    m_currentPageIndex = 0;
    m_dirty = false;
    m_undoStack.clear();
    m_redoStack.clear();
    m_showEraserCursor = false;
    clearSelection();
    m_viewport = fittedSheetViewport(m_pageFormat, size());
    emit viewportChanged(m_viewport.zoom);
    updateCursorShape();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
}

bool CanvasWidget::createNotebook(const QString& path, ThemeId theme, BackgroundTemplate background,
                                  PageFormat format) {
    if (m_mode != InteractionMode::None || path.isEmpty())
        return false;
    PageData page;
    page.viewport = fittedSheetViewport(format, size());
    const auto settings = documentSettings(m_shapeKind, m_holdToShapeEnabled, m_smartGuidesEnabled,
                                           m_swapPenButtons, m_pressureProfile, theme, background,
                                           page.viewport, format);
    // El borrador anterior sigue abierto si falla la creación del archivo.
    return serializeDocumentToFile(path, {page}, 0, settings, true) && loadFromFile(path);
}

bool CanvasWidget::saveToFile(const QString& path, bool newFile) {
    if (!preparePageChange() || path.isEmpty())
        return false;
    syncWorkingPage(m_pages, m_currentPageIndex, currentSnapshot(), m_viewport);
    const auto settings = documentSettings(m_shapeKind, m_holdToShapeEnabled, m_smartGuidesEnabled,
                                           m_swapPenButtons, m_pressureProfile, m_themeId, m_template,
                                           m_viewport, m_pageFormat);
    if (!serializeDocumentToFile(path, m_pages, m_currentPageIndex, settings, newFile)) {
        emit toastRequested(tr("No se pudo guardar el cuaderno"));
        return false;
    }
    m_currentFilePath = path;
    m_dirty = false;
    updateCoverThumbnailForPages(path, m_pages, m_themeId, m_template, m_pageFormat);
    saveAutosave();
    emit stateChanged();
    emit toastRequested(tr("Guardado ✓"));
    return true;
}

void CanvasWidget::saveAutosave() const { m_autosaveTimer->start(); }

void CanvasWidget::flushAutosave() {
    if (m_autosaveFuture.valid())
        m_autosaveFuture.wait();
    if (m_autosaveTimer->isActive()) {
        m_autosaveTimer->stop();
        writeAutosaveNow(false);
    }
}

void CanvasWidget::writeAutosaveNow(bool async) const {
    if (async && m_autosaveFuture.valid()
        && m_autosaveFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        m_autosaveTimer->start(400);
        return;
    }
    DocumentSnapshot document = currentSnapshot();
    if (m_mode == InteractionMode::Erase)
        document = m_snapshotBeforeErase;
    if (m_mode == InteractionMode::ShapeDragHandle || m_mode == InteractionMode::ShapeMove
        || m_mode == InteractionMode::ShapeScale)
        document = m_snapshotBeforeShapeEdit;
    if (m_mode == InteractionMode::LassoMove || m_mode == InteractionMode::LassoScale
        || m_mode == InteractionMode::LassoRotate)
        document = m_snapshotBeforeLassoTransform;
    if (m_mode == InteractionMode::ImageMove || m_mode == InteractionMode::ImageResize)
        document = m_snapshotBeforeImageEdit;
    if (m_mode == InteractionMode::TextMove)
        document = m_snapshotBeforeTextEdit;
    const auto path = autosavePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return;
    auto* self = const_cast<CanvasWidget*>(this);
    syncWorkingPage(self->m_pages, self->m_currentPageIndex, document, m_viewport);
    auto settings = documentSettings(m_shapeKind, m_holdToShapeEnabled, m_smartGuidesEnabled,
                                     m_swapPenButtons, m_pressureProfile, m_themeId, m_template,
                                     m_viewport, m_pageFormat);

    const QVector<PageData> pagesCopy = m_pages;
    const int currentPageCopy = m_currentPageIndex;
    const QString filePathCopy = m_currentFilePath;
    const bool writeNotebook = !filePathCopy.isEmpty() && m_dirty;
    if (writeNotebook)
        self->m_dirty = false;
    const bool sessionDirty = self->m_dirty;
    // Renderizar la miniatura en el hilo GUI (QPainter/QFont en worker falla en Windows DirectWrite).
    QImage coverThumbCopy;
    if (writeNotebook && !pagesCopy.isEmpty()) {
        const auto& first = pagesCopy.first();
        const bool hasFirstContent = !first.strokes.isEmpty() || !first.shapes.isEmpty()
            || !first.images.isEmpty() || !first.texts.isEmpty();
        if (hasFirstContent)
            coverThumbCopy = renderPageThumbnail(first, m_themeId, m_template, m_pageFormat, QSize(132, 170));
    }

    auto job = [=]() mutable {
        if (writeNotebook) {
            serializeDocumentToFile(filePathCopy, pagesCopy, currentPageCopy, settings);
            saveCoverThumbnail(filePathCopy, coverThumbCopy);
        }
        settings.insert("sessionFilePath", filePathCopy);
        settings.insert("sessionDirty", sessionDirty);
        serializeDocumentToFile(path, pagesCopy, currentPageCopy, settings);
    };

    if (async) {
        if (m_autosaveFuture.valid())
            m_autosaveFuture.wait();
        m_autosaveFuture = std::async(std::launch::async, std::move(job));
    } else {
        if (m_autosaveFuture.valid())
            m_autosaveFuture.wait();
        job();
    }
}

void CanvasWidget::loadAutosave() {
    const auto previousPath = m_currentFilePath;
    const bool previousDirty = m_dirty;
    if (!loadFromFile(autosavePath())) {
        m_currentFilePath = previousPath;
        m_dirty = previousDirty;
        return;
    }
    // loadFromFile ya restauró la sesión; un autoguardado antiguo (sin datos de
    // sesión) deja la ruta apuntando al propio autosave.json.
    if (m_currentFilePath == autosavePath()) {
        m_currentFilePath.clear();
        m_dirty = true;
    }
    if (!m_currentFilePath.isEmpty() && !QFileInfo::exists(m_currentFilePath)) {
        m_currentFilePath.clear();
        m_dirty = true;
    }
    m_autosaveTimer->stop();
    emit stateChanged();
}

bool CanvasWidget::loadFromFile(const QString& path) {
    if (m_mode != InteractionMode::None)
        return false;
    const auto root = loadDocumentFromFile(path);
    if (root.isEmpty())
        return false;
    m_zoomAnimation->stop();
    stopShapePulse();
    clearPenCursor();
    finishTextEditor(this, false);
    m_beautifyTimer->stop();
    m_pendingBeautifyStart = -1;
    m_pendingBeautifyBox = QRectF();
    m_pendingBeautifyIds.clear();
    QVector<PageData> pages;
    QHash<QString, QVector<ImageItem>> pdfSources;
    if (root.value("version").toInt() == 5)
        for (const auto& value : root.value("pages").toArray())
            pages.append(deserializePage(value.toObject(), pdfSources));
    else
        pages.append(deserializePage(root, pdfSources));
    if (pages.isEmpty())
        pages.append(PageData{});
    m_liveInk.reset();
    m_liveInkLayer = QImage();
    m_radialOpen = false;
    m_pages = std::move(pages);
    m_currentPageIndex = std::clamp(root.value("currentPageIndex").toInt(), 0, int(m_pages.size()) - 1);
    const auto page = m_pages[m_currentPageIndex];
    m_strokes = page.strokes;
    m_shapes = page.shapes;
    m_images = page.images;
    m_texts = page.texts;
    m_viewport = page.viewport;
    m_selectedTextId.clear();
    m_laserTrail.clear();
    m_laserTimer->stop();
    m_hasActiveStroke = false;
    m_undoStack.clear();
    m_redoStack.clear();
    m_selectedStrokeIds.clear();
    m_selectedShapeIds.clear();
    m_selectedTextIds.clear();
    m_selectedImageIds.clear();
    m_selectedImageId.clear();
    m_lassoBounds = {};
    m_lassoPolygon.clear();
    m_linkedPreview = false;
    m_polylineActive = false;
    m_lastPolylineTapMs = -1;
    m_polylineVertices.clear();
    m_hasActiveShape = false;
    m_selectedShapeId.clear();
    m_activeGuides.clear();
    const int shapeKind = root.value("shapeKind").toInt(int(ShapeKind::Triangle));
    if (shapeKind >= 0 && shapeKind <= int(ShapeKind::AngleMarker)
        && shapeKind != int(ShapeKind::ShadedRegion))
        m_shapeKind = ShapeKind(shapeKind);
    m_holdToShapeEnabled = root.value("holdToShapeEnabled").toBool(true);
    m_smartGuidesEnabled = root.value("smartGuidesEnabled").toBool(true);
    m_swapPenButtons = root.value("swapPenButtons").toBool(false);
    const int profile = root.value("pressureProfile").toInt(int(PressureProfile::Light));
    if (profile >= int(PressureProfile::Light) && profile <= int(PressureProfile::Firm))
        m_pressureProfile = PressureProfile(profile);
    const int format = root.value("pageFormat").toInt(int(PageFormat::Infinite));
    m_pageFormat = format >= int(PageFormat::Infinite) && format <= int(PageFormat::Slide16x9)
        ? PageFormat(format)
        : PageFormat::Infinite;
    const int theme = root.value("theme").toInt(0);
    const int tpl = root.value("template").toInt(2);
    if (theme >= 0 && theme <= int(ThemeId::PaperCream))
        m_themeId = ThemeId(theme);
    if (tpl >= 0 && tpl <= int(BackgroundTemplate::Ruled))
        m_template = BackgroundTemplate(tpl);
    m_color = getThemeConfig(m_themeId).defaultInk;
    m_currentFilePath = path;
    m_dirty = false;
    if (root.contains("sessionFilePath")) {
        m_currentFilePath = root.value("sessionFilePath").toString();
        m_dirty = root.value("sessionDirty").toBool(false);
    }
    if (m_pageFormat != PageFormat::Infinite && !size().isEmpty()) {
        if (!currentPageHasContent())
            m_viewport = fittedSheetViewport(m_pageFormat, size());
        else
            clampFiniteViewport();
    }
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
    emit viewportChanged(m_viewport.zoom);
    return true;
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
