#pragma once

#include "license/feature_gate.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace license {

struct ActivationCodePayload {
    int version = 0;
    QString product;
    QString serial;
    QString watermark;
    QString edition;
    QString deviceFingerprint;
    QStringList featureCodes;
    QString issuedAt;
    QString expireAt;
    QString signature;
};

struct ActivationCodeParseResult {
    bool ok = false;
    QString errorMessage;
    QString prefix;
    QString originalPayloadJson;
    QString check8;
    ActivationCodePayload payload;
};

struct ActivationValidationResult {
    bool ok = false;
    QString errorMessage;
    QList<Feature> resolvedFeatures;
};

class ActivationCodeService final {
public:
    ActivationCodeParseResult parseActivationCode(const QString& code) const;
    ActivationValidationResult validateActivationCode(const ActivationCodePayload& payload,
                                                      const QString& originalPayloadJson,
                                                      const QString& expectedCheck8,
                                                      const QString& currentDeviceFingerprint) const;
    QByteArray buildLicenseFileContent(const ActivationCodePayload& payload,
                                       const QList<Feature>& features,
                                       const QString& activationPrefix,
                                       const QString& activationCheck8) const;

    static QString maskActivationCodeForLog(const QString& code);

private:
    static QString calculateCrc32UpperHex(const QByteArray& data);
    static bool parsePayloadObject(const QByteArray& decodedPayloadBytes,
                                   ActivationCodePayload* payload,
                                   QString* originalPayloadJson,
                                   QString* errorMessage);
    static bool decodePayloadBase64Url(const QString& payloadBase64Url, QByteArray* decoded, QString* errorMessage);
    static bool verifyActivationSignature(const ActivationCodePayload& payload,
                                          const QString& originalPayloadJson,
                                          const QString& check8,
                                          QString* errorMessage);
    static bool decryptActivationPayload(QString* payloadJson, QString* errorMessage);
    static bool validateExpiration(const QString& expireAt, QString* errorMessage);
    static bool validateFeatureSet(const QList<Feature>& features, QString* errorMessage);
};

}  // namespace license

