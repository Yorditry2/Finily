#include "ui/HomeScreen.h"

#include <QAction>
#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPropertyAnimation>
#include <QtMath>
#include <cmath>
#include <numbers>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {
namespace {

// Nombre de archivo seguro (sin separadores ni caracteres prohibidos).
QString sanitizedBaseName(QString name) {
    name = name.trimmed();
    static const QRegularExpression invalid(QStringLiteral(R"([\\/:*?"<>|])"));
    name.remove(invalid);
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    name.replace(spaces, QStringLiteral(" "));
    if (name.isEmpty())
        name = QStringLiteral("Cuaderno sin título");
    return name;
}

// Ruta única en la biblioteca: "Nombre.finily", "Nombre (2).finily", ...
QString uniqueNotebookPath(const QString& baseName) {
    const QString dir = HomeScreen::libraryDir();
    QString candidate = dir + QStringLiteral("/") + baseName + QStringLiteral(".finily");
    int n = 2;
    while (QFileInfo::exists(candidate))
        candidate = dir + QStringLiteral("/") + baseName + QStringLiteral(" (%1)").arg(n++)
                    + QStringLiteral(".finily");
    return candidate;
}

// Fecha relativa estilo StarNote: 'Hoy 23:10', 'Ayer', '21 nov', '21 nov 2025'.
QString relativeDate(const QDateTime& when) {
    const QDate date = when.date();
    const QDate today = QDate::currentDate();
    if (date == today)
        return QStringLiteral("Hoy ") + when.time().toString(QStringLiteral("HH:mm"));
    if (date == today.addDays(-1))
        return QStringLiteral("Ayer");
    const QLocale spanish(QLocale::Spanish);
    if (date.year() == today.year())
        return spanish.toString(date, QStringLiteral("d MMM"));
    return spanish.toString(date, QStringLiteral("d MMM yyyy"));
}

// Metadatos ligeros leídos del .finily (JSON compacto v1..v5).
struct NotebookMeta {
    QString path;
    QString title;
    ThemeId theme = ThemeId::DarkBoard;
    BackgroundTemplate tpl = BackgroundTemplate::Dots;
    int pages = 1;
    bool hasPdf = false;
    QDateTime modified;
};

NotebookMeta readNotebookMeta(const QFileInfo& info) {
    NotebookMeta meta;
    meta.path = info.absoluteFilePath();
    meta.title = info.completeBaseName();
    meta.modified = info.lastModified();

    QFile file(meta.path);
    if (!file.open(QIODevice::ReadOnly))
        return meta;

    // En el JSON compacto los ajustes (theme/template/version) se escriben
    // ANTES del array "pages": bastan los primeros 4 KiB para leerlos.
    const QString head = QString::fromUtf8(file.read(4096));
    file.seek(0); // el recuento de páginas debe barrer el archivo completo
    static const QRegularExpression themeRe(QStringLiteral("\"theme\"\\s*:\\s*(\\d+)"));
    static const QRegularExpression tplRe(QStringLiteral("\"template\"\\s*:\\s*(\\d+)"));
    const auto themeMatch = themeRe.match(head);
    const auto tplMatch = tplRe.match(head);
    if (themeMatch.hasMatch()) {
        const int theme = themeMatch.captured(1).toInt();
        if (theme >= 0 && theme <= 3)
            meta.theme = static_cast<ThemeId>(theme);
    }
    if (tplMatch.hasMatch()) {
        const int tpl = tplMatch.captured(1).toInt();
        if (tpl >= 0 && tpl <= 3)
            meta.tpl = static_cast<BackgroundTemplate>(tpl);
    }

    // Recuento de páginas barato y exacto: en v5 cada página aporta exactamente
    // un `"strokes":[` (imposible dentro del base64 de las imágenes, que no
    // contiene comillas ni dos puntos); en v1..v4 solo existe el array raíz.
    // En el mismo barrado se detecta si el cuaderno tiene páginas de PDF
    // ("isPdfPage":true) para la pestaña 'Con PDF'.
    static const QByteArray strokesNeedle = "\"strokes\":[";
    static const QByteArray pdfNeedle = "\"isPdfPage\":true";
    static const qsizetype carrySize = std::max(strokesNeedle.size(), pdfNeedle.size()) - 1;
    qint64 pages = 0;
    qint64 pdfPages = 0;
    QByteArray carry;
    auto countNonOverlapping = [](const QByteArray& buf, const QByteArray& needle, qsizetype start) -> qint64 {
        qint64 n = 0;
        qsizetype pos = start;
        while ((pos = buf.indexOf(needle, pos)) != -1) {
            ++n;
            pos += needle.size();
        }
        return n;
    };
    while (!file.atEnd()) {
        const qsizetype prevCarry = carry.size();
        const QByteArray window = carry + file.read(1 << 20);
        const qsizetype strokesStart = std::max<qsizetype>(0, prevCarry - strokesNeedle.size() + 1);
        const qsizetype pdfStart = std::max<qsizetype>(0, prevCarry - pdfNeedle.size() + 1);
        pages += countNonOverlapping(window, strokesNeedle, strokesStart);
        pdfPages += countNonOverlapping(window, pdfNeedle, pdfStart);
        carry = window.right(carrySize);
    }
    meta.pages = std::max(1, int(pages));
    meta.hasPdf = pdfPages > 0;
    return meta;
}

// Mini plantilla pintada dentro del área de vista previa de cada tarjeta.
void paintTemplatePattern(QPainter& painter, const QRectF& area, const ThemeConfig& theme,
                          BackgroundTemplate tpl) {
    painter.save();
    painter.setPen(QPen(theme.patternColor, 1.0));
    painter.setBrush(Qt::NoBrush);
    const qreal step = 22.0;
    switch (tpl) {
        case BackgroundTemplate::Grid:
            for (qreal x = area.left() + step; x < area.right(); x += step)
                painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            for (qreal y = area.top() + step; y < area.bottom(); y += step)
                painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            break;
        case BackgroundTemplate::Dots:
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme.patternColor);
            for (qreal y = area.top() + step * 0.75; y < area.bottom(); y += step)
                for (qreal x = area.left() + step * 0.75; x < area.right(); x += step)
                    painter.drawEllipse(QPointF(x, y), 1.3, 1.3);
            break;
        case BackgroundTemplate::Ruled:
            for (qreal y = area.top() + step; y < area.bottom(); y += step)
                painter.drawLine(QPointF(area.left() + 6, y), QPointF(area.right() - 6, y));
            break;
        case BackgroundTemplate::Blank:
            break;
    }
    painter.restore();
}

// Paleta de la biblioteca: neutra y luminosa (claro) o grafito (oscuro), con
// un único acento índigo. Todo se pinta a mano o con QSS explícito para que el
// estilo del sistema (KDE, Caelestia...) no pueda colarse.
struct HomePalette {
    QColor bg;
    QColor sidebar;     // superficie elevada (buscador, segmentado activo)
    QColor border;
    QColor text;
    QColor muted;
    QColor accent;
    QColor accentHover;
    QColor surface;     // tarjetas y diálogos
    QColor tint;        // relleno suave del acento
    QColor hover;
    QColor cardActionBg;
    QColor track;       // carril del control segmentado
};

HomePalette paletteFor(bool dark) {
    if (dark) {
        return {QColor("#111215"), QColor("#24262d"), QColor(255, 255, 255, 22), QColor("#f2f3f5"),
                QColor("#8d929c"), QColor("#7c8cff"), QColor("#97a4ff"), QColor("#1b1c21"),
                QColor(124, 140, 255, 40), QColor(255, 255, 255, 14), QColor(36, 38, 45, 240),
                QColor(255, 255, 255, 12)};
    }
    return {QColor("#f6f5f2"), QColor("#ffffff"), QColor(20, 20, 30, 20), QColor("#1d1d22"),
            QColor("#8b8a90"), QColor("#4f5bd5"), QColor("#3f4ac0"), QColor("#ffffff"),
            QColor(79, 91, 213, 22), QColor(20, 20, 30, 10), QColor(255, 255, 255, 245),
            QColor(20, 20, 30, 12)};
}

// Tapas pastel (claro) que en modo oscuro se apagan un poco para no deslumbrar.
const QVector<QColor>& coverColors() {
    static const QVector<QColor> colors = {
        QColor("#f2b8c2"), QColor("#a9d3b8"), QColor("#a9c4ee"), QColor("#f4dc8e"),
        QColor("#cbb9ef"), QColor("#f5c3a2"), QColor("#a8dcd3"), QColor("#2f3542"),
    };
    return colors;
}

QString coverKey(const QString& path) {
    return QStringLiteral("covers/") + QFileInfo(path).fileName();
}

int coverIndexFor(const NotebookMeta& meta) {
    QSettings settings("Finily", "Finily");
    const QVariant stored = settings.value(coverKey(meta.path));
    if (stored.isValid())
        return std::clamp(stored.toInt(), 0, int(coverColors().size()) - 1);
    return int(qHash(meta.title) % uint(coverColors().size() - 1)); // sin la tapa oscura
}

// Glifos finos de la cabecera (lupa, luna, sol, PDF, carpeta, reloj).
QIcon homeGlyph(const QString& kind, const QColor& color) {
    QPixmap pixmap(40, 40);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (kind == QLatin1String("search")) {
        p.drawEllipse(QPointF(9, 9), 5.5, 5.5);
        p.drawLine(QPointF(13.2, 13.2), QPointF(17, 17));
    } else if (kind == QLatin1String("moon")) {
        QPainterPath moon;
        moon.addEllipse(QPointF(10, 10), 7, 7);
        QPainterPath bite;
        bite.addEllipse(QPointF(14, 6.5), 6, 6);
        p.drawPath(moon.subtracted(bite));
    } else if (kind == QLatin1String("sun")) {
        p.drawEllipse(QPointF(10, 10), 3.6, 3.6);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * std::numbers::pi / 4.0;
            p.drawLine(QPointF(10 + std::cos(a) * 6.2, 10 + std::sin(a) * 6.2),
                       QPointF(10 + std::cos(a) * 8.2, 10 + std::sin(a) * 8.2));
        }
    } else if (kind == QLatin1String("pdf")) {
        QPainterPath doc;
        doc.moveTo(5, 2.5); doc.lineTo(12, 2.5); doc.lineTo(16, 6.5); doc.lineTo(16, 17.5);
        doc.lineTo(5, 17.5); doc.closeSubpath();
        p.drawPath(doc);
        p.drawLine(QPointF(10.5, 8), QPointF(10.5, 14));
        p.drawLine(QPointF(8, 11.5), QPointF(10.5, 14));
        p.drawLine(QPointF(13, 11.5), QPointF(10.5, 14));
    } else if (kind == QLatin1String("folder")) {
        QPainterPath f;
        f.moveTo(2.5, 5); f.lineTo(8, 5); f.lineTo(9.8, 7); f.lineTo(17.5, 7); f.lineTo(17.5, 16);
        f.lineTo(2.5, 16); f.closeSubpath();
        p.drawPath(f);
    } else if (kind == QLatin1String("restore")) {
        p.drawArc(QRectF(3.5, 3.5, 13, 13), 100 * 16, 290 * 16);
        p.drawLine(QPointF(8.2, 3.6), QPointF(5.4, 2));
        p.drawLine(QPointF(8.2, 3.6), QPointF(6.4, 6.4));
        p.drawLine(QPointF(10, 7), QPointF(10, 10.5));
        p.drawLine(QPointF(10, 10.5), QPointF(12.5, 12));
    } else if (kind == QLatin1String("more")) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int i = 0; i < 3; ++i)
            p.drawEllipse(QPointF(5 + i * 5, 10), 1.6, 1.6);
    }
    return QIcon(pixmap);
}

} // namespace

// Insignia de la marca: cuadrado redondeado índigo con una "f" manuscrita
// trazada como pincelada.
class FinilyBadge : public QWidget {
public:
    explicit FinilyBadge(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(34, 34);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF frame = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        QLinearGradient gradient(frame.topLeft(), frame.bottomRight());
        gradient.setColorAt(0.0, QColor("#6d7bf2"));
        gradient.setColorAt(1.0, QColor("#4338ca"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(frame, 10, 10);
        // "f" de pluma: curva superior + asta + travesaño.
        QPainterPath f;
        f.moveTo(21.5, 9.5);
        f.cubicTo(18.5, 7.5, 15.0, 8.5, 15.0, 13.0);
        f.lineTo(15.0, 25.5);
        painter.setPen(QPen(QColor(255, 255, 255, 240), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(f);
        painter.drawLine(QPointF(11.5, 15.5), QPointF(20.0, 15.5));
    }
};

// Interruptor claro/oscuro: botón redondo con luna o sol.
class ThemeSwitch : public QToolButton {
    Q_OBJECT

public:
    explicit ThemeSwitch(QWidget* parent = nullptr)
        : QToolButton(parent) {
        setObjectName("finilyRound");
        setFixedSize(38, 38);
        setIconSize(QSize(20, 20));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        connect(this, &QToolButton::clicked, this, [this] {
            m_dark = !m_dark;
            emit changed(m_dark);
        });
    }

    void setDark(bool dark) { m_dark = dark; }
    void setGlyphColor(const QColor& color) {
        setIcon(homeGlyph(m_dark ? QStringLiteral("sun") : QStringLiteral("moon"), color));
        setToolTip(m_dark ? QStringLiteral("Modo claro") : QStringLiteral("Modo oscuro"));
    }

signals:
    void changed(bool dark);

private:
    bool m_dark = false;
};

namespace {
constexpr int kCardW = 168;
constexpr int kCardH = 268;
constexpr int kCoverH = 212;
const QRectF kCover(14.0, 12.0, kCardW - 28.0, kCoverH - 18.0);

// Sombra suave en capas bajo un rectángulo redondeado (más difusa al elevarse).
void paintSoftShadow(QPainter& painter, const QRectF& r, qreal radius, qreal lift, bool dark) {
    painter.save();
    painter.setPen(Qt::NoPen);
    const int base = dark ? 70 : 22;
    for (int i = 0; i < 6; ++i) {
        const qreal spread = 1.5 + i * (1.6 + lift * 0.25);
        QColor c(10, 10, 25, std::max(2, int(base * (1.0 - i / 6.0)) / 2));
        painter.setBrush(c);
        painter.drawRoundedRect(r.adjusted(-spread * 0.6, 2 + lift * 0.6 + i, spread * 0.6, spread + lift),
                                radius + spread * 0.5, radius + spread * 0.5);
    }
    painter.restore();
}
} // namespace

// Mosaico "Crear": tarjeta clara con un botón circular de acento.
class NewNotebookCard : public QWidget {
    Q_OBJECT

public:
    explicit NewNotebookCard(QWidget* parent = nullptr)
        : QWidget(parent) {
        setObjectName("finilyNewCard");
        setFixedSize(kCardW, kCardH);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }

    void setPalette(const HomePalette& pal, bool dark) {
        m_pal = pal;
        m_dark = dark;
        update();
    }

signals:
    void createRequested();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool hover = underMouse();
        const QRectF frame = kCover;
        paintSoftShadow(painter, frame, 14, hover ? 4 : 1, m_dark);
        painter.setPen(QPen(m_pal.border, 1.0));
        painter.setBrush(m_dark ? QColor("#202228") : QColor("#fbfbfa"));
        painter.drawRoundedRect(frame, 14, 14);

        const QPointF c = frame.center();
        const qreal r = hover ? 25.0 : 23.0;
        painter.setPen(Qt::NoPen);
        QColor halo = m_pal.accent;
        halo.setAlpha(hover ? 46 : 26);
        painter.setBrush(halo);
        painter.drawEllipse(c, r + 9, r + 9);
        painter.setBrush(m_pal.accent);
        painter.drawEllipse(c, r, r);
        painter.setPen(QPen(Qt::white, 2.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(c + QPointF(-8, 0), c + QPointF(8, 0));
        painter.drawLine(c + QPointF(0, -8), c + QPointF(0, 8));

        QFont font = painter.font();
        font.setPixelSize(13);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(m_pal.text);
        painter.drawText(QRectF(0, kCoverH, width(), 22), Qt::AlignHCenter | Qt::AlignVCenter,
                         QStringLiteral("Crear"));
        font.setPixelSize(11);
        font.setWeight(QFont::Normal);
        painter.setFont(font);
        painter.setPen(m_pal.muted);
        painter.drawText(QRectF(0, kCoverH + 20, width(), 18), Qt::AlignHCenter | Qt::AlignVCenter,
                         QStringLiteral("Cuaderno nuevo"));
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit createRequested();
        QWidget::mouseReleaseEvent(event);
    }

private:
    HomePalette m_pal = paletteFor(false);
    bool m_dark = false;
};

// Cuaderno: tapa de color con lomo, cantos de páginas, banda elástica y una
// etiqueta con la plantilla real. Título y fecha debajo, como en una
// estantería. Se eleva suavemente al pasar el cursor.
class NotebookCard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal lift READ lift WRITE setLift)

public:
    explicit NotebookCard(const NotebookMeta& meta, QWidget* parent = nullptr)
        : QWidget(parent), m_meta(meta) {
        setObjectName("finilyCard");
        setFixedSize(kCardW, kCardH);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, false);
        setAutoFillBackground(false);
        m_cover = coverIndexFor(meta);

        m_moreBtn = new QToolButton(this);
        m_moreBtn->setObjectName("finilyCardAction");
        m_moreBtn->setToolTip(QStringLiteral("Opciones"));
        m_moreBtn->setFixedSize(28, 28);
        m_moreBtn->setIconSize(QSize(20, 20));
        m_moreBtn->setCursor(Qt::PointingHandCursor);
        m_moreBtn->setFocusPolicy(Qt::NoFocus);
        m_moreBtn->move(int(kCover.right()) - 34, int(kCover.top()) + 8);
        m_moreBtn->hide();
        connect(m_moreBtn, &QToolButton::clicked, this, [this] { showMenu(m_moreBtn->mapToGlobal(QPoint(0, 30))); });

        m_anim = new QPropertyAnimation(this, "lift", this);
        m_anim->setDuration(160);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
    }

    QString filePath() const { return m_meta.path; }
    QString title() const { return m_meta.title; }
    qreal lift() const { return m_lift; }
    void setLift(qreal v) {
        m_lift = v;
        update();
    }

    void setPalette(const HomePalette& pal, bool dark) {
        m_pal = pal;
        m_dark = dark;
        if (m_moreBtn)
            m_moreBtn->setIcon(homeGlyph(QStringLiteral("more"), pal.text));
        update();
    }

    bool matchesSearch(const QString& needle) const {
        return needle.isEmpty() || m_meta.title.contains(needle, Qt::CaseInsensitive);
    }

    // 0 = Todos, 1 = Recientes (7 días), 2 = Con PDF.
    bool matchesFilter(int filter) const {
        switch (filter) {
            case 1:
                return m_meta.modified.daysTo(QDateTime::currentDateTime()) <= 7;
            case 2:
                return m_meta.hasPdf;
            default:
                return true;
        }
    }

signals:
    void openRequested(const QString& filePath);
    void renameRequested();
    void deleteRequested();
    void coverChanged();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF cover = kCover.translated(0, -m_lift);
        paintSoftShadow(painter, cover, 12, m_lift, m_dark);

        QColor base = coverColors().at(m_cover);
        if (m_dark && m_cover != coverColors().size() - 1)
            base = base.darker(118);
        const bool darkCover = base.lightness() < 110;

        // Cantos de las hojas asomando por la derecha y abajo.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#ecebe6"));
        painter.drawRoundedRect(cover.adjusted(6, 4, 3, 2), 10, 10);
        painter.setBrush(QColor("#fbfaf7"));
        painter.drawRoundedRect(cover.adjusted(5, 2, 1.5, 0.5), 11, 11);

        // Tapa: esquinas izquierdas pequeñas (encuadernado), derechas suaves.
        QPainterPath shape;
        const qreal rl = 4.0, rr = 12.0;
        shape.moveTo(cover.left() + rl, cover.top());
        shape.lineTo(cover.right() - rr, cover.top());
        shape.quadTo(cover.right(), cover.top(), cover.right(), cover.top() + rr);
        shape.lineTo(cover.right(), cover.bottom() - rr);
        shape.quadTo(cover.right(), cover.bottom(), cover.right() - rr, cover.bottom());
        shape.lineTo(cover.left() + rl, cover.bottom());
        shape.quadTo(cover.left(), cover.bottom(), cover.left(), cover.bottom() - rl);
        shape.lineTo(cover.left(), cover.top() + rl);
        shape.quadTo(cover.left(), cover.top(), cover.left() + rl, cover.top());
        QLinearGradient fill(cover.topLeft(), cover.bottomRight());
        fill.setColorAt(0.0, base.lighter(106));
        fill.setColorAt(1.0, base.darker(104));
        painter.setBrush(fill);
        painter.drawPath(shape);

        painter.save();
        painter.setClipPath(shape);
        // Lomo con doblez.
        painter.fillRect(QRectF(cover.left(), cover.top(), 13, cover.height()), QColor(0, 0, 0, 26));
        painter.fillRect(QRectF(cover.left() + 13, cover.top(), 1.2, cover.height()), QColor(255, 255, 255, 70));
        painter.fillRect(QRectF(cover.left() + 14.2, cover.top(), 1.0, cover.height()), QColor(0, 0, 0, 22));
        // Banda elástica.
        painter.fillRect(QRectF(cover.right() - 22, cover.top(), 6, cover.height()),
                         darkCover ? QColor(255, 255, 255, 40) : base.darker(130));
        painter.fillRect(QRectF(cover.right() - 22, cover.top(), 1.2, cover.height()), QColor(255, 255, 255, 50));
        // Brillo superior muy sutil.
        QLinearGradient sheen(cover.topLeft(), QPointF(cover.left(), cover.top() + 60));
        sheen.setColorAt(0.0, QColor(255, 255, 255, 46));
        sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillRect(cover, sheen);
        painter.restore();

        // Etiqueta de papel (como la de un cuaderno real) con el título a
        // mano alzada y una muestra redonda de la hoja: color + plantilla.
        const QRectF label(cover.left() + 26, cover.top() + 34, cover.width() - 58, 66);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 20));
        painter.drawRoundedRect(label.translated(0, 1.5), 6, 6);
        painter.setBrush(QColor("#fdfcf8"));
        painter.drawRoundedRect(label, 6, 6);
        painter.setPen(QPen(QColor(120, 140, 180, 60), 0.8));
        for (qreal y = label.top() + 22; y < label.bottom() - 6; y += 13)
            painter.drawLine(QPointF(label.left() + 8, y), QPointF(label.right() - 8, y));
        QFont labelFont = painter.font();
        labelFont.setPixelSize(11);
        labelFont.setWeight(QFont::DemiBold);
        painter.setFont(labelFont);
        painter.setPen(QColor("#2b2b33"));
        const QRectF textBox = label.adjusted(8, 7, -8, -16);
        const QString elided = QFontMetrics(labelFont).elidedText(m_meta.title, Qt::ElideRight,
                                                                  int(textBox.width() * 2 - 12));
        painter.drawText(textBox, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, elided);

        const ThemeConfig theme = getThemeConfig(m_meta.theme);
        const QRectF swatch(label.right() - 18, label.bottom() - 18, 13, 13);
        painter.save();
        QPainterPath swatchClip;
        swatchClip.addEllipse(swatch);
        painter.setClipPath(swatchClip);
        painter.fillRect(swatch, theme.background);
        paintTemplatePattern(painter, swatch.adjusted(-6, -6, 6, 6), theme, m_meta.tpl);
        painter.restore();
        painter.setPen(QPen(QColor(0, 0, 0, 40), 0.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(swatch);
        if (m_meta.hasPdf) {
            QFont tag = painter.font();
            tag.setPixelSize(9);
            tag.setWeight(QFont::Bold);
            painter.setFont(tag);
            const QRectF chip(label.left() + 6, label.bottom() - 17, 26, 13);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#ef4444"));
            painter.drawRoundedRect(chip, 4, 4);
            painter.setPen(Qt::white);
            painter.drawText(chip, Qt::AlignCenter, QStringLiteral("PDF"));
        }

        // Número de páginas, discreto en la esquina inferior.
        QFont small = painter.font();
        small.setPixelSize(10);
        small.setWeight(QFont::DemiBold);
        painter.setFont(small);
        painter.setPen(darkCover ? QColor(255, 255, 255, 170) : QColor(0, 0, 0, 110));
        const QString pagesText = m_meta.pages == 1 ? QStringLiteral("1 página")
                                                    : QStringLiteral("%1 páginas").arg(m_meta.pages);
        painter.drawText(QRectF(cover.left() + 24, cover.bottom() - 26, cover.width() - 54, 16),
                         Qt::AlignLeft | Qt::AlignVCenter, pagesText);

        if (m_hovered) {
            painter.setPen(QPen(m_pal.accent, 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(cover.adjusted(-3.5, -3.5, 3.5, 3.5), 14, 14);
        }

        // Título y fecha bajo la tapa.
        QFont titleFont = painter.font();
        titleFont.setPixelSize(13);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.setPen(m_pal.text);
        painter.drawText(QRectF(8, kCoverH, width() - 16, 22), Qt::AlignHCenter | Qt::AlignVCenter,
                         QFontMetrics(titleFont).elidedText(m_meta.title, Qt::ElideRight, width() - 16));
        QFont dateFont = painter.font();
        dateFont.setPixelSize(11);
        dateFont.setWeight(QFont::Normal);
        painter.setFont(dateFont);
        painter.setPen(m_pal.muted);
        painter.drawText(QRectF(8, kCoverH + 20, width() - 16, 18), Qt::AlignHCenter | Qt::AlignVCenter,
                         relativeDate(m_meta.modified));
    }

    void enterEvent(QEnterEvent*) override {
        m_hovered = true;
        m_moreBtn->show();
        animateLift(5.0);
    }

    void leaveEvent(QEvent*) override {
        m_hovered = false;
        m_moreBtn->hide();
        animateLift(0.0);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit openRequested(m_meta.path);
        QWidget::mouseReleaseEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override { showMenu(event->globalPos()); }

private:
    void animateLift(qreal target) {
        m_anim->stop();
        m_anim->setStartValue(m_lift);
        m_anim->setEndValue(target);
        m_anim->start();
    }

    void showMenu(const QPoint& at) {
        QMenu menu(this);
        menu.setStyleSheet(QStringLiteral(
            "QMenu { background: %1; border: 1px solid %2; border-radius: 12px; padding: 6px; }"
            "QMenu::item { color: %3; padding: 7px 18px; border-radius: 8px; font-size: 13px; }"
            "QMenu::item:selected { background: %4; }"
            "QMenu::separator { height: 1px; background: %2; margin: 5px 8px; }")
                               .arg(m_pal.surface.name(), m_pal.border.name(QColor::HexArgb),
                                    m_pal.text.name(), m_pal.tint.name(QColor::HexArgb)));
        menu.addAction(QStringLiteral("Abrir"), this, [this] { emit openRequested(m_meta.path); });
        menu.addAction(QStringLiteral("Renombrar…"), this, &NotebookCard::renameRequested);
        QMenu* colors = menu.addMenu(QStringLiteral("Color de tapa"));
        colors->setStyleSheet(menu.styleSheet());
        static const QStringList names = {"Rosa", "Salvia", "Cielo", "Mantequilla",
                                          "Lavanda", "Durazno", "Menta", "Grafito"};
        for (int i = 0; i < coverColors().size(); ++i) {
            QPixmap dot(28, 28);
            dot.setDevicePixelRatio(2.0);
            dot.fill(Qt::transparent);
            QPainter p(&dot);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(0, 0, 0, 40), 0.8));
            p.setBrush(coverColors().at(i));
            p.drawEllipse(QRectF(1.5, 1.5, 11, 11));
            p.end();
            QAction* action = colors->addAction(QIcon(dot), names.value(i));
            connect(action, &QAction::triggered, this, [this, i] {
                QSettings settings("Finily", "Finily");
                settings.setValue(coverKey(m_meta.path), i);
                m_cover = i;
                update();
            });
        }
        menu.addSeparator();
        menu.addAction(QStringLiteral("Eliminar"), this, &NotebookCard::deleteRequested);
        menu.exec(at);
    }

    NotebookMeta m_meta;
    HomePalette m_pal = paletteFor(false);
    bool m_dark = false;
    bool m_hovered = false;
    int m_cover = 0;
    qreal m_lift = 0.0;
    QToolButton* m_moreBtn = nullptr;
    QPropertyAnimation* m_anim = nullptr;
};

HomeScreen::HomeScreen(QWidget* parent)
    : QWidget(parent) {
    setObjectName("finilyHome");
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    QSettings settings("Finily", "Finily");
    m_dark = settings.value("home/darkMode", false).toBool();

    buildSidebar();
    buildGallery();
    applyTheme();
    refreshLibrary();
}

QString HomeScreen::libraryDir() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                        + QStringLiteral("/Finily");
    QDir().mkpath(dir);
    return dir;
}

// Cabecera: marca a la izquierda; buscador, acciones y modo a la derecha.
void HomeScreen::buildSidebar() {
    m_sidebar = new QWidget(this);
    m_sidebar->setAttribute(Qt::WA_StyledBackground, false);
    auto* row = new QHBoxLayout(m_sidebar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    m_badge = new FinilyBadge(m_sidebar);
    row->addWidget(m_badge, 0, Qt::AlignVCenter);
    m_titleLabel = new QLabel(QStringLiteral("Finily"), m_sidebar);
    m_titleLabel->setObjectName("finilyHomeTitle");
    row->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    row->addStretch(1);

    m_search = new QLineEdit(m_sidebar);
    m_search->setObjectName("finilySearch");
    m_search->setPlaceholderText(QStringLiteral("Buscar"));
    m_search->setClearButtonEnabled(true);
    m_search->setFocusPolicy(Qt::ClickFocus); // sin anillo de foco al abrir
    m_search->setFixedSize(240, 38);
    m_searchAction = m_search->addAction(QIcon(), QLineEdit::LeadingPosition);
    connect(m_search, &QLineEdit::textChanged, this, &HomeScreen::applySearchFilter);
    row->addWidget(m_search, 0, Qt::AlignVCenter);

    auto makeAction = [this](const QString& text, const QString& tip) {
        auto* b = new QToolButton(m_sidebar);
        b->setObjectName("finilyGhost");
        b->setText(text);
        b->setToolTip(tip);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(18, 18));
        b->setFixedHeight(38);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    m_draftBtn = makeAction(QStringLiteral("Continuar borrador"), QStringLiteral("Recuperar la última sesión autoguardada"));
    m_draftBtn->setObjectName("finilyDraft");
    connect(m_draftBtn, &QToolButton::clicked, this, &HomeScreen::continueDraftRequested);
    row->addWidget(m_draftBtn);
    m_importBtn = makeAction(QStringLiteral("Importar PDF"), QStringLiteral("Convertir un PDF en cuaderno"));
    connect(m_importBtn, &QToolButton::clicked, this, &HomeScreen::importPdfFromHomeRequested);
    row->addWidget(m_importBtn);
    m_openBtn = makeAction(QStringLiteral("Abrir"), QStringLiteral("Abrir un cuaderno .finily desde el disco"));
    connect(m_openBtn, &QToolButton::clicked, this, &HomeScreen::openExternalFileRequested);
    row->addWidget(m_openBtn);

    m_themeSwitch = new ThemeSwitch(m_sidebar);
    m_themeSwitch->setDark(m_dark);
    connect(m_themeSwitch, &ThemeSwitch::changed, this, &HomeScreen::setDarkMode);
    row->addWidget(m_themeSwitch, 0, Qt::AlignVCenter);
}

void HomeScreen::buildGallery() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 26, 40, 0);
    root->setSpacing(0);
    root->addWidget(m_sidebar);
    root->addSpacing(30);

    // Título grande + subtítulo con el recuento.
    m_sectionTitle = new QLabel(QStringLiteral("Mis cuadernos"), this);
    m_sectionTitle->setObjectName("finilySectionTitle");
    root->addWidget(m_sectionTitle);
    root->addSpacing(2);
    m_countPill = new QLabel(this);
    m_countPill->setObjectName("finilyCountPill");
    root->addWidget(m_countPill);
    root->addSpacing(18);

    // Control segmentado de filtros.
    auto* segment = new QWidget(this);
    segment->setObjectName("finilySegment");
    segment->setAttribute(Qt::WA_StyledBackground, true);
    auto* segRow = new QHBoxLayout(segment);
    segRow->setContentsMargins(4, 4, 4, 4);
    segRow->setSpacing(2);
    auto* tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);
    const QStringList tabNames = {QStringLiteral("Todos"), QStringLiteral("Recientes"), QStringLiteral("Con PDF")};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* tab = new QToolButton(segment);
        tab->setObjectName("finilyFilter");
        tab->setText(tabNames.at(i));
        tab->setCheckable(true);
        tab->setChecked(i == 0);
        tab->setFixedHeight(30);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFocusPolicy(Qt::NoFocus);
        tabGroup->addButton(tab, i);
        connect(tab, &QToolButton::clicked, this, [this, i] { selectFilter(i); });
        m_filterTabs.append(tab);
        segRow->addWidget(tab);
    }
    auto* segLine = new QHBoxLayout();
    segLine->addWidget(segment);
    segLine->addStretch(1);
    root->addLayout(segLine);
    root->addSpacing(10);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("finilyHomeScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->viewport()->setAttribute(Qt::WA_StyledBackground, false);
    m_gridContent = new QWidget;
    m_gridContent->setObjectName("finilyHomeGrid");
    m_gridContent->setAutoFillBackground(false);
    m_gridContent->setAttribute(Qt::WA_StyledBackground, false);
    m_scroll->setWidget(m_gridContent);
    root->addWidget(m_scroll, 1);

    m_newCard = new NewNotebookCard(m_gridContent);
    connect(m_newCard, &NewNotebookCard::createRequested, this, &HomeScreen::openCreateDialog);

    m_emptyState = new QWidget(m_gridContent);
    m_emptyState->setAttribute(Qt::WA_StyledBackground, false);
    auto* emptyBox = new QVBoxLayout(m_emptyState);
    emptyBox->setContentsMargins(30, 12, 30, 12);
    m_emptyLabel = new QLabel(m_emptyState);
    m_emptyLabel->setObjectName("finilyEmpty");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    emptyBox->addWidget(m_emptyLabel);
    m_emptyState->hide();
}

void HomeScreen::applyTheme() {
    const HomePalette pal = paletteFor(m_dark);
    setStyleSheet(QStringLiteral(R"(
QLabel { color: %1; background: transparent; }
QLabel#finilyHomeTitle { color: %2; font-size: 17px; font-weight: 700; }
QLabel#finilySectionTitle { color: %2; font-size: 30px; font-weight: 800; }
QLabel#finilyCountPill { color: %1; font-size: 13px; }
QLabel#finilyEmpty { color: %1; font-size: 14px; }
QLineEdit#finilySearch {
    background: %6; border: 1px solid %5; border-radius: 19px;
    color: %2; padding: 0px 12px 0px 4px; font-size: 13px;
    selection-background-color: %3; selection-color: #ffffff;
}
QLineEdit#finilySearch:focus { border: 1px solid %3; }
QToolButton { background: transparent; border: none; color: %2; font-size: 13px; }
QToolButton#finilyGhost, QToolButton#finilyDraft {
    background: %6; border: 1px solid %5; border-radius: 19px; padding: 0px 14px; font-weight: 600;
}
QToolButton#finilyGhost:hover { border: 1px solid %3; }
QToolButton#finilyDraft { background: %4; border: 1px solid transparent; color: %3; }
QToolButton#finilyDraft:hover { border: 1px solid %3; }
QToolButton#finilyRound { background: %6; border: 1px solid %5; border-radius: 19px; }
QToolButton#finilyRound:hover { border: 1px solid %3; }
QWidget#finilySegment { background: @TRACK@; border-radius: 19px; }
QToolButton#finilyFilter {
    color: %1; font-weight: 600; border-radius: 15px; padding: 0px 18px;
}
QToolButton#finilyFilter:hover { color: %2; }
QToolButton#finilyFilter:checked { background: @SEGON@; color: %2; border: 1px solid %5; }
QToolButton#finilyCardAction { background: %7; border: 1px solid %5; border-radius: 14px; }
QToolButton#finilyCardAction:hover { border: 1px solid %3; }
QScrollArea, QWidget#finilyHomeGrid { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px 4px 0px; }
QScrollBar::handle:vertical { background: %5; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)")
                      .arg(pal.muted.name(QColor::HexArgb))
                      .arg(pal.text.name(QColor::HexArgb))
                      .arg(pal.accent.name(QColor::HexArgb))
                      .arg(pal.tint.name(QColor::HexArgb))
                      .arg(pal.border.name(QColor::HexArgb))
                      .arg(pal.surface.name(QColor::HexArgb))
                      .arg(pal.cardActionBg.name(QColor::HexArgb))
                      .replace(QStringLiteral("@TRACK@"), QStringLiteral("rgba(%1,%2,%3,%4)")
                                                             .arg(pal.track.red()).arg(pal.track.green())
                                                             .arg(pal.track.blue()).arg(pal.track.alpha()))
                      .replace(QStringLiteral("@SEGON@"), m_dark ? QStringLiteral("#2c2e36") : QStringLiteral("#ffffff")));
    if (m_scroll)
        m_scroll->viewport()->setAutoFillBackground(false);

    if (m_searchAction)
        m_searchAction->setIcon(homeGlyph(QStringLiteral("search"), pal.muted));
    if (m_importBtn)
        m_importBtn->setIcon(homeGlyph(QStringLiteral("pdf"), pal.text));
    if (m_openBtn)
        m_openBtn->setIcon(homeGlyph(QStringLiteral("folder"), pal.text));
    if (m_draftBtn)
        m_draftBtn->setIcon(homeGlyph(QStringLiteral("restore"), pal.accent));
    if (m_themeSwitch) {
        m_themeSwitch->setDark(m_dark);
        m_themeSwitch->setGlyphColor(pal.text);
    }
    if (m_newCard)
        m_newCard->setPalette(pal, m_dark);
    for (NotebookCard* card : m_cards)
        if (card)
            card->setPalette(pal, m_dark);
    update();
}

void HomeScreen::setDarkMode(bool dark) {
    if (m_dark == dark)
        return;
    m_dark = dark;
    QSettings settings("Finily", "Finily");
    settings.setValue("home/darkMode", dark);
    applyTheme();
}

void HomeScreen::paintEvent(QPaintEvent*) {
    const HomePalette pal = paletteFor(m_dark);
    QPainter painter(this);
    painter.fillRect(rect(), pal.bg);
}

void HomeScreen::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

void HomeScreen::refreshLibrary() {
    for (NotebookCard* card : m_cards)
        if (card)
            card->deleteLater();
    m_cards.clear();

    const QDir dir(libraryDir());
    const QFileInfoList entries = dir.entryInfoList({QStringLiteral("*.finily")}, QDir::Files, QDir::Time);
    const HomePalette pal = paletteFor(m_dark);
    for (const QFileInfo& info : entries) {
        const NotebookMeta meta = readNotebookMeta(info);
        auto* card = new NotebookCard(meta, m_gridContent);
        card->setPalette(pal, m_dark);
        connect(card, &NotebookCard::openRequested, this, &HomeScreen::openNotebookRequested);
        connect(card, &NotebookCard::renameRequested, this, [this, card] { renameNotebook(card); });
        connect(card, &NotebookCard::deleteRequested, this, [this, card] { deleteNotebook(card); });
        m_cards.append(card);
    }

    const QString autosave = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/autosave.json");
    if (m_draftBtn)
        m_draftBtn->setVisible(QFileInfo::exists(autosave));

    applySearchFilter(m_search ? m_search->text() : QString());
}

void HomeScreen::selectFilter(int index) {
    m_filter = static_cast<Filter>(std::clamp(index, 0, 2));
    for (int i = 0; i < m_filterTabs.size(); ++i)
        if (m_filterTabs.at(i))
            m_filterTabs.at(i)->setChecked(i == index);
    applySearchFilter(m_search ? m_search->text() : QString());
}

// Visibilidad con setHidden + isHidden(): isVisible() es falso mientras la
// pantalla de inicio está oculta en el QStackedWidget, lo que dejaba tarjetas
// sin colocar (amontonadas en 0,0) al volver del editor.
void HomeScreen::applySearchFilter(const QString& text) {
    const QString needle = text.trimmed();
    for (NotebookCard* card : m_cards)
        if (card)
            card->setHidden(!(card->matchesSearch(needle) && card->matchesFilter(int(m_filter))));
    if (m_newCard)
        m_newCard->setHidden(!needle.isEmpty());
    updateEmptyState();
}

void HomeScreen::updateEmptyState() {
    int visible = 0;
    for (const NotebookCard* card : m_cards)
        if (card && !card->isHidden())
            ++visible;
    const bool searching = m_search && !m_search->text().trimmed().isEmpty();
    const bool showEmpty = visible == 0 && (searching || m_filter != Filter::Todos);
    if (m_emptyState) {
        m_emptyState->setHidden(!showEmpty);
        if (m_emptyLabel)
            m_emptyLabel->setText(searching ? QStringLiteral("No hay cuadernos que coincidan con la búsqueda")
                                  : m_filter == Filter::ConPdf ? QStringLiteral("Aún no tienes cuadernos con PDF")
                                                               : QStringLiteral("Nada editado en los últimos 7 días"));
    }
    updateSectionHeader();
    relayoutGrid();
}

void HomeScreen::updateSectionHeader() {
    if (!m_countPill)
        return;
    const int total = m_cards.size();
    m_countPill->setText(total == 0   ? QStringLiteral("Crea tu primer cuaderno para empezar la clase")
                         : total == 1 ? QStringLiteral("1 cuaderno")
                                      : QStringLiteral("%1 cuadernos").arg(total));
}

void HomeScreen::relayoutGrid() {
    if (!m_gridContent || !m_scroll)
        return;
    constexpr int spacingX = 18;
    constexpr int spacingY = 20;
    constexpr int top = 14;

    const int contentWidth = m_scroll->viewport()->width();
    const int columns = std::max(1, (contentWidth + spacingX) / (kCardW + spacingX));

    QList<QWidget*> visibleCards;
    if (m_newCard && !m_newCard->isHidden())
        visibleCards.append(m_newCard);
    for (NotebookCard* card : m_cards)
        if (card && !card->isHidden())
            visibleCards.append(card);

    int index = 0;
    for (QWidget* card : visibleCards) {
        const int row = index / columns;
        const int col = index % columns;
        card->setGeometry(col * (kCardW + spacingX) - 14 + 14, top + row * (kCardH + spacingY), kCardW, kCardH);
        card->show();
        ++index;
    }
    const int rows = std::max(1, (index + columns - 1) / columns);
    m_gridContent->setMinimumHeight(top + rows * (kCardH + spacingY) + 24);

    if (m_emptyState && !m_emptyState->isHidden())
        m_emptyState->setGeometry(0, top + (index > 0 ? kCardH + 30 : 40), std::max(0, contentWidth), 80);
}

void HomeScreen::openCreateDialog() {
    const HomePalette pal = paletteFor(m_dark);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Nuevo cuaderno"));
    dialog.setModal(true);
    dialog.setFixedWidth(480);
    dialog.setStyleSheet(
        QStringLiteral(R"(
QDialog { background: %1; }
QLabel { color: %2; background: transparent; font-size: 11px; font-weight: 700; letter-spacing: 0.6px; }
QLabel#finilyDialogHeading { color: %3; font-size: 20px; font-weight: 800; letter-spacing: 0px; }
QLabel#finilyDialogSub { color: %2; font-size: 12px; font-weight: 400; letter-spacing: 0px; }
QLineEdit {
    background: %4; border: 1.5px solid %5; border-radius: 12px;
    color: %3; padding: 9px 14px; font-size: 14px; font-weight: 500;
    selection-background-color: %6; selection-color: #ffffff;
}
QLineEdit:focus { border: 1.5px solid %6; }
QToolButton {
    background: %4; border: 1.5px solid %5; border-radius: 11px;
    color: %3; padding: 7px 10px; font-size: 12px; font-weight: 600;
}
QToolButton:hover { border: 1.5px solid %6; }
QToolButton:checked {
    background: %7; border: 1.5px solid %6; color: %3; font-weight: 700;
}
QToolButton#finilyThemeCard {
    background: %4; border: 1.5px solid %5; border-radius: 13px;
    color: %3; padding: 8px 6px; font-size: 11px; font-weight: 600;
}
QToolButton#finilyThemeCard:hover { border: 1.5px solid %6; }
QToolButton#finilyThemeCard:checked {
    background: %7; border: 2px solid %6; color: %3; font-weight: 700;
}
QPushButton {
    background: transparent; border: 1.5px solid %5; border-radius: 11px;
    color: %3; padding: 8px 18px; font-size: 13px; font-weight: 600;
}
QPushButton:hover { border: 1.5px solid %6; }
QPushButton#finilyCreatePrimary {
    background: %6; border: 1.5px solid %6; color: #ffffff;
    padding: 8px 22px; font-size: 13px; font-weight: 700;
}
QPushButton#finilyCreatePrimary:hover { background: %6; border: 1.5px solid %3; }
)")
            .arg(pal.bg.name(QColor::HexArgb))
            .arg(pal.muted.name(QColor::HexArgb))
            .arg(pal.text.name(QColor::HexArgb))
            .arg(pal.surface.name(QColor::HexArgb))
            .arg(pal.border.name(QColor::HexArgb))
            .arg(pal.accent.name(QColor::HexArgb))
            .arg(pal.tint.name(QColor::HexArgb)));

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(24, 22, 24, 20);
    column->setSpacing(10);

    auto* heading = new QLabel(QStringLiteral("Nuevo cuaderno"), &dialog);
    heading->setObjectName("finilyDialogHeading");
    column->addWidget(heading);

    auto* sub = new QLabel(QStringLiteral("Elige el estilo de papel y formato para tu clase o apuntes"), &dialog);
    sub->setObjectName("finilyDialogSub");
    column->addWidget(sub);
    column->addSpacing(4);

    column->addWidget(new QLabel(QStringLiteral("NOMBRE"), &dialog));
    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setFixedHeight(40);
    nameEdit->setPlaceholderText(QStringLiteral("Ej: Álgebra — Semana 4"));
    nameEdit->setText(QStringLiteral("Cuaderno sin título"));
    nameEdit->selectAll();
    column->addWidget(nameEdit);
    column->addSpacing(4);

    // 4 tarjetas visuales de tema con su color REAL de pizarra/papel
    column->addWidget(new QLabel(QStringLiteral("SUPERFICIE"), &dialog));
    auto* themeRow = new QHBoxLayout();
    themeRow->setSpacing(8);
    auto* themeGroup = new QButtonGroup(&dialog);
    themeGroup->setExclusive(true);
    const QVector<ThemeId> themes = {ThemeId::DarkBoard, ThemeId::GreenBoard, ThemeId::PaperLight,
                                     ThemeId::PaperCream};
    const QVector<QString> shortThemeNames = {QStringLiteral("Oscura"), QStringLiteral("Academia"),
                                              QStringLiteral("Blanco"), QStringLiteral("Crema")};
    auto makeThemePreview = [](ThemeId id) -> QIcon {
        const ThemeConfig tc = getThemeConfig(id);
        QPixmap pm(144, 88);
        pm.setDevicePixelRatio(2.0);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r(1.0, 1.0, 70.0, 42.0);
        p.setPen(QPen(tc.isDark ? QColor(255, 255, 255, 40) : QColor(15, 23, 42, 35), 1.0));
        p.setBrush(tc.background);
        p.drawRoundedRect(r, 7.0, 7.0);
        p.setPen(Qt::NoPen);
        p.setBrush(tc.patternColor);
        for (int y = 10; y <= 34; y += 12)
            for (int x = 12; x <= 60; x += 12)
                p.drawEllipse(QPointF(x, y), 1.0, 1.0);
        QPainterPath wave;
        wave.moveTo(14.0, 28.0);
        wave.cubicTo(24.0, 15.0, 38.0, 32.0, 56.0, 17.0);
        p.setPen(QPen(tc.defaultInk, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(wave);
        return QIcon(pm);
    };
    for (int i = 0; i < themes.size(); ++i) {
        auto* button = new QToolButton(&dialog);
        button->setObjectName("finilyThemeCard");
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(makeThemePreview(themes.at(i)));
        button->setIconSize(QSize(72, 44));
        button->setText(shortThemeNames.at(i));
        button->setFixedHeight(84);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        themeGroup->addButton(button, i);
        themeRow->addWidget(button);
    }
    column->addLayout(themeRow);
    column->addSpacing(4);

    // Formato de hoja: Infinita / Hoja A4 / Diapo 16:9
    column->addWidget(new QLabel(QStringLiteral("FORMATO DE HOJA"), &dialog));
    auto* formatRow = new QHBoxLayout();
    formatRow->setSpacing(8);
    auto* formatGroup = new QButtonGroup(&dialog);
    formatGroup->setExclusive(true);
    const QVector<QPair<QString, PageFormat>> formats = {
        {QStringLiteral("Infinita"), PageFormat::Infinite},
        {QStringLiteral("Hoja A4"), PageFormat::A4},
        {QStringLiteral("Diapo 16:9"), PageFormat::Slide16x9},
    };
    for (int i = 0; i < formats.size(); ++i) {
        auto* button = new QToolButton(&dialog);
        button->setText(formats.at(i).first);
        button->setFixedHeight(36);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        formatGroup->addButton(button, i);
        formatRow->addWidget(button);
    }
    column->addLayout(formatRow);
    column->addSpacing(4);

    // Plantilla: Liso / Cuadrícula / Puntos / Rayado
    column->addWidget(new QLabel(QStringLiteral("PLANTILLA"), &dialog));
    auto* templateRow = new QHBoxLayout();
    templateRow->setSpacing(8);
    auto* templateGroup = new QButtonGroup(&dialog);
    templateGroup->setExclusive(true);
    const QVector<QPair<QString, BackgroundTemplate>> templates = {
        {QStringLiteral("Liso"), BackgroundTemplate::Blank},
        {QStringLiteral("Cuadrícula"), BackgroundTemplate::Grid},
        {QStringLiteral("Puntos"), BackgroundTemplate::Dots},
        {QStringLiteral("Rayado"), BackgroundTemplate::Ruled},
    };
    for (int i = 0; i < templates.size(); ++i) {
        auto* button = new QToolButton(&dialog);
        button->setText(templates.at(i).first);
        button->setFixedHeight(36);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setCheckable(true);
        button->setChecked(templates.at(i).second == BackgroundTemplate::Grid);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        templateGroup->addButton(button, i);
        templateRow->addWidget(button);
    }
    column->addLayout(templateRow);
    column->addSpacing(10);

    // Botones de acción limpios
    auto* actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(10);
    actionsRow->addStretch(1);
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancelar"), &dialog);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setFocusPolicy(Qt::NoFocus);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    actionsRow->addWidget(cancelBtn);

    auto* createBtn = new QPushButton(QStringLiteral("Crear cuaderno"), &dialog);
    createBtn->setObjectName("finilyCreatePrimary");
    createBtn->setDefault(true);
    createBtn->setCursor(Qt::PointingHandCursor);
    connect(createBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    actionsRow->addWidget(createBtn);
    column->addLayout(actionsRow);

    if (dialog.exec() != QDialog::Accepted)
        return;
    if (themeGroup->checkedId() < 0 || templateGroup->checkedId() < 0 || formatGroup->checkedId() < 0)
        return;
    const ThemeId theme = themes.at(themeGroup->checkedId());
    const BackgroundTemplate tpl = templates.at(templateGroup->checkedId()).second;
    const PageFormat format = formats.at(formatGroup->checkedId()).second;
    const QString path = uniqueNotebookPath(sanitizedBaseName(nameEdit->text()));
    emit createNotebookRequested(path, theme, tpl, format);
}

void HomeScreen::renameNotebook(NotebookCard* card) {
    if (!card)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Renombrar cuaderno"),
                                               QStringLiteral("Nuevo nombre:"), QLineEdit::Normal,
                                               card->title(), &ok);
    if (!ok)
        return;
    const QString clean = sanitizedBaseName(name);
    const QString dir = libraryDir();
    const QString target = dir + QStringLiteral("/") + clean + QStringLiteral(".finily");
    if (target == card->filePath())
        return; // sin cambios
    QString newPath = target;
    int n = 2;
    while (QFileInfo::exists(newPath))
        newPath = dir + QStringLiteral("/") + clean + QStringLiteral(" (%1)").arg(n++)
                  + QStringLiteral(".finily");
    if (!QFile::rename(card->filePath(), newPath)) {
        QMessageBox::warning(this, QStringLiteral("Renombrar cuaderno"),
                             QStringLiteral("No se pudo renombrar el cuaderno."));
        return;
    }
    QSettings settings("Finily", "Finily");
    const QVariant cover = settings.value(coverKey(card->filePath()));
    if (cover.isValid()) {
        settings.setValue(coverKey(newPath), cover);
        settings.remove(coverKey(card->filePath()));
    }
    refreshLibrary();
}

void HomeScreen::deleteNotebook(NotebookCard* card) {
    if (!card)
        return;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Eliminar cuaderno"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("¿Eliminar «%1»?\nEsta acción no se puede deshacer.")
                    .arg(card->title()));
    QPushButton* removeButton = box.addButton(QStringLiteral("Eliminar"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Cancelar"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == removeButton) {
        QFile::remove(card->filePath());
        QSettings("Finily", "Finily").remove(coverKey(card->filePath()));
        refreshLibrary();
    }
}

// AUTOMOC: las clases Q_OBJECT definidas en este .cpp (ThemeSwitch,
// NewNotebookCard, NotebookCard) viven en finily — el código moc generado
// debe compilarse dentro de este mismo namespace.
#include "HomeScreen.moc"

} // namespace finily
