#pragma once

#include "ui/FloatingToolbarInternal.h"

#include <QPair>
#include <QPointer>
#include <QString>
#include <QVector>

class QHBoxLayout;
class QLabel;
class QSlider;
class QToolButton;
class QWidget;

namespace finily {

class CanvasWidget;

class FilePopover : public GlassCapsule {
    Q_OBJECT

public:
    explicit FilePopover(CanvasWidget* canvas, QWidget* parent = nullptr);

    void rememberRecentImage(const QString& path);
    void setUpdateAvailable(const QString& version);
    void syncFromCanvas() override;
    void applyTheme(const ThemeConfig& theme, const QString& qss) override;

signals:
    void newNotebookRequested();
    void newTabRequested();
    void openNotebookRequested();
    void saveNotebookRequested();
    void saveNotebookAsRequested();
    void importPdfRequested();
    void exportPdfRequested();
    void configureAiApiRequested();
    void checkForUpdatesRequested();
    void layoutAdjusted();
    void closeRequested();
    void refreshToolbarRequested();

private:
    void rebuildRecentGallery();

    QPointer<CanvasWidget> m_canvas;
    bool m_updatingUi = false;

    QToolButton* m_newNotebookBtn = nullptr;
    QToolButton* m_openNotebookBtn = nullptr;
    QToolButton* m_saveNotebookBtn = nullptr;
    QToolButton* m_saveNotebookAsBtn = nullptr;
    QToolButton* m_importPdfBtn = nullptr;
    QToolButton* m_importImageBtn = nullptr;
    QToolButton* m_pasteBtn = nullptr;
    QWidget* m_recentGalleryWrap = nullptr;
    QHBoxLayout* m_recentGalleryRow = nullptr;
    QToolButton* m_exportBtn = nullptr;
    QLabel* m_pdfInfoLabel = nullptr;

    QLabel* m_pressureLabel = nullptr;
    QSlider* m_pressureSlider = nullptr;
    QToolButton* m_swapButtonsBtn = nullptr;
    QToolButton* m_mathBeautifyBtn = nullptr;
    QToolButton* m_holdToShapeBtn = nullptr;
    QToolButton* m_smartGuidesBtn = nullptr;
    QVector<QToolButton*> m_cursorBtns;
    QToolButton* m_updateBtn = nullptr;
    QVector<QPair<QToolButton*, QString>> m_iconSpecs;
};

} // namespace finily
