find_package(Qt6 REQUIRED COMPONENTS Test)

set(MATH_SEARCH_TEST_SUPPORT_SOURCES
    ${PROJECT_SOURCE_DIR}/src/core/logging/logger.h
    ${PROJECT_SOURCE_DIR}/src/core/logging/logger.cpp
    ${PROJECT_SOURCE_DIR}/src/core/logging/log_categories.h
    ${PROJECT_SOURCE_DIR}/src/license/license_state.h
    ${PROJECT_SOURCE_DIR}/src/license/license_state.cpp
    ${PROJECT_SOURCE_DIR}/src/license/device_fingerprint_service.h
    ${PROJECT_SOURCE_DIR}/src/license/device_fingerprint_service.cpp
    ${PROJECT_SOURCE_DIR}/src/license/activation_code_service.h
    ${PROJECT_SOURCE_DIR}/src/license/activation_code_service.cpp
    ${PROJECT_SOURCE_DIR}/src/license/license_service.h
    ${PROJECT_SOURCE_DIR}/src/license/license_service.cpp
    ${PROJECT_SOURCE_DIR}/src/license/feature_gate.h
    ${PROJECT_SOURCE_DIR}/src/license/feature_gate.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/search_index_models.h
    ${PROJECT_SOURCE_DIR}/src/domain/models/search_index_models.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/search_result_models.h
    ${PROJECT_SOURCE_DIR}/src/domain/models/search_result_models.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/search_history_item.h
    ${PROJECT_SOURCE_DIR}/src/domain/models/app_settings.h
    ${PROJECT_SOURCE_DIR}/src/domain/models/app_settings.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/repositories/favorites_repository.h
    ${PROJECT_SOURCE_DIR}/src/domain/repositories/favorites_repository.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/repositories/history_repository.h
    ${PROJECT_SOURCE_DIR}/src/domain/repositories/history_repository.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/repositories/settings_repository.h
    ${PROJECT_SOURCE_DIR}/src/domain/repositories/settings_repository.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/services/search_service.h
    ${PROJECT_SOURCE_DIR}/src/domain/services/search_service.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/services/suggest_service.h
    ${PROJECT_SOURCE_DIR}/src/domain/services/suggest_service.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/backend_search_index_loader.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/backend_search_index_loader.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/conclusion_index_repository.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/conclusion_index_repository.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/storage/local_storage_service.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/storage/local_storage_service.cpp
    ${PROJECT_SOURCE_DIR}/src/shared/paths.h
    ${PROJECT_SOURCE_DIR}/src/shared/paths.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_perf_aggregator.h
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_perf_aggregator.cpp
)

add_library(math_search_test_support STATIC ${MATH_SEARCH_TEST_SUPPORT_SOURCES})
target_include_directories(math_search_test_support PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
)
target_link_libraries(math_search_test_support PUBLIC Qt6::Core)

if(MSVC)
    target_compile_options(math_search_test_support PRIVATE /utf-8)
endif()

function(configure_math_search_test_target target_name)
    target_compile_definitions(${target_name} PRIVATE
        MATH_SEARCH_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        MATH_SEARCH_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_include_directories(${target_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/shared
    )
    target_link_libraries(${target_name} PRIVATE
        math_search_test_support
        Qt6::Test
    )
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /utf-8)
    endif()
endfunction()

add_executable(math_search_search_tests
    search/test_search_service.cpp
    shared/test_fixture_loader.h
    shared/test_fixture_loader.cpp
    shared/test_dump_utils.h
)
configure_math_search_test_target(math_search_search_tests)
add_test(
    NAME search_service_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:math_search_search_tests>
)

add_executable(math_search_suggest_tests
    suggest/test_suggest_service.cpp
    shared/test_fixture_loader.h
    shared/test_fixture_loader.cpp
    shared/test_dump_utils.h
)
configure_math_search_test_target(math_search_suggest_tests)
add_test(
    NAME suggest_service_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:math_search_suggest_tests>
)

add_executable(math_search_index_repository_tests
    repository/test_conclusion_index_repository.cpp
    shared/test_fixture_loader.h
    shared/test_fixture_loader.cpp
)
configure_math_search_test_target(math_search_index_repository_tests)
add_test(
    NAME conclusion_index_repository_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:math_search_index_repository_tests>
)

add_executable(math_search_detail_perf_tests
    detail/test_detail_perf_aggregator.cpp
)
configure_math_search_test_target(math_search_detail_perf_tests)
add_test(
    NAME detail_perf_aggregator_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:math_search_detail_perf_tests>
)

add_executable(math_search_detail_render_chain_tests
    detail/test_detail_rendering_chain.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_render_coordinator.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_view_data_mapper.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_html_renderer.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_render_path_resolver.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_fallback_content_builder.cpp
)
configure_math_search_test_target(math_search_detail_render_chain_tests)
add_test(
    NAME detail_render_chain_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:math_search_detail_render_chain_tests>
)

add_executable(math_search_storage_tests
    storage/test_local_storage_repositories.cpp
)
configure_math_search_test_target(math_search_storage_tests)
add_test(
    NAME storage_repository_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:math_search_storage_tests>
)

add_executable(math_search_activation_code_tests
    activation/test_activation_code_service.cpp
)
configure_math_search_test_target(math_search_activation_code_tests)
add_test(
    NAME activation_code_service_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:math_search_activation_code_tests>
)

add_executable(math_search_license_service_tests
    activation/test_license_service.cpp
)
configure_math_search_test_target(math_search_license_service_tests)
add_test(
    NAME license_service_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:math_search_license_service_tests>
)

add_executable(math_search_feature_gate_tests
    activation/test_feature_gate.cpp
)
configure_math_search_test_target(math_search_feature_gate_tests)
add_test(
    NAME feature_gate_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        $<TARGET_FILE:math_search_feature_gate_tests>
)

add_executable(math_search_activation_page_tests
    activation/test_activation_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/activation_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/style/app_style.cpp
)
configure_math_search_test_target(math_search_activation_page_tests)
target_link_libraries(math_search_activation_page_tests PRIVATE Qt6::Widgets)
add_test(
    NAME activation_page_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Widgets>;$ENV{PATH}"
        $<TARGET_FILE:math_search_activation_page_tests>
)

add_executable(math_search_page_wiring_tests
    ui/test_page_wiring.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/home_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/recent_searches_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/favorites_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/widgets/recent_search_item_widget.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/widgets/favorites/favorite_item_card.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/style/app_style.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/render_section.h
    ${PROJECT_SOURCE_DIR}/src/domain/models/render_section.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/conclusion_record.h
    ${PROJECT_SOURCE_DIR}/src/domain/models/conclusion_record.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/canonical_content_loader.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/canonical_content_loader.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/conclusion_content_repository.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/conclusion_content_repository.cpp
)
configure_math_search_test_target(math_search_page_wiring_tests)
target_link_libraries(math_search_page_wiring_tests PRIVATE Qt6::Widgets)
add_test(
    NAME page_wiring_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Widgets>;$ENV{PATH}"
        $<TARGET_FILE:math_search_page_wiring_tests>
)

add_executable(math_search_main_window_round5_tests
    ui/test_main_window_round5.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/main_window.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/widgets/navigation_sidebar.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/widgets/top_bar.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/widgets/bottom_status_bar.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/home_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/search_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/favorites_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/recent_searches_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/settings_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/activation_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/widgets/recent_search_item_widget.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/widgets/favorites/favorite_item_card.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/style/app_style.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_html_renderer.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_perf_aggregator.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_pane.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_render_coordinator.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_render_path_resolver.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_fallback_content_builder.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_view_data_mapper.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/render_section.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/conclusion_record.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/adapters/conclusion_card_adapter.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/adapters/conclusion_detail_adapter.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/canonical_content_loader.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/conclusion_content_repository.cpp
)
configure_math_search_test_target(math_search_main_window_round5_tests)
target_link_libraries(math_search_main_window_round5_tests PRIVATE Qt6::Widgets Qt6::WebEngineWidgets)
add_test(
    NAME main_window_round5_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::WebEngineWidgets>;$ENV{PATH}"
        $<TARGET_FILE:math_search_main_window_round5_tests>
)

add_executable(math_search_search_page_round5_tests
    ui/test_search_page_round5.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/search_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/style/app_style.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_html_renderer.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_perf_aggregator.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_pane.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_render_coordinator.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_render_path_resolver.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_fallback_content_builder.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/detail/detail_view_data_mapper.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/render_section.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/conclusion_record.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/adapters/conclusion_card_adapter.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/adapters/conclusion_detail_adapter.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/canonical_content_loader.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/conclusion_content_repository.cpp
)
configure_math_search_test_target(math_search_search_page_round5_tests)
target_link_libraries(math_search_search_page_round5_tests PRIVATE Qt6::Widgets Qt6::WebEngineWidgets)
add_test(
    NAME search_page_round5_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::WebEngineWidgets>;$ENV{PATH}"
        $<TARGET_FILE:math_search_search_page_round5_tests>
)

add_executable(math_search_settings_activation_round5_tests
    ui/test_settings_activation_round5.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/settings_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/pages/activation_page.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/style/app_style.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/render_section.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/conclusion_record.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/canonical_content_loader.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/conclusion_content_repository.cpp
)
configure_math_search_test_target(math_search_settings_activation_round5_tests)
target_link_libraries(math_search_settings_activation_round5_tests PRIVATE Qt6::Widgets)
add_test(
    NAME settings_activation_round5_tests
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Widgets>;$ENV{PATH}"
        $<TARGET_FILE:math_search_settings_activation_round5_tests>
)
