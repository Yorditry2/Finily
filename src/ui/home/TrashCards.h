#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/ObjectCard.h"

namespace finily {

// Elemento en la papelera (mockup 09): el objeto atenuado y «Quedan N días»
// (en rojo cuando faltan 3 o menos). Un clic abre Restaurar / Eliminar.
class TrashCard : public ObjectCard {
    Q_OBJECT

public:
    explicit TrashCard(const TrashEntry& entry, QWidget* parent = nullptr);

    const TrashEntry& entry() const { return m_entry; }
    int daysLeft() const;
    bool matchesSearch(const QString& needle) const;

protected:
    void paintVisual(QPainter& painter, const QPointF& bottomCenter) override;
    QString nameText() const override { return m_entry.name; }
    QString detailText() const override;
    void activate() override;
    void paintEvent(QPaintEvent* event) override;

private:
    TrashEntry m_entry;
    bool m_hasDoc = false;
    NotebookMeta m_doc;
    FolderMeta m_folder;
};

// Barra informativa de la Papelera: «Se eliminan para siempre tras 30 días» + «Vaciar papelera».
class TrashInfoBar : public QWidget {
    Q_OBJECT

public:
    explicit TrashInfoBar(QWidget* parent = nullptr);
    void setPalette(const HomePalette& pal, bool dark);
    void setCount(int count);

signals:
    void emptyRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect buttonRect() const;

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    bool m_hover = false;
    int m_count = 0;
};

} // namespace finily
