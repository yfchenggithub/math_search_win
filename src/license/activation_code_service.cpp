#include "license/activation_code_service.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace license {
namespace {

constexpr quint32 kCrc32Polynomial = 0xEDB88320u;

QString normalizeFeatureCode(const QString& value)
{
    return value.trimmed().toLower();
}

bool isUpperHex8(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9A-F]{8}$"));
    return pattern.match(value).hasMatch();
}

}  // namespace

ActivationCodeParseResult ActivationCodeService::parseActivationCode(const QString& code) const
{
    ActivationCodeParseResult result;
    const QString normalizedCode = code.trimmed();

    if (normalizedCode.isEmpty()) {
        result.errorMessage = QStringLiteral("激活码不能为空。");
        return result;
    }

    const QString maskedCode = maskActivationCodeForLog(normalizedCode);
    LOG_INFO(LogCategory::Config,
             QStringLiteral("activation code parse begin code=%1").arg(maskedCode));

    const QStringList parts = normalizedCode.split(QLatin1Char('.'), Qt::KeepEmptyParts);
    if (parts.size() != 3) {
        result.errorMessage = QStringLiteral("激活码格式错误，应为三段式：MSW1.payload.check8。");
        return result;
    }

    result.prefix = parts.at(0).trimmed();
    const QString payloadBase64Url = parts.at(1).trimmed();
    result.check8 = parts.at(2).trimmed().toUpper();

    if (result.prefix != QStringLiteral("MSW1")) {
        result.errorMessage = QStringLiteral("激活码前缀无效，当前仅支持 MSW1。");
        return result;
    }
    if (payloadBase64Url.isEmpty()) {
        result.errorMessage = QStringLiteral("激活码 payload 为空。");
        return result;
    }
    if (!isUpperHex8(result.check8)) {
        result.errorMessage = QStringLiteral("激活码 check8 无效，需为 8 位大写十六进制。");
        return result;
    }

    QByteArray decodedPayloadBytes;
    if (!decodePayloadBase64Url(payloadBase64Url, &decodedPayloadBytes, &result.errorMessage)) {
        LOG_WARN(LogCategory::Config,
                 QStringLiteral("activation code decode failed code=%1 reason=%2")
                     .arg(maskedCode, result.errorMessage));
        return result;
    }

    if (!parsePayloadObject(decodedPayloadBytes, &result.payload, &result.originalPayloadJson, &result.errorMessage)) {
        LOG_WARN(LogCategory::Config,
                 QStringLiteral("activation code payload parse failed code=%1 reason=%2")
                     .arg(maskedCode, result.errorMessage));
        return result;
    }

    result.ok = true;
    LOG_INFO(LogCategory::Config,
             QStringLiteral("activation code parse success code=%1 serial=%2 device=%3")
                 .arg(maskedCode, result.payload.serial, result.payload.deviceFingerprint));
    return result;
}

ActivationValidationResult ActivationCodeService::validateActivationCode(const ActivationCodePayload& payload,
                                                                         const QString& originalPayloadJson,
                                                                         const QString& expectedCheck8,
                                                                         const QString& currentDeviceFingerprint) const
{
    ActivationValidationResult result;

    const QString checkFromCode = expectedCheck8.trimmed().toUpper();
    const QString actualCheck = calculateCrc32UpperHex(originalPayloadJson.toUtf8());
    if (checkFromCode != actualCheck) {
        result.errorMessage = QStringLiteral("激活码校验失败（CRC32 不一致）。");
        return result;
    }

    QString signatureError;
    if (!verifyActivationSignature(payload, originalPayloadJson, checkFromCode, &signatureError)) {
        result.errorMessage = signatureError.trimmed().isEmpty() ? QStringLiteral("激活码签名校验失败。")
                                                                 : signatureError.trimmed();
        return result;
    }

    if (payload.version != 1) {
        result.errorMessage = QStringLiteral("激活码版本不受支持。");
        return result;
    }
    if (payload.product != QStringLiteral("msw")) {
        result.errorMessage = QStringLiteral("激活码产品标识无效。");
        return result;
    }
    if (payload.edition != QStringLiteral("full")) {
        result.errorMessage = QStringLiteral("当前仅支持 full 正式版激活码。");
        return result;
    }

    const QString currentDevice = currentDeviceFingerprint.trimmed();
    if (currentDevice.isEmpty()) {
        result.errorMessage = QStringLiteral("无法获取当前设备码，请稍后重试。");
        return result;
    }
    if (payload.deviceFingerprint.trimmed() != currentDevice) {
        result.errorMessage = QStringLiteral("激活码绑定设备与当前设备不匹配。");
        return result;
    }

    QSet<Feature> deduped;
    for (const QString& rawCode : payload.featureCodes) {
        const QString code = normalizeFeatureCode(rawCode);
        if (code.isEmpty()) {
            continue;
        }

        const std::optional<Feature> feature = FeatureGate::featureFromShortCode(code);
        if (!feature.has_value()) {
            LOG_WARN(LogCategory::Config,
                     QStringLiteral("activation code unknown feature short_code=%1 ignored").arg(code));
            continue;
        }
        if (!deduped.contains(feature.value())) {
            deduped.insert(feature.value());
            result.resolvedFeatures.push_back(feature.value());
        }
    }

    if (result.resolvedFeatures.isEmpty()) {
        result.errorMessage = QStringLiteral("激活码未包含可识别的功能集合。");
        return result;
    }

    QString expirationError;
    if (!validateExpiration(payload.expireAt, &expirationError)) {
        result.errorMessage = expirationError.trimmed().isEmpty() ? QStringLiteral("激活码已过期。")
                                                                  : expirationError.trimmed();
        return result;
    }

    QString featureSetError;
    if (!validateFeatureSet(result.resolvedFeatures, &featureSetError)) {
        result.errorMessage = featureSetError.trimmed().isEmpty() ? QStringLiteral("激活码功能集合校验失败。")
                                                                  : featureSetError.trimmed();
        return result;
    }

    result.ok = true;
    return result;
}

QByteArray ActivationCodeService::buildLicenseFileContent(const ActivationCodePayload& payload,
                                                          const QList<Feature>& features,
                                                          const QString& activationPrefix,
                                                          const QString& activationCheck8) const
{
    QStringList featureKeys = FeatureGate::featureKeysFromList(features);
    featureKeys.removeAll(QStringLiteral("unknown"));
    featureKeys.removeDuplicates();
    const QString featureList = featureKeys.join(QStringLiteral(","));

    const QString prefix = activationPrefix.trimmed().isEmpty() ? QStringLiteral("MSW1") : activationPrefix.trimmed();
    const QString check8 = activationCheck8.trimmed().toUpper();

    QStringList lines;
    lines << QStringLiteral("format=msw-license-v1");
    lines << QStringLiteral("product=math_search_win");
    lines << QStringLiteral("serial=%1").arg(payload.serial.trimmed());
    lines << QStringLiteral("watermark=%1").arg(payload.watermark.trimmed());
    lines << QStringLiteral("edition=%1").arg(payload.edition.trimmed());
    lines << QStringLiteral("device=%1").arg(payload.deviceFingerprint.trimmed());
    lines << QStringLiteral("features=%1").arg(featureList);
    lines << QStringLiteral("issued_at=%1").arg(payload.issuedAt.trimmed());
    lines << QStringLiteral("expire_at=%1").arg(payload.expireAt.trimmed());
    lines << QStringLiteral("issuer=offline-manual");
    lines << QStringLiteral("source=activation_code");
    lines << QStringLiteral("payload_ver=%1").arg(payload.version);
    lines << QStringLiteral("activation_prefix=%1").arg(prefix);
    lines << QStringLiteral("activation_check=%1").arg(check8);
    lines << QStringLiteral("status=valid");

    QString content = lines.join(QLatin1Char('\n'));
    content.append(QLatin1Char('\n'));
    return content.toUtf8();
}

QString ActivationCodeService::maskActivationCodeForLog(const QString& code)
{
    const QString normalized = code.trimmed();
    if (normalized.isEmpty()) {
        return QStringLiteral("<empty>");
    }
    if (normalized.size() <= 14) {
        return normalized;
    }
    return QStringLiteral("%1...%2")
        .arg(normalized.left(8), normalized.right(6));
}

QString ActivationCodeService::calculateCrc32UpperHex(const QByteArray& data)
{
    quint32 crc = 0xFFFFFFFFu;
    for (const unsigned char byte : data) {
        crc ^= static_cast<quint32>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const bool lsb = (crc & 1u) != 0u;
            crc >>= 1u;
            if (lsb) {
                crc ^= kCrc32Polynomial;
            }
        }
    }
    crc ^= 0xFFFFFFFFu;
    return QStringLiteral("%1")
        .arg(static_cast<qulonglong>(crc), 8, 16, QLatin1Char('0'))
        .toUpper();
}

bool ActivationCodeService::parsePayloadObject(const QByteArray& decodedPayloadBytes,
                                                ActivationCodePayload* payload,
                                                QString* originalPayloadJson,
                                                QString* errorMessage)
{
    if (payload == nullptr || originalPayloadJson == nullptr || errorMessage == nullptr) {
        return false;
    }

    *payload = {};
    *errorMessage = {};
    *originalPayloadJson = QString::fromUtf8(decodedPayloadBytes);
    if (originalPayloadJson->trimmed().isEmpty()) {
        *errorMessage = QStringLiteral("激活码 payload 为空。");
        return false;
    }

    QString payloadJson = *originalPayloadJson;
    if (!decryptActivationPayload(&payloadJson, errorMessage)) {
        if (errorMessage->trimmed().isEmpty()) {
            *errorMessage = QStringLiteral("激活码 payload 解密失败。");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payloadJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *errorMessage = QStringLiteral("激活码 payload JSON 解析失败。");
        return false;
    }

    const QJsonObject object = document.object();
    const QJsonValue vValue = object.value(QStringLiteral("v"));
    const QJsonValue pValue = object.value(QStringLiteral("p"));
    const QJsonValue sValue = object.value(QStringLiteral("s"));
    const QJsonValue wValue = object.value(QStringLiteral("w"));
    const QJsonValue eValue = object.value(QStringLiteral("e"));
    const QJsonValue dValue = object.value(QStringLiteral("d"));
    const QJsonValue fValue = object.value(QStringLiteral("f"));
    const QJsonValue iatValue = object.value(QStringLiteral("iat"));
    const QJsonValue expValue = object.value(QStringLiteral("exp"));
    const QJsonValue sigValue = object.value(QStringLiteral("sig"));

    if (!vValue.isDouble()) {
        *errorMessage = QStringLiteral("激活码字段 v 无效。");
        return false;
    }
    payload->version = vValue.toInt(-1);
    payload->product = pValue.toString().trimmed().toLower();
    payload->serial = sValue.toString().trimmed();
    payload->watermark = wValue.toString().trimmed();
    payload->edition = eValue.toString().trimmed().toLower();
    payload->deviceFingerprint = dValue.toString().trimmed();
    payload->issuedAt = iatValue.toString().trimmed();
    payload->expireAt = expValue.isString() ? expValue.toString().trimmed() : QString();
    payload->signature = sigValue.toString().trimmed();

    if (!fValue.isArray()) {
        *errorMessage = QStringLiteral("激活码字段 f 无效，必须为数组。");
        return false;
    }
    const QJsonArray featureArray = fValue.toArray();
    for (const QJsonValue& value : featureArray) {
        const QString featureCode = value.toString().trimmed().toLower();
        if (!featureCode.isEmpty()) {
            payload->featureCodes.push_back(featureCode);
        }
    }

    if (payload->version != 1) {
        *errorMessage = QStringLiteral("激活码版本不受支持（v 必须为 1）。");
        return false;
    }
    if (payload->product != QStringLiteral("msw")) {
        *errorMessage = QStringLiteral("激活码产品标识无效（p 必须为 msw）。");
        return false;
    }
    if (payload->serial.isEmpty()) {
        *errorMessage = QStringLiteral("激活码缺少授权编号（s）。");
        return false;
    }
    if (payload->watermark.isEmpty()) {
        *errorMessage = QStringLiteral("激活码缺少水印编号（w）。");
        return false;
    }
    if (payload->edition.isEmpty()) {
        *errorMessage = QStringLiteral("激活码缺少授权类型（e）。");
        return false;
    }
    if (payload->deviceFingerprint.isEmpty()) {
        *errorMessage = QStringLiteral("激活码缺少绑定设备码（d）。");
        return false;
    }
    if (payload->featureCodes.isEmpty()) {
        *errorMessage = QStringLiteral("激活码缺少功能集合（f）。");
        return false;
    }
    if (payload->issuedAt.isEmpty()) {
        *errorMessage = QStringLiteral("激活码缺少签发日期（iat）。");
        return false;
    }

    return true;
}

bool ActivationCodeService::decodePayloadBase64Url(const QString& payloadBase64Url,
                                                   QByteArray* decoded,
                                                   QString* errorMessage)
{
    if (decoded == nullptr || errorMessage == nullptr) {
        return false;
    }

    static const QRegularExpression allowedPattern(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (!allowedPattern.match(payloadBase64Url).hasMatch()) {
        *errorMessage = QStringLiteral("激活码 payload 含有非法字符。");
        return false;
    }

    QString normalized = payloadBase64Url;
    normalized.replace(QLatin1Char('-'), QLatin1Char('+'));
    normalized.replace(QLatin1Char('_'), QLatin1Char('/'));
    while (normalized.size() % 4 != 0) {
        normalized.append(QLatin1Char('='));
    }

    *decoded = QByteArray::fromBase64(normalized.toLatin1());
    if (decoded->isEmpty()) {
        *errorMessage = QStringLiteral("激活码 payload Base64Url 解码失败。");
        return false;
    }
    return true;
}

bool ActivationCodeService::verifyActivationSignature(const ActivationCodePayload&,
                                                      const QString&,
                                                      const QString&,
                                                      QString* errorMessage)
{
    Q_UNUSED(errorMessage);
    // TODO: 接入真实签名体系（例如 Ed25519/ECDSA）替代当前 CRC32 轻校验。
    return true;
}

bool ActivationCodeService::decryptActivationPayload(QString* payloadJson, QString* errorMessage)
{
    Q_UNUSED(errorMessage);
    if (payloadJson == nullptr) {
        return false;
    }
    // TODO: 接入 payload 加密协议（当前阶段为明文 JSON）。
    return true;
}

bool ActivationCodeService::validateExpiration(const QString& expireAt, QString* errorMessage)
{
    const QString text = expireAt.trimmed();
    if (text.isEmpty()) {
        return true;
    }

    const QDate date = QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("激活码到期日期格式无效（exp）。");
        }
        return false;
    }

    // TODO: 升级为更严格的到期策略（UTC/时区、宽限期、服务端时间源）。
    if (QDate::currentDate() > date) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("激活码已过期。");
        }
        return false;
    }
    return true;
}

bool ActivationCodeService::validateFeatureSet(const QList<Feature>& features, QString* errorMessage)
{
    if (features.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("激活码功能集合为空。");
        }
        return false;
    }

    // TODO: 后续按套餐校验功能集合完整性（如专业版/教育版差异）。
    return true;
}

}  // namespace license

