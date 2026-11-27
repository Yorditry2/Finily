#pragma once

#include "ui/home/HomeTheme.h"

#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QWidget>

class QLineEdit;
class QPropertyAnimation;
class QVariantAnimation;

namespace finily {

// Base de los objetos de la rejilla del Inicio (.obj del mockup 00):
// sin caja al pasar el cursor; el propio objeto se eleva y abre su sombra.
// Las subclases pintan solo su silueta física.
class ObjectCard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal pressScale READ pressScale WRITE setPressScale)
    Q_PROPERTY(qreal appearScale READ appearScale WRITE setAppearScale)

public:
    static constexpr int kCardWidth = 216;
    static constexpr int kCardHeight = 238;

    explicit ObjectCard(QWidget* parent = nullptr);

    qreal hoverProgress() const { return m_hover; }
    void setHoverProgress(qreal v);
    qreal pressScale() const { return m_pressScale; }
    void setPressScale(qreal v);
    qreal appearScale() const { return m_appearScale; }
    void setAppearScale(qreal v);
    void playPopIn();

    virtual void setPalette(const HomePalette& pal, bool dark);

    void setSelected(bool on);
    bool isSelected() const { return m_selected; }
    void setDropTarget(bool on);
    bool isDropTarget() const { return m_dropTarget; }
    void setDimmed(bool on);
    bool isDimmed() const { return m_dimmed; }
    void setFavoriteBadge(bool on);
    bool hasFavoriteBadge() const { return m_favoriteBadge; }
    void setShowDetail(bool show);
    bool showDetail() const { return m_showDetail; }
    void setShowMoreButton(bool show);
    bool showMoreButton() const { return m_showMoreButton; }

    QPixmap grabVisualPixmap() const;
    QRect visualRectInCard() const;

    // Renombrar en línea (F2): un campo sobre el nombre; Enter o clic fuera guarda, Esc cancela.
    void beginRename();
    void setRenameError(const QString& message);
    bool isRenaming() const { return m_renameEdit != nullptr; }

    // Búsqueda (mockup 09): resalta la coincidencia en el nombre y muestra la
    // ruta («Clases › Física») en lugar de la fecha.
    void setHighlight(const QString& needle);
    void setDetailOverride(const QString& detail);
    QString displayName() const { return nameText(); }
    void open() { activate(); }

signals:
    void menuRequested(const QPoint& globalPos);
    void renameCommitted(const QString& newName);
    void selectionClicked(finily::ObjectCard* card, Qt::KeyboardModifiers modifiers);
    void dragStarted(finily::ObjectCard* card, const QPoint& globalPos);

protected:
    // Base del área visual: el objeto se apoya aquí y crece hacia arriba.
    QPointF visualBottomCenter() const;
    virtual qreal visualLift() const;
    virtual qreal contentOpacity() const { return m_dimmed ? 0.55 : 1.0; }
    virtual QRectF visualBounds(const QPointF& bottomCenter) const;
    virtual bool isActionButtonAt(const QPointF& pos) const;
    virtual void paintVisual(QPainter& painter, const QPointF& bottomCenter) = 0;
    virtual void paintVisualWithLift(QPainter& painter, const QPointF& bottomCenter, qreal lift);
    virtual QString nameText() const = 0;
    virtual QString detailText() const = 0;
    virtual void activate() = 0;
    // Texto inicial del campo al renombrar (por defecto, el nombre visible).
    virtual QString renameText() const { return nameText(); }
    bool isDraggingOut() const { return m_draggingOut; }

    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;

private:
    QRectF moreButtonRect() const;
    void openRenameEditor(const QString& initialText);
    void updateRenameStyle();
    void finishRename(bool commit);
    void startPressAnim(qreal target, int durationMs, const QEasingCurve& curve);

    qreal m_hover = 0.0;
    qreal m_pressScale = 1.0;
    qreal m_appearScale = 1.0;
    qreal m_appearOpacity = 1.0;

    QPropertyAnimation* m_hoverAnim = nullptr;
    QPropertyAnimation* m_pressAnim = nullptr;
    QPropertyAnimation* m_appearAnim = nullptr;
    QVariantAnimation* m_appearFadeAnim = nullptr;

    bool m_selected = false;
    bool m_dropTarget = false;
    bool m_dimmed = false;
    bool m_favoriteBadge = false;
    bool m_showDetail = true;
    bool m_showMoreButton = true;

    bool m_pressed = false;
    bool m_canStartDrag = false;
    bool m_draggingOut = false;
    QPoint m_pressPos;

    QLineEdit* m_renameEdit = nullptr;
    QString m_renameError;
    QString m_lastRenameAttempt;
    QString m_highlight;
    QString m_detailOverride;
    bool m_hasDetailOverride = false;
};

} // namespace finily
