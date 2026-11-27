#pragma once

#include "core/Types.h"
#include "engine/StickerLibrary.h"
#include "ui/editor/EditorTheme.h"

#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QNetworkAccessManager;
class QScrollArea;
class QVBoxLayout;

namespace finily {

class CanvasWidget;

// Diagrama vectorial monocromático del lápiz Wacom que refleja acciones e «Invertir» en vivo.
class WacomStylusDiagram : public QWidget {
    Q_OBJECT

public:
    explicit WacomStylusDiagram(QWidget* parent = nullptr);

    void setConfig(TabletAction lower, TabletAction upper, bool invert,
                   bool rearEraser, int cursorStyle);
    void applyEditorTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;

private:
    TabletAction m_lower = TabletAction::RightClickMenu;
    TabletAction m_upper = TabletAction::HoldEraser;
    bool m_invert = false;
    bool m_rearEraser = true;
    int m_cursorStyle = 0;
    bool m_dark = false;
    EditorPalette m_pal;
};

// Modal global de ⚙ Ajustes del editor (690x560, 9 secciones).
class EditorSettingsModal : public QWidget {
    Q_OBJECT

public:
    explicit EditorSettingsModal(QWidget* parent = nullptr);

    void setDropdownOpener(DropdownRequestFn fn);
    void openSection(const QString& sectionKey = QStringLiteral("escritura"));
    void syncFromCanvas(CanvasWidget* canvas, ToolbarDockMode dockMode, int pageNavDir);
    void applyEditorTheme(bool dark);

signals:
    void dockModeChanged(ToolbarDockMode mode);
    void elementsSideChanged(bool rightSide);
    void defaultStickerStyleChanged(int style);
    void pageNavDirectionChanged(int dir);
    void padActionsChanged();
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;

private:
    void buildSectionContent();
    void centerCard();
    void updateNavStyles();

    QPointer<CanvasWidget> m_canvas;
    DropdownRequestFn m_dropdownOpener;
    ToolbarDockMode m_dockMode = ToolbarDockMode::Top;
    int m_pageNavDir = 0;
    bool m_dark = false;
    EditorPalette m_pal;

    QString m_currentSection = QStringLiteral("escritura");
    EditorGlassCard* m_card = nullptr;
    QWidget* m_navHost = nullptr;
    QVector<EditorChipButton*> m_navBtns;
    QVector<QString> m_navKeys;
    EditorIconButton* m_closeBtn = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_bodyHost = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
};

// Modal de Biblioteca (690x560): Escritura, Figuras, Elementos, Paletas, Atajos 1-5, Descargados.
class EditorLibraryModal : public QWidget {
    Q_OBJECT

public:
    explicit EditorLibraryModal(QWidget* parent = nullptr);

    void openSection(const QString& section = QStringLiteral("escritura"));
    void setPresets(const QVector<PenPreset>& presets);
    void syncFromCanvas(CanvasWidget* canvas);
    void applyEditorTheme(bool dark);

signals:
    void savePresetRequested(int index);
    void selectPresetRequested(int index);
    void penTipSelected(int tip);
    void solidSelected(int solidKind);
    void paletteApplied(ToolType tool, const QVector<QColor>& colors);
    void stickerSelected(const QString& stickerId);
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;

private:
    void buildSectionContent();
    void centerCard();
    void updateNavStyles();

    QPointer<CanvasWidget> m_canvas;
    QVector<PenPreset> m_presets;
    bool m_dark = false;
    EditorPalette m_pal;

    QString m_currentSection = QStringLiteral("escritura");
    EditorGlassCard* m_card = nullptr;
    QWidget* m_navHost = nullptr;
    QVector<EditorChipButton*> m_navBtns;
    QVector<QString> m_navKeys;
    EditorIconButton* m_closeBtn = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_bodyHost = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
};

} // namespace finily
