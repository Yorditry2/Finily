#include "ui/FloatingToolbar.h"
#include "ui/FloatingToolbarInternal.h"
#include "ui/CanvasWidget.h"
#include "engine/StickerLibrary.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {
namespace {

QString normalizeSearchText(const QString& input) {
    const QString decomposed = input.normalized(QString::NormalizationForm_D).toLower();
    QString out;
    out.reserve(decomposed.size());
    for (const QChar& ch : decomposed) {
        if (ch.category() != QChar::Mark_NonSpacing)
            out.append(ch);
    }
    return out;
}

} // namespace

void FloatingToolbar::buildStrokePopover() {
    // Popover hijo normal del overlay (NO Qt::Popup): en Hyprland/Wayland las
    // ventanas Qt::Popup se malposicionan y pierden entrada del lápiz. Como hijo,
    // vive en el espacio de coordenadas del overlay y su geometría entra en la
    // máscara de eventos de updateOverlayMask().
    m_strokePopover = new GlassCapsule(this);
    m_strokePopover->setObjectName("finilyPopover");
    m_strokePopover->setFixedWidth(300);
    m_strokePopover->hide();

    auto* column = new QVBoxLayout(m_strokePopover);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    // --- Estilo del trazo (color, grosor, patrón); oculto con el borrador activo ---
    m_strokeStyleSection = new QWidget(m_strokePopover);
    auto* styleColumn = new QVBoxLayout(m_strokeStyleSection);
    styleColumn->setContentsMargins(0, 0, 0, 0);
    styleColumn->setSpacing(8);

    // --- Tipos de lápiz ilustrados (sin texto, solo puntas reales + tooltip) ---
    m_penTipSection = new QWidget(m_strokeStyleSection);
    auto* penTipRow = new QHBoxLayout(m_penTipSection);
    penTipRow->setContentsMargins(0, 0, 0, 0);
    penTipRow->setSpacing(6);
    const QStringList penTipNames = {"Bolígrafo", "Pluma fuente", "Lápiz grafito", "Tiza"};
    for (int i = 0; i < penTipNames.size(); ++i) {
        auto* btn = new QToolButton(m_penTipSection);
        btn->setFixedSize(63, 44);
        btn->setIconSize(QSize(58, 38));
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setToolTip(penTipNames[i]);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            if (m_canvas)
                m_canvas->setPenTip(i);
            refreshFromCanvas();
        });
        m_penTipBtns.append(btn);
        penTipRow->addWidget(btn);
    }
    styleColumn->addWidget(m_penTipSection);

    // --- Tipos de resaltador minimalistas (2 modos: Libre / Recto) ---
    m_hlTipSection = new QWidget(m_strokeStyleSection);
    auto* hlTipRow = new QHBoxLayout(m_hlTipSection);
    hlTipRow->setContentsMargins(0, 0, 0, 0);
    hlTipRow->setSpacing(6);
    const QStringList hlTipNames = {"Resaltador libre (mantén para recto)", "Subrayado recto"};
    const int hlTipIds[2] = {0, 2};
    for (int i = 0; i < 2; ++i) {
        auto* btn = new QToolButton(m_hlTipSection);
        btn->setFixedSize(132, 40);
        btn->setIconSize(QSize(72, 34));
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setToolTip(hlTipNames[i]);
        btn->setCheckable(true);
        polishButton(btn);
        const int tipId = hlTipIds[i];
        connect(btn, &QToolButton::clicked, this, [this, tipId] {
            if (m_canvas)
                m_canvas->setHighlighterTip(tipId);
            refreshFromCanvas();
        });
        m_hlTipBtns.append(btn);
        hlTipRow->addWidget(btn);
    }
    styleColumn->addWidget(m_hlTipSection);

    // --- Modo de Lazo minimalista (solo íconos: Lazo libre / Rectangular) ---
    m_lassoModeSection = new QWidget(m_strokePopover);
    auto* lassoRow = new QHBoxLayout(m_lassoModeSection);
    lassoRow->setContentsMargins(0, 0, 0, 0);
    lassoRow->setSpacing(6);
    m_lassoFreeBtn = new QToolButton(m_lassoModeSection);
    m_lassoFreeBtn->setFixedSize(133, 38);
    m_lassoFreeBtn->setIconSize(QSize(20, 20));
    m_lassoFreeBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_lassoFreeBtn->setToolTip("Lazo libre");
    m_lassoFreeBtn->setCheckable(true);
    polishButton(m_lassoFreeBtn);
    m_ctxIconSpecs.append(qMakePair(m_lassoFreeBtn, QStringLiteral("lasso-free")));
    connect(m_lassoFreeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) m_canvas->setLassoRectMode(false);
        refreshFromCanvas();
        closePopovers();
    });
    m_lassoRectBtn = new QToolButton(m_lassoModeSection);
    m_lassoRectBtn->setFixedSize(133, 38);
    m_lassoRectBtn->setIconSize(QSize(20, 20));
    m_lassoRectBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_lassoRectBtn->setToolTip("Selección rectangular");
    m_lassoRectBtn->setCheckable(true);
    polishButton(m_lassoRectBtn);
    m_ctxIconSpecs.append(qMakePair(m_lassoRectBtn, QStringLiteral("lasso-rect")));
    connect(m_lassoRectBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) m_canvas->setLassoRectMode(true);
        refreshFromCanvas();
        closePopovers();
    });
    lassoRow->addWidget(m_lassoFreeBtn);
    lassoRow->addWidget(m_lassoRectBtn);
    column->addWidget(m_lassoModeSection);

    auto* swatchRow = new QHBoxLayout();
    swatchRow->setSpacing(6);
    for (int i = 0; i < 7; ++i) {
        auto* swatch = new ColorDotButton(m_strokeStyleSection);
        polishButton(swatch);
        connect(swatch, &QToolButton::clicked, this, [this, i] {
            if (i < m_palette.size())
                setColor(m_palette.at(i));
        });
        m_swatches.append(swatch);
        swatchRow->addWidget(swatch);
    }
    m_customColorBtn = new QToolButton(m_strokeStyleSection);
    m_customColorBtn->setFixedSize(24, 24);
    m_customColorBtn->setText("+");
    m_customColorBtn->setToolTip("Color personalizado…");
    polishButton(m_customColorBtn);
    connect(m_customColorBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        const QColor chosen = QColorDialog::getColor(m_canvas->inkColor(), window(), "Color personalizado");
        if (chosen.isValid()) {
            if (!m_palette.isEmpty())
                m_palette.last() = chosen;
            setColor(chosen);
            refreshFromCanvas();
        }
    });
    swatchRow->addWidget(m_customColorBtn);
    swatchRow->addStretch(1);
    styleColumn->addLayout(swatchRow);

    m_fillStyleSection = new QWidget(m_strokeStyleSection);
    auto* fillLayout = new QGridLayout(m_fillStyleSection);
    fillLayout->setContentsMargins(0, 0, 0, 0);
    fillLayout->setSpacing(6);
    auto* fillGroup = new QButtonGroup(m_fillStyleSection);
    const QPair<ShadePattern, QString> fills[] = {
        {ShadePattern::Solid, tr("Color sólido")},
        {ShadePattern::Soft, tr("Tono suave")},
        {ShadePattern::Diagonal, tr("Rayado diagonal")},
        {ShadePattern::Crosshatch, tr("Rayado cruzado")},
        {ShadePattern::Grid, tr("Cuadrícula fina")},
        {ShadePattern::Dotted, tr("Punteado")},
    };
    for (const auto& [pattern, label] : fills) {
        auto* button = new QToolButton(m_fillStyleSection);
        button->setToolTip(label);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setFixedSize(86, 42);
        button->setIconSize(QSize(58, 36));
        button->setCheckable(true);
        polishButton(button);
        fillGroup->addButton(button);
        const auto index = m_fillStyles.size();
        fillLayout->addWidget(button, index / 3, index % 3);
        m_fillStyles.append({button, pattern});
        connect(button, &QToolButton::clicked, this, [this, pattern] {
            if (m_canvas)
                m_canvas->setShadePattern(pattern);
            refreshFromCanvas();
            closePopovers();
        });
    }
    styleColumn->addWidget(m_fillStyleSection);

    m_strokeGeometrySection = new QWidget(m_strokeStyleSection);
    auto* geometryColumn = new QVBoxLayout(m_strokeGeometrySection);
    geometryColumn->setContentsMargins(0, 0, 0, 0);
    geometryColumn->setSpacing(8);
    styleColumn->addWidget(m_strokeGeometrySection);

    // Grosor dividido en 10 puntos sobre la línea.
    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(10);
    m_sizeLabel = new QLabel("3 / 10", m_strokeStyleSection);
    m_sizeLabel->hide();
    m_strokePreview = new StrokePreviewWidget(m_strokeGeometrySection);
    sizeRow->addWidget(m_strokePreview);
    m_sizeSlider = new DotStepSlider(m_strokeGeometrySection);
    m_sizeSlider->setRange(1, 10);
    m_sizeSlider->setPageStep(1);
    m_sizeSlider->setSingleStep(1);
    m_sizeSlider->setToolTip("Grosor del trazo");
    sizeRow->addWidget(m_sizeSlider, 1);
    geometryColumn->addLayout(sizeRow);
    column->addWidget(m_strokeStyleSection);

    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_updatingUi || !m_canvas)
            return;
        const int idx = std::clamp(level - 1, 0, 9);
        const bool isHl = m_canvas->tool() == ToolType::Highlighter;
        const double px = isHl ? kHlLevels[idx] : kPenLevels[idx];
        m_canvas->setStrokeSize(px);
        m_sizeLabel->setText(QString("%1 / 10").arg(level));
        updateStrokePreview();
        syncActivePresetFromCanvas();
        updateStationery();
    });

    // --- Estabilizador minimalista con puntitos discretos sobre la línea ---
    m_stabilizerRow = new QWidget(m_strokePopover);
    auto* stabRow = new QHBoxLayout(m_stabilizerRow);
    stabRow->setContentsMargins(0, 2, 0, 0);
    stabRow->setSpacing(10);
    m_stabilizerIcon = new QLabel(m_stabilizerRow);
    m_stabilizerIcon->setFixedSize(28, 26);
    m_stabilizerIcon->setAlignment(Qt::AlignCenter);
    m_stabilizerIcon->setToolTip("Estabilizador de trazo");
    stabRow->addWidget(m_stabilizerIcon);
    m_stabilizerSlider = new DotStepSlider(m_stabilizerRow);
    m_stabilizerSlider->setRange(0, 9);
    m_stabilizerSlider->setSingleStep(1);
    m_stabilizerSlider->setPageStep(1);
    m_stabilizerSlider->setToolTip("Estabilizador de trazo");
    stabRow->addWidget(m_stabilizerSlider, 1);
    m_stabilizerLabel = new QLabel(m_stabilizerRow);
    m_stabilizerLabel->hide();
    connect(m_stabilizerSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setStabilizer(double(level) / m_stabilizerSlider->maximum());
    });
    column->addWidget(m_stabilizerRow);

    // --- Opacidad del resaltador (reemplaza al estabilizador cuando estás en Resaltador) ---
    m_hlOpacityRow = new QWidget(m_strokePopover);
    auto* hlOpRow = new QHBoxLayout(m_hlOpacityRow);
    hlOpRow->setContentsMargins(0, 2, 0, 0);
    hlOpRow->setSpacing(10);
    m_hlOpacityIcon = new QLabel(m_hlOpacityRow);
    m_hlOpacityIcon->setFixedSize(28, 26);
    m_hlOpacityIcon->setAlignment(Qt::AlignCenter);
    m_hlOpacityIcon->setToolTip("Transparencia del resaltador");
    hlOpRow->addWidget(m_hlOpacityIcon);
    m_hlOpacitySlider = new DotStepSlider(m_hlOpacityRow);
    m_hlOpacitySlider->setRange(1, 10);
    m_hlOpacitySlider->setSingleStep(1);
    m_hlOpacitySlider->setPageStep(1);
    m_hlOpacitySlider->setToolTip("Transparencia del resaltador (10% a 68%)");
    hlOpRow->addWidget(m_hlOpacitySlider, 1);
    connect(m_hlOpacitySlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_updatingUi || !m_canvas)
            return;
        const int idx = std::clamp(level - 1, 0, 9);
        m_canvas->setHighlighterOpacity(kHlOpacityLevels[idx]);
    });
    column->addWidget(m_hlOpacityRow);

    m_strokeBeautifyBtn = new SwitchButton(m_strokePopover);
    m_strokeBeautifyBtn->setFixedSize(272, 30);
    m_strokeBeautifyBtn->setText("Embellecer letra por palabra (M)");
    m_strokeBeautifyBtn->setToolTip("Al terminar de escribir cada palabra, suaviza y alinea tu letra automáticamente respetando tu unión y espaciado");
    m_strokeBeautifyBtn->setCheckable(true);
    m_strokeBeautifyBtn->setChecked(m_canvas && m_canvas->mathBeautifyEnabled());
    connect(m_strokeBeautifyBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_strokeBeautifyBtn)
            m_canvas->setMathBeautifyEnabled(m_strokeBeautifyBtn->isChecked());
        refreshFromCanvas();
    });
    column->addWidget(m_strokeBeautifyBtn);

    // --- Borrador minimalista (solo 2 íconos + grosor en 10 pasos) ---
    m_eraserSection = new QWidget(m_strokePopover);
    auto* eraserColumn = new QVBoxLayout(m_eraserSection);
    eraserColumn->setContentsMargins(0, 0, 0, 0);
    eraserColumn->setSpacing(8);
    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(6);
    m_eraserStrokeBtn = new QToolButton(m_eraserSection);
    m_eraserStrokeBtn->setFixedSize(133, 32);
    m_eraserStrokeBtn->setText("Por trazo");
    m_eraserStrokeBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_eraserStrokeBtn->setToolTip("Borra el trazo completo al tocarlo");
    m_eraserStrokeBtn->setCheckable(true);
    polishButton(m_eraserStrokeBtn);
    connect(m_eraserStrokeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setEraserMode(EraserMode::Stroke);
        refreshFromCanvas();
    });
    m_eraserPartialBtn = new QToolButton(m_eraserSection);
    m_eraserPartialBtn->setFixedSize(133, 32);
    m_eraserPartialBtn->setText("Parcial");
    m_eraserPartialBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_eraserPartialBtn->setToolTip("Recorta únicamente el tramo que tocas (mantén Shift para recortar limpio hasta intersecciones)");
    m_eraserPartialBtn->setCheckable(true);
    polishButton(m_eraserPartialBtn);
    connect(m_eraserPartialBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setEraserMode(EraserMode::Partial);
        refreshFromCanvas();
    });
    modeRow->addWidget(m_eraserStrokeBtn);
    modeRow->addWidget(m_eraserPartialBtn);
    eraserColumn->addLayout(modeRow);

    auto* erSizeRow = new QHBoxLayout();
    erSizeRow->setSpacing(10);
    m_eraserPreview = new StrokePreviewWidget(m_eraserSection);
    m_eraserPreview->setHollow(true);
    erSizeRow->addWidget(m_eraserPreview);
    m_eraserSizeSlider = new DotStepSlider(m_eraserSection);
    m_eraserSizeSlider->setRange(1, 10);
    m_eraserSizeSlider->setSingleStep(1);
    m_eraserSizeSlider->setPageStep(1);
    m_eraserSizeSlider->setToolTip("Tamaño del borrador");
    erSizeRow->addWidget(m_eraserSizeSlider, 1);
    m_eraserSizeLabel = new QLabel(m_eraserSection);
    m_eraserSizeLabel->hide();
    eraserColumn->addLayout(erSizeRow);
    connect(m_eraserSizeSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_eraserPreview) {
            QColor erCol = getThemeConfig(m_theme).uiText;
            erCol.setAlpha(185);
            m_eraserPreview->setInk(erCol, 4.0 + level * 1.8);
        }
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setEraserSize(level * 8.0);
    });

    auto* erFilterLbl = new QLabel("Qué borrar", m_eraserSection);
    erFilterLbl->setObjectName("finilySectionTitle");
    eraserColumn->addWidget(erFilterLbl);

    auto* erFilterRow = new QHBoxLayout();
    erFilterRow->setSpacing(6);
    const QStringList erFilterLabels = {"Todo", "Sin figuras", "Resaltador"};
    const QStringList erFilterTips = {
        "Borra tinta, figuras, ejes y texto",
        "Borra tinta y texto conservando figuras y ejes",
        "Solo borra trazos de resaltador"
    };
    for (int i = 0; i < 3; ++i) {
        auto* btn = new QToolButton(m_eraserSection);
        btn->setFixedSize(86, 28);
        btn->setText(erFilterLabels[i]);
        btn->setToolTip(erFilterTips[i]);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            if (!m_canvas) return;
            if (i == 0) {
                m_canvas->setEraserOnlyHighlight(false);
                m_canvas->setEraserPreserveShapes(false);
            } else if (i == 1) {
                m_canvas->setEraserOnlyHighlight(false);
                m_canvas->setEraserPreserveShapes(true);
            } else {
                m_canvas->setEraserOnlyHighlight(true);
            }
            refreshFromCanvas();
        });
        m_eraserFilterBtns.append(btn);
        erFilterRow->addWidget(btn);
    }
    eraserColumn->addLayout(erFilterRow);
    column->addWidget(m_eraserSection);
}

void FloatingToolbar::buildPaperPopover() {
    m_paperPopover = new GlassCapsule(this);
    m_paperPopover->setObjectName("finilyPopover");
    m_paperPopover->setFixedWidth(300);
    m_paperPopover->hide();

    auto* column = new QVBoxLayout(m_paperPopover);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    auto* themeGrid = new QGridLayout();
    themeGrid->setHorizontalSpacing(4);
    themeGrid->setVerticalSpacing(0);
    const QVector<ThemeId> themes = {ThemeId::DarkBoard, ThemeId::GreenBoard, ThemeId::PaperLight, ThemeId::PaperCream};
    for (int i = 0; i < themes.size(); ++i) {
        auto* button = new QToolButton(m_paperPopover);
        button->setFixedSize(56, 44);
        button->setIconSize(QSize(34, 34));
        {
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
        }
        button->setCheckable(true);
        polishButton(button);
        const ThemeId theme = themes.at(i);
        connect(button, &QToolButton::clicked, this, [this, theme] {
            if (m_canvas)
                m_canvas->setThemeId(theme);
            refreshFromCanvas();
        });
        m_themeBtns.append(button);
        m_themeSpecs.append(theme);
        themeGrid->addWidget(button, 0, i);
    }
    column->addLayout(themeGrid);

    auto* templateRow = new QHBoxLayout();
    templateRow->setSpacing(4);
    const QVector<QString> templateNames = {"Liso", "Cuadrícula", "Puntos", "Rayado"};
    const QVector<BackgroundTemplate> templates = {BackgroundTemplate::Blank, BackgroundTemplate::Grid,
                                                   BackgroundTemplate::Dots, BackgroundTemplate::Ruled};
    for (int i = 0; i < templates.size(); ++i) {
        auto* button = new QToolButton(m_paperPopover);
        button->setFixedSize(66, 40);
        button->setIconSize(QSize(44, 30));
        button->setCheckable(true);
        button->setToolTip(templateNames.at(i));
        polishButton(button);
        const BackgroundTemplate tpl = templates.at(i);
        connect(button, &QToolButton::clicked, this, [this, tpl] {
            if (m_canvas)
                m_canvas->setBackgroundTemplate(tpl);
            refreshFromCanvas();
        });
        m_templateBtns.append(button);
        m_templateSpecs.append(qMakePair(button, tpl));
        templateRow->addWidget(button);
    }
    templateRow->addStretch(1);
    column->addLayout(templateRow);

    auto* formatRow = new QHBoxLayout();
    formatRow->setSpacing(4);
    const QVector<QPair<QString, PageFormat>> formats = {
        {QStringLiteral("Infinita"), PageFormat::Infinite},
        {QStringLiteral("A4"), PageFormat::A4},
        {QStringLiteral("16:9"), PageFormat::Slide16x9},
    };
    for (int i = 0; i < formats.size(); ++i) {
        auto* button = new QToolButton(m_paperPopover);
        button->setFixedSize(88, 30);
        button->setText(formats.at(i).first);
        button->setCheckable(true);
        polishButton(button);
        const PageFormat format = formats.at(i).second;
        connect(button, &QToolButton::clicked, this, [this, format] {
            if (m_canvas)
                m_canvas->setPageFormat(format);
            refreshFromCanvas();
        });
        m_formatBtns.append(button);
        m_formatSpecs.append(format);
        formatRow->addWidget(button);
    }
    formatRow->addStretch(1);
    column->addLayout(formatRow);

    auto* dockRow = new QHBoxLayout();
    dockRow->setSpacing(4);
    m_dockLeftBtn = new QToolButton(m_paperPopover);
    m_dockLeftBtn->setFixedSize(88, 30);
    m_dockLeftBtn->setText("Izquierda");
    m_dockLeftBtn->setToolTip("Ancla el dock a la izquierda");
    m_dockLeftBtn->setCheckable(true);
    polishButton(m_dockLeftBtn);
    connect(m_dockLeftBtn, &QToolButton::clicked, this, [this] { setDockPosition(DockPos::Izquierda); });
    dockRow->addWidget(m_dockLeftBtn);
    m_dockTopBtn = new QToolButton(m_paperPopover);
    m_dockTopBtn->setFixedSize(88, 30);
    m_dockTopBtn->setText("Arriba");
    m_dockTopBtn->setToolTip("Ancla el dock arriba");
    m_dockTopBtn->setCheckable(true);
    polishButton(m_dockTopBtn);
    connect(m_dockTopBtn, &QToolButton::clicked, this, [this] { setDockPosition(DockPos::Arriba); });
    dockRow->addWidget(m_dockTopBtn);
    m_dockBottomBtn = new QToolButton(m_paperPopover);
    m_dockBottomBtn->setFixedSize(88, 30);
    m_dockBottomBtn->setText("Abajo");
    m_dockBottomBtn->setToolTip("Ancla el dock abajo");
    m_dockBottomBtn->setCheckable(true);
    polishButton(m_dockBottomBtn);
    connect(m_dockBottomBtn, &QToolButton::clicked, this, [this] { setDockPosition(DockPos::Abajo); });
    dockRow->addWidget(m_dockBottomBtn);
    column->addLayout(dockRow);

    m_clearBtn = new QPushButton("Borrar anotaciones de esta página", m_paperPopover);
    m_clearBtn->setFixedSize(272, 34);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setFocusPolicy(Qt::NoFocus);
    m_clearBtn->setToolTip("Borra las anotaciones de la página. Conserva el fondo PDF.");
    connect(m_clearBtn, &QPushButton::clicked, this, &FloatingToolbar::onClearClicked);
    column->addWidget(m_clearBtn);
    column->addStretch(1);
}

void FloatingToolbar::buildShapesPopover() {
    m_shapesPopover = new GlassCapsule(this);
    m_shapesPopover->setObjectName("finilyPopover");
    m_shapesPopover->setFixedWidth(344);
    m_shapesPopover->hide();

    auto* column = new QVBoxLayout(m_shapesPopover);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    struct ShapeSpec { QString name, glyph, tip; ShapeKind kind; bool dimension; int sides; int tri; int quad; int solid; QString mark; };
    const QVector<QPair<QString, QVector<ShapeSpec>>> pages = {
        {QStringLiteral("Líneas"), {
            {"Segmento", "shape-line", "Arrastra para dibujar. Mayús: ajusta el ángulo", ShapeKind::Line, false, 0, -1, 0, -1, {}},
            {"Curva", "shape-curve", "Curva con punto central editable", ShapeKind::Curve, false, 0, -1, 0, -1, {}},
            {"Arco circular", "shape-arc", "Traza un radio y ajusta sus extremos", ShapeKind::Arc, false, 0, -1, 0, -1, {}},
            {"Cota", "shape-dimension", "Línea de medida con topes y etiqueta editable", ShapeKind::Line, true, 0, -1, 0, -1, {}}}},
        {QStringLiteral("Formas"), {
            {"Óvalo / Círculo", "shape-ellipse", "Mayús para un círculo perfecto", ShapeKind::Ellipse, false, 0, -1, 0, -1, {}},
            {"Rectángulo", "shape-rect", "Con Mayús sale cuadrado perfecto", ShapeKind::Rectangle, false, 0, -1, 0, -1, {}},
            {"Sector circular", "shape-sector", "Traza un radio y ajusta la abertura", ShapeKind::Sector, false, 0, -1, 0, -1, {}},
            {"Rombo", "shape-poly-4", "Rombo con 4 vértices editables", ShapeKind::Polyline, false, 4, -1, 0, -1, {}},
            {"Paralelogramo", "shape-parallelogram", "Lados opuestos paralelos", ShapeKind::Polyline, false, 4, -1, 1, -1, {}},
            {"Trapecio isósceles", "shape-trap-iso", "Trapecio simétrico editable", ShapeKind::Polyline, false, 4, -1, 2, -1, {}},
            {"Trapecio rectángulo", "shape-trap-right", "Trapecio con 2 ángulos rectos", ShapeKind::Polyline, false, 4, -1, 3, -1, {}},
            {"Trapezoide", "shape-trapezoid", "Cuadrilátero libre de 4 vértices", ShapeKind::Polyline, false, 4, -1, 4, -1, {}},
            {"Triángulo isósceles", "shape-triangle", "Triángulo con vértice centrado", ShapeKind::Triangle, false, 0, 0, 0, -1, {}},
            {"Triángulo rectángulo", "shape-tri-right", "Triángulo rectángulo con cuadradito 90° automático", ShapeKind::Triangle, false, 0, 1, 0, -1, {}},
            {"Triángulo equilátero", "shape-tri-equi", "Tres lados iguales", ShapeKind::Triangle, false, 0, 3, 0, -1, {}},
            {"Pentágono", "shape-poly-5", "Polígono regular de 5 lados", ShapeKind::Polyline, false, 5, -1, 0, -1, {}},
            {"Hexágono", "shape-poly-6", "Polígono regular de 6 lados", ShapeKind::Polyline, false, 6, -1, 0, -1, {}},
            {"Heptágono", "shape-poly-7", "Polígono regular de 7 lados", ShapeKind::Polyline, false, 7, -1, 0, -1, {}},
            {"Octágono", "shape-poly-8", "Polígono regular de 8 lados", ShapeKind::Polyline, false, 8, -1, 0, -1, {}}}},
        {QStringLiteral("Sólidos"), {
            {"Cubo / Hexaedro", "shape-3d-cube", "Hexaedro con aristas ocultas punteadas", ShapeKind::Solid3D, false, 0, -1, 0, 0, {}},
            {"Prisma", "shape-3d-prism", "Prisma triangular recto", ShapeKind::Solid3D, false, 0, -1, 0, 1, {}},
            {"Cilindro", "shape-3d-cyl", "Cilindro circular recto", ShapeKind::Solid3D, false, 0, -1, 0, 2, {}},
            {"Cono", "shape-3d-cone", "Cono circular recto", ShapeKind::Solid3D, false, 0, -1, 0, 3, {}},
            {"Esfera", "shape-3d-sphere", "Esfera con ecuador en perspectiva", ShapeKind::Solid3D, false, 0, -1, 0, 4, {}},
            {"Pirámide", "shape-3d-pyr", "Pirámide cuadrangular", ShapeKind::Solid3D, false, 0, -1, 0, 5, {}},
            {"Tetraedro", "shape-3d-tetra", "Tetraedro 3D", ShapeKind::Solid3D, false, 0, -1, 0, 6, {}},
            {"Octaedro", "shape-3d-octa", "Octaedro regular 3D", ShapeKind::Solid3D, false, 0, -1, 0, 7, {}},
            {"Tronco de cono", "shape-3d-fcone", "Tronco de cono", ShapeKind::Solid3D, false, 0, -1, 0, 8, {}},
            {"Tronco de pirámide", "shape-3d-fpyr", "Tronco de pirámide cuadrangular", ShapeKind::Solid3D, false, 0, -1, 0, 9, {}}}},
        {QStringLiteral("Matemáticas"), {
            {"Ejes X/Y", "shape-axes", "Plano cartesiano con ejes X/Y", ShapeKind::Axes, false, 0, -1, 0, -1, {}},
            {"Raíz √", "shape-radical", "Raíz cuadrada auto-ajustable (R)", ShapeKind::Radical, false, 0, -1, 0, -1, {}},
            {"Ángulo", "shape-angle", "Dos lados y una medida automática editable", ShapeKind::AngleMarker, false, 0, -1, 0, -1, {}},
            {"Ángulo recto", "shape-rightangle", "Marca de ángulo de 90°", ShapeKind::RightAngle, false, 0, -1, 0, -1, {}},
            {"Llave", "shape-brace", "Llave { } para agrupar", ShapeKind::Brace, false, 0, -1, 0, -1, {}},
            {"Serrucho (congruencia)", "mark-zigzag", "Toca cualquier lado de una figura para imantar un serrucho de lados iguales", ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:zigzag")},
            {"Resorte (congruencia)", "mark-spring", "Toca cualquier lado de una figura para imantar un resorte/onda de lados iguales", ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:spring")},
            {"Bolita (congruencia)", "mark-dot", "Toca cualquier lado de una figura para imantar un punto relleno de lados iguales", ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:dot")},
            {"Tronquito (congruencia)", "mark-bar", "Toca cualquier lado de una figura para imantar un rectángulo delgado de lados iguales", ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:bar")},
            {"Rayitas || (congruencia)", "mark-ticks", "Toca un lado para imantar 1, 2 o 3 rayitas de congruencia (repite clic para ciclar)", ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:tick2")},
            {"Paralelas >>", "mark-parallel", "Toca un lado para imantar flechas de lados paralelos (repite clic para 1 o 2)", ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:parallel2")}}},
    };

    auto* tabsRow = new QHBoxLayout();
    tabsRow->setSpacing(2);
    auto* tabGroup = new QButtonGroup(m_shapesPopover);
    tabGroup->setExclusive(true);
    auto* stack = new QStackedWidget(m_shapesPopover);
    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        auto* tab = new QToolButton(m_shapesPopover);
        tab->setObjectName("finilyShapeBtn");
        tab->setText(pages.at(pageIndex).first);
        tab->setCheckable(true);
        tab->setChecked(pageIndex == 0);
        tab->setFixedHeight(26);
        tab->setCursor(Qt::PointingHandCursor);
        QFont tabFont = tab->font();
        tabFont.setPixelSize(11);
        tab->setFont(tabFont);
        polishButton(tab);
        tabGroup->addButton(tab, pageIndex);
        tabsRow->addWidget(tab);

        auto* page = new QWidget(stack);
        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(6);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        const auto& specs = pages.at(pageIndex).second;
        for (int i = 0; i < specs.size(); ++i) {
            const ShapeSpec spec = specs.at(i);
            auto* button = new QToolButton(page);
            button->setObjectName("finilyShapeBtn");
            button->setFixedSize(54, 40);
            button->setIconSize(QSize(20, 20));
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setToolTip(spec.name + " · " + spec.tip);
            button->setCheckable(true);
            polishButton(button);
            connect(button, &QToolButton::clicked, this, [this, spec] {
                if (!m_canvas)
                    return;
                m_canvas->setShapeKind(spec.kind);
                m_canvas->setCongruenceMarkSpec(spec.mark);
                if (spec.sides > 0)
                    m_canvas->setPolygonSides(spec.sides);
                m_canvas->setQuadVariant(spec.quad);
                if (spec.solid >= 0)
                    m_canvas->setSolidKind(spec.solid);
                if (spec.tri >= 0)
                    m_canvas->setTriangleVariant(spec.tri);
                if (spec.dimension)
                    m_canvas->setLinePattern(LinePattern::Dimension);
                else if (m_canvas->linePattern() == LinePattern::Dimension)
                    m_canvas->setLinePattern(LinePattern::Solid);
                m_canvas->setTool(ToolType::Shape);
                closePopovers();
                refreshFromCanvas();
            });
            m_shapeKindBtns.append(button);
            m_shapeKindSpecs.append(spec.kind);
            m_shapeDimensionSpecs.append(spec.dimension);
            m_shapeSidesSpecs.append(spec.sides);
            m_shapeTriSpecs.append(spec.tri);
            m_shapeQuadSpecs.append(spec.quad);
            m_shapeSolidSpecs.append(spec.solid);
            m_shapeMarkSpecs.append(spec.mark);
            m_iconSpecs.append(qMakePair(button, spec.glyph));
            grid->addWidget(button, i / 5, i % 5);
        }
        if (pageIndex == 0) {
            auto* poly = new QToolButton(page);
            poly->setObjectName("finilyShapeBtn");
            poly->setFixedSize(54, 40);
            poly->setIconSize(QSize(20, 20));
            poly->setToolTip(tr("Polilínea (L) · puntos libres; Mayús + arrastrar para editar una figura"));
            polishButton(poly);
            connect(poly, &QToolButton::clicked, this, [this] {
                closePopovers();
                selectTool(ToolType::Polyline);
            });
            m_iconSpecs.append(qMakePair(poly, QString("polyline")));
            grid->addWidget(poly, specs.size() / 5, specs.size() % 5);
        } else if (pageIndex == 3) {
            // Botón generador de Tablas / Matrices en la pestaña Matemáticas
            auto* tableBtn = new QToolButton(page);
            tableBtn->setObjectName("finilyShapeBtn");
            tableBtn->setFixedSize(54, 40);
            tableBtn->setText(QStringLiteral("Tabla"));
            tableBtn->setToolTip(tr("Insertar tabla o cuadrícula de matriz (Filas × Columnas)"));
            polishButton(tableBtn);
            connect(tableBtn, &QToolButton::clicked, this, [this] {
                if (!m_canvas)
                    return;
                closePopovers();
                bool ok = false;
                const QString spec = QInputDialog::getText(
                    window(), tr("Insertar Tabla / Matriz"),
                    tr("Filas × Columnas (ej. 3x4, 4x4, 2x5):"),
                    QLineEdit::Normal, QStringLiteral("3x4"), &ok);
                if (!ok || spec.trimmed().isEmpty())
                    return;
                const QStringList parts = spec.trimmed().toLower().split('x');
                const int r = parts.size() >= 1 ? std::clamp(parts[0].trimmed().toInt(), 1, 12) : 3;
                const int c = parts.size() >= 2 ? std::clamp(parts[1].trimmed().toInt(), 1, 12) : 4;
                m_canvas->createTableShape(r, c);
                refreshFromCanvas();
            });
            grid->addWidget(tableBtn, specs.size() / 5, specs.size() % 5);
        }
        stack->addWidget(page);
    }
    auto fitStack = [this, stack](int index) {
        for (int i = 0; i < stack->count(); ++i)
            stack->widget(i)->setSizePolicy(i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                            i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        stack->setCurrentIndex(index);
        stack->updateGeometry();
        if (m_shapesPopover) {
            if (m_shapesPopover->layout())
                m_shapesPopover->layout()->activate();
            m_shapesPopover->adjustSize();
            if (m_openPopover == m_shapesPopover) {
                positionOpenPopover();
                updateOverlayMask();
            }
        }
    };
    connect(tabGroup, &QButtonGroup::idClicked, this, fitStack);
    fitStack(0);
    column->addLayout(tabsRow);
    column->addWidget(stack);

    auto* lineStyleRow = new QHBoxLayout();
    lineStyleRow->setSpacing(6);
    const QPair<LinePattern, QString> shapeLines[] = {
        {LinePattern::Solid, QStringLiteral("Línea sólida")},
        {LinePattern::Dashed, QStringLiteral("Línea discontinua")},
        {LinePattern::Dotted, QStringLiteral("Línea punteada")},
    };
    for (const auto& [pat, tip] : shapeLines) {
        auto* btn = new QToolButton(m_shapesPopover);
        btn->setFixedSize(86, 28);
        btn->setIconSize(QSize(44, 14));
        btn->setToolTip(tip);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, pat] {
            if (m_canvas && m_canvas->linePattern() != LinePattern::Dimension)
                m_canvas->setLinePattern(pat);
            refreshFromCanvas();
        });
        m_patternBtns.append(btn);
        m_patternSpecs.append(qMakePair(btn, pat));
        lineStyleRow->addWidget(btn);
    }
    column->addLayout(lineStyleRow);
    m_shapeEndings = new QComboBox(m_shapesPopover);
    m_shapeEndings->addItems({tr("Sin punta"), tr("Flecha"), tr("Doble flecha")});
    m_shapeEndings->setToolTip(tr("Remate para segmentos, curvas y polilíneas"));
    connect(m_shapeEndings, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_canvas->setLineEnding(LineEnding(index));
    });
    column->addWidget(m_shapeEndings);
    column->addStretch(1);
}

void FloatingToolbar::buildStickersPopover() {
    m_stickersPopover = new GlassCapsule(this);
    m_stickersPopover->setObjectName("finilyPopover");
    m_stickersPopover->setFixedWidth(404);
    m_stickersPopover->setMaximumHeight(524);
    m_stickersPopover->hide();

    QSettings settings("Finily", "Finily");
    const int savedStyle = std::clamp(settings.value("stickers/style", 0).toInt(), 0, 2);
    m_stickerStyle = static_cast<StickerStyle>(savedStyle);

    auto* column = new QVBoxLayout(m_stickersPopover);
    column->setContentsMargins(14, 14, 14, 12);
    column->setSpacing(9);

    // Buscador rápido
    m_stickerSearchEdit = new QLineEdit(m_stickersPopover);
    m_stickerSearchEdit->setObjectName("finilyStickerSearch");
    m_stickerSearchEdit->setPlaceholderText(QStringLiteral("Buscar teorema, fórmula o notable..."));
    m_stickerSearchEdit->setClearButtonEnabled(true);
    m_stickerSearchEdit->setFixedHeight(34);
    connect(m_stickerSearchEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        rebuildStickersGrid();
    });
    connect(m_stickerSearchEdit, &QLineEdit::returnPressed, this, [this] {
        if (!m_canvas || !m_stickerGridHost)
            return;
        const QString firstId = m_stickerGridHost->property("firstStickerId").toString();
        if (firstId.isEmpty())
            return;
        m_canvas->insertStickerTemplate(firstId, m_stickerStyle);
        closePopovers();
        refreshFromCanvas();
    });
    column->addWidget(m_stickerSearchEdit);

    // Control segmentado de estilo (Limpio / Ficha / Solo dibujo)
    auto* styleRow = new QHBoxLayout();
    styleRow->setSpacing(8);
    auto* styleLabel = new QLabel(QStringLiteral("Estilo"), m_stickersPopover);
    styleLabel->setObjectName("finilyShortcut");
    styleRow->addWidget(styleLabel);

    auto* segFrame = new QFrame(m_stickersPopover);
    segFrame->setObjectName("finilyStickerSeg");
    auto* segLayout = new QHBoxLayout(segFrame);
    segLayout->setContentsMargins(3, 3, 3, 3);
    segLayout->setSpacing(3);

    const QVector<QPair<QString, StickerStyle>> styles = {
        {QStringLiteral("Limpio"), StickerStyle::CleanWithTitle},
        {QStringLiteral("Ficha"), StickerStyle::Card},
        {QStringLiteral("Solo dibujo"), StickerStyle::DiagramOnly},
    };
    m_stickerStyleBtns.clear();
    for (int i = 0; i < styles.size(); ++i) {
        auto* btn = new QToolButton(segFrame);
        btn->setObjectName("finilyStickerSegBtn");
        btn->setText(styles.at(i).first);
        btn->setCheckable(true);
        btn->setChecked(styles.at(i).second == m_stickerStyle);
        btn->setFixedHeight(24);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        polishButton(btn);
        const StickerStyle st = styles.at(i).second;
        connect(btn, &QToolButton::clicked, this, [this, st] {
            m_stickerStyle = st;
            QSettings s("Finily", "Finily");
            s.setValue("stickers/style", int(st));
            for (int k = 0; k < m_stickerStyleBtns.size(); ++k)
                m_stickerStyleBtns.at(k)->setChecked(k == int(st));
            rebuildStickersGrid();
        });
        m_stickerStyleBtns.append(btn);
        segLayout->addWidget(btn);
    }
    styleRow->addWidget(segFrame, 1);
    column->addLayout(styleRow);

    // Categorías en 2 filas compactas de 4 chips
    auto* catGrid = new QGridLayout();
    catGrid->setContentsMargins(0, 0, 0, 0);
    catGrid->setHorizontalSpacing(5);
    catGrid->setVerticalSpacing(5);
    const QStringList categories = {
        QStringLiteral("Todos"),
        QStringLiteral("Triángulos"),
        QStringLiteral("Notables"),
        QStringLiteral("Congruencia"),
        QStringLiteral("Circunferencia"),
        QStringLiteral("Métricas"),
        QStringLiteral("Áreas"),
        QStringLiteral("Mis ★"),
    };
    m_stickerCatBtns.clear();
    for (int i = 0; i < categories.size(); ++i) {
        const QString cat = categories.at(i);
        auto* btn = new QToolButton(m_stickersPopover);
        btn->setObjectName("finilyStickerChip");
        btn->setText(cat);
        btn->setCheckable(true);
        btn->setChecked(cat == m_activeStickerCategory);
        btn->setFixedHeight(26);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, cat] {
            m_activeStickerCategory = cat;
            for (QToolButton* b : m_stickerCatBtns)
                b->setChecked(b->text() == cat);
            rebuildStickersGrid();
        });
        m_stickerCatBtns.append(btn);
        catGrid->addWidget(btn, i / 4, i % 4);
    }
    column->addLayout(catGrid);

    // Área desplazable de tarjetas con scrollbar estilizada
    auto* scroll = new QScrollArea(m_stickersPopover);
    scroll->setObjectName("finilyStickerScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFixedHeight(310);
    scroll->viewport()->setAutoFillBackground(false);

    m_stickerGridHost = new QWidget(scroll);
    m_stickerGridHost->setAttribute(Qt::WA_TranslucentBackground);
    m_stickerGrid = new QGridLayout(m_stickerGridHost);
    m_stickerGrid->setContentsMargins(0, 2, 6, 4);
    m_stickerGrid->setHorizontalSpacing(10);
    m_stickerGrid->setVerticalSpacing(10);
    m_stickerGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setWidget(m_stickerGridHost);
    column->addWidget(scroll);

    auto* divider = new QFrame(m_stickersPopover);
    divider->setObjectName("finilyDivider");
    column->addWidget(divider);

    // Botón inferior bien delimitado para guardar la selección actual
    auto* saveSelBtn = new QToolButton(m_stickersPopover);
    saveSelBtn->setObjectName("finilyStickerSaveBtn");
    saveSelBtn->setText(QStringLiteral("+ Guardar selección actual como sticker"));
    saveSelBtn->setFixedHeight(32);
    saveSelBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    polishButton(saveSelBtn);
    connect(saveSelBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        if (!m_canvas->hasSelection()) {
            showToast(QStringLiteral("Selecciona un dibujo o figura con el Lazo primero"));
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(
            window(), tr("Guardar como sticker"),
            tr("Nombre corto para el sticker:"),
            QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        if (m_canvas->saveSelectionAsSticker(name)) {
            m_activeStickerCategory = QStringLiteral("Mis ★");
            for (QToolButton* b : m_stickerCatBtns)
                b->setChecked(b->text() == m_activeStickerCategory);
            rebuildStickersGrid();
        }
    });
    column->addWidget(saveSelBtn);
}

void FloatingToolbar::rebuildStickersGrid() {
    if (!m_stickerGrid || !m_stickerGridHost)
        return;

    while (QLayoutItem* item = m_stickerGrid->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    const QString query = m_stickerSearchEdit
        ? normalizeSearchText(m_stickerSearchEdit->text().trimmed())
        : QString();
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QVector<StickerTemplate> templates = allStickerTemplates();

    QVector<StickerTemplate> matched;
    matched.reserve(templates.size());
    for (const auto& tpl : templates) {
        if (m_activeStickerCategory != QStringLiteral("Todos") && tpl.category != m_activeStickerCategory)
            continue;
        if (!query.isEmpty()) {
            bool hit = normalizeSearchText(tpl.title).contains(query)
                       || normalizeSearchText(tpl.category).contains(query)
                       || normalizeSearchText(tpl.formula).contains(query);
            if (!hit) {
                for (const QString& kw : tpl.keywords) {
                    if (normalizeSearchText(kw).contains(query)) {
                        hit = true;
                        break;
                    }
                }
            }
            if (!hit)
                continue;
        }
        matched.append(tpl);
    }

    m_stickerGridHost->setProperty("firstStickerId", matched.isEmpty() ? QString() : matched.first().id);

    if (matched.isEmpty()) {
        auto* empty = new QLabel(m_stickerGridHost);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter);
        empty->setObjectName("finilyShortcut");
        empty->setMinimumHeight(110);
        empty->setText((m_activeStickerCategory == QStringLiteral("Mis ★") && query.isEmpty())
                           ? QStringLiteral("Selecciona un dibujo con el Lazo y guárdalo como sticker ★")
                           : QStringLiteral("Sin resultados"));
        m_stickerGrid->addWidget(empty, 0, 0, 1, 2);
        return;
    }

    for (int i = 0; i < matched.size(); ++i) {
        const StickerTemplate& tpl = matched.at(i);
        auto* card = new QToolButton(m_stickerGridHost);
        card->setObjectName("finilyStickerCard");
        card->setFixedSize(176, 126);
        card->setToolTip(tpl.formula.isEmpty() ? tpl.title : (tpl.title + QStringLiteral("\n") + tpl.formula));
        polishButton(card);

        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 8, 8, 7);
        cardLayout->setSpacing(3);

        // Miniatura siempre en modo DiagramOnly para máxima nitidez y sin duplicar el título
        auto* preview = new QLabel(card);
        preview->setAlignment(Qt::AlignCenter);
        preview->setFixedHeight(74);
        preview->setAttribute(Qt::WA_TransparentForMouseEvents);
        preview->setPixmap(renderStickerThumbnail(tpl, QSize(160, 74), theme, StickerStyle::DiagramOnly));
        cardLayout->addWidget(preview);

        auto* titleLbl = new QLabel(tpl.title, card);
        titleLbl->setObjectName("finilyStickerCardTitle");
        titleLbl->setAlignment(Qt::AlignCenter);
        titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        cardLayout->addWidget(titleLbl);

        if (!tpl.formula.isEmpty()) {
            auto* formulaLbl = new QLabel(tpl.formula, card);
            formulaLbl->setObjectName("finilyStickerCardFormula");
            formulaLbl->setAlignment(Qt::AlignCenter);
            formulaLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
            cardLayout->addWidget(formulaLbl);
        }

        const QString id = tpl.id;
        connect(card, &QToolButton::clicked, this, [this, id] {
            if (m_canvas)
                m_canvas->insertStickerTemplate(id, m_stickerStyle);
            closePopovers();
            refreshFromCanvas();
        });

        if (tpl.isCustom) {
            auto* delBtn = new QToolButton(card);
            delBtn->setText(QStringLiteral("×"));
            delBtn->setToolTip(tr("Eliminar sticker personalizado"));
            delBtn->setFixedSize(18, 18);
            delBtn->move(card->width() - 23, 5);
            delBtn->setCursor(Qt::PointingHandCursor);
            delBtn->setFocusPolicy(Qt::NoFocus);
            delBtn->setStyleSheet(QStringLiteral(
                "QToolButton { background: rgba(239, 68, 68, 180); color: #ffffff; border: none;"
                " border-radius: 9px; font-weight: 700; font-size: 11px; padding: 0px; }"
                "QToolButton:hover { background: rgba(220, 38, 38, 235); }"));
            connect(delBtn, &QToolButton::clicked, this, [this, id] {
                if (deleteCustomSticker(id))
                    showToast(QStringLiteral("Sticker eliminado"));
                rebuildStickersGrid();
            });
            delBtn->raise();
        }

        m_stickerGrid->addWidget(card, i / 2, i % 2);
    }
}

void FloatingToolbar::openStickersPopover() {
    if (!m_stickersPopover || !m_stickersBtn)
        return;
    openPopover(m_stickersPopover, m_stickersBtn);
    if (m_openPopover == m_stickersPopover) {
        rebuildStickersGrid();
        if (m_stickerSearchEdit) {
            m_stickerSearchEdit->setFocus();
            m_stickerSearchEdit->selectAll();
        }
    }
}

void FloatingToolbar::buildContextPill() {
    m_contextPill = new GlassCapsule(this);
    m_contextPill->setObjectName("finilyContext");
    auto* row = new QHBoxLayout(m_contextPill);
    row->setContentsMargins(6, 5, 6, 5);
    row->setSpacing(3);

    auto makeCtxBtn = [&](const QString& iconKind, const QString& tip, bool accent = false) {
        auto* b = new QToolButton(m_contextPill);
        if (accent)
            b->setObjectName("finilyAccentBtn");
        b->setFixedSize(32, 32);
        b->setIconSize(QSize(18, 18));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setToolTip(tip);
        polishButton(b);
        m_ctxIconSpecs.append(qMakePair(b, iconKind));
        row->addWidget(b);
        return b;
    };

    m_finishPolylineBtn = makeCtxBtn("check", "Terminar polilínea (Enter)");
    connect(m_finishPolylineBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->finishActivePolyline(false);
        refreshFromCanvas();
    });

    m_closePolylineBtn = makeCtxBtn("close-poly", "Cerrar polígono");
    connect(m_closePolylineBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->finishActivePolyline(true);
        refreshFromCanvas();
    });

    m_ctxBeautifyBtn = makeCtxBtn("straighten", tr("Embellecer y ordenar escritura"), true);
    connect(m_ctxBeautifyBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->straightenSelection();
        refreshFromCanvas();
    });

    m_ctxAxesFuncBtn = makeCtxBtn("fx", tr("Graficar función f(x) en estos ejes…"), true);
    connect(m_ctxAxesFuncBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        bool ok = false;
        const QString current = m_canvas->selectedAxesFunction();
        const QString expr = QInputDialog::getText(
            window(), tr("Graficar función en Ejes X/Y"),
            tr("Escribe f(x) (ej: x^2 - 4, sin(x), 2*x + 1, sqrt(x), cos(x)):"),
            QLineEdit::Normal, current.isEmpty() ? QStringLiteral("x^2 - 4") : current, &ok);
        if (ok)
            m_canvas->setSelectedAxesFunction(expr.trimmed());
        refreshFromCanvas();
    });

    m_ctxDuplicateBtn = makeCtxBtn("duplicate", tr("Duplicar selección (Ctrl+D)"));
    connect(m_ctxDuplicateBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->duplicateSelection();
        refreshFromCanvas();
    });

    m_ctxDeleteBtn = makeCtxBtn("trash", tr("Eliminar selección (Supr)"));
    connect(m_ctxDeleteBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->deleteSelection();
        refreshFromCanvas();
    });

    m_selectionActions = makeCtxBtn("more", tr("Más acciones de selección…"));
    connect(m_selectionActions, &QToolButton::clicked, this, [this] {
        showSelectionMenu(m_selectionActions->mapToGlobal(QPoint(0, 0)));
    });
    m_contextPill->hide();
}

void FloatingToolbar::showSelectionMenu(const QPoint& position) {
    if (!m_canvas || m_cleanUi || !m_canvas->hasSelection() || m_canvas->isDrawingPolyline())
        return;
    QMenu menu(this);
    const auto style = m_canvas->selectionStyle();
    if (style.color || style.filled)
        menu.addAction(tr("Propiedades…"), this, &FloatingToolbar::editSelectionProperties);
    if (m_canvas->hasSelectedAxes()) {
        menu.addAction(tr("Graficar función f(x) en estos ejes…"), this, [this] {
            bool ok = false;
            const QString current = m_canvas->selectedAxesFunction();
            const QString expr = QInputDialog::getText(
                window(), tr("Graficar función en Ejes X/Y"),
                tr("Escribe f(x) (ej: x^2 - 4, sin(x), 2*x + 1, sqrt(x), cos(x)):"),
                QLineEdit::Normal, current.isEmpty() ? QStringLiteral("x^2 - 4") : current, &ok);
            if (ok)
                m_canvas->setSelectedAxesFunction(expr.trimmed());
        });
    }
    menu.addAction(tr("Enviar recorte a página nueva"), this, [this] { m_canvas->cropSelectionToNewPage(); });
    menu.addAction(tr("Abrir espacio vertical debajo"), this, [this] { m_canvas->insertVerticalSpaceAtSelection(140.0); });
    if (m_canvas->hasLassoSelection() || m_canvas->hasSelectedShape() || m_canvas->hasSelectedText()) {
        menu.addAction(tr("Guardar como sticker ★"), this, [this] {
            bool ok = false;
            const QString name = QInputDialog::getText(
                window(), tr("Guardar como sticker"),
                tr("Nombre corto para el sticker:"),
                QLineEdit::Normal, QString(), &ok).trimmed();
            if (ok && !name.isEmpty()) {
                if (m_canvas->saveSelectionAsSticker(name))
                    rebuildStickersGrid();
            }
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Cortar"), this, [this] { m_canvas->cutSelection(); });
    menu.addAction(tr("Copiar"), this, [this] { m_canvas->copySelection(); });
    menu.addAction(tr("Duplicar"), this, [this] { m_canvas->duplicateSelection(); });
    menu.addSeparator();
    menu.addAction(tr("Voltear horizontalmente"), this, [this] { m_canvas->flipSelectionHorizontal(); });
    menu.addAction(tr("Voltear verticalmente"), this, [this] { m_canvas->flipSelectionVertical(); });
    menu.addAction(tr("Traer al frente"), this, [this] { m_canvas->bringSelectionToFront(); });
    menu.addAction(tr("Enviar al fondo"), this, [this] { m_canvas->sendSelectionToBack(); });
    if (m_canvas->canGroupSelection())
        menu.addAction(tr("Agrupar"), this, [this] { m_canvas->groupSelection(); });
    if (m_canvas->canUngroupSelection())
        menu.addAction(tr("Desagrupar"), this, [this] { m_canvas->ungroupSelection(); });
    if (m_canvas->hasLockableSelection())
        menu.addAction(m_canvas->isSelectionLocked() ? tr("Desbloquear") : tr("Bloquear"),
                       this, [this] { m_canvas->toggleLockSelection(); });
    if (m_canvas->hasSelectedStrokes()) {
        menu.addSeparator();
        menu.addAction(tr("Embellecer y ordenar escritura"), this, [this] { m_canvas->straightenSelection(); });
    }
    menu.addSeparator();
    menu.addAction(tr("Eliminar"), this, [this] { m_canvas->deleteSelection(); });
    menu.exec(position);
}

void FloatingToolbar::editSelectionProperties() {
    const auto current = m_canvas->selectionStyle();
    SelectionStyle edits;
    QDialog dialog(window());
    dialog.setWindowTitle(tr("Propiedades de la selección"));
    auto* form = new QFormLayout(&dialog);
    auto colorField = [&](const QString& label, QColor initial, std::optional<QColor>& value) {
        auto* button = new QPushButton(initial.name(), &dialog);
        connect(button, &QPushButton::clicked, &dialog, [button, &dialog, &value] {
            const auto color = QColorDialog::getColor(QColor(button->text()), &dialog);
            if (color.isValid()) {
                value = color;
                button->setText(color.name());
            }
        });
        form->addRow(label, button);
        return button;
    };
    auto options = [&](const QString& label, const QStringList& names, int currentIndex) {
        auto* combo = new QComboBox(&dialog);
        combo->addItems(names);
        combo->setCurrentIndex(currentIndex);
        form->addRow(label, combo);
        return combo;
    };
    if (current.color)
        colorField(current.width ? tr("Color de contorno") : tr("Color"), *current.color, edits.color);
    if (current.width) {
        auto* width = new QDoubleSpinBox(&dialog);
        width->setDecimals(1);
        width->setRange(.1, 200);
        width->setSingleStep(.5);
        width->setSuffix(tr(" px"));
        width->setValue(*current.width);
        connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) { edits.width = value; });
        form->addRow(tr("Grosor"), width);
    }
    if (current.pattern) {
        auto* pattern = options(tr("Línea"), {tr("Sólida"), tr("Guiones"), tr("Punteada")}, int(*current.pattern));
        connect(pattern, &QComboBox::currentIndexChanged, &dialog,
                [&](int index) { edits.pattern = LinePattern(index); });
    }
    if (current.ending) {
        auto* ending = options(tr("Remates"), {tr("Sin punta"), tr("Flecha"), tr("Doble flecha")}, int(*current.ending));
        connect(ending, &QComboBox::currentIndexChanged, &dialog,
                [&](int index) { edits.ending = LineEnding(index); });
    }
    if (current.label) {
        auto* label = new QLineEdit(*current.label, &dialog);
        label->setMaxLength(80);
        label->setPlaceholderText(tr("Escribe una medida o una etiqueta"));
        connect(label, &QLineEdit::textChanged, &dialog,
                [&](const QString& value) { edits.label = value.trimmed(); });
        form->addRow(tr("Etiqueta"), label);
    }
    if (current.filled) {
        auto* filled = new QCheckBox(tr("Con relleno"), &dialog);
        filled->setChecked(*current.filled);
        form->addRow(filled);
        auto* fillColor = colorField(tr("Color de relleno"),
                                     current.fillColor.value_or(current.color.value_or(m_canvas->inkColor())),
                                     edits.fillColor);
        auto* pattern = options(tr("Patrón de relleno"), {tr("Suave"), tr("Diagonal"), tr("Punteado"),
                                tr("Sólido"), tr("Rayado cruzado"), tr("Cuadrícula")},
                                int(current.fillPattern.value_or(ShadePattern::Soft)));
        fillColor->setEnabled(*current.filled);
        pattern->setEnabled(*current.filled);
        connect(filled, &QCheckBox::toggled, &dialog, [&, fillColor, pattern](bool checked) {
            edits.filled = checked;
            fillColor->setEnabled(checked);
            pattern->setEnabled(checked);
        });
        connect(pattern, &QComboBox::currentIndexChanged, &dialog,
                [&](int index) { edits.fillPattern = ShadePattern(index); });
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Aplicar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        m_canvas->setSelectionStyle(edits);
}

void FloatingToolbar::buildFilePopover() {
    m_filePopover = new GlassCapsule(this);
    m_filePopover->setObjectName("finilyPopover");
    m_filePopover->setFixedWidth(300);
    m_filePopover->hide();

    auto* column = new QVBoxLayout(m_filePopover);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    m_newNotebookBtn = new QToolButton(m_filePopover);
    m_newNotebookBtn->setFixedSize(272, 34);
    m_newNotebookBtn->setText("Nuevo cuaderno (Ctrl+N)");
    m_newNotebookBtn->setToolTip("Empieza un cuaderno en blanco (descarta el contenido actual)");
    polishButton(m_newNotebookBtn);
    connect(m_newNotebookBtn, &QToolButton::clicked, this, &FloatingToolbar::newDocumentAction);
    column->addWidget(m_newNotebookBtn);

    m_openNotebookBtn = new QToolButton(m_filePopover);
    m_openNotebookBtn->setFixedSize(272, 34);
    m_openNotebookBtn->setText("Abrir cuaderno... (Ctrl+Shift+O)");
    m_openNotebookBtn->setToolTip("Abre un cuaderno guardado con extensión .finily");
    polishButton(m_openNotebookBtn);
    connect(m_openNotebookBtn, &QToolButton::clicked, this, &FloatingToolbar::openNotebookDialog);
    column->addWidget(m_openNotebookBtn);

    m_saveNotebookBtn = new QToolButton(m_filePopover);
    m_saveNotebookBtn->setFixedSize(272, 34);
    m_saveNotebookBtn->setText("Guardar (Ctrl+S)");
    m_saveNotebookBtn->setToolTip("Guarda el cuaderno actual (.finily); si aún no tiene archivo, pide nombre");
    polishButton(m_saveNotebookBtn);
    connect(m_saveNotebookBtn, &QToolButton::clicked, this, &FloatingToolbar::saveNotebookAction);
    column->addWidget(m_saveNotebookBtn);

    m_saveNotebookAsBtn = new QToolButton(m_filePopover);
    m_saveNotebookAsBtn->setFixedSize(272, 34);
    m_saveNotebookAsBtn->setText("Guardar como... (Ctrl+Shift+S)");
    m_saveNotebookAsBtn->setToolTip("Guarda el cuaderno en un archivo .finily nuevo (añade la extensión si falta)");
    polishButton(m_saveNotebookAsBtn);
    connect(m_saveNotebookAsBtn, &QToolButton::clicked, this, &FloatingToolbar::saveNotebookAsDialog);
    column->addWidget(m_saveNotebookAsBtn);

    column->addWidget(makeDivider(m_filePopover));

    m_importPdfBtn = new QToolButton(m_filePopover);
    m_importPdfBtn->setFixedSize(272, 34);
    m_importPdfBtn->setText("Importar PDF (Ctrl+O)");
    m_importPdfBtn->setToolTip("Inserta las hojas de un PDF como páginas del cuaderno");
    polishButton(m_importPdfBtn);
    connect(m_importPdfBtn, &QToolButton::clicked, this, &FloatingToolbar::importPdfDialog);
    column->addWidget(m_importPdfBtn);

    m_importImageBtn = new QToolButton(m_filePopover);
    m_importImageBtn->setFixedSize(272, 34);
    m_importImageBtn->setText("Insertar imagen...");
    m_importImageBtn->setToolTip("Inserta una imagen PNG, JPG, WEBP o BMP desde el disco");
    polishButton(m_importImageBtn);
    connect(m_importImageBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        closePopovers();
        QSettings settings("Finily", "Finily");
        const QString lastDir = settings.value("files/lastImageDir").toString();
        const QString path = QFileDialog::getOpenFileName(window(), "Insertar imagen", lastDir,
                                                          "Imágenes (*.png *.jpg *.jpeg *.webp *.bmp)");
        if (!path.isEmpty()) {
            settings.setValue("files/lastImageDir", QFileInfo(path).absolutePath());
            m_canvas->importImageFile(path);
            rememberRecentImage(path);
            refreshFromCanvas();
        }
    });
    column->addWidget(m_importImageBtn);

    m_pasteBtn = new QToolButton(m_filePopover);
    m_pasteBtn->setFixedSize(272, 34);
    m_pasteBtn->setText("Pegar imagen (Ctrl+V)");
    m_pasteBtn->setToolTip("Pega la imagen del portapapeles en el lienzo");
    polishButton(m_pasteBtn);
    connect(m_pasteBtn, &QToolButton::clicked, this, [this] {
        closePopovers();
        if (m_canvas)
            m_canvas->pasteFromClipboard();
        refreshFromCanvas();
    });
    column->addWidget(m_pasteBtn);

    m_recentGalleryWrap = new QWidget(m_filePopover);
    auto* galCol = new QVBoxLayout(m_recentGalleryWrap);
    galCol->setContentsMargins(2, 4, 2, 4);
    galCol->setSpacing(4);
    auto* galLbl = new QLabel("Galería rápida", m_recentGalleryWrap);
    galLbl->setObjectName("finilySectionTitle");
    galCol->addWidget(galLbl);
    m_recentGalleryRow = new QHBoxLayout();
    m_recentGalleryRow->setContentsMargins(0, 0, 0, 0);
    m_recentGalleryRow->setSpacing(6);
    galCol->addLayout(m_recentGalleryRow);
    column->addWidget(m_recentGalleryWrap);
    rebuildRecentGallery();

    m_exportBtn = new QToolButton(m_filePopover);
    m_exportBtn->setFixedSize(272, 34);
    m_exportBtn->setText("Exportar clase a PDF (Ctrl+E)");
    m_exportBtn->setToolTip("Guarda toda la clase (trazos, figuras e imágenes) como PDF");
    polishButton(m_exportBtn);
    connect(m_exportBtn, &QToolButton::clicked, this, &FloatingToolbar::exportPdfDialog);
    column->addWidget(m_exportBtn);

    for (QToolButton* item : {m_newNotebookBtn, m_openNotebookBtn, m_saveNotebookBtn, m_saveNotebookAsBtn,
                              m_importPdfBtn, m_importImageBtn, m_pasteBtn, m_exportBtn})
        makeMenuRow(item);
    column->setSpacing(2);

    m_pdfInfoLabel = new QLabel(m_filePopover);
    m_pdfInfoLabel->setWordWrap(true);
    m_pdfInfoLabel->hide();
    column->addWidget(m_pdfInfoLabel);

    column->addWidget(makeDivider(m_filePopover));
    auto* penCfgToggle = new QToolButton(m_filePopover);
    penCfgToggle->setObjectName("finilyMoreToggle");
    penCfgToggle->setText("Ajustes de lápiz y tableta  ▾");
    penCfgToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    penCfgToggle->setCursor(Qt::PointingHandCursor);
    penCfgToggle->setFocusPolicy(Qt::NoFocus);
    penCfgToggle->setFixedHeight(28);
    column->addWidget(penCfgToggle);
    auto* penCfgBox = new QWidget(m_filePopover);
    penCfgBox->setVisible(false);
    auto* pressureColumn = new QVBoxLayout(penCfgBox);
    pressureColumn->setContentsMargins(0, 4, 0, 4);
    pressureColumn->setSpacing(6);
    connect(penCfgToggle, &QToolButton::clicked, this, [this, penCfgBox, penCfgToggle] {
        const bool show = !penCfgBox->isVisible();
        penCfgBox->setVisible(show);
        penCfgToggle->setText(show ? "Ajustes de lápiz y tableta  ▴" : "Ajustes de lápiz y tableta  ▾");
        m_filePopover->adjustSize();
        updateOverlayMask();
    });
    auto* pressureHdr = new QHBoxLayout();
    pressureHdr->setSpacing(6);
    auto* pressureTitle = new QLabel("Sensibilidad a la presión", penCfgBox);
    pressureTitle->setObjectName("finilySectionTitle");
    pressureHdr->addWidget(pressureTitle, 1);
    m_pressureLabel = new QLabel("50%", penCfgBox);
    m_pressureLabel->setObjectName("finilySectionTitle");
    pressureHdr->addWidget(m_pressureLabel);
    pressureColumn->addLayout(pressureHdr);

    m_pressureSlider = new DotStepSlider(penCfgBox);
    m_pressureSlider->setRange(0, 10);
    m_pressureSlider->setSingleStep(1);
    m_pressureSlider->setPageStep(1);
    m_pressureSlider->setToolTip("Intensidad de presión del lápiz (0% = trazo uniforme, 100% = contraste máximo)");
    connect(m_pressureSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_pressureLabel)
            m_pressureLabel->setText(QString("%1%").arg(level * 10));
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setPressureSensitivity(double(level) / 10.0);
    });
    pressureColumn->addWidget(m_pressureSlider);

    auto* swapBtn = new SwitchButton(penCfgBox);
    swapBtn->setFixedSize(272, 30);
    swapBtn->setText("Invertir botones del lápiz");
    swapBtn->setCheckable(true);
    swapBtn->setChecked(m_canvas && m_canvas->swapPenButtons());
    connect(swapBtn, &QToolButton::clicked, this, [this, swapBtn] {
        if (m_canvas) m_canvas->setSwapPenButtons(swapBtn->isChecked());
    });
    pressureColumn->addWidget(swapBtn);

    m_mathBeautifyBtn = new SwitchButton(penCfgBox);
    m_mathBeautifyBtn->setFixedSize(272, 30);
    m_mathBeautifyBtn->setText("Embellecer letra por palabra (M)");
    m_mathBeautifyBtn->setToolTip("Al terminar de escribir cada palabra, suaviza y alinea tu letra automáticamente respetando tu unión y espaciado");
    m_mathBeautifyBtn->setCheckable(true);
    m_mathBeautifyBtn->setChecked(m_canvas && m_canvas->mathBeautifyEnabled());
    connect(m_mathBeautifyBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_mathBeautifyBtn) m_canvas->setMathBeautifyEnabled(m_mathBeautifyBtn->isChecked());
        refreshFromCanvas();
    });
    pressureColumn->addWidget(m_mathBeautifyBtn);

    m_holdToShapeBtn = new SwitchButton(penCfgBox);
    m_holdToShapeBtn->setFixedSize(272, 30);
    m_holdToShapeBtn->setText("Auto-forma al mantener");
    m_holdToShapeBtn->setCheckable(true);
    m_holdToShapeBtn->setChecked(m_canvas ? m_canvas->holdToShapeEnabled() : true);
    connect(m_holdToShapeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_holdToShapeBtn) m_canvas->setHoldToShapeEnabled(m_holdToShapeBtn->isChecked());
        refreshFromCanvas();
    });
    pressureColumn->addWidget(m_holdToShapeBtn);

    m_smartGuidesBtn = new SwitchButton(penCfgBox);
    m_smartGuidesBtn->setFixedSize(272, 30);
    m_smartGuidesBtn->setText("Guías inteligentes e imán");
    m_smartGuidesBtn->setCheckable(true);
    m_smartGuidesBtn->setChecked(m_canvas ? m_canvas->smartGuidesEnabled() : true);
    connect(m_smartGuidesBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_smartGuidesBtn) m_canvas->setSmartGuidesEnabled(m_smartGuidesBtn->isChecked());
        refreshFromCanvas();
    });
    pressureColumn->addWidget(m_smartGuidesBtn);

    auto* cursorLbl = new QLabel("Puntero de lápiz", penCfgBox);
    cursorLbl->setObjectName("finilySectionTitle");
    pressureColumn->addWidget(cursorLbl);
    auto* cursorRow = new QHBoxLayout();
    cursorRow->setSpacing(6);
    const QStringList cursorNames = {QStringLiteral("Lapicito"), QStringLiteral("Aro"), QStringLiteral("Cruz")};
    for (int i = 0; i < cursorNames.size(); ++i) {
        auto* btn = new QToolButton(penCfgBox);
        btn->setFixedSize(86, 26);
        btn->setText(cursorNames[i]);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            if (m_canvas) m_canvas->setCursorStyle(i);
            refreshFromCanvas();
        });
        m_cursorBtns.push_back(btn);
        cursorRow->addWidget(btn);
    }
    pressureColumn->addLayout(cursorRow);
    column->addWidget(penCfgBox);

    column->addWidget(makeDivider(m_filePopover));
    m_updateBtn = new QToolButton(m_filePopover);
    m_updateBtn->setFixedSize(272, 34);
    m_updateBtn->setText(QStringLiteral("Buscar actualizaciones…  (v%1)").arg(QApplication::applicationVersion()));
    m_updateBtn->setToolTip("Comprueba si existe una nueva versión de Finily en GitHub");
    polishButton(m_updateBtn);
    connect(m_updateBtn, &QToolButton::clicked, this, [this] {
        closePopovers();
        emit checkForUpdatesRequested();
    });
    column->addWidget(m_updateBtn);

    column->addStretch(1);
}

void FloatingToolbar::setUpdateAvailable(const QString& version) {
    if (!m_updateBtn)
        return;
    if (version.trimmed().isEmpty()) {
        m_updateBtn->setText(QStringLiteral("Buscar actualizaciones…  (v%1)").arg(QApplication::applicationVersion()));
    } else {
        m_updateBtn->setText(QStringLiteral("Actualizar a v%1…").arg(version));
    }
}

void FloatingToolbar::rememberRecentImage(const QString& path) {
    if (path.isEmpty())
        return;
    QSettings settings("Finily", "Finily");
    QStringList recent = settings.value("files/recentImages").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 4)
        recent.removeLast();
    settings.setValue("files/recentImages", recent);
    rebuildRecentGallery();
}

void FloatingToolbar::rebuildRecentGallery() {
    if (!m_recentGalleryWrap || !m_recentGalleryRow)
        return;
    while (QLayoutItem* item = m_recentGalleryRow->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    QSettings settings("Finily", "Finily");
    const QStringList raw = settings.value("files/recentImages").toStringList();
    QStringList valid;
    for (const QString& p : raw) {
        if (QFileInfo::exists(p))
            valid.push_back(p);
        if (valid.size() >= 4)
            break;
    }
    if (valid.isEmpty()) {
        m_recentGalleryWrap->hide();
        return;
    }

    m_recentGalleryWrap->show();
    for (const QString& path : valid) {
        auto* btn = new QToolButton(m_recentGalleryWrap);
        btn->setFixedSize(62, 44);
        btn->setIconSize(QSize(56, 38));
        btn->setToolTip(QStringLiteral("Insertar: ") + QFileInfo(path).fileName());
        polishButton(btn);
        QPixmap px(path);
        if (!px.isNull())
            btn->setIcon(QIcon(px.scaled(56, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        else
            btn->setText(QFileInfo(path).fileName().left(6));
        connect(btn, &QToolButton::clicked, this, [this, path] {
            closePopovers();
            if (m_canvas && QFileInfo::exists(path)) {
                m_canvas->importImageFile(path);
                rememberRecentImage(path);
                refreshFromCanvas();
            }
        });
        m_recentGalleryRow->addWidget(btn);
    }
    m_recentGalleryRow->addStretch(1);
}

} // namespace finily
