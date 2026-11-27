#pragma once

#include "engine/LibraryStore.h"

#include <QRectF>

class QPainter;

namespace finily {

// Dibujo de los «objetos» de la biblioteca (mockup 00): sin caja dura,
// cada objeto es su propia silueta con sombra suave. Todos los tamaños son
// el tamaño visual en píxeles lógicos; `bottomCenter` ancla la base.
namespace libobj {

constexpr qreal kVisualHeight = 136.0; // alto del área .vis (112 del mockup × ~1.2)

// Carpeta de dos tonos; `open` (0..1) levanta la tapa al pasar el cursor.
QRectF folderRect(const QPointF& bottomCenter);
void paintFolder(QPainter& p, const QPointF& bottomCenter, int colorIndex, int iconIndex, qreal open,
                 bool dark);

// Pizarra infinita: marco redondeado con la vista previa y «∞».
QRectF boardRect(const QPointF& bottomCenter);
void paintBoard(QPainter& p, const QPointF& bottomCenter, const NotebookMeta& meta, bool dark);

// Cuaderno con tapa de color, lomo, etiqueta y formato.
QRectF bookRect(const QPointF& bottomCenter, PageFormat fmt);
void paintBook(QPainter& p, const QPointF& bottomCenter, const NotebookMeta& meta, bool dark);

// Documento PDF: hoja blanca con primera página y etiqueta «PDF».
QRectF pdfRect(const QPointF& bottomCenter);
void paintPdf(QPainter& p, const QPointF& bottomCenter, const NotebookMeta& meta, bool dark);

// Rectángulo del objeto que corresponde a un documento.
QRectF documentRect(const QPointF& bottomCenter, const NotebookMeta& meta);
void paintDocument(QPainter& p, const QPointF& bottomCenter, const NotebookMeta& meta, bool dark);

// Sombra difusa equivalente a `0 10px 22px -8px` del mockup.
void paintObjectShadow(QPainter& p, const QRectF& r, qreal radius, qreal lift, bool dark);

} // namespace libobj
} // namespace finily
