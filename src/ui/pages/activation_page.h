#pragma once

#include "license/license_state.h"

#include <QString>
#include <QWidget>

namespace license {
class ActivationCodeService;
class DeviceFingerprintService;
class LicenseService;
}  // namespace license

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class ActivationPage : public QWidget {
    Q_OBJECT

public:
    explicit ActivationPage(license::LicenseService* licenseService,
                            const license::DeviceFingerprintService* deviceFingerprintService,
                            const license::ActivationCodeService* activationCodeService,
                            QWidget* parent = nullptr);
    void reloadData();

private slots:
    void onActivateClicked();
    void onReloadLicenseClicked();
    void onViewUpgradePlanClicked();

private:
    void setupUi();
    void setupHeader();
    void setupSections();

    QWidget* buildVersionSection();
    QWidget* buildLicenseStatusSection();
    QWidget* buildDeviceCodeSection();
    QWidget* buildActivationInputSection();
    QWidget* buildUpgradeSection();

    QWidget* createSection(const QString& title, const QString& summary, const QString& sectionRole);
    QWidget* createInfoRow(const QString& label, QLabel** valueLabel, bool wrapValue = false);

    void updateLicenseStateUi(const license::LicenseState& state);
    QString licenseStatusHintText(const license::LicenseState& state) const;

private:
    license::LicenseService* licenseService_ = nullptr;
    const license::DeviceFingerprintService* deviceFingerprintService_ = nullptr;
    const license::ActivationCodeService* activationCodeService_ = nullptr;

    QVBoxLayout* rootLayout_ = nullptr;
    QWidget* headerWidget_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* subtitleLabel_ = nullptr;
    QLabel* headerMetaLabel_ = nullptr;

    QScrollArea* scrollArea_ = nullptr;
    QWidget* contentWidget_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;

    QWidget* versionGroup_ = nullptr;
    QWidget* licenseStatusGroup_ = nullptr;
    QWidget* deviceCodeGroup_ = nullptr;
    QWidget* activationInputGroup_ = nullptr;
    QWidget* upgradeGroup_ = nullptr;

    QLabel* versionValueLabel_ = nullptr;
    QLabel* channelValueLabel_ = nullptr;
    QLabel* runModeValueLabel_ = nullptr;
    QLabel* licenseStatusValueLabel_ = nullptr;
    QLabel* licenseStatusHintLabel_ = nullptr;
    QLabel* licenseTypeValueLabel_ = nullptr;
    QLabel* licenseExpireValueLabel_ = nullptr;
    QLabel* licenseSerialValueLabel_ = nullptr;
    QLabel* watermarkValueLabel_ = nullptr;
    QLineEdit* deviceCodeLineEdit_ = nullptr;
    QLineEdit* activationCodeLineEdit_ = nullptr;
    QPushButton* activateButton_ = nullptr;
    QPushButton* reloadLicenseButton_ = nullptr;
    QPushButton* viewUpgradePlanButton_ = nullptr;
    QLabel* upgradeDescriptionLabel_ = nullptr;

    QString transientActivationError_;
};
