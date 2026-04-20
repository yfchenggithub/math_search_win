#include "license/activation_code_service.h"
#include "license/device_fingerprint_service.h"
#include "license/license_service.h"
#include "core/logging/logger.h"
#include "ui/pages/activation_page.h"

#include <QtTest/QtTest>

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTimer>

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
            QStringLiteral("page_%1_%2")
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

QString buildPayloadJson(const QString& deviceFingerprint)
{
    return QStringLiteral(
               "{\"v\":1,\"p\":\"msw\",\"s\":\"LIC-2026-0001\",\"w\":\"WM-0001\",\"e\":\"full\","
               "\"d\":\"%1\",\"f\":[\"bsp\",\"fs\",\"fd\",\"fav\",\"af\"],\"iat\":\"2026-04-20\",\"exp\":\"\"}")
        .arg(deviceFingerprint);
}

QString buildActivationCodeForDevice(const QString& deviceFingerprint)
{
    const QString payloadJson = buildPayloadJson(deviceFingerprint);
    const QByteArray payload = payloadJson.toUtf8();
    return QStringLiteral("MSW1.%1.%2").arg(toBase64Url(payload), crc32UpperHex(payload));
}

void closeAnyMessageBoxes()
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        auto* box = qobject_cast<QMessageBox*>(widget);
        if (box != nullptr) {
            box->done(QDialog::Accepted);
        }
    }
}

void scheduleAutoCloseMessageBoxes()
{
    QTimer::singleShot(0, []() {
        closeAnyMessageBoxes();
    });
    QTimer::singleShot(80, []() {
        closeAnyMessageBoxes();
    });
}

}  // namespace

class ActivationPageTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void page_usesCurrentDeviceFingerprintAndInactiveClassOnTrial();
    void activateButton_withEmptyInput_keepsTrialAndMarksError();
    void activateButton_withValidCode_writesLicenseAndSwitchesToFull();
    void page_withNullServices_staysStableAndShowsUnknownThenError();
};

void ActivationPageTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void ActivationPageTest::page_usesCurrentDeviceFingerprintAndInactiveClassOnTrial()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const license::DeviceFingerprintService deviceService;
    const license::ActivationCodeService activationCodeService;
    license::LicenseService licenseService(&deviceService);
    licenseService.initialize();

    ActivationPage page(&licenseService, &deviceService, &activationCodeService);

    auto* deviceCodeField = page.findChild<QLineEdit*>(QStringLiteral("deviceCodeField"));
    auto* statusLabel = page.findChild<QLabel*>(QStringLiteral("licenseStatusValue"));
    QVERIFY(deviceCodeField != nullptr);
    QVERIFY(statusLabel != nullptr);

    QCOMPARE(deviceCodeField->text(), deviceService.deviceFingerprint());
    QCOMPARE(statusLabel->property("statusClass").toString(), QStringLiteral("licenseStatusInactive"));
}

void ActivationPageTest::activateButton_withEmptyInput_keepsTrialAndMarksError()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const license::DeviceFingerprintService deviceService;
    const license::ActivationCodeService activationCodeService;
    license::LicenseService licenseService(&deviceService);
    licenseService.initialize();

    ActivationPage page(&licenseService, &deviceService, &activationCodeService);
    auto* activationInput = page.findChild<QLineEdit*>(QStringLiteral("activationCodeField"));
    auto* activateButton = page.findChild<QPushButton*>(QStringLiteral("activationPrimaryButton"));
    auto* statusLabel = page.findChild<QLabel*>(QStringLiteral("licenseStatusValue"));
    QVERIFY(activationInput != nullptr);
    QVERIFY(activateButton != nullptr);
    QVERIFY(statusLabel != nullptr);

    activationInput->setText(QStringLiteral("   \t\r\n "));
    scheduleAutoCloseMessageBoxes();
    activateButton->click();

    QVERIFY(!licenseService.currentState().isFull);
    QVERIFY(!QFileInfo::exists(licenseService.licenseFilePath()));
    QCOMPARE(statusLabel->property("statusClass").toString(), QStringLiteral("licenseStatusError"));
}

void ActivationPageTest::activateButton_withValidCode_writesLicenseAndSwitchesToFull()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const license::DeviceFingerprintService deviceService;
    const license::ActivationCodeService activationCodeService;
    license::LicenseService licenseService(&deviceService);
    licenseService.initialize();

    ActivationPage page(&licenseService, &deviceService, &activationCodeService);
    auto* activationInput = page.findChild<QLineEdit*>(QStringLiteral("activationCodeField"));
    auto* activateButton = page.findChild<QPushButton*>(QStringLiteral("activationPrimaryButton"));
    auto* statusLabel = page.findChild<QLabel*>(QStringLiteral("licenseStatusValue"));
    QVERIFY(activationInput != nullptr);
    QVERIFY(activateButton != nullptr);
    QVERIFY(statusLabel != nullptr);

    const QString code = buildActivationCodeForDevice(deviceService.deviceFingerprint());
    activationInput->setText(code);

    scheduleAutoCloseMessageBoxes();
    activateButton->click();

    QTRY_VERIFY_WITH_TIMEOUT(licenseService.currentState().isFull, 1500);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(licenseService.licenseFilePath()), 1500);
    QCOMPARE(statusLabel->property("statusClass").toString(), QStringLiteral("licenseStatusActive"));
    QVERIFY(activationInput->text().isEmpty());

    QFile file(licenseService.licenseFilePath());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("serial=LIC-2026-0001")));
    QVERIFY(content.contains(QStringLiteral("status=valid")));
}

void ActivationPageTest::page_withNullServices_staysStableAndShowsUnknownThenError()
{
    ActivationPage page(nullptr, nullptr, nullptr);

    auto* deviceCodeField = page.findChild<QLineEdit*>(QStringLiteral("deviceCodeField"));
    auto* activationInput = page.findChild<QLineEdit*>(QStringLiteral("activationCodeField"));
    auto* activateButton = page.findChild<QPushButton*>(QStringLiteral("activationPrimaryButton"));
    auto* statusLabel = page.findChild<QLabel*>(QStringLiteral("licenseStatusValue"));
    QVERIFY(deviceCodeField != nullptr);
    QVERIFY(activationInput != nullptr);
    QVERIFY(activateButton != nullptr);
    QVERIFY(statusLabel != nullptr);

    QVERIFY(!deviceCodeField->text().trimmed().isEmpty());
    QCOMPARE(statusLabel->property("statusClass").toString(), QStringLiteral("licenseStatusUnknown"));

    activationInput->setText(QStringLiteral("MSW1.fake.00000000"));
    activateButton->click();
    QCOMPARE(statusLabel->property("statusClass").toString(), QStringLiteral("licenseStatusError"));
}

QTEST_MAIN(ActivationPageTest)

#include "test_activation_page.moc"
