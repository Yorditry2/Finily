#include "ui/FloatingToolbar.h"
#include "ui/FloatingToolbarInternal.h"

#include "ui/CanvasWidget.h"
#include "ui/IconLibrary.h"
#include "ui/ToolbarIcons.h"

#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QEasingCurve>
#include <QEvent>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QScrollBar>
#include <functional>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QVariantAnimation>

#include <algorithm>
#include <numbers>

namespace finily {
namespace {

QString rgba(const QColor& color, int alpha = -1) {
    const int a = alpha < 0 ? color.alpha() : alpha;
    return QString("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(a);
}

// Las tintas por defecto de pizarra (claras) y de papel (oscuras) forman
// parejas: al pasar de pizarra a papel la tiza blanca se vuelve tinta negra,
// el amarillo se oscurece, etc. Así ningún color favorito queda invisible.
QColor adaptInkToTheme(const QColor& color, bool dark) {
    static const QVector<QPair<QString, QString>> pairs = {
        {"#f8fafc", "#0f172a"}, {"#fde047", "#ca8a04"}, {"#38bdf8", "#0284c7"}, {"#f87171", "#dc2626"},
        {"#4ade80", "#16a34a"}, {"#c084fc", "#9333ea"}, {"#fb923c", "#ea580c"},
    };
    const QString name = color.name();
    for (const auto& pair : pairs) {
        if (dark && name == pair.second)
            return QColor(pair.first);
        if (!dark && name == pair.first)
            return QColor(pair.second);
    }
    return color;
}

} // namespace

// Útil de escritura del estuche: un lápiz dibujado en vertical que asoma del
// borde de la bandeja. El seleccionado sube (animado) como si lo tomaras; el
// color de la banda/punta es la tinta actual. En el dock izquierdo se dibuja
// girado 90° (la punta mira a la derecha).
class PenTrayButton : public QToolButton {
public:
    enum class Kind { Fountain, Gel, Marker, Eraser };

    PenTrayButton(Kind kind, QWidget* parent)
        : QToolButton(parent), m_kind(kind) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(170);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_lift = v.toDouble();
            update();
        });
        connect(this, &QToolButton::toggled, this, [this] { animateTo(targetLift()); });
    }

    void setInk(const QColor& ink) {
        if (m_ink == ink)
            return;
        m_ink = ink;
        update();
    }
    void setDarkTheme(bool dark) {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }
    void setSideways(bool sideways) {
        if (m_sideways == sideways)
            return;
        m_sideways = sideways;
        update();
    }

protected:
    void enterEvent(QEnterEvent* e) override {
        m_hover = true;
        animateTo(targetLift());
        QToolButton::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        m_hover = false;
        animateTo(targetLift());
        QToolButton::leaveEvent(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Sistema local: lápiz vertical con la punta arriba; W x H "sin girar".
        qreal W = width(), H = height();
        if (m_sideways) {
            p.translate(width(), 0);
            p.rotate(90);
            std::swap(W, H);
        }
        const qreal cx = W / 2.0;
        const qreal top = 16.0 - m_lift; // punta en reposo a 16 px del borde

        const bool marker = m_kind == Kind::Marker;
        const bool eraser = m_kind == Kind::Eraser;
        const qreal bodyW = marker || eraser ? 20.0 : 15.0;
        const qreal tipH = m_kind == Kind::Fountain ? 17.0 : m_kind == Kind::Gel ? 14.0 : marker ? 9.0 : 0.0;
        const qreal bodyTop = top + tipH;
        const QRectF body(cx - bodyW / 2, bodyTop, bodyW, H - bodyTop + 8);

        // Sombra suave a la derecha.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, m_dark ? 70 : 28));
        p.drawRoundedRect(body.translated(2.0, 2.0), 4, 4);

        // Cuerpo cilíndrico (luz desde la izquierda).
        QColor shell = m_dark ? QColor("#e9e9ee") : QColor("#fbfbfd");
        if (eraser)
            shell = QColor("#f4f1ea");
        QLinearGradient cyl(body.left(), 0, body.right(), 0);
        cyl.setColorAt(0.0, shell.darker(112));
        cyl.setColorAt(0.28, shell.lighter(104));
        cyl.setColorAt(0.55, shell);
        cyl.setColorAt(1.0, shell.darker(122));
        p.setBrush(cyl);
        p.drawRoundedRect(body, eraser ? 6 : 3.5, eraser ? 6 : 3.5);

        auto shaded = [&](const QColor& c) {
            QLinearGradient g(body.left(), 0, body.right(), 0);
            g.setColorAt(0.0, c.darker(118));
            g.setColorAt(0.3, c.lighter(112));
            g.setColorAt(1.0, c.darker(135));
            return g;
        };

        if (eraser) {
            // Goma rosada arriba + funda de color.
            const QRectF rubber(body.left(), body.top() - 2, body.width(), 18);
            p.setBrush(shaded(QColor("#f3a1b0")));
            p.drawRoundedRect(rubber, 6, 6);
            p.setBrush(shaded(QColor("#5b7cfa")));
            p.drawRect(QRectF(body.left(), body.top() + 14, body.width(), H));
            p.setBrush(QColor(255, 255, 255, 120));
            p.drawRect(QRectF(body.left(), body.top() + 22, body.width(), 2));
            return;
        }

        // Banda de color de tinta en el cuerpo.
        QColor band = m_ink;
        if (marker)
            band.setAlpha(255);
        p.setBrush(shaded(band));
        // Contorno fino para que la tinta blanca/clara se distinga del cuerpo.
        p.setPen(QPen(QColor(0, 0, 0, 55), 0.8));
        p.drawRect(QRectF(body.left(), bodyTop + (marker ? 0 : 7), body.width(), marker ? 13 : 7));
        p.setPen(Qt::NoPen);

        QPainterPath tip;
        switch (m_kind) {
        case Kind::Fountain: {
            // Grip oscuro + plumín metálico con hendidura.
            p.setBrush(shaded(QColor("#3a3d46")));
            p.drawRect(QRectF(body.left(), bodyTop, body.width(), 7));
            tip.moveTo(cx - 6.5, bodyTop + 0.5);
            tip.cubicTo(cx - 6.5, bodyTop - 7, cx - 1.5, top + 4, cx, top);
            tip.cubicTo(cx + 1.5, top + 4, cx + 6.5, bodyTop - 7, cx + 6.5, bodyTop + 0.5);
            tip.closeSubpath();
            QLinearGradient metal(cx - 6.5, 0, cx + 6.5, 0);
            metal.setColorAt(0.0, QColor("#b8892f"));
            metal.setColorAt(0.4, QColor("#f3d27a"));
            metal.setColorAt(1.0, QColor("#9a6f22"));
            p.setBrush(metal);
            p.drawPath(tip);
            p.setPen(QPen(QColor(90, 60, 15, 200), 0.9));
            p.drawLine(QPointF(cx, top + 2), QPointF(cx, bodyTop - 5));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(90, 60, 15, 200));
            p.drawEllipse(QPointF(cx, bodyTop - 5), 1.3, 1.3);
            break;
        }
        case Kind::Gel: {
            tip.moveTo(cx - bodyW / 2 + 0.5, bodyTop + 0.5);
            tip.lineTo(cx - 1.6, top + 3);
            tip.lineTo(cx + 1.6, top + 3);
            tip.lineTo(cx + bodyW / 2 - 0.5, bodyTop + 0.5);
            tip.closeSubpath();
            QLinearGradient cone(cx - bodyW / 2, 0, cx + bodyW / 2, 0);
            cone.setColorAt(0.0, QColor("#a9adb8"));
            cone.setColorAt(0.35, QColor("#eef0f4"));
            cone.setColorAt(1.0, QColor("#8b909c"));
            p.setBrush(cone);
            p.drawPath(tip);
            p.setBrush(m_ink);
            p.drawEllipse(QPointF(cx, top + 2.5), 1.9, 2.4);
            break;
        }
        case Kind::Marker: {
            // Punta biselada translúcida.
            QColor nib = m_ink;
            nib.setAlpha(225);
            tip.moveTo(cx - 6, bodyTop + 0.5);
            tip.lineTo(cx - 6, top + 6);
            tip.lineTo(cx + 6, top);
            tip.lineTo(cx + 6, bodyTop + 0.5);
            tip.closeSubpath();
            p.setBrush(nib);
            p.drawPath(tip);
            p.setBrush(QColor(255, 255, 255, 70));
            p.drawRect(QRectF(cx - 6, top + 6, 2.5, bodyTop - top - 6));
            break;
        }
        case Kind::Eraser:
            break;
        }
    }

private:
    qreal targetLift() const { return isChecked() ? 11.0 : (m_hover ? 4.0 : 0.0); }
    void animateTo(qreal target) {
        m_anim->stop();
        m_anim->setStartValue(m_lift);
        m_anim->setEndValue(target);
        m_anim->start();
    }

    Kind m_kind;
    QColor m_ink{"#1d4ed8"};
    bool m_dark = false;
    bool m_sideways = false;
    bool m_hover = false;
    qreal m_lift = 0.0;
    QVariantAnimation* m_anim = nullptr;
};

FloatingToolbar::FloatingToolbar(CanvasWidget* canvas, QWidget* parent)
    : QWidget(parent), m_canvas(canvas) {
    setObjectName("finilyOverlay");
    // NO usar WA_TransparentForMouseEvents: en Qt6 se hereda a TODOS los hijos y
    // dejaría botones y sliders inciclables. La transparencia al lienzo se logra
    // con setMask() en updateOverlayMask() (solo los rectángulos de los hijos
    // visibles capturan eventos; el resto pasa al CanvasWidget).
    setFocusPolicy(Qt::NoFocus);

    initPresets();
    loadPresets();
    m_palette = getPaletteForTheme(true);
    loadDockPosition();

    if (m_canvas) {
        m_canvas->installEventFilter(this);
        connect(m_canvas, &CanvasWidget::stateChanged, this, &FloatingToolbar::refreshFromCanvas);
        connect(m_canvas, &CanvasWidget::viewportChanged, this, [this](double) { updateZoomLabel(); });
        connect(m_canvas, &CanvasWidget::toastRequested, this, &FloatingToolbar::showToast);
        connect(m_canvas, &CanvasWidget::selectionMenuRequested, this, &FloatingToolbar::showSelectionMenu);
        connect(m_canvas, &CanvasWidget::edgeMarkMenuRequested, this, &FloatingToolbar::showEdgeMarkPill);
    }

    buildUtilityPill();
    buildTopRight();
    buildDock();
    buildZoomPill();
    buildPagePill();
    buildRestoreButton();
    buildStrokePopover();
    buildPaperPopover();
    buildShapesPopover();
    buildStickersPopover();
    buildFilePopover();
    buildContextPill();
    buildEdgeMarkPill();
    buildThumbPanel();

    // Toast flotante: cápsula de cristal + etiqueta centrada. El fundido usa
    // una QGraphicsOpacityEffect sobre la cápsula (único efecto del widget).
    m_toastPill = new GlassCapsule(this);
    m_toastPill->setCapsuleRadius(16);
    auto* toastLayout = new QVBoxLayout(m_toastPill);
    toastLayout->setContentsMargins(16, 9, 16, 9);
    toastLayout->setSpacing(0);
    m_toastLabel = new QLabel(m_toastPill);
    m_toastLabel->setObjectName("finilyToast");
    m_toastLabel->setAlignment(Qt::AlignCenter);
    toastLayout->addWidget(m_toastLabel);
    m_toastPill->hide();
    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, this, [this] {
        if (!m_toastPill || !m_toastPill->isVisible())
            return;
        auto* opacity = qobject_cast<QGraphicsOpacityEffect*>(m_toastPill->graphicsEffect());
        if (!opacity)
            return;
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
            if (m_toastPill)
                m_toastPill->hide();
            if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(m_toastPill ? m_toastPill->graphicsEffect() : nullptr))
                effect->setOpacity(1.0);
            updateOverlayMask();
        });
        m_toastAnim->start();
    });

    // Cápsulas de cristal antialias (inmunes al estilo de sistema) + sombras
    // suaves reales (blur 22, yOffset 6). El registro m_capsules permite
    // recolorearlas todas al instante en applyThemeStylesheet().
    const ThemeConfig initialTheme = getThemeConfig(m_theme);
    const QVector<QPair<GlassCapsule*, int>> capsuleSpecs = {
        {m_pill, 22},          {m_contextPill, 16}, {m_edgeMarkPill, 16}, {m_zoomPill, 18},
        {m_pagePill, 18},      {m_toastPill, 16},   {m_strokePopover, 16},
        {m_paperPopover, 16},  {m_shapesPopover, 16}, {m_stickersPopover, 16}, {m_filePopover, 16},
        {m_dock, 20},          {m_topRight, 22},    {m_thumbPanel, 16},
    };
    for (const auto& capsule : capsuleSpecs) {
        if (!capsule.first)
            continue;
        capsule.first->setCapsuleRadius(capsule.second);
        capsule.first->setCapsuleTheme(initialTheme);
        m_capsules.append(capsule.first);
        applyShadow(capsule.first);
    }

    m_clearTimer = new QTimer(this);
    m_clearTimer->setSingleShot(true);
    connect(m_clearTimer, &QTimer::timeout, this, &FloatingToolbar::disarmClearButton);

    applyThemeStylesheet();

    if (m_canvas)
        setGeometry(m_canvas->rect());
    positionChildren();
    refreshFromCanvas();
}

void FloatingToolbar::initPresets() {
    m_presets.clear();
    m_presets.append(PenPreset{0, "Tiza principal", ToolType::Pen, QColor("#f8fafc"), 2.6, LinePattern::Solid});
    m_presets.append(PenPreset{1, "Amarillo", ToolType::Pen, QColor("#fde047"), 2.6, LinePattern::Solid});
    m_presets.append(PenPreset{2, "Celeste", ToolType::Pen, QColor("#38bdf8"), 2.6, LinePattern::Solid});
    m_presets.append(PenPreset{3, "Rojo", ToolType::Pen, QColor("#f87171"), 2.6, LinePattern::Solid});
    m_presets.append(PenPreset{4, "Resaltador", ToolType::Highlighter, QColor("#fde047"), 12.0, LinePattern::Solid});
    m_activePreset = 0;
    m_lastPresetsSig.clear();
}

// Restaura los 5 presets favoritos persistidos en QSettings("Finily", "Finily").
// Solo marca m_presetsRestored si existía al menos una clave guardada, para no
// desactivar innecesariamente la adaptación temática del preset inicial.
void FloatingToolbar::loadPresets() {
    QSettings settings("Finily", "Finily");
    const QString firstKey = QString("presets/%1/tool").arg(m_presets.first().index);
    if (!settings.contains(firstKey))
        return;
    for (int i = 0; i < m_presets.size(); ++i) {
        const QString prefix = QString("presets/%1/").arg(i);
        PenPreset& preset = m_presets[i];
        const int tool = settings.value(prefix + "tool", int(preset.tool)).toInt();
        if (tool >= int(ToolType::Pen) && tool <= int(ToolType::Hand))
            preset.tool = static_cast<ToolType>(tool);
        const QString colorName = settings.value(prefix + "color", preset.color.name(QColor::HexArgb)).toString();
        const QColor color(colorName);
        if (color.isValid())
            preset.color = color;
        const double size = settings.value(prefix + "size", preset.size).toDouble();
        if (size > 0.0 && size <= 50.0)
            preset.size = size;
        const int pattern = settings.value(prefix + "linePattern", int(preset.linePattern)).toInt();
        if (pattern >= int(LinePattern::Solid) && pattern <= int(LinePattern::Dimension))
            preset.linePattern = static_cast<LinePattern>(pattern);
    }
    m_presetsRestored = true;
}

// Persiste los 5 presets favoritos (herramienta, color, grosor y tipo de línea).
void FloatingToolbar::savePresets() {
    QSettings settings("Finily", "Finily");
    for (int i = 0; i < m_presets.size(); ++i) {
        const QString prefix = QString("presets/%1/").arg(i);
        const PenPreset& preset = m_presets.at(i);
        settings.setValue(prefix + "tool", int(preset.tool));
        settings.setValue(prefix + "color", preset.color.name(QColor::HexArgb));
        settings.setValue(prefix + "size", preset.size);
        settings.setValue(prefix + "linePattern", int(preset.linePattern));
    }
    settings.sync();
}

// Pill compacta de utilidades (arriba-izquierda, x=14 y=12): Inicio | Deshacer
// y Rehacer | Trazo, Papel, Archivo | modo interfaz limpia. Las herramientas
// de dibujo viven en el dock de papelería (buildDock) para dejar el cromo
// superior despejado.
void FloatingToolbar::buildUtilityPill() {
    m_pill = new GlassCapsule(this);
    m_pill->setObjectName("finilyPill");
    auto* row = new QHBoxLayout(m_pill);
    row->setContentsMargins(10, 7, 10, 7);
    row->setSpacing(3);

    // 'Inicio' (biblioteca): primer botón a la izquierda, separado por un
    // divisor fino antes de Deshacer/Rehacer.
    m_homeBtn = new QToolButton(m_pill);
    m_homeBtn->setFixedSize(36, 36);
    m_homeBtn->setIconSize(QSize(22, 22));
    m_homeBtn->setToolTip("Inicio");
    m_homeBtn->setStatusTip(m_homeBtn->toolTip());
    polishButton(m_homeBtn);
    connect(m_homeBtn, &QToolButton::clicked, this, &FloatingToolbar::homeRequested);
    m_studioUtilSpecs.append(qMakePair(m_homeBtn, QStringLiteral("home")));
    row->addWidget(m_homeBtn);

    m_titleDot = new QLabel(m_pill);
    m_titleDot->setFixedSize(6, 6);
    row->addSpacing(4);
    row->addWidget(m_titleDot, 0, Qt::AlignVCenter);
    m_titleLabel = new QLabel("Sin título", m_pill);
    m_titleLabel->setMaximumWidth(200);
    m_titleLabel->setCursor(Qt::PointingHandCursor);
    m_titleLabel->setToolTip("Haz clic para renombrar el cuaderno");
    m_titleLabel->installEventFilter(this);
    row->addWidget(m_titleLabel);
    m_titleEdit = new QLineEdit(m_pill);
    m_titleEdit->setMaximumWidth(180);
    m_titleEdit->setFixedHeight(26);
    m_titleEdit->hide();
    connect(m_titleEdit, &QLineEdit::editingFinished, this, [this] {
        if (!m_titleEdit || !m_titleEdit->isVisible())
            return;
        const QString next = m_titleEdit->text().trimmed();
        m_titleEdit->hide();
        if (m_titleLabel)
            m_titleLabel->show();
        positionChildren();
        if (!next.isEmpty() && (!m_titleLabel || next != m_titleLabel->text()))
            emit renameRequested(next);
    });
    row->addWidget(m_titleEdit);
    row->addSpacing(4);

    row->addWidget(makeSeparator(m_pill));

    // Deshacer / Rehacer
    m_undoBtn = new QToolButton(m_pill);
    m_undoBtn->setFixedSize(36, 36);
    m_undoBtn->setIconSize(QSize(22, 22));
    m_undoBtn->setToolTip("Deshacer");
    polishButton(m_undoBtn);
    connect(m_undoBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->undo();
        refreshFromCanvas();
    });
    m_studioUtilSpecs.append(qMakePair(m_undoBtn, QStringLiteral("undo")));
    row->addWidget(m_undoBtn);

    m_redoBtn = new QToolButton(m_pill);
    m_redoBtn->setFixedSize(36, 36);
    m_redoBtn->setIconSize(QSize(22, 22));
    m_redoBtn->setToolTip("Rehacer");
    polishButton(m_redoBtn);
    connect(m_redoBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->redo();
        refreshFromCanvas();
    });
    m_studioUtilSpecs.append(qMakePair(m_redoBtn, QStringLiteral("redo")));
    row->addWidget(m_redoBtn);

    // Modo Ecuación (atajo M): botón oculto, la lógica sigue en refreshFromCanvas.
    m_mathModeBtn = new QToolButton(m_pill);
    m_mathModeBtn->setCheckable(true);
    m_mathModeBtn->hide();
    connect(m_mathModeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_mathModeBtn)
            m_canvas->setMathBeautifyEnabled(m_mathModeBtn->isChecked());
        refreshFromCanvas();
    });
}

// Cápsula superior derecha: Presentar (interfaz limpia), Páginas y menú ⋯.
void FloatingToolbar::buildTopRight() {
    m_topRight = new GlassCapsule(this);
    m_topRight->setObjectName("finilyPill");
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
    m_cleanBtn = mk("Presentar · oculta la interfaz (Tab)", "present");
    connect(m_cleanBtn, &QToolButton::clicked, this, &FloatingToolbar::toggleCleanUi);
    m_paperBtn = mk("Papel y plantilla", "paper");
    connect(m_paperBtn, &QToolButton::clicked, this, [this] { openPopover(m_paperPopover, m_paperBtn); });
    m_pagesBtn = mk("Páginas", "pages");
    m_pagesBtn->setCheckable(true);
    connect(m_pagesBtn, &QToolButton::clicked, this, [this] {
        closePopovers();
        if (m_thumbPanel) {
            m_thumbPanel->setVisible(m_pagesBtn->isChecked());
            if (m_pagesBtn->isChecked())
                rebuildThumbs();
        }
        positionChildren();
    });
    m_fileBtn = mk("Menú", "more");
    connect(m_fileBtn, &QToolButton::clicked, this, [this] { openPopover(m_filePopover, m_fileBtn); });
}

void FloatingToolbar::setDocumentTitle(const QString& title, bool unsaved) {
    if (!m_titleLabel)
        return;
    m_titleLabel->setText(title);
    m_titleLabel->setToolTip(title);
    const ThemeConfig theme = getThemeConfig(m_theme);
    m_titleDot->setStyleSheet(QString("background:%1;border-radius:3px;")
                                  .arg(unsaved ? QStringLiteral("#f5b84b") : theme.accent.name()));
    m_titleDot->setToolTip(unsaved ? "Cambios sin guardar" : "Guardado");
    positionChildren();
}

// Dock de herramientas 100% homologado (10 herramientas exclusivas, cero
// duplicados): arriba los 4 útiles ilustrados SVG (Pluma fuente, Bolígrafo gel,
// Resaltador, Borrador en bloque) con color de tinta dinámico y doble toque
// inteligente para abrir "Trazo"; divisor fino; abajo las 6 herramientas
// duotono SVG en la misma cuadrícula óptica 32x32 (Figuras, Polilínea, Lazo,
// Texto, Láser, Mano).
void FloatingToolbar::buildDock() {
    m_dock = new GlassCapsule(this);
    m_dock->setObjectName("finilyDock");
    m_dock->setCapsuleRadius(20);

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    // Primer clic para activar, segundo clic para abrir los ajustes.
    const QVector<QPair<ToolType, QPair<QString, QString>>> writingTools = {
        {ToolType::Pen, qMakePair(QStringLiteral("Pluma (P)"), QStringLiteral("pen"))},
        {ToolType::Highlighter, qMakePair(QStringLiteral("Resaltador (H)"), QStringLiteral("highlighter"))},
        {ToolType::Eraser, qMakePair(QStringLiteral("Borrador (E)"), QStringLiteral("eraser"))},
        {ToolType::Fill, qMakePair(tr("Relleno (F). Toca dentro de una figura cerrada. Segundo clic para elegir el estilo."),
                                   QStringLiteral("paint-bucket"))},
    };
    for (const auto& entry : writingTools) {
        auto* button = new QToolButton(m_dock);
        button->setObjectName("finilyTool");
        button->setFixedSize(42, 40);
        button->setIconSize(QSize(24, 24));
        button->setCheckable(true);
        polishButton(button);
        m_iconSpecs.append(qMakePair(static_cast<QToolButton*>(button), entry.second.second));
        button->setToolTip(entry.second.first);
        m_toolGroup->addButton(button);
        const ToolType tool = entry.first;
        connect(button, &QToolButton::clicked, this, [this, tool, button] {
            if (!m_canvas)
                return;
            const ToolType active = m_canvas->tool();
            const bool sameTool = (active == tool) || (tool == ToolType::Pen && active == ToolType::Ballpoint);
            if (sameTool) {
                // Segundo clic sobre la herramienta ya activa: abrir/cerrar "Trazo"
                if (m_openPopover == m_strokePopover)
                    closePopovers();
                else
                    openPopover(m_strokePopover, button);
            } else {
                closePopovers();
                selectTool(tool == ToolType::Pen && m_canvas->penTip() == 0 ? ToolType::Ballpoint : tool);
            }
        });
        m_writingKinds.append(tool);
        m_writingBtns.append(button);
    }

    m_dockDivider = new QWidget(m_dock);
    m_dockDivider->setObjectName("finilySep");

    // 6 herramientas duotono SVG bajo el divisor.
    const QVector<QPair<ToolType, QPair<QString, QString>>> secondary = {
        {ToolType::Select, qMakePair(tr("Seleccionar (U). Toca para seleccionar, arrastra para mover. Círculos: vértices. Cuadrados: tamaño."),
                                     QStringLiteral("select-pointer"))},
        {ToolType::Shape, qMakePair(tr("Figuras (S). Arrastra para dibujar. Mayús + arrastrar para editar."), QStringLiteral("shapes"))},
        {ToolType::Lasso, qMakePair(QStringLiteral("Lazo (A)"), QStringLiteral("lasso"))},
        {ToolType::Text, qMakePair(QStringLiteral("Texto (T)"), QStringLiteral("text"))},
        {ToolType::Laser, qMakePair(QStringLiteral("Láser (K)"), QStringLiteral("laser"))},
    };
    for (const auto& entry : secondary) {
        auto* button = new QToolButton(m_dock);
        button->setObjectName("finilyTool");
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
                if (!m_canvas)
                    return;
                if (m_canvas->tool() == ToolType::Shape) {
                    if (m_openPopover == m_shapesPopover)
                        closePopovers();
                    else
                        openPopover(m_shapesPopover, m_shapeBtn);
                } else {
                    closePopovers();
                    selectTool(ToolType::Shape);
                    openPopover(m_shapesPopover, m_shapeBtn);
                }
            });
        } else if (tool == ToolType::Lasso) {
            connect(button, &QToolButton::clicked, this, [this, button] {
                if (m_canvas && m_canvas->tool() == ToolType::Lasso) {
                    if (m_openPopover == m_strokePopover)
                        closePopovers();
                    else
                        openPopover(m_strokePopover, button);
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
            m_stickersBtn->setObjectName("finilyTool");
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

    // 4 tintas rápidas del estuche: 1 toque cambia el color sin abrir menús.
    m_trayDivider = new QWidget(m_dock);
    m_trayDivider->setObjectName("finilySep");
    for (int i = 0; i < 4; ++i) {
        auto* dot = new ColorDotButton(m_dock);
        dot->setFixedSize(26, 26);
        dot->setToolTip("Cambiar color rápido");
        polishButton(dot);
        connect(dot, &QToolButton::clicked, this, [this, i] {
            if (i < m_palette.size())
                setColor(m_palette.at(i));
            if (m_canvas && (m_canvas->tool() == ToolType::Eraser || m_canvas->tool() == ToolType::Hand
                             || m_canvas->tool() == ToolType::Lasso || m_canvas->tool() == ToolType::Laser))
                selectTool(ToolType::Pen);
        });
        m_trayDots.append(dot);
    }
    auto* colorBtn = new ColorDotButton(m_dock);
    m_trayColorBtn = colorBtn;
    m_trayMoreBtn = colorBtn;
    colorBtn->setCheckable(false);
    colorBtn->setFixedSize(38, 38);
    colorBtn->hide();
}

// Coloca los hijos del dock según la orientación (vertical a la izquierda,
// horizontal arriba/abajo) y fija el tamaño de la cápsula.
void FloatingToolbar::layoutDock() {
    if (!m_dock)
        return;
    const bool vertical = m_dockPos == DockPos::Izquierda;
    const int pad = 10;
    const int toolSize = 40;
    const int dotSize = 26;
    const int crossLen = 56;
    const int along = pad + m_writingBtns.size() * (toolSize + 2) + 12
                      + m_secondaryBtns.size() * (toolSize + 2) + 12
                      + m_trayDots.size() * (dotSize + 4) + pad;
    if (vertical)
        m_dock->setFixedSize(crossLen, along);
    else
        m_dock->setFixedSize(along, crossLen);
    m_dock->setCapsuleRadius(24);

    auto place = [&](QWidget* w, int pos, int len, int cross) {
        const int off = (crossLen - cross) / 2;
        if (vertical)
            w->setGeometry(off, pos, cross, len);
        else
            w->setGeometry(pos, off, len, cross);
        w->raise();
    };

    int pos = pad;
    for (QToolButton* button : m_writingBtns) {
        place(button, pos, toolSize, toolSize);
        pos += toolSize + 2;
    }
    pos += 4;
    if (vertical)
        m_dockDivider->setGeometry(22, pos, crossLen - 44, 1);
    else
        m_dockDivider->setGeometry(pos, 22, 1, crossLen - 44);
    pos += 8;
    for (QToolButton* button : m_secondaryBtns) {
        place(button, pos, toolSize, toolSize);
        pos += toolSize + 2;
    }
    pos += 4;
    if (m_trayDivider) {
        if (vertical)
            m_trayDivider->setGeometry(20, pos, crossLen - 40, 1);
        else
            m_trayDivider->setGeometry(pos, 20, 1, crossLen - 40);
    }
    pos += 8;
    for (ColorDotButton* dot : m_trayDots) {
        place(dot, pos, dotSize, dotSize);
        pos += dotSize + 4;
    }
    m_dock->update();
}

void FloatingToolbar::buildZoomPill() {
    m_zoomPill = new GlassCapsule(this);
    m_zoomPill->setObjectName("finilyZoom");
    auto* row = new QHBoxLayout(m_zoomPill);
    row->setContentsMargins(8, 5, 8, 5);
    row->setSpacing(2);

    m_zoomOutBtn = new QToolButton(m_zoomPill);
    m_zoomOutBtn->setFixedSize(30, 30);
    m_zoomOutBtn->setIconSize(QSize(16, 16));
    m_zoomOutBtn->setToolTip("Alejar");
    polishButton(m_zoomOutBtn);
    connect(m_zoomOutBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->zoomOut();
        updateZoomLabel();
    });
    m_iconSpecs.append(qMakePair(m_zoomOutBtn, QString("zoom-out")));
    row->addWidget(m_zoomOutBtn);

    m_zoomResetBtn = new QToolButton(m_zoomPill);
    m_zoomResetBtn->setObjectName("finilyZoomReset");
    m_zoomResetBtn->setFixedHeight(30);
    m_zoomResetBtn->setText("100 %");
    m_zoomResetBtn->setToolTip(tr("Ajustar página o restablecer zoom (Ctrl+0)"));
    polishButton(m_zoomResetBtn);
    connect(m_zoomResetBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->resetViewport();
        updateZoomLabel();
    });
    row->addWidget(m_zoomResetBtn);

    m_zoomInBtn = new QToolButton(m_zoomPill);
    m_zoomInBtn->setFixedSize(30, 30);
    m_zoomInBtn->setIconSize(QSize(16, 16));
    m_zoomInBtn->setToolTip("Acercar");
    polishButton(m_zoomInBtn);
    connect(m_zoomInBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->zoomIn();
        updateZoomLabel();
    });
    m_iconSpecs.append(qMakePair(m_zoomInBtn, QString("zoom-in")));
    row->addWidget(m_zoomInBtn);
}

void FloatingToolbar::buildPagePill() {
    // Pill de páginas en la esquina inferior izquierda, espejo del estilo de la
    // pill de zoom. Hijo normal del overlay (máscara Hyprland-safe en
    // updateOverlayMask()); todos los controles con Qt::NoFocus para el lápiz.
    m_pagePill = new GlassCapsule(this);
    m_pagePill->setObjectName("finilyPagePill");
    auto* row = new QHBoxLayout(m_pagePill);
    row->setContentsMargins(8, 5, 8, 5);
    row->setSpacing(2);

    m_pagePrevBtn = new QToolButton(m_pagePill);
    m_pagePrevBtn->setFixedSize(30, 30);
    m_pagePrevBtn->setIconSize(QSize(16, 16));
    m_iconSpecs.append(qMakePair(m_pagePrevBtn, QString("chevron-left")));
    m_pagePrevBtn->setToolTip("Página anterior (Re Pág)");
    polishButton(m_pagePrevBtn);
    connect(m_pagePrevBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->prevPage();
        refreshFromCanvas();
    });
    row->addWidget(m_pagePrevBtn);

    m_pageLabel = new QLabel("1 / 1", m_pagePill);
    m_pageLabel->setObjectName("finilyPageLabel");
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setContentsMargins(4, 0, 4, 0);
    m_pageLabel->setFixedHeight(30);
    row->addWidget(m_pageLabel);

    m_pageNextBtn = new QToolButton(m_pagePill);
    m_pageNextBtn->setFixedSize(30, 30);
    m_pageNextBtn->setIconSize(QSize(16, 16));
    m_iconSpecs.append(qMakePair(m_pageNextBtn, QString("chevron-right")));
    m_pageNextBtn->setToolTip("Página siguiente (Av Pág)");
    polishButton(m_pageNextBtn);
    connect(m_pageNextBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->nextPage();
        refreshFromCanvas();
    });
    row->addWidget(m_pageNextBtn);

    row->addWidget(makeSeparator(m_pagePill));

    m_pageAddBtn = new QToolButton(m_pagePill);
    m_pageAddBtn->setFixedSize(30, 30);
    m_pageAddBtn->setIconSize(QSize(16, 16));
    m_iconSpecs.append(qMakePair(m_pageAddBtn, QString("plus")));
    m_pageAddBtn->setToolTip("Añadir página nueva (Ctrl+Mayús+N)");
    polishButton(m_pageAddBtn);
    connect(m_pageAddBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->addPage();
        refreshFromCanvas();
    });
    row->addWidget(m_pageAddBtn);
}

void FloatingToolbar::buildRestoreButton() {
    m_restoreBtn = new QToolButton(this);
    m_restoreBtn->setObjectName("finilyRestore");
    m_restoreBtn->setFixedSize(26, 26);
    m_restoreBtn->setIconSize(QSize(14, 14));
    m_restoreBtn->setToolTip("Mostrar interfaz (Tab)");
    polishButton(m_restoreBtn);
    connect(m_restoreBtn, &QToolButton::clicked, this, &FloatingToolbar::toggleCleanUi);
    m_iconSpecs.append(qMakePair(m_restoreBtn, QString("restore")));
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
/* Las superficies (pills y popovers) son GlassCapsule pintadas a mano: sin
   reglas QSS de fondo para que el estilo de sistema (Caelestia/KDE) jamas
   las sobreescriba con cuadrados gris carbon. */
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
QLabel#finilyTitle {
    color: %4;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1px;
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
QToolButton#finilyMoreToggle { color: %4; font-size: 11px; font-weight: 600; text-align: left; border-radius: 8px; padding: 0 4px; }
QToolButton#finilyMoreToggle:hover { color: %3; background: transparent; }
QToolButton:hover:checked, QPushButton:hover:checked { background: %13; }
QToolButton:disabled { color: %4; }
/* Herramientas del dock (útiles ilustrados + duotono): cápsula activa refinada
   con tinte suave de acento y borde fino para no tapar jamás el arte SVG. */
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
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0px; border-radius: 7px; background: %9; }
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

    // La primera tinta se adapta automáticamente al tema: tiza (oscuro) / tinta
    // (claro). Si el usuario restauró presets personalizados desde QSettings,
    // se respeta su color guardado en lugar de sobrescribirlo.
    if (!m_presetsRestored) {
        m_presets[0].color = getPaletteForTheme(theme.isDark).first();
        m_presets[0].label = theme.isDark ? "Tiza principal" : "Tinta principal";
    }
    for (PenPreset& preset : m_presets)
        if (preset.tool != ToolType::Highlighter)
            preset.color = adaptInkToTheme(preset.color, theme.isDark);

    const QString qss = buildStylesheet(theme);
    setStyleSheet(qss);
    if (m_strokePopover)
        m_strokePopover->setStyleSheet(qss);
    if (m_paperPopover)
        m_paperPopover->setStyleSheet(qss);
    if (m_shapesPopover)
        m_shapesPopover->setStyleSheet(qss);
    if (m_stickersPopover)
        m_stickersPopover->setStyleSheet(qss);
    if (m_filePopover)
        m_filePopover->setStyleSheet(qss);

    refreshIcons();
    updateStationery();

    // Recolorea TODAS las cápsulas de cristal al instante: DarkBoard <-> 
    // PaperLight cambia cada superficie de vidrio oscuro a blanco luminoso.
    // Cada cápsula recibe la hoja de estilo directamente: si solo se pone en
    // la barra, los hijos ya pulidos se quedan con el color del tema anterior
    // (texto blanco sobre papel blanco).
    for (GlassCapsule* capsule : m_capsules)
        if (capsule) {
            capsule->setCapsuleTheme(theme);
            capsule->setStyleSheet(qss);
        }

    refreshShadowColors();
    if (m_stickersPopover)
        rebuildStickersGrid();
    disarmClearButton();
}

void FloatingToolbar::refreshIcons() {
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor ink = m_canvas ? m_canvas->inkColor() : theme.accent;

    // 1. Barra superior de utilidades (SVG homologado 22px)
    for (const auto& spec : m_studioUtilSpecs)
        spec.first->setIcon(makeStudioIcon(spec.second, 22, theme.uiText, theme.accent, ink, theme.isDark));

    // 2. 4 útiles ilustrados del dock (SVG 32px con tinta activa)

    for (QToolButton* b : findChildren<QToolButton*>("finilySwitch"))
        if (auto* sw = dynamic_cast<SwitchButton*>(b))
            sw->setColors(theme.uiText, theme.accent, theme.isDark);
    if (m_titleLabel)
        m_titleLabel->setStyleSheet(QString("color:%1;font-size:13px;font-weight:600;background:transparent;")
                                        .arg(theme.uiText.name()));
    if (m_titleDot)
        m_titleDot->setStyleSheet(QString("background:%1;border-radius:3px;").arg(theme.accent.name()));

    // 3. 6 herramientas duotono del dock (SVG 24px)
    for (int i = 0; i < m_secondaryBtns.size() && i < m_secondaryIcons.size(); ++i)
        m_secondaryBtns.at(i)->setIcon(dockIcon(m_secondaryIcons.at(i), theme.uiText, theme.accent));

    m_lastInkIconSig = QString("%1|%2").arg(int(m_theme)).arg(ink.name(QColor::HexArgb));

    // 4. Mini-iconos auxiliares (zoom, páginas, popovers de figuras/patrones/plantillas)
    static const QStringList dockKinds = {"pen", "highlighter", "eraser", "paint-bucket"};
    for (const auto& spec : m_iconSpecs)
        spec.first->setIcon(dockKinds.contains(spec.second) ? dockIcon(spec.second, theme.uiText, theme.accent)
                                                            : glyphIcon(spec.second, theme.uiText));
    for (const auto& spec : m_ctxIconSpecs)
        spec.first->setIcon(QIcon(contextIconPixmap(spec.second, theme.uiText)));
    if (m_stabilizerIcon)
        m_stabilizerIcon->setPixmap(contextIconPixmap(QStringLiteral("stabilizer"), theme.uiText));
    if (m_hlOpacityIcon)
        m_hlOpacityIcon->setPixmap(contextIconPixmap(QStringLiteral("opacity"), theme.uiText));
    for (QSlider* sl : {m_sizeSlider, m_stabilizerSlider, m_eraserSizeSlider, m_pressureSlider, m_hlOpacitySlider})
        if (auto* ds = dynamic_cast<DotStepSlider*>(sl))
            ds->setColors(theme.accent, theme.isDark);
    for (const auto& spec : m_patternSpecs)
        spec.first->setIcon(QIcon(patternPixmap(spec.second, theme.uiText)));
    for (const auto& spec : m_templateSpecs)
        spec.first->setIcon(QIcon(templatePixmap(spec.second, theme.uiText)));
    updateFillSwatches();
    updateTipIcons();
}

void FloatingToolbar::updateFillSwatches() {
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor col = m_canvas ? m_canvas->inkColor() : theme.accent;
    for (const auto& [button, pattern] : m_fillStyles)
        button->setIcon(QIcon(fillSwatchPixmap(pattern, col, theme.isDark)));
}

void FloatingToolbar::updateTipIcons() {
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor col = m_canvas ? m_canvas->inkColor() : theme.accent;
    for (int i = 0; i < m_penTipBtns.size(); ++i)
        m_penTipBtns.at(i)->setIcon(QIcon(penTipPixmap(i, col, theme.isDark)));
    for (int i = 0; i < m_hlTipBtns.size(); ++i)
        m_hlTipBtns.at(i)->setIcon(QIcon(hlTipPixmap(i, col, theme.isDark)));
}

void FloatingToolbar::positionChildren() {
    // Pill de utilidades: siempre arriba-izquierda (x=14, y=12).
    if (m_pill && !m_pill->isHidden()) {
        m_pill->adjustSize();
        m_pill->move(14, 12);
    }
    if (m_topRight && !m_topRight->isHidden()) {
        m_topRight->adjustSize();
        m_topRight->move(width() - m_topRight->width() - 14, 12);
    }
    if (m_thumbPanel && !m_thumbPanel->isHidden()) {
        const int top = m_topRight ? m_topRight->geometry().bottom() + 10 : 60;
        const int bottom = height() - (m_zoomPill ? m_zoomPill->height() : 40) - 28;
        m_thumbPanel->setGeometry(width() - m_thumbPanel->width() - 14, top, m_thumbPanel->width(),
                                  std::max(160, bottom - top));
    }
    if (m_zoomPill && !m_zoomPill->isHidden()) {
        m_zoomPill->adjustSize();
        m_zoomPill->move(width() - m_zoomPill->width() - 14, height() - m_zoomPill->height() - 14);
    }
    if (m_pagePill && !m_pagePill->isHidden()) {
        m_pagePill->adjustSize();
        const int zx = (m_zoomPill && !m_zoomPill->isHidden()) ? m_zoomPill->x() - 8 : width() - 14;
        m_pagePill->move(zx - m_pagePill->width(), height() - m_pagePill->height() - 14);
    }
    // Dock de papelería: vertical centrado en el borde izquierdo (x=12) o
    // horizontal arriba/abajo, esquivando las pills de esquinas.
    if (m_dock && !m_dock->isHidden()) {
        layoutDock();
        if (m_dockPos == DockPos::Izquierda) {
            m_dock->move(12, std::max(12, (height() - m_dock->height()) / 2));
        } else if (m_dockPos == DockPos::Arriba) {
            const int centeredX = (width() - m_dock->width()) / 2;
            const int minX = m_pill && !m_pill->isHidden() ? m_pill->geometry().right() + 14 : 14;
            m_dock->move(std::max(minX, centeredX), 12);
        } else {
            int x = (width() - m_dock->width()) / 2;
            if (m_pagePill && !m_pagePill->isHidden())
                x = std::min(x, m_pagePill->geometry().left() - m_dock->width() - 14);
            m_dock->move(std::max(12, x), height() - m_dock->height() - 12);
        }
    }
    if (m_restoreBtn && !m_restoreBtn->isHidden())
        m_restoreBtn->move(width() - 34, 10);
    if (m_contextPill && !m_contextPill->isHidden()) {
        m_contextPill->adjustSize();
        // Fija abajo-centro: nunca tapa vértices ni se atraviesa al dibujar o editar.
        const int x = std::max(76, (width() - m_contextPill->width()) / 2);
        const int y = std::max(12, height() - m_contextPill->height() - 16);
        m_contextPill->move(x, y);
    }
    if (m_toastPill && !m_toastPill->isHidden()) {
        m_toastPill->adjustSize();
        // Dock abajo: toast arriba-centrado; en otro caso, abajo del todo
        // (el dock izquierdo queda centrado y no tropieza con el toast).
        const int y = m_dockPos == DockPos::Abajo ? 18 : height() - m_toastPill->height() - 64;
        m_toastPill->move(std::max(6, (width() - m_toastPill->width()) / 2), y);
    }
    positionOpenPopover();
    updateOverlayMask();
}

// Máscara de entrada del overlay: únicamente los rectángulos de los hijos
// visibles capturan eventos (pill, pill de zoom, botón de restauración y
// popovers abiertos); todo lo demás pasa al CanvasWidget debajo, cuyo
// eventFilter cierra cualquier popover abierto.
void FloatingToolbar::updateOverlayMask() {
    QRegion region;
    const QVector<QWidget*> overlayChildren = {m_pill,          m_topRight,     m_thumbPanel,   m_contextPill, m_edgeMarkPill, m_zoomPill,   m_restoreBtn,
                                               m_strokePopover, m_paperPopover, m_shapesPopover, m_stickersPopover, m_filePopover,
                                               m_pagePill,      m_toastPill,    m_dock};
    for (QWidget* child : overlayChildren) {
        if (!child || child->isHidden())
            continue;
        // Margen estrecho (-8,-6,+8,+12): deja respirar la sombra suave sin
        // robar clics de dibujo lejos del cromo (el hit-test queda pegado a la
        // pill; solo el halo de la sombra se expande).
        region += child->geometry().adjusted(-8, -6, 8, 12);
    }
    setMask(region);
}

// Con lápiz Wacom, los QTabletEvent no manejados por los hijos (botones,
// sliders) suben hasta aquí: al ignorarlos, siguen hacia el contenedor plano
// de MainWindow (nunca CanvasWidget, que es hermano) y quedan sin manejar,
// por lo que Qt sintetiza eventos de mouse nítidos para los controles.
void FloatingToolbar::tabletEvent(QTabletEvent* event) {
    event->ignore();
}

QPoint FloatingToolbar::popoverPosition(const GlassCapsule* popover, const QWidget* anchor) const {
    const int margin = 12;
    // Los popovers abren LIMPIOS junto al cromo: a la derecha del dock
    // vertical (bajo la pill de utilidades), debajo del dock acoplado arriba,
    // o bajo la pill en cualquier otro caso; nunca tapados ni recortados.
    int x, y;
    if (m_topRight && anchor->parentWidget() == m_topRight) {
        x = m_topRight->geometry().right() - popover->width();
        y = m_topRight->geometry().bottom() + 10;
    } else if (m_dockPos == DockPos::Izquierda && m_dock && !m_dock->isHidden()) {
        x = m_dock->geometry().right() + 10;
        if (anchor->parentWidget() == m_dock) {
            // Al abrir desde un útil o Figuras del dock, alinear verticalmente junto a ese botón
            y = m_dock->y() + anchor->y() - 12;
        } else {
            y = (m_pill && !m_pill->isHidden() ? m_pill->geometry().bottom() : 40) + 10;
        }
    } else if (m_dockPos == DockPos::Arriba && m_dock && !m_dock->isHidden()) {
        x = margin;
        y = m_dock->geometry().bottom() + 10;
    } else {
        x = margin;
        y = (m_pill && !m_pill->isHidden() ? m_pill->geometry().bottom() : 40) + 10;
    }
    if (m_dockPos == DockPos::Abajo && m_dock && !m_dock->isHidden())
        // Con el dock abajo, no invadir el dock: ciñe el popover por encima.
        y = std::min(y, std::max(margin, m_dock->geometry().top() - popover->height() - 10));
    x = std::clamp(x, margin, std::max(margin, width() - popover->width() - margin));
    y = std::clamp(y, margin, std::max(margin, height() - popover->height() - margin));
    return QPoint(x, y);
}

void FloatingToolbar::positionOpenPopover() {
    if (!m_openPopover)
        return;
    m_openPopover->adjustSize();
    const QPoint target = popoverPosition(m_openPopover, m_popoverAnchor);
    if (m_popoverAnim && m_popoverAnim->state() == QAbstractAnimation::Running) {
        if (m_popoverAnim->endValue().toPoint() == target)
            return;
        m_popoverAnim->stop();
    }
    m_openPopover->move(target);
}

// Abre el panel junto a su botón en las coordenadas del overlay.
void FloatingToolbar::openPopover(GlassCapsule* popover, QWidget* anchor) {
    if (!popover || !anchor)
        return;
    const bool wasOpen = m_openPopover == popover;
    closePopovers();
    if (wasOpen)
        return;
    m_openPopover = popover;
    m_popoverAnchor = anchor;

    // Al mostrar el panel, Qt vuelve a medir las secciones que cambiaron mientras estaba oculto.
    popover->show();
    popover->adjustSize();
    const QPoint finalPos = popoverPosition(popover, anchor);
    // Desliza 8 px hacia su lugar final (desde el cromo que lo ancla).
    QPoint offset(0, 8);
    if ((m_topRight && anchor->parentWidget() == m_topRight) || m_dockPos == DockPos::Abajo)
        offset = QPoint(0, -8);
    else if (m_dockPos == DockPos::Izquierda)
        offset = QPoint(-8, 0);
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
    m_edgeMarkPill->setObjectName("finilyContextPill");
    auto* row = new QHBoxLayout(m_edgeMarkPill);
    row->setContentsMargins(6, 5, 6, 5);
    row->setSpacing(3);

    struct EdgeMarkDef {
        QString glyph;
        QString mark;
        QString tip;
    };
    const QVector<EdgeMarkDef> defs = {
        {"mark-zigzag",   "mark:zigzag",    tr("Serrucho (zig-zag)")},
        {"mark-spring",   "mark:spring",    tr("Resorte (ondas)")},
        {"mark-dot",      "mark:dot",       tr("Bolita / punto")},
        {"mark-bar",      "mark:bar",       tr("Bultito / rectángulo")},
        {"mark-ticks",    "mark:tick1",     tr("Rayitas de congruencia (cicla 1–3)")},
        {"mark-parallel", "mark:parallel1", tr("Flechas paralelas (cicla 1–2)")},
    };
    for (const auto& def : defs) {
        auto* btn = new QToolButton(m_edgeMarkPill);
        btn->setObjectName("finilyShapeBtn");
        btn->setFixedSize(36, 34);
        btn->setIconSize(QSize(19, 19));
        btn->setToolTip(def.tip);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, mark = def.mark] {
            if (m_canvas)
                m_canvas->stampCongruenceMarkAt(m_edgeMarkWorldPos, mark);
            if (m_edgeMarkPill)
                m_edgeMarkPill->hide();
            updateOverlayMask();
        });
        m_iconSpecs.append(qMakePair(btn, def.glyph));
        row->addWidget(btn);
    }
    m_edgeMarkPill->hide();
}

void FloatingToolbar::showEdgeMarkPill(const QPointF& screenPos, const QPointF& worldPos) {
    if (!m_edgeMarkPill)
        return;
    closePopovers();
    m_edgeMarkWorldPos = worldPos;
    m_edgeMarkPill->adjustSize();
    const int margin = 12;
    const int w = m_edgeMarkPill->width();
    const int h = m_edgeMarkPill->height();
    int x = std::clamp(qRound(screenPos.x()) - w / 2, margin, std::max(margin, width() - w - margin));
    int y = qRound(screenPos.y()) - h - 18;
    if (y < margin)
        y = std::min(height() - h - margin, qRound(screenPos.y()) + 18);
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
    if (changed)
        updateOverlayMask();
}

// Toast flotante estilo StarNote: cápsula de cristal centrada (abajo, o arriba
// si la barra está acoplada abajo) con fundido de entrada 140 ms, hold de
// 1.2 s y salida con fundido 220 ms. La QGraphicsOpacityEffect vive en la
// cápsula y es su único efecto, así no pisa ninguna sombra.
void FloatingToolbar::showToast(const QString& message) {
    if (!m_toastPill || !m_toastLabel)
        return;
    if (m_toastAnim) {
        m_toastAnim->stop();
        m_toastAnim->deleteLater();
    }
    if (m_toastTimer)
        m_toastTimer->stop();
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
    if (m_toastTimer)
        m_toastTimer->start(1200);
}

// Repinta el punto de vista previa con la tinta y el grosor reales del lienzo.
void FloatingToolbar::updateStrokePreview() {
    if (m_canvas && m_strokePreview)
        m_strokePreview->setInk(m_canvas->inkColor(), m_canvas->strokeSize());
}

// Sombra suave real: blur 22, yOffset 6, alfa 70 (tema oscuro) / 35 (claro).
void FloatingToolbar::applyShadow(QWidget* widget) {
    if (!widget || widget->graphicsEffect())
        return;
    auto* effect = new QGraphicsDropShadowEffect;
    effect->setBlurRadius(28);
    effect->setOffset(0.0, 8.0);
    effect->setColor(QColor(0, 0, 0, getThemeConfig(m_theme).isDark ? 90 : 30));
    widget->setGraphicsEffect(effect);
}

// Recolorea las sombras al cambiar de tema sin recrear efectos.
void FloatingToolbar::refreshShadowColors() {
    const QColor shadowColor(0, 0, 0, getThemeConfig(m_theme).isDark ? 90 : 30);
    const QVector<QWidget*> elevated = {m_pill,          m_contextPill,   m_edgeMarkPill,  m_zoomPill,      m_pagePill,
                                        m_strokePopover, m_paperPopover,   m_shapesPopover, m_stickersPopover, m_filePopover,
                                        m_dock};
    for (QWidget* widget : elevated) {
        if (!widget)
            continue;
        if (auto* effect = qobject_cast<QGraphicsDropShadowEffect*>(widget->graphicsEffect()))
            effect->setColor(shadowColor);
    }
}

// Restaura la posición acoplable persistida ("toolbar/dockPos": left/top/
// bottom, con migración desde el antiguo "toolbar/dockBottom" booleano).
void FloatingToolbar::loadDockPosition() {
    QSettings settings("Finily", "Finily");
    const QString pos = settings.value("toolbar/trayPos").toString();
    if (pos == QLatin1String("top"))
        m_dockPos = DockPos::Arriba;
    else if (pos == QLatin1String("bottom"))
        m_dockPos = DockPos::Abajo;
    else if (pos == QLatin1String("left"))
        m_dockPos = DockPos::Izquierda;
    else // por defecto a la izquierda (pensado para zurdos: la mano no tapa la barra)
        m_dockPos = DockPos::Izquierda;
}

// Alterna "Posición de barra" (Izquierda/Arriba/Abajo): reorienta el dock,
// reancla pills, popovers y toast; persiste y reabre el popover activo.
void FloatingToolbar::setDockPosition(DockPos pos) {
    if (m_dockPos == pos)
        return;
    m_dockPos = pos;
    QSettings settings("Finily", "Finily");
    settings.setValue("toolbar/trayPos", pos == DockPos::Izquierda ? "left"
                                         : pos == DockPos::Arriba  ? "top"
                                                                   : "bottom");

    GlassCapsule* open = m_openPopover;
    QWidget* anchor = m_popoverAnchor;
    closePopovers();
    positionChildren();
    refreshFromCanvas(); // sincroniza los botones Izquierda/Arriba/Abajo
    if (open)
        openPopover(open, anchor);
}

void FloatingToolbar::toggleCleanUi() {
    m_cleanUi = !m_cleanUi;
    // Corta cualquier animación previa de la pill (sin disparar su lambda de
    // cierre para no esconder controles que se van a restaurar).
    if (m_pillAnim) {
        QObject::disconnect(m_pillAnim, &QPropertyAnimation::finished, this, nullptr);
        m_pillAnim->stop();
        m_pillAnim->deleteLater();
        m_pillAnim = nullptr;
    }
    if (m_cleanUi) {
        closePopovers();
        if (m_zoomPill)
            m_zoomPill->hide();
        if (m_contextPill)
            m_contextPill->hide();
        if (m_pagePill)
            m_pagePill->hide();
        if (m_topRight)
            m_topRight->hide();
        if (m_thumbPanel)
            m_thumbPanel->hide();
        if (m_dock)
            m_dock->hide();
        if (m_restoreBtn)
            m_restoreBtn->show();
        positionChildren();
        // Desliza la pill 18 px hacia arriba (150 ms OutCubic) y la esconde.
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
                if (m_pill)
                    m_pill->hide();
                positionChildren();
            });
            m_pillAnim->start();
        }
    } else {
        if (m_restoreBtn)
            m_restoreBtn->hide();
        if (m_pill)
            m_pill->show();
        if (m_zoomPill)
            m_zoomPill->show();
        if (m_pagePill)
            m_pagePill->show();
        if (m_topRight)
            m_topRight->show();
        if (m_thumbPanel && m_pagesBtn && m_pagesBtn->isChecked()) {
            m_thumbPanel->show();
            rebuildThumbs();
        }
        if (m_dock)
            m_dock->show();
        positionChildren();
        // La pill entra deslizándose desde arriba (150 ms OutCubic).
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
    if (!m_canvas)
        return;
    // Seleccionar una herramienta distinta libera el preset activo.
    if (m_activePreset >= 0 && m_activePreset < m_presets.size() && m_presets.at(m_activePreset).tool != tool)
        m_activePreset = -1;
    m_canvas->setTool(tool);
    refreshFromCanvas();
}

void FloatingToolbar::selectPreset(int index) {
    if (index < 0 || index >= m_presets.size() || !m_canvas)
        return;
    m_activePreset = index;
    const PenPreset& preset = m_presets[index];
    m_canvas->setTool(preset.tool);
    m_canvas->setInkColor(preset.color);
    m_canvas->setStrokeSize(preset.size);
    m_canvas->setLinePattern(preset.linePattern);
    refreshFromCanvas();
}

void FloatingToolbar::setColor(const QColor& color) {
    if (!m_canvas || !color.isValid())
        return;
    m_canvas->setInkColor(color);
    syncActivePresetFromCanvas();
    updateStationery();
    refreshFromCanvas();
}

void FloatingToolbar::syncActivePresetFromCanvas() {
    if (!m_canvas || m_activePreset < 0 || m_activePreset >= m_presets.size())
        return;
    if (m_canvas->tool() == ToolType::Eraser) // el borrador no tiene preset de tinta
        return;
    PenPreset& preset = m_presets[m_activePreset];
    preset.tool = m_canvas->tool();
    preset.color = m_canvas->inkColor();
    preset.size = m_canvas->strokeSize();
    preset.linePattern = m_canvas->linePattern();
    // Persistir los presets favoritos solo cuando realmente cambian.
    const QString sig = QString("%1|%2|%3|%4|%5")
                            .arg(m_activePreset)
                            .arg(int(preset.tool))
                            .arg(preset.color.name(QColor::HexArgb))
                            .arg(preset.size)
                            .arg(int(preset.linePattern));
    if (sig != m_lastPresetsSig) {
        m_lastPresetsSig = sig;
        savePresets();
    }
}

void FloatingToolbar::updateStationery() {
    // Actualiza el color de tinta en los 4 útiles ilustrados SVG + el botón
    // "Trazo" de la pill superior (solo si el color o tema realmente cambió).
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor ink = m_canvas ? m_canvas->inkColor() : theme.accent;
    const QString sig = QString("%1|%2").arg(int(m_theme)).arg(ink.name(QColor::HexArgb));
    // Los lápices del estuche se repintan solo si su tinta cambia (setInk es barato).
    for (QToolButton* button : m_writingBtns)
        if (auto* pen = dynamic_cast<PenTrayButton*>(button)) {
            pen->setInk(ink);
            pen->setDarkTheme(theme.isDark);
        }
    if (sig != m_lastInkIconSig) {
        m_lastInkIconSig = sig;
    }

    // Muestras de la paleta del popover "Trazo" (ColorDotButton antialias):
    // punto de color real + rim sutil de 1px que contrasta con el tema.
    const QColor rim = theme.isDark ? QColor(255, 255, 255, 70) : QColor(15, 23, 42, 80);
    for (int i = 0; i < m_swatches.size() && i < m_palette.size(); ++i) {
        ColorDotButton* swatch = m_swatches.at(i);
        swatch->setDotColor(m_palette.at(i));
        swatch->setAccentColor(theme.accent);
        swatch->setRimColor(rim);
        swatch->setToolTip(m_palette.at(i).name());
    }
    if (m_trayColorBtn && m_canvas) {
        m_trayColorBtn->setDotColor(m_canvas->inkColor());
        m_trayColorBtn->setAccentColor(theme.accent);
        m_trayColorBtn->setRimColor(rim);
    }
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
}

void FloatingToolbar::updateZoomLabel() {
    if (!m_canvas || !m_zoomResetBtn)
        return;
    const int percent = qRound(m_canvas->viewport().zoom * 100.0);
    m_zoomResetBtn->setText(QString("%1 %").arg(percent));
}

void FloatingToolbar::onClearClicked() {
    if (!m_clearBtn)
        return;
    if (!m_clearArmed) {
        // Primer paso: armar la confirmación (se desarma sola a los 3 s).
        m_clearArmed = true;
        m_clearBtn->setText("¿Borrar anotaciones? Pulsa de nuevo");
        m_clearBtn->setStyleSheet(
            "QPushButton { background: rgba(239, 68, 68, 70); color: #fecaca; border-radius: 14px; }");
        if (m_clearTimer)
            m_clearTimer->start(3000);
        return;
    }
    disarmClearButton();
    if (m_canvas)
        m_canvas->clearBoard();
    closePopovers();
    refreshFromCanvas();
}

void FloatingToolbar::disarmClearButton() {
    m_clearArmed = false;
    if (m_clearTimer && m_clearTimer->isActive())
        m_clearTimer->stop();
    if (m_clearBtn) {
        m_clearBtn->setText("Borrar anotaciones de esta página");
        m_clearBtn->setStyleSheet(QString());
    }
}

void FloatingToolbar::refreshFromCanvas() {
    if (!m_canvas)
        return;

    // Cambio de tema: rederivar todo el cromo (QSS, iconos, paleta, presets).
    const ThemeId theme = m_canvas->themeId();
    if (theme != m_theme) {
        m_theme = theme;
        m_palette = getPaletteForTheme(getThemeConfig(theme).isDark);
        applyThemeStylesheet();
        if (m_canvas->tool() != ToolType::Highlighter)
            m_canvas->setInkColor(adaptInkToTheme(m_canvas->inkColor(), getThemeConfig(theme).isDark));
    }

    if (m_thumbPanel && !m_thumbPanel->isHidden() && m_thumbTimer) {
        const int active = m_canvas->currentPageIndex();
        if (m_thumbCurrent != active) {
            if (auto* previous = m_thumbList->item(m_thumbCurrent))
                previous->setIcon(QIcon(QPixmap::fromImage(m_canvas->pageThumbnail(m_thumbCurrent, thumbnailSize))));
            m_thumbCurrent = active;
            if (m_thumbList->currentRow() != active)
                m_thumbList->setCurrentRow(active, QItemSelectionModel::ClearAndSelect);
        }
        m_thumbTimer->start();
    }
    syncActivePresetFromCanvas();

    // Toggle "Posición de barra" (estado propio del toolbar, fuera de la
    // huella del lienzo para que siempre quede sincronizado).
    if (m_dockLeftBtn && m_dockTopBtn && m_dockBottomBtn) {
        m_dockLeftBtn->setChecked(m_dockPos == DockPos::Izquierda);
        m_dockTopBtn->setChecked(m_dockPos == DockPos::Arriba);
        m_dockBottomBtn->setChecked(m_dockPos == DockPos::Abajo);
    }
    // Formato de hoja (Infinita / Hoja A4 / Diapo 16:9).
    const PageFormat pageFormat = m_canvas->pageFormat();
    for (int i = 0; i < m_formatBtns.size() && i < m_formatSpecs.size(); ++i)
        m_formatBtns.at(i)->setChecked(m_formatSpecs.at(i) == pageFormat);
    if (m_mathBeautifyBtn)
        m_mathBeautifyBtn->setChecked(m_canvas->mathBeautifyEnabled());
    if (m_strokeBeautifyBtn)
        m_strokeBeautifyBtn->setChecked(m_canvas->mathBeautifyEnabled());
    if (m_mathModeBtn)
        m_mathModeBtn->setChecked(m_canvas->mathBeautifyEnabled());
    const int curStyle = m_canvas->cursorStyle();
    for (int i = 0; i < m_cursorBtns.size(); ++i)
        m_cursorBtns.at(i)->setChecked(i == curStyle);

    // Huella de estado: evita reflashear la UI en cada punto de un trazo a 200 Hz.
    const int pdfPageCount = m_canvas->documentPdfPageCount();
    const QString signature = QString("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17|%18|%19|%20|%21|%22|%23|%24|%25|%26|%27|%28|%29|%30")
                                  .arg(int(m_canvas->tool()))
                                  .arg(m_canvas->inkColor().name(QColor::HexArgb))
                                  .arg(m_canvas->strokeSize())
                                  .arg(int(m_canvas->linePattern()))
                                  .arg(m_canvas->stabilizer())
                                  .arg(qRound(m_canvas->highlighterOpacity() * 100.0))
                                  .arg(int(m_canvas->eraserMode()))
                                  .arg(m_canvas->eraserSize())
                                  .arg(int(m_canvas->eraserOnlyHighlight()))
                                  .arg(qRound(m_canvas->pressureSensitivity() * 100.0))
                                  .arg(int(m_canvas->themeId()))
                                  .arg(int(m_canvas->backgroundTemplate()))
                                  .arg(m_canvas->canUndo())
                                  .arg(m_canvas->canRedo())
                                  .arg(m_canvas->viewport().zoom)
                                  .arg(int(m_canvas->shapeKind()))
                                  .arg(m_canvas->holdToShapeEnabled())
                                  .arg(m_canvas->smartGuidesEnabled())
                                  .arg(int(m_canvas->isDrawingPolyline()) * 4 + int(m_canvas->hasSelectedAxes()) * 2 + int(m_canvas->hasSelectedShape()))
                                  .arg(int(m_canvas->hasLassoSelection()) + 2 * int(m_canvas->hasSelectedStrokes())
                                       + 4 * int(m_canvas->canGroupSelection()) + 8 * int(m_canvas->canUngroupSelection()))
                                  .arg(m_canvas->hasSelectedImage())
                                  .arg(m_canvas->hasSelectedText())
                                  .arg(pdfPageCount)
                                  .arg(m_canvas->currentPageIndex())
                                  .arg(m_canvas->pageCount())
                                  .arg(int(m_canvas->lineEnding()))
                                  .arg(m_canvas->polygonSides())
                                  .arg(m_canvas->triangleVariant())
                                  .arg(m_canvas->quadVariant())
                                  .arg(m_canvas->solidKind())
                                  .arg(m_canvas->isActivelyDragging())
                                  .arg(m_canvas->penTip() * 64 + m_canvas->highlighterTip() * 16
                                       + int(m_canvas->lassoRectMode()) * 8 + int(m_canvas->shadePattern())
                                       + qHash(m_canvas->congruenceMarkSpec()));
    if (signature == m_lastStateSig)
        return;
    m_lastStateSig = signature;

    m_updatingUi = true;

    // Historial
    m_undoBtn->setEnabled(m_canvas->canUndo());
    m_redoBtn->setEnabled(m_canvas->canRedo());

    // Herramientas (10 exclusivas: 4 útiles ilustrados + 6 duotono)
    const ToolType tool = m_canvas->tool();
    for (int i = 0; i < m_writingBtns.size() && i < m_writingKinds.size(); ++i)
        if (m_writingKinds.at(i) == tool || (m_writingKinds.at(i) == ToolType::Pen && tool == ToolType::Ballpoint))
            m_writingBtns.at(i)->setChecked(true);
    for (int i = 0; i < m_secondaryBtns.size() && i < m_secondaryKinds.size(); ++i)
        if (m_secondaryBtns.at(i) != m_stickersBtn && m_secondaryKinds.at(i) == tool)
            m_secondaryBtns.at(i)->setChecked(true);
    updateStationery();

    // Popover "Trazo" (10 niveles discretos + puntas de lápiz/resaltador + cuadritos de relleno)
    const bool isHl = tool == ToolType::Highlighter;
    const bool isPen = tool == ToolType::Pen || tool == ToolType::Ballpoint;
    const int sizeLevel = sizeToLevel(m_canvas->strokeSize(), isHl);
    if (m_sizeSlider->value() != sizeLevel)
        m_sizeSlider->setValue(sizeLevel);
    m_sizeLabel->setText(QString("%1 / 10").arg(sizeLevel));
    updateStrokePreview();
    updateFillSwatches();
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
    const LinePattern pattern = m_canvas->linePattern();
    for (int i = 0; i < m_patternSpecs.size(); ++i) {
        m_patternBtns.at(i)->setChecked(m_patternSpecs.at(i).second == pattern);
        m_patternBtns.at(i)->setEnabled(pattern != LinePattern::Dimension);
    }

    const int stabilizerLevel = qRound(m_canvas->stabilizer() * m_stabilizerSlider->maximum());
    if (m_stabilizerSlider->value() != stabilizerLevel)
        m_stabilizerSlider->setValue(stabilizerLevel);
    m_stabilizerLabel->setText(QString("%1 / 10").arg(stabilizerLevel));

    // Sensibilidad a la presión (0%..100%) y auto-embellecedor de escritura
    const int pressureLevel = qRound(m_canvas->pressureSensitivity() * 10.0);
    if (m_pressureSlider && m_pressureSlider->value() != pressureLevel)
        m_pressureSlider->setValue(pressureLevel);
    if (m_pressureLabel)
        m_pressureLabel->setText(QString("%1%").arg(pressureLevel * 10));
    if (m_mathBeautifyBtn)
        m_mathBeautifyBtn->setChecked(m_canvas->mathBeautifyEnabled());
    if (m_strokeBeautifyBtn)
        m_strokeBeautifyBtn->setChecked(m_canvas->mathBeautifyEnabled());
    if (m_mathModeBtn)
        m_mathModeBtn->setChecked(m_canvas->mathBeautifyEnabled());

    const QColor ink = m_canvas->inkColor();
    for (int i = 0; i < m_swatches.size() && i < m_palette.size(); ++i)
        m_swatches.at(i)->setChecked(m_palette.at(i) == ink);
    const bool inking = tool == ToolType::Pen || tool == ToolType::Ballpoint
                        || tool == ToolType::Highlighter || tool == ToolType::Shape
                        || tool == ToolType::Polyline || tool == ToolType::Text || tool == ToolType::Fill;
    for (int i = 0; i < m_trayDots.size() && i < m_palette.size(); ++i)
        m_trayDots.at(i)->setChecked(inking && m_palette.at(i) == ink);

    // Controles del borrador / lazo / relleno
    const bool erasing = tool == ToolType::Eraser;
    const bool filling = tool == ToolType::Fill;
    const bool lassoTool = tool == ToolType::Lasso;
    updateTipIcons();
    if (m_lassoModeSection)
        m_lassoModeSection->setVisible(lassoTool);
    if (m_lassoFreeBtn)
        m_lassoFreeBtn->setChecked(!m_canvas->lassoRectMode());
    if (m_lassoRectBtn)
        m_lassoRectBtn->setChecked(m_canvas->lassoRectMode());
    m_strokeStyleSection->setVisible(!erasing && !lassoTool);
    m_strokeGeometrySection->setVisible(!filling && !lassoTool);
    m_fillStyleSection->setVisible(filling);
    for (const auto& [button, pattern] : m_fillStyles)
        if (pattern == m_canvas->shadePattern())
            button->setChecked(true);
    m_stabilizerRow->setVisible(!erasing && !filling && !lassoTool && !isHl);
    if (m_hlOpacityRow) {
        m_hlOpacityRow->setVisible(isHl);
        if (m_hlOpacitySlider) {
            const double curOp = m_canvas->highlighterOpacity();
            int bestIdx = 3;
            double bestDist = 1e9;
            for (int i = 0; i < 10; ++i) {
                const double d = std::abs(kHlOpacityLevels[i] - curOp);
                if (d < bestDist) { bestDist = d; bestIdx = i; }
            }
            if (m_hlOpacitySlider->value() != bestIdx + 1)
                m_hlOpacitySlider->setValue(bestIdx + 1);
        }
    }
    if (m_strokeBeautifyBtn)
        m_strokeBeautifyBtn->setVisible(isPen);
    m_eraserSection->setVisible(erasing);
    m_eraserStrokeBtn->setChecked(m_canvas->eraserMode() == EraserMode::Stroke);
    m_eraserPartialBtn->setChecked(m_canvas->eraserMode() == EraserMode::Partial);
    const int eraserFilterIdx = m_canvas->eraserOnlyHighlight() ? 2 : (m_canvas->eraserPreserveShapes() ? 1 : 0);
    for (int i = 0; i < m_eraserFilterBtns.size(); ++i)
        m_eraserFilterBtns.at(i)->setChecked(i == eraserFilterIdx);
    const int eraserLevel = std::clamp(qRound(m_canvas->eraserSize() / 8.0), 1, 10);
    if (m_eraserPreview) {
        QColor erCol = getThemeConfig(m_theme).uiText;
        erCol.setAlpha(185);
        m_eraserPreview->setInk(erCol, 4.0 + eraserLevel * 1.8);
    }
    if (m_eraserSizeSlider->value() != eraserLevel)
        m_eraserSizeSlider->setValue(eraserLevel);
    m_eraserSizeLabel->setText(QString("%1 / 10").arg(eraserLevel));
    if (m_openPopover == m_strokePopover) {
        const QRect oldGeometry = m_strokePopover->geometry();
        positionOpenPopover();
        if (m_strokePopover->geometry() != oldGeometry)
            updateOverlayMask();
    }

    // Popover "Papel"
    const ThemeId activeTheme = m_canvas->themeId();
    for (int i = 0; i < m_themeBtns.size() && i < m_themeSpecs.size(); ++i)
        m_themeBtns.at(i)->setChecked(m_themeSpecs.at(i) == activeTheme);
    const BackgroundTemplate tpl = m_canvas->backgroundTemplate();
    for (int i = 0; i < m_templateSpecs.size(); ++i)
        m_templateBtns.at(i)->setChecked(m_templateSpecs.at(i).second == tpl);

    // Popover "Figuras": tipo de figura y toggles
    const ShapeKind shapeKind = m_canvas->shapeKind();
    const LinePattern pat = m_canvas->linePattern();
    const bool dimension = pat == LinePattern::Dimension;
    {
        const QSignalBlocker blocker(m_shapeEndings);
        m_shapeEndings->setCurrentIndex(int(m_canvas->lineEnding()));
        m_shapeEndings->setEnabled(!dimension);
    }
    for (int i = 0; i < m_shapeKindBtns.size(); ++i) {
        const QString btnMark = i < m_shapeMarkSpecs.size() ? m_shapeMarkSpecs.at(i) : QString();
        bool on = m_shapeKindSpecs.at(i) == shapeKind && btnMark == m_canvas->congruenceMarkSpec();
        if (on && btnMark.isEmpty() && m_shapeKindSpecs.at(i) == ShapeKind::Line)
            on = m_shapeDimensionSpecs.at(i) == dimension;
        if (on && m_shapeSidesSpecs.at(i) > 0)
            on = m_shapeSidesSpecs.at(i) == m_canvas->polygonSides()
                 && (i < m_shapeQuadSpecs.size() ? m_shapeQuadSpecs.at(i) == m_canvas->quadVariant() : true);
        if (on && i < m_shapeSolidSpecs.size() && m_shapeSolidSpecs.at(i) >= 0)
            on = m_shapeSolidSpecs.at(i) == m_canvas->solidKind();
        if (on && m_shapeTriSpecs.at(i) >= 0)
            on = m_shapeTriSpecs.at(i) == m_canvas->triangleVariant();
        m_shapeKindBtns.at(i)->setChecked(on);
    }
    m_holdToShapeBtn->setChecked(m_canvas->holdToShapeEnabled());
    m_smartGuidesBtn->setChecked(m_canvas->smartGuidesEnabled());

    const bool drawingPolyline = m_canvas->isDrawingPolyline();
    const bool hasSel = !drawingPolyline && m_canvas->hasSelection();
    m_finishPolylineBtn->setVisible(drawingPolyline);
    m_closePolylineBtn->setVisible(drawingPolyline);
    if (m_ctxBeautifyBtn)
        m_ctxBeautifyBtn->setVisible(hasSel && m_canvas->hasSelectedStrokes());
    if (m_ctxAxesFuncBtn)
        m_ctxAxesFuncBtn->setVisible(hasSel && m_canvas->hasSelectedAxes());
    if (m_ctxDuplicateBtn)
        m_ctxDuplicateBtn->setVisible(hasSel);
    if (m_ctxDeleteBtn)
        m_ctxDeleteBtn->setVisible(hasSel);
    m_selectionActions->setVisible(hasSel);
    const bool contextDesired = !m_cleanUi && !m_canvas->isActivelyDragging()
        && (drawingPolyline || m_canvas->hasSelection());
    const bool contextChanged = (m_contextPill->isVisible() != contextDesired);
    if (contextChanged)
        m_contextPill->setVisible(contextDesired);
    if (contextChanged || contextDesired)
        positionChildren(); // recalcula posición y máscara con el contenido actual

    // Etiqueta informativa del PDF cargado
    m_pdfInfoLabel->setVisible(pdfPageCount > 0);
    if (pdfPageCount > 0)
        m_pdfInfoLabel->setText(tr("Páginas de PDF en el cuaderno · %1").arg(pdfPageCount));

    // Pill de páginas (inferior izquierda): indicador y habilitaciones
    const int pageIndex = m_canvas->currentPageIndex();
    const int pageCount = m_canvas->pageCount();
    m_pageLabel->setText(QString("%1 / %2").arg(pageIndex + 1).arg(pageCount));
    m_pagePrevBtn->setEnabled(pageIndex > 0);
    m_pageNextBtn->setEnabled(pageIndex < pageCount - 1);
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
            // Un clic o toque del lápiz sobre el lienzo cierra cualquier popover abierto.
            if (m_openPopover)
                closePopovers();
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace finily
