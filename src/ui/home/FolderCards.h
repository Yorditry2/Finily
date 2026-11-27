#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/HomeTheme.h"

#include <QWidget>

class QPropertyAnimation;
class QToolButton;

namespace finily {

// Tarjeta '+' en la grilla que abre el menú desplegable Liquid Glass de creación.
class CreateTileCard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal lift READ lift WRITE setLift)

public:
    static constexpr int kCardWidth = 184;
    static constexpr int kCardHeight = 258;

    explicit CreateTileCard(QWidget* parent = nullptr);

    qreal lift() const { return m_lift; }
    void setLift(qreal v);
    void setPalette(const HomePalette& pal, bool dark);

signals:
    void createClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void animateLift(qreal target);

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    bool m_hovered = false;
    qreal m_lift = 0.0;
    QPropertyAnimation* m_anim = nullptr;
};

// Tarjeta de carpeta o subcarpeta en la grilla.
class FolderCard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal lift READ lift WRITE setLift)

public:
    static constexpr int kCardWidth = 184;
    static constexpr int kCardHeight = 258;

    explicit FolderCard(const FolderMeta& meta, QWidget* parent = nullptr);

    const FolderMeta& meta() const { return m_meta; }
    qreal lift() const { return m_lift; }
    void setLift(qreal v);

    void setPalette(const HomePalette& pal, bool dark);
    bool matchesSearch(const QString& needle) const;

signals:
    void openRequested(const QString& relativePath);
    void newSubfolderRequested(const QString& relativePath);
    void editRequested(const finily::FolderMeta& meta);
    void renameRequested(const QString& relativePath);
    void deleteRequested(const QString& relativePath);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void animateLift(qreal target);
    void showMenu(const QPoint& at);
    QString summarySubtitle() const;

    FolderMeta m_meta;
    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    bool m_hovered = false;
    qreal m_lift = 0.0;
    QToolButton* m_moreBtn = nullptr;
    QPropertyAnimation* m_anim = nullptr;
};

} // namespace finily
