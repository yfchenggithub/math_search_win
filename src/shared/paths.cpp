#include "shared/paths.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QCoreApplication>
#include <QDir>

QString AppPaths::appRoot()
{
    QDir currentDir(QDir::currentPath());
    if (currentDir.exists(QStringLiteral("src")) && currentDir.exists(QStringLiteral("resources"))) {
        const QString resolved = currentDir.absolutePath();
        LOG_DEBUG(LogCategory::Config, QStringLiteral("appRoot resolved from currentPath=%1").arg(resolved));
        return resolved;
    }

    QDir probeDir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 6; ++depth) {
        if (probeDir.exists(QStringLiteral("src")) && probeDir.exists(QStringLiteral("resources"))) {
            const QString resolved = probeDir.absolutePath();
            LOG_DEBUG(LogCategory::Config,
                      QStringLiteral("appRoot resolved from applicationDir depth=%1 path=%2").arg(depth).arg(resolved));
            return resolved;
        }
        if (!probeDir.cdUp()) {
            break;
        }
    }

    const QString fallback = QDir::currentPath();
    LOG_WARN(LogCategory::Config,
             QStringLiteral("appRoot fallback to currentPath=%1 (project markers src/resources not found)").arg(fallback));
    return fallback;
}

QString AppPaths::dataDir()
{
    const QString path = QDir(appRoot()).filePath(QStringLiteral("data"));
    LOG_DEBUG(LogCategory::FileIo, QStringLiteral("dataDir=%1").arg(path));
    return path;
}

QString AppPaths::cacheDir()
{
    const QString path = QDir(appRoot()).filePath(QStringLiteral("cache"));
    LOG_DEBUG(LogCategory::FileIo, QStringLiteral("cacheDir=%1").arg(path));
    return path;
}

QString AppPaths::licenseDir()
{
    const QString path = QDir(appRoot()).filePath(QStringLiteral("license"));
    LOG_DEBUG(LogCategory::FileIo, QStringLiteral("licenseDir=%1").arg(path));
    return path;
}
