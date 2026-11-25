#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace finily {

struct UpdateInfo {
    QString version;
    QString tag;
    QString publishedAt;
    QString releaseUrl;
    QString installerAsset;
    QString installerUrl;
    QString installerSha256;
    bool valid = false;
};

bool isTrustedUpdateUrl(const QUrl& url);
bool isNewerVersion(const QString& candidateVersion, const QString& currentVersion);
std::optional<UpdateInfo> parseUpdateManifest(const QByteArray& jsonBytes);
QString computeFileSha256(const QString& filePath);

class UpdateManager final : public QObject {
    Q_OBJECT

public:
    explicit UpdateManager(QObject* parent = nullptr);
    ~UpdateManager() override;

    static QString defaultManifestUrl();
    static QString updatesCacheDir();

    bool autoCheckEnabled() const;
    void setAutoCheckEnabled(bool enabled);
    void skipVersion(const QString& version);

    bool isDownloading() const;

    void checkForUpdates(bool manual = false);
    void downloadUpdate(const UpdateInfo& info);
    void cancelDownload();

signals:
    void updateAvailable(const finily::UpdateInfo& info, bool manual);
    void upToDate(bool manual);
    void checkFailed(const QString& error, bool manual);

    void downloadProgress(qint64 received, qint64 total);
    void installerReady(const QString& filePath, const finily::UpdateInfo& info);
    void downloadFailed(const QString& error);
    void downloadCancelled();

private:
    void cleanupCachedInstallers();

    QNetworkAccessManager* m_nam = nullptr;
    QNetworkReply* m_checkReply = nullptr;
    QNetworkReply* m_downloadReply = nullptr;
    QFile* m_downloadFile = nullptr;
    QString m_downloadPartPath;
    QString m_downloadFinalPath;
    UpdateInfo m_pendingInfo;
    bool m_downloadCancelled = false;
};

} // namespace finily
