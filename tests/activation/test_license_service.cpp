#include "core/logging/logger.h"
#include "license/feature_gate.h"
#include "license/license_service.h"

#include <QtTest/QtTest>

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStringList>

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

QByteArray serializeLicenseFields(const QMap<QString, QString>& fields)
{
    QStringList lines;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        lines.push_back(QStringLiteral("%1=%2").arg(it.key(), it.value()));
    }

    QString text = lines.join(QLatin1Char('\n'));
    text.append(QLatin1Char('\n'));
    return text.toUtf8();
}

QMap<QString, QString> buildValidFullFields(const QString& expireAt = QString())
{
    QMap<QString, QString> fields;
    fields.insert(QStringLiteral("format"), QStringLiteral("msw-license-v1"));
    fields.insert(QStringLiteral("product"), QStringLiteral("math_search_win"));
    fields.insert(QStringLiteral("serial"), QStringLiteral("LIC-2026-0001"));
    fields.insert(QStringLiteral("watermark"), QStringLiteral("WM-0001"));
    fields.insert(QStringLiteral("edition"), QStringLiteral("full"));
    fields.insert(QStringLiteral("device"), QStringLiteral("TEST-DEVICE-0001"));
    fields.insert(QStringLiteral("features"),
                  QStringLiteral("basic_search_preview,full_search,full_detail,favorites,advanced_filter"));
    fields.insert(QStringLiteral("issued_at"), QStringLiteral("2026-04-20"));
    fields.insert(QStringLiteral("expire_at"), expireAt.trimmed());
    return fields;
}

QStringList trialFeatureKeys()
{
    return license::FeatureGate::featureKeysFromList(license::FeatureGate::trialFeatures());
}

}  // namespace

class LicenseServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void reload_whenLicenseFileMissing_setsMissingState();
    void reload_whenLicenseFileEmpty_setsParseErrorState();
    void reload_whenFormatVersionInvalid_setsInvalidState();
    void reload_whenRequiredFieldMissing_setsInvalidState();
    void reload_whenExpired_setsInvalidState();
    void reload_whenTrialFallback_onlyEnablesTrialFeatures();
    void reload_whenValidFull_setsFullState();
    void reload_invalidThenValid_transitionsAndEmitsSignals();
};

void LicenseServiceTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void LicenseServiceTest::reload_whenLicenseFileMissing_setsMissingState()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    license::LicenseService licenseService(nullptr);
    QSignalSpy stateSpy(&licenseService, &license::LicenseService::licenseStateChanged);

    licenseService.reload();

    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::Missing);
    QVERIFY(state.isTrial);
    QVERIFY(!state.isFull);
    QVERIFY(!state.licenseFileExists);
    QCOMPARE(state.enabledFeatures, trialFeatureKeys());
    QVERIFY2(stateSpy.count() >= 1, "reload should emit at least one state change");
}

void LicenseServiceTest::reload_whenLicenseFileEmpty_setsParseErrorState()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    license::LicenseService licenseService(nullptr);
    QVERIFY(writeUtf8File(licenseService.licenseFilePath(), QByteArray()));

    licenseService.reload();

    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::ParseError);
    QVERIFY(state.isTrial);
    QVERIFY(!state.isFull);
    QVERIFY(state.licenseFileExists);
}

void LicenseServiceTest::reload_whenFormatVersionInvalid_setsInvalidState()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    QMap<QString, QString> fields = buildValidFullFields();
    fields.insert(QStringLiteral("format"), QStringLiteral("msw-license-v9"));

    license::LicenseService licenseService(nullptr);
    QVERIFY(writeUtf8File(licenseService.licenseFilePath(), serializeLicenseFields(fields)));

    licenseService.reload();

    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::Invalid);
    QVERIFY(state.isTrial);
    QVERIFY(!state.isFull);
    QVERIFY(state.technicalReason.contains(QStringLiteral("format mismatch")));
}

void LicenseServiceTest::reload_whenRequiredFieldMissing_setsInvalidState()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    QMap<QString, QString> fields = buildValidFullFields();
    fields.remove(QStringLiteral("serial"));

    license::LicenseService licenseService(nullptr);
    QVERIFY(writeUtf8File(licenseService.licenseFilePath(), serializeLicenseFields(fields)));

    licenseService.reload();

    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::Invalid);
    QVERIFY(state.isTrial);
    QVERIFY(!state.isFull);
    QVERIFY(state.technicalReason.contains(QStringLiteral("serial missing")));
}

void LicenseServiceTest::reload_whenExpired_setsInvalidState()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const QString yesterday = QDate::currentDate().addDays(-1).toString(QStringLiteral("yyyy-MM-dd"));
    const QMap<QString, QString> fields = buildValidFullFields(yesterday);

    license::LicenseService licenseService(nullptr);
    QVERIFY(writeUtf8File(licenseService.licenseFilePath(), serializeLicenseFields(fields)));

    licenseService.reload();

    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::Invalid);
    QVERIFY(state.isTrial);
    QVERIFY(!state.isFull);
    QCOMPARE(state.expireAt, yesterday);
    QVERIFY(state.technicalReason.contains(QStringLiteral("license expired")));
}

void LicenseServiceTest::reload_whenTrialFallback_onlyEnablesTrialFeatures()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const QByteArray malformed = QByteArrayLiteral("format=msw-license-v1\nbroken_line_without_equal\n");

    license::LicenseService licenseService(nullptr);
    QVERIFY(writeUtf8File(licenseService.licenseFilePath(), malformed));

    licenseService.reload();

    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::ParseError);
    QCOMPARE(state.enabledFeatures, trialFeatureKeys());
    QVERIFY(state.isTrial);
    QVERIFY(!state.isFull);
}

void LicenseServiceTest::reload_whenValidFull_setsFullState()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const QMap<QString, QString> fields = buildValidFullFields();

    license::LicenseService licenseService(nullptr);
    QVERIFY(writeUtf8File(licenseService.licenseFilePath(), serializeLicenseFields(fields)));

    licenseService.reload();

    const license::LicenseState state = licenseService.currentState();
    QCOMPARE(state.status, license::LicenseStatus::ValidFull);
    QVERIFY(state.isFull);
    QVERIFY(!state.isTrial);
    QCOMPARE(state.licenseSerial, QStringLiteral("LIC-2026-0001"));
    QCOMPARE(state.watermarkId, QStringLiteral("WM-0001"));
    QCOMPARE(state.boundDeviceFingerprint, QStringLiteral("TEST-DEVICE-0001"));
    QCOMPARE(state.enabledFeatures.size(), 5);
    QVERIFY(state.enabledFeatures.contains(QStringLiteral("full_search")));
    QVERIFY(state.enabledFeatures.contains(QStringLiteral("full_detail")));
}

void LicenseServiceTest::reload_invalidThenValid_transitionsAndEmitsSignals()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    license::LicenseService licenseService(nullptr);
    QSignalSpy stateSpy(&licenseService, &license::LicenseService::licenseStateChanged);

    QMap<QString, QString> invalid = buildValidFullFields();
    invalid.insert(QStringLiteral("edition"), QStringLiteral("trial"));
    QVERIFY(writeUtf8File(licenseService.licenseFilePath(), serializeLicenseFields(invalid)));

    licenseService.reload();
    QCOMPARE(licenseService.currentState().status, license::LicenseStatus::Invalid);

    QVERIFY(writeUtf8File(licenseService.licenseFilePath(), serializeLicenseFields(buildValidFullFields())));
    licenseService.reload();

    const license::LicenseState finalState = licenseService.currentState();
    QCOMPARE(finalState.status, license::LicenseStatus::ValidFull);
    QVERIFY(finalState.isFull);
    QVERIFY2(stateSpy.count() >= 2, "invalid -> valid should emit state changes");
}

QTEST_APPLESS_MAIN(LicenseServiceTest)

#include "test_license_service.moc"

