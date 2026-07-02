cmake_minimum_required(VERSION 3.16)

project(cpp-mission-control-protocol-server
    VERSION 1.0.0
    DESCRIPTION "C++20 Mission Control Protocol Server"
    LANGUAGES CXX)

option(MCPS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MCPS_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

add_library(mcps_core STATIC
    src/CommandProcessor.cpp
    src/Logger.cpp
    src/MissionControlServer.cpp
    src/Protocol.cpp
    src/ServerState.cpp
    src/StringUtil.cpp
)

target_include_directories(mcps_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(mcps_core PUBLIC cxx_std_20)
target_link_libraries(mcps_core PUBLIC Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mcps_core PRIVATE -Wall -Wextra -Wpedantic)
    if(MCPS_WARNINGS_AS_ERRORS)
        target_compile_options(mcps_core PRIVATE -Werror)
    endif()
endif()

add_executable(mcps_server apps/mcps_server.cpp)
target_link_libraries(mcps_server PRIVATE mcps_core)

add_executable(mcps_client apps/mcps_client.cpp)
target_link_libraries(mcps_client PRIVATE mcps_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mcps_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(mcps_client PRIVATE -Wall -Wextra -Wpedantic)
    if(MCPS_WARNINGS_AS_ERRORS)
        target_compile_options(mcps_server PRIVATE -Werror)
        target_compile_options(mcps_client PRIVATE -Werror)
    endif()
endif()

install(TARGETS mcps_core mcps_server mcps_client
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(MCPS_BUILD_TESTS)
    enable_testing()
    add_executable(mcps_tests tests/test_main.cpp)
    target_link_libraries(mcps_tests PRIVATE mcps_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(mcps_tests PRIVATE -Wall -Wextra -Wpedantic)
        if(MCPS_WARNINGS_AS_ERRORS)
            target_compile_options(mcps_tests PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME mcps_tests COMMAND mcps_tests)
endif()
