#pragma once

#include "ui/home/HomeTheme.h"

#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

namespace finily {

// Tarjeta compacta de actualización en el pie del sidebar (mockup 09, escenas 06-07).
class UpdateBanner : public QWidget {
    Q_OBJECT

public:
    enum class State { Hidden, Ready, Downloading, ReadyToInstall, Error };

    explicit UpdateBanner(QWidget* parent = nullptr);

    State state() const { return m_state; }
    void applyTheme(const HomePalette& pal, bool dark);
    void showUpdate(const QString& version, bool canInstallInApp, const QString& summary = {});
    void setDownloadProgress(int percent);
    void showReadyToInstall(const QString& version);
    void showError(const QString& msg);
    void resetState();
    void hideBanner();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void installRequested();
    void cancelRequested();
    void releaseNotesRequested();
    void dismissed();
    void restartNowRequested();
    void laterRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class HitAction { Install, Notes, Cancel, Dismiss, RestartNow, Later };
    struct HitButton {
        QRectF rect;
        QString label;
        HitAction action = HitAction::Install;
        bool primary = false;
        bool iconOnly = false;
    };

    QFont uiFont(qreal px, int weight) const;
    void relayout(int availableWidth);
    int hitIndexAt(const QPointF& pos) const;

    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    State m_state = State::Hidden;
    QString m_version;
    QString m_summary;
    QString m_error;
    bool m_canInstall = false;
    int m_progress = 0;

    QRectF m_cardRect;
    QRectF m_iconRect;
    QRectF m_titleRect;
    QRectF m_percentRect;
    QRectF m_bodyRect;
    QRectF m_pbarRect;
    QString m_titleText;
    QString m_bodyText;
    QString m_percentText;
    QVector<HitButton> m_hits;
    int m_hoveredHit = -1;
    int m_totalHeight = 0;
};

} // namespace finily
