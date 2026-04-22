#include "ui/style/app_style.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "shared/paths.h"

#include <QApplication>
#include <QDate>
#include <QFile>
#include <QFileInfo>

#include <cmath>

namespace ui::style {
namespace {

QString locateAppStylePath()
{
    return AppPaths::appStylePath();
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
