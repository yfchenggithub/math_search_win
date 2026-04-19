/*
 * Unified logger implementation for the offline desktop app.
 * Design goal:
 * - Thread-safe logging to console and local files.
 * - Daily log file rotation with stable, searchable line format.
 * - Qt message redirection into the same output stream.
 * Usage:
 * - Call Logger::instance().initialize() during app startup.
 * - Use LOG_* macros for business logs with file/line/function metadata.
 */

#include "core/logging/logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <cstdio>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace logging {
namespace {

struct LoggerState {
    mutable QMutex mutex;
    bool initialized = false;
    LogLevel minLevel =
#if defined(NDEBUG)
        LogLevel::Info;
#else
        LogLevel::Debug;
#endif
    bool logToConsole = true;
    bool logToFile = true;
    bool captureQtMessages = true;
    QString logDirectory;
    QFile logFile;
    QString activeDateToken;
    QtMessageHandler previousHandler = nullptr;
};

LoggerState& state()
{
    static LoggerState singletonState;
    return singletonState;
}

bool parseBoolString(const QString& rawValue, bool fallback)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true") || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("on")) {
        return true;
    }
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("false") || normalized == QStringLiteral("no")
        || normalized == QStringLiteral("off")) {
        return false;
    }
    return fallback;
}

LogLevel parseLogLevel(const QString& rawValue, LogLevel fallback)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("trace")) {
        return LogLevel::Trace;
    }
    if (normalized == QStringLiteral("debug")) {
        return LogLevel::Debug;
    }
    if (normalized == QStringLiteral("info")) {
        return LogLevel::Info;
    }
    if (normalized == QStringLiteral("warn") || normalized == QStringLiteral("warning")) {
        return LogLevel::Warn;
    }
    if (normalized == QStringLiteral("error")) {
        return LogLevel::Error;
    }
    if (normalized == QStringLiteral("fatal")) {
        return LogLevel::Fatal;
    }
    return fallback;
}

QString fileNameOnly(const char* filePath)
{
    if (filePath == nullptr || filePath[0] == '\0') {
        return QStringLiteral("<unknown>");
    }
    return QFileInfo(QString::fromUtf8(filePath)).fileName();
}

QString functionNameOnly(const char* functionName)
{
    if (functionName == nullptr || functionName[0] == '\0') {
        return QStringLiteral("<unknown>");
    }
    return QString::fromUtf8(functionName);
}

QString currentThreadId()
{
    const quintptr threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    return QStringLiteral("0x%1").arg(threadId, QT_POINTER_SIZE * 2, 16, QLatin1Char('0'));
}

void writeToConsole(const QString& line)
{
#if defined(Q_OS_WIN)
    const QString withNewline = line + QLatin1Char('\n');
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(withNewline.utf16()));
#endif

    const QByteArray utf8 = line.toUtf8();
    std::fwrite(utf8.constData(), sizeof(char), static_cast<std::size_t>(utf8.size()), stderr);
    std::fwrite("\n", sizeof(char), 1U, stderr);
    std::fflush(stderr);
}

bool ensureFileReady(LoggerState& currentState, const QDateTime& now)
{
    const QString dateToken = now.date().toString(QStringLiteral("yyyyMMdd"));
    if (currentState.logFile.isOpen() && currentState.activeDateToken == dateToken) {
        return true;
    }

    if (currentState.logFile.isOpen()) {
        currentState.logFile.flush();
        currentState.logFile.close();
    }

    QDir dir(currentState.logDirectory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    const QString dailyName = QStringLiteral("math_search_%1.log").arg(dateToken);
    currentState.logFile.setFileName(dir.filePath(dailyName));
    if (!currentState.logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    currentState.activeDateToken = dateToken;
    return true;
}

QString resolveDefaultLogDirectory()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString preferred = QDir(appDir).filePath(QStringLiteral("logs"));
    QDir preferredDir(preferred);
    if (preferredDir.exists() || preferredDir.mkpath(QStringLiteral("."))) {
        return preferredDir.absolutePath();
    }

    QString localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (localData.isEmpty()) {
        localData = QDir(QDir::tempPath()).filePath(QStringLiteral("math_search_win"));
    }

    QDir fallbackBase(localData);
    if (!fallbackBase.exists()) {
        fallbackBase.mkpath(QStringLiteral("."));
    }

    const QString fallback = fallbackBase.filePath(QStringLiteral("logs"));
    QDir fallbackDir(fallback);
    if (!fallbackDir.exists()) {
        fallbackDir.mkpath(QStringLiteral("."));
    }
    return fallbackDir.absolutePath();
}

LogLevel qtMsgTypeToLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return LogLevel::Debug;
    case QtInfoMsg:
        return LogLevel::Info;
    case QtWarningMsg:
        return LogLevel::Warn;
    case QtCriticalMsg:
        return LogLevel::Error;
    case QtFatalMsg:
        return LogLevel::Fatal;
    }
    return LogLevel::Info;
}

}  // namespace

Logger::Logger() = default;

Logger::~Logger()
{
    shutdown();
}

Logger& Logger::instance()
{
    static Logger singletonLogger;
    return singletonLogger;
}

void Logger::initialize()
{
    LoggerState& currentState = state();

    LogLevel configuredLevel = LogLevel::Info;
    QString configuredDir;
    bool configuredConsole = true;
    bool configuredFile = true;

    {
        QMutexLocker lock(&currentState.mutex);
        if (currentState.initialized) {
            return;
        }

#if defined(NDEBUG)
        currentState.minLevel = LogLevel::Info;
#else
        currentState.minLevel = LogLevel::Debug;
#endif
        currentState.logToConsole = true;
        currentState.logToFile = true;
        currentState.captureQtMessages = true;
        currentState.logDirectory = resolveDefaultLogDirectory();

        const QString envLevel = qEnvironmentVariable("MATH_SEARCH_LOG_LEVEL").trimmed();
        if (!envLevel.isEmpty()) {
            currentState.minLevel = parseLogLevel(envLevel, currentState.minLevel);
        }

        const QString envDir = qEnvironmentVariable("MATH_SEARCH_LOG_DIR").trimmed();
        if (!envDir.isEmpty()) {
            const QDir envPathDir(envDir);
            if (envPathDir.isAbsolute()) {
                currentState.logDirectory = envPathDir.absolutePath();
            } else {
                currentState.logDirectory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(envDir);
            }
        }

        const QString envConsole = qEnvironmentVariable("MATH_SEARCH_LOG_TO_CONSOLE").trimmed();
        if (!envConsole.isEmpty()) {
            currentState.logToConsole = parseBoolString(envConsole, currentState.logToConsole);
        }

        const QString envFile = qEnvironmentVariable("MATH_SEARCH_LOG_TO_FILE").trimmed();
        if (!envFile.isEmpty()) {
            currentState.logToFile = parseBoolString(envFile, currentState.logToFile);
        }

        if (currentState.logToFile && !ensureFileReady(currentState, QDateTime::currentDateTime())) {
            currentState.logToFile = false;
        }

        if (currentState.captureQtMessages) {
            currentState.previousHandler = qInstallMessageHandler(&Logger::qtMessageHandler);
        }

        currentState.initialized = true;
        configuredLevel = currentState.minLevel;
        configuredDir = currentState.logDirectory;
        configuredConsole = currentState.logToConsole;
        configuredFile = currentState.logToFile;
    }

    const QString summary = QStringLiteral("logger initialized level=%1 dir=%2 console=%3 file=%4")
                                .arg(logLevelToString(configuredLevel),
                                     configuredDir,
                                     configuredConsole ? QStringLiteral("on") : QStringLiteral("off"),
                                     configuredFile ? QStringLiteral("on") : QStringLiteral("off"));
    log(LogLevel::Info, QStringLiteral("config"), summary, __FILE__, __LINE__, Q_FUNC_INFO);
}

void Logger::shutdown()
{
    LoggerState& currentState = state();

    QMutexLocker lock(&currentState.mutex);
    if (!currentState.initialized) {
        return;
    }

    qInstallMessageHandler(currentState.previousHandler);
    currentState.previousHandler = nullptr;
    currentState.captureQtMessages = false;

    if (currentState.logFile.isOpen()) {
        currentState.logFile.flush();
        currentState.logFile.close();
    }
    currentState.activeDateToken.clear();
    currentState.initialized = false;
}

void Logger::log(LogLevel level,
                 const QString& category,
                 const QString& message,
                 const char* file,
                 int line,
                 const char* function)
{
    initialize();

    const QDateTime now = QDateTime::currentDateTime();
    const QString normalizedCategory = category.trimmed().isEmpty() ? QStringLiteral("general") : category.trimmed();
    const QString lineText =
        QStringLiteral("%1 | %2 | %3 | %4:%5 | %6 | tid=%7 | %8")
            .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                 logLevelToString(level),
                 normalizedCategory,
                 fileNameOnly(file),
                 QString::number(line),
                 functionNameOnly(function),
                 currentThreadId(),
                 message);

    LoggerState& currentState = state();
    QMutexLocker lock(&currentState.mutex);

    if (!currentState.initialized) {
        return;
    }
    if (static_cast<int>(level) < static_cast<int>(currentState.minLevel)) {
        return;
    }

    if (currentState.logToConsole) {
        writeToConsole(lineText);
    }

    if (currentState.logToFile) {
        if (!ensureFileReady(currentState, now)) {
            currentState.logToFile = false;
            if (currentState.logToConsole) {
                writeToConsole(QStringLiteral(
                    "logging fallback: failed to prepare log file, file sink disabled for this session"));
            }
            return;
        }

        const QByteArray utf8 = lineText.toUtf8();
        currentState.logFile.write(utf8);
        currentState.logFile.write("\n", 1);
        currentState.logFile.flush();
    }
}

bool Logger::isEnabled(LogLevel level) const
{
    const LoggerState& currentState = state();
    QMutexLocker lock(&currentState.mutex);
    return static_cast<int>(level) >= static_cast<int>(currentState.minLevel);
}

LogLevel Logger::minLevel() const
{
    const LoggerState& currentState = state();
    QMutexLocker lock(&currentState.mutex);
    return currentState.minLevel;
}

QString Logger::logDirectory() const
{
    const LoggerState& currentState = state();
    QMutexLocker lock(&currentState.mutex);
    return currentState.logDirectory;
}

QString Logger::activeLogFilePath() const
{
    const LoggerState& currentState = state();
    QMutexLocker lock(&currentState.mutex);
    return currentState.logFile.fileName();
}

void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QString qtCategory = QStringLiteral("qt.default");
    if (context.category != nullptr && context.category[0] != '\0') {
        qtCategory = QStringLiteral("qt.%1").arg(QString::fromUtf8(context.category));
    }

    Logger::instance().log(qtMsgTypeToLevel(type), qtCategory, message, context.file, context.line, context.function);
}

QString logLevelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return QStringLiteral("TRACE");
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Warn:
        return QStringLiteral("WARN");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    case LogLevel::Fatal:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO");
}

}  // namespace logging
