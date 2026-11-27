#include "ui/home/TrashCards.h"

#include "ui/UiIcons.h"
#include "ui/home/LibraryObjects.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace finily {

TrashCard::TrashCard(const TrashEntry& entry, QWidget* parent)
    : ObjectCard(parent)
    , m_entry(entry) {
    const QString holder = QFileInfo(entry.itemPath).absolutePath();
    if (entry.isFolder) {
        for (const FolderMeta& f : LibraryStore::listFoldersInDirectories({holder}, QString())) {
            if (f.name == QFileInfo(entry.itemPath).fileName())
                m_folder = f;
        }
    } else {
        for (const NotebookMeta& m : LibraryStore::listNotebooksInDirectories({holder}, QString())) {
            if (QFileInfo(m.path).fileName() == QFileInfo(entry.itemPath).fileName()) {
                m_doc = m;
                m_hasDoc = true;
            }
        }
    }
}

int TrashCard::daysLeft() const {
    const qint64 used = m_entry.deletedAt.daysTo(QDateTime::currentDateTimeUtc());
    return int(std::max<qint64>(0, LibraryStore::kTrashRetentionDays - used));
}

bool TrashCard::matchesSearch(const QString& needle) const {
    return needle.isEmpty() || m_entry.name.contains(needle, Qt::CaseInsensitive);
}

void TrashCard::paintVisual(QPainter& painter, const QPointF& bc) {
    painter.save();
    painter.setOpacity(0.5 + 0.25 * hoverProgress());
    if (m_entry.isFolder)
        libobj::paintFolder(painter, bc, m_folder.colorIndex, m_folder.iconIndex, 0.0, m_dark);
    else if (m_hasDoc)
        libobj::paintDocument(painter, bc, m_doc, m_dark);
    painter.restore();
}

QString TrashCard::detailText() const {
    const int d = daysLeft();
    if (d <= 0)
        return QStringLiteral("Se elimina hoy");
    return d == 1 ? QStringLiteral("Queda 1 día") : QStringLiteral("Quedan %1 días").arg(d);
}

void TrashCard::activate() {
    emit menuRequested(mapToGlobal(QPoint(width() / 2 - 40, int(libobj::kVisualHeight) - 10)));
}

void TrashCard::paintEvent(QPaintEvent* event) {
    // Rojo cuando quedan 3 días o menos: se repinta la paleta solo para el detalle.
    if (daysLeft() <= 3) {
        const QColor saved = m_pal.muted;
        m_pal.muted = QColor(0xB4, 0x23, 0x18);
        if (m_dark)
            m_pal.muted = QColor(0xF9, 0x70, 0x66);
        ObjectCard::paintEvent(event);
        m_pal.muted = saved;
        return;
    }
    ObjectCard::paintEvent(event);
}

TrashInfoBar::TrashInfoBar(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(44);
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground, false);
}

void TrashInfoBar::setPalette(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    update();
}

void TrashInfoBar::setCount(int count) {
    m_count = count;
    update();
}

QRect TrashInfoBar::buttonRect() const {
    QFont f = font();
    f.setPixelSize(12);
    f.setWeight(QFont::DemiBold);
    const int w = QFontMetrics(f).horizontalAdvance(QStringLiteral("Vaciar papelera")) + 26;
    return QRect(width() - w - 7, (height() - 30) / 2, w, 30);
}

void TrashInfoBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(m_pal.hover);
    p.drawRoundedRect(QRectF(rect()), 12, 12);

    paintUiIcon(&p, QRectF(14, (height() - 15) / 2.0, 15, 15), QStringLiteral("info"), m_pal.muted);
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(m_pal.muted);
    p.drawText(QRect(38, 0, buttonRect().left() - 46, height()), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Los elementos se eliminan para siempre %1 días después de llegar aquí.")
                   .arg(LibraryStore::kTrashRetentionDays));

    if (m_count <= 0)
        return;
    const QRect b = buttonRect();
    const QColor danger = m_dark ? QColor(0xF9, 0x70, 0x66) : QColor(0xB4, 0x23, 0x18);
    p.setPen(QPen(m_hover ? danger : m_pal.border, 1));
    QColor fill = m_pal.surface;
    if (m_hover) {
        fill = danger;
        fill.setAlpha(m_dark ? 40 : 18);
    }
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(b).adjusted(0.5, 0.5, -0.5, -0.5), 15, 15);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(danger);
    p.drawText(b, Qt::AlignCenter, QStringLiteral("Vaciar papelera"));
}

void TrashInfoBar::mouseMoveEvent(QMouseEvent* event) {
    const bool h = m_count > 0 && buttonRect().contains(event->pos());
    if (h != m_hover) {
        m_hover = h;
        setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void TrashInfoBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_count > 0 && buttonRect().contains(event->pos()))
        emit emptyRequested();
}

void TrashInfoBar::leaveEvent(QEvent*) {
    m_hover = false;
    update();
}

} // namespace finily
