#include "shared/paths.h"

#include <QCoreApplication>
#include <QDir>

QString AppPaths::appRoot()
{
    QDir currentDir(QDir::currentPath());
    if (currentDir.exists(QStringLiteral("src")) && currentDir.exists(QStringLiteral("resources"))) {
        return currentDir.absolutePath();
    }

    QDir probeDir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 6; ++depth) {
        if (probeDir.exists(QStringLiteral("src")) && probeDir.exists(QStringLiteral("resources"))) {
            return probeDir.absolutePath();
        }
        if (!probeDir.cdUp()) {
            break;
        }
    }

    return QDir::currentPath();
}

QString AppPaths::dataDir()
{
    return QDir(appRoot()).filePath(QStringLiteral("data"));
}

QString AppPaths::cacheDir()
{
    return QDir(appRoot()).filePath(QStringLiteral("cache"));
}

QString AppPaths::licenseDir()
{
    return QDir(appRoot()).filePath(QStringLiteral("license"));
}

