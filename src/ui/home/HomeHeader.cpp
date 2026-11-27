#include "ui/home/HomeHeader.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

namespace finily {

// Botón de ícono 32 px, radio 9 (.icon-btn del mockup).
class HeaderIconButton : public QWidget {
    Q_OBJECT

public:
    HeaderIconButton(QString icon, const QString& tip, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_icon(std::move(icon)) {
        setFixedSize(32, 32);
        setToolTip(tip);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }
    void setPalette(const HomePalette& pal) { m_pal = pal; update(); }
    void setBadge(bool on) { m_badge = on; update(); }
    void setPressedLook(bool on) { m_pressedLook = on; update(); }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (underMouse() || m_pressedLook) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_pal.hover);
            painter.drawRoundedRect(QRectF(rect()), tokens::radius::Nav, tokens::radius::Nav);
        }
        paintUiIcon(&painter, QRectF(8, 8, 16, 16), m_icon, m_pal.icon);
        if (m_badge) {
            painter.setPen(QPen(m_pal.bg, 2));
            painter.setBrush(m_pal.accent);
            painter.drawEllipse(QPointF(24, 8), 3.5, 3.5);
        }
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked();
    }

private:
    QString m_icon;
    bool m_badge = false;
    bool m_pressedLook = false;
    HomePalette m_pal = homePaletteFor(false);
};

// Ficha de ámbito (.sfield .scope): «[carpeta] Apuntes ×».
class SearchScopeChip : public QWidget {
    Q_OBJECT

public:
    explicit SearchScopeChip(QWidget* parent = nullptr)
        : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(24);
        setToolTip(QStringLiteral("Buscar en toda la biblioteca"));
        hide();
    }
    void setName(const QString& name) {
        m_name = name;
        const QFont f = chipFont();
        setFixedWidth(8 + 12 + 5 + QFontMetrics(f).horizontalAdvance(name) + 5 + 12 + 6);
        setVisible(!name.isEmpty());
        update();
    }
    HomePalette pal = homePaletteFor(false);
    bool dark = false;

signals:
    void cleared();

protected:
    QFont chipFont() const {
        QFont f = font();
        f.setPixelSize(12);
        f.setWeight(QFont::DemiBold);
        return f;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(dark);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 23, 42, dark ? 60 : 18));
        p.drawRoundedRect(QRectF(rect()).adjusted(0, 1, 0, 1), 7, 7);
        p.setBrush(tk.surface);
        p.drawRoundedRect(QRectF(rect()), 7, 7);
        paintUiIcon(&p, QRectF(8, 6, 12, 12), QStringLiteral("folder"), tk.icon);
        p.setFont(chipFont());
        p.setPen(tk.icon);
        p.drawText(QRect(25, 0, width() - 25 - 18, height()), Qt::AlignLeft | Qt::AlignVCenter, m_name);
        paintUiIcon(&p, QRectF(width() - 18, 6, 12, 12), QStringLiteral("x"), tk.faint);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
            emit cleared();
    }

private:
    QString m_name;
};

// Píldora «+ Nuevo»: único disparador de creación (DESIGN.md).
class HeaderNewButton : public QWidget {
    Q_OBJECT

public:
    explicit HeaderNewButton(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedHeight(32);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        QFont f = font();
        f.setPixelSize(13);
        f.setWeight(QFont::DemiBold);
        setFont(f);
        setFixedWidth(11 + 16 + 6 + QFontMetrics(f).horizontalAdvance(QStringLiteral("Nuevo")) + 14);
    }
    void setPalette(const HomePalette& pal) { m_pal = pal; update(); }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(underMouse() ? m_pal.accentHover : m_pal.accent);
        painter.drawRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
        paintUiIcon(&painter, QRectF(11, 8, 16, 16), QStringLiteral("plus"), m_pal.accentFg);
        painter.setPen(m_pal.accentFg);
        painter.drawText(QRect(33, 0, width() - 33, height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Nuevo"));
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked();
    }

private:
    HomePalette m_pal = homePaletteFor(false);
};

// Campo de búsqueda desplegado (.sfield): fondo de campo, lupa y «Esc».
class HeaderSearchField : public QWidget {
public:
    explicit HeaderSearchField(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedHeight(36);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(37, 0, 46, 0);
        row->setSpacing(8);
        scope = new SearchScopeChip(this);
        row->addWidget(scope);
        edit = new QLineEdit(this);
        edit->setObjectName(QStringLiteral("finilySearch"));
        edit->setPlaceholderText(QStringLiteral("Buscar en la biblioteca"));
        edit->setFrame(false);
        row->addWidget(edit);
    }
    QLineEdit* edit = nullptr;
    SearchScopeChip* scope = nullptr;
    HomePalette pal = homePaletteFor(false);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surfaceAlt);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), tokens::radius::Control,
                                tokens::radius::Control);
        paintUiIcon(&painter, QRectF(12, 10, 16, 16), QStringLiteral("search"), pal.muted);

        // Tecla «Esc» como chip pequeño a la derecha.
        QFont f = painter.font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        painter.setFont(f);
        const QRectF kbd(width() - 40, 9, 30, 18);
        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surface);
        painter.drawRoundedRect(kbd, 5, 5);
        painter.setPen(pal.muted);
        painter.drawText(kbd, Qt::AlignCenter, QStringLiteral("Esc"));
    }
};

HomeHeader::HomeHeader(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setFixedHeight(tokens::space::HeaderHeight);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(22, 0, 22, 1);
    row->setSpacing(6);

    m_reopenBtn = new HeaderIconButton(QStringLiteral("side"), QStringLiteral("Mostrar barra lateral"), this);
    m_reopenBtn->hide();
    connect(m_reopenBtn, &HeaderIconButton::clicked, this, &HomeHeader::reopenSidebarRequested);
    row->addWidget(m_reopenBtn);

    m_titleBox = new QWidget(this);
    m_crumbRow = new QHBoxLayout(m_titleBox);
    m_crumbRow->setContentsMargins(0, 0, 0, 0);
    m_crumbRow->setSpacing(6);
    row->addWidget(m_titleBox, 1);

    m_searchField = new HeaderSearchField(this);
    m_search = m_searchField->edit;
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &HomeHeader::searchTextChanged);
    m_searchField->hide();
    row->addWidget(m_searchField, 1);

    connect(m_searchField->scope, &SearchScopeChip::cleared, this, [this] {
        emit searchScopeCleared();
        m_search->setFocus(Qt::OtherFocusReason);
    });
    connect(m_search, &QLineEdit::returnPressed, this, &HomeHeader::searchSubmitted);

    m_searchBtn = new HeaderIconButton(QStringLiteral("search"), QStringLiteral("Buscar (Ctrl+F)"), this);
    connect(m_searchBtn, &HeaderIconButton::clicked, this, [this] { setSearchOpen(true); });
    row->addWidget(m_searchBtn);

    m_filterBtn = new HeaderIconButton(QStringLiteral("filter"), QStringLiteral("Filtrar y ordenar"), this);
    connect(m_filterBtn, &HeaderIconButton::clicked, this, &HomeHeader::filterRequested);
    row->addWidget(m_filterBtn);

    m_newBtn = new HeaderNewButton(this);
    connect(m_newBtn, &HeaderNewButton::clicked, this, &HomeHeader::newRequested);
    row->addSpacing(4);
    row->addWidget(m_newBtn);

    setSectionTitle(QStringLiteral("Documentos"));
}

void HomeHeader::clearCrumbs() {
    while (QLayoutItem* item = m_crumbRow->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

void HomeHeader::setSectionTitle(const QString& title) {
    clearCrumbs();
    auto* label = new QLabel(title, m_titleBox);
    label->setObjectName(QStringLiteral("finilySectionTitle"));
    m_crumbRow->addWidget(label);
    m_crumbRow->addStretch(1);
}

void HomeHeader::setFolderBreadcrumb(const QString& relativePath) {
    clearCrumbs();
    const QStringList parts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    auto addSep = [this] {
        auto* sep = new QLabel(m_titleBox);
        sep->setPixmap(uiIcon(QStringLiteral("chevR"), m_pal.faint, 13).pixmap(13, 13));
        m_crumbRow->addWidget(sep);
    };
    auto addCrumb = [this](const QString& text, const QString& target) {
        auto* btn = new QToolButton(m_titleBox);
        btn->setObjectName(QStringLiteral("finilyCrumbBtn"));
        btn->setText(text);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QToolButton::clicked, this, [this, target] { emit breadcrumbClicked(target); });
        m_crumbRow->addWidget(btn);
    };

    // Raíz «Carpetas» y luego cada nivel; el último es el título actual.
    addCrumb(QStringLiteral("Carpetas"), QString());
    QString accum;
    for (int i = 0; i < parts.size(); ++i) {
        addSep();
        accum = accum.isEmpty() ? parts.at(i) : accum + QLatin1Char('/') + parts.at(i);
        if (i + 1 < parts.size()) {
            addCrumb(parts.at(i), accum);
        } else {
            auto* label = new QLabel(parts.at(i), m_titleBox);
            label->setObjectName(QStringLiteral("finilySectionTitle"));
            m_crumbRow->addWidget(label);
        }
    }
    m_crumbRow->addStretch(1);
}

void HomeHeader::setSidebarCollapsed(bool collapsed) {
    m_reopenBtn->setVisible(collapsed);
}

QString HomeHeader::searchText() const {
    return m_search->text();
}

QRect HomeHeader::newButtonGeometry() const {
    return m_newBtn->geometry();
}

QRect HomeHeader::filterButtonGeometry() const {
    return m_filterBtn->geometry();
}

void HomeHeader::setFilterActive(bool active) {
    m_filterBtn->setBadge(active);
}

void HomeHeader::setFilterVisible(bool visible) {
    m_filterBtn->setVisible(visible);
}

void HomeHeader::setSearchScope(const QString& name) {
    m_searchField->scope->setName(name);
}

void HomeHeader::openSearch() {
    setSearchOpen(true);
}

void HomeHeader::setLibraryControlsVisible(bool visible) {
    if (!visible)
        closeSearch();
    m_searchBtn->setVisible(visible);
    m_newBtn->setVisible(visible);
    if (!visible)
        m_filterBtn->setVisible(false);
}

void HomeHeader::closeSearch() {
    if (m_searchField->isVisible())
        setSearchOpen(false);
}

void HomeHeader::setSearchOpen(bool open) {
    m_titleBox->setVisible(!open);
    m_searchBtn->setVisible(!open);
    m_searchField->setVisible(open);
    if (open) {
        m_search->setFocus(Qt::ShortcutFocusReason);
    } else {
        m_search->clear();
        m_search->clearFocus();
    }
}

bool HomeHeader::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_search) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            setSearchOpen(false);
            return true;
        }
        // Al perder el foco con el campo vacío, vuelve el título.
        if (event->type() == QEvent::FocusOut && m_search->text().isEmpty())
            setSearchOpen(false);
    }
    return QWidget::eventFilter(watched, event);
}

void HomeHeader::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_reopenBtn->setPalette(pal);
    m_searchBtn->setPalette(pal);
    m_filterBtn->setPalette(pal);
    m_searchField->scope->pal = pal;
    m_searchField->scope->dark = dark;
    m_newBtn->setPalette(pal);
    m_searchField->pal = pal;
    m_searchField->update();
    update();
}

void HomeHeader::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(QRect(0, height() - 1, width(), 1), m_pal.border);
}

#include "HomeHeader.moc"

} // namespace finily
