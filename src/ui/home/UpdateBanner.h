#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace finily {

class UpdateBanner : public QWidget {
    Q_OBJECT
public:
    explicit UpdateBanner(QWidget* parent = nullptr);

    void showUpdate(const QString& version, bool canInstallInApp);
    void setDownloadProgress(int percent);
    void resetState();

signals:
    void installRequested();
    void cancelRequested();
    void releaseNotesRequested();
    void dismissed();

private:
    QLabel* m_label = nullptr;
    QToolButton* m_notesBtn = nullptr;
    QToolButton* m_primaryBtn = nullptr;
    QToolButton* m_cancelBtn = nullptr;
    QToolButton* m_dismissBtn = nullptr;
    bool m_canInstall = false;
};

} // namespace finily
