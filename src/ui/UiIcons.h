#pragma once

#include <QColor>
#include <QIcon>
#include <QRectF>
#include <QString>

class QPainter;

namespace finily {

// Íconos de interfaz como datos (DESIGN.md principio 6): cada ícono es un SVG
// de trazo en resources/icons/<pack>/<nombre>.svg con stroke="currentColor".
// Un archivo con el mismo nombre en <datos de la app>/icons/<pack>/ lo reemplaza,
// así un paquete de íconos del usuario no requiere recompilar.
//   pack "ui"     -> navegación y acciones
//   pack "folder" -> símbolos sobre las carpetas
void paintUiIcon(QPainter* painter, const QRectF& rect, const QString& name,
                 const QColor& color, const QString& pack = QStringLiteral("ui"));
QIcon uiIcon(const QString& name, const QColor& color, int logicalSize = 16,
             const QString& pack = QStringLiteral("ui"));

} // namespace finily
