#include "ui/home/SettingsView.h"

#include "core/Utils.h"
#include "engine/LibraryStore.h"
#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabletEvent>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace finily {

namespace {

constexpr double kPressureValues[] = {0.3, 0.5, 0.8};
constexpr int kRetentionDays[] = {7, 30, 90, 0};

QFont pxFont(const QFont& base, qreal px, int weight) {
    QFont f = base;
    f.setPixelSize(std::max(1, int(px + 0.5)));
    f.setWeight(QFont::Weight(weight));
    return f;
}

QString appVersionText() {
    const QString v = QCoreApplication::applicationVersion().trimmed();
    return v.isEmpty() ? QStringLiteral("0.2.1") : v;
}

double pressureMix(double base, double varied, double sensitivity) {
    return base + (varied - base) * std::clamp(sensitivity, 0.0, 1.0);
}

QString humanSize(qint64 bytes) {
    return QLocale(QLocale::Spanish).formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

qint64 dirSize(const QString& path) {
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// Base común para los controles de Ajustes pintados con DesignTokens.
class Themed : public QWidget {
public:
    using QWidget::QWidget;
    void setTheme(bool dark) {
        m_dark = dark;
        update();
    }

protected:
    tokens::Palette tk() const { return tokens::paletteFor(m_dark); }
    bool m_dark = false;
};

// .sgroup: panel redondeado con filas separadas por líneas finas.
class SettingsGroup : public Themed {
public:
    explicit SettingsGroup(QWidget* parent)
        : Themed(parent) {
        layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }
    QVBoxLayout* layout = nullptr;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor line = m_dark ? QColor(255, 255, 255, 18) : QColor(15, 23, 42, 18);
        p.setPen(QPen(line, 1));
        p.setBrush(tk().surface);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
        for (int i = 1; i < layout->count(); ++i) {
            if (QWidget* w = layout->itemAt(i)->widget())
                p.drawLine(QPointF(0, w->y() + 0.5), QPointF(width(), w->y() + 0.5));
        }
    }
};

// .text-btn
class TextButton : public Themed {
public:
    TextButton(const QString& text, const QString& icon, std::function<void()> run, QWidget* parent)
        : Themed(parent)
        , m_text(text)
        , m_icon(icon)
        , m_run(std::move(run)) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        const QFont f = pxFont(font(), 12, QFont::DemiBold);
        setFixedSize(QFontMetrics(f).horizontalAdvance(text) + 26 + (icon.isEmpty() ? 0 : 18), 30);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(m_dark ? QColor(255, 255, 255, 26) : QColor(15, 23, 42, 26), 1));
        p.setBrush(underMouse() ? tk().hover : tk().surface);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 15, 15);
        qreal x = 13;
        if (!m_icon.isEmpty()) {
            paintUiIcon(&p, QRectF(x, 8.5, 13, 13), m_icon, tk().icon);
            x += 18;
        }
        p.setFont(pxFont(font(), 12, QFont::DemiBold));
        p.setPen(tk().icon);
        p.drawText(QRectF(x, 0, width() - x, height()), Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_run)
            m_run();
    }

private:
    QString m_text;
    QString m_icon;
    std::function<void()> m_run;
};

// Fila del índice lateral (.snav .nav).
class NavItem : public Themed {
public:
    NavItem(const QString& icon, const QString& text, const QString& badge, QWidget* parent)
        : Themed(parent)
        , m_icon(icon)
        , m_text(text)
        , m_badge(badge) {
        setFixedHeight(32);
        setAttribute(Qt::WA_Hover, true);
        if (badge.isEmpty())
            setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> run;
    bool on = false;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const bool soon = !m_badge.isEmpty();
        if (on || (underMouse() && !soon)) {
            p.setPen(Qt::NoPen);
            p.setBrush(on ? tk().soft : tk().hover);
            p.drawRoundedRect(QRectF(rect()), tokens::radius::Nav, tokens::radius::Nav);
        }
        const QColor fg = soon ? tk().faint : on ? tk().text : tk().icon;
        paintUiIcon(&p, QRectF(10, 9, 14, 14), m_icon, fg);
        p.setFont(pxFont(font(), 12.5, on ? QFont::DemiBold : QFont::Medium));
        p.setPen(fg);
        p.drawText(QRect(32, 0, width() - 32, height()), Qt::AlignLeft | Qt::AlignVCenter, m_text);
        if (soon) {
            p.setFont(pxFont(font(), 10, QFont::DemiBold));
            p.setPen(tk().faint);
            p.drawText(QRect(0, 0, width() - 10, height()), Qt::AlignRight | Qt::AlignVCenter, m_badge);
        }
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && run)
            run();
    }

private:
    QString m_icon;
    QString m_text;
    QString m_badge;
};

// Tarjeta «Personalización avanzada · próximamente» (.soon).
class SoonCard : public Themed {
public:
    explicit SoonCard(QWidget* parent)
        : Themed(parent) {
        setMinimumHeight(88);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(m_dark ? QColor(255, 255, 255, 50) : QColor(24, 24, 27, 56), 1, Qt::DashLine);
        p.setPen(pen);
        p.setBrush(tk().sidebar);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
        paintUiIcon(&p, QRectF(16, 15, 18, 18), QStringLiteral("sparkle"), tk().icon);
        p.setFont(pxFont(font(), 12.5, QFont::DemiBold));
        p.setPen(tk().text);
        p.drawText(QRect(46, 14, width() - 62, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Personalización avanzada · próximamente"));
        p.setFont(pxFont(font(), 11.5, QFont::Normal));
        p.setPen(tk().muted);
        p.drawText(QRect(46, 36, width() - 62, height() - 44), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("Aquí podrás cambiar el aspecto de las carpetas, usar tus propios iconos, crear "
                                  "temas completos e instalar paquetes. Por eso el diseño base es neutro: todo lo de "
                                  "color se podrá reemplazar."));
    }
};

// Logo oficial B1 (Facetada 45°) en Acerca de.
class AboutLogo : public Themed {
public:
    explicit AboutLogo(QWidget* parent)
        : Themed(parent) {
        setFixedSize(48, 48);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r(rect());
        QLinearGradient grad(r.topLeft(), r.bottomRight());
        if (m_dark) {
            grad.setColorAt(0.0, QColor(255, 255, 255));
            grad.setColorAt(1.0, QColor(228, 228, 231));
        } else {
            grad.setColorAt(0.0, QColor(39, 39, 44));
            grad.setColorAt(1.0, QColor(20, 20, 23));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawRoundedRect(r, 13, 13);
        const QColor fg = m_dark ? QColor(20, 21, 26) : QColor(255, 255, 255);
        paintUiIcon(&p, QRectF(7, 7, 34, 34), QStringLiteral("brand-mark"), fg);
    }
};

} // namespace

// .switch
class ToggleSwitch : public Themed {
public:
    explicit ToggleSwitch(QWidget* parent)
        : Themed(parent) {
        setFixedSize(32, 19);
        setCursor(Qt::PointingHandCursor);
    }
    bool checked = false;
    std::function<void(bool)> toggled;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(checked ? tk().accent : (m_dark ? QColor(0x3F, 0x40, 0x4A) : QColor(0xD4, 0xD4, 0xD8)));
        p.drawRoundedRect(QRectF(rect()), 9.5, 9.5);
        p.setBrush(checked ? tk().accentFg : QColor(Qt::white));
        p.drawEllipse(QRectF(checked ? 15 : 2, 2, 15, 15));
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton || !rect().contains(e->pos()))
            return;
        checked = !checked;
        update();
        if (toggled)
            toggled(checked);
    }
};

// .seg
class SegmentedControl : public Themed {
public:
    SegmentedControl(const QStringList& labels, QWidget* parent)
        : Themed(parent)
        , m_labels(labels) {
        setCursor(Qt::PointingHandCursor);
        const QFontMetrics fm(pxFont(font(), 11.5, QFont::DemiBold));
        qreal x = 2;
        for (const QString& l : labels) {
            const qreal w = fm.horizontalAdvance(l) + 20;
            m_rects.append(QRectF(x, 2, w, 26));
            x += w + 2;
        }
        setFixedSize(int(x), 30);
    }
    int current = 0;
    std::function<void(int)> changed;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(tk().field);
        p.drawRoundedRect(QRectF(rect()), 9, 9);
        p.setFont(pxFont(font(), 11.5, QFont::DemiBold));
        for (int i = 0; i < m_rects.size(); ++i) {
            if (i == current) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(15, 23, 42, m_dark ? 60 : 22));
                p.drawRoundedRect(m_rects[i].translated(0, 1), 7, 7);
                p.setBrush(tk().surface);
                p.drawRoundedRect(m_rects[i], 7, 7);
            }
            p.setPen(i == current ? tk().text : tk().muted);
            p.drawText(m_rects[i], Qt::AlignCenter, m_labels[i]);
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton)
            return;
        for (int i = 0; i < m_rects.size(); ++i) {
            if (m_rects[i].contains(e->position()) && i != current) {
                current = i;
                update();
                if (changed)
                    changed(i);
                return;
            }
        }
    }

private:
    QStringList m_labels;
    QVector<QRectF> m_rects;
};

// .theme-pick: tres miniaturas Claro / Oscuro / Sistema.
class ThemePicker : public Themed {
public:
    explicit ThemePicker(QWidget* parent)
        : Themed(parent) {
        setFixedSize(3 * 92 + 2 * 10 + 8, 60 + 6 + 16 + 6);
        setCursor(Qt::PointingHandCursor);
    }
    int current = 0;
    std::function<void(int)> changed;

protected:
    QRectF cardRect(int i) const { return QRectF(4 + i * 102, 4, 92, 60); }
    void paintMini(QPainter& p, const QRectF& r, const QColor& sb, const QColor& main, const QColor& item) {
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(r, 10, 10);
        p.setClipPath(clip);
        p.fillRect(r, main);
        p.fillRect(QRectF(r.left(), r.top(), 26, r.height()), sb);
        p.setPen(Qt::NoPen);
        p.setBrush(item);
        for (int k = 0; k < 6; ++k)
            p.drawRoundedRect(QRectF(r.left() + 33 + (k % 3) * 21, r.top() + 8 + (k / 3) * 17, 16, 12), 3, 3);
        p.restore();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QString labels[] = {QStringLiteral("Claro"), QStringLiteral("Oscuro"), QStringLiteral("Sistema")};
        for (int i = 0; i < 3; ++i) {
            const QRectF r = cardRect(i);
            if (i == 0) {
                paintMini(p, r, QColor(0xF6, 0xF6, 0xF4), Qt::white, QColor(0xE4, 0xE4, 0xE7));
            } else if (i == 1) {
                paintMini(p, r, QColor(0x15, 0x16, 0x1C), QColor(0x1A, 0x1B, 0x22), QColor(0x2E, 0x30, 0x3A));
            } else {
                p.save();
                QPainterPath clip;
                clip.addRoundedRect(r, 10, 10);
                p.setClipPath(clip);
                p.fillRect(r, Qt::white);
                QPolygonF tri;
                tri << r.topRight() << r.bottomRight() << r.bottomLeft();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x1A, 0x1B, 0x22));
                p.drawPolygon(tri);
                p.restore();
            }
            p.setBrush(Qt::NoBrush);
            if (i == current) {
                p.setPen(QPen(tk().accent, 1.5));
                p.drawRoundedRect(r.adjusted(-2.75, -2.75, 2.75, 2.75), 12.5, 12.5);
            } else {
                p.setPen(QPen(m_dark ? QColor(255, 255, 255, 30) : QColor(15, 23, 42, 26), 1));
                p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
            }
            p.setFont(pxFont(font(), 11.5, QFont::DemiBold));
            p.setPen(i == current ? tk().text : tk().muted);
            p.drawText(QRectF(r.left(), r.bottom() + 6, r.width(), 16), Qt::AlignCenter, labels[i]);
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton)
            return;
        for (int i = 0; i < 3; ++i) {
            if (cardRect(i).adjusted(0, 0, 0, 24).contains(e->position()) && i != current) {
                current = i;
                update();
                if (changed)
                    changed(i);
                return;
            }
        }
    }
};

// .acc: 5 muestras circulares de color de acento.
class AccentPicker : public Themed {
public:
    explicit AccentPicker(QWidget* parent)
        : Themed(parent) {
        setFixedSize(tokens::kAccentPresetCount * 22 + (tokens::kAccentPresetCount - 1) * 8 + 8, 30);
        setCursor(Qt::PointingHandCursor);
    }

    int current = 0;
    std::function<void(int)> changed;

protected:
    QRectF swatchRect(int i) const { return QRectF(4 + i * 30, 4, 22, 22); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < tokens::kAccentPresetCount; ++i) {
            const QRectF r = swatchRect(i);
            if (i == current) {
                p.setPen(QPen(tk().accent, 1.5));
                p.setBrush(tk().surface);
                p.drawEllipse(r.adjusted(-2.75, -2.75, 2.75, 2.75));
            }
            p.setPen(QPen(m_dark ? QColor(255, 255, 255, 42) : QColor(15, 23, 42, 28), 1));
            p.setBrush(tokens::accentSwatchColor(i));
            p.drawEllipse(r);
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton)
            return;
        for (int i = 0; i < tokens::kAccentPresetCount; ++i) {
            if (swatchRect(i).adjusted(-3, -3, 3, 3).contains(e->position()) && i != current) {
                current = i;
                update();
                if (changed)
                    changed(i);
                return;
            }
        }
    }
};

// .curve: vista previa cúbica de respuesta a la presión (Suave / Normal / Firme).
class CurvePreviewWidget : public Themed {
public:
    explicit CurvePreviewWidget(QWidget* parent)
        : Themed(parent) {
        setFixedSize(140, 70);
    }

    double sensitivity = 0.5;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(tk().field);
        p.drawRoundedRect(QRectF(rect()), 10.0, 10.0);

        const QPointF p0(12.0, 58.0);
        const QPointF p3(128.0, 12.0);
        p.setPen(QPen(tk().guide, 1.0, Qt::DashLine));
        p.drawLine(p0, p3);

        // Interpolación entre Suave (0.3), Normal (0.5) y Firme (0.8).
        const double s = std::clamp(sensitivity, 0.3, 0.8);
        QPointF c1;
        QPointF c2;
        if (s <= 0.5) {
            const double t = (s - 0.3) / 0.2;
            c1 = QPointF(38.0 + (50.0 - 38.0) * t, 22.0 + (58.0 - 22.0) * t);
            c2 = QPointF(78.0 + (80.0 - 78.0) * t, 12.0 + (26.0 - 12.0) * t);
        } else {
            const double t = (s - 0.5) / 0.3;
            c1 = QPointF(50.0 + (78.0 - 50.0) * t, 58.0);
            c2 = QPointF(80.0 + (108.0 - 80.0) * t, 26.0 + (44.0 - 26.0) * t);
        }

        QPainterPath path(p0);
        path.cubicTo(c1, c2, p3);
        p.setPen(QPen(tk().accent, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        p.setPen(Qt::NoPen);
        p.setBrush(tk().accent);
        p.drawEllipse(path.pointAtPercent(0.5), 3.0, 3.0);
    }
};

// .test: caja interactiva «Prueba aquí» para trazo con tableta o ratón.
class PenTestCanvas : public Themed {
public:
    explicit PenTestCanvas(QWidget* parent)
        : Themed(parent) {
        setFixedHeight(90);
        setCursor(Qt::CrossCursor);
        setAttribute(Qt::WA_AcceptTouchEvents, false);
    }

    double sensitivity = 0.5;

protected:
    struct Sample {
        QPointF pos;
        double pressure = 0.5;
    };

    void paintVariableStroke(QPainter& p, const QVector<Sample>& pts) const {
        p.setPen(Qt::NoPen);
        p.setBrush(tk().text);
        for (int i = 1; i < pts.size(); ++i) {
            const QPointF a = pts[i - 1].pos;
            const QPointF b = pts[i].pos;
            const double r0 = pressureMix(2.4, 0.8 + 4.4 * pts[i - 1].pressure, sensitivity);
            const double r1 = pressureMix(2.4, 0.8 + 4.4 * pts[i].pressure, sensitivity);
            const double dist = std::hypot(b.x() - a.x(), b.y() - a.y());
            const int steps = std::max(1, int(std::ceil(dist / 1.8)));
            for (int k = 0; k <= steps; ++k) {
                const double t = double(k) / double(steps);
                const QPointF pt = a + (b - a) * t;
                const double r = r0 + (r1 - r0) * t;
                p.drawEllipse(pt, r, r);
            }
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QColor dashColor = m_dark ? QColor(255, 255, 255, 50) : QColor(24, 24, 27, 51);
        p.setPen(QPen(dashColor, 1.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12.0, 12.0);

        p.save();
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 11.0, 11.0);
        p.setClipPath(clip);

        if (m_points.size() >= 2) {
            paintVariableStroke(p, m_points);
        } else {
            // Trazo de muestra sensible a la configuración actual hasta que el usuario dibuje.
            QVector<Sample> demo;
            const qreal w = std::max<qreal>(120.0, width() - 48.0);
            for (int i = 0; i <= 48; ++i) {
                const double t = double(i) / 48.0;
                const qreal x = 24.0 + w * t;
                const qreal y = 46.0 - 20.0 * std::sin(t * 3.14159265 * 2.5);
                const double pres = 0.15 + 0.85 * std::sin(t * 3.14159265);
                demo.append({QPointF(x, y), pres});
            }
            paintVariableStroke(p, demo);
        }
        p.restore();

        p.setFont(pxFont(font(), 10.5, QFont::Medium));
        p.setPen(tk().faint);
        p.drawText(QRectF(0, 0, width() - 10, height() - 7), Qt::AlignRight | Qt::AlignBottom,
                   QStringLiteral("Prueba aquí"));
    }

    void tabletEvent(QTabletEvent* event) override {
        const double pres = std::clamp(double(event->pressure()), 0.05, 1.0);
        if (event->type() == QEvent::TabletPress) {
            m_points.clear();
            m_drawing = true;
            m_points.append({event->position(), pres});
            update();
            event->accept();
            return;
        }
        if (event->type() == QEvent::TabletMove && m_drawing) {
            m_points.append({event->position(), pres});
            update();
            event->accept();
            return;
        }
        if (event->type() == QEvent::TabletRelease) {
            m_drawing = false;
            event->accept();
            return;
        }
        QWidget::tabletEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_points.clear();
            m_drawing = true;
            m_simPressure = 0.65;
            m_points.append({event->position(), m_simPressure});
            update();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!m_drawing || m_points.isEmpty())
            return;
        const QPointF pos = event->position();
        const QPointF prev = m_points.back().pos;
        const double dist = std::hypot(pos.x() - prev.x(), pos.y() - prev.y());
        if (dist < 1.5)
            return;
        const double target = std::clamp(0.88 - dist * 0.018, 0.20, 0.95);
        m_simPressure = m_simPressure * 0.65 + target * 0.35;
        m_points.append({pos, m_simPressure});
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton)
            m_drawing = false;
    }

private:
    QVector<Sample> m_points;
    bool m_drawing = false;
    double m_simPressure = 0.65;
};

// Barra de 3 segmentos y leyenda de espacio usado (mockup 10 escena 04).
class StorageMeterWidget : public Themed {
public:
    explicit StorageMeterWidget(QWidget* parent)
        : Themed(parent) {
        setFixedHeight(74);
    }

    void setSizes(qint64 notebooks, qint64 pdfs, qint64 trash) {
        m_notebooks = std::max<qint64>(0, notebooks);
        m_pdfs = std::max<qint64>(0, pdfs);
        m_trash = std::max<qint64>(0, trash);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qint64 total = m_notebooks + m_pdfs + m_trash;
        p.setFont(pxFont(font(), 12.5, QFont::DemiBold));
        p.setPen(tk().text);
        p.drawText(QRectF(14, 12, width() - 28, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Espacio usado · %1").arg(humanSize(total)));

        const QRectF bar(14, 36, std::max<qreal>(40.0, width() - 28), 6.0);
        p.setPen(Qt::NoPen);
        p.setBrush(tk().field);
        p.drawRoundedRect(bar, 3.0, 3.0);

        const QColor segColors[3] = {tk().accent, tk().muted, tk().faint};
        const qint64 segSizes[3] = {m_notebooks, m_pdfs, m_trash};
        if (total > 0) {
            QPainterPath clip;
            clip.addRoundedRect(bar, 3.0, 3.0);
            p.save();
            p.setClipPath(clip);
            qreal x = bar.left();
            for (int i = 0; i < 3; ++i) {
                if (segSizes[i] <= 0)
                    continue;
                qreal segW = bar.width() * (double(segSizes[i]) / double(total));
                segW = std::max<qreal>(segW, 4.0);
                segW = std::min(segW, bar.right() - x);
                if (segW > 0.0) {
                    p.fillRect(QRectF(x, bar.top(), segW, bar.height()), segColors[i]);
                    x += segW;
                }
            }
            p.restore();
        }

        const QString labels[3] = {
            QStringLiteral("Cuadernos %1").arg(humanSize(m_notebooks)),
            QStringLiteral("PDF %1").arg(humanSize(m_pdfs)),
            QStringLiteral("Papelera %1").arg(humanSize(m_trash)),
        };
        const QFont lf = pxFont(font(), 11.0, QFont::Normal);
        const QFontMetricsF fm(lf);
        p.setFont(lf);
        qreal lx = 14.0;
        const qreal ly = 50.0;
        for (int i = 0; i < 3; ++i) {
            p.setPen(Qt::NoPen);
            p.setBrush(segColors[i]);
            p.drawRoundedRect(QRectF(lx, ly + 4.0, 8.0, 8.0), 2.0, 2.0);
            lx += 13.0;
            const qreal tw = std::ceil(fm.horizontalAdvance(labels[i]));
            p.setPen(tk().muted);
            p.drawText(QRectF(lx, ly, tw + 2.0, 16.0), Qt::AlignLeft | Qt::AlignVCenter, labels[i]);
            lx += tw + 14.0;
        }
    }

private:
    qint64 m_notebooks = 0;
    qint64 m_pdfs = 0;
    qint64 m_trash = 0;
};

// ============================================================== SettingsView

int SettingsView::themeMode() {
    const QVariant v = readAppSetting(QStringLiteral("home/themeMode"), QVariant());
    if (v.isValid())
        return std::clamp(v.toInt(), 0, 2);
    return readAppSetting(QStringLiteral("home/darkMode"), false).toBool() ? Dark : Light;
}

void SettingsView::setThemeMode(int mode) {
    writeAppSetting(QStringLiteral("home/themeMode"), mode);
}

SettingsView::SettingsView(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    // Índice lateral.
    m_nav = new QWidget(this);
    m_nav->setFixedWidth(196);
    auto* navCol = new QVBoxLayout(m_nav);
    navCol->setContentsMargins(10, 16, 10, 16);
    navCol->setSpacing(1);
    row->addWidget(m_nav);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->installEventFilter(this);
    m_scroll->viewport()->installEventFilter(this);
    m_scroll->verticalScrollBar()->installEventFilter(this);

    auto* content = new QWidget;
    content->setAutoFillBackground(false);
    auto* outer = new QHBoxLayout(content);
    outer->setContentsMargins(34, 20, 34, 40);
    auto* paneBox = new QWidget(content);
    paneBox->setMaximumWidth(600);
    auto* pane = new QVBoxLayout(paneBox);
    pane->setContentsMargins(0, 0, 0, 0);
    pane->setSpacing(10);
    outer->addWidget(paneBox, 1);
    outer->addStretch(0);
    m_scroll->setWidget(content);
    row->addWidget(m_scroll, 1);

    auto trackThemed = [this](auto* w) {
        m_themed.append(w);
        return w;
    };

    // --- 0. Apariencia
    QWidget* g = addGroup(pane, QStringLiteral("Apariencia"));
    m_themePicker = new ThemePicker(g);
    m_themePicker->changed = [this](int mode) {
        setThemeMode(mode);
        emit themeModeChanged(mode);
    };
    addRow(g, QStringLiteral("Tema"),
           QStringLiteral("Sistema sigue el modo claro u oscuro de tu escritorio."),
           m_themePicker);

    m_accentPicker = new AccentPicker(g);
    m_accentPicker->changed = [this](int index) {
        tokens::setAccentPreset(index);
        refreshLocalTheme();
        emit appearanceChanged();
    };
    addRow(g, QStringLiteral("Color de acento"),
           QStringLiteral("Botón + Nuevo, selección y controles activos."),
           m_accentPicker);

    m_reducedMotion = new ToggleSwitch(g);
    m_reducedMotion->toggled = [this](bool on) {
        tokens::setReducedMotion(on);
        refreshLocalTheme();
        emit appearanceChanged();
    };
    addRow(g, QStringLiteral("Reducir animaciones"),
           QStringLiteral("Quita rebotes y transiciones largas."),
           m_reducedMotion);

    auto* soon = trackThemed(new SoonCard(paneBox));
    pane->addSpacing(2);
    pane->addWidget(soon);

    // --- 1. Inicio
    g = addGroup(pane, QStringLiteral("Inicio"));
    m_startupSeg = new SegmentedControl(
        {QStringLiteral("Biblioteca"), QStringLiteral("Último doc"), QStringLiteral("Pizarra")}, g);
    m_startupSeg->changed = [this](int i) {
        writeAppSetting(QStringLiteral("home/startupBehavior"), i);
        emit homePrefsChanged();
    };
    addRow(g, QStringLiteral("Al abrir Finily"), QString(), m_startupSeg);

    m_showDates = new ToggleSwitch(g);
    m_showDates->toggled = [this](bool on) {
        writeAppSetting(QStringLiteral("home/showDates"), on);
        emit homePrefsChanged();
    };
    addRow(g, QStringLiteral("Mostrar fecha bajo el nombre"), QString(), m_showDates);

    m_foldersFirst = new ToggleSwitch(g);
    m_foldersFirst->toggled = [this](bool on) {
        writeAppSetting(QStringLiteral("home/foldersFirst"), on);
        emit homePrefsChanged();
    };
    addRow(g, QStringLiteral("Carpetas antes que documentos"), QString(), m_foldersFirst);

    m_openInNewTab = new ToggleSwitch(g);
    m_openInNewTab->toggled = [this](bool on) {
        writeAppSetting(QStringLiteral("home/openInNewTab"), on);
        emit homePrefsChanged();
    };
    addRow(g, QStringLiteral("Abrir documentos en pestaña nueva"),
           QStringLiteral("Si no, reemplaza la pestaña actual."),
           m_openInNewTab);

    // --- 2. Lápiz y tableta
    g = addGroup(pane, QStringLiteral("Lápiz y tableta"));
    {
        auto* pressureRow = new QWidget(g);
        auto* ph = new QHBoxLayout(pressureRow);
        ph->setContentsMargins(14, 14, 14, 14);
        ph->setSpacing(14);

        auto* left = new QVBoxLayout();
        left->setSpacing(4);
        auto* t = new QLabel(QStringLiteral("Sensibilidad a la presión"), pressureRow);
        t->setObjectName(QStringLiteral("rowTitle"));
        auto* d = new QLabel(QStringLiteral("Cuánto cambia el grosor del trazo: Suave · Normal · Firme."), pressureRow);
        d->setObjectName(QStringLiteral("rowDesc"));
        d->setWordWrap(true);
        m_pressureSeg = trackThemed(new SegmentedControl(
            {QStringLiteral("Suave"), QStringLiteral("Normal"), QStringLiteral("Firme")}, pressureRow));
        left->addWidget(t);
        left->addWidget(d);
        left->addSpacing(6);
        left->addWidget(m_pressureSeg, 0, Qt::AlignLeft);

        m_curvePreview = trackThemed(new CurvePreviewWidget(pressureRow));
        ph->addLayout(left, 1);
        ph->addWidget(m_curvePreview, 0, Qt::AlignVCenter);
        static_cast<SettingsGroup*>(g)->layout->addWidget(pressureRow);

        auto* testRow = new QWidget(g);
        auto* th = new QHBoxLayout(testRow);
        th->setContentsMargins(14, 12, 14, 12);
        m_penTest = trackThemed(new PenTestCanvas(testRow));
        th->addWidget(m_penTest);
        static_cast<SettingsGroup*>(g)->layout->addWidget(testRow);

        m_pressureSeg->changed = [this](int i) {
            const double val = kPressureValues[std::clamp(i, 0, 2)];
            writeAppSetting(QStringLiteral("ink/pressureSensitivity"), val);
            m_curvePreview->sensitivity = val;
            m_curvePreview->update();
            m_penTest->sensitivity = val;
            m_penTest->update();
            emit pressureSensitivityChanged(val);
        };
    }

    m_palmRejection = new ToggleSwitch(g);
    m_palmRejection->toggled = [this](bool on) {
        writeAppSetting(QStringLiteral("ink/palmRejection"), on);
        emit palmRejectionChanged(on);
    };
    addRow(g, QStringLiteral("Ignorar la palma"),
           QStringLiteral("Con lápiz cerca, el toque con la mano no dibuja."),
           m_palmRejection);

    // --- 3. Almacenamiento
    g = addGroup(pane, QStringLiteral("Almacenamiento"));
    m_pathLabel = new QLabel(g);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    {
        auto* btns = new QWidget(g);
        auto* bh = new QHBoxLayout(btns);
        bh->setContentsMargins(0, 0, 0, 0);
        bh->setSpacing(6);
        auto* openDir = trackThemed(new TextButton(QStringLiteral("Abrir carpeta"), QStringLiteral("open"), [] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(LibraryStore::resolveDirectory()));
        }, btns));
        auto* changeDir = trackThemed(new TextButton(QStringLiteral("Cambiar…"), QString(), [this] {
            const QString current = LibraryStore::resolveDirectory();
            const QString picked = QFileDialog::getExistingDirectory(
                this, QStringLiteral("Elegir carpeta de la biblioteca"), current);
            if (!picked.isEmpty()) {
                LibraryStore::setCustomLibraryDirectory(picked);
                refreshStorageInfo();
                emit libraryDirChanged();
            }
        }, btns));
        bh->addWidget(openDir);
        bh->addWidget(changeDir);
        addRow(g, QStringLiteral("Carpeta de la biblioteca"), QString(), btns);
    }

    m_storageMeter = trackThemed(new StorageMeterWidget(g));
    static_cast<SettingsGroup*>(g)->layout->addWidget(m_storageMeter);

    m_retentionSeg = new SegmentedControl(
        {QStringLiteral("7 días"), QStringLiteral("30 días"), QStringLiteral("90 días"), QStringLiteral("Nunca")}, g);
    m_retentionSeg->changed = [](int i) {
        LibraryStore::setTrashRetentionDays(kRetentionDays[std::clamp(i, 0, 3)]);
    };
    addRow(g, QStringLiteral("Vaciar la papelera automáticamente"), QString(), m_retentionSeg);

    // --- 4. Actualizaciones
    g = addGroup(pane, QStringLiteral("Actualizaciones"));
    auto* checkNow = new TextButton(QStringLiteral("Buscar ahora"), QStringLiteral("refresh"),
                                    [this] { emit checkUpdatesRequested(); }, g);
    addRow(g, QStringLiteral("Finily %1").arg(appVersionText()), QString(), checkNow);
    m_updatesLabel = g->findChildren<QLabel*>(QStringLiteral("rowDesc")).value(0);
    m_autoCheck = new ToggleSwitch(g);
    m_autoCheck->toggled = [](bool on) { writeAppSetting(QStringLiteral("updates/autoCheck"), on); };
    addRow(g, QStringLiteral("Buscar actualizaciones al abrir"), QString(), m_autoCheck);

    // --- 5. Acerca de
    g = addGroup(pane, QStringLiteral("Acerca de"));
    auto* logo = trackThemed(new AboutLogo(g));
    {
        auto* r = new QWidget(g);
        auto* h = new QHBoxLayout(r);
        h->setContentsMargins(14, 14, 14, 14);
        h->setSpacing(14);
        h->addWidget(logo);
        auto* texts = new QVBoxLayout();
        texts->setSpacing(2);
        auto* t = new QLabel(QStringLiteral("Finily %1").arg(appVersionText()), r);
        t->setObjectName(QStringLiteral("rowTitle"));
        auto* d = new QLabel(QStringLiteral("Licencia GPL-3.0 · Software libre para escribir y dibujar a mano."), r);
        d->setObjectName(QStringLiteral("rowDesc"));
        d->setWordWrap(true);
        texts->addWidget(t);
        texts->addWidget(d);
        h->addLayout(texts, 1);
        static_cast<SettingsGroup*>(g)->layout->addWidget(r);
    }
    pane->addStretch(1);

    // Índice: 6 secciones + Personalizar · pronto.
    const QStringList icons = {
        QStringLiteral("palette"), QStringLiteral("grid"),    QStringLiteral("pen"),
        QStringLiteral("hdd"),     QStringLiteral("refresh"), QStringLiteral("info"),
    };
    for (int i = 0; i < m_sectionHeads.size(); ++i) {
        auto* item = trackThemed(
            new NavItem(icons.value(i), static_cast<QLabel*>(m_sectionHeads[i])->text(), QString(), m_nav));
        item->run = [this, i] { showSection(i); };
        navCol->addWidget(item);
    }
    navCol->addStretch(1);
    auto* custom = trackThemed(
        new NavItem(QStringLiteral("sparkle"), QStringLiteral("Personalizar"), QStringLiteral("pronto"), m_nav));
    custom->setToolTip(QStringLiteral("Próximamente: aspecto de carpetas, iconos y temas propios"));
    navCol->addWidget(custom);

    connect(m_scroll->verticalScrollBar(), &QScrollBar::sliderPressed, this, [this] { m_pinnedSection = -1; });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::sliderMoved, this, [this] { m_pinnedSection = -1; });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        if (m_pinnedSection >= 0) {
            updateActiveNav(m_pinnedSection);
            return;
        }
        int active = 0;
        for (int i = 0; i < m_sectionHeads.size(); ++i) {
            if (m_sectionHeads[i]->y() - 30 <= v)
                active = i;
        }
        if (v <= 0)
            active = 0;
        else if (v >= m_scroll->verticalScrollBar()->maximum())
            active = int(m_sectionHeads.size()) - 1;
        updateActiveNav(active);
    });
}

QWidget* SettingsView::addGroup(QVBoxLayout* pane, const QString& title) {
    auto* head = new QLabel(title, pane->parentWidget());
    head->setObjectName(QStringLiteral("sectionHead"));
    if (!m_sectionHeads.isEmpty())
        pane->addSpacing(14);
    pane->addWidget(head);
    m_sectionHeads.append(head);
    auto* group = new SettingsGroup(pane->parentWidget());
    m_themed.append(group);
    pane->addWidget(group);
    return group;
}

void SettingsView::addRow(QWidget* group, const QString& title, const QString& desc, QWidget* control) {
    auto* r = new QWidget(group);
    r->setMinimumHeight(52);
    auto* h = new QHBoxLayout(r);
    h->setContentsMargins(14, 10, 14, 10);
    h->setSpacing(14);
    auto* texts = new QVBoxLayout();
    texts->setSpacing(2);
    texts->addStretch(1);
    auto* t = new QLabel(title, r);
    t->setObjectName(QStringLiteral("rowTitle"));
    texts->addWidget(t);
    if (m_pathLabel && m_pathLabel->parentWidget() == group
        && title == QStringLiteral("Carpeta de la biblioteca")) {
        m_pathLabel->setParent(r);
        m_pathLabel->setObjectName(QStringLiteral("pathBox"));
        texts->addSpacing(3);
        texts->addWidget(m_pathLabel, 0, Qt::AlignLeft);
    }
    auto* d = new QLabel(desc, r);
    d->setObjectName(QStringLiteral("rowDesc"));
    d->setWordWrap(true);
    d->setVisible(!desc.isEmpty() || title.startsWith(QStringLiteral("Finily")));
    texts->addWidget(d);
    texts->addStretch(1);
    h->addLayout(texts, 1);
    if (control) {
        control->setParent(r);
        if (auto* th = dynamic_cast<Themed*>(control))
            m_themed.append(th);
        h->addWidget(control, 0, Qt::AlignVCenter);
    }
    static_cast<SettingsGroup*>(group)->layout->addWidget(r);
}

void SettingsView::updateActiveNav(int active) {
    const auto items = m_nav->findChildren<QWidget*>();
    int idx = 0;
    for (QWidget* w : items) {
        auto* n = dynamic_cast<NavItem*>(w);
        if (!n || n->run == nullptr)
            continue;
        const bool next = (idx++ == active);
        if (n->on != next) {
            n->on = next;
            n->update();
        }
    }
}

void SettingsView::refreshLocalTheme() {
    for (QWidget* w : m_themed) {
        if (auto* th = dynamic_cast<Themed*>(w))
            th->setTheme(m_dark);
    }
    update();
}

void SettingsView::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    const tokens::Palette tk = tokens::paletteFor(dark);
    refreshLocalTheme();
    setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; }"
        "QLabel#sectionHead { color: %1; font-size: 17px; font-weight: 700; }"
        "QLabel#rowTitle { color: %1; font-size: 12.5px; font-weight: 600; }"
        "QLabel#rowDesc { color: %2; font-size: 11.5px; }"
        "QLabel#pathBox { color: %3; background: %4; border-radius: 8px; padding: 5px 9px;"
        " font-family: monospace; font-size: 11px; }")
                      .arg(tk.text.name(), tk.muted.name(), tk.icon.name(), tk.field.name()));
    update();
}

void SettingsView::refreshStorageInfo() {
    const QString root = LibraryStore::resolveDirectory();
    QString shown = QDir::toNativeSeparators(root);
    const QString home = QDir::homePath();
    if (shown.startsWith(home))
        shown = QStringLiteral("~") + shown.mid(home.size());
    m_pathLabel->setText(shown);

    qint64 notebooksBytes = 0;
    qint64 pdfBytes = 0;
    const QVector<NotebookMeta> allDocs = LibraryStore::listAllNotebooksRecursive();
    for (const NotebookMeta& meta : allDocs) {
        const qint64 sz = QFileInfo(meta.path).size();
        if (meta.hasPdf)
            pdfBytes += sz;
        else
            notebooksBytes += sz;
    }
    const qint64 trashBytes = dirSize(QDir(root).filePath(QStringLiteral(".papelera")));
    if (m_storageMeter)
        m_storageMeter->setSizes(notebooksBytes, pdfBytes, trashBytes);
}

void SettingsView::reload() {
    m_themePicker->current = themeMode();
    m_themePicker->update();
    m_accentPicker->current = tokens::accentPreset();
    m_accentPicker->update();
    m_reducedMotion->checked = tokens::reducedMotion();
    m_reducedMotion->update();

    m_startupSeg->current = std::clamp(readAppSetting(QStringLiteral("home/startupBehavior"), 0).toInt(), 0, 2);
    m_startupSeg->update();
    m_showDates->checked = readAppSetting(QStringLiteral("home/showDates"), true).toBool();
    m_showDates->update();
    m_foldersFirst->checked = readAppSetting(QStringLiteral("home/foldersFirst"), true).toBool();
    m_foldersFirst->update();
    m_openInNewTab->checked = readAppSetting(QStringLiteral("home/openInNewTab"), true).toBool();
    m_openInNewTab->update();

    const double ps = readAppSetting(QStringLiteral("ink/pressureSensitivity"), 0.5).toDouble();
    int best = 0;
    for (int i = 1; i < 3; ++i) {
        if (std::abs(kPressureValues[i] - ps) < std::abs(kPressureValues[best] - ps))
            best = i;
    }
    m_pressureSeg->current = best;
    m_pressureSeg->update();
    m_curvePreview->sensitivity = kPressureValues[best];
    m_curvePreview->update();
    m_penTest->sensitivity = kPressureValues[best];
    m_penTest->update();
    m_palmRejection->checked = readAppSetting(QStringLiteral("ink/palmRejection"), true).toBool();
    m_palmRejection->update();

    refreshStorageInfo();

    const int days = LibraryStore::trashRetentionDays();
    int retIdx = 1;
    for (int i = 0; i < 4; ++i) {
        if (kRetentionDays[i] == days) {
            retIdx = i;
            break;
        }
    }
    m_retentionSeg->current = retIdx;
    m_retentionSeg->update();

    m_autoCheck->checked = readAppSetting(QStringLiteral("updates/autoCheck"), true).toBool();
    m_autoCheck->update();

    if (m_updatesLabel) {
        const qint64 last = readAppSetting(QStringLiteral("updates/lastCheckUtc"), 0).toLongLong();
        m_updatesLabel->setText(
            last > 0 ? QStringLiteral("Última comprobación: %1")
                           .arg(QLocale(QLocale::Spanish).toString(QDateTime::fromSecsSinceEpoch(last),
                                                                    QStringLiteral("d MMM, HH:mm")))
                     : QStringLiteral("Aún no se ha comprobado."));
    }
    showSection(0);
}

void SettingsView::showSection(int index) {
    if (index < 0 || index >= m_sectionHeads.size())
        return;
    m_pinnedSection = index;
    m_scroll->verticalScrollBar()->setValue(index == 0 ? 0 : m_sectionHeads[index]->y() - 10);
    updateActiveNav(index);
}

bool SettingsView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_scroll->viewport() || watched == m_scroll->verticalScrollBar() || watched == m_scroll) {
        const QEvent::Type t = event->type();
        if (t == QEvent::Wheel || t == QEvent::MouseButtonPress) {
            m_pinnedSection = -1;
        } else if (t == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp
                || key == Qt::Key_PageDown || key == Qt::Key_Home || key == Qt::Key_End) {
                m_pinnedSection = -1;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SettingsView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QColor line = m_dark ? QColor(255, 255, 255, 18) : QColor(15, 23, 42, 18);
    p.fillRect(QRect(m_nav->width(), 0, 1, height()), line);
}

void SettingsView::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        emit closeRequested();
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace finily
