find_package(Qt6 REQUIRED COMPONENTS Test)

set(MATH_SEARCH_TEST_SUPPORT_SOURCES
    ${PROJECT_SOURCE_DIR}/src/core/logging/logger.h
    ${PROJECT_SOURCE_DIR}/src/core/logging/logger.cpp
    ${PROJECT_SOURCE_DIR}/src/core/logging/log_categories.h
    ${PROJECT_SOURCE_DIR}/src/domain/models/search_index_models.h
    ${PROJECT_SOURCE_DIR}/src/domain/models/search_index_models.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/models/search_result_models.h
    ${PROJECT_SOURCE_DIR}/src/domain/models/search_result_models.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/services/search_service.h
    ${PROJECT_SOURCE_DIR}/src/domain/services/search_service.cpp
    ${PROJECT_SOURCE_DIR}/src/domain/services/suggest_service.h
    ${PROJECT_SOURCE_DIR}/src/domain/services/suggest_service.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/backend_search_index_loader.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/backend_search_index_loader.cpp
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/conclusion_index_repository.h
    ${PROJECT_SOURCE_DIR}/src/infrastructure/data/conclusion_index_repository.cpp
    ${PROJECT_SOURCE_DIR}/src/shared/paths.h
    ${PROJECT_SOURCE_DIR}/src/shared/paths.cpp
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
