#include "engine/UpdateManager.h"

#include "core/Utils.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QVersionNumber>

#include <algorithm>
#include <utility>

namespace finily {
namespace {

constexpr qint64 kAutoCheckIntervalSecs = 86400; // 24 h

QVersionNumber parseVersionParts(QString raw, QString* prereleaseOut = nullptr) {
    raw = raw.trimmed();
    if (raw.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        raw.remove(0, 1);
    const qsizetype plus = raw.indexOf(QLatin1Char('+'));
    if (plus >= 0)
        raw = raw.left(plus);
    const qsizetype dash = raw.indexOf(QLatin1Char('-'));
    QString prerelease;
    if (dash >= 0) {
        prerelease = raw.mid(dash + 1);
        raw = raw.left(dash);
    }
    if (prereleaseOut)
        *prereleaseOut = prerelease;
    if (raw.isEmpty() || (dash >= 0 && prerelease.isEmpty()))
        return {};
    qsizetype suffixIndex = 0;
    const QVersionNumber parsed = QVersionNumber::fromString(raw, &suffixIndex);
    if (parsed.isNull() || suffixIndex != raw.size())
        return {};
    return parsed;
}

bool isAsciiDigits(const QString& s) {
    if (s.isEmpty())
        return false;
    for (QChar ch : s) {
        if (ch.unicode() < '0' || ch.unicode() > '9')
            return false;
    }
    return true;
}

int compareNumericStrings(QString a, QString b) {
    auto trimLeadingZeros = [](QString& s) {
        qsizetype idx = 0;
        while (idx + 1 < s.size() && s.at(idx) == QLatin1Char('0'))
            ++idx;
        if (idx > 0)
            s.remove(0, idx);
    };
    trimLeadingZeros(a);
    trimLeadingZeros(b);
    if (a.size() != b.size())
        return a.size() < b.size() ? -1 : 1;
    return a.compare(b, Qt::CaseSensitive);
}

int comparePrerelease(const QString& a, const QString& b) {
    const QStringList partsA = a.split(QLatin1Char('.'));
    const QStringList partsB = b.split(QLatin1Char('.'));
    const qsizetype count = std::min(partsA.size(), partsB.size());
    for (qsizetype i = 0; i < count; ++i) {
        const QString& pa = partsA.at(i);
        const QString& pb = partsB.at(i);
        const bool numA = isAsciiDigits(pa);
        const bool numB = isAsciiDigits(pb);
        if (numA && numB) {
            const int cmp = compareNumericStrings(pa, pb);
            if (cmp != 0)
                return cmp;
        } else if (numA != numB) {
            return numA ? -1 : 1;
        } else {
            const int cmp = pa.compare(pb, Qt::CaseSensitive);
            if (cmp != 0)
                return cmp;
        }
    }
    if (partsA.size() == partsB.size())
        return 0;
    return partsA.size() < partsB.size() ? -1 : 1;
}

bool isValidSha256Hex(const QString& value) {
    if (value.size() != 64)
        return false;
    for (QChar ch : value) {
        const ushort u = ch.unicode();
        const bool isDigit = (u >= '0' && u <= '9');
        const bool isHexLower = (u >= 'a' && u <= 'f');
        if (!isDigit && !isHexLower)
            return false;
    }
    return true;
}

QNetworkRequest makeGitHubRequest(const QUrl& url) {
    QNetworkRequest req(url);
    const QString ver = QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("0.2.0")
        : QCoreApplication::applicationVersion();
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Finily/%1").arg(ver));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);
    return req;
}

} // namespace

bool isTrustedUpdateUrl(const QUrl& url) {
    if (!url.isValid() || url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0)
        return false;
    if (!url.userInfo().isEmpty())
        return false;
    if (url.port() != -1 && url.port() != 443)
        return false;
    const QString host = url.host().toLower();
    if (host.isEmpty())
        return false;
    if (host == QLatin1String("github.com")) {
        const QString normalizedPath = url.adjusted(QUrl::NormalizePathSegments).path();
        return url.path().startsWith(QLatin1String("/Yorditry2/Finily/"), Qt::CaseInsensitive) &&
               normalizedPath.startsWith(QLatin1String("/Yorditry2/Finily/"), Qt::CaseInsensitive);
    }
    return host == QLatin1String("objects.githubusercontent.com") ||
           host == QLatin1String("release-assets.githubusercontent.com") ||
           host == QLatin1String("github-releases.githubusercontent.com");
}

bool isSafeInstallerAssetName(const QString& asset) {
    if (asset.isEmpty() || asset != asset.trimmed())
        return false;
    if (asset == QLatin1String(".") || asset == QLatin1String(".."))
        return false;
    for (QChar ch : asset) {
        const ushort u = ch.unicode();
        if (u < 0x20 || ch == QLatin1Char('/') || ch == QLatin1Char('\\') ||
            ch == QLatin1Char(':')) {
            return false;
        }
    }
    return QFileInfo(asset).fileName() == asset;
}

bool isNewerVersion(const QString& candidateVersion, const QString& currentVersion) {
    QString candPre;
    QString curPre;
    const QVersionNumber cand = parseVersionParts(candidateVersion, &candPre);
    const QVersionNumber cur = parseVersionParts(currentVersion, &curPre);
    if (cand.isNull() || cur.isNull())
        return false;
    const int cmp = QVersionNumber::compare(cand, cur);
    if (cmp != 0)
        return cmp > 0;
    if (candPre.isEmpty() && !curPre.isEmpty())
        return true;
    if (!candPre.isEmpty() && curPre.isEmpty())
        return false;
    if (!candPre.isEmpty() && !curPre.isEmpty())
        return comparePrerelease(candPre, curPre) > 0;
    return false;
}

std::optional<UpdateInfo> parseUpdateManifest(const QByteArray& jsonBytes) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;

    const QJsonObject obj = doc.object();
    const QString version = obj.value(QLatin1String("version")).toString().trimmed();
    if (parseVersionParts(version).isNull())
        return std::nullopt;

    const QString releaseUrl = obj.value(QLatin1String("release_url")).toString().trimmed();
    if (!isTrustedUpdateUrl(QUrl(releaseUrl)))
        return std::nullopt;

    UpdateInfo info;
    info.version = version;
    info.tag = obj.value(QLatin1String("tag")).toString().trimmed();
    info.publishedAt = obj.value(QLatin1String("published_at")).toString().trimmed();
    info.releaseUrl = releaseUrl;

    if (obj.contains(QLatin1String("windows"))) {
        const QJsonValue winVal = obj.value(QLatin1String("windows"));
        if (!winVal.isObject())
            return std::nullopt;
        const QJsonObject winObj = winVal.toObject();
        const QString asset = winObj.value(QLatin1String("asset")).toString().trimmed();
        const QString url = winObj.value(QLatin1String("url")).toString().trimmed();
        const QString sha = winObj.value(QLatin1String("sha256")).toString().trimmed().toLower();

        if (!isSafeInstallerAssetName(asset))
            return std::nullopt;
        if (!isTrustedUpdateUrl(QUrl(url)))
            return std::nullopt;
        if (!isValidSha256Hex(sha))
            return std::nullopt;

        info.installerAsset = asset;
        info.installerUrl = url;
        info.installerSha256 = sha;
    }

    info.valid = true;
    return info;
}

QString computeFileSha256(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex()).toLower();
}

UpdateManager::UpdateManager(QObject* parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this)) {
    cleanupCachedInstallers();
}

UpdateManager::~UpdateManager() {
    cleanupActiveDownload();
    if (QNetworkReply* reply = m_checkReply.data()) {
        m_checkReply.clear();
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

QString UpdateManager::defaultManifestUrl() {
    return QStringLiteral(
        "https://github.com/Yorditry2/Finily/releases/latest/download/update-manifest.json");
}

QString UpdateManager::updatesCacheDir() {
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(cache).filePath(QStringLiteral("updates"));
}

bool UpdateManager::autoCheckEnabled() const {
    return readAppSetting(QStringLiteral("updates/autoCheck"), true).toBool();
}

void UpdateManager::setAutoCheckEnabled(bool enabled) {
    writeAppSetting(QStringLiteral("updates/autoCheck"), enabled);
}

void UpdateManager::skipVersion(const QString& version) {
    writeAppSetting(QStringLiteral("updates/skippedVersion"), version.trimmed());
}

bool UpdateManager::isDownloading() const {
    return !m_downloadReply.isNull();
}

void UpdateManager::cleanupCachedInstallers() {
    const QDir dir(updatesCacheDir());
    if (!dir.exists())
        return;
    const QFileInfoList stale = dir.entryInfoList(
        {QStringLiteral("*.exe"), QStringLiteral("*.part")}, QDir::Files);
    for (const QFileInfo& entry : stale)
        QFile::remove(entry.absoluteFilePath());
}

void UpdateManager::cleanupActiveDownload() {
    if (QNetworkReply* reply = m_downloadReply.data()) {
        m_downloadReply.clear();
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    if (!m_downloadPartPath.isEmpty()) {
        QFile::remove(m_downloadPartPath);
        m_downloadPartPath.clear();
    }
    m_downloadFinalPath.clear();
    m_pendingInfo = UpdateInfo{};
    m_downloadCancelled = false;
    m_downloadWriteFailed = false;
    m_downloadWriteError.clear();
}

void UpdateManager::checkForUpdates(bool manual) {
    if (m_checkReply)
        return;

    if (!manual) {
        if (!autoCheckEnabled())
            return;
        const qint64 lastCheck = readAppSetting(QStringLiteral("updates/lastCheckUtc"), 0).toLongLong();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (lastCheck > 0 && (now - lastCheck) < kAutoCheckIntervalSecs)
            return;
    }

    QNetworkReply* reply = m_nam->get(makeGitHubRequest(QUrl(defaultManifestUrl())));
    m_checkReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        if (m_checkReply != reply) {
            reply->deleteLater();
            return;
        }
        m_checkReply.clear();

        const auto netErr = reply->error();
        const QString errStr = reply->errorString();
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QUrl finalUrl = reply->url();
        const QByteArray payload = (netErr == QNetworkReply::NoError) ? reply->readAll() : QByteArray();
        reply->deleteLater();

        if (netErr != QNetworkReply::NoError) {
            emit checkFailed(errStr, manual);
            return;
        }
        if (status.isValid() && status.toInt() != 200) {
            emit checkFailed(tr("Respuesta HTTP %1 del servidor de actualizaciones.").arg(status.toInt()),
                             manual);
            return;
        }
        if (!isTrustedUpdateUrl(finalUrl)) {
            emit checkFailed(tr("Origen de actualización no confiable."), manual);
            return;
        }

        const auto parsed = parseUpdateManifest(payload);
        if (!parsed || !parsed->valid) {
            emit checkFailed(tr("Manifiesto de actualización inválido."), manual);
            return;
        }

        auto st = appSettings();
        st->setValue(QStringLiteral("updates/lastCheckUtc"), QDateTime::currentSecsSinceEpoch());

        const QString currentVersion = QCoreApplication::applicationVersion();
        if (isNewerVersion(parsed->version, currentVersion)) {
            const QString skipped =
                st->value(QStringLiteral("updates/skippedVersion")).toString().trimmed();
            if (!manual && !skipped.isEmpty() && skipped == parsed->version)
                return;
            emit updateAvailable(*parsed, manual);
        } else {
            emit upToDate(manual);
        }
    });
}

void UpdateManager::downloadUpdate(const UpdateInfo& info) {
    if (!info.valid || !isSafeInstallerAssetName(info.installerAsset) ||
        !isTrustedUpdateUrl(QUrl(info.installerUrl)) || !isValidSha256Hex(info.installerSha256)) {
        emit downloadFailed(tr("Metadatos del instalador inválidos o incompletos."));
        return;
    }

    if (m_downloadReply || m_downloadFile)
        cleanupActiveDownload();

    const QString cacheDir = updatesCacheDir();
    if (!QDir().mkpath(cacheDir)) {
        emit downloadFailed(tr("No se pudo crear la carpeta temporal de actualizaciones."));
        return;
    }

    m_downloadFinalPath = QDir(cacheDir).filePath(info.installerAsset);
    m_downloadPartPath = m_downloadFinalPath + QStringLiteral(".part");
    m_pendingInfo = info;
    m_downloadCancelled = false;
    m_downloadWriteFailed = false;
    m_downloadWriteError.clear();

    auto* file = new QFile(m_downloadPartPath, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = file->errorString();
        delete file;
        m_downloadFinalPath.clear();
        m_downloadPartPath.clear();
        m_pendingInfo = UpdateInfo{};
        emit downloadFailed(tr("No se pudo escribir el instalador temporal: %1").arg(err));
        return;
    }
    m_downloadFile = file;

    QNetworkRequest req = makeGitHubRequest(QUrl(info.installerUrl));
    req.setTransferTimeout(60000);
    QNetworkReply* reply = m_nam->get(req);
    m_downloadReply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_downloadReply != reply || !m_downloadFile || m_downloadWriteFailed)
            return;
        const QByteArray chunk = reply->readAll();
        if (!chunk.isEmpty() && m_downloadFile->write(chunk) != chunk.size()) {
            m_downloadWriteFailed = true;
            m_downloadWriteError = m_downloadFile->errorString();
            if (m_downloadWriteError.isEmpty())
                m_downloadWriteError = tr("Error al escribir el instalador en disco.");
            m_downloadCancelled = false;
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 received, qint64 total) {
                if (m_downloadReply == reply)
                    emit downloadProgress(received, total);
            });

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_downloadReply != reply) {
            reply->deleteLater();
            return;
        }
        m_downloadReply.clear();

        const bool cancelled = m_downloadCancelled;
        bool writeFailed = m_downloadWriteFailed;
        QString writeError = m_downloadWriteError;
        m_downloadCancelled = false;
        m_downloadWriteFailed = false;
        m_downloadWriteError.clear();

        const auto netErr = reply->error();
        const QString errStr = reply->errorString();
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QUrl finalUrl = reply->url();
        const QString partPath = std::exchange(m_downloadPartPath, QString{});
        const QString finalPath = std::exchange(m_downloadFinalPath, QString{});
        const UpdateInfo infoCopy = std::exchange(m_pendingInfo, UpdateInfo{});

        if (m_downloadFile) {
            if (!writeFailed && netErr == QNetworkReply::NoError && !cancelled &&
                (!status.isValid() || status.toInt() == 200)) {
                const QByteArray tail = reply->readAll();
                if (!tail.isEmpty() && m_downloadFile->write(tail) != tail.size()) {
                    writeFailed = true;
                    writeError = m_downloadFile->errorString();
                } else if (!m_downloadFile->flush()) {
                    writeFailed = true;
                    writeError = m_downloadFile->errorString();
                }
            }
            m_downloadFile->close();
            delete m_downloadFile;
            m_downloadFile = nullptr;
        }
        reply->deleteLater();

        if (writeFailed) {
            QFile::remove(partPath);
            emit downloadFailed(writeError.isEmpty()
                                    ? tr("Error al escribir el instalador en disco.")
                                    : writeError);
            return;
        }
        if (cancelled) {
            QFile::remove(partPath);
            emit downloadCancelled();
            return;
        }
        if (netErr != QNetworkReply::NoError) {
            QFile::remove(partPath);
            emit downloadFailed(errStr);
            return;
        }
        if (status.isValid() && status.toInt() != 200) {
            QFile::remove(partPath);
            emit downloadFailed(tr("Respuesta HTTP %1 al descargar el instalador.").arg(status.toInt()));
            return;
        }
        if (!isTrustedUpdateUrl(finalUrl)) {
            QFile::remove(partPath);
            emit downloadFailed(tr("Descarga bloqueada desde un dominio no confiable."));
            return;
        }
        if (computeFileSha256(partPath) != infoCopy.installerSha256) {
            QFile::remove(partPath);
            emit downloadFailed(tr("La verificación SHA-256 falló. Se descartó el archivo."));
            return;
        }

        QFile::remove(finalPath);
        if (!QFile::rename(partPath, finalPath)) {
            QFile::remove(partPath);
            emit downloadFailed(tr("No se pudo preparar el instalador descargado."));
            return;
        }

        emit installerReady(finalPath, infoCopy);
    });
}

void UpdateManager::cancelDownload() {
    if (!m_downloadReply && !m_downloadFile)
        return;
    m_downloadCancelled = true;
    cleanupActiveDownload();
    emit downloadCancelled();
}

} // namespace finily
