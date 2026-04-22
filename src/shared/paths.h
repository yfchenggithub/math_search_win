#pragma once

#include <QString>
#include <QStringList>

struct RuntimeLayoutStatus {
    QString executableDir;
    QString appRoot;
    QString dataDir;
    QString cacheDir;
    QString licenseDir;
    QString appResourcesDir;
    QString detailDir;
    QString katexDir;
    QString detailTemplatePath;
    bool dataDirExists = false;
    bool cacheDirReady = false;
    bool licenseDirExists = false;
    bool appResourcesDirExists = false;
    bool detailDirExists = false;
    bool katexDirExists = false;
    bool detailTemplateExists = false;
    QStringList warnings;
    QStringList errors;

    bool webResourcesReady() const;
};

class AppPaths final {
public:
    static QString executableDir();
    static QString appRoot();
    static QString dataDir();
    static QString cacheDir();
    static QString licenseDir();
    static QString appResourcesDir();
    static QString detailResourcesDir();
    static QString katexDir();
    static QString detailTemplatePath();
    static QString appStylePath();
    static bool ensureCacheDir(QString* errorMessage = nullptr);
    static RuntimeLayoutStatus inspectRuntimeLayout(bool ensureCacheDirIfMissing = true);
};
