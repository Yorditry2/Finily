#include "ui/home/HomeMenu.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "engine/LibraryStore.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QTimer>

#include <algorithm>

namespace finily {
namespace {

// Medidas del mockup 07 (.menu / .mi / .msep / .mhead / .mswatch).
constexpr int kMenuW = 236;
constexpr int kPad = 5;
constexpr int kRowH = 30;
constexpr int kSepH = 9;
constexpr int kHeadH = 22;
constexpr int kSwatchH = 30;
constexpr int kSwatch = 18;
constexpr int kSwatchGap = 7;

QFont uiFont(const QFont& base, qreal px, int weight) {
    QFont f = base;
    f.setPixelSize(qRound(px));
    f.setWeight(QFont::Weight(weight));
    return f;
}

} // namespace

// ============================================================== HomeMenu
HomeMenu::HomeMenu(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_anim = new QPropertyAnimation(this, "progress", this);
    m_anim->setDuration(tokens::motion::Fast);
    m_anim->setEasingCurve(tokens::motion::out());
    hide();
}

void HomeMenu::setProgress(qreal p) {
    m_progress = std::clamp(p, 0.0, 1.0);
    update();
}

void HomeMenu::applyTheme(bool dark) {
    m_dark = dark;
    update();
}

int HomeMenu::itemHeight(const HomeMenuItem& it) {
    switch (it.kind) {
        case HomeMenuItem::Kind::Separator: return kSepH;
        case HomeMenuItem::Kind::Header: return kHeadH;
        case HomeMenuItem::Kind::Swatches: return kSwatchH;
        default: return kRowH;
    }
}

void HomeMenu::layoutPanel(Panel& p, const QPoint& at, bool preferLeftOf, int leftEdge) {
    int h = kPad * 2;
    for (const auto& it : p.items)
        h += itemHeight(it);
    int x = at.x();
    if (x + kMenuW > width() - 10)
        x = preferLeftOf ? leftEdge - kMenuW - 4 : width() - 10 - kMenuW;
    const int y = std::clamp(at.y(), 10, std::max(10, height() - h - 10));
    p.rect = QRect(std::max(10, x), y, kMenuW, h);
    if (parentWidget() && parentWidget()->isVisible()) {
        const bool wasVisible = isVisible();
        if (wasVisible)
            setUpdatesEnabled(false);
        p.backdrop = blurBackdropImage(parentWidget()->grab(p.rect).toImage(), 11);
        if (wasVisible)
            setUpdatesEnabled(true);
    }
}

QRect HomeMenu::itemRect(const Panel& p, int index) const {
    int y = p.rect.top() + kPad;
    for (int i = 0; i < index; ++i)
        y += itemHeight(p.items.at(i));
    return QRect(p.rect.left() + kPad, y, kMenuW - 2 * kPad, itemHeight(p.items.at(index)));
}

QRect HomeMenu::swatchRect(const QRect& row, int i) const {
    return QRect(row.left() + 8 + i * (kSwatch + kSwatchGap), row.top() + 5, kSwatch, kSwatch);
}

void HomeMenu::popup(const QPoint& posInParent, const QVector<HomeMenuItem>& items) {
    if (!parentWidget())
        return;
    if (isVisible())
        hide();
    setGeometry(parentWidget()->rect());
    m_panels.clear();
    m_submenuOf = -1;
    Panel main;
    main.items = items;
    layoutPanel(main, posInParent + QPoint(2, 2), false, posInParent.x());
    if (main.rect.left() < posInParent.x() && posInParent.x() - kMenuW - 2 >= 10)
        main.rect.moveLeft(posInParent.x() - kMenuW - 2); // abre hacia la izquierda si no cabe
    m_panels.append(main);
    m_progress = 0.0;
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
    m_anim->stop();
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void HomeMenu::closeMenu() {
    if (!isVisible())
        return;
    m_anim->stop();
    m_panels.clear();
    hide();
}

void HomeMenu::openSubmenu(int item) {
    if (m_submenuOf == item)
        return;
    while (m_panels.size() > 1)
        m_panels.removeLast();
    m_submenuOf = -1;
    if (item < 0 || m_panels.isEmpty())
        return;
    const HomeMenuItem& it = m_panels[0].items.at(item);
    if (it.children.isEmpty() || !it.enabled)
        return;
    Panel sub;
    sub.items = it.children;
    const QRect row = itemRect(m_panels[0], item);
    layoutPanel(sub, QPoint(m_panels[0].rect.right() + 4, row.top() - kPad), true, m_panels[0].rect.left());
    m_panels.append(sub);
    m_submenuOf = item;
}

bool HomeMenu::hitTest(const QPoint& pos, int* panel, int* item, int* swatch) const {
    for (int pi = int(m_panels.size()) - 1; pi >= 0; --pi) {
        const Panel& p = m_panels.at(pi);
        if (!p.rect.contains(pos))
            continue;
        *panel = pi;
        *item = -1;
        *swatch = -1;
        for (int i = 0; i < p.items.size(); ++i) {
            const QRect r = itemRect(p, i);
            if (!r.contains(pos))
                continue;
            *item = i;
            if (p.items.at(i).kind == HomeMenuItem::Kind::Swatches) {
                for (int s = 0; s < LibraryStore::kCoverColorCount; ++s)
                    if (swatchRect(r, s).adjusted(-3, -3, 3, 3).contains(pos))
                        *swatch = s;
            }
            break;
        }
        return true;
    }
    return false;
}

void HomeMenu::trigger(int panel, int item, int swatch) {
    if (panel < 0 || panel >= m_panels.size() || item < 0)
        return;
    const HomeMenuItem it = m_panels.at(panel).items.at(item);
    if (!it.enabled)
        return;
    if (it.kind == HomeMenuItem::Kind::Swatches) {
        if (swatch < 0 || !it.swatchPicked)
            return;
        closeMenu();
        it.swatchPicked(swatch);
        return;
    }
    if (it.kind != HomeMenuItem::Kind::Action || !it.children.isEmpty() || !it.action)
        return;
    closeMenu();
    it.action();
}

void HomeMenu::mouseMoveEvent(QMouseEvent* e) {
    int pi = -1, ii = -1, si = -1;
    const bool inside = hitTest(e->position().toPoint(), &pi, &ii, &si);
    for (auto& p : m_panels) {
        p.hovered = -1;
        p.hoveredSwatch = -1;
    }
    if (inside && ii >= 0) {
        const auto& it = m_panels[pi].items.at(ii);
        if (it.kind == HomeMenuItem::Kind::Action && it.enabled)
            m_panels[pi].hovered = ii;
        m_panels[pi].hoveredSwatch = si;
        if (pi == 0)
            openSubmenu(it.children.isEmpty() ? -1 : ii);
    }
    if (m_submenuOf >= 0 && m_panels.size() > 0 && m_panels[0].hovered < 0)
        m_panels[0].hovered = m_submenuOf;
    setCursor(inside && ii >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void HomeMenu::mousePressEvent(QMouseEvent* e) {
    int pi = -1, ii = -1, si = -1;
    if (!hitTest(e->position().toPoint(), &pi, &ii, &si))
        closeMenu();
}

void HomeMenu::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    int pi = -1, ii = -1, si = -1;
    if (hitTest(e->position().toPoint(), &pi, &ii, &si))
        trigger(pi, ii, si);
}

void HomeMenu::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        closeMenu();
        return;
    }
    if (m_panels.isEmpty())
        return;
    Panel& p = m_panels.last();
    const int n = int(p.items.size());
    auto selectable = [&](int i) {
        return p.items.at(i).kind == HomeMenuItem::Kind::Action && p.items.at(i).enabled;
    };
    if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Up) {
        const int step = e->key() == Qt::Key_Down ? 1 : -1;
        int i = p.hovered;
        for (int k = 0; k < n; ++k) {
            i = (i + step + n) % n;
            if (selectable(i))
                break;
        }
        p.hovered = i;
        update();
    } else if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && p.hovered >= 0) {
        if (m_panels.size() == 1 && !p.items.at(p.hovered).children.isEmpty())
            openSubmenu(p.hovered), update();
        else
            trigger(int(m_panels.size()) - 1, p.hovered, -1);
    } else if (e->key() == Qt::Key_Left && m_panels.size() > 1) {
        openSubmenu(-1);
        update();
    }
}

void HomeMenu::paintPanel(QPainter& painter, const Panel& p) {
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    paintLiquidGlassPanel(painter, QRectF(p.rect), 12.0, p.backdrop, homePaletteFor(m_dark), m_dark);

    for (int i = 0; i < p.items.size(); ++i) {
        const HomeMenuItem& it = p.items.at(i);
        const QRect r = itemRect(p, i);
        switch (it.kind) {
            case HomeMenuItem::Kind::Separator:
                painter.fillRect(QRect(r.left() + 6, r.center().y(), r.width() - 12, 1), tk.border);
                break;
            case HomeMenuItem::Kind::Header:
                painter.setFont(uiFont(font(), 10, QFont::Bold));
                painter.setPen(tk.muted);
                painter.drawText(QRect(r.left() + 8, r.top() + 6, r.width() - 16, 14),
                                 Qt::AlignLeft | Qt::AlignVCenter, it.label.toUpper());
                break;
            case HomeMenuItem::Kind::Swatches: {
                for (int s = 0; s < LibraryStore::kCoverColorCount; ++s) {
                    const QRectF sr = QRectF(swatchRect(r, s));
                    const bool on = s == it.swatchCurrent;
                    const bool hov = s == p.hoveredSwatch;
                    if (on) {
                        painter.setPen(QPen(tk.accent, 1.5));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawEllipse(sr.adjusted(-3.5, -3.5, 3.5, 3.5));
                    }
                    const qreal grow = hov && !on ? 1.0 : 0.0;
                    const QRectF dot = sr.adjusted(-grow, -grow, grow, grow);
                    QPainterPath circle;
                    circle.addEllipse(dot);
                    painter.save();
                    painter.setClipPath(circle);
                    const tokens::FolderColor& fc = tokens::folderColor(s);
                    painter.fillRect(dot, fc.body2);
                    painter.fillRect(QRectF(dot.left(), dot.top(), dot.width(), dot.height() * 0.38), fc.flap);
                    painter.restore();
                }
                break;
            }
            case HomeMenuItem::Kind::Action: {
                const bool hov = i == p.hovered;
                if (hov) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(it.danger ? tk.dangerSoft : tk.hover);
                    painter.drawRoundedRect(QRectF(r), 7, 7);
                }
                QColor fg = it.danger ? tk.danger : (hov ? tk.text : tk.icon);
                if (!it.enabled)
                    fg = tk.faint;
                if (!it.icon.isEmpty())
                    paintUiIcon(&painter, QRectF(r.left() + 8, r.center().y() - 7.0, 15, 15), it.icon, fg);
                const int tx = r.left() + 8 + 15 + 9;
                int right = r.right() - 8;
                if (!it.children.isEmpty()) {
                    paintUiIcon(&painter, QRectF(right - 12, r.center().y() - 6.0, 12, 12),
                                QStringLiteral("chevR"), tk.faint);
                    right -= 16;
                } else if (!it.shortcut.isEmpty()) {
                    painter.setFont(uiFont(font(), 11, QFont::Normal));
                    painter.setPen(tk.faint);
                    const int kw = QFontMetrics(painter.font()).horizontalAdvance(it.shortcut);
                    painter.drawText(QRect(right - kw, r.top(), kw, r.height()), Qt::AlignVCenter, it.shortcut);
                    right -= kw + 8;
                }
                const QFont lf = uiFont(font(), 12.5, QFont::Medium);
                painter.setFont(lf);
                painter.setPen(fg);
                painter.drawText(QRect(tx, r.top(), right - tx, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                                 QFontMetrics(lf).elidedText(it.label, Qt::ElideRight, right - tx));
                break;
            }
        }
    }
}

void HomeMenu::paintEvent(QPaintEvent*) {
    if (m_panels.isEmpty())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect& r0 = m_panels.first().rect;
    const QPointF origin(r0.left(), r0.top());
    const qreal sc = 0.97 + 0.03 * m_progress;
    painter.translate(origin);
    painter.scale(sc, sc);
    painter.translate(-origin);
    painter.setOpacity(m_progress);
    for (const Panel& p : m_panels)
        paintPanel(painter, p);
}

// ============================================================== HomeToast
HomeToast::HomeToast(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &HomeToast::dismiss);
    hide();
}

void HomeToast::setArea(const QRect& area) {
    m_area = area;
    if (isVisible())
        showMessage(m_text, m_actionText, m_action, m_timer->remainingTime() > 0 ? m_timer->remainingTime() : 3000);
}

void HomeToast::showMessage(const QString& text, const QString& actionText, std::function<void()> action,
                            int msec) {
    m_text = text;
    m_actionText = actionText;
    m_action = std::move(action);
    m_hover = 0;
    const QFont tf = uiFont(font(), 12.5, QFont::Medium);
    const QFont bf = uiFont(font(), 12, QFont::DemiBold);
    int w = 14 + QFontMetrics(tf).horizontalAdvance(text) + 12 + 8; // margen por redondeo de métricas
    if (!actionText.isEmpty())
        w += QFontMetrics(bf).horizontalAdvance(actionText) + 20 + 6;
    w += 26 + 10;
    const QRect area = m_area.isValid() ? m_area : (parentWidget() ? parentWidget()->rect() : QRect());
    w = std::min(w, area.width() - 40);
    setGeometry(area.center().x() - w / 2, area.bottom() - 20 - 44, w, 44);
    show();
    raise();
    update();
    m_timer->start(msec);
}

QRect HomeToast::closeRect() const { return QRect(width() - 10 - 26, 9, 26, 26); }

QRect HomeToast::actionRect() const {
    if (m_actionText.isEmpty())
        return {};
    const int bw = QFontMetrics(uiFont(font(), 12, QFont::DemiBold)).horizontalAdvance(m_actionText) + 20;
    return QRect(closeRect().left() - 6 - bw, 9, bw, 26);
}

void HomeToast::dismiss() {
    m_timer->stop();
    hide();
}

void HomeToast::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(24, 24, 27, 230));
    p.drawRoundedRect(QRectF(rect()), 12, 12);
    const QFont tf = uiFont(font(), 12.5, QFont::Medium);
    p.setFont(tf);
    p.setPen(QColor(0xF4, 0xF4, 0xF5));
    const int textRight = (m_actionText.isEmpty() ? closeRect().left() : actionRect().left()) - 12;
    p.drawText(QRect(14, 0, textRight - 14, height()), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(tf).elidedText(m_text, Qt::ElideRight, textRight - 14));
    if (!m_actionText.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, m_hover == 1 ? 46 : 31));
        p.drawRoundedRect(QRectF(actionRect()), 8, 8);
        p.setFont(uiFont(font(), 12, QFont::DemiBold));
        p.setPen(QColor(255, 255, 255));
        p.drawText(actionRect(), Qt::AlignCenter, m_actionText);
    }
    if (m_hover == 2) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 26));
        p.drawRoundedRect(QRectF(closeRect()), 8, 8);
    }
    paintUiIcon(&p, QRectF(closeRect()).adjusted(7, 7, -7, -7), QStringLiteral("x"), QColor(0xA1, 0xA1, 0xAA));
}

void HomeToast::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    const int h = actionRect().contains(pos) ? 1 : (closeRect().contains(pos) ? 2 : 0);
    if (h != m_hover) {
        m_hover = h;
        setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void HomeToast::leaveEvent(QEvent*) {
    m_hover = 0;
    update();
}

void HomeToast::mouseReleaseEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    if (actionRect().contains(pos)) {
        auto fn = m_action;
        dismiss();
        if (fn)
            fn();
    } else if (closeRect().contains(pos)) {
        dismiss();
    }
}

// ============================================================== ConfirmDialog
ConfirmDialog::ConfirmDialog(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    hide();
}

void ConfirmDialog::applyTheme(bool dark) {
    m_dark = dark;
    update();
}

void ConfirmDialog::ask(const QString& icon, const QString& title, const QString& body,
                        const QString& confirmText, std::function<void()> onConfirm) {
    m_icon = icon;
    m_title = title;
    m_body = body;
    m_confirm = confirmText;
    m_onConfirm = std::move(onConfirm);
    m_hover = 0;
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
}

int ConfirmDialog::bodyHeight() const {
    const QFontMetrics fm(uiFont(font(), 12.5, QFont::Normal));
    return fm.boundingRect(QRect(0, 0, 320, 400), Qt::TextWordWrap, m_body).height() + 4;
}

QRect ConfirmDialog::cardRect() const {
    const int h = 20 + 22 + 14 + bodyHeight() + 14 + 38 + 20;
    return QRect(width() / 2 - 180, height() / 2 - h / 2, 360, h);
}

QRect ConfirmDialog::cancelRect() const {
    const QRect c = cardRect();
    return QRect(c.left() + 20, c.bottom() - 20 - 38 + 1, 156, 38);
}

QRect ConfirmDialog::confirmRect() const {
    const QRect c = cardRect();
    return QRect(c.left() + 20 + 156 + 8, c.bottom() - 20 - 38 + 1, 156, 38);
}

void ConfirmDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    p.fillRect(rect(), m_dark ? QColor(0, 0, 0, 90) : QColor(24, 24, 27, 36));
    const QRect c = cardRect();
    paintSoftShadow(p, QRectF(c), 18, 6.0, m_dark);
    p.setPen(QPen(tk.borderPop, 1));
    p.setBrush(tk.surface);
    p.drawRoundedRect(QRectF(c).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);

    p.setFont(uiFont(font(), 15, QFont::DemiBold));
    p.setPen(tk.text);
    p.drawText(QRect(c.left() + 20, c.top() + 20, 320, 22), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    p.setFont(uiFont(font(), 12.5, QFont::Normal));
    p.setPen(m_dark ? tk.muted : QColor(0x52, 0x52, 0x5B));
    p.drawText(QRect(c.left() + 20, c.top() + 20 + 22 + 14, 320, bodyHeight()), Qt::TextWordWrap, m_body);

    // Cancelar con anillo de foco (foco por defecto) y el botón peligroso en rojo.
    const QRectF cr = QRectF(cancelRect());
    p.setPen(QPen(tk.accent, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(cr.adjusted(-3, -3, 3, 3), 13, 13);
    p.setPen(Qt::NoPen);
    p.setBrush(m_hover == 1 ? tk.hover : tk.field);
    p.drawRoundedRect(cr, 11, 11);
    p.setBrush(m_hover == 2 ? tk.danger.darker(112) : tk.danger);
    p.drawRoundedRect(QRectF(confirmRect()), 11, 11);
    p.setFont(uiFont(font(), 13, QFont::DemiBold));
    p.setPen(tk.text);
    p.drawText(cancelRect(), Qt::AlignCenter, QStringLiteral("Cancelar"));
    p.setPen(Qt::white);
    p.drawText(confirmRect(), Qt::AlignCenter, m_confirm);
}

void ConfirmDialog::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    const int h = cancelRect().contains(pos) ? 1 : (confirmRect().contains(pos) ? 2 : 0);
    if (h != m_hover) {
        m_hover = h;
        setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void ConfirmDialog::mouseReleaseEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    if (confirmRect().contains(pos))
        finish(true);
    else if (cancelRect().contains(pos) || !cardRect().contains(pos))
        finish(false);
}

void ConfirmDialog::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
        finish(false); // el foco por defecto está en «Cancelar»
}

void ConfirmDialog::finish(bool accepted) {
    auto fn = m_onConfirm;
    m_onConfirm = {};
    hide();
    if (accepted && fn)
        fn();
}

} // namespace finily
