#include "infrastructure/storage/local_storage_service.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "shared/paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QSaveFile>

namespace infrastructure::storage {

LocalStorageService::LocalStorageService(QString cacheDirPath)
{
    const QString normalizedPath = cacheDirPath.trimmed();
    if (normalizedPath.isEmpty()) {
        if (QCoreApplication::instance() != nullptr) {
            cacheDirPath_ = AppPaths::cacheDir();
        } else {
            cacheDirPath_ = QDir(QDir::currentPath()).filePath(QStringLiteral("cache"));
        }
    } else {
        cacheDirPath_ = QDir::fromNativeSeparators(QDir::cleanPath(normalizedPath));
    }
}

QString LocalStorageService::cacheDir() const
{
    return cacheDirPath_;
}

QString LocalStorageService::favoritesFilePath() const
{
    return QDir(cacheDirPath_).filePath(QStringLiteral("favorites.json"));
}

QString LocalStorageService::historyFilePath() const
{
    return QDir(cacheDirPath_).filePath(QStringLiteral("history.json"));
}

QString LocalStorageService::settingsFilePath() const
{
    return QDir(cacheDirPath_).filePath(QStringLiteral("settings.json"));
}

bool LocalStorageService::ensureCacheDirExists()
{
    if (cacheDirPath_.trimmed().isEmpty()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("cache directory path is empty"));
        return false;
    }

    const QFileInfo info(cacheDirPath_);
    if (info.exists() && !info.isDir()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("cache path exists but is not a directory path=%1").arg(cacheDirPath_));
        return false;
    }

    if (info.exists() && info.isDir()) {
        return true;
    }

    QDir parentDir = info.absoluteDir();
    if (!parentDir.exists() && !QDir().mkpath(parentDir.absolutePath())) {
        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("failed to create cache parent directory path=%1").arg(parentDir.absolutePath()));
        return false;
    }

    if (!parentDir.mkpath(info.fileName()) && !QDir(cacheDirPath_).exists()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("failed to create cache directory path=%1").arg(cacheDirPath_));
        return false;
    }

    LOG_INFO(LogCategory::FileIo, QStringLiteral("cache directory created path=%1").arg(cacheDirPath_));
    return true;
}

QJsonDocument LocalStorageService::readJsonFile(const QString& path, bool* ok) const
{
    if (ok != nullptr) {
        *ok = false;
    }

    const QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        LOG_INFO(LogCategory::FileIo, QStringLiteral("json file missing path=%1").arg(path));
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("failed to open json file for read path=%1").arg(path));
        return {};
    }

    const QByteArray payload = file.readAll();
    if (payload.trimmed().isEmpty()) {
        LOG_WARN(LogCategory::FileIo, QStringLiteral("json file is empty path=%1").arg(path));
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("failed to parse json path=%1 offset=%2 err=%3")
                     .arg(path)
                     .arg(parseError.offset)
                     .arg(parseError.errorString()));
        return {};
    }

    if (ok != nullptr) {
        *ok = true;
    }
    return document;
}

bool LocalStorageService::writeJsonFileAtomically(const QString& path, const QJsonDocument& doc)
{
    if (!ensureCacheDirExists()) {
        return false;
    }

    const QFileInfo fileInfo(path);
    QDir parentDir = fileInfo.dir();
    if (!parentDir.exists() && !QDir().mkpath(parentDir.absolutePath())) {
        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("failed to create parent directory path=%1").arg(parentDir.absolutePath()));
        return false;
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("failed to open json file for write path=%1 err=%2").arg(path, file.errorString()));
        return false;
    }

    const QByteArray payload = doc.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("failed to write json payload path=%1 err=%2").arg(path, file.errorString()));
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        const QString commitError = file.errorString();
        file.cancelWriting();
        LOG_WARN(LogCategory::FileIo,
                 QStringLiteral("atomic commit failed, fallback to direct write path=%1 err=%2").arg(path, commitError));

        QFile directFile(path);
        if (!directFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            LOG_WARN(LogCategory::FileIo,
                     QStringLiteral("fallback direct write open failed path=%1 err=%2").arg(path, directFile.errorString()));
            return false;
        }
        if (directFile.write(payload) != payload.size()) {
            LOG_WARN(LogCategory::FileIo,
                     QStringLiteral("fallback direct write failed path=%1 err=%2").arg(path, directFile.errorString()));
            return false;
        }
        directFile.close();
        return true;
    }

    return true;
}

}  // namespace infrastructure::storage
