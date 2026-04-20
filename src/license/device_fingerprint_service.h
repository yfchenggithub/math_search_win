#pragma once

#include <QString>

namespace license {

class DeviceFingerprintService final {
public:
    QString deviceFingerprint() const;

private:
    QString buildDeviceFingerprint() const;
    static QString formatFingerprint(const QByteArray& sha256HexUpper);

private:
    mutable QString cachedFingerprint_;
};

}  // namespace license

