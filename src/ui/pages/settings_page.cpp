#include "ui/pages/settings_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "license/license_service.h"
#include "license/license_state.h"
#include "shared/constants.h"
#include "shared/paths.h"
#include "ui/style/app_style.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>

namespace {

constexpr int kInfoLabelMinWidth = 132;
const QString kFeedbackEmail = QStringLiteral("support@example.com");

QString normalizedNativePath(const QString& path)
{
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

bool openWithSystemDefaultApp(const QString& absolutePath)
{
#if defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("cmd.exe"),
                                   QStringList{QStringLiteral("/c"),
                                               QStringLiteral("start"),
                                               QStringLiteral(""),
                                               normalizedNativePath(absolutePath)});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
#endif
}

bool openPathInExplorer(const QString& absolutePath)
{
#if defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList{normalizedNativePath(absolutePath)});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
#endif
}

QString buildTypeText()
{
#if defined(QT_NO_DEBUG)
    return QStringLiteral("Release");
#else
    return QStringLiteral("Debug");
#endif
}

QString licenseFileStatusText(const license::LicenseState& state)
{
    switch (state.status) {
    case license::LicenseStatus::ValidFull:
        return QStringLiteral("有效");
    case license::LicenseStatus::Missing:
    case license::LicenseStatus::Trial:
        return QStringLiteral("未找到");
    case license::LicenseStatus::DeviceMismatch:
        return QStringLiteral("设备不匹配");
    case license::LicenseStatus::ReadError:
        return QStringLiteral("读取失败");
    case license::LicenseStatus::ParseError:
    case license::LicenseStatus::Invalid:
        return QStringLiteral("无效");
    case license::LicenseStatus::ActivationCodeInvalid:
        return QStringLiteral("激活失败");
    case license::LicenseStatus::WriteError:
        return QStringLiteral("写入失败");
    case license::LicenseStatus::Unknown:
    default:
        return QStringLiteral("未知");
    }
}

}  // namespace

SettingsPage::SettingsPage(const infrastructure::data::ConclusionIndexRepository* indexRepository,
                           const infrastructure::data::ConclusionContentRepository* contentRepository,
                           const license::LicenseService* licenseService,
                           bool indexLoaded,
                           bool contentLoaded,
                           QWidget* parent)
    : QWidget(parent),
      indexRepository_(indexRepository),
      contentRepository_(contentRepository),
      licenseService_(licenseService),
      indexLoaded_(indexLoaded),
      contentLoaded_(contentLoaded)
{
    ui::style::ensureAppStyleSheetLoaded();
    setupUi();
    reloadData();

    LOG_DEBUG(LogCategory::Config, QStringLiteral("page constructed name=settings mode=settings_about_refined"));
}

void SettingsPage::setupUi()
{
    setObjectName(QStringLiteral("settingsPage"));
    setProperty("pageRole", QStringLiteral("settings"));

    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin,
                                    ui::style::tokens::kPageOuterMargin);
    rootLayout_->setSpacing(ui::style::tokens::kPageSectionSpacing);

    setupHeader();
    rootLayout_->addWidget(headerWidget_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName(QStringLiteral("settingsScrollArea"));
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    contentWidget_ = new QWidget(scrollArea_);
    contentWidget_->setObjectName(QStringLiteral("settingsContent"));

    contentLayout_ = new QVBoxLayout(contentWidget_);
    contentLayout_->setContentsMargins(0, 2, 0, 2);
    contentLayout_->setSpacing(ui::style::tokens::kCardSpacing);

    setupSections();

    scrollArea_->setWidget(contentWidget_);
    rootLayout_->addWidget(scrollArea_, 1);
}

void SettingsPage::setupHeader()
{
    headerWidget_ = new QWidget(this);
    headerWidget_->setObjectName(QStringLiteral("settingsPageHeader"));

    auto* headerLayout = new QHBoxLayout(headerWidget_);
    headerLayout->setContentsMargins(18, 16, 18, 16);
    headerLayout->setSpacing(16);

    auto* titleBlock = new QWidget(headerWidget_);
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(6);

    titleLabel_ = new QLabel(QStringLiteral("设置 / 关于"), titleBlock);
    titleLabel_->setObjectName(QStringLiteral("settingsPageTitle"));

    subtitleLabel_ = new QLabel(QStringLiteral("管理应用信息、数据状态与帮助入口"), titleBlock);
    subtitleLabel_->setObjectName(QStringLiteral("settingsPageSubtitle"));
    subtitleLabel_->setWordWrap(true);

    titleLayout->addWidget(titleLabel_);
    titleLayout->addWidget(subtitleLabel_);

    headerMetaLabel_ = new QLabel(QStringLiteral("本地离线 · MVP"), headerWidget_);
    headerMetaLabel_->setObjectName(QStringLiteral("settingsMetaText"));
    headerMetaLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    headerLayout->addWidget(titleBlock, 1);
    headerLayout->addWidget(headerMetaLabel_, 0, Qt::AlignTop);
}

void SettingsPage::setupSections()
{
    softwareInfoGroup_ = buildSoftwareInfoSection();
    licenseInfoGroup_ = buildLicenseInfoSection();
    dataInfoGroup_ = buildDataInfoSection();
    helpGroup_ = buildHelpSection();
    feedbackGroup_ = buildFeedbackSection();
    expansionHintGroup_ = buildExpansionHintSection();

    contentLayout_->addWidget(softwareInfoGroup_);
    contentLayout_->addWidget(licenseInfoGroup_);
    contentLayout_->addWidget(dataInfoGroup_);
    contentLayout_->addWidget(helpGroup_);
    contentLayout_->addWidget(feedbackGroup_);
    contentLayout_->addWidget(expansionHintGroup_);
    contentLayout_->addStretch(1);
}

QWidget* SettingsPage::createSection(const QString& title, const QString& summary, const QString& sectionRole)
{
    auto* section = new QWidget(contentWidget_);
    section->setObjectName(QStringLiteral("settingsGroup"));
    section->setProperty("sectionRole", sectionRole);

    auto* sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(ui::style::tokens::kCardPaddingHorizontal,
                                      ui::style::tokens::kCardPaddingVertical,
                                      ui::style::tokens::kCardPaddingHorizontal,
                                      ui::style::tokens::kCardPaddingVertical);
    sectionLayout->setSpacing(ui::style::tokens::kMediumSpacing);

    auto* titleLabel = new QLabel(title, section);
    titleLabel->setObjectName(QStringLiteral("settingsGroupTitle"));

    auto* summaryLabel = new QLabel(summary, section);
    summaryLabel->setObjectName(QStringLiteral("settingsMetaText"));
    summaryLabel->setWordWrap(true);
    summaryLabel->setVisible(!summary.trimmed().isEmpty());

    sectionLayout->addWidget(titleLabel);
    if (!summary.trimmed().isEmpty()) {
        sectionLayout->addWidget(summaryLabel);
    }

    return section;
}

QWidget* SettingsPage::createInfoRow(const QString& label, QLabel** valueLabel, bool wrapValue)
{
    auto* row = new QWidget(contentWidget_);
    row->setObjectName(QStringLiteral("settingsInfoRow"));

    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(12, 9, 12, 9);
    rowLayout->setSpacing(14);

    auto* keyLabel = new QLabel(label, row);
    keyLabel->setObjectName(QStringLiteral("settingsInfoLabel"));
    keyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    keyLabel->setMinimumWidth(kInfoLabelMinWidth);
    keyLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    auto* value = new QLabel(row);
    value->setObjectName(QStringLiteral("settingsInfoValue"));
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

QWidget* SettingsPage::buildSoftwareInfoSection()
{
    auto* section = createSection(QStringLiteral("软件信息"),
                                  QStringLiteral("当前应用版本、构建环境与运行模式。"),
                                  QStringLiteral("software"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    sectionLayout->addWidget(createInfoRow(QStringLiteral("应用名称"), &appNameValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("版本号"), &versionValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("构建信息"), &buildValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("运行模式"), &modeValueLabel_));

    auto* hintLabel = new QLabel(QStringLiteral("用于问题反馈时快速确认环境信息，不涉及业务配置修改。"), section);
    hintLabel->setObjectName(QStringLiteral("settingsHintText"));
    hintLabel->setWordWrap(true);
    sectionLayout->addWidget(hintLabel);

    return section;
}

QWidget* SettingsPage::buildLicenseInfoSection()
{
    auto* section =
        createSection(QStringLiteral("授权信息"), QStringLiteral("展示当前离线授权状态，仅用于查看，不在此页执行激活。"), QStringLiteral("license"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    sectionLayout->addWidget(createInfoRow(QStringLiteral("当前授权状态"), &licenseStatusValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("授权文件状态"), &licenseFileStatusValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("授权编号"), &licenseSerialValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("水印编号"), &licenseWatermarkValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("本机设备码"), &deviceFingerprintValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("授权文件路径"), &licensePathValueLabel_, true));

    licenseHintLabel_ = new QLabel(section);
    licenseHintLabel_->setObjectName(QStringLiteral("settingsHintText"));
    licenseHintLabel_->setWordWrap(true);
    sectionLayout->addWidget(licenseHintLabel_);

    return section;
}

QWidget* SettingsPage::buildDataInfoSection()
{
    auto* section =
        createSection(QStringLiteral("数据信息"), QStringLiteral("展示本地数据状态与目录，保持现有加载流程不变。"), QStringLiteral("data"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    sectionLayout->addWidget(createInfoRow(QStringLiteral("本地状态"), &dataStatusValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("数据目录"), &dataDirValueLabel_, true));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("内容记录数"), &contentStatsValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("索引文档数"), &indexStatsValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("模块数量"), &moduleStatsValueLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("数据源文件"), &dataSourceValueLabel_, true));

    sectionLayout->addWidget(createInfoRow(QStringLiteral("日志目录"), &logDirValueLabel_, true));

    dataHintLabel_ = new QLabel(section);
    dataHintLabel_->setObjectName(QStringLiteral("settingsHintText"));
    dataHintLabel_->setWordWrap(true);
    sectionLayout->addWidget(dataHintLabel_);

    auto* actionRow = new QWidget(section);
    actionRow->setObjectName(QStringLiteral("settingsInfoRow"));

    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(12, 9, 12, 9);
    actionLayout->setSpacing(14);

    auto* actionLabel = new QLabel(QStringLiteral("目录操作"), actionRow);
    actionLabel->setObjectName(QStringLiteral("settingsInfoLabel"));
    actionLabel->setMinimumWidth(kInfoLabelMinWidth);

    openDataDirButton_ = new QPushButton(QStringLiteral("打开数据目录"), actionRow);
    openDataDirButton_->setObjectName(QStringLiteral("settingsSecondaryButton"));
    openDataDirButton_->setCursor(Qt::PointingHandCursor);

    actionLayout->addWidget(actionLabel);
    actionLayout->addWidget(openDataDirButton_, 0, Qt::AlignLeft);
    actionLayout->addStretch(1);
    sectionLayout->addWidget(actionRow);

    connect(openDataDirButton_, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::dataDir()));
    });

    auto* logActionRow = new QWidget(section);
    logActionRow->setObjectName(QStringLiteral("settingsInfoRow"));

    auto* logActionLayout = new QHBoxLayout(logActionRow);
    logActionLayout->setContentsMargins(12, 9, 12, 9);
    logActionLayout->setSpacing(14);

    auto* logActionLabel = new QLabel(QStringLiteral("日志操作"), logActionRow);
    logActionLabel->setObjectName(QStringLiteral("settingsInfoLabel"));
    logActionLabel->setMinimumWidth(kInfoLabelMinWidth);

    openLogDirButton_ = new QPushButton(QStringLiteral("打开日志目录"), logActionRow);
    openLogDirButton_->setObjectName(QStringLiteral("settingsSecondaryButton"));
    openLogDirButton_->setCursor(Qt::PointingHandCursor);

    logActionLayout->addWidget(logActionLabel);
    logActionLayout->addWidget(openLogDirButton_, 0, Qt::AlignLeft);
    logActionLayout->addStretch(1);
    sectionLayout->addWidget(logActionRow);

    connect(openLogDirButton_, &QPushButton::clicked, this, [this]() {
        const QString rawLogDir = logging::Logger::instance().logDirectory().trimmed();
        if (rawLogDir.isEmpty()) {
            LOG_WARN(LogCategory::Config, QStringLiteral("open_log_dir aborted reason=empty_log_directory"));
            QMessageBox::warning(
                this, QStringLiteral("打开日志目录"), QStringLiteral("当前日志目录为空，请先启动应用后重试。"));
            return;
        }

        QDir logDir(rawLogDir);
        if (!logDir.exists() && !logDir.mkpath(QStringLiteral("."))) {
            const QString normalized = QDir::toNativeSeparators(QDir::cleanPath(rawLogDir));
            LOG_ERROR(LogCategory::FileIo,
                      QStringLiteral("open_log_dir failed reason=mkdir_failed path=%1").arg(normalized));
            QMessageBox::warning(this,
                                 QStringLiteral("打开日志目录"),
                                 QStringLiteral("日志目录不存在且创建失败：%1").arg(normalized));
            return;
        }

        const QString resolvedDir = QDir::toNativeSeparators(logDir.absolutePath());
        const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(logDir.absolutePath()));
        if (!opened) {
            LOG_ERROR(LogCategory::FileIo,
                      QStringLiteral("open_log_dir failed reason=open_url_failed path=%1").arg(resolvedDir));
            QMessageBox::warning(
                this, QStringLiteral("打开日志目录"), QStringLiteral("无法打开日志目录：%1").arg(resolvedDir));
            return;
        }

        LOG_INFO(LogCategory::Config, QStringLiteral("open_log_dir success path=%1").arg(resolvedDir));
    });

    return section;
}

QWidget* SettingsPage::buildHelpSection()
{
    auto* section =
        createSection(QStringLiteral("使用帮助"), QStringLiteral("以可执行入口和说明为主，不堆叠冗长占位文案。"), QStringLiteral("help"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    sectionLayout->addWidget(createInfoRow(QStringLiteral("使用说明"), &helpTextLabel_, true));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("快捷键"), &shortcutHintLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("文档入口"), &helpDocLabel_, true));

    auto* actionRow = new QWidget(section);
    actionRow->setObjectName(QStringLiteral("settingsInfoRow"));
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(12, 9, 12, 9);
    actionLayout->setSpacing(14);

    auto* actionLabel = new QLabel(QStringLiteral("快速查看"), actionRow);
    actionLabel->setObjectName(QStringLiteral("settingsInfoLabel"));
    actionLabel->setMinimumWidth(kInfoLabelMinWidth);

    openReadmeButton_ = new QPushButton(QStringLiteral("打开 README"), actionRow);
    openReadmeButton_->setObjectName(QStringLiteral("settingsLinkButton"));
    openReadmeButton_->setCursor(Qt::PointingHandCursor);

    actionLayout->addWidget(actionLabel);
    actionLayout->addWidget(openReadmeButton_, 0, Qt::AlignLeft);
    actionLayout->addStretch(1);
    sectionLayout->addWidget(actionRow);

    connect(openReadmeButton_, &QPushButton::clicked, this, [this]() {
        const QString appRoot = AppPaths::appRoot();
        const QString readmePath = QDir(appRoot).filePath(QStringLiteral("README.md"));
        const QString docsPath = QDir(appRoot).filePath(QStringLiteral("docs"));

        if (QFileInfo::exists(readmePath)) {
            const QString nativeReadmePath = normalizedNativePath(readmePath);
            if (openWithSystemDefaultApp(readmePath)) {
                LOG_INFO(LogCategory::Config, QStringLiteral("open_readme success method=system_default path=%1").arg(nativeReadmePath));
                return;
            }

            LOG_WARN(LogCategory::FileIo,
                     QStringLiteral("open_readme default_open_failed path=%1").arg(nativeReadmePath));

            if (QProcess::startDetached(QStringLiteral("notepad.exe"), QStringList{nativeReadmePath})) {
                LOG_INFO(LogCategory::Config, QStringLiteral("open_readme fallback_notepad path=%1").arg(nativeReadmePath));
                return;
            }

            const bool openedDocs = QFileInfo::exists(docsPath) && openPathInExplorer(docsPath);
            if (openedDocs) {
                LOG_WARN(LogCategory::Config,
                         QStringLiteral("open_readme fallback_docs_opened reason=notepad_failed docs=%1")
                             .arg(normalizedNativePath(docsPath)));
                return;
            }

            QMessageBox::warning(this,
                                 QStringLiteral("打开 README"),
                                 QStringLiteral("无法打开 README。\n路径：%1\n已尝试：系统默认程序、记事本。")
                                     .arg(nativeReadmePath));
            return;
        }

        if (QFileInfo::exists(docsPath) && openPathInExplorer(docsPath)) {
            LOG_WARN(LogCategory::Config,
                     QStringLiteral("open_readme missing_readme fallback_docs_opened docs=%1").arg(normalizedNativePath(docsPath)));
            return;
        }

        QMessageBox::warning(this,
                             QStringLiteral("打开 README"),
                             QStringLiteral("README 不存在，且无法打开 docs 目录。\n预期 README 路径：%1")
                                 .arg(normalizedNativePath(readmePath)));
    });

    return section;
}

QWidget* SettingsPage::buildFeedbackSection()
{
    auto* section =
        createSection(QStringLiteral("联系与反馈"), QStringLiteral("反馈时建议附上版本号、数据状态和复现步骤。"), QStringLiteral("feedback"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    sectionLayout->addWidget(createInfoRow(QStringLiteral("反馈邮箱"), &feedbackEmailLabel_));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("建议附带"), &feedbackGuideLabel_, true));
    sectionLayout->addWidget(createInfoRow(QStringLiteral("问题类型"), &feedbackIssueLabel_, true));

    auto* actionRow = new QWidget(section);
    actionRow->setObjectName(QStringLiteral("settingsInfoRow"));
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(12, 9, 12, 9);
    actionLayout->setSpacing(14);

    auto* actionLabel = new QLabel(QStringLiteral("反馈操作"), actionRow);
    actionLabel->setObjectName(QStringLiteral("settingsInfoLabel"));
    actionLabel->setMinimumWidth(kInfoLabelMinWidth);

    copyFeedbackEmailButton_ = new QPushButton(QStringLiteral("复制邮箱"), actionRow);
    copyFeedbackEmailButton_->setObjectName(QStringLiteral("settingsLinkButton"));
    copyFeedbackEmailButton_->setCursor(Qt::PointingHandCursor);

    actionLayout->addWidget(actionLabel);
    actionLayout->addWidget(copyFeedbackEmailButton_, 0, Qt::AlignLeft);
    actionLayout->addStretch(1);
    sectionLayout->addWidget(actionRow);

    connect(copyFeedbackEmailButton_, &QPushButton::clicked, this, [this]() {
        if (QGuiApplication::clipboard() != nullptr) {
            QGuiApplication::clipboard()->setText(kFeedbackEmail);
        }
        copyFeedbackEmailButton_->setText(QStringLiteral("邮箱已复制"));
    });

    return section;
}

QWidget* SettingsPage::buildExpansionHintSection()
{
    auto* section = createSection(QStringLiteral("扩展预留"),
                                  QStringLiteral("更多真实设置项将逐步接入，当前版本不展示未接入开关。"),
                                  QStringLiteral("future"));
    auto* sectionLayout = static_cast<QVBoxLayout*>(section->layout());

    sectionLayout->addWidget(createInfoRow(QStringLiteral("后续方向"), &expansionHintLabel_, true));

    auto* hintLabel = new QLabel(
        QStringLiteral("可扩展方向：数据目录切换、主题/缩放、快捷键帮助、日志入口、激活状态摘要。"), section);
    hintLabel->setObjectName(QStringLiteral("settingsHintText"));
    hintLabel->setWordWrap(true);
    sectionLayout->addWidget(hintLabel);

    return section;
}

void SettingsPage::reloadData()
{
    if (titleLabel_ == nullptr) {
        return;
    }

    appNameValueLabel_->setText(UiConstants::kAppTitle);
    versionValueLabel_->setText(UiConstants::kStatusVersion);
    buildValueLabel_->setText(QStringLiteral("%1 · Qt %2").arg(buildTypeText(), QString::fromLatin1(qVersion())));
    modeValueLabel_->setText(QStringLiteral("%1 · 搜索与详情均使用本地数据").arg(UiConstants::kStatusOffline));

    if (licenseService_ != nullptr) {
        const license::LicenseState licenseState = licenseService_->currentState();
        licenseStatusValueLabel_->setText(licenseState.isFull ? QStringLiteral("正式版") : QStringLiteral("体验版"));
        licenseFileStatusValueLabel_->setText(licenseFileStatusText(licenseState));
        licenseSerialValueLabel_->setText(licenseState.licenseSerial.trimmed().isEmpty() ? QStringLiteral("—")
                                                                                          : licenseState.licenseSerial.trimmed());
        licenseWatermarkValueLabel_->setText(licenseState.watermarkId.trimmed().isEmpty() ? QStringLiteral("—")
                                                                                            : licenseState.watermarkId.trimmed());
        deviceFingerprintValueLabel_->setText(licenseState.deviceFingerprint.trimmed().isEmpty()
                                                  ? QStringLiteral("—")
                                                  : licenseState.deviceFingerprint.trimmed());

        const QString licensePath =
            licenseState.licenseFilePath.trimmed().isEmpty() ? QDir::toNativeSeparators(licenseService_->licenseFilePath())
                                                             : QDir::toNativeSeparators(licenseState.licenseFilePath);
        licensePathValueLabel_->setText(licensePath);
        licensePathValueLabel_->setToolTip(licensePath);

        const QString message = licenseState.message.trimmed();
        const QString technical = licenseState.technicalReason.trimmed();
        if (!message.isEmpty() && !technical.isEmpty()) {
            licenseHintLabel_->setText(QStringLiteral("%1（%2）").arg(message, technical));
        } else if (!message.isEmpty()) {
            licenseHintLabel_->setText(message);
        } else if (!technical.isEmpty()) {
            licenseHintLabel_->setText(technical);
        } else {
            licenseHintLabel_->setText(QStringLiteral("授权状态正常。"));
        }
    } else {
        licenseStatusValueLabel_->setText(QStringLiteral("体验版"));
        licenseFileStatusValueLabel_->setText(QStringLiteral("未找到"));
        licenseSerialValueLabel_->setText(QStringLiteral("—"));
        licenseWatermarkValueLabel_->setText(QStringLiteral("—"));
        deviceFingerprintValueLabel_->setText(QStringLiteral("—"));
        licensePathValueLabel_->setText(
            QDir::toNativeSeparators(QDir(AppPaths::licenseDir()).filePath(QStringLiteral("license.dat"))));
        licenseHintLabel_->setText(QStringLiteral("授权服务未接入，默认按体验版运行。"));
    }

    const QString dataDir = QDir::toNativeSeparators(AppPaths::dataDir());
    dataStatusValueLabel_->setText(buildDataStatusText());
    dataDirValueLabel_->setText(dataDir);
    dataDirValueLabel_->setToolTip(dataDir);
    const QString logDir = QDir::toNativeSeparators(logging::Logger::instance().logDirectory().trimmed());
    const QString displayLogDir = logDir.trimmed().isEmpty() ? QStringLiteral("当前未配置") : logDir;
    logDirValueLabel_->setText(displayLogDir);
    logDirValueLabel_->setToolTip(displayLogDir);

    if (contentLoaded_ && contentRepository_ != nullptr) {
        contentStatsValueLabel_->setText(QStringLiteral("%1 条").arg(static_cast<qlonglong>(contentRepository_->size())));
    } else {
        contentStatsValueLabel_->setText(QStringLiteral("未加载"));
    }

    if (indexLoaded_ && indexRepository_ != nullptr) {
        indexStatsValueLabel_->setText(QStringLiteral("%1 条").arg(static_cast<qlonglong>(indexRepository_->docCount())));
    } else {
        indexStatsValueLabel_->setText(QStringLiteral("未加载"));
    }

    const qsizetype indexModuleCount = indexRepository_ == nullptr ? 0 : indexRepository_->modules().size();
    const qsizetype contentModuleCount = contentRepository_ == nullptr ? 0 : contentRepository_->modules().size();
    const qsizetype moduleCount = std::max(indexModuleCount, contentModuleCount);
    if (moduleCount > 0) {
        moduleStatsValueLabel_->setText(QStringLiteral("%1 个").arg(static_cast<qlonglong>(moduleCount)));
    } else if (indexLoaded_ || contentLoaded_) {
        moduleStatsValueLabel_->setText(QStringLiteral("0 个"));
    } else {
        moduleStatsValueLabel_->setText(QStringLiteral("未加载"));
    }

    const QString contentPathFallback = QDir(AppPaths::dataDir()).filePath(QStringLiteral("canonical_content_v2.json"));
    const QString indexPathFallback = QDir(AppPaths::dataDir()).filePath(QStringLiteral("backend_search_index.json"));
    const QString contentPath = resolvedPath(contentRepository_ == nullptr ? QString() : contentRepository_->activeContentPath(),
                                             contentPathFallback);
    const QString indexPath =
        resolvedPath(indexRepository_ == nullptr ? QString() : indexRepository_->activeIndexPath(), indexPathFallback);

    const QString sourceText = QStringLiteral("content: %1\nindex: %2").arg(contentPath, indexPath);
    dataSourceValueLabel_->setText(sourceText);
    dataSourceValueLabel_->setToolTip(sourceText);
    dataHintLabel_->setText(buildDataHintText());

    helpTextLabel_->setText(QStringLiteral("建议先在搜索页检索，再把高频结论加入收藏，最后通过最近搜索回访。"));
    shortcutHintLabel_->setText(QStringLiteral("当前版本未提供全局快捷键，后续补充常用操作提示。"));
    const QString helpPathText =
        QStringLiteral("README：%1\n文档目录：%2")
            .arg(resolvedPath(QDir(AppPaths::appRoot()).filePath(QStringLiteral("README.md")), QString()),
                 resolvedPath(QDir(AppPaths::appRoot()).filePath(QStringLiteral("docs")), QString()));
    helpDocLabel_->setText(helpPathText);
    helpDocLabel_->setToolTip(helpPathText);

    feedbackEmailLabel_->setText(QStringLiteral("%1（当前占位，待确认）").arg(kFeedbackEmail));
    feedbackGuideLabel_->setText(QStringLiteral("请附上版本号、数据状态、复现步骤和截图，便于快速定位问题。"));
    feedbackIssueLabel_->setText(QStringLiteral("检索结果异常 / 内容缺失 / 详情渲染问题 / 激活相关问题。"));

    expansionHintLabel_->setText(QStringLiteral("将逐步开放真实设置项，不再依赖静态说明扩展页面。"));
}

QString SettingsPage::buildDataStatusText() const
{
    if (indexLoaded_ && contentLoaded_) {
        return QStringLiteral("已就绪（索引与内容数据均可用）");
    }
    if (indexLoaded_ && !contentLoaded_) {
        return QStringLiteral("部分可用（索引可用，内容未加载）");
    }
    if (!indexLoaded_ && contentLoaded_) {
        return QStringLiteral("部分可用（内容可用，索引未加载）");
    }
    return QStringLiteral("未就绪（索引与内容均未加载）");
}

QString SettingsPage::buildDataHintText() const
{
    QStringList hints;

    if (!indexLoaded_ && indexRepository_ != nullptr) {
        const QString reason = indexRepository_->diagnostics().fatalError.trimmed();
        hints.push_back(reason.isEmpty() ? QStringLiteral("索引加载失败，请检查 backend_search_index.json。")
                                         : QStringLiteral("索引加载失败：%1").arg(reason));
    }

    if (!contentLoaded_ && contentRepository_ != nullptr) {
        const QString reason = contentRepository_->diagnostics().fatalError.trimmed();
        hints.push_back(reason.isEmpty() ? QStringLiteral("内容加载失败，请检查 canonical_content_v2.json。")
                                         : QStringLiteral("内容加载失败：%1").arg(reason));
    }

    if (hints.isEmpty()) {
        return QStringLiteral("数据加载沿用主窗口启动流程；本页仅展示状态与目录信息。");
    }

    return hints.join(QStringLiteral(" "));
}

QString SettingsPage::resolvedPath(const QString& candidate, const QString& fallback) const
{
    const QString path = candidate.trimmed().isEmpty() ? fallback.trimmed() : candidate.trimmed();
    if (path.isEmpty()) {
        return QStringLiteral("当前未提供");
    }
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}
