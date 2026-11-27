#include "ui/popovers/PaperPopover.h"

#include "ui/CanvasWidget.h"
#include "ui/ToolbarIcons.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace finily {

PaperPopover::PaperPopover(CanvasWidget* canvas, QWidget* parent)
    : GlassCapsule(parent)
    , m_canvas(canvas) {
    setObjectName(QStringLiteral("finilyPopover"));
    setCapsuleRadius(16);
    setFixedWidth(300);
    hide();

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    auto* themeGrid = new QGridLayout();
    themeGrid->setHorizontalSpacing(4);
    themeGrid->setVerticalSpacing(0);
    const QVector<ThemeId> themes = {
        ThemeId::DarkBoard, ThemeId::GreenBoard, ThemeId::PaperLight, ThemeId::PaperCream
    };
    for (int i = 0; i < themes.size(); ++i) {
        auto* button = new QToolButton(this);
        button->setFixedSize(56, 44);
        button->setIconSize(QSize(34, 34));
        const ThemeConfig tc = getThemeConfig(themes.at(i));
        QPixmap pm(68, 68);
        pm.fill(Qt::transparent);
        QPainter pp(&pm);
        pp.setRenderHint(QPainter::Antialiasing, true);
        pp.setPen(QPen(QColor(128, 128, 128, 110), 2));
        pp.setBrush(tc.background);
        pp.drawEllipse(QRectF(3, 3, 62, 62));
        pp.end();
        pm.setDevicePixelRatio(2.0);
        button->setIcon(QIcon(pm));
        button->setToolTip(tc.name);
        button->setCheckable(true);
        polishButton(button);
        const ThemeId theme = themes.at(i);
        connect(button, &QToolButton::clicked, this, [this, theme] {
            if (m_canvas)
                m_canvas->setThemeId(theme);
            emit refreshToolbarRequested();
        });
        m_themeBtns.append(button);
        m_themeSpecs.append(theme);
        themeGrid->addWidget(button, 0, i);
    }
    column->addLayout(themeGrid);

    auto* templateGrid = new QGridLayout();
    templateGrid->setHorizontalSpacing(4);
    templateGrid->setVerticalSpacing(4);
    const QVector<QPair<QString, BackgroundTemplate>> templates = {
        {QStringLiteral("Liso"), BackgroundTemplate::Blank},
        {QStringLiteral("Cuadrícula"), BackgroundTemplate::Grid},
        {QStringLiteral("Puntos"), BackgroundTemplate::Dots},
        {QStringLiteral("Rayado"), BackgroundTemplate::Ruled},
    };
    for (int i = 0; i < templates.size(); ++i) {
        auto* button = new QToolButton(this);
        button->setFixedSize(65, 36);
        button->setIconSize(QSize(40, 26));
        button->setCheckable(true);
        button->setToolTip(templates.at(i).first);
        polishButton(button);
        const BackgroundTemplate tpl = templates.at(i).second;
        button->setIcon(QIcon(templatePixmap(tpl, m_theme.uiText)));
        connect(button, &QToolButton::clicked, this, [this, tpl] {
            if (m_canvas)
                m_canvas->setBackgroundTemplate(tpl);
            emit refreshToolbarRequested();
        });
        m_templateBtns.append(button);
        m_templateSpecs.append(qMakePair(button, tpl));
        templateGrid->addWidget(button, 0, i);
    }
    column->addLayout(templateGrid);

    auto* gridSizeRow = new QHBoxLayout();
    gridSizeRow->setSpacing(4);
    const QVector<QPair<QString, double>> gridSizes = {
        {QStringLiteral("Chica"), 0.70},
        {QStringLiteral("Normal"), 1.00},
        {QStringLiteral("Grande"), 1.45},
    };
    for (const auto& spec : gridSizes) {
        auto* button = new QToolButton(this);
        button->setFixedSize(88, 28);
        button->setText(spec.first);
        button->setToolTip(QStringLiteral("Tamaño de cuadrícula/puntos/renglón: %1").arg(spec.first));
        button->setCheckable(true);
        polishButton(button);
        const double sc = spec.second;
        connect(button, &QToolButton::clicked, this, [this, sc] {
            if (m_canvas)
                m_canvas->setTemplateScale(sc);
            emit refreshToolbarRequested();
        });
        m_gridSizeBtns.append(button);
        m_gridSizeScales.append(sc);
        gridSizeRow->addWidget(button);
    }
    gridSizeRow->addStretch(1);
    column->addLayout(gridSizeRow);

    auto* formatRow = new QHBoxLayout();
    formatRow->setSpacing(4);
    const QVector<QPair<QString, PageFormat>> formats = {
        {QStringLiteral("Infinita"), PageFormat::Infinite},
        {QStringLiteral("A4 ↕"), PageFormat::A4},
        {QStringLiteral("A4 ↔"), PageFormat::A4Landscape},
        {QStringLiteral("16:9"), PageFormat::Slide16x9},
    };
    for (int i = 0; i < formats.size(); ++i) {
        auto* button = new QToolButton(this);
        button->setFixedSize(65, 30);
        button->setText(formats.at(i).first);
        button->setToolTip(i == 1 ? QStringLiteral("Hoja A4 vertical")
                         : i == 2 ? QStringLiteral("Hoja A4 horizontal")
                                  : formats.at(i).first);
        button->setCheckable(true);
        polishButton(button);
        const PageFormat format = formats.at(i).second;
        connect(button, &QToolButton::clicked, this, [this, format] {
            if (m_canvas)
                m_canvas->setPageFormat(format);
            emit refreshToolbarRequested();
        });
        m_formatBtns.append(button);
        m_formatSpecs.append(format);
        formatRow->addWidget(button);
    }
    column->addLayout(formatRow);

    auto* dockRow = new QHBoxLayout();
    dockRow->setSpacing(4);
    m_dockLeftBtn = new QToolButton(this);
    m_dockLeftBtn->setFixedSize(88, 30);
    m_dockLeftBtn->setText(QStringLiteral("Izquierda"));
    m_dockLeftBtn->setToolTip(QStringLiteral("Ancla el dock a la izquierda"));
    m_dockLeftBtn->setCheckable(true);
    polishButton(m_dockLeftBtn);
    connect(m_dockLeftBtn, &QToolButton::clicked, this, [this] {
        m_dockPos = ToolbarDockPos::Izquierda;
        updateToolbarButtons();
        emit dockPositionRequested(ToolbarDockPos::Izquierda);
    });
    dockRow->addWidget(m_dockLeftBtn);

    m_dockTopBtn = new QToolButton(this);
    m_dockTopBtn->setFixedSize(88, 30);
    m_dockTopBtn->setText(QStringLiteral("Arriba"));
    m_dockTopBtn->setToolTip(QStringLiteral("Ancla el dock arriba"));
    m_dockTopBtn->setCheckable(true);
    polishButton(m_dockTopBtn);
    connect(m_dockTopBtn, &QToolButton::clicked, this, [this] {
        m_dockPos = ToolbarDockPos::Arriba;
        updateToolbarButtons();
        emit dockPositionRequested(ToolbarDockPos::Arriba);
    });
    dockRow->addWidget(m_dockTopBtn);

    m_dockBottomBtn = new QToolButton(this);
    m_dockBottomBtn->setFixedSize(88, 30);
    m_dockBottomBtn->setText(QStringLiteral("Abajo"));
    m_dockBottomBtn->setToolTip(QStringLiteral("Ancla el dock abajo"));
    m_dockBottomBtn->setCheckable(true);
    polishButton(m_dockBottomBtn);
    connect(m_dockBottomBtn, &QToolButton::clicked, this, [this] {
        m_dockPos = ToolbarDockPos::Abajo;
        updateToolbarButtons();
        emit dockPositionRequested(ToolbarDockPos::Abajo);
    });
    dockRow->addWidget(m_dockBottomBtn);
    column->addLayout(dockRow);

    auto* navRow = new QHBoxLayout();
    navRow->setSpacing(4);
    m_navHorizBtn = new QToolButton(this);
    m_navHorizBtn->setFixedSize(134, 28);
    m_navHorizBtn->setText(QStringLiteral("↔ Pág. horiz."));
    m_navHorizBtn->setToolTip(QStringLiteral("Pasar páginas de izquierda a derecha (Re/Av Pág o Alt+←/→)"));
    m_navHorizBtn->setCheckable(true);
    polishButton(m_navHorizBtn);
    connect(m_navHorizBtn, &QToolButton::clicked, this, [this] {
        m_pageNavDir = PageNavDirection::Horizontal;
        updateToolbarButtons();
        emit pageNavDirectionRequested(PageNavDirection::Horizontal);
    });
    navRow->addWidget(m_navHorizBtn);

    m_navVertBtn = new QToolButton(this);
    m_navVertBtn->setFixedSize(134, 28);
    m_navVertBtn->setText(QStringLiteral("↕ Pág. vert."));
    m_navVertBtn->setToolTip(QStringLiteral("Pasar páginas de arriba a abajo (Re/Av Pág o Alt+↑/↓)"));
    m_navVertBtn->setCheckable(true);
    polishButton(m_navVertBtn);
    connect(m_navVertBtn, &QToolButton::clicked, this, [this] {
        m_pageNavDir = PageNavDirection::Vertical;
        updateToolbarButtons();
        emit pageNavDirectionRequested(PageNavDirection::Vertical);
    });
    navRow->addWidget(m_navVertBtn);
    column->addLayout(navRow);

    m_clearBtn = new QPushButton(QStringLiteral("Borrar anotaciones de esta página"), this);
    m_clearBtn->setFixedSize(272, 34);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setFocusPolicy(Qt::NoFocus);
    m_clearBtn->setToolTip(QStringLiteral("Borra las anotaciones de la página. Conserva el fondo PDF."));
    connect(m_clearBtn, &QPushButton::clicked, this, &PaperPopover::onClearClicked);
    column->addWidget(m_clearBtn);
    column->addStretch(1);

    m_clearTimer = new QTimer(this);
    m_clearTimer->setSingleShot(true);
    connect(m_clearTimer, &QTimer::timeout, this, &PaperPopover::disarmClearButton);
}

PaperPopover::~PaperPopover() = default;

void PaperPopover::setToolbarState(ToolbarDockPos dockPos, PageNavDirection pageNavDir) {
    m_dockPos = dockPos;
    m_pageNavDir = pageNavDir;
    updateToolbarButtons();
}

void PaperPopover::disarmClearButton() {
    m_clearArmed = false;
    if (m_clearTimer && m_clearTimer->isActive())
        m_clearTimer->stop();
    if (m_clearBtn) {
        m_clearBtn->setText(QStringLiteral("Borrar anotaciones de esta página"));
        m_clearBtn->setStyleSheet(QString());
    }
}

void PaperPopover::syncFromCanvas() {
    updateToolbarButtons();
    if (!m_canvas)
        return;

    const ThemeId activeTheme = m_canvas->themeId();
    for (int i = 0; i < m_themeBtns.size() && i < m_themeSpecs.size(); ++i)
        m_themeBtns.at(i)->setChecked(m_themeSpecs.at(i) == activeTheme);

    const BackgroundTemplate tpl = m_canvas->backgroundTemplate();
    for (int i = 0; i < m_templateSpecs.size(); ++i)
        m_templateSpecs.at(i).first->setChecked(m_templateSpecs.at(i).second == tpl);

    const PageFormat pageFormat = m_canvas->pageFormat();
    for (int i = 0; i < m_formatBtns.size() && i < m_formatSpecs.size(); ++i)
        m_formatBtns.at(i)->setChecked(m_formatSpecs.at(i) == pageFormat);

    const double tplScale = m_canvas->templateScale();
    for (int i = 0; i < m_gridSizeBtns.size() && i < m_gridSizeScales.size(); ++i)
        m_gridSizeBtns.at(i)->setChecked(std::abs(m_gridSizeScales.at(i) - tplScale) < 0.12);
}

void PaperPopover::applyTheme(const ThemeConfig& theme, const QString& qss) {
    m_theme = theme;
    GlassCapsule::applyTheme(theme, qss);
    for (const auto& spec : m_templateSpecs) {
        if (spec.first)
            spec.first->setIcon(QIcon(templatePixmap(spec.second, theme.uiText)));
    }
    for (QToolButton* b : findChildren<QToolButton*>(QStringLiteral("finilySwitch"))) {
        if (auto* sw = dynamic_cast<SwitchButton*>(b))
            sw->setColors(theme.uiText, theme.accent, theme.isDark);
    }
    disarmClearButton();
}

void PaperPopover::onClearClicked() {
    if (!m_clearBtn)
        return;
    if (!m_clearArmed) {
        m_clearArmed = true;
        m_clearBtn->setText(QStringLiteral("¿Borrar anotaciones? Pulsa de nuevo"));
        m_clearBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: rgba(239, 68, 68, 70); color: #fecaca;"
            " border-radius: 14px; }"));
        if (m_clearTimer)
            m_clearTimer->start(3000);
        return;
    }
    disarmClearButton();
    if (m_canvas)
        m_canvas->clearBoard();
    emit closeRequested();
    emit refreshToolbarRequested();
}

void PaperPopover::updateToolbarButtons() {
    if (m_dockLeftBtn && m_dockTopBtn && m_dockBottomBtn) {
        m_dockLeftBtn->setChecked(m_dockPos == ToolbarDockPos::Izquierda);
        m_dockTopBtn->setChecked(m_dockPos == ToolbarDockPos::Arriba);
        m_dockBottomBtn->setChecked(m_dockPos == ToolbarDockPos::Abajo);
    }
    if (m_navHorizBtn && m_navVertBtn) {
        m_navHorizBtn->setChecked(m_pageNavDir == PageNavDirection::Horizontal);
        m_navVertBtn->setChecked(m_pageNavDir == PageNavDirection::Vertical);
    }
}

} // namespace finily
