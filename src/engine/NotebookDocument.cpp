#include "engine/NotebookDocument.h"

#include "core/Utils.h"
#include "engine/DocumentSerializer.h"
#include "engine/PdfEngine.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <utility>

namespace finily {

namespace {

constexpr qsizetype kMaxUndoEntries = 50;
constexpr int kMaxFullPageHistoryEntries = 12;
constexpr QSize kCoverThumbSize(132, 170);

bool pageHasVisibleContent(const PageData& page) {
    return !page.strokes.isEmpty() || !page.shapes.isEmpty()
        || !page.images.isEmpty() || !page.texts.isEmpty();
}

} // namespace

NotebookDocument::NotebookDocument() {
    m_pages.append(PageData{});
}

void NotebookDocument::ensureValidState() {
    if (m_pages.isEmpty())
        m_pages.append(PageData{});
    m_currentPageIndex = std::clamp(m_currentPageIndex, 0, int(m_pages.size()) - 1);
}

DocumentSnapshot NotebookDocument::currentSnapshot() const {
    const auto& page = currentPage();
    return {page.strokes, page.shapes, page.images, page.texts};
}

void NotebookDocument::restoreSnapshot(const DocumentSnapshot& snapshot) {
    auto& page = currentPage();
    page.strokes = snapshot.strokes;
    page.shapes = snapshot.shapes;
    page.images = snapshot.images;
    page.texts = snapshot.texts;
}

void NotebookDocument::setThemeId(ThemeId themeId) {
    if (m_themeId == themeId && !m_customBackground.isValid())
        return;
    m_themeId = themeId;
    m_customBackground = QColor();
    ++m_revision;
}

BackgroundTemplate NotebookDocument::pageTemplate(int index) const {
    if (index < 0 || index >= m_pages.size())
        return m_defaultTemplate;
    return m_pages.at(index).background.value_or(m_defaultTemplate);
}

BackgroundTemplate NotebookDocument::currentTemplate() const {
    return currentPage().background.value_or(m_defaultTemplate);
}

bool NotebookDocument::setBackgroundTemplate(BackgroundTemplate tpl) {
    if (tpl == currentTemplate())
        return false;
    if (currentPage().background.has_value()) {
        pushPageHistory();
        currentPage().background = tpl;
    } else {
        m_defaultTemplate = tpl;
        ++m_revision;
    }
    return true;
}

void NotebookDocument::setPageFormat(PageFormat format) {
    if (!isValidPageFormat(int(format)))
        return;
    if (m_pageFormat == format)
        return;
    m_pageFormat = format;
    ++m_revision;
}

void NotebookDocument::setTemplateScale(double scale) {
    const double clamped = clampTemplateScale(scale);
    if (std::abs(m_templateScale - clamped) < 1e-4)
        return;
    m_templateScale = clamped;
    ++m_revision;
}

void NotebookDocument::setCoverStyle(CoverStyle cover) {
    if (m_cover.colorIndex == cover.colorIndex && m_cover.iconIndex == cover.iconIndex)
        return;
    m_cover = cover;
    ++m_revision;
}

void NotebookDocument::markSaved(uint64_t rev) {
    if (rev == m_revision)
        m_savedRevision = rev;
}

bool NotebookDocument::currentPageHasContent() const {
    return pageHasVisibleContent(currentPage());
}

bool NotebookDocument::hasAnyContent() const {
    return std::any_of(m_pages.cbegin(), m_pages.cend(), pageHasVisibleContent);
}

bool NotebookDocument::hasUnsavedChanges() const {
    if (!isDirty())
        return false;
    if (currentPageHasContent() || m_pages.size() > 1 || m_pageFormat != PageFormat::Infinite)
        return true;
    return !m_filePath.isEmpty();
}

int NotebookDocument::pdfPageCount() const {
    const auto& imgs = images();
    return int(std::count_if(imgs.cbegin(), imgs.cend(),
                             [](const ImageItem& image) { return image.isPdfPage; }));
}

int NotebookDocument::documentPdfPageCount() const {
    int total = 0;
    for (const auto& page : m_pages) {
        total += int(std::count_if(page.images.cbegin(), page.images.cend(),
                                   [](const ImageItem& item) { return item.isPdfPage; }));
    }
    return total;
}

bool NotebookDocument::goToPage(int index) {
    if (index < 0 || index >= pageCount() || index == m_currentPageIndex)
        return false;
    m_currentPageIndex = index;
    return true;
}

bool NotebookDocument::insertPages(int index, const QVector<PageData>& pages) {
    if (pages.isEmpty() || index < 0 || index > pageCount())
        return false;
    pushPageHistory();
    int position = index;
    for (const auto& page : pages)
        m_pages.insert(position++, page);
    m_currentPageIndex = index;
    ensureValidState();
    return true;
}

bool NotebookDocument::movePages(QVector<int> indices, int destination) {
    indices = validPageIndices(std::move(indices), pageCount());
    if (indices.isEmpty() || destination < 0 || destination > pageCount())
        return false;
    const int before = int(std::lower_bound(indices.cbegin(), indices.cend(), destination)
                           - indices.cbegin());
    const int target = destination - before;
    if (indices.last() - indices.first() + 1 == indices.size() && target == indices.first())
        return false;
    pushPageHistory();
    QVector<PageData> moved;
    moved.reserve(indices.size());
    for (int index : std::as_const(indices))
        moved.append(m_pages.at(index));
    for (auto it = indices.crbegin(); it != indices.crend(); ++it)
        m_pages.removeAt(*it);
    for (qsizetype i = 0; i < moved.size(); ++i)
        m_pages.insert(target + i, moved.at(i));
    m_currentPageIndex = target;
    ensureValidState();
    return true;
}

bool NotebookDocument::duplicatePages(QVector<int> indices) {
    indices = validPageIndices(std::move(indices), pageCount());
    if (indices.isEmpty())
        return false;
    QVector<PageData> copies;
    copies.reserve(indices.size());
    for (int index : std::as_const(indices))
        copies.append(m_pages.at(index));
    return insertPages(indices.last() + 1, copies);
}

QVector<PageData> NotebookDocument::copyPages(QVector<int> indices) const {
    indices = validPageIndices(std::move(indices), pageCount());
    QVector<PageData> copies;
    copies.reserve(indices.size());
    for (int index : std::as_const(indices)) {
        copies.append(m_pages.at(index));
        if (!copies.last().background.has_value())
            copies.last().background = m_defaultTemplate;
    }
    return copies;
}

bool NotebookDocument::deletePages(QVector<int> indices, const Viewport& blankFallbackViewport) {
    indices = validPageIndices(std::move(indices), pageCount());
    if (indices.isEmpty())
        return false;
    pushPageHistory();
    const int current = m_currentPageIndex;
    const auto firstAtOrAfter = std::lower_bound(indices.cbegin(), indices.cend(), current);
    const int removedBefore = int(firstAtOrAfter - indices.cbegin());
    for (auto it = indices.crbegin(); it != indices.crend(); ++it)
        m_pages.removeAt(*it);
    if (m_pages.isEmpty()) {
        PageData blank;
        blank.viewport = blankFallbackViewport;
        m_pages.append(blank);
    }
    m_currentPageIndex = std::clamp(current - removedBefore, 0, int(m_pages.size()) - 1);
    return true;
}

int NotebookDocument::insertPdfPages(const QVector<ImageItem>& imported, bool replaceBlankIfEligible) {
    if (imported.isEmpty())
        return m_currentPageIndex;
    const bool replaceBlank = replaceBlankIfEligible && pageCount() == 1
        && !currentPageHasContent() && !m_pages.at(0).background.has_value();
    const int first = m_currentPageIndex + (replaceBlank ? 0 : 1);
    pushPageHistory();
    if (replaceBlank)
        m_pages.removeAt(first);
    int index = first;
    for (const auto& image : imported) {
        PageData page;
        page.images.append(image);
        m_pages.insert(index++, page);
    }
    m_currentPageIndex = first;
    ensureValidState();
    return m_currentPageIndex;
}

bool NotebookDocument::clearCurrentPage() {
    auto& page = currentPage();
    const bool hasNonPdfImage = std::any_of(page.images.cbegin(), page.images.cend(),
                                            [](const ImageItem& img) { return !img.isPdfPage; });
    if (page.strokes.isEmpty() && page.shapes.isEmpty() && page.texts.isEmpty() && !hasNonPdfImage)
        return false;
    pushUndoSnapshot(currentSnapshot());
    page.strokes.clear();
    page.shapes.clear();
    page.images.removeIf([](const ImageItem& image) { return !image.isPdfPage; });
    page.texts.clear();
    return true;
}

void NotebookDocument::resetNew(const Viewport& initialViewport) {
    m_pages.clear();
    PageData blank;
    blank.viewport = initialViewport;
    m_pages.append(std::move(blank));
    m_currentPageIndex = 0;
    m_filePath.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_revision = 0;
    m_savedRevision = 0;
    ++m_sessionEpoch;
}

void NotebookDocument::configureBlank(ThemeId theme, BackgroundTemplate tpl, PageFormat format,
                                      double templateScale, CoverStyle cover,
                                      const Viewport& initialViewport,
                                      const QColor& customBackground) {
    m_themeId = theme;
    m_defaultTemplate = tpl;
    m_pageFormat = isValidPageFormat(int(format)) ? format : PageFormat::Infinite;
    m_templateScale = clampTemplateScale(templateScale);
    m_cover = cover;
    m_customBackground = customBackground.isValid() ? customBackground : QColor();
    resetNew(initialViewport);
}

void NotebookDocument::pushUndoSnapshot(const DocumentSnapshot& before) {
    m_undoStack.append({m_currentPageIndex, before, {}});
    if (m_undoStack.size() > kMaxUndoEntries)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    m_redoStack.squeeze();
    ++m_revision;
}

void NotebookDocument::pushPageHistory() {
    // Truncate everything at or below the oldest evicted structural snapshot so
    // no stale single-page entry can outlive a structural page-count change.
    int fullPageEntries = 0;
    qsizetype truncateAt = -1;
    for (qsizetype i = m_undoStack.size() - 1; i >= 0; --i) {
        if (!m_undoStack.at(i).pages.isEmpty()) {
            ++fullPageEntries;
            if (fullPageEntries >= kMaxFullPageHistoryEntries) {
                truncateAt = i;
                break;
            }
        }
    }
    if (truncateAt >= 0)
        m_undoStack.remove(0, truncateAt + 1);

    m_undoStack.append({m_currentPageIndex, {}, m_pages});
    if (m_undoStack.size() > kMaxUndoEntries)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    m_redoStack.squeeze();
    ++m_revision;
}

NotebookDocument::HistoryRestoreResult NotebookDocument::restoreHistoryEntry(
    QVector<HistoryEntry>& source,
    QVector<HistoryEntry>& destination) {
    if (source.isEmpty())
        return {};
    HistoryEntry entry = source.takeLast();
    const bool structureChanged = !entry.pages.isEmpty();
    if (structureChanged) {
        destination.append({m_currentPageIndex, {}, m_pages});
        m_pages = std::move(entry.pages);
        if (m_pages.isEmpty())
            m_pages.append(PageData{});
    } else {
        const int targetIndex = std::clamp(entry.pageIndex, 0, int(m_pages.size()) - 1);
        auto& page = m_pages[targetIndex];
        destination.append({targetIndex, {page.strokes, page.shapes, page.images, page.texts}, {}});
        page.strokes = std::move(entry.content.strokes);
        page.shapes = std::move(entry.content.shapes);
        page.images = std::move(entry.content.images);
        page.texts = std::move(entry.content.texts);
    }
    m_currentPageIndex = std::clamp(entry.pageIndex, 0, int(m_pages.size()) - 1);
    ++m_revision;
    return {true, structureChanged, m_currentPageIndex};
}

NotebookDocument::HistoryRestoreResult NotebookDocument::undo() {
    return restoreHistoryEntry(m_undoStack, m_redoStack);
}

NotebookDocument::HistoryRestoreResult NotebookDocument::redo() {
    return restoreHistoryEntry(m_redoStack, m_undoStack);
}

bool NotebookDocument::createNotebook(const QString& path, ThemeId theme, BackgroundTemplate tpl,
                                      PageFormat format, const Viewport& initialViewport,
                                      const DocumentToolSettings& toolSettings,
                                      double templateScale, CoverStyle cover,
                                      const QColor& customBackground) {
    if (path.isEmpty())
        return false;
    PageData page;
    page.viewport = initialViewport;
    const auto settings = documentSettings(
        toolSettings.shapeKind, toolSettings.holdToShapeEnabled, toolSettings.smartGuidesEnabled,
        toolSettings.swapPenButtons, toolSettings.pressureProfile, theme, tpl, page.viewport,
        format, clampTemplateScale(templateScale), cover.colorIndex, cover.iconIndex,
        customBackground);
    if (!serializeDocumentToFile(path, {page}, 0, settings, true))
        return false;
    return loadFromFile(path, nullptr, false);
}

bool NotebookDocument::saveToFile(const QString& path, const DocumentToolSettings& toolSettings,
                                  bool newFile) {
    if (path.isEmpty())
        return false;
    ensureValidState();
    const auto settings = documentSettings(
        toolSettings.shapeKind, toolSettings.holdToShapeEnabled, toolSettings.smartGuidesEnabled,
        toolSettings.swapPenButtons, toolSettings.pressureProfile, m_themeId, m_defaultTemplate,
        viewport(), m_pageFormat, m_templateScale, m_cover.colorIndex, m_cover.iconIndex,
        m_customBackground);
    if (!serializeDocumentToFile(path, m_pages, m_currentPageIndex, settings, newFile))
        return false;
    m_filePath = path;
    m_savedRevision = m_revision;
    updateCoverThumbnailForPages(path, m_pages, m_themeId, m_defaultTemplate, m_pageFormat,
                                 m_templateScale, m_customBackground);
    return true;
}

bool NotebookDocument::loadFromFile(const QString& path, DocumentToolSettings* outToolSettings,
                                    bool isAutosaveRestore) {
    const QJsonObject root = loadDocumentFromFile(path);
    if (root.isEmpty())
        return false;

    QVector<PageData> loadedPages;
    QHash<QString, QVector<ImageItem>> pdfSources;
    if (root.value("version").toInt() == 5) {
        const QJsonArray arr = root.value("pages").toArray();
        loadedPages.reserve(arr.size());
        for (const auto& value : arr)
            loadedPages.append(deserializePage(value.toObject(), pdfSources));
    } else {
        loadedPages.append(deserializePage(root, pdfSources));
    }
    if (loadedPages.isEmpty())
        loadedPages.append(PageData{});

    m_pages = std::move(loadedPages);
    m_currentPageIndex = std::clamp(root.value("currentPageIndex").toInt(), 0, int(m_pages.size()) - 1);
    m_undoStack.clear();
    m_redoStack.clear();
    ++m_sessionEpoch;

    if (outToolSettings) {
        const int shapeKind = root.value("shapeKind").toInt(int(ShapeKind::Triangle));
        if (shapeKind >= 0 && shapeKind <= int(ShapeKind::AngleMarker)
            && shapeKind != int(ShapeKind::ShadedRegion)) {
            outToolSettings->shapeKind = ShapeKind(shapeKind);
        }
        outToolSettings->holdToShapeEnabled = root.value("holdToShapeEnabled").toBool(true);
        outToolSettings->smartGuidesEnabled = root.value("smartGuidesEnabled").toBool(true);
        outToolSettings->swapPenButtons = root.value("swapPenButtons").toBool(false);
        const int profile = root.value("pressureProfile").toInt(int(PressureProfile::Light));
        if (profile >= int(PressureProfile::Light) && profile <= int(PressureProfile::Firm))
            outToolSettings->pressureProfile = PressureProfile(profile);
    }

    m_pageFormat = pageFormatFromInt(root.value("pageFormat").toInt(int(PageFormat::Infinite)));
    const int theme = root.value("theme").toInt(0);
    if (theme >= 0 && theme <= int(ThemeId::PaperCream))
        m_themeId = ThemeId(theme);
    const int tpl = root.value("template").toInt(int(BackgroundTemplate::Dots));
    if (tpl >= 0 && tpl <= int(BackgroundTemplate::School))
        m_defaultTemplate = BackgroundTemplate(tpl);
    m_templateScale = clampTemplateScale(root.value("templateScale").toDouble(1.0));
    const int storedCover = root.value("coverColor").toInt(-1);
    m_cover.colorIndex = (storedCover >= 0 && storedCover < 8) ? storedCover : -1;
    m_cover.iconIndex = std::clamp(root.value("coverIcon").toInt(0), 0, 7);
    const QString customBgStr = root.value("customBackground").toString();
    const QColor customBg = customBgStr.isEmpty() ? QColor() : QColor(customBgStr);
    m_customBackground = customBg.isValid() ? customBg : QColor();

    m_filePath = path;
    bool dirtyAfterLoad = false;
    if (isAutosaveRestore && root.contains("sessionFilePath")) {
        m_filePath = root.value("sessionFilePath").toString();
        dirtyAfterLoad = root.value("sessionDirty").toBool(false);
    }
    m_revision = dirtyAfterLoad ? 1 : 0;
    m_savedRevision = 0;
    return true;
}

bool NotebookDocument::loadAutosave(DocumentToolSettings* outToolSettings) {
    const QString autosaveFile = autosavePath();
    if (!loadFromFile(autosaveFile, outToolSettings, true))
        return false;
    if (m_filePath == autosaveFile || (!m_filePath.isEmpty() && !QFileInfo::exists(m_filePath))) {
        m_filePath.clear();
        m_revision = std::max<uint64_t>(1, m_revision);
        m_savedRevision = 0;
    }
    return true;
}

NotebookDocument::AutosaveJob NotebookDocument::prepareAutosave(
    const DocumentToolSettings& toolSettings,
    const std::optional<DocumentSnapshot>& currentPageOverride,
    bool renderCoverThumb) const {
    AutosaveJob job;
    job.autosaveFile = autosavePath();
    job.notebookFile = m_filePath;
    job.revision = m_revision;
    job.sessionEpoch = m_sessionEpoch;
    job.pages = m_pages;
    if (job.pages.isEmpty())
        job.pages.append(PageData{});
    job.currentPage = std::clamp(m_currentPageIndex, 0, int(job.pages.size()) - 1);
    if (currentPageOverride.has_value()) {
        auto& active = job.pages[job.currentPage];
        active.strokes = currentPageOverride->strokes;
        active.shapes = currentPageOverride->shapes;
        active.images = currentPageOverride->images;
        active.texts = currentPageOverride->texts;
    }
    job.writeNotebook = !job.notebookFile.isEmpty() && isDirty();
    job.sessionDirty = isDirty() && !job.writeNotebook;
    job.settings = documentSettings(toolSettings.shapeKind, toolSettings.holdToShapeEnabled,
                                    toolSettings.smartGuidesEnabled, toolSettings.swapPenButtons,
                                    toolSettings.pressureProfile, m_themeId, m_defaultTemplate,
                                    job.pages.at(job.currentPage).viewport, m_pageFormat,
                                    m_templateScale, m_cover.colorIndex, m_cover.iconIndex,
                                    m_customBackground);
    if (renderCoverThumb && job.writeNotebook && !job.pages.isEmpty()) {
        const auto& first = job.pages.first();
        if (pageHasVisibleContent(first))
            job.coverThumb = renderPageThumbnail(first, m_themeId, m_defaultTemplate,
                                                 m_pageFormat, kCoverThumbSize, m_templateScale,
                                                 m_customBackground);
    }
    return job;
}

bool NotebookDocument::executeAutosaveJob(AutosaveJob job) {
    if (!job.autosaveFile.isEmpty())
        QDir().mkpath(QFileInfo(job.autosaveFile).absolutePath());

    bool notebookSaved = true;
    if (job.writeNotebook) {
        notebookSaved = serializeDocumentToFile(job.notebookFile, job.pages, job.currentPage, job.settings);
        if (notebookSaved)
            saveCoverThumbnail(job.notebookFile, job.coverThumb);
    }

    QJsonObject sessionSettings = job.settings;
    sessionSettings.insert("sessionFilePath", job.notebookFile);
    sessionSettings.insert("sessionDirty", notebookSaved ? job.sessionDirty : true);
    if (!job.autosaveFile.isEmpty())
        serializeDocumentToFile(job.autosaveFile, job.pages, job.currentPage, sessionSettings);
    return notebookSaved;
}

NotebookSession NotebookDocument::exportSession() const {
    NotebookSession session;
    session.filePath = m_filePath;
    session.pages = m_pages;
    if (session.pages.isEmpty())
        session.pages.append(PageData{});
    session.currentPageIndex = std::clamp(m_currentPageIndex, 0, int(session.pages.size()) - 1);
    session.themeId = m_themeId;
    session.tpl = m_defaultTemplate;
    session.pageFormat = m_pageFormat;
    session.templateScale = m_templateScale;
    session.cover = m_cover;
    session.customBackground = m_customBackground;
    session.dirty = isDirty();
    return session;
}

void NotebookDocument::importSession(const NotebookSession& session) {
    m_pages = session.pages;
    ensureValidState();
    m_currentPageIndex = std::clamp(session.currentPageIndex, 0, int(m_pages.size()) - 1);
    m_themeId = session.themeId;
    m_defaultTemplate = session.tpl;
    m_pageFormat = session.pageFormat;
    m_templateScale = clampTemplateScale(session.templateScale);
    m_cover = session.cover;
    m_customBackground = session.customBackground.isValid() ? session.customBackground : QColor();
    m_filePath = session.filePath;
    m_undoStack.clear();
    m_redoStack.clear();
    m_revision = session.dirty ? 1 : 0;
    m_savedRevision = 0;
    ++m_sessionEpoch;
}

QImage NotebookDocument::pageThumbnail(int index, const QSize& size, double templateScale) const {
    if (size.isEmpty())
        return {};
    if (index < 0 || index >= m_pages.size()) {
        QImage blank(size * 2, QImage::Format_ARGB32_Premultiplied);
        blank.setDevicePixelRatio(2.0);
        blank.fill(effectiveThemeConfig(m_themeId, m_customBackground).background);
        return blank;
    }
    return renderPageThumbnail(m_pages.at(index), m_themeId, m_defaultTemplate,
                               m_pageFormat, size, templateScale, m_customBackground);
}

bool NotebookDocument::exportToPdf(const QString& path) const {
    return exportPagesToPdf(path, m_pages, effectiveThemeConfig(m_themeId, m_customBackground),
                              m_defaultTemplate, m_pageFormat, m_templateScale);
}

} // namespace finily
