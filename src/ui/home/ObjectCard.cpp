#include "ui/home/ObjectCard.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/LibraryObjects.h"

#include <QAbstractAnimation>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVariantAnimation>

#include <algorithm>
#include <functional>

namespace finily {
namespace {
constexpr qreal kVisTop = 12.0;
constexpr qreal kNameTop = kVisTop + libobj::kVisualHeight + 11.0;

QPainterPath starIconPath(const QRectF& target) {
    // Misma geometría que resources/icons/ui/star.svg (viewBox 24x24).
    static constexpr QPointF pts[10] = {
        {12.0, 3.5}, {14.6, 8.8}, {20.4, 9.6}, {16.2, 13.7}, {17.2, 19.5},
        {12.0, 16.8}, {6.8, 19.5}, {7.8, 13.7}, {3.6, 9.6}, {9.4, 8.8},
    };
    QPainterPath path;
    for (int i = 0; i < 10; ++i) {
        const QPointF p(target.left() + (pts[i].x() / 24.0) * target.width(),
                        target.top() + (pts[i].y() / 24.0) * target.height());
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

class RenameKeyFilter : public QObject {
public:
    RenameKeyFilter(QObject* parent, std::function<void()> onCancel)
        : QObject(parent), m_onCancel(std::move(onCancel)) {}

protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            m_onCancel();
            return true;
        }
        return QObject::eventFilter(o, e);
    }

private:
    std::function<void()> m_onCancel;
};

} // namespace

ObjectCard::ObjectCard(QWidget* parent)
    : QWidget(parent) {
    setFixedSize(kCardWidth, kCardHeight);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_Hover, true);
    setAutoFillBackground(false);
    setMouseTracking(true);

    m_hoverAnim = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnim->setDuration(tokens::motion::Fast);
    m_hoverAnim->setEasingCurve(tokens::motion::out());

    m_pressAnim = new QPropertyAnimation(this, "pressScale", this);
}

void ObjectCard::setHoverProgress(qreal v) {
    m_hover = std::clamp(v, 0.0, 1.0);
    update();
}

void ObjectCard::setPressScale(qreal v) {
    m_pressScale = v;
    update();
}

void ObjectCard::setAppearScale(qreal v) {
    m_appearScale = v;
    if (!m_appearFadeAnim)
        m_appearOpacity = std::clamp((v - 0.8) / 0.2, 0.0, 1.0);
    update();
}

void ObjectCard::playPopIn() {
    if (m_appearAnim) {
        m_appearAnim->stop();
        m_appearAnim->deleteLater();
        m_appearAnim = nullptr;
    }
    if (m_appearFadeAnim) {
        m_appearFadeAnim->stop();
        m_appearFadeAnim->deleteLater();
        m_appearFadeAnim = nullptr;
    }

    if (tokens::reducedMotion()) {
        m_appearScale = 1.0;
        m_appearOpacity = 0.0;
        auto* fade = new QVariantAnimation(this);
        fade->setDuration(tokens::motion::Reduced);
        fade->setEasingCurve(QEasingCurve::Linear);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        connect(fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
            m_appearOpacity = std::clamp(val.toReal(), 0.0, 1.0);
            update();
        });
        connect(fade, &QVariantAnimation::finished, this, [this, fade]() {
            if (m_appearFadeAnim == fade)
                m_appearFadeAnim = nullptr;
            m_appearOpacity = 1.0;
            update();
        });
        m_appearFadeAnim = fade;
        fade->start(QAbstractAnimation::DeleteWhenStopped);
        return;
    }

    m_appearScale = 0.8;
    m_appearOpacity = 0.0;
    auto* anim = new QPropertyAnimation(this, "appearScale", this);
    anim->setDuration(tokens::motion::Slow);
    anim->setEasingCurve(tokens::motion::spring());
    anim->setStartValue(0.8);
    anim->setEndValue(1.0);
    connect(anim, &QPropertyAnimation::finished, this, [this, anim]() {
        if (m_appearAnim == anim)
            m_appearAnim = nullptr;
        m_appearScale = 1.0;
        m_appearOpacity = 1.0;
        update();
    });
    m_appearAnim = anim;
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ObjectCard::setPalette(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    if (m_renameEdit)
        updateRenameStyle();
    update();
}

void ObjectCard::setSelected(bool on) {
    if (m_selected == on)
        return;
    m_selected = on;
    update();
}

void ObjectCard::setDropTarget(bool on) {
    if (m_dropTarget == on)
        return;
    m_dropTarget = on;
    update();
}

void ObjectCard::setDimmed(bool on) {
    if (m_dimmed == on)
        return;
    m_dimmed = on;
    update();
}

void ObjectCard::setFavoriteBadge(bool on) {
    if (m_favoriteBadge == on)
        return;
    m_favoriteBadge = on;
    update();
}

void ObjectCard::setShowDetail(bool show) {
    if (m_showDetail == show)
        return;
    m_showDetail = show;
    update();
}

void ObjectCard::setShowMoreButton(bool show) {
    if (m_showMoreButton == show)
        return;
    m_showMoreButton = show;
    update();
}

qreal ObjectCard::visualLift() const {
    return tokens::reducedMotion() ? 0.0 : m_hover * 3.0;
}

QPointF ObjectCard::visualBottomCenter() const {
    return QPointF(width() / 2.0, kVisTop + libobj::kVisualHeight - visualLift());
}

QRectF ObjectCard::visualBounds(const QPointF& bc) const {
    return QRectF(bc.x() - 92.0, bc.y() - libobj::kVisualHeight, 184.0, libobj::kVisualHeight);
}

bool ObjectCard::isActionButtonAt(const QPointF& pos) const {
    return m_showMoreButton && moreButtonRect().adjusted(-3, -3, 3, 3).contains(pos);
}

void ObjectCard::paintVisualWithLift(QPainter& painter, const QPointF& bc, qreal lift) {
    Q_UNUSED(lift);
    paintVisual(painter, bc);
}

QRect ObjectCard::visualRectInCard() const {
    const QPointF bc = visualBottomCenter();
    const QRectF raw = visualBounds(bc).adjusted(-6.0, -4.0, 6.0, 10.0);
    return raw.toAlignedRect().intersected(rect());
}

QPixmap ObjectCard::grabVisualPixmap() const {
    const QRect r = visualRectInCard();
    if (r.isEmpty())
        return {};
    constexpr qreal kDpr = 2.0;
    QPixmap pix(int(r.width() * kDpr), int(r.height() * kDpr));
    pix.setDevicePixelRatio(kDpr);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.translate(-r.topLeft());
    const_cast<ObjectCard*>(this)->paintVisualWithLift(p, visualBottomCenter(), 0.0);
    p.end();
    return pix;
}

QRectF ObjectCard::moreButtonRect() const {
    return QRectF(width() - 36.0, 10.0, 28.0, 28.0);
}

void ObjectCard::startPressAnim(qreal target, int durationMs, const QEasingCurve& curve) {
    if (!m_pressAnim)
        return;
    m_pressAnim->stop();
    m_pressAnim->setDuration(durationMs);
    m_pressAnim->setEasingCurve(curve);
    m_pressAnim->setStartValue(m_pressScale);
    m_pressAnim->setEndValue(target);
    m_pressAnim->start();
}

void ObjectCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_appearOpacity < 1.0 || m_appearScale != 1.0) {
        painter.setOpacity(m_appearOpacity);
        const QPointF center = QRectF(rect()).center();
        painter.translate(center);
        painter.scale(m_appearScale, m_appearScale);
        painter.translate(-center);
    }

    const tokens::Palette tk = tokens::paletteFor(m_dark);

    // Solo selección (.obj.sel) o destino de arrastre (.obj.drop) pintan caja; nunca en hover.
    if (m_selected || m_dropTarget) {
        const QColor borderCol = m_dropTarget
            ? tk.accent
            : (m_dark ? QColor(255, 255, 255, 56) : QColor(24, 24, 27, 41));
        painter.setPen(QPen(borderCol, 1.5));
        painter.setBrush(tk.hover);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 14.0, 14.0);
    }

    const QPointF bc = visualBottomCenter();
    const qreal itemOpacity = contentOpacity();

    painter.save();
    painter.setOpacity(painter.opacity() * itemOpacity);
    if (m_pressScale != 1.0) {
        painter.translate(bc);
        painter.scale(m_pressScale, m_pressScale);
        painter.translate(-bc);
    }
    paintVisualWithLift(painter, bc, visualLift());
    painter.restore();

    const qreal w = width() - 20.0;
    QFont nf = QApplication::font();
    nf.setPixelSize(14);
    nf.setWeight(QFont::Medium);
    painter.save();
    painter.setOpacity(painter.opacity() * itemOpacity);
    painter.setFont(nf);
    painter.setPen(m_pal.text);
    if (!m_renameEdit) {
        const QFontMetrics fm(nf);
        const QString shown = fm.elidedText(nameText(), Qt::ElideRight, int(w));
        if (!m_highlight.isEmpty()) {
            const int at = shown.indexOf(m_highlight, 0, Qt::CaseInsensitive);
            if (at >= 0) {
                const qreal tw = fm.horizontalAdvance(shown);
                const qreal x0 = 10 + (w - tw) / 2.0 + fm.horizontalAdvance(shown.left(at));
                const qreal mw = fm.horizontalAdvance(shown.mid(at, m_highlight.size()));
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_dark ? QColor(250, 204, 21, 80) : QColor(250, 204, 21, 115));
                painter.drawRoundedRect(QRectF(x0 - 1, kNameTop + 0.5, mw + 2, 19), 3, 3);
                painter.setPen(m_pal.text);
            }
        }
        painter.drawText(QRectF(10, kNameTop, w, 20), Qt::AlignHCenter | Qt::AlignVCenter, shown);
    }

    if (!m_renameError.isEmpty()) {
        QFont ef = QApplication::font();
        ef.setPixelSize(11);
        ef.setWeight(QFont::Medium);
        const QString errShown = QFontMetrics(ef).elidedText(m_renameError, Qt::ElideRight, int(width() - 24));
        const qreal tw = QFontMetrics(ef).horizontalAdvance(errShown);
        const qreal pw = std::min(width() - 12.0, tw + 14.0);
        const QRectF pill((width() - pw) / 2.0, kNameTop + 25.0, pw, 18.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tk.dangerSoft);
        painter.drawRoundedRect(pill, 6.0, 6.0);
        painter.setFont(ef);
        painter.setPen(tk.danger);
        painter.drawText(pill, Qt::AlignCenter, errShown);
    } else if (m_showDetail) {
        QFont df = QApplication::font();
        df.setPixelSize(12);
        painter.setFont(df);
        painter.setPen(m_pal.muted);
        painter.drawText(QRectF(10, kNameTop + 21, w, 17), Qt::AlignHCenter | Qt::AlignVCenter,
                         QFontMetrics(df).elidedText(m_hasDetailOverride ? m_detailOverride : detailText(),
                                                     Qt::ElideMiddle, int(w)));
    }
    painter.restore();

    if (m_favoriteBadge) {
        const qreal fadeOut = m_showMoreButton ? (1.0 - std::clamp((m_hover - 0.2) / 0.4, 0.0, 1.0)) : 1.0;
        if (fadeOut > 0.01) {
            painter.save();
            painter.setOpacity(painter.opacity() * fadeOut);
            painter.setPen(Qt::NoPen);
            painter.setBrush(tk.faint);
            painter.drawPath(starIconPath(QRectF(width() - 30.0, 14.0, 15.0, 15.0)));
            painter.restore();
        }
    }

    if (m_showMoreButton && m_hover > 0.5) {
        const QRectF mb = moreButtonRect();
        painter.save();
        painter.setOpacity(painter.opacity() * ((m_hover - 0.5) * 2.0));
        painter.setPen(QPen(m_pal.border, 1));
        painter.setBrush(m_pal.surface);
        painter.drawEllipse(mb);
        paintUiIcon(&painter, mb.adjusted(6, 6, -6, -6), QStringLiteral("more"), m_pal.icon);
        painter.restore();
    }
}

void ObjectCard::enterEvent(QEnterEvent*) {
    m_hoverAnim->stop();
    m_hoverAnim->setDuration(tokens::motion::duration(tokens::motion::Fast));
    m_hoverAnim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
    m_hoverAnim->setStartValue(m_hover);
    m_hoverAnim->setEndValue(1.0);
    m_hoverAnim->start();
}

void ObjectCard::leaveEvent(QEvent*) {
    m_hoverAnim->stop();
    m_hoverAnim->setDuration(tokens::motion::duration(tokens::motion::Fast));
    m_hoverAnim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
    m_hoverAnim->setStartValue(m_hover);
    m_hoverAnim->setEndValue(0.0);
    m_hoverAnim->start();

    if (m_pressed) {
        m_pressed = false;
        if (tokens::reducedMotion())
            setPressScale(1.0);
        else
            startPressAnim(1.0, tokens::motion::Base, tokens::motion::spring());
    }
}

void ObjectCard::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->position().toPoint();
        m_draggingOut = false;
        if (!m_renameEdit && !isActionButtonAt(event->position())) {
            m_pressed = true;
            m_canStartDrag = true;
            if (!tokens::reducedMotion())
                startPressAnim(0.96, 90, tokens::motion::in());
        } else {
            m_pressed = false;
            m_canStartDrag = false;
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ObjectCard::mouseMoveEvent(QMouseEvent* event) {
    if ((event->buttons() & Qt::LeftButton) && m_canStartDrag && !m_draggingOut && !m_renameEdit) {
        if ((event->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_draggingOut = true;
            m_canStartDrag = false;
            m_pressed = false;
            if (m_pressAnim)
                m_pressAnim->stop();
            setPressScale(1.0);
            emit dragStarted(this, event->globalPosition().toPoint());
            event->accept();
            return;
        }
    }
    QWidget::mouseMoveEvent(event);
}

void ObjectCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const bool wasDraggingOut = m_draggingOut;
        const bool wasPressed = m_pressed;
        m_pressed = false;
        m_canStartDrag = false;

        if (wasPressed) {
            if (tokens::reducedMotion())
                setPressScale(1.0);
            else
                startPressAnim(1.0, tokens::motion::Base, tokens::motion::spring());
        }

        if (wasDraggingOut) {
            m_draggingOut = false;
            event->accept();
            return;
        }

        if (rect().contains(event->pos())) {
            if (m_showMoreButton && moreButtonRect().adjusted(-3, -3, 3, 3).contains(event->position())) {
                emit menuRequested(mapToGlobal(moreButtonRect().bottomLeft().toPoint() + QPoint(0, 4)));
                event->accept();
                return;
            }
            if (isActionButtonAt(event->position())) {
                QWidget::mouseReleaseEvent(event);
                return;
            }
            if (!m_renameEdit) {
                if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
                    emit selectionClicked(this, event->modifiers());
                else
                    activate();
            }
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ObjectCard::mouseDoubleClickEvent(QMouseEvent* event) {
    // Un clic ya abre; ignorar el doble clic evita abrir dos veces.
    event->accept();
}

void ObjectCard::contextMenuEvent(QContextMenuEvent* event) {
    emit menuRequested(event->globalPos());
}

void ObjectCard::updateRenameStyle() {
    if (!m_renameEdit)
        return;
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    const QColor borderCol = m_renameError.isEmpty() ? tk.accent : tk.danger;
    m_renameEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; color: %2; border: 1.5px solid %3; border-radius: 6px;"
        " padding: 0 4px; selection-background-color: %4; selection-color: %2; }")
        .arg(tk.surface.name(), tk.text.name(), borderCol.name(), tk.soft.name(QColor::HexArgb)));
}

void ObjectCard::openRenameEditor(const QString& initialText) {
    if (m_renameEdit)
        return;
    m_renameEdit = new QLineEdit(initialText, this);
    m_renameEdit->setAlignment(Qt::AlignCenter);
    m_renameEdit->setFrame(false);
    QFont nf = QApplication::font();
    nf.setPixelSize(14);
    nf.setWeight(QFont::Medium);
    m_renameEdit->setFont(nf);
    updateRenameStyle();
    m_renameEdit->setGeometry(8, int(kNameTop) - 3, width() - 16, 26);
    m_renameEdit->installEventFilter(new RenameKeyFilter(m_renameEdit, [this] { finishRename(false); }));
    connect(m_renameEdit, &QLineEdit::textChanged, this, [this] {
        if (!m_renameError.isEmpty()) {
            m_renameError.clear();
            updateRenameStyle();
            update();
        }
    });
    connect(m_renameEdit, &QLineEdit::returnPressed, this, [this] { finishRename(true); });
    connect(m_renameEdit, &QLineEdit::editingFinished, this, [this] {
        finishRename(m_renameError.isEmpty());
    });
    m_renameEdit->show();
    m_renameEdit->setFocus(Qt::OtherFocusReason);
    m_renameEdit->selectAll();
    update();
}

void ObjectCard::beginRename() {
    if (m_renameEdit)
        return;
    m_renameError.clear();
    m_lastRenameAttempt.clear();
    openRenameEditor(renameText());
}

void ObjectCard::setRenameError(const QString& message) {
    m_renameError = message.trimmed();
    if (m_renameError.isEmpty()) {
        if (m_renameEdit)
            updateRenameStyle();
        update();
        return;
    }
    if (!m_renameEdit) {
        const QString seed = !m_lastRenameAttempt.isEmpty() ? m_lastRenameAttempt : renameText();
        openRenameEditor(seed);
    } else {
        updateRenameStyle();
        m_renameEdit->setFocus(Qt::OtherFocusReason);
        m_renameEdit->selectAll();
    }
    update();
}

void ObjectCard::finishRename(bool commit) {
    if (!m_renameEdit)
        return;
    QLineEdit* edit = m_renameEdit;
    m_renameEdit = nullptr; // evita reentrar con editingFinished
    const QString text = edit->text().trimmed();
    edit->hide();
    edit->deleteLater();
    if (commit && !text.isEmpty() && text != renameText()) {
        m_lastRenameAttempt = text;
        m_renameError.clear();
        update();
        emit renameCommitted(text);
        return;
    }
    m_renameError.clear();
    m_lastRenameAttempt.clear();
    update();
}

void ObjectCard::setHighlight(const QString& needle) {
    m_highlight = needle.trimmed();
    update();
}

void ObjectCard::setDetailOverride(const QString& detail) {
    m_detailOverride = detail;
    m_hasDetailOverride = true;
    update();
}

} // namespace finily
