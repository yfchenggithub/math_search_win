# math_search_win

## 1. Project Overview

`math_search_win` is a Windows offline desktop app (Qt Widgets) for fast high-school math conclusion search.
The core flow is:

- local index search
- local content lookup
- local detail rendering in `QWebEngineView` with local KaTeX assets

This repository targets MVP release hardening: runnability, packageability, and handoff readiness.

## 2. Directory Structure

```text
math_search_win/
  src/                            # C++ source code
  app_resources/
    detail/                       # detail HTML/CSS/JS template assets
      detail_template.html
      detail.css
      detail.js
    katex/                        # local KaTeX (css/js/contrib/fonts)
      katex.min.css
      katex.min.js
      contrib/auto-render.min.js
      fonts/...
  data/                           # local business data (index/content)
    backend_search_index.json
    canonical_content_v2.json
  license/                        # local license folder
    license.dat                   # optional; missing means trial mode
  cache/                          # runtime writable cache and local state
  docs/                           # architecture and handover docs
  CMakeLists.txt
  run-debug.ps1
  run-release.ps1
```

## 3. Development Startup Flow

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
powershell .\run-debug.ps1
```

At startup the app now performs runtime layout checks for:

- `data`
- `app_resources`
- `app_resources/detail`
- `app_resources/katex`
- `license`
- `cache`

Results are written to logs and reflected in UI status text.

## 4. Release Runtime Layout Contract

Runtime paths are resolved from app root (`AppPaths`) with stable folder contracts:

- `data/` for business/index/content data
- `license/` for license file(s)
- `cache/` for writable runtime state
- `app_resources/` for read-only app static assets

If `cache/` is missing, the app tries to create it.
If `data/` or `app_resources/` is missing, startup status and page-level messages show explicit errors.

## 5. `data` Folder

- Contains search index and canonical content.
- Current files:
  - `data/backend_search_index.json`
  - `data/canonical_content_v2.json`
- Must not include web static resources, license files, or runtime user cache.

## 6. `license` Folder

- Contains local license files (`license.dat`).
- Missing `license/` or missing `license.dat` falls back to trial mode with clear status text.
- Invalid/expired/unreadable license also falls back to trial mode with diagnostics.

## 7. `cache` Folder

- Runtime writable data:
  - `favorites.json`
  - `history.json`
  - `settings.json`
  - `webengine/` (QWebEngine cache and storage path)
- Safe to recreate.
- Must not store read-only static assets.

## 8. `app_resources` Folder

- Read-only static assets.
- Current critical assets:
  - `app_resources/detail/*`
  - `app_resources/katex/*`
- Runtime user data must not be written here.

## 9. WebEngine Local Resource Notes

Detail page is loaded as local file:

- `QWebEngineView::load(QUrl::fromLocalFile(...))`
- template: `app_resources/detail/detail_template.html`
- relative subresources:
  - `./detail.css`
  - `./detail.js`
  - `../katex/...`

Hardening behavior:

- missing template/app_resources are detected at renderer init
- load/render failure triggers fallback mode
- fallback status is visible in page UI, not only in logs

## 10. KaTeX Local Resource Notes

Detail math rendering depends on local KaTeX:

- `app_resources/katex/katex.min.css`
- `app_resources/katex/katex.min.js`
- `app_resources/katex/contrib/auto-render.min.js`
- `app_resources/katex/fonts/...`

**Hard requirement: KaTeX is local-only; do not use CDN.**

## 11. Release Dependency Checklist

Release delivery must include:

- app executable (`math_search_win.exe`)
- Qt runtime DLLs for Widgets + WebEngine
- `platforms/qwindows.dll`
- `QtWebEngineProcess.exe`
- Qt WebEngine resource files
- app `app_resources/detail/...`
- app `app_resources/katex/...`
- `data/`
- `license/`
- `cache/`

Depending on Qt setup, include needed plugin folders such as:

- `styles/`
- `imageformats/`
- `tls/`
- `translations/`

**Hard requirement: deploy `QtWebEngineProcess` and related WebEngine resources.**

## 12. Recommended `windeployqt` Packaging

**Hard requirement: use `windeployqt` with the same Qt version used to build.**

Example:

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release

D:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe `
  --release `
  --compiler-runtime `
  D:\work\math_search_win\out\build\msvc-release\Release\math_search_win.exe
```

Then manually copy app-owned folders into release output:

- `app_resources/detail`
- `app_resources/katex`
- `data`
- `license`
- `cache`

## 13. Recommended Release Folder Tree

```text
MyApp/
  math_search_win.exe
  Qt6Core.dll
  Qt6Gui.dll
  Qt6Widgets.dll
  Qt6WebEngineWidgets.dll
  QtWebEngineProcess.exe
  platforms/
    qwindows.dll
  resources/                      # Qt WebEngine runtime files from windeployqt
    icudtl.dat
    qtwebengine_*.pak
  app_resources/                  # app-owned detail/katex static assets
    detail/
      detail_template.html
      detail.css
      detail.js
    katex/
      katex.min.css
      katex.min.js
      contrib/
        auto-render.min.js
      fonts/
        ...
  data/
    backend_search_index.json
    canonical_content_v2.json
  license/
    license.dat                   # optional for trial-first release
  cache/
```

## 14. Manual Verification and Troubleshooting

Minimum manual checks before shipment:

1. First launch succeeds.
2. Missing `data/` shows clear startup/page errors.
3. Missing `license/` or `license.dat` shows clear trial status.
4. Missing `app_resources/detail/` shows clear detail fallback message.
5. Missing `app_resources/katex/` shows clear formula/fallback message.
6. Search empty-input state is correct.
7. Search no-result state is correct.
8. Detail unselected state is correct.
9. Detail formulas render correctly when assets are complete.
10. WebEngine failure is visible to user (no silent white screen).
11. Moving the whole release folder still works (no dev absolute path dependency).
12. After `windeployqt`, WebEngine dependencies are complete.

Common checks:

- detail white screen: verify `app_resources/detail` and `app_resources/katex`
- formula not rendering: verify KaTeX css/js/contrib/fonts are all present
- license issues: verify `license/license.dat`, binding, and expiry fields
- favorites/history not saving: verify `cache/` exists and is writable

