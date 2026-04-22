#include "core/logging/logger.h"
#include "license/activation_code_service.h"
#include "license/feature_gate.h"
#include "license/license_service.h"

#include <QtTest/QtTest>

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStringList>

namespace {

constexpr quint32 kCrc32Polynomial = 0xEDB88320u;

class ScopedSandboxRoot final {
public:
    ScopedSandboxRoot()
        : previousCwd_(QDir::currentPath())
    {
        const QString baseDir = QDir(previousCwd_).filePath(QStringLiteral(".tmp_activation_tests"));
        if (!QDir().mkpath(baseDir)) {
            return;
        }

        rootPath_ = QDir(baseDir).filePath(
            QStringLiteral("activation_code_%1_%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(QRandomGenerator::global()->bounded(1000000)));
        if (!QDir().mkpath(rootPath_)) {
            rootPath_.clear();
            return;
        }

        QDir root(rootPath_);
        if (!root.mkpath(QStringLiteral("src")) || !root.mkpath(QStringLiteral("app_resources"))) {
            return;
        }

        QDir::setCurrent(rootPath_);
    }

    ~ScopedSandboxRoot()
    {
        if (!previousCwd_.isEmpty()) {
            QDir::setCurrent(previousCwd_);
        }
        if (!rootPath_.isEmpty()) {
            QDir(rootPath_).removeRecursively();
        }
    }

    bool isValid() const
    {
        return !rootPath_.isEmpty()
            && QDir(rootPath_).exists(QStringLiteral("src"))
            && QDir(rootPath_).exists(QStringLiteral("app_resources"));
    }

private:
    QString previousCwd_;
    QString rootPath_;
};

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

QString buildPayloadJson(const QString& deviceFingerprint,
                        const QStringList& featureCodes,
                        const QString& expireAt = QString())
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
        {QStringLiteral("exp"), expireAt.trimmed()},
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

    void parseActivationCode_rejectsMalformedFormats();
    void parseActivationCode_rejectsInvalidPayloadCharacters();
    void validateActivationCode_rejectsCrcMismatch();
    void validateActivationCode_rejectsDeviceMismatch();
    void validateActivationCode_rejectsExpiredBeforeToday();
    void parseAndValidate_acceptsValidCode();
    void validActivationCode_canWriteLicenseAndReloadFull();

    void buildLicenseFileContent_outputsExpectedKeyValueRows();
};

void ActivationCodeServiceTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void ActivationCodeServiceTest::parseActivationCode_rejectsMalformedFormats()
{
    const license::ActivationCodeService service;

    const auto empty = service.parseActivationCode(QStringLiteral("   \t\n"));
    QVERIFY(!empty.ok);

    const auto missingParts = service.parseActivationCode(QStringLiteral("MSW1.only_two_parts"));
    QVERIFY(!missingParts.ok);

    const auto wrongPrefix = service.parseActivationCode(QStringLiteral("ABC1.payload.1234ABCD"));
    QVERIFY(!wrongPrefix.ok);

    const auto wrongCheck = service.parseActivationCode(QStringLiteral("MSW1.payload.not_hex"));
    QVERIFY(!wrongCheck.ok);
}

void ActivationCodeServiceTest::parseActivationCode_rejectsInvalidPayloadCharacters()
{
    const license::ActivationCodeService service;
    const auto result = service.parseActivationCode(QStringLiteral("MSW1.abc*def.A1B2C3D4"));
    QVERIFY(!result.ok);
}

void ActivationCodeServiceTest::validateActivationCode_rejectsCrcMismatch()
{
    const QString deviceFingerprint = QStringLiteral("TEST-DEVICE-0001");
    const QString activationCode = buildActivationCode(
        buildPayloadJson(deviceFingerprint, {QStringLiteral("bsp"), QStringLiteral("fs")}));

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

void ActivationCodeServiceTest::validateActivationCode_rejectsDeviceMismatch()
{
    const QString activationCode = buildActivationCode(
        buildPayloadJson(QStringLiteral("TEST-DEVICE-0001"), {QStringLiteral("bsp"), QStringLiteral("fs")}));

    const license::ActivationCodeService service;
    const auto parseResult = service.parseActivationCode(activationCode);
    QVERIFY(parseResult.ok);

    const auto validation = service.validateActivationCode(parseResult.payload,
                                                           parseResult.originalPayloadJson,
                                                           parseResult.check8,
                                                           QStringLiteral("ANOTHER-DEVICE-9999"));
    QVERIFY(!validation.ok);
    QVERIFY(!validation.errorMessage.trimmed().isEmpty());
}

void ActivationCodeServiceTest::validateActivationCode_rejectsExpiredBeforeToday()
{
    const QString expiredDate = QDate::currentDate().addDays(-1).toString(QStringLiteral("yyyy-MM-dd"));
    const QString deviceFingerprint = QStringLiteral("TEST-DEVICE-0001");
    const QString activationCode = buildActivationCode(
        buildPayloadJson(deviceFingerprint,
                         {QStringLiteral("bsp"), QStringLiteral("fs"), QStringLiteral("fd")},
                         expiredDate));

    const license::ActivationCodeService service;
    const auto parseResult = service.parseActivationCode(activationCode);
    QVERIFY2(parseResult.ok, qPrintable(parseResult.errorMessage));

    const auto validation = service.validateActivationCode(parseResult.payload,
                                                           parseResult.originalPayloadJson,
                                                           parseResult.check8,
                                                           deviceFingerprint);
    QVERIFY(!validation.ok);
    QVERIFY(!validation.errorMessage.trimmed().isEmpty());
}

void ActivationCodeServiceTest::parseAndValidate_acceptsValidCode()
{
    const QString deviceFingerprint = QStringLiteral("TEST-DEVICE-0001");
    const QString activationCode = buildActivationCode(buildPayloadJson(deviceFingerprint,
                                                                        {QStringLiteral("bsp"),
                                                                         QStringLiteral("fs"),
                                                                         QStringLiteral("fd"),
                                                                         QStringLiteral("fav"),
                                                                         QStringLiteral("af")}));

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

void ActivationCodeServiceTest::validActivationCode_canWriteLicenseAndReloadFull()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const QString deviceFingerprint = QStringLiteral("TEST-DEVICE-0001");
    const QString activationCode = buildActivationCode(buildPayloadJson(deviceFingerprint,
                                                                        {QStringLiteral("bsp"),
                                                                         QStringLiteral("fs"),
                                                                         QStringLiteral("fd"),
                                                                         QStringLiteral("fav"),
                                                                         QStringLiteral("af")}));

    const license::ActivationCodeService activationCodeService;
    const auto parseResult = activationCodeService.parseActivationCode(activationCode);
    QVERIFY2(parseResult.ok, qPrintable(parseResult.errorMessage));

    const auto validation = activationCodeService.validateActivationCode(parseResult.payload,
                                                                         parseResult.originalPayloadJson,
                                                                         parseResult.check8,
                                                                         deviceFingerprint);
    QVERIFY2(validation.ok, qPrintable(validation.errorMessage));

    const QByteArray content = activationCodeService.buildLicenseFileContent(parseResult.payload,
                                                                              validation.resolvedFeatures,
                                                                              parseResult.prefix,
                                                                              parseResult.check8);

    license::LicenseService licenseService(nullptr);
    QString writeError;
    QVERIFY2(licenseService.writeLicenseFile(content, &writeError), qPrintable(writeError));

    licenseService.reload();
    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::ValidFull);
    QVERIFY(state.isFull);
    QVERIFY(QFileInfo::exists(licenseService.licenseFilePath()));
}

void ActivationCodeServiceTest::buildLicenseFileContent_outputsExpectedKeyValueRows()
{
    license::ActivationCodePayload payload;
    payload.version = 1;
    payload.product = QStringLiteral("msw");
    payload.serial = QStringLiteral("LIC-2026-0001");
    payload.watermark = QStringLiteral("WM-0001");
    payload.edition = QStringLiteral("full");
    payload.deviceFingerprint = QStringLiteral("TEST-DEVICE-0001");
    payload.featureCodes = {QStringLiteral("bsp"), QStringLiteral("fs")};
    payload.issuedAt = QStringLiteral("2026-04-20");
    payload.expireAt = QString();

    const license::ActivationCodeService service;
    const QByteArray content = service.buildLicenseFileContent(payload,
                                                               {license::Feature::BasicSearchPreview,
                                                                license::Feature::FullSearch},
                                                               QStringLiteral("MSW1"),
                                                               QStringLiteral("B4411850"));

    const QString text = QString::fromUtf8(content);
    QVERIFY(text.contains(QStringLiteral("format=msw-license-v1")));
    QVERIFY(text.contains(QStringLiteral("product=math_search_win")));
    QVERIFY(text.contains(QStringLiteral("serial=LIC-2026-0001")));
    QVERIFY(text.contains(QStringLiteral("watermark=WM-0001")));
    QVERIFY(text.contains(QStringLiteral("edition=full")));
    QVERIFY(text.contains(QStringLiteral("device=TEST-DEVICE-0001")));
    QVERIFY(text.contains(QStringLiteral("features=basic_search_preview,full_search")));
    QVERIFY(text.contains(QStringLiteral("activation_prefix=MSW1")));
    QVERIFY(text.contains(QStringLiteral("activation_check=B4411850")));
}

QTEST_APPLESS_MAIN(ActivationCodeServiceTest)

#include "test_activation_code_service.moc"


