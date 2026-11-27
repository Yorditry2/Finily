#include "ui/editor/EditorChrome.h"

#include "ui/CanvasWidget.h"
#include "ui/UiIcons.h"

#include <QEventLoop>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace finily {

namespace {

struct ToolSpec {
    ToolType tool;
    const char* icon;
    bool hasSub;
    bool sepBefore;
    bool isElements = false;
};

const QVector<ToolSpec>& canonicalTools() {
    static const QVector<ToolSpec> list = {
        {ToolType::Select, "sel", false, false},
        {ToolType::Hand, "hand", false, false},
        {ToolType::Pen, "pen", true, true},
        {ToolType::Highlighter, "hl", true, false},
        {ToolType::Eraser, "er", true, false},
        {ToolType::Fill, "fill", true, false},
        {ToolType::Lasso, "lasso", true, true},
        {ToolType::Shape, "shapes", true, true},
        {ToolType::Text, "text", false, false},
        {ToolType::Select, "el", false, false, true},
        {ToolType::Laser, "laser", true, false},
    };
    return list;
}

bool toolUsesWidths(ToolType t) {
    return t == ToolType::Pen || t == ToolType::Ballpoint || t == ToolType::Highlighter ||
           t == ToolType::Eraser || t == ToolType::Shape || t == ToolType::Polyline;
}

bool toolUsesColors(ToolType t) {
    return t == ToolType::Pen || t == ToolType::Ballpoint || t == ToolType::Highlighter ||
           t == ToolType::Fill || t == ToolType::Shape || t == ToolType::Polyline ||
           t == ToolType::Text || t == ToolType::Laser;
}

bool isPenGroup(ToolType t) {
    return t == ToolType::Pen || t == ToolType::Ballpoint;
}

bool isShapeGroup(ToolType t) {
    return t == ToolType::Shape || t == ToolType::Polyline;
}

qreal pillHeightForTool(ToolType t, double mm) {
    if (t == ToolType::Highlighter)
        return std::clamp(mm * 0.45, 2.5, 8.0);
    if (t == ToolType::Eraser)
        return std::clamp(mm * 0.28, 2.5, 8.0);
    return std::clamp(mm * 1.15, 2.0, 8.0);
}

ToolbarDockMode dockModeFromGlobalDrag(const QWidget* topLevelRef, const QPoint& globalPos) {
    if (!topLevelRef)
        return ToolbarDockMode::Top;
    const QPoint local = topLevelRef->mapFromGlobal(globalPos);
    const QSize sz = topLevelRef->size();
    if (local.x() < std::min(160, sz.width() / 4) && local.y() > 54 && local.y() < sz.height() - 120)
        return ToolbarDockMode::Left;
    if (local.y() > sz.height() - std::min(150, sz.height() / 4))
        return ToolbarDockMode::Float;
    return ToolbarDockMode::Top;
}

} // namespace

// ---------------- EditorDropdownMenu ----------------

EditorDropdownMenu::EditorDropdownMenu(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::PopoverSolid);
    setCornerRadius(12.0);
    setMouseTracking(true);
    hide();
}

void EditorDropdownMenu::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
}

QVector<QRectF> EditorDropdownMenu::rowRects() const {
    QVector<QRectF> rects;
    rects.reserve(m_items.size());
    qreal y = 6.0;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).separatorBefore && i > 0)
            y += 9.0;
        rects.append(QRectF(6.0, y, m_menuWidth - 12.0, 32.0));
        y += 33.0;
    }
    return rects;
}

int EditorDropdownMenu::totalHeight() const {
    qreal y = 6.0;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).separatorBefore && i > 0)
            y += 9.0;
        y += 33.0;
    }
    return int(std::ceil(y + 6.0));
}

void EditorDropdownMenu::openMenu(const QVector<EditorDropdownItem>& items,
                                  const QPoint& posInParent,
                                  const QSize& bounds,
                                  std::function<void(int id)> callback,
                                  int minWidth) {
    m_items = items;
    m_callback = std::move(callback);
    m_hoverIndex = -1;

    const QFontMetricsF fm(editorFont(12.0, QFont::Medium));
    qreal reqW = minWidth;
    for (const auto& it : m_items) {
        qreal w = 24.0 + std::ceil(fm.horizontalAdvance(it.label));
        if (!it.iconName.isEmpty())
            w += 24.0;
        if (!it.shortcut.isEmpty())
            w += 22.0 + std::ceil(fm.horizontalAdvance(it.shortcut));
        else if (it.checked)
            w += 22.0;
        reqW = std::max(reqW, w);
    }
    m_menuWidth = std::clamp(int(std::ceil(reqW)), minWidth, std::max(minWidth, bounds.width() - 24));
    const int h = std::min(totalHeight(), std::max(100, bounds.height() - 24));
    setFixedSize(m_menuWidth, h);

    int x = posInParent.x();
    int y = posInParent.y();
    if (x + m_menuWidth > bounds.width() - 10)
        x = bounds.width() - m_menuWidth - 10;
    if (y + h > bounds.height() - 10)
        y = posInParent.y() - h - 6;
    move(std::max(10, x), std::max(10, y));
    show();
    raise();
    update();
}

void EditorDropdownMenu::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QVector<QRectF> rects = rowRects();
    for (int i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items.at(i);
        const QRectF& rr = rects.at(i);

        if (item.separatorBefore && i > 0) {
            p.setPen(QPen(m_pal.borderSoft, 1.0));
            p.drawLine(QPointF(12.0, rr.top() - 5.0), QPointF(width() - 12.0, rr.top() - 5.0));
        }

        if (i == m_hoverIndex && item.enabled) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pal.hover);
            p.drawRoundedRect(rr, 8.0, 8.0);
        }

        QColor fg = item.danger ? m_pal.danger : m_pal.text;
        if (!item.enabled)
            fg.setAlphaF(0.4);

        qreal tx = rr.left() + 10.0;
        if (!item.iconName.isEmpty()) {
            const QRectF ir(tx, rr.center().y() - 7.5, 15.0, 15.0);
            paintUiIcon(&p, ir, item.iconName, item.danger ? m_pal.danger : m_pal.icon, item.iconPack);
            tx += 23.0;
        }

        qreal rightEdge = rr.right() - 10.0;
        if (item.checked) {
            const QRectF cr(rightEdge - 14.0, rr.center().y() - 7.0, 14.0, 14.0);
            paintUiIcon(&p, cr, QStringLiteral("check"), m_pal.accent, QStringLiteral("ui"));
            rightEdge -= 20.0;
        } else if (!item.shortcut.isEmpty()) {
            p.setFont(editorFont(10.5, QFont::Medium));
            p.setPen(m_pal.muted);
            const QRectF sr(rr.right() - 80.0, rr.top(), 70.0, rr.height());
            p.drawText(sr, Qt::AlignRight | Qt::AlignVCenter, item.shortcut);
            rightEdge -= 64.0;
        }

        p.setFont(editorFont(12.0, item.checked ? QFont::DemiBold : QFont::Medium));
        p.setPen(fg);
        p.drawText(QRectF(tx, rr.top(), std::max<qreal>(40.0, rightEdge - tx), rr.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, item.label);
    }
}

void EditorDropdownMenu::mouseMoveEvent(QMouseEvent* event) {
    const QVector<QRectF> rects = rowRects();
    int hit = -1;
    for (int i = 0; i < rects.size(); ++i) {
        if (rects.at(i).contains(event->position()) && m_items.at(i).enabled) {
            hit = i;
            break;
        }
    }
    if (hit != m_hoverIndex) {
        m_hoverIndex = hit;
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorDropdownMenu::leaveEvent(QEvent*) {
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        update();
    }
}

void EditorDropdownMenu::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    const QVector<QRectF> rects = rowRects();
    for (int i = 0; i < rects.size(); ++i) {
        if (rects.at(i).contains(event->position()) && m_items.at(i).enabled) {
            auto cb = std::move(m_callback);
            const int id = m_items.at(i).id;
            hide();
            emit menuClosed();
            if (cb)
                cb(id);
            return;
        }
    }
}

// ---------------- EditorToast ----------------

EditorToast::EditorToast(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::ContextCapsule);
    setCornerRadius(12.0);
    setMouseTracking(true);
    hide();
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] {
        hide();
        emit visibilityChanged();
    });
}

void EditorToast::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
}

void EditorToast::showToast(const QString& msg, bool withUndo, int durationMs) {
    m_msg = msg;
    m_withUndo = withUndo;
    m_hoverUndo = false;
    const QFontMetricsF fm(editorFont(12.0, QFont::Medium));
    const int textW = int(std::ceil(fm.horizontalAdvance(msg)));
    const int w = std::clamp(textW + (withUndo ? 114 : 36), 140, 520);
    setFixedSize(w, 36);
    show();
    raise();
    emit visibilityChanged();
    update();
    if (durationMs > 0)
        m_timer->start(durationMs);
}

QRectF EditorToast::undoRect() const {
    return QRectF(width() - 82.0, 5.0, 74.0, height() - 10.0);
}

void EditorToast::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(12.0, QFont::Medium));
    p.setPen(m_pal.text);
    const QRectF textR(14.0, 0.0, width() - (m_withUndo ? 102.0 : 28.0), height());
    p.drawText(textR, Qt::AlignLeft | Qt::AlignVCenter, m_msg);

    if (m_withUndo) {
        const QRectF ur = undoRect();
        p.setPen(Qt::NoPen);
        p.setBrush(m_hoverUndo ? m_pal.accent : m_pal.subtle);
        p.drawRoundedRect(ur, 7.0, 7.0);
        p.setFont(editorFont(11.5, QFont::DemiBold));
        p.setPen(m_hoverUndo ? m_pal.accentFg : m_pal.text);
        p.drawText(ur, Qt::AlignCenter, QStringLiteral("Deshacer"));
    }
}

void EditorToast::mouseMoveEvent(QMouseEvent* event) {
    const bool over = m_withUndo && undoRect().contains(event->position());
    if (over != m_hoverUndo) {
        m_hoverUndo = over;
        setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorToast::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_withUndo && undoRect().contains(event->position())) {
        m_timer->stop();
        hide();
        emit visibilityChanged();
        emit undoRequested();
    }
}

// ---------------- EditorConfirmCard ----------------

EditorConfirmCard::EditorConfirmCard(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::ModalCard);
    setCornerRadius(18.0);
    setFixedSize(376, 174);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    hide();
}

void EditorConfirmCard::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
}

QRectF EditorConfirmCard::btnRect(int idx) const {
    const qreal y = height() - 48.0;
    if (idx == 0)
        return QRectF(18.0, y, 88.0, 32.0);
    if (idx == 1)
        return QRectF(width() - 196.0, y, 88.0, 32.0);
    return QRectF(width() - 100.0, y, 82.0, 32.0);
}

int EditorConfirmCard::execModal(QWidget* overlayParent, const QString& title, const QString& body) {
    if (!overlayParent)
        return 0;
    m_title = title;
    m_body = body;
    m_hoverBtn = -1;
    m_result = 0;

    setParent(overlayParent);
    move((overlayParent->width() - width()) / 2, (overlayParent->height() - height()) / 2);
    show();
    raise();
    setFocus();
    emit visibilityChanged();
    update();

    QEventLoop loop;
    auto conn = connect(this, &EditorConfirmCard::visibilityChanged, &loop, [&] {
        if (!isVisible())
            loop.quit();
    });
    loop.exec();
    disconnect(conn);
    return m_result;
}

void EditorConfirmCard::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setFont(editorFont(14.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(20.0, 18.0, width() - 40.0, 24.0), Qt::AlignLeft | Qt::AlignVCenter, m_title);

    p.setFont(editorFont(12.0, QFont::Normal));
    p.setPen(m_pal.muted);
    p.drawText(QRectF(20.0, 48.0, width() - 40.0, 56.0),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_body);

    const QString labels[3] = {QStringLiteral("Cancelar"), QStringLiteral("Descartar"), QStringLiteral("Guardar")};
    for (int i = 0; i < 3; ++i) {
        const QRectF r = btnRect(i);
        p.setPen(Qt::NoPen);
        if (i == 2)
            p.setBrush(m_pal.accent);
        else if (i == 1)
            p.setBrush(m_hoverBtn == i ? QColor(m_pal.danger.red(), m_pal.danger.green(), m_pal.danger.blue(), 42)
                                       : QColor(m_pal.danger.red(), m_pal.danger.green(), m_pal.danger.blue(), 22));
        else
            p.setBrush(m_hoverBtn == i ? m_pal.hover : m_pal.subtle);
        p.drawRoundedRect(r, 9.0, 9.0);

        p.setFont(editorFont(11.5, QFont::DemiBold));
        p.setPen(i == 2 ? m_pal.accentFg : (i == 1 ? m_pal.danger : m_pal.text));
        p.drawText(r, Qt::AlignCenter, labels[i]);
    }
}

void EditorConfirmCard::mouseMoveEvent(QMouseEvent* event) {
    int h = -1;
    for (int i = 0; i < 3; ++i) {
        if (btnRect(i).contains(event->position())) {
            h = i;
            break;
        }
    }
    if (h != m_hoverBtn) {
        m_hoverBtn = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorConfirmCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < 3; ++i) {
        if (btnRect(i).contains(event->position())) {
            m_result = (i == 2) ? 1 : (i == 1 ? 2 : 0);
            hide();
            emit visibilityChanged();
            return;
        }
    }
}

void EditorConfirmCard::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        m_result = 0;
        hide();
        emit visibilityChanged();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        m_result = 1;
        hide();
        emit visibilityChanged();
        return;
    }
    EditorGlassCard::keyPressEvent(event);
}

// ---------------- EditorTopBar ----------------

EditorTopBar::EditorTopBar(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::BarGlass);
    setCornerRadius(0.0);
    setFixedHeight(84);
    setMouseTracking(true);

    m_renameEdit = new EditorLineEdit(this);
    m_renameEdit->hide();
    m_renameEdit->installEventFilter(this);
    connect(m_renameEdit, &QLineEdit::editingFinished, this, [this] { finishInlineRename(true); });
    connect(m_renameEdit, &QLineEdit::returnPressed, this, [this] { finishInlineRename(true); });
}

bool EditorTopBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_renameEdit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            finishInlineRename(false);
            return true;
        }
    }
    return EditorGlassCard::eventFilter(watched, event);
}

void EditorTopBar::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    if (m_renameEdit)
        m_renameEdit->applyEditorTheme(dark);
}

void EditorTopBar::setDockMode(ToolbarDockMode mode) {
    if (m_dockMode == mode)
        return;
    m_dockMode = mode;
    setFixedHeight(mode == ToolbarDockMode::Top ? 84 : 38);
    rebuildHits();
    update();
}

void EditorTopBar::setTabs(const QVector<EditorTabItem>& tabs) {
    m_tabs = tabs;
    rebuildHits();
    update();
}

void EditorTopBar::setElementsPanelOpen(bool open) {
    if (m_elementsOpen == open)
        return;
    m_elementsOpen = open;
    update();
}

void EditorTopBar::syncFromCanvas(CanvasWidget* canvas) {
    if (!canvas)
        return;
    m_activeTool = canvas->tool();
    m_canUndo = canvas->canUndo();
    m_canRedo = canvas->canRedo();
    m_zoomPercent = std::max(10, qRound(canvas->viewport().zoom * 100.0));
    m_pageCurrent = canvas->currentPageIndex() + 1;
    m_pageCount = std::max(1, canvas->pageCount());
    m_infinitePage = (canvas->pageFormat() == PageFormat::Infinite);
    m_darkSheet = canvas->isCurrentSheetDark();

    m_widths = canvas->toolWidths(m_activeTool);
    m_activeWidthSlot = canvas->activeWidthSlot(m_activeTool);
    m_showWidths = toolUsesWidths(m_activeTool);

    m_colors = canvas->toolPalette(m_activeTool);
    m_activeColorIdx = canvas->activeColorIndex(m_activeTool);
    m_showColors = toolUsesColors(m_activeTool);

    const auto penPal = canvas->toolPalette(ToolType::Pen);
    const int penIdx = canvas->activeColorIndex(ToolType::Pen);
    m_penNibColor = penPal.isEmpty()
        ? resolveAdaptableInkColor(canvas->inkColor(), m_darkSheet)
        : resolveAdaptableInkColor(penPal.at(std::clamp(penIdx, 0, int(penPal.size()) - 1)), m_darkSheet);

    const auto hlPal = canvas->toolPalette(ToolType::Highlighter);
    const int hlIdx = canvas->activeColorIndex(ToolType::Highlighter);
    m_hlBarColor = hlPal.isEmpty()
        ? QColor("#FACC15")
        : hlPal.at(std::clamp(hlIdx, 0, int(hlPal.size()) - 1));

    rebuildHits();
    update();
}

void EditorTopBar::resizeEvent(QResizeEvent* event) {
    EditorGlassCard::resizeEvent(event);
    rebuildHits();
}

void EditorTopBar::rebuildHits() {
    m_hits.clear();
    const int W = width();
    if (W <= 60)
        return;

    // Fila 1 (38 px): Inicio + pestañas + (+) + controles derecha (si Left/Float) + Pantalla completa
    m_hits.append({QRectF(8, 5, 28, 28), HitKind::Home, 0, ToolType::Pen, false, QStringLiteral("home"), {}});

    int rightUsed = 40; // fullscreen button on right
    if (m_dockMode != ToolbarDockMode::Top) {
        rightUsed += 28 + 28 + 9; // undo + redo + div
        if (!m_infinitePage)
            rightUsed += 56; // pages pill
        rightUsed += 28 + 86 + 28 + 28 + 12; // paper + zoom(-,%,+) + export + gear + div
    }

    const int tabStartX = 44;
    const int tabMaxEnd = std::max(tabStartX + 120, W - rightUsed - 38);
    const int nTabs = std::max(1, int(m_tabs.size()));
    const int availForTabs = std::max(80, tabMaxEnd - tabStartX);
    const int tabW = std::clamp((availForTabs - (nTabs - 1) * 3) / nTabs, 92, 188);

    int tx = tabStartX;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const QRectF tr(tx, 6, tabW, 32);
        m_hits.append({tr, HitKind::Tab, i, ToolType::Pen, false, {}, m_tabs.at(i).title});
        m_hits.append({QRectF(tr.right() - 22, tr.center().y() - 8, 16, 16), HitKind::TabClose, i, ToolType::Pen, false, {}, {}});
        tx += tabW + 3;
    }
    m_hits.append({QRectF(tx + 3, 5, 28, 28), HitKind::NewTab, 0, ToolType::Pen, false, QStringLiteral("plus"), {}});

    int rx = W - 36;
    m_hits.append({QRectF(rx, 4, 30, 30), HitKind::Fullscreen, 0, ToolType::Pen, false, QStringLiteral("fit"), {}});

    if (m_dockMode != ToolbarDockMode::Top) {
        rx -= 10;
        rx -= 28; m_hits.append({QRectF(rx, 5, 28, 28), HitKind::Gear, 0, ToolType::Pen, false, QStringLiteral("gear"), {}});
        rx -= 30; m_hits.append({QRectF(rx, 5, 28, 28), HitKind::Export, 0, ToolType::Pen, false, QStringLiteral("export"), {}});
        rx -= 6;
        rx -= 26; m_hits.append({QRectF(rx, 5, 26, 28), HitKind::ZoomIn, 0, ToolType::Pen, false, QStringLiteral("plus"), {}});
        rx -= 46; m_hits.append({QRectF(rx, 5, 46, 28), HitKind::ZoomReset, 0, ToolType::Pen, false, {}, QStringLiteral("%1 %").arg(m_zoomPercent)});
        rx -= 26; m_hits.append({QRectF(rx, 5, 26, 28), HitKind::ZoomOut, 0, ToolType::Pen, false, QStringLiteral("minus"), {}});
        rx -= 6;
        rx -= 28; m_hits.append({QRectF(rx, 5, 28, 28), HitKind::Paper, 0, ToolType::Pen, false, QStringLiteral("paper"), {}});
        if (!m_infinitePage) {
            rx -= 58;
            m_hits.append({QRectF(rx, 5, 56, 28), HitKind::Pages, 0, ToolType::Pen, false, QStringLiteral("pages"),
                           QStringLiteral("%1/%2").arg(m_pageCurrent).arg(m_pageCount)});
        }
        rx -= 10;
        rx -= 28; m_hits.append({QRectF(rx, 5, 28, 28), HitKind::Redo, 0, ToolType::Pen, false, QStringLiteral("redo"), {}});
        rx -= 30; m_hits.append({QRectF(rx, 5, 28, 28), HitKind::Undo, 0, ToolType::Pen, false, QStringLiteral("undo"), {}});
    }

    if (m_dockMode != ToolbarDockMode::Top)
        return;

    // Fila 2 (y = 38..84, alto 46 px)
    const qreal y2 = 44.0;
    m_hits.append({QRectF(10, y2, 34, 34), HitKind::Undo, 0, ToolType::Pen, false, QStringLiteral("undo"), {}});
    m_hits.append({QRectF(46, y2, 34, 34), HitKind::Redo, 0, ToolType::Pen, false, QStringLiteral("redo"), {}});

    // Calcular bloque derecho primero para centrar la barra de herramientas sin solapar
    int r2x = W - 10;
    r2x -= 34; m_hits.append({QRectF(r2x, y2, 34, 34), HitKind::Gear, 0, ToolType::Pen, false, QStringLiteral("gear"), {}});
    r2x -= 36; m_hits.append({QRectF(r2x, y2, 34, 34), HitKind::Export, 0, ToolType::Pen, false, QStringLiteral("export"), {}});
    r2x -= 9; // vdiv
    r2x -= 28; m_hits.append({QRectF(r2x, y2 + 3, 28, 28), HitKind::ZoomIn, 0, ToolType::Pen, false, QStringLiteral("plus"), {}});
    r2x -= 48; m_hits.append({QRectF(r2x, y2 + 3, 46, 28), HitKind::ZoomReset, 0, ToolType::Pen, false, {}, QStringLiteral("%1 %").arg(m_zoomPercent)});
    r2x -= 30; m_hits.append({QRectF(r2x, y2 + 3, 28, 28), HitKind::ZoomOut, 0, ToolType::Pen, false, QStringLiteral("minus"), {}});
    r2x -= 9; // vdiv
    r2x -= 34; m_hits.append({QRectF(r2x, y2, 34, 34), HitKind::Paper, 0, ToolType::Pen, false, QStringLiteral("paper"), {}});
    if (!m_infinitePage) {
        r2x -= 64;
        m_hits.append({QRectF(r2x, y2 + 3, 60, 28), HitKind::Pages, 0, ToolType::Pen, false, QStringLiteral("pages"),
                       QStringLiteral("%1 / %2").arg(m_pageCurrent).arg(m_pageCount)});
    }

    // Calcular ancho del grupo central
    const auto& tools = canonicalTools();
    int centerW = 14; // grip
    for (const auto& t : tools) {
        if (t.sepBefore)
            centerW += 9;
        centerW += 36;
    }
    if (m_showWidths)
        centerW += 9 + 3 * 31;
    if (m_showColors)
        centerW += 9 + int(m_colors.size()) * 24 + 24;

    const int leftBound = 94;
    const int rightBound = r2x - 10;
    const int cx = std::clamp((W - centerW) / 2, leftBound, std::max(leftBound, rightBound - centerW));

    qreal x = cx;
    m_hits.append({QRectF(x, y2 + 5, 12, 24), HitKind::Grip, 0, ToolType::Pen, false, {}, {}});
    x += 14;

    for (const auto& t : tools) {
        if (t.sepBefore)
            x += 9;
        m_hits.append({QRectF(x, y2, 34, 34),
                       t.isElements ? HitKind::Elements : HitKind::Tool,
                       0, t.tool, t.hasSub, QString::fromLatin1(t.icon), {}});
        x += 36;
    }

    if (m_showWidths) {
        x += 9;
        for (int i = 0; i < 3; ++i) {
            m_hits.append({QRectF(x, y2 + 3, 30, 28), HitKind::WidthSlot, i, m_activeTool, false, {}, {}});
            x += 31;
        }
    }

    if (m_showColors) {
        x += 9;
        for (int i = 0; i < m_colors.size(); ++i) {
            m_hits.append({QRectF(x, y2 + 7, 20, 20), HitKind::ColorSwatch, i, m_activeTool, false, {}, {}});
            x += 24;
        }
        m_hits.append({QRectF(x, y2 + 7, 20, 20), HitKind::ColorWheel, 0, m_activeTool, false, {}, {}});
    }
}

int EditorTopBar::hitIndexAt(const QPointF& pos) const {
    // Check TabClose before Tab so the close cross wins inside active/hovered tab
    for (int i = 0; i < m_hits.size(); ++i) {
        if (m_hits.at(i).kind == HitKind::TabClose && m_hits.at(i).rect.contains(pos))
            return i;
    }
    for (int i = 0; i < m_hits.size(); ++i) {
        if (m_hits.at(i).rect.contains(pos))
            return i;
    }
    return -1;
}

void EditorTopBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Fondo vidrio translúcido + fila 1 de pestañas
    p.fillRect(rect(), m_pal.surfaceGlass);
    p.fillRect(QRectF(0, 0, width(), 38), m_pal.tabsBg);
    p.setPen(QPen(m_pal.borderSoft, 1.0));
    p.drawLine(QPointF(0, 37.5), QPointF(width(), 37.5));
    if (m_dockMode == ToolbarDockMode::Top) {
        p.setPen(QPen(m_pal.border, 1.0));
        p.drawLine(QPointF(0, height() - 0.5), QPointF(width(), height() - 0.5));
    }

    // Dibujar separadores verticales de fila 2 (sobre la línea media de botones)
    if (m_dockMode == ToolbarDockMode::Top) {
        p.setPen(QPen(m_pal.border, 1.0));
        for (int i = 1; i < m_hits.size(); ++i) {
            const auto& prev = m_hits.at(i - 1);
            const auto& cur = m_hits.at(i);
            if (cur.rect.top() > 38.0 && prev.rect.top() > 38.0 && (cur.rect.left() - prev.rect.right()) >= 7.0) {
                const qreal dx = std::round((prev.rect.right() + cur.rect.left()) * 0.5) + 0.5;
                p.drawLine(QPointF(dx, 51.0), QPointF(dx, 71.0));
            }
        }
    }

    for (int i = 0; i < m_hits.size(); ++i) {
        const auto& h = m_hits.at(i);
        const bool hovered = (i == m_hoverHit);

        if (h.kind == HitKind::Tab) {
            const auto& tab = m_tabs.at(h.index);
            const bool active = tab.active;
            const bool tabHovered = hovered || (m_hoverHit >= 0 && m_hits.at(m_hoverHit).kind == HitKind::TabClose && m_hits.at(m_hoverHit).index == h.index);
            QPainterPath path;
            const qreal r = 10.0;
            const QRectF b = h.rect;
            path.moveTo(b.left(), b.bottom() + 1.0);
            path.lineTo(b.left(), b.top() + r);
            path.quadTo(b.left(), b.top(), b.left() + r, b.top());
            path.lineTo(b.right() - r, b.top());
            path.quadTo(b.right(), b.top(), b.right(), b.top() + r);
            path.lineTo(b.right(), b.bottom() + 1.0);
            if (active) {
                p.setPen(QPen(m_pal.border, 1.0));
                p.setBrush(m_pal.surfaceSolid);
                p.drawPath(path);
            } else if (tabHovered) {
                p.setPen(Qt::NoPen);
                p.setBrush(m_pal.hover);
                p.drawPath(path);
            }

            const QString docIcon = tab.isPdf ? QStringLiteral("pdf")
                                  : (tab.isBoard ? QStringLiteral("board") : QStringLiteral("book"));
            paintUiIcon(&p, QRectF(b.left() + 10.0, b.center().y() - 7.0, 14.0, 14.0),
                        docIcon, active ? m_pal.icon : m_pal.muted);

            qreal rightReserve = 12.0;
            if (active || tabHovered)
                rightReserve += 18.0;
            if (tab.dirty)
                rightReserve += 10.0;

            p.setFont(editorFont(11.5, active ? QFont::DemiBold : QFont::Medium));
            p.setPen(active ? m_pal.text : m_pal.muted);
            const QRectF titleRect(b.left() + 29.0, b.top(), std::max<qreal>(20.0, b.width() - 29.0 - rightReserve), b.height());
            const QString elided = QFontMetricsF(p.font()).elidedText(tab.title, Qt::ElideRight, titleRect.width());
            p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, elided);

            if (tab.dirty) {
                p.setPen(Qt::NoPen);
                p.setBrush(m_pal.unsavedAmber);
                const qreal dotX = b.right() - ((active || tabHovered) ? 31.0 : 14.0);
                p.drawEllipse(QPointF(dotX, b.center().y()), 3.0, 3.0);
            }
            continue;
        }

        if (h.kind == HitKind::TabClose) {
            const auto& tab = m_tabs.at(h.index);
            const bool parentHovered = (m_hoverHit >= 0 && m_hits.at(m_hoverHit).kind == HitKind::Tab && m_hits.at(m_hoverHit).index == h.index);
            if (tab.active || hovered || parentHovered) {
                if (hovered) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(m_pal.hover);
                    p.drawRoundedRect(h.rect, 5.0, 5.0);
                }
                paintUiIcon(&p, h.rect.adjusted(2.5, 2.5, -2.5, -2.5), QStringLiteral("x"), hovered ? m_pal.text : m_pal.muted);
            }
            continue;
        }

        if (h.kind == HitKind::Grip) {
            paintEditorGripDots(p, h.rect, m_pal.icon, hovered ? 0.85 : 0.45);
            continue;
        }

        if (h.kind == HitKind::WidthSlot) {
            const qreal mm = m_widths[std::clamp(h.index, 0, 2)];
            const QColor col = m_colors.isEmpty()
                ? m_pal.text
                : resolveAdaptableInkColor(m_colors.at(std::clamp(m_activeColorIdx, 0, int(m_colors.size()) - 1)), m_darkSheet);
            paintEditorWidthSlot(p, h.rect, pillHeightForTool(m_activeTool, mm),
                                 h.index == m_activeWidthSlot, hovered, col, m_pal);
            continue;
        }

        if (h.kind == HitKind::ColorSwatch) {
            const QColor c = (h.index < m_colors.size())
                ? resolveAdaptableInkColor(m_colors.at(h.index), m_darkSheet)
                : m_pal.text;
            paintEditorSwatch(p, h.rect, c, h.index == m_activeColorIdx, m_pal);
            continue;
        }

        if (h.kind == HitKind::ColorWheel) {
            paintEditorRainbowSwatch(p, h.rect, m_activeColorIdx < 0 || m_activeColorIdx >= m_colors.size(), m_pal);
            continue;
        }

        if (h.kind == HitKind::Pages || h.kind == HitKind::ZoomReset) {
            p.setPen(Qt::NoPen);
            p.setBrush(hovered ? m_pal.hover : m_pal.subtle);
            p.drawRoundedRect(h.rect, 8.0, 8.0);
            p.setFont(editorFont(11.0, QFont::DemiBold));
            p.setPen(m_pal.text);
            if (h.kind == HitKind::Pages) {
                paintUiIcon(&p, QRectF(h.rect.left() + 7.0, h.rect.center().y() - 6.5, 13.0, 13.0),
                            QStringLiteral("pages"), m_pal.icon);
                p.drawText(h.rect.adjusted(21.0, 0.0, -5.0, 0.0), Qt::AlignCenter, h.label);
            } else {
                p.drawText(h.rect, Qt::AlignCenter, h.label);
            }
            continue;
        }

        bool active = false;
        bool enabled = true;
        if (h.kind == HitKind::Tool) {
            if (isPenGroup(h.tool))
                active = isPenGroup(m_activeTool);
            else if (isShapeGroup(h.tool))
                active = isShapeGroup(m_activeTool);
            else
                active = (m_activeTool == h.tool);
        } else if (h.kind == HitKind::Elements) {
            active = m_elementsOpen;
        } else if (h.kind == HitKind::Undo) {
            enabled = m_canUndo;
        } else if (h.kind == HitKind::Redo) {
            enabled = m_canRedo;
        }

        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pal.accent);
            p.drawRoundedRect(h.rect, 9.0, 9.0);
        } else if (hovered && enabled) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pal.hover);
            p.drawRoundedRect(h.rect, 9.0, 9.0);
        }

        QColor fg = active ? m_pal.accentFg : m_pal.icon;
        if (!enabled)
            fg.setAlphaF(0.32);

        const qreal icSz = (h.rect.height() <= 30.0) ? 15.0 : 17.0;
        const QRectF ir(h.rect.center().x() - icSz * 0.5, h.rect.center().y() - icSz * 0.5, icSz, icSz);
        if (h.kind == HitKind::Tool && h.tool == ToolType::Pen)
            paintDynamicPenIcon(&p, ir, fg, m_penNibColor);
        else if (h.kind == HitKind::Tool && h.tool == ToolType::Highlighter)
            paintDynamicHighlighterIcon(&p, ir, fg, m_hlBarColor);
        else
            paintUiIcon(&p, ir, h.iconName, fg);

        if (h.hasSub)
            paintEditorSubChevron(p, h.rect, fg);
    }
}

void EditorTopBar::mouseMoveEvent(QMouseEvent* event) {
    if (m_draggingGrip) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const int idx = hitIndexAt(event->position());
    if (idx != m_hoverHit) {
        m_hoverHit = idx;
        if (idx >= 0 && m_hits.at(idx).kind == HitKind::Grip)
            setCursor(Qt::OpenHandCursor);
        else
            setCursor(idx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorTopBar::leaveEvent(QEvent*) {
    if (m_hoverHit != -1) {
        m_hoverHit = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorTopBar::mousePressEvent(QMouseEvent* event) {
    const int idx = hitIndexAt(event->position());
    if (idx < 0)
        return;
    const auto& h = m_hits.at(idx);

    if (event->button() == Qt::RightButton) {
        if (h.kind == HitKind::Tab)
            emit tabMenuRequested(h.index, event->globalPosition().toPoint());
        else if (h.kind == HitKind::WidthSlot)
            emit fineWidthRequested(h.tool, QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize()));
        return;
    }

    if (event->button() == Qt::LeftButton && h.kind == HitKind::Grip) {
        m_draggingGrip = true;
        m_dragStartGlobal = event->globalPosition().toPoint();
        setCursor(Qt::ClosedHandCursor);
    }
}

void EditorTopBar::mouseDoubleClickEvent(QMouseEvent* event) {
    const int idx = hitIndexAt(event->position());
    if (idx >= 0 && m_hits.at(idx).kind == HitKind::Tab) {
        m_renamingTab = m_hits.at(idx).index;
        const QRect r = m_hits.at(idx).rect.adjusted(26, 4, -24, -4).toRect();
        m_renameEdit->setGeometry(r);
        m_renameEdit->setText(m_tabs.at(m_renamingTab).title);
        m_renameEdit->show();
        m_renameEdit->selectAll();
        m_renameEdit->setFocus();
    }
}

void EditorTopBar::startInlineRenameActiveTab() {
    for (const auto& h : m_hits) {
        if (h.kind == HitKind::Tab && h.index < m_tabs.size() && m_tabs.at(h.index).active) {
            m_renamingTab = h.index;
            m_renameEdit->setGeometry(h.rect.adjusted(26, 4, -24, -4).toRect());
            m_renameEdit->setText(m_tabs.at(m_renamingTab).title);
            m_renameEdit->show();
            m_renameEdit->selectAll();
            m_renameEdit->setFocus();
            return;
        }
    }
}

void EditorTopBar::finishInlineRename(bool accept) {
    if (!m_renameEdit || !m_renameEdit->isVisible())
        return;
    const QString text = m_renameEdit->text().trimmed();
    const int idx = m_renamingTab;
    m_renamingTab = -1;
    m_renameEdit->hide();
    if (accept && idx >= 0 && !text.isEmpty())
        emit tabRenamed(idx, text);
}

void EditorTopBar::mouseReleaseEvent(QMouseEvent* event) {
    if (m_draggingGrip) {
        m_draggingGrip = false;
        setCursor(Qt::ArrowCursor);
        const auto nextMode = dockModeFromGlobalDrag(parentWidget(), event->globalPosition().toPoint());
        if (nextMode != m_dockMode)
            emit dockModeDragFinished(nextMode);
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    const int idx = hitIndexAt(event->position());
    if (idx < 0)
        return;
    const auto h = m_hits.at(idx);
    const QRect anchorInParent(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());

    switch (h.kind) {
    case HitKind::Home: emit homeRequested(); break;
    case HitKind::Tab: emit tabSelected(h.index); break;
    case HitKind::TabClose: emit closeTabRequested(h.index); break;
    case HitKind::NewTab: emit addTabRequested(); break;
    case HitKind::Fullscreen: emit fullscreenRequested(); break;
    case HitKind::Undo: if (m_canUndo) emit undoRequested(); break;
    case HitKind::Redo: if (m_canRedo) emit redoRequested(); break;
    case HitKind::Elements: emit elementsPanelToggled(); break;
    case HitKind::Pages: emit pagesRequested(); break;
    case HitKind::Paper: emit paperRequested(anchorInParent); break;
    case HitKind::ZoomOut: emit zoomOutRequested(); break;
    case HitKind::ZoomReset: emit zoomResetRequested(); break;
    case HitKind::ZoomIn: emit zoomInRequested(); break;
    case HitKind::Export: emit exportRequested(anchorInParent); break;
    case HitKind::Gear: emit gearRequested(); break;
    case HitKind::ColorSwatch: emit colorSwatchSelected(h.tool, h.index); break;
    case HitKind::ColorWheel: emit customColorRequested(h.tool, anchorInParent); break;
    case HitKind::WidthSlot:
        if (h.index == m_activeWidthSlot)
            emit fineWidthRequested(h.tool, anchorInParent);
        else
            emit widthSlotSelected(h.tool, h.index);
        break;
    case HitKind::Tool: {
        const bool alreadyActive = (isPenGroup(h.tool) && isPenGroup(m_activeTool)) ||
                                   (isShapeGroup(h.tool) && isShapeGroup(m_activeTool)) ||
                                   (m_activeTool == h.tool);
        if (alreadyActive && h.hasSub)
            emit toolPanelToggled(h.tool, anchorInParent);
        else
            emit toolSelected(h.tool);
        break;
    }
    default: break;
    }
}

QRect EditorTopBar::anchorRectForTool(ToolType tool) const {
    for (const auto& h : m_hits) {
        if (h.kind == HitKind::Tool &&
            ((isPenGroup(tool) && isPenGroup(h.tool)) || (isShapeGroup(tool) && isShapeGroup(h.tool)) || h.tool == tool)) {
            return QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());
        }
    }
    return QRect(120, 44, 34, 34);
}

QRect EditorTopBar::paperButtonRect() const {
    for (const auto& h : m_hits)
        if (h.kind == HitKind::Paper)
            return QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());
    return QRect(width() - 180, 44, 34, 34);
}

QRect EditorTopBar::pagesButtonRect() const {
    for (const auto& h : m_hits)
        if (h.kind == HitKind::Pages)
            return QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());
    return QRect(width() - 240, 44, 60, 28);
}

QRect EditorTopBar::exportButtonRect() const {
    for (const auto& h : m_hits)
        if (h.kind == HitKind::Export)
            return QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());
    return QRect(width() - 80, 44, 34, 34);
}

QRect EditorTopBar::activeColorAreaRect() const {
    for (const auto& h : m_hits)
        if (h.kind == HitKind::ColorWheel)
            return QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());
    return QRect(width() / 2, 48, 20, 20);
}

QRect EditorTopBar::activeWidthSlotRect() const {
    for (const auto& h : m_hits)
        if (h.kind == HitKind::WidthSlot && h.index == m_activeWidthSlot)
            return QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());
    return QRect(width() / 2 - 40, 48, 30, 28);
}

// ---------------- EditorDetachedDock ----------------

EditorDetachedDock::EditorDetachedDock(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::BarGlass);
    setCornerRadius(14.0);
    setMouseTracking(true);
    hide();
}

void EditorDetachedDock::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
}

void EditorDetachedDock::setElementsPanelOpen(bool open) {
    if (m_elementsOpen == open)
        return;
    m_elementsOpen = open;
    update();
}

void EditorDetachedDock::syncFromCanvas(CanvasWidget* canvas, ToolbarDockMode mode) {
    if (!canvas)
        return;
    m_dockMode = mode;
    m_activeTool = canvas->tool();
    m_darkSheet = canvas->isCurrentSheetDark();
    m_widths = canvas->toolWidths(m_activeTool);
    m_activeWidthSlot = canvas->activeWidthSlot(m_activeTool);
    m_showWidths = toolUsesWidths(m_activeTool);
    m_colors = canvas->toolPalette(m_activeTool);
    m_activeColorIdx = canvas->activeColorIndex(m_activeTool);
    m_showColors = toolUsesColors(m_activeTool);

    const auto penPal = canvas->toolPalette(ToolType::Pen);
    const int penIdx = canvas->activeColorIndex(ToolType::Pen);
    m_penNibColor = penPal.isEmpty()
        ? resolveAdaptableInkColor(canvas->inkColor(), m_darkSheet)
        : resolveAdaptableInkColor(penPal.at(std::clamp(penIdx, 0, int(penPal.size()) - 1)), m_darkSheet);

    const auto hlPal = canvas->toolPalette(ToolType::Highlighter);
    const int hlIdx = canvas->activeColorIndex(ToolType::Highlighter);
    m_hlBarColor = hlPal.isEmpty()
        ? QColor("#FACC15")
        : hlPal.at(std::clamp(hlIdx, 0, int(hlPal.size()) - 1));

    rebuildLayoutAndHits();
    update();
}

void EditorDetachedDock::rebuildLayoutAndHits() {
    m_hits.clear();
    const auto& tools = canonicalTools();
    const bool vertical = (m_dockMode == ToolbarDockMode::Left);

    if (vertical) {
        qreal y = 6.0;
        m_hits.append({QRectF(11.0, y, 24.0, 14.0), HitKind::Grip, 0, ToolType::Pen, false, {}});
        y += 18.0;
        for (const auto& t : tools) {
            if (t.sepBefore)
                y += 7.0;
            m_hits.append({QRectF(6.0, y, 34.0, 32.0),
                           t.isElements ? HitKind::Elements : HitKind::Tool,
                           0, t.tool, t.hasSub, QString::fromLatin1(t.icon)});
            y += 33.0;
        }
        if (m_showWidths) {
            y += 7.0;
            for (int i = 0; i < 3; ++i) {
                m_hits.append({QRectF(6.0, y, 34.0, 22.0), HitKind::WidthSlot, i, m_activeTool, false, {}});
                y += 23.0;
            }
        }
        if (m_showColors) {
            y += 8.0;
            for (int i = 0; i < m_colors.size(); ++i) {
                m_hits.append({QRectF(13.0, y, 20.0, 20.0), HitKind::ColorSwatch, i, m_activeTool, false, {}});
                y += 23.0;
            }
            m_hits.append({QRectF(13.0, y, 20.0, 20.0), HitKind::ColorWheel, 0, m_activeTool, false, {}});
            y += 22.0;
        }
        setFixedSize(46, int(std::ceil(y + 6.0)));
    } else {
        qreal x = 6.0;
        m_hits.append({QRectF(x, 11.0, 12.0, 24.0), HitKind::Grip, 0, ToolType::Pen, false, {}});
        x += 14.0;
        for (const auto& t : tools) {
            if (t.sepBefore)
                x += 8.0;
            m_hits.append({QRectF(x, 6.0, 34.0, 34.0),
                           t.isElements ? HitKind::Elements : HitKind::Tool,
                           0, t.tool, t.hasSub, QString::fromLatin1(t.icon)});
            x += 36.0;
        }
        if (m_showWidths) {
            x += 8.0;
            for (int i = 0; i < 3; ++i) {
                m_hits.append({QRectF(x, 9.0, 30.0, 28.0), HitKind::WidthSlot, i, m_activeTool, false, {}});
                x += 31.0;
            }
        }
        if (m_showColors) {
            x += 8.0;
            for (int i = 0; i < m_colors.size(); ++i) {
                m_hits.append({QRectF(x, 13.0, 20.0, 20.0), HitKind::ColorSwatch, i, m_activeTool, false, {}});
                x += 24.0;
            }
            m_hits.append({QRectF(x, 13.0, 20.0, 20.0), HitKind::ColorWheel, 0, m_activeTool, false, {}});
            x += 22.0;
        }
        setFixedSize(int(std::ceil(x + 6.0)), 46);
    }
}

int EditorDetachedDock::hitIndexAt(const QPointF& pos) const {
    for (int i = 0; i < m_hits.size(); ++i)
        if (m_hits.at(i).rect.contains(pos))
            return i;
    return -1;
}

void EditorDetachedDock::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool vertical = (m_dockMode == ToolbarDockMode::Left);
    p.setPen(QPen(m_pal.border, 1.0));
    for (int i = 1; i < m_hits.size(); ++i) {
        const auto& prev = m_hits.at(i - 1);
        const auto& cur = m_hits.at(i);
        const qreal gap = vertical ? (cur.rect.top() - prev.rect.bottom()) : (cur.rect.left() - prev.rect.right());
        if (gap >= 6.0 && prev.kind != HitKind::Grip) {
            if (vertical) {
                const qreal dy = std::round((prev.rect.bottom() + cur.rect.top()) * 0.5) + 0.5;
                p.drawLine(QPointF(11.0, dy), QPointF(width() - 11.0, dy));
            } else {
                const qreal dx = std::round((prev.rect.right() + cur.rect.left()) * 0.5) + 0.5;
                p.drawLine(QPointF(dx, 13.0), QPointF(dx, height() - 13.0));
            }
        }
    }

    for (int i = 0; i < m_hits.size(); ++i) {
        const auto& h = m_hits.at(i);
        const bool hovered = (i == m_hoverHit);

        if (h.kind == HitKind::Grip) {
            paintEditorGripDots(p, h.rect, m_pal.icon, hovered ? 0.85 : 0.45);
            continue;
        }
        if (h.kind == HitKind::WidthSlot) {
            const qreal mm = m_widths[std::clamp(h.index, 0, 2)];
            const QColor col = m_colors.isEmpty()
                ? m_pal.text
                : resolveAdaptableInkColor(m_colors.at(std::clamp(m_activeColorIdx, 0, int(m_colors.size()) - 1)), m_darkSheet);
            paintEditorWidthSlot(p, h.rect, pillHeightForTool(m_activeTool, mm),
                                 h.index == m_activeWidthSlot, hovered, col, m_pal);
            continue;
        }
        if (h.kind == HitKind::ColorSwatch) {
            const QColor c = (h.index < m_colors.size())
                ? resolveAdaptableInkColor(m_colors.at(h.index), m_darkSheet)
                : m_pal.text;
            paintEditorSwatch(p, h.rect, c, h.index == m_activeColorIdx, m_pal);
            continue;
        }
        if (h.kind == HitKind::ColorWheel) {
            paintEditorRainbowSwatch(p, h.rect, m_activeColorIdx < 0 || m_activeColorIdx >= m_colors.size(), m_pal);
            continue;
        }

        bool active = false;
        if (h.kind == HitKind::Tool)
            active = (isPenGroup(h.tool) && isPenGroup(m_activeTool)) ||
                     (isShapeGroup(h.tool) && isShapeGroup(m_activeTool)) ||
                     (m_activeTool == h.tool);
        else if (h.kind == HitKind::Elements)
            active = m_elementsOpen;

        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pal.accent);
            p.drawRoundedRect(h.rect, 9.0, 9.0);
        } else if (hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pal.hover);
            p.drawRoundedRect(h.rect, 9.0, 9.0);
        }

        const QColor fg = active ? m_pal.accentFg : m_pal.icon;
        const QRectF ir(h.rect.center().x() - 8.5, h.rect.center().y() - 8.5, 17.0, 17.0);
        if (h.kind == HitKind::Tool && h.tool == ToolType::Pen)
            paintDynamicPenIcon(&p, ir, fg, m_penNibColor);
        else if (h.kind == HitKind::Tool && h.tool == ToolType::Highlighter)
            paintDynamicHighlighterIcon(&p, ir, fg, m_hlBarColor);
        else
            paintUiIcon(&p, ir, h.iconName, fg);

        if (h.hasSub)
            paintEditorSubChevron(p, h.rect, fg);
    }
}

void EditorDetachedDock::mouseMoveEvent(QMouseEvent* event) {
    if (m_draggingGrip) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const int idx = hitIndexAt(event->position());
    if (idx != m_hoverHit) {
        m_hoverHit = idx;
        setCursor((idx >= 0 && m_hits.at(idx).kind == HitKind::Grip)
                  ? Qt::OpenHandCursor
                  : (idx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor));
        update();
    }
}

void EditorDetachedDock::leaveEvent(QEvent*) {
    if (m_hoverHit != -1) {
        m_hoverHit = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorDetachedDock::mousePressEvent(QMouseEvent* event) {
    const int idx = hitIndexAt(event->position());
    if (idx < 0)
        return;
    const auto& h = m_hits.at(idx);
    if (event->button() == Qt::RightButton && h.kind == HitKind::WidthSlot) {
        emit fineWidthRequested(h.tool, QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize()));
        return;
    }
    if (event->button() == Qt::LeftButton && h.kind == HitKind::Grip) {
        m_draggingGrip = true;
        setCursor(Qt::ClosedHandCursor);
    }
}

void EditorDetachedDock::mouseReleaseEvent(QMouseEvent* event) {
    if (m_draggingGrip) {
        m_draggingGrip = false;
        setCursor(Qt::ArrowCursor);
        const auto nextMode = dockModeFromGlobalDrag(parentWidget(), event->globalPosition().toPoint());
        if (nextMode != m_dockMode)
            emit dockModeDragFinished(nextMode);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    const int idx = hitIndexAt(event->position());
    if (idx < 0)
        return;
    const auto h = m_hits.at(idx);
    const QRect anchorInParent(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());

    switch (h.kind) {
    case HitKind::Elements: emit elementsPanelToggled(); break;
    case HitKind::ColorSwatch: emit colorSwatchSelected(h.tool, h.index); break;
    case HitKind::ColorWheel: emit customColorRequested(h.tool, anchorInParent); break;
    case HitKind::WidthSlot:
        if (h.index == m_activeWidthSlot)
            emit fineWidthRequested(h.tool, anchorInParent);
        else
            emit widthSlotSelected(h.tool, h.index);
        break;
    case HitKind::Tool: {
        const bool alreadyActive = (isPenGroup(h.tool) && isPenGroup(m_activeTool)) ||
                                   (isShapeGroup(h.tool) && isShapeGroup(m_activeTool)) ||
                                   (m_activeTool == h.tool);
        if (alreadyActive && h.hasSub)
            emit toolPanelToggled(h.tool, anchorInParent);
        else
            emit toolSelected(h.tool);
        break;
    }
    default: break;
    }
}

QRect EditorDetachedDock::anchorRectForTool(ToolType tool) const {
    for (const auto& h : m_hits) {
        if (h.kind == HitKind::Tool &&
            ((isPenGroup(tool) && isPenGroup(h.tool)) || (isShapeGroup(tool) && isShapeGroup(h.tool)) || h.tool == tool)) {
            return QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());
        }
    }
    return QRect(mapTo(parentWidget(), QPoint(0, 0)), size());
}

QRect EditorDetachedDock::activeColorAreaRect() const {
    for (const auto& h : m_hits)
        if (h.kind == HitKind::ColorWheel)
            return QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());
    return QRect(mapTo(parentWidget(), QPoint(0, 0)), size());
}

QRect EditorDetachedDock::activeWidthSlotRect() const {
    for (const auto& h : m_hits)
        if (h.kind == HitKind::WidthSlot && h.index == m_activeWidthSlot)
            return QRect(mapTo(parentWidget(), h.rect.topLeft().toPoint()), h.rect.size().toSize());
    return QRect(mapTo(parentWidget(), QPoint(0, 0)), size());
}

// ---------------- EditorPageCapsule ----------------

EditorPageCapsule::EditorPageCapsule(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::ContextCapsule);
    setCornerRadius(12.0);
    setFixedSize(142, 34);
    setMouseTracking(true);
    hide();
}

void EditorPageCapsule::setPageInfo(int current, int count) {
    m_current = std::max(1, current);
    m_count = std::max(1, count);
    update();
}

void EditorPageCapsule::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rPrev(4, 4, 26, 26);
    const QRectF rLbl(32, 4, 50, 26);
    const QRectF rNext(84, 4, 26, 26);
    const QRectF rAdd(112, 4, 26, 26);
    const QRectF zones[4] = {rPrev, rLbl, rNext, rAdd};

    for (int i = 0; i < 4; ++i) {
        if (m_hoverPart == i) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pal.hover);
            p.drawRoundedRect(zones[i], 7.0, 7.0);
        }
    }

    QColor prevCol = m_pal.icon;
    if (m_current <= 1)
        prevCol.setAlphaF(0.32);
    paintUiIcon(&p, rPrev.adjusted(6, 6, -6, -6), QStringLiteral("chevL"), prevCol);

    p.setFont(editorFont(11.5, QFont::DemiBold));
    p.setPen(m_pal.text);
    p.drawText(rLbl, Qt::AlignCenter, QStringLiteral("%1 / %2").arg(m_current).arg(m_count));

    QColor nextCol = m_pal.icon;
    if (m_current >= m_count)
        nextCol.setAlphaF(0.32);
    paintUiIcon(&p, rNext.adjusted(6, 6, -6, -6), QStringLiteral("chevR"), nextCol);

    paintUiIcon(&p, rAdd.adjusted(6, 6, -6, -6), QStringLiteral("plus"), m_pal.icon);
}

void EditorPageCapsule::mouseMoveEvent(QMouseEvent* event) {
    const qreal x = event->position().x();
    int part = (x < 31) ? 0 : (x < 83 ? 1 : (x < 111 ? 2 : 3));
    if (part != m_hoverPart) {
        m_hoverPart = part;
        setCursor(Qt::PointingHandCursor);
        update();
    }
}

void EditorPageCapsule::leaveEvent(QEvent*) {
    if (m_hoverPart != -1) {
        m_hoverPart = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorPageCapsule::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    const qreal x = event->position().x();
    if (x < 31) {
        if (m_current > 1) emit prevPageRequested();
    } else if (x < 83) {
        emit pagesPanelRequested();
    } else if (x < 111) {
        if (m_current < m_count) emit nextPageRequested();
    } else {
        emit addPageRequested();
    }
}

} // namespace finily

