#include "ui/pages/activation_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "shared/constants.h"
#include "ui/style/app_style.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

namespace {

constexpr int kInfoLabelMinWidth = 124;

}  // namespace

ActivationPage::ActivationPage(QWidget* parent) : QWidget(parent)
{
    ui::style::ensureAppStyleSheetLoaded();
    setupUi();
    reloadData();

    LOG_DEBUG(LogCategory::Config, QStringLiteral("page constructed name=activation mode=entry_refined"));
}

void ActivationPage::setupUi()
{
    setObjectName(QStringLiteral("activationPage"));
    setProperty("pageRole", QStringLiteral("activation"));

    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin);
    rootLayout_->setSpacing(ui::style::tokens::kPageSectionSpacing);

    setupHeader();
    rootLayout_->addWidget(headerWidget_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName(QStringLiteral("activationScrollArea"));
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    contentWidget_ = new QWidget(scrollArea_);
    contentWidget_->setObjectName(QStringLiteral("activationContent"));

    contentLayout_ = new QVBoxLayout(contentWidget_);
    contentLayout_->setContentsMargins(0, 2, 0, 2);
    contentLayout_->setSpacing(ui::style::tokens::kCardSpacing);

    setupSections();
    contentLayout_->addStretch(1);

    scrollArea_->setWidget(contentWidget_);
    rootLayout_->addWidget(scrollArea_, 1);
}

void ActivationPage::setupHeader()
{
    headerWidget_ = new QWidget(this);
    headerWidget_->setObjectName(QStringLiteral("activationPageHeader"));

    auto* headerLayout = new QHBoxLayout(headerWidget_);
    headerLayout->setContentsMargins(18, 16, 18, 16);
    headerLayout->setSpacing(16);

    auto* titleBlock = new QWidget(headerWidget_);
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(6);

    titleLabel_ = new QLabel(QStringLiteral("激活 / 升级"), titleBlock);
    titleLabel_->setObjectName(QStringLiteral("activationPageTitle"));

    subtitleLabel_ = new QLabel(QStringLiteral("状态确认、激活操作与升级入口集中管理"), titleBlock);
    subtitleLabel_->setObjectName(QStringLiteral("activationPageSubtitle"));
    subtitleLabel_->setWordWrap(true);

    titleLayout->addWidget(titleLabel_);
    titleLayout->addWidget(subtitleLabel_);

    headerMetaLabel_ = new QLabel(QStringLiteral("本地离线 · 授权入口"), headerWidget_);
    headerMetaLabel_->setObjectName(QStringLiteral("activationMetaText"));
    headerMetaLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    headerLayout->addWidget(titleBlock, 1);
    headerLayout->addWidget(headerMetaLabel_, 0, Qt::AlignTop);
}

void ActivationPage::setupSections()
{
    auto* statusPhaseLabel = new QLabel(QStringLiteral("状态确认"), contentWidget_);
    statusPhaseLabel->setObjectName(QStringLiteral("activationMetaText"));
    contentLayout_->addWidget(statusPhaseLabel);

    auto* statusRow = new QWidget(contentWidget_);
    auto* statusRowLayout = new QHBoxLayout(statusRow);
    statusRowLayout->setContentsMargins(0, 0, 0, 0);
    statusRowLayout->setSpacing(ui::style::tokens::kCardSpacing);

    versionGroup_ = buildVersionSection();
    licenseStatusGroup_ = buildLicenseStatusSection();

    statusRowLayout->addWidget(versionGroup_, 1);
    statusRowLayout->addWidget(licenseStatusGroup_, 1);
    contentLayout_->addWidget(statusRow);

    auto* activationPhaseLabel = new QLabel(QStringLiteral("激活操作"), contentWidget_);
    activationPhaseLabel->setObjectName(QStringLiteral("activationMetaText"));
    contentLayout_->addWidget(activationPhaseLabel);

    deviceCodeGroup_ = buildDeviceCodeSection();
    activationInputGroup_ = buildActivationInputSection();
    contentLayout_->addWidget(deviceCodeGroup_);
    contentLayout_->addWidget(activationInputGroup_);

    auto* upgradePhaseLabel = new QLabel(QStringLiteral("升级说明"), contentWidget_);
    upgradePhaseLabel->setObjectName(QStringLiteral("activationMetaText"));
    contentLayout_->addWidget(upgradePhaseLabel);

    upgradeGroup_ = buildUpgradeSection();
    contentLayout_->addWidget(upgradeGroup_);
}

QWidget* ActivationPage::createSection(const QString& title, const QString& summary, const QString& sectionRole)
{
    auto* section = new QWidget(contentWidget_);
    section->setObjectName(QStringLiteral("activationGroup"));
    section->setProperty("sectionRole", sectionRole);
    section->setAttribute(Qt::WA_StyledBackground, true);

    auto* sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(ui::style::tokens::kCardPaddingHorizontal,
                                      ui::style::tokens::kCardPaddingVertical,
                                      ui::style::tokens::kCardPaddingHorizontal,
                                      ui::style::tokens::kCardPaddingVertical);
    sectionLayout->setSpacing(ui::style::tokens::kMediumSpacing);

    auto* titleLabel = new QLabel(title, section);
    titleLabel->setObjectName(QStringLiteral("activationGroupTitle"));

    auto* summaryLabel = new QLabel(summary, section);
    summaryLabel->setObjectName(QStringLiteral("activationMetaText"));
    summaryLabel->setWordWrap(true);
    summaryLabel->setVisible(!summary.trimmed().isEmpty());

    sectionLayout->addWidget(titleLabel);
    if (!summary.trimmed().isEmpty()) {
        sectionLayout->addWidget(summaryLabel);
    }
    return section;
}

QWidget* ActivationPage::createInfoRow(const QString& label, QLabel** valueLabel, bool wrapValue)
{
    auto* row = new QWidget(contentWidget_);
    row->setObjectName(QStringLiteral("activationInfoRow"));

    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(12, 9, 12, 9);
    rowLayout->setSpacing(14);

    auto* keyLabel = new QLabel(label, row);
    keyLabel->setObjectName(QStringLiteral("activationInfoLabel"));
    keyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    keyLabel->setMinimumWidth(kInfoLabelMinWidth);
    keyLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    auto* value = new QLabel(row);
    value->setObjectName(QStringLiteral("activationInfoValue"));
    value->setWordWrap(wrapValue);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    rowLayout->addWidget(keyLabel);
    rowLayout->addWidget(value, 1);

    if (valueLabel != nullptr) {
        *valueLabel = value;
    }
    return row;
}

QWidget* ActivationPage::buildVersionSection()
{
    auto* section = createSection(QStringLiteral("当前版本状态"),
                                  QStringLiteral("用于确认当前版本、通道与运行模式。"),
                                  QStringLiteral("version"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    sectionLayout->addWidget(createInfoRow(QStringLiteral("当前版本"), &versionValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("版本通道"), &channelValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("运行模式"), &runModeValueLabel_));

    auto* hintLabel =
        new QLabel(QStringLiteral("该区域仅展示状态信息，不触发授权或升级变更。"), section);
    hintLabel->setObjectName(QStringLiteral("activationHintText"));
    hintLabel->setWordWrap(true);
    sectionLayout->addWidget(hintLabel);

    return section;
}

QWidget* ActivationPage::buildLicenseStatusSection()
{
    auto* section = createSection(QStringLiteral("授权状态"),
                                  QStringLiteral("先确认授权状态，再执行激活操作。"),
                                  QStringLiteral("licenseStatus"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    auto* statusRow = new QWidget(section);
    statusRow->setObjectName(QStringLiteral("activationInfoRow"));
    auto* statusLayout = new QHBoxLayout(statusRow);
    statusLayout->setContentsMargins(12, 9, 12, 9);
    statusLayout->setSpacing(14);

    auto* statusLabel = new QLabel(QStringLiteral("授权状态"), statusRow);
    statusLabel->setObjectName(QStringLiteral("activationInfoLabel"));
    statusLabel->setMinimumWidth(kInfoLabelMinWidth);
    statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    licenseStatusValueLabel_ = new QLabel(statusRow);
    licenseStatusValueLabel_->setObjectName(QStringLiteral("licenseStatusValue"));
    licenseStatusValueLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    licenseStatusValueLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    licenseStatusValueLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    statusLayout->addWidget(statusLabel);
    statusLayout->addWidget(licenseStatusValueLabel_, 1);
    sectionLayout->addWidget(statusRow);

    sectionLayout->addWidget(createInfoRow(QStringLiteral("授权类型"), &licenseTypeValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("有效期"), &licenseExpireValueLabel_));

    licenseStatusHintLabel_ = new QLabel(section);
    licenseStatusHintLabel_->setObjectName(QStringLiteral("licenseStatusHint"));
    licenseStatusHintLabel_->setWordWrap(true);
    sectionLayout->addWidget(licenseStatusHintLabel_);

    return section;
}

QWidget* ActivationPage::buildDeviceCodeSection()
{
    auto* section = createSection(QStringLiteral("设备码"),
                                  QStringLiteral("设备码用于生成本机授权，请按原样提供给支持方。"),
                                  QStringLiteral("deviceCode"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    deviceCodeLineEdit_ = new QLineEdit(section);
    deviceCodeLineEdit_->setObjectName(QStringLiteral("deviceCodeField"));
    deviceCodeLineEdit_->setReadOnly(true);
    deviceCodeLineEdit_->setMinimumHeight(38);
    deviceCodeLineEdit_->setClearButtonEnabled(false);
    deviceCodeLineEdit_->setPlaceholderText(QStringLiteral("待接入设备码生成逻辑"));
    sectionLayout->addWidget(deviceCodeLineEdit_);

    auto* hintLabel = new QLabel(QStringLiteral("设备码是授权识别码，请勿手动修改。"), section);
    hintLabel->setObjectName(QStringLiteral("activationHintText"));
    hintLabel->setWordWrap(true);
    sectionLayout->addWidget(hintLabel);

    return section;
}

QWidget* ActivationPage::buildActivationInputSection()
{
    auto* section = createSection(QStringLiteral("激活码输入"),
                                  QStringLiteral("请输入收到的激活码并执行激活。"),
                                  QStringLiteral("activationInput"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    auto* inputRow = new QWidget(section);
    inputRow->setObjectName(QStringLiteral("activationInfoRow"));
    auto* inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(12, 9, 12, 9);
    inputLayout->setSpacing(10);

    activationCodeLineEdit_ = new QLineEdit(inputRow);
    activationCodeLineEdit_->setObjectName(QStringLiteral("activationCodeField"));
    activationCodeLineEdit_->setMinimumHeight(38);
    activationCodeLineEdit_->setPlaceholderText(QStringLiteral("请输入激活码"));
    activationCodeLineEdit_->setClearButtonEnabled(true);

    activateButton_ = new QPushButton(QStringLiteral("立即激活"), inputRow);
    activateButton_->setObjectName(QStringLiteral("activationPrimaryButton"));
    activateButton_->setCursor(Qt::PointingHandCursor);

    inputLayout->addWidget(activationCodeLineEdit_, 1);
    inputLayout->addWidget(activateButton_, 0);
    sectionLayout->addWidget(inputRow);

    auto* hintLabel = new QLabel(QStringLiteral("如未激活，请先确认设备码与激活码是否匹配。"), section);
    hintLabel->setObjectName(QStringLiteral("activationHintText"));
    hintLabel->setWordWrap(true);
    sectionLayout->addWidget(hintLabel);

    auto* supportHint = new QLabel(QStringLiteral("遇到问题可联系支持，并提供设备码和当前授权状态。"), section);
    supportHint->setObjectName(QStringLiteral("activationMetaText"));
    supportHint->setWordWrap(true);
    sectionLayout->addWidget(supportHint);

    return section;
}

QWidget* ActivationPage::buildUpgradeSection()
{
    auto* section = createSection(QStringLiteral("升级说明"),
                                  QStringLiteral("查看可升级版本与说明，不改变当前授权数据。"),
                                  QStringLiteral("upgrade"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    upgradeDescriptionLabel_ = new QLabel(section);
    upgradeDescriptionLabel_->setObjectName(QStringLiteral("activationInfoValue"));
    upgradeDescriptionLabel_->setWordWrap(true);
    sectionLayout->addWidget(upgradeDescriptionLabel_);

    auto* actionRow = new QWidget(section);
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(0);

    viewUpgradePlanButton_ = new QPushButton(QStringLiteral("查看升级方案"), actionRow);
    viewUpgradePlanButton_->setObjectName(QStringLiteral("activationSecondaryButton"));
    viewUpgradePlanButton_->setCursor(Qt::PointingHandCursor);

    actionLayout->addWidget(viewUpgradePlanButton_, 0, Qt::AlignLeft);
    actionLayout->addStretch(1);
    sectionLayout->addWidget(actionRow);

    auto* hintLabel = new QLabel(QStringLiteral("当前仅提供入口骨架，后续可接入真实升级方案详情。"), section);
    hintLabel->setObjectName(QStringLiteral("activationHintText"));
    hintLabel->setWordWrap(true);
    sectionLayout->addWidget(hintLabel);

    return section;
}

void ActivationPage::reloadData()
{
    if (versionValueLabel_ == nullptr) {
        return;
    }

    versionValueLabel_->setText(UiConstants::kStatusVersion);
    channelValueLabel_->setText(QStringLiteral("本地离线通道"));
    runModeValueLabel_->setText(QStringLiteral("%1 · 单机运行").arg(UiConstants::kStatusOffline));

    licenseTypeValueLabel_->setText(QStringLiteral("待接入"));
    licenseExpireValueLabel_->setText(QStringLiteral("当前未提供"));

    deviceCodeLineEdit_->setText(QStringLiteral("DEVICE-CODE-PLACEHOLDER"));
    activationCodeLineEdit_->clear();

    upgradeDescriptionLabel_->setText(QStringLiteral("当前版本可通过“查看升级方案”了解后续版本策略、授权差异和接入计划。"));

    licenseUiState_ = LicenseUiState::Inactive;
    updateLicenseStateUi();
}

void ActivationPage::updateLicenseStateUi()
{
    if (licenseStatusValueLabel_ == nullptr || licenseStatusHintLabel_ == nullptr) {
        return;
    }

    QString statusText;
    QString hintText;
    QString statusClass;

    switch (licenseUiState_) {
    case LicenseUiState::Active:
        statusText = QStringLiteral("已激活");
        hintText = QStringLiteral("当前设备已绑定有效授权，可继续正常使用。");
        statusClass = QStringLiteral("licenseStatusActive");
        break;
    case LicenseUiState::Inactive:
        statusText = QStringLiteral("未激活");
        hintText = QStringLiteral("尚未检测到有效授权。请先确认设备码，再输入激活码完成激活。");
        statusClass = QStringLiteral("licenseStatusInactive");
        break;
    case LicenseUiState::Unknown:
        statusText = QStringLiteral("状态待确认");
        hintText = QStringLiteral("当前版本尚未接入完整状态查询，后续会展示真实授权状态。");
        statusClass = QStringLiteral("licenseStatusUnknown");
        break;
    case LicenseUiState::Error:
        statusText = QStringLiteral("状态异常");
        hintText = QStringLiteral("授权状态读取失败，请稍后重试或联系支持。");
        statusClass = QStringLiteral("licenseStatusError");
        break;
    }

    licenseStatusValueLabel_->setText(statusText);
    licenseStatusValueLabel_->setProperty("statusClass", statusClass);
    licenseStatusHintLabel_->setText(hintText);
    licenseStatusHintLabel_->setProperty("statusClass", statusClass);

    auto refreshStyle = [](QWidget* widget) {
        if (widget == nullptr || widget->style() == nullptr) {
            return;
        }
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    };

    refreshStyle(licenseStatusValueLabel_);
    refreshStyle(licenseStatusHintLabel_);
}
