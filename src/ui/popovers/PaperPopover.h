#pragma once

#include "core/Types.h"
#include "ui/FloatingToolbarInternal.h"

#include <QPair>
#include <QPointer>
#include <QVector>

class QPushButton;
class QTimer;
class QToolButton;

namespace finily {

class CanvasWidget;

class PaperPopover : public GlassCapsule {
    Q_OBJECT

public:
    explicit PaperPopover(CanvasWidget* canvas, QWidget* parent = nullptr);
    ~PaperPopover() override;

    void setToolbarState(ToolbarDockPos dockPos, PageNavDirection pageNavDir);
    void disarmClearButton();
    void syncFromCanvas() override;
    void applyTheme(const ThemeConfig& theme, const QString& qss) override;

signals:
    void dockPositionRequested(finily::ToolbarDockPos pos);
    void pageNavDirectionRequested(finily::PageNavDirection dir);
    void closeRequested();
    void refreshToolbarRequested();

private:
    void onClearClicked();
    void updateToolbarButtons();

    QPointer<CanvasWidget> m_canvas;
    ThemeConfig m_theme = getThemeConfig(ThemeId::DarkBoard);
    ToolbarDockPos m_dockPos = ToolbarDockPos::Izquierda;
    PageNavDirection m_pageNavDir = PageNavDirection::Horizontal;

    QVector<QToolButton*> m_themeBtns;
    QVector<ThemeId> m_themeSpecs;
    QVector<QToolButton*> m_templateBtns;
    QVector<QPair<QToolButton*, BackgroundTemplate>> m_templateSpecs;
    QVector<QToolButton*> m_gridSizeBtns;
    QVector<double> m_gridSizeScales;
    QVector<QToolButton*> m_formatBtns;
    QVector<PageFormat> m_formatSpecs;

    QToolButton* m_dockLeftBtn = nullptr;
    QToolButton* m_dockTopBtn = nullptr;
    QToolButton* m_dockBottomBtn = nullptr;
    QToolButton* m_navHorizBtn = nullptr;
    QToolButton* m_navVertBtn = nullptr;

    QPushButton* m_clearBtn = nullptr;
    QTimer* m_clearTimer = nullptr;
    bool m_clearArmed = false;
};

} // namespace finily
