#pragma once

#include "ui/home/HomeTheme.h"

#include <QImage>
#include <QRect>
#include <QVector>
#include <QWidget>

class QPropertyAnimation;

namespace finily {

// Menú desplegable Liquid Glass (Opción B — lista vertical en 2 bloques)
// anclado a la tarjeta '+ Nuevo'. Hijo directo de HomeScreen (sin Qt::Popup).
class CreateMenu : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit CreateMenu(QWidget* parent = nullptr);

    qreal progress() const { return m_progress; }
    void setProgress(qreal p);

    void applyTheme(const HomePalette& pal, bool dark);
    void openAnchoredTo(const QRect& anchorInParent, const QString& currentFolderName = {});
    void reposition(const QRect& anchorInParent);
    void closeMenu();

signals:
    void quickNoteChosen();
    void boardChosen();
    void notebookChosen();
    void folderChosen();
    void importPdfChosen();
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct MenuRow {
        QString glyph;
        QString title;
        QString subtitle;
        bool separatorAbove = false;
    };

    void updateCardGeometry(const QRect& anchorInParent, bool grabBackdrop);
    void paintRow(QPainter& painter, int index, const tokens::Palette& tk) const;
    int rowAt(const QPoint& pos) const;
    QRect rowRect(int index) const;

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    bool m_closing = false;
    bool m_grabbingBackdrop = false;
    QRect m_cardRect;
    QImage m_blurredBg;
    qreal m_progress = 1.0;
    int m_hoveredRow = -1;
    bool m_pressedInside = false;
    QVector<MenuRow> m_rows;
    QPropertyAnimation* m_anim = nullptr;
};

} // namespace finily
