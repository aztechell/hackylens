cmake_minimum_required(VERSION 3.20)
project(hackylens_app_sdk_fixture LANGUAGES C)

if(NOT DEFINED HACKYLENS_SOURCE_DIR)
    message(FATAL_ERROR "HACKYLENS_SOURCE_DIR is required")
endif()
file(TO_CMAKE_PATH "${HACKYLENS_SOURCE_DIR}" HACKYLENS_SOURCE_DIR)

include("${HACKYLENS_SOURCE_DIR}/tests/app_runtime_host.cmake")
add_subdirectory("${HACKYLENS_SOURCE_DIR}/sdk" hackylens-sdk)
add_executable(app_sdk_fixture
    minimal_app.c
    minimal_app_test.c
    ${HK_APP_RUNTIME_HOST_SOURCES}
)
target_include_directories(app_sdk_fixture PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    ${HK_APP_RUNTIME_HOST_INCLUDE_DIRS}
)
target_link_libraries(app_sdk_fixture PRIVATE HackyLens::AppSDK)
set_target_properties(app_sdk_fixture PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(MSVC)
    target_compile_options(app_sdk_fixture PRIVATE /W4 /WX)
else()
    target_compile_options(app_sdk_fixture PRIVATE -Wall -Wextra -Werror)
endif()
