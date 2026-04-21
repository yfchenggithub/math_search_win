#include "core/logging/logger.h"
#include "license/activation_code_service.h"
#include "license/device_fingerprint_service.h"
#include "license/license_service.h"
#include "ui/pages/activation_page.h"

#define private public
#include "ui/pages/settings_page.h"
#undef private

#include <QtTest/QtTest>

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSharedPointer>
#include <QTimer>

namespace {

class ScopedSandboxRoot final {
public:
    ScopedSandboxRoot()
        : previousCwd_(QDir::currentPath())
    {
        const QString baseDir = QDir(previousCwd_).filePath(QStringLiteral(".tmp_settings_activation_round5_tests"));
        if (!QDir().mkpath(baseDir)) {
            return;
        }

        rootPath_ = QDir(baseDir).filePath(
            QStringLiteral("case_%1_%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(QRandomGenerator::global()->bounded(1000000)));
        if (!QDir().mkpath(rootPath_)) {
            rootPath_.clear();
            return;
        }

        QDir root(rootPath_);
        const QStringList requiredDirs = {
            QStringLiteral("src"),
            QStringLiteral("resources"),
            QStringLiteral("data"),
            QStringLiteral("cache"),
            QStringLiteral("license"),
        };
        for (const QString& dir : requiredDirs) {
            if (!root.mkpath(dir)) {
                rootPath_.clear();
                return;
            }
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
            && QDir(rootPath_).exists(QStringLiteral("resources"))
            && QDir(rootPath_).exists(QStringLiteral("cache"))
            && QDir(rootPath_).exists(QStringLiteral("license"));
    }

private:
    QString previousCwd_;
    QString rootPath_;
};

void closeAnyMessageBoxes(const QSharedPointer<QString>& capturedMessage = {})
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        auto* box = qobject_cast<QMessageBox*>(widget);
        if (box == nullptr) {
            continue;
        }
        if (!capturedMessage.isNull() && capturedMessage->trimmed().isEmpty()) {
            *capturedMessage = box->text();
        }
        box->done(QDialog::Accepted);
    }
}

void scheduleAutoCloseMessageBoxes(const QSharedPointer<QString>& capturedMessage = {})
{
    QTimer::singleShot(0, [capturedMessage]() { closeAnyMessageBoxes(capturedMessage); });
    QTimer::singleShot(80, []() { closeAnyMessageBoxes(); });
}

}  // namespace

class SettingsActivationRound5UiTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void settingsPage_buttonsAndDataStatusToggle_stayStable();
    void activationPage_keyButtonsKeepStableAndStatusClassToggles();
};

void SettingsActivationRound5UiTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void SettingsActivationRound5UiTest::settingsPage_buttonsAndDataStatusToggle_stayStable()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    SettingsPage unavailablePage(nullptr, nullptr, nullptr, false, false, nullptr);
    SettingsPage readyPage(nullptr, nullptr, nullptr, true, true, nullptr);

    QVERIFY(unavailablePage.dataStatusValueLabel_ != nullptr);
    QVERIFY(readyPage.dataStatusValueLabel_ != nullptr);
    QVERIFY(unavailablePage.openDataDirButton_ != nullptr);
    QVERIFY(unavailablePage.openReadmeButton_ != nullptr);
    QVERIFY(unavailablePage.copyFeedbackEmailButton_ != nullptr);

    QVERIFY(unavailablePage.dataStatusValueLabel_->text().trimmed() != readyPage.dataStatusValueLabel_->text().trimmed());

    const QString originalCopyText = unavailablePage.copyFeedbackEmailButton_->text();
    QTest::mouseClick(unavailablePage.openDataDirButton_, Qt::LeftButton);
    QTest::mouseClick(unavailablePage.openReadmeButton_, Qt::LeftButton);
    QTest::mouseClick(unavailablePage.copyFeedbackEmailButton_, Qt::LeftButton);
    QVERIFY(unavailablePage.copyFeedbackEmailButton_->text() != originalCopyText);
}

void SettingsActivationRound5UiTest::activationPage_keyButtonsKeepStableAndStatusClassToggles()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const license::DeviceFingerprintService deviceService(QStringLiteral("ROUND5-DEVICE-0001"));
    const license::ActivationCodeService activationCodeService;
    license::LicenseService licenseService(&deviceService);
    licenseService.initialize();

    ActivationPage page(&licenseService, &deviceService, &activationCodeService);

    auto* activationInput = page.findChild<QLineEdit*>(QStringLiteral("activationCodeField"));
    auto* activateButton = page.findChild<QPushButton*>(QStringLiteral("activationPrimaryButton"));
    auto* reloadButton = page.findChild<QPushButton*>(QStringLiteral("activationSecondaryButton"));
    auto* statusLabel = page.findChild<QLabel*>(QStringLiteral("licenseStatusValue"));
    QVERIFY(activationInput != nullptr);
    QVERIFY(activateButton != nullptr);
    QVERIFY(reloadButton != nullptr);
    QVERIFY(statusLabel != nullptr);

    QCOMPARE(statusLabel->property("statusClass").toString(), QStringLiteral("licenseStatusInactive"));

    activationInput->setText(QStringLiteral("   \t\r\n "));
    const auto capturedMessage = QSharedPointer<QString>::create();
    scheduleAutoCloseMessageBoxes(capturedMessage);
    QTest::mouseClick(activateButton, Qt::LeftButton);
    QCOMPARE(statusLabel->property("statusClass").toString(), QStringLiteral("licenseStatusError"));
    QVERIFY2(!capturedMessage->trimmed().isEmpty(), "activate button should show a message on empty input");

    scheduleAutoCloseMessageBoxes();
    QTest::mouseClick(reloadButton, Qt::LeftButton);
    QTRY_COMPARE(statusLabel->property("statusClass").toString(), QStringLiteral("licenseStatusInactive"));

    const QList<QPushButton*> allSecondaryButtons =
        page.findChildren<QPushButton*>(QStringLiteral("activationSecondaryButton"));
    QVERIFY(allSecondaryButtons.size() >= 2);
    scheduleAutoCloseMessageBoxes();
    QTest::mouseClick(allSecondaryButtons.at(1), Qt::LeftButton);
}

QTEST_MAIN(SettingsActivationRound5UiTest)

#include "test_settings_activation_round5.moc"
