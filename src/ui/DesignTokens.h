#pragma once

// Tokens de diseño aprobados (DESIGN.md §4 y «Movimiento»).
// Fuente única de colores, radios y tiempos de la interfaz: no repetir
// valores sueltos en los widgets, leerlos de aquí.

#include "core/Utils.h"

#include <QColor>
#include <QEasingCurve>
#include <QPointF>

#include <algorithm>

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

inline constexpr int kAccentPresetCount = 5;

// Preferencias cacheadas en memoria para evitar lecturas de QSettings durante paintEvent.
inline bool& cachedReducedMotion() {
    static bool value = readAppSetting(QStringLiteral("ui/reducedMotion"), false).toBool();
    return value;
}

inline bool reducedMotion() { return cachedReducedMotion(); }

inline void setReducedMotion(bool on) {
    cachedReducedMotion() = on;
    writeAppSetting(QStringLiteral("ui/reducedMotion"), on);
}

inline int& cachedAccentPreset() {
    static int value = std::clamp(
        readAppSetting(QStringLiteral("ui/accentPreset"), 0).toInt(),
        0, kAccentPresetCount - 1);
    return value;
}

inline int accentPreset() { return cachedAccentPreset(); }

inline void setAccentPreset(int index) {
    const int clamped = std::clamp(index, 0, kAccentPresetCount - 1);
    cachedAccentPreset() = clamped;
    writeAppSetting(QStringLiteral("ui/accentPreset"), clamped);
}

// Muestra visual del preset de acento (mockup 10: Tinta, Grafito, Acero, Salvia, Ciruela).
inline QColor accentSwatchColor(int index) {
    static const QColor kLightAccents[kAccentPresetCount] = {
        QColor(0x18, 0x18, 0x1B), // 0: Tinta
        QColor(0x52, 0x52, 0x5B), // 1: Grafito
        QColor(0x3F, 0x5F, 0x8A), // 2: Acero
        QColor(0x4F, 0x7A, 0x64), // 3: Salvia
        QColor(0x8A, 0x4F, 0x6A), // 4: Ciruela
    };
    return kLightAccents[std::clamp(index, 0, kAccentPresetCount - 1)];
}

inline QColor accentColorFor(int index, bool dark) {
    static const QColor kDarkAccents[kAccentPresetCount] = {
        QColor(0xF4, 0xF4, 0xF5), // 0: Tinta (Marfil)
        QColor(0xA1, 0xA1, 0xAA), // 1: Grafito
        QColor(0x7C, 0xA2, 0xD4), // 2: Acero
        QColor(0x79, 0xB3, 0x95), // 3: Salvia
        QColor(0xC4, 0x83, 0xA1), // 4: Ciruela
    };
    const int clamped = std::clamp(index, 0, kAccentPresetCount - 1);
    return dark ? kDarkAccents[clamped] : accentSwatchColor(clamped);
}

inline Palette paletteFor(bool dark) {
    Palette p = dark ? darkPalette() : lightPalette();
    const int preset = accentPreset();
    if (preset > 0) {
        p.accent = accentColorFor(preset, dark);
        const double lum = 0.299 * p.accent.red() + 0.587 * p.accent.green() + 0.114 * p.accent.blue();
        p.accentFg = (lum < 150.0) ? QColor(0xFF, 0xFF, 0xFF) : QColor(0x18, 0x18, 0x1B);
    }
    return p;
}

// Carpetas de 2 tonos (paquete por defecto; el índice se guarda en la carpeta).
struct FolderColor {
    QColor body1;  // cuerpo, arriba
    QColor body2;  // cuerpo, abajo
    QColor flap;   // solapa trasera
    QColor ink;    // símbolo sobre la carpeta
};

inline const FolderColor& folderColor(int index) {
    static const FolderColor colors[] = {
        {QColor("#F2A4B9"), QColor("#E78FA8"), QColor("#D57892"), QColor("#8C3952")}, // rosa
        {QColor("#EDD1B8"), QColor("#E1C0A2"), QColor("#CCA887"), QColor("#7A5739")}, // arena
        {QColor("#A6CEBB"), QColor("#92BFA9"), QColor("#77A38E"), QColor("#315C48")}, // salvia
        {QColor("#A9C7E8"), QColor("#92B5DD"), QColor("#7499C6"), QColor("#2F5482")}, // cielo
        {QColor("#C5B7E6"), QColor("#B3A2DC"), QColor("#9682C6"), QColor("#4F3D85")}, // lavanda
        {QColor("#F5BFA0"), QColor("#EDA984"), QColor("#D98B64"), QColor("#8A4526")}, // durazno
        {QColor("#EDE09A"), QColor("#E3D27C"), QColor("#C9B65C"), QColor("#6B5B17")}, // limón
        {QColor("#B9BCC6"), QColor("#A3A7B3"), QColor("#868B99"), QColor("#363A45")}, // grafito
    };
    constexpr int count = int(sizeof(colors) / sizeof(colors[0]));
    return colors[((index % count) + count) % count];
}

namespace radius {
inline constexpr qreal Card = 18.0;      // tarjetas flotantes, diálogos
inline constexpr qreal Popover = 16.0;   // popover de color
inline constexpr qreal Menu = 14.0;      // menú + Nuevo, contextual
inline constexpr qreal Control = 11.0;   // CTA, campos, muestras
inline constexpr qreal Nav = 9.0;        // ítems de navegación
} // namespace radius

namespace space {
inline constexpr int SidebarWidth = 248;
inline constexpr int HeaderHeight = 66;
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

inline int duration(int ms) { return reducedMotion() ? Reduced : ms; }
inline QEasingCurve curve(const QEasingCurve& normal) {
    return reducedMotion() ? QEasingCurve(QEasingCurve::Linear) : normal;
}
} // namespace motion

} // namespace finily::tokens
