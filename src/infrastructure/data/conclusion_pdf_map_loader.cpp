#include "infrastructure/data/conclusion_pdf_map_loader.h"

#include "shared/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace infrastructure::data {
namespace {

QString normalizeFilePath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

}  // namespace

bool ConclusionPdfMapLoader::loadFromFile(const QString& filePath)
{
    diagnostics_ = ConclusionPdfMapDiagnostics();
    mapping_.clear();
    activeFilePath_.clear();

    const QString resolvedPath = filePath.trimmed().isEmpty() ? defaultMapFilePath() : filePath.trimmed();
    const QString normalizedPath = normalizeFilePath(resolvedPath);
    diagnostics_.filePath = normalizedPath;
    activeFilePath_ = normalizedPath;

    const QFileInfo fileInfo(normalizedPath);
    diagnostics_.fileExists = fileInfo.exists() && fileInfo.isFile();
    if (!diagnostics_.fileExists) {
        diagnostics_.fatalError = QStringLiteral("conclusion pdf map file missing: %1").arg(normalizedPath);
        return false;
    }

    QFile file(normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        diagnostics_.fatalError = QStringLiteral("failed to open conclusion pdf map file: %1").arg(normalizedPath);
        return false;
    }

    const QByteArray payload = file.readAll();
    file.close();
    if (payload.trimmed().isEmpty()) {
        diagnostics_.fatalError = QStringLiteral("conclusion pdf map file is empty: %1").arg(normalizedPath);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    diagnostics_.parseError = parseError;
    if (parseError.error != QJsonParseError::NoError) {
        diagnostics_.fatalError =
            QStringLiteral("failed to parse conclusion pdf map file: %1 (%2)").arg(normalizedPath, parseError.errorString());
        return false;
    }

    if (!doc.isObject()) {
        diagnostics_.fatalError =
            QStringLiteral("conclusion pdf map root is not object: %1").arg(normalizedPath);
        return false;
    }

    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString key = normalizeKey(it.key());
        if (key.isEmpty()) {
            continue;
        }

        if (!it.value().isString()) {
            diagnostics_.warnings.push_back(
                QStringLiteral("mapping value should be string key=%1").arg(it.key()));
            continue;
        }

        const QString mapped = it.value().toString().trimmed();
        if (mapped.isEmpty()) {
            diagnostics_.warnings.push_back(
                QStringLiteral("mapping value is empty key=%1").arg(it.key()));
            continue;
        }

        mapping_.insert(key, mapped);
    }

    diagnostics_.loadedEntryCount = mapping_.size();
    if (mapping_.isEmpty()) {
        diagnostics_.fatalError =
            QStringLiteral("conclusion pdf map has no valid entries: %1").arg(normalizedPath);
        return false;
    }

    return true;
}

QString ConclusionPdfMapLoader::mappedPdfFileName(const QString& conclusionId) const
{
    return mapping_.value(normalizeKey(conclusionId));
}

bool ConclusionPdfMapLoader::contains(const QString& conclusionId) const
{
    return mapping_.contains(normalizeKey(conclusionId));
}

const ConclusionPdfMapDiagnostics& ConclusionPdfMapLoader::diagnostics() const
{
    return diagnostics_;
}

QString ConclusionPdfMapLoader::activeFilePath() const
{
    return activeFilePath_;
}

QString ConclusionPdfMapLoader::defaultMapFilePath()
{
    return QDir(AppPaths::dataDir()).filePath(QStringLiteral("conclusion_pdf_map.json"));
}

QString ConclusionPdfMapLoader::normalizeKey(const QString& key)
{
    return key.trimmed();
}

}  // namespace infrastructure::data
