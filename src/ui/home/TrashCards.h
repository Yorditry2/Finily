#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/ObjectCard.h"

namespace finily {

// Elemento en la papelera (mockup 09): el objeto atenuado, «Quedan N días»
// (en rojo cuando faltan 3 o menos) y botones rápidos de restaurar / eliminar al pasar el cursor.
class TrashCard : public ObjectCard {
    Q_OBJECT

public:
    explicit TrashCard(const TrashEntry& entry, QWidget* parent = nullptr);

    const TrashEntry& entry() const { return m_entry; }
    int daysLeft() const;
    bool matchesSearch(const QString& needle) const;

signals:
    void restoreRequested(const QString& id);
    void restorePromptRequested(const finily::TrashEntry& entry);
    void deleteForeverRequested(const finily::TrashEntry& entry);

protected:
    qreal contentOpacity() const override { return 0.55 + 0.30 * hoverProgress(); }
    QRectF visualBounds(const QPointF& bottomCenter) const override;
    bool isActionButtonAt(const QPointF& pos) const override;
    void paintVisual(QPainter& painter, const QPointF& bottomCenter) override;
    void paintVisualWithLift(QPainter& painter, const QPointF& bottomCenter, qreal lift) override;
    QString nameText() const override { return m_entry.name; }
    QString detailText() const override;
    void activate() override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF restoreButtonRect() const;
    QRectF deleteButtonRect() const;

    TrashEntry m_entry;
    bool m_hasDoc = false;
    NotebookMeta m_doc;
    FolderMeta m_folder;
    int m_hoverBtn = 0; // 0 = ninguno, 1 = restaurar, 2 = eliminar
};

// Barra informativa de la Papelera (mockup 09 escena 05): solo aviso de retención.
class TrashInfoBar : public QWidget {
    Q_OBJECT

public:
    explicit TrashInfoBar(QWidget* parent = nullptr);
    void setPalette(const HomePalette& pal, bool dark);
    void setCount(int count);
    void setRetentionDays(int days);

signals:
    void emptyRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    int m_count = 0;
    int m_retentionDays = 30;
};

} // namespace finily
