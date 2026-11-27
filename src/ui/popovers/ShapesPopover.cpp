#include "ui/popovers/ShapesPopover.h"

#include "ui/CanvasWidget.h"
#include "ui/ToolbarIcons.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLayout>
#include <QLineEdit>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {

ShapesPopover::ShapesPopover(CanvasWidget* canvas, QWidget* parent)
    : GlassCapsule(parent), m_canvas(canvas) {
    setObjectName(QStringLiteral("finilyPopover"));
    setFixedWidth(344);
    hide();

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    struct ShapeSpec {
        QString name;
        QString glyph;
        QString tip;
        ShapeKind kind;
        bool dimension;
        int sides;
        int tri;
        int quad;
        int solid;
        QString mark;
    };
    const QVector<QPair<QString, QVector<ShapeSpec>>> pages = {
        {QStringLiteral("Líneas"),
         {
             {QStringLiteral("Segmento"), QStringLiteral("shape-line"),
              QStringLiteral("Arrastra para dibujar. Mayús: ajusta el ángulo"), ShapeKind::Line, false, 0,
              -1, 0, -1, {}},
             {QStringLiteral("Curva"), QStringLiteral("shape-curve"),
              QStringLiteral("Curva con punto central editable"), ShapeKind::Curve, false, 0, -1, 0, -1, {}},
             {QStringLiteral("Arco circular"), QStringLiteral("shape-arc"),
              QStringLiteral("Traza un radio y ajusta sus extremos"), ShapeKind::Arc, false, 0, -1, 0, -1,
              {}},
             {QStringLiteral("Cota"), QStringLiteral("shape-dimension"),
              QStringLiteral("Línea de medida con topes y etiqueta editable"), ShapeKind::Line, true, 0, -1,
              0, -1, {}},
             {QStringLiteral("Resorte (Física)"), QStringLiteral("mark-spring"),
              QStringLiteral(
                  "Resorte elástico estirable: arrastra o ancla sus extremos a paredes y bloques"),
              ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("physics:spring")},
         }},
        {QStringLiteral("Formas"),
         {
             {QStringLiteral("Óvalo / Círculo"), QStringLiteral("shape-ellipse"),
              QStringLiteral("Mayús para un círculo perfecto"), ShapeKind::Ellipse, false, 0, -1, 0, -1, {}},
             {QStringLiteral("Rectángulo"), QStringLiteral("shape-rect"),
              QStringLiteral("Con Mayús sale cuadrado perfecto"), ShapeKind::Rectangle, false, 0, -1, 0, -1,
              {}},
             {QStringLiteral("Sector circular"), QStringLiteral("shape-sector"),
              QStringLiteral("Traza un radio y ajusta la abertura"), ShapeKind::Sector, false, 0, -1, 0, -1,
              {}},
             {QStringLiteral("Rombo"), QStringLiteral("shape-poly-4"),
              QStringLiteral("Rombo con 4 vértices editables"), ShapeKind::Polyline, false, 4, -1, 0, -1,
              {}},
             {QStringLiteral("Paralelogramo"), QStringLiteral("shape-parallelogram"),
              QStringLiteral("Lados opuestos paralelos"), ShapeKind::Polyline, false, 4, -1, 1, -1, {}},
             {QStringLiteral("Trapecio isósceles"), QStringLiteral("shape-trap-iso"),
              QStringLiteral("Trapecio simétrico editable"), ShapeKind::Polyline, false, 4, -1, 2, -1, {}},
             {QStringLiteral("Trapecio rectángulo"), QStringLiteral("shape-trap-right"),
              QStringLiteral("Trapecio con 2 ángulos rectos"), ShapeKind::Polyline, false, 4, -1, 3, -1, {}},
             {QStringLiteral("Trapezoide"), QStringLiteral("shape-trapezoid"),
              QStringLiteral("Cuadrilátero libre de 4 vértices"), ShapeKind::Polyline, false, 4, -1, 4, -1,
              {}},
             {QStringLiteral("Triángulo isósceles"), QStringLiteral("shape-triangle"),
              QStringLiteral("Triángulo con vértice centrado"), ShapeKind::Triangle, false, 0, 0, 0, -1, {}},
             {QStringLiteral("Triángulo rectángulo"), QStringLiteral("shape-tri-right"),
              QStringLiteral("Triángulo rectángulo con cuadradito 90° automático"), ShapeKind::Triangle,
              false, 0, 1, 0, -1, {}},
             {QStringLiteral("Triángulo equilátero"), QStringLiteral("shape-tri-equi"),
              QStringLiteral("Tres lados iguales"), ShapeKind::Triangle, false, 0, 3, 0, -1, {}},
             {QStringLiteral("Pentágono"), QStringLiteral("shape-poly-5"),
              QStringLiteral("Polígono regular de 5 lados"), ShapeKind::Polyline, false, 5, -1, 0, -1, {}},
             {QStringLiteral("Hexágono"), QStringLiteral("shape-poly-6"),
              QStringLiteral("Polígono regular de 6 lados"), ShapeKind::Polyline, false, 6, -1, 0, -1, {}},
             {QStringLiteral("Heptágono"), QStringLiteral("shape-poly-7"),
              QStringLiteral("Polígono regular de 7 lados"), ShapeKind::Polyline, false, 7, -1, 0, -1, {}},
             {QStringLiteral("Octágono"), QStringLiteral("shape-poly-8"),
              QStringLiteral("Polígono regular de 8 lados"), ShapeKind::Polyline, false, 8, -1, 0, -1, {}},
         }},
        {QStringLiteral("Sólidos"),
         {
             {QStringLiteral("Cubo / Hexaedro"), QStringLiteral("shape-3d-cube"),
              QStringLiteral("Hexaedro con aristas ocultas punteadas"), ShapeKind::Solid3D, false, 0, -1, 0,
              0, {}},
             {QStringLiteral("Prisma"), QStringLiteral("shape-3d-prism"),
              QStringLiteral("Prisma triangular recto"), ShapeKind::Solid3D, false, 0, -1, 0, 1, {}},
             {QStringLiteral("Cilindro"), QStringLiteral("shape-3d-cyl"),
              QStringLiteral("Cilindro circular recto"), ShapeKind::Solid3D, false, 0, -1, 0, 2, {}},
             {QStringLiteral("Cono"), QStringLiteral("shape-3d-cone"), QStringLiteral("Cono circular recto"),
              ShapeKind::Solid3D, false, 0, -1, 0, 3, {}},
             {QStringLiteral("Esfera"), QStringLiteral("shape-3d-sphere"),
              QStringLiteral("Esfera con ecuador en perspectiva"), ShapeKind::Solid3D, false, 0, -1, 0, 4,
              {}},
             {QStringLiteral("Pirámide"), QStringLiteral("shape-3d-pyr"),
              QStringLiteral("Pirámide cuadrangular"), ShapeKind::Solid3D, false, 0, -1, 0, 5, {}},
             {QStringLiteral("Tetraedro"), QStringLiteral("shape-3d-tetra"), QStringLiteral("Tetraedro 3D"),
              ShapeKind::Solid3D, false, 0, -1, 0, 6, {}},
             {QStringLiteral("Octaedro"), QStringLiteral("shape-3d-octa"),
              QStringLiteral("Octaedro regular 3D"), ShapeKind::Solid3D, false, 0, -1, 0, 7, {}},
             {QStringLiteral("Tronco de cono"), QStringLiteral("shape-3d-fcone"),
              QStringLiteral("Tronco de cono"), ShapeKind::Solid3D, false, 0, -1, 0, 8, {}},
             {QStringLiteral("Tronco de pirámide"), QStringLiteral("shape-3d-fpyr"),
              QStringLiteral("Tronco de pirámide cuadrangular"), ShapeKind::Solid3D, false, 0, -1, 0, 9, {}},
         }},
        {QStringLiteral("Matemáticas"),
         {
             {QStringLiteral("Ejes X/Y"), QStringLiteral("shape-axes"),
              QStringLiteral("Plano cartesiano con ejes X/Y"), ShapeKind::Axes, false, 0, -1, 0, -1, {}},
             {QStringLiteral("Raíz √"), QStringLiteral("shape-radical"),
              QStringLiteral("Raíz cuadrada auto-ajustable (R)"), ShapeKind::Radical, false, 0, -1, 0, -1,
              {}},
             {QStringLiteral("Ángulo"), QStringLiteral("shape-angle"),
              QStringLiteral("Dos lados y una medida automática editable"), ShapeKind::AngleMarker, false, 0,
              -1, 0, -1, {}},
             {QStringLiteral("Ángulo recto"), QStringLiteral("shape-rightangle"),
              QStringLiteral("Marca de ángulo de 90°"), ShapeKind::RightAngle, false, 0, -1, 0, -1, {}},
             {QStringLiteral("Llave"), QStringLiteral("shape-brace"),
              QStringLiteral("Llave { } para agrupar"), ShapeKind::Brace, false, 0, -1, 0, -1, {}},
             {QStringLiteral("Serrucho (congruencia)"), QStringLiteral("mark-zigzag"),
              QStringLiteral("Toca cualquier lado de una figura para imantar un serrucho de lados iguales"),
              ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:zigzag")},
             {QStringLiteral("Resorte (congruencia)"), QStringLiteral("mark-spring"),
              QStringLiteral(
                  "Toca cualquier lado de una figura para imantar un resorte/onda de lados iguales"),
              ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:spring")},
             {QStringLiteral("Bolita (congruencia)"), QStringLiteral("mark-dot"),
              QStringLiteral(
                  "Toca cualquier lado de una figura para imantar un punto relleno de lados iguales"),
              ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:dot")},
             {QStringLiteral("Punto divisor"), QStringLiteral("mark-divider"),
              QStringLiteral(
                  "Imanta un punto sobre cualquier lado para dividirlo en dos subsegmentos"),
              ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:divider")},
             {QStringLiteral("Tronquito (congruencia)"), QStringLiteral("mark-bar"),
              QStringLiteral(
                  "Toca cualquier lado de una figura para imantar un rectángulo delgado de lados iguales"),
              ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:bar")},
             {QStringLiteral("Rayitas || (congruencia)"), QStringLiteral("mark-ticks"),
              QStringLiteral(
                  "Toca un lado para imantar 1, 2 o 3 rayitas de congruencia (repite clic para ciclar)"),
              ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:tick2")},
             {QStringLiteral("Paralelas >>"), QStringLiteral("mark-parallel"),
              QStringLiteral(
                  "Toca un lado para imantar flechas de lados paralelos (repite clic para 1 o 2)"),
              ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:parallel2")},
             {QStringLiteral("Pared / suelo (Física)"), QStringLiteral("mark-wall"),
              QStringLiteral(
                  "Toca un lado para añadir rayado de pared o superficie rugosa (repite clic para "
                  "invertir)"),
              ShapeKind::Line, false, 0, -1, 0, -1, QStringLiteral("mark:wall")},
         }},
    };

    auto* tabsRow = new QHBoxLayout();
    tabsRow->setSpacing(2);
    auto* tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);
    auto* stack = new QStackedWidget(this);

    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        auto* tab = new QToolButton(this);
        tab->setObjectName(QStringLiteral("finilyShapeBtn"));
        tab->setText(pages.at(pageIndex).first);
        tab->setCheckable(true);
        tab->setChecked(pageIndex == 0);
        tab->setFixedHeight(26);
        QFont tabFont = tab->font();
        tabFont.setPixelSize(11);
        tab->setFont(tabFont);
        polishButton(tab);
        tabGroup->addButton(tab, pageIndex);
        tabsRow->addWidget(tab);

        auto* page = new QWidget(stack);
        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(6);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        const auto& specs = pages.at(pageIndex).second;
        for (int i = 0; i < specs.size(); ++i) {
            const ShapeSpec spec = specs.at(i);
            auto* button = new QToolButton(page);
            button->setObjectName(QStringLiteral("finilyShapeBtn"));
            button->setFixedSize(54, 40);
            button->setIconSize(QSize(20, 20));
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setToolTip(spec.name + QStringLiteral(" · ") + spec.tip);
            button->setCheckable(true);
            polishButton(button);
            connect(button, &QToolButton::clicked, this, [this, spec] {
                if (!m_canvas)
                    return;
                m_canvas->setShapeKind(spec.kind);
                m_canvas->setCongruenceMarkSpec(spec.mark);
                if (spec.sides > 0)
                    m_canvas->setPolygonSides(spec.sides);
                m_canvas->setQuadVariant(spec.quad);
                if (spec.solid >= 0)
                    m_canvas->setSolidKind(spec.solid);
                if (spec.tri >= 0)
                    m_canvas->setTriangleVariant(spec.tri);
                if (spec.dimension)
                    m_canvas->setLinePattern(LinePattern::Dimension);
                else if (m_canvas->linePattern() == LinePattern::Dimension)
                    m_canvas->setLinePattern(LinePattern::Solid);
                m_canvas->setTool(ToolType::Shape);
                emit closeRequested();
                emit refreshToolbarRequested();
            });
            m_shapeKindBtns.append(button);
            m_shapeKindSpecs.append(spec.kind);
            m_shapeDimensionSpecs.append(spec.dimension);
            m_shapeSidesSpecs.append(spec.sides);
            m_shapeTriSpecs.append(spec.tri);
            m_shapeQuadSpecs.append(spec.quad);
            m_shapeSolidSpecs.append(spec.solid);
            m_shapeMarkSpecs.append(spec.mark);
            m_iconSpecs.append(qMakePair(button, spec.glyph));
            grid->addWidget(button, i / 5, i % 5);
        }

        if (pageIndex == 0) {
            auto* poly = new QToolButton(page);
            poly->setObjectName(QStringLiteral("finilyShapeBtn"));
            poly->setFixedSize(54, 40);
            poly->setIconSize(QSize(20, 20));
            poly->setToolTip(
                tr("Polilínea (L) · puntos libres; Mayús + arrastrar para editar una figura"));
            polishButton(poly);
            connect(poly, &QToolButton::clicked, this, [this] {
                emit closeRequested();
                if (m_canvas)
                    m_canvas->setTool(ToolType::Polyline);
                emit refreshToolbarRequested();
            });
            m_iconSpecs.append(qMakePair(poly, QStringLiteral("polyline")));
            grid->addWidget(poly, specs.size() / 5, specs.size() % 5);
        } else if (pageIndex == 3) {
            auto* tableBtn = new QToolButton(page);
            tableBtn->setObjectName(QStringLiteral("finilyShapeBtn"));
            tableBtn->setFixedSize(54, 40);
            tableBtn->setText(QStringLiteral("Tabla"));
            tableBtn->setToolTip(tr("Insertar tabla o cuadrícula de matriz (Filas × Columnas)"));
            polishButton(tableBtn);
            connect(tableBtn, &QToolButton::clicked, this, [this] {
                if (!m_canvas)
                    return;
                emit closeRequested();
                bool ok = false;
                const QString spec = QInputDialog::getText(
                    window(), tr("Insertar Tabla / Matriz"),
                    tr("Filas × Columnas (ej. 3x4, 4x4, 2x5):"),
                    QLineEdit::Normal, QStringLiteral("3x4"), &ok);
                if (!ok || spec.trimmed().isEmpty())
                    return;
                const QStringList parts = spec.trimmed().toLower().split(QLatin1Char('x'));
                const int r = parts.size() >= 1 ? std::clamp(parts[0].trimmed().toInt(), 1, 12) : 3;
                const int c = parts.size() >= 2 ? std::clamp(parts[1].trimmed().toInt(), 1, 12) : 4;
                m_canvas->createTableShape(r, c);
                emit refreshToolbarRequested();
            });
            grid->addWidget(tableBtn, specs.size() / 5, specs.size() % 5);
        }
        stack->addWidget(page);
    }

    auto fitStack = [this, stack](int index) {
        for (int i = 0; i < stack->count(); ++i) {
            const bool active = (i == index);
            stack->widget(i)->setSizePolicy(
                active ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                active ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        }
        stack->setCurrentIndex(index);
        stack->updateGeometry();
        if (layout())
            layout()->activate();
        adjustSize();
        emit layoutAdjusted();
    };
    connect(tabGroup, &QButtonGroup::idClicked, this, fitStack);
    fitStack(0);
    column->addLayout(tabsRow);
    column->addWidget(stack);

    auto* lineStyleRow = new QHBoxLayout();
    lineStyleRow->setSpacing(6);
    const QPair<LinePattern, QString> shapeLines[] = {
        {LinePattern::Solid, QStringLiteral("Línea sólida")},
        {LinePattern::Dashed, QStringLiteral("Línea discontinua")},
        {LinePattern::Dotted, QStringLiteral("Línea punteada")},
    };
    for (const auto& [pat, tip] : shapeLines) {
        auto* btn = new QToolButton(this);
        btn->setFixedSize(86, 28);
        btn->setIconSize(QSize(44, 14));
        btn->setToolTip(tip);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, pat] {
            if (m_canvas && m_canvas->linePattern() != LinePattern::Dimension)
                m_canvas->setLinePattern(pat);
            syncFromCanvas();
            emit refreshToolbarRequested();
        });
        m_patternBtns.append(btn);
        m_patternSpecs.append(qMakePair(btn, pat));
        lineStyleRow->addWidget(btn);
    }
    column->addLayout(lineStyleRow);

    auto* endingRow = new QHBoxLayout();
    endingRow->setSpacing(6);
    struct EndingSpec {
        LineEnding ending;
        QString label;
        QString tip;
    };
    const QVector<EndingSpec> endings = {
        {LineEnding::None, QStringLiteral("— Sin flechas"), QStringLiteral("Sin flechas en los extremos")},
        {LineEnding::Arrow, QStringLiteral("→ Flecha"), QStringLiteral("Flecha en el extremo final")},
        {LineEnding::DoubleArrow, QStringLiteral("↔ Doble flecha"),
         QStringLiteral("Flecha en ambos extremos")},
    };
    for (const auto& spec : endings) {
        auto* btn = new QToolButton(this);
        btn->setObjectName(QStringLiteral("finilyShapeBtn"));
        btn->setText(spec.label);
        btn->setToolTip(spec.tip);
        btn->setCheckable(true);
        btn->setFixedHeight(28);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        polishButton(btn);
        const LineEnding ending = spec.ending;
        connect(btn, &QToolButton::clicked, this, [this, ending] {
            if (m_canvas && m_canvas->linePattern() != LinePattern::Dimension)
                m_canvas->setLineEnding(ending);
            syncFromCanvas();
            emit refreshToolbarRequested();
        });
        m_endingBtns.append(btn);
        m_endingSpecs.append(ending);
        endingRow->addWidget(btn, 1);
    }
    column->addLayout(endingRow);
    column->addStretch(1);

    syncFromCanvas();
}

void ShapesPopover::syncFromCanvas() {
    if (!m_canvas)
        return;

    const ShapeKind shapeKind = m_canvas->shapeKind();
    const LinePattern pat = m_canvas->linePattern();
    const bool dimension = (pat == LinePattern::Dimension);

    for (int i = 0; i < m_patternSpecs.size(); ++i) {
        m_patternBtns.at(i)->setChecked(m_patternSpecs.at(i).second == pat);
        m_patternBtns.at(i)->setEnabled(!dimension);
    }

    const LineEnding ending = m_canvas->lineEnding();
    for (int i = 0; i < m_endingBtns.size() && i < m_endingSpecs.size(); ++i) {
        m_endingBtns.at(i)->setChecked(m_endingSpecs.at(i) == ending);
        m_endingBtns.at(i)->setEnabled(!dimension);
    }

    for (int i = 0; i < m_shapeKindBtns.size(); ++i) {
        const QString btnMark = i < m_shapeMarkSpecs.size() ? m_shapeMarkSpecs.at(i) : QString();
        bool on = (m_shapeKindSpecs.at(i) == shapeKind) && (btnMark == m_canvas->congruenceMarkSpec());
        if (on && btnMark.isEmpty() && m_shapeKindSpecs.at(i) == ShapeKind::Line)
            on = (m_shapeDimensionSpecs.at(i) == dimension);
        if (on && m_shapeSidesSpecs.at(i) > 0) {
            on = (m_shapeSidesSpecs.at(i) == m_canvas->polygonSides())
                 && (i < m_shapeQuadSpecs.size() ? m_shapeQuadSpecs.at(i) == m_canvas->quadVariant() : true);
        }
        if (on && i < m_shapeSolidSpecs.size() && m_shapeSolidSpecs.at(i) >= 0)
            on = (m_shapeSolidSpecs.at(i) == m_canvas->solidKind());
        if (on && m_shapeTriSpecs.at(i) >= 0)
            on = (m_shapeTriSpecs.at(i) == m_canvas->triangleVariant());
        m_shapeKindBtns.at(i)->setChecked(on);
    }
}

void ShapesPopover::applyTheme(const ThemeConfig& theme, const QString& qss) {
    GlassCapsule::applyTheme(theme, qss);
    for (QToolButton* b : findChildren<QToolButton*>(QStringLiteral("finilySwitch"))) {
        if (auto* sw = dynamic_cast<SwitchButton*>(b))
            sw->setColors(theme.uiText, theme.accent, theme.isDark);
    }
    for (const auto& spec : m_iconSpecs)
        spec.first->setIcon(glyphIcon(spec.second, theme.uiText));
    for (const auto& spec : m_patternSpecs)
        spec.first->setIcon(QIcon(patternPixmap(spec.second, theme.uiText)));
}

} // namespace finily
