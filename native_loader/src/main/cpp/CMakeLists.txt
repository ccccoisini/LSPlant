cmake_minimum_required(VERSION 3.28)
project(zygisk_framework)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT DEFINED ZYGISK_FRAMEWORK_GENERATED_FRAMEWORK_DIR)
    set(ZYGISK_FRAMEWORK_GENERATED_FRAMEWORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
endif()

execute_process(
    COMMAND git -C ${CMAKE_SOURCE_DIR}/../../../../third_party/LSPlant rev-parse HEAD
    OUTPUT_VARIABLE ZYGISK_FRAMEWORK_LSPLANT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND git -C ${CMAKE_SOURCE_DIR}/../../../../third_party/Dobby rev-parse HEAD
    OUTPUT_VARIABLE ZYGISK_FRAMEWORK_DOBBY_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(Plugin.ImportTableReplace OFF CACHE BOOL "" FORCE)
set(Plugin.Android.BionicLinkerUtil OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/../../../../third_party/Dobby dobby)

set(LSPLANT_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(
    ${CMAKE_SOURCE_DIR}/../../../../third_party/LSPlant/lsplant/src/main/jni
    lsplant
)

add_library(zygisk_framework SHARED
    zygisk_entry.cpp
    companion.cpp
    target_client.cpp
    fd_protocol.cpp
    preference_store.cpp
    shared_preferences_transport.cpp
    remote_preferences_manager.cpp
    remote_preferences.cpp
    lsplant_engine.cpp
    art_symbol_resolver.cpp
    dex_loader.cpp
    jni_bridge.cpp
)

target_include_directories(zygisk_framework PRIVATE
    include
    ${ZYGISK_FRAMEWORK_GENERATED_FRAMEWORK_DIR}
)
target_compile_definitions(zygisk_framework PRIVATE
    ZYGISK_FRAMEWORK_LSPLANT_COMMIT="${ZYGISK_FRAMEWORK_LSPLANT_COMMIT}"
    ZYGISK_FRAMEWORK_DOBBY_COMMIT="${ZYGISK_FRAMEWORK_DOBBY_COMMIT}"
)
target_compile_options(zygisk_framework PRIVATE
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -ffunction-sections
    -fdata-sections
)
target_link_options(zygisk_framework PRIVATE
    -Wl,--gc-sections
    -Wl,--exclude-libs,ALL
    -Wl,--strip-all
    -Wl,--hash-style=both
)
target_link_libraries(zygisk_framework
    PRIVATE
    lsplant_static
    dobby_static
    log
    dl
)

add_executable(remote_preferences_transport_test
    shared_preferences_transport_test.cpp
    shared_preferences_transport.cpp
)
target_include_directories(remote_preferences_transport_test PRIVATE include)
