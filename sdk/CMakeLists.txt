cmake_minimum_required(VERSION 3.20)

add_library(hackylens_app_sdk INTERFACE)
add_library(HackyLens::AppSDK ALIAS hackylens_app_sdk)
target_include_directories(hackylens_app_sdk INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/include"
)
target_compile_features(hackylens_app_sdk INTERFACE c_std_11)
