#include "ui/popovers/StrokePopover.h"

#include "ui/CanvasWidget.h"
#include "ui/ToolbarIcons.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace finily {

StrokePopover::StrokePopover(CanvasWidget* canvas, QWidget* parent)
    : GlassCapsule(parent), m_canvas(canvas), m_palette(getPaletteForTheme(true)) {
    setObjectName(QStringLiteral("finilyPopover"));
    setFixedWidth(300);
    hide();

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    m_strokeStyleSection = new QWidget(this);
    auto* styleColumn = new QVBoxLayout(m_strokeStyleSection);
    styleColumn->setContentsMargins(0, 0, 0, 0);
    styleColumn->setSpacing(8);

    m_penTipSection = new QWidget(m_strokeStyleSection);
    auto* penTipRow = new QHBoxLayout(m_penTipSection);
    penTipRow->setContentsMargins(0, 0, 0, 0);
    penTipRow->setSpacing(6);
    const QStringList penTipNames = {
        QStringLiteral("Bolígrafo"),
        QStringLiteral("Pluma fuente"),
        QStringLiteral("Lápiz grafito"),
        QStringLiteral("Tiza"),
    };
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
            syncFromCanvas();
            emit refreshToolbarRequested();
        });
        m_penTipBtns.append(btn);
        penTipRow->addWidget(btn);
    }
    styleColumn->addWidget(m_penTipSection);

    m_hlTipSection = new QWidget(m_strokeStyleSection);
    auto* hlTipRow = new QHBoxLayout(m_hlTipSection);
    hlTipRow->setContentsMargins(0, 0, 0, 0);
    hlTipRow->setSpacing(6);
    const QStringList hlTipNames = {
        QStringLiteral("Resaltador libre (mantén para recto)"),
        QStringLiteral("Subrayado recto"),
    };
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
            syncFromCanvas();
            emit refreshToolbarRequested();
        });
        m_hlTipBtns.append(btn);
        hlTipRow->addWidget(btn);
    }
    styleColumn->addWidget(m_hlTipSection);

    m_lassoModeSection = new QWidget(this);
    auto* lassoRow = new QHBoxLayout(m_lassoModeSection);
    lassoRow->setContentsMargins(0, 0, 0, 0);
    lassoRow->setSpacing(6);
    m_lassoFreeBtn = new QToolButton(m_lassoModeSection);
    m_lassoFreeBtn->setFixedSize(133, 38);
    m_lassoFreeBtn->setIconSize(QSize(20, 20));
    m_lassoFreeBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_lassoFreeBtn->setToolTip(QStringLiteral("Lazo libre"));
    m_lassoFreeBtn->setCheckable(true);
    polishButton(m_lassoFreeBtn);
    m_ctxIconSpecs.append(qMakePair(m_lassoFreeBtn, QStringLiteral("lasso-free")));
    connect(m_lassoFreeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setLassoRectMode(false);
        syncFromCanvas();
        emit refreshToolbarRequested();
        emit closeRequested();
    });

    m_lassoRectBtn = new QToolButton(m_lassoModeSection);
    m_lassoRectBtn->setFixedSize(133, 38);
    m_lassoRectBtn->setIconSize(QSize(20, 20));
    m_lassoRectBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_lassoRectBtn->setToolTip(QStringLiteral("Selección rectangular"));
    m_lassoRectBtn->setCheckable(true);
    polishButton(m_lassoRectBtn);
    m_ctxIconSpecs.append(qMakePair(m_lassoRectBtn, QStringLiteral("lasso-rect")));
    connect(m_lassoRectBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setLassoRectMode(true);
        syncFromCanvas();
        emit refreshToolbarRequested();
        emit closeRequested();
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
                emit colorPicked(m_palette.at(i));
        });
        m_swatches.append(swatch);
        swatchRow->addWidget(swatch);
    }

    m_customColorBtn = new QToolButton(m_strokeStyleSection);
    m_customColorBtn->setFixedSize(24, 24);
    m_customColorBtn->setText(QStringLiteral("+"));
    m_customColorBtn->setToolTip(QStringLiteral("Color personalizado…"));
    polishButton(m_customColorBtn);
    connect(m_customColorBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        const QColor chosen = QColorDialog::getColor(
            m_canvas->inkColor(), window(), QStringLiteral("Color personalizado"));
        if (chosen.isValid()) {
            if (!m_palette.isEmpty())
                m_palette.last() = chosen;
            updateSwatches();
            emit colorPicked(chosen);
            emit refreshToolbarRequested();
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
            syncFromCanvas();
            emit refreshToolbarRequested();
            emit closeRequested();
        });
    }
    styleColumn->addWidget(m_fillStyleSection);

    m_strokeGeometrySection = new QWidget(m_strokeStyleSection);
    auto* geometryColumn = new QVBoxLayout(m_strokeGeometrySection);
    geometryColumn->setContentsMargins(0, 0, 0, 0);
    geometryColumn->setSpacing(8);
    styleColumn->addWidget(m_strokeGeometrySection);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(10);
    m_strokePreview = new StrokePreviewWidget(m_strokeGeometrySection);
    sizeRow->addWidget(m_strokePreview);
    m_sizeSlider = new DotStepSlider(m_strokeGeometrySection);
    m_sizeSlider->setRange(1, 10);
    m_sizeSlider->setPageStep(1);
    m_sizeSlider->setSingleStep(1);
    m_sizeSlider->setToolTip(QStringLiteral("Grosor del trazo"));
    sizeRow->addWidget(m_sizeSlider, 1);

    m_favMenuBtn = new QToolButton(m_strokeGeometrySection);
    m_favMenuBtn->setFixedSize(28, 28);
    m_favMenuBtn->setText(QStringLiteral("★"));
    m_favMenuBtn->setToolTip(QStringLiteral("Pinceles favoritos (guardar pincel actual o elegir favorito)"));
    polishButton(m_favMenuBtn);
    connect(m_favMenuBtn, &QToolButton::clicked, this, [this] {
        emit favoritesMenuRequested(m_favMenuBtn);
    });
    sizeRow->addWidget(m_favMenuBtn);
    geometryColumn->addLayout(sizeRow);
    column->addWidget(m_strokeStyleSection);

    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_updatingUi || !m_canvas)
            return;
        const int idx = std::clamp(level - 1, 0, 9);
        const bool isHl = m_canvas->tool() == ToolType::Highlighter;
        const double px = isHl ? kHlLevels[idx] : kPenLevels[idx];
        m_canvas->setStrokeSize(px);
        updateStrokePreview();
        emit refreshToolbarRequested();
    });

    m_stabilizerRow = new QWidget(this);
    auto* stabRow = new QHBoxLayout(m_stabilizerRow);
    stabRow->setContentsMargins(0, 2, 0, 0);
    stabRow->setSpacing(10);
    m_stabilizerIcon = new QLabel(m_stabilizerRow);
    m_stabilizerIcon->setFixedSize(28, 26);
    m_stabilizerIcon->setAlignment(Qt::AlignCenter);
    m_stabilizerIcon->setToolTip(QStringLiteral("Estabilizador de trazo"));
    stabRow->addWidget(m_stabilizerIcon);
    m_stabilizerSlider = new DotStepSlider(m_stabilizerRow);
    m_stabilizerSlider->setRange(0, 9);
    m_stabilizerSlider->setSingleStep(1);
    m_stabilizerSlider->setPageStep(1);
    m_stabilizerSlider->setToolTip(QStringLiteral("Estabilizador de trazo"));
    stabRow->addWidget(m_stabilizerSlider, 1);
    connect(m_stabilizerSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setStabilizer(double(level) / m_stabilizerSlider->maximum());
    });
    column->addWidget(m_stabilizerRow);

    m_hlOpacityRow = new QWidget(this);
    auto* hlOpRow = new QHBoxLayout(m_hlOpacityRow);
    hlOpRow->setContentsMargins(0, 2, 0, 0);
    hlOpRow->setSpacing(10);
    m_hlOpacityIcon = new QLabel(m_hlOpacityRow);
    m_hlOpacityIcon->setFixedSize(28, 26);
    m_hlOpacityIcon->setAlignment(Qt::AlignCenter);
    m_hlOpacityIcon->setToolTip(QStringLiteral("Transparencia del resaltador"));
    hlOpRow->addWidget(m_hlOpacityIcon);
    m_hlOpacitySlider = new DotStepSlider(m_hlOpacityRow);
    m_hlOpacitySlider->setRange(1, 10);
    m_hlOpacitySlider->setSingleStep(1);
    m_hlOpacitySlider->setPageStep(1);
    m_hlOpacitySlider->setToolTip(QStringLiteral("Transparencia del resaltador (10% a 68%)"));
    hlOpRow->addWidget(m_hlOpacitySlider, 1);
    connect(m_hlOpacitySlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_updatingUi || !m_canvas)
            return;
        const int idx = std::clamp(level - 1, 0, 9);
        m_canvas->setHighlighterOpacity(kHlOpacityLevels[idx]);
    });
    column->addWidget(m_hlOpacityRow);

    m_strokeBeautifyBtn = new SwitchButton(this);
    m_strokeBeautifyBtn->setFixedSize(272, 30);
    m_strokeBeautifyBtn->setText(QStringLiteral("Embellecer letra por palabra (M)"));
    m_strokeBeautifyBtn->setToolTip(
        QStringLiteral("Al terminar de escribir cada palabra, suaviza y alinea tu letra automáticamente "
                       "respetando tu unión y espaciado"));
    m_strokeBeautifyBtn->setCheckable(true);
    m_strokeBeautifyBtn->setChecked(m_canvas && m_canvas->mathBeautifyEnabled());
    connect(m_strokeBeautifyBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_strokeBeautifyBtn)
            m_canvas->setMathBeautifyEnabled(m_strokeBeautifyBtn->isChecked());
        emit refreshToolbarRequested();
    });
    column->addWidget(m_strokeBeautifyBtn);

    m_eraserSection = new QWidget(this);
    auto* eraserColumn = new QVBoxLayout(m_eraserSection);
    eraserColumn->setContentsMargins(0, 0, 0, 0);
    eraserColumn->setSpacing(8);
    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(6);
    m_eraserStrokeBtn = new QToolButton(m_eraserSection);
    m_eraserStrokeBtn->setFixedSize(133, 32);
    m_eraserStrokeBtn->setText(QStringLiteral("Por trazo"));
    m_eraserStrokeBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_eraserStrokeBtn->setToolTip(QStringLiteral("Borra el trazo completo al tocarlo"));
    m_eraserStrokeBtn->setCheckable(true);
    polishButton(m_eraserStrokeBtn);
    connect(m_eraserStrokeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setEraserMode(EraserMode::Stroke);
        syncFromCanvas();
        emit refreshToolbarRequested();
    });

    m_eraserPartialBtn = new QToolButton(m_eraserSection);
    m_eraserPartialBtn->setFixedSize(133, 32);
    m_eraserPartialBtn->setText(QStringLiteral("Parcial"));
    m_eraserPartialBtn->setToolTip(
        QStringLiteral("Recorta únicamente el tramo que tocas (mantén Shift para recortar limpio hasta "
                       "intersecciones)"));
    m_eraserPartialBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_eraserPartialBtn->setCheckable(true);
    polishButton(m_eraserPartialBtn);
    connect(m_eraserPartialBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setEraserMode(EraserMode::Partial);
        syncFromCanvas();
        emit refreshToolbarRequested();
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
    m_eraserSizeSlider->setToolTip(QStringLiteral("Tamaño del borrador"));
    erSizeRow->addWidget(m_eraserSizeSlider, 1);
    eraserColumn->addLayout(erSizeRow);
    connect(m_eraserSizeSlider, &QSlider::valueChanged, this, [this](int level) {
        updateEraserPreview();
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setEraserSize(level * 8.0);
    });

    auto* erFilterLbl = new QLabel(QStringLiteral("Qué borrar"), m_eraserSection);
    erFilterLbl->setObjectName(QStringLiteral("finilySectionTitle"));
    eraserColumn->addWidget(erFilterLbl);

    auto* erFilterRow = new QHBoxLayout();
    erFilterRow->setSpacing(6);
    const QStringList erFilterLabels = {
        QStringLiteral("Todo"),
        QStringLiteral("Sin figuras"),
        QStringLiteral("Resaltador"),
    };
    const QStringList erFilterTips = {
        QStringLiteral("Borra tinta, figuras, ejes y texto"),
        QStringLiteral("Borra tinta y texto conservando figuras y ejes"),
        QStringLiteral("Solo borra trazos de resaltador"),
    };
    for (int i = 0; i < 3; ++i) {
        auto* btn = new QToolButton(m_eraserSection);
        btn->setFixedSize(86, 28);
        btn->setText(erFilterLabels[i]);
        btn->setToolTip(erFilterTips[i]);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            if (!m_canvas)
                return;
            if (i == 0) {
                m_canvas->setEraserOnlyHighlight(false);
                m_canvas->setEraserPreserveShapes(false);
            } else if (i == 1) {
                m_canvas->setEraserOnlyHighlight(false);
                m_canvas->setEraserPreserveShapes(true);
            } else {
                m_canvas->setEraserOnlyHighlight(true);
            }
            syncFromCanvas();
            emit refreshToolbarRequested();
        });
        m_eraserFilterBtns.append(btn);
        erFilterRow->addWidget(btn);
    }
    eraserColumn->addLayout(erFilterRow);
    column->addWidget(m_eraserSection);

    setActivePresetIndex(-1, nullptr);
    updateSwatches();
    syncFromCanvas();
}

void StrokePopover::setPalette(const QVector<QColor>& palette) {
    m_palette = palette;
    updateSwatches();
    if (m_canvas) {
        const QColor ink = m_canvas->inkColor();
        for (int i = 0; i < m_swatches.size() && i < m_palette.size(); ++i)
            m_swatches.at(i)->setChecked(m_palette.at(i) == ink);
    }
}

void StrokePopover::setActivePresetIndex(int index, const PenPreset* activePreset) {
    if (!m_favMenuBtn)
        return;
    const bool hasActive = (index >= 0 && activePreset != nullptr);
    m_favMenuBtn->setText(hasActive ? QStringLiteral("★") : QStringLiteral("☆"));
    if (hasActive) {
        const QString sc = activePreset->shortcut.isEmpty() ? tr("sin atajo") : activePreset->shortcut;
        m_favMenuBtn->setToolTip(
            tr("Favorito activo: %1 (%2)\nClic para guardar o cambiar pincel favorito")
                .arg(activePreset->label, sc));
    } else {
        m_favMenuBtn->setToolTip(tr("Guardar pincel actual en Favoritos o elegir un favorito"));
    }
}

void StrokePopover::syncFromCanvas() {
    if (!m_canvas)
        return;

    m_updatingUi = true;

    const ToolType tool = m_canvas->tool();
    const bool isHl = (tool == ToolType::Highlighter);
    const bool isPen = (tool == ToolType::Pen || tool == ToolType::Ballpoint);
    const bool erasing = (tool == ToolType::Eraser);
    const bool filling = (tool == ToolType::Fill);
    const bool lassoTool = (tool == ToolType::Lasso);

    auto changed = [](const QWidget* w, bool show) {
        return w && (w->isHidden() == show);
    };
    const bool showStyle = !erasing && !lassoTool;
    const bool showGeom = !filling && !lassoTool;
    const bool showStab = showStyle && !filling && !isHl;
    const bool visChanged = changed(m_penTipSection, isPen)
                            || changed(m_hlTipSection, isHl)
                            || changed(m_lassoModeSection, lassoTool)
                            || changed(m_strokeStyleSection, showStyle)
                            || changed(m_strokeGeometrySection, showGeom)
                            || changed(m_fillStyleSection, filling)
                            || changed(m_stabilizerRow, showStab)
                            || changed(m_hlOpacityRow, isHl)
                            || changed(m_strokeBeautifyBtn, isPen)
                            || changed(m_eraserSection, erasing);
    const QRect oldGeometry = geometry();

    const int sizeLevel = sizeToLevel(m_canvas->strokeSize(), isHl);
    if (m_sizeSlider && m_sizeSlider->value() != sizeLevel)
        m_sizeSlider->setValue(sizeLevel);

    updateStrokePreview();
    updateFillSwatches();
    updateTipIcons();

    if (m_penTipSection)
        m_penTipSection->setVisible(isPen);
    for (int i = 0; i < m_penTipBtns.size(); ++i)
        m_penTipBtns.at(i)->setChecked(i == m_canvas->penTip());

    if (m_hlTipSection)
        m_hlTipSection->setVisible(isHl);
    const bool isRectHl = (m_canvas->highlighterTip() == 2);
    if (m_hlTipBtns.size() >= 2) {
        m_hlTipBtns.at(0)->setChecked(!isRectHl);
        m_hlTipBtns.at(1)->setChecked(isRectHl);
    }

    if (m_stabilizerSlider) {
        const int stabilizerLevel = qRound(m_canvas->stabilizer() * m_stabilizerSlider->maximum());
        if (m_stabilizerSlider->value() != stabilizerLevel)
            m_stabilizerSlider->setValue(stabilizerLevel);
    }

    if (m_strokeBeautifyBtn)
        m_strokeBeautifyBtn->setChecked(m_canvas->mathBeautifyEnabled());

    const QColor ink = m_canvas->inkColor();
    for (int i = 0; i < m_swatches.size() && i < m_palette.size(); ++i)
        m_swatches.at(i)->setChecked(m_palette.at(i) == ink);

    if (m_lassoModeSection)
        m_lassoModeSection->setVisible(lassoTool);
    if (m_lassoFreeBtn)
        m_lassoFreeBtn->setChecked(!m_canvas->lassoRectMode());
    if (m_lassoRectBtn)
        m_lassoRectBtn->setChecked(m_canvas->lassoRectMode());

    if (m_strokeStyleSection)
        m_strokeStyleSection->setVisible(!erasing && !lassoTool);
    if (m_strokeGeometrySection)
        m_strokeGeometrySection->setVisible(!filling && !lassoTool);
    if (m_fillStyleSection)
        m_fillStyleSection->setVisible(filling);
    for (const auto& [button, pattern] : m_fillStyles)
        button->setChecked(pattern == m_canvas->shadePattern());

    if (m_stabilizerRow)
        m_stabilizerRow->setVisible(!erasing && !filling && !lassoTool && !isHl);
    if (m_hlOpacityRow) {
        m_hlOpacityRow->setVisible(isHl);
        if (m_hlOpacitySlider) {
            const double curOp = m_canvas->highlighterOpacity();
            int bestIdx = 3;
            double bestDist = 1e9;
            for (int i = 0; i < 10; ++i) {
                const double d = std::abs(kHlOpacityLevels[i] - curOp);
                if (d < bestDist) {
                    bestDist = d;
                    bestIdx = i;
                }
            }
            if (m_hlOpacitySlider->value() != bestIdx + 1)
                m_hlOpacitySlider->setValue(bestIdx + 1);
        }
    }

    if (m_strokeBeautifyBtn)
        m_strokeBeautifyBtn->setVisible(isPen);

    if (m_eraserSection)
        m_eraserSection->setVisible(erasing);
    if (m_eraserStrokeBtn)
        m_eraserStrokeBtn->setChecked(m_canvas->eraserMode() == EraserMode::Stroke);
    if (m_eraserPartialBtn)
        m_eraserPartialBtn->setChecked(m_canvas->eraserMode() == EraserMode::Partial);
    const int eraserFilterIdx = m_canvas->eraserOnlyHighlight()
        ? 2
        : (m_canvas->eraserPreserveShapes() ? 1 : 0);
    for (int i = 0; i < m_eraserFilterBtns.size(); ++i)
        m_eraserFilterBtns.at(i)->setChecked(i == eraserFilterIdx);

    const int eraserLevel = std::clamp(qRound(m_canvas->eraserSize() / 8.0), 1, 10);
    if (m_eraserSizeSlider && m_eraserSizeSlider->value() != eraserLevel)
        m_eraserSizeSlider->setValue(eraserLevel);
    updateEraserPreview();

    m_updatingUi = false;

    if (visChanged) {
        adjustSize();
        if (geometry() != oldGeometry || !isHidden())
            emit layoutAdjusted();
    }
}

void StrokePopover::applyTheme(const ThemeConfig& theme, const QString& qss) {
    m_theme = theme;
    GlassCapsule::applyTheme(theme, qss);
    updateSwatches();

    for (QToolButton* b : findChildren<QToolButton*>(QStringLiteral("finilySwitch"))) {
        if (auto* sw = dynamic_cast<SwitchButton*>(b))
            sw->setColors(theme.uiText, theme.accent, theme.isDark);
    }
    for (QSlider* sl : {m_sizeSlider, m_stabilizerSlider, m_hlOpacitySlider, m_eraserSizeSlider}) {
        if (auto* ds = dynamic_cast<DotStepSlider*>(sl))
            ds->setColors(theme.accent, theme.isDark);
    }
    for (const auto& spec : m_iconSpecs)
        spec.first->setIcon(glyphIcon(spec.second, theme.uiText));
    for (const auto& spec : m_ctxIconSpecs)
        spec.first->setIcon(QIcon(contextIconPixmap(spec.second, theme.uiText)));
    if (m_stabilizerIcon)
        m_stabilizerIcon->setPixmap(contextIconPixmap(QStringLiteral("stabilizer"), theme.uiText));
    if (m_hlOpacityIcon)
        m_hlOpacityIcon->setPixmap(contextIconPixmap(QStringLiteral("opacity"), theme.uiText));

    updateTipIcons();
    updateFillSwatches();
    updateEraserPreview();
}

void StrokePopover::updateSwatches() {
    const QColor rim = m_theme.isDark ? QColor(255, 255, 255, 70) : QColor(15, 23, 42, 80);
    for (int i = 0; i < m_swatches.size() && i < m_palette.size(); ++i) {
        ColorDotButton* swatch = m_swatches.at(i);
        swatch->setDotColor(m_palette.at(i));
        swatch->setAccentColor(m_theme.accent);
        swatch->setRimColor(rim);
        swatch->setToolTip(m_palette.at(i).name());
    }
}

void StrokePopover::updateStrokePreview() {
    if (m_canvas && m_strokePreview)
        m_strokePreview->setInk(m_canvas->inkColor(), m_canvas->strokeSize());
}

void StrokePopover::updateFillSwatches() {
    const QColor col = m_canvas ? m_canvas->inkColor() : m_theme.accent;
    for (const auto& [button, pattern] : m_fillStyles)
        button->setIcon(QIcon(fillSwatchPixmap(pattern, col, m_theme.isDark)));
}

void StrokePopover::updateTipIcons() {
    const QColor col = m_canvas ? m_canvas->inkColor() : m_theme.accent;
    for (int i = 0; i < m_penTipBtns.size(); ++i)
        m_penTipBtns.at(i)->setIcon(QIcon(penTipPixmap(i, col, m_theme.isDark)));
    for (int i = 0; i < m_hlTipBtns.size(); ++i)
        m_hlTipBtns.at(i)->setIcon(QIcon(hlTipPixmap(i, col, m_theme.isDark)));
}

void StrokePopover::updateEraserPreview() {
    if (!m_eraserPreview)
        return;
    const int level = m_eraserSizeSlider
        ? m_eraserSizeSlider->value()
        : (m_canvas ? std::clamp(qRound(m_canvas->eraserSize() / 8.0), 1, 10) : 2);
    QColor erCol = m_theme.uiText;
    erCol.setAlpha(185);
    m_eraserPreview->setInk(erCol, 4.0 + level * 1.8);
}

} // namespace finily
