#pragma once

#include "ui/home/HomeTheme.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace finily {

class AccentPicker;
class CurvePreviewWidget;
class PenTestCanvas;
class SegmentedControl;
class StorageMeterWidget;
class ThemePicker;
class ToggleSwitch;

// Ajustes básicos (mockup 10): ocupan el área de la biblioteca, con índice de
// secciones a la izquierda. Solo opciones que ya funcionan; todo se aplica al
// instante, sin botón «Guardar».
class SettingsView : public QWidget {
    Q_OBJECT

public:
    enum ThemeMode { Light = 0, Dark = 1, System = 2 };

    explicit SettingsView(QWidget* parent = nullptr);
    void applyTheme(const HomePalette& pal, bool dark);
    // Relee valores (tema, presión, espacio usado…) al abrir la vista.
    void reload();
    void showSection(int index);

    static int themeMode();
    static void setThemeMode(int mode);

signals:
    void themeModeChanged(int mode);
    void appearanceChanged();
    void homePrefsChanged();
    void pressureSensitivityChanged(double value);
    void palmRejectionChanged(bool enabled);
    void libraryDirChanged();
    void checkUpdatesRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* addGroup(QVBoxLayout* pane, const QString& title);
    void addRow(QWidget* group, const QString& title, const QString& desc, QWidget* control);
    void refreshStorageInfo();
    void refreshLocalTheme();
    void updateActiveNav(int active);

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    int m_pinnedSection = -1;

    QWidget* m_nav = nullptr;
    QScrollArea* m_scroll = nullptr;
    QVector<QWidget*> m_sectionHeads;
    QVector<QWidget*> m_themed;

    QLabel* m_pathLabel = nullptr;
    QLabel* m_updatesLabel = nullptr;

    ThemePicker* m_themePicker = nullptr;
    AccentPicker* m_accentPicker = nullptr;
    ToggleSwitch* m_reducedMotion = nullptr;

    SegmentedControl* m_startupSeg = nullptr;
    ToggleSwitch* m_showDates = nullptr;
    ToggleSwitch* m_foldersFirst = nullptr;
    ToggleSwitch* m_openInNewTab = nullptr;

    SegmentedControl* m_pressureSeg = nullptr;
    CurvePreviewWidget* m_curvePreview = nullptr;
    PenTestCanvas* m_penTest = nullptr;
    ToggleSwitch* m_palmRejection = nullptr;

    StorageMeterWidget* m_storageMeter = nullptr;
    SegmentedControl* m_retentionSeg = nullptr;
    ToggleSwitch* m_autoCheck = nullptr;
};

} // namespace finily
