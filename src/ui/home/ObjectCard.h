#pragma once

#include "ui/home/HomeTheme.h"

#include <QWidget>

class QLineEdit;
class QPropertyAnimation;

namespace finily {

// Base de los objetos de la rejilla del Inicio (.obj del mockup 00):
// sin caja; al pasar el cursor aparece un fondo suave, el objeto se eleva
// y surge el botón «más». Las subclases pintan solo su silueta.
class ObjectCard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    static constexpr int kCardWidth = 176;
    static constexpr int kCardHeight = 194;

    explicit ObjectCard(QWidget* parent = nullptr);

    qreal hoverProgress() const { return m_hover; }
    void setHoverProgress(qreal v);
    virtual void setPalette(const HomePalette& pal, bool dark);

    // Renombrar en línea (F2): un campo sobre el nombre; Enter o clic fuera guarda, Esc cancela.
    void beginRename();
    bool isRenaming() const { return m_renameEdit != nullptr; }

signals:
    void menuRequested(const QPoint& globalPos);
    void renameCommitted(const QString& newName);

protected:
    // Base del área visual: el objeto se apoya aquí y crece hacia arriba.
    QPointF visualBottomCenter() const;
    virtual void paintVisual(QPainter& painter, const QPointF& bottomCenter) = 0;
    virtual QString nameText() const = 0;
    virtual QString detailText() const = 0;
    virtual void activate() = 0;
    // Texto inicial del campo al renombrar (por defecto, el nombre visible).
    virtual QString renameText() const { return nameText(); }

    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;

private:
    QRectF moreButtonRect() const;
    void finishRename(bool commit);

    qreal m_hover = 0.0;
    QPropertyAnimation* m_anim = nullptr;
    QLineEdit* m_renameEdit = nullptr;
};

} // namespace finily
