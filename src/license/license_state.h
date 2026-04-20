#pragma once

#include <QString>
#include <QStringList>
#include <QMetaType>

namespace license {

enum class LicenseStatus {
    Missing,
    Trial,
    ValidFull,
    Invalid,
    ReadError,
    ParseError,
    DeviceMismatch,
    ActivationCodeInvalid,
    WriteError,
    Unknown
};

struct LicenseState {
    LicenseStatus status = LicenseStatus::Unknown;
    bool isTrial = true;
    bool isFull = false;
    bool licenseFileExists = false;
    QString licenseFilePath;
    QString licenseSerial;
    QString watermarkId;
    QString edition;
    QString deviceFingerprint;
    QString boundDeviceFingerprint;
    QString issuedAt;
    QString expireAt;
    QString message;
    QString technicalReason;
    QStringList enabledFeatures;
};

QString licenseStatusCode(LicenseStatus status);
QString licenseStatusDisplayText(LicenseStatus status);

}  // namespace license

Q_DECLARE_METATYPE(license::LicenseState)

