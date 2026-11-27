#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/ObjectCard.h"

#include <QVector>

namespace finily {

// Documento de la biblioteca: pizarra, cuaderno o PDF (mockup 00).
class NotebookCard : public ObjectCard {
    Q_OBJECT

public:
    explicit NotebookCard(const NotebookMeta& meta, QWidget* parent = nullptr);

    QString filePath() const { return m_meta.path; }
    QString title() const { return m_meta.title; }
    const NotebookMeta& meta() const { return m_meta; }

    bool matchesSearch(const QString& needle) const;
    bool matchesFilter(int filter) const;

signals:
    void openRequested(const QString& filePath);

protected:
    void paintVisual(QPainter& painter, const QPointF& bottomCenter) override;
    QString nameText() const override { return m_meta.title; }
    QString detailText() const override;
    void activate() override { emit openRequested(m_meta.path); }

private:
    NotebookMeta m_meta;
};

} // namespace finily
