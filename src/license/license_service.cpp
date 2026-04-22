#include "license/license_service.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "license/feature_gate.h"
#include "shared/paths.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace license {
namespace {

QString statusMessageForParseOrValidation(const QString& fallback, const QString& details)
{
    const QString trimmed = details.trimmed();
    if (trimmed.isEmpty()) {
        return fallback;
    }
    return QStringLiteral("%1（%2）").arg(fallback, trimmed);
}

bool validateLicenseExpiration(const QString& expireAt, QString* errorMessage, QString* technicalReason)
{
    const QString text = expireAt.trimmed();
    if (text.isEmpty()) {
        return true;
    }

    const QDate expireDate = QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
    if (!expireDate.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("授权到期日期格式无效（expire_at）。");
        }
        if (technicalReason != nullptr) {
            *technicalReason = QStringLiteral("expire_at invalid format value=%1").arg(text);
        }
        return false;
    }

    const QDate today = QDate::currentDate();
    if (today > expireDate) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("授权已过期。");
        }
        if (technicalReason != nullptr) {
            *technicalReason = QStringLiteral("license expired expire_at=%1 today=%2")
                                   .arg(text, today.toString(QStringLiteral("yyyy-MM-dd")));
        }
        return false;
    }

    return true;
}

}  // namespace

LicenseService::LicenseService(const DeviceFingerprintService* deviceFingerprintService, QObject* parent)
    : QObject(parent), deviceFingerprintService_(deviceFingerprintService)
{
    state_.status = LicenseStatus::Trial;
    state_.isTrial = true;
    state_.isFull = false;
    state_.licenseFilePath = licenseFilePath();
    state_.deviceFingerprint = deviceFingerprintService_ == nullptr ? QString()
                                                                   : deviceFingerprintService_->deviceFingerprint();
    state_.enabledFeatures = FeatureGate::featureKeysFromList(FeatureGate::trialFeatures());
    state_.message = QStringLiteral("未检测到有效授权，当前为体验版。");
}

void LicenseService::initialize()
{
    LOG_INFO(LogCategory::Config, QStringLiteral("license initialize begin path=%1").arg(licenseFilePath()));
    reload();
}

void LicenseService::reload()
{
    const QString licenseDirectory = AppPaths::licenseDir();
    const QFileInfo licenseDirInfo(licenseDirectory);
    if (!licenseDirInfo.exists()) {
        setState(buildTrialFallbackState(LicenseStatus::Missing,
                                         QStringLiteral("未检测到授权目录，当前为体验版。"),
                                         QStringLiteral("license directory missing path=%1").arg(licenseDirectory),
                                         false),
                 true);
        return;
    }
    if (!licenseDirInfo.isDir()) {
        setState(buildTrialFallbackState(LicenseStatus::ReadError,
                                         QStringLiteral("授权目录路径异常，当前为体验版。"),
                                         QStringLiteral("license path is not a directory path=%1").arg(licenseDirectory),
                                         false),
                 true);
        return;
    }

    const LicenseReadResult readResult = readLicenseFile();
    if (!readResult.exists) {
        setState(buildTrialFallbackState(LicenseStatus::Missing,
                                         QStringLiteral("未检测到授权文件，当前为体验版。"),
                                         QStringLiteral("license.dat not found"),
                                         false),
                 true);
        return;
    }

    if (!readResult.ok) {
        setState(buildTrialFallbackState(LicenseStatus::ReadError,
                                         statusMessageForParseOrValidation(QStringLiteral("授权文件读取失败，已降级为体验版。"),
                                                                           readResult.errorMessage),
                                         readResult.errorMessage,
                                         true),
                 true);
        return;
    }

    const LicenseParseResult parseResult = parseLicenseContent(readResult.content);
    if (!parseResult.ok) {
        setState(buildTrialFallbackState(LicenseStatus::ParseError,
                                         statusMessageForParseOrValidation(QStringLiteral("授权文件解析失败，已降级为体验版。"),
                                                                           parseResult.errorMessage),
                                         parseResult.errorMessage,
                                         true),
                 true);
        return;
    }

    const LicenseValidationResult validation = validateLicense(parseResult.fields);
    if (!validation.ok) {
        LicenseState fallback = buildTrialFallbackState(
            validation.status == LicenseStatus::Unknown ? LicenseStatus::Invalid : validation.status,
            statusMessageForParseOrValidation(QStringLiteral("授权无效，已降级为体验版。"), validation.errorMessage),
            validation.technicalReason.isEmpty() ? validation.errorMessage : validation.technicalReason,
            true);
        fallback.licenseSerial = validation.serial;
        fallback.watermarkId = validation.watermark;
        fallback.edition = validation.edition;
        fallback.boundDeviceFingerprint = validation.boundDeviceFingerprint;
        fallback.issuedAt = validation.issuedAt;
        fallback.expireAt = validation.expireAt;
        setState(fallback, true);
        return;
    }

    LicenseState fullState;
    fullState.status = LicenseStatus::ValidFull;
    fullState.isTrial = false;
    fullState.isFull = true;
    fullState.licenseFileExists = true;
    fullState.licenseFilePath = licenseFilePath();
    fullState.licenseSerial = validation.serial;
    fullState.watermarkId = validation.watermark;
    fullState.edition = validation.edition;
    fullState.deviceFingerprint = deviceFingerprintService_ == nullptr ? QString()
                                                                      : deviceFingerprintService_->deviceFingerprint();
    fullState.boundDeviceFingerprint = validation.boundDeviceFingerprint;
    fullState.issuedAt = validation.issuedAt;
    fullState.expireAt = validation.expireAt;
    fullState.message = QStringLiteral("已检测到有效正式版授权。");
    fullState.technicalReason = QStringLiteral("license.dat validation success");
    fullState.enabledFeatures = validation.enabledFeatures;
    if (fullState.enabledFeatures.isEmpty()) {
        fullState.enabledFeatures = FeatureGate::featureKeysFromList(FeatureGate::fullFeatures());
    }

    setState(fullState, true);
}

LicenseState LicenseService::currentState() const
{
    return state_;
}

QString LicenseService::licenseFilePath() const
{
    return QDir(AppPaths::licenseDir()).filePath(QStringLiteral("license.dat"));
}

bool LicenseService::writeLicenseFile(const QByteArray& content, QString* errorMessage)
{
    const QString path = licenseFilePath();
    LOG_INFO(LogCategory::FileIo,
             QStringLiteral("license write begin path=%1 bytes=%2").arg(path).arg(content.size()));

    QDir licenseDir(AppPaths::licenseDir());
    if (!licenseDir.exists() && !licenseDir.mkpath(QStringLiteral("."))) {
        const QString error = QStringLiteral("无法创建授权目录：%1").arg(licenseDir.path());
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        LOG_ERROR(LogCategory::FileIo, QStringLiteral("license write failed reason=%1").arg(error));
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        const QString error = QStringLiteral("无法写入授权文件：%1").arg(file.errorString());
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        LOG_ERROR(LogCategory::FileIo, QStringLiteral("license write failed reason=%1").arg(error));
        return false;
    }

    const qint64 written = file.write(content);
    if (written != content.size()) {
        const QString error = QStringLiteral("授权文件写入不完整（%1/%2）。").arg(written).arg(content.size());
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        LOG_ERROR(LogCategory::FileIo, QStringLiteral("license write failed reason=%1").arg(error));
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        const QString error = QStringLiteral("授权文件写入提交失败：%1").arg(file.errorString());
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        LOG_ERROR(LogCategory::FileIo, QStringLiteral("license write failed reason=%1").arg(error));
        return false;
    }

    LOG_INFO(LogCategory::FileIo, QStringLiteral("license write success path=%1").arg(path));
    return true;
}

bool LicenseService::clearInvalidLicenseIfNeeded(QString* errorMessage)
{
    if (state_.isFull || !state_.licenseFileExists || state_.licenseFilePath.trimmed().isEmpty()) {
        return true;
    }

    QFile file(state_.licenseFilePath);
    if (!file.exists()) {
        return true;
    }
    if (!file.remove()) {
        const QString error = QStringLiteral("删除无效授权文件失败：%1").arg(file.errorString());
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        LOG_WARN(LogCategory::FileIo, QStringLiteral("license clear invalid failed reason=%1").arg(error));
        return false;
    }

    LOG_INFO(LogCategory::FileIo, QStringLiteral("invalid license removed path=%1").arg(state_.licenseFilePath));
    reload();
    return true;
}

LicenseReadResult LicenseService::readLicenseFile() const
{
    LicenseReadResult result;
    const QString path = licenseFilePath();
    const QFileInfo fileInfo(path);
    result.exists = fileInfo.exists() && fileInfo.isFile();

    if (!result.exists) {
        LOG_INFO(LogCategory::FileIo, QStringLiteral("license read skip reason=file_missing path=%1").arg(path));
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.ok = false;
        result.errorMessage = QStringLiteral("打开授权文件失败：%1").arg(file.errorString());
        LOG_ERROR(LogCategory::FileIo, QStringLiteral("license read failed path=%1 reason=%2").arg(path, result.errorMessage));
        return result;
    }

    result.content = file.readAll();
    result.ok = true;
    LOG_INFO(LogCategory::FileIo,
             QStringLiteral("license read success path=%1 bytes=%2").arg(path).arg(result.content.size()));
    return result;
}

LicenseParseResult LicenseService::parseLicenseContent(const QByteArray& content) const
{
    LicenseParseResult result;
    const QString text = QString::fromUtf8(content);
    const QStringList lines = text.split(QLatin1Char('\n'));

    int lineNumber = 0;
    for (const QString& rawLine : lines) {
        ++lineNumber;
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }

        const int index = line.indexOf(QLatin1Char('='));
        if (index <= 0) {
            result.errorMessage = QStringLiteral("第 %1 行缺少 key=value 格式。").arg(lineNumber);
            return result;
        }

        const QString key = line.left(index).trimmed().toLower();
        const QString value = line.mid(index + 1).trimmed();
        if (key.isEmpty()) {
            result.errorMessage = QStringLiteral("第 %1 行键名为空。").arg(lineNumber);
            return result;
        }
        result.fields.insert(key, value);
    }

    if (result.fields.isEmpty()) {
        result.errorMessage = QStringLiteral("授权文件为空或无有效字段。");
        return result;
    }

    result.ok = true;
    return result;
}

LicenseValidationResult LicenseService::validateLicense(const QMap<QString, QString>& fields) const
{
    LicenseValidationResult result;
    result.status = LicenseStatus::Invalid;

    const QString format = field(fields, QStringLiteral("format"));
    const QString product = field(fields, QStringLiteral("product"));
    const QString serial = field(fields, QStringLiteral("serial"));
    const QString watermark = field(fields, QStringLiteral("watermark"));
    const QString edition = field(fields, QStringLiteral("edition")).toLower();
    const QString device = field(fields, QStringLiteral("device"));
    const QString featuresRaw = field(fields, QStringLiteral("features"));

    result.serial = serial;
    result.watermark = watermark;
    result.edition = edition;
    result.boundDeviceFingerprint = device;
    result.issuedAt = field(fields, QStringLiteral("issued_at"));
    result.expireAt = field(fields, QStringLiteral("expire_at"));

    if (format != QStringLiteral("msw-license-v1")) {
        result.errorMessage = QStringLiteral("授权格式不受支持。");
        result.technicalReason = QStringLiteral("format mismatch");
        return result;
    }
    if (product != QStringLiteral("math_search_win")) {
        result.errorMessage = QStringLiteral("授权产品标识不匹配。");
        result.technicalReason = QStringLiteral("product mismatch");
        return result;
    }
    if (serial.isEmpty()) {
        result.errorMessage = QStringLiteral("授权编号缺失。");
        result.technicalReason = QStringLiteral("serial missing");
        return result;
    }
    if (watermark.isEmpty()) {
        result.errorMessage = QStringLiteral("水印编号缺失。");
        result.technicalReason = QStringLiteral("watermark missing");
        return result;
    }
    if (edition != QStringLiteral("full")) {
        result.errorMessage = QStringLiteral("授权类型无效（仅支持 full）。");
        result.technicalReason = QStringLiteral("edition is not full");
        return result;
    }

    const QString currentDevice = deviceFingerprintService_ == nullptr ? QString()
                                                                      : deviceFingerprintService_->deviceFingerprint();
    if (device.isEmpty()) {
        result.errorMessage = QStringLiteral("授权文件缺少绑定设备码。");
        result.technicalReason = QStringLiteral("device missing");
        return result;
    }
    if (!currentDevice.isEmpty() && device != currentDevice) {
        result.status = LicenseStatus::DeviceMismatch;
        result.errorMessage = QStringLiteral("授权绑定设备与当前设备不匹配。");
        result.technicalReason =
            QStringLiteral("device mismatch current=%1 bound=%2").arg(currentDevice, device);
        return result;
    }

    if (!validateLicenseExpiration(result.expireAt, &result.errorMessage, &result.technicalReason)) {
        return result;
    }

    const QStringList featureKeys = parseCsv(featuresRaw);
    for (const QString& key : featureKeys) {
        const std::optional<Feature> feature = FeatureGate::featureFromKey(key);
        if (!feature.has_value()) {
            LOG_WARN(LogCategory::Config,
                     QStringLiteral("license validate ignored unknown feature=%1").arg(key));
            continue;
        }
        result.enabledFeatures.push_back(FeatureGate::featureToKey(feature.value()));
    }
    result.enabledFeatures.removeDuplicates();
    if (result.enabledFeatures.isEmpty()) {
        result.errorMessage = QStringLiteral("授权功能集合为空或不可识别。");
        result.technicalReason = QStringLiteral("features empty");
        return result;
    }

    QString signatureError;
    if (!verifyLicenseSignature(fields, &signatureError)) {
        result.errorMessage = signatureError.trimmed().isEmpty() ? QStringLiteral("授权签名校验失败。")
                                                                 : signatureError.trimmed();
        result.technicalReason = QStringLiteral("license signature invalid");
        return result;
    }

    result.ok = true;
    result.status = LicenseStatus::ValidFull;
    return result;
}

void LicenseService::setState(const LicenseState& state, bool emitSignal)
{
    const bool changed =
        state_.status != state.status || state_.isTrial != state.isTrial || state_.isFull != state.isFull
        || state_.licenseFileExists != state.licenseFileExists || state_.licenseFilePath != state.licenseFilePath
        || state_.licenseSerial != state.licenseSerial || state_.watermarkId != state.watermarkId
        || state_.edition != state.edition || state_.deviceFingerprint != state.deviceFingerprint
        || state_.boundDeviceFingerprint != state.boundDeviceFingerprint || state_.issuedAt != state.issuedAt
        || state_.expireAt != state.expireAt || state_.message != state.message
        || state_.technicalReason != state.technicalReason || state_.enabledFeatures != state.enabledFeatures;

    state_ = state;

    LOG_INFO(LogCategory::Config,
             QStringLiteral("license state status=%1 full=%2 trial=%3 serial=%4 device_bound=%5")
                 .arg(licenseStatusCode(state_.status))
                 .arg(state_.isFull ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(state_.isTrial ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(state_.licenseSerial.isEmpty() ? QStringLiteral("-") : state_.licenseSerial)
                 .arg(state_.boundDeviceFingerprint.isEmpty() ? QStringLiteral("-") : state_.boundDeviceFingerprint));

    if (emitSignal && changed) {
        emit licenseStateChanged(state_);
    }
}

LicenseState LicenseService::buildTrialFallbackState(LicenseStatus status,
                                                     const QString& message,
                                                     const QString& technicalReason,
                                                     bool fileExists) const
{
    LicenseState fallback;
    fallback.status = status;
    fallback.isTrial = true;
    fallback.isFull = false;
    fallback.licenseFileExists = fileExists;
    fallback.licenseFilePath = licenseFilePath();
    fallback.deviceFingerprint = deviceFingerprintService_ == nullptr ? QString()
                                                                     : deviceFingerprintService_->deviceFingerprint();
    fallback.edition = QStringLiteral("trial");
    fallback.message = message;
    fallback.technicalReason = technicalReason;
    fallback.enabledFeatures = FeatureGate::featureKeysFromList(FeatureGate::trialFeatures());
    return fallback;
}

QStringList LicenseService::parseCsv(const QString& csv)
{
    const QStringList raw = csv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    QStringList normalized;
    normalized.reserve(raw.size());
    for (const QString& item : raw) {
        const QString value = item.trimmed().toLower();
        if (!value.isEmpty()) {
            normalized.push_back(value);
        }
    }
    normalized.removeDuplicates();
    return normalized;
}

bool LicenseService::verifyLicenseSignature(const QMap<QString, QString>& fields, QString* errorMessage)
{
    Q_UNUSED(fields);
    Q_UNUSED(errorMessage);
    // TODO: 当 license.dat 增加签名字段后，在此接入 verifyLicenseSignature(...) 真正校验逻辑。
    return true;
}

QString LicenseService::field(const QMap<QString, QString>& fields, const QString& key)
{
    return fields.value(key.toLower()).trimmed();
}

}  // namespace license
