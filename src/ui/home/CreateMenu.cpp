#include "ui/home/CreateMenu.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

#include <algorithm>

namespace finily {
namespace {

// Medidas del mockup 00 (.newmenu): 262 px, padding 5, filas de 46.
constexpr int kMenuWidth = 262;
constexpr int kPad = 5;
constexpr int kRowHeight = 46;
constexpr int kSepGap = 9;

} // namespace

CreateMenu::CreateMenu(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_StyledBackground, false);
    m_rows = {
        {QStringLiteral("pen"), QStringLiteral("Nota rápida"),
         QStringLiteral("Abre un lienzo al instante"), false},
        {QStringLiteral("board"), QStringLiteral("Pizarra infinita"),
         QStringLiteral("Lienzo sin límites"), false},
        {QStringLiteral("book"), QStringLiteral("Cuaderno"),
         QStringLiteral("Portada, hojas y plantilla"), false},
        {QStringLiteral("folder"), QStringLiteral("Carpeta"),
         QStringLiteral("Agrupa documentos"), true},
        {QStringLiteral("pdf"), QStringLiteral("Importar PDF"),
         QStringLiteral("Para anotar encima"), false},
    };
    m_anim = new QPropertyAnimation(this, "progress", this);
    m_anim->setDuration(tokens::motion::Base);
    m_anim->setEasingCurve(tokens::motion::out());
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
        ? QStringLiteral("Agrupa documentos")
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

    // Entrada: aparece escalando 0.96→1 desde la esquina del botón «Nuevo» (mockup 11).
    const QPointF origin(m_cardRect.right(), m_cardRect.top());
    const qreal sc = 0.96 + 0.04 * m_progress;
    painter.translate(origin);
    painter.scale(sc, sc);
    painter.translate(-origin);
    painter.setOpacity(m_progress);

    const tokens::Palette tk = tokens::paletteFor(m_dark);
    paintLiquidGlassPanel(painter, QRectF(m_cardRect), 14.0, m_blurredBg, m_pal, m_dark);

    for (int i = 0; i < m_rows.size(); ++i) {
        const auto& row = m_rows.at(i);
        const QRect rr = rowRect(i);
        if (row.separatorAbove) {
            const int sy = rr.top() - kSepGap / 2;
            painter.fillRect(QRect(m_cardRect.left() + 11, sy, kMenuWidth - 22, 1), tk.border);
        }
        const bool hov = i == m_hoveredRow;
        if (hov) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(tk.hover);
            painter.drawRoundedRect(QRectF(rr), 9.0, 9.0);
        }

        // Cuadro de ícono: neutro; al pasar el cursor se invierte (acento).
        const QRect iconBox(rr.left() + 8, rr.top() + 8, 30, 30);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hov ? tk.accent : tk.field);
        painter.drawRoundedRect(QRectF(iconBox), 9.0, 9.0);
        paintUiIcon(&painter, QRectF(iconBox).adjusted(7.5, 7.5, -7.5, -7.5), row.glyph,
                    hov ? tk.accentFg : tk.icon);

        const int kbdW = (i == 0) ? 50 : 0;
        const int textX = iconBox.right() + 12;
        const int textW = std::max(40, rr.right() - textX - 10 - kbdW);

        QFont tf = painter.font();
        tf.setPixelSize(12);
        tf.setWeight(QFont::DemiBold);
        painter.setFont(tf);
        painter.setPen(tk.text);
        painter.drawText(QRect(textX, rr.top() + 6, textW, 18), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(tf).elidedText(row.title, Qt::ElideRight, textW));

        QFont sf = painter.font();
        sf.setPixelSize(11);
        sf.setWeight(QFont::Normal);
        painter.setFont(sf);
        painter.setPen(tk.muted);
        painter.drawText(QRect(textX, rr.top() + 24, textW, 16), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(sf).elidedText(row.subtitle, Qt::ElideRight, textW));

        if (i == 0) {
            // Atajo solo en la acción más usada.
            const QRect kb(rr.right() - 8 - 46, rr.top() + 13, 46, 20);
            painter.setPen(QPen(tk.border, 1));
            painter.setBrush(tk.field);
            painter.drawRoundedRect(QRectF(kb).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            QFont kf = painter.font();
            kf.setPixelSize(10);
            kf.setWeight(QFont::Medium);
            painter.setFont(kf);
            painter.setPen(tk.faint);
            painter.drawText(kb, Qt::AlignCenter, QStringLiteral("Ctrl N"));
        }
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
