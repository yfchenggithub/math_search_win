#pragma once

#include "license/device_fingerprint_service.h"
#include "license/license_state.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>

namespace license {

struct LicenseReadResult {
    bool exists = false;
    bool ok = false;
    QByteArray content;
    QString errorMessage;
};

struct LicenseParseResult {
    bool ok = false;
    QMap<QString, QString> fields;
    QString errorMessage;
};

struct LicenseValidationResult {
    bool ok = false;
    LicenseStatus status = LicenseStatus::Unknown;
    QString errorMessage;
    QString technicalReason;
    QString serial;
    QString watermark;
    QString edition;
    QString boundDeviceFingerprint;
    QString issuedAt;
    QString expireAt;
    QStringList enabledFeatures;
};

class LicenseService final : public QObject {
    Q_OBJECT

public:
    explicit LicenseService(const DeviceFingerprintService* deviceFingerprintService, QObject* parent = nullptr);

    void initialize();
    void reload();
    LicenseState currentState() const;
    QString licenseFilePath() const;
    bool writeLicenseFile(const QByteArray& content, QString* errorMessage = nullptr);
    bool clearInvalidLicenseIfNeeded(QString* errorMessage = nullptr);

    LicenseReadResult readLicenseFile() const;
    LicenseParseResult parseLicenseContent(const QByteArray& content) const;
    LicenseValidationResult validateLicense(const QMap<QString, QString>& fields) const;

signals:
    void licenseStateChanged(const license::LicenseState& state);

private:
    void setState(const LicenseState& state, bool emitSignal);
    LicenseState buildTrialFallbackState(LicenseStatus status,
                                         const QString& message,
                                         const QString& technicalReason,
                                         bool fileExists) const;
    static bool verifyLicenseSignature(const QMap<QString, QString>& fields, QString* errorMessage);
    static QStringList parseCsv(const QString& csv);
    static QString field(const QMap<QString, QString>& fields, const QString& key);

private:
    const DeviceFingerprintService* deviceFingerprintService_ = nullptr;
    LicenseState state_;
};

}  // namespace license
