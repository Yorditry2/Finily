#include "ui/HomeScreen.h"

#include <QButtonGroup>
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
    // ("pdfSourcePath") para la pestaña 'Con PDF'.
    static const QByteArray strokesNeedle = "\"strokes\":[";
    static const QByteArray pdfNeedle = "\"pdfSourcePath\"";
    static const int carrySize = std::max(strokesNeedle.size(), pdfNeedle.size()) - 1;
    qint64 pages = 0;
    qint64 pdfPages = 0;
    QByteArray carry;
    while (!file.atEnd()) {
        const QByteArray window = carry + file.read(1 << 20);
        pages += window.count(strokesNeedle);
        pdfPages += window.count(pdfNeedle);
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

// Paleta del estudio editorial cálido: Warm Light Studio (por defecto) y su
// gemelo oscuro. Todo el cromo se deriva de aquí (pintado a mano + QSS
// explícito), inmune al estilo de sistema Caelestia/KDE.
struct HomePalette {
    QColor bg;
    QColor sidebar;
    QColor border;
    QColor text;
    QColor muted;
    QColor accent;
    QColor accentHover;
    QColor surface;
    QColor tint;
    QColor hover;
    QColor cardActionBg;
};

HomePalette paletteFor(bool dark) {
    if (dark) {
        return {QColor("#16171b"),
                QColor("#1e2027"),
                QColor(255, 255, 255, 26),
                QColor("#f3f4f6"),
                QColor("#9ca3af"),
                QColor("#f59e0b"),
                QColor("#fbbf24"),
                QColor("#262932"),
                QColor(245, 158, 11, 34),
                QColor(255, 255, 255, 18),
                QColor(15, 23, 42, 190)};
    }
    return {QColor("#f5f4f0"),
            QColor("#ffffff"),
            QColor("#e6e4dd"),
            QColor("#292524"),
            QColor("#8a8375"),
            QColor("#d97706"),
            QColor("#b45309"),
            QColor("#ffffff"),
            QColor(217, 119, 6, 26),
            QColor(41, 37, 36, 16),
            QColor(255, 255, 255, 235)};
}

} // namespace

// Insignia 'F' pintada a mano: cuadrado redondeado con degradado cálido.
class FinilyBadge : public QWidget {
public:
    explicit FinilyBadge(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(36, 36);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF frame = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        QLinearGradient gradient(frame.topLeft(), frame.bottomRight());
        gradient.setColorAt(0.0, QColor("#f59e0b"));
        gradient.setColorAt(1.0, QColor("#ea580c"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(frame, 11, 11);
        QFont font = painter.font();
        font.setPixelSize(19);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor(255, 255, 255, 238));
        painter.drawText(frame, Qt::AlignCenter, QStringLiteral("F"));
    }
};

// Alternancia Claro/Oscuro del pie de la sidebar: píldora pintada a mano con
// perilla deslizante (persistida en QSettings por HomeScreen).
class ThemeSwitch : public QWidget {
    Q_OBJECT

public:
    explicit ThemeSwitch(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(46, 24);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    void setDark(bool dark) {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }

    void setColors(const QColor& on, const QColor& off, const QColor& knob) {
        m_on = on;
        m_off = off;
        m_knob = knob;
        update();
    }

signals:
    void changed(bool dark);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF track = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_dark ? m_on : m_off);
        painter.drawRoundedRect(track, 11, 11);
        // Perilla + punto cálido (sol/luna simplificado) en el lado libre.
        const qreal cy = track.center().y();
        const qreal knobX = m_dark ? track.right() - 11.0 : track.left() + 11.0;
        const qreal glyphX = m_dark ? track.left() + 11.0 : track.right() - 11.0;
        painter.setBrush(m_knob);
        painter.drawEllipse(QPointF(knobX, cy), 8.0, 8.0);
        painter.setBrush(m_dark ? QColor("#fde68a") : QColor("#d97706"));
        painter.drawEllipse(QPointF(glyphX, cy), 3.0, 3.0);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            m_dark = !m_dark;
            update();
            emit changed(m_dark);
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    bool m_dark = false;
    QColor m_on{217, 119, 6};
    QColor m_off{0, 0, 0, 40};
    QColor m_knob{255, 255, 255};
};

// Tarjeta '+' punteada que abre el diálogo de nuevo cuaderno: primera carta
// de la estantería (cuando no hay búsqueda) para que la balda se sienta
// cálida y equilibrada, como en GoodNotes/StarNote.
class NewNotebookCard : public QWidget {
    Q_OBJECT

public:
    explicit NewNotebookCard(QWidget* parent = nullptr)
        : QWidget(parent) {
        setObjectName("finilyNewCard");
        setFixedSize(184, 244);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true); // repinta al entrar/salir el cursor
    }

signals:
    void createRequested();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF frame = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -14.0);

        painter.setPen(QPen(QColor("#d97706"), 1.6, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(underMouse() ? QColor(217, 119, 6, 30) : QColor(217, 119, 6, 14));
        painter.drawRoundedRect(frame, 12, 12);

        // Círculo punteado con '+' centrado.
        const QPointF center(frame.center().x(), frame.center().y() - 16.0);
        QPen dashed(QColor("#d97706"), 1.6, Qt::DashLine, Qt::RoundCap);
        painter.setPen(dashed);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, 24.0, 24.0);
        painter.setPen(QPen(QColor("#d97706"), 2.2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(center + QPointF(-8.0, 0.0), center + QPointF(8.0, 0.0));
        painter.drawLine(center + QPointF(0.0, -8.0), center + QPointF(0.0, 8.0));

        QFont font = painter.font();
        font.setPixelSize(13);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#292524"));
        painter.drawText(QRectF(frame.left(), center.y() + 34.0, frame.width(), 22.0),
                         Qt::AlignHCenter, QStringLiteral("Nuevo cuaderno"));
        QFont subFont = painter.font();
        subFont.setPixelSize(10);
        subFont.setBold(false);
        painter.setFont(subFont);
        painter.setPen(QColor("#8a8375"));
        painter.drawText(QRectF(frame.left(), center.y() + 56.0, frame.width(), 18.0),
                         Qt::AlignHCenter, QStringLiteral("Crea y empieza a escribir"));
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit createRequested();
        QWidget::mouseReleaseEvent(event);
    }
};

// Tarjeta de cuaderno pintada a mano: libro vertical 3:4 con sombra suave
// multicapa, lomo de encuadernación de 14 px con doblez, portada con el fondo
// y la plantilla REALES del cuaderno, banda de papel con el título en negrita,
// píldora de páginas, fecha relativa y acciones al pasar el cursor. Inmune a
// Caelestia/KDE.
class NotebookCard : public QWidget {
    Q_OBJECT

public:
    explicit NotebookCard(const NotebookMeta& meta, QWidget* parent = nullptr)
        : QWidget(parent), m_meta(meta) {
        setObjectName("finilyCard");
        setFixedSize(184, 244);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, false);
        setAutoFillBackground(false);

        // Acciones sutiles de esquina (visibles al pasar el cursor).
        m_renameBtn = new QToolButton(this);
        m_renameBtn->setObjectName("finilyCardAction");
        m_renameBtn->setText(QStringLiteral("✎"));
        m_renameBtn->setToolTip("Renombrar");
        m_renameBtn->setFixedSize(24, 22);
        m_renameBtn->setCursor(Qt::PointingHandCursor);
        m_renameBtn->setFocusPolicy(Qt::NoFocus);
        connect(m_renameBtn, &QToolButton::clicked, this, &NotebookCard::renameRequested);
        m_renameBtn->move(184 - 58, 12);
        m_renameBtn->hide();

        m_deleteBtn = new QToolButton(this);
        m_deleteBtn->setObjectName("finilyCardAction");
        m_deleteBtn->setText(QStringLiteral("✕"));
        m_deleteBtn->setToolTip("Eliminar");
        m_deleteBtn->setFixedSize(24, 22);
        m_deleteBtn->setCursor(Qt::PointingHandCursor);
        m_deleteBtn->setFocusPolicy(Qt::NoFocus);
        connect(m_deleteBtn, &QToolButton::clicked, this, &NotebookCard::deleteRequested);
        m_deleteBtn->move(184 - 30, 12);
        m_deleteBtn->hide();
    }

    QString filePath() const { return m_meta.path; }
    QString title() const { return m_meta.title; }

    // Búsqueda por título (vacía = todo coincide).
    bool matchesSearch(const QString& needle) const {
        return needle.isEmpty() || m_meta.title.contains(needle, Qt::CaseInsensitive);
    }

    // Filtros de pestaña: 0 = Todos, 1 = Recientes (7 días), 2 = Con PDF.
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

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Libro con elevación sutil al pasar el cursor (se alza 3 px).
        const QRectF book =
            QRectF(rect()).adjusted(1.0, m_hovered ? 5.0 : 8.0, -1.0, m_hovered ? -11.0 : -14.0);

        // Sombra suave multicapa (3 capas cálidas de profundidad creciente).
        painter.setPen(Qt::NoPen);
        QColor shadow(64, 54, 42, 0);
        shadow.setAlpha(m_hovered ? 30 : 20);
        painter.setBrush(shadow);
        painter.drawRoundedRect(book.translated(0, 7).adjusted(-3, 0, 3, 2), 16, 16);
        shadow.setAlpha(m_hovered ? 48 : 34);
        painter.setBrush(shadow);
        painter.drawRoundedRect(book.translated(0, 4), 14, 14);
        shadow.setAlpha(m_hovered ? 62 : 44);
        painter.setBrush(shadow);
        painter.drawRoundedRect(book.translated(0, 2), 12, 12);

        const ThemeConfig theme = getThemeConfig(m_meta.theme);
        painter.save();
        QPainterPath clip;
        clip.addRoundedRect(book, 9, 9);
        painter.setClipPath(clip);

        // Portada con el fondo REAL del cuaderno.
        painter.fillRect(book, theme.background);

        // Lomo de encuadernación (14 px) más oscuro + doblez con brillo.
        const QRectF spine(book.left(), book.top(), 14.0, book.height());
        painter.fillRect(spine, QColor(0, 0, 0, 46));
        painter.fillRect(QRectF(spine.right() - 1.2, book.top(), 1.2, book.height()),
                         QColor(255, 255, 255, 88));
        painter.fillRect(QRectF(spine.right() + 0.8, book.top(), 0.8, book.height()),
                         QColor(0, 0, 0, 26));

        // Mini vista previa de la plantilla real (cuadrícula/puntos/rayado).
        const QRectF preview(spine.right() + 10.0, book.top() + 14.0,
                             book.width() - spine.width() - 20.0, 96.0);
        paintTemplatePattern(painter, preview, theme, m_meta.tpl);

        // Banda decorativa de papel con el título en negrita + fecha relativa.
        const QRectF band(spine.right() + 8.0, book.top() + 122.0, book.width() - spine.width() - 16.0,
                          54.0);
        painter.setPen(QPen(QColor(41, 37, 36, 30), 1.0));
        painter.setBrush(QColor(255, 253, 248, 234));
        painter.drawRoundedRect(band, 8, 8);
        QFont titleFont = painter.font();
        titleFont.setPixelSize(12);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.setPen(QColor("#292524"));
        painter.drawText(QRectF(band.left() + 8, band.top() + 7, band.width() - 16, 20),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(titleFont).elidedText(m_meta.title, Qt::ElideRight,
                                                            int(band.width() - 16)));
        QFont dateFont = painter.font();
        dateFont.setPixelSize(10);
        dateFont.setBold(false);
        painter.setFont(dateFont);
        painter.setPen(QColor("#8a8375"));
        painter.drawText(QRectF(band.left() + 8, band.top() + 29, band.width() - 16, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, relativeDate(m_meta.modified));

        // Píldora de páginas sobre la portada.
        const QString pagesText = m_meta.pages == 1 ? QStringLiteral("1 pág.")
                                                    : QStringLiteral("%1 págs.").arg(m_meta.pages);
        QFont badgeFont = painter.font();
        badgeFont.setPixelSize(10);
        badgeFont.setBold(true);
        painter.setFont(badgeFont);
        const qreal badgeWidth = QFontMetrics(badgeFont).horizontalAdvance(pagesText) + 14.0;
        const QRectF pill(spine.right() + 8.0, book.bottom() - 26.0, badgeWidth, 18.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(41, 37, 36, 150));
        painter.drawRoundedRect(pill, 9, 9);
        painter.setPen(QColor(255, 253, 248, 232));
        painter.drawText(pill, Qt::AlignCenter, pagesText);
        painter.restore();

        // Borde nítido del libro (acento cálido al pasar el cursor).
        painter.setPen(QPen(m_hovered ? QColor("#d97706") : QColor(41, 37, 36, 48),
                            m_hovered ? 1.6 : 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(book, 9, 9);
    }

    void enterEvent(QEnterEvent*) override {
        m_hovered = true;
        if (m_renameBtn)
            m_renameBtn->show();
        if (m_deleteBtn)
            m_deleteBtn->show();
        update();
    }

    void leaveEvent(QEvent*) override {
        m_hovered = false;
        if (m_renameBtn)
            m_renameBtn->hide();
        if (m_deleteBtn)
            m_deleteBtn->hide();
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit openRequested(m_meta.path);
        QWidget::mouseReleaseEvent(event);
    }

private:
    NotebookMeta m_meta;
    bool m_hovered = false;
    QToolButton* m_renameBtn = nullptr;
    QToolButton* m_deleteBtn = nullptr;
};

HomeScreen::HomeScreen(QWidget* parent)
    : QWidget(parent) {
    setObjectName("finilyHome");
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    // Modo del estudio (Warm Light Studio por defecto), persistido.
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

void HomeScreen::buildSidebar() {
    m_sidebar = new QWidget(this);
    m_sidebar->setAttribute(Qt::WA_StyledBackground, false);
    m_sidebar->setAutoFillBackground(false);
    m_sidebar->setFixedWidth(232);

    auto* column = new QVBoxLayout(m_sidebar);
    column->setContentsMargins(18, 20, 18, 16);
    column->setSpacing(10);

    // Marca: insignia 'F' + 'Finily' + 'Tus cuadernos'.
    auto* brandRow = new QHBoxLayout();
    brandRow->setSpacing(10);
    m_badge = new FinilyBadge(m_sidebar);
    brandRow->addWidget(m_badge, 0, Qt::AlignVCenter);
    auto* brandText = new QWidget(m_sidebar);
    brandText->setAttribute(Qt::WA_StyledBackground, false);
    auto* brandBox = new QVBoxLayout(brandText);
    brandBox->setContentsMargins(0, 0, 0, 0);
    brandBox->setSpacing(1);
    m_titleLabel = new QLabel(QStringLiteral("Finily"), brandText);
    m_titleLabel->setObjectName("finilyHomeTitle");
    m_subtitleLabel = new QLabel(QStringLiteral("Tus cuadernos"), brandText);
    m_subtitleLabel->setObjectName("finilyHomeSub");
    brandBox->addWidget(m_titleLabel);
    brandBox->addWidget(m_subtitleLabel);
    brandRow->addWidget(brandText, 1, Qt::AlignVCenter);
    column->addLayout(brandRow);
    column->addSpacing(8);

    // Botón primario de acento.
    m_newBtn = new QToolButton(m_sidebar);
    m_newBtn->setObjectName("finilyPrimary");
    m_newBtn->setText(QStringLiteral("+ Nuevo cuaderno"));
    m_newBtn->setToolTip("Crear un cuaderno nuevo en la biblioteca");
    m_newBtn->setCursor(Qt::PointingHandCursor);
    m_newBtn->setFocusPolicy(Qt::NoFocus);
    m_newBtn->setFixedHeight(40);
    connect(m_newBtn, &QToolButton::clicked, this, &HomeScreen::openCreateDialog);
    column->addWidget(m_newBtn);
    column->addSpacing(4);

    // Pestañas de filtro.
    auto* tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);
    const QStringList tabNames = {QStringLiteral("Todos los cuadernos"), QStringLiteral("Recientes"),
                                  QStringLiteral("Con PDF")};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* tab = new QToolButton(m_sidebar);
        tab->setObjectName("finilyFilter");
        tab->setText(tabNames.at(i));
        tab->setCheckable(true);
        tab->setChecked(i == 0);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFocusPolicy(Qt::NoFocus);
        tabGroup->addButton(tab, i);
        connect(tab, &QToolButton::clicked, this, [this, i] { selectFilter(i); });
        m_filterTabs.append(tab);
        column->addWidget(tab);
    }

    // Divisor sutil + acciones rápidas.
    auto* divider = new QWidget(m_sidebar);
    divider->setObjectName("finilySideDivider");
    divider->setFixedHeight(1);
    column->addWidget(divider);
    column->addSpacing(4);

    m_importBtn = new QToolButton(m_sidebar);
    m_importBtn->setObjectName("finilyQuick");
    m_importBtn->setText(QStringLiteral("Importar PDF"));
    m_importBtn->setToolTip("Convertir un PDF en cuaderno");
    m_importBtn->setCursor(Qt::PointingHandCursor);
    m_importBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_importBtn, &QToolButton::clicked, this, &HomeScreen::importPdfFromHomeRequested);
    column->addWidget(m_importBtn);

    m_openBtn = new QToolButton(m_sidebar);
    m_openBtn->setObjectName("finilyQuick");
    m_openBtn->setText(QStringLiteral("Abrir archivo..."));
    m_openBtn->setToolTip("Abrir un cuaderno .finily desde el disco");
    m_openBtn->setCursor(Qt::PointingHandCursor);
    m_openBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_openBtn, &QToolButton::clicked, this, &HomeScreen::openExternalFileRequested);
    column->addWidget(m_openBtn);

    m_draftBtn = new QToolButton(m_sidebar);
    m_draftBtn->setObjectName("finilyQuick");
    m_draftBtn->setText(QStringLiteral("Continuar borrador"));
    m_draftBtn->setToolTip("Restaurar la sesión autoguardada");
    m_draftBtn->setCursor(Qt::PointingHandCursor);
    m_draftBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_draftBtn, &QToolButton::clicked, this, &HomeScreen::continueDraftRequested);
    column->addWidget(m_draftBtn);

    column->addStretch(1);

    // Pie: alternancia Claro/Oscuro persistida.
    auto* footerDivider = new QWidget(m_sidebar);
    footerDivider->setObjectName("finilySideDivider");
    footerDivider->setFixedHeight(1);
    column->addWidget(footerDivider);
    column->addSpacing(6);

    auto* themeRow = new QHBoxLayout();
    themeRow->setSpacing(10);
    m_themeSwitch = new ThemeSwitch(m_sidebar);
    m_themeSwitch->setDark(m_dark);
    connect(m_themeSwitch, &ThemeSwitch::changed, this, &HomeScreen::setDarkMode);
    themeRow->addWidget(m_themeSwitch, 0, Qt::AlignVCenter);
    m_themeLabel = new QLabel(m_dark ? QStringLiteral("Modo oscuro") : QStringLiteral("Modo claro"),
                              m_sidebar);
    m_themeLabel->setObjectName("finilyThemeLabel");
    themeRow->addWidget(m_themeLabel, 1, Qt::AlignVCenter);
    column->addLayout(themeRow);
}

void HomeScreen::buildGallery() {
    auto* gallery = new QWidget(this);
    gallery->setAttribute(Qt::WA_StyledBackground, false);
    gallery->setAutoFillBackground(false);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_sidebar, 0);
    root->addWidget(gallery, 1);

    auto* galleryColumn = new QVBoxLayout(gallery);
    galleryColumn->setContentsMargins(26, 20, 26, 14);
    galleryColumn->setSpacing(14);

    // Barra superior: título de sección + píldora de conteo + búsqueda.
    auto* topBar = new QWidget(gallery);
    topBar->setAttribute(Qt::WA_StyledBackground, false);
    auto* topRow = new QHBoxLayout(topBar);
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(10);

    m_sectionTitle = new QLabel(QStringLiteral("Todos los cuadernos"), topBar);
    m_sectionTitle->setObjectName("finilySectionTitle");
    topRow->addWidget(m_sectionTitle, 0, Qt::AlignVCenter);

    m_countPill = new QLabel(QStringLiteral("0"), topBar);
    m_countPill->setObjectName("finilyCountPill");
    topRow->addWidget(m_countPill, 0, Qt::AlignVCenter);

    topRow->addStretch(1);

    m_search = new QLineEdit(topBar);
    m_search->setObjectName("finilySearch");
    m_search->setPlaceholderText(QStringLiteral("Buscar cuadernos..."));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedSize(240, 36);
    connect(m_search, &QLineEdit::textChanged, this, &HomeScreen::applySearchFilter);
    topRow->addWidget(m_search, 0, Qt::AlignVCenter);

    galleryColumn->addWidget(topBar, 0);

    // Rejilla de tarjetas (scroll vertical).
    m_scroll = new QScrollArea(gallery);
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

    galleryColumn->addWidget(m_scroll, 1);

    // Primera carta de la estantería: siempre '+' Nuevo cuaderno (sin búsqueda).
    m_newCard = new NewNotebookCard(m_gridContent);
    connect(m_newCard, &NewNotebookCard::createRequested, this, &HomeScreen::openCreateDialog);
    m_newCard->show();

    // Estado vacío: SOLO cuando hay búsqueda escrita y cero coincidencias.
    m_emptyState = new QWidget(m_gridContent);
    m_emptyState->setAttribute(Qt::WA_StyledBackground, false);
    auto* emptyBox = new QVBoxLayout(m_emptyState);
    emptyBox->setContentsMargins(30, 12, 30, 12);
    m_emptyLabel = new QLabel(m_emptyState);
    m_emptyLabel->setObjectName("finilyEmpty");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    emptyBox->addStretch(1);
    emptyBox->addWidget(m_emptyLabel);
    emptyBox->addStretch(1);
    m_emptyState->hide();
}

void HomeScreen::applyTheme() {
    const HomePalette pal = paletteFor(m_dark);

    // QSS explícito para TODO el texto y los controles: Caelestia/KDE jamás
    // puede inyectar colores o fondos aquí.
    setStyleSheet(QStringLiteral(R"(
QLabel { color: %1; background: transparent; }
QLabel#finilyHomeTitle { color: %2; font-size: 20px; font-weight: 800; }
QLabel#finilyHomeSub { color: %1; font-size: 11px; }
QLabel#finilySectionTitle { color: %2; font-size: 18px; font-weight: 800; }
QLabel#finilyCountPill {
    color: %3; background: %4; border-radius: 10px;
    padding: 2px 10px; font-size: 11px; font-weight: 700;
}
QLabel#finilyThemeLabel { color: %1; font-size: 11px; font-weight: 600; }
QLabel#finilyEmpty { color: %1; font-size: 13px; }
QWidget#finilySideDivider { background: %5; }
QLineEdit#finilySearch {
    background: %6; border: 1px solid %5; border-radius: 12px;
    color: %2; padding: 7px 12px; font-size: 12px;
    selection-background-color: %3; selection-color: #ffffff;
}
QLineEdit#finilySearch:focus { border: 1px solid %3; }
QToolButton {
    background: transparent; border: none; border-radius: 10px;
    color: %2; padding: 7px 10px; font-size: 12px; text-align: left;
}
QToolButton:hover { background: %7; }
QToolButton#finilyPrimary {
    background: %3; color: #ffffff; font-weight: 700;
    border-radius: 12px; text-align: center;
}
QToolButton#finilyPrimary:hover { background: %8; }
QToolButton#finilyFilter { color: %1; font-weight: 600; border-radius: 9px; }
QToolButton#finilyFilter:checked { background: %4; color: %3; }
QToolButton#finilyCardAction {
    background: %9; border: 1px solid %5; border-radius: 7px;
    color: %2; padding: 0px; font-size: 11px; text-align: center;
}
QToolButton#finilyCardAction:hover { border: 1px solid %3; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px 4px 0px; }
QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: %7; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)")
                         .arg(pal.muted.name(QColor::HexArgb))
                         .arg(pal.text.name(QColor::HexArgb))
                         .arg(pal.accent.name(QColor::HexArgb))
                         .arg(pal.tint.name(QColor::HexArgb))
                         .arg(pal.border.name(QColor::HexArgb))
                         .arg(pal.surface.name(QColor::HexArgb))
                         .arg(pal.hover.name(QColor::HexArgb))
                         .arg(pal.accentHover.name(QColor::HexArgb))
                         .arg(pal.cardActionBg.name(QColor::HexArgb)));

    if (m_themeSwitch) {
        m_themeSwitch->setColors(pal.accent, QColor(0, 0, 0, m_dark ? 70 : 40), QColor("#ffffff"));
        m_themeSwitch->setDark(m_dark);
    }
    if (m_themeLabel)
        m_themeLabel->setText(m_dark ? QStringLiteral("Modo oscuro") : QStringLiteral("Modo claro"));
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
    // Fondo pintado a mano: estudio cálido + glow suave de acento. La sidebar
    // blanca y su borde derecho también van pintados (nada de QSS de fondo).
    const HomePalette pal = paletteFor(m_dark);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), pal.bg);

    QRadialGradient glow(QPointF(width() * 0.92, -height() * 0.12),
                         std::max(width(), height()) * 0.85);
    QColor glowColor = pal.accent;
    glowColor.setAlpha(m_dark ? 34 : 44);
    glow.setColorAt(0.0, glowColor);
    QColor fade = glowColor;
    fade.setAlpha(0);
    glow.setColorAt(1.0, fade);
    painter.fillRect(rect(), glow);

    // Sidebar blanca fija con borde derecho cálido de 1px.
    painter.fillRect(QRectF(0, 0, 232, height()), pal.sidebar);
    painter.fillRect(QRectF(232, 0, 1, height()), pal.border);
}

void HomeScreen::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // La QScrollArea reajusta el contenido tras el resize: recolocar después.
    QTimer::singleShot(0, this, &HomeScreen::relayoutGrid);
}

void HomeScreen::refreshLibrary() {
    for (NotebookCard* card : m_cards) {
        if (card)
            card->deleteLater();
    }
    m_cards.clear();

    const QDir dir(libraryDir());
    const QFileInfoList entries =
        dir.entryInfoList({QStringLiteral("*.finily")}, QDir::Files, QDir::Time);
    for (const QFileInfo& info : entries) {
        const NotebookMeta meta = readNotebookMeta(info);
        auto* card = new NotebookCard(meta, m_gridContent);
        connect(card, &NotebookCard::openRequested, this, &HomeScreen::openNotebookRequested);
        connect(card, &NotebookCard::renameRequested, this, [this, card] { renameNotebook(card); });
        connect(card, &NotebookCard::deleteRequested, this, [this, card] { deleteNotebook(card); });
        card->show();
        m_cards.append(card);
    }

    // 'Continuar borrador' solo si existe un autoguardado en disco (la ruta
    // coincide con la de CanvasWidget::autosavePath()).
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

void HomeScreen::applySearchFilter(const QString& text) {
    const QString needle = text.trimmed();
    for (NotebookCard* card : m_cards) {
        if (!card)
            continue;
        card->setVisible(card->matchesSearch(needle) && card->matchesFilter(int(m_filter)));
    }
    if (m_newCard)
        m_newCard->setVisible(needle.isEmpty());
    updateEmptyState();
}

void HomeScreen::updateEmptyState() {
    // NUNCA mostrar el estado vacío con cuadernos visibles o búsqueda vacía:
    // solo el mensaje limpio de búsqueda sin coincidencias.
    int visible = 0;
    for (const NotebookCard* card : m_cards)
        if (card && card->isVisible())
            ++visible;
    const bool showEmpty = !m_cards.isEmpty() && visible == 0 &&
                           !m_search->text().trimmed().isEmpty();
    if (m_emptyState) {
        m_emptyState->setVisible(showEmpty);
        if (showEmpty && m_emptyLabel)
            m_emptyLabel->setText(QStringLiteral("No se encontraron cuadernos"));
    }
    updateSectionHeader();
    relayoutGrid();
}

void HomeScreen::updateSectionHeader() {
    static const QStringList titles = {QStringLiteral("Todos los cuadernos"),
                                       QStringLiteral("Recientes"), QStringLiteral("Con PDF")};
    if (m_sectionTitle)
        m_sectionTitle->setText(titles[int(m_filter)]);
    if (m_countPill) {
        int visible = 0;
        for (const NotebookCard* card : m_cards)
            if (card && card->isVisible())
                ++visible;
        m_countPill->setText(QString::number(visible));
        m_countPill->setVisible(visible > 0);
    }
}

void HomeScreen::relayoutGrid() {
    if (!m_gridContent || !m_scroll)
        return;
    constexpr int spacing = 22;
    constexpr int cardWidth = 184;
    constexpr int cardHeight = 244;
    constexpr int sideMargin = 26;

    const int contentWidth = m_scroll->viewport()->width();
    const int usable = std::max(0, contentWidth - sideMargin * 2);
    const int columns = std::max(1, (usable + spacing) / (cardWidth + spacing));
    const int gridWidth = columns * (cardWidth + spacing) - spacing;
    const int offsetX = std::max(sideMargin, (contentWidth - gridWidth) / 2);

    // La carta '+' siempre encabeza la estantería (cuando no hay búsqueda).
    QList<QWidget*> visibleCards;
    if (m_newCard && m_newCard->isVisible())
        visibleCards.append(m_newCard);
    for (NotebookCard* card : m_cards)
        if (card && card->isVisible())
            visibleCards.append(card);

    int index = 0;
    for (QWidget* card : visibleCards) {
        const int row = index / columns;
        const int col = index % columns;
        card->setGeometry(offsetX + col * (cardWidth + spacing),
                          sideMargin + row * (cardHeight + spacing), cardWidth, cardHeight);
        ++index;
    }
    const int rows = std::max(1, (index + columns - 1) / columns);
    m_gridContent->setMinimumHeight(sideMargin * 2 + rows * (cardHeight + spacing) - spacing);

    if (m_emptyState && m_emptyState->isVisible())
        m_emptyState->setGeometry(0, 40, std::max(0, contentWidth), 200);
}

void HomeScreen::openCreateDialog() {
    const HomePalette pal = paletteFor(m_dark);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Nuevo cuaderno"));
    dialog.setModal(true);
    dialog.setMinimumWidth(420);
    dialog.setStyleSheet(
        QStringLiteral(R"(
QDialog { background: %1; }
QLabel { color: %2; background: transparent; font-size: 12px; }
QLabel#finilyDialogHeading { color: %3; font-size: 15px; font-weight: 700; }
QLineEdit {
    background: %4; border: 1px solid %5; border-radius: 10px;
    color: %3; padding: 7px 10px; font-size: 12px;
}
QLineEdit:focus { border: 1px solid %6; }
QToolButton {
    background: %4; border: 1px solid %5; border-radius: 10px;
    color: %3; padding: 6px 10px; font-size: 12px;
}
QToolButton:hover { border: 1px solid %6; }
QToolButton:checked {
    background: %7; border: 1px solid %6; color: %3; font-weight: 600;
}
QPushButton {
    background: %4; border: 1px solid %5; border-radius: 10px;
    color: %3; padding: 7px 16px; font-size: 12px;
}
QPushButton:hover { border: 1px solid %6; }
QPushButton:default { background: %6; border: 1px solid %6; color: #ffffff; font-weight: 600; }
})")
            .arg(pal.bg.name(QColor::HexArgb))
            .arg(pal.muted.name(QColor::HexArgb))
            .arg(pal.text.name(QColor::HexArgb))
            .arg(pal.surface.name(QColor::HexArgb))
            .arg(pal.border.name(QColor::HexArgb))
            .arg(pal.accent.name(QColor::HexArgb))
            .arg(pal.tint.name(QColor::HexArgb)));

    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(18, 16, 18, 14);
    column->setSpacing(10);

    auto* heading = new QLabel(QStringLiteral("Crear un cuaderno nuevo"), &dialog);
    heading->setObjectName("finilyDialogHeading");
    column->addWidget(heading);

    column->addWidget(new QLabel(QStringLiteral("Nombre"), &dialog));
    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setText(QStringLiteral("Cuaderno sin título"));
    nameEdit->selectAll();
    column->addWidget(nameEdit);

    // Formato de hoja: Infinita / Hoja A4 / Diapo 16:9.
    column->addWidget(new QLabel(QStringLiteral("Formato"), &dialog));
    auto* formatRow = new QHBoxLayout();
    formatRow->setSpacing(6);
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
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        formatGroup->addButton(button, i);
        formatRow->addWidget(button);
    }
    column->addLayout(formatRow);

    column->addWidget(new QLabel(QStringLiteral("Tema"), &dialog));
    auto* themeRow = new QGridLayout();
    themeRow->setHorizontalSpacing(6);
    themeRow->setVerticalSpacing(6);
    auto* themeGroup = new QButtonGroup(&dialog);
    themeGroup->setExclusive(true);
    const QVector<ThemeId> themes = {ThemeId::DarkBoard, ThemeId::GreenBoard, ThemeId::PaperLight,
                                     ThemeId::PaperCream};
    for (int i = 0; i < themes.size(); ++i) {
        auto* button = new QToolButton(&dialog);
        button->setText(getThemeConfig(themes.at(i)).name);
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        themeGroup->addButton(button, i);
        themeRow->addWidget(button, i / 2, i % 2);
    }
    column->addLayout(themeRow);

    column->addWidget(new QLabel(QStringLiteral("Plantilla"), &dialog));
    auto* templateRow = new QHBoxLayout();
    templateRow->setSpacing(6);
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
        button->setCheckable(true);
        button->setChecked(templates.at(i).second == BackgroundTemplate::Dots);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        templateGroup->addButton(button, i);
        templateRow->addWidget(button);
    }
    column->addLayout(templateRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Crear"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    column->addWidget(buttons);

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
        refreshLibrary();
    }
}

// AUTOMOC: las clases Q_OBJECT definidas en este .cpp (ThemeSwitch,
// NewNotebookCard, NotebookCard) viven en finily — el código moc generado
// debe compilarse dentro de este mismo namespace.
#include "HomeScreen.moc"

} // namespace finily
