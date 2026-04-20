$ErrorActionPreference = "Stop"

# Align terminal encoding with logger UTF-8 output to avoid mojibake in VSCode terminal.
chcp 65001 | Out-Null
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$env:PATH = "D:\Qt\6.11.0\msvc2022_64\bin;$env:PATH"
# $env:QT_DEBUG_PLUGINS = "1"
# Optional: enable content-side probe logs on startup.
# $env:MATH_SEARCH_CONTENT_PROBE = "1"

& "D:\work\math_search_win\out\build\msvc-release\Release\math_search_win.exe"
