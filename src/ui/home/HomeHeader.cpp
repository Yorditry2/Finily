#include "ui/home/HomeHeader.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace finily {

HomeHeader::HomeHeader(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);

    auto* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(2);

    m_crumbRow = new QHBoxLayout();
    m_crumbRow->setContentsMargins(0, 0, 0, 0);
    m_crumbRow->setSpacing(6);
    m_titleLabel = new QLabel(QStringLiteral("Todos"), this);
    m_titleLabel->setObjectName(QStringLiteral("finilySectionTitle"));
    m_crumbRow->addWidget(m_titleLabel);
    m_crumbRow->addStretch(1);
    titleCol->addLayout(m_crumbRow);

    m_subtitleLabel = new QLabel(this);
    m_subtitleLabel->setObjectName(QStringLiteral("finilyCountPill"));
    titleCol->addWidget(m_subtitleLabel);
    row->addLayout(titleCol, 1);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("finilySearch"));
    m_search->setPlaceholderText(QStringLiteral("Buscar documentos…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFocusPolicy(Qt::ClickFocus);
    m_search->setFixedSize(244, 36);
    m_searchAction = m_search->addAction(QIcon(), QLineEdit::LeadingPosition);
    connect(m_search, &QLineEdit::textChanged, this, &HomeHeader::searchTextChanged);
    row->addWidget(m_search, 0, Qt::AlignVCenter);
}

void HomeHeader::clearBreadcrumb() {
    if (!m_crumbRow)
        return;
    while (m_crumbRow->count() > 0) {
        QLayoutItem* item = m_crumbRow->takeAt(0);
        if (QWidget* w = item->widget()) {
            if (w != m_titleLabel)
                w->deleteLater();
        }
        delete item;
    }
}

void HomeHeader::setSectionTitle(const QString& title, const QString& subtitle) {
    clearBreadcrumb();
    m_titleLabel->setText(title);
    m_titleLabel->show();
    m_crumbRow->addWidget(m_titleLabel);
    m_crumbRow->addStretch(1);
    if (m_subtitleLabel)
        m_subtitleLabel->setText(subtitle);
}

void HomeHeader::setFolderBreadcrumb(const QString& relativePath, const QString& subtitle) {
    clearBreadcrumb();

    auto addCrumbBtn = [this](const QString& text, const QString& targetRel) {
        auto* btn = new QToolButton(this);
        btn->setObjectName(QStringLiteral("finilyCrumbBtn"));
        btn->setText(text);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QToolButton::clicked, this, [this, targetRel] {
            emit breadcrumbClicked(targetRel);
        });
        m_crumbRow->addWidget(btn);
    };
    auto addSep = [this] {
        auto* sep = new QLabel(QStringLiteral("›"), this);
        sep->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 500;"));
        m_crumbRow->addWidget(sep);
    };

    addCrumbBtn(QStringLiteral("Todos"), QString());
    const QStringList parts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString accum;
    for (int i = 0; i < parts.size(); ++i) {
        addSep();
        accum = accum.isEmpty() ? parts.at(i) : (accum + QLatin1Char('/') + parts.at(i));
        if (i + 1 < parts.size()) {
            addCrumbBtn(parts.at(i), accum);
        } else {
            m_titleLabel->setText(parts.at(i));
            m_titleLabel->show();
            m_crumbRow->addWidget(m_titleLabel);
        }
    }
    m_crumbRow->addStretch(1);
    if (m_subtitleLabel)
        m_subtitleLabel->setText(subtitle);
}

QString HomeHeader::searchText() const {
    return m_search ? m_search->text() : QString();
}

void HomeHeader::applyTheme(const HomePalette& pal, bool) {
    if (m_searchAction)
        m_searchAction->setIcon(homeGlyph(QStringLiteral("search"), pal.muted));
}

} // namespace finily
