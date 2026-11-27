#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/ObjectCard.h"

#include <QVector>
#include <QWidget>

namespace finily {

// Documento de la biblioteca: pizarra, cuaderno o PDF (mockup 00).
class NotebookCard : public ObjectCard {
    Q_OBJECT

public:
    explicit NotebookCard(const NotebookMeta& meta, QWidget* parent = nullptr);

    QString filePath() const { return m_meta.path; }
    QString title() const { return m_meta.title; }
    const NotebookMeta& meta() const { return m_meta; }
    void setMeta(const NotebookMeta& meta);

    bool matchesSearch(const QString& needle) const;
    bool matchesFilter(int filter) const;

signals:
    void openRequested(const QString& filePath);

protected:
    QRectF visualBounds(const QPointF& bottomCenter) const override;
    void paintVisual(QPainter& painter, const QPointF& bottomCenter) override;
    void paintVisualWithLift(QPainter& painter, const QPointF& bottomCenter, qreal lift) override;
    QString nameText() const override { return m_meta.title; }
    QString detailText() const override;
    void activate() override { emit openRequested(m_meta.path); }

private:
    NotebookMeta m_meta;
};

// Marcador de importación en curso en la grilla (mockup 07, escena 08).
class ImportingCard : public QWidget {
    Q_OBJECT

public:
    ImportingCard(const QString& title, QWidget* parent = nullptr);

    void setDark(bool dark);
    void setStatus(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_title;
    QString m_status = QStringLiteral("Importando…");
    qreal m_phase = 0.0;
    bool m_dark = false;
};

} // namespace finily
