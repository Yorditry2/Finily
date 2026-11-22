#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <cstdint>

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
    Hand
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
    Axes
};

enum class EraserMode {
    Stroke,
    Partial
};

enum class LinePattern {
    Solid,
    Dashed,
    Dotted,
    Arrow,
    DoubleArrow
};

enum class BackgroundTemplate {
    Blank,
    Grid,
    Dots,
    Ruled
};

enum class ThemeId {
    DarkBoard,
    GreenBoard,
    PaperLight,
    PaperCream
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
    bool hasHardwarePressure = false;
    QVector<StrokePoint> points;
    QRectF bounds;
};

struct ShapeItem {
    QString id;
    ShapeKind kind = ShapeKind::Line;
    QColor color = QColor("#f8fafc");
    double size = 2.6;
    double opacity = 1.0;
    LinePattern linePattern = LinePattern::Solid;
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
     */
    QVector<QPointF> vertices;
    bool closed = false;
    QRectF bounds;
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
};

struct TextItem {
    QString id;
    QPointF pos;              // Top-left anchor in world space
    QString text;
    QColor color = QColor("#f8fafc");
    double fontSize = 22.0;   // World pixels
    QRectF bounds;
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
};

struct PenPreset {
    int index = 0;
    QString label;
    ToolType tool = ToolType::Pen;
    QColor color = QColor("#f8fafc");
    double size = 2.6;
    LinePattern linePattern = LinePattern::Solid;
};

inline ThemeConfig getThemeConfig(ThemeId id) {
    switch (id) {
        case ThemeId::DarkBoard:
            return {
                ThemeId::DarkBoard, "Pizarra Oscura",
                QColor("#16181d"), QColor(255, 255, 255, 20),
                QColor(28, 31, 38, 232), QColor(255, 255, 255, 28),
                QColor("#f3f4f6"), QColor("#9ca3af"),
                QColor("#60a5fa"), QColor("#f8fafc"), true
            };
        case ThemeId::GreenBoard:
            return {
                ThemeId::GreenBoard, "Pizarra Academia",
                QColor("#132a23"), QColor(255, 255, 255, 20),
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
