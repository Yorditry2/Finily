#include "CanvasWidget.h"
#include "CanvasWidgetInternal.h"
#include "core/Utils.h"

#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <chrono>
#include <future>
#include <optional>
#include <utility>

namespace finily {

void CanvasWidget::finishTextEditor(bool accept) {
    if (m_textEditor)
        m_textEditor->finish(accept);
}

void CanvasWidget::cancelPendingBeautify() {
    if (m_beautifyTimer)
        m_beautifyTimer->stop();
    m_pendingBeautifyBox = QRectF();
    m_pendingBeautifyIds.clear();
}

NotebookDocument::DocumentToolSettings CanvasWidget::toolSettingsForDocument() const {
    NotebookDocument::DocumentToolSettings settings;
    settings.shapeKind = m_shapeKind;
    settings.holdToShapeEnabled = m_holdToShapeEnabled;
    settings.smartGuidesEnabled = m_smartGuidesEnabled;
    settings.swapPenButtons = m_swapPenButtons;
    settings.pressureProfile = m_pressureProfile;
    return settings;
}

void CanvasWidget::applyDocumentToolSettings(const NotebookDocument::DocumentToolSettings& settings) {
    m_shapeKind = settings.shapeKind;
    m_holdToShapeEnabled = settings.holdToShapeEnabled;
    m_smartGuidesEnabled = settings.smartGuidesEnabled;
    m_smartGuideOptions.enabled = m_smartGuidesEnabled;
    m_swapPenButtons = settings.swapPenButtons;
    m_pressureProfile = settings.pressureProfile;
}

void CanvasWidget::waitForAutosave() {
    if (m_autosaveFuture.valid())
        m_autosaveFuture.wait();
}

bool CanvasWidget::preparePageChange() {
    if (m_mode == InteractionMode::LaserTrail)
        m_mode = InteractionMode::None;
    if (m_mode != InteractionMode::None)
        return false;
    finishTextEditor(true);
    if (m_polylineActive)
        finishActivePolyline();
    flushPendingBeautify();
    m_zoomAnimation->stop();
    return true;
}

void CanvasWidget::afterPageLoaded(bool structureChanged) {
    m_zoomAnimation->stop();
    stopShapePulse();
    clearPenCursor();
    m_holdTimer->stop();
    m_laserTimer->stop();
    m_laserTrail.clear();
    m_laserDotFading = false;
    m_laserDotReleaseMs = 0;
    m_lastLaserRect = {};
    m_mode = InteractionMode::None;
    m_tabletActive = false;
    m_hasActiveStroke = false;
    m_activeStroke = {};
    m_lastLiveDirtyRect = {};
    m_liveInkPending = false;
    m_liveInk.reset();
    m_liveInkLayer = QImage();
    m_radialOpen = false;
    m_radialHoveredColor = -1;
    m_radialHoveredTool = -1;
    m_hasActiveShape = false;
    m_holdSnappedToShape = false;
    m_polylineActive = false;
    m_lastPolylineTapMs = -1;
    m_polylineVertices.clear();
    m_polylineSnapClose = false;
    m_showEraserCursor = false;
    m_eraserCursorRect = {};
    m_linkedPreview = false;
    m_activeGuides.clear();
    m_rightClickOnSelection = false;

    Viewport& vp = m_doc.viewport();
    if (!size().isEmpty() && vp.x == 0.0 && vp.y == 0.0 && vp.zoom == 1.0) {
        bool fittedPdf = false;
        for (const auto& image : m_doc.images()) {
            if (image.isPdfPage && !image.rect.isEmpty()) {
                vp = fittedPdfViewport(image.rect, size());
                fittedPdf = true;
                break;
            }
        }
        if (!fittedPdf && m_doc.pageFormat() != PageFormat::Infinite)
            vp = fittedSheetViewport(m_doc.pageFormat(), size());
    }

    clampFiniteViewport();
    clearSelection();
    updateCursorShape();
    invalidateStaticCache();
    saveAutosave();
    if (structureChanged)
        emit pageStructureChanged();
    emit stateChanged();
    emit viewportChanged(m_doc.viewport().zoom);
}

void CanvasWidget::goToPage(int index) {
    if (index < 0 || index >= m_doc.pageCount() || index == m_doc.currentPageIndex()
        || !preparePageChange())
        return;
    if (m_doc.goToPage(index))
        afterPageLoaded(false);
}

void CanvasWidget::addPage() {
    insertPage(m_doc.currentPageIndex() + 1);
}

void CanvasWidget::nextPage() {
    goToPage(m_doc.currentPageIndex() + 1);
}

void CanvasWidget::prevPage() {
    goToPage(m_doc.currentPageIndex() - 1);
}

void CanvasWidget::deleteCurrentPage() {
    deletePages({m_doc.currentPageIndex()});
}

void CanvasWidget::insertPage(int index, std::optional<BackgroundTemplate> background) {
    PageData page;
    page.viewport = fittedSheetViewport(m_doc.pageFormat(), size());
    page.background = background;
    insertPages(index, {page});
}

void CanvasWidget::insertPages(int index, const QVector<PageData>& pages) {
    if (pages.isEmpty() || index < 0 || index > m_doc.pageCount() || !preparePageChange())
        return;
    if (m_doc.insertPages(index, pages))
        afterPageLoaded(true);
}

bool CanvasWidget::movePages(QVector<int> indices, int destination) {
    indices = validPageIndices(std::move(indices), m_doc.pageCount());
    if (indices.isEmpty() || destination < 0 || destination > m_doc.pageCount())
        return false;
    const int before = int(std::lower_bound(indices.cbegin(), indices.cend(), destination)
                           - indices.cbegin());
    const int target = destination - before;
    if (indices.last() - indices.first() + 1 == indices.size() && target == indices.first())
        return false;
    if (!preparePageChange())
        return false;
    if (!m_doc.movePages(std::move(indices), destination))
        return false;
    afterPageLoaded(true);
    return true;
}

void CanvasWidget::duplicatePages(QVector<int> indices) {
    indices = validPageIndices(std::move(indices), m_doc.pageCount());
    if (indices.isEmpty() || !preparePageChange())
        return;
    if (m_doc.duplicatePages(std::move(indices)))
        afterPageLoaded(true);
}

bool CanvasWidget::copyPages(QVector<int> indices) {
    indices = validPageIndices(std::move(indices), m_doc.pageCount());
    if (indices.isEmpty() || !preparePageChange())
        return false;
    m_copiedPages = m_doc.copyPages(indices);
    if (m_copiedPages.isEmpty())
        return false;
    emit toastRequested(tr("Páginas copiadas · %1").arg(m_copiedPages.size()));
    return true;
}

void CanvasWidget::pastePages(int index) {
    insertPages(index, m_copiedPages);
}

void CanvasWidget::deletePages(QVector<int> indices) {
    indices = validPageIndices(std::move(indices), m_doc.pageCount());
    if (indices.isEmpty() || !preparePageChange())
        return;
    if (m_doc.deletePages(std::move(indices), fittedSheetViewport(m_doc.pageFormat(), size())))
        afterPageLoaded(true);
}

void CanvasWidget::pushUndoSnapshot(const DocumentSnapshot& snapshot) {
    m_doc.pushUndoSnapshot(snapshot);
}

void CanvasWidget::pushPageHistory() {
    m_doc.pushPageHistory();
}

void CanvasWidget::undo() {
    if (m_mode != InteractionMode::None)
        return;
    finishTextEditor(true);
    if (m_polylineActive) {
        cancelActivePolyline();
        return;
    }
    cancelPendingBeautify();
    const auto res = m_doc.undo();
    if (res.applied)
        afterPageLoaded(res.structureChanged);
}

void CanvasWidget::redo() {
    if (m_mode != InteractionMode::None || m_polylineActive)
        return;
    finishTextEditor(true);
    cancelPendingBeautify();
    const auto res = m_doc.redo();
    if (res.applied)
        afterPageLoaded(res.structureChanged);
}

void CanvasWidget::clearBoard() {
    if (m_mode != InteractionMode::None)
        return;
    finishTextEditor(true);
    if (m_polylineActive)
        cancelActivePolyline();
    flushPendingBeautify();
    if (!m_doc.clearCurrentPage())
        return;
    clearSelection();
    invalidateStaticCache();
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::newDocument() {
    m_autosaveTimer->stop();
    waitForAutosave();
    finishTextEditor(false);
    cancelPendingBeautify();
    if (m_polylineActive)
        cancelActivePolyline();
    m_doc.resetNew(fittedSheetViewport(m_doc.pageFormat(), size()));
    refreshActiveColorFromTheme();
    afterPageLoaded(true);
}

bool CanvasWidget::createNotebook(const QString& path, ThemeId theme, BackgroundTemplate background,
                                  PageFormat format, double templateScale, CoverStyle cover,
                                  const QColor& customBackground) {
    if (m_mode != InteractionMode::None || path.isEmpty())
        return false;
    const bool hadPendingAutosave = m_autosaveTimer->isActive();
    m_autosaveTimer->stop();
    waitForAutosave();
    if (!m_doc.createNotebook(path, theme, background, format,
                              fittedSheetViewport(format, size()), toolSettingsForDocument(),
                              templateScale, cover, customBackground)) {
        if (hadPendingAutosave)
            saveAutosave();
        return false;
    }
    finishTextEditor(false);
    cancelPendingBeautify();
    if (m_polylineActive)
        cancelActivePolyline();
    refreshActiveColorFromTheme();
    afterPageLoaded(true);
    return true;
}

void CanvasWidget::startEphemeralDocument(ThemeId theme, BackgroundTemplate background,
                                          PageFormat format, double templateScale,
                                          CoverStyle cover, const QColor& customBackground) {
    m_autosaveTimer->stop();
    waitForAutosave();
    finishTextEditor(false);
    cancelPendingBeautify();
    if (m_polylineActive)
        cancelActivePolyline();
    m_doc.configureBlank(theme, background, format, templateScale, cover,
                         fittedSheetViewport(format, size()), customBackground);
    refreshActiveColorFromTheme();
    afterPageLoaded(true);
    m_autosaveTimer->stop();
}

bool CanvasWidget::saveToFile(const QString& path, bool newFile, bool silent) {
    if (path.isEmpty() || !preparePageChange())
        return false;
    m_autosaveTimer->stop();
    waitForAutosave();
    if (!m_doc.saveToFile(path, toolSettingsForDocument(), newFile)) {
        emit toastRequested(tr("No se pudo guardar el cuaderno"));
        return false;
    }
    saveAutosave();
    emit stateChanged();
    if (!silent)
        emit toastRequested(tr("Guardado ✓"));
    return true;
}

void CanvasWidget::saveAutosave() {
    m_autosaveTimer->start(1500);
}

void CanvasWidget::cancelPendingAutosave() {
    m_autosaveTimer->stop();
}

void CanvasWidget::flushAutosave() {
    waitForAutosave();
    if (m_autosaveTimer->isActive()) {
        m_autosaveTimer->stop();
        writeAutosaveNow(false);
    }
}

void CanvasWidget::writeAutosaveNow(bool async) {
    if (async && m_autosaveFuture.valid()
        && m_autosaveFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        m_autosaveTimer->start(400);
        return;
    }

    std::optional<DocumentSnapshot> overrideSnap;
    switch (m_mode) {
    case InteractionMode::Erase:
        overrideSnap = m_snapshotBeforeErase;
        break;
    case InteractionMode::ShapeDragHandle:
    case InteractionMode::ShapeMove:
    case InteractionMode::ShapeScale:
        overrideSnap = m_snapshotBeforeShapeEdit;
        break;
    case InteractionMode::LassoMove:
    case InteractionMode::LassoScale:
    case InteractionMode::LassoRotate:
        overrideSnap = m_snapshotBeforeLassoTransform;
        break;
    case InteractionMode::ImageMove:
    case InteractionMode::ImageResize:
        overrideSnap = m_snapshotBeforeImageEdit;
        break;
    case InteractionMode::TextMove:
        overrideSnap = m_snapshotBeforeTextEdit;
        break;
    default:
        break;
    }

    const bool wantCover = m_doc.isCoverThumbDirty();
    NotebookDocument::AutosaveJob job =
        m_doc.prepareAutosave(toolSettingsForDocument(), overrideSnap, wantCover, async);
    const uint64_t rev = job.revision;
    const uint64_t epoch = job.sessionEpoch;
    const bool wroteNotebook = job.writeNotebook;
    const bool savedCover = job.saveCoverThumb;

    waitForAutosave();
    if (async) {
        m_autosaveFuture = std::async(
            std::launch::async,
            [guard = QPointer<CanvasWidget>(this),
             job = std::move(job),
             rev,
             epoch,
             wroteNotebook,
             savedCover]() mutable {
                const bool ok = NotebookDocument::executeAutosaveJob(std::move(job));
                if (ok && wroteNotebook && guard) {
                    QMetaObject::invokeMethod(
                        guard,
                        [guard, rev, epoch, savedCover] {
                            if (guard && guard->m_doc.sessionEpoch() == epoch) {
                                guard->m_doc.markSaved(rev);
                                if (savedCover)
                                    guard->m_doc.markCoverThumbSaved(rev);
                                emit guard->stateChanged();
                            }
                        },
                        Qt::QueuedConnection);
                }
            });
    } else {
        if (NotebookDocument::executeAutosaveJob(std::move(job)) && wroteNotebook) {
            m_doc.markSaved(rev);
            if (savedCover)
                m_doc.markCoverThumbSaved(rev);
        }
    }
}

void CanvasWidget::loadAutosave() {
    const bool hadPendingAutosave = m_autosaveTimer->isActive();
    m_autosaveTimer->stop();
    waitForAutosave();
    NotebookDocument::DocumentToolSettings ts = toolSettingsForDocument();
    if (!m_doc.loadAutosave(&ts)) {
        if (hadPendingAutosave)
            saveAutosave();
        return;
    }
    finishTextEditor(false);
    cancelPendingBeautify();
    if (m_polylineActive)
        cancelActivePolyline();
    applyDocumentToolSettings(ts);
    refreshActiveColorFromTheme();
    afterPageLoaded(true);
    m_autosaveTimer->stop();
}

bool CanvasWidget::loadFromFile(const QString& path) {
    if (m_mode != InteractionMode::None)
        return false;
    const bool hadPendingAutosave = m_autosaveTimer->isActive();
    m_autosaveTimer->stop();
    waitForAutosave();
    NotebookDocument::DocumentToolSettings ts = toolSettingsForDocument();
    if (!m_doc.loadFromFile(path, &ts, false)) {
        if (hadPendingAutosave)
            saveAutosave();
        return false;
    }
    finishTextEditor(false);
    cancelPendingBeautify();
    if (m_polylineActive)
        cancelActivePolyline();
    applyDocumentToolSettings(ts);
    refreshActiveColorFromTheme();
    afterPageLoaded(true);
    return true;
}

NotebookSession CanvasWidget::exportSession() const {
    return m_doc.exportSession();
}

void CanvasWidget::importSession(const NotebookSession& session) {
    m_autosaveTimer->stop();
    waitForAutosave();
    finishTextEditor(false);
    cancelPendingBeautify();
    if (m_polylineActive)
        cancelActivePolyline();
    m_doc.importSession(session);
    refreshActiveColorFromTheme();
    afterPageLoaded(true);
    update();
}

QImage CanvasWidget::pageThumbnail(int index, const QSize& size) const {
    return m_doc.pageThumbnail(index, size, templateScale());
}

QImage CanvasWidget::renderCurrentPageImage(const QSize& targetSize) const {
    const PageFormat fmt = m_doc.pageFormat();
    const bool landscape = (fmt == PageFormat::A4Landscape || fmt == PageFormat::Slide16x9
                            || fmt == PageFormat::A5Landscape || fmt == PageFormat::LetterLandscape
                            || fmt == PageFormat::LegalLandscape);
    const QSize sz = targetSize.isEmpty() ? (landscape ? QSize(2260, 1600) : QSize(1600, 2260)) : targetSize;
    return pageThumbnail(m_doc.currentPageIndex(), sz);
}

bool CanvasWidget::exportCurrentPageToPng(const QString& filePath, const QSize& targetSize) const {
    if (filePath.isEmpty())
        return false;
    const QImage img = renderCurrentPageImage(targetSize);
    return !img.isNull() && img.save(filePath, "PNG");
}

} // namespace finily
