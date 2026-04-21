#include "license/device_fingerprint_service.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QCryptographicHash>
#include <QSysInfo>
#include <QStringList>

namespace license {
namespace {

QString nonEmptyOrFallback(const QString& value, const QString& fallback)
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

}  // namespace

DeviceFingerprintService::DeviceFingerprintService(QString fixedFingerprint)
    : fixedFingerprint_(fixedFingerprint.trimmed())
{
}

QString DeviceFingerprintService::deviceFingerprint() const
{
    if (!fixedFingerprint_.isEmpty()) {
        return fixedFingerprint_;
    }

    if (!cachedFingerprint_.isEmpty()) {
        return cachedFingerprint_;
    }

    cachedFingerprint_ = buildDeviceFingerprint();
    LOG_INFO(LogCategory::Config,
             QStringLiteral("device fingerprint generated value=%1").arg(cachedFingerprint_));
    return cachedFingerprint_;
}

QString DeviceFingerprintService::buildDeviceFingerprint() const
{
    QStringList rawParts;

    const QByteArray machineId = QSysInfo::machineUniqueId();
    if (!machineId.isEmpty()) {
        rawParts.push_back(QString::fromLatin1(machineId.toHex()));
    }

    const QByteArray bootId = QSysInfo::bootUniqueId();
    if (!bootId.isEmpty()) {
        rawParts.push_back(QString::fromLatin1(bootId.toHex()));
    }

    rawParts.push_back(nonEmptyOrFallback(QSysInfo::machineHostName(), QStringLiteral("host-unknown")));
    rawParts.push_back(nonEmptyOrFallback(qEnvironmentVariable("COMPUTERNAME"), QStringLiteral("computer-unknown")));
    rawParts.push_back(nonEmptyOrFallback(QSysInfo::productType(), QStringLiteral("product-unknown")));
    rawParts.push_back(nonEmptyOrFallback(QSysInfo::productVersion(), QStringLiteral("version-unknown")));
    rawParts.push_back(nonEmptyOrFallback(QSysInfo::kernelType(), QStringLiteral("kernel-unknown")));
    rawParts.push_back(nonEmptyOrFallback(QSysInfo::kernelVersion(), QStringLiteral("kernel-ver-unknown")));
    rawParts.push_back(nonEmptyOrFallback(QSysInfo::currentCpuArchitecture(), QStringLiteral("cpu-unknown")));
    rawParts.push_back(nonEmptyOrFallback(QSysInfo::buildAbi(), QStringLiteral("abi-unknown")));

    const QString sourceText = rawParts.join(QStringLiteral("|"));
    const QByteArray hash =
        QCryptographicHash::hash(sourceText.toUtf8(), QCryptographicHash::Sha256).toHex().toUpper();
    if (hash.isEmpty()) {
        return QStringLiteral("UNKN-UNKN-UNKN-UNKN");
    }

    return formatFingerprint(hash);
}

QString DeviceFingerprintService::formatFingerprint(const QByteArray& sha256HexUpper)
{
    QByteArray token = sha256HexUpper.left(16);
    while (token.size() < 16) {
        token.append('0');
    }

    QString text = QString::fromLatin1(token);
    return QStringLiteral("%1-%2-%3-%4")
        .arg(text.mid(0, 4), text.mid(4, 4), text.mid(8, 4), text.mid(12, 4));
}

}  // namespace license
