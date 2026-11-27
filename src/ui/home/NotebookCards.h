#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/HomeTheme.h"

#include <QVector>
#include <QWidget>

class QPropertyAnimation;
class QToolButton;

namespace finily {

class NotebookCard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal lift READ lift WRITE setLift)

public:
    static constexpr int kCardWidth = 184;
    static constexpr int kCardHeight = 258;

    explicit NotebookCard(const NotebookMeta& meta, QWidget* parent = nullptr);

    QString filePath() const { return m_meta.path; }
    QString title() const { return m_meta.title; }
    const NotebookMeta& meta() const { return m_meta; }

    qreal lift() const { return m_lift; }
    void setLift(qreal v);

    void setPalette(const HomePalette& pal, bool dark);
    void setAvailableFolders(const QVector<FolderMeta>& folders);
    bool matchesSearch(const QString& needle) const;
    bool matchesFilter(int filter) const;

signals:
    void openRequested(const QString& filePath);
    void renameRequested();
    void deleteRequested();
    void editCoverRequested(const finily::NotebookMeta& meta);
    void moveToFolderRequested(const QString& filePath, const QString& targetRelativeFolder);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void animateLift(qreal target);
    void paintInfiniteBoard(QPainter& painter, const QRectF& frame) const;
    void paintNotebookCover(QPainter& painter, const QRectF& frame) const;
    void paintPdfBadge(QPainter& painter, const QRectF& area) const;
    void paintFooter(QPainter& painter) const;
    void showMenu(const QPoint& at);
    QString summarySubtitle() const;

    NotebookMeta m_meta;
    QVector<FolderMeta> m_folders;
    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    bool m_hovered = false;
    int m_cover = 0;
    qreal m_lift = 0.0;
    QToolButton* m_moreBtn = nullptr;
    QPropertyAnimation* m_anim = nullptr;
};

} // namespace finily
