#pragma once

#include <QString>

class AppPaths final {
public:
    static QString appRoot();
    static QString dataDir();
    static QString cacheDir();
    static QString licenseDir();
};

