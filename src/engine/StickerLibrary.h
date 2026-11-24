#pragma once

#include "core/Types.h"

#include <QPixmap>
#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace finily {

enum class StickerStyle {
    CleanWithTitle = 0, // Estilo A: esquema limpio + título breve arriba + fórmula abajo
    Card = 1,           // Estilo B: mini-pizarra / ficha con recuadro sutil, título y separador
    DiagramOnly = 2     // Solo dibujo vectorial sin título ni fórmula
};

struct StickerTemplate {
    QString id;
    QString title;
    QString category; // "Triángulos", "Notables", "Congruencia", "Circunferencia", "Métricas", "Áreas", "Mis ★"
    QStringList keywords;
    QString formula;
    bool isCustom = false;
    QSizeF baseSize{220.0, 140.0};
    QVector<ShapeItem> shapes;
    QVector<Stroke> strokes;
    QVector<TextItem> texts;
};

struct InstantiatedSticker {
    QVector<ShapeItem> shapes;
    QVector<Stroke> strokes;
    QVector<TextItem> texts;
    QString groupId;
    QString title;
};

QStringList stickerCategories();
QVector<StickerTemplate> allStickerTemplates();
std::optional<StickerTemplate> findStickerTemplate(const QString& id);

InstantiatedSticker instantiateSticker(const StickerTemplate& tpl,
                                       const QPointF& centerWorld,
                                       const ThemeConfig& theme,
                                       StickerStyle style,
                                       double scale = 1.0);

QPixmap renderStickerThumbnail(const StickerTemplate& tpl,
                               const QSize& pixelSize,
                               const ThemeConfig& theme,
                               StickerStyle style);

bool saveCustomSticker(const QString& title,
                       const QString& category,
                       const QVector<Stroke>& strokes,
                       const QVector<ShapeItem>& shapes,
                       const QVector<TextItem>& texts,
                       QString* outId = nullptr);

bool deleteCustomSticker(const QString& id);

} // namespace finily
