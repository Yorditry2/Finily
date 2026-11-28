#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>
#include <optional>

namespace finily {

struct StrokePoint {
    double x = 0.0;
    double y = 0.0;
    double pressure = 0.5;   // Hardware Wacom pressure [0.0, 1.0]
    qint64 timestampMs = 0;  // Monotonic timestamp in ms
    double tiltX = 0.0;      // Wacom X tilt in degrees [-60, 60]
    double tiltY = 0.0;      // Wacom Y tilt in degrees [-60, 60]
};

enum class PressureProfile {
    Light,   // Mano ligera: firme y fluida sin presionar fuerte
    Normal,  // Balanceada
    Firm     // Expresiva / mayor rango dinámico
};

enum class ToolType {
    Pen,
    Ballpoint,
    Highlighter,
    Shape,
    Polyline,
    Lasso,
    Text,
    Laser,
    Eraser,
    Hand,
    Fill,
    Select
};

enum class ShapeKind {
    Line,
    Circle,
    Ellipse,
    Triangle,
    Rectangle,
    Radical,
    Polyline,
    Curve,
    RightAngle,
    Brace,
    Axes,
    ShadedRegion,
    Solid3D,
    Arc,
    Sector,
    AngleMarker
};

enum class ShadePattern {
    Soft,
    Diagonal,
    Dotted,
    Solid,
    Crosshatch,
    Grid
};

enum class EraserMode {
    Stroke,
    Partial
};

struct EraserTargets {
    bool ink = true;         // Pen, Ballpoint y texto
    bool highlighter = true; // Resaltador
    bool shapes = false;     // Figuras y regiones sombreadas
    bool operator==(const EraserTargets&) const = default;
};

struct LassoTargetFilter {
    bool ink = true;    // Trazos (Pluma, Bolígrafo, Resaltador)
    bool shapes = true; // Figuras y regiones
    bool media = true;  // Texto e imágenes
    bool operator==(const LassoTargetFilter&) const = default;
};

enum class LaserMode {
    Dot = 0,
    Trail = 1
};

struct AutoShapeOptions {
    bool line = true;
    bool curve = true;
    bool circle = true;
    bool triangle = true;
    bool rectangle = true;
    bool quadrilateral = true;
    bool polygon = true;
    bool radical = true;
    bool rightAngleL = true;
    bool operator==(const AutoShapeOptions&) const = default;
};

struct GeometryGestureOptions {
    bool angleArc = true;
    bool rightAngle = true;
    bool freehandArrow = true;
    bool attachArrowhead = true;
    bool dividerDot = true;
    bool edgeMarks = true;
    bool holdEdgeMenu = true;
    bool operator==(const GeometryGestureOptions&) const = default;
};

struct SmartGuideOptions {
    bool enabled = true;
    bool snapKeyPoints = true;
    bool orthoPerpGuides = true;
    bool alignXY = true;
    bool operator==(const SmartGuideOptions&) const = default;
};

enum class TabletAction : int {
    None = 0,
    Undo,
    Redo,
    Pen,
    Highlighter,
    Eraser,
    HoldEraser,
    LastWriteTool,
    RightClickMenu,
    Lasso,
    Select,
    Shapes,
    Polyline,
    Hand,
    Laser,
    RadialMenu,
    ToggleCleanUi,
    NextPage,
    PrevPage
};

enum class ToolbarDockMode : int {
    Top = 0,
    Left = 1,
    Float = 2
};

enum class ShapeFamily : int {
    None = 0,
    Triangle,
    Quad,
    Circle,
    Polygon,
    Solid3D
};

enum class LinePattern {
    Solid,
    Dashed,
    Dotted,
    Arrow,
    DoubleArrow,
    Dimension // Cota de medida: |——   ——| con topes en extremos y hueco central
};

enum class LineEnding { None, Arrow, DoubleArrow };

enum class BackgroundTemplate {
    Blank,
    Grid,
    Dots,
    Ruled,
    Engineering, // Milimetrado 5x5
    Isometric,   // Malla isométrica 30° para sólidos 3D
    Cornell,     // Apuntes Cornell (columna izquierda + rayado + resumen)
    TwoColumn,   // Dos columnas (enunciado | desarrollo) con cuadrícula suave
    School       // Hoja escolar con encabezado doble y margen izquierdo
};

enum class PageFormat {
    Infinite = 0,
    A4 = 1,
    Slide16x9 = 2,
    A4Landscape = 3,
    A5 = 4,
    Letter = 5,
    A5Landscape = 6,
    LetterLandscape = 7,
    Legal = 8,
    LegalLandscape = 9,
    Square = 10
};

enum class ThemeId {
    DarkBoard,
    GreenBoard,
    PaperLight,
    PaperCream
};

enum class DocumentKind {
    Notebook = 0,
    Board = 1,
    QuickNote = 2
};

struct CoverStyle {
    int colorIndex = -1; // -1 = automático; 0..7 = color elegido
    int iconIndex = 0;   // 0 = sin ícono; 1..7 = ícono editorial de portada
};

struct NewDocumentSpec {
    DocumentKind kind = DocumentKind::Notebook;
    QString title;
    ThemeId theme = ThemeId::DarkBoard;
    BackgroundTemplate tpl = BackgroundTemplate::Grid;
    PageFormat format = PageFormat::A4;
    double templateScale = 1.0;
    CoverStyle cover;
    QString folderRelativePath; // vacío = raíz (preparado para futura fase de Carpetas)
    bool ephemeralIfEmpty = false;
    QColor customBackground;
    QStringList tagIds;
};

struct ThemeConfig {
    ThemeId id;
    QString name;
    QColor background;
    QColor patternColor;
    QColor uiSurface;
    QColor uiBorder;
    QColor uiText;
    QColor uiMuted;
    QColor accent;
    QColor defaultInk;
    bool isDark;
};

struct Stroke {
    QString id;
    ToolType tool = ToolType::Pen;
    QColor color = QColor("#f8fafc");
    double size = 2.6;
    double opacity = 1.0;
    LinePattern linePattern = LinePattern::Solid;
    PressureProfile pressureProfile = PressureProfile::Light;
    double pressureSensitivity = 0.5; // 0.0 (sin presión) .. 1.0 (máxima intensidad)
    bool hasHardwarePressure = false;
    int tipStyle = 0; // Lápiz: 0=Bolígrafo, 1=Pluma, 2=Lápiz grafito, 3=Tiza | Resaltador: 0=Biselado, 1=Redondo, 2=Recto
    QVector<StrokePoint> points;
    QRectF bounds;
    QString groupId;
    mutable QPainterPath cachedPath;
    mutable QPointF cachedOrigin;
    mutable quint64 cachedShapeHash = 0;
};

struct ShapeAttachment {
    int vertex = 0;
    QString targetId;
    int edge = 0;
    double position = 0;
};

struct ShapeItem {
    QString id;
    ShapeKind kind = ShapeKind::Line;
    QColor color = QColor("#f8fafc");
    double size = 2.6;
    double opacity = 1.0;
    LinePattern linePattern = LinePattern::Solid;
    LineEnding lineEnding = LineEnding::None;
    QString label;
    /**
     * Control vertices in world space:
     * - Line:      [start, end]
     * - Circle:    [center, rimPoint]
     * - Ellipse:   [topLeft, bottomRight]
     * - Rectangle: [topLeft, bottomRight]
     * - Triangle:  [v0, v1, v2]
     * - Radical:   [baseLeft, roofLeft, roofRight]
     *              (baseLeft = bottom-left of check hook, roofLeft = top-left corner of roof,
     *               roofRight = right end of horizontal roof — drag roofRight to extend!)
     * - Polyline:  [v0, v1, ..., vN]
     * - Arc / Sector / AngleMarker: [center, first ray end, second ray end]
     * - ShadedRegion: start point followed by triples [control1, control2, end].
     */
    QVector<QPointF> vertices;
    bool closed = false;
    QRectF bounds;
    ShadePattern shadePattern = ShadePattern::Soft;
    int shadeLayer = 0; // 0 = base, 1..2 = capas superpuestas (desplazadas/alternadas)
    QVector<int> shadeContourStarts; // Offsets of additional subpaths in vertices.
    QString groupId;
    QVector<ShapeAttachment> attachments;
    bool attachmentsKnown = false;
    QHash<QString, int> shadeBoundary;
    QPointF shadeProbe;
    bool shadeVisible = true;
    bool locked = false;
    int solidKind = 0; // Para ShapeKind::Solid3D: 0=Cubo,1=Prisma,2=Cilindro,3=Cono,4=Esfera,5=Pirámide,6=Tetraedro,7=Octaedro,8=TroncoCono,9=TroncoPirámide
    double dimensionGap = -1.0; // Para LinePattern::Dimension: semiancho del hueco central (< 0 = automático)
    int shapeVariant = -1; // Variante explícita (p.ej. Triángulo: 0=Libre, 1=Isósceles, 2=Rectángulo, 3=Equilátero)
};

struct SmartGuide {
    QPointF a;
    QPointF b;
    QString label;
    bool isAngleGuide = false;
};

struct ImageItem {
    QString id;
    QRectF rect;             // World-space position and size
    QImage image;            // Rendered bitmap (pasted screenshot or PDF page)
    bool isPdfPage = false;  // PDF pages stay anchored on background layer below ink
    int pdfPageIndex = -1;
    QString pdfSourcePath;
    QString groupId;
    bool locked = false;
    double opacity = 1.0;
};

struct TextItem {
    QString id;
    QPointF pos;              // Top-left anchor in world space
    QString text;
    QColor color = QColor("#f8fafc");
    double fontSize = 22.0;   // World pixels
    QRectF bounds;
    QString groupId;
    bool locked = false;
    int fontFamilyIndex = 0;  // 0=Inter, 1=Manuscrita, 2=Mono
    bool bold = false;
    bool italic = false;
};

struct DocumentSnapshot {
    QVector<Stroke> strokes;
    QVector<ShapeItem> shapes;
    QVector<ImageItem> images;
    QVector<TextItem> texts;
};

struct Viewport {
    double x = 0.0;     // Screen X offset: screenX = worldX * zoom + x
    double y = 0.0;     // Screen Y offset: screenY = worldY * zoom + y
    double zoom = 1.0;  // [0.2, 5.0]
};

struct PageData {
    QVector<Stroke> strokes;
    QVector<ShapeItem> shapes;
    QVector<ImageItem> images;
    QVector<TextItem> texts;
    Viewport viewport;
    std::optional<BackgroundTemplate> background;
};

struct NotebookSession {
    QString id;
    QString filePath;
    QVector<PageData> pages;
    int currentPageIndex = 0;
    ThemeId themeId = ThemeId::DarkBoard;
    BackgroundTemplate tpl = BackgroundTemplate::Dots;
    PageFormat pageFormat = PageFormat::Infinite;
    double templateScale = 1.0;
    CoverStyle cover;
    QColor customBackground;
    bool ephemeralIfEmpty = false;
    QString pendingTitle;
    bool dirty = false;
};

struct PenPreset {
    int index = 0;
    QString label;
    ToolType tool = ToolType::Pen;
    QColor color = QColor("#f8fafc");
    double size = 2.6;
    LinePattern linePattern = LinePattern::Solid;
    int tipStyle = 1;
    double opacity = 1.0;
    QString shortcut;
    ShapeKind shapeKind = ShapeKind::Triangle;
    int shapeSubVariant = 0;
};

inline ThemeConfig getThemeConfig(ThemeId id) {
    switch (id) {
        case ThemeId::DarkBoard:
            return {
                ThemeId::DarkBoard, "Pizarra Oscura",
                QColor("#16181d"), QColor(255, 255, 255, 10),
                QColor(28, 31, 38, 232), QColor(255, 255, 255, 28),
                QColor("#f3f4f6"), QColor("#9ca3af"),
                QColor("#60a5fa"), QColor("#f8fafc"), true
            };
        case ThemeId::GreenBoard:
            return {
                ThemeId::GreenBoard, "Pizarra Academia",
                QColor("#132a23"), QColor(255, 255, 255, 10),
                QColor(18, 43, 36, 235), QColor(255, 255, 255, 32),
                QColor("#ecfdf5"), QColor("#94b8ac"),
                QColor("#34d399"), QColor("#f8fafc"), true
            };
        case ThemeId::PaperLight:
            return {
                ThemeId::PaperLight, "Papel Blanco",
                QColor("#ffffff"), QColor(59, 130, 246, 72),
                QColor(255, 255, 255, 238), QColor(15, 23, 42, 28),
                QColor("#0f172a"), QColor("#64748b"),
                QColor("#2563eb"), QColor("#1d4ed8"), false
            };
        case ThemeId::PaperCream:
            return {
                ThemeId::PaperCream, "Papel Crema",
                QColor("#f8f4e8"), QColor(120, 98, 68, 42),
                QColor(252, 249, 240, 238), QColor(100, 82, 54, 36),
                QColor("#292524"), QColor("#78716c"),
                QColor("#d97706"), QColor("#1c1917"), false
            };
    }
    return getThemeConfig(ThemeId::DarkBoard);
}

inline ThemeConfig effectiveThemeConfig(ThemeId id, const QColor& customBg) {
    ThemeConfig cfg = getThemeConfig(id);
    if (!customBg.isValid())
        return cfg;
    cfg.background = customBg;
    const double lum = 0.299 * customBg.red() + 0.587 * customBg.green() + 0.114 * customBg.blue();
    cfg.isDark = lum < 140.0;
    cfg.patternColor = cfg.isDark ? QColor(255, 255, 255, 10) : QColor(15, 23, 42, 16);
    cfg.defaultInk = cfg.isDark ? QColor("#f8fafc") : QColor("#0f172a");
    return cfg;
}

inline QVector<QColor> getPaletteForTheme(bool isDark) {
    if (isDark) {
        return {
            QColor("#f8fafc"), // Blanco tiza
            QColor("#fde047"), // Amarillo academia
            QColor("#38bdf8"), // Celeste
            QColor("#f87171"), // Rojo coral
            QColor("#4ade80"), // Verde menta
            QColor("#c084fc"), // Lila
            QColor("#fb923c"), // Naranja
        };
    }
    return {
        QColor("#0f172a"), // Negro tinta
        QColor("#1d4ed8"), // Azul lapicero (Xournal style)
        QColor("#dc2626"), // Rojo
        QColor("#16a34a"), // Verde
        QColor("#9333ea"), // Morado
        QColor("#ea580c"), // Naranja
        QColor("#ca8a04"), // Amarillo oscuro
    };
}

} // namespace finily
