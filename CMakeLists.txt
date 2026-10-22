cmake_minimum_required(VERSION 3.15)

project(StructuraSystems CXX C)

include(CMakeDependentOption)

option(RUN_TESTS "Build the tests" ON)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "~/Qt/6.11.1/macos/")
endif(APPLE)
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")
endif(WIN32)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent)

# QtKeychain stores the password in the OS keychain (Credential Manager, macOS Keychain, libsecret on Linux).
# It is always built as a static library and linked into the application, so no extra DLL has to be deployed.
include(FetchContent)
set(BUILD_WITH_QT6 ON)
set(BUILD_TRANSLATIONS OFF)
set(BUILD_TEST_APPLICATION OFF)
set(QTKEYCHAIN_BUILD_SHARED_BACKUP ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    # EXCLUDE_FROM_ALL also keeps the QtKeychain install rules out of our install step.
    # SYSTEM (3.25+) silences warnings from its headers.
    FetchContent_Declare(qtkeychain
            GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
            GIT_TAG 0.15.0
            GIT_SHALLOW TRUE
            SYSTEM
            EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(qtkeychain
            GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
            GIT_TAG 0.15.0
            GIT_SHALLOW TRUE)
endif()
FetchContent_MakeAvailable(qtkeychain)
set(BUILD_SHARED_LIBS ${QTKEYCHAIN_BUILD_SHARED_BACKUP})
unset(QTKEYCHAIN_BUILD_SHARED_BACKUP)
if(NOT TARGET Qt6Keychain::Qt6Keychain)
    add_library(Qt6Keychain::Qt6Keychain ALIAS qt6keychain)
endif()

find_package(md4c REQUIRED)
find_package(sysmllib REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

add_subdirectory(./stucturasystems/)