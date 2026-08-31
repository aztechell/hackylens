cmake_minimum_required(VERSION 3.20)

add_library(hackylens_app_sdk INTERFACE)
add_library(HackyLens::AppSDK ALIAS hackylens_app_sdk)
target_include_directories(hackylens_app_sdk INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/include"
)
target_compile_features(hackylens_app_sdk INTERFACE c_std_11)

add_library(hackylens_app_host_fake STATIC
    "${CMAKE_CURRENT_LIST_DIR}/host/src/host_fake.c"
)
add_library(HackyLens::AppHostFake ALIAS hackylens_app_host_fake)
target_link_libraries(hackylens_app_host_fake PUBLIC HackyLens::AppSDK)
target_compile_features(hackylens_app_host_fake PUBLIC c_std_11)
set_target_properties(hackylens_app_host_fake PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(hackylens_app_host_fake PRIVATE /W4 /WX)
else()
    target_compile_options(hackylens_app_host_fake PRIVATE
        -Wall -Wextra -Werror
    )
endif()
