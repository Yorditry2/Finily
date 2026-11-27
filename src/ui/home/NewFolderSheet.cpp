#include "ui/home/NewFolderSheet.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/LibraryObjects.h"
#include "ui/home/SheetWidgets.h"

#include <QApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace finily {
namespace {

constexpr int kModalW = 680, kModalH = 410, kLeftW = 268;

} // namespace

// Vista previa limpia de carpeta en la columna izquierda del modal.
class FolderPreview : public QWidget {
public:
    explicit FolderPreview(QWidget* parent) : QWidget(parent) { setFixedHeight(196); }
    void setLook(int colorIndex, int iconIndex, bool dark) {
        m_colorIndex = colorIndex;
        m_iconIndex = iconIndex;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(m_dark);
        p.setPen(Qt::NoPen);
        p.setBrush(tk.neutral);
        p.drawRoundedRect(QRectF(rect()), 14, 14);

        const QRectF nat = libobj::folderRect(QPointF(0, 0));
        const qreal scale = 184.0 / std::max<qreal>(1.0, nat.width());
        const QPointF bc(width() / 2.0, height() / 2.0 + nat.height() * scale * 0.5);
        p.save();
        p.translate(bc);
        p.scale(scale, scale);
        libobj::paintFolder(p, QPointF(0, 0), m_colorIndex, m_iconIndex, 0.0, m_dark);
        p.restore();
    }

private:
    int m_colorIndex = 2;
    int m_iconIndex = 1;
    bool m_dark = false;
};

// Botón «Dentro de»: ícono de carpeta + ruta con migas + chevron.
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
        p.setPen(QPen(m_dark ? QColor(255, 255, 255, 22) : QColor(15, 23, 42, 15), 1));
        p.setBrush(tk.field);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        paintUiIcon(&p, QRectF(10, 10, 14, 14), QStringLiteral("folder"), tk.muted);
        QFont f = QApplication::font();
        f.setPixelSize(12);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        const QFontMetrics fm(f);
        p.save();
        p.setClipRect(QRectF(30, 0, std::max(0, width() - 60), height()));
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
        p.restore();
        paintUiIcon(&p, QRectF(width() - 24, 10, 14, 14), QStringLiteral("chevD"), tk.muted);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && onClick)
            onClick();
    }

private:
    QStringList m_parts;
    bool m_dark = false;
};

NewFolderSheet::NewFolderSheet(QWidget* parent)
    : GlassSheet(parent) {
    setSideCard(false);
    setCardWidth(kModalW);
    setHeaderVisible(false);
    setBodyMargins(0, 0, 0, 0);
    bodyLayout()->setSpacing(0);

    auto* shell = new QWidget(this);
    shell->setAttribute(Qt::WA_StyledBackground, false);
    shell->setAutoFillBackground(false);
    shell->setFixedSize(kModalW, kModalH);
    auto* row = new QHBoxLayout(shell);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    bodyLayout()->addWidget(shell);

    // ---- columna izquierda
    m_left = new QWidget(shell);
    m_left->setAttribute(Qt::WA_StyledBackground, false);
    m_left->setAutoFillBackground(false);
    m_left->setFixedWidth(kLeftW);
    auto* lc = new QVBoxLayout(m_left);
    lc->setContentsMargins(18, 18, 18, 18);
    lc->setSpacing(14);

    m_preview = new FolderPreview(m_left);
    lc->addWidget(m_preview);

    m_nameEdit = new QLineEdit(m_left);
    m_nameEdit->setPlaceholderText(QStringLiteral("Nombre de la carpeta"));
    m_nameEdit->setFixedHeight(30);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, [this] { onConfirmed(); });
    lc->addWidget(m_nameEdit);

    lc->addStretch(1);

    m_locSection = new QWidget(m_left);
    m_locSection->setAttribute(Qt::WA_StyledBackground, false);
    m_locSection->setAutoFillBackground(false);
    auto* lv = new QVBoxLayout(m_locSection);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(6);
    lv->addWidget(makeKicker(QStringLiteral("Dentro de"), m_locSection, false));
    m_locBtn = new LocationButton(m_locSection);
    lv->addWidget(m_locBtn);
    lc->addWidget(m_locSection);

    m_cta = makeCtaButton(QStringLiteral("Crear carpeta"), m_left, false);
    connect(m_cta, &QPushButton::clicked, this, [this] { onConfirmed(); });
    lc->addWidget(m_cta);
    row->addWidget(m_left);

    m_divider = new QWidget(shell);
    m_divider->setFixedWidth(1);
    row->addWidget(m_divider);

    // ---- columna derecha
    m_right = new QWidget(shell);
    m_right->setAttribute(Qt::WA_StyledBackground, false);
    m_right->setAutoFillBackground(false);
    auto* rc = new QVBoxLayout(m_right);
    rc->setContentsMargins(24, 42, 24, 22);
    rc->setSpacing(22);

    auto* colSec = new QVBoxLayout();
    colSec->setSpacing(10);
    colSec->addWidget(makeKicker(QStringLiteral("Color"), m_right, false));
    m_dots = new DotRow(m_right);
    connect(m_dots, &DotRow::picked, this, [this](int i) {
        m_colorIndex = i;
        refreshState();
    });
    colSec->addWidget(m_dots);
    rc->addLayout(colSec);

    auto* iconSec = new QVBoxLayout();
    iconSec->setSpacing(10);
    iconSec->addWidget(makeKicker(QStringLiteral("Icono"), m_right, false));
    m_icons = new IconGrid(m_right);
    m_icons->setFixedHeight(106);
    connect(m_icons, &IconGrid::picked, this, [this](int i) {
        m_iconIndex = i;
        refreshState();
    });
    iconSec->addWidget(m_icons);
    rc->addLayout(iconSec);

    rc->addStretch(1);
    row->addWidget(m_right, 1);

    m_closeBtn = makeModalCloseButton(m_right);
    m_closeBtn->move(kModalW - kLeftW - 1 - 40, 12);
    connect(m_closeBtn, &QPushButton::clicked, this, [this] { closeSheet(false); });
    m_closeBtn->raise();

    // El menú de ubicación es hermano en HomeScreen.
    m_locPop = new LocationPopover(parent);
    connect(m_locPop, &LocationPopover::picked, this, [this](const QString& rel) {
        m_parentRelPath = rel;
        refreshState();
    });
    m_locBtn->onClick = [this] {
        if (!m_locPop)
            return;
        if (m_locPop->isVisible() && !m_locPop->isClosing()) {
            m_locPop->closePopover();
            return;
        }
        QVector<LocationEntry> entries;
        entries.append({QString(), QStringLiteral("Carpetas"), 0, 0});
        for (const FolderMeta& f : m_folders)
            entries.append({f.relativePath, f.name, f.depth + 1, f.colorIndex});
        m_locPop->setEntries(entries);
        m_locPop->setCurrent(m_parentRelPath);
        const QPoint tl = parentWidget() ? m_locBtn->mapTo(parentWidget(), QPoint(0, 0)) : QPoint();
        m_locPop->popup(QRect(tl, m_locBtn->size()));
    };
}

void NewFolderSheet::setFolders(const QVector<FolderMeta>& folders) {
    m_folders = folders;
}

void NewFolderSheet::applyDark(bool dark) {
    const auto pal = tokens::paletteFor(dark);
    m_divider->setStyleSheet(QStringLiteral("background:%1;").arg(pal.border.name(QColor::HexArgb)));
    styleModalTitleEdit(m_nameEdit, dark, 15.0);
    styleModalCloseButton(m_closeBtn, dark);
    m_dots->setDark(dark);
    m_icons->setDark(dark);
    m_locBtn->setDark(dark);
    if (m_locPop)
        m_locPop->setDark(dark);
}

void NewFolderSheet::refreshState() {
    m_preview->setLook(m_colorIndex, m_iconIndex, isDarkMode());
    m_dots->setCurrent(m_colorIndex);
    m_icons->setCurrent(m_iconIndex);
    QStringList parts{QStringLiteral("Carpetas")};
    if (!m_parentRelPath.isEmpty())
        parts += m_parentRelPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    m_locBtn->setPath(parts);
}

void NewFolderSheet::onClosing() {
    if (m_locPop)
        m_locPop->closePopover();
}

void NewFolderSheet::hideEvent(QHideEvent* event) {
    if (m_locPop)
        m_locPop->hide();
    GlassSheet::hideEvent(event);
}

void NewFolderSheet::openCreate(const QString& parentRelativePath, const HomePalette& pal, bool dark) {
    m_editMode = false;
    m_parentRelPath = parentRelativePath;
    m_editRelPath.clear();
    m_colorIndex = 2; // Salvia por defecto
    m_iconIndex = 1;
    applyDark(dark);
    m_locSection->setVisible(true);
    m_cta->setText(QStringLiteral("Crear carpeta"));
    m_nameEdit->setText(QStringLiteral("Nueva carpeta"));
    openSheet(pal, dark);
    refreshState();
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void NewFolderSheet::openEdit(const FolderMeta& folder, const HomePalette& pal, bool dark) {
    m_editMode = true;
    m_editRelPath = folder.relativePath;
    m_parentRelPath.clear();
    m_colorIndex = std::clamp(folder.colorIndex, 0, LibraryStore::kCoverColorCount - 1);
    m_iconIndex = std::clamp(folder.iconIndex, 0, LibraryStore::kIconCount - 1);
    applyDark(dark);
    m_locSection->setVisible(false);
    m_cta->setText(QStringLiteral("Guardar cambios"));
    m_nameEdit->setText(folder.name);
    openSheet(pal, dark);
    refreshState();
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void NewFolderSheet::onConfirmed() {
    const QString clean = LibraryStore::sanitizeBaseName(m_nameEdit->text());
    const QString finalName = clean.isEmpty() ? QStringLiteral("Nueva carpeta") : clean;
    closeSheet(true);
    if (m_editMode)
        emit editSubmitted(m_editRelPath, finalName, m_colorIndex, m_iconIndex);
    else
        emit createSubmitted(m_parentRelPath, finalName, m_colorIndex, m_iconIndex);
}

} // namespace finily
