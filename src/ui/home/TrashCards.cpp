#include "ui/home/TrashCards.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/LibraryObjects.h"

#include <QApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace finily {

TrashCard::TrashCard(const TrashEntry& entry, QWidget* parent)
    : ObjectCard(parent)
    , m_entry(entry) {
    setShowMoreButton(false);
    setDimmed(true);

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
    const int retention = LibraryStore::trashRetentionDays();
    if (retention <= 0)
        return -1;
    const qint64 used = m_entry.deletedAt.daysTo(QDateTime::currentDateTimeUtc());
    return int(std::max<qint64>(0, retention - used));
}

bool TrashCard::matchesSearch(const QString& needle) const {
    return needle.isEmpty() || m_entry.name.contains(needle, Qt::CaseInsensitive);
}

QRectF TrashCard::restoreButtonRect() const {
    return QRectF(width() - 10.0 - 30.0 - 5.0 - 30.0, 8.0, 30.0, 30.0);
}

QRectF TrashCard::deleteButtonRect() const {
    return QRectF(width() - 10.0 - 30.0, 8.0, 30.0, 30.0);
}

bool TrashCard::isActionButtonAt(const QPointF& pos) const {
    return hoverProgress() > 0.35
        && (restoreButtonRect().contains(pos) || deleteButtonRect().contains(pos));
}

QRectF TrashCard::visualBounds(const QPointF& bc) const {
    if (m_entry.isFolder)
        return libobj::folderRect(bc);
    if (m_hasDoc)
        return libobj::documentRect(bc, m_doc);
    return ObjectCard::visualBounds(bc);
}

void TrashCard::paintVisual(QPainter& painter, const QPointF& bc) {
    paintVisualWithLift(painter, bc, 0.0);
}

void TrashCard::paintVisualWithLift(QPainter& painter, const QPointF& bc, qreal lift) {
    if (m_entry.isFolder)
        libobj::paintFolder(painter, bc, m_folder.colorIndex, m_folder.iconIndex, 0.0, m_dark);
    else if (m_hasDoc)
        libobj::paintDocument(painter, bc, m_doc, m_dark, lift);
}

QString TrashCard::detailText() const {
    const int d = daysLeft();
    QString base;
    if (d < 0)
        base = QStringLiteral("Sin borrado automático");
    else if (d == 0)
        base = QStringLiteral("Se elimina hoy");
    else if (d == 1)
        base = QStringLiteral("Queda 1 día");
    else
        base = QStringLiteral("Quedan %1 días").arg(d);

    if (m_entry.isFolder && m_folder.documentCount > 0) {
        const QString items = m_folder.documentCount == 1
            ? QStringLiteral("1 elemento")
            : QStringLiteral("%1 elementos").arg(m_folder.documentCount);
        return QStringLiteral("%1 · %2").arg(base, items);
    }
    return base;
}

void TrashCard::activate() {
    emit selectionClicked(this, Qt::NoModifier);
}

void TrashCard::paintEvent(QPaintEvent* event) {
    const int left = daysLeft();
    if (left >= 0 && left <= 3) {
        const QColor saved = m_pal.muted;
        m_pal.muted = tokens::paletteFor(m_dark).danger;
        ObjectCard::paintEvent(event);
        m_pal.muted = saved;
    } else {
        ObjectCard::paintEvent(event);
    }

    if (hoverProgress() <= 0.35)
        return;

    const tokens::Palette tk = tokens::paletteFor(m_dark);
    const qreal alpha = std::clamp((hoverProgress() - 0.35) / 0.65, 0.0, 1.0);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(alpha);

    auto drawAction = [&](const QRectF& rect, const QString& icon, const QColor& iconColor, bool hovered, bool danger) {
        paintSoftShadow(p, rect, 8.0, 1.5, m_dark);
        p.setPen(QPen(tk.border, 1.0));
        QColor fill = tk.surface;
        if (hovered)
            fill = danger ? tk.dangerSoft : tk.field;
        p.setBrush(fill);
        p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 9.0, 9.0);
        paintUiIcon(&p, rect.adjusted(7.0, 7.0, -7.0, -7.0), icon, iconColor);
    };

    drawAction(restoreButtonRect(), QStringLiteral("restore"), tk.icon, m_hoverBtn == 1, false);
    drawAction(deleteButtonRect(), QStringLiteral("trash"), tk.danger, m_hoverBtn == 2, true);
}

void TrashCard::mouseMoveEvent(QMouseEvent* event) {
    int btn = 0;
    if (hoverProgress() > 0.35) {
        if (restoreButtonRect().contains(event->position()))
            btn = 1;
        else if (deleteButtonRect().contains(event->position()))
            btn = 2;
    }
    if (btn != m_hoverBtn) {
        m_hoverBtn = btn;
        update();
    }
    ObjectCard::mouseMoveEvent(event);
}

void TrashCard::leaveEvent(QEvent* event) {
    m_hoverBtn = 0;
    ObjectCard::leaveEvent(event);
}

void TrashCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !isDraggingOut() && hoverProgress() > 0.35
        && rect().contains(event->pos())) {
        if (restoreButtonRect().contains(event->position())) {
            emit restoreRequested(m_entry.id);
            event->accept();
            return;
        }
        if (deleteButtonRect().contains(event->position())) {
            emit deleteForeverRequested(m_entry);
            event->accept();
            return;
        }
    }
    ObjectCard::mouseReleaseEvent(event);
}

void TrashCard::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !isActionButtonAt(event->position())) {
        emit restorePromptRequested(m_entry);
        event->accept();
        return;
    }
    ObjectCard::mouseDoubleClickEvent(event);
}

// ============================================================== TrashInfoBar
TrashInfoBar::TrashInfoBar(QWidget* parent)
    : QWidget(parent)
    , m_retentionDays(LibraryStore::trashRetentionDays()) {
    setFixedHeight(38);
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

void TrashInfoBar::setRetentionDays(int days) {
    m_retentionDays = std::max(0, days);
    update();
}

void TrashInfoBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = tokens::paletteFor(m_dark);

    p.setPen(Qt::NoPen);
    p.setBrush(tk.field);
    p.drawRoundedRect(QRectF(rect()), 10.0, 10.0);

    paintUiIcon(&p, QRectF(14.0, (height() - 16.0) / 2.0, 16.0, 16.0), QStringLiteral("info"), tk.muted);

    QFont f = QApplication::font();
    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(tk.muted);

    const QString text = (m_retentionDays > 0)
        ? QStringLiteral("Lo que está aquí se elimina solo a los %1 días. Si restauras algo, vuelve a su carpeta original.")
              .arg(m_retentionDays)
        : QStringLiteral("La limpieza automática de la papelera está desactivada.");

    const int textLeft = 14 + 16 + 9;
    const int availW = std::max(0, width() - textLeft - 12);
    p.drawText(QRect(textLeft, 0, availW, height()), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(f).elidedText(text, Qt::ElideRight, availW));
}

} // namespace finily
