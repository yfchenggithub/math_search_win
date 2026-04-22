#include "shared/paths.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <utility>

namespace {

QString normalizePath(const QString& rawPath)
{
    if (rawPath.trimmed().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(rawPath.trimmed()));
}

bool hasLegacyResourcesLayout(const QDir& dir)
{
    return dir.exists(QStringLiteral("resources/detail")) && dir.exists(QStringLiteral("resources/katex"));
}

bool hasAppResourcesLayout(const QDir& dir)
{
    return dir.exists(QStringLiteral("app_resources")) || hasLegacyResourcesLayout(dir);
}

bool hasReleaseLikeMarkers(const QDir& dir)
{
    if (hasAppResourcesLayout(dir)) {
        return true;
    }
    return dir.exists(QStringLiteral("data")) || dir.exists(QStringLiteral("cache")) || dir.exists(QStringLiteral("license"));
}

bool hasDevLikeMarkers(const QDir& dir)
{
    return dir.exists(QStringLiteral("src")) && hasAppResourcesLayout(dir);
}

bool hasRootMarkers(const QDir& dir)
{
    return hasReleaseLikeMarkers(dir) || hasDevLikeMarkers(dir);
}

QString probeRootFromStart(const QString& startPath)
{
    QDir probe(startPath);
    if (!probe.exists()) {
        return {};
    }

    for (int depth = 0; depth <= 10; ++depth) {
        if (hasRootMarkers(probe)) {
            return normalizePath(probe.absolutePath());
        }
        if (!probe.cdUp()) {
            break;
        }
    }

    return {};
}

QString resolveAppRoot()
{
    const QString envRoot = normalizePath(qEnvironmentVariable("MATH_SEARCH_ROOT"));
    if (!envRoot.isEmpty()) {
        const QFileInfo envInfo(envRoot);
        if (envInfo.exists() && envInfo.isDir()) {
            const QString resolved = envInfo.absoluteFilePath();
            LOG_INFO(LogCategory::Config, QStringLiteral("appRoot resolved from env MATH_SEARCH_ROOT=%1").arg(resolved));
            return resolved;
        }
        LOG_WARN(LogCategory::Config,
                 QStringLiteral("appRoot env ignored reason=not_directory path=%1").arg(envRoot));
    }

    QStringList probeStarts;
    probeStarts.push_back(normalizePath(QDir::currentPath()));
    if (QCoreApplication::instance() != nullptr) {
        probeStarts.push_back(normalizePath(QCoreApplication::applicationDirPath()));
    }

    QSet<QString> dedupe;
    for (const QString& start : std::as_const(probeStarts)) {
        if (start.isEmpty() || dedupe.contains(start)) {
            continue;
        }
        dedupe.insert(start);
        const QString resolved = probeRootFromStart(start);
        if (!resolved.isEmpty()) {
            LOG_INFO(LogCategory::Config, QStringLiteral("appRoot resolved path=%1 start=%2").arg(resolved, start));
            return resolved;
        }
    }

    if (QCoreApplication::instance() != nullptr) {
        const QString fallback = normalizePath(QCoreApplication::applicationDirPath());
        LOG_WARN(LogCategory::Config,
                 QStringLiteral("appRoot fallback to executable dir path=%1").arg(fallback));
        return fallback;
    }

    const QString fallback = normalizePath(QDir::currentPath());
    LOG_WARN(LogCategory::Config, QStringLiteral("appRoot fallback to current dir path=%1").arg(fallback));
    return fallback;
}

}  // namespace

bool RuntimeLayoutStatus::webResourcesReady() const
{
    return appResourcesDirExists && detailDirExists && katexDirExists && detailTemplateExists;
}

QString AppPaths::executableDir()
{
    if (QCoreApplication::instance() != nullptr) {
        return normalizePath(QCoreApplication::applicationDirPath());
    }
    return normalizePath(QDir::currentPath());
}

QString AppPaths::appRoot()
{
    return resolveAppRoot();
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

QString AppPaths::appResourcesDir()
{
    const QString rootPath = appRoot();
    const QString appResourcesPath = QDir(rootPath).filePath(QStringLiteral("app_resources"));
    const QFileInfo appResourcesInfo(appResourcesPath);
    if (appResourcesInfo.exists() && appResourcesInfo.isDir()) {
        LOG_DEBUG(LogCategory::FileIo, QStringLiteral("appResourcesDir=%1").arg(appResourcesPath));
        return appResourcesPath;
    }

    const QString legacyResourcesPath = QDir(rootPath).filePath(QStringLiteral("resources"));
    const QFileInfo legacyResourcesInfo(legacyResourcesPath);
    const QDir legacyResourcesDir(legacyResourcesPath);
    const bool legacyDetailReady = legacyResourcesDir.exists(QStringLiteral("detail"));
    const bool legacyKatexReady = legacyResourcesDir.exists(QStringLiteral("katex"));
    if (legacyResourcesInfo.exists() && legacyResourcesInfo.isDir() && legacyDetailReady && legacyKatexReady) {
        LOG_WARN(LogCategory::Config,
                 QStringLiteral("legacy resources dir detected; prefer app_resources path=%1").arg(legacyResourcesPath));
        return legacyResourcesPath;
    }

    LOG_DEBUG(LogCategory::FileIo, QStringLiteral("appResourcesDir=%1").arg(appResourcesPath));
    return appResourcesPath;
}

QString AppPaths::detailResourcesDir()
{
    const QString path = QDir(appResourcesDir()).filePath(QStringLiteral("detail"));
    LOG_DEBUG(LogCategory::FileIo, QStringLiteral("detailResourcesDir=%1").arg(path));
    return path;
}

QString AppPaths::katexDir()
{
    const QString path = QDir(appResourcesDir()).filePath(QStringLiteral("katex"));
    LOG_DEBUG(LogCategory::FileIo, QStringLiteral("katexDir=%1").arg(path));
    return path;
}

QString AppPaths::detailTemplatePath()
{
    const QString path = QDir(detailResourcesDir()).filePath(QStringLiteral("detail_template.html"));
    LOG_DEBUG(LogCategory::FileIo, QStringLiteral("detailTemplatePath=%1").arg(path));
    return path;
}

QString AppPaths::appStylePath()
{
    QDir probe(appRoot());
    for (int depth = 0; depth <= 10; ++depth) {
        const QStringList candidates = {
            probe.filePath(QStringLiteral("app_resources/styles/app.qss")),
            probe.filePath(QStringLiteral("app_resources/style/app.qss")),
            probe.filePath(QStringLiteral("resources/styles/app.qss")),
            probe.filePath(QStringLiteral("resources/style/app.qss")),
            probe.filePath(QStringLiteral("src/ui/style/app.qss")),
            probe.filePath(QStringLiteral("app.qss")),
        };

        for (const QString& candidate : candidates) {
            const QFileInfo info(candidate);
            if (info.exists() && info.isFile()) {
                const QString path = normalizePath(info.absoluteFilePath());
                LOG_DEBUG(LogCategory::FileIo, QStringLiteral("appStylePath=%1").arg(path));
                return path;
            }
        }

        if (!probe.cdUp()) {
            break;
        }
    }

    LOG_WARN(LogCategory::FileIo, QStringLiteral("appStylePath not found root=%1").arg(appRoot()));
    return {};
}

bool AppPaths::ensureCacheDir(QString* errorMessage)
{
    const QString path = cacheDir();
    const QFileInfo info(path);
    if (info.exists()) {
        if (info.isDir()) {
            return true;
        }
        const QString error = QStringLiteral("cache path exists but is not a directory: %1").arg(path);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        LOG_ERROR(LogCategory::FileIo, error);
        return false;
    }

    QDir dir;
    if (!dir.mkpath(path)) {
        const QString error = QStringLiteral("failed to create cache directory: %1").arg(path);
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        LOG_ERROR(LogCategory::FileIo, error);
        return false;
    }

    LOG_INFO(LogCategory::FileIo, QStringLiteral("cache directory created path=%1").arg(path));
    return true;
}

RuntimeLayoutStatus AppPaths::inspectRuntimeLayout(bool ensureCacheDirIfMissing)
{
    RuntimeLayoutStatus status;
    status.executableDir = executableDir();
    status.appRoot = appRoot();
    status.dataDir = dataDir();
    status.cacheDir = cacheDir();
    status.licenseDir = licenseDir();
    status.appResourcesDir = appResourcesDir();
    status.detailDir = detailResourcesDir();
    status.katexDir = katexDir();
    status.detailTemplatePath = detailTemplatePath();

    const QFileInfo dataInfo(status.dataDir);
    status.dataDirExists = dataInfo.exists() && dataInfo.isDir();
    if (!status.dataDirExists) {
        status.errors.push_back(QStringLiteral("data directory missing: %1").arg(status.dataDir));
    }

    const QFileInfo appResourcesInfo(status.appResourcesDir);
    status.appResourcesDirExists = appResourcesInfo.exists() && appResourcesInfo.isDir();
    if (!status.appResourcesDirExists) {
        status.errors.push_back(QStringLiteral("app_resources directory missing: %1").arg(status.appResourcesDir));
    }

    const QFileInfo detailInfo(status.detailDir);
    status.detailDirExists = detailInfo.exists() && detailInfo.isDir();
    if (!status.detailDirExists) {
        status.warnings.push_back(QStringLiteral("detail resources missing: %1").arg(status.detailDir));
    }

    const QFileInfo katexInfo(status.katexDir);
    status.katexDirExists = katexInfo.exists() && katexInfo.isDir();
    if (!status.katexDirExists) {
        status.warnings.push_back(QStringLiteral("katex resources missing: %1").arg(status.katexDir));
    }

    const QFileInfo templateInfo(status.detailTemplatePath);
    status.detailTemplateExists = templateInfo.exists() && templateInfo.isFile();
    if (!status.detailTemplateExists) {
        status.warnings.push_back(QStringLiteral("detail template missing: %1").arg(status.detailTemplatePath));
    }

    const QFileInfo licenseInfo(status.licenseDir);
    status.licenseDirExists = licenseInfo.exists() && licenseInfo.isDir();
    if (!status.licenseDirExists) {
        status.warnings.push_back(QStringLiteral("license directory missing: %1").arg(status.licenseDir));
    }

    if (ensureCacheDirIfMissing) {
        QString cacheError;
        status.cacheDirReady = ensureCacheDir(&cacheError);
        if (!status.cacheDirReady) {
            status.errors.push_back(cacheError.trimmed().isEmpty()
                                        ? QStringLiteral("cache directory unavailable: %1").arg(status.cacheDir)
                                        : cacheError.trimmed());
        }
    } else {
        const QFileInfo cacheInfo(status.cacheDir);
        status.cacheDirReady = cacheInfo.exists() && cacheInfo.isDir();
        if (!status.cacheDirReady) {
            status.errors.push_back(QStringLiteral("cache directory missing: %1").arg(status.cacheDir));
        }
    }

    LOG_INFO(LogCategory::Config,
             QStringLiteral("runtime layout root=%1 exe_dir=%2 data=%3 cache=%4 license=%5 app_resources=%6 detail=%7 katex=%8 template=%9")
                 .arg(status.appRoot,
                      status.executableDir,
                      status.dataDirExists ? QStringLiteral("ready") : QStringLiteral("missing"),
                      status.cacheDirReady ? QStringLiteral("ready") : QStringLiteral("missing"),
                      status.licenseDirExists ? QStringLiteral("ready") : QStringLiteral("missing"),
                      status.appResourcesDirExists ? QStringLiteral("ready") : QStringLiteral("missing"),
                      status.detailDirExists ? QStringLiteral("ready") : QStringLiteral("missing"),
                      status.katexDirExists ? QStringLiteral("ready") : QStringLiteral("missing"),
                      status.detailTemplateExists ? QStringLiteral("ready") : QStringLiteral("missing")));

    return status;
}
