#include "ui/home/CreateMenu.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

#include <algorithm>

namespace finily {
namespace {

constexpr int kMenuWidth = 300;
constexpr int kPad = 8;
constexpr int kRowHeight = 52;
constexpr int kSepGap = 11;

} // namespace

CreateMenu::CreateMenu(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_StyledBackground, false);
    m_rows = {
        {QStringLiteral("bolt"), QStringLiteral("Nota rápida"),
         QStringLiteral("Lienzo al instante"), false},
        {QStringLiteral("board"), QStringLiteral("Pizarra"),
         QStringLiteral("Espacio infinito"), false},
        {QStringLiteral("notebook"), QStringLiteral("Cuaderno"),
         QStringLiteral("Hojas A4 o 16:9"), false},
        {QStringLiteral("folder"), QStringLiteral("Carpeta"),
         QStringLiteral("Agrupa por curso o tema"), true},
        {QStringLiteral("pdf"), QStringLiteral("Importar PDF"),
         QStringLiteral("Anota sobre un archivo"), false},
    };
    m_anim = new QPropertyAnimation(this, "progress", this);
    m_anim->setDuration(140);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    hide();
}

void CreateMenu::setProgress(qreal p) {
    m_progress = std::clamp(p, 0.0, 1.0);
    update();
}

void CreateMenu::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    update();
}

void CreateMenu::openAnchoredTo(const QRect& anchorInParent, const QString& currentFolderName) {
    if (!parentWidget())
        return;
    m_rows[3].subtitle = currentFolderName.isEmpty()
        ? QStringLiteral("Agrupa por curso o tema")
        : QStringLiteral("Subcarpeta en «%1»").arg(currentFolderName);
    if (isVisible())
        hide();
    setGeometry(parentWidget()->rect());
    updateCardGeometry(anchorInParent, true);
    m_hoveredRow = -1;
    m_pressedInside = false;
    m_progress = 0.0;
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
    m_anim->stop();
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void CreateMenu::reposition(const QRect& anchorInParent) {
    if (!isVisible() || !parentWidget())
        return;
    setGeometry(parentWidget()->rect());
    updateCardGeometry(anchorInParent, false);
    update();
}

void CreateMenu::closeMenu() {
    if (!isVisible())
        return;
    m_anim->stop();
    m_hoveredRow = -1;
    m_blurredBg = QImage();
    hide();
    emit closed();
}

void CreateMenu::updateCardGeometry(const QRect& anchorInParent, bool grabBackdrop) {
    int totalH = kPad * 2 + int(m_rows.size()) * kRowHeight;
    for (const auto& r : m_rows) {
        if (r.separatorAbove)
            totalH += kSepGap;
    }

    int x = anchorInParent.right() + 12;
    int y = anchorInParent.top() + 8;
    if (anchorInParent.height() < 60) {
        // Botón «+ Nuevo» de la cabecera: el menú cae debajo, alineado a su borde derecho.
        x = anchorInParent.right() - kMenuWidth;
        y = anchorInParent.bottom() + 8;
    } else if (x + kMenuWidth > width() - 14) {
        const int leftSide = anchorInParent.left() - kMenuWidth - 12;
        x = (leftSide >= 14) ? leftSide : std::clamp(anchorInParent.left(), 14, std::max(14, width() - kMenuWidth - 14));
        if (leftSide < 14 && anchorInParent.bottom() + 8 + totalH <= height() - 14)
            y = anchorInParent.bottom() + 8;
    }
    x = std::clamp(x, 14, std::max(14, width() - kMenuWidth - 14));
    y = std::clamp(y, 14, std::max(14, height() - totalH - 14));
    m_cardRect = QRect(x, y, kMenuWidth, totalH);

    if (grabBackdrop && parentWidget()->isVisible() && !m_cardRect.isEmpty()) {
        const QPixmap snap = parentWidget()->grab(m_cardRect);
        m_blurredBg = blurBackdropImage(snap.toImage(), 11);
    }
}

QRect CreateMenu::rowRect(int index) const {
    if (index < 0 || index >= m_rows.size())
        return {};
    int y = m_cardRect.top() + kPad;
    for (int i = 0; i <= index; ++i) {
        if (m_rows.at(i).separatorAbove)
            y += kSepGap;
        if (i == index)
            return QRect(m_cardRect.left() + kPad, y, kMenuWidth - kPad * 2, kRowHeight);
        y += kRowHeight;
    }
    return {};
}

int CreateMenu::rowAt(const QPoint& pos) const {
    for (int i = 0; i < m_rows.size(); ++i) {
        if (rowRect(i).contains(pos))
            return i;
    }
    return -1;
}

void CreateMenu::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal dy = (1.0 - m_progress) * 6.0;
    painter.setOpacity(m_progress);
    const QRectF card = QRectF(m_cardRect).translated(0.0, dy);
    paintLiquidGlassPanel(painter, card, 16.0, m_blurredBg, m_pal, m_dark);

    for (int i = 0; i < m_rows.size(); ++i) {
        const auto& row = m_rows.at(i);
        const QRect rr = rowRect(i).translated(0, qRound(dy));
        if (row.separatorAbove) {
            const int sy = rr.top() - kSepGap / 2;
            painter.fillRect(QRect(m_cardRect.left() + 14, sy, kMenuWidth - 28, 1), m_pal.border);
        }
        if (i == m_hoveredRow) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_pal.hover);
            painter.drawRoundedRect(QRectF(rr), 10.0, 10.0);
        }

        const QRect iconBox(rr.left() + 8, rr.top() + 9, 34, 34);
        painter.setPen(Qt::NoPen);
        painter.setBrush(i == 0 ? m_pal.tint : m_pal.surfaceAlt);
        painter.drawRoundedRect(QRectF(iconBox), 9.0, 9.0);

        const QColor iconColor = (i == 0) ? m_pal.accent : m_pal.text;
        homeGlyph(row.glyph, iconColor).paint(&painter, iconBox.adjusted(8, 8, -8, -8));

        const int textX = iconBox.right() + 12;
        const int textW = std::max(40, rr.right() - textX - 10);

        QFont tf = painter.font();
        tf.setPixelSize(13);
        tf.setWeight(QFont::DemiBold);
        painter.setFont(tf);
        painter.setPen(m_pal.text);
        painter.drawText(QRect(textX, rr.top() + 7, textW, 18), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(tf).elidedText(row.title, Qt::ElideRight, textW));

        QFont sf = painter.font();
        sf.setPixelSize(11);
        sf.setWeight(QFont::Normal);
        painter.setFont(sf);
        painter.setPen(m_pal.muted);
        painter.drawText(QRect(textX, rr.top() + 26, textW, 17), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(sf).elidedText(row.subtitle, Qt::ElideRight, textW));
    }
}

void CreateMenu::mouseMoveEvent(QMouseEvent* event) {
    const int next = rowAt(event->pos());
    if (next != m_hoveredRow) {
        m_hoveredRow = next;
        setCursor(next >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void CreateMenu::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (!m_cardRect.contains(event->pos())) {
            closeMenu();
            return;
        }
        m_pressedInside = true;
    }
}

void CreateMenu::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    const int idx = m_pressedInside ? rowAt(event->pos()) : -1;
    m_pressedInside = false;
    closeMenu();
    switch (idx) {
        case 0: emit quickNoteChosen(); break;
        case 1: emit boardChosen(); break;
        case 2: emit notebookChosen(); break;
        case 3: emit folderChosen(); break;
        case 4: emit importPdfChosen(); break;
        default: break;
    }
}

void CreateMenu::leaveEvent(QEvent*) {
    if (m_hoveredRow != -1) {
        m_hoveredRow = -1;
        update();
    }
}

void CreateMenu::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        closeMenu();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace finily
