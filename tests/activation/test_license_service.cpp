#include "license/activation_code_service.h"
#include "license/device_fingerprint_service.h"
#include "license/feature_gate.h"
#include "license/license_service.h"
#include "core/logging/logger.h"

#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

namespace {

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
            QStringLiteral("license_%1_%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(QRandomGenerator::global()->bounded(1000000)));
        if (!QDir().mkpath(rootPath_)) {
            rootPath_.clear();
            return;
        }

        QDir root(rootPath_);
        if (!root.mkpath(QStringLiteral("src")) || !root.mkpath(QStringLiteral("resources"))) {
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
            && QDir(rootPath_).exists(QStringLiteral("resources"));
    }

private:
    QString previousCwd_;
    QString rootPath_;
};

bool writeUtf8File(const QString& filePath, const QByteArray& data)
{
    QFileInfo info(filePath);
    QDir().mkpath(info.absolutePath());
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    return file.write(data) == data.size();
}

QByteArray buildValidLicenseContent(const QString& deviceFingerprint,
                                    const QString& serial = QStringLiteral("LIC-2026-0001"),
                                    const QString& watermark = QStringLiteral("WM-0001"))
{
    license::ActivationCodePayload payload;
    payload.version = 1;
    payload.product = QStringLiteral("msw");
    payload.serial = serial;
    payload.watermark = watermark;
    payload.edition = QStringLiteral("full");
    payload.deviceFingerprint = deviceFingerprint;
    payload.featureCodes = {QStringLiteral("bsp"),
                            QStringLiteral("fs"),
                            QStringLiteral("fd"),
                            QStringLiteral("fav"),
                            QStringLiteral("af")};
    payload.issuedAt = QStringLiteral("2026-04-20");
    payload.expireAt = QString();

    const license::ActivationCodeService activationCodeService;
    return activationCodeService.buildLicenseFileContent(payload,
                                                         license::FeatureGate::fullFeatures(),
                                                         QStringLiteral("MSW1"),
                                                         QStringLiteral("B4411850"));
}

}  // namespace

class LicenseServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void initialize_withoutLicenseFile_setsTrialMissingState();
    void reload_withValidLicenseFile_switchesToValidFullState();
    void reload_withMalformedLicenseFile_setsParseErrorState();
    void validateLicense_withMismatchedDevice_reportsDeviceMismatch();
    void featureGate_trialDisabledReason_isReadableChinese();
};

void LicenseServiceTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void LicenseServiceTest::initialize_withoutLicenseFile_setsTrialMissingState()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const license::DeviceFingerprintService deviceService;
    license::LicenseService licenseService(&deviceService);
    licenseService.initialize();

    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::Missing);
    QVERIFY(state.isTrial);
    QVERIFY(!state.isFull);
    QVERIFY(!state.licenseFileExists);
    QVERIFY(!QFileInfo::exists(licenseService.licenseFilePath()));
}

void LicenseServiceTest::reload_withValidLicenseFile_switchesToValidFullState()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const license::DeviceFingerprintService deviceService;
    const QString currentDevice = deviceService.deviceFingerprint();
    QVERIFY(!currentDevice.trimmed().isEmpty());

    license::LicenseService licenseService(&deviceService);
    licenseService.initialize();

    QString writeError;
    QVERIFY2(licenseService.writeLicenseFile(buildValidLicenseContent(currentDevice), &writeError), qPrintable(writeError));

    licenseService.reload();
    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::ValidFull);
    QVERIFY(state.isFull);
    QVERIFY(!state.isTrial);
    QCOMPARE(state.licenseSerial, QStringLiteral("LIC-2026-0001"));
    QCOMPARE(state.watermarkId, QStringLiteral("WM-0001"));
    QCOMPARE(state.boundDeviceFingerprint, currentDevice);
}

void LicenseServiceTest::reload_withMalformedLicenseFile_setsParseErrorState()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const license::DeviceFingerprintService deviceService;
    license::LicenseService licenseService(&deviceService);
    licenseService.initialize();

    const QByteArray malformed = QByteArrayLiteral("format=msw-license-v1\nbroken_line_without_equal_sign\n");
    QVERIFY(writeUtf8File(licenseService.licenseFilePath(), malformed));

    licenseService.reload();
    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::ParseError);
    QVERIFY(state.isTrial);
    QVERIFY(!state.isFull);
}

void LicenseServiceTest::validateLicense_withMismatchedDevice_reportsDeviceMismatch()
{
    const license::DeviceFingerprintService deviceService;
    const QString currentDevice = deviceService.deviceFingerprint();
    QVERIFY(!currentDevice.trimmed().isEmpty());

    license::LicenseService licenseService(&deviceService);

    QMap<QString, QString> fields;
    fields.insert(QStringLiteral("format"), QStringLiteral("msw-license-v1"));
    fields.insert(QStringLiteral("product"), QStringLiteral("math_search_win"));
    fields.insert(QStringLiteral("serial"), QStringLiteral("LIC-2026-0099"));
    fields.insert(QStringLiteral("watermark"), QStringLiteral("WM-0099"));
    fields.insert(QStringLiteral("edition"), QStringLiteral("full"));
    fields.insert(QStringLiteral("device"), QStringLiteral("ZZZZ-YYYY-XXXX-WWWW"));
    fields.insert(QStringLiteral("features"),
                  QStringLiteral("basic_search_preview,full_search,full_detail,favorites,advanced_filter"));
    fields.insert(QStringLiteral("issued_at"), QStringLiteral("2026-04-20"));
    fields.insert(QStringLiteral("expire_at"), QString());

    const license::LicenseValidationResult validation = licenseService.validateLicense(fields);
    QVERIFY(!validation.ok);
    QCOMPARE(validation.status, license::LicenseStatus::DeviceMismatch);
    QCOMPARE(validation.boundDeviceFingerprint, QStringLiteral("ZZZZ-YYYY-XXXX-WWWW"));
    QVERIFY(validation.technicalReason.contains(currentDevice));
}

void LicenseServiceTest::featureGate_trialDisabledReason_isReadableChinese()
{
    const license::FeatureGate featureGate;
    const QString reason = featureGate.disabledReason(license::Feature::Favorites);

    QCOMPARE(reason, QStringLiteral("体验版不支持收藏，正式版可收藏与管理结论。"));
    QVERIFY(!reason.contains(QStringLiteral("Ã")));
}

QTEST_APPLESS_MAIN(LicenseServiceTest)

#include "test_license_service.moc"
