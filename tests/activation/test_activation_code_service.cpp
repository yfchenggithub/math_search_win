#include "license/activation_code_service.h"
#include "license/feature_gate.h"
#include "core/logging/logger.h"

#include <QtTest/QtTest>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace {

constexpr quint32 kCrc32Polynomial = 0xEDB88320u;

QString crc32UpperHex(const QByteArray& data)
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

QString toBase64Url(const QByteArray& input)
{
    QString encoded = QString::fromLatin1(input.toBase64(QByteArray::Base64Encoding));
    encoded.replace(QLatin1Char('+'), QLatin1Char('-'));
    encoded.replace(QLatin1Char('/'), QLatin1Char('_'));
    while (encoded.endsWith(QLatin1Char('='))) {
        encoded.chop(1);
    }
    return encoded;
}

QString buildPayloadJson(const QString& deviceFingerprint, const QStringList& featureCodes)
{
    QJsonArray features;
    for (const QString& code : featureCodes) {
        features.push_back(code);
    }

    const QJsonObject payload = {
        {QStringLiteral("v"), 1},
        {QStringLiteral("p"), QStringLiteral("msw")},
        {QStringLiteral("s"), QStringLiteral("LIC-2026-0001")},
        {QStringLiteral("w"), QStringLiteral("WM-0001")},
        {QStringLiteral("e"), QStringLiteral("full")},
        {QStringLiteral("d"), deviceFingerprint},
        {QStringLiteral("f"), features},
        {QStringLiteral("iat"), QStringLiteral("2026-04-20")},
        {QStringLiteral("exp"), QStringLiteral("")},
    };
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

QString buildActivationCode(const QString& payloadJson)
{
    const QByteArray payloadBytes = payloadJson.toUtf8();
    return QStringLiteral("MSW1.%1.%2").arg(toBase64Url(payloadBytes), crc32UpperHex(payloadBytes));
}

}  // namespace

class ActivationCodeServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void parseActivationCode_rejectsEmptyOrWhitespace_data();
    void parseActivationCode_rejectsEmptyOrWhitespace();

    void parseActivationCode_rejectsMalformedCode();
    void parseActivationCode_rejectsInvalidPayloadCharacters();
    void parseActivationCode_rejectsExtremeLongGarbage();

    void parseAndValidate_acceptsValidCode();
    void validateActivationCode_rejectsDeviceMismatch();
    void validateActivationCode_rejectsProvidedExpiredCode();
    void validateActivationCode_rejectsCrcMismatch();
    void validateActivationCode_ignoresUnknownFeatureCodes();

    void buildLicenseFileContent_outputsExpectedKeyValueRows();
};

void ActivationCodeServiceTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void ActivationCodeServiceTest::parseActivationCode_rejectsEmptyOrWhitespace_data()
{
    QTest::addColumn<QString>("code");
    QTest::newRow("empty") << QString();
    QTest::newRow("spaces") << QStringLiteral("   \t\r\n   ");
}

void ActivationCodeServiceTest::parseActivationCode_rejectsEmptyOrWhitespace()
{
    QFETCH(QString, code);

    const license::ActivationCodeService service;
    const auto result = service.parseActivationCode(code);

    QVERIFY(!result.ok);
    QVERIFY(!result.errorMessage.trimmed().isEmpty());
}

void ActivationCodeServiceTest::parseActivationCode_rejectsMalformedCode()
{
    const license::ActivationCodeService service;
    const auto result = service.parseActivationCode(QStringLiteral("MSW1.only_two_parts"));
    QVERIFY(!result.ok);
}

void ActivationCodeServiceTest::parseActivationCode_rejectsInvalidPayloadCharacters()
{
    const license::ActivationCodeService service;
    const auto result = service.parseActivationCode(QStringLiteral("MSW1.abc*def.A1B2C3D4"));
    QVERIFY(!result.ok);
}

void ActivationCodeServiceTest::parseActivationCode_rejectsExtremeLongGarbage()
{
    const license::ActivationCodeService service;
    const QString longGarbage = QStringLiteral("X").repeated(20000);
    const auto result = service.parseActivationCode(longGarbage);
    QVERIFY(!result.ok);
}

void ActivationCodeServiceTest::parseAndValidate_acceptsValidCode()
{
    const QString deviceFingerprint = QStringLiteral("ABCD-EFGH-IJKL");
    const QString payloadJson = buildPayloadJson(deviceFingerprint, {QStringLiteral("bsp"),
                                                                     QStringLiteral("fs"),
                                                                     QStringLiteral("fd"),
                                                                     QStringLiteral("fav"),
                                                                     QStringLiteral("af")});
    const QString activationCode = buildActivationCode(payloadJson);

    const license::ActivationCodeService service;
    const auto parseResult = service.parseActivationCode(activationCode);
    QVERIFY(parseResult.ok);
    QCOMPARE(parseResult.prefix, QStringLiteral("MSW1"));
    QCOMPARE(parseResult.payload.deviceFingerprint, deviceFingerprint);

    const auto validation = service.validateActivationCode(parseResult.payload,
                                                           parseResult.originalPayloadJson,
                                                           parseResult.check8,
                                                           deviceFingerprint);
    QVERIFY(validation.ok);
    QCOMPARE(validation.resolvedFeatures.size(), 5);
}

void ActivationCodeServiceTest::validateActivationCode_rejectsDeviceMismatch()
{
    const QString payloadJson = buildPayloadJson(QStringLiteral("ABCD-EFGH-IJKL"), {QStringLiteral("bsp"), QStringLiteral("fs")});
    const QString activationCode = buildActivationCode(payloadJson);

    const license::ActivationCodeService service;
    const auto parseResult = service.parseActivationCode(activationCode);
    QVERIFY(parseResult.ok);

    const auto validation = service.validateActivationCode(parseResult.payload,
                                                           parseResult.originalPayloadJson,
                                                           parseResult.check8,
                                                           QStringLiteral("WXYZ-0000-0000"));
    QVERIFY(!validation.ok);
    QVERIFY(!validation.errorMessage.trimmed().isEmpty());
}

void ActivationCodeServiceTest::validateActivationCode_rejectsProvidedExpiredCode()
{
    const QString expiredCode = QStringLiteral(
        "MSW1.eyJ2IjoxLCJwIjoibXN3IiwicyI6IkxJQy0yMDI2LTAwMDEiLCJ3IjoiV00tMDAwMSIsImUiOiJmdWxsIiwiZCI6IjExNTUtRUJGQy02RUZDLThCRDIiLCJmIjpbImJzcCIsImZzIiwiZmQiLCJmYXYiLCJhZiJdLCJpYXQiOiIyMDI2LTA0LTIwIiwiZXhwIjoiMjAyNi0wNC0yMCJ9.D067C37A");
    const QString deviceFingerprint = QStringLiteral("1155-EBFC-6EFC-8BD2");

    const license::ActivationCodeService service;
    const auto parseResult = service.parseActivationCode(expiredCode);
    QVERIFY2(parseResult.ok, qPrintable(parseResult.errorMessage));
    QCOMPARE(parseResult.payload.deviceFingerprint, deviceFingerprint);
    QCOMPARE(parseResult.payload.expireAt, QStringLiteral("2026-04-20"));

    const auto validation = service.validateActivationCode(parseResult.payload,
                                                           parseResult.originalPayloadJson,
                                                           parseResult.check8,
                                                           deviceFingerprint);
    QVERIFY(!validation.ok);
    QVERIFY2(validation.errorMessage.contains(QStringLiteral("过期")), qPrintable(validation.errorMessage));
}

void ActivationCodeServiceTest::validateActivationCode_rejectsCrcMismatch()
{
    const QString deviceFingerprint = QStringLiteral("ABCD-EFGH-IJKL");
    const QString payloadJson = buildPayloadJson(deviceFingerprint, {QStringLiteral("bsp"), QStringLiteral("fs")});
    const QString activationCode = buildActivationCode(payloadJson);

    const license::ActivationCodeService service;
    const auto parseResult = service.parseActivationCode(activationCode);
    QVERIFY(parseResult.ok);

    const auto validation = service.validateActivationCode(parseResult.payload,
                                                           parseResult.originalPayloadJson,
                                                           QStringLiteral("00000000"),
                                                           deviceFingerprint);
    QVERIFY(!validation.ok);
    QVERIFY(!validation.errorMessage.trimmed().isEmpty());
}

void ActivationCodeServiceTest::validateActivationCode_ignoresUnknownFeatureCodes()
{
    const QString deviceFingerprint = QStringLiteral("ABCD-EFGH-IJKL");
    const QString payloadJson = buildPayloadJson(deviceFingerprint, {QStringLiteral("unknown_feature"), QStringLiteral("fs")});
    const QString activationCode = buildActivationCode(payloadJson);

    const license::ActivationCodeService service;
    const auto parseResult = service.parseActivationCode(activationCode);
    QVERIFY(parseResult.ok);

    const auto validation = service.validateActivationCode(parseResult.payload,
                                                           parseResult.originalPayloadJson,
                                                           parseResult.check8,
                                                           deviceFingerprint);
    QVERIFY(validation.ok);
    QCOMPARE(validation.resolvedFeatures.size(), 1);
    QCOMPARE(validation.resolvedFeatures.first(), license::Feature::FullSearch);
}

void ActivationCodeServiceTest::buildLicenseFileContent_outputsExpectedKeyValueRows()
{
    license::ActivationCodePayload payload;
    payload.version = 1;
    payload.product = QStringLiteral("msw");
    payload.serial = QStringLiteral("LIC-2026-0001");
    payload.watermark = QStringLiteral("WM-0001");
    payload.edition = QStringLiteral("full");
    payload.deviceFingerprint = QStringLiteral("ABCD-EFGH-IJKL");
    payload.featureCodes = {QStringLiteral("bsp"), QStringLiteral("fs")};
    payload.issuedAt = QStringLiteral("2026-04-20");
    payload.expireAt = QString();

    const license::ActivationCodeService service;
    const QByteArray content = service.buildLicenseFileContent(payload,
                                                               {license::Feature::BasicSearchPreview, license::Feature::FullSearch},
                                                               QStringLiteral("MSW1"),
                                                               QStringLiteral("B4411850"));

    const QString text = QString::fromUtf8(content);
    QVERIFY(text.contains(QStringLiteral("format=msw-license-v1")));
    QVERIFY(text.contains(QStringLiteral("product=math_search_win")));
    QVERIFY(text.contains(QStringLiteral("serial=LIC-2026-0001")));
    QVERIFY(text.contains(QStringLiteral("watermark=WM-0001")));
    QVERIFY(text.contains(QStringLiteral("edition=full")));
    QVERIFY(text.contains(QStringLiteral("device=ABCD-EFGH-IJKL")));
    QVERIFY(text.contains(QStringLiteral("features=basic_search_preview,full_search")));
    QVERIFY(text.contains(QStringLiteral("activation_prefix=MSW1")));
    QVERIFY(text.contains(QStringLiteral("activation_check=B4411850")));
}

QTEST_APPLESS_MAIN(ActivationCodeServiceTest)

#include "test_activation_code_service.moc"
