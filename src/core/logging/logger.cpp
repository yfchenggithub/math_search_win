/*
 * Unified logger implementation for the offline desktop app.
 * Design goal:
 * - Thread-safe logging to console and local files.
 * - Daily log file rotation with stable, searchable line format.
 * - Qt message redirection into the same output stream.
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
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QUrl>

#include <algorithm>
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
    LoggerOptions options;
    QString projectRoot;
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

QString normalizePath(const QString& rawPath)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(rawPath.trimmed()));
}

bool hasProjectMarkers(const QDir& dir)
{
    const bool hasAppResources = dir.exists(QStringLiteral("app_resources"));
    const bool hasLegacyResources =
        dir.exists(QStringLiteral("resources/detail")) && dir.exists(QStringLiteral("resources/katex"));
    return dir.exists(QStringLiteral("src")) && (hasAppResources || hasLegacyResources);
}

QString detectProjectRoot()
{
    QDir currentDir(QDir::currentPath());
    if (hasProjectMarkers(currentDir)) {
        return currentDir.absolutePath();
    }

    QDir probeDir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (hasProjectMarkers(probeDir)) {
            return probeDir.absolutePath();
        }
        if (!probeDir.cdUp()) {
            break;
        }
    }

    return currentDir.absolutePath();
}

QString sourceFileName(const char* filePath)
{
    if (filePath == nullptr || filePath[0] == '\0') {
        return QStringLiteral("<unknown>");
    }

    const QFileInfo info(QString::fromUtf8(filePath));
    if (!info.fileName().isEmpty()) {
        return info.fileName();
    }

    const QString fallback = normalizePath(QString::fromUtf8(filePath));
    return fallback.isEmpty() ? QStringLiteral("<unknown>") : fallback;
}

QString compactPathInternal(const QString& rawPath,
                            const LoggerOptions& options,
                            const QString& projectRoot,
                            bool diagnosticsContext)
{
    if (rawPath.trimmed().isEmpty()) {
        return QStringLiteral("<unknown>");
    }

    const QFileInfo rawInfo(rawPath);
    const QString normalizedRaw = normalizePath(rawPath);
    const QString normalizedAbs = normalizePath(rawInfo.absoluteFilePath());

    if (diagnosticsContext || !options.compactPath || options.debugVerboseMode) {
        return normalizedAbs.isEmpty() ? normalizedRaw : normalizedAbs;
    }

    if (rawInfo.isRelative()) {
        return normalizedRaw;
    }

    const QString normalizedRoot = normalizePath(projectRoot);
    if (!normalizedRoot.isEmpty()) {
        const QString rootPrefix = normalizedRoot + QLatin1Char('/');
        if (normalizedRaw.startsWith(rootPrefix, Qt::CaseInsensitive)) {
            return normalizedRaw.mid(rootPrefix.size());
        }
        if (normalizedAbs.startsWith(rootPrefix, Qt::CaseInsensitive)) {
            return normalizedAbs.mid(rootPrefix.size());
        }
    }

    static const QStringList kKeepMarkers = {
        QStringLiteral("/src/"),
        QStringLiteral("/app_resources/"),
        QStringLiteral("/resources/"),
        QStringLiteral("/data/"),
        QStringLiteral("/cache/"),
        QStringLiteral("/license/"),
        QStringLiteral("/logs/"),
    };

    for (const QString& marker : kKeepMarkers) {
        const int idx = normalizedRaw.indexOf(marker, 0, Qt::CaseInsensitive);
        if (idx >= 0) {
            return normalizedRaw.mid(idx + 1);
        }
    }
    for (const QString& marker : kKeepMarkers) {
        const int idx = normalizedAbs.indexOf(marker, 0, Qt::CaseInsensitive);
        if (idx >= 0) {
            return normalizedAbs.mid(idx + 1);
        }
    }

    const QString fileName = rawInfo.fileName();
    return fileName.isEmpty() ? normalizedRaw : fileName;
}

QString compactPathValue(const QString& key,
                         const QString& rawValue,
                         const LoggerOptions& options,
                         const QString& projectRoot,
                         bool diagnosticsContext)
{
    Q_UNUSED(key);

    if (rawValue.isEmpty()) {
        return rawValue;
    }

    QString value = rawValue;
    QString suffix;
    while (!value.isEmpty()) {
        const QChar tail = value.back();
        if (tail == QLatin1Char(',') || tail == QLatin1Char(';') || tail == QLatin1Char(')') || tail == QLatin1Char(']')) {
            suffix.prepend(tail);
            value.chop(1);
            continue;
        }
        break;
    }

    QString quotePrefix;
    QString quoteSuffix;
    if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))) {
        quotePrefix = value.left(1);
        quoteSuffix = value.left(1);
        value = value.mid(1, value.size() - 2);
    }

    QString transformed = value;
    bool handled = false;
    if (value.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        const QUrl url(value);
        if (url.isLocalFile()) {
            transformed = compactPathInternal(url.toLocalFile(), options, projectRoot, diagnosticsContext);
            handled = true;
        }
    }

    static const QRegularExpression kWindowsDrivePattern(QStringLiteral("^[A-Za-z]:[\\\\/]"));
    const bool valueHasPathMarkers = value.contains(QLatin1Char('/')) || value.contains(QLatin1Char('\\'))
                                     || kWindowsDrivePattern.match(value).hasMatch()
                                     || value.startsWith(QStringLiteral("./"))
                                     || value.startsWith(QStringLiteral("../"));
    if (!handled && valueHasPathMarkers) {
        transformed = compactPathInternal(value, options, projectRoot, diagnosticsContext);
        handled = true;
    }

    if (!handled) {
        return rawValue;
    }

    return quotePrefix + transformed + quoteSuffix + suffix;
}

QString compactMessagePaths(const QString& message,
                           const LoggerOptions& options,
                           const QString& projectRoot,
                           bool diagnosticsContext)
{
    if (message.trimmed().isEmpty()) {
        return message;
    }

    // Only rewrite key=value tokens to avoid changing free-form human text.
    static const QRegularExpression tokenPattern(QStringLiteral("([A-Za-z0-9_.-]+)=([^\\s]+)"));
    QRegularExpressionMatchIterator it = tokenPattern.globalMatch(message);

    QString rewritten = message;
    int offset = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (!match.hasMatch()) {
            continue;
        }
        const QString key = match.captured(1);
        const QString value = match.captured(2);
        const QString replaced = compactPathValue(key, value, options, projectRoot, diagnosticsContext);
        if (replaced == value) {
            continue;
        }

        const int start = match.capturedStart(2) + offset;
        rewritten.replace(start, value.size(), replaced);
        offset += replaced.size() - value.size();
    }

    return rewritten;
}

QString simplifySignature(QString signature)
{
    if (signature.isEmpty()) {
        return signature;
    }

    signature.replace(QLatin1Char('\t'), QLatin1Char(' '));
    signature = signature.simplified();
    signature.replace(QRegularExpression(QStringLiteral("\\b(public|private|protected):")), QString());
    signature = signature.simplified();

    static const QStringList kNoiseTokens = {
        QStringLiteral("__cdecl"),
        QStringLiteral("__thiscall"),
        QStringLiteral("__stdcall"),
        QStringLiteral("__vectorcall"),
        QStringLiteral("__fastcall"),
        QStringLiteral("virtual"),
        QStringLiteral("static"),
        QStringLiteral("inline"),
        QStringLiteral("constexpr"),
    };
    for (const QString& token : kNoiseTokens) {
        signature.replace(
            QRegularExpression(QStringLiteral("(^|\\s)%1(?=\\s|$)").arg(QRegularExpression::escape(token))),
            QStringLiteral(" "));
    }

    signature.replace(QRegularExpression(QStringLiteral("\\b(class|struct|enum)\\s+")), QString());
    signature = signature.simplified();
    return signature;
}

QString normalizeCtorDtor(const QString& symbol)
{
    if (!symbol.contains(QStringLiteral("::"))) {
        if (symbol.startsWith(QLatin1Char('~')) && symbol.size() > 1) {
            return symbol.mid(1) + QStringLiteral("::dtor");
        }
        return symbol;
    }

    QStringList scopes = symbol.split(QStringLiteral("::"), Qt::SkipEmptyParts);
    if (scopes.size() < 2) {
        return symbol;
    }

    const QString owner = scopes.at(scopes.size() - 2);
    const QString leaf = scopes.last();
    if (leaf == owner) {
        scopes.last() = QStringLiteral("ctor");
        return scopes.join(QStringLiteral("::"));
    }
    if (leaf == QStringLiteral("~%1").arg(owner)) {
        scopes.last() = QStringLiteral("dtor");
        return scopes.join(QStringLiteral("::"));
    }
    return symbol;
}

QString shortFunctionNameInternal(const char* rawFunction,
                                  const char* displayFunction,
                                  const LoggerOptions& options)
{
    const QString display = displayFunction == nullptr ? QString() : QString::fromUtf8(displayFunction).trimmed();
    if (!display.isEmpty()) {
        return display;
    }

    const QString raw = rawFunction == nullptr ? QString() : QString::fromUtf8(rawFunction).trimmed();
    if (raw.isEmpty()) {
        return QStringLiteral("<unknown>");
    }
    if (!options.useShortFunctionName || options.debugVerboseMode) {
        return raw;
    }

    const QString simplified = simplifySignature(raw);
    const int parenPos = simplified.indexOf(QLatin1Char('('));
    QString head = parenPos >= 0 ? simplified.left(parenPos).trimmed() : simplified.trimmed();
    if (head.isEmpty()) {
        return QStringLiteral("<unknown>");
    }

    QString candidate = head;
    const int lastSpace = head.lastIndexOf(QLatin1Char(' '));
    if (lastSpace >= 0 && lastSpace + 1 < head.size()) {
        candidate = head.mid(lastSpace + 1).trimmed();
    }

    candidate.remove(QLatin1Char('*'));
    candidate.remove(QLatin1Char('&'));
    candidate = candidate.trimmed();
    if (candidate.isEmpty()) {
        candidate = head;
    }

    if (candidate == QStringLiteral("main") || candidate.endsWith(QStringLiteral("::main"))) {
        return QStringLiteral("main");
    }

    if (candidate.contains(QStringLiteral("<lambda")) || candidate.contains(QStringLiteral("operator"))) {
        QString lambdaBase = candidate;
        lambdaBase.replace(
            QRegularExpression(QStringLiteral("::<?lambda_[^:>]*>?::operator\\s*$")), QString());
        lambdaBase.replace(QRegularExpression(QStringLiteral("::operator\\s*$")), QString());
        lambdaBase = normalizeCtorDtor(lambdaBase.trimmed());
        if (!lambdaBase.isEmpty() && lambdaBase != candidate) {
            return lambdaBase + QStringLiteral("::lambda");
        }

        const QRegularExpression fallbackPattern(
            QStringLiteral("([A-Za-z_][A-Za-z0-9_:~]*)::<?lambda_[^:>]*>?::operator"));
        const QRegularExpressionMatch m = fallbackPattern.match(simplified);
        if (m.hasMatch()) {
            return normalizeCtorDtor(m.captured(1)) + QStringLiteral("::lambda");
        }
        return QStringLiteral("lambda");
    }

    return normalizeCtorDtor(candidate);
}

QString currentThreadTag(const LoggerOptions& options)
{
    if (!options.showThreadId) {
        return {};
    }

    // Extension point: if app code sets QThread objectName, prefer that human-readable label.
    QThread* current = QThread::currentThread();
    if (current != nullptr) {
        const QString name = current->objectName().trimmed();
        if (!name.isEmpty()) {
            return QStringLiteral("[t:%1]").arg(name);
        }
    }

#if defined(Q_OS_WIN)
    const DWORD osThreadId = GetCurrentThreadId();
    return QStringLiteral("[t:%1]").arg(QString::number(static_cast<qulonglong>(osThreadId), 16));
#else
    const quintptr threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    return QStringLiteral("[t:%1]").arg(QString::number(static_cast<qulonglong>(threadId), 16));
#endif
}

bool isDiagnosticLevel(LogLevel level)
{
    return level == LogLevel::Warn || level == LogLevel::Error || level == LogLevel::Fatal;
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
    LoggerOptions configuredOptions;

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
        currentState.options = LoggerOptions();
        currentState.projectRoot = detectProjectRoot();

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

        const QString envShowThreadId = qEnvironmentVariable("MATH_SEARCH_LOG_SHOW_THREAD_ID").trimmed();
        if (!envShowThreadId.isEmpty()) {
            currentState.options.showThreadId = parseBoolString(envShowThreadId, currentState.options.showThreadId);
        }

        const QString envCompactPath = qEnvironmentVariable("MATH_SEARCH_LOG_COMPACT_PATH").trimmed();
        if (!envCompactPath.isEmpty()) {
            currentState.options.compactPath = parseBoolString(envCompactPath, currentState.options.compactPath);
        }

        const QString envShortFunc = qEnvironmentVariable("MATH_SEARCH_LOG_SHORT_FUNC").trimmed();
        if (!envShortFunc.isEmpty()) {
            currentState.options.useShortFunctionName =
                parseBoolString(envShortFunc, currentState.options.useShortFunctionName);
        }

        const QString envDebugVerbose = qEnvironmentVariable("MATH_SEARCH_LOG_DEBUG_VERBOSE").trimmed();
        if (!envDebugVerbose.isEmpty()) {
            currentState.options.debugVerboseMode =
                parseBoolString(envDebugVerbose, currentState.options.debugVerboseMode);
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
        configuredOptions = currentState.options;
    }

    const QString summary =
        QStringLiteral(
            "logger initialized level=%1 dir=%2 console=%3 file=%4 show_thread=%5 compact_path=%6 short_func=%7 "
            "debug_verbose=%8")
            .arg(logLevelToString(configuredLevel),
                 compactPathForLog(configuredDir),
                 configuredConsole ? QStringLiteral("on") : QStringLiteral("off"),
                 configuredFile ? QStringLiteral("on") : QStringLiteral("off"),
                 configuredOptions.showThreadId ? QStringLiteral("on") : QStringLiteral("off"),
                 configuredOptions.compactPath ? QStringLiteral("on") : QStringLiteral("off"),
                 configuredOptions.useShortFunctionName ? QStringLiteral("on") : QStringLiteral("off"),
                 configuredOptions.debugVerboseMode ? QStringLiteral("on") : QStringLiteral("off"));
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
                 const char* function,
                 const char* displayFunction)
{
    initialize();

    LoggerState& currentState = state();
    LoggerOptions optionsSnapshot;
    QString projectRootSnapshot;
    {
        QMutexLocker lock(&currentState.mutex);
        if (!currentState.initialized) {
            return;
        }
        if (static_cast<int>(level) < static_cast<int>(currentState.minLevel)) {
            return;
        }
        optionsSnapshot = currentState.options;
        projectRootSnapshot = currentState.projectRoot;
    }

    const bool diagnosticsContext = isDiagnosticLevel(level);
    const QString normalizedCategory = category.trimmed().isEmpty() ? QStringLiteral("general") : category.trimmed();
    const QString compactedMessage =
        compactMessagePaths(message, optionsSnapshot, projectRootSnapshot, diagnosticsContext);
    const QString messageWithThread = [&optionsSnapshot, &compactedMessage]() {
        const QString threadTag = currentThreadTag(optionsSnapshot);
        if (threadTag.isEmpty()) {
            return compactedMessage;
        }
        return QStringLiteral("%1 %2").arg(threadTag, compactedMessage);
    }();

    const QString fileAndLine =
        QStringLiteral("%1:%2").arg(sourceFileName(file), QString::number(qMax(0, line)));
    const QString fileField = fileAndLine.leftJustified(20, QLatin1Char(' '), true);
    const QString functionField =
        shortFunctionNameInternal(function, displayFunction, optionsSnapshot).leftJustified(28, QLatin1Char(' '), true);
    const QString categoryField = normalizedCategory.leftJustified(14, QLatin1Char(' '), true);

    const QDateTime now = QDateTime::currentDateTime();
    const QString lineText =
        QStringLiteral("%1 | %2 | %3 | %4 | %5 | %6")
            .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                 logLevelToLetter(level),
                 categoryField,
                 fileField,
                 functionField,
                 messageWithThread);

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

void Logger::setOptions(const LoggerOptions& options)
{
    LoggerState& currentState = state();
    QMutexLocker lock(&currentState.mutex);
    currentState.options = options;
}

LoggerOptions Logger::options() const
{
    const LoggerState& currentState = state();
    QMutexLocker lock(&currentState.mutex);
    return currentState.options;
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

    Logger::instance().log(
        qtMsgTypeToLevel(type), qtCategory, message, context.file, context.line, context.function, nullptr);
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

QString logLevelToLetter(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return QStringLiteral("T");
    case LogLevel::Debug:
        return QStringLiteral("D");
    case LogLevel::Info:
        return QStringLiteral("I");
    case LogLevel::Warn:
        return QStringLiteral("W");
    case LogLevel::Error:
        return QStringLiteral("E");
    case LogLevel::Fatal:
        return QStringLiteral("F");
    }
    return QStringLiteral("I");
}

QString shortFunctionName(const char* rawFunction, const char* displayFunction)
{
    const LoggerState& currentState = state();
    QMutexLocker lock(&currentState.mutex);
    return shortFunctionNameInternal(rawFunction, displayFunction, currentState.options);
}

QString compactPathForLog(const QString& rawPath, bool diagnosticsContext)
{
    const LoggerState& currentState = state();
    QMutexLocker lock(&currentState.mutex);
    return compactPathInternal(rawPath, currentState.options, currentState.projectRoot, diagnosticsContext);
}

}  // namespace logging
