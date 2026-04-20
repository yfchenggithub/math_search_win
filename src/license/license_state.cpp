#include "license/license_state.h"

namespace license {

QString licenseStatusCode(LicenseStatus status)
{
    switch (status) {
    case LicenseStatus::Missing:
        return QStringLiteral("missing");
    case LicenseStatus::Trial:
        return QStringLiteral("trial");
    case LicenseStatus::ValidFull:
        return QStringLiteral("valid_full");
    case LicenseStatus::Invalid:
        return QStringLiteral("invalid");
    case LicenseStatus::ReadError:
        return QStringLiteral("read_error");
    case LicenseStatus::ParseError:
        return QStringLiteral("parse_error");
    case LicenseStatus::DeviceMismatch:
        return QStringLiteral("device_mismatch");
    case LicenseStatus::ActivationCodeInvalid:
        return QStringLiteral("activation_code_invalid");
    case LicenseStatus::WriteError:
        return QStringLiteral("write_error");
    case LicenseStatus::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

QString licenseStatusDisplayText(LicenseStatus status)
{
    switch (status) {
    case LicenseStatus::Missing:
    case LicenseStatus::Trial:
        return QStringLiteral("未激活");
    case LicenseStatus::ValidFull:
        return QStringLiteral("已激活");
    case LicenseStatus::Invalid:
        return QStringLiteral("授权无效");
    case LicenseStatus::ReadError:
        return QStringLiteral("读取失败");
    case LicenseStatus::ParseError:
        return QStringLiteral("解析失败");
    case LicenseStatus::DeviceMismatch:
        return QStringLiteral("设备不匹配");
    case LicenseStatus::ActivationCodeInvalid:
        return QStringLiteral("激活失败");
    case LicenseStatus::WriteError:
        return QStringLiteral("写入失败");
    case LicenseStatus::Unknown:
    default:
        return QStringLiteral("状态未知");
    }
}

}  // namespace license

