#pragma once

#include "core/Types.h"

#include <QString>
#include <QStringView>

#include <algorithm>
#include <optional>

namespace finily {

enum class EdgeMarkKind {
    None,
    Zigzag,
    Spring,
    Dot,
    Divider,
    Bar,
    Tick1,
    Tick2,
    Tick3,
    Parallel1,
    Parallel2,
    Wall,
    WallFlip
};

enum class HighlightStyle {
    None,
    Glow,
    Solid,
    Dashed
};

struct TableGridSpec {
    int rows = 1;
    int cols = 1;
};

enum class Solid3DKind : int {
    Cube = 0,
    Prism,
    Cylinder,
    Cone,
    Sphere,
    Pyramid,
    Tetrahedron,
    Octahedron,
    FrustumCone,
    FrustumPyramid
};

inline bool hasMarkPrefix(QStringView label) {
    return label.trimmed().startsWith(QLatin1String("mark:"), Qt::CaseInsensitive);
}

inline EdgeMarkKind parseEdgeMark(QStringView spec) {
    const QStringView t = spec.trimmed();
    if (!t.startsWith(QLatin1String("mark:"), Qt::CaseInsensitive))
        return EdgeMarkKind::None;
    const QStringView sub = t.mid(5);
    if (sub.compare(QLatin1String("zigzag"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Zigzag;
    if (sub.compare(QLatin1String("spring"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Spring;
    if (sub.compare(QLatin1String("dot"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Dot;
    if (sub.compare(QLatin1String("divider"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Divider;
    if (sub.compare(QLatin1String("bar"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Bar;
    if (sub.compare(QLatin1String("tick1"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Tick1;
    if (sub.compare(QLatin1String("tick2"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Tick2;
    if (sub.compare(QLatin1String("tick3"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Tick3;
    if (sub.compare(QLatin1String("parallel1"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Parallel1;
    if (sub.compare(QLatin1String("parallel2"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Parallel2;
    if (sub.compare(QLatin1String("wall"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::Wall;
    if (sub.compare(QLatin1String("wall_flip"), Qt::CaseInsensitive) == 0)
        return EdgeMarkKind::WallFlip;
    return EdgeMarkKind::None;
}

inline QString edgeMarkLabel(EdgeMarkKind kind) {
    switch (kind) {
    case EdgeMarkKind::None:
        return {};
    case EdgeMarkKind::Zigzag:
        return QStringLiteral("mark:zigzag");
    case EdgeMarkKind::Spring:
        return QStringLiteral("mark:spring");
    case EdgeMarkKind::Dot:
        return QStringLiteral("mark:dot");
    case EdgeMarkKind::Divider:
        return QStringLiteral("mark:divider");
    case EdgeMarkKind::Bar:
        return QStringLiteral("mark:bar");
    case EdgeMarkKind::Tick1:
        return QStringLiteral("mark:tick1");
    case EdgeMarkKind::Tick2:
        return QStringLiteral("mark:tick2");
    case EdgeMarkKind::Tick3:
        return QStringLiteral("mark:tick3");
    case EdgeMarkKind::Parallel1:
        return QStringLiteral("mark:parallel1");
    case EdgeMarkKind::Parallel2:
        return QStringLiteral("mark:parallel2");
    case EdgeMarkKind::Wall:
        return QStringLiteral("mark:wall");
    case EdgeMarkKind::WallFlip:
        return QStringLiteral("mark:wall_flip");
    }
    return {};
}

inline EdgeMarkKind edgeMarkKind(const ShapeItem& shape) {
    if (shape.kind != ShapeKind::Line)
        return EdgeMarkKind::None;
    return parseEdgeMark(shape.label);
}

inline bool isEdgeMark(const ShapeItem& shape) {
    return shape.kind == ShapeKind::Line
        && (parseEdgeMark(shape.label) != EdgeMarkKind::None || hasMarkPrefix(shape.label));
}

inline bool isDividerMark(const ShapeItem& shape) {
    return edgeMarkKind(shape) == EdgeMarkKind::Divider;
}

inline EdgeMarkKind cycleEdgeMark(EdgeMarkKind current, EdgeMarkKind requested) {
    auto isTick = [](EdgeMarkKind k) {
        return k == EdgeMarkKind::Tick1 || k == EdgeMarkKind::Tick2 || k == EdgeMarkKind::Tick3;
    };
    auto isParallel = [](EdgeMarkKind k) {
        return k == EdgeMarkKind::Parallel1 || k == EdgeMarkKind::Parallel2;
    };
    auto isWall = [](EdgeMarkKind k) {
        return k == EdgeMarkKind::Wall || k == EdgeMarkKind::WallFlip;
    };

    if (isTick(requested) && isTick(current)) {
        if (current == EdgeMarkKind::Tick1)
            return EdgeMarkKind::Tick2;
        if (current == EdgeMarkKind::Tick2)
            return EdgeMarkKind::Tick3;
        return EdgeMarkKind::Tick1;
    }
    if (isParallel(requested) && isParallel(current))
        return (current == EdgeMarkKind::Parallel1) ? EdgeMarkKind::Parallel2 : EdgeMarkKind::Parallel1;
    if (isWall(requested) && isWall(current))
        return (current == EdgeMarkKind::Wall) ? EdgeMarkKind::WallFlip : EdgeMarkKind::Wall;
    return requested;
}

inline QString edgeMarkToast(EdgeMarkKind kind) {
    switch (kind) {
    case EdgeMarkKind::Zigzag:
        return QStringLiteral("Serrucho ✓");
    case EdgeMarkKind::Spring:
        return QStringLiteral("Resorte ✓");
    case EdgeMarkKind::Dot:
        return QStringLiteral("Bolita ✓");
    case EdgeMarkKind::Divider:
        return QStringLiteral("Punto divisor ✓");
    case EdgeMarkKind::Bar:
        return QStringLiteral("Bultito ✓");
    case EdgeMarkKind::Tick1:
    case EdgeMarkKind::Tick2:
    case EdgeMarkKind::Tick3:
        return QStringLiteral("Rayitas ✓");
    case EdgeMarkKind::Parallel1:
    case EdgeMarkKind::Parallel2:
        return QStringLiteral("Paralelas ✓");
    case EdgeMarkKind::Wall:
    case EdgeMarkKind::WallFlip:
        return QStringLiteral("Pared rugosa ✓");
    case EdgeMarkKind::None:
        break;
    }
    return QStringLiteral("Marca ✓");
}

inline HighlightStyle parseHighlightStyle(QStringView label) {
    const QStringView t = label.trimmed();
    if (!t.startsWith(QLatin1String("highlight:"), Qt::CaseInsensitive))
        return HighlightStyle::None;
    const QStringView sub = t.mid(10);
    if (sub.compare(QLatin1String("glow"), Qt::CaseInsensitive) == 0)
        return HighlightStyle::Glow;
    if (sub.compare(QLatin1String("solid"), Qt::CaseInsensitive) == 0)
        return HighlightStyle::Solid;
    if (sub.compare(QLatin1String("dashed"), Qt::CaseInsensitive) == 0)
        return HighlightStyle::Dashed;
    return HighlightStyle::None;
}

inline QString highlightLabel(HighlightStyle style) {
    switch (style) {
    case HighlightStyle::None:
        return {};
    case HighlightStyle::Glow:
        return QStringLiteral("highlight:glow");
    case HighlightStyle::Solid:
        return QStringLiteral("highlight:solid");
    case HighlightStyle::Dashed:
        return QStringLiteral("highlight:dashed");
    }
    return {};
}

inline HighlightStyle highlightStyle(const ShapeItem& shape) {
    if (shape.kind != ShapeKind::Line && shape.kind != ShapeKind::Arc)
        return HighlightStyle::None;
    return parseHighlightStyle(shape.label);
}

inline bool isHighlightOverlay(const ShapeItem& shape) {
    return (shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Arc)
        && (parseHighlightStyle(shape.label) != HighlightStyle::None
            || shape.label.trimmed().startsWith(QLatin1String("highlight:"), Qt::CaseInsensitive));
}

inline QString physicsSpringLabel() {
    return QStringLiteral("physics:spring");
}

inline bool isPhysicsSpringSpec(QStringView spec) {
    return spec.trimmed().compare(QLatin1String("physics:spring"), Qt::CaseInsensitive) == 0;
}

inline bool isPhysicsSpring(const ShapeItem& shape) {
    return shape.kind == ShapeKind::Line && isPhysicsSpringSpec(shape.label);
}

inline QString autoVertexAngleLabel() {
    return QStringLiteral("auto_vertex");
}

inline bool isAutoVertexAngle(const ShapeItem& shape) {
    return shape.kind == ShapeKind::AngleMarker
        && shape.label.trimmed().compare(QLatin1String("auto_vertex"), Qt::CaseInsensitive) == 0;
}

inline std::optional<TableGridSpec> parseTableSpec(QStringView label) {
    const QStringView trimmed = label.trimmed();
    if (!trimmed.startsWith(QLatin1String("tabla:"), Qt::CaseInsensitive))
        return std::nullopt;
    const QStringView body = trimmed.mid(6).trimmed();
    qsizetype sepIdx = body.indexOf(QLatin1Char('x'), 0, Qt::CaseInsensitive);
    if (sepIdx < 0)
        sepIdx = body.indexOf(QChar(0x00D7));
    if (sepIdx <= 0 || sepIdx + 1 >= body.size())
        return std::nullopt;
    bool okR = false;
    bool okC = false;
    const int r = body.left(sepIdx).trimmed().toInt(&okR);
    const int c = body.mid(sepIdx + 1).trimmed().toInt(&okC);
    if (!okR || !okC || r < 1 || c < 1)
        return std::nullopt;
    return TableGridSpec{std::clamp(r, 1, 12), std::clamp(c, 1, 12)};
}

inline QString formatTableSpec(int rows, int cols) {
    return QStringLiteral("tabla:%1x%2").arg(std::clamp(rows, 1, 12)).arg(std::clamp(cols, 1, 12));
}

inline bool hasEditableMeasurementLabel(const ShapeItem& shape) {
    if (shape.kind == ShapeKind::AngleMarker)
        return !isAutoVertexAngle(shape);
    if (shape.kind == ShapeKind::Line && shape.linePattern == LinePattern::Dimension)
        return !isEdgeMark(shape) && !isHighlightOverlay(shape) && !isPhysicsSpring(shape);
    return false;
}

} // namespace finily
