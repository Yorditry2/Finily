#pragma once

#include "core/Types.h"

#include <QImage>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace finily {

struct ExtractedContent {
    QVector<ShapeItem> shapes;
    QVector<TextItem> texts;
};

bool isValidGeminiModelName(const QString& modelName);
std::optional<QNetworkRequest> buildVisionRequest(const QString& modelName, const QString& apiKeyValue);

// Parsea la respuesta JSON de Gemini (tanto el envoltorio completo
// candidates[0].content.parts[0].text como el JSON directo) y convierte
// figuras y textos normalizados [0..1000] al espacio del lienzo dentro de targetRect.
std::optional<ExtractedContent> parseVisionExtraction(const QByteArray& jsonBytes,
                                                      const QRectF& targetRect,
                                                      const QColor& defaultInk,
                                                      double defaultStrokeSize);

class VisionExtractor : public QObject {
    Q_OBJECT

public:
    explicit VisionExtractor(QObject* parent = nullptr);

    static QString apiKey();
    static void setApiKey(const QString& key);
    static QString model();
    static void setModel(const QString& model);
    static QStringList supportedModels();

    bool isBusy() const { return !m_activeReply.isNull(); }
    void extractFromImage(const QImage& image,
                          const QRectF& targetRect,
                          const QColor& defaultInk,
                          double defaultStrokeSize);
    void cancel();

signals:
    void extractionFinished(const finily::ExtractedContent& content);
    void extractionFailed(const QString& error);

private:
    void sendRequest(const QString& modelName,
                     const QString& apiKey,
                     const QByteArray& requestBody,
                     const QRectF& targetRect,
                     const QColor& defaultInk,
                     double defaultStrokeSize,
                     bool allowFallback);

    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_activeReply;
};

} // namespace finily
