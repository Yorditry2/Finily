#include "ui/home/NewFolderSheet.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {

class FolderHeroPreview : public QWidget {
public:
    explicit FolderHeroPreview(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(116, 94);
    }

    void setStyle(int colorIdx, int iconIdx, bool dark) {
        m_colorIdx = std::clamp(colorIdx, 0, int(coverColors().size()) - 1);
        m_iconIdx = std::clamp(iconIdx, 0, LibraryStore::kIconCount - 1);
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(6.0, 4.0, -6.0, -6.0);
        paintSoftShadow(painter, r, 12.0, 2.0, m_dark);
        paintEditorialFolder(painter, r, coverColors().at(m_colorIdx), m_iconIdx, m_dark);
    }

private:
    int m_colorIdx = 0;
    int m_iconIdx = 0;
    bool m_dark = false;
};

NewFolderSheet::NewFolderSheet(QWidget* parent)
    : GlassSheet(parent) {
    setCardWidth(416);

    m_preview = new FolderHeroPreview(this);
    bodyLayout()->addWidget(m_preview, 0, Qt::AlignHCenter);

    m_nameEdit = makeNameField(QStringLiteral("Ej: Álgebra"), QStringLiteral("Nueva carpeta"));
    bodyLayout()->addWidget(m_nameEdit);

    bodyLayout()->addWidget(makeSectionLabel(QStringLiteral("COLOR")));
    bodyLayout()->addWidget(makeColorSwatchRow(
        m_colorIndex,
        [this](int idx) {
            m_colorIndex = idx;
            m_preview->setStyle(m_colorIndex, m_iconIndex, isDarkMode());
        },
        &m_colorGroup));

    bodyLayout()->addWidget(makeSectionLabel(QStringLiteral("ÍCONO")));
    bodyLayout()->addWidget(makeSymbolIconRow(
        m_iconIndex,
        [this](int idx) {
            m_iconIndex = idx;
            m_preview->setStyle(m_colorIndex, m_iconIndex, isDarkMode());
        },
        &m_iconGroup));
}

void NewFolderSheet::openCreate(const QString& parentRelativePath, const HomePalette& pal, bool dark) {
    m_editMode = false;
    m_parentRelPath = parentRelativePath;
    m_editRelPath.clear();
    m_colorIndex = 0;
    m_iconIndex = 1;
    configureHeader(parentRelativePath.isEmpty() ? QStringLiteral("Nueva carpeta")
                                                 : QStringLiteral("Nueva subcarpeta"),
                    QStringLiteral("Crear"));
    m_nameEdit->setText(QStringLiteral("Nueva carpeta"));
    m_nameEdit->selectAll();
    openSheet(pal, dark);
    refreshGroups();
    m_preview->setStyle(m_colorIndex, m_iconIndex, dark);
    m_nameEdit->setFocus();
}

void NewFolderSheet::openEdit(const FolderMeta& folder, const HomePalette& pal, bool dark) {
    m_editMode = true;
    m_editRelPath = folder.relativePath;
    m_parentRelPath.clear();
    m_colorIndex = std::clamp(folder.colorIndex, 0, LibraryStore::kCoverColorCount - 1);
    m_iconIndex = std::clamp(folder.iconIndex, 0, LibraryStore::kIconCount - 1);
    configureHeader(QStringLiteral("Editar carpeta"), QStringLiteral("Listo"));
    m_nameEdit->setText(folder.name);
    m_nameEdit->selectAll();
    openSheet(pal, dark);
    refreshGroups();
    m_preview->setStyle(m_colorIndex, m_iconIndex, dark);
    m_nameEdit->setFocus();
}

void NewFolderSheet::refreshGroups() {
    if (m_colorGroup) {
        if (auto* b = m_colorGroup->button(m_colorIndex))
            b->setChecked(true);
    }
    if (m_iconGroup) {
        if (auto* b = m_iconGroup->button(m_iconIndex))
            b->setChecked(true);
    }
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
