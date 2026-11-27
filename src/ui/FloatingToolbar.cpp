#include "ui/FloatingToolbar.h"

#include "core/Utils.h"
#include "ui/CanvasWidget.h"
#include "ui/FloatingToolbarInternal.h"
#include "ui/IconLibrary.h"
#include "ui/ToolbarIcons.h"
#include "ui/popovers/FilePopover.h"
#include "ui/popovers/PaperPopover.h"
#include "ui/popovers/ShapesPopover.h"
#include "ui/popovers/StickersPopover.h"
#include "ui/popovers/StrokePopover.h"
#include "ui/popovers/ThumbnailPanel.h"

#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEasingCurve>
#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace finily {
namespace {

QString rgba(const QColor& color, int alpha = -1) {
    const int a = alpha < 0 ? color.alpha() : alpha;
    return QString("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(a);
}

QColor adaptInkToTheme(const QColor& color, bool dark) {
    static const QVector<QPair<QString, QString>> pairs = {
        {"#f8fafc", "#0f172a"}, {"#fde047", "#ca8a04"}, {"#38bdf8", "#0284c7"}, {"#f87171", "#dc2626"},
        {"#4ade80", "#16a34a"}, {"#c084fc", "#9333ea"}, {"#fb923c", "#ea580c"},
    };
    const QString name = color.name();
    for (const auto& pair : pairs) {
        if (dark && name == pair.second) {
            return QColor(pair.first);
        }
        if (!dark && name == pair.first) {
            return QColor(pair.second);
        }
    }
    return color;
}

} // namespace

FloatingToolbar::FloatingToolbar(CanvasWidget* canvas, QWidget* parent)
    : QWidget(parent)
    , m_canvas(canvas) {
    setObjectName(QStringLiteral("finilyOverlay"));
    // Sin WA_TransparentForMouseEvents para no bloquear clics sobre controles hijos;
    // la transparencia al lienzo se logra con setMask() en updateOverlayMask().
    setFocusPolicy(Qt::NoFocus);

    initPresets();
    loadPresets();
    m_palette = getPaletteForTheme(true);
    loadDockPosition();
    {
        const auto settings = appSettings();
        const int dir = settings->value(QStringLiteral("ui/pageNavDirection"), 0).toInt();
        m_pageNavDir = (dir == 1) ? PageNavDirection::Vertical : PageNavDirection::Horizontal;
    }

    if (m_canvas) {
        m_canvas->installEventFilter(this);
        connect(m_canvas, &CanvasWidget::stateChanged, this, &FloatingToolbar::refreshFromCanvas);
        connect(m_canvas, &CanvasWidget::viewportChanged, this, [this](double) { updateZoomLabel(); });
        connect(m_canvas, &CanvasWidget::toastRequested, this, &FloatingToolbar::showToast);
        connect(m_canvas, &CanvasWidget::selectionMenuRequested, this, &FloatingToolbar::showSelectionMenu);
        connect(m_canvas, &CanvasWidget::edgeMarkMenuRequested, this, &FloatingToolbar::showEdgeMarkPill);
    }

    buildUtilityPill();
    buildTabBarPill();
    buildTopRight();
    buildDock();
    buildZoomPill();
    buildPagePill();
    buildRestoreButton();
    buildContextPill();
    buildEdgeMarkPill();

    // Popovers modulares (hijos directos del overlay, nunca Qt::Popup).
    m_strokePopover = new StrokePopover(m_canvas, this);
    m_strokePopover->setPalette(m_palette);
    connect(m_strokePopover, &StrokePopover::colorPicked, this, &FloatingToolbar::setColor);
    connect(m_strokePopover, &StrokePopover::favoritesMenuRequested, this,
            [this](QWidget* anchor) { showFavoritesMenu(anchor); });
    connect(m_strokePopover, &StrokePopover::layoutAdjusted, this, [this] {
        positionOpenPopover();
        updateOverlayMask();
    });
    connect(m_strokePopover, &StrokePopover::refreshToolbarRequested, this,
            &FloatingToolbar::refreshFromCanvas);
    connect(m_strokePopover, &StrokePopover::closeRequested, this, &FloatingToolbar::closePopovers);

    m_paperPopover = new PaperPopover(m_canvas, this);
    m_paperPopover->setToolbarState(m_dockPos, m_pageNavDir);
    connect(m_paperPopover, &PaperPopover::dockPositionRequested, this, &FloatingToolbar::setDockPosition);
    connect(m_paperPopover, &PaperPopover::pageNavDirectionRequested, this,
            &FloatingToolbar::setPageNavDirection);
    connect(m_paperPopover, &PaperPopover::closeRequested, this, &FloatingToolbar::closePopovers);
    connect(m_paperPopover, &PaperPopover::refreshToolbarRequested, this,
            &FloatingToolbar::refreshFromCanvas);

    m_shapesPopover = new ShapesPopover(m_canvas, this);
    connect(m_shapesPopover, &ShapesPopover::layoutAdjusted, this, [this] {
        positionOpenPopover();
        updateOverlayMask();
    });
    connect(m_shapesPopover, &ShapesPopover::refreshToolbarRequested, this,
            &FloatingToolbar::refreshFromCanvas);
    connect(m_shapesPopover, &ShapesPopover::closeRequested, this, &FloatingToolbar::closePopovers);

    m_stickersPopover = new StickersPopover(m_canvas, this);
    connect(m_stickersPopover, &StickersPopover::closeRequested, this, &FloatingToolbar::closePopovers);
    connect(m_stickersPopover, &StickersPopover::refreshToolbarRequested, this,
            &FloatingToolbar::refreshFromCanvas);
    connect(m_stickersPopover, &StickersPopover::toastRequested, this, &FloatingToolbar::showToast);

    m_filePopover = new FilePopover(m_canvas, this);
    connect(m_filePopover, &FilePopover::newNotebookRequested, this, &FloatingToolbar::newDocumentAction);
    connect(m_filePopover, &FilePopover::newTabRequested, this, [this] {
        closePopovers();
        emit newTabRequested();
    });
    connect(m_filePopover, &FilePopover::openNotebookRequested, this, &FloatingToolbar::openNotebookDialog);
    connect(m_filePopover, &FilePopover::saveNotebookRequested, this, &FloatingToolbar::saveNotebookAction);
    connect(m_filePopover, &FilePopover::saveNotebookAsRequested, this,
            &FloatingToolbar::saveNotebookAsDialog);
    connect(m_filePopover, &FilePopover::importPdfRequested, this, &FloatingToolbar::importPdfDialog);
    connect(m_filePopover, &FilePopover::exportPdfRequested, this, &FloatingToolbar::exportPdfDialog);
    connect(m_filePopover, &FilePopover::configureAiApiRequested, this, [this] {
        closePopovers();
        emit configureAiApiRequested();
    });
    connect(m_filePopover, &FilePopover::checkForUpdatesRequested, this, [this] {
        closePopovers();
        emit checkForUpdatesRequested();
    });
    connect(m_filePopover, &FilePopover::layoutAdjusted, this, [this] {
        positionOpenPopover();
        updateOverlayMask();
    });
    connect(m_filePopover, &FilePopover::closeRequested, this, &FloatingToolbar::closePopovers);
    connect(m_filePopover, &FilePopover::refreshToolbarRequested, this, &FloatingToolbar::refreshFromCanvas);

    m_thumbPanel = new ThumbnailPanel(m_canvas, this);
    connect(m_thumbPanel, &ThumbnailPanel::closePopoversRequested, this, &FloatingToolbar::closePopovers);
    connect(m_thumbPanel, &ThumbnailPanel::refreshToolbarRequested, this,
            &FloatingToolbar::refreshFromCanvas);
    connect(m_thumbPanel, &ThumbnailPanel::importPdfRequested, this, &FloatingToolbar::importPdfDialog);
    connect(m_thumbPanel, &ThumbnailPanel::exportPdfRequested, this, &FloatingToolbar::exportPdfDialog);

    // Toast flotante: usa QGraphicsOpacityEffect únicamente (sin shadow effect encima).
    m_toastPill = new GlassCapsule(this);
    m_toastPill->setCapsuleRadius(16);
    auto* toastLayout = new QVBoxLayout(m_toastPill);
    toastLayout->setContentsMargins(16, 9, 16, 9);
    toastLayout->setSpacing(0);
    m_toastLabel = new QLabel(m_toastPill);
    m_toastLabel->setObjectName(QStringLiteral("finilyToast"));
    m_toastLabel->setAlignment(Qt::AlignCenter);
    toastLayout->addWidget(m_toastLabel);
    m_toastPill->hide();
    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, this, [this] {
        if (!m_toastPill || !m_toastPill->isVisible()) {
            return;
        }
        auto* opacity = qobject_cast<QGraphicsOpacityEffect*>(m_toastPill->graphicsEffect());
        if (!opacity) {
            return;
        }
        if (m_toastAnim) {
            m_toastAnim->stop();
            m_toastAnim->deleteLater();
        }
        m_toastAnim = new QPropertyAnimation(opacity, "opacity", this);
        m_toastAnim->setDuration(220);
        m_toastAnim->setStartValue(opacity->opacity());
        m_toastAnim->setEndValue(0.0);
        m_toastAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_toastAnim, &QPropertyAnimation::finished, this, [this] {
            if (m_toastPill) {
                m_toastPill->hide();
            }
            auto* effect = qobject_cast<QGraphicsOpacityEffect*>(
                m_toastPill ? m_toastPill->graphicsEffect() : nullptr);
            if (effect) {
                effect->setOpacity(1.0);
            }
            updateOverlayMask();
        });
        m_toastAnim->start();
    });

    const ThemeConfig initialTheme = getThemeConfig(m_theme);
    const QVector<QPair<GlassCapsule*, int>> capsuleSpecs = {
        {m_pill, 22},          {m_tabBarPill, 18},   {m_contextPill, 16}, {m_edgeMarkPill, 16},
        {m_zoomPill, 18},      {m_pagePill, 18},     {m_toastPill, 16},   {m_strokePopover, 16},
        {m_paperPopover, 16},  {m_shapesPopover, 16}, {m_stickersPopover, 16}, {m_filePopover, 16},
        {m_dock, 20},          {m_topRight, 22},     {m_thumbPanel, 16},
    };
    for (const auto& capsule : capsuleSpecs) {
        if (!capsule.first) {
            continue;
        }
        capsule.first->setCapsuleRadius(capsule.second);
        capsule.first->setCapsuleTheme(initialTheme);
        m_capsules.append(capsule.first);
    }

    applyThemeStylesheet();

    if (m_canvas) {
        setGeometry(m_canvas->rect());
    }
    positionChildren();
    refreshFromCanvas();
}

void FloatingToolbar::initPresets() {
    m_presets.clear();
    m_presets.append(PenPreset{0, QStringLiteral("Tiza principal"), ToolType::Pen, QColor("#f8fafc"), 2.6,
                               LinePattern::Solid, 1, 1.0, QStringLiteral("1")});
    m_presets.append(PenPreset{1, QStringLiteral("Amarillo"), ToolType::Pen, QColor("#fde047"), 2.6,
                               LinePattern::Solid, 1, 1.0, QStringLiteral("2")});
    m_presets.append(PenPreset{2, QStringLiteral("Celeste"), ToolType::Pen, QColor("#38bdf8"), 2.6,
                               LinePattern::Solid, 1, 1.0, QStringLiteral("3")});
    m_presets.append(PenPreset{3, QStringLiteral("Rojo"), ToolType::Pen, QColor("#f87171"), 2.6,
                               LinePattern::Solid, 1, 1.0, QStringLiteral("4")});
    m_presets.append(PenPreset{4, QStringLiteral("Resaltador"), ToolType::Highlighter, QColor("#fde047"),
                               12.0, LinePattern::Solid, 0, 0.25, QStringLiteral("5")});
    m_activePreset = 0;
    m_lastPresetsSig.clear();
}

void FloatingToolbar::loadPresets() {
    const auto settings = appSettings();
    const QString firstKey = QString("presets/%1/tool").arg(m_presets.first().index);
    if (!settings->contains(firstKey)) {
        return;
    }
    for (int i = 0; i < m_presets.size(); ++i) {
        const QString prefix = QString("presets/%1/").arg(i);
        PenPreset& preset = m_presets[i];
        const QString lbl = settings->value(prefix + "label", preset.label).toString().trimmed();
        if (!lbl.isEmpty()) {
            preset.label = lbl;
        }
        const int tool = settings->value(prefix + "tool", int(preset.tool)).toInt();
        if (tool >= int(ToolType::Pen) && tool <= int(ToolType::Select)) {
            preset.tool = static_cast<ToolType>(tool);
        }
        const QString colorName =
            settings->value(prefix + "color", preset.color.name(QColor::HexArgb)).toString();
        const QColor color(colorName);
        if (color.isValid()) {
            preset.color = color;
        }
        const double size = settings->value(prefix + "size", preset.size).toDouble();
        if (std::isfinite(size) && size > 0.0 && size <= 60.0) {
            preset.size = size;
        }
        const int pattern = settings->value(prefix + "linePattern", int(preset.linePattern)).toInt();
        if (pattern >= int(LinePattern::Solid) && pattern <= int(LinePattern::Dimension)) {
            preset.linePattern = static_cast<LinePattern>(pattern);
        }
        preset.tipStyle = std::clamp(settings->value(prefix + "tipStyle", preset.tipStyle).toInt(), 0, 3);
        preset.opacity =
            std::clamp(settings->value(prefix + "opacity", preset.opacity).toDouble(), 0.10, 1.0);
        preset.shortcut = settings->value(prefix + "shortcut", preset.shortcut).toString().trimmed();
    }
    m_presetsRestored = true;
}

void FloatingToolbar::savePresets() {
    auto settings = appSettings();
    for (int i = 0; i < m_presets.size(); ++i) {
        const QString prefix = QString("presets/%1/").arg(i);
        const PenPreset& preset = m_presets.at(i);
        settings->setValue(prefix + "label", preset.label);
        settings->setValue(prefix + "tool", int(preset.tool));
        settings->setValue(prefix + "color", preset.color.name(QColor::HexArgb));
        settings->setValue(prefix + "size", preset.size);
        settings->setValue(prefix + "linePattern", int(preset.linePattern));
        settings->setValue(prefix + "tipStyle", preset.tipStyle);
        settings->setValue(prefix + "opacity", preset.opacity);
        settings->setValue(prefix + "shortcut", preset.shortcut);
    }
    settings->sync();
}

void FloatingToolbar::buildUtilityPill() {
    m_pill = new GlassCapsule(this);
    m_pill->setObjectName(QStringLiteral("finilyPill"));
    auto* row = new QHBoxLayout(m_pill);
    row->setContentsMargins(10, 7, 10, 7);
    row->setSpacing(3);

    m_homeBtn = new QToolButton(m_pill);
    m_homeBtn->setFixedSize(36, 36);
    m_homeBtn->setIconSize(QSize(22, 22));
    m_homeBtn->setToolTip(QStringLiteral("Inicio"));
    m_homeBtn->setStatusTip(m_homeBtn->toolTip());
    polishButton(m_homeBtn);
    connect(m_homeBtn, &QToolButton::clicked, this, &FloatingToolbar::homeRequested);
    m_studioUtilSpecs.append(qMakePair(m_homeBtn, QStringLiteral("home")));
    row->addWidget(m_homeBtn);

    m_titleDot = new QLabel(m_pill);
    m_titleDot->setFixedSize(6, 6);
    row->addSpacing(4);
    row->addWidget(m_titleDot, 0, Qt::AlignVCenter);
    m_titleLabel = new QLabel(QStringLiteral("Sin título"), m_pill);
    m_titleLabel->setMaximumWidth(200);
    m_titleLabel->setCursor(Qt::PointingHandCursor);
    m_titleLabel->setToolTip(QStringLiteral("Haz clic para renombrar el cuaderno"));
    m_titleLabel->installEventFilter(this);
    row->addWidget(m_titleLabel);
    m_titleEdit = new QLineEdit(m_pill);
    m_titleEdit->setMaximumWidth(180);
    m_titleEdit->setFixedHeight(26);
    m_titleEdit->hide();
    connect(m_titleEdit, &QLineEdit::editingFinished, this, [this] {
        if (!m_titleEdit || !m_titleEdit->isVisible()) {
            return;
        }
        const QString next = m_titleEdit->text().trimmed();
        m_titleEdit->hide();
        if (m_titleLabel) {
            m_titleLabel->show();
        }
        positionChildren();
        if (!next.isEmpty() && (!m_titleLabel || next != m_titleLabel->text())) {
            emit renameRequested(next);
        }
    });
    row->addWidget(m_titleEdit);
    row->addSpacing(4);

    row->addWidget(makeSeparator(m_pill));

    m_undoBtn = new QToolButton(m_pill);
    m_undoBtn->setFixedSize(36, 36);
    m_undoBtn->setIconSize(QSize(22, 22));
    m_undoBtn->setToolTip(QStringLiteral("Deshacer"));
    polishButton(m_undoBtn);
    connect(m_undoBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->undo();
        }
        refreshFromCanvas();
    });
    m_studioUtilSpecs.append(qMakePair(m_undoBtn, QStringLiteral("undo")));
    row->addWidget(m_undoBtn);

    m_redoBtn = new QToolButton(m_pill);
    m_redoBtn->setFixedSize(36, 36);
    m_redoBtn->setIconSize(QSize(22, 22));
    m_redoBtn->setToolTip(QStringLiteral("Rehacer"));
    polishButton(m_redoBtn);
    connect(m_redoBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->redo();
        }
        refreshFromCanvas();
    });
    m_studioUtilSpecs.append(qMakePair(m_redoBtn, QStringLiteral("redo")));
    row->addWidget(m_redoBtn);
}

void FloatingToolbar::buildTopRight() {
    m_topRight = new GlassCapsule(this);
    m_topRight->setObjectName(QStringLiteral("finilyPill"));
    auto* row = new QHBoxLayout(m_topRight);
    row->setContentsMargins(8, 7, 8, 7);
    row->setSpacing(3);
    auto mk = [&](const QString& tip, const QString& glyph) {
        auto* b = new QToolButton(m_topRight);
        b->setFixedSize(36, 36);
        b->setIconSize(QSize(22, 22));
        b->setToolTip(tip);
        polishButton(b);
        m_iconSpecs.append(qMakePair(b, glyph));
        row->addWidget(b);
        return b;
    };
    m_cleanBtn = mk(QStringLiteral("Presentar · oculta la interfaz (Tab)"), QStringLiteral("present"));
    connect(m_cleanBtn, &QToolButton::clicked, this, &FloatingToolbar::toggleCleanUi);
    m_paperBtn = mk(QStringLiteral("Papel y plantilla"), QStringLiteral("paper"));
    connect(m_paperBtn, &QToolButton::clicked, this, [this] { openPopover(m_paperPopover, m_paperBtn); });
    m_pagesBtn = mk(QStringLiteral("Páginas"), QStringLiteral("pages"));
    m_pagesBtn->setCheckable(true);
    connect(m_pagesBtn, &QToolButton::clicked, this, [this] {
        closePopovers();
        if (m_thumbPanel) {
            m_thumbPanel->setVisible(m_pagesBtn->isChecked());
            if (m_pagesBtn->isChecked()) {
                m_thumbPanel->rebuild();
            }
        }
        positionChildren();
    });
    m_fileBtn = mk(QStringLiteral("Menú"), QStringLiteral("more"));
    connect(m_fileBtn, &QToolButton::clicked, this, [this] { openPopover(m_filePopover, m_fileBtn); });
}

void FloatingToolbar::updateTitleDot() {
    if (!m_titleDot) {
        return;
    }
    const ThemeConfig theme = getThemeConfig(m_theme);
    m_titleDot->setStyleSheet(QString("background:%1;border-radius:3px;")
                                  .arg(m_titleUnsaved ? QStringLiteral("#f5b84b") : theme.accent.name()));
    m_titleDot->setToolTip(m_titleUnsaved ? QStringLiteral("Cambios sin guardar")
                                          : QStringLiteral("Guardado"));
}

void FloatingToolbar::setDocumentTitle(const QString& title, bool unsaved) {
    if (!m_titleLabel) {
        return;
    }
    m_titleUnsaved = unsaved;
    m_titleLabel->setText(title);
    m_titleLabel->setToolTip(title);
    updateTitleDot();
    positionChildren();
}

void FloatingToolbar::buildTabBarPill() {
    m_tabBarPill = new GlassCapsule(this);
    m_tabBarPill->setObjectName(QStringLiteral("finilyTabPill"));
    m_tabBarLayout = new QHBoxLayout(m_tabBarPill);
    m_tabBarLayout->setContentsMargins(6, 4, 6, 4);
    m_tabBarLayout->setSpacing(3);
    m_tabBarPill->hide();
}

void FloatingToolbar::setNotebookTabs(const QVector<TabEntry>& tabs) {
    if (!m_tabBarPill || !m_tabBarLayout) {
        return;
    }
    m_tabCount = int(tabs.size());
    while (QLayoutItem* item = m_tabBarLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    if (tabs.size() <= 1) {
        m_tabBarPill->hide();
        positionChildren();
        return;
    }

    for (int i = 0; i < tabs.size(); ++i) {
        const TabEntry& t = tabs.at(i);
        auto* btn = new QToolButton(m_tabBarPill);
        btn->setObjectName(QStringLiteral("finilyShapeBtn"));
        btn->setCheckable(true);
        btn->setChecked(t.active);
        btn->setFixedHeight(28);
        btn->setMaximumWidth(148);
        const QString baseTitle = t.title.trimmed().isEmpty() ? tr("Sin título") : t.title.trimmed();
        const QString elided = QFontMetrics(btn->font()).elidedText(baseTitle, Qt::ElideRight, 118);
        btn->setText((t.dirty ? QStringLiteral("• ") : QString()) + elided);
        btn->setToolTip(baseTitle);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, i] { emit switchTabRequested(i); });
        m_tabBarLayout->addWidget(btn);

        auto* closeBtn = new QToolButton(m_tabBarPill);
        closeBtn->setText(QStringLiteral("×"));
        closeBtn->setFixedSize(20, 22);
        closeBtn->setToolTip(tr("Cerrar pestaña (Ctrl+W)"));
        polishButton(closeBtn);
        connect(closeBtn, &QToolButton::clicked, this, [this, i] { emit closeTabRequested(i); });
        m_tabBarLayout->addWidget(closeBtn);
    }

    auto* addBtn = new QToolButton(m_tabBarPill);
    addBtn->setText(QStringLiteral("+"));
    addBtn->setFixedSize(26, 26);
    addBtn->setToolTip(tr("Nueva pestaña (Ctrl+T)"));
    polishButton(addBtn);
    connect(addBtn, &QToolButton::clicked, this, &FloatingToolbar::newTabRequested);
    m_tabBarLayout->addWidget(addBtn);

    m_tabBarPill->setVisible(!m_cleanUi);
    if (!m_cleanUi) {
        m_tabBarPill->raise();
    }
    positionChildren();
}

void FloatingToolbar::buildDock() {
    m_dock = new GlassCapsule(this);
    m_dock->setObjectName(QStringLiteral("finilyDock"));
    m_dock->setCapsuleRadius(20);

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    const QVector<QPair<ToolType, QPair<QString, QString>>> writingTools = {
        {ToolType::Pen, qMakePair(QStringLiteral("Pluma (P)"), QStringLiteral("pen"))},
        {ToolType::Highlighter, qMakePair(QStringLiteral("Resaltador (H)"), QStringLiteral("highlighter"))},
        {ToolType::Eraser, qMakePair(QStringLiteral("Borrador (E)"), QStringLiteral("eraser"))},
        {ToolType::Fill,
         qMakePair(tr("Relleno (F). Toca dentro de una figura cerrada. Segundo clic para elegir el estilo."),
                   QStringLiteral("paint-bucket"))},
    };
    for (const auto& entry : writingTools) {
        auto* button = new QToolButton(m_dock);
        button->setObjectName(QStringLiteral("finilyTool"));
        button->setFixedSize(42, 40);
        button->setIconSize(QSize(24, 24));
        button->setCheckable(true);
        polishButton(button);
        m_iconSpecs.append(qMakePair(button, entry.second.second));
        button->setToolTip(entry.second.first);
        m_toolGroup->addButton(button);
        const ToolType tool = entry.first;
        connect(button, &QToolButton::clicked, this, [this, tool, button] {
            if (!m_canvas) {
                return;
            }
            const ToolType active = m_canvas->tool();
            const bool sameTool =
                (active == tool) || (tool == ToolType::Pen && active == ToolType::Ballpoint);
            if (sameTool) {
                if (m_openPopover == m_strokePopover) {
                    closePopovers();
                } else {
                    openPopover(m_strokePopover, button);
                }
            } else {
                closePopovers();
                selectTool(tool == ToolType::Pen && m_canvas->penTip() == 0 ? ToolType::Ballpoint : tool);
            }
        });
        m_writingKinds.append(tool);
        m_writingBtns.append(button);
    }

    m_dockDivider = new QWidget(m_dock);
    m_dockDivider->setObjectName(QStringLiteral("finilySep"));

    const QVector<QPair<ToolType, QPair<QString, QString>>> secondary = {
        {ToolType::Select,
         qMakePair(tr("Seleccionar (U). Toca para seleccionar, arrastra para mover. Círculos: vértices. "
                      "Cuadrados: tamaño."),
                   QStringLiteral("select-pointer"))},
        {ToolType::Shape,
         qMakePair(tr("Figuras (S). Arrastra para dibujar. Mayús + arrastrar para editar."),
                   QStringLiteral("shapes"))},
        {ToolType::Lasso, qMakePair(QStringLiteral("Lazo (A)"), QStringLiteral("lasso"))},
        {ToolType::Text, qMakePair(QStringLiteral("Texto (T)"), QStringLiteral("text"))},
        {ToolType::Laser, qMakePair(QStringLiteral("Láser (K)"), QStringLiteral("laser"))},
    };
    for (const auto& entry : secondary) {
        auto* button = new QToolButton(m_dock);
        button->setObjectName(QStringLiteral("finilyTool"));
        button->setFixedSize(42, 40);
        button->setIconSize(QSize(24, 24));
        button->setCheckable(true);
        button->setToolTip(entry.second.first);
        polishButton(button);
        m_toolGroup->addButton(button);
        const ToolType tool = entry.first;
        if (tool == ToolType::Shape) {
            m_shapeBtn = button;
            connect(button, &QToolButton::clicked, this, [this] {
                if (!m_canvas) {
                    return;
                }
                if (m_canvas->tool() == ToolType::Shape) {
                    if (m_openPopover == m_shapesPopover) {
                        closePopovers();
                    } else {
                        openPopover(m_shapesPopover, m_shapeBtn);
                    }
                } else {
                    closePopovers();
                    selectTool(ToolType::Shape);
                    openPopover(m_shapesPopover, m_shapeBtn);
                }
            });
        } else if (tool == ToolType::Lasso) {
            connect(button, &QToolButton::clicked, this, [this, button] {
                if (m_canvas && m_canvas->tool() == ToolType::Lasso) {
                    if (m_openPopover == m_strokePopover) {
                        closePopovers();
                    } else {
                        openPopover(m_strokePopover, button);
                    }
                } else {
                    closePopovers();
                    selectTool(ToolType::Lasso);
                }
            });
        } else {
            connect(button, &QToolButton::clicked, this, [this, tool] {
                closePopovers();
                selectTool(tool);
            });
        }
        m_secondaryKinds.append(tool);
        m_secondaryBtns.append(button);
        m_secondaryIcons.append(entry.second.second);
        if (tool == ToolType::Shape) {
            m_stickersBtn = new QToolButton(m_dock);
            m_stickersBtn->setObjectName(QStringLiteral("finilyTool"));
            m_stickersBtn->setFixedSize(42, 40);
            m_stickersBtn->setIconSize(QSize(24, 24));
            m_stickersBtn->setToolTip(QStringLiteral("Fórmulas y Stickers de Geometría (Ctrl+K)"));
            polishButton(m_stickersBtn);
            connect(m_stickersBtn, &QToolButton::clicked, this, &FloatingToolbar::openStickersPopover);
            m_secondaryKinds.append(ToolType::Hand);
            m_secondaryBtns.append(m_stickersBtn);
            m_secondaryIcons.append(QStringLiteral("stickers"));
        }
    }

    m_trayDivider = new QWidget(m_dock);
    m_trayDivider->setObjectName(QStringLiteral("finilySep"));
    for (int i = 0; i < 4; ++i) {
        auto* dot = new ColorDotButton(m_dock);
        dot->setFixedSize(26, 26);
        dot->setToolTip(QStringLiteral("Cambiar color rápido"));
        polishButton(dot);
        connect(dot, &QToolButton::clicked, this, [this, i] {
            if (i < m_palette.size()) {
                setColor(m_palette.at(i));
            }
            if (m_canvas
                && (m_canvas->tool() == ToolType::Eraser || m_canvas->tool() == ToolType::Hand
                    || m_canvas->tool() == ToolType::Lasso || m_canvas->tool() == ToolType::Laser)) {
                selectTool(ToolType::Pen);
            }
        });
        m_trayDots.append(dot);
    }
}

void FloatingToolbar::layoutDock() {
    if (!m_dock) {
        return;
    }
    const bool vertical = m_dockPos == DockPos::Izquierda;
    const int pad = 10;
    const int toolSize = 40;
    const int dotSize = 26;
    const int crossLen = 56;
    const int along = pad + m_writingBtns.size() * (toolSize + 2) + 12
                      + m_secondaryBtns.size() * (toolSize + 2) + 12
                      + m_trayDots.size() * (dotSize + 4) + pad;
    if (vertical) {
        m_dock->setFixedSize(crossLen, along);
    } else {
        m_dock->setFixedSize(along, crossLen);
    }
    m_dock->setCapsuleRadius(24);

    auto place = [&](QWidget* w, int pos, int len, int cross) {
        const int off = (crossLen - cross) / 2;
        if (vertical) {
            w->setGeometry(off, pos, cross, len);
        } else {
            w->setGeometry(pos, off, len, cross);
        }
        w->raise();
    };

    int pos = pad;
    for (QToolButton* button : m_writingBtns) {
        place(button, pos, toolSize, toolSize);
        pos += toolSize + 2;
    }
    pos += 4;
    if (vertical) {
        m_dockDivider->setGeometry(22, pos, crossLen - 44, 1);
    } else {
        m_dockDivider->setGeometry(pos, 22, 1, crossLen - 44);
    }
    pos += 8;
    for (QToolButton* button : m_secondaryBtns) {
        place(button, pos, toolSize, toolSize);
        pos += toolSize + 2;
    }
    pos += 4;
    if (m_trayDivider) {
        if (vertical) {
            m_trayDivider->setGeometry(20, pos, crossLen - 40, 1);
        } else {
            m_trayDivider->setGeometry(pos, 20, 1, crossLen - 40);
        }
    }
    pos += 8;
    for (ColorDotButton* dot : m_trayDots) {
        place(dot, pos, dotSize, dotSize);
        pos += dotSize + 4;
    }
}

void FloatingToolbar::buildZoomPill() {
    m_zoomPill = new GlassCapsule(this);
    m_zoomPill->setObjectName(QStringLiteral("finilyZoom"));
    auto* row = new QHBoxLayout(m_zoomPill);
    row->setContentsMargins(8, 5, 8, 5);
    row->setSpacing(2);

    m_zoomOutBtn = new QToolButton(m_zoomPill);
    m_zoomOutBtn->setFixedSize(30, 30);
    m_zoomOutBtn->setIconSize(QSize(16, 16));
    m_zoomOutBtn->setToolTip(QStringLiteral("Alejar"));
    polishButton(m_zoomOutBtn);
    connect(m_zoomOutBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->zoomOut();
        }
        updateZoomLabel();
    });
    m_iconSpecs.append(qMakePair(m_zoomOutBtn, QStringLiteral("zoom-out")));
    row->addWidget(m_zoomOutBtn);

    m_zoomResetBtn = new QToolButton(m_zoomPill);
    m_zoomResetBtn->setObjectName(QStringLiteral("finilyZoomReset"));
    m_zoomResetBtn->setFixedHeight(30);
    m_zoomResetBtn->setText(QStringLiteral("100 %"));
    m_zoomResetBtn->setToolTip(tr("Ajustar página o restablecer zoom (Ctrl+0)"));
    polishButton(m_zoomResetBtn);
    connect(m_zoomResetBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->resetViewport();
        }
        updateZoomLabel();
    });
    row->addWidget(m_zoomResetBtn);

    m_zoomInBtn = new QToolButton(m_zoomPill);
    m_zoomInBtn->setFixedSize(30, 30);
    m_zoomInBtn->setIconSize(QSize(16, 16));
    m_zoomInBtn->setToolTip(QStringLiteral("Acercar"));
    polishButton(m_zoomInBtn);
    connect(m_zoomInBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->zoomIn();
        }
        updateZoomLabel();
    });
    m_iconSpecs.append(qMakePair(m_zoomInBtn, QStringLiteral("zoom-in")));
    row->addWidget(m_zoomInBtn);
}

void FloatingToolbar::buildPagePill() {
    m_pagePill = new GlassCapsule(this);
    m_pagePill->setObjectName(QStringLiteral("finilyPagePill"));
    auto* row = new QHBoxLayout(m_pagePill);
    row->setContentsMargins(8, 5, 8, 5);
    row->setSpacing(2);

    m_pagePrevBtn = new QToolButton(m_pagePill);
    m_pagePrevBtn->setFixedSize(30, 30);
    m_pagePrevBtn->setIconSize(QSize(16, 16));
    m_iconSpecs.append(qMakePair(m_pagePrevBtn, QStringLiteral("chevron-left")));
    m_pagePrevBtn->setToolTip(QStringLiteral("Página anterior (Re Pág)"));
    polishButton(m_pagePrevBtn);
    connect(m_pagePrevBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->prevPage();
        }
        refreshFromCanvas();
    });
    row->addWidget(m_pagePrevBtn);

    m_pageLabel = new QLabel(QStringLiteral("1 / 1"), m_pagePill);
    m_pageLabel->setObjectName(QStringLiteral("finilyPageLabel"));
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setContentsMargins(4, 0, 4, 0);
    m_pageLabel->setFixedHeight(30);
    row->addWidget(m_pageLabel);

    m_pageNextBtn = new QToolButton(m_pagePill);
    m_pageNextBtn->setFixedSize(30, 30);
    m_pageNextBtn->setIconSize(QSize(16, 16));
    m_iconSpecs.append(qMakePair(m_pageNextBtn, QStringLiteral("chevron-right")));
    m_pageNextBtn->setToolTip(QStringLiteral("Página siguiente (Av Pág)"));
    polishButton(m_pageNextBtn);
    connect(m_pageNextBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->nextPage();
        }
        refreshFromCanvas();
    });
    row->addWidget(m_pageNextBtn);

    row->addWidget(makeSeparator(m_pagePill));

    m_pageAddBtn = new QToolButton(m_pagePill);
    m_pageAddBtn->setFixedSize(30, 30);
    m_pageAddBtn->setIconSize(QSize(16, 16));
    m_iconSpecs.append(qMakePair(m_pageAddBtn, QStringLiteral("plus")));
    m_pageAddBtn->setToolTip(QStringLiteral("Añadir página nueva (Ctrl+Mayús+N)"));
    polishButton(m_pageAddBtn);
    connect(m_pageAddBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->addPage();
        }
        refreshFromCanvas();
    });
    row->addWidget(m_pageAddBtn);
    updatePageNavButtons();
}

void FloatingToolbar::buildRestoreButton() {
    m_restoreBtn = new QToolButton(this);
    m_restoreBtn->setObjectName(QStringLiteral("finilyRestore"));
    m_restoreBtn->setFixedSize(26, 26);
    m_restoreBtn->setIconSize(QSize(14, 14));
    m_restoreBtn->setToolTip(QStringLiteral("Mostrar interfaz (Tab)"));
    polishButton(m_restoreBtn);
    connect(m_restoreBtn, &QToolButton::clicked, this, &FloatingToolbar::toggleCleanUi);
    m_iconSpecs.append(qMakePair(m_restoreBtn, QStringLiteral("restore")));
    m_restoreBtn->hide();
}

QString FloatingToolbar::buildStylesheet(const ThemeConfig& theme) const {
    const QString surface = rgba(theme.uiSurface);
    const QString border = rgba(theme.uiBorder, 55);
    const QString borderElev = rgba(theme.isDark ? QColor(255, 255, 255) : QColor(15, 23, 42),
                                    theme.isDark ? 36 : 42);
    const QString text = rgba(theme.uiText);
    const QString muted = rgba(theme.uiMuted);
    const QString hover = rgba(theme.uiText, 26);
    const QString pressed = rgba(theme.uiText, 40);
    const QString checkedBg = rgba(theme.accent, 46);
    const QString checkedBgSoft = rgba(theme.accent, 24);
    const QString groove = rgba(theme.uiText, 55);
    const QString accent = rgba(theme.accent);
    const QString subPage = rgba(theme.accent, 120);
    const QString accentSoft = rgba(theme.accent, 70);
    const QString divider = rgba(theme.uiBorder, 80);

    return QString(R"(
/* Las superficies (pills y popovers) son GlassCapsule pintadas a mano. */
#finilyRestore {
    background: %1;
    border: 1px solid %2;
    border-radius: 13px;
}
#finilySep { background: %14; }
#finilyDivider {
    background: %14;
    min-height: 1px;
    max-height: 1px;
    border: none;
    margin: 3px 2px;
}
QLabel { color: %3; background: transparent; }
QLabel#finilySectionTitle {
    color: %4;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.5px;
}
QLabel#finilyToast { font-size: 12px; font-weight: 600; }
QLabel#finilyPageLabel { font-size: 12px; font-weight: 600; }
QLabel#finilyShortcut { color: %4; font-size: 11px; }
QLabel#finilyValue { font-size: 12px; font-weight: 600; }
QToolButton#finilyMenuRow { border-radius: 9px; font-size: 13px; }
QToolButton#finilyMenuRow QLabel { font-size: 13px; }
QToolButton#finilyMenuRow QLabel#finilyShortcut { font-size: 11px; }
QToolButton#finilyShapeBtn { font-size: 11px; padding: 0px 5px; border-radius: 10px; }
QToolButton#finilyShapeBtn:hover { background: %5; }
QToolButton#finilyShapeBtn:checked { background: %6; border: 1px solid %9; }
QFrame#finilyStickerSeg {
    background: %5;
    border: 1px solid %14;
    border-radius: 10px;
}
QToolButton#finilyStickerSegBtn {
    background: transparent;
    border: none;
    border-radius: 8px;
    font-size: 11px;
    font-weight: 600;
    color: %4;
    padding: 0px 8px;
}
QToolButton#finilyStickerSegBtn:hover { color: %3; background: %5; }
QToolButton#finilyStickerSegBtn:checked {
    background: %7;
    color: %3;
    border: 1px solid %13;
}
QToolButton#finilyStickerChip {
    background: %5;
    border: 1px solid transparent;
    border-radius: 8px;
    font-size: 11px;
    font-weight: 500;
    color: %4;
    padding: 0px 6px;
}
QToolButton#finilyStickerChip:hover { color: %3; border: 1px solid %14; }
QToolButton#finilyStickerChip:checked {
    background: %7;
    color: %3;
    border: 1px solid %13;
    font-weight: 600;
}
QToolButton#finilyStickerCard {
    background: %5;
    border: 1px solid %14;
    border-radius: 12px;
    padding: 0px;
}
QToolButton#finilyStickerCard:hover { background: %6; border: 1px solid %9; }
QToolButton#finilyStickerCard:pressed { background: %7; }
QLabel#finilyStickerCardTitle {
    color: %3;
    font-size: 11px;
    font-weight: 600;
    background: transparent;
}
QLabel#finilyStickerCardFormula {
    color: %4;
    font-size: 10px;
    font-weight: 500;
    background: transparent;
}
QLineEdit#finilyStickerSearch {
    background: %5;
    color: %3;
    border: 1px solid %14;
    border-radius: 10px;
    padding: 0px 11px;
    font-size: 12px;
}
QLineEdit#finilyStickerSearch:focus { border: 1px solid %9; }
QToolButton#finilyStickerSaveBtn {
    background: %5;
    border: 1px dashed %12;
    border-radius: 10px;
    color: %3;
    font-size: 11px;
    font-weight: 600;
    padding: 0px 10px;
}
QToolButton#finilyStickerSaveBtn:hover { background: %6; border: 1px solid %9; }
QScrollArea#finilyStickerScroll { background: transparent; border: none; }
QScrollArea#finilyStickerScroll > QWidget > QWidget { background: transparent; }
QScrollArea#finilyStickerScroll QScrollBar:vertical {
    background: transparent;
    width: 6px;
    margin: 2px 0px 2px 2px;
    border: none;
}
QScrollArea#finilyStickerScroll QScrollBar::handle:vertical {
    background: %12;
    min-height: 28px;
    border-radius: 3px;
}
QScrollArea#finilyStickerScroll QScrollBar::handle:vertical:hover {
    background: %9;
}
QScrollArea#finilyStickerScroll QScrollBar::add-line:vertical,
QScrollArea#finilyStickerScroll QScrollBar::sub-line:vertical,
QScrollArea#finilyStickerScroll QScrollBar::add-page:vertical,
QScrollArea#finilyStickerScroll QScrollBar::sub-page:vertical {
    background: transparent;
    height: 0px;
    border: none;
}
QToolButton#finilyAccentBtn { background: %11; font-weight: 600; border: 1px solid %12; }
QToolButton, QPushButton {
    background: transparent;
    border: none;
    border-radius: 11px;
    color: %3;
    padding: 2px;
    font-size: 12px;
}
QToolButton:hover, QPushButton:hover { background: %5; }
QToolButton:pressed, QPushButton:pressed { background: %6; }
QToolButton:checked, QPushButton:checked {
    background: %7;
    border: 1px solid %13;
}
QToolButton#finilyMoreToggle {
    color: %4;
    font-size: 11px;
    font-weight: 600;
    text-align: left;
    border-radius: 8px;
    padding: 0 4px;
}
QToolButton#finilyMoreToggle:hover { color: %3; background: transparent; }
QToolButton:hover:checked, QPushButton:hover:checked { background: %13; }
QToolButton:disabled { color: %4; }
QToolButton#finilyWritingTool, QToolButton#finilyTool {
    border-radius: 12px;
    border: 1.5px solid transparent;
    padding: 0px;
}
QToolButton#finilyWritingTool:hover, QToolButton#finilyTool:hover {
    background: %5;
}
QToolButton#finilyWritingTool:checked, QToolButton#finilyTool:checked {
    background: %7;
    border: 1.5px solid transparent;
}
QToolButton#finilyWritingTool:hover:checked, QToolButton#finilyTool:hover:checked {
    background: %11;
    border: 1.5px solid transparent;
}
QToolButton#finilyZoomReset { font-size: 11px; padding: 0px 8px; border-radius: 14px; }
QSlider { background: transparent; min-height: 22px; }
QSlider::groove:horizontal { border: none; height: 4px; border-radius: 2px; background: %8; }
QSlider::sub-page:horizontal { background: %10; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 14px; height: 14px; margin: -5px 0px; border-radius: 7px; background: %9;
}
QSlider::handle:horizontal:hover { background: %3; }
QSlider::handle:horizontal:disabled { background: %4; }
)")
        .arg(surface)
        .arg(border)
        .arg(text)
        .arg(muted)
        .arg(hover)
        .arg(pressed)
        .arg(checkedBg)
        .arg(groove)
        .arg(accent)
        .arg(subPage)
        .arg(accentSoft)
        .arg(borderElev)
        .arg(checkedBgSoft)
        .arg(divider);
}

void FloatingToolbar::applyThemeStylesheet() {
    const ThemeConfig theme = getThemeConfig(m_theme);

    if (!m_presetsRestored) {
        m_presets[0].color = getPaletteForTheme(theme.isDark).first();
        m_presets[0].label =
            theme.isDark ? QStringLiteral("Tiza principal") : QStringLiteral("Tinta principal");
    }
    for (PenPreset& preset : m_presets) {
        if (preset.tool != ToolType::Highlighter) {
            preset.color = adaptInkToTheme(preset.color, theme.isDark);
        }
    }

    const QString qss = buildStylesheet(theme);
    setStyleSheet(qss);
    for (GlassCapsule* capsule : m_capsules) {
        if (capsule) {
            capsule->applyTheme(theme, qss);
        }
    }

    refreshIcons();
    updateStationery();
    refreshShadowColors();
}

void FloatingToolbar::refreshIcons() {
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor ink = m_canvas ? m_canvas->inkColor() : theme.accent;

    for (const auto& spec : m_studioUtilSpecs) {
        spec.first->setIcon(makeStudioIcon(spec.second, 22, theme.uiText, theme.accent, ink, theme.isDark));
    }
    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(
            QString("color:%1;font-size:13px;font-weight:600;background:transparent;")
                .arg(theme.uiText.name()));
    }
    updateTitleDot();

    for (int i = 0; i < m_secondaryBtns.size() && i < m_secondaryIcons.size(); ++i) {
        m_secondaryBtns.at(i)->setIcon(dockIcon(m_secondaryIcons.at(i), theme.uiText, theme.accent));
    }

    static const QStringList dockKinds = {QStringLiteral("pen"), QStringLiteral("highlighter"),
                                          QStringLiteral("eraser"), QStringLiteral("paint-bucket")};
    for (const auto& spec : m_iconSpecs) {
        spec.first->setIcon(dockKinds.contains(spec.second)
                                ? dockIcon(spec.second, theme.uiText, theme.accent)
                                : glyphIcon(spec.second, theme.uiText));
    }
    for (const auto& spec : m_ctxIconSpecs) {
        spec.first->setIcon(QIcon(contextIconPixmap(spec.second, theme.uiText)));
    }
}

void FloatingToolbar::positionChildren() {
    if (m_pill && !m_pill->isHidden()) {
        m_pill->adjustSize();
        m_pill->move(14, 12);
    }
    if (m_topRight && !m_topRight->isHidden()) {
        m_topRight->adjustSize();
        m_topRight->move(width() - m_topRight->width() - 14, 12);
    }
    if (m_zoomPill && !m_zoomPill->isHidden()) {
        m_zoomPill->adjustSize();
        m_zoomPill->move(width() - m_zoomPill->width() - 14, height() - m_zoomPill->height() - 14);
    }
    if (m_pagePill && !m_pagePill->isHidden()) {
        m_pagePill->adjustSize();
        const int zx = (m_zoomPill && !m_zoomPill->isHidden()) ? (m_zoomPill->x() - 8) : (width() - 14);
        m_pagePill->move(zx - m_pagePill->width(), height() - m_pagePill->height() - 14);
    }
    if (m_dock && !m_dock->isHidden()) {
        layoutDock();
        if (m_dockPos == DockPos::Izquierda) {
            m_dock->move(12, std::max(12, (height() - m_dock->height()) / 2));
        } else if (m_dockPos == DockPos::Arriba) {
            const int centeredX = (width() - m_dock->width()) / 2;
            const int minX = (m_pill && !m_pill->isHidden()) ? (m_pill->geometry().right() + 14) : 14;
            m_dock->move(std::max(minX, centeredX), 12);
        } else {
            int x = (width() - m_dock->width()) / 2;
            if (m_pagePill && !m_pagePill->isHidden()) {
                x = std::min(x, m_pagePill->geometry().left() - m_dock->width() - 14);
            }
            m_dock->move(std::max(12, x), height() - m_dock->height() - 12);
        }
    }
    if (m_tabBarPill && !m_tabBarPill->isHidden()) {
        m_tabBarPill->adjustSize();
        const int leftBound = (m_pill && !m_pill->isHidden()) ? (m_pill->geometry().right() + 12) : 14;
        const int rightBound =
            (m_topRight && !m_topRight->isHidden()) ? (m_topRight->x() - 12) : (width() - 14);
        if (m_dockPos != DockPos::Arriba && (rightBound - leftBound >= m_tabBarPill->width())) {
            const int x = leftBound + (rightBound - leftBound - m_tabBarPill->width()) / 2;
            m_tabBarPill->move(x, 14);
        } else {
            int belowY = (m_pill && !m_pill->isHidden()) ? (m_pill->geometry().bottom() + 8) : 60;
            if (m_dockPos == DockPos::Arriba && m_dock && !m_dock->isHidden()) {
                belowY = std::max(belowY, m_dock->geometry().bottom() + 8);
            }
            const int minX =
                (m_dockPos == DockPos::Izquierda && m_dock && !m_dock->isHidden())
                    ? (m_dock->geometry().right() + 12)
                    : 14;
            m_tabBarPill->move(minX, belowY);
        }
    }
    if (m_thumbPanel && !m_thumbPanel->isHidden()) {
        const int top = (m_topRight && !m_topRight->isHidden()) ? (m_topRight->geometry().bottom() + 10) : 60;
        const int zoomH = (m_zoomPill && !m_zoomPill->isHidden()) ? m_zoomPill->height() : 40;
        const int bottom = height() - zoomH - 28;
        m_thumbPanel->setGeometry(width() - m_thumbPanel->width() - 14, top, m_thumbPanel->width(),
                                  std::max(160, bottom - top));
    }
    if (m_restoreBtn && !m_restoreBtn->isHidden()) {
        m_restoreBtn->move(width() - 34, 10);
    }
    if (m_contextPill && !m_contextPill->isHidden()) {
        m_contextPill->adjustSize();
        const int x = std::max(76, (width() - m_contextPill->width()) / 2);
        int y = std::max(12, height() - m_contextPill->height() - 16);
        if (m_dockPos == DockPos::Abajo && m_dock && !m_dock->isHidden()) {
            y = std::max(12, m_dock->geometry().top() - m_contextPill->height() - 10);
        }
        m_contextPill->move(x, y);
    }
    if (m_toastPill && !m_toastPill->isHidden()) {
        m_toastPill->adjustSize();
        const int y = (m_dockPos == DockPos::Abajo) ? 18 : (height() - m_toastPill->height() - 64);
        m_toastPill->move(std::max(6, (width() - m_toastPill->width()) / 2), y);
    }
    positionOpenPopover();
    updateOverlayMask();
}

void FloatingToolbar::updateOverlayMask() {
    QRegion region;
    for (GlassCapsule* capsule : m_capsules) {
        if (!capsule || capsule->isHidden()) {
            continue;
        }
        region += capsule->geometry().adjusted(-8, -6, 8, 12);
    }
    if (m_restoreBtn && !m_restoreBtn->isHidden()) {
        region += m_restoreBtn->geometry().adjusted(-8, -6, 8, 12);
    }
    if (region != m_lastOverlayMask) {
        m_lastOverlayMask = region;
        setMask(region);
    }
}

void FloatingToolbar::paintEvent(QPaintEvent* event) {
    // Sombra ligera directa: evita 14 blur Gaussiano CPU de QGraphicsDropShadowEffect durante Meet.
    const bool isDark = getThemeConfig(m_theme).isDark;
    const int alphas[3] = {isDark ? 24 : 14, isDark ? 13 : 8, isDark ? 6 : 4};
    const int spreads[3] = {1, 3, 5};
    const int yOffsets[3] = {2, 4, 6};

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const QRect dirty = event->rect();
    for (const GlassCapsule* capsule : m_capsules) {
        if (!capsule || !capsule->isVisible() || capsule == m_toastPill)
            continue;
        const QRect base = capsule->geometry();
        if (!base.adjusted(-6, -2, 6, 10).intersects(dirty))
            continue;
        const int radius = capsule->capsuleRadius();
        for (int layer = 2; layer >= 0; --layer) {
            const int spread = spreads[layer];
            const QRectF r = QRectF(base).adjusted(-spread, -spread + yOffsets[layer],
                                                  spread, spread + yOffsets[layer]);
            painter.setBrush(QColor(0, 0, 0, alphas[layer]));
            painter.drawRoundedRect(r, radius + spread, radius + spread);
        }
    }
}

void FloatingToolbar::tabletEvent(QTabletEvent* event) {
    event->ignore();
}

QPoint FloatingToolbar::popoverPosition(const GlassCapsule* popover, const QWidget* anchor) const {
    const int margin = 12;
    int x = margin;
    int y = (m_pill && !m_pill->isHidden() ? m_pill->geometry().bottom() : 40) + 10;
    if (anchor && m_topRight && anchor->parentWidget() == m_topRight) {
        x = m_topRight->geometry().right() - popover->width();
        y = m_topRight->geometry().bottom() + 10;
    } else if (m_dockPos == DockPos::Izquierda && m_dock && !m_dock->isHidden()) {
        x = m_dock->geometry().right() + 10;
        if (anchor && anchor->parentWidget() == m_dock) {
            y = m_dock->y() + anchor->y() - 12;
        }
    } else if (m_dockPos == DockPos::Arriba && m_dock && !m_dock->isHidden()) {
        x = margin;
        y = m_dock->geometry().bottom() + 10;
    }
    if (m_dockPos == DockPos::Abajo && m_dock && !m_dock->isHidden()) {
        y = std::min(y, std::max(margin, m_dock->geometry().top() - popover->height() - 10));
    }
    x = std::clamp(x, margin, std::max(margin, width() - popover->width() - margin));
    y = std::clamp(y, margin, std::max(margin, height() - popover->height() - margin));
    return QPoint(x, y);
}

void FloatingToolbar::positionOpenPopover() {
    if (!m_openPopover) {
        return;
    }
    m_openPopover->adjustSize();
    const QPoint target = popoverPosition(m_openPopover, m_popoverAnchor);
    if (m_popoverAnim && m_popoverAnim->state() == QAbstractAnimation::Running) {
        if (m_popoverAnim->endValue().toPoint() == target) {
            return;
        }
        m_popoverAnim->stop();
    }
    m_openPopover->move(target);
}

void FloatingToolbar::openPopover(GlassCapsule* popover, QWidget* anchor) {
    if (!popover || !anchor) {
        return;
    }
    const bool wasOpen = (m_openPopover == popover);
    closePopovers();
    if (wasOpen) {
        return;
    }
    m_openPopover = popover;
    m_popoverAnchor = anchor;

    popover->show();
    popover->adjustSize();
    const QPoint finalPos = popoverPosition(popover, anchor);
    QPoint offset(0, 8);
    if ((m_topRight && anchor->parentWidget() == m_topRight) || m_dockPos == DockPos::Abajo) {
        offset = QPoint(0, -8);
    } else if (m_dockPos == DockPos::Izquierda) {
        offset = QPoint(-8, 0);
    }
    const QPoint startPos = finalPos + offset;
    popover->move(startPos);
    popover->raise();
    updateOverlayMask();

    m_popoverAnim = new QPropertyAnimation(popover, "pos", this);
    m_popoverAnim->setDuration(140);
    m_popoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_popoverAnim->setStartValue(startPos);
    m_popoverAnim->setEndValue(finalPos);
    connect(m_popoverAnim, &QPropertyAnimation::valueChanged, this,
            [this](const QVariant&) { updateOverlayMask(); });
    m_popoverAnim->start();
}

void FloatingToolbar::buildEdgeMarkPill() {
    m_edgeMarkPill = new GlassCapsule(this);
    m_edgeMarkPill->setObjectName(QStringLiteral("finilyContextPill"));
    auto* col = new QVBoxLayout(m_edgeMarkPill);
    col->setContentsMargins(7, 6, 7, 6);
    col->setSpacing(5);

    auto* marksRow = new QHBoxLayout();
    marksRow->setContentsMargins(0, 0, 0, 0);
    marksRow->setSpacing(3);

    struct EdgeMarkDef {
        QString glyph;
        QString mark;
        QString tip;
    };
    const QVector<EdgeMarkDef> defs = {
        {QStringLiteral("mark-zigzag"), QStringLiteral("mark:zigzag"), tr("Serrucho (zig-zag)")},
        {QStringLiteral("mark-spring"), QStringLiteral("mark:spring"), tr("Resorte (ondas)")},
        {QStringLiteral("mark-dot"), QStringLiteral("mark:dot"), tr("Bolita (congruencia)")},
        {QStringLiteral("mark-divider"), QStringLiteral("mark:divider"),
         tr("Punto divisor (parte la arista para marcas y recorte)")},
        {QStringLiteral("mark-bar"), QStringLiteral("mark:bar"), tr("Bultito / rectángulo")},
        {QStringLiteral("mark-ticks"), QStringLiteral("mark:tick1"),
         tr("Rayitas de congruencia (cicla 1–3)")},
        {QStringLiteral("mark-parallel"), QStringLiteral("mark:parallel1"),
         tr("Flechas paralelas (cicla 1–2)")},
        {QStringLiteral("mark-wall"), QStringLiteral("mark:wall"),
         tr("Pared / suelo rugoso (repite clic para invertir lado)")},
    };
    for (const auto& def : defs) {
        auto* btn = new QToolButton(m_edgeMarkPill);
        btn->setObjectName(QStringLiteral("finilyShapeBtn"));
        btn->setFixedSize(36, 32);
        btn->setIconSize(QSize(19, 19));
        btn->setToolTip(def.tip);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, mark = def.mark] {
            if (m_canvas) {
                m_canvas->stampCongruenceMarkAt(m_edgeMarkWorldPos, mark);
            }
            if (m_edgeMarkPill) {
                m_edgeMarkPill->hide();
            }
            updateOverlayMask();
        });
        m_iconSpecs.append(qMakePair(btn, def.glyph));
        marksRow->addWidget(btn);
    }
    col->addLayout(marksRow);
    col->addWidget(makeDivider(m_edgeMarkPill));

    auto* colorRow = new QHBoxLayout();
    colorRow->setContentsMargins(0, 0, 0, 0);
    colorRow->setSpacing(8);
    colorRow->addStretch(1);

    const QVector<QPair<QString, QString>> quickColors = {
        {QStringLiteral("#fde047"), tr("Amarillo luminoso")},
        {QStringLiteral("#38bdf8"), tr("Celeste")},
        {QStringLiteral("#f87171"), tr("Coral / Rojo")},
        {QStringLiteral("#4ade80"), tr("Verde")},
        {QStringLiteral("#c084fc"), tr("Morado")},
        {QStringLiteral("#fb923c"), tr("Naranja")},
    };
    for (const auto& [hex, tip] : quickColors) {
        const QColor c(hex);
        auto* dot = new ColorDotButton(m_edgeMarkPill);
        dot->setDotColor(c);
        dot->setCheckable(false);
        dot->setFixedSize(24, 24);
        dot->setToolTip(tr("Resaltar tramo/arco en %1").arg(tip));
        connect(dot, &QToolButton::clicked, this, [this, c] {
            if (m_canvas) {
                m_canvas->highlightSegmentAt(m_edgeMarkWorldPos, c, 0);
            }
            if (m_edgeMarkPill) {
                m_edgeMarkPill->hide();
            }
            updateOverlayMask();
        });
        m_edgeMarkDots.append(dot);
        colorRow->addWidget(dot);
    }
    colorRow->addStretch(1);
    col->addLayout(colorRow);
    m_edgeMarkPill->hide();
}

void FloatingToolbar::showEdgeMarkPill(const QPointF& screenPos, const QPointF& worldPos) {
    if (!m_edgeMarkPill) {
        return;
    }
    closePopovers();
    m_edgeMarkWorldPos = worldPos;
    m_edgeMarkPill->adjustSize();
    const int margin = 12;
    const int w = m_edgeMarkPill->width();
    const int h = m_edgeMarkPill->height();
    const int x = std::clamp(qRound(screenPos.x()) - w / 2, margin, std::max(margin, width() - w - margin));
    int y = qRound(screenPos.y()) - h - 18;
    if (y < margin) {
        y = std::min(height() - h - margin, qRound(screenPos.y()) + 18);
    }
    m_edgeMarkPill->move(x, y);
    m_edgeMarkPill->show();
    m_edgeMarkPill->raise();
    updateOverlayMask();
}

void FloatingToolbar::closePopovers() {
    bool changed = false;
    if (m_edgeMarkPill && !m_edgeMarkPill->isHidden()) {
        m_edgeMarkPill->hide();
        changed = true;
    }
    if (m_openPopover) {
        if (m_popoverAnim) {
            m_popoverAnim->stop();
            m_popoverAnim->deleteLater();
            m_popoverAnim = nullptr;
        }
        m_openPopover->close();
        m_openPopover = nullptr;
        m_popoverAnchor = nullptr;
        changed = true;
    }
    if (changed) {
        updateOverlayMask();
    }
}

void FloatingToolbar::showToast(const QString& message) {
    if (!m_toastPill || !m_toastLabel) {
        return;
    }
    if (m_toastAnim) {
        m_toastAnim->stop();
        m_toastAnim->deleteLater();
        m_toastAnim = nullptr;
    }
    if (m_toastTimer) {
        m_toastTimer->stop();
    }
    m_toastLabel->setText(message);
    m_toastPill->adjustSize();
    m_toastPill->show();
    m_toastPill->raise();
    positionChildren();

    auto* opacity = qobject_cast<QGraphicsOpacityEffect*>(m_toastPill->graphicsEffect());
    if (!opacity) {
        opacity = new QGraphicsOpacityEffect(m_toastPill);
        m_toastPill->setGraphicsEffect(opacity);
    }
    opacity->setOpacity(0.0);
    m_toastAnim = new QPropertyAnimation(opacity, "opacity", this);
    m_toastAnim->setDuration(140);
    m_toastAnim->setStartValue(0.0);
    m_toastAnim->setEndValue(1.0);
    m_toastAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_toastAnim->start();
    if (m_toastTimer) {
        m_toastTimer->start(1200);
    }
}

void FloatingToolbar::refreshShadowColors() {
    update();
}

void FloatingToolbar::loadDockPosition() {
    const auto settings = appSettings();
    const QString pos = settings->value(QStringLiteral("toolbar/trayPos")).toString();
    if (pos == QLatin1String("top")) {
        m_dockPos = DockPos::Arriba;
    } else if (pos == QLatin1String("bottom")) {
        m_dockPos = DockPos::Abajo;
    } else {
        m_dockPos = DockPos::Izquierda;
    }
}

void FloatingToolbar::setDockPosition(DockPos pos) {
    if (m_dockPos == pos) {
        return;
    }
    m_dockPos = pos;
    auto settings = appSettings();
    settings->setValue(QStringLiteral("toolbar/trayPos"),
                       pos == DockPos::Izquierda ? QStringLiteral("left")
                                                 : (pos == DockPos::Arriba ? QStringLiteral("top")
                                                                           : QStringLiteral("bottom")));
    if (m_paperPopover) {
        m_paperPopover->setToolbarState(m_dockPos, m_pageNavDir);
    }

    GlassCapsule* open = m_openPopover;
    QWidget* anchor = m_popoverAnchor;
    closePopovers();
    positionChildren();
    refreshFromCanvas();
    if (open) {
        openPopover(open, anchor);
    }
}

void FloatingToolbar::toggleCleanUi() {
    m_cleanUi = !m_cleanUi;
    if (m_pillAnim) {
        QObject::disconnect(m_pillAnim, &QPropertyAnimation::finished, this, nullptr);
        m_pillAnim->stop();
        m_pillAnim->deleteLater();
        m_pillAnim = nullptr;
    }
    if (m_cleanUi) {
        closePopovers();
        if (m_zoomPill) {
            m_zoomPill->hide();
        }
        if (m_tabBarPill) {
            m_tabBarPill->hide();
        }
        if (m_contextPill) {
            m_contextPill->hide();
        }
        if (m_pagePill) {
            m_pagePill->hide();
        }
        if (m_topRight) {
            m_topRight->hide();
        }
        if (m_thumbPanel) {
            m_thumbPanel->hide();
        }
        if (m_dock) {
            m_dock->hide();
        }
        if (m_restoreBtn) {
            m_restoreBtn->show();
        }
        positionChildren();
        if (m_pill && m_pill->isVisible()) {
            const QPoint start = m_pill->pos();
            m_pillAnim = new QPropertyAnimation(m_pill, "pos", this);
            m_pillAnim->setDuration(150);
            m_pillAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_pillAnim->setStartValue(start);
            m_pillAnim->setEndValue(QPoint(start.x(), start.y() - 18));
            connect(m_pillAnim, &QPropertyAnimation::valueChanged, this,
                    [this](const QVariant&) { updateOverlayMask(); });
            connect(m_pillAnim, &QPropertyAnimation::finished, this, [this] {
                if (m_pill) {
                    m_pill->hide();
                }
                positionChildren();
            });
            m_pillAnim->start();
        }
    } else {
        if (m_restoreBtn) {
            m_restoreBtn->hide();
        }
        if (m_pill) {
            m_pill->show();
        }
        if (m_tabBarPill && m_tabCount >= 2) {
            m_tabBarPill->show();
        }
        if (m_zoomPill) {
            m_zoomPill->show();
        }
        if (m_pagePill) {
            m_pagePill->show();
        }
        if (m_topRight) {
            m_topRight->show();
        }
        if (m_thumbPanel && m_pagesBtn && m_pagesBtn->isChecked()) {
            m_thumbPanel->show();
            m_thumbPanel->rebuild();
        }
        if (m_dock) {
            m_dock->show();
        }
        m_lastCanvasState.valid = false;
        refreshFromCanvas();
        positionChildren();
        if (m_pill) {
            const QPoint target = m_pill->pos();
            m_pillAnim = new QPropertyAnimation(m_pill, "pos", this);
            m_pillAnim->setDuration(150);
            m_pillAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_pillAnim->setStartValue(QPoint(target.x(), target.y() - 16));
            m_pillAnim->setEndValue(target);
            connect(m_pillAnim, &QPropertyAnimation::valueChanged, this,
                    [this](const QVariant&) { updateOverlayMask(); });
            m_pillAnim->start();
        }
    }
    updateOverlayMask();
}

void FloatingToolbar::selectTool(ToolType tool) {
    if (!m_canvas) {
        return;
    }
    if (m_activePreset >= 0 && m_activePreset < m_presets.size()
        && m_presets.at(m_activePreset).tool != tool) {
        m_activePreset = -1;
    }
    m_canvas->setTool(tool);
    refreshFromCanvas();
}

QString FloatingToolbar::presetShortcut(int index) const {
    if (index < 0 || index >= m_presets.size()) {
        return {};
    }
    return m_presets.at(index).shortcut;
}

void FloatingToolbar::setPageNavDirection(PageNavDirection dir) {
    if (m_pageNavDir == dir) {
        return;
    }
    m_pageNavDir = dir;
    auto settings = appSettings();
    settings->setValue(QStringLiteral("ui/pageNavDirection"), dir == PageNavDirection::Vertical ? 1 : 0);
    if (m_paperPopover) {
        m_paperPopover->setToolbarState(m_dockPos, m_pageNavDir);
    }
    updatePageNavButtons();
    emit pageNavDirectionChanged(dir);
    refreshFromCanvas();
}

void FloatingToolbar::updatePageNavButtons() {
    const bool vert = (m_pageNavDir == PageNavDirection::Vertical);
    const QString prevName = vert ? QStringLiteral("chevron-up") : QStringLiteral("chevron-left");
    const QString nextName = vert ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right");
    for (auto& spec : m_iconSpecs) {
        if (spec.first == m_pagePrevBtn) {
            spec.second = prevName;
        } else if (spec.first == m_pageNextBtn) {
            spec.second = nextName;
        }
    }
    const QColor uiText = getThemeConfig(m_theme).uiText;
    if (m_pagePrevBtn) {
        m_pagePrevBtn->setIcon(glyphIcon(prevName, uiText));
        m_pagePrevBtn->setToolTip(vert ? tr("Página anterior (Re Pág / Alt+↑)")
                                       : tr("Página anterior (Re Pág / Alt+←)"));
    }
    if (m_pageNextBtn) {
        m_pageNextBtn->setIcon(glyphIcon(nextName, uiText));
        m_pageNextBtn->setToolTip(vert ? tr("Página siguiente (Av Pág / Alt+↓)")
                                       : tr("Página siguiente (Av Pág / Alt+→)"));
    }
}

void FloatingToolbar::updatePresetButtons() {
    if (!m_strokePopover) {
        return;
    }
    const PenPreset* active =
        (m_activePreset >= 0 && m_activePreset < m_presets.size()) ? &m_presets.at(m_activePreset) : nullptr;
    m_strokePopover->setActivePresetIndex(m_activePreset, active);
}

void FloatingToolbar::showFavoritesMenu(QWidget* anchor) {
    QWidget* menuAnchor = anchor ? anchor : static_cast<QWidget*>(m_strokePopover);
    if (!menuAnchor) {
        return;
    }
    QMenu menu(this);
    for (int i = 0; i < m_presets.size(); ++i) {
        const PenPreset& preset = m_presets.at(i);
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter pp(&pm);
        pp.setRenderHint(QPainter::Antialiasing, true);
        pp.setPen(QPen(QColor(128, 128, 128, 120), 1.5));
        pp.setBrush(preset.color);
        pp.drawEllipse(QRectF(2, 2, 16, 16));
        pp.end();
        pm.setDevicePixelRatio(2.0);
        const QString sc =
            preset.shortcut.isEmpty() ? QString() : QStringLiteral("  (%1)").arg(preset.shortcut);
        auto* act = menu.addAction(QIcon(pm), preset.label + sc, [this, i] { selectPreset(i); });
        act->setCheckable(true);
        act->setChecked(i == m_activePreset);
    }
    menu.addSeparator();
    auto* saveMenu = menu.addMenu(tr("★ Guardar pincel actual en…"));
    for (int i = 0; i < m_presets.size(); ++i) {
        const PenPreset& preset = m_presets.at(i);
        const QString sc =
            preset.shortcut.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(preset.shortcut);
        saveMenu->addAction(QStringLiteral("%1%2").arg(preset.label, sc),
                            [this, i] { saveCurrentToPreset(i); });
    }
    auto* editMenu = menu.addMenu(tr("Configurar nombre / atajo…"));
    for (int i = 0; i < m_presets.size(); ++i) {
        const PenPreset& preset = m_presets.at(i);
        const QString sc = preset.shortcut.isEmpty() ? tr("sin atajo") : preset.shortcut;
        editMenu->addAction(QStringLiteral("%1 (%2)").arg(preset.label, sc), [this, i, menuAnchor] {
            editPresetMenu(i, menuAnchor->mapToGlobal(QPoint(0, menuAnchor->height())));
        });
    }
    menu.exec(menuAnchor->mapToGlobal(QPoint(0, menuAnchor->height())));
}

void FloatingToolbar::saveCurrentToPreset(int index) {
    if (index < 0 || index >= m_presets.size() || !m_canvas) {
        return;
    }
    const ToolType t = m_canvas->tool();
    if (t == ToolType::Eraser || t == ToolType::Select || t == ToolType::Hand || t == ToolType::Laser) {
        return;
    }
    PenPreset& preset = m_presets[index];
    preset.tool = t;
    preset.color = m_canvas->inkColor();
    preset.size = m_canvas->strokeSize();
    preset.linePattern = m_canvas->linePattern();
    preset.tipStyle = (t == ToolType::Highlighter) ? m_canvas->highlighterTip() : m_canvas->penTip();
    preset.opacity = (t == ToolType::Highlighter) ? m_canvas->highlighterOpacity() : 1.0;
    m_activePreset = index;
    m_presetsRestored = true;
    savePresets();
    updatePresetButtons();
    showToast(tr("Favorito «%1» actualizado").arg(preset.label));
}

void FloatingToolbar::editPresetMenu(int index, const QPoint& globalPos) {
    if (index < 0 || index >= m_presets.size()) {
        return;
    }
    QMenu menu(this);
    menu.addAction(tr("Guardar pincel actual aquí"), [this, index] { saveCurrentToPreset(index); });
    menu.addAction(tr("Renombrar favorito…"), [this, index] {
        bool ok = false;
        const QString name = QInputDialog::getText(window(), tr("Renombrar favorito"), tr("Nombre:"),
                                                   QLineEdit::Normal, m_presets.at(index).label, &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return;
        }
        m_presets[index].label = name.trimmed();
        m_presetsRestored = true;
        savePresets();
        updatePresetButtons();
    });
    menu.addAction(tr("Cambiar atajo de teclado…"), [this, index] {
        QDialog dialog(window());
        dialog.setWindowTitle(tr("Atajo para «%1»").arg(m_presets.at(index).label));
        auto* col = new QVBoxLayout(&dialog);
        col->addWidget(new QLabel(tr("Pulsa la tecla o combinación que quieres usar:"), &dialog));
        auto* edit = new QKeySequenceEdit(QKeySequence(m_presets.at(index).shortcut), &dialog);
        col->addWidget(edit);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        auto* clearBtn = buttons->addButton(tr("Quitar"), QDialogButtonBox::ResetRole);
        connect(clearBtn, &QPushButton::clicked, edit, &QKeySequenceEdit::clear);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        col->addWidget(buttons);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        m_presets[index].shortcut = edit->keySequence().toString(QKeySequence::PortableText);
        m_presetsRestored = true;
        savePresets();
        updatePresetButtons();
        emit presetsChanged();
    });
    menu.addSeparator();
    menu.addAction(tr("Restablecer favoritos por defecto"), [this] {
        auto settings = appSettings();
        settings->remove(QStringLiteral("presets"));
        m_presetsRestored = false;
        initPresets();
        updatePresetButtons();
        emit presetsChanged();
        refreshFromCanvas();
    });
    menu.exec(globalPos);
}

void FloatingToolbar::selectPreset(int index) {
    if (index < 0 || index >= m_presets.size() || !m_canvas) {
        return;
    }
    m_activePreset = index;
    const PenPreset& preset = m_presets[index];
    m_canvas->setTool(preset.tool);
    m_canvas->setInkColor(preset.color);
    m_canvas->setStrokeSize(preset.size);
    m_canvas->setLinePattern(preset.linePattern);
    if (preset.tool == ToolType::Highlighter) {
        m_canvas->setHighlighterTip(preset.tipStyle);
        m_canvas->setHighlighterOpacity(preset.opacity);
    } else if (preset.tool == ToolType::Pen || preset.tool == ToolType::Ballpoint) {
        m_canvas->setPenTip(preset.tipStyle);
    }
    updatePresetButtons();
    refreshFromCanvas();
}

void FloatingToolbar::setColor(const QColor& color) {
    if (!m_canvas || !color.isValid()) {
        return;
    }
    m_canvas->setInkColor(color);
    syncActivePresetFromCanvas();
    updateStationery();
    refreshFromCanvas();
}

void FloatingToolbar::syncActivePresetFromCanvas() {
    if (!m_canvas || m_activePreset < 0 || m_activePreset >= m_presets.size()) {
        return;
    }
    if (m_canvas->tool() == ToolType::Eraser) {
        return;
    }
    PenPreset& preset = m_presets[m_activePreset];
    preset.tool = m_canvas->tool();
    preset.color = m_canvas->inkColor();
    preset.size = m_canvas->strokeSize();
    preset.linePattern = m_canvas->linePattern();
    preset.tipStyle =
        (preset.tool == ToolType::Highlighter) ? m_canvas->highlighterTip() : m_canvas->penTip();
    preset.opacity = (preset.tool == ToolType::Highlighter) ? m_canvas->highlighterOpacity() : 1.0;
    const QString sig = QString("%1|%2|%3|%4|%5|%6|%7")
                            .arg(m_activePreset)
                            .arg(int(preset.tool))
                            .arg(preset.color.name(QColor::HexArgb))
                            .arg(preset.size)
                            .arg(int(preset.linePattern))
                            .arg(preset.tipStyle)
                            .arg(qRound(preset.opacity * 100.0));
    if (sig != m_lastPresetsSig) {
        m_lastPresetsSig = sig;
        savePresets();
        updatePresetButtons();
    }
}

void FloatingToolbar::updateStationery() {
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor rim = theme.isDark ? QColor(255, 255, 255, 70) : QColor(15, 23, 42, 80);
    for (int i = 0; i < m_trayDots.size(); ++i) {
        ColorDotButton* dot = m_trayDots.at(i);
        if (i >= m_palette.size()) {
            dot->hide();
            continue;
        }
        dot->setDotColor(m_palette.at(i));
        dot->setAccentColor(theme.accent);
        dot->setRimColor(rim);
        dot->show();
    }
    for (ColorDotButton* dot : m_edgeMarkDots) {
        if (!dot) {
            continue;
        }
        dot->setAccentColor(theme.accent);
        dot->setRimColor(rim);
    }
}

void FloatingToolbar::updateZoomLabel() {
    if (!m_canvas || !m_zoomResetBtn) {
        return;
    }
    const int percent = qRound(m_canvas->viewport().zoom * 100.0);
    m_zoomResetBtn->setText(QString("%1 %").arg(percent));
}

void FloatingToolbar::refreshFromCanvas() {
    if (!m_canvas) {
        return;
    }

    const ThemeId theme = m_canvas->themeId();
    if (theme != m_theme) {
        m_theme = theme;
        m_palette = getPaletteForTheme(getThemeConfig(theme).isDark);
        if (m_strokePopover) {
            m_strokePopover->setPalette(m_palette);
        }
        applyThemeStylesheet();
        if (m_canvas->tool() != ToolType::Highlighter) {
            m_canvas->setInkColor(adaptInkToTheme(m_canvas->inkColor(), getThemeConfig(theme).isDark));
        }
    }

    if (m_thumbPanel) {
        m_thumbPanel->syncFromCanvas();
    }
    syncActivePresetFromCanvas();
    updatePresetButtons();

    ToolbarCanvasState state;
    state.tool = m_canvas->tool();
    state.inkColor = m_canvas->inkColor().rgba();
    state.strokeSize = m_canvas->strokeSize();
    state.linePattern = m_canvas->linePattern();
    state.lineEnding = m_canvas->lineEnding();
    state.stabilizer = m_canvas->stabilizer();
    state.highlighterOpacity = m_canvas->highlighterOpacity();
    state.eraserMode = m_canvas->eraserMode();
    state.eraserSize = m_canvas->eraserSize();
    state.eraserPreserveShapes = m_canvas->eraserPreserveShapes();
    state.eraserOnlyHighlight = m_canvas->eraserOnlyHighlight();
    state.pressureSensitivity = m_canvas->pressureSensitivity();
    state.themeId = m_canvas->themeId();
    state.backgroundTemplate = m_canvas->backgroundTemplate();
    state.pageFormat = m_canvas->pageFormat();
    state.templateScale = m_canvas->templateScale();
    state.canUndo = m_canvas->canUndo();
    state.canRedo = m_canvas->canRedo();
    state.zoom = m_canvas->viewport().zoom;
    state.shapeKind = m_canvas->shapeKind();
    state.polygonSides = m_canvas->polygonSides();
    state.triangleVariant = m_canvas->triangleVariant();
    state.quadVariant = m_canvas->quadVariant();
    state.solidKind = m_canvas->solidKind();
    state.congruenceMarkSpec = m_canvas->congruenceMarkSpec();
    state.holdToShapeEnabled = m_canvas->holdToShapeEnabled();
    state.smartGuidesEnabled = m_canvas->smartGuidesEnabled();
    state.mathBeautifyEnabled = m_canvas->mathBeautifyEnabled();
    state.swapPenButtons = m_canvas->swapPenButtons();
    state.cursorStyle = m_canvas->cursorStyle();
    state.drawingPolyline = m_canvas->isDrawingPolyline();
    state.hasSelection = m_canvas->hasSelection();
    state.hasSelectedShape = m_canvas->hasSelectedShape();
    state.hasSelectedAxes = m_canvas->hasSelectedAxes();
    state.hasSelectedStrokes = m_canvas->hasSelectedStrokes();
    state.hasSelectedImage = m_canvas->hasSelectedImage();
    state.hasSelectedText = m_canvas->hasSelectedText();
    state.hasLassoSelection = m_canvas->hasLassoSelection();
    state.canGroupSelection = m_canvas->canGroupSelection();
    state.canUngroupSelection = m_canvas->canUngroupSelection();
    state.activelyDragging = m_canvas->isActivelyDragging();
    state.lassoRectMode = m_canvas->lassoRectMode();
    state.penTip = m_canvas->penTip();
    state.highlighterTip = m_canvas->highlighterTip();
    state.shadePattern = m_canvas->shadePattern();
    state.pdfPageCount = m_canvas->documentPdfPageCount();
    state.currentPageIndex = m_canvas->currentPageIndex();
    state.pageCount = m_canvas->pageCount();
    state.cleanUi = m_cleanUi;
    state.dockPos = m_dockPos;
    state.pageNavDir = m_pageNavDir;
    state.valid = true;

    if (state == m_lastCanvasState) {
        return;
    }
    m_lastCanvasState = state;
    m_updatingUi = true;

    m_undoBtn->setEnabled(state.canUndo);
    m_redoBtn->setEnabled(state.canRedo);

    const ToolType tool = state.tool;
    for (int i = 0; i < m_writingBtns.size() && i < m_writingKinds.size(); ++i) {
        if (m_writingKinds.at(i) == tool
            || (m_writingKinds.at(i) == ToolType::Pen && tool == ToolType::Ballpoint)) {
            m_writingBtns.at(i)->setChecked(true);
        }
    }
    for (int i = 0; i < m_secondaryBtns.size() && i < m_secondaryKinds.size(); ++i) {
        if (m_secondaryBtns.at(i) != m_stickersBtn && m_secondaryKinds.at(i) == tool) {
            m_secondaryBtns.at(i)->setChecked(true);
        }
    }

    const QColor ink = m_canvas->inkColor();
    const bool inking = tool == ToolType::Pen || tool == ToolType::Ballpoint
                        || tool == ToolType::Highlighter || tool == ToolType::Shape
                        || tool == ToolType::Polyline || tool == ToolType::Text || tool == ToolType::Fill;
    for (int i = 0; i < m_trayDots.size() && i < m_palette.size(); ++i) {
        m_trayDots.at(i)->setChecked(inking && m_palette.at(i) == ink);
    }
    updateStationery();

    if (m_strokePopover) {
        m_strokePopover->syncFromCanvas();
    }
    if (m_paperPopover) {
        m_paperPopover->setToolbarState(m_dockPos, m_pageNavDir);
        m_paperPopover->syncFromCanvas();
    }
    if (m_shapesPopover) {
        m_shapesPopover->syncFromCanvas();
    }
    if (m_filePopover) {
        m_filePopover->syncFromCanvas();
    }

    const bool drawingPolyline = state.drawingPolyline;
    const bool hasSel = !drawingPolyline && state.hasSelection;
    m_finishPolylineBtn->setVisible(drawingPolyline);
    m_closePolylineBtn->setVisible(drawingPolyline);
    if (m_ctxBeautifyBtn) {
        m_ctxBeautifyBtn->setVisible(hasSel && state.hasSelectedStrokes);
    }
    if (m_ctxAxesFuncBtn) {
        m_ctxAxesFuncBtn->setVisible(hasSel && state.hasSelectedAxes);
    }
    if (m_ctxDuplicateBtn) {
        m_ctxDuplicateBtn->setVisible(hasSel);
    }
    if (m_ctxDeleteBtn) {
        m_ctxDeleteBtn->setVisible(hasSel);
    }
    m_selectionActions->setVisible(hasSel);
    const bool contextDesired =
        !m_cleanUi && !state.activelyDragging && (drawingPolyline || state.hasSelection);
    const bool contextChanged = (m_contextPill->isVisible() != contextDesired);
    if (contextChanged) {
        m_contextPill->setVisible(contextDesired);
    }
    if (contextChanged || contextDesired) {
        positionChildren();
    }

    m_pageLabel->setText(QString("%1 / %2").arg(state.currentPageIndex + 1).arg(state.pageCount));
    m_pagePrevBtn->setEnabled(state.currentPageIndex > 0);
    m_pageNextBtn->setEnabled(state.currentPageIndex < state.pageCount - 1);
    m_updatingUi = false;

    updateZoomLabel();
}

void FloatingToolbar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionChildren();
}

bool FloatingToolbar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_titleLabel && event->type() == QEvent::MouseButtonRelease && m_titleEdit) {
        closePopovers();
        m_titleEdit->setText(m_titleLabel->text());
        m_titleLabel->hide();
        m_titleEdit->show();
        m_titleEdit->setFocus();
        m_titleEdit->selectAll();
        positionChildren();
        return true;
    }
    if (watched == m_canvas) {
        if (event->type() == QEvent::Resize) {
            setGeometry(m_canvas->rect());
            positionChildren();
        } else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TabletPress) {
            if (m_openPopover || (m_edgeMarkPill && !m_edgeMarkPill->isHidden())) {
                closePopovers();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace finily
