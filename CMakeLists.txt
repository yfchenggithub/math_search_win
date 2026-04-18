cmake_minimum_required(VERSION 3.21)
project(math_search_win LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets SvgWidgets)

add_executable(math_search_win
    src/main.cpp
)

target_link_libraries(math_search_win PRIVATE
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::SvgWidgets
)