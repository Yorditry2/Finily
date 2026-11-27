#pragma once

#include "ui/FloatingToolbarInternal.h"

#include <QPair>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QVector>

class QListWidget;
class QTimer;
class QToolButton;

namespace finily {

class CanvasWidget;

class ThumbnailPanel : public GlassCapsule {
    Q_OBJECT

public:
    explicit ThumbnailPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    ~ThumbnailPanel() override;

    void rebuild();
    void refreshCurrentThumbnail();
    void applyTheme(const ThemeConfig& theme, const QString& qss) override;
    void syncFromCanvas() override;

signals:
    void closePopoversRequested();
    void refreshToolbarRequested();
    void importPdfRequested();
    void exportPdfRequested();

private:
    void showPageMenu(const QPoint& globalPosition, int clickedRow = -1);

    QPointer<CanvasWidget> m_canvas;
    QListWidget* m_list = nullptr;
    QToolButton* m_plusBtn = nullptr;
    QToolButton* m_actionsBtn = nullptr;
    QVector<QPair<QToolButton*, QString>> m_iconSpecs;
    QTimer* m_thumbTimer = nullptr;
    int m_thumbCurrent = -1;
};

} // namespace finily
