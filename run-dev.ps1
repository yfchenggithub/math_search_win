$ErrorActionPreference = "Stop"

$env:PATH = "D:\Qt\6.11.0\msvc2022_64\bin;$env:PATH"
$env:QT_DEBUG_PLUGINS = "1"

& "D:\work\math_search_win\out\build\msvc-debug\Debug\math_search_win.exe"