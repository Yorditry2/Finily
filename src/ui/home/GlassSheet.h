#pragma once

#include "ui/home/HomeTheme.h"

#include <QPair>
#include <QRect>
#include <QVector>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QGraphicsOpacityEffect;
class QLabel;
class QLineEdit;
class QPropertyAnimation;
class QToolButton;
class QVBoxLayout;

namespace finily {

// Hoja flotante Liquid Glass base (patrón Freenotes), hija directa de
// HomeScreen (nunca QDialog ni Qt::Popup) para mantener compatibilidad Wayland+Wacom.
class GlassSheet : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit GlassSheet(QWidget* parent = nullptr);

    qreal progress() const { return m_progress; }
    void setProgress(qreal p);
    void repositionOnResize();
    void closeSheet(bool accepted = false);

signals:
    void cancelled();
    void closed();

protected:
    void configureHeader(const QString& title, const QString& confirmText);
    void setCardWidth(int width);
    // Modo tarjeta lateral (mockup 04/06): 344 px a la derecha bajo la cabecera,
    // sin cabecera propia ni telón, entra deslizándose desde la derecha.
    void setSideCard(bool side);
    void setBodyMargins(int l, int t, int r, int b);
    void setHeaderVisible(bool visible);
    int bodySpacing() const;
    QVBoxLayout* bodyLayout() const { return m_bodyLayout; }
    QWidget* cardWidget() const { return m_cardWidget; }
    QRect cardRect() const { return m_cardRect; }
    const HomePalette& palette() const { return m_pal; }
    bool isDarkMode() const { return m_dark; }
    bool isClosing() const { return m_closing; }

    void openSheet(const HomePalette& pal, bool dark);
    virtual bool validateBeforeAccept() { return true; }
    virtual void onConfirmed() = 0;
    virtual void onClosing() {}

    // Helpers de formulario compartidos
    QLabel* makeSectionLabel(const QString& text);
    QLineEdit* makeNameField(const QString& placeholder, const QString& initialText);
    QWidget* makeColorSwatchRow(int initialIndex, const std::function<void(int)>& onSelect,
                                QButtonGroup** outGroup = nullptr);
    QWidget* makeSymbolIconRow(int initialIndex, const std::function<void(int)>& onSelect,
                               QButtonGroup** outGroup = nullptr);
    QWidget* makeThemeCardRow(bool boardOrder, ThemeId initialTheme,
                              const std::function<void(ThemeId)>& onSelect,
                              QButtonGroup** outGroup = nullptr);
    QWidget* makePillRow(const QVector<QPair<QString, int>>& options, int initialValue,
                         const std::function<void(int)>& onSelect,
                         QButtonGroup** outGroup = nullptr,
                         QVector<QToolButton*>* outButtons = nullptr);

    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateCardGeometry(bool grabBackdrop);
    QString sheetStylesheet() const;

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    bool m_closing = false;
    bool m_pendingAccept = false;
    bool m_grabbingBackdrop = false;
    int m_cardWidth = 430;
    QRect m_cardRect;
    QImage m_blurredBg;
    qreal m_progress = 1.0;

    bool m_side = false;
    QWidget* m_header = nullptr;
    QWidget* m_cardWidget = nullptr;
    QGraphicsOpacityEffect* m_cardOpacity = nullptr;
    QToolButton* m_cancelBtn = nullptr;
    QLabel* m_titleLabel = nullptr;
    QToolButton* m_confirmBtn = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
    QPropertyAnimation* m_anim = nullptr;
};

} // namespace finily
