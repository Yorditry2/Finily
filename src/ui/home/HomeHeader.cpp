#include "ui/home/HomeHeader.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPauseAnimation>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSequentialAnimationGroup>
#include <QSizePolicy>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <functional>

namespace finily {

// Botón de ícono 32 px, radio 9 (.icon-btn del mockup).
class HeaderIconButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal revealProgress READ revealProgress WRITE setRevealProgress)

public:
    HeaderIconButton(QString icon, const QString& tip, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_icon(std::move(icon)) {
        setFixedSize(36, 36);
        setToolTip(tip);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }

    void setCollapsible(bool on) { m_collapsible = on; }
    void setBadgeRadius(qreal r) { m_badgeRadius = r; update(); }
    void setTheme(const HomePalette& pal, bool dark) {
        m_pal = pal;
        m_dark = dark;
        update();
    }
    void setBadge(bool on) {
        if (m_badge != on) {
            m_badge = on;
            update();
        }
    }
    void setPressedLook(bool on) {
        m_pressedLook = on;
        update();
    }

    qreal revealProgress() const { return m_reveal; }
    void setRevealProgress(qreal p) {
        m_reveal = std::clamp(p, 0.0, 1.0);
        if (m_collapsible) {
            const int w = tokens::reducedMotion() ? (m_reveal > 0.001 ? 36 : 0)
                                                  : qRound(36.0 * m_reveal);
            setFixedWidth(w);
        }
        update();
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        if (m_reveal <= 0.001 || width() <= 0)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setOpacity(m_reveal);

        const tokens::Palette tk = tokens::paletteFor(m_dark);
        if (underMouse() || m_pressedLook) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(tk.hover);
            painter.drawRoundedRect(QRectF(rect()), tokens::radius::Nav, tokens::radius::Nav);
        }

        const qreal cx = width() * 0.5;
        const qreal cy = height() * 0.5;
        paintUiIcon(&painter, QRectF(cx - 9.0, cy - 9.0, 18.0, 18.0), m_icon, tk.icon);

        if (m_badge) {
            painter.setPen(QPen(tk.bg, 1.5));
            painter.setBrush(tk.accent);
            painter.drawEllipse(QPointF(cx + 9.0, cy - 9.0), m_badgeRadius, m_badgeRadius);
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
    bool m_collapsible = false;
    bool m_dark = false;
    qreal m_badgeRadius = 3.0;
    qreal m_reveal = 1.0;
    HomePalette m_pal = homePaletteFor(false);
};

// Píldora de acción en cabecera (.text-btn / .text-btn.danger).
class HeaderPillButton : public QWidget {
    Q_OBJECT

public:
    HeaderPillButton(QString text, bool danger, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_text(std::move(text))
        , m_danger(danger) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        QFont f = font();
        f.setPixelSize(13);
        f.setWeight(QFont::DemiBold);
        setFont(f);
        setFixedSize(QFontMetrics(f).horizontalAdvance(m_text) + 28, 34);
    }

    void setTheme(const HomePalette& pal, bool dark) {
        m_pal = pal;
        m_dark = dark;
        update();
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(m_dark);
        const QColor border = m_dark ? QColor(255, 255, 255, 26) : QColor(15, 23, 42, 26);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(underMouse() ? tk.hover : tk.surface);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 17.0, 17.0);
        painter.setFont(font());
        painter.setPen(m_danger ? tk.danger : tk.icon);
        painter.drawText(rect(), Qt::AlignCenter, m_text);
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked();
    }

private:
    QString m_text;
    bool m_danger = false;
    bool m_dark = false;
    HomePalette m_pal = homePaletteFor(false);
};

// Chip clicable «Esc» para cerrar Ajustes (mockup 10).
class HeaderKbdButton : public QWidget {
    Q_OBJECT

public:
    explicit HeaderKbdButton(QWidget* parent = nullptr)
        : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setToolTip(QStringLiteral("Volver a la biblioteca (Esc)"));
        setFixedSize(42, 26);
    }

    void setTheme(bool dark) {
        m_dark = dark;
        update();
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(m_dark);
        const QColor border = m_dark ? QColor(255, 255, 255, 26) : QColor(15, 23, 42, 22);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(underMouse() ? tk.hover : tk.field);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);

        QFont f = font();
        f.setPixelSize(12);
        f.setWeight(QFont::DemiBold);
        painter.setFont(f);
        painter.setPen(tk.icon);
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Esc"));
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked();
    }

private:
    bool m_dark = false;
};

// Ficha de ámbito (.sfield .scope): «[carpeta] Apuntes ×».
class SearchScopeChip : public QWidget {
    Q_OBJECT

public:
    explicit SearchScopeChip(QWidget* parent = nullptr)
        : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(26);
        setToolTip(QStringLiteral("Buscar en toda la biblioteca"));
        hide();
    }

    void setName(const QString& name) {
        m_name = name;
        const QFont f = chipFont();
        setFixedWidth(9 + 13 + 6 + QFontMetrics(f).horizontalAdvance(name) + 6 + 13 + 7);
        setVisible(!name.isEmpty());
        update();
    }

    bool hasScope() const { return !m_name.isEmpty(); }
    HomePalette pal = homePaletteFor(false);
    bool dark = false;

signals:
    void cleared();

protected:
    QFont chipFont() const {
        QFont f = font();
        f.setPixelSize(13);
        f.setWeight(QFont::DemiBold);
        return f;
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(dark);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 23, 42, dark ? 60 : 18));
        p.drawRoundedRect(QRectF(rect()).adjusted(0, 1, 0, 1), 8, 8);
        p.setBrush(tk.surface);
        p.drawRoundedRect(QRectF(rect()), 8, 8);
        paintUiIcon(&p, QRectF(9, 6.5, 13, 13), QStringLiteral("folder"), tk.icon);
        p.setFont(chipFont());
        p.setPen(tk.icon);
        p.drawText(QRect(28, 0, width() - 28 - 20, height()), Qt::AlignLeft | Qt::AlignVCenter, m_name);
        paintUiIcon(&p, QRectF(width() - 20, 6.5, 13, 13), QStringLiteral("x"), tk.faint);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
            emit cleared();
    }

private:
    QString m_name;
};

// Píldora «+ Nuevo»: único disparador de creación (DESIGN.md + mockup 11 escena 02).
class HeaderNewButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pressScale READ pressScale WRITE setPressScale)

public:
    explicit HeaderNewButton(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedHeight(36);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        QFont f = font();
        f.setPixelSize(14);
        f.setWeight(QFont::DemiBold);
        setFont(f);
        setFixedWidth(13 + 17 + 7 + QFontMetrics(f).horizontalAdvance(QStringLiteral("Nuevo")) + 16);
    }

    void setPalette(const HomePalette& pal) {
        m_pal = pal;
        update();
    }

    qreal pressScale() const { return m_pressScale; }
    void setPressScale(qreal s) {
        m_pressScale = s;
        update();
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (std::abs(m_pressScale - 1.0) > 0.001) {
            const QPointF c(width() * 0.5, height() * 0.5);
            painter.translate(c);
            painter.scale(m_pressScale, m_pressScale);
            painter.translate(-c);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(underMouse() ? m_pal.accentHover : m_pal.accent);
        painter.drawRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
        paintUiIcon(&painter, QRectF(13, 9.5, 17, 17), QStringLiteral("plus"), m_pal.accentFg);
        painter.setFont(font());
        painter.setPen(m_pal.accentFg);
        painter.drawText(QRect(37, 0, width() - 37, height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Nuevo"));
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && !tokens::reducedMotion())
            animateScale(0.94, 80, tokens::motion::in());
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (!tokens::reducedMotion())
                animateScale(1.0, tokens::motion::Base, tokens::motion::spring());
            else
                setPressScale(1.0);
            if (rect().contains(event->pos()))
                emit clicked();
        }
    }

private:
    void animateScale(qreal target, int durationMs, const QEasingCurve& curve) {
        if (!m_scaleAnim)
            m_scaleAnim = new QPropertyAnimation(this, "pressScale", this);
        m_scaleAnim->stop();
        m_scaleAnim->setDuration(durationMs);
        m_scaleAnim->setEasingCurve(curve);
        m_scaleAnim->setStartValue(m_pressScale);
        m_scaleAnim->setEndValue(target);
        m_scaleAnim->start();
    }

    HomePalette m_pal = homePaletteFor(false);
    qreal m_pressScale = 1.0;
    QPropertyAnimation* m_scaleAnim = nullptr;
};

// Campo de búsqueda desplegado (.sfield): fondo de campo, lupa y «Esc».
class HeaderSearchField : public QWidget {
    Q_OBJECT

public:
    explicit HeaderSearchField(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedHeight(40);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(40, 0, 50, 0);
        row->setSpacing(8);
        scope = new SearchScopeChip(this);
        row->addWidget(scope);
        edit = new QLineEdit(this);
        edit->setObjectName(QStringLiteral("finilySearch"));
        edit->setPlaceholderText(QStringLiteral("Buscar en la biblioteca"));
        edit->setFrame(false);
        row->addWidget(edit);
    }

    void setReveal(qreal clipFraction, qreal opacity) {
        m_clipFrac = std::clamp(clipFraction, 0.0, 1.0);
        m_opacity = std::clamp(opacity, 0.0, 1.0);
        const bool settled = m_clipFrac >= 0.999 && m_opacity >= 0.999;
        edit->setVisible(settled);
        scope->setVisible(settled && scope->hasScope());
        update();
    }

    QLineEdit* edit = nullptr;
    SearchScopeChip* scope = nullptr;
    HomePalette pal = homePaletteFor(false);

signals:
    void escapeClicked();

protected:
    QRectF kbdRect() const { return QRectF(width() - 44.0, 10.0, 34.0, 20.0); }

    void paintEvent(QPaintEvent*) override {
        if (m_clipFrac <= 0.001 || m_opacity <= 0.001)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setOpacity(m_opacity);

        if (m_clipFrac < 0.999) {
            const qreal visW = width() * m_clipFrac;
            painter.setClipRect(QRectF(width() - visW, 0.0, visW, height()));
        }

        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surfaceAlt);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                tokens::radius::Control, tokens::radius::Control);
        paintUiIcon(&painter, QRectF(13, 11, 18, 18), QStringLiteral("search"), pal.muted);

        if (!edit->isVisible()) {
            QFont pf = font();
            pf.setPixelSize(14);
            pf.setWeight(QFont::Normal);
            painter.setFont(pf);
            painter.setPen(pal.faint);
            painter.drawText(QRectF(40, 0, std::max<qreal>(0.0, width() - 96.0), height()),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("Buscar en la biblioteca"));
        }

        QFont f = font();
        f.setPixelSize(11);
        f.setWeight(QFont::DemiBold);
        painter.setFont(f);
        const QRectF kbd = kbdRect();
        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surface);
        painter.drawRoundedRect(kbd, 5, 5);
        painter.setPen(pal.muted);
        painter.drawText(kbd, Qt::AlignCenter, QStringLiteral("Esc"));
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && kbdRect().adjusted(-4, -4, 4, 4).contains(event->position()))
            emit escapeClicked();
        QWidget::mouseReleaseEvent(event);
    }

private:
    qreal m_clipFrac = 1.0;
    qreal m_opacity = 1.0;
};

// Superposición ligera para deslizar/fundir el cambio de título o migas (mockup 11 escena 04).
class TitleTransitionOverlay : public QWidget {
public:
    explicit TitleTransitionOverlay(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_StyledBackground, false);
        hide();
    }

    void play(const QPixmap& oldSnap, const QPixmap& newSnap, const QRect& boxRect, const QColor& bg) {
        m_old = oldSnap;
        m_new = newSnap;
        m_bg = bg;
        setGeometry(boxRect);
        raise();
        show();

        if (!m_anim) {
            m_anim = new QVariantAnimation(this);
            m_anim->setStartValue(0.0);
            m_anim->setEndValue(1.0);
            connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
                m_t = val.toReal();
                update();
            });
            connect(m_anim, &QVariantAnimation::finished, this, [this] { hide(); });
        }
        m_anim->stop();
        m_anim->setDuration(tokens::motion::duration(tokens::motion::Base));
        m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
        m_t = 0.0;
        m_anim->start();
    }

    void cancel() {
        if (m_anim)
            m_anim->stop();
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), m_bg);
        const bool reduced = tokens::reducedMotion();
        const qreal oldY = reduced ? 0.0 : -6.0 * m_t;
        const qreal newY = reduced ? 0.0 : 6.0 * (1.0 - m_t);
        if (!m_old.isNull() && m_t < 0.999) {
            painter.save();
            painter.setOpacity(1.0 - m_t);
            painter.drawPixmap(QPointF(0.0, oldY), m_old);
            painter.restore();
        }
        if (!m_new.isNull() && m_t > 0.001) {
            painter.save();
            painter.setOpacity(m_t);
            painter.drawPixmap(QPointF(0.0, newY), m_new);
            painter.restore();
        }
    }

private:
    QPixmap m_old;
    QPixmap m_new;
    QColor m_bg = tokens::lightPalette().bg;
    qreal m_t = 0.0;
    QVariantAnimation* m_anim = nullptr;
};

HomeHeader::HomeHeader(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setFixedHeight(tokens::space::HeaderHeight);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(26, 0, 26, 1);
    row->setSpacing(8);

    m_reopenBtn = new HeaderIconButton(QStringLiteral("side"), QStringLiteral("Mostrar barra lateral"), this);
    m_reopenBtn->setCollapsible(true);
    m_reopenBtn->setRevealProgress(0.0);
    m_reopenBtn->hide();
    connect(m_reopenBtn, &HeaderIconButton::clicked, this, &HomeHeader::reopenSidebarRequested);
    row->addWidget(m_reopenBtn);

    m_titleBox = new QWidget(this);
    QSizePolicy sp = m_titleBox->sizePolicy();
    sp.setRetainSizeWhenHidden(true);
    m_titleBox->setSizePolicy(sp);
    m_crumbRow = new QHBoxLayout(m_titleBox);
    m_crumbRow->setContentsMargins(0, 0, 0, 0);
    m_crumbRow->setSpacing(6);
    row->addWidget(m_titleBox, 1);

    m_emptyTrashBtn = new HeaderPillButton(QStringLiteral("Vaciar papelera"), true, this);
    m_emptyTrashBtn->hide();
    connect(m_emptyTrashBtn, &HeaderPillButton::clicked, this, &HomeHeader::emptyTrashRequested);
    row->addWidget(m_emptyTrashBtn);

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

    m_escBtn = new HeaderKbdButton(this);
    m_escBtn->hide();
    connect(m_escBtn, &HeaderKbdButton::clicked, this, &HomeHeader::settingsCloseRequested);
    row->addWidget(m_escBtn);

#ifdef Q_OS_WIN
    auto makeWinBtn = [this](int kind, const QString& tip) {
        auto* btn = new QToolButton(this);
        btn->setFixedSize(30, 30);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tip);
        btn->setAttribute(Qt::WA_Hover, true);
        btn->setProperty("winKind", kind);
        btn->setStyleSheet(QStringLiteral(
            "QToolButton { border: none; border-radius: 8px; background: transparent; }"
            "QToolButton:hover { background: %1; }")
            .arg(kind == 2 ? QStringLiteral("rgba(220,38,38,220)") : QStringLiteral("rgba(128,128,128,45)")));
        btn->installEventFilter(this);
        connect(btn, &QToolButton::clicked, this, [this, kind] {
            QWidget* w = window();
            if (!w)
                return;
            if (kind == 0)
                w->showMinimized();
            else if (kind == 1) {
                if (w->isMaximized())
                    w->showNormal();
                else
                    w->showMaximized();
            } else {
                w->close();
            }
        });
        return btn;
    };
    row->addSpacing(6);
    row->addWidget(makeWinBtn(0, QStringLiteral("Minimizar")));
    row->addWidget(makeWinBtn(1, QStringLiteral("Maximizar / Restaurar")));
    row->addWidget(makeWinBtn(2, QStringLiteral("Cerrar")));
#endif

    m_searchField = new HeaderSearchField(this);
    m_search = m_searchField->edit;
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &HomeHeader::searchTextChanged);
    connect(m_searchField, &HeaderSearchField::escapeClicked, this, [this] { setSearchOpen(false); });
    m_searchField->hide();

    connect(m_searchField->scope, &SearchScopeChip::cleared, this, [this] {
        emit searchScopeCleared();
        m_search->setFocus(Qt::OtherFocusReason);
    });
    connect(m_search, &QLineEdit::returnPressed, this, &HomeHeader::searchSubmitted);

    m_titleOverlay = new TitleTransitionOverlay(this);

    setSectionTitle(QStringLiteral("Documentos"));
}

void HomeHeader::clearCrumbs() {
    m_sepLabels.clear();
    m_mutedLabels.clear();
    while (QLayoutItem* item = m_crumbRow->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
}

namespace {

QPixmap renderWidgetTransparent(QWidget* w) {
    if (!w || w->width() <= 0 || w->height() <= 0)
        return {};
    const qreal dpr = w->devicePixelRatioF();
    QPixmap pix(qRound(w->width() * dpr), qRound(w->height() * dpr));
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    w->render(&pix, QPoint(), QRegion(), QWidget::DrawChildren);
    return pix;
}

} // namespace

void HomeHeader::runTitleChange(const QString& key, bool animate, const std::function<void()>& rebuild) {
    if (key == m_headerKey)
        return;
    m_headerKey = key;

    const bool canAnimate = animate && isVisible() && m_titleBox->isVisible()
                            && m_titleBox->width() > 0 && m_titleBox->height() > 0;
    QPixmap oldSnap;
    if (canAnimate)
        oldSnap = renderWidgetTransparent(m_titleBox);
    else if (m_titleOverlay)
        m_titleOverlay->cancel();

    clearCrumbs();
    rebuild();
    for (int i = 0; i < m_crumbRow->count(); ++i) {
        if (QWidget* w = m_crumbRow->itemAt(i)->widget())
            w->show();
    }
    m_crumbRow->activate();

    if (canAnimate && !oldSnap.isNull() && m_titleOverlay) {
        const QPixmap newSnap = renderWidgetTransparent(m_titleBox);
        m_titleOverlay->play(oldSnap, newSnap, m_titleBox->geometry(), m_pal.bg);
    }
}

void HomeHeader::setSectionTitle(const QString& title, bool animate) {
    runTitleChange(QStringLiteral("S:") + title, animate, [this, &title] {
        auto* label = new QLabel(title, m_titleBox);
        label->setObjectName(QStringLiteral("finilySectionTitle"));
        m_crumbRow->addWidget(label);
        m_crumbRow->addStretch(1);
    });
}

void HomeHeader::setFolderBreadcrumb(const QString& relativePath, bool animate) {
    runTitleChange(QStringLiteral("F:") + relativePath, animate, [this, &relativePath] {
        const QStringList parts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        auto addSep = [this] {
            auto* sep = new QLabel(m_titleBox);
            sep->setPixmap(uiIcon(QStringLiteral("chevR"), m_pal.faint, 15).pixmap(15, 15));
            m_sepLabels.append(sep);
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
    });
}

void HomeHeader::setTagBreadcrumb(const QString& tagName, bool animate) {
    runTitleChange(QStringLiteral("T:") + tagName, animate, [this, &tagName] {
        auto* rootCrumb = new QLabel(QStringLiteral("Etiquetas"), m_titleBox);
        QFont f = font();
        f.setPixelSize(18);
        f.setWeight(QFont::Medium);
        rootCrumb->setFont(f);
        rootCrumb->setStyleSheet(QStringLiteral("color: %1;").arg(m_pal.muted.name(QColor::HexArgb)));
        m_mutedLabels.append(rootCrumb);
        m_crumbRow->addWidget(rootCrumb);

        auto* sep = new QLabel(m_titleBox);
        sep->setPixmap(uiIcon(QStringLiteral("chevR"), m_pal.faint, 15).pixmap(15, 15));
        m_sepLabels.append(sep);
        m_crumbRow->addWidget(sep);

        auto* label = new QLabel(QStringLiteral("# ") + tagName, m_titleBox);
        label->setObjectName(QStringLiteral("finilySectionTitle"));
        m_crumbRow->addWidget(label);
        m_crumbRow->addStretch(1);
    });
}

void HomeHeader::setSidebarCollapsed(bool collapsed, bool animated) {
    m_sidebarCollapsed = collapsed;
    if (m_reopenAnim) {
        m_reopenAnim->stop();
        m_reopenAnim->clear();
    }

    if (!animated || !isVisible()) {
        m_reopenBtn->setRevealProgress(collapsed ? 1.0 : 0.0);
        m_reopenBtn->setVisible(collapsed);
        return;
    }

    if (!m_reopenAnim) {
        m_reopenAnim = new QSequentialAnimationGroup(this);
        connect(m_reopenAnim, &QSequentialAnimationGroup::finished, this, [this] {
            if (!m_sidebarCollapsed && m_reopenBtn)
                m_reopenBtn->hide();
        });
    }

    if (collapsed) {
        m_reopenBtn->setRevealProgress(0.0);
        m_reopenBtn->show();
        if (!tokens::reducedMotion()) {
            auto* delay = new QPauseAnimation(120, m_reopenAnim);
            m_reopenAnim->addAnimation(delay);
        }
        auto* reveal = new QPropertyAnimation(m_reopenBtn, "revealProgress", m_reopenAnim);
        reveal->setDuration(tokens::motion::duration(tokens::motion::Slow));
        reveal->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
        reveal->setStartValue(0.0);
        reveal->setEndValue(1.0);
        m_reopenAnim->addAnimation(reveal);
    } else {
        auto* hideAnim = new QPropertyAnimation(m_reopenBtn, "revealProgress", m_reopenAnim);
        hideAnim->setDuration(tokens::motion::duration(tokens::motion::Base));
        hideAnim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
        hideAnim->setStartValue(m_reopenBtn->revealProgress());
        hideAnim->setEndValue(0.0);
        m_reopenAnim->addAnimation(hideAnim);
    }
    m_reopenAnim->start();
}

void HomeHeader::setUpdateDotVisible(bool visible) {
    m_updateDotVisible = visible;
    if (m_reopenBtn)
        m_reopenBtn->setBadge(visible);
}

void HomeHeader::setTrashEmptyButtonVisible(bool visible) {
    m_trashEmptyVisible = visible;
    if (m_emptyTrashBtn)
        m_emptyTrashBtn->setVisible(visible && !m_settingsMode && !m_searchOpen);
}

void HomeHeader::setSettingsMode(bool on) {
    m_settingsMode = on;
    if (on)
        closeSearch();
    m_searchBtn->setVisible(!on && !m_searchOpen);
    m_filterBtn->setVisible(!on && m_filterVisible);
    m_newBtn->setVisible(!on);
    m_emptyTrashBtn->setVisible(!on && m_trashEmptyVisible && !m_searchOpen);
    m_escBtn->setVisible(on);
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
    m_filterVisible = visible;
    m_filterBtn->setVisible(visible && !m_settingsMode);
}

void HomeHeader::setSearchScope(const QString& name) {
    m_searchField->scope->setName(name);
    if (m_searchProgress >= 0.999)
        m_searchField->scope->setVisible(!name.isEmpty());
}

void HomeHeader::openSearch() {
    setSearchOpen(true);
}

void HomeHeader::setLibraryControlsVisible(bool visible) {
    setSettingsMode(!visible);
}

void HomeHeader::closeSearch() {
    if (m_searchOpen || m_searchField->isVisible() || m_searchProgress > 0.0)
        setSearchOpen(false);
}

QRect HomeHeader::searchTargetRect() const {
    const int left = (m_titleBox && m_titleBox->width() > 0)
        ? m_titleBox->geometry().left()
        : (m_reopenBtn->isVisible() ? m_reopenBtn->geometry().right() + 8 : 26);
    int right = width() - 26;
    if (m_filterBtn && m_filterBtn->isVisible() && m_filterBtn->x() > 0)
        right = m_filterBtn->geometry().left() - 8;
    else if (m_newBtn && m_newBtn->isVisible() && m_newBtn->x() > 0)
        right = m_newBtn->geometry().left() - 12;
    const int top = (height() - 40) / 2;
    return QRect(left, top, std::max(80, right - left), 40);
}

void HomeHeader::setSearchProgress(qreal progress) {
    m_searchProgress = std::clamp(progress, 0.0, 1.0);
    if (m_searchProgress <= 0.001) {
        m_searchField->hide();
        m_titleBox->show();
        m_searchBtn->setVisible(!m_settingsMode);
        m_emptyTrashBtn->setVisible(m_trashEmptyVisible && !m_settingsMode);
        return;
    }

    m_searchField->setGeometry(searchTargetRect());
    if (m_searchProgress >= 0.999) {
        m_titleBox->hide();
        m_searchBtn->hide();
        m_emptyTrashBtn->hide();
        m_searchField->setReveal(1.0, 1.0);
        m_searchField->show();
        m_searchField->raise();
        if (m_searchOpen && !m_search->hasFocus())
            m_search->setFocus(Qt::ShortcutFocusReason);
        return;
    }

    m_titleBox->show();
    m_searchBtn->setVisible(!m_settingsMode);
    if (tokens::reducedMotion())
        m_searchField->setReveal(1.0, m_searchProgress);
    else
        m_searchField->setReveal(m_searchProgress, m_searchProgress);
    m_searchField->raise();
    m_searchField->show();
}

void HomeHeader::setSearchOpen(bool open) {
    if (open && m_settingsMode)
        return;
    if (open && m_searchOpen && m_searchProgress >= 0.999) {
        m_search->setFocus(Qt::ShortcutFocusReason);
        return;
    }
    if (!open && !m_searchOpen && m_searchProgress <= 0.001)
        return;

    m_searchOpen = open;
    if (m_titleOverlay)
        m_titleOverlay->cancel();

    if (!open) {
        m_search->clear();
        m_search->clearFocus();
    }

    if (!isVisible() || width() <= 0) {
        setSearchProgress(open ? 1.0 : 0.0);
        return;
    }

    if (!m_searchAnim)
        m_searchAnim = new QPropertyAnimation(this, "searchProgress", this);
    m_searchAnim->stop();
    if (open) {
        m_searchAnim->setDuration(tokens::motion::duration(tokens::motion::Base));
        m_searchAnim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
        m_searchAnim->setStartValue(m_searchProgress);
        m_searchAnim->setEndValue(1.0);
    } else {
        m_searchAnim->setDuration(tokens::motion::duration(tokens::motion::Fast));
        m_searchAnim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
        m_searchAnim->setStartValue(m_searchProgress);
        m_searchAnim->setEndValue(0.0);
    }
    m_searchAnim->start();
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
#ifdef Q_OS_WIN
    if (event->type() == QEvent::Paint && watched && watched->property("winKind").isValid()) {
        auto* btn = qobject_cast<QWidget*>(watched);
        if (btn) {
            const bool result = QWidget::eventFilter(watched, event);
            QPainter p(btn);
            p.setRenderHint(QPainter::Antialiasing, true);
            const int kind = watched->property("winKind").toInt();
            const bool hovered = btn->underMouse();
            const QColor fg = (kind == 2 && hovered) ? QColor(Qt::white) : m_pal.text;
            p.setPen(QPen(fg, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            const QPointF c = QRectF(btn->rect()).center();
            if (kind == 0) {
                p.drawLine(QPointF(c.x() - 5.0, c.y() + 1.0), QPointF(c.x() + 5.0, c.y() + 1.0));
            } else if (kind == 1) {
                const bool maxed = window() && window()->isMaximized();
                if (maxed) {
                    p.drawRect(QRectF(c.x() - 4.5, c.y() - 2.5, 7.0, 7.0));
                    p.drawLine(QPointF(c.x() - 2.0, c.y() - 4.5), QPointF(c.x() + 4.5, c.y() - 4.5));
                    p.drawLine(QPointF(c.x() + 4.5, c.y() - 4.5), QPointF(c.x() + 4.5, c.y() + 2.0));
                } else {
                    p.drawRoundedRect(QRectF(c.x() - 4.5, c.y() - 4.5, 9.0, 9.0), 1.5, 1.5);
                }
            } else {
                p.drawLine(QPointF(c.x() - 4.2, c.y() - 4.2), QPointF(c.x() + 4.2, c.y() + 4.2));
                p.drawLine(QPointF(c.x() + 4.2, c.y() - 4.2), QPointF(c.x() - 4.2, c.y() + 4.2));
            }
            return result;
        }
    }
#endif
    return QWidget::eventFilter(watched, event);
}

void HomeHeader::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    m_reopenBtn->setTheme(pal, dark);
    m_emptyTrashBtn->setTheme(pal, dark);
    m_searchBtn->setTheme(pal, dark);
    m_filterBtn->setTheme(pal, dark);
    m_escBtn->setTheme(dark);
    m_searchField->scope->pal = pal;
    m_searchField->scope->dark = dark;
    m_searchField->scope->update();
    m_newBtn->setPalette(pal);
    m_searchField->pal = pal;
    m_searchField->update();

    for (const auto& sep : m_sepLabels) {
        if (sep)
            sep->setPixmap(uiIcon(QStringLiteral("chevR"), pal.faint, 15).pixmap(15, 15));
    }
    for (const auto& lbl : m_mutedLabels) {
        if (lbl)
            lbl->setStyleSheet(QStringLiteral("color: %1;").arg(pal.muted.name(QColor::HexArgb)));
    }
    update();
}

void HomeHeader::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(QRect(0, height() - 1, width(), 1), m_pal.border);
}

void HomeHeader::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_searchProgress > 0.001 && m_searchField)
        m_searchField->setGeometry(searchTargetRect());
}

void HomeHeader::mousePressEvent(QMouseEvent* event) {
#ifdef Q_OS_WIN
    if (event->button() == Qt::LeftButton && !childAt(event->pos())) {
        if (QWidget* w = window()) {
            if (QWindow* wh = w->windowHandle()) {
                wh->startSystemMove();
                return;
            }
        }
    }
#endif
    QWidget::mousePressEvent(event);
}

void HomeHeader::mouseDoubleClickEvent(QMouseEvent* event) {
#ifdef Q_OS_WIN
    if (event->button() == Qt::LeftButton && !childAt(event->pos())) {
        if (QWidget* w = window()) {
            if (w->isMaximized())
                w->showNormal();
            else
                w->showMaximized();
            return;
        }
    }
#endif
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace finily

#include "HomeHeader.moc"
