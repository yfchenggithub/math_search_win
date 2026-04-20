#include "ui/style/app_style.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cmath>

namespace ui::style {
namespace {

QString locateAppStylePath()
{
    auto tryFindInTree = [](const QString& startPath) -> QString {
        QDir dir(startPath);
        for (int depth = 0; depth < 10; ++depth) {
            const QString rootStyle = dir.filePath(QStringLiteral("app.qss"));
            if (QFileInfo::exists(rootStyle)) {
                return QDir::cleanPath(rootStyle);
            }

            const QString sourceStyle = dir.filePath(QStringLiteral("src/ui/style/app.qss"));
            if (QFileInfo::exists(sourceStyle)) {
                return QDir::cleanPath(sourceStyle);
            }

            if (!dir.cdUp()) {
                break;
            }
        }
        return {};
    };

    const QString fromAppDir = tryFindInTree(QCoreApplication::applicationDirPath());
    if (!fromAppDir.isEmpty()) {
        return fromAppDir;
    }

    return tryFindInTree(QDir::currentPath());
}

bool loadTextFile(const QString& path, QString* outText)
{
    if (outText == nullptr || path.trimmed().isEmpty()) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    *outText = QString::fromUtf8(file.readAll());
    return !outText->trimmed().isEmpty();
}

}  // namespace

bool ensureAppStyleSheetLoaded()
{
    if (qApp == nullptr) {
        return false;
    }

    constexpr auto kAttemptedProperty = "math_search_app_qss_attempted";
    constexpr auto kAppliedProperty = "math_search_app_qss_applied";
    constexpr auto kPathProperty = "math_search_app_qss_path";

    const bool alreadyAttempted = qApp->property(kAttemptedProperty).toBool();
    if (alreadyAttempted) {
        return qApp->property(kAppliedProperty).toBool();
    }
    qApp->setProperty(kAttemptedProperty, true);

    const QString stylePath = locateAppStylePath();
    QString styleText;
    if (stylePath.isEmpty() || !loadTextFile(stylePath, &styleText)) {
        LOG_WARN(LogCategory::UiMainWindow, QStringLiteral("app stylesheet missing path=app.qss"));
        qApp->setProperty(kAppliedProperty, false);
        return false;
    }

    const QString existingStyle = qApp->styleSheet().trimmed();
    if (existingStyle.isEmpty()) {
        qApp->setStyleSheet(styleText);
    } else {
        qApp->setStyleSheet(existingStyle + QStringLiteral("\n\n") + styleText);
    }

    qApp->setProperty(kAppliedProperty, true);
    qApp->setProperty(kPathProperty, stylePath);
    LOG_INFO(LogCategory::UiMainWindow, QStringLiteral("app stylesheet loaded path=%1").arg(stylePath));
    return true;
}

QString formatRelativeDateTime(const QDateTime& timestamp)
{
    if (!timestamp.isValid()) {
        return {};
    }

    const QDateTime localTime = timestamp.toLocalTime();
    const QDate eventDate = localTime.date();
    const QDate today = QDate::currentDate();

    if (eventDate == today) {
        return QStringLiteral("今天 %1").arg(localTime.time().toString(QStringLiteral("HH:mm")));
    }
    if (eventDate == today.addDays(-1)) {
        return QStringLiteral("昨天 %1").arg(localTime.time().toString(QStringLiteral("HH:mm")));
    }
    return localTime.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString formatDifficultyText(double difficulty)
{
    if (difficulty <= 0.0) {
        return {};
    }

    const double rounded = std::round(difficulty);
    if (std::fabs(difficulty - rounded) < 1e-6) {
        return QStringLiteral("难度 %1").arg(static_cast<int>(rounded));
    }
    return QStringLiteral("难度 %1").arg(QString::number(difficulty, 'f', 1));
}

}  // namespace ui::style
