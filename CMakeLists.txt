cmake_minimum_required(VERSION 3.21)
project(math_search_win LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
include(CTest)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(math_search_win
    src/main.cpp
    src/core/logging/log_categories.h
    src/core/logging/logger.h
    src/core/logging/logger.cpp
    src/domain/models/render_section.h
    src/domain/models/render_section.cpp
    src/domain/models/conclusion_record.h
    src/domain/models/conclusion_record.cpp
    src/domain/models/search_index_models.h
    src/domain/models/search_index_models.cpp
    src/domain/models/search_result_models.h
    src/domain/models/search_result_models.cpp
    src/domain/adapters/conclusion_card_adapter.h
    src/domain/adapters/conclusion_card_adapter.cpp
    src/domain/adapters/conclusion_detail_adapter.h
    src/domain/adapters/conclusion_detail_adapter.cpp
    src/domain/services/search_service.h
    src/domain/services/search_service.cpp
    src/domain/services/suggest_service.h
    src/domain/services/suggest_service.cpp
    src/infrastructure/data/canonical_content_loader.h
    src/infrastructure/data/canonical_content_loader.cpp
    src/infrastructure/data/conclusion_content_repository.h
    src/infrastructure/data/conclusion_content_repository.cpp
    src/infrastructure/data/backend_search_index_loader.h
    src/infrastructure/data/backend_search_index_loader.cpp
    src/infrastructure/data/conclusion_index_repository.h
    src/infrastructure/data/conclusion_index_repository.cpp
    src/shared/constants.h
    src/shared/paths.h
    src/shared/paths.cpp
    src/ui/main_window.h
    src/ui/main_window.cpp
    src/ui/widgets/navigation_sidebar.h
    src/ui/widgets/navigation_sidebar.cpp
    src/ui/widgets/top_bar.h
    src/ui/widgets/top_bar.cpp
    src/ui/widgets/bottom_status_bar.h
    src/ui/widgets/bottom_status_bar.cpp
    src/ui/pages/home_page.h
    src/ui/pages/home_page.cpp
    src/ui/pages/search_page.h
    src/ui/pages/search_page.cpp
    src/ui/pages/favorites_page.h
    src/ui/pages/favorites_page.cpp
    src/ui/pages/recent_searches_page.h
    src/ui/pages/recent_searches_page.cpp
    src/ui/pages/settings_page.h
    src/ui/pages/settings_page.cpp
    src/ui/pages/activation_page.h
    src/ui/pages/activation_page.cpp
)

target_include_directories(math_search_win PRIVATE src)

if(MSVC)
    target_compile_options(math_search_win PRIVATE /utf-8)
endif()

target_link_libraries(math_search_win PRIVATE
    Qt6::Widgets
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
