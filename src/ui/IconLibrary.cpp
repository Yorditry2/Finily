#include "ui/IconLibrary.h"

#include <QPainter>
#include <QSvgRenderer>

namespace finily {
namespace {

QString hex(const QColor& c) {
    return c.isValid() ? c.name(QColor::HexRgb) : QStringLiteral("#2563eb");
}

// Devuelve el documento SVG (viewBox 0 0 32 32) para cada útil o herramienta.
// Todos los iconos comparten la misma cuadrícula óptica 32x32, esquinas suaves
// y lenguaje visual coherente (útiles ilustrados arriba + duotono elegante abajo).
QString svgSourceFor(const QString& name,
                     const QColor& strokeColor,
                     const QColor& accentColor,
                     const QColor& inkColor,
                     bool isDark) {
    const QString st = hex(strokeColor);
    const QString ac = hex(accentColor);
    const QString ink = hex(inkColor);
    const QString bodyLight = isDark ? QStringLiteral("#e2e8f0") : QStringLiteral("#ffffff");
    const QString bodyMid = isDark ? QStringLiteral("#cbd5e1") : QStringLiteral("#e2e8f0");
    const QString bodyOutline = isDark ? QStringLiteral("#94a3b8") : QStringLiteral("#475569");
    const QString metalLight = QStringLiteral("#f1f5f9");
    const QString metalDark = QStringLiteral("#64748b");
    const QString duoFill = ac;
    const QString duoOpacity = isDark ? QStringLiteral("0.24") : QStringLiteral("0.16");

    // ================= 1. ÚTILES ILUSTRADOS PREMIUM (45° estudio) =================

    if (name == QLatin1String("fountain-pen")) {
        // Pluma fuente clásica en diagonal: barril elegante, anillo de color de tinta
        // y plumilla metálica en diamante con rendija y respiradero.
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <defs>
    <linearGradient id="fpBody" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#475569"/>
      <stop offset="50%" stop-color="#334155"/>
      <stop offset="100%" stop-color="#0f172a"/>
    </linearGradient>
    <linearGradient id="fpNib" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="%1"/>
      <stop offset="100%" stop-color="%2"/>
    </linearGradient>
  </defs>
  <!-- Sombra suave -->
  <ellipse cx="14" cy="27.5" rx="8" ry="1.8" fill="#0f172a" fill-opacity="0.12"/>
  <!-- Barril superior -->
  <path d="M25.5 4.2 C26.8 5.5 27.6 6.8 27.8 7.5 L19.4 15.9 L16.1 12.6 L24.5 4.2 Z"
        fill="url(#fpBody)" stroke="#1e293b" stroke-width="1.2" stroke-linejoin="round"/>
  <!-- Anillo del color de tinta activo -->
  <path d="M16.1 12.6 L19.4 15.9 L17.6 17.7 L14.3 14.4 Z"
        fill="%3" stroke="#1e293b" stroke-width="1.0" stroke-linejoin="round"/>
  <!-- Plumilla metálica en diamante -->
  <path d="M14.3 14.4 L17.6 17.7 L14.8 22.8 L6.2 25.8 L9.2 17.2 Z"
        fill="url(#fpNib)" stroke="#334155" stroke-width="1.25" stroke-linejoin="round"/>
  <!-- Rendija y respiradero -->
  <line x1="6.8" y1="25.2" x2="12.6" y2="19.4" stroke="#1e293b" stroke-width="1.15" stroke-linecap="round"/>
  <circle cx="13.2" cy="18.8" r="1.35" fill="%3" stroke="#1e293b" stroke-width="0.7"/>
</svg>)SVG")
            .arg(metalLight, metalDark, ink);
    }

    if (name == QLatin1String("gel-pen")) {
        // Bolígrafo gel real en diagonal: pulsador superior, clip metálico lateral,
        // cuerpo limpio, agarre ergonómico del color de tinta y punta fina precisa.
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <defs>
    <linearGradient id="gpBarrel" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="%1"/>
      <stop offset="100%" stop-color="%2"/>
    </linearGradient>
  </defs>
  <ellipse cx="14" cy="27.5" rx="7.5" ry="1.7" fill="#0f172a" fill-opacity="0.11"/>
  <!-- Pulsador superior del color de tinta -->
  <path d="M24.2 5.0 L26.0 3.2 C26.6 2.6 27.6 2.6 28.2 3.2 L28.8 3.8 C29.4 4.4 29.4 5.4 28.8 6.0 L27.0 7.8 Z"
        fill="%3" stroke="%4" stroke-width="1.1" stroke-linejoin="round"/>
  <!-- Clip lateral elegante -->
  <path d="M23.5 6.8 L17.8 12.5" stroke="%4" stroke-width="1.4" stroke-linecap="round"/>
  <!-- Cuerpo principal -->
  <path d="M24.2 5.0 L27.0 7.8 L16.2 18.6 L13.4 15.8 Z"
        fill="url(#gpBarrel)" stroke="%4" stroke-width="1.2" stroke-linejoin="round"/>
  <!-- Agarre ergonómico de color -->
  <path d="M13.4 15.8 L16.2 18.6 L12.5 22.3 L9.7 19.5 Z"
        fill="%3" stroke="%4" stroke-width="1.15" stroke-linejoin="round"/>
  <!-- Cono metálico + punta de bola -->
  <path d="M9.7 19.5 L12.5 22.3 L7.6 24.9 L7.1 24.4 Z"
        fill="#cbd5e1" stroke="%4" stroke-width="1.1" stroke-linejoin="round"/>
  <line x1="7.3" y1="24.7" x2="5.4" y2="26.6" stroke="%3" stroke-width="1.8" stroke-linecap="round"/>
</svg>)SVG")
            .arg(bodyLight, bodyMid, ink, bodyOutline);
    }

    if (name == QLatin1String("highlighter")) {
        // Resaltador inconfundible: cuerpo ancho biselado, franja vibrante de color,
        // punta de cincel ancha Y trazo fluorescente translúcido debajo de la punta.
        const QString hlColor = (inkColor == QColor("#0f172a") || inkColor == QColor("#f8fafc"))
                                    ? QStringLiteral("#facc15")
                                    : ink;
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <defs>
    <linearGradient id="hlBody" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="%1"/>
      <stop offset="100%" stop-color="%2"/>
    </linearGradient>
  </defs>
  <!-- Trazo de resaltado translúcido debajo de la punta (hace 100% obvio que es resaltador!) -->
  <path d="M4.5 25.5 L20.5 25.5" stroke="%3" stroke-width="4.4" stroke-linecap="round" stroke-opacity="0.45"/>
  <!-- Cuerpo ancho -->
  <path d="M21.2 4.2 L27.2 10.2 L16.6 20.8 L10.6 14.8 Z"
        fill="url(#hlBody)" stroke="%4" stroke-width="1.25" stroke-linejoin="round"/>
  <!-- Banda de color vibrante en el cuerpo -->
  <path d="M17.2 8.2 L23.2 14.2 L20.2 17.2 L14.2 11.2 Z"
        fill="%3" fill-opacity="0.92"/>
  <!-- Cuello -->
  <path d="M10.6 14.8 L16.6 20.8 L13.8 22.8 L8.8 17.8 Z"
        fill="#cbd5e1" stroke="%4" stroke-width="1.15" stroke-linejoin="round"/>
  <!-- Punta biselada de cincel -->
  <path d="M8.8 17.8 L13.8 22.8 L8.2 25.2 L5.8 22.8 Z"
        fill="%3" stroke="%4" stroke-width="1.15" stroke-linejoin="round"/>
</svg>)SVG")
            .arg(bodyLight, bodyMid, hlColor, bodyOutline);
    }

    if (name == QLatin1String("eraser")) {
        // Borrador en bloque real (NO parece plumón!): bloque grueso rectangular
        // inclinado con mitad goma rosa pastel 3D + mitad funda de papel celeste/blanca.
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <defs>
    <linearGradient id="erPink" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#fda4af"/>
      <stop offset="100%" stop-color="#f43f5e"/>
    </linearGradient>
    <linearGradient id="erSleeve" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#e0f2fe"/>
      <stop offset="100%" stop-color="#93c5fd"/>
    </linearGradient>
  </defs>
  <!-- Línea limpia de borrado en la base -->
  <line x1="12.5" y1="26.0" x2="26.5" y2="26.0" stroke="%1" stroke-width="1.8" stroke-linecap="round" stroke-opacity="0.55"/>
  <!-- Mitad superior: funda de papel celeste-azul -->
  <path d="M18.6 6.2 C19.5 5.3 21.0 5.3 21.9 6.2 L26.8 11.1 C27.7 12.0 27.7 13.5 26.8 14.4 L19.8 21.4 L11.6 13.2 Z"
        fill="url(#erSleeve)" stroke="#334155" stroke-width="1.3" stroke-linejoin="round"/>
  <!-- Mitad inferior: goma rosa pastel -->
  <path d="M11.6 13.2 L19.8 21.4 L15.4 25.8 L9.4 25.8 L5.4 21.8 C4.5 20.9 4.5 19.4 5.4 18.5 Z"
        fill="url(#erPink)" stroke="#334155" stroke-width="1.3" stroke-linejoin="round"/>
  <!-- Brillo superior en la goma -->
  <path d="M8.5 19.2 L12.0 15.7" stroke="#ffffff" stroke-width="1.4" stroke-linecap="round" stroke-opacity="0.65"/>
</svg>)SVG")
            .arg(bodyOutline);
    }

    // ================= 2. HERRAMIENTAS DUOTONO 100% HOMOLOGADAS =================

    if (name == QLatin1String("shapes")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <circle cx="12.5" cy="12.5" r="6.5" fill="%2" fill-opacity="%3" stroke="%1" stroke-width="2.0"/>
  <rect x="14.5" y="14.5" width="11.5" height="11.5" rx="2.8" fill="%2" fill-opacity="%3" stroke="%1" stroke-width="2.0"/>
</svg>)SVG")
            .arg(st, duoFill, duoOpacity);
    }

    if (name == QLatin1String("polyline")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <path d="M6.5 23.5 L13.0 10.5 L20.0 19.5 L25.5 8.5" fill="none" stroke="%1" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round"/>
  <circle cx="6.5" cy="23.5" r="2.6" fill="%2" stroke="%1" stroke-width="1.7"/>
  <circle cx="13.0" cy="10.5" r="2.6" fill="%2" stroke="%1" stroke-width="1.7"/>
  <circle cx="20.0" cy="19.5" r="2.6" fill="%2" stroke="%1" stroke-width="1.7"/>
  <circle cx="25.5" cy="8.5" r="2.6" fill="%2" stroke="%1" stroke-width="1.7"/>
</svg>)SVG")
            .arg(st, ac);
    }

    if (name == QLatin1String("lasso")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <ellipse cx="16.5" cy="14.2" rx="9.5" ry="7.2" fill="%2" fill-opacity="%3" stroke="%1" stroke-width="2.0" stroke-dasharray="4.2 3.0" stroke-linecap="round"/>
  <circle cx="11.2" cy="20.6" r="2.4" fill="%2" stroke="%1" stroke-width="1.8"/>
  <path d="M10.2 22.8 C9.2 25.0 7.6 26.2 6.0 26.8" fill="none" stroke="%1" stroke-width="2.0" stroke-linecap="round"/>
</svg>)SVG")
            .arg(st, ac, duoOpacity);
    }

    if (name == QLatin1String("text")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <rect x="5.5" y="5.5" width="21" height="21" rx="5.5" fill="%2" fill-opacity="%3"/>
  <path d="M9.5 10.5 H22.5 M16.0 10.5 V23.5 M12.5 23.5 H19.5" fill="none" stroke="%1" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M9.5 10.5 V12.5 M22.5 10.5 V12.5" fill="none" stroke="%1" stroke-width="2.0" stroke-linecap="round"/>
</svg>)SVG")
            .arg(st, duoFill, duoOpacity);
    }

    if (name == QLatin1String("laser")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <circle cx="11.5" cy="20.5" r="6.5" fill="#f43f5e" fill-opacity="0.22"/>
  <path d="M24.5 7.5 L14.2 17.8" stroke="%1" stroke-width="2.2" stroke-linecap="round"/>
  <path d="M11.5 12.5 V15.2 M11.5 25.8 V28.2 M4.0 20.5 H6.6 M16.6 20.5 H19.0 M6.4 15.4 L8.2 17.2 M14.8 23.8 L16.5 25.5"
        stroke="#f43f5e" stroke-width="1.6" stroke-linecap="round"/>
  <circle cx="11.5" cy="20.5" r="3.1" fill="#f43f5e"/>
  <circle cx="11.5" cy="20.5" r="1.2" fill="#ffffff"/>
</svg>)SVG")
            .arg(st);
    }

    if (name == QLatin1String("hand")) {
        // Silueta de mano abierta elegante (estilo GoodNotes / Figma)
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <path d="M12.2 15.8 V8.8 C12.2 7.8 13.0 7.0 14.0 7.0 C15.0 7.0 15.8 7.8 15.8 8.8 V14.2
           V6.8 C15.8 5.8 16.6 5.0 17.6 5.0 C18.6 5.0 19.4 5.8 19.4 6.8 V14.2
           V8.4 C19.4 7.4 20.2 6.6 21.2 6.6 C22.2 6.6 23.0 7.4 23.0 8.4 V15.0
           V11.6 C23.0 10.6 23.8 9.8 24.8 9.8 C25.8 9.8 26.5 10.6 26.5 11.6 V18.6
           C26.5 23.4 23.0 26.8 18.2 26.8 H15.4 C12.2 26.8 9.8 24.9 8.2 22.0 L6.0 18.2
           C5.4 17.2 5.8 16.0 6.8 15.4 C7.7 14.9 8.9 15.2 9.5 16.1 L11.4 19.0"
        fill="%2" fill-opacity="%3" stroke="%1" stroke-width="1.85" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)SVG")
            .arg(st, duoFill, duoOpacity);
    }

    // ================= 3. BARRA SUPERIOR DE UTILIDADES (HOMOLOGADA) =================

    if (name == QLatin1String("home")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <path d="M6.5 14.8 L16.0 6.8 L25.5 14.8 V24.5 C25.5 25.3 24.8 26.0 24.0 26.0 H8.0 C7.2 26.0 6.5 25.3 6.5 24.5 Z"
        fill="%2" fill-opacity="%3" stroke="%1" stroke-width="2.0" stroke-linejoin="round"/>
  <path d="M13.2 26.0 V18.8 H18.8 V26.0" fill="none" stroke="%1" stroke-width="1.9" stroke-linejoin="round"/>
</svg>)SVG")
            .arg(st, duoFill, duoOpacity);
    }

    if (name == QLatin1String("undo")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <path d="M12.0 8.5 L6.5 13.8 L12.0 19.1" fill="none" stroke="%1" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M7.2 13.8 H18.2 C22.4 13.8 25.5 16.7 25.5 20.6 C25.5 22.8 24.4 24.6 22.5 25.6" fill="none" stroke="%1" stroke-width="2.1" stroke-linecap="round"/>
</svg>)SVG")
            .arg(st);
    }

    if (name == QLatin1String("redo")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <path d="M20.0 8.5 L25.5 13.8 L20.0 19.1" fill="none" stroke="%1" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M24.8 13.8 H13.8 C9.6 13.8 6.5 16.7 6.5 20.6 C6.5 22.8 7.6 24.6 9.5 25.6" fill="none" stroke="%1" stroke-width="2.1" stroke-linecap="round"/>
</svg>)SVG")
            .arg(st);
    }

    if (name == QLatin1String("math")) {
        // Símbolo ∑ (Modo Ecuación) en la misma cuadrícula óptica 32x32 duotono
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <rect x="5.0" y="5.0" width="22" height="22" rx="6.0" fill="%2" fill-opacity="%3"/>
  <path d="M22.2 9.5 H10.2 L16.8 16.0 L10.2 22.5 H22.2" fill="none" stroke="%1" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M22.2 9.5 V11.5 M22.2 22.5 V20.5" fill="none" stroke="%1" stroke-width="2.0" stroke-linecap="round"/>
</svg>)SVG")
            .arg(st, duoFill, duoOpacity);
    }

    if (name == QLatin1String("stroke")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <path d="M6.0 22.5 C10.5 12.0 15.5 23.0 21.5 12.5 C23.2 9.5 25.0 8.5 26.2 8.2"
        fill="none" stroke="%1" stroke-width="2.3" stroke-linecap="round"/>
  <circle cx="25.5" cy="22.0" r="3.4" fill="%2" stroke="%1" stroke-width="1.5"/>
</svg>)SVG")
            .arg(st, ink);
    }

    if (name == QLatin1String("paper")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <rect x="7.5" y="5.5" width="17" height="21" rx="3.2" fill="%2" fill-opacity="%3" stroke="%1" stroke-width="2.0"/>
  <path d="M11.5 11.5 H20.5 M11.5 16.0 H20.5 M11.5 20.5 H17.2" fill="none" stroke="%1" stroke-width="1.8" stroke-linecap="round"/>
</svg>)SVG")
            .arg(st, duoFill, duoOpacity);
    }

    if (name == QLatin1String("file")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <path d="M5.5 10.5 C5.5 9.1 6.6 8.0 8.0 8.0 H13.2 L15.6 10.6 H24.0 C25.4 10.6 26.5 11.7 26.5 13.1 V22.5 C26.5 23.9 25.4 25.0 24.0 25.0 H8.0 C6.6 25.0 5.5 23.9 5.5 22.5 Z"
        fill="%2" fill-opacity="%3" stroke="%1" stroke-width="2.0" stroke-linejoin="round"/>
</svg>)SVG")
            .arg(st, duoFill, duoOpacity);
    }

    if (name == QLatin1String("clean")) {
        return QStringLiteral(R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <path d="M5.0 16.0 C8.2 10.6 12.2 8.8 16.0 8.8 C19.8 8.8 23.8 10.6 27.0 16.0 C23.8 21.4 19.8 23.2 16.0 23.2 C12.2 23.2 8.2 21.4 5.0 16.0 Z"
        fill="%2" fill-opacity="%3" stroke="%1" stroke-width="2.0" stroke-linejoin="round"/>
  <circle cx="16.0" cy="16.0" r="3.0" fill="none" stroke="%1" stroke-width="1.9"/>
  <line x1="7.5" y1="24.5" x2="24.5" y2="7.5" stroke="%1" stroke-width="2.0" stroke-linecap="round"/>
</svg>)SVG")
            .arg(st, duoFill, duoOpacity);
    }

    return {};
}

} // namespace

QPixmap renderSvgIcon(const QString& name,
                      int logicalSize,
                      const QColor& strokeColor,
                      const QColor& accentColor,
                      const QColor& inkColor,
                      bool isDark) {
    const int dpr = 2;
    QPixmap pixmap(logicalSize * dpr, logicalSize * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    const QString svg = svgSourceFor(name, strokeColor, accentColor, inkColor, isDark);
    if (svg.isEmpty())
        return pixmap;

    QSvgRenderer renderer(svg.toUtf8());
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(0, 0, logicalSize, logicalSize));
    return pixmap;
}

QIcon makeStudioIcon(const QString& name,
                     int logicalSize,
                     const QColor& strokeColor,
                     const QColor& accentColor,
                     const QColor& inkColor,
                     bool isDark) {
    return QIcon(renderSvgIcon(name, logicalSize, strokeColor, accentColor, inkColor, isDark));
}

} // namespace finily
