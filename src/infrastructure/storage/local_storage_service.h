#pragma once

#include <QJsonDocument>
#include <QString>

namespace infrastructure::storage {

class LocalStorageService final {
public:
    explicit LocalStorageService(QString cacheDirPath = QString());

    QString cacheDir() const;
    QString favoritesFilePath() const;
    QString historyFilePath() const;
    QString settingsFilePath() const;

    bool ensureCacheDirExists();
    QJsonDocument readJsonFile(const QString& path, bool* ok = nullptr) const;
    bool writeJsonFileAtomically(const QString& path, const QJsonDocument& doc);

private:
    QString cacheDirPath_;
};

}  // namespace infrastructure::storage

