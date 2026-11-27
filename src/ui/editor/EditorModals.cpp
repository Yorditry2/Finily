#include "ui/editor/EditorModals.h"
#include "ui/home/HomeTheme.h"

#include "core/Utils.h"
#include "engine/LibraryStore.h"
#include "engine/VisionExtractor.h"
#include "ui/CanvasWidget.h"
#include "ui/UiIcons.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTabletEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace finily {

namespace {

QString tabletActionName(TabletAction act) {
    switch (act) {
    case TabletAction::None: return QStringLiteral("Desactivado");
    case TabletAction::Undo: return QStringLiteral("Deshacer");
    case TabletAction::Redo: return QStringLiteral("Rehacer");
    case TabletAction::Pen: return QStringLiteral("Pluma");
    case TabletAction::Highlighter: return QStringLiteral("Resaltador");
    case TabletAction::Eraser: return QStringLiteral("Borrador");
    case TabletAction::HoldEraser: return QStringLiteral("Borrador mientras se pulsa");
    case TabletAction::LastWriteTool: return QStringLiteral("Último lápiz");
    case TabletAction::RightClickMenu: return QStringLiteral("Clic derecho / menú");
    case TabletAction::Lasso: return QStringLiteral("Lazo");
    case TabletAction::Select: return QStringLiteral("Puntero");
    case TabletAction::Shapes: return QStringLiteral("Figuras");
    case TabletAction::Polyline: return QStringLiteral("Polilínea");
    case TabletAction::Hand: return QStringLiteral("Mano");
    case TabletAction::Laser: return QStringLiteral("Láser");
    case TabletAction::RadialMenu: return QStringLiteral("Rueda de colores");
    case TabletAction::ToggleCleanUi: return QStringLiteral("Interfaz limpia");
    case TabletAction::NextPage: return QStringLiteral("Página siguiente");
    case TabletAction::PrevPage: return QStringLiteral("Página anterior");
    }
    return QStringLiteral("Ninguna");
}

QVector<EditorDropdownItem> stylusActionOptions(TabletAction current) {
    const QVector<TabletAction> list = {
        TabletAction::RightClickMenu, TabletAction::HoldEraser, TabletAction::LastWriteTool,
        TabletAction::Eraser, TabletAction::Undo, TabletAction::Redo,
        TabletAction::RadialMenu, TabletAction::Select, TabletAction::Lasso, TabletAction::Hand, TabletAction::None
    };
    QVector<EditorDropdownItem> out;
    for (TabletAction a : list)
        out.append({int(a), tabletActionName(a), {}, {}, QStringLiteral("ui"), false, false, a == current});
    return out;
}

QVector<EditorDropdownItem> padActionOptions(TabletAction current) {
    const QVector<TabletAction> list = {
        TabletAction::Undo, TabletAction::Redo, TabletAction::Lasso, TabletAction::RadialMenu,
        TabletAction::Pen, TabletAction::Highlighter, TabletAction::Eraser, TabletAction::Shapes,
        TabletAction::Polyline, TabletAction::Hand, TabletAction::Laser, TabletAction::ToggleCleanUi,
        TabletAction::PrevPage, TabletAction::NextPage, TabletAction::None
    };
    QVector<EditorDropdownItem> out;
    for (TabletAction a : list)
        out.append({int(a), tabletActionName(a), {}, {}, QStringLiteral("ui"), false, false, a == current});
    return out;
}

void clearLayout(QLayout* layout) {
    if (!layout)
        return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        } else if (item->layout()) {
            clearLayout(item->layout());
            delete item->layout();
        }
        delete item;
    }
}

QLabel* makeSectionHeading(const QString& text, const EditorPalette& pal, QWidget* parent) {
    auto* lbl = new QLabel(text, parent);
    lbl->setFont(editorFont(14.0, QFont::Bold));
    lbl->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(pal.text.name()));
    return lbl;
}

QLabel* makeKicker(const QString& text, const EditorPalette& pal, QWidget* parent) {
    auto* lbl = new QLabel(text, parent);
    lbl->setFont(editorFont(10.0, QFont::Bold));
    lbl->setStyleSheet(QStringLiteral("color: %1; background: transparent; padding-top: 6px;").arg(pal.muted.name()));
    return lbl;
}

QWidget* makeSettingRow(const QString& label, QWidget* rightControl, const EditorPalette& pal, QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 3, 0, 3);
    h->setSpacing(10);
    auto* lbl = new QLabel(label, w);
    lbl->setFont(editorFont(12.0, QFont::Medium));
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(pal.text.name()));
    h->addWidget(lbl, 1);
    h->addWidget(rightControl, 0, Qt::AlignRight | Qt::AlignVCenter);
    return w;
}

class PenWavePreview : public QWidget {
public:
    PenWavePreview(int tipId, const QColor& col, const EditorPalette& pal, QWidget* parent)
        : QWidget(parent), m_tip(tipId), m_col(col), m_pal(pal) {
        setFixedSize(210, 26);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath wave;
        for (int i = 0; i <= 32; ++i) {
            const qreal t = i / 32.0;
            const qreal x = 6.0 + t * (width() - 12.0);
            const qreal y = height() * 0.5 + std::sin(t * 6.28318 * 2.0) * 6.5;
            if (i == 0) wave.moveTo(x, y);
            else wave.lineTo(x, y);
        }
        QColor c = m_col.isValid() ? m_col : m_pal.text;
        const qreal w = (m_tip == 0) ? 1.8 : (m_tip == 1 ? 3.0 : (m_tip == 2 ? 2.4 : 4.5));
        c.setAlphaF(m_tip == 2 ? 0.80 : (m_tip == 3 ? 0.72 : 1.0));
        p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(wave);
    }
private:
    int m_tip;
    QColor m_col;
    EditorPalette m_pal;
};

} // namespace

// ---------------- WacomStylusDiagram ----------------

WacomStylusDiagram::WacomStylusDiagram(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(112);
    m_pal = editorPaletteFor(false);
}

void WacomStylusDiagram::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    update();
}

void WacomStylusDiagram::setConfig(TabletAction lower, TabletAction upper, bool invert,
                                   bool rearEraser, int cursorStyle) {
    m_lower = lower;
    m_upper = upper;
    m_invert = invert;
    m_rearEraser = rearEraser;
    m_cursorStyle = cursorStyle;
    update();
}

void WacomStylusDiagram::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(m_pal.borderSoft, 1.0));
    p.setBrush(m_pal.subtle);
    p.drawRoundedRect(r, 12.0, 12.0);

    const qreal sx = r.center().x() - 90.0;
    const qreal sy = r.center().y() + 4.0;
    const qreal sw = 178.0;
    const qreal sh = 14.0;

    // Goma trasera (izquierda)
    p.setPen(QPen(m_pal.icon, 1.3));
    p.setBrush(m_rearEraser ? m_pal.surfaceSolid : m_pal.subtle);
    p.drawRoundedRect(QRectF(sx - 18, sy - sh * 0.5, 22, sh), 5.0, 5.0);

    // Cuerpo del lápiz
    p.setPen(QPen(m_pal.icon, 1.4));
    p.setBrush(m_pal.surfaceSolid);
    p.drawRoundedRect(QRectF(sx, sy - sh * 0.5, sw, sh), 3.0, 3.0);

    // Punta (derecha)
    QPainterPath nib;
    nib.moveTo(sx + sw, sy - sh * 0.5);
    nib.lineTo(sx + sw + 28.0, sy);
    nib.lineTo(sx + sw, sy + sh * 0.5);
    nib.closeSubpath();
    p.setBrush(m_pal.accent);
    p.setPen(QPen(m_pal.icon, 1.2));
    p.drawPath(nib);

    // Botones laterales (Superior = hacia goma, Inferior = hacia punta; invertidos si m_invert)
    const QRectF btnBack(sx + 68, sy - sh * 0.5 - 3.0, 26, 5.0);
    const QRectF btnFront(sx + 104, sy - sh * 0.5 - 3.0, 26, 5.0);
    p.setPen(Qt::NoPen);
    p.setBrush(m_pal.text);
    p.drawRoundedRect(btnBack, 2.5, 2.5);
    p.drawRoundedRect(btnFront, 2.5, 2.5);

    const TabletAction upperAct = m_invert ? m_lower : m_upper;
    const TabletAction lowerAct = m_invert ? m_upper : m_lower;

    p.setFont(editorFont(10.5, QFont::Medium));
    p.setPen(m_pal.muted);

    // Etiquetas + líneas guía centradas sobre cada botón dentro de los márgenes seguros
    p.drawText(QRectF(10, sy - 16, std::max(40.0, sx - 30.0), 32), Qt::AlignRight | Qt::AlignVCenter,
               m_rearEraser ? QStringLiteral("Goma trasera") : QStringLiteral("Goma desactivada"));

    const qreal supLeft = std::clamp(btnBack.center().x() - 120.0, 10.0, std::max(10.0, width() - 250.0));
    p.drawText(QRectF(supLeft, 6, 240, 22), Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("Sup: ") + tabletActionName(upperAct));
    p.setPen(QPen(m_pal.border, 1.0));
    p.drawLine(QPointF(btnBack.center().x(), btnBack.top()), QPointF(btnBack.center().x(), 27));

    p.setPen(m_pal.muted);
    const qreal infLeft = std::clamp(btnFront.center().x() - 120.0, 10.0, std::max(10.0, width() - 250.0));
    p.drawText(QRectF(infLeft, sy + 18, 240, 22), Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("Inf: ") + tabletActionName(lowerAct));
    p.setPen(QPen(m_pal.border, 1.0));
    p.drawLine(QPointF(btnFront.center().x(), sy + sh * 0.5), QPointF(btnFront.center().x(), sy + 20));

    const QString ptr = (m_cursorStyle == 0) ? QStringLiteral("Lapicito")
                                             : (m_cursorStyle == 1 ? QStringLiteral("Aro") : QStringLiteral("Cruz"));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(sx + sw + 34, sy - 16, width() - (sx + sw + 42), 32),
               Qt::AlignLeft | Qt::AlignVCenter, ptr);
}

void WacomStylusDiagram::tabletEvent(QTabletEvent* event) {
    event->ignore();
}

// ---------------- EditorSettingsModal ----------------

EditorSettingsModal::EditorSettingsModal(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_pal = editorPaletteFor(false);
    m_nam = new QNetworkAccessManager(this);
    hide();

    m_card = new EditorGlassCard(this);
    m_card->setRole(EditorGlassCard::Role::ModalCard);
    m_card->setCornerRadius(18.0);
    m_card->setFixedSize(690, 560);

    m_navHost = new QWidget(m_card);
    m_navHost->setGeometry(0, 0, 176, 560);

    struct NavDef { const char* key; const char* title; const char* icon; const char* pack; };
    const NavDef defs[9] = {
        {"escritura", "Escritura", "pen", "ui"},
        {"resaltador", "Resaltador", "wand", "editor"},
        {"figuras", "Figuras", "shapes", "ui"},
        {"laser", "Láser", "laser", "ui"},
        {"paneles", "Paneles", "sliders", "editor"},
        {"paginas", "Páginas", "pages", "ui"},
        {"tableta", "Tableta y táctil", "stylus", "editor"},
        {"ia", "IA", "sparkle", "ui"},
        {"atajos", "Gestos y atajos", "keyboard", "editor"},
    };

    int y = 44;
    for (const auto& d : defs) {
        const QString key = QString::fromLatin1(d.key);
        auto* btn = new EditorChipButton(QString::fromUtf8(d.title), m_navHost);
        btn->setIcon(QString::fromLatin1(d.icon), QString::fromLatin1(d.pack));
        btn->setGeometry(10, y, 156, 32);
        btn->applyEditorTheme(m_dark);
        connect(btn, &EditorChipButton::clicked, this, [this, key] { openSection(key); });
        m_navBtns.append(btn);
        m_navKeys.append(key);
        y += 36;
    }

    m_closeBtn = new EditorIconButton(QStringLiteral("x"), QStringLiteral("ui"), m_card);
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setIconSizePx(14.0);
    m_closeBtn->move(690 - 40, 12);
    connect(m_closeBtn, &EditorIconButton::clicked, this, [this] {
        hide();
        emit closed();
    });

    m_scroll = new QScrollArea(m_card);
    m_scroll->setGeometry(178, 42, 690 - 186, 560 - 52);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(editorScrollAreaStyle(m_dark));

    m_bodyHost = new QWidget();
    m_bodyHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_bodyLayout = new QVBoxLayout(m_bodyHost);
    m_bodyLayout->setContentsMargins(16, 4, 20, 16);
    m_bodyLayout->setSpacing(10);
    m_scroll->setWidget(m_bodyHost);
}

void EditorSettingsModal::setDropdownOpener(DropdownRequestFn fn) {
    m_dropdownOpener = std::move(fn);
}

void EditorSettingsModal::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    m_card->applyEditorTheme(dark);
    m_closeBtn->applyEditorTheme(dark);
    if (m_scroll)
        m_scroll->setStyleSheet(editorScrollAreaStyle(dark));
    m_navHost->setStyleSheet(QStringLiteral("background: %1; border-right: 1px solid %2; border-top-left-radius: 18px; border-bottom-left-radius: 18px;")
                                 .arg(m_pal.subtle.name(), m_pal.borderSoft.name(QColor::HexArgb)));
    for (auto* b : m_navBtns)
        b->applyEditorTheme(dark);
    if (isVisible())
        buildSectionContent();
    update();
}

void EditorSettingsModal::centerCard() {
    if (m_card)
        m_card->move(std::max(0, (width() - m_card->width()) / 2),
                     std::max(0, (height() - m_card->height()) / 2));
}

void EditorSettingsModal::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    centerCard();
}

void EditorSettingsModal::openSection(const QString& sectionKey) {
    m_currentSection = sectionKey.isEmpty() ? QStringLiteral("escritura") : sectionKey;
    centerCard();
    updateNavStyles();
    buildSectionContent();
    if (m_scroll && m_scroll->verticalScrollBar())
        m_scroll->verticalScrollBar()->setValue(0);
    show();
    raise();
    setFocus();
}

void EditorSettingsModal::syncFromCanvas(CanvasWidget* canvas, ToolbarDockMode dockMode, int pageNavDir) {
    m_canvas = canvas;
    m_dockMode = dockMode;
    m_pageNavDir = pageNavDir;
    if (isVisible())
        buildSectionContent();
}

void EditorSettingsModal::updateNavStyles() {
    for (int i = 0; i < m_navBtns.size(); ++i)
        m_navBtns.at(i)->setChecked(m_navKeys.at(i) == m_currentSection);
}

void EditorSettingsModal::buildSectionContent() {
    clearLayout(m_bodyLayout);
    const QString& key = m_currentSection;

    auto addCheck = [this](const QString& title, const QString& sub, bool checked, std::function<void(bool)> cb) {
        auto* chk = new EditorCheckBox(title, sub, m_bodyHost);
        chk->setChecked(checked);
        chk->applyEditorTheme(m_dark);
        connect(chk, &EditorCheckBox::toggled, this, std::move(cb));
        m_bodyLayout->addWidget(chk);
        return chk;
    };

    auto addSwitchRow = [this](const QString& title, bool checked, std::function<void(bool)> cb) {
        auto* sw = new EditorSwitch(m_bodyHost);
        sw->setChecked(checked);
        sw->applyEditorTheme(m_dark);
        connect(sw, &EditorSwitch::toggled, this, std::move(cb));
        m_bodyLayout->addWidget(makeSettingRow(title, sw, m_pal, m_bodyHost));
        return sw;
    };

    if (key == QStringLiteral("escritura")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Escritura"), m_pal, m_bodyHost));
        const double sens = m_canvas ? m_canvas->pressureSensitivity() : 0.5;
        addSwitchRow(QStringLiteral("Sensible a la presión"), sens > 0.01, [this](bool on) {
            if (!m_canvas) return;
            if (on) {
                const double restored = std::clamp(
                    readAppSetting(QStringLiteral("ink/pressureSensitivitySaved"), 0.5).toDouble(), 0.05, 1.0);
                m_canvas->setPressureSensitivity(restored);
            } else {
                const double cur = m_canvas->pressureSensitivity();
                if (cur > 0.01)
                    writeAppSetting(QStringLiteral("ink/pressureSensitivitySaved"), cur);
                m_canvas->setPressureSensitivity(0.0);
            }
        });

        auto* profSeg = new EditorSegControl(m_bodyHost);
        profSeg->setOptions({QStringLiteral("Ligera"), QStringLiteral("Normal"), QStringLiteral("Firme")});
        profSeg->setCurrentIndex(m_canvas ? int(m_canvas->pressureProfile()) : 0);
        profSeg->applyEditorTheme(m_dark);
        connect(profSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
            if (m_canvas) m_canvas->setPressureProfile(PressureProfile(idx));
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Perfil de presión"), profSeg, m_pal, m_bodyHost));

        const int stabLevel = m_canvas ? std::clamp(qRound(m_canvas->stabilizer() * 9.0), 0, 9) : 2;
        auto* stabSeg = new EditorSegControl(m_bodyHost);
        stabSeg->setOptions({QStringLiteral("Bajo"), QStringLiteral("Medio"), QStringLiteral("Alto"), QStringLiteral("Pers.")});
        stabSeg->setCurrentIndex(stabLevel <= 2 ? 0 : (stabLevel <= 5 ? 1 : (stabLevel <= 7 ? 2 : 3)));
        stabSeg->applyEditorTheme(m_dark);
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Suavizado de trazo"), stabSeg, m_pal, m_bodyHost));

        auto* stabSlider = new EditorSlider(m_bodyHost);
        stabSlider->setRange(0.0, 9.0, 1.0);
        stabSlider->setValue(stabLevel);
        stabSlider->applyEditorTheme(m_dark);
        connect(stabSeg, &EditorSegControl::currentIndexChanged, this, [this, stabSlider](int idx) {
            const int lv = (idx == 0) ? 2 : (idx == 1 ? 4 : (idx == 2 ? 7 : qRound(stabSlider->value())));
            stabSlider->setValue(lv);
            if (m_canvas) m_canvas->setStabilizer(lv / 9.0);
        });
        connect(stabSlider, &EditorSlider::valueChanged, this, [this, stabSeg](double v) {
            if (m_canvas) m_canvas->setStabilizer(v / 9.0);
            const int lv = qRound(v);
            const int seg = (lv == 2) ? 0 : (lv == 4 ? 1 : (lv == 7 ? 2 : 3));
            QSignalBlocker blocker(stabSeg);
            stabSeg->setCurrentIndex(seg);
        });
        m_bodyLayout->addWidget(stabSlider);

        addSwitchRow(QStringLiteral("Predicción de punta"),
                     m_canvas ? m_canvas->tipPredictionEnabled() : true,
                     [this](bool on) { if (m_canvas) m_canvas->setTipPredictionEnabled(on); });
        addSwitchRow(QStringLiteral("Remate afilado (Pluma fuente)"),
                     m_canvas ? m_canvas->fountainFlickEnabled() : true,
                     [this](bool on) { if (m_canvas) m_canvas->setFountainFlickEnabled(on); });
        addSwitchRow(QStringLiteral("Auto-embellecer al pausar (M)"),
                     m_canvas ? m_canvas->mathBeautifyEnabled() : false,
                     [this](bool on) { if (m_canvas) m_canvas->setMathBeautifyEnabled(on); });

        auto* pauseSlider = new EditorSlider(m_bodyHost);
        pauseSlider->setRange(300.0, 1500.0, 50.0);
        pauseSlider->setValue(m_canvas ? m_canvas->beautifyPauseMs() : 650);
        pauseSlider->applyEditorTheme(m_dark);
        connect(pauseSlider, &EditorSlider::valueChanged, this, [this](double v) {
            if (m_canvas) m_canvas->setBeautifyPauseMs(qRound(v));
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Tiempo de pausa (ms)"), pauseSlider, m_pal, m_bodyHost));
    } else if (key == QStringLiteral("resaltador")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Resaltador"), m_pal, m_bodyHost));
        addSwitchRow(QStringLiteral("Pintar detrás de la escritura"),
                     m_canvas ? m_canvas->highlighterDrawBehindInk() : true,
                     [this](bool on) { if (m_canvas) m_canvas->setHighlighterDrawBehindInk(on); });
        addSwitchRow(QStringLiteral("Enderezar al soltar"),
                     m_canvas ? m_canvas->highlighterStraightenOnRelease() : false,
                     [this](bool on) { if (m_canvas) m_canvas->setHighlighterStraightenOnRelease(on); });
        addSwitchRow(QStringLiteral("Punta redonda"),
                     m_canvas ? m_canvas->highlighterRoundTip() : false,
                     [this](bool on) { if (m_canvas) m_canvas->setHighlighterRoundTip(on); });
    } else if (key == QStringLiteral("figuras")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Figuras"), m_pal, m_bodyHost));
        addSwitchRow(QStringLiteral("Figura perfecta al mantener"),
                     m_canvas ? m_canvas->holdToShapeEnabled() : true,
                     [this](bool on) { if (m_canvas) m_canvas->setHoldToShapeEnabled(on); });

        auto* holdSlider = new EditorSlider(m_bodyHost);
        holdSlider->setRange(200.0, 800.0, 20.0);
        holdSlider->setValue(m_canvas ? m_canvas->holdToShapeDelayMs() : 380);
        holdSlider->applyEditorTheme(m_dark);
        connect(holdSlider, &EditorSlider::valueChanged, this, [this](double v) {
            if (m_canvas) m_canvas->setHoldToShapeDelayMs(qRound(v));
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Espera al mantener (ms)"), holdSlider, m_pal, m_bodyHost));

        m_bodyLayout->addWidget(makeKicker(QStringLiteral("QUÉ RECONOCE"), m_pal, m_bodyHost));
        auto* gridW = new QWidget(m_bodyHost);
        auto* grid = new QGridLayout(gridW);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(4);

        AutoShapeOptions ao = m_canvas ? m_canvas->autoShapeOptions() : AutoShapeOptions{};
        auto addAutoOpt = [this, grid, gridW](int idx, const QString& lbl, bool val, std::function<void(AutoShapeOptions&, bool)> setter) {
            auto* c = new EditorCheckBox(lbl, {}, gridW);
            c->setChecked(val);
            c->applyEditorTheme(m_dark);
            connect(c, &EditorCheckBox::toggled, this, [this, setter](bool on) {
                if (!m_canvas) return;
                AutoShapeOptions cur = m_canvas->autoShapeOptions();
                setter(cur, on);
                m_canvas->setAutoShapeOptions(cur);
            });
            grid->addWidget(c, idx / 2, idx % 2);
        };
        addAutoOpt(0, QStringLiteral("Línea"), ao.line, [](AutoShapeOptions& o, bool v) { o.line = v; });
        addAutoOpt(1, QStringLiteral("Curva"), ao.curve, [](AutoShapeOptions& o, bool v) { o.curve = v; });
        addAutoOpt(2, QStringLiteral("Círculo y elipse"), ao.circle, [](AutoShapeOptions& o, bool v) { o.circle = v; });
        addAutoOpt(3, QStringLiteral("Triángulo"), ao.triangle, [](AutoShapeOptions& o, bool v) { o.triangle = v; });
        addAutoOpt(4, QStringLiteral("Cuadrado y rectángulo"), ao.rectangle, [](AutoShapeOptions& o, bool v) { o.rectangle = v; });
        addAutoOpt(5, QStringLiteral("Otros cuadriláteros"), ao.quadrilateral, [](AutoShapeOptions& o, bool v) { o.quadrilateral = v; });
        addAutoOpt(6, QStringLiteral("Polígonos de 5 a 8 lados"), ao.polygon, [](AutoShapeOptions& o, bool v) { o.polygon = v; });
        addAutoOpt(7, QStringLiteral("Raíz cuadrada"), ao.radical, [](AutoShapeOptions& o, bool v) { o.radical = v; });
        addAutoOpt(8, QStringLiteral("Ángulo en L"), ao.rightAngleL, [](AutoShapeOptions& o, bool v) { o.rightAngleL = v; });
        m_bodyLayout->addWidget(gridW);

        m_bodyLayout->addWidget(makeKicker(QStringLiteral("GESTOS SOBRE FIGURAS"), m_pal, m_bodyHost));
        GeometryGestureOptions go = m_canvas ? m_canvas->geometryGestureOptions() : GeometryGestureOptions{};
        auto addGest = [&](const QString& t, const QString& sub, bool val, std::function<void(GeometryGestureOptions&, bool)> setter) {
            addCheck(t, sub, val, [this, setter](bool on) {
                if (!m_canvas) return;
                GeometryGestureOptions cur = m_canvas->geometryGestureOptions();
                setter(cur, on);
                m_canvas->setGeometryGestureOptions(cur);
            });
        };
        addGest(QStringLiteral("Marcar ángulo"), QStringLiteral("Un arquito en una esquina"), go.angleArc,
                [](GeometryGestureOptions& o, bool v) { o.angleArc = v; });
        addGest(QStringLiteral("Ángulo recto"), QStringLiteral("Una escuadrita marca 90°"), go.rightAngle,
                [](GeometryGestureOptions& o, bool v) { o.rightAngle = v; });
        addGest(QStringLiteral("Flecha"), QStringLiteral("Línea con punta en un solo trazo"), go.freehandArrow,
                [](GeometryGestureOptions& o, bool v) { o.freehandArrow = v; });
        addGest(QStringLiteral("Punta de flecha"), QStringLiteral("Dibujar «>» al final de una línea"), go.attachArrowhead,
                [](GeometryGestureOptions& o, bool v) { o.attachArrowhead = v; });
        addGest(QStringLiteral("Punto divisor"), QStringLiteral("Un puntito sobre un lado"), go.dividerDot,
                [](GeometryGestureOptions& o, bool v) { o.dividerDot = v; });
        addGest(QStringLiteral("Marcas de lado"), QStringLiteral("Rayitas de igualdad a mano"), go.edgeMarks,
                [](GeometryGestureOptions& o, bool v) { o.edgeMarks = v; });
        addGest(QStringLiteral("Mantener sobre un lado"), QStringLiteral("Abre la cápsula de marcas"), go.holdEdgeMenu,
                [](GeometryGestureOptions& o, bool v) { o.holdEdgeMenu = v; });

        m_bodyLayout->addWidget(makeKicker(QStringLiteral("GUÍAS E IMÁN"), m_pal, m_bodyHost));
        SmartGuideOptions so = m_canvas ? m_canvas->smartGuideOptions() : SmartGuideOptions{};
        addSwitchRow(QStringLiteral("Guías inteligentes e imantado"), so.enabled, [this](bool on) {
            if (!m_canvas) return;
            SmartGuideOptions cur = m_canvas->smartGuideOptions();
            cur.enabled = on;
            m_canvas->setSmartGuideOptions(cur);
        });
        addCheck(QStringLiteral("Imán a puntos clave"), {}, so.snapKeyPoints, [this](bool on) {
            if (!m_canvas) return;
            SmartGuideOptions cur = m_canvas->smartGuideOptions();
            cur.snapKeyPoints = on;
            m_canvas->setSmartGuideOptions(cur);
        });
        addCheck(QStringLiteral("Guías 0°/90° y perpendicular"), {}, so.orthoPerpGuides, [this](bool on) {
            if (!m_canvas) return;
            SmartGuideOptions cur = m_canvas->smartGuideOptions();
            cur.orthoPerpGuides = on;
            m_canvas->setSmartGuideOptions(cur);
        });
        addCheck(QStringLiteral("Alineación X / Y"), {}, so.alignXY, [this](bool on) {
            if (!m_canvas) return;
            SmartGuideOptions cur = m_canvas->smartGuideOptions();
            cur.alignXY = on;
            m_canvas->setSmartGuideOptions(cur);
        });
        addSwitchRow(QStringLiteral("Mostrar medidas automáticas"),
                     m_canvas ? m_canvas->showAutoMeasurements() : false,
                     [this](bool on) { if (m_canvas) m_canvas->setShowAutoMeasurements(on); });
    } else if (key == QStringLiteral("laser")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Láser"), m_pal, m_bodyHost));
        auto* seg = new EditorSegControl(m_bodyHost);
        seg->setOptions({QStringLiteral("1 s"), QStringLiteral("2 s"), QStringLiteral("4 s")});
        const int ms = m_canvas ? m_canvas->laserDurationMs() : 1000;
        seg->setCurrentIndex(ms <= 1200 ? 0 : (ms <= 2500 ? 1 : 2));
        seg->applyEditorTheme(m_dark);
        connect(seg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
            if (m_canvas)
                m_canvas->setLaserDurationMs(idx == 0 ? 1000 : (idx == 1 ? 2000 : 4000));
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Duración de la estela"), seg, m_pal, m_bodyHost));
    } else if (key == QStringLiteral("paneles")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Paneles"), m_pal, m_bodyHost));
        auto* dockSeg = new EditorSegControl(m_bodyHost);
        dockSeg->setOptions({QStringLiteral("Arriba"), QStringLiteral("Izquierda"), QStringLiteral("Flotante")});
        dockSeg->setCurrentIndex(int(m_dockMode));
        dockSeg->applyEditorTheme(m_dark);
        connect(dockSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
            m_dockMode = ToolbarDockMode(idx);
            emit dockModeChanged(m_dockMode);
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Posición de la barra"), dockSeg, m_pal, m_bodyHost));

        auto* sideSeg = new EditorSegControl(m_bodyHost);
        sideSeg->setOptions({QStringLiteral("Izquierda"), QStringLiteral("Derecha")});
        sideSeg->setCurrentIndex(readAppSetting(QStringLiteral("panels/elementsRight"), false).toBool() ? 1 : 0);
        sideSeg->applyEditorTheme(m_dark);
        connect(sideSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
            writeAppSetting(QStringLiteral("panels/elementsRight"), idx == 1);
            emit elementsSideChanged(idx == 1);
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Panel de Elementos"), sideSeg, m_pal, m_bodyHost));

        auto* stySeg = new EditorSegControl(m_bodyHost);
        stySeg->setOptions({QStringLiteral("Limpio"), QStringLiteral("Ficha"), QStringLiteral("Solo dibujo")});
        stySeg->setCurrentIndex(std::clamp(readAppSetting(QStringLiteral("panels/defaultStickerStyle"), 0).toInt(), 0, 2));
        stySeg->applyEditorTheme(m_dark);
        connect(stySeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
            writeAppSetting(QStringLiteral("panels/defaultStickerStyle"), idx);
            emit defaultStickerStyleChanged(idx);
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Estilo de sticker por defecto"), stySeg, m_pal, m_bodyHost));
    } else if (key == QStringLiteral("paginas")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Páginas"), m_pal, m_bodyHost));
        auto* dirSeg = new EditorSegControl(m_bodyHost);
        dirSeg->setOptions({QStringLiteral("Vertical"), QStringLiteral("Horizontal")});
        dirSeg->setCurrentIndex(m_pageNavDir == 1 ? 0 : 1);
        dirSeg->applyEditorTheme(m_dark);
        connect(dirSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
            m_pageNavDir = (idx == 0 ? 1 : 0);
            emit pageNavDirectionChanged(m_pageNavDir);
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Dirección de las páginas"), dirSeg, m_pal, m_bodyHost));

        const QStringList fmtNames = {
            QStringLiteral("Igual que la anterior"), QStringLiteral("A4 ↕"),
            QStringLiteral("A4 ↔"), QStringLiteral("Carta"), QStringLiteral("16:9")
        };
        const int curFmt = std::clamp(readAppSetting(QStringLiteral("pages/defaultNewFormat"), 0).toInt(), 0, 4);
        auto* fmtBtn = new EditorChipButton(fmtNames.at(curFmt) + QStringLiteral(" ▾"), m_bodyHost);
        fmtBtn->applyEditorTheme(m_dark);
        connect(fmtBtn, &EditorChipButton::clicked, this, [this, fmtBtn, fmtNames] {
            if (!m_dropdownOpener) return;
            const int cur = std::clamp(readAppSetting(QStringLiteral("pages/defaultNewFormat"), 0).toInt(), 0, 4);
            QVector<EditorDropdownItem> items;
            for (int i = 0; i < fmtNames.size(); ++i)
                items.append({i, fmtNames.at(i), {}, {}, QStringLiteral("ui"), false, false, i == cur});
            m_dropdownOpener(fmtBtn, items, [fmtBtn, fmtNames](int id) {
                writeAppSetting(QStringLiteral("pages/defaultNewFormat"), id);
                fmtBtn->setText(fmtNames.at(id) + QStringLiteral(" ▾"));
            });
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Formato de hoja nueva"), fmtBtn, m_pal, m_bodyHost));

        addSwitchRow(QStringLiteral("Mostrar número de página"),
                     readAppSetting(QStringLiteral("pages/showNumber"), true).toBool(),
                     [](bool on) { writeAppSetting(QStringLiteral("pages/showNumber"), on); });
    } else if (key == QStringLiteral("tableta")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Tableta y táctil"), m_pal, m_bodyHost));

        auto* diagram = new WacomStylusDiagram(m_bodyHost);
        diagram->applyEditorTheme(m_dark);
        auto refreshDiagram = [this, diagram] {
            if (!m_canvas) return;
            diagram->setConfig(m_canvas->stylusLowerAction(), m_canvas->stylusUpperAction(),
                               m_canvas->swapPenButtons(), m_canvas->stylusEraserTipEnabled(),
                               m_canvas->cursorStyle());
        };
        refreshDiagram();
        m_bodyLayout->addWidget(diagram);

        m_bodyLayout->addWidget(makeKicker(QStringLiteral("LÁPIZ"), m_pal, m_bodyHost));
        const TabletAction lowerAct = m_canvas ? m_canvas->stylusLowerAction() : TabletAction::LastWriteTool;
        auto* lowerBtn = new EditorChipButton(tabletActionName(lowerAct) + QStringLiteral(" ▾"), m_bodyHost);
        lowerBtn->applyEditorTheme(m_dark);
        connect(lowerBtn, &EditorChipButton::clicked, this, [this, lowerBtn, refreshDiagram] {
            if (!m_dropdownOpener || !m_canvas) return;
            m_dropdownOpener(lowerBtn, stylusActionOptions(m_canvas->stylusLowerAction()),
                             [this, lowerBtn, refreshDiagram](int id) {
                                 m_canvas->setStylusLowerAction(TabletAction(id));
                                 lowerBtn->setText(tabletActionName(TabletAction(id)) + QStringLiteral(" ▾"));
                                 refreshDiagram();
                             });
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Botón lateral inferior"), lowerBtn, m_pal, m_bodyHost));

        const TabletAction upperAct = m_canvas ? m_canvas->stylusUpperAction() : TabletAction::Eraser;
        auto* upperBtn = new EditorChipButton(tabletActionName(upperAct) + QStringLiteral(" ▾"), m_bodyHost);
        upperBtn->applyEditorTheme(m_dark);
        connect(upperBtn, &EditorChipButton::clicked, this, [this, upperBtn, refreshDiagram] {
            if (!m_dropdownOpener || !m_canvas) return;
            m_dropdownOpener(upperBtn, stylusActionOptions(m_canvas->stylusUpperAction()),
                             [this, upperBtn, refreshDiagram](int id) {
                                 m_canvas->setStylusUpperAction(TabletAction(id));
                                 upperBtn->setText(tabletActionName(TabletAction(id)) + QStringLiteral(" ▾"));
                                 refreshDiagram();
                             });
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Botón lateral superior"), upperBtn, m_pal, m_bodyHost));

        addSwitchRow(QStringLiteral("Invertir botones del lápiz"),
                     m_canvas ? m_canvas->swapPenButtons() : false,
                     [this, refreshDiagram](bool on) {
                         if (m_canvas) m_canvas->setSwapPenButtons(on);
                         refreshDiagram();
                     });
        addSwitchRow(QStringLiteral("Goma trasera del lápiz"),
                     m_canvas ? m_canvas->stylusEraserTipEnabled() : true,
                     [this, refreshDiagram](bool on) {
                         if (m_canvas) m_canvas->setStylusEraserTipEnabled(on);
                         refreshDiagram();
                     });

        m_bodyLayout->addWidget(makeKicker(QStringLiteral("BOTONES DEL PAD"), m_pal, m_bodyHost));
        const TabletAction padDefaults[4] = {
            TabletAction::Undo, TabletAction::Redo, TabletAction::Lasso, TabletAction::RadialMenu
        };
        for (int b = 0; b < 4; ++b) {
            const QString settingKey = QStringLiteral("tablet/padButton%1").arg(b);
            const auto cur = TabletAction(readAppSetting(settingKey, int(padDefaults[b])).toInt());
            auto* padBtn = new EditorChipButton(tabletActionName(cur) + QStringLiteral(" ▾"), m_bodyHost);
            padBtn->applyEditorTheme(m_dark);
            connect(padBtn, &EditorChipButton::clicked, this, [this, padBtn, settingKey, padDefaults, b] {
                if (!m_dropdownOpener) return;
                const auto curVal = TabletAction(readAppSetting(settingKey, int(padDefaults[b])).toInt());
                m_dropdownOpener(padBtn, padActionOptions(curVal), [this, padBtn, settingKey](int id) {
                    writeAppSetting(settingKey, id);
                    padBtn->setText(tabletActionName(TabletAction(id)) + QStringLiteral(" ▾"));
                    emit padActionsChanged();
                });
            });
            m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Botón %1").arg(b + 1), padBtn, m_pal, m_bodyHost));
        }

        auto* ptrSeg = new EditorSegControl(m_bodyHost);
        ptrSeg->setOptions({QStringLiteral("Lapicito"), QStringLiteral("Aro"), QStringLiteral("Cruz")});
        ptrSeg->setCurrentIndex(m_canvas ? std::clamp(m_canvas->cursorStyle(), 0, 2) : 0);
        ptrSeg->applyEditorTheme(m_dark);
        connect(ptrSeg, &EditorSegControl::currentIndexChanged, this, [this, refreshDiagram](int idx) {
            if (m_canvas) m_canvas->setCursorStyle(idx);
            refreshDiagram();
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Estilo de puntero"), ptrSeg, m_pal, m_bodyHost));

        addSwitchRow(QStringLiteral("Rueda radial de colores"),
                     m_canvas ? m_canvas->radialMenuEnabled() : true,
                     [this](bool on) { if (m_canvas) m_canvas->setRadialMenuEnabled(on); });
        addSwitchRow(QStringLiteral("Ignorar la palma (rechazo táctil)"),
                     m_canvas ? m_canvas->palmRejectionEnabled() : true,
                     [this](bool on) { if (m_canvas) m_canvas->setPalmRejectionEnabled(on); });
    } else if (key == QStringLiteral("ia")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("IA"), m_pal, m_bodyHost));
        auto* keyEdit = new EditorLineEdit(m_bodyHost);
        keyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
        keyEdit->setPlaceholderText(QStringLiteral("AIza..."));
        keyEdit->setText(VisionExtractor::apiKey());
        keyEdit->setFixedSize(210, 28);
        keyEdit->applyEditorTheme(m_dark);
        connect(keyEdit, &QLineEdit::editingFinished, this, [keyEdit] {
            VisionExtractor::setApiKey(keyEdit->text().trimmed());
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Clave de API"), keyEdit, m_pal, m_bodyHost));

        auto* modelBtn = new EditorChipButton(VisionExtractor::model() + QStringLiteral(" ▾"), m_bodyHost);
        modelBtn->applyEditorTheme(m_dark);
        connect(modelBtn, &EditorChipButton::clicked, this, [this, modelBtn] {
            if (!m_dropdownOpener) return;
            const QStringList models = VisionExtractor::supportedModels();
            const QString cur = VisionExtractor::model();
            QVector<EditorDropdownItem> items;
            for (int i = 0; i < models.size(); ++i)
                items.append({i, models.at(i), {}, {}, QStringLiteral("ui"), false, false, models.at(i) == cur});
            m_dropdownOpener(modelBtn, items, [modelBtn, models](int id) {
                VisionExtractor::setModel(models.at(id));
                modelBtn->setText(models.at(id) + QStringLiteral(" ▾"));
            });
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Modelo"), modelBtn, m_pal, m_bodyHost));

        auto* statusLbl = new QLabel(QString(), m_bodyHost);
        statusLbl->setFont(editorFont(11.5, QFont::Medium));
        statusLbl->setStyleSheet(QStringLiteral("color: %1;").arg(m_pal.muted.name()));

        auto* testBtn = new EditorChipButton(QStringLiteral("Probar conexión"), m_bodyHost);
        testBtn->setChecked(true);
        testBtn->applyEditorTheme(m_dark);
        connect(testBtn, &EditorChipButton::clicked, this, [this, keyEdit, statusLbl] {
            const QString apiKey = keyEdit->text().trimmed();
            VisionExtractor::setApiKey(apiKey);
            if (apiKey.isEmpty()) {
                statusLbl->setText(QStringLiteral("Introduce una clave de API primero."));
                return;
            }
            const auto req = buildVisionRequest(VisionExtractor::model(), apiKey);
            if (!req) {
                statusLbl->setText(QStringLiteral("Modelo no válido."));
                return;
            }
            statusLbl->setText(QStringLiteral("Comprobando…"));
            QNetworkRequest probe(QUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/") + VisionExtractor::model()));
            probe.setRawHeader("x-goog-api-key", apiKey.toUtf8());
            QNetworkReply* reply = m_nam->get(probe);
            QPointer<QLabel> safeStatus(statusLbl);
            connect(reply, &QNetworkReply::finished, this, [reply, safeStatus] {
                const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const auto err = reply->error();
                reply->deleteLater();
                if (!safeStatus)
                    return;
                if (err == QNetworkReply::NoError && code >= 200 && code < 300)
                    safeStatus->setText(QStringLiteral("Conexión correcta"));
                else
                    safeStatus->setText(QStringLiteral("Error de conexión (%1)").arg(code > 0 ? QString::number(code) : QStringLiteral("red")));
            });
        });
        m_bodyLayout->addWidget(makeSettingRow(QStringLiteral("Comprobar clave y modelo"), testBtn, m_pal, m_bodyHost));
        m_bodyLayout->addWidget(statusLbl);
    } else {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Gestos y atajos"), m_pal, m_bodyHost));
        struct ShortcutRow { const char* desc; const char* key; };
        const ShortcutRow rows[] = {
            {"Línea recta cada 15°", "Mayús al escribir"},
            {"Trazo auxiliar en guiones", "D"},
            {"Rueda de colores", "Doble P / botón del lápiz"},
            {"Recortar entre cruces", "Mayús + borrador"},
            {"Auto-embellecer al pausar", "M"},
            {"Interfaz limpia · Pantalla completa", "Tab · F11"},
            {"Pinceles favoritos", "1 – 5"},
            {"Puntero · Mano", "V · Espacio"},
            {"Pluma · Resaltador · Borrador · Relleno", "P · H · E · F / G"},
            {"Lazo · Trazado · Figuras · Texto · Láser", "L · Shift+L · S · T · K"},
            {"Copiar · Cortar · Pegar · Duplicar", "Ctrl+C · X · V · D"},
        };
        for (const auto& sr : rows) {
            auto* kbd = new EditorChipButton(QString::fromUtf8(sr.key), m_bodyHost);
            kbd->applyEditorTheme(m_dark);
            m_bodyLayout->addWidget(makeSettingRow(QString::fromUtf8(sr.desc), kbd, m_pal, m_bodyHost));
        }
    }

    m_bodyLayout->addStretch(1);
}

void EditorSettingsModal::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), m_pal.veil);
    if (m_card && m_card->isVisible())
        paintSoftShadow(p, QRectF(m_card->geometry()).adjusted(0.5, 0.5, -0.5, -0.5),
                        m_card->cornerRadius(), 6.0, m_dark);
}

void EditorSettingsModal::mouseReleaseEvent(QMouseEvent* event) {
    if (m_card && !m_card->geometry().contains(event->pos())) {
        hide();
        emit closed();
    }
}

void EditorSettingsModal::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void EditorSettingsModal::tabletEvent(QTabletEvent* event) {
    event->ignore();
}

// ---------------- EditorLibraryModal ----------------

EditorLibraryModal::EditorLibraryModal(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_pal = editorPaletteFor(false);
    hide();

    m_card = new EditorGlassCard(this);
    m_card->setRole(EditorGlassCard::Role::ModalCard);
    m_card->setCornerRadius(18.0);
    m_card->setFixedSize(690, 560);

    m_navHost = new QWidget(m_card);
    m_navHost->setGeometry(0, 0, 176, 560);

    struct NavDef { const char* key; const char* title; const char* icon; const char* pack; };
    const NavDef defs[6] = {
        {"escritura", "Escritura", "pen", "ui"},
        {"figuras", "Figuras", "shapes", "ui"},
        {"elementos", "Elementos", "el", "ui"},
        {"paletas", "Paletas", "palette", "editor"},
        {"atajos", "Atajos 1–5", "bookmark", "editor"},
        {"descargados", "Descargados", "download", "ui"},
    };
    int y = 44;
    for (const auto& d : defs) {
        const QString key = QString::fromLatin1(d.key);
        auto* btn = new EditorChipButton(QString::fromUtf8(d.title), m_navHost);
        btn->setIcon(QString::fromLatin1(d.icon), QString::fromLatin1(d.pack));
        btn->setGeometry(10, y, 156, 32);
        btn->applyEditorTheme(m_dark);
        connect(btn, &EditorChipButton::clicked, this, [this, key] { openSection(key); });
        m_navBtns.append(btn);
        m_navKeys.append(key);
        y += 36;
    }

    m_closeBtn = new EditorIconButton(QStringLiteral("x"), QStringLiteral("ui"), m_card);
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setIconSizePx(14.0);
    m_closeBtn->move(690 - 40, 12);
    connect(m_closeBtn, &EditorIconButton::clicked, this, [this] {
        hide();
        emit closed();
    });

    m_scroll = new QScrollArea(m_card);
    m_scroll->setGeometry(178, 42, 690 - 186, 560 - 52);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(editorScrollAreaStyle(m_dark));

    m_bodyHost = new QWidget();
    m_bodyHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_bodyLayout = new QVBoxLayout(m_bodyHost);
    m_bodyLayout->setContentsMargins(16, 4, 20, 16);
    m_bodyLayout->setSpacing(10);
    m_scroll->setWidget(m_bodyHost);
}

void EditorLibraryModal::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    m_card->applyEditorTheme(dark);
    m_closeBtn->applyEditorTheme(dark);
    if (m_scroll)
        m_scroll->setStyleSheet(editorScrollAreaStyle(dark));
    m_navHost->setStyleSheet(QStringLiteral("background: %1; border-right: 1px solid %2; border-top-left-radius: 18px; border-bottom-left-radius: 18px;")
                                 .arg(m_pal.subtle.name(), m_pal.borderSoft.name(QColor::HexArgb)));
    for (auto* b : m_navBtns)
        b->applyEditorTheme(dark);
    if (isVisible())
        buildSectionContent();
    update();
}

void EditorLibraryModal::centerCard() {
    if (m_card)
        m_card->move(std::max(0, (width() - m_card->width()) / 2),
                     std::max(0, (height() - m_card->height()) / 2));
}

void EditorLibraryModal::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    centerCard();
}

void EditorLibraryModal::openSection(const QString& section) {
    QString key = section;
    if (key == QLatin1String("tips")) key = QStringLiteral("escritura");
    else if (key == QLatin1String("shapes")) key = QStringLiteral("figuras");
    else if (key == QLatin1String("elements")) key = QStringLiteral("elementos");
    m_currentSection = key.isEmpty() ? QStringLiteral("escritura") : key;
    centerCard();
    updateNavStyles();
    buildSectionContent();
    if (m_scroll && m_scroll->verticalScrollBar())
        m_scroll->verticalScrollBar()->setValue(0);
    show();
    raise();
    setFocus();
}

void EditorLibraryModal::setPresets(const QVector<PenPreset>& presets) {
    m_presets = presets;
    if (isVisible() && m_currentSection == QLatin1String("atajos"))
        buildSectionContent();
}

void EditorLibraryModal::syncFromCanvas(CanvasWidget* canvas) {
    m_canvas = canvas;
    if (isVisible())
        buildSectionContent();
}

void EditorLibraryModal::updateNavStyles() {
    for (int i = 0; i < m_navBtns.size(); ++i)
        m_navBtns.at(i)->setChecked(m_navKeys.at(i) == m_currentSection);
}

void EditorLibraryModal::buildSectionContent() {
    clearLayout(m_bodyLayout);
    const QString& key = m_currentSection;

    if (key == QStringLiteral("escritura")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Puntas de escritura"), m_pal, m_bodyHost));
        struct Nib { int tip; const char* name; };
        const Nib nibs[4] = {
            {1, "Pluma estilográfica"}, {0, "Bolígrafo técnico"},
            {2, "Lápiz grafito 2B"}, {3, "Tiza de pizarra"}
        };
        const QColor ink = m_canvas ? m_canvas->inkColor() : m_pal.text;
        for (const auto& n : nibs) {
            auto* row = new QWidget(m_bodyHost);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 4, 0, 4);
            auto* lbl = new QLabel(QString::fromUtf8(n.name), row);
            lbl->setFont(editorFont(12.0, QFont::DemiBold));
            lbl->setStyleSheet(QStringLiteral("color: %1;").arg(m_pal.text.name()));
            h->addWidget(lbl, 1);
            h->addWidget(new PenWavePreview(n.tip, ink, m_pal, row));
            auto* useBtn = new EditorChipButton(QStringLiteral("Usar"), row);
            useBtn->applyEditorTheme(m_dark);
            const int tipId = n.tip;
            connect(useBtn, &EditorChipButton::clicked, this, [this, tipId] {
                emit penTipSelected(tipId);
                hide();
                emit closed();
            });
            h->addWidget(useBtn);
            m_bodyLayout->addWidget(row);
        }
    } else if (key == QStringLiteral("figuras")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Sólidos 3D y figuras extendidas"), m_pal, m_bodyHost));
        auto* gridW = new QWidget(m_bodyHost);
        auto* grid = new QGridLayout(gridW);
        grid->setSpacing(8);
        struct SolidSpec { int kind; const char* name; const char* icon; };
        const SolidSpec solids[10] = {
            {0, "Cubo", "shp-cube"}, {1, "Prisma", "shp-prism"},
            {2, "Cilindro", "shp-cylinder"}, {3, "Cono", "shp-cone"},
            {4, "Esfera", "shp-sphere"}, {5, "Pirámide", "shp-pyramid"},
            {6, "Tetraedro", "shp-tetrahedron"}, {7, "Octaedro", "shp-octahedron"},
            {8, "Tronco de cono", "shp-frustum-cone"}, {9, "Tronco pirámide", "shp-frustum-pyramid"},
        };
        for (int i = 0; i < 10; ++i) {
            auto* btn = new EditorChipButton(QString::fromUtf8(solids[i].name), gridW);
            btn->setIcon(QString::fromLatin1(solids[i].icon), QStringLiteral("editor"));
            btn->applyEditorTheme(m_dark);
            const int k = solids[i].kind;
            connect(btn, &EditorChipButton::clicked, this, [this, k] {
                emit solidSelected(k);
                hide();
                emit closed();
            });
            grid->addWidget(btn, i / 3, i % 3);
        }
        m_bodyLayout->addWidget(gridW);
    } else if (key == QStringLiteral("elementos")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Paquetes de elementos"), m_pal, m_bodyHost));
        const QVector<StickerTemplate> all = allStickerTemplates();
        auto* gridW = new QWidget(m_bodyHost);
        auto* grid = new QGridLayout(gridW);
        grid->setSpacing(8);
        const ThemeConfig theme = m_canvas
            ? effectiveThemeConfig(m_canvas->themeId(), m_canvas->customBackground())
            : getThemeConfig(ThemeId::PaperLight);
        int count = 0;
        for (const auto& tpl : all) {
            if (count >= 18) break;
            auto* btn = new EditorChipButton(tpl.title, gridW);
            btn->applyEditorTheme(m_dark);
            const QString id = tpl.id;
            connect(btn, &EditorChipButton::clicked, this, [this, id] {
                emit stickerSelected(id);
                hide();
                emit closed();
            });
            grid->addWidget(btn, count / 3, count % 3);
            ++count;
        }
        Q_UNUSED(theme);
        m_bodyLayout->addWidget(gridW);
    } else if (key == QStringLiteral("paletas")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Paletas de colores"), m_pal, m_bodyHost));
        struct PalSpec { const char* name; ToolType tool; QVector<QColor> cols; };
        const QVector<PalSpec> pals = {
            {"Estudio Tinta (Pluma)", ToolType::Pen, {QColor("#18181B"), QColor("#2563EB"), QColor("#DC2626"), QColor("#16A34A"), QColor("#D97706"), QColor("#7C3AED")}},
            {"Clásica Escolar (Pluma)", ToolType::Pen, {QColor("#0F172A"), QColor("#1D4ED8"), QColor("#B91C1C"), QColor("#15803D"), QColor("#9333EA"), QColor("#EA580C")}},
            {"Pastel (Resaltador)", ToolType::Highlighter, {QColor("#FACC15"), QColor("#86EFAC"), QColor("#93C5FD"), QColor("#F9A8D4"), QColor("#FDBA74"), QColor("#C4B5FD")}},
            {"Alta Visibilidad (Láser)", ToolType::Laser, {QColor("#EF4444"), QColor("#22C55E"), QColor("#3B82F6"), QColor("#F59E0B"), QColor("#EC4899"), QColor("#8B5CF6")}},
        };
        for (const auto& ps : pals) {
            auto* btn = new EditorChipButton(QStringLiteral("Aplicar"), m_bodyHost);
            btn->applyEditorTheme(m_dark);
            const ToolType tt = ps.tool;
            const QVector<QColor> cols = ps.cols;
            connect(btn, &EditorChipButton::clicked, this, [this, tt, cols] {
                emit paletteApplied(tt, cols);
                hide();
                emit closed();
            });
            m_bodyLayout->addWidget(makeSettingRow(QString::fromUtf8(ps.name), btn, m_pal, m_bodyHost));
        }
    } else if (key == QStringLiteral("atajos")) {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Atajos 1–5"), m_pal, m_bodyHost));
        for (int i = 0; i < 5; ++i) {
            PenPreset pr;
            if (i < m_presets.size())
                pr = m_presets.at(i);
            else
                pr.index = i;
            auto* row = new QWidget(m_bodyHost);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 4, 0, 4);
            h->setSpacing(8);

            auto* badge = new EditorChipButton(QString::number(i + 1), row);
            badge->setDotColor(pr.color);
            badge->applyEditorTheme(m_dark);
            connect(badge, &EditorChipButton::clicked, this, [this, i] {
                emit selectPresetRequested(i);
                hide();
                emit closed();
            });
            h->addWidget(badge);

            const QString desc = QStringLiteral("%1 · %2 mm")
                                     .arg(pr.label.isEmpty() ? QStringLiteral("Atajo %1").arg(i + 1) : pr.label)
                                     .arg(pr.size, 0, 'f', 1);
            auto* lbl = new QLabel(desc, row);
            lbl->setFont(editorFont(12.0, QFont::Medium));
            lbl->setStyleSheet(QStringLiteral("color: %1;").arg(m_pal.text.name()));
            h->addWidget(lbl, 1);

            auto* saveBtn = new EditorChipButton(QStringLiteral("Guardar actual"), row);
            saveBtn->applyEditorTheme(m_dark);
            connect(saveBtn, &EditorChipButton::clicked, this, [this, i] {
                emit savePresetRequested(i);
            });
            h->addWidget(saveBtn);
            m_bodyLayout->addWidget(row);
        }
    } else {
        m_bodyLayout->addWidget(makeSectionHeading(QStringLiteral("Descargados"), m_pal, m_bodyHost));
        auto* note = new QLabel(QStringLiteral("+ Próximamente: paquetes y creaciones de la comunidad."), m_bodyHost);
        note->setFont(editorFont(12.0, QFont::Medium));
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: %1; padding: 14px; border: 1px dashed %2; border-radius: 12px;")
                                .arg(m_pal.muted.name(), m_pal.border.name(QColor::HexArgb)));
        m_bodyLayout->addWidget(note);
    }

    m_bodyLayout->addStretch(1);
}

void EditorLibraryModal::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), m_pal.veil);
    if (m_card && m_card->isVisible())
        paintSoftShadow(p, QRectF(m_card->geometry()).adjusted(0.5, 0.5, -0.5, -0.5),
                        m_card->cornerRadius(), 6.0, m_dark);
}

void EditorLibraryModal::mouseReleaseEvent(QMouseEvent* event) {
    if (m_card && !m_card->geometry().contains(event->pos())) {
        hide();
        emit closed();
    }
}

void EditorLibraryModal::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void EditorLibraryModal::tabletEvent(QTabletEvent* event) {
    event->ignore();
}

// ---------------- EditorLibraryPickerModal ----------------

EditorLibraryPickerModal::EditorLibraryPickerModal(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_pal = editorPaletteFor(false);
    hide();

    m_card = new EditorGlassCard(this);
    m_card->setRole(EditorGlassCard::Role::ModalCard);
    m_card->setCornerRadius(18.0);
    m_card->setFixedSize(540, 460);

    m_titleLbl = new QLabel(QStringLiteral("Abrir cuaderno o pizarra"), m_card);
    m_titleLbl->setFont(editorFont(15.0, QFont::Bold));
    m_titleLbl->move(20, 14);

    m_closeBtn = new EditorIconButton(QStringLiteral("x"), QStringLiteral("ui"), m_card);
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setIconSizePx(14.0);
    m_closeBtn->move(540 - 40, 12);
    connect(m_closeBtn, &EditorIconButton::clicked, this, [this] {
        hide();
        emit closed();
    });

    m_searchEdit = new EditorLineEdit(m_card);
    m_searchEdit->setPlaceholderText(QStringLiteral("Buscar por nombre o carpeta…"));
    m_searchEdit->setGeometry(20, 48, 500, 32);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { rebuildList(); });

    m_filterSeg = new EditorSegControl(m_card);
    m_filterSeg->setOptions({QStringLiteral("Todos"), QStringLiteral("Cuadernos"),
                             QStringLiteral("Pizarras"), QStringLiteral("PDFs")});
    m_filterSeg->setGeometry(20, 88, 500, 30);
    connect(m_filterSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        m_filter = idx;
        rebuildList();
    });

    m_scroll = new QScrollArea(m_card);
    m_scroll->setGeometry(14, 126, 512, 460 - 140);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    m_listHost = new QWidget();
    m_listHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(6, 4, 10, 10);
    m_listLayout->setSpacing(6);
    m_scroll->setWidget(m_listHost);

    applyEditorTheme(false);
}

void EditorLibraryPickerModal::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    if (m_card) m_card->applyEditorTheme(dark);
    if (m_closeBtn) m_closeBtn->applyEditorTheme(dark);
    if (m_searchEdit) m_searchEdit->applyEditorTheme(dark);
    if (m_filterSeg) m_filterSeg->applyEditorTheme(dark);
    if (m_titleLbl)
        m_titleLbl->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(m_pal.text.name()));
    if (isVisible())
        rebuildList();
    update();
}

void EditorLibraryPickerModal::centerCard() {
    if (m_card)
        m_card->move(std::max(0, (width() - m_card->width()) / 2),
                     std::max(0, (height() - m_card->height()) / 2));
}

void EditorLibraryPickerModal::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    centerCard();
}

void EditorLibraryPickerModal::openPicker(const QStringList& openPaths) {
    m_openPaths = openPaths;
    m_docs = LibraryStore::listAllNotebooksRecursive();
    std::sort(m_docs.begin(), m_docs.end(), [](const NotebookMeta& a, const NotebookMeta& b) {
        return a.modified > b.modified;
    });
    if (m_searchEdit) {
        QSignalBlocker blk(m_searchEdit);
        m_searchEdit->clear();
    }
    m_filter = 0;
    if (m_filterSeg) {
        QSignalBlocker blk(m_filterSeg);
        m_filterSeg->setCurrentIndex(0);
    }
    rebuildList();
    centerCard();
    show();
    raise();
    if (m_searchEdit)
        m_searchEdit->setFocus();
}

void EditorLibraryPickerModal::rebuildList() {
    if (!m_listLayout || !m_listHost)
        return;
    clearLayout(m_listLayout);

    const QString query = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();
    int shown = 0;
    for (const NotebookMeta& doc : m_docs) {
        const bool isBoard = (!doc.hasPdf && doc.pageFormat == PageFormat::Infinite);
        const bool isBook = (!doc.hasPdf && doc.pageFormat != PageFormat::Infinite);
        if (m_filter == 1 && !isBook) continue;
        if (m_filter == 2 && !isBoard) continue;
        if (m_filter == 3 && !doc.hasPdf) continue;

        const QString title = doc.title.isEmpty() ? QFileInfo(doc.path).completeBaseName() : doc.title;
        const QString folder = doc.folderRelativePath.isEmpty() ? QStringLiteral("Biblioteca") : doc.folderRelativePath;
        if (!query.isEmpty() && !title.toLower().contains(query) && !folder.toLower().contains(query))
            continue;

        const QString canon = QFileInfo(doc.path).canonicalFilePath();
        const bool alreadyOpen = m_openPaths.contains(canon.isEmpty() ? doc.path : canon);
        const QString icon = doc.hasPdf ? QStringLiteral("pdf")
                                        : (isBoard ? QStringLiteral("board") : QStringLiteral("book"));
        QString detail = folder;
        if (!isBoard)
            detail += QStringLiteral(" · %1 pág.").arg(std::max(1, doc.pages));
        if (doc.modified.isValid())
            detail += QStringLiteral(" · ") + doc.modified.toString(QStringLiteral("dd/MM/yy"));
        if (alreadyOpen)
            detail += QStringLiteral(" · Abierto");

        auto* rowBtn = new EditorChipButton(title + QStringLiteral("  —  ") + detail, m_listHost);
        rowBtn->setIcon(icon, QStringLiteral("ui"));
        rowBtn->setChecked(alreadyOpen);
        rowBtn->setFixedHeight(36);
        rowBtn->applyEditorTheme(m_dark);
        const QString path = doc.path;
        connect(rowBtn, &EditorChipButton::clicked, this, [this, path] {
            hide();
            emit closed();
            emit notebookSelected(path);
        });
        m_listLayout->addWidget(rowBtn);
        ++shown;
    }

    if (shown == 0) {
        auto* empty = new QLabel(
            m_docs.isEmpty() ? QStringLiteral("Aún no hay cuadernos guardados en tu biblioteca.")
                             : QStringLiteral("Ningún documento coincide con el filtro."),
            m_listHost);
        empty->setFont(editorFont(12.0, QFont::Medium));
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QStringLiteral("color: %1; padding: 28px; background: transparent;").arg(m_pal.muted.name()));
        m_listLayout->addWidget(empty);
    }

    m_listLayout->addStretch(1);
}

void EditorLibraryPickerModal::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), m_pal.veil);
    if (m_card && m_card->isVisible())
        paintSoftShadow(p, QRectF(m_card->geometry()).adjusted(0.5, 0.5, -0.5, -0.5),
                        m_card->cornerRadius(), 6.0, m_dark);
}

void EditorLibraryPickerModal::mouseReleaseEvent(QMouseEvent* event) {
    if (m_card && !m_card->geometry().contains(event->pos())) {
        hide();
        emit closed();
    }
}

void EditorLibraryPickerModal::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void EditorLibraryPickerModal::tabletEvent(QTabletEvent* event) {
    event->ignore();
}

} // namespace finily
