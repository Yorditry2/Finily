#pragma once

// Tokens de diseño aprobados (DESIGN.md §4 y «Movimiento»).
// Fuente única de colores, radios y tiempos de la interfaz: no repetir
// valores sueltos en los widgets, leerlos de aquí.

#include <QColor>
#include <QEasingCurve>
#include <QPointF>

namespace finily::tokens {

struct Palette {
    QColor bg;          // área principal
    QColor sidebar;     // panel lateral
    QColor surface;     // tarjetas, menús, campos sobre fondo
    QColor field;       // relleno de campos y muestras
    QColor neutral;     // fondo neutro de héroes/plantillas
    QColor border;      // divisores sutiles
    QColor borderPop;   // borde de cápsulas flotantes
    QColor text;        // texto principal
    QColor muted;       // fechas, subtítulos
    QColor faint;       // marcadores de posición
    QColor icon;        // íconos de navegación
    QColor accent;      // CTA «+ Nuevo», «Crear…»
    QColor accentFg;    // texto/ícono sobre accent
    QColor soft;        // ítem activo en sidebar
    QColor hover;       // hover discreto
    QColor guide;       // guía vertical del árbol
    QColor danger;      // acciones irreversibles
    QColor dangerSoft;
    QColor glass;       // relleno translúcido de menús/tarjetas (sobre blur)
    QColor glassHi;     // brillo de 1 px en el borde superior
};

// Modo Claro «Tinta».
inline Palette lightPalette() {
    return {
        QColor("#FFFFFF"), QColor("#F6F6F4"), QColor("#FFFFFF"), QColor("#F4F4F5"),
        QColor("#F6F6F4"), QColor(15, 23, 42, 15), QColor(15, 23, 42, 20), QColor("#18181B"),
        QColor("#71717A"), QColor("#A1A1AA"), QColor("#3F3F46"), QColor("#18181B"),
        QColor("#FFFFFF"), QColor(24, 24, 27, 20), QColor(24, 24, 27, 11), QColor(24, 24, 27, 33),
        QColor("#B42318"), QColor(180, 35, 24, 20), QColor(255, 255, 255, 204),
        QColor(255, 255, 255, 242),
    };
}

// Modo Oscuro «Marfil sobre Grafito».
inline Palette darkPalette() {
    return {
        QColor("#1A1B22"), QColor("#15161C"), QColor("#23242D"), QColor("#262731"),
        QColor("#22232B"), QColor(255, 255, 255, 15), QColor(255, 255, 255, 23), QColor("#F4F4F5"),
        QColor("#9A9AA6"), QColor("#6B6B78"), QColor("#C9C9D1"), QColor("#F4F4F5"),
        QColor("#18181B"), QColor(255, 255, 255, 23), QColor(255, 255, 255, 13),
        QColor(255, 255, 255, 36), QColor("#F04438"), QColor(240, 68, 56, 28),
        QColor(35, 36, 45, 194), QColor(255, 255, 255, 18),
    };
}

inline Palette paletteFor(bool dark) { return dark ? darkPalette() : lightPalette(); }

namespace radius {
inline constexpr qreal Card = 18.0;      // tarjetas flotantes, diálogos
inline constexpr qreal Popover = 16.0;   // popover de color
inline constexpr qreal Menu = 14.0;      // menú + Nuevo, contextual
inline constexpr qreal Control = 11.0;   // CTA, campos, muestras
inline constexpr qreal Nav = 9.0;        // ítems de navegación
} // namespace radius

namespace space {
inline constexpr int SidebarWidth = 214;
inline constexpr int HeaderHeight = 58;
inline constexpr int FloatCardWidth = 344;
} // namespace space

// Movimiento: 3 duraciones y 3 curvas (DESIGN.md «Movimiento»).
namespace motion {
inline constexpr int Fast = 120;   // hover, pulsar, cerrar
inline constexpr int Base = 200;   // menús, búsqueda, avisos, diálogos
inline constexpr int Slow = 320;   // tarjetas laterales, carpeta, sidebar, papelera
inline constexpr int Theme = 420;  // revelado circular de tema
inline constexpr int Reduced = 80; // «Reducir animaciones»: solo fundidos

inline QEasingCurve bezier(qreal x1, qreal y1, qreal x2, qreal y2) {
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(x1, y1), QPointF(x2, y2), QPointF(1.0, 1.0));
    return curve;
}
inline QEasingCurve out() { return bezier(0.22, 1.0, 0.36, 1.0); }    // lo que entra
inline QEasingCurve in() { return bezier(0.4, 0.0, 1.0, 1.0); }       // lo que sale
inline QEasingCurve spring() { return bezier(0.34, 1.45, 0.64, 1.0); } // detalles pequeños
} // namespace motion

} // namespace finily::tokens
