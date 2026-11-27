#pragma once

#include "core/Types.h"

#include <QImage>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>

namespace finily {

class NotebookDocument {
public:
    struct DocumentToolSettings {
        ShapeKind shapeKind = ShapeKind::Triangle;
        bool holdToShapeEnabled = true;
        bool smartGuidesEnabled = true;
        bool swapPenButtons = false;
        PressureProfile pressureProfile = PressureProfile::Light;
    };

    struct HistoryRestoreResult {
        bool applied = false;
        bool structureChanged = false;
        int pageIndex = 0;
    };

    struct AutosaveJob {
        QString autosaveFile;
        QString notebookFile;
        bool writeNotebook = false;
        uint64_t revision = 0;
        uint64_t sessionEpoch = 0;
        bool sessionDirty = false;
        QVector<PageData> pages;
        int currentPage = 0;
        QJsonObject settings;
        QImage coverThumb;
    };

    NotebookDocument();

    PageData& currentPage() { return m_pages[m_currentPageIndex]; }
    const PageData& currentPage() const { return m_pages[m_currentPageIndex]; }

    QVector<Stroke>& strokes() { return currentPage().strokes; }
    const QVector<Stroke>& strokes() const { return currentPage().strokes; }

    QVector<ShapeItem>& shapes() { return currentPage().shapes; }
    const QVector<ShapeItem>& shapes() const { return currentPage().shapes; }

    QVector<ImageItem>& images() { return currentPage().images; }
    const QVector<ImageItem>& images() const { return currentPage().images; }

    QVector<TextItem>& texts() { return currentPage().texts; }
    const QVector<TextItem>& texts() const { return currentPage().texts; }

    Viewport& viewport() { return currentPage().viewport; }
    const Viewport& viewport() const { return currentPage().viewport; }

    const PageData& page(int index) const { return m_pages.at(index); }
    const QVector<PageData>& pages() const { return m_pages; }
    int pageCount() const { return std::max(1, int(m_pages.size())); }
    int currentPageIndex() const { return m_currentPageIndex; }

    DocumentSnapshot currentSnapshot() const;
    void restoreSnapshot(const DocumentSnapshot& snapshot);

    ThemeId themeId() const { return m_themeId; }
    void setThemeId(ThemeId themeId);

    BackgroundTemplate defaultTemplate() const { return m_defaultTemplate; }
    BackgroundTemplate pageTemplate(int index) const;
    BackgroundTemplate currentTemplate() const;
    bool setBackgroundTemplate(BackgroundTemplate tpl);

    PageFormat pageFormat() const { return m_pageFormat; }
    void setPageFormat(PageFormat format);

    double templateScale() const { return m_templateScale; }
    void setTemplateScale(double scale);

    CoverStyle coverStyle() const { return m_cover; }
    void setCoverStyle(CoverStyle cover);

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }

    bool isDirty() const { return m_revision != m_savedRevision; }
    uint64_t revision() const { return m_revision; }
    uint64_t sessionEpoch() const { return m_sessionEpoch; }
    void markDirty() { ++m_revision; }
    void markSaved(uint64_t rev);

    bool currentPageHasContent() const;
    bool hasAnyContent() const;
    bool hasUnsavedChanges() const;
    int pdfPageCount() const;
    int documentPdfPageCount() const;

    bool goToPage(int index);
    bool insertPages(int index, const QVector<PageData>& pages);
    bool movePages(QVector<int> indices, int destination);
    bool duplicatePages(QVector<int> indices);
    QVector<PageData> copyPages(QVector<int> indices) const;
    bool deletePages(QVector<int> indices, const Viewport& blankFallbackViewport = {});
    int insertPdfPages(const QVector<ImageItem>& imported, bool replaceBlankIfEligible = true);
    bool clearCurrentPage();
    void resetNew(const Viewport& initialViewport = {});
    void configureBlank(ThemeId theme, BackgroundTemplate tpl, PageFormat format,
                        double templateScale, CoverStyle cover,
                        const Viewport& initialViewport = {});

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    void pushUndoSnapshot(const DocumentSnapshot& before);
    void pushPageHistory();
    HistoryRestoreResult undo();
    HistoryRestoreResult redo();

    bool createNotebook(const QString& path, ThemeId theme, BackgroundTemplate tpl,
                        PageFormat format, const Viewport& initialViewport,
                        const DocumentToolSettings& toolSettings,
                        double templateScale = 1.0, CoverStyle cover = {});
    bool saveToFile(const QString& path, const DocumentToolSettings& toolSettings,
                    bool newFile = false);
    bool loadFromFile(const QString& path, DocumentToolSettings* outToolSettings = nullptr,
                      bool isAutosaveRestore = false);
    bool loadAutosave(DocumentToolSettings* outToolSettings = nullptr);

    AutosaveJob prepareAutosave(const DocumentToolSettings& toolSettings,
                                const std::optional<DocumentSnapshot>& currentPageOverride = std::nullopt,
                                bool renderCoverThumb = true) const;
    static bool executeAutosaveJob(AutosaveJob job);

    NotebookSession exportSession() const;
    void importSession(const NotebookSession& session);

    QImage pageThumbnail(int index, const QSize& size, double templateScale = 1.0) const;
    bool exportToPdf(const QString& path) const;

private:
    struct HistoryEntry {
        int pageIndex = 0;
        DocumentSnapshot content;
        QVector<PageData> pages;
    };

    HistoryRestoreResult restoreHistoryEntry(QVector<HistoryEntry>& source,
                                             QVector<HistoryEntry>& destination);
    void ensureValidState();

    QVector<PageData> m_pages;
    int m_currentPageIndex = 0;
    QVector<HistoryEntry> m_undoStack;
    QVector<HistoryEntry> m_redoStack;
    QString m_filePath;
    uint64_t m_revision = 0;
    uint64_t m_savedRevision = 0;
    uint64_t m_sessionEpoch = 1;
    ThemeId m_themeId = ThemeId::DarkBoard;
    BackgroundTemplate m_defaultTemplate = BackgroundTemplate::Dots;
    PageFormat m_pageFormat = PageFormat::Infinite;
    double m_templateScale = 1.0;
    CoverStyle m_cover;
};

} // namespace finily
