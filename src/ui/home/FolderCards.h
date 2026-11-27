#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/ObjectCard.h"

namespace finily {

// Carpeta o subcarpeta en la rejilla: silueta de dos tonos que se abre al pasar el cursor.
class FolderCard : public ObjectCard {
    Q_OBJECT

public:
    explicit FolderCard(const FolderMeta& meta, QWidget* parent = nullptr);

    const FolderMeta& meta() const { return m_meta; }
    bool matchesSearch(const QString& needle) const;

signals:
    void openRequested(const QString& relativePath);

protected:
    void paintVisual(QPainter& painter, const QPointF& bottomCenter) override;
    QString nameText() const override { return m_meta.name; }
    QString detailText() const override;
    void activate() override { emit openRequested(m_meta.relativePath); }

private:
    FolderMeta m_meta;
};

} // namespace finily
