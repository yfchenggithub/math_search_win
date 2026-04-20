/*
 * Unified log categories for the offline desktop app.
 * Design goal:
 * - Keep category names stable and searchable across modules.
 * - Make future promotion to broader logging coverage low cost.
 * Usage:
 * - Pass these constants into LOG_* macros, for example:
 *   LOG_INFO(LogCategory::AppStartup, QStringLiteral("Application started"));
 */

#pragma once

namespace LogCategory {

inline constexpr const char* AppStartup = "app.startup";
inline constexpr const char* AppShutdown = "app.shutdown";
inline constexpr const char* UiMainWindow = "ui.main_window";
inline constexpr const char* UiNavigation = "ui.navigation";
inline constexpr const char* DataLoader = "data.loader";
inline constexpr const char* SearchEngine = "search.engine";
inline constexpr const char* SearchIndex = "search.index";
inline constexpr const char* DetailRender = "detail.render";
inline constexpr const char* WebViewKatex = "webview.katex";
inline constexpr const char* PerfStartup = "perf.startup";
inline constexpr const char* PerfSearch = "perf.search";
inline constexpr const char* PerfDetail = "perf.detail";
inline constexpr const char* PerfWebView = "perf.webview";
inline constexpr const char* Config = "config";
inline constexpr const char* FileIo = "file.io";

}  // namespace LogCategory
