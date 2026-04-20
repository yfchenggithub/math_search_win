/*
 * Unified logger for the offline Windows desktop project.
 *
 * Why this exists:
 * - One logging entrypoint for app code and Qt messages.
 * - Stable, searchable lines with source location and thread id.
 * - Two sinks by default: console + local file (daily split).
 *
 * Quick start:
 * 1) Initialize early in main():
 *      logging::Logger::instance().initialize();
 * 2) Log in business code with macros:
 *      LOG_INFO(LogCategory::AppStartup, QStringLiteral("startup ok"));
 *      LOG_WARN(LogCategory::FileIo, QStringLiteral("missing file path=%1").arg(path));
 * 3) Optional shutdown flush:
 *      logging::Logger::instance().shutdown();
 *
 * Log format:
 *   yyyy-MM-dd HH:mm:ss.zzz | L | category | file:line | func_short | message
 *
 * Default behavior:
 * - Debug build default level: DEBUG and above.
 * - Release build default level: INFO and above.
 * - Output sinks: stderr (+ OutputDebugString on Windows) and daily log file.
 * - Daily file name pattern: math_search_YYYYMMDD.log.
 *
 * Where to read logs:
 * - VSCode debugging: Debug Console / terminal stderr output.
 * - File (preferred): <exe_dir>/logs/math_search_YYYYMMDD.log
 * - File fallback: QStandardPaths::AppLocalDataLocation/logs
 *   (Windows example: %LOCALAPPDATA%/<Organization>/<Application>/logs)
 *
 * Environment variables (read when initialize() runs):
 * - MATH_SEARCH_LOG_LEVEL
 *   Values: trace | debug | info | warn | error | fatal
 * - MATH_SEARCH_LOG_DIR
 *   Absolute path, or relative path under executable directory.
 * - MATH_SEARCH_LOG_TO_CONSOLE
 *   Boolean: 1/0, true/false, yes/no, on/off
 * - MATH_SEARCH_LOG_TO_FILE
 *   Boolean: 1/0, true/false, yes/no, on/off
 * - MATH_SEARCH_LOG_SHOW_THREAD_ID
 *   Boolean: 1/0, true/false, yes/no, on/off
 * - MATH_SEARCH_LOG_COMPACT_PATH
 *   Boolean: 1/0, true/false, yes/no, on/off
 * - MATH_SEARCH_LOG_SHORT_FUNC
 *   Boolean: 1/0, true/false, yes/no, on/off
 * - MATH_SEARCH_LOG_DEBUG_VERBOSE
 *   Boolean: 1/0, true/false, yes/no, on/off
 *
 * PowerShell examples (set before launching app):
 *   $env:MATH_SEARCH_LOG_LEVEL = "debug"
 *   $env:MATH_SEARCH_LOG_TO_CONSOLE = "1"
 *   $env:MATH_SEARCH_LOG_TO_FILE = "1"
 *   $env:MATH_SEARCH_LOG_DIR = "D:\\temp\\math_search_logs"
 *
 * Maintainer notes:
 * - Prefer LOG_* macros over qDebug/std::cout/printf in app code.
 * - LOG_* auto-captures file/line/function via macros.
 * - Keep category names stable and centralized in log_categories.h.
 */

#pragma once

#include <QByteArray>
#include <QMessageLogContext>
#include <QString>
#include <QStringView>
#include <QtGlobal>

#include <string>

namespace logging {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5
};

struct LoggerOptions {
    bool showThreadId = false;
    bool compactPath = true;
    bool useShortFunctionName = true;
    bool debugVerboseMode = false;
};

class Logger final {
public:
    static Logger& instance();

    void initialize();
    void shutdown();

    void log(LogLevel level,
             const QString& category,
             const QString& message,
             const char* file,
             int line,
             const char* function,
             const char* displayFunction = nullptr);

    bool isEnabled(LogLevel level) const;
    void setOptions(const LoggerOptions& options);
    LoggerOptions options() const;

    LogLevel minLevel() const;
    QString logDirectory() const;
    QString activeLogFilePath() const;

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
};

QString logLevelToString(LogLevel level);
QString logLevelToLetter(LogLevel level);
QString shortFunctionName(const char* rawFunction, const char* displayFunction = nullptr);
QString compactPathForLog(const QString& rawPath, bool diagnosticsContext = false);

inline QString toQString(const QString& value)
{
    return value;
}

inline QString toQString(QStringView value)
{
    return value.toString();
}

inline QString toQString(QLatin1StringView value)
{
    return value.toString();
}

inline QString toQString(const QByteArray& value)
{
    return QString::fromUtf8(value);
}

inline QString toQString(const char* value)
{
    return value ? QString::fromUtf8(value) : QStringLiteral("<null>");
}

template <std::size_t N>
inline QString toQString(const char (&value)[N])
{
    return QString::fromUtf8(value, static_cast<int>(N - 1));
}

inline QString toQString(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

template <typename CategoryT, typename MessageT>
inline void logWithContext(LogLevel level,
                           const CategoryT& category,
                           const MessageT& message,
                           const char* file,
                           int line,
                           const char* function,
                           const char* displayFunction = nullptr)
{
    Logger& logger = Logger::instance();
    logger.initialize();
    if (!logger.isEnabled(level)) {
        return;
    }
    logger.log(level, toQString(category), toQString(message), file, line, function, displayFunction);
}

#define LOG_TRACE(category, message) \
    ::logging::logWithContext(::logging::LogLevel::Trace, (category), (message), __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_DEBUG(category, message) \
    ::logging::logWithContext(::logging::LogLevel::Debug, (category), (message), __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_INFO(category, message) \
    ::logging::logWithContext(::logging::LogLevel::Info, (category), (message), __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_WARN(category, message) \
    ::logging::logWithContext(::logging::LogLevel::Warn, (category), (message), __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_ERROR(category, message) \
    ::logging::logWithContext(::logging::LogLevel::Error, (category), (message), __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_FATAL(category, message) \
    ::logging::logWithContext(::logging::LogLevel::Fatal, (category), (message), __FILE__, __LINE__, Q_FUNC_INFO)

#define LOG_TRACE_F(category, displayFunction, message)                                                                  \
    ::logging::logWithContext(::logging::LogLevel::Trace,                                                               \
                              (category),                                                                               \
                              (message),                                                                                \
                              __FILE__,                                                                                 \
                              __LINE__,                                                                                 \
                              Q_FUNC_INFO,                                                                              \
                              (displayFunction))

#define LOG_DEBUG_F(category, displayFunction, message)                                                                  \
    ::logging::logWithContext(::logging::LogLevel::Debug,                                                               \
                              (category),                                                                               \
                              (message),                                                                                \
                              __FILE__,                                                                                 \
                              __LINE__,                                                                                 \
                              Q_FUNC_INFO,                                                                              \
                              (displayFunction))

#define LOG_INFO_F(category, displayFunction, message)                                                                   \
    ::logging::logWithContext(::logging::LogLevel::Info,                                                                \
                              (category),                                                                               \
                              (message),                                                                                \
                              __FILE__,                                                                                 \
                              __LINE__,                                                                                 \
                              Q_FUNC_INFO,                                                                              \
                              (displayFunction))

#define LOG_WARN_F(category, displayFunction, message)                                                                   \
    ::logging::logWithContext(::logging::LogLevel::Warn,                                                                \
                              (category),                                                                               \
                              (message),                                                                                \
                              __FILE__,                                                                                 \
                              __LINE__,                                                                                 \
                              Q_FUNC_INFO,                                                                              \
                              (displayFunction))

#define LOG_ERROR_F(category, displayFunction, message)                                                                  \
    ::logging::logWithContext(::logging::LogLevel::Error,                                                               \
                              (category),                                                                               \
                              (message),                                                                                \
                              __FILE__,                                                                                 \
                              __LINE__,                                                                                 \
                              Q_FUNC_INFO,                                                                              \
                              (displayFunction))

#define LOG_FATAL_F(category, displayFunction, message)                                                                  \
    ::logging::logWithContext(::logging::LogLevel::Fatal,                                                               \
                              (category),                                                                               \
                              (message),                                                                                \
                              __FILE__,                                                                                 \
                              __LINE__,                                                                                 \
                              Q_FUNC_INFO,                                                                              \
                              (displayFunction))

}  // namespace logging
