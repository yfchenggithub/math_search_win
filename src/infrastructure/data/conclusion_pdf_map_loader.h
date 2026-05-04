#pragma once

#include <QHash>
#include <QJsonParseError>
#include <QString>
#include <QStringList>

namespace infrastructure::data {

struct ConclusionPdfMapDiagnostics final {
    QString filePath;
    bool fileExists = false;
    QJsonParseError parseError;
    int loadedEntryCount = 0;
    QString fatalError;
    QStringList warnings;
};

class ConclusionPdfMapLoader final {
public:
    bool loadFromFile(const QString& filePath = QString());

    QString mappedPdfFileName(const QString& conclusionId) const;
    bool contains(const QString& conclusionId) const;
    const ConclusionPdfMapDiagnostics& diagnostics() const;
    QString activeFilePath() const;

private:
    static QString defaultMapFilePath();
    static QString normalizeKey(const QString& key);

private:
    ConclusionPdfMapDiagnostics diagnostics_;
    QHash<QString, QString> mapping_;
    QString activeFilePath_;
};

}  // namespace infrastructure::data
