#include "ui/home/ObjectCard.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/LibraryObjects.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

#include <functional>

namespace finily {
namespace {
constexpr qreal kVisTop = 10.0;
constexpr qreal kNameTop = kVisTop + libobj::kVisualHeight + 9.0;
} // namespace

ObjectCard::ObjectCard(QWidget* parent)
    : QWidget(parent) {
    setFixedSize(kCardWidth, kCardHeight);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_Hover, true);
    setAutoFillBackground(false);
    setMouseTracking(true);

    m_anim = new QPropertyAnimation(this, "hoverProgress", this);
    m_anim->setDuration(tokens::motion::Fast);
    m_anim->setEasingCurve(tokens::motion::out());
}

void ObjectCard::setHoverProgress(qreal v) {
    m_hover = v;
    update();
}

void ObjectCard::setPalette(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    update();
}

QPointF ObjectCard::visualBottomCenter() const {
    return QPointF(width() / 2.0, kVisTop + libobj::kVisualHeight - 2.0 * m_hover);
}

QRectF ObjectCard::moreButtonRect() const {
    return QRectF(width() - 32.0, 8.0, 24.0, 24.0);
}

void ObjectCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_hover > 0.01) {
        QColor bg = m_pal.hover;
        bg.setAlphaF(bg.alphaF() * m_hover);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(QRectF(rect()), 14, 14);
    }

    paintVisual(painter, visualBottomCenter());

    const qreal w = width() - 20.0;
    QFont nf = font();
    nf.setPixelSize(12);
    nf.setWeight(QFont::Medium);
    painter.setFont(nf);
    painter.setPen(m_pal.text);
    if (!m_renameEdit)
        painter.drawText(QRectF(10, kNameTop, w, 17), Qt::AlignHCenter | Qt::AlignVCenter,
                         QFontMetrics(nf).elidedText(nameText(), Qt::ElideRight, int(w)));

    QFont df = font();
    df.setPixelSize(11);
    painter.setFont(df);
    painter.setPen(m_pal.muted);
    painter.drawText(QRectF(10, kNameTop + 18, w, 15), Qt::AlignHCenter | Qt::AlignVCenter,
                     QFontMetrics(df).elidedText(detailText(), Qt::ElideRight, int(w)));

    if (m_hover > 0.5) {
        const QRectF mb = moreButtonRect();
        painter.setOpacity((m_hover - 0.5) * 2.0);
        painter.setPen(QPen(m_pal.border, 1));
        painter.setBrush(m_pal.surface);
        painter.drawEllipse(mb);
        paintUiIcon(&painter, mb.adjusted(5, 5, -5, -5), QStringLiteral("more"), m_pal.icon);
        painter.setOpacity(1.0);
    }
}

void ObjectCard::enterEvent(QEnterEvent*) {
    m_anim->stop();
    m_anim->setStartValue(m_hover);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void ObjectCard::leaveEvent(QEvent*) {
    m_anim->stop();
    m_anim->setStartValue(m_hover);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

void ObjectCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        if (moreButtonRect().adjusted(-3, -3, 3, 3).contains(event->position()))
            emit menuRequested(mapToGlobal(moreButtonRect().bottomLeft().toPoint() + QPoint(0, 4)));
        else
            activate();
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

namespace {
// Esc cancela; Enter lo resuelve returnPressed.
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

void ObjectCard::beginRename() {
    if (m_renameEdit)
        return;
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    m_renameEdit = new QLineEdit(renameText(), this);
    m_renameEdit->setAlignment(Qt::AlignCenter);
    m_renameEdit->setFrame(false);
    QFont nf = font();
    nf.setPixelSize(12);
    nf.setWeight(QFont::Medium);
    m_renameEdit->setFont(nf);
    m_renameEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; color: %2; border: 1.5px solid %3; border-radius: 6px;"
        " padding: 0 4px; selection-background-color: %4; selection-color: %2; }")
        .arg(tk.surface.name(), tk.text.name(), tk.accent.name(), tk.soft.name(QColor::HexArgb)));
    m_renameEdit->setGeometry(8, int(kNameTop) - 3, width() - 16, 23);
    m_renameEdit->installEventFilter(new RenameKeyFilter(m_renameEdit, [this] { finishRename(false); }));
    connect(m_renameEdit, &QLineEdit::returnPressed, this, [this] { finishRename(true); });
    connect(m_renameEdit, &QLineEdit::editingFinished, this, [this] { finishRename(true); });
    m_renameEdit->show();
    m_renameEdit->setFocus(Qt::OtherFocusReason);
    m_renameEdit->selectAll();
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
    update();
    if (commit && !text.isEmpty() && text != renameText())
        emit renameCommitted(text);
}

} // namespace finily
