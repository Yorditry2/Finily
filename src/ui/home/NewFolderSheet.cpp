#include "ui/home/NewFolderSheet.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/SheetWidgets.h"

#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {

// Botón «Dentro de»: ícono de carpeta + ruta con migas + chevron (mockup .loc).
class LocationButton : public QWidget {
public:
    explicit LocationButton(QWidget* parent)
        : QWidget(parent) {
        setFixedHeight(34);
        setCursor(Qt::PointingHandCursor);
    }
    void setPath(const QStringList& parts) { m_parts = parts; update(); }
    void setDark(bool d) { m_dark = d; update(); }
    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(m_dark);
        p.setPen(QPen(m_dark ? QColor(255, 255, 255, 18) : QColor(15, 23, 42, 15), 1));
        p.setBrush(tk.field);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        paintUiIcon(&p, QRectF(10, 10, 14, 14), QStringLiteral("folder"), tk.muted);
        QFont f = font();
        f.setPixelSize(12);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        const QFontMetrics fm(f);
        qreal x = 32;
        for (int i = 0; i < m_parts.size(); ++i) {
            const bool last = i == m_parts.size() - 1;
            p.setPen(last ? tk.text : tk.muted);
            const int w = fm.horizontalAdvance(m_parts[i]);
            p.drawText(QRectF(x, 0, w + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, m_parts[i]);
            x += w + 4;
            if (!last) {
                paintUiIcon(&p, QRectF(x, 11.5, 11, 11), QStringLiteral("chevR"), tk.faint);
                x += 15;
            }
        }
        paintUiIcon(&p, QRectF(width() - 24, 10, 14, 14), QStringLiteral("chevD"), tk.muted);
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        if (onClick)
            onClick();
    }

private:
    QStringList m_parts;
    bool m_dark = false;
};

NewFolderSheet::NewFolderSheet(QWidget* parent)
    : GlassSheet(parent) {
    setSideCard(true);
    setBodyMargins(0, 0, 0, 0);
    bodyLayout()->setSpacing(0);
    bodyLayout()->setContentsMargins(0, 0, 0, 0);

    m_hero = new SheetHero(QStringLiteral("Carpeta"), QStringLiteral("folder"), this, 196);
    connect(m_hero, &SheetHero::closeClicked, this, [this] { closeSheet(false); });
    connect(m_hero->titleEdit(), &QLineEdit::returnPressed, this, [this] { onConfirmed(); });
    bodyLayout()->addWidget(m_hero);

    auto* body = new QWidget(this);
    body->setAttribute(Qt::WA_StyledBackground, false);
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(16, 15, 16, 16);
    col->setSpacing(16);
    bodyLayout()->addWidget(body);

    auto section = [&](const QString& title, QWidget* content) {
        auto* box = new QWidget(body);
        auto* v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(9);
        v->addWidget(makeKicker(title, box, false));
        v->addWidget(content);
        col->addWidget(box);
        return box;
    };

    m_dots = new DotRow(body);
    connect(m_dots, &DotRow::picked, this, [this](int i) { m_colorIndex = i; refreshState(); });
    section(QStringLiteral("Color"), m_dots);

    m_icons = new IconGrid(body);
    connect(m_icons, &IconGrid::picked, this, [this](int i) { m_iconIndex = i; refreshState(); });
    section(QStringLiteral("Icono"), m_icons);

    m_locBtn = new LocationButton(body);
    m_locSection = section(QStringLiteral("Dentro de"), m_locBtn);

    m_cta = makeCtaButton(QStringLiteral("Crear carpeta"), body, false);
    connect(m_cta, &QPushButton::clicked, this, [this] { onConfirmed(); });
    col->addWidget(m_cta);

    // El menú de ubicación es hermano de la tarjeta (hijo de HomeScreen), a su izquierda.
    m_locPop = new LocationPopover(parent);
    connect(m_locPop, &LocationPopover::picked, this, [this](const QString& rel) {
        m_parentRelPath = rel;
        refreshState();
    });
    m_locBtn->onClick = [this] {
        if (m_locPop->isVisible()) {
            m_locPop->hide();
            return;
        }
        QVector<LocationEntry> entries;
        entries.append({QString(), QStringLiteral("Carpetas"), 0, 0});
        for (const FolderMeta& f : m_folders)
            entries.append({f.relativePath, f.name, f.depth + 1, f.colorIndex});
        m_locPop->setEntries(entries);
        m_locPop->setCurrent(m_parentRelPath);
        const QPoint tl = m_locBtn->mapTo(parentWidget(), QPoint(0, 0));
        m_locPop->popup(QRect(tl.x() - 16, tl.y(),
                              tokens::space::FloatCardWidth, m_locBtn->height()));
    };
}

void NewFolderSheet::setFolders(const QVector<FolderMeta>& folders) {
    m_folders = folders;
}

QString NewFolderSheet::pathLabel(const QString& rel) const {
    QStringList parts{QStringLiteral("Carpetas")};
    if (!rel.isEmpty())
        parts += rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.join(QStringLiteral(" › "));
}

void NewFolderSheet::refreshState() {
    m_hero->setFolder(m_colorIndex, m_iconIndex);
    m_dots->setCurrent(m_colorIndex);
    m_icons->setCurrent(m_iconIndex);
    QStringList parts{QStringLiteral("Carpetas")};
    if (!m_parentRelPath.isEmpty())
        parts += m_parentRelPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    m_locBtn->setPath(parts);
}

void NewFolderSheet::hideEvent(QHideEvent* event) {
    m_locPop->hide();
    GlassSheet::hideEvent(event);
}

void NewFolderSheet::openCreate(const QString& parentRelativePath, const HomePalette& pal, bool dark) {
    m_editMode = false;
    m_parentRelPath = parentRelativePath;
    m_editRelPath.clear();
    m_colorIndex = 2; // Salvia, como en el mockup
    m_iconIndex = 1;
    m_hero->setDark(dark);
    m_dots->setDark(dark);
    m_icons->setDark(dark);
    m_locBtn->setDark(dark);
    m_locPop->setDark(dark);
    m_locSection->setVisible(true);
    m_cta->setText(QStringLiteral("Crear carpeta"));
    m_hero->titleEdit()->setText(QStringLiteral("Nueva carpeta"));
    openSheet(pal, dark);
    refreshState();
    m_hero->titleEdit()->setFocus();
    m_hero->titleEdit()->selectAll();
}

void NewFolderSheet::openEdit(const FolderMeta& folder, const HomePalette& pal, bool dark) {
    m_editMode = true;
    m_editRelPath = folder.relativePath;
    m_parentRelPath.clear();
    m_colorIndex = std::clamp(folder.colorIndex, 0, LibraryStore::kCoverColorCount - 1);
    m_iconIndex = std::clamp(folder.iconIndex, 0, LibraryStore::kIconCount - 1);
    m_hero->setDark(dark);
    m_dots->setDark(dark);
    m_icons->setDark(dark);
    m_locBtn->setDark(dark);
    m_locPop->setDark(dark);
    m_locSection->setVisible(false);
    m_cta->setText(QStringLiteral("Guardar cambios"));
    m_hero->titleEdit()->setText(folder.name);
    openSheet(pal, dark);
    refreshState();
    m_hero->titleEdit()->setFocus();
    m_hero->titleEdit()->selectAll();
}

void NewFolderSheet::onConfirmed() {
    const QString clean = LibraryStore::sanitizeBaseName(m_hero->titleEdit()->text());
    const QString finalName = clean.isEmpty() ? QStringLiteral("Nueva carpeta") : clean;
    closeSheet(true);
    if (m_editMode)
        emit editSubmitted(m_editRelPath, finalName, m_colorIndex, m_iconIndex);
    else
        emit createSubmitted(m_parentRelPath, finalName, m_colorIndex, m_iconIndex);
}

} // namespace finily
