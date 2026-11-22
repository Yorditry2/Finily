#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace finily {

// Biblioteca unificada de iconos SVG de Finily (cuadrícula óptica 32x32,
// útiles ilustrados premium + herramientas duotono 100% homologadas).
QPixmap renderSvgIcon(const QString& name,
                      int logicalSize,
                      const QColor& strokeColor,
                      const QColor& accentColor,
                      const QColor& inkColor,
                      bool isDark);

QIcon makeStudioIcon(const QString& name,
                     int logicalSize,
                     const QColor& strokeColor,
                     const QColor& accentColor,
                     const QColor& inkColor,
                     bool isDark);

} // namespace finily
