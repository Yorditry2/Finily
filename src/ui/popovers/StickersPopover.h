#pragma once

#include "core/Types.h"
#include "engine/StickerLibrary.h"
#include "ui/FloatingToolbarInternal.h"

#include <QPair>
#include <QPointer>
#include <QString>
#include <QVector>

class QGridLayout;
class QLineEdit;
class QToolButton;
class QWidget;

namespace finily {

class CanvasWidget;

class StickersPopover : public GlassCapsule {
    Q_OBJECT

public:
    explicit StickersPopover(CanvasWidget* canvas, QWidget* parent = nullptr);
    ~StickersPopover() override;

    void onOpened();
    void rebuildGrid();
    void applyTheme(const ThemeConfig& theme, const QString& qss) override;

signals:
    void closeRequested();
    void refreshToolbarRequested();
    void toastRequested(const QString& message);

private:
    QPointer<CanvasWidget> m_canvas;
    ThemeConfig m_theme = getThemeConfig(ThemeId::DarkBoard);
    QLineEdit* m_searchEdit = nullptr;
    QWidget* m_gridHost = nullptr;
    QGridLayout* m_grid = nullptr;
    QVector<QToolButton*> m_catBtns;
    QVector<QToolButton*> m_styleBtns;
    QVector<QPair<QToolButton*, QString>> m_iconSpecs;
    QString m_activeCategory = QStringLiteral("Todos");
    StickerStyle m_stickerStyle = StickerStyle::CleanWithTitle;
};

} // namespace finily
