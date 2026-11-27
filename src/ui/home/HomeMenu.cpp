#include "ui/home/HomeMenu.h"

#include "engine/LibraryStore.h"
#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"

#include <QApplication>
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

// Márgenes del aviso inferior para sombra y deslizamiento sin recortes.
constexpr int kToastPadX = 14;
constexpr int kToastPadTop = 10;
constexpr int kToastPadBottom = 22;
constexpr int kToastH = 44;
constexpr int kProgressBarW = 120;
constexpr int kProgressBarH = 4;

QFont uiFont(qreal px, int weight) {
    QFont f = QApplication::font();
    f.setPixelSize(qRound(px));
    f.setWeight(QFont::Weight(weight));
    return f;
}

bool isSelectableKind(HomeMenuItem::Kind k) {
    return k == HomeMenuItem::Kind::Action || k == HomeMenuItem::Kind::Check;
}

} // namespace

// ============================================================== HomeMenu
HomeMenu::HomeMenu(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_anim = new QPropertyAnimation(this, "progress", this);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (!m_closing)
            return;
        m_closing = false;
        m_panels.clear();
        m_submenuOf = -1;
        hide();
    });
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
    if (parentWidget() && parentWidget()->isVisible() && !tokens::reducedMotion()) {
        m_grabbingBackdrop = true;
        p.backdrop = blurBackdropImage(parentWidget()->grab(p.rect).toImage(), 11);
        m_grabbingBackdrop = false;
    } else {
        p.backdrop = QImage();
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
    m_anim->stop();
    m_closing = false;
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
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
    m_anim->setDuration(tokens::motion::duration(tokens::motion::Base));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void HomeMenu::closeMenu() {
    if (!isVisible() || m_closing)
        return;
    m_closing = true;
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_anim->stop();
    m_anim->setDuration(tokens::motion::duration(tokens::motion::Fast));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(0.0);
    m_anim->start();
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
    HomeMenuItem& mutItem = m_panels[panel].items[item];
    if (!mutItem.enabled)
        return;
    if (mutItem.kind == HomeMenuItem::Kind::Swatches) {
        if (swatch < 0 || !mutItem.swatchPicked)
            return;
        auto fn = mutItem.swatchPicked;
        closeMenu();
        fn(swatch);
        return;
    }
    if (mutItem.kind == HomeMenuItem::Kind::Check) {
        mutItem.checked = !mutItem.checked;
        const bool nextState = mutItem.checked;
        auto onToggle = mutItem.checkToggled;
        auto onAct = mutItem.action;
        if (!mutItem.keepOpenOnTrigger)
            closeMenu();
        else
            update();
        if (onToggle)
            onToggle(nextState);
        else if (onAct)
            onAct();
        return;
    }
    if (mutItem.kind != HomeMenuItem::Kind::Action || !mutItem.children.isEmpty() || !mutItem.action)
        return;
    auto fn = mutItem.action;
    closeMenu();
    fn();
}

void HomeMenu::mouseMoveEvent(QMouseEvent* e) {
    if (m_closing)
        return;
    int pi = -1, ii = -1, si = -1;
    const bool inside = hitTest(e->position().toPoint(), &pi, &ii, &si);
    for (auto& p : m_panels) {
        p.hovered = -1;
        p.hoveredSwatch = -1;
    }
    if (inside && ii >= 0) {
        const auto& it = m_panels[pi].items.at(ii);
        if (isSelectableKind(it.kind) && it.enabled)
            m_panels[pi].hovered = ii;
        m_panels[pi].hoveredSwatch = si;
        if (pi == 0)
            openSubmenu(it.children.isEmpty() ? -1 : ii);
    }
    if (m_submenuOf >= 0 && !m_panels.isEmpty() && m_panels[0].hovered < 0)
        m_panels[0].hovered = m_submenuOf;
    setCursor(inside && ii >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void HomeMenu::mousePressEvent(QMouseEvent* e) {
    if (m_closing)
        return;
    int pi = -1, ii = -1, si = -1;
    if (!hitTest(e->position().toPoint(), &pi, &ii, &si))
        closeMenu();
}

void HomeMenu::mouseReleaseEvent(QMouseEvent* e) {
    if (m_closing || e->button() != Qt::LeftButton)
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
    if (m_panels.isEmpty() || m_closing)
        return;
    Panel& p = m_panels.last();
    const int n = int(p.items.size());
    auto selectable = [&](int i) {
        return isSelectableKind(p.items.at(i).kind) && p.items.at(i).enabled;
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
        if (m_panels.size() == 1 && !p.items.at(p.hovered).children.isEmpty()) {
            openSubmenu(p.hovered);
            update();
        } else {
            trigger(int(m_panels.size()) - 1, p.hovered, -1);
        }
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
                painter.setFont(uiFont(10, QFont::Bold));
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
            case HomeMenuItem::Kind::Check: {
                const bool hov = (i == p.hovered);
                if (hov) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(tk.hover);
                    painter.drawRoundedRect(QRectF(r), 7, 7);
                }
                const QRectF box(r.left() + 8, r.center().y() - 7.5, 15, 15);
                if (it.checked) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(tk.accent);
                    painter.drawRoundedRect(box, 4, 4);
                    paintUiIcon(&painter, box.adjusted(2.0, 2.0, -2.0, -2.0), QStringLiteral("check"), tk.accentFg);
                } else {
                    painter.setPen(QPen(m_dark ? QColor(255, 255, 255, 70) : QColor(0xC4, 0xC4, 0xCB), 1.5));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRoundedRect(box.adjusted(0.75, 0.75, -0.75, -0.75), 4, 4);
                }
                qreal tx = box.right() + 9;
                if (it.dotColor.isValid()) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(it.dotColor);
                    painter.drawEllipse(QRectF(tx, r.center().y() - 4.0, 8, 8));
                    tx += 15;
                }
                const QFont lf = uiFont(12.5, it.checked ? QFont::DemiBold : QFont::Medium);
                painter.setFont(lf);
                painter.setPen(!it.enabled ? tk.faint : ((it.checked || hov) ? tk.text : tk.icon));
                const int right = r.right() - 8;
                painter.drawText(QRect(qRound(tx), r.top(), std::max(20, right - qRound(tx)), r.height()),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QFontMetrics(lf).elidedText(it.label, Qt::ElideRight, std::max(20, right - qRound(tx))));
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
                    painter.setFont(uiFont(11, QFont::Normal));
                    painter.setPen(tk.faint);
                    const int kw = QFontMetrics(painter.font()).horizontalAdvance(it.shortcut);
                    painter.drawText(QRect(right - kw, r.top(), kw, r.height()), Qt::AlignVCenter, it.shortcut);
                    right -= kw + 8;
                }
                const QFont lf = uiFont(12.5, QFont::Medium);
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
    if (m_grabbingBackdrop || m_panels.isEmpty())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (!tokens::reducedMotion()) {
        const QRect& r0 = m_panels.first().rect;
        const QPointF origin(r0.left(), r0.top());
        const qreal sc = m_closing ? (0.98 + 0.02 * m_progress) : (0.97 + 0.03 * m_progress);
        painter.translate(origin);
        painter.scale(sc, sc);
        painter.translate(-origin);
    }
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
    m_anim = new QPropertyAnimation(this, "progress", this);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (!m_closing)
            return;
        m_closing = false;
        m_backdrop = QImage();
        hide();
    });
    hide();
}

void HomeToast::setProgress(qreal p) {
    m_progress = std::clamp(p, 0.0, 1.0);
    update();
}

void HomeToast::setArea(const QRect& area) {
    m_area = area;
    if (isVisible() && !m_closing)
        updateGeometryAndBackdrop(true);
}

int HomeToast::progressCounterWidth() const {
    const QFontMetrics fm(uiFont(11, QFont::Medium));
    const QString cur = QStringLiteral("%1 de %2").arg(m_done).arg(m_total);
    const QString widest = QStringLiteral("%1 de %1").arg(m_total);
    return std::max(fm.horizontalAdvance(cur), fm.horizontalAdvance(widest));
}

void HomeToast::updateGeometryAndBackdrop(bool grabBackdrop) {
    const QFont tf = uiFont(12.5, QFont::Medium);
    const QFont bf = uiFont(12, QFont::DemiBold);
    const QFont kf = uiFont(11, QFont::Medium);

    int w = 14 + QFontMetrics(tf).horizontalAdvance(m_text) + 12;
    if (!m_icon.isEmpty())
        w += 15 + 10;
    if (m_isProgress) {
        w += kProgressBarW + 12 + progressCounterWidth() + 12;
        if (!m_actionText.isEmpty())
            w += QFontMetrics(bf).horizontalAdvance(m_actionText) + 20 + 10;
    } else {
        if (!m_actionText.isEmpty())
            w += QFontMetrics(bf).horizontalAdvance(m_actionText) + 20 + 10;
        if (!m_shortcut.isEmpty())
            w += QFontMetrics(kf).horizontalAdvance(m_shortcut) + 10;
        w += 26 + 10;
    }

    const QRect area = m_area.isValid() ? m_area : (parentWidget() ? parentWidget()->rect() : QRect(0, 0, 800, 600));
    m_pillW = std::clamp(w, 120, std::max(120, area.width() - 40));

    const int pillLeft = area.center().x() - m_pillW / 2;
    const int pillTop = area.bottom() - 20 - kToastH;
    setGeometry(pillLeft - kToastPadX, pillTop - kToastPadTop,
                m_pillW + 2 * kToastPadX, kToastH + kToastPadTop + kToastPadBottom);

    if (grabBackdrop && parentWidget() && parentWidget()->isVisible() && !tokens::reducedMotion()) {
        const QRect pillInParent(pillLeft, pillTop, m_pillW, kToastH);
        m_grabbingBackdrop = true;
        m_backdrop = blurBackdropImage(parentWidget()->grab(pillInParent).toImage(), 10);
        m_grabbingBackdrop = false;
    } else if (tokens::reducedMotion()) {
        m_backdrop = QImage();
    }
}

void HomeToast::startOpenAnimation() {
    m_anim->stop();
    m_closing = false;
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_progress = 0.0;
    show();
    raise();
    m_anim->setDuration(tokens::motion::duration(tokens::motion::Base));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void HomeToast::showMessage(const QString& text, const QString& actionText, std::function<void()> action,
                            int msec, const QString& icon, const QString& shortcut) {
    m_isProgress = false;
    m_icon = icon;
    m_text = text;
    m_actionText = actionText;
    m_shortcut = shortcut;
    m_action = std::move(action);
    m_done = 0;
    m_total = 0;
    m_hover = 0;
    updateGeometryAndBackdrop(true);
    startOpenAnimation();
    m_timer->start(msec);
}

void HomeToast::showProgress(const QString& icon, const QString& text, int done, int total,
                             std::function<void()> onCancel) {
    const bool alreadyOpen = isVisible() && !m_closing;
    const int prevW = m_pillW;
    m_timer->stop();
    m_isProgress = true;
    m_icon = icon;
    m_text = text;
    m_done = done;
    m_total = total;
    m_actionText = QStringLiteral("Cancelar");
    m_shortcut.clear();
    m_action = std::move(onCancel);
    if (alreadyOpen) {
        updateGeometryAndBackdrop(false);
        if (m_pillW != prevW || m_backdrop.isNull())
            updateGeometryAndBackdrop(true);
        raise();
        update();
    } else {
        m_hover = 0;
        updateGeometryAndBackdrop(true);
        startOpenAnimation();
    }
}

bool HomeToast::hasAction() const {
    return isVisible() && !m_closing && !m_isProgress && !m_actionText.isEmpty() && bool(m_action);
}

void HomeToast::triggerAction() {
    if (!hasAction())
        return;
    auto fn = std::move(m_action);
    dismiss();
    if (fn)
        fn();
}

QRect HomeToast::pillRect() const {
    return QRect(kToastPadX, kToastPadTop, m_pillW, kToastH);
}

QRect HomeToast::closeRect() const {
    if (m_isProgress)
        return {};
    const QRect pill = pillRect();
    return QRect(pill.right() - 10 - 26, pill.top() + 9, 26, 26);
}

QRect HomeToast::actionRect() const {
    if (m_actionText.isEmpty())
        return {};
    const QRect pill = pillRect();
    const int bw = QFontMetrics(uiFont(12, QFont::DemiBold)).horizontalAdvance(m_actionText) + 20;
    if (m_isProgress)
        return QRect(pill.right() - 10 - bw, pill.top() + 9, bw, 26);
    int rightEdge = closeRect().left() - 6;
    if (!m_shortcut.isEmpty())
        rightEdge -= QFontMetrics(uiFont(11, QFont::Medium)).horizontalAdvance(m_shortcut) + 10;
    return QRect(rightEdge - bw, pill.top() + 9, bw, 26);
}

void HomeToast::dismiss() {
    m_timer->stop();
    if (!isVisible() || m_closing)
        return;
    m_closing = true;
    m_hover = 0;
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_anim->stop();
    m_anim->setDuration(tokens::motion::duration(tokens::motion::Fast));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

void HomeToast::paintEvent(QPaintEvent*) {
    if (m_grabbingBackdrop)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect pill = pillRect();
    const bool reduced = tokens::reducedMotion();
    if (!reduced) {
        const qreal dy = m_closing ? ((1.0 - m_progress) * 8.0) : ((1.0 - m_progress) * 14.0);
        const qreal sc = m_closing ? 1.0 : (0.98 + 0.02 * m_progress);
        p.translate(pill.center().x(), pill.center().y() + dy);
        p.scale(sc, sc);
        p.translate(-pill.center());
    }
    p.setOpacity(m_progress);

    paintToastGlass(p, QRectF(pill), 12.0, m_backdrop);

    int x = pill.left() + 14;
    if (!m_icon.isEmpty()) {
        paintUiIcon(&p, QRectF(x, pill.top() + (kToastH - 15) / 2.0, 15, 15), m_icon, QColor(0xF4, 0xF4, 0xF5));
        x += 15 + 10;
    }

    const QFont tf = uiFont(12.5, QFont::Medium);
    p.setFont(tf);
    p.setPen(QColor(0xF4, 0xF4, 0xF5));

    if (m_isProgress) {
        const QRect actR = actionRect();
        const int counterW = progressCounterWidth();
        const int counterX = actR.left() - 12 - counterW;
        const int barX = counterX - 12 - kProgressBarW;
        const int textW = std::max(30, barX - 12 - x);
        p.drawText(QRect(x, pill.top(), textW, kToastH), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(tf).elidedText(m_text, Qt::ElideRight, textW));

        const QRectF barRect(barX, pill.center().y() - kProgressBarH / 2.0, kProgressBarW, kProgressBarH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 41));
        p.drawRoundedRect(barRect, 2.0, 2.0);
        const qreal ratio = (m_total > 0) ? std::clamp(qreal(m_done) / m_total, 0.0, 1.0) : 0.0;
        const qreal fillW = (m_total > 0 && m_done == 0) ? 0.0 : std::max<qreal>(4.0, kProgressBarW * ratio);
        if (fillW > 0.0) {
            p.setBrush(Qt::white);
            p.drawRoundedRect(QRectF(barRect.left(), barRect.top(), std::min<qreal>(kProgressBarW, fillW), kProgressBarH),
                              2.0, 2.0);
        }

        p.setFont(uiFont(11, QFont::Medium));
        p.setPen(QColor(0xA1, 0xA1, 0xAA));
        p.drawText(QRect(counterX, pill.top(), counterW, kToastH), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 de %2").arg(m_done).arg(m_total));
    } else {
        int textRight = closeRect().left() - 10;
        if (!m_shortcut.isEmpty()) {
            const QFont kf = uiFont(11, QFont::Medium);
            const int kw = QFontMetrics(kf).horizontalAdvance(m_shortcut);
            const int kx = closeRect().left() - 6 - kw;
            p.setFont(kf);
            p.setPen(QColor(0xA1, 0xA1, 0xAA));
            p.drawText(QRect(kx, pill.top(), kw, kToastH), Qt::AlignCenter, m_shortcut);
            textRight = kx - 10;
        }
        if (!m_actionText.isEmpty())
            textRight = actionRect().left() - 12;

        p.setFont(tf);
        p.setPen(QColor(0xF4, 0xF4, 0xF5));
        const int textW = std::max(24, textRight - x);
        p.drawText(QRect(x, pill.top(), textW, kToastH), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(tf).elidedText(m_text, Qt::ElideRight, textW));
    }

    if (!m_actionText.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, m_hover == 1 ? 46 : 31));
        p.drawRoundedRect(QRectF(actionRect()), 8, 8);
        p.setFont(uiFont(12, QFont::DemiBold));
        p.setPen(QColor(255, 255, 255));
        p.drawText(actionRect(), Qt::AlignCenter, m_actionText);
    }
    if (!m_isProgress) {
        if (m_hover == 2) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 26));
            p.drawRoundedRect(QRectF(closeRect()), 8, 8);
        }
        paintUiIcon(&p, QRectF(closeRect()).adjusted(7, 7, -7, -7), QStringLiteral("x"), QColor(0xA1, 0xA1, 0xAA));
    }
}

void HomeToast::mouseMoveEvent(QMouseEvent* e) {
    if (m_closing)
        return;
    const QPoint pos = e->position().toPoint();
    const int h = actionRect().contains(pos) ? 1 : ((!m_isProgress && closeRect().contains(pos)) ? 2 : 0);
    if (h != m_hover) {
        m_hover = h;
        setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void HomeToast::leaveEvent(QEvent*) {
    if (m_hover != 0) {
        m_hover = 0;
        update();
    }
}

void HomeToast::mouseReleaseEvent(QMouseEvent* e) {
    if (m_closing || e->button() != Qt::LeftButton)
        return;
    const QPoint pos = e->position().toPoint();
    if (actionRect().contains(pos)) {
        auto fn = std::move(m_action);
        dismiss();
        if (fn)
            fn();
    } else if (!m_isProgress && closeRect().contains(pos)) {
        dismiss();
    }
}

// ============================================================== ConfirmDialog
ConfirmDialog::ConfirmDialog(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_anim = new QPropertyAnimation(this, "progress", this);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (!m_closing)
            return;
        m_closing = false;
        const bool accepted = m_pendingAccept;
        m_pendingAccept = false;
        auto fn = std::move(m_onConfirm);
        m_onConfirm = {};
        m_scrimBackdrop = QImage();
        m_cardBackdrop = QImage();
        hide();
        if (accepted && fn)
            fn();
    });
    hide();
}

void ConfirmDialog::setProgress(qreal p) {
    m_progress = std::clamp(p, 0.0, 1.0);
    update();
}

void ConfirmDialog::applyTheme(bool dark) {
    m_dark = dark;
    update();
}

void ConfirmDialog::captureBackdrops() {
    if (!parentWidget() || !parentWidget()->isVisible() || tokens::reducedMotion()) {
        m_scrimBackdrop = QImage();
        m_cardBackdrop = QImage();
        return;
    }
    m_grabbingBackdrop = true;
    const QImage full = parentWidget()->grab(parentWidget()->rect()).toImage();
    const QImage card = parentWidget()->grab(cardRect()).toImage();
    m_grabbingBackdrop = false;
    m_scrimBackdrop = blurBackdropImage(full, 4);
    m_cardBackdrop = blurBackdropImage(card, 11);
}

void ConfirmDialog::ask(const QString& icon, const QString& title, const QString& body,
                        const QString& confirmText, std::function<void()> onConfirm) {
    m_anim->stop();
    m_closing = false;
    m_pendingAccept = false;
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    if (isVisible())
        hide();

    m_icon = icon;
    m_title = title;
    m_body = body;
    m_confirm = confirmText;
    m_onConfirm = std::move(onConfirm);
    m_hover = 0;
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    captureBackdrops();

    m_progress = 0.0;
    show();
    raise();
    setFocus(Qt::PopupFocusReason);

    m_anim->setDuration(tokens::motion::duration(tokens::motion::Base));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

int ConfirmDialog::bodyHeight() const {
    const QFontMetrics fm(uiFont(12.5, QFont::Normal));
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
    if (m_grabbingBackdrop)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    const bool reduced = tokens::reducedMotion();

    if (!m_scrimBackdrop.isNull() && !reduced) {
        p.save();
        p.setOpacity(m_progress);
        p.drawImage(rect(), m_scrimBackdrop);
        p.restore();
    }
    p.fillRect(rect(), m_dark ? QColor(0, 0, 0, qRound(90 * m_progress))
                              : QColor(24, 24, 27, qRound(36 * m_progress)));

    const QRect c = cardRect();
    p.save();
    p.setOpacity(m_progress);
    if (!reduced) {
        const qreal sc = m_closing ? (0.98 + 0.02 * m_progress) : (0.96 + 0.04 * m_progress);
        p.translate(c.center());
        p.scale(sc, sc);
        p.translate(-c.center());
    }
    paintLiquidGlassPanel(p, QRectF(c), tokens::radius::Card, m_cardBackdrop, homePaletteFor(m_dark), m_dark);

    p.setFont(uiFont(15, QFont::DemiBold));
    p.setPen(tk.text);
    p.drawText(QRect(c.left() + 20, c.top() + 20, 320, 22), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    p.setFont(uiFont(12.5, QFont::Normal));
    p.setPen(m_dark ? tk.muted : QColor(0x52, 0x52, 0x5B));
    p.drawText(QRect(c.left() + 20, c.top() + 20 + 22 + 14, 320, bodyHeight()), Qt::TextWordWrap, m_body);

    // Cancelar con anillo de foco (foco por defecto) y el botón peligroso en rojo.
    const QRectF cr = QRectF(cancelRect());
    p.setPen(QPen(tk.surface, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(cr.adjusted(-1.5, -1.5, 1.5, 1.5), 12, 12);
    p.setPen(QPen(tk.accent, 1.5));
    p.drawRoundedRect(cr.adjusted(-3.2, -3.2, 3.2, 3.2), 13.5, 13.5);
    p.setPen(Qt::NoPen);
    p.setBrush(m_hover == 1 ? tk.hover : tk.field);
    p.drawRoundedRect(cr, 11, 11);
    p.setBrush(m_hover == 2 ? tk.danger.darker(112) : tk.danger);
    p.drawRoundedRect(QRectF(confirmRect()), 11, 11);
    p.setFont(uiFont(13, QFont::DemiBold));
    p.setPen(tk.text);
    p.drawText(cancelRect(), Qt::AlignCenter, QStringLiteral("Cancelar"));
    p.setPen(Qt::white);
    p.drawText(confirmRect(), Qt::AlignCenter, m_confirm);
    p.restore();
}

void ConfirmDialog::mouseMoveEvent(QMouseEvent* e) {
    if (m_closing)
        return;
    const QPoint pos = e->position().toPoint();
    const int h = cancelRect().contains(pos) ? 1 : (confirmRect().contains(pos) ? 2 : 0);
    if (h != m_hover) {
        m_hover = h;
        setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void ConfirmDialog::mouseReleaseEvent(QMouseEvent* e) {
    if (m_closing || e->button() != Qt::LeftButton)
        return;
    const QPoint pos = e->position().toPoint();
    if (confirmRect().contains(pos))
        finish(true);
    else if (cancelRect().contains(pos) || !cardRect().contains(pos))
        finish(false);
}

void ConfirmDialog::keyPressEvent(QKeyEvent* e) {
    if (m_closing)
        return;
    if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
        finish(false); // el foco por defecto está en «Cancelar»
}

void ConfirmDialog::finish(bool accepted) {
    if (!isVisible() || m_closing)
        return;
    m_closing = true;
    m_pendingAccept = accepted;
    m_hover = 0;
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_anim->stop();
    m_anim->setDuration(tokens::motion::duration(tokens::motion::Fast));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

} // namespace finily
