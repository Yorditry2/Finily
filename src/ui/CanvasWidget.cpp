#include "CanvasWidget.h"
#include "CanvasWidgetInternal.h"
#include "core/Utils.h"
#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"
#include "ui/editor/EditorTheme.h"

#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace finily {

namespace {

using BankKey = CanvasWidget::BankKey;

QString bankSettingsPrefix(BankKey key) {
    switch (key) {
    case BankKey::Pen:
        return QStringLiteral("bank/pen");
    case BankKey::Highlighter:
        return QStringLiteral("bank/highlighter");
    case BankKey::Eraser:
        return QStringLiteral("bank/eraser");
    case BankKey::Laser:
        return QStringLiteral("bank/laser");
    }
    return QStringLiteral("bank/pen");
}

ToolStrokeBank defaultToolBank(BankKey key) {
    ToolStrokeBank bank;
    switch (key) {
    case BankKey::Pen:
        bank.widths = {1.2, 2.4, 4.5};
        bank.activeSlot = 1;
        bank.palette = {
            QColor(QStringLiteral("#18181B")),
            QColor(QStringLiteral("#2563EB")),
            QColor(QStringLiteral("#DC2626")),
            QColor(QStringLiteral("#16A34A")),
            QColor(QStringLiteral("#D97706")),
            QColor(QStringLiteral("#7C3AED")),
        };
        break;
    case BankKey::Highlighter:
        bank.widths = {6.0, 10.0, 16.0};
        bank.activeSlot = 1;
        bank.palette = {
            QColor(QStringLiteral("#FACC15")),
            QColor(QStringLiteral("#86EFAC")),
            QColor(QStringLiteral("#93C5FD")),
            QColor(QStringLiteral("#F9A8D4")),
            QColor(QStringLiteral("#FDBA74")),
            QColor(QStringLiteral("#C4B5FD")),
        };
        break;
    case BankKey::Eraser:
        bank.widths = {8.0, 16.0, 28.0};
        bank.activeSlot = 1;
        break;
    case BankKey::Laser:
        bank.widths = {3.0, 4.0, 6.0};
        bank.activeSlot = 1;
        bank.palette = {
            QColor(QStringLiteral("#EF4444")),
            QColor(QStringLiteral("#22C55E")),
            QColor(QStringLiteral("#3B82F6")),
            QColor(QStringLiteral("#F59E0B")),
            QColor(QStringLiteral("#EC4899")),
            QColor(QStringLiteral("#8B5CF6")),
        };
        break;
    }
    return bank;
}

ToolStrokeBank loadBankFromSettings(QSettings& settings, BankKey key) {
    ToolStrokeBank bank = defaultToolBank(key);
    const QString prefix = bankSettingsPrefix(key);
    const QVariantList rawWidths = settings.value(prefix + QStringLiteral("/widths")).toList();
    if (rawWidths.size() == 3) {
        for (int i = 0; i < 3; ++i) {
            bool ok = false;
            const double val = rawWidths.at(i).toDouble(&ok);
            if (ok && isFinite(val))
                bank.widths[size_t(i)] = std::clamp(val, 0.2, 200.0);
        }
    }
    bank.activeSlot = std::clamp(settings.value(prefix + QStringLiteral("/activeSlot"), bank.activeSlot).toInt(), 0, 2);
    const QStringList rawPalette = settings.value(prefix + QStringLiteral("/palette")).toStringList();
    if (!rawPalette.isEmpty()) {
        QVector<QColor> loaded;
        loaded.reserve(rawPalette.size());
        for (const QString& hex : rawPalette) {
            const QColor c(hex);
            if (c.isValid())
                loaded.append(c);
        }
        if (!loaded.isEmpty())
            bank.palette = std::move(loaded);
    }
    if (!bank.palette.isEmpty()) {
        bank.activeColorIndex = std::clamp(
            settings.value(prefix + QStringLiteral("/activeColorIndex"), bank.activeColorIndex).toInt(),
            0, int(bank.palette.size()) - 1);
    } else {
        bank.activeColorIndex = 0;
    }
    return bank;
}

void writeBankToSettings(QSettings& settings, BankKey key, const ToolStrokeBank& bank) {
    const QString prefix = bankSettingsPrefix(key);
    QVariantList widthList;
    widthList.reserve(3);
    for (double w : bank.widths)
        widthList.append(w);
    settings.setValue(prefix + QStringLiteral("/widths"), widthList);
    settings.setValue(prefix + QStringLiteral("/activeSlot"), std::clamp(bank.activeSlot, 0, 2));
    QStringList hexList;
    hexList.reserve(bank.palette.size());
    for (const QColor& c : bank.palette)
        hexList.append(c.name(QColor::HexRgb));
    settings.setValue(prefix + QStringLiteral("/palette"), hexList);
    settings.setValue(prefix + QStringLiteral("/activeColorIndex"), bank.activeColorIndex);
}

int sanitizeLaserDuration(int ms) {
    if (ms >= 3000)
        return 4000;
    if (ms >= 1500)
        return 2000;
    return 1000;
}

} // namespace

CanvasWidget::CanvasWidget(QWidget* parent)
    : QWidget(parent)
    , m_stabilizerFilter(std::make_unique<VelocityGaussianStabilizer>()) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_StaticContents);
    setAttribute(Qt::WA_TabletTracking);
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    m_stabilizerFilter->setStrength(m_stabilizer);

    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(380);
    connect(m_holdTimer, &QTimer::timeout, this, &CanvasWidget::triggerHoldToShape);

    m_laserTimer = new QTimer(this);
    m_laserTimer->setInterval(25);
    connect(m_laserTimer, &QTimer::timeout, this, [this] {
        const auto now = nowMs();
        if (m_laserMode == LaserMode::Dot) {
            if (m_laserDotFading && now - m_laserDotReleaseMs >= 180) {
                m_laserTrail.clear();
                m_laserDotFading = false;
                m_laserTimer->stop();
            }
        } else {
            const int maxAge = std::max(100, m_laserDurationMs);
            m_laserTrail.removeIf([now, maxAge](const LaserSample& sample) {
                return now - sample.timestampMs >= maxAge;
            });
            if (m_laserTrail.isEmpty())
                m_laserTimer->stop();
        }
        updateLaserDirtyRect();
    });

    m_zoomAnimation = new QVariantAnimation(this);
    m_zoomAnimation->setDuration(140);
    m_zoomAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_zoomAnimation->setStartValue(0.0);
    m_zoomAnimation->setEndValue(1.0);
    connect(m_zoomAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const double t = value.toDouble();
        m_doc.viewport() = {
            m_zoomStart.x + (m_zoomTarget.x - m_zoomStart.x) * t,
            m_zoomStart.y + (m_zoomTarget.y - m_zoomStart.y) * t,
            m_zoomStart.zoom + (m_zoomTarget.zoom - m_zoomStart.zoom) * t
        };
        emit viewportChanged(m_doc.viewport().zoom);
        update();
    });
    connect(m_zoomAnimation, &QVariantAnimation::finished, this, &CanvasWidget::invalidateStaticCache);

    m_shapePulseAnimation = new QVariantAnimation(this);
    m_shapePulseAnimation->setDuration(220);
    m_shapePulseAnimation->setStartValue(1.0);
    m_shapePulseAnimation->setEndValue(0.0);
    connect(m_shapePulseAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                m_shapePulseAlpha = value.toDouble();
                update(shapePulseRect());
            });

    // Autoguardado diferido: se agrupan los cambios y se escribe 1.5 s después
    // del último, nunca en mitad de un trazo.
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setSingleShot(true);
    m_autosaveTimer->setInterval(1500);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] {
        if (m_mode != InteractionMode::None) {
            m_autosaveTimer->start(1500);
            return;
        }
        writeAutosaveNow();
    });

    m_beautifyTimer = new QTimer(this);
    m_beautifyTimer->setSingleShot(true);
    m_beautifyTimer->setInterval(650);
    connect(m_beautifyTimer, &QTimer::timeout, this, &CanvasWidget::flushPendingBeautify);

    {
        auto settings = appSettings();
        const double startupScale =
            clampTemplateScale(settings->value("ink/templateScale", 1.0).toDouble());
        m_doc.configureBlank(ThemeId::DarkBoard, BackgroundTemplate::Dots, PageFormat::Infinite,
                             startupScale, CoverStyle{}, Viewport{});
    }
    loadToolBanksAndSettings();

    loadAutosave();
    refreshActiveColorFromTheme();
    updateCursorShape();
}

CanvasWidget::~CanvasWidget() {
    finishTextEditor(false);
    m_autosaveTimer->stop();
    waitForAutosave();
    writeAutosaveNow(false);
}

void CanvasWidget::setTool(ToolType tool) {
    clearPenCursor();
    clearEraserCursor();
    const bool hadRadial = m_radialOpen;
    m_radialOpen = false;
    finishTextEditor(true);
    if (m_tool == ToolType::Select && tool != m_tool && m_mode != InteractionMode::None)
        cancelInteraction();
    if (m_polylineActive && tool != m_tool)
        finishActivePolyline();
    if ((hasLassoSelection() || hasSelectedImage()) && tool != ToolType::Lasso && tool != ToolType::Select)
        clearSelection();
    if (tool == ToolType::Lasso && m_tool != ToolType::Lasso)
        clearSelection();
    if (tool == ToolType::Select && m_tool == ToolType::Lasso && hasLassoSelection())
        clearSelection();
    if (tool == ToolType::Pen || tool == ToolType::Ballpoint || tool == ToolType::Highlighter)
        m_lastWriteTool = tool;
    m_tool = tool;
    syncActiveToolFromBank();
    if (tool == ToolType::Polyline && m_linePattern == LinePattern::Dimension)
        m_linePattern = LinePattern::Solid;
    if (tool != ToolType::Text && tool != ToolType::Lasso && tool != ToolType::Select
        && tool != ToolType::Shape && tool != ToolType::Polyline
        && tool != ToolType::Pen && tool != ToolType::Ballpoint) {
        clearSelection();
    }
    updateCursorShape();
    if (hadRadial)
        update(radialMenuRect());
    emit stateChanged();
}

void CanvasWidget::setShapeKind(ShapeKind kind) {
    m_shapeKind = kind;
    emit stateChanged();
}

void CanvasWidget::setPolygonSides(int sides) {
    m_polygonSides = std::clamp(sides, 3, 12);
    emit stateChanged();
}

void CanvasWidget::setShadePattern(ShadePattern pattern) {
    m_shadePattern = pattern;
    emit stateChanged();
}

void CanvasWidget::setHoldToShapeEnabled(bool enabled) {
    m_holdToShapeEnabled = enabled;
    if (!enabled)
        m_holdTimer->stop();
    emit stateChanged();
}

void CanvasWidget::setSmartGuidesEnabled(bool enabled) {
    m_smartGuidesEnabled = enabled;
    m_smartGuideOptions.enabled = enabled;
    writeAppSetting(QStringLiteral("smartGuide/enabled"), enabled);
    if (!enabled)
        m_activeGuides.clear();
    update();
    emit stateChanged();
}

void CanvasWidget::setMathBeautifyEnabled(bool enabled) {
    if (m_mathBeautifyEnabled == enabled)
        return;
    if (!enabled)
        flushPendingBeautify();
    m_mathBeautifyEnabled = enabled;
    writeAppSetting(QStringLiteral("ink/mathBeautify"), enabled);
    emit toastRequested(enabled ? QStringLiteral("Auto-embellecer escritura activado (M)")
                                : QStringLiteral("Modo Escritura libre (M)"));
    emit stateChanged();
}

void CanvasWidget::setAutoColorOperators(bool enabled) {
    if (m_autoColorOperators == enabled)
        return;
    m_autoColorOperators = enabled;
    writeAppSetting(QStringLiteral("ink/autoColorOperators"), enabled);
    emit stateChanged();
}

void CanvasWidget::setOperatorColor(const QColor& color) {
    if (!color.isValid() || m_operatorColor == color)
        return;
    m_operatorColor = color;
    writeAppSetting(QStringLiteral("ink/operatorColor"), color.name(QColor::HexRgb));
    emit stateChanged();
}

void CanvasWidget::setCursorStyle(int style) {
    style = std::clamp(style, 0, 2);
    if (m_cursorStyle == style)
        return;
    m_cursorStyle = style;
    writeAppSetting(QStringLiteral("ink/cursorStyle"), style);
    clearPenCursor();
    emit stateChanged();
}

void CanvasWidget::setPenTip(int tip) {
    tip = std::clamp(tip, 0, 3);
    m_penTip = tip;
    if (m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint) {
        m_tool = (tip == 0) ? ToolType::Ballpoint : ToolType::Pen;
        m_lastWriteTool = m_tool;
    }
    writeAppSetting(QStringLiteral("ink/penTip"), tip);
    emit stateChanged();
}

void CanvasWidget::setHighlighterTip(int tip) {
    tip = std::clamp(tip, 0, 2);
    if (m_highlighterTip == tip)
        return;
    m_highlighterTip = tip;
    writeAppSetting(QStringLiteral("ink/highlighterTip"), tip);
    emit stateChanged();
}

void CanvasWidget::setSwapPenButtons(bool swapped) {
    m_swapPenButtons = swapped;
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::setLassoRectMode(bool rectMode) {
    if (m_lassoRectMode == rectMode)
        return;
    m_lassoRectMode = rectMode;
    writeAppSetting(QStringLiteral("lasso/rectMode"), rectMode);
    emit stateChanged();
}

void CanvasWidget::setLassoTargetFilter(const LassoTargetFilter& filter) {
    if (m_lassoFilter.ink == filter.ink
        && m_lassoFilter.shapes == filter.shapes
        && m_lassoFilter.media == filter.media) {
        return;
    }
    m_lassoFilter = filter;
    writeAppSetting(QStringLiteral("lasso/grabInk"), filter.ink);
    writeAppSetting(QStringLiteral("lasso/grabShapes"), filter.shapes);
    writeAppSetting(QStringLiteral("lasso/grabMedia"), filter.media);
    emit stateChanged();
}

void CanvasWidget::setEraserMode(EraserMode mode) {
    if (m_eraserMode == mode)
        return;
    m_eraserMode = mode;
    writeAppSetting(QStringLiteral("eraser/mode"), int(mode));
    emit stateChanged();
}

void CanvasWidget::setEraserSize(double size) {
    if (!isFinite(size))
        return;
    const double clamped = std::clamp(size, 1.0, 500.0);
    m_eraserSize = clamped;
    const int slot = std::clamp(m_eraserBank.activeSlot, 0, 2);
    m_eraserBank.widths[size_t(slot)] = clamped;
    saveToolBank(BankKey::Eraser);
    update();
    emit stateChanged();
}

void CanvasWidget::setEraserPreserveShapes(bool preserve) {
    if (m_eraserPreserveShapes == preserve)
        return;
    m_eraserPreserveShapes = preserve;
    if (!m_eraserOnlyHighlight)
        m_eraserTargets.shapes = !preserve;
    writeAppSetting(QStringLiteral("ink/eraserPreserveShapes"), preserve);
    writeAppSetting(QStringLiteral("eraser/targetShapes"), m_eraserTargets.shapes);
    emit stateChanged();
}

void CanvasWidget::setEraserOnlyHighlight(bool onlyHighlight) {
    if (m_eraserOnlyHighlight == onlyHighlight)
        return;
    m_eraserOnlyHighlight = onlyHighlight;
    if (onlyHighlight) {
        m_eraserTargets = {false, true, false};
    } else {
        m_eraserTargets = {true, true, !m_eraserPreserveShapes};
    }
    writeAppSetting(QStringLiteral("ink/eraserOnlyHighlight"), onlyHighlight);
    writeAppSetting(QStringLiteral("eraser/targetInk"), m_eraserTargets.ink);
    writeAppSetting(QStringLiteral("eraser/targetHighlighter"), m_eraserTargets.highlighter);
    writeAppSetting(QStringLiteral("eraser/targetShapes"), m_eraserTargets.shapes);
    emit stateChanged();
}

void CanvasWidget::setEraserTargets(const EraserTargets& targets) {
    if (m_eraserTargets.ink == targets.ink
        && m_eraserTargets.highlighter == targets.highlighter
        && m_eraserTargets.shapes == targets.shapes) {
        return;
    }
    m_eraserTargets = targets;
    m_eraserOnlyHighlight = !targets.ink && targets.highlighter && !targets.shapes;
    m_eraserPreserveShapes = !targets.shapes;
    writeAppSetting(QStringLiteral("eraser/targetInk"), targets.ink);
    writeAppSetting(QStringLiteral("eraser/targetHighlighter"), targets.highlighter);
    writeAppSetting(QStringLiteral("eraser/targetShapes"), targets.shapes);
    writeAppSetting(QStringLiteral("ink/eraserOnlyHighlight"), m_eraserOnlyHighlight);
    writeAppSetting(QStringLiteral("ink/eraserPreserveShapes"), m_eraserPreserveShapes);
    emit stateChanged();
}

void CanvasWidget::setInkColor(const QColor& color) {
    if (!color.isValid())
        return;
    clearPenCursor();
    m_color = color;
    const BankKey key = bankKeyForTool(m_tool == ToolType::Eraser ? m_lastWriteTool : m_tool);
    if (key != BankKey::Eraser) {
        auto& bank = bankForKey(key);
        const bool dark = isCurrentSheetDark();
        for (int i = 0; i < bank.palette.size(); ++i) {
            const QColor candidate = (key == BankKey::Pen)
                ? resolveAdaptableInkColor(bank.palette.at(i), dark)
                : bank.palette.at(i);
            if (candidate == color || bank.palette.at(i) == color) {
                bank.activeColorIndex = i;
                saveToolBank(key);
                break;
            }
        }
        if (key == BankKey::Laser)
            m_laserColor = color;
    }
    // Cambio de color en 1 clic si hay objetos seleccionados (estilo Xournal++ / PDF Annotator)
    if (m_mode == InteractionMode::None && !m_polylineActive && hasSelection()) {
        const uint64_t revBefore = m_doc.revision();
        SelectionStyle edits;
        edits.color = color;
        setSelectionStyle(edits);
        if (m_doc.revision() == revBefore)
            emit stateChanged();
        return;
    }
    emit stateChanged();
}

void CanvasWidget::setStrokeSize(double size) {
    if (!isFinite(size))
        return;
    const double width = std::clamp(size, 0.1, 200.0);
    clearPenCursor();
    m_size = width;
    const BankKey key = bankKeyForTool(m_tool);
    if (key == BankKey::Pen || key == BankKey::Highlighter) {
        auto& bank = bankForKey(key);
        const int slot = std::clamp(bank.activeSlot, 0, 2);
        bank.widths[size_t(slot)] = width;
        saveToolBank(key);
    }
    if (m_mode == InteractionMode::None && !m_polylineActive && hasSelection()) {
        const uint64_t revBefore = m_doc.revision();
        SelectionStyle edits;
        edits.width = width;
        setSelectionStyle(edits);
        if (m_doc.revision() == revBefore)
            emit stateChanged();
        return;
    }
    emit stateChanged();
}

void CanvasWidget::setLinePattern(LinePattern pattern) {
    if (m_linePattern == pattern)
        return;
    m_linePattern = pattern;
    emit stateChanged();
}

void CanvasWidget::setLineEnding(LineEnding ending) {
    if (m_lineEnding == ending)
        return;
    m_lineEnding = ending;
    emit stateChanged();
}

void CanvasWidget::setPressureSensitivity(double sensitivity) {
    if (!isFinite(sensitivity))
        return;
    const double clamped = std::clamp(sensitivity, 0.0, 1.0);
    if (std::abs(m_pressureSensitivity - clamped) < 1e-6)
        return;
    m_pressureSensitivity = clamped;
    writeAppSetting(QStringLiteral("ink/pressureSensitivity"), clamped);
    emit stateChanged();
}

void CanvasWidget::setPressureProfile(PressureProfile profile) {
    if (int(profile) < int(PressureProfile::Light) || int(profile) > int(PressureProfile::Firm))
        return;
    if (m_pressureProfile == profile)
        return;
    m_pressureProfile = profile;
    saveAutosave();
    emit stateChanged();
}

void CanvasWidget::setStabilizer(double strength) {
    if (!isFinite(strength))
        return;
    m_stabilizer = std::clamp(strength, 0.0, 1.0);
    m_stabilizerFilter->setStrength(m_stabilizer);
    emit stateChanged();
}

void CanvasWidget::setHighlighterOpacity(double opacity) {
    if (!isFinite(opacity))
        return;
    const double clamped = std::clamp(opacity, 0.10, 0.70);
    if (std::abs(m_highlighterOpacity - clamped) < 1e-6)
        return;
    m_highlighterOpacity = clamped;
    writeAppSetting(QStringLiteral("ink/highlighterOpacity"), clamped);
    emit stateChanged();
}

bool CanvasWidget::isCurrentSheetDark() const {
    return effectiveThemeConfig(m_doc.themeId(), m_doc.customBackground()).isDark;
}

CanvasWidget::BankKey CanvasWidget::bankKeyForTool(ToolType tool) {
    switch (tool) {
    case ToolType::Highlighter:
        return BankKey::Highlighter;
    case ToolType::Eraser:
        return BankKey::Eraser;
    case ToolType::Laser:
        return BankKey::Laser;
    default:
        return BankKey::Pen;
    }
}

const ToolStrokeBank& CanvasWidget::bankForKey(BankKey key) const {
    switch (key) {
    case BankKey::Pen:
        return m_penBank;
    case BankKey::Highlighter:
        return m_highlighterBank;
    case BankKey::Eraser:
        return m_eraserBank;
    case BankKey::Laser:
        return m_laserBank;
    }
    return m_penBank;
}

ToolStrokeBank& CanvasWidget::bankForKey(BankKey key) {
    switch (key) {
    case BankKey::Pen:
        return m_penBank;
    case BankKey::Highlighter:
        return m_highlighterBank;
    case BankKey::Eraser:
        return m_eraserBank;
    case BankKey::Laser:
        return m_laserBank;
    }
    return m_penBank;
}

void CanvasWidget::saveToolBank(BankKey key) const {
    auto settings = appSettings();
    writeBankToSettings(*settings, key, bankForKey(key));
}

QColor CanvasWidget::resolvedColorForBank(BankKey key) const {
    const auto& bank = bankForKey(key);
    if (bank.palette.isEmpty())
        return m_color;
    const int idx = std::clamp(bank.activeColorIndex, 0, int(bank.palette.size()) - 1);
    const QColor& stored = bank.palette.at(idx);
    if (key == BankKey::Pen)
        return resolveAdaptableInkColor(stored, isCurrentSheetDark());
    return stored;
}

void CanvasWidget::refreshActiveColorFromTheme() {
    m_laserColor = resolvedColorForBank(BankKey::Laser);
    const BankKey key = bankKeyForTool(m_tool);
    if (key == BankKey::Eraser)
        m_color = resolvedColorForBank(BankKey::Pen);
    else
        m_color = resolvedColorForBank(key);
}

void CanvasWidget::syncActiveToolFromBank() {
    const BankKey key = bankKeyForTool(m_tool);
    const auto& bank = bankForKey(key);
    const int slot = std::clamp(bank.activeSlot, 0, 2);
    if (key == BankKey::Eraser) {
        m_eraserSize = bank.widths[size_t(slot)];
    } else if (key == BankKey::Laser) {
        m_laserColor = resolvedColorForBank(BankKey::Laser);
        m_color = m_laserColor;
    } else {
        m_size = bank.widths[size_t(slot)];
        m_color = resolvedColorForBank(key);
    }
}

void CanvasWidget::loadToolBanksAndSettings() {
    auto settings = appSettings();
    m_penBank = loadBankFromSettings(*settings, BankKey::Pen);
    m_highlighterBank = loadBankFromSettings(*settings, BankKey::Highlighter);
    m_eraserBank = loadBankFromSettings(*settings, BankKey::Eraser);
    m_laserBank = loadBankFromSettings(*settings, BankKey::Laser);

    m_mathBeautifyEnabled = settings->value(QStringLiteral("ink/mathBeautify"), false).toBool();
    m_beautifyPauseMs = std::clamp(settings->value(QStringLiteral("ink/beautifyPauseMs"), 650).toInt(), 200, 2500);
    m_autoColorOperators = settings->value(QStringLiteral("ink/autoColorOperators"), true).toBool();
    const QColor savedOp(settings->value(QStringLiteral("ink/operatorColor"), QStringLiteral("#ef4444")).toString());
    if (savedOp.isValid())
        m_operatorColor = savedOp;
    m_showAutoMeasurements = settings->value(QStringLiteral("ink/showAutoMeasurements"), false).toBool();
    m_cursorStyle = std::clamp(settings->value(QStringLiteral("ink/cursorStyle"), 0).toInt(), 0, 2);
    m_penTip = std::clamp(settings->value(QStringLiteral("ink/penTip"), 1).toInt(), 0, 3);
    m_tipPredictionEnabled = settings->value(QStringLiteral("ink/tipPrediction"), true).toBool();
    m_fountainFlickEnabled = settings->value(QStringLiteral("ink/fountainFlick"), true).toBool();
    m_highlighterTip = std::clamp(settings->value(QStringLiteral("ink/highlighterTip"), 0).toInt(), 0, 2);
    m_hlDrawBehindInk = settings->value(QStringLiteral("highlighter/drawBehindInk"), true).toBool();
    m_hlStraightenOnRelease = settings->value(QStringLiteral("highlighter/straightenOnRelease"), false).toBool();
    m_hlRoundTip = settings->value(QStringLiteral("highlighter/roundTip"), false).toBool();
    m_pressureSensitivity =
        std::clamp(settings->value(QStringLiteral("ink/pressureSensitivity"), 0.5).toDouble(), 0.0, 1.0);
    m_highlighterOpacity =
        std::clamp(settings->value(QStringLiteral("ink/highlighterOpacity"), 0.25).toDouble(), 0.10, 0.70);

    m_eraserPreserveShapes = settings->value(QStringLiteral("ink/eraserPreserveShapes"), false).toBool();
    m_eraserOnlyHighlight = settings->value(QStringLiteral("ink/eraserOnlyHighlight"), false).toBool();
    const int savedEraserMode = settings->value(QStringLiteral("eraser/mode"), int(EraserMode::Stroke)).toInt();
    m_eraserMode = (savedEraserMode == int(EraserMode::Partial)) ? EraserMode::Partial : EraserMode::Stroke;
    if (settings->contains(QStringLiteral("eraser/targetInk"))
        || settings->contains(QStringLiteral("eraser/targetHighlighter"))
        || settings->contains(QStringLiteral("eraser/targetShapes"))) {
        m_eraserTargets.ink = settings->value(QStringLiteral("eraser/targetInk"), true).toBool();
        m_eraserTargets.highlighter = settings->value(QStringLiteral("eraser/targetHighlighter"), true).toBool();
        m_eraserTargets.shapes = settings->value(QStringLiteral("eraser/targetShapes"), false).toBool();
        m_eraserOnlyHighlight = !m_eraserTargets.ink && m_eraserTargets.highlighter && !m_eraserTargets.shapes;
        m_eraserPreserveShapes = !m_eraserTargets.shapes;
    } else {
        m_eraserTargets.ink = !m_eraserOnlyHighlight;
        m_eraserTargets.highlighter = true;
        m_eraserTargets.shapes = !m_eraserOnlyHighlight && !m_eraserPreserveShapes;
    }

    m_lassoFilter.ink = settings->value(QStringLiteral("lasso/grabInk"), true).toBool();
    m_lassoFilter.shapes = settings->value(QStringLiteral("lasso/grabShapes"), true).toBool();
    m_lassoFilter.media = settings->value(QStringLiteral("lasso/grabMedia"), true).toBool();
    m_lassoRectMode = settings->value(QStringLiteral("lasso/rectMode"), false).toBool();

    const int savedLaserMode = settings->value(QStringLiteral("laser/mode"), int(LaserMode::Trail)).toInt();
    m_laserMode = (savedLaserMode == int(LaserMode::Dot)) ? LaserMode::Dot : LaserMode::Trail;
    m_laserDurationMs = sanitizeLaserDuration(settings->value(QStringLiteral("laser/durationMs"), 1000).toInt());

    m_autoShapeOptions.line = settings->value(QStringLiteral("autoShape/line"), true).toBool();
    m_autoShapeOptions.curve = settings->value(QStringLiteral("autoShape/curve"), true).toBool();
    m_autoShapeOptions.circle = settings->value(QStringLiteral("autoShape/circle"), true).toBool();
    m_autoShapeOptions.triangle = settings->value(QStringLiteral("autoShape/triangle"), true).toBool();
    m_autoShapeOptions.rectangle = settings->value(QStringLiteral("autoShape/rectangle"), true).toBool();
    m_autoShapeOptions.quadrilateral = settings->value(QStringLiteral("autoShape/quadrilateral"), true).toBool();
    m_autoShapeOptions.polygon = settings->value(QStringLiteral("autoShape/polygon"), true).toBool();
    m_autoShapeOptions.radical = settings->value(QStringLiteral("autoShape/radical"), true).toBool();
    m_autoShapeOptions.rightAngleL = settings->value(QStringLiteral("autoShape/rightAngleL"), true).toBool();
    m_holdToShapeDelayMs = std::clamp(settings->value(QStringLiteral("autoShape/holdDelayMs"), 380).toInt(), 150, 1000);

    m_gestureOptions.angleArc = settings->value(QStringLiteral("gesture/angleArc"), true).toBool();
    m_gestureOptions.rightAngle = settings->value(QStringLiteral("gesture/rightAngle"), true).toBool();
    m_gestureOptions.freehandArrow = settings->value(QStringLiteral("gesture/freehandArrow"), true).toBool();
    m_gestureOptions.attachArrowhead = settings->value(QStringLiteral("gesture/attachArrowhead"), true).toBool();
    m_gestureOptions.dividerDot = settings->value(QStringLiteral("gesture/dividerDot"), true).toBool();
    m_gestureOptions.edgeMarks = settings->value(QStringLiteral("gesture/edgeMarks"), true).toBool();
    m_gestureOptions.holdEdgeMenu = settings->value(QStringLiteral("gesture/holdEdgeMenu"), true).toBool();

    m_smartGuideOptions.enabled = settings->value(QStringLiteral("smartGuide/enabled"), true).toBool();
    m_smartGuideOptions.snapKeyPoints = settings->value(QStringLiteral("smartGuide/snapKeyPoints"), true).toBool();
    m_smartGuideOptions.orthoPerpGuides = settings->value(QStringLiteral("smartGuide/orthoPerpGuides"), true).toBool();
    m_smartGuideOptions.alignXY = settings->value(QStringLiteral("smartGuide/alignXY"), true).toBool();
    m_smartGuidesEnabled = m_smartGuideOptions.enabled;

    auto readTabletAction = [&](const QString& key, TabletAction def) {
        const int raw = settings->value(key, int(def)).toInt();
        if (raw >= int(TabletAction::None) && raw <= int(TabletAction::PrevPage))
            return static_cast<TabletAction>(raw);
        return def;
    };
    m_stylusLowerAction = readTabletAction(QStringLiteral("tablet/lowerAction"), TabletAction::RightClickMenu);
    m_stylusUpperAction = readTabletAction(QStringLiteral("tablet/upperAction"), TabletAction::HoldEraser);
    m_stylusEraserTipEnabled = settings->value(QStringLiteral("tablet/eraserTipEnabled"), true).toBool();
    m_radialMenuEnabled = settings->value(QStringLiteral("tablet/radialMenuEnabled"), true).toBool();
    m_palmRejectionEnabled = settings->value(QStringLiteral("tablet/palmRejection"), true).toBool();

    m_holdTimer->setInterval(m_holdToShapeDelayMs);
    m_beautifyTimer->setInterval(m_beautifyPauseMs);
    m_eraserSize = m_eraserBank.widths[std::clamp(m_eraserBank.activeSlot, 0, 2)];
    syncActiveToolFromBank();
    refreshActiveColorFromTheme();
}

std::array<double, 3> CanvasWidget::toolWidths(ToolType tool) const {
    return bankForKey(bankKeyForTool(tool)).widths;
}

int CanvasWidget::activeWidthSlot(ToolType tool) const {
    return bankForKey(bankKeyForTool(tool)).activeSlot;
}

void CanvasWidget::setToolWidthSlot(ToolType tool, int slot) {
    slot = std::clamp(slot, 0, 2);
    const BankKey key = bankKeyForTool(tool);
    auto& bank = bankForKey(key);
    bank.activeSlot = slot;
    saveToolBank(key);
    if (bankKeyForTool(m_tool) == key)
        syncActiveToolFromBank();
    clearPenCursor();
    update();
    emit stateChanged();
}

void CanvasWidget::setToolFineWidth(ToolType tool, double mm) {
    if (!isFinite(mm))
        return;
    const double clamped = std::clamp(mm, 0.2, 200.0);
    const BankKey key = bankKeyForTool(tool);
    auto& bank = bankForKey(key);
    const int slot = std::clamp(bank.activeSlot, 0, 2);
    bank.widths[size_t(slot)] = clamped;
    saveToolBank(key);
    if (bankKeyForTool(m_tool) == key)
        syncActiveToolFromBank();
    clearPenCursor();
    update();
    emit stateChanged();
}

QVector<QColor> CanvasWidget::toolPalette(ToolType tool) const {
    return bankForKey(bankKeyForTool(tool)).palette;
}

int CanvasWidget::activeColorIndex(ToolType tool) const {
    return bankForKey(bankKeyForTool(tool)).activeColorIndex;
}

void CanvasWidget::setToolColorIndex(ToolType tool, int idx) {
    const BankKey key = bankKeyForTool(tool);
    auto& bank = bankForKey(key);
    if (bank.palette.isEmpty())
        return;
    idx = std::clamp(idx, 0, int(bank.palette.size()) - 1);
    bank.activeColorIndex = idx;
    saveToolBank(key);
    if (key == BankKey::Laser)
        m_laserColor = resolvedColorForBank(BankKey::Laser);
    if (bankKeyForTool(m_tool) == key)
        syncActiveToolFromBank();
    clearPenCursor();
    update();
    emit stateChanged();
}

void CanvasWidget::setCustomSwatchColor(ToolType tool, const QColor& color) {
    if (!color.isValid())
        return;
    const BankKey key = bankKeyForTool(tool);
    auto& bank = bankForKey(key);
    if (bank.palette.isEmpty()) {
        bank.palette.append(color);
        bank.activeColorIndex = 0;
    } else {
        const int idx = std::clamp(bank.activeColorIndex, 0, int(bank.palette.size()) - 1);
        bank.palette[idx] = color;
        bank.activeColorIndex = idx;
    }
    saveToolBank(key);
    if (key == BankKey::Laser)
        m_laserColor = resolvedColorForBank(BankKey::Laser);
    if (bankKeyForTool(m_tool) == key)
        syncActiveToolFromBank();
    clearPenCursor();
    update();
    emit stateChanged();
}

void CanvasWidget::setToolPalette(ToolType tool, const QVector<QColor>& palette) {
    QVector<QColor> cleaned;
    cleaned.reserve(palette.size());
    for (const QColor& c : palette) {
        if (c.isValid())
            cleaned.append(c);
    }
    if (cleaned.isEmpty())
        return;
    const BankKey key = bankKeyForTool(tool);
    auto& bank = bankForKey(key);
    bank.palette = std::move(cleaned);
    bank.activeColorIndex = std::clamp(bank.activeColorIndex, 0, int(bank.palette.size()) - 1);
    saveToolBank(key);
    if (key == BankKey::Laser)
        m_laserColor = resolvedColorForBank(BankKey::Laser);
    if (bankKeyForTool(m_tool) == key)
        syncActiveToolFromBank();
    emit stateChanged();
}

void CanvasWidget::setLaserMode(LaserMode mode) {
    if (m_laserMode == mode)
        return;
    m_laserMode = mode;
    m_laserDotFading = false;
    m_laserTrail.clear();
    m_laserTimer->stop();
    updateLaserDirtyRect();
    writeAppSetting(QStringLiteral("laser/mode"), int(mode));
    emit stateChanged();
}

void CanvasWidget::setLaserDurationMs(int ms) {
    const int sanitized = sanitizeLaserDuration(ms);
    if (m_laserDurationMs == sanitized)
        return;
    m_laserDurationMs = sanitized;
    writeAppSetting(QStringLiteral("laser/durationMs"), sanitized);
    emit stateChanged();
}

void CanvasWidget::setLaserColor(const QColor& color) {
    if (!color.isValid())
        return;
    m_laserColor = color;
    for (int i = 0; i < m_laserBank.palette.size(); ++i) {
        if (m_laserBank.palette.at(i) == color) {
            m_laserBank.activeColorIndex = i;
            saveToolBank(BankKey::Laser);
            break;
        }
    }
    if (m_tool == ToolType::Laser)
        m_color = color;
    updateLaserDirtyRect();
    emit stateChanged();
}

void CanvasWidget::setAutoShapeOptions(const AutoShapeOptions& opts) {
    m_autoShapeOptions = opts;
    auto settings = appSettings();
    settings->setValue(QStringLiteral("autoShape/line"), opts.line);
    settings->setValue(QStringLiteral("autoShape/curve"), opts.curve);
    settings->setValue(QStringLiteral("autoShape/circle"), opts.circle);
    settings->setValue(QStringLiteral("autoShape/triangle"), opts.triangle);
    settings->setValue(QStringLiteral("autoShape/rectangle"), opts.rectangle);
    settings->setValue(QStringLiteral("autoShape/quadrilateral"), opts.quadrilateral);
    settings->setValue(QStringLiteral("autoShape/polygon"), opts.polygon);
    settings->setValue(QStringLiteral("autoShape/radical"), opts.radical);
    settings->setValue(QStringLiteral("autoShape/rightAngleL"), opts.rightAngleL);
    emit stateChanged();
}

void CanvasWidget::setHoldToShapeDelayMs(int ms) {
    const int clamped = std::clamp(ms, 150, 1000);
    if (m_holdToShapeDelayMs == clamped)
        return;
    m_holdToShapeDelayMs = clamped;
    m_holdTimer->setInterval(clamped);
    writeAppSetting(QStringLiteral("autoShape/holdDelayMs"), clamped);
    emit stateChanged();
}

void CanvasWidget::setGeometryGestureOptions(const GeometryGestureOptions& opts) {
    m_gestureOptions = opts;
    auto settings = appSettings();
    settings->setValue(QStringLiteral("gesture/angleArc"), opts.angleArc);
    settings->setValue(QStringLiteral("gesture/rightAngle"), opts.rightAngle);
    settings->setValue(QStringLiteral("gesture/freehandArrow"), opts.freehandArrow);
    settings->setValue(QStringLiteral("gesture/attachArrowhead"), opts.attachArrowhead);
    settings->setValue(QStringLiteral("gesture/dividerDot"), opts.dividerDot);
    settings->setValue(QStringLiteral("gesture/edgeMarks"), opts.edgeMarks);
    settings->setValue(QStringLiteral("gesture/holdEdgeMenu"), opts.holdEdgeMenu);
    emit stateChanged();
}

void CanvasWidget::setSmartGuideOptions(const SmartGuideOptions& opts) {
    m_smartGuideOptions = opts;
    m_smartGuidesEnabled = opts.enabled;
    auto settings = appSettings();
    settings->setValue(QStringLiteral("smartGuide/enabled"), opts.enabled);
    settings->setValue(QStringLiteral("smartGuide/snapKeyPoints"), opts.snapKeyPoints);
    settings->setValue(QStringLiteral("smartGuide/orthoPerpGuides"), opts.orthoPerpGuides);
    settings->setValue(QStringLiteral("smartGuide/alignXY"), opts.alignXY);
    if (!opts.enabled)
        m_activeGuides.clear();
    update();
    emit stateChanged();
}

void CanvasWidget::setTipPredictionEnabled(bool enabled) {
    if (m_tipPredictionEnabled == enabled)
        return;
    m_tipPredictionEnabled = enabled;
    writeAppSetting(QStringLiteral("ink/tipPrediction"), enabled);
    emit stateChanged();
}

void CanvasWidget::setFountainFlickEnabled(bool enabled) {
    if (m_fountainFlickEnabled == enabled)
        return;
    m_fountainFlickEnabled = enabled;
    writeAppSetting(QStringLiteral("ink/fountainFlick"), enabled);
    emit stateChanged();
}

void CanvasWidget::setHighlighterDrawBehindInk(bool behind) {
    if (m_hlDrawBehindInk == behind)
        return;
    m_hlDrawBehindInk = behind;
    writeAppSetting(QStringLiteral("highlighter/drawBehindInk"), behind);
    invalidateStaticCache();
    emit stateChanged();
}

void CanvasWidget::setHighlighterStraightenOnRelease(bool straighten) {
    if (m_hlStraightenOnRelease == straighten)
        return;
    m_hlStraightenOnRelease = straighten;
    writeAppSetting(QStringLiteral("highlighter/straightenOnRelease"), straighten);
    emit stateChanged();
}

void CanvasWidget::setHighlighterRoundTip(bool roundTip) {
    if (m_hlRoundTip == roundTip)
        return;
    m_hlRoundTip = roundTip;
    writeAppSetting(QStringLiteral("highlighter/roundTip"), roundTip);
    emit stateChanged();
}

void CanvasWidget::setBeautifyPauseMs(int ms) {
    const int clamped = std::clamp(ms, 200, 2500);
    if (m_beautifyPauseMs == clamped)
        return;
    m_beautifyPauseMs = clamped;
    m_beautifyTimer->setInterval(clamped);
    writeAppSetting(QStringLiteral("ink/beautifyPauseMs"), clamped);
    emit stateChanged();
}

void CanvasWidget::setShowAutoMeasurements(bool show) {
    if (m_showAutoMeasurements == show)
        return;
    m_showAutoMeasurements = show;
    writeAppSetting(QStringLiteral("ink/showAutoMeasurements"), show);
    update();
    emit stateChanged();
}

void CanvasWidget::setPalmRejectionEnabled(bool enabled) {
    if (m_palmRejectionEnabled == enabled)
        return;
    m_palmRejectionEnabled = enabled;
    writeAppSetting(QStringLiteral("tablet/palmRejection"), enabled);
    emit stateChanged();
}

void CanvasWidget::setStylusLowerAction(TabletAction action) {
    if (m_stylusLowerAction == action)
        return;
    m_stylusLowerAction = action;
    writeAppSetting(QStringLiteral("tablet/lowerAction"), int(action));
    emit stateChanged();
}

void CanvasWidget::setStylusUpperAction(TabletAction action) {
    if (m_stylusUpperAction == action)
        return;
    m_stylusUpperAction = action;
    writeAppSetting(QStringLiteral("tablet/upperAction"), int(action));
    emit stateChanged();
}

void CanvasWidget::setStylusEraserTipEnabled(bool enabled) {
    if (m_stylusEraserTipEnabled == enabled)
        return;
    m_stylusEraserTipEnabled = enabled;
    writeAppSetting(QStringLiteral("tablet/eraserTipEnabled"), enabled);
    emit stateChanged();
}

void CanvasWidget::setRadialMenuEnabled(bool enabled) {
    if (m_radialMenuEnabled == enabled)
        return;
    m_radialMenuEnabled = enabled;
    writeAppSetting(QStringLiteral("tablet/radialMenuEnabled"), enabled);
    if (!enabled && m_radialOpen) {
        m_radialOpen = false;
        updateCursorShape();
        update(radialMenuRect());
    }
    emit stateChanged();
}

void CanvasWidget::setThemeId(ThemeId id) {
    clearPenCursor();
    if (m_doc.customBackground().isValid()) {
        NotebookSession session = m_doc.exportSession();
        session.themeId = id;
        session.customBackground = QColor();
        session.dirty = true;
        m_doc.importSession(session);
    } else {
        m_doc.setThemeId(id);
    }
    refreshActiveColorFromTheme();
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
}

void CanvasWidget::setCustomBackground(const QColor& color) {
    const QColor normalized = color.isValid() ? color : QColor();
    if (m_doc.customBackground() == normalized)
        return;
    clearPenCursor();
    NotebookSession session = m_doc.exportSession();
    session.customBackground = normalized;
    session.dirty = true;
    m_doc.importSession(session);
    refreshActiveColorFromTheme();
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
}

void CanvasWidget::setBackgroundTemplate(BackgroundTemplate tpl) {
    if (tpl == m_doc.currentTemplate())
        return;
    const bool pageLevel = m_doc.page(m_doc.currentPageIndex()).background.has_value();
    if (pageLevel && !preparePageChange())
        return;
    if (!m_doc.setBackgroundTemplate(tpl))
        return;
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
}

void CanvasWidget::setTemplateScale(double scale) {
    const double clamped = clampTemplateScale(scale);
    if (std::abs(m_doc.templateScale() - clamped) < 1e-4)
        return;
    m_doc.setTemplateScale(clamped);
    writeAppSetting(QStringLiteral("ink/templateScale"), clamped);
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
}

QRectF CanvasWidget::activeFiniteSheetRect() const {
    for (const auto& item : m_doc.images())
        if (item.isPdfPage && !item.rect.isEmpty())
            return item.rect;
    return sheetRectForPageFormat(m_doc.pageFormat());
}

void CanvasWidget::clampFiniteViewport() {
    if (width() <= 0 || height() <= 0)
        return;
    const QRectF sheet = activeFiniteSheetRect();
    if (sheet.isEmpty())
        return;

    Viewport& vp = m_doc.viewport();
    const double availW = std::max(240.0, width() - 156.0);
    const double availH = std::max(200.0, height() - 116.0);
    const double minZoom = std::clamp(std::min(availW / sheet.width(), availH / sheet.height()), 0.2, 5.0);
    vp.zoom = std::clamp(vp.zoom, minZoom, 5.0);

    const double sheetW = sheet.width() * vp.zoom;
    const double sheetH = sheet.height() * vp.zoom;
    constexpr double marginX = 72.0;
    constexpr double marginY = 58.0;

    if (sheetW <= width() - marginX * 2.0) {
        vp.x = width() / 2.0 - sheet.center().x() * vp.zoom;
    } else {
        const double maxX = marginX - sheet.left() * vp.zoom;
        const double minX = (width() - marginX) - sheet.right() * vp.zoom;
        vp.x = std::clamp(vp.x, std::min(minX, maxX), std::max(minX, maxX));
    }

    if (sheetH <= height() - marginY * 2.0) {
        vp.y = height() / 2.0 - sheet.center().y() * vp.zoom;
    } else {
        const double maxY = marginY - sheet.top() * vp.zoom;
        const double minY = (height() - marginY) - sheet.bottom() * vp.zoom;
        vp.y = std::clamp(vp.y, std::min(minY, maxY), std::max(minY, maxY));
    }
}

void CanvasWidget::setPageFormat(PageFormat format) {
    if (!isValidPageFormat(int(format)))
        return;
    if (m_doc.pageFormat() == format)
        return;
    m_doc.setPageFormat(format);
    m_zoomAnimation->stop();
    clearPenCursor();
    if (format != PageFormat::Infinite) {
        m_doc.viewport() = fittedSheetViewport(format, size());
        clampFiniteViewport();
    }
    invalidateStaticCache();
    saveAutosave();
    emit pageStructureChanged();
    emit stateChanged();
    emit viewportChanged(m_doc.viewport().zoom);
}

QPointF CanvasWidget::screenToWorld(const QPointF& p) const {
    const Viewport& vp = m_doc.viewport();
    return {(p.x() - vp.x) / vp.zoom, (p.y() - vp.y) / vp.zoom};
}

void CanvasWidget::zoomByFactor(double factor, const QPointF& anchor) {
    if (!isFinite(factor) || factor <= 0.0)
        return;
    m_zoomAnimation->stop();
    clearPenCursor();
    const auto world = screenToWorld(anchor);
    Viewport& vp = m_doc.viewport();
    vp.zoom = std::clamp(vp.zoom * factor, 0.2, 5.0);
    vp.x = anchor.x() - world.x() * vp.zoom;
    vp.y = anchor.y() - world.y() * vp.zoom;
    clampFiniteViewport();
    invalidateStaticCache();
    emit viewportChanged(vp.zoom);
}

void CanvasWidget::animateViewport(const Viewport& target) {
    clearPenCursor();
    m_zoomAnimation->stop();
    m_zoomStart = m_doc.viewport();
    m_zoomTarget = target;
    m_zoomAnimation->start();
}

void CanvasWidget::animateZoom(double factor) {
    const QPointF anchor = rect().center();
    const Viewport base = m_zoomAnimation->state() == QAbstractAnimation::Running
        ? m_zoomTarget
        : m_doc.viewport();
    const QPointF world((anchor.x() - base.x) / base.zoom, (anchor.y() - base.y) / base.zoom);
    const double zoom = std::clamp(base.zoom * factor, 0.2, 5.0);
    Viewport target{anchor.x() - world.x() * zoom, anchor.y() - world.y() * zoom, zoom};
    if (!activeFiniteSheetRect().isEmpty()) {
        m_doc.viewport() = target;
        clampFiniteViewport();
        target = m_doc.viewport();
        m_doc.viewport() = base;
    }
    animateViewport(target);
}

void CanvasWidget::zoomIn() {
    animateZoom(1.18);
}

void CanvasWidget::zoomOut() {
    animateZoom(1.0 / 1.18);
}

void CanvasWidget::resetViewport() {
    Viewport target;
    for (const auto& image : m_doc.images()) {
        if (image.isPdfPage && !image.rect.isEmpty()) {
            target = fittedPdfViewport(image.rect, size());
            animateViewport(target);
            saveAutosave();
            return;
        }
    }
    if (m_doc.pageFormat() != PageFormat::Infinite)
        target = fittedSheetViewport(m_doc.pageFormat(), size());
    animateViewport(target);
    saveAutosave();
}

void CanvasWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    m_zoomAnimation->stop();
    const auto& imgs = m_doc.images();
    const auto pdf = std::find_if(imgs.cbegin(), imgs.cend(), [](const auto& image) {
        return image.isPdfPage && !image.rect.isEmpty();
    });
    if (pdf != imgs.cend() && !size().isEmpty()) {
        const auto previous = fittedPdfViewport(pdf->rect, event->oldSize());
        const Viewport& vp = m_doc.viewport();
        const bool wasFitted = std::abs(previous.x - vp.x) < 1.0
            && std::abs(previous.y - vp.y) < 1.0
            && std::abs(previous.zoom - vp.zoom) < 0.005;
        if (wasFitted || event->oldSize().isEmpty() || event->oldSize().width() <= 960) {
            m_doc.viewport() = fittedPdfViewport(pdf->rect, size());
            clampFiniteViewport();
            emit viewportChanged(m_doc.viewport().zoom);
        }
    } else if (m_doc.pageFormat() != PageFormat::Infinite && !size().isEmpty()) {
        const auto previous = fittedSheetViewport(m_doc.pageFormat(), event->oldSize());
        const Viewport& vp = m_doc.viewport();
        const bool wasFitted = std::abs(previous.x - vp.x) < 1.0
            && std::abs(previous.y - vp.y) < 1.0
            && std::abs(previous.zoom - vp.zoom) < 0.005;
        if (wasFitted || event->oldSize().isEmpty() || event->oldSize().width() <= 960)
            m_doc.viewport() = fittedSheetViewport(m_doc.pageFormat(), size());
        clampFiniteViewport();
        emit viewportChanged(m_doc.viewport().zoom);
    }
    invalidateStaticCache();
}

void CanvasWidget::openRadialMenu(const QPointF& centerScreen) {
    clearPenCursor();
    m_radialOpen = true;
    m_radialHoveredColor = -1;
    QPointF c = centerScreen;
    c.setX(std::clamp(c.x(), 90.0, std::max(90.0, width() - 90.0)));
    c.setY(std::clamp(c.y(), 90.0, std::max(90.0, height() - 90.0)));
    m_radialCenter = c;
    m_radialCursorPos = c;
    updateCursorShape();
    update(radialMenuRect());
}

void CanvasWidget::tabletEvent(QTabletEvent* event) {
    event->accept();
    const bool eraserTip = m_stylusEraserTipEnabled
        && (event->pointerType() == QPointingDevice::PointerType::Eraser);
    const Qt::MouseButtons btns = event->buttons();
    const Qt::MouseButton btn = event->button();

    // Lower barrel button (near nib): MiddleButton (RightButton if swapped)
    // Upper barrel button (away from nib): RightButton (MiddleButton if swapped)
    const Qt::MouseButton lowerBtn = m_swapPenButtons ? Qt::RightButton : Qt::MiddleButton;
    const Qt::MouseButton upperBtn = m_swapPenButtons ? Qt::MiddleButton : Qt::RightButton;

    const bool newlyPressedUpper = ((btns & upperBtn) && !(m_lastTabletButtons & upperBtn));
    const bool newlyPressedLower = ((btns & lowerBtn) && !(m_lastTabletButtons & lowerBtn));
    m_lastTabletButtons = btns;

    auto handleButtonTrigger = [&](TabletAction action) -> bool {
        if (action == TabletAction::None || action == TabletAction::HoldEraser)
            return false;
        if (action == TabletAction::RadialMenu) {
            if (m_radialOpen) {
                m_radialOpen = false;
                updateCursorShape();
                update(radialMenuRect());
            } else if (m_radialMenuEnabled) {
                openRadialMenu(event->position());
            }
            return true;
        }
        if (action == TabletAction::LastWriteTool) {
            const qint64 now = nowMs();
            if (m_radialOpen) {
                m_radialOpen = false;
                updateCursorShape();
                update(radialMenuRect());
                return true;
            }
            const qint64 dt = now - m_lastPenTriggerMs;
            if (m_radialMenuEnabled && m_mode == InteractionMode::None
                && m_lastPenTriggerMs > 0 && dt >= 70 && dt <= 420) {
                m_lastPenTriggerMs = 0;
                openRadialMenu(event->position());
            } else {
                m_lastPenTriggerMs = now;
                switchTabletTool(m_lastWriteTool);
            }
            return true;
        }
        if (m_radialOpen) {
            m_radialOpen = false;
            updateCursorShape();
            update(radialMenuRect());
        }
        executeTabletAction(action, event->position());
        return true;
    };

    if (event->type() != QEvent::TabletRelease) {
        if (newlyPressedUpper && event->pressure() <= 0.01) {
            if (handleButtonTrigger(m_stylusUpperAction))
                return;
        }
        if (newlyPressedLower && event->pressure() <= 0.01) {
            if (handleButtonTrigger(m_stylusLowerAction))
                return;
        }
    }
    if (m_radialOpen && event->pressure() <= 0.01) {
        updateRadialHover(event->position());
        return;
    }

    m_holdEraseUpper = (m_stylusUpperAction == TabletAction::HoldEraser) && ((btns & upperBtn) != 0);
    m_holdEraseLower = (m_stylusLowerAction == TabletAction::HoldEraser) && ((btns & lowerBtn) != 0);
    const bool useEraser = eraserTip || m_holdEraseUpper || m_holdEraseLower;

    switch (event->type()) {
    case QEvent::TabletPress:
        if (event->pressure() <= 0.01) {
            if (btn == upperBtn && handleButtonTrigger(m_stylusUpperAction))
                return;
            if (btn == lowerBtn && handleButtonTrigger(m_stylusLowerAction))
                return;
        }
        m_tabletActive = true;
        beginInput(event->position(), event->pressure(), event->xTilt(), event->yTilt(),
                   true, useEraser, Qt::LeftButton, event->modifiers());
        break;
    case QEvent::TabletMove:
        moveInput(event->position(), event->pressure(), event->xTilt(), event->yTilt(),
                  true, useEraser, event->modifiers());
        break;
    case QEvent::TabletRelease:
        if (m_tabletActive) {
            moveInput(event->position(), event->pressure(), event->xTilt(), event->yTilt(),
                      true, useEraser, event->modifiers());
            if (m_mode == InteractionMode::Draw && m_hasActiveStroke && !m_activeStroke.points.isEmpty())
                m_activeStroke.points.back().pressure = std::clamp(event->pressure(), 0.0, 1.0);
            endInput(event->position(), event->modifiers());
        }
        m_tabletActive = false;
        break;
    default:
        break;
    }
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    event->accept();
    if (m_tabletActive || (m_palmRejectionEnabled && event->source() != Qt::MouseEventNotSynthesized))
        return;
    beginInput(event->position(), 0.5, 0.0, 0.0, false, false, event->button(), event->modifiers());
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    event->accept();
    if (m_mode == InteractionMode::None && !m_tabletActive) {
        if (m_radialOpen) {
            updateRadialHover(event->position());
            return;
        }
        const auto world = screenToWorld(event->position());
        const bool showPen = !m_spaceHeld
            && (m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint
                || m_tool == ToolType::Highlighter || m_tool == ToolType::Laser);
        updatePenCursor(world, showPen);
    }
    if (m_tabletActive || (m_palmRejectionEnabled && event->source() != Qt::MouseEventNotSynthesized))
        return;
    moveInput(event->position(), 0.5, 0.0, 0.0, false, false, event->modifiers());
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    event->accept();
    if (m_tabletActive || (m_palmRejectionEnabled && event->source() != Qt::MouseEventNotSynthesized))
        return;
    if ((m_tool == ToolType::Text || m_tool == ToolType::Lasso || m_tool == ToolType::Select)
        && event->button() == Qt::LeftButton) {
        const auto world = screenToWorld(event->position());
        if (std::any_of(m_doc.texts().cbegin(), m_doc.texts().cend(), [&](const TextItem& item) {
                return item.bounds.adjusted(-4, -4, 4, 4).contains(world);
            })) {
            m_mode = InteractionMode::None;
            beginTextEditAt(event->position(), world);
            return;
        }
    }
    if (m_tool == ToolType::Polyline && m_polylineActive && event->button() == Qt::LeftButton) {
        const auto world = screenToWorld(event->position());
        if (m_polylineVertices.size() >= 3
            && pointDistance(world, m_polylineVertices.front()) * m_doc.viewport().zoom <= 14.0) {
            finishActivePolyline(true);
        } else {
            if (pointDistance(world, m_polylineVertices.back()) * m_doc.viewport().zoom > 2.0)
                beginInput(event->position(), 0.5, 0.0, 0.0, false, false, Qt::LeftButton,
                           event->modifiers());
            finishActivePolyline();
        }
    }
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    event->accept();
    if (m_tabletActive || (m_palmRejectionEnabled && event->source() != Qt::MouseEventNotSynthesized))
        return;
    if (m_mode == InteractionMode::Pan
        && (event->buttons() & (Qt::LeftButton | Qt::MiddleButton | Qt::RightButton))) {
        return;
    }
    if (m_mode == InteractionMode::Draw && event->button() != Qt::LeftButton)
        return;
    const auto delta = event->position() - m_panStartScreen;
    const bool showMenu = m_mode == InteractionMode::Pan && m_rightClickOnSelection
        && event->button() == Qt::RightButton && std::hypot(delta.x(), delta.y()) < 4.0;
    m_rightClickOnSelection = false;
    if (showMenu) {
        m_mode = InteractionMode::None;
        updateCursorShape();
        emit selectionMenuRequested(event->globalPosition().toPoint());
        return;
    }
    endInput(event->position(), event->modifiers());
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    event->accept();
    if (m_zoomAnimation->state() == QAbstractAnimation::Running) {
        m_zoomAnimation->stop();
        invalidateStaticCache();
    }
    if (m_mode != InteractionMode::None)
        return;
    const auto mods = event->modifiers();
    if (mods.testFlag(Qt::ControlModifier)) {
        const double delta = event->pixelDelta().isNull()
            ? event->angleDelta().y()
            : event->pixelDelta().y() * 2.0;
        zoomByFactor(std::exp(delta * 0.0015), event->position());
        return;
    }
    clearPenCursor();
    QPointF scrollDelta;
    if (!event->pixelDelta().isNull()) {
        scrollDelta = QPointF(event->pixelDelta());
    } else {
        const QPoint angle = event->angleDelta();
        if (mods.testFlag(Qt::ShiftModifier) && angle.x() == 0)
            scrollDelta = QPointF(angle.y() * 0.65, 0.0);
        else
            scrollDelta = QPointF(angle.x() * 0.65, angle.y() * 0.65);
    }
    if (scrollDelta.isNull())
        return;
    Viewport& vp = m_doc.viewport();
    vp.x += scrollDelta.x();
    vp.y += scrollDelta.y();
    clampFiniteViewport();
    invalidateStaticCache();
    saveAutosave();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_0 && event->modifiers().testFlag(Qt::ControlModifier)) {
        resetViewport();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_P && !event->isAutoRepeat() && event->modifiers() == Qt::NoModifier) {
        if (m_radialOpen) {
            m_radialOpen = false;
            updateCursorShape();
            update(radialMenuRect());
            event->accept();
            return;
        }
        const qint64 now = nowMs();
        const qint64 dt = now - m_lastPenTriggerMs;
        if (m_radialMenuEnabled && m_mode == InteractionMode::None
            && m_lastPenTriggerMs > 0 && dt >= 70 && dt <= 420) {
            m_lastPenTriggerMs = 0;
            const Viewport& vp = m_doc.viewport();
            QPointF c = m_showPenCursor
                ? (m_hoverWorldPos * vp.zoom + QPointF(vp.x, vp.y))
                : QPointF(mapFromGlobal(QCursor::pos()));
            if (!rect().contains(c.toPoint()))
                c = QPointF(width() / 2.0, height() / 2.0);
            openRadialMenu(c);
        } else {
            m_lastPenTriggerMs = now;
            setTool(m_lastWriteTool);
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space) {
        clearPenCursor();
        m_spaceHeld = true;
        updateCursorShape();
        event->accept();
        return;
    }
    if (m_polylineActive && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        finishActivePolyline();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        cancelInteraction();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        cutSelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        pasteFromClipboard();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_D && event->modifiers().testFlag(Qt::ControlModifier)) {
        duplicateSelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Undo)) {
        undo();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        event->accept();
        return;
    }
    if (pageCount() > 1 && !hasLassoSelection() && !hasSelectedShape()
        && !hasSelectedText() && !hasSelectedImage()) {
        if (event->key() == Qt::Key_Left) {
            prevPage();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            nextPage();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        updateCursorShape();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CanvasWidget::focusOutEvent(QFocusEvent* event) {
    m_spaceHeld = false;
    if (m_radialOpen) {
        m_radialOpen = false;
        update(radialMenuRect());
    }
    updateCursorShape();
    QWidget::focusOutEvent(event);
}

void CanvasWidget::switchTabletTool(ToolType tool) {
    if (m_tool == tool)
        return;
    setTool(tool);
    QString label = QStringLiteral("Pluma fuente");
    switch (tool) {
    case ToolType::Eraser:
        label = QStringLiteral("Borrador");
        break;
    case ToolType::Highlighter:
        label = QStringLiteral("Resaltador");
        break;
    case ToolType::Ballpoint:
        label = QStringLiteral("Bolígrafo gel");
        break;
    case ToolType::Lasso:
        label = QStringLiteral("Lazo");
        break;
    case ToolType::Select:
        label = QStringLiteral("Seleccionar");
        break;
    case ToolType::Shape:
        label = QStringLiteral("Figuras");
        break;
    case ToolType::Polyline:
        label = QStringLiteral("Trazado");
        break;
    case ToolType::Hand:
        label = QStringLiteral("Mano");
        break;
    case ToolType::Laser:
        label = QStringLiteral("Puntero láser");
        break;
    default:
        break;
    }
    emit toastRequested(label);
}

void CanvasWidget::executeTabletAction(TabletAction action, const QPointF& screenPos) {
    switch (action) {
    case TabletAction::None:
    case TabletAction::HoldEraser:
        return;
    case TabletAction::Undo:
        undo();
        return;
    case TabletAction::Redo:
        redo();
        return;
    case TabletAction::Pen:
        switchTabletTool(ToolType::Pen);
        return;
    case TabletAction::Highlighter:
        switchTabletTool(ToolType::Highlighter);
        return;
    case TabletAction::Eraser:
        switchTabletTool(ToolType::Eraser);
        return;
    case TabletAction::LastWriteTool:
        switchTabletTool(m_lastWriteTool);
        return;
    case TabletAction::Lasso:
        switchTabletTool(ToolType::Lasso);
        return;
    case TabletAction::Select:
        switchTabletTool(ToolType::Select);
        return;
    case TabletAction::Shapes:
        switchTabletTool(ToolType::Shape);
        return;
    case TabletAction::Polyline:
        switchTabletTool(ToolType::Polyline);
        return;
    case TabletAction::Hand:
        switchTabletTool(ToolType::Hand);
        return;
    case TabletAction::Laser:
        switchTabletTool(ToolType::Laser);
        return;
    case TabletAction::NextPage:
        nextPage();
        return;
    case TabletAction::PrevPage:
        prevPage();
        return;
    case TabletAction::RadialMenu:
        if (m_radialOpen) {
            m_radialOpen = false;
            updateCursorShape();
            update(radialMenuRect());
        } else if (m_radialMenuEnabled) {
            openRadialMenu(screenPos);
        }
        return;
    case TabletAction::RightClickMenu:
        if (hasSelection())
            emit selectionMenuRequested(mapToGlobal(screenPos.toPoint()));
        else
            emit tabletActionTriggered(TabletAction::RightClickMenu, screenPos);
        return;
    case TabletAction::ToggleCleanUi:
        emit tabletActionTriggered(TabletAction::ToggleCleanUi, screenPos);
        return;
    }
}

void CanvasWidget::leaveEvent(QEvent* event) {
    clearPenCursor();
    clearEraserCursor();
    QWidget::leaveEvent(event);
}

void CanvasWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() && (event->mimeData()->hasImage() || event->mimeData()->hasUrls()))
        event->acceptProposedAction();
}

void CanvasWidget::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (!mime)
        return;
    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            const QString local = url.toLocalFile();
            if (local.isEmpty())
                continue;
            if (local.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
                importPdfFile(local);
            else
                importImageFile(local);
        }
        event->acceptProposedAction();
    } else if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            QGuiApplication::clipboard()->setImage(img);
            pasteFromClipboard();
            event->acceptProposedAction();
        }
    }
}

void CanvasWidget::updateCursorShape() {
    if (m_radialOpen) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (m_mode == InteractionMode::Pan) {
        setCursor(Qt::ClosedHandCursor);
    } else if (m_spaceHeld || m_tool == ToolType::Hand) {
        setCursor(Qt::OpenHandCursor);
    } else if (m_tool == ToolType::Eraser || m_mode == InteractionMode::Erase
               || m_tool == ToolType::Pen || m_tool == ToolType::Ballpoint
               || m_tool == ToolType::Highlighter || m_tool == ToolType::Laser) {
        setCursor(Qt::BlankCursor);
    } else if (m_tool == ToolType::Select) {
        setCursor(Qt::ArrowCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

} // namespace finily
