#pragma once

#include "ui/home/HomeTheme.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace finily {

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
    void pressureSensitivityChanged(double value);
    void checkUpdatesRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QWidget* addGroup(QVBoxLayout* pane, const QString& title);
    void addRow(QWidget* group, const QString& title, const QString& desc, QWidget* control);

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    QWidget* m_nav = nullptr;
    QScrollArea* m_scroll = nullptr;
    QVector<QWidget*> m_sectionHeads;
    QVector<QWidget*> m_themed;
    QLabel* m_storageLabel = nullptr;
    QLabel* m_pathLabel = nullptr;
    QLabel* m_updatesLabel = nullptr;
    class ThemePicker* m_themePicker = nullptr;
    class SegmentedControl* m_pressureSeg = nullptr;
    class ToggleSwitch* m_autoCheck = nullptr;
};

} // namespace finily
