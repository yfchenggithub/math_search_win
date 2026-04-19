#include "ui/pages/activation_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ActivationPage::ActivationPage(QWidget* parent) : QWidget(parent)
{
    LOG_INFO(LogCategory::Config, QStringLiteral("ActivationPage constructed (license workflow placeholder)"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    mainLayout->addWidget(new QLabel(QStringLiteral("激活/升级"), this));

    auto* versionStatusBox = new QGroupBox(QStringLiteral("当前版本状态"), this);
    auto* versionStatusLayout = new QVBoxLayout(versionStatusBox);
    versionStatusLayout->addWidget(new QLabel(QStringLiteral("当前版本：MVP v0.1"), versionStatusBox));
    versionStatusLayout->addWidget(new QLabel(QStringLiteral("版本通道：本地离线版"), versionStatusBox));
    mainLayout->addWidget(versionStatusBox);

    auto* licenseStatusBox = new QGroupBox(QStringLiteral("授权状态"), this);
    auto* licenseStatusLayout = new QVBoxLayout(licenseStatusBox);
    licenseStatusLayout->addWidget(new QLabel(QStringLiteral("授权状态：未激活（占位）"), licenseStatusBox));
    mainLayout->addWidget(licenseStatusBox);

    auto* deviceCodeBox = new QGroupBox(QStringLiteral("设备码"), this);
    auto* deviceCodeLayout = new QVBoxLayout(deviceCodeBox);
    auto* deviceCodeField = new QLineEdit(deviceCodeBox);
    deviceCodeField->setReadOnly(true);
    deviceCodeField->setText(QStringLiteral("DEVICE-CODE-PLACEHOLDER"));
    deviceCodeLayout->addWidget(deviceCodeField);
    mainLayout->addWidget(deviceCodeBox);

    auto* activationCodeBox = new QGroupBox(QStringLiteral("激活码"), this);
    auto* activationCodeLayout = new QHBoxLayout(activationCodeBox);
    auto* activationCodeInput = new QLineEdit(activationCodeBox);
    activationCodeInput->setPlaceholderText(QStringLiteral("请输入激活码（占位）"));
    activationCodeLayout->addWidget(activationCodeInput, 1);
    activationCodeLayout->addWidget(new QPushButton(QStringLiteral("激活（占位）"), activationCodeBox));
    mainLayout->addWidget(activationCodeBox);

    auto* upgradeInfoBox = new QGroupBox(QStringLiteral("升级说明"), this);
    auto* upgradeInfoLayout = new QVBoxLayout(upgradeInfoBox);
    upgradeInfoLayout->addWidget(new QLabel(QStringLiteral("升级入口占位：后续接入升级策略。"), upgradeInfoBox));
    upgradeInfoLayout->addWidget(new QPushButton(QStringLiteral("查看升级方案（占位）"), upgradeInfoBox));
    mainLayout->addWidget(upgradeInfoBox);

    mainLayout->addStretch();
}
