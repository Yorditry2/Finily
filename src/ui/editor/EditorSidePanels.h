#pragma once

#include "engine/StickerLibrary.h"
#include "ui/editor/EditorTheme.h"

#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QVector>

class QScrollArea;

namespace finily {

class CanvasWidget;

// Panel lateral izquierdo/derecho de Elementos e inserción (286 px).
class EditorElementsPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorElementsPanel(CanvasWidget* canvas, QWidget* parent = nullptr);

    void setDropdownOpener(DropdownRequestFn fn);
    void setStickerStyle(StickerStyle style);
    StickerStyle stickerStyle() const { return m_style; }
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

signals:
    void closeRequested();
    void insertImageRequested();
    void importPdfRequested();
    void libraryRequested(const QString& section);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildCategories();
    void rebuildGrid();
    void layoutChildren();
    bool matchesCategory(const StickerTemplate& tpl) const;

    QPointer<CanvasWidget> m_canvas;
    DropdownRequestFn m_dropdownOpener;
    StickerStyle m_style = StickerStyle::CleanWithTitle;
    QString m_activeCategory = QStringLiteral("Todo");
    bool m_tablePickerOpen = false;
    int m_tableRows = 3;
    int m_tableCols = 4;

    EditorIconButton* m_closeBtn = nullptr;
    EditorSegControl* m_styleSeg = nullptr;
    EditorChipButton* m_imgBtn = nullptr;
    EditorChipButton* m_pdfBtn = nullptr;
    EditorChipButton* m_tableBtn = nullptr;

    QWidget* m_tableRow = nullptr;
    EditorIconButton* m_rowsMinus = nullptr;
    EditorIconButton* m_rowsPlus = nullptr;
    EditorIconButton* m_colsMinus = nullptr;
    EditorIconButton* m_colsPlus = nullptr;
    EditorChipButton* m_insertTableBtn = nullptr;

    EditorLineEdit* m_searchEdit = nullptr;
    QWidget* m_catRow = nullptr;
    QVector<EditorChipButton*> m_catChips;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_gridHost = nullptr;
    EditorChipButton* m_exploreBtn = nullptr;
};

// Panel lateral derecho de Páginas (164 px) con miniaturas en vivo.
class EditorPagesPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorPagesPanel(CanvasWidget* canvas, QWidget* parent = nullptr);

    void setDropdownOpener(DropdownRequestFn fn);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

signals:
    void closeRequested();
    void pageDeletedToastRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildThumbnails();

    QPointer<CanvasWidget> m_canvas;
    DropdownRequestFn m_dropdownOpener;
    EditorIconButton* m_closeBtn = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_listHost = nullptr;
    EditorChipButton* m_addPageBtn = nullptr;
};

} // namespace finily
