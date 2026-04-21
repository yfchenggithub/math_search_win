#pragma once

#include <QString>

namespace license {

class DeviceFingerprintService final {
public:
    explicit DeviceFingerprintService(QString fixedFingerprint = QString());
    QString deviceFingerprint() const;

private:
    QString buildDeviceFingerprint() const;
    static QString formatFingerprint(const QByteArray& sha256HexUpper);

private:
    QString fixedFingerprint_;
    mutable QString cachedFingerprint_;
};

}  // namespace license
